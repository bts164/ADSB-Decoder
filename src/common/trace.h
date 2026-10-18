#pragma once

/**
 * @file
 * Trace points for a profiler, on top of Tracy.
 *
 *     TRACE_ZONE("history_insert");                     // from here to the end of the scope
 *     TRACE_ZONE_VALUE("sink_block", frames.size());    // the same, with a number shown on the zone
 *     TRACE_PLOT("rtlsdr blocks dropped", dropped);     // a value over time, as a graph of its own
 *     TRACE_MESSAGE("vrt block dropped");               // a mark on the timeline; a string literal
 *     TRACE_THREAD_NAME("reader");                      // names the calling thread's row
 *
 * Built in when coro is built with tracing=tracy, which brings in Tracy and defines TRACY_ENABLE; otherwise
 * every macro is empty and its arguments aren't evaluated. A tracing build records nothing until a profiler
 * connects (coro builds Tracy with on_demand), so it can run as an ordinary build: an idle trace point costs a
 * few ns. Code that exists only to compute a traced value goes under `if constexpr (trace::kEnabled)`.
 *
 * A zone must not span a co_await: it has to close on the thread that opened it, and a coroutine may resume on
 * another worker. Close it first with a block. A task's own time needs no zone: coro shows each poll of a task
 * under the task's name (coro::build_task().name()).
 *
 * Every trace point goes through these macros (CLAUDE.md, "Tracing"). The exceptions are
 * common/tasksys.cpp and common/kernel_trace.h, which build Tracy source locations by hand for the ispc kernels.
 */

#ifdef TRACY_ENABLE

#include <cstdint>

#include <tracy/Tracy.hpp>

#define TRACE_CONCAT_INNER(a, b) a##b
#define TRACE_CONCAT(a, b) TRACE_CONCAT_INNER(a, b)
// Named for its line, so zones in one scope don't collide.
#define TRACE_ZONE_VAR TRACE_CONCAT(trace_zone_, __LINE__)

/** Opens a zone named `name` (a string literal) until the end of the enclosing scope. */
#define TRACE_ZONE(name) ZoneNamedN(TRACE_ZONE_VAR, name, true)

// The value is `...` in the two macros that take one, so an expression with a comma in it (a template's
// arguments) needs no extra parentheses.
/** TRACE_ZONE, with a value (an unsigned integer) shown on the zone. Two statements: not after an unbraced if. */
#define TRACE_ZONE_VALUE(name, ...) \
    TRACE_ZONE(name);               \
    TRACE_ZONE_VAR.Value(static_cast<uint64_t>(__VA_ARGS__))
/** Adds a point to the plot named `name` (a string literal): an int64_t, a double or a float. */
#define TRACE_PLOT(name, ...) TracyPlot(name, (__VA_ARGS__))
/** Marks the timeline with `text`, a string literal. */
#define TRACE_MESSAGE(text) TracyMessageL(text)
/** Names the calling thread's row, for a thread coro doesn't own (coro names its own). Copies `name`. */
#define TRACE_THREAD_NAME(name) tracy::SetThreadName(name)

#else

#define TRACE_ZONE(name)
#define TRACE_ZONE_VALUE(name, ...)
#define TRACE_PLOT(name, ...)
#define TRACE_MESSAGE(text)
#define TRACE_THREAD_NAME(name)

#endif

namespace trace {

/** Whether the trace points are built in. */
#ifdef TRACY_ENABLE
inline constexpr bool kEnabled = true;
#else
inline constexpr bool kEnabled = false;
#endif

}  // namespace trace
