#pragma once

/**
 * @file
 * Splits the machine's CPUs between async IO and compute work.
 *
 * One physical "async" core (with its SMT siblings) runs the coro runtime thread: socket/file IO and
 * packet handling. The remaining "compute" CPUs run the demod thread and the OpenMP team it creates for
 * ispc tasks, which inherits its mask. Compute threads are only restricted to a mask, not pinned
 * one-to-one, so the OS can still balance them around other load. `ADSB_PIN=0` disables the split.
 */

namespace adsb::cpu_layout {

/**
 * Whether the split is active: not disabled by `ADSB_PIN=0`, and the machine has CPUs left over for
 * compute once the async core's siblings are taken out.
 */
bool enabled();

/**
 * Number of CPUs threads doing ispc work may use (>= 1): the compute set if enabled(), otherwise all
 * hardware threads.
 */
unsigned compute_cpu_count();

/**
 * Restricts the calling thread, and threads it goes on to create (uv loop, libuv fs workers), to the
 * async core's SMT siblings. No-op unless enabled().
 */
void bind_async_thread();

/**
 * Restricts the calling thread, and threads it goes on to create (the OpenMP team), to the compute CPUs. No-op
 * unless enabled().
 */
void bind_compute_thread();

/**
 * Gives the calling thread, and threads it goes on to create, a 100 µs scheduler slice (`on`), or the kernel's
 * default back (`!on`). Linux 6.12+ (EEVDF) only; elsewhere, or on older kernels, a no-op.
 *
 * A thread that wakes normally waits for the running thread's slice to end (~3 ms by default). A desktop
 * thread (compositor, shell) on the async core held it that long about once a second, and the socket
 * receive buffer (~1 ms of a 50 Msps stream) overflowed. With a short slice a waking thread preempts at
 * once. It needs no privileges, unlike a real-time policy or negative nice. Use it for threads that
 * sleep until IO arrives; long-running compute threads should keep the default.
 */
void set_short_slice(bool on);

}  // namespace adsb::cpu_layout
