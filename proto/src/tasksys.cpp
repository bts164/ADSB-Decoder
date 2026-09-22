// Minimal task system for ispc's `launch`/`sync` (see kernels.ispc:
// resample_task). Implements the three-function ABI ispc requires --
// ISPCAlloc/ISPCLaunch/ISPCSync -- documented in the ispc User's Guide,
// "Task Parallelism: Runtime Requirements". ispc ships a reference
// implementation (examples/common/tasksys.cpp) but it isn't part of this
// snap install, so this is a small from-scratch equivalent.
//
// Fork-join per launch, not a persistent worker pool: each ISPCLaunch call
// spawns one std::thread per task directly, and ISPCSync joins them. This
// kernel set only launches tasks once per program run (resample_block, a
// handful of tasks), so thread-creation overhead is irrelevant, and
// fork-join sidesteps the lifetime hazards of a shared work queue plus a
// pool of worker threads that would otherwise need explicit shutdown
// (never-joined std::thread objects call std::terminate() on destruction).
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <vector>

namespace {

using TaskFuncPtr = void (*)(void *data, int threadIndex, int threadCount, int taskIndex, int taskCount,
                              int taskIndex0, int taskIndex1, int taskIndex2, int taskCount0, int taskCount1,
                              int taskCount2);

struct TaskGroup {
    std::vector<std::thread> threads;
    std::vector<void *> allocs;  // freed on sync, see ISPCAlloc
};

}  // namespace

extern "C" {

void *ISPCAlloc(void **handlePtr, int64_t size, int32_t alignment) {
    auto *group = static_cast<TaskGroup *>(*handlePtr);
    if (group == nullptr) {
        group = new TaskGroup();
        *handlePtr = group;
    }
    void *mem = aligned_alloc(static_cast<size_t>(alignment),
                               (static_cast<size_t>(size) + alignment - 1) / alignment * alignment);
    group->allocs.push_back(mem);
    return mem;
}

void ISPCLaunch(void **handlePtr, void *f, void *data, int count0, int count1, int count2) {
    auto *group = static_cast<TaskGroup *>(*handlePtr);
    if (group == nullptr) {
        group = new TaskGroup();
        *handlePtr = group;
    }
    auto func = reinterpret_cast<TaskFuncPtr>(f);
    int taskCount = count0 * count1 * count2;
    int threadCount = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    group->threads.reserve(group->threads.size() + static_cast<size_t>(taskCount));
    for (int i2 = 0; i2 < count2; i2++) {
        for (int i1 = 0; i1 < count1; i1++) {
            for (int i0 = 0; i0 < count0; i0++) {
                int taskIndex = i0 + count0 * (i1 + count1 * i2);
                group->threads.emplace_back(func, data, taskIndex % threadCount, threadCount, taskIndex, taskCount,
                                             i0, i1, i2, count0, count1, count2);
            }
        }
    }
}

void ISPCSync(void *handle) {
    if (handle == nullptr) return;
    auto *group = static_cast<TaskGroup *>(handle);
    for (auto &t : group->threads) t.join();
    for (void *p : group->allocs) free(p);
    delete group;
}

}  // extern "C"
