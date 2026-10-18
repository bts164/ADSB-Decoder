// Task system for ispc's `launch`/`sync` (see dsp/kernels.ispc: resample_task).
// Implements the three-function ABI ispc requires -- ISPCAlloc/ISPCLaunch/
// ISPCSync -- documented in the ispc User's Guide, "Task Parallelism: Runtime
// Requirements", on top of OpenMP, the same approach as ispc's reference
// tasksys OpenMP mode: ISPCLaunch runs every task of the launch in an
// `omp parallel for` before returning, so ISPCSync only frees ISPCAlloc memory.
//
// Team size = compute CPU count (see cpu_layout.h); the team's threads inherit
// the launching thread's affinity mask. Tasks are handed to
// threads per OMP_SCHEDULE (libgomp default dynamic,1: each thread grabs the
// next task when it finishes one), so launching more tasks than threads load
// balances. Nothing sets OMP_WAIT_POLICY: libgomp's default spins between
// launches, which burned ~5 cores on live input, so a program that launches
// per block should start with OMP_WAIT_POLICY=passive (it is read once, before
// the first parallel region). Each non-OpenMP thread that launches gets
// its own team, and a launch from inside a task runs its tasks on a single
// thread (no nested parallelism).
//
// When the build has Tracy (TRACY_ENABLE, which comes with a coro built with tracing=tracy), each launch is
// traced, and each task is named for its kernel (common/kernel_trace.h): see the `trace` namespace below.
#include <omp.h>
#if defined(__SANITIZE_THREAD__)
#include <sanitizer/tsan_interface.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <vector>

#include "common/cpu_layout.h"
#include "common/kernel_trace.h"
#include "common/log.h"

#ifdef TRACY_ENABLE
#include <cstdio>
#include <iterator>
#include <optional>

#include <tracy/Tracy.hpp>
#endif

