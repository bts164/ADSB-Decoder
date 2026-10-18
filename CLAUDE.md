# Guidelines

Conventions for code and docs in this repository, for everyone who works on it (Claude Code
loads this file automatically). Coroutine code also follows coro's own `doc/guidelines.md`, whose
rules are cited by their IDs (e.g. CA.1, RAII for all resources owned by a coroutine frame).

## Layout

- An app directory (`src/apps/<app>/`) holds only code coupled to that app. Code another app or a
  test could use goes in a library under `src/`.
- Each source's command-line options are added and read in that source's own files, next to the
  code that uses them. An argparse dependency there is fine; a library links it `PRIVATE` and its
  headers forward-declare `argparse::ArgumentParser`.
- Option code is static members of the struct the options fill, not free functions:
  `X::add_options(parser)` adds them and `X::from_options(parser)` returns the filled struct
  (`FileStreamParams`, `FilterOptions`). The struct stays an aggregate, so tests and other callers
  can still build one with designated initializers.
- A subcommand's struct takes both parsers, `X::from_options(program, cmd)`, whether or not it reads
  any top-level option today, so one it needs later is already in reach. `main.cpp` checks which
  subcommand was used and calls only that one's `from_options`.
- Keep it simple: `adsb` is a showcase of coro in a real use case. Where a few lines repeated
  inline do the job (each source's `--rate`/`--freq`, say), repeat them rather than design an
  abstraction to share them.
- Options every source shares are added by the app's `main.cpp` and read into `const` locals at the
  top of `async_main`.

## Pipelines

The apps are chains of tasks joined by streams and channels; the README's
[Design](README.md#design) section explains the shape. New code follows it:

- A stage is a task that owns its state. Pass data between stages over a
  stream or channel, never through shared state or a lock.
- Every queue is bounded. A full queue holds its producer back, except where
  the producer is real-time hardware; there it drops, and logs and plots the
  drop.
- A resource that several tasks use (a database, a file) is owned by one actor
  task that takes requests over one channel. A request that needs an answer
  carries a oneshot sender.
- Split a stage's own work with `coro::parallel()` rather than adding stages.
- Only `main.cpp` handles Ctrl+C. A stage ends when its stream does or when it
  is cancelled.

## Namespaces and linkage

- Code private to one `.cpp` goes in an anonymous namespace, never `static`.
  `-Wmissing-declarations` finds the violations. `extern "C"` entry points, such as the ispc task
  system's, are the exception.
- Apps have no named namespace of their own. A library may have one (`net`, `vrt`, `adsb_sim`).
- No constants at global scope as a rule. Scope a constant to the type it belongs to as a
  `static constexpr` member (`PaceStats::kOverrunS`), to its library's namespace, or, if it is
  private to one file, to that file's anonymous namespace.

## Naming

- Functions, variables and parameters: `snake_case`. Types: `PascalCase`. Constants: `kPascalCase`.
- A class's private data members take an `m_` prefix (`m_tuning`, `m_db`), so a member reads apart
  from a local or a parameter in its methods. A plain aggregate struct, all public data like the
  options structs, uses plain names (`FileStreamParams::rate_hz`).

## Comments

- A comment explains a non-obvious why: a constraint, a hazard, or the reason for a choice that
  looks wrong. Don't narrate what the code does, and don't record history; that is what git is
  for.

## Logging

Every decision a user can't otherwise see is logged. A user won't have a debugger or a profiler,
and a silent fallback is the hardest kind of bug to find.

- **INFO, once at startup:** every setting in effect, after defaults and automatic choices are
  resolved, marking the automatic ones. A setting that can come from more than one place (option,
  environment variable, file, default) says where this one came from.
- **Environment variables:** a variable that changes behaviour is logged when it is set, valid or
  not. Prefer an option: an environment variable is invisible in the command line a user reports.
- **WARNING:** every fallback to a degraded mode, saying what was lost. A feature never turns itself
  off silently.
- **Fatal:** a setting that would make the output wrong, rather than just worse, is an error, not a
  warning. For example, a sample rate the device rejects would mislabel every sample.
- **INFO, for the run's state:** started, input ended, stopped and why, and a summary at the end.
- **`VLOGF(1)`** for detail per connection or per phase, and **`VLOGF(2)`** for detail per batch or
  per item. A disabled statement costs one comparison, so the hot path may have them.
- **Before and after logging starts:** argument-parsing errors print the usage, so they stay on
  `std::cerr`. Every error after logging has started goes through `LOGF`.
- **Where:** a library logs its own decisions, where it makes them, so every user of it gets the
  line. The exception is a library that tests build many instances of without setting up logging,
  such as `adsb_sim::Simulator`. It reports what it resolved through accessors, and its callers log
  that.

`common/log.h` has the macros: `LOGF`, `VLOGF` and `LOGF_EVERY_N_SEC`, all printf-style and checked
at compile time.

## Tracing

Trace points are for a profiler, and build in only with coro's `tracing=tracy` (`TRACY_ENABLE`).
There is no runtime switch: a tracing build records nothing until a profiler connects.

- Every trace point uses `common/trace.h`'s macros, never Tracy's directly. The exceptions are
  `tasksys.cpp` and `kernel_trace.h`, which name the ispc kernels' tasks.
- Code that only computes a traced value goes under `if constexpr (trace::kEnabled)`, not
  `#ifdef`, so it is compiled, and checked, in every build.
- **Zones:** each stage that can take more than about 10 µs per block or batch gets one.
  Name it in snake_case after its function or stage. Library code can use `Type::method`, as the
  VRT assembler does.
- A zone must not span a `co_await`: the coroutine may resume on another thread. Close it first
  with a block. A task's own time needs no zone; coro shows each poll under the task's name.
- **Plots:** each drop gets a `TRACE_PLOT` beside its log line, so a drop lines up with the
  zones around it.

## Docs

- When adding diagrams (architecture, flow, sequence, state, etc.) to docs in
  this repo, prefer Mermaid over ASCII art. Fall back to plain ASCII (or
  another format) when it's genuinely clearer for the content — e.g. file/
  directory trees, or simple box-and-arrow sketches where Mermaid's syntax
  overhead isn't worth it.
