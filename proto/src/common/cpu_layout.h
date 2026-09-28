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

}  // namespace adsb::cpu_layout
