// Task system for ispc's `launch`/`sync` (see dsp/kernels.ispc: resample_task).
// Implements the three-function ABI ispc requires -- ISPCAlloc/ISPCLaunch/
// ISPCSync -- documented in the ispc User's Guide, "Task Parallelism: Runtime
// Requirements", on top of OpenMP, the same approach as ispc's reference
// tasksys OpenMP mode: ISPCLaunch runs every task of the launch in an
// `omp parallel for` before returning, so ISPCSync only frees ISPCAlloc memory.
//
// Team size = compute CPU count (see cpu_layout.h); the team's threads inherit
// the launching (demod) thread's compute affinity mask. Tasks are handed to
// threads per OMP_SCHEDULE (libgomp default dynamic,1: each thread grabs the
// next task when it finishes one), so launching more tasks than threads load
// balances. adsb defaults OMP_WAIT_POLICY to passive (see main.cpp: libgomp's
// default spin between launches burned ~5 cores on live input); override with
// OMP_WAIT_POLICY / GOMP_SPINCOUNT. Each non-OpenMP thread that launches gets
// its own team, and a launch from inside a task runs its tasks on a single
// thread (no nested parallelism).
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
#include "common/log.h"

namespace {
// ADSB_TASKSYS_STATS=1: per-kernel launch/sync timing, logged at exit.
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
// nowait: the end of the region is the one barrier, as in a combined `parallel for`.
#pragma omp for schedule(runtime) nowait
        for (int index = 0; index < launch.task_count; index++) {
            run_task(launch, times, index);
        }
        ADSB_TSAN_RELEASE(&joined);
    }
    ADSB_TSAN_ACQUIRE(&joined);
}

}  // namespace

extern "C" {

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
    run_tasks(launch, times, std::min(task_count, team_size()));
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
