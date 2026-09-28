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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

#include "common/cpu_layout.h"

namespace {
// ADSB_TASKSYS_STATS=1: per-kernel launch/sync timing, printed at exit.
struct Stats { long n=0; double span=0, wake=0, wake_n=0, tail=0, busy=0, first=0, ntasks=0, endspread=0; };
inline int64_t now_ns(){ return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
const bool g_stats = std::getenv("ADSB_TASKSYS_STATS") != nullptr;
std::mutex g_stats_mu; std::vector<std::pair<void*,Stats>> g_stats_v;
Stats& stats_for(void* f){ for(auto&p:g_stats_v) if(p.first==f) return p.second; g_stats_v.push_back({f,{}}); return g_stats_v.back().second; }
struct StatsPrinter { ~StatsPrinter(){ if(!g_stats) return; for(auto&p:g_stats_v){ auto&s=p.second; fprintf(stderr,"[tasksys] func %p: %ld launches, %.0f tasks/launch | launch->sync-return %.1f us | first-task-start %.1f us | worker start latency %.1f us (%.1f workers/launch) | busy sum %.1f us | last-end->sync-return %.1f us | end spread %.1f us\n",p.first,s.n,s.ntasks/s.n,s.span/s.n/1e3,s.first/s.n/1e3,s.wake/std::max(1.0,s.wake_n)/1e3,s.wake_n/s.n,s.busy/s.n/1e3,s.tail/s.n/1e3,s.endspread/s.n/1e3);} } } g_printer;

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
    auto func = reinterpret_cast<TaskFuncPtr>(f);
    const int task_count = count0 * count1 * count2;
    const int64_t launch_ns = g_stats ? now_ns() : 0;
    std::atomic<int64_t> first_start{INT64_MAX}, last_end{0}, busy{0}, wake_sum{0}, first_end{INT64_MAX};
    std::atomic<int> wake_n{0};

#pragma omp parallel for schedule(runtime) num_threads(std::min(task_count, team_size()))
    for (int index = 0; index < task_count; index++) {
        const int thread_index = omp_get_thread_num();
        int64_t t0 = 0;
        if (g_stats) {
            t0 = now_ns();
            int64_t v = first_start.load(); while (t0 < v && !first_start.compare_exchange_weak(v, t0)) {}
            if (thread_index != 0) { wake_sum.fetch_add(t0 - launch_ns); wake_n.fetch_add(1); }
        }
        func(data, thread_index, omp_get_num_threads(), index, task_count, index % count0,
             (index / count0) % count1, index / (count0 * count1), count0, count1, count2);
        if (g_stats) {
            int64_t t1 = now_ns();
            busy.fetch_add(t1 - t0);
            int64_t v = last_end.load(); while (t1 > v && !last_end.compare_exchange_weak(v, t1)) {}
            v = first_end.load(); while (t1 < v && !first_end.compare_exchange_weak(v, t1)) {}
        }
    }

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
