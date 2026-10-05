#include "common/cpu_layout.h"

#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

namespace adsb::cpu_layout {
namespace {

struct Layout {
    bool enabled = false;
    cpu_set_t async_set;
    cpu_set_t compute_set;
    unsigned compute_count = 1;
};

// Parses "0-1,8" style lists from sysfs into `set`.
void add_list(cpu_set_t& set, const std::string& list) {
    size_t i = 0;
    while (i < list.size()) {
        char* end = nullptr;
        long lo = std::strtol(list.c_str() + i, &end, 10);
        long hi = lo;
        i = static_cast<size_t>(end - list.c_str());
        if (i < list.size() && list[i] == '-') {
            hi = std::strtol(list.c_str() + i + 1, &end, 10);
            i = static_cast<size_t>(end - list.c_str());
        }
        for (long c = lo; c <= hi && c < CPU_SETSIZE; c++) CPU_SET(static_cast<int>(c), &set);
        while (i < list.size() && (list[i] == ',' || list[i] == '\n')) i++;
    }
}

Layout make_layout() {
    Layout l;
    CPU_ZERO(&l.async_set);
    CPU_ZERO(&l.compute_set);
    const char* env = std::getenv("ADSB_PIN");
    if (env && std::atoi(env) == 0) return l;

    cpu_set_t allowed;
    if (sched_getaffinity(0, sizeof allowed, &allowed) != 0) return l;
    int async_cpu = -1;
    for (int c = 0; c < CPU_SETSIZE; c++)
        if (CPU_ISSET(c, &allowed)) { async_cpu = c; break; }
    if (async_cpu < 0 || CPU_COUNT(&allowed) < 2) return l;

    // The async core's SMT siblings share its execution units, so keep compute off them too.
    cpu_set_t siblings;
    CPU_ZERO(&siblings);
    std::ifstream f("/sys/devices/system/cpu/cpu" + std::to_string(async_cpu) + "/topology/thread_siblings_list");
    std::string list;
    if (f && std::getline(f, list)) add_list(siblings, list);
    CPU_SET(async_cpu, &siblings);

    l.async_set = siblings;
    for (int c = 0; c < CPU_SETSIZE; c++)
        if (CPU_ISSET(c, &allowed) && !CPU_ISSET(c, &siblings)) CPU_SET(c, &l.compute_set);
    if (CPU_COUNT(&l.compute_set) == 0) return l;  // everything is a sibling: nothing to split
    l.compute_count = static_cast<unsigned>(CPU_COUNT(&l.compute_set));
    l.enabled = true;
    return l;
}

const Layout& layout() {
    static const Layout l = make_layout();
    return l;
}

}  // namespace

bool enabled() { return layout().enabled; }

unsigned compute_cpu_count() {
    return layout().enabled ? layout().compute_count : std::max(1u, std::thread::hardware_concurrency());
}

void bind_async_thread() {
    if (layout().enabled) sched_setaffinity(0, sizeof(cpu_set_t), &layout().async_set);
}

void bind_compute_thread() {
    if (layout().enabled) sched_setaffinity(0, sizeof(cpu_set_t), &layout().compute_set);
}

void set_short_slice(bool on) {
#ifdef SYS_sched_setattr
    // The kernel's struct sched_attr (SCHED_ATTR_SIZE_VER0); glibc only declares it from 2.41.
    struct {
        uint32_t size, sched_policy;
        uint64_t sched_flags;
        int32_t sched_nice;
        uint32_t sched_priority;
        uint64_t sched_runtime, sched_deadline, sched_period;
    } attr{};
    if (sched_getscheduler(0) != SCHED_OTHER) return;  // real-time or batch/idle: leave it as launched
    attr.size = sizeof attr;
    attr.sched_policy = SCHED_OTHER;
    attr.sched_nice = getpriority(PRIO_PROCESS, 0);  // the calling thread's nice, which the call would reset
    // For SCHED_OTHER, sched_runtime is the slice (6.12+; older kernels ignore it); 0 restores the default.
    attr.sched_runtime = on ? 100'000 : 0;
    syscall(SYS_sched_setattr, 0, &attr, 0);
#else
    (void)on;
#endif
}

}  // namespace adsb::cpu_layout