namespace {
// ADSB_TASKSYS_STATS=1: per-kernel launch/sync timing, logged at exit. No app launches ispc tasks now, so it is
// in no app's --help or README section; an app that does again should list it in both, and log it. It is read
// during static initialization, before logging starts, so logging it needs it read lazily first.
struct Stats { long n=0; double span=0, wake=0, wake_n=0, tail=0, busy=0, first=0, ntasks=0, endspread=0; };
inline int64_t now_ns(){ return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
const bool g_stats = std::getenv("ADSB_TASKSYS_STATS") != nullptr;
std::mutex g_stats_mu; std::vector<std::pair<void*,Stats>> g_stats_v;
Stats& stats_for(void* f){ for(auto&p:g_stats_v) if(p.first==f) return p.second; g_stats_v.push_back({f,{}}); return g_stats_v.back().second; }
struct StatsPrinter { ~StatsPrinter(){ if(!g_stats) return; for(auto&p:g_stats_v){ auto&s=p.second; LOGF(INFO,"tasksys func %p: %ld launches, %.0f tasks/launch | launch->sync-return %.1f us | first-task-start %.1f us | worker start latency %.1f us (%.1f workers/launch) | busy sum %.1f us | last-end->sync-return %.1f us | end spread %.1f us",p.first,s.n,s.ntasks/s.n,s.span/s.n/1e3,s.first/s.n/1e3,s.wake/std::max(1.0,s.wake_n)/1e3,s.wake_n/s.n,s.busy/s.n/1e3,s.tail/s.n/1e3,s.endspread/s.n/1e3);} } } g_printer;

using TaskFuncPtr = void (*)(void *data, int threadIndex, int threadCount, int taskIndex, int taskCount,
                              int taskIndex0, int taskIndex1, int taskIndex2, int taskCount0, int taskCount1,
                              int taskCount2);

struct TaskGroup {
    std::vector<void *> allocs;  // freed on sync, see ISPCAlloc
};

TaskGroup *group_of(void **handle_ptr) {
    auto *group = static_cast<TaskGroup *>(*handle_ptr);
    if (group == nullptr) {
        group = new TaskGroup();
        *handle_ptr = group;
    }
    return group;
}

int team_size() {
    static const int n = static_cast<int>(adsb::cpu_layout::compute_cpu_count());
    return n;
}


// ThreadSanitizer can't see libgomp's own synchronisation (the system libgomp isn't built with it, and its
// barriers are raw futexes), so it knows neither that a launch's tasks start after the launch nor that they
// are over when it returns. Without that it reports the launching thread's reuse of its stack after a launch
// as a race with the tasks that read from it. These tell it of the two orderings; they are nothing in any
// other build.
#if defined(__SANITIZE_THREAD__)
#define ADSB_TSAN_RELEASE(addr) __tsan_release(addr)
#define ADSB_TSAN_ACQUIRE(addr) __tsan_acquire(addr)
#define ADSB_TSAN_UNCHECKED __attribute__((no_sanitize("thread")))
#define ADSB_TSAN_CHECKED __attribute__((noinline))
#else
#define ADSB_TSAN_RELEASE(addr) ((void)(addr))
#define ADSB_TSAN_ACQUIRE(addr) ((void)(addr))
#define ADSB_TSAN_UNCHECKED
#define ADSB_TSAN_CHECKED
#endif

// Tracy zones for a launch. On the launching thread's row, `launch` spans the whole parallel region, fork to
// join. On each team thread's row, `omp task` is one task, and `omp idle` is everything between that thread's
// part of one launch and its part of the next: the wait at the join barrier, the sleep or spin between
// launches, and the wake-up. So a team thread's start-up latency is the gap from the start of `launch` to the
// end of its `omp idle`.
//
// libgomp has no hooks for any of this. `omp idle` works because a zone only has to begin and end on the same
// thread, and nothing else opens a zone on a team thread between launches: the thread opens it as the last
// thing it does in one region and closes it as the first thing it does in the next. The launching thread has
// no idle zone; it is thread 0 of its own team and goes back to its caller.
//
// Inside `omp task`, a zone named for the kernel says what the task was: the task opens it by calling
// adsb_trace_kernel() (common/kernel_trace.h), and its TaskZone closes it when the task function returns. Each
// kernel has a location of its own, so Tracy's statistics and Find Zone keep the kernels apart. A launch from
// inside a task runs its tasks on the same thread (see the top of this file), nested in the outer task's zones:
// each TaskZone keeps the one it replaces in t_kernel and puts it back.
namespace trace {
#ifdef TRACY_ENABLE

thread_local std::optional<tracy::ScopedZone> t_idle;
thread_local bool t_named = false;

constexpr tracy::SourceLocationData kLaunch{"launch", "ISPCLaunch", __FILE__, __LINE__, 0};
constexpr tracy::SourceLocationData kTask{"omp task", "run_task", __FILE__, __LINE__, 0};
constexpr tracy::SourceLocationData kIdle{"omp idle", "run_tasks", __FILE__, __LINE__, 0};

// Indexed by the IDs in common/kernel_trace.h. The location is the kernel's task in its ispc file; no line.
constexpr tracy::SourceLocationData kKernels[] = {
    {"resample (f32)", "resample_task_f32", "dsp/kernels.ispc", 0, 0},
    {"resample (i16be)", "resample_task_i16be", "dsp/kernels.ispc", 0, 0},
    {"preamble", "preamble_task", "dsp/kernels.ispc", 0, 0},
};
static_assert(std::size(kKernels) == ADSB_KERNEL_COUNT, "a kernel in common/kernel_trace.h has no name here");

// The running task's kernel zone, which adsb_trace_kernel() opens; null outside a task.
thread_local std::optional<tracy::ScopedZone> *t_kernel = nullptr;

struct LaunchZone {
    explicit LaunchZone(int task_count) : zone(&kLaunch) { zone.Value(static_cast<uint64_t>(task_count)); }
    tracy::ScopedZone zone;
};

struct TaskZone {
    TaskZone() : zone(&kTask), outer(t_kernel) { t_kernel = &kernel; }
    ~TaskZone() {
        kernel.reset();  // before `zone`, which it is inside
        t_kernel = outer;
    }
    TaskZone(const TaskZone &) = delete;
    TaskZone &operator=(const TaskZone &) = delete;

    tracy::ScopedZone zone;
    std::optional<tracy::ScopedZone> kernel;
    std::optional<tracy::ScopedZone> *outer;  // an enclosing task's kernel zone, if this one is nested in it
};

// Opens the running task's kernel zone; ignored outside a task, and if the task has already opened one.
void kernel_zone(int kernel) {
    if (t_kernel == nullptr || t_kernel->has_value() || kernel < 0 || kernel >= ADSB_KERNEL_COUNT) return;
    t_kernel->emplace(&kKernels[kernel]);
}

// First thing a thread does in a parallel region.
ADSB_TSAN_CHECKED void region_begin() {
    const int thread = omp_get_thread_num();
    if (thread == 0) return;
    t_idle.reset();
    if (!t_named) {
        char name[32];
        std::snprintf(name, sizeof(name), "omp-%d", thread);
        tracy::SetThreadName(name);
        t_named = true;
    }
}

// Last thing a thread does in a parallel region, before the barrier that ends it.
ADSB_TSAN_CHECKED void region_end() {
    if (omp_get_thread_num() != 0) t_idle.emplace(&kIdle);
}

#else

struct LaunchZone {
    explicit LaunchZone(int) {}
};
struct TaskZone {
    TaskZone() {}
};
inline void region_begin() {}
inline void region_end() {}
inline void kernel_zone(int) {}

#endif
}  // namespace trace

// One ISPCLaunch call.
struct Launch {
    TaskFuncPtr func;
    void *data;
    int count0, count1, count2;
    int task_count;
    int64_t launch_ns;  // 0 unless g_stats
};

// What the tasks of one launch record for the stats; unused unless g_stats.
struct LaunchTimes {
    std::atomic<int64_t> first_start{INT64_MAX}, last_end{0}, busy{0}, wake_sum{0}, first_end{INT64_MAX};
    std::atomic<int> wake_n{0};
};

// Kept out of run_tasks() so that it is checked by ThreadSanitizer as usual.
ADSB_TSAN_CHECKED void run_task(const Launch &launch, LaunchTimes &times, int index) {
    const int thread_index = omp_get_thread_num();
    trace::TaskZone zone;
    int64_t t0 = 0;
    if (g_stats) {
        t0 = now_ns();
        int64_t v = times.first_start.load(); while (t0 < v && !times.first_start.compare_exchange_weak(v, t0)) {}
        if (thread_index != 0) { times.wake_sum.fetch_add(t0 - launch.launch_ns); times.wake_n.fetch_add(1); }
    }
    launch.func(launch.data, thread_index, omp_get_num_threads(), index, launch.task_count, index % launch.count0,
                (index / launch.count0) % launch.count1, index / (launch.count0 * launch.count1), launch.count0,
                launch.count1, launch.count2);
    if (g_stats) {
        int64_t t1 = now_ns();
        times.busy.fetch_add(t1 - t0);
        int64_t v = times.last_end.load(); while (t1 > v && !times.last_end.compare_exchange_weak(v, t1)) {}
        v = times.first_end.load(); while (t1 < v && !times.first_end.compare_exchange_weak(v, t1)) {}
    }
}

// Runs every task of a launch and returns when all are done.
//
// Not instrumented for ThreadSanitizer, and the attribute carries over to the function the compiler makes of
// the parallel region. That function reads the region's shared variables from this one's stack before it can
// tell ThreadSanitizer anything, so those reads have to go unseen; the two tokens then order everything the
// tasks themselves do. Keep this function to the region and nothing else: whatever is in it is not checked.
ADSB_TSAN_UNCHECKED void run_tasks(const Launch &launch, LaunchTimes &times, int threads) {
    char forked = 0, joined = 0;
    ADSB_TSAN_RELEASE(&forked);
#pragma omp parallel num_threads(threads)
    {
        ADSB_TSAN_ACQUIRE(&forked);
        trace::region_begin();
// nowait: the end of the region is the one barrier, as in a combined `parallel for`.
#pragma omp for schedule(runtime) nowait
        for (int index = 0; index < launch.task_count; index++) {
            run_task(launch, times, index);
        }
        ADSB_TSAN_RELEASE(&joined);
        trace::region_end();
    }
    ADSB_TSAN_ACQUIRE(&joined);
}

}  // namespace

extern "C" {

void adsb_trace_kernel(int kernel) { trace::kernel_zone(kernel); }

void *ISPCAlloc(void **handlePtr, int64_t size, int32_t alignment) {
    TaskGroup *group = group_of(handlePtr);
    void *mem = aligned_alloc(static_cast<size_t>(alignment),
                               (static_cast<size_t>(size) + alignment - 1) / alignment * alignment);
    group->allocs.push_back(mem);
    return mem;
}

void ISPCLaunch(void **handlePtr, void *f, void *data, int count0, int count1, int count2) {
    group_of(handlePtr);
    const Launch launch{reinterpret_cast<TaskFuncPtr>(f), data, count0, count1, count2, count0 * count1 * count2,
                        g_stats ? now_ns() : 0};
    const int task_count = launch.task_count;
    const int64_t launch_ns = launch.launch_ns;
    LaunchTimes times;
    {
        trace::LaunchZone zone(task_count);
        run_tasks(launch, times, std::min(task_count, team_size()));
    }
    auto &[first_start, last_end, busy, wake_sum, first_end, wake_n] = times;

    if (g_stats) {
        const int64_t ret = now_ns();
        std::lock_guard lk(g_stats_mu);
        Stats &st = stats_for(f);
        st.n++; st.ntasks += task_count; st.span += ret - launch_ns; st.first += first_start - launch_ns;
        st.wake += wake_sum; st.wake_n += wake_n; st.busy += busy; st.tail += ret - last_end;
        st.endspread += last_end - first_end;
    }
}

void ISPCSync(void *handle) {
    if (handle == nullptr) return;
    auto *group = static_cast<TaskGroup *>(handle);
    for (void *m : group->allocs) free(m);
    delete group;
}

}  // extern "C"
