# adsb-demod

An ADS-B (1090 MHz Mode S) receiver. It reads IQ samples from a SigMF
recording, a VITA-49 (VRT) UDP stream or a live RTL-SDR. It demodulates Mode
S frames with an [ispc](https://ispc.github.io/)-vectorized, multithreaded DSP
core, validates and decodes them, and tracks aircraft state. Results go to a
WebSocket that feeds a Flutter map UI, and optionally to a SQLite history that
the UI can scrub back through and to an HDF5 file for offline analysis.

The repository holds:

- **`adsb`**, the receiver (C++, in [src/apps/adsb](src/apps/adsb)).
- **`vita49_send`**, a test source that streams IQ as VITA-49 over UDP. The IQ
  comes from an RTL-SDR or from a built-in ADS-B signal simulator. It stands
  in for a VITA-49 SDR front end for `adsb vita49`, and its simulator can drive
  the receiver at up to 200 Msps.
- **`adsb_sim_eval`**, which runs the simulator straight into the demodulator
  and scores the output against the simulator's ground truth.
- **The UI** ([ui/frontend](ui/frontend)), a Flutter app with a map, an
  aircraft table, a frame log, a spectrum waterfall and a history scrubber.
- **The Julia prototype** ([adsb_detect.jl](adsb_detect.jl),
  [polyphase.jl](polyphase.jl), [adsb.jl](adsb.jl),
  [preamble_detect.jl](preamble_detect.jl)) that the C++ code was developed
  from. The C++ resampling filter still uses the design in
  [polyphase.jl](polyphase.jl).
- **[prototype/](prototype)**, a cleaned-up Julia implementation of the
  demodulator for debugging individual frames at the REPL. It reads SigMF,
  raw IQ or a `--debug-h5` file, computes what `AdsbDemod` computes stage by
  stage and plots it. Start from [prototype/examples.jl](prototype/examples.jl).

## Building

### Prerequisites

- A C++23 compiler (GCC is the one in use), CMake and OpenMP.
- [`ispc`](https://ispc.github.io/), the vectorizing compiler for the DSP
  kernels. Put it on `PATH`, or set `ISPC_ROOT` to its install directory.
- [Conan 2](https://docs.conan.io/2/), which supplies every library
  dependency (see below).

### Conan

Conan is a package manager for C/C++. Each project declares the libraries it
needs in a `conanfile.py`, which for adsb is
[conanfile.py](conanfile.py).

- `conan install` resolves those libraries and builds any that aren't built
  yet.
- Packages are fetched from *remotes*, the default remote being ConanCenter.
- Built packages are stored in a *local cache* under `~/.conan2`.
- `conan install` also writes a CMake toolchain and CMake presets, so CMake
  can find the libraries.

The libraries adsb uses:

- `argparse`, `nlohmann_json`, `fftw`, `sqlite3`, `xtensor` and `highfive`
  come from ConanCenter automatically.
- `librtlsdr` and `coro` aren't on any public remote, so you add them to your
  local cache yourself (below).

#### One-time Conan setup

```sh
pip install conan          # or: pipx install conan
conan profile detect       # writes ~/.conan2/profiles/default for your compiler
```

adsb and coro are C++23, so edit `~/.conan2/profiles/default` and set
`compiler.cppstd=gnu23`. The dependencies are then built with the same
language standard.

#### Add librtlsdr and coro to the local cache

Run these once, and again whenever either package changes.

**librtlsdr.** Its recipe lives in this repository and downloads the upstream
osmocom source when it is built:

```sh
conan export conan-recipes/librtlsdr
```

`conan export` only copies the recipe into the cache. The library itself is
built by the `--build=missing` step in [Compile and test](#compile-and-test).

**coro.** coro is the coroutine and async-IO runtime developed alongside this
project, in its own repository. From a checkout of it:

```sh
cd <path to coro>
conan create . --version=0.1.2+dev
```

`conan create` builds coro and puts it in the cache.

- **Why the explicit version.** adsb accepts any coro version, but Conan
  ignores prerelease versions when matching a range like that. Without
  `--version`, an untagged coro commit gets a prerelease version, which adsb
  would silently skip.
- **Choosing the version.** Pick one patch above coro's latest release tag:
  `git -C <path to coro> describe --tags` shows that tag.
- **Picking up coro changes.** Re-run `conan create` with a higher version,
  e.g. `0.1.3+dev`, since Conan picks the newest version in the cache.
- **Working on both projects at once.** Register coro as an *editable* package
  instead, so adsb builds against your coro working tree directly:
  `conan editable add . --version=0.1.2+dev`. Remove it again with
  `conan editable remove .`.

coro's `doc/versioning.md` covers this in detail.

### Compile and test

```sh
conan install . --build=missing      # build missing dependencies, generate CMake presets
cmake --preset conan-release
cmake --build build/Release -j
ctest --test-dir build/Release
```

The first `conan install` builds every dependency from source that has no
prebuilt binary for your profile. That run takes a while; later runs reuse
the cache.

The executables (`adsb`, `vita49_send`, `adsb_sim_eval` and `udp_bench`) end
up in `build/Release/`.

CMake cache options:

| Option | Default | Meaning |
|---|---|---|
| `ADSB_SAMPLES_PER_SYMBOL` | 12 | Resampled rate in samples per 1 µs Mode S symbol. Compiled into the kernels. |
| `ADSB_ISPC_TARGET` | *(autodetect)* | ispc `--target` override, e.g. `avx2-i32x8`, to compare against AVX-512. |
| `ADSB_WITH_HDF5` | ON | Build `--debug-h5`, which needs HighFive and HDF5. Set by the Conan option `with_hdf5` (`-o "&:with_hdf5=False"`). |

### The UI

```sh
cd ui/frontend
flutter run -d linux     # or -d chrome
```

The UI connects to `ws://127.0.0.1:8765`, so run `adsb` with `--ws-port 8765`.

## Running

From the repository root:

```sh
# Replay a recording (the format, rate and frequency come from the .sigmf-meta)
build/Release/adsb file --iq capture.sigmf-data

# Live from an RTL-SDR, publishing to the UI and recording history
build/Release/adsb --ws-port 8765 --history-db history.sqlite rtlsdr --gain 40

# Simulated traffic over VITA-49/UDP: the receiver in one terminal...
build/Release/adsb --ws-port 8765 vita49 --port 4991 --rate 10e6
# ...the sender in another
build/Release/vita49_send --rate 10e6 --dest-port 4991 sim --scenario scenarios/boston.json

# No input: serve a recorded history database to the UI
build/Release/adsb --ws-port 8765 --history-db history.sqlite serve

# Detection statistics against the simulator's ground truth
build/Release/adsb_sim_eval --scenario scenarios/boston.json --snr-db 20 --duration 20
```

`adsb` and `vita49_send` take their options in this order:

1. global options;
2. a source subcommand (or `serve`, for `adsb`);
3. that source's options.

`<program> <subcommand> --help` lists a source's options. Ctrl+C stops either
program cleanly.

`--version` prints what went into a build, for `adsb`, `vita49_send` and
`adsb_sim_eval`, and exits. Its level is optional:

- `summary`, the default: the git version, the compiler, the build type, the
  options and the direct dependencies;
- `deps`: also the Conan profiles and every dependency and build tool, with
  its options;
- `full`: also each one's recipe revision, package ID and package revision,
  and every shared object the program loaded;
- `json`: all of it as JSON.

Every level also lists the versions of the libraries loaded at run time, UHD
and libsidekiq, in a build with them. A library that won't load is reported
as unavailable. At startup, `adsb` and `vita49_send` log the summary's first
line, and `adsb_sim_eval` prints it above its report.

## Repository layout

```
adsb/
  *.jl                     the original Julia prototype (kept for reference; superseded by prototype/)
  prototype/               Julia reference implementation of the demodulator, for debugging frames at the REPL
  conan-recipes/librtlsdr  local Conan recipe for librtlsdr
  ui/frontend/             Flutter UI (map, aircraft table, frame log, waterfall, scrubber)
  CMakeLists.txt, conanfile.py   the C++ build
  src/
    common/   constants, cpu_layout (CPU split), tasksys.cpp (ispc task runtime on OpenMP)
    dsp/      demod, peak_select, filter_bank, spectrum, iq_block (buffer, block, stitch), kernels.ispc
    decode/   frame_decode (CRC, DF, ICAO), aircraft (callsign/altitude/CPR position/velocity, Comm-B)
    net/      UDP sending helpers: datagram_batch (GSO batches + buffer pool), gso
    vrt/      the VITA-49 (VRT) wire format: format, encode (sender), assembler (receiver reorder/gap-fill)
    input/    file_stream, vita49_stream + iq_block_chunker, rtlsdr_stream + rtlsdr_device, sidekiq_stream, uhd_stream, sigmf_meta
    output/   api_server (the UI's API over WebSocket), aircraft_history (SQLite), frame_recorder (debug HDF5)
    sim/      adsb_sim (Mode S signal simulator), sim_kernels.ispc
    apps/
      filter_options.h  resampling-filter command-line options
      adsb/             the receiver: main.cpp (CLI), pipeline.{h,cpp} (demod loop, per-frame handling)
      vita49_send/      VRT/UDP sender: main.cpp (CLI), rtlsdr_source / sim_source (the two sources),
                        packet_sender (send loop), pace_stats (overrun reporting)
      adsb_sim_eval/    simulator -> demodulator detection statistics
  tests/      ctest tests, vrt_to_sigmf.py; tests/data holds captures (not in git)
  filters/    example filter coefficient files + export_filter.jl
  scenarios/  simulator scenarios (boston.json)
  doc/design/ vita49-format.md (the VRT wire format both ends implement)
  tools/      udp_bench.cpp (UDP microbenchmark), udp_sink.c (UDP receive diagnostics),
              debug_h5.jl (offline rerun of --debug-h5 frames)
```

Each `src/` directory builds as one static library. Headers sit next to their
sources and are included as `"dir/name.h"`. Module internals live in anonymous
namespaces in the `.cpp` files, and headers declare only what other
translation units use. The libraries and executables depend on each other
like this:

```mermaid
flowchart LR
    common --> dsp --> decode --> output
    dsp --> input
    common --> sim
    input --> adsb & vita49_send
    output --> adsb
    sim --> vita49_send & adsb_sim_eval
    decode --> adsb_sim_eval
```

## Design

`adsb` and `vita49_send` are both built as a pipeline: a chain of tasks on one
coro runtime, each a stage that owns its state, connected by streams and
channels. The diagrams below show each app's chain. The rules are:

- **A stage is a task that owns its state.** The sink's `AircraftTracker`
  and `stitch()`'s `BlockStitcher` belong to their tasks, and no stage
  reaches into another's. The only thing stages share is an `IqBlock`, which
  is read-only, so nothing needs a lock.
- **Stages are joined by bounded queues.** Where one stage just feeds the
  next, it is a buffered task stream (`build_task().buffer(n)`). Where a
  callback or a thread of its own produces, it is an mpsc channel. A queue's
  depth is how far its producer may run ahead.
- **A full queue holds the producer back.** The exceptions are the stages
  fed by hardware in real time, the RTL-SDR callback and the UDP socket. They
  drop instead, since waiting would lose more than it saves, and log and plot
  every drop.
- **A shared resource gets an actor.** One task owns it and handles requests
  from one channel, in order. A request that needs an answer carries a
  oneshot sender for it. The history database is one: the sink sends it
  inserts and the WebSocket clients' calls send it queries.
- **Fan-out is a broadcast, shared status a watch.** Every live subscription
  has its own receiver on one broadcast channel of frames, and reads the
  live aircraft and the input status from watch channels. `vita49_send`'s
  stages report drops on one watch channel, which the pace monitor reads.
- **Parallelism is inside a stage.** A stage that needs more than one core
  splits its own work with `coro::parallel()` (the demodulator, the sim's
  renderer). More stages would only add queues.
- **Shutdown starts at the end of the chain.** Ctrl+C is handled once, in
  `main.cpp`. Dropping the last stage's task cancels it, which drops the
  stream it pulls from, and so on back to the source; no stage watches for
  the signal itself. The detached services (the WebSocket server and the
  history actor) are cancelled and drained by the runtime as it shuts down,
  and the history actor writes what is still queued first.

## The receiver (`adsb`)

![The receiver's pipeline: sources, the demodulate task, the demod loop and its outputs, and the WebSocket server](doc/images/pipeline.svg)

How to read the diagram:

- **Boxes:** each blue box is a task. The amber ones, marked "blocking
  thread", run on coro's blocking pool (`spawn_blocking`). The white boxes
  inside the demodulate task are not tasks: each is a stage of the stream that task
  pulls, run in the task's polls, and the arrow between stages is a direct
  call, with no queue. The stages listed in the run_demod_loop task's box run in its
  polls in the same way. The two white boxes inside the ws_client task are not tasks
  either: they are its reader and its writer, run together with
  `coro::join()`, and whichever ends first, on a disconnect or a send that
  takes over 20 s, cancels the other. The grey cylinders are files.
- **Arrows:** each arrow is labelled with its channel and its depth, in
  blocks or requests where no unit is given. A dashed arrow or box is there
  only with the option it names. The tee, the spectrum task and the input status are
  for WebSocket clients, so they are there only with `--ws-port`; without it,
  `stitch()` feeds `demodulate()` directly.
- **`serve`:** the subcommand has no source and no demodulation. It runs only
  the history actor and the WebSocket server.

The pieces fit together like this:

1. A source turns its input into a `coro::CoroStream<IqBuffer>`.
2. A demodulate task on the same runtime pulls buffers from that stream,
   through `stitch()` ([iq_block.h](src/dsp/iq_block.h)) first. It fills
   each buffer's padding from its neighbors and yields it as an `IqBlock`,
   read-only and reference counted, so a stage that needs a block another
   stage also reads takes a share of it rather than a copy. With
   `--ws-port`, the next stage is `tee()` ([pipeline.cpp](src/apps/adsb/pipeline.cpp)). It passes every block
   on unchanged, and hands a share of each to every side branch whose queue
   isn't full. The one branch is the spectrum task, whose queue holds 2 blocks.
   While a client has a spectrum call open, which it reads from the server's
   settings watch, it runs 4096-point FFTs over every sample of the blocks it
   gets, and keeps each bin's peak, so each burst
   shows, however short. Every 50 ms it broadcasts the peaks, on its own clone
   of the frames' sender, to the WebSocket clients for the waterfall. While it
   is busy, blocks go to the demodulator alone, so the spectrum never holds
   the demodulator back. With `--capture-dir`, a second branch, 8 blocks
   deep, feeds the input capture task, which writes the blocks to a file while
   a `capture` call is in progress and drops them otherwise. Then
   `demodulate()` ([demod.h](src/dsp/demod.h)) demodulates each block, splitting its resampling and preamble search across the runtime's
   workers. The demodulate task can run ahead of the demod loop, through a
   buffered stream (4 blocks deep with `--debug-h5`, since each block then
   holds its IQ buffer).
3. The demod loop validates and decodes each block's frames in order, and
   keeps each aircraft's state in its `AircraftTracker`. It then hands the
   block's results to the outputs:
    - with `--ws-port`, one broadcast send of the block's frames to the
      WebSocket clients, and one update of the live aircraft in a watch,
      which keeps each aircraft heard in the last 10 minutes as last
      published. Each subscription builds its own updates from these;
    - with `--history-db`, one insert of the block's aircraft updates to the
      history actor, which owns the database and also answers the clients'
      history calls;
    - with `--capture-dir`, while an envelope capture is in progress, the
      block's envelope to the envelope capture task;
    - with `--debug-h5`, the frames and their IQ, to a recorder task, so
      writing the file doesn't hold up the loop.

[main.cpp](src/apps/adsb/main.cpp) only parses the command line, designs
the filter and picks the source. Every source then feeds the same
`run_demod_loop` call in [pipeline.h](src/apps/adsb/pipeline.h).

### Options

| Global option | Default | |
|---|---|---|
| `--filter-taps` | 32 | Taps per phase of the designed resampling filter |
| `--filter-phases` | 64 | Phases of the designed resampling filter |
| `--filter-cutoff` | 3e6 | Lowpass cutoff of the designed filter in Hz, clamped to the input and resampled Nyquist |
| `--filter-window` | hann | Window of the designed lowpass: `hann`, `hamming`, `blackman` or `kaiser` |
| `--filter-kaiser-beta` | 8 | Kaiser window beta, with `--filter-window kaiser` |
| `--filter-matched` | off | Also convolve the designed lowpass with a 0.5 µs boxcar (a filter matched to Mode S pulses) |
| `--filter`, `--filter-meta` | off | Load the filter bank from these files instead of designing it |
| `--preamble-min` | 3.0 | Preamble correlation threshold, in units of the block RMS |
| `--slice-mag-min` | 112 | Bit-slice magnitude threshold, in units of the block RMS |
| `--ws-port` | off | Serve the UI's API over WebSocket on this port (see [WebSocket](#websocket)) |
| `--ws-interval` | 1.0 | Seconds between a subscription's live updates, unless it asks for another interval |
| `--history-db` | off | SQLite file recording every aircraft update, for the UI's time scrubber |
| `--capture-dir` | off | With `--ws-port`, answer `capture` calls, which record the input IQ or the demodulator's envelope as SigMF into this existing directory (see [WebSocket](#websocket)) |
| `--debug-h5` | off | Write every decoded frame, with its IQ and everything needed to rerun the demodulator, to this HDF5 file (see [Debug HDF5](#debug-hdf5)). Not in a build without `ADSB_WITH_HDF5`. |
| `--debug-h5-failed-only` | | With `--debug-h5`, record only frames that fail CRC (not unchecked ones) |
| `--log-level` | info | Least severe log message written to stderr: `info`, `warning`, `error`, `fatal`, or `none` |
| `--log-file` | off | Also append log messages to this file |
| `--log-file-level` | info | Least severe log message written to `--log-file`, independent of `--log-level` |
| `--verbosity` | 0 | Extra log messages (see [Log](#log)): 1 adds a line for each WebSocket call, 2 for each aircraft change, 3 for every decoded frame |

| Source | Options |
|---|---|
| `file` | `--iq x.sigmf-data` (required), `--meta x.sigmf-meta` (default: the `--iq` file's sibling, if there is one), `--rate Hz`, `--freq Hz` |
| `vita49` | `--port P` (required), `--bind addr` (0.0.0.0), `--stream-id N` (default: the first one seen), `--idle-timeout s` (2; 0 = never), `--rate Hz` (required), `--freq Hz` (1090e6) |
| `rtlsdr` | `--device N` (0), `--gain dB` (negative = auto), `--buf-num N`, `--buf-len B` (0 = librtlsdr's defaults: 15 buffers of 256 KB), `--rate Hz` (2.4e6), `--freq Hz` (1090e6) |
| `sidekiq` | Only in a build with `ADSB_WITH_SIDEKIQ`. `--lib path` (libsidekiq.so, searched for on LD_LIBRARY_PATH), `--card N` (0), `--handle A1\|A2` (A1), `--port J1\|J2\|…` (libsidekiq's choice), `--gain index\|auto` (auto), `--rate Hz` (50e6), `--bandwidth Hz` (20e6), `--freq Hz` (1090e6), `--preselect-filter MHz-MHz` (libsidekiq's choice) |
| `uhd` | Only in a build with `ADSB_WITH_UHD` (conan option `with_uhd`). `--args str` (the first USRP found), `--subdev spec` (UHD's), `--channel N` (0), `--antenna name` (UHD's), `--gain dB\|auto` (auto), `--rate Hz` (8e6), `--bandwidth Hz` (UHD's), `--freq Hz` (1090e6) |

**Sample rate and center frequency.** The frequency is the tuner frequency
for `rtlsdr`, `sidekiq` and `uhd`; otherwise it only labels the spectrum frames.

- `file` reads them, and the sample format (`cf32_le` or `ci16_be`), from the
  SigMF metadata. `--rate` and `--freq` override it, with a warning when they
  differ. Without a metadata file it reads raw `cf32_le` at 2.4e6 and 1090e6,
  with a warning.
- `vita49` uses `--rate`, which must match the sender's rate: an IF Context
  packet that gives a different rate is a fatal error.
- `rtlsdr` tunes to `--rate` and `--freq`. A device that runs more than 100
  ppm from `--rate` is a fatal error, as every sample would be mislabelled;
  closer than that, the samples are labelled with `--rate`, with a warning
  when the device differs by a hertz or more. librtlsdr's rounding of its
  usual rates is far smaller.
- `sidekiq` does the same. The defaults are the X40's slowest profile, 50
  Msps with 20 MHz of bandwidth. It goes lower only with the FPGA's
  decimator, which not every bitstream has; the startup log and the error
  give the handle's range of rates.
- `uhd` does the same. `--rate` must divide the device's master clock; the
  error for one that doesn't gives the rate the device would run at.

**`serve` subcommand.** `serve` takes no input and has no options of its own.

- It serves an existing `--history-db` to `--ws-port` clients until Ctrl+C,
  answering the UI's history calls (see [WebSocket](#websocket)).
- It requires both of those global options, and the database file must
  already exist.
- No frames or live aircraft are published, so the UI's live view shows the
  database's latest snapshot.

### Sources

Every source yields `IqBuffer`s ([iq_block.h](src/dsp/iq_block.h)) of
128k samples, which `stitch()` turns into the `IqBlock`s the rest of the
pipeline reads. `rtlsdr` yields one buffer per librtlsdr buffer, which is the
same size by default.

- Each block is allocated with `demod_padding()` zeroed samples on either
  side of the real ones.
- Each block records its stream sample index (`start`).
- Each block holds its samples in the format its source receives them in
  ([iq_format.h](src/dsp/iq_format.h)), and the resampler reads that format
  directly. No source spends a pass over its samples converting them:
  `rtlsdr` yields float blocks, `file` blocks in the file's format, and
  `vita49` the packets' big-endian int16 unchanged.
- A block dropped because the demodulator fell behind still counts toward
  `start`, so frame indices stay correct across drops.
- The lead padding is `(taps-1)/2` samples, which is how far the tap window
  reaches back.
- The trailing padding covers the tap window plus one full frame after the
  block's last sample. It grows with the sample rate: 317 samples at
  2.4 Msps and 1515 at 12 Msps (32 taps).

The three sources:

- **`file`** ([file_stream.h](src/input/file_stream.h)) replays a SigMF
  recording (`cf32_le` or `ci16_be`) or a raw `cf32_le` file. It reads a few
  blocks ahead of the consumer on the coro runtime, so file IO overlaps
  demodulation.
- **`vita49`** ([vita49_stream.h](src/input/vita49_stream.h)) receives
  the stream `vita49_send` emits. That stream has two kinds of packet: VRT IF
  Data packets carry big-endian int16 IQ, and IF Context packets carry the
  sample rate and RF frequency. The format is described in
  [doc/design/vita49-format.md](doc/design/vita49-format.md).
  - **Receiving.** A receive task on the runtime keeps draining the socket
    while the demod thread is busy. It reads with UDP GRO where available, so
    a run of same-size datagrams arrives in one syscall.
  - **Assembly.** `vrt::Assembler`
    ([vrt/assembler.h](src/vrt/assembler.h)) puts the packets
    back in order:
    - it re-sorts packets by timestamp in a 256-packet window;
    - it zero-fills gaps where packets never arrived;
    - it treats a timestamp jump of more than 1 s as a sender restart.
  - **Blocks.** The Assembler copies each payload, unconverted, straight
    into an `IqBlockChunker`
    ([iq_block_chunker.h](src/input/iq_block_chunker.h)), which builds
    the padded blocks in place with no further copy.
  - **Handoff.** Finished blocks go to the demod thread through a 128-block
    (64 MiB) channel. If that channel fills, blocks are dropped, so the socket keeps
    being read.
  - **Rate check.** Every 2 s while packets arrive, the source checks that
    the input keeps up with real time:
    - A slow sender leaves no gaps in the stream (its timestamps count
      samples), so the check compares stream time against the wall clock.
    - It counts samples lost in transit: sequence gaps, plus the socket's
      kernel drop counter from `/proc/net/udp{,6}`.
    - It counts blocks dropped because the demodulator fell behind.
    - If less than 98% of the real-time signal was demodulated, or anything
      was lost or dropped, it logs a warning saying where the shortfall
      went.
    - It logs `back to full rate` on recovery, and a summary of the whole
      run at exit.
    - With `--ws-port`, each check is also sent to live subscriptions as an
      `InputStatus` (see [WebSocket](#websocket)).
  - **Socket buffer.** `adsb` doesn't set `SO_RCVBUF`, so the socket gets
    the system default receive buffer. At tens of Msps and up, raise that
    default before starting `adsb` (see [High sample rates](#high-sample-rates)).
- **`rtlsdr`** ([rtlsdr_stream.h](src/input/rtlsdr_stream.h)) reads a
  live dongle.
  - librtlsdr's own thread calls back with each uint8 buffer.
  - The callback converts the buffer to a float block and hands it over
    through a 4-block channel.
  - The callback never blocks. If the channel is full the block is dropped,
    because stalling the USB thread loses far more.
  - If the dongle is unplugged, the source reopens it: at once, then after
    1, 2, 4 and every 8 seconds until it is back. Stream time skips the
    outage, so aircraft times stay on the wall clock.
  - The device handling (open, tune, reconnect, stop when dropped) is in
    [rtlsdr_device.h](src/input/rtlsdr_device.h), which `vita49_send`
    also uses.

### Demodulation

Before a block is demodulated, `stitch()`'s `BlockStitcher` fills its padding
with the neighboring blocks' samples:

1. It holds each buffer until the next one arrives.
2. It copies the head of the new buffer into the held buffer's trailing
   padding, and the held buffer's tail into the new buffer's leading padding.
3. It then yields the held buffer, as an `IqBlock`, to be demodulated.

This costs one block of latency (about 55 ms at 2.4 Msps) and copies a few
hundred samples per block. Each block scans only the preamble positions
centered on its own samples. It reads into the trailing padding for the tap
window and the rest of the frame, so a frame that straddles a boundary
decodes as if the stream were one array.

`AdsbDemod::exec` ([demod.cpp](src/dsp/demod.cpp)) runs stages 1–4.
`compute_frame_view` and `update_aircraft_from_me` run the decode stages.

```mermaid
flowchart TD
    IQ["IQ block (cf32, guard-padded)"] --> R["1. Polyphase resample to 12 samples/µs → |z|<br/>+ block sum of squares (RMS)"]
    R --> P["2. Preamble correlation at every position"]
    P --> S["3. Greedy peak selection<br/>(threshold × RMS, exclusion radius)"]
    S --> B["4. PPM bit slicing, 112 bits<br/>(magnitude threshold × RMS)"]
    B --> C["5. CRC-24 / DF / ICAO validation<br/>+ single-bit correction (DF17/18)"]
    C --> A["6. Aircraft state from DF17/18 ME field"]
```

1. **Resample and envelope** (`resample_body` in
   [kernels.ispc](src/dsp/kernels.ispc), compiled once per sample format as
   `resample_block_f32` and `resample_block_i16be`).
   - The block's format picks the kernel. Each converts its samples to float
     as it reads them and scales its outputs, so everything after this stage
     sees the same values whatever the format.
   - A polyphase FIR bank (`Np` phases × `taps`; the default is 64 × 32)
     resamples the complex input to `ADSB_SAMPLES_PER_SYMBOL` (12) samples
     per µs. Each output is the magnitude `|z|`.
   - Output `m` sits at input position `m·ry/rx + 1/2 + 1/(2Np)`.
     - The position is computed in closed form in 36-bit fixed point.
     - Its integer part gives the center sample, and its fractional part
       gives the filter phase.
     - Outputs don't depend on each other, and no divide is needed.
     - With an even tap count the window is centered half a sample later, so
       the `1/2` is dropped and outputs land at the same input times.
   - 32 taps fill each filter row exactly (64 floats); 33 would pad each
     row to 80.
   - The SIMD gang runs across taps for one output at a time.
     - Every tap is stored twice (`h0,h0,h1,h1,…`), so each load of
       coefficients lines up with the interleaved I/Q input without shuffles.
     - Each filter row is zero-padded to a multiple of 16.
   - Partial sums are combined with a transpose-and-sum across the batch of
     outputs.
   - The same pass also sums the squares of the block's own outputs, which
     gives the block RMS. It is taken after the filter, so the thresholds
     are relative to the noise in the band the filter passes. The raw input's
     RMS would include the noise the filter removes, which with a 20 MHz
     front end at 50 Msps is about 8 dB more.
   - The work is split into contiguous ranges, one per ispc task. The output
     count is rounded up to a multiple of 64, so every batch is full and the
     kernel has no bounds checks.
2. **Preamble correlation** (`preamble_scan`).
   - Every output position `k` gets the score `Σ y[k + j·6]·pat[j]`, summed
     over the 16 half-µs chips of the fixed Mode S preamble
     `[1,-1,1,-1,-1,-1,-1,1,-1,1,-1,-1,-1,-1,-1,-1]`.
   - There is no sub-chip phase search. Peak selection handles alignment.
3. **Peak selection** (`select_preamble_peaks` in
   [peak_select.cpp](src/dsp/peak_select.cpp)).
   - The candidates are the positions scoring at least
     `--preamble-min × RMS`.
   - Repeat until none are left: accept the highest remaining score, then
     discard everything within `(8 + 112) µs` of it on either side.
   - This rejects the correlation's own sidelobes and matches inside the
     message pulses, so the slicer starts on the true peak.
   - The last pick's exclusion zone carries over into the next block, if
     that block is contiguous.
   - **A rare miss at block ends.** The selection is greedy per block, so a
     pick near the end of a block stays final even if a stronger peak
     follows just past the boundary.
     - A sidelobe picked there fails CRC, and its exclusion zone then
       suppresses the true peak, so the frame is lost.
     - This happens to about 1e-4 of strong frames.
     - See the note on `demodulate()` in [demod.h](src/dsp/demod.h) for a
       fix.
4. **Bit slicing** (`slice_scan`).
   - For each accepted candidate, the slicer reads 112 bits starting 8 µs
     after the preamble. Bit `i` is `y[a] > y[a+6]`, which compares the early
     chip against the late chip.
   - `Σ|y[a] − y[a+6]|` is the frame magnitude. Frames below
     `--slice-mag-min × RMS` are dropped. `confidence` is this magnitude
     divided by the RMS.
   - The thresholds are scaled by the RMS instead of normalizing `y`. This
     works because everything downstream is linear in the input scale.
5. **Validation** ([frame_decode.cpp](src/decode/frame_decode.cpp)).
   - The DF comes from the first 5 bits. For short formats (DF 0/4/5/11) the
     CRC-24 runs on the leading 56 bits.
   - **DF17/18**: the frame is valid when the remainder is 0.
     - Otherwise the remainder is looked up in a table of single-bit
       syndromes, which leaves out the 5 DF bits.
     - On a match, that bit is flipped back (`crc=fixed`).
   - **DF11**: the frame is valid when the remainder is < 63, since the low
     bits carry the interrogator ID.
   - An ICAO address from a valid DF11/17/18 is added to a set of known
     addresses.
   - **DF 0/4/5/16/20/21/24**: the address/parity field is the ICAO address
     XOR the CRC.
     - The frame is accepted when the remainder matches an address that has
       already been seen.
     - DF24 covers any DF from 24 to 31, since only its first two bits are
       the format.
   - **DF19/22** (military) have no general parity rule, so they are
     reported as `crc=unchecked`.
   - Every other DF is unassigned, which means the frame is corrupt, so it
     fails.
6. **Aircraft decode** ([aircraft.cpp](src/decode/aircraft.cpp)).
   - Valid DF17/18 frames update per-ICAO state:
     - callsign and emitter category (TC 1–4);
     - barometric altitude and position (TC 9–18), using global CPR decode
       from an even/odd frame pair;
     - ground speed and track, or magnetic heading and IAS/TAS, plus vertical
       rate (TC 19 subtypes 1–4);
     - squawk (TC 28 subtype 1, emergency status);
     - selected altitude and heading, baro setting and autopilot modes (TC 29
       version 2);
     - ADS-B version, NACp and SIL (TC 31, operational status);
     - on-ground or airborne, from the position type (TC 5–8 surface, 9–18
       and 20–22 airborne);
     - alert/SPI, from the surveillance status.
   - Valid or address-matched replies of other DFs add:
     - altitude (DF0/4/16/20);
     - squawk (DF5/21);
     - flight status: on ground, alert and SPI (DF4/5/20/21);
     - vertical status (DF0/16);
     - capability (DF11).
   - **Comm-B.** The Comm-B field of DF20/21 is decoded when it holds one of
     these registers:
     - BDS 2,0 (callsign);
     - BDS 4,0 (selected altitude, baro setting);
     - BDS 5,0 (roll, TAS);
     - BDS 6,0 (heading, IAS, Mach).

     The reply doesn't say which register it holds, so the register is
     inferred by pyModeS's rules. A field that fits both 5,0 and 6,0 goes to
     whichever agrees with the aircraft's ADS-B velocity. Any other
     ambiguous field is skipped.
   - **Not decoded:**
     - surface and GNSS positions;
     - other Comm-B registers;
     - ACAS advisories;
     - Gillham or metric altitudes.
   - **What is published.**
     - Every frame is published with a one-line `summary` of what it says.
     - Every valid or address-matched frame, of any DF, counts toward the
       aircraft's message count and signal level, and updates its last-seen
       time.
     - The aircraft record is emitted when a decoded field changes, and
       otherwise at most once a second.

### Resampling filter

By default `adsb` designs the polyphase resampling filter at startup, once
the input rate is known. The design is the same as `PolyphaseFilterBank` in
[polyphase.jl](polyphase.jl):

- a windowed sinc lowpass (Hann by default) of `taps × phases` coefficients,
  at `phases` times the input rate;
- scaled to unity DC gain;
- phase p takes every `phases`-th coefficient, starting from coefficient p.

The cutoff is `--filter-cutoff` (3 MHz), clamped to two limits: the input
Nyquist, and the resampled Nyquist (6 MHz at 12 Msps out).

- Above 6 Msps in, the filter therefore also band-limits the input. It acts
  as the anti-alias and noise filter.
- At 2.4 Msps the cutoff clamps to 1.2 MHz. With `--filter-taps 33` that is
  exactly `filters/filter.bin`.

Lower cutoffs pay off at high input rates. The table gives clean-frame CRC OK
rates from `adsb_sim_eval` by cutoff, on the boston scenario for 20 s with 33
taps. 32 taps gives the same rates, or within one frame.

| Input rate, SNR | 1.5 MHz | 2 MHz | 3 MHz | 4 MHz | Nyquist |
|---|---|---|---|---|---|
| 12 Msps, 8 dB | 27.5% | 25.2% | 18.2% | 11.9% | 1.0% |
| 12 Msps, 12 dB | 51.6% | 49.2% | 42.6% | 38.5% | 28.1% |
| 24 Msps, 8 dB | 34.6% | 34.6% | 29.9% | 27.3% | 19.1% |
| 24 Msps, 12 dB | 58.4% | 58.6% | 55.0% | 50.7% | 44.0% |

The window barely matters, and a pulse-matched filter doesn't help. The
table compares clean-frame CRC OK rates from `adsb_sim_eval` on the boston
scenario, 30 s × 3 seeds, with 32 taps:

| Design | 2.4 Msps, 18 dB | 2.4 Msps, 22 dB | 12 Msps, 8 dB | 12 Msps, 14 dB |
|---|---|---|---|---|
| Hann (default) | 52.8% | 78.4% | 18.4% | 57.3% |
| Hamming | 52.9% | 78.3% | 18.3% | 57.4% |
| Blackman | 53.0% | 78.4% | 18.6% | 57.4% |
| Kaiser β 4 / 8 / 12 | 52.8–53.1% | 78.4–78.6% | 18.2–18.7% | 57.2–57.6% |
| Hann, `--filter-matched` | 47.7% | 73.2% | 22.9% | 63.2% |
| Hann, cutoff 1.5 MHz | 52.8% | 78.4% | 27.4% | 66.6% |
| Hann, cutoff 1.0 MHz | 52.0% | 78.1% | 13.9% | 52.5% |

- **Window:** sidelobe level doesn't matter at the demodulator's SNRs, and
  the transition width changes the response less than the cutoff does.
- **Matched filter:**
  - At 12 Msps, `--filter-matched` beats the default only because it narrows
    the band. A plain 1.5 MHz cutoff does better.
  - At 2.4 Msps it loses about 5 points, almost all in preamble detection.
    The wider pulses leak into the neighbouring chips, which the preamble
    correlation counts negatively.
  - On a real 3.2 Msps gqrx capture it decodes 78 DF11/17/18 frames, against
    90 for every window.

The cutoff is what to tune, and only at high input rates.

`--filter` and `--filter-meta` load a bank from files instead:

- The `.bin` file holds raw float32 coefficients in Julia column-major order
  (taps × Np).
- The sidecar holds `key=value` lines: `taps`, `Np`, `planes`, `dtype`.
- Only plane 0 is used.

[export_filter.jl](filters/export_filter.jl) writes these files from
`PolyphaseFilterBank`:

```sh
cd adsb
julia filters/export_filter.jl [M=33] [Np=64] [out_prefix]
```

`filters/filter.bin` (33 × 64) and `filters/filter101.bin` (101 × 65) are
example files. Both use the input-Nyquist cutoff.

Either way, the taps are then prepared for the kernel:

1. They are reversed, so the kernel walks coefficients and samples in the
   same direction.
2. Each tap is duplicated for interleaved I/Q.
3. Each row is padded.

### Threading

- **One runtime.** A work-stealing coro runtime with one worker per hardware
  thread runs everything: the sources, the WebSocket clients and the demod
  loop. No thread is pinned to a CPU.
- **Demodulator.** `AdsbDemod::exec()` runs the resampler and the preamble
  correlation as one `coro::parallel()` call, one part per worker.
  - Each part computes its share of the resampled envelope
    (`resample_chunk_*` in [kernels.ispc](src/dsp/kernels.ispc)), waits at
    the call's barrier for the others, then computes its share of the
    preamble scores (`preamble_chunk`).
  - The parts start on workers that are already running or briefly parked,
    so a block costs no thread launch; peak picking, bit slicing and frame
    assembly run in the demodulate task itself.
  - `ADSB_NTASKS` overrides the part count.
- **OpenMP.** [kernels.ispc](src/dsp/kernels.ispc) also has `launch`
  versions of the two kernels (`resample_block_*`, `preamble_scan`), which
  nothing calls now. [tasksys.cpp](src/common/tasksys.cpp) implements ispc's
  `launch`/`sync` ABI on OpenMP for them.
- **Simulator.** `vita49_send sim` runs on one runtime too, with the
  renderer as a task of its own (see [`sim` source](#sim-source)).
  - `Simulator::generate()` runs the decimation filter and the noise stage
    as one `coro::parallel()` call of `--render-threads` parts
    (`sim_finish_range` in [sim_kernels.ispc](src/sim/sim_kernels.ispc)).
    It fills a list of spans, so the renderer writes the samples straight
    into the VRT packets' payloads.
- **Scheduler slice.** On Linux 6.12+ `vita49_send`'s runtime workers ask
  for a 100 µs scheduler slice.
  - With it, a thread woken by a packet preempts whatever desktop thread
    holds its core, instead of waiting out that thread's ~3 ms slice. The
    sim's rendering shares those workers, so a send that comes due preempts
    it too.
  - Without it, both `adsb` and `vita49_send` lost about 0.5% of a 50 Msps
    stream. `adsb` doesn't currently ask for it.

### Outputs

#### Log

`adsb` prints nothing to stdout. Its messages are log lines, written to stderr
and, with `--log-file`, appended to a file. Each destination has its own
level (`--log-level`, `--log-file-level`).

The log is for watching the receiver, not for reading its results: those are
in the WebSocket messages, the history database and the
[debug HDF5](#debug-hdf5). At `info` it tells the story of the run:

- the version and the settings in effect, at startup;
- each WebSocket client connecting and disconnecting, and each streaming call,
  such as the UI's subscription and spectrum, starting and ending;
- the spectrum starting and stopping, as clients open and close it;
- each aircraft heard, and lost once it has gone 10 minutes unheard:

  ```
  aircraft a1b2c3: heard
  aircraft a1b2c3 UAL123: lost, not heard for 10 min (523 messages)
  ```

- problems with the input, and its recovery.

`--verbosity` adds more, also logged at `info`. At 1, every WebSocket call,
including each history query. At 2 or higher, also one line each time an
aircraft's state changes, with only the fields that are known (and the flags
that are set):

```
aircraft icao=... callsign=... cat=... squawk=... alt=...ft lat=... lon=... gs=...kt trk=... vr=...fpm hdg=... ias=...kt tas=...kt mach=... roll=... sel_alt=...ft ground alert spi t=...s
```

With `--verbosity 3`, also one line per frame:

```
frame idx=<sample> bits=112 confidence=<c> df=<n> icao=<hex|??????> crc=<ok|fixed|fail|unchecked> payload=<32 hex>
```

`unchecked` marks military DF19/22, which have no general parity rule.

#### WebSocket

`--ws-port` serves the UI's API: calls, like gRPC's, over one WebSocket per
client. [api.proto](proto/adsb/api/v1/api.proto) defines the messages, and
[ui_api.md](doc/design/ui_api.md) describes the protocol and its design. In
short:

- Each WebSocket message is one binary protobuf: a `ClientMessage` from the
  client, or a `ServerMessage` from `adsb`.
- A client starts a call by sending its request with a new `id`, larger than
  every earlier one on the connection. Each reply carries the call's `id`.
- Every call ends with exactly one message with `end` set: a unary call's
  reply, a streaming call's last message, or an `Error`. A client stops a
  call early by sending a `cancel` with its `id`. The call then ends with a
  `CANCELLED` error, unless its `end` was already on its way.
- A call can also be one the client streams. Its later requests carry the
  call's `id`, and the client's last message has `end` set. The protocol
  supports these, though none of today's calls is one.
- A call that fails ends with an `Error`, whose code says why:
    - `NO_HISTORY`, a history call to an `adsb` without `--history-db`;
    - `NO_LIVE_INPUT`, a spectrum call to `adsb serve`;
    - `NO_CAPTURE`, a capture call to an `adsb` without `--capture-dir`;
    - `INVALID_ARGUMENT`, a bad request, such as an `id` that isn't new, an
      empty heatmap area, or a client stream's message for another kind of
      call;
    - `RESOURCE_EXHAUSTED`, a ninth call while a connection already has 8 in
      progress, a capture of a point already being captured, or a client
      stream that sent more than 16 messages ahead of its call;
    - `UNIMPLEMENTED`, a call this `adsb` doesn't know;
    - `INTERNAL`, a failure `adsb` has logged.
- A client that takes over 20 s to accept a message is disconnected.

The UI's [ApiClient](ui/frontend/lib/api_client.dart) makes each call a
method, and the UI makes them in this order: `server_info`, `subscribe`, then
with history a `snapshot` to backfill the sky. It makes a `spectrum` call
while its Spectrum pane is shown, and a `capture` call from its capture
button.

**`server_info`** says what this `adsb` serves: whether it answers the
history calls (`--history-db`), whether it answers the spectrum call (any
subcommand but `serve`), whether it answers the capture call
(`--capture-dir`) and for how long at most, and its `--ws-interval`.

**`subscribe`** streams the live data. It sends an `Update` every
`interval_s` (default `--ws-interval`, at least 0.1 s). An update holds:

- the frames published since the last update, in order;
- each aircraft that changed since the last update, as it was last published;
  the first update holds every aircraft heard in the last 10 minutes;
- the input status, if it changed.

An interval with none of these sends nothing.

**`spectrum`** streams the spectrum rows for the waterfall, one `SpectrumRow`
per reply. `adsb` computes them only while at least one spectrum call is open.

**`capture`** records one point of the pipeline to `<name>.sigmf-data` and
`<name>.sigmf-meta` in `--capture-dir`, for up to 60 s of stream time, and
replies when it is done:

- The point is `INPUT`, the IQ as the source delivered it, which `adsb file`
  replays, or `ENVELOPE`, the demodulator's resampled magnitude as `rf32_le`
  at 12 Msps.
- It never overwrites a file, and takes one capture per point at a time.
- Cancelling the call stops the capture, keeping what it has written.
- The reply has the data file's path, its datatype and rate, the stream index
  of its first sample, and the samples written and missing.
- Samples the writer falls behind on are dropped, not waited for, so the
  demodulator never slows. Each gap starts a new SigMF capture segment, at
  the `core:global_index` where the samples resume, and is logged.

A `Frame` is sent for every frame. Besides the frame itself it has:

- `crc_ok`;
- `crc_checked`, false for DF19/22;
- `crc_fixed_bit`, the bit that single-bit correction flipped, unset if none;
- `summary`, a one-line description of what the frame says, for example
  `Velocity · 223 kt GS · trk 235° · -1408 fpm`. It is empty when the CRC
  failed.

The UI uses these fields for its CRC statistics:

- It reports the CRC pass rate of the self-checking formats (DF11/17/18)
  separately from the address-match rate of DF0/4/5/16/20/21/24.
- The address-match rate also fails correct frames from aircraft not yet
  confirmed on DF11/17/18.
- It counts corrected frames separately.

An `Aircraft` carries the aircraft's whole state, each field unset until it
is known. It is published when a decoded field changes, and otherwise at most
once a second while frames from the aircraft keep arriving. Besides the ADS-B
fields it has:

- `category`, the emitter category (for example `A3`, large);
- `squawk`, from DF5/21 replies and ES emergency status;
- `altitude_ft`, from ES airborne position and DF0/4/16/20 replies;
- `heading_deg` (magnetic), `ias_kt` and `tas_kt`, from airspeed velocity
  messages (TC19 subtypes 3-4) and Comm-B BDS 5,0 and 6,0;
- `mach` (BDS 6,0) and `roll_deg` (BDS 5,0, positive right wing down);
- `on_ground`, from DF4/5/20/21 flight status, DF0/16 vertical status, DF11
  capability and the ES position type; unset until one of these arrives;
- `alert` (squawk changed, or an emergency) and `spi` (ident), from flight
  status and ES surveillance status;
- `selected_altitude_ft`, `selected_altitude_source` (`MCP` or `FMS`),
  `selected_heading_deg`, `baro_setting_hpa` and `autopilot_modes`, from ES
  target state (TC29, version 2):
    - `autopilot_modes` lists the engaged modes, drawn from `AP`, `VNAV`,
      `ALT`, `APP` and `LNAV`. It is unset until target state arrives, so that
      unknown differs from none engaged;
    - selected altitude and baro setting also come from BDS 4,0;
- `adsb_version`, `nac_p` (position accuracy, 0-11) and `sil` (integrity,
  0-3), from ES operational status (TC31); `nac_p` and `sil` need version 1
  or later;
- `messages`, the count of frames of any DF from that address;
- `signal_db`, a moving average of the per-bit pulse amplitude relative to
  its block's RMS. This is a relative level, not absolute power;
- `last_seen_s`, in stream time;
- `last_seen_unix_s`, in wall-clock time: the stream's start plus
  `last_seen_s`. In file playback it runs ahead of, or behind, the real
  clock.

A `SpectrumRow` is the peak of each bin of 4096-point FFTs of the raw IQ
since the last row, in dB.

An `InputStatus` comes from the `vita49` source, every 2 s while packets
arrive. It covers the last `window_s` of wall time:

- In that window the source delivered `stream_s` seconds of signal at
  `rate_hz`.
- `lost_s` of that was lost in transit.
- `dropped_s` of it was dropped because the demodulator fell behind.
- `kernel_drops` counts the times the kernel dropped on the socket because the
  receive buffer was full.
    - Each drop is one datagram, or with GRO a whole coalesced batch, so the
      amount lost is given by `lost_s`.
    - This field is unset where `/proc/net/udp` isn't readable.
- `ok` is false when less than 98% of real time was demodulated, or anything
  was lost.
- `summary` says the same in words.

The UI shows a red warning in the top bar when `ok` is false. The warning
gives the percentage demodulated and has `summary` as its tooltip. The UI
hides the warning once it is 6 s old.

The history calls answer from `--history-db`, and fail with `NO_HISTORY`
without it. Each is unary. Times are Unix seconds.

**`snapshot`** gives the sky at time `t`, default the newest update:

- `aircraft` holds every aircraft updated in the 10 minutes up to `t`, each
  with its latest `state` and its `track`: its positions in the window, up to
  1000, oldest first.
- `t_min` and `t_max` span the whole database, and are unset when it's empty.
- The UI asks for one on connect, to backfill the live sky, and one for each
  seek of its scrubber.

**`aircraft_history`** gives one aircraft's recent history:

- It takes the aircraft's `icao` and a `t`, default the aircraft's latest
  update.
- Its `points` are that aircraft's updates in the 30 minutes up to `t`,
  oldest first, each field unset where not yet known.
- The UI uses it for the selected aircraft's detail charts.

**`activity`** counts aircraft over time:

- It takes a `from`, default the whole database.
- `counts` holds the number of distinct aircraft updated in each `bucket_s`
  (60 s) bucket.
    - Buckets are aligned to the Unix epoch.
    - The buckets are consecutive, starting with the bucket that begins at
      `t0`. A bucket with no aircraft holds 0.
    - They run from the bucket holding `from` to the latest one.
    - The last bucket may still be filling.
- `t0` is unset when there are no buckets.
- The UI fetches the whole database after connecting, which takes about 15 ms
  per hour of history. It then re-fetches from the last bucket every 30 s.
  This drives the scrubber's histogram.

**`heatmap`** gives traffic density over an area:

- It takes the area (`west`, `south`, `east` and `north`, in degrees), a
  grid size (`width` and `height`), and `t0` and `t1`, default the whole
  database. An empty area (east not past west, or north not past south) is
  refused.
- It divides the area into a `width`×`height` grid. Each dimension is clamped
  to 1..512.
    - The grid is even in Web Mercator, so it lines up with the map.
    - Row 0 is at the north.
- The reply lists the nonzero cells: `cell_index[i]` is a cell's index,
  `row*width+col`, and `cell_count[i]` the number of distinct flights that
  crossed it between `t0` and `t1`.
- `max` is the largest count.
- How flights are counted:
    - A flight is one aircraft's positions with no gap of 30 minutes or more.
    - Positions under 60 s apart are joined by a line, so a flight counts in
      every cell it passed through, not just where it was sampled.
- The grid is computed on request. That takes about 40 ms for an hour of
  history, or 0.2 s for 5 h over a 400×300 grid.
- The UI's map heatmap asks for one grid cell per 4×4 pixels of the current
  view. It asks again after the view or the shown time changes, and every
  30 s while live.

#### SQLite

`--history-db` ([aircraft_history.h](src/output/aircraft_history.h))
stores the history in one table:

- The table is `aircraft_updates(t, icao, lat, lon, altitude_ft,
  ground_speed_kt, vertical_rate_fpm, signal_db, selected_altitude_ft,
  state)`, in WAL mode.
- It is indexed on `(t, icao)` and `(icao, t)`.
- `t` is `last_seen_unix_s`.
- Every published aircraft update is written live, with `state` holding the
  published `Aircraft`, serialized, and the other columns copied out of it
  for the queries.
- The file records its schema's version (`PRAGMA user_version`). `adsb`
  refuses a file of another version, older files included, rather than
  misread it: record to a new file.
- Queries run over a separate read-only connection.

#### Debug HDF5

`--debug-h5 <file>` ([frame_recorder.h](src/output/frame_recorder.h))
records each frame `demodulate()` yields, whether or not it passes CRC. A
build with `ADSB_WITH_HDF5` off, such as a cross build, has no `--debug-h5`.

- With each frame it stores the IQ the frame came from and everything needed
  to rerun the demodulator on it.
- The file is flushed about once a second, so it stays readable after Ctrl+C.

[tools/debug_h5.jl](tools/debug_h5.jl) reruns every recorded frame from
its IQ. It checks the result against the stored envelope, bits, preamble score
and confidence. It also works as a library for stepping through a single
frame:

```sh
adsb --debug-h5 frames.h5 --debug-h5-failed-only file --iq capture.sigmf-data
julia tools/debug_h5.jl frames.h5   # needs HDF5.jl
```

Positions are 0-based:

- *Output* indices count resampled envelope samples from the start of the
  frame's block.
- *Input* indices count IQ samples from the block's first real sample.

| Path | Contents |
|---|---|
| `/` attributes | `format_version`, `command_line`, `sample_rate_hz`, `output_rate_hz`, `samples_per_symbol`, `num_bits`, `preamble_pattern`, `preamble_spacing`, `bit_offset`, `exclusion_radius`, `max_lookahead`, `preamble_score_min`, `slice_magnitude_min`, and the resampler schedule `pos_frac_bits`, `pos_step`, `pos_offset`, `index_offset`, `window_lead` |
| `/filter` | float32 `{num_phases, taps}` in C order, so Julia reads it as `h[tap, phase]`. Taps are in the order they multiply increasing sample indices. Attributes: `taps`, `num_phases`, `cutoff_hz` (0 when loaded), `description` (for example `hann, cutoff 1.2 MHz` or `loaded <path>`) |
| `/frames/<sample_index>` attributes | `sample_index` (stream index of the preamble, as printed), `block_start`, `block_n`, `output_index` (preamble position), `envelope_start`, `iq_start`, `level` (block RMS), `preamble_score` and `confidence` (both divided by `level`), `df`, `icao`, `icao_known`, `crc` (`ok`, `fixed`, `fail` or `unchecked`), `crc_ok`, `fixed_bit`, `num_bits`, `payload_raw` and `payload` (28 hex digits, before and after CRC correction) |
| `/frames/<sample_index>/iq` | complex float32 (`r`, `i` compound), input indices `iq_start ...`. These are exactly the samples the stored envelope reads. |
| `/frames/<sample_index>/envelope` | float32 envelope at output indices `envelope_start ...`, running from 16 us before the preamble to 8 us past the frame, clipped to the block |
| `/frames/<sample_index>/bits` | uint8, the 112 sliced bits (raw, before correction) |

To recompute the demodulator's steps:

- **Envelope at output `m`:**
  1. Let `P = m*pos_step + pos_offset`.
  2. Let `base = P >> pos_frac_bits`.
  3. Let `phase = ((P & (2^pos_frac_bits - 1)) * num_phases) >> pos_frac_bits`.
  4. Then `y[m] = |sum_j h[phase, j] * x[base - window_lead + j]|`, where
     `x[i]` is `iq[i - iq_start]`.
- **Preamble score:**
  `sum_k preamble_pattern[k] * y[output_index + k*preamble_spacing] / level`.
- **Bit `i`:** let `s = output_index + bit_offset + i*samples_per_symbol`.
  The bit is 1 when `y[s] - y[s + samples_per_symbol/2] > 0`.
- **Confidence:** the sum of `|y[s] - y[s + samples_per_symbol/2]|` over all
  bits, divided by `level`.

`level` is the RMS of the frame's whole block, so it can't be recomputed
from the stored IQ alone. It is stored as an attribute instead.

### Limitations

- **Per-block RMS.** The thresholds scale with each block's own RMS, with no
  smoothing across blocks.
- **Gaps break stitching.** Blocks are joined only when their sample ranges
  meet.
  - After a dropped block (the compute thread fell behind), the padding stays
    zero, so a frame across the gap is lost.
  - Stitching looks just one block ahead. A block shorter than the trailing
    padding, such as the last one at the end of a stream, leaves the rest of
    the padding zero.
- **Detection is sensitive to amplitude.** At 20 dB SNR, `adsb_sim_eval` on
  `boston.json` decodes:

  | Aircraft amplitude | Decoded |
  |---|---|
  | 1.0 and 0.7 | about 100% |
  | 0.5 | about 93% |
  | 0.35 | about 29% |
  | 0.25 | about 0% |

  Nearly all of the misses have no preamble candidate at all.
- **Exit with a WebSocket client connected.** `adsb` can segfault at exit
  while a WebSocket client is still connected.

## The UI

[ui/frontend](ui/frontend) is one page: the map above the aircraft table, with
a scrubber bar under both.

- **Frames and Spectrum.** The **Frames** and **Spectrum** chips in the top
  bar show the frame log and spectrum waterfall in a dock that slides in on
  the right.
  - When both are shown, they are stacked.
  - Drag the dock's left edge to resize it.
- **Map.** Markers and trails are coloured by altitude, with a legend at the
  bottom left.
- **Aircraft details.** Selecting an aircraft, on the map or in the table,
  opens a panel of charts covering the last 30 minutes: altitude, ground
  speed, vertical rate and signal.
  - With a history db, these charts start full.
  - Without one, they fill in from live updates.

With a history db, the scrubber shows the table and map (with trails) as of
any past time. It narrows down in three levels:

- **Whole recording.** A thin strip on top spans the whole recording.
  - Its ticks are full height at local midnight.
  - It is shaded as a histogram of how many aircraft were seen per minute,
    with each pixel showing its busiest minute, so busy periods and gaps stand
    out.
  - A box marks the range the strip below covers. Clicking or dragging jumps
    to that time and brings the range along.
- **Range.** A second strip, drawn the same way, covers only that range.
  - The range is 6 h to 7 days, 1 day by default. Pick it from the upper menu
    on the left.
  - A box marks the slider's window. Clicking or dragging jumps to that time
    within the range.
- **Slider.** The slider below the strips covers only a window of the range.
  - The window is 10 min to 12 h, 30 min by default. Pick it from the lower
    menu on the left.
  - ◀ and ▶ move by one window.
  - The time label opens a date and time picker.

**Live**, or releasing the slider at the right end of the recording, returns
to live data. The frame log and spectrum always show live data.

## The test source (`vita49_send`)

`vita49_send` streams IQ as VRT IF Data packets over UDP, the way a VITA-49
SDR front end would. It also sends periodic IF Context packets, which
advertise the sample rate, RF frequency and bandwidth.

The wire format is described in
[doc/design/vita49-format.md](doc/design/vita49-format.md). In
short:

- no Class ID and no Trailer;
- UTC + picosecond timestamps;
- big-endian int16 IQ.

Usage:

```sh
vita49_send [--rate Hz] [--freq Hz] [--dest-host H] --dest-port P [--stream-id N]
            [--samples-per-packet N] [--context-interval s] {rtlsdr|sim} [source options]
```

The defaults are:

| Option | Default |
|---|---|
| `--rate` | 2.4e6 |
| `--freq` | 1090e6 |
| `--dest-host` | 127.0.0.1 |
| `--stream-id` | 1 |
| `--samples-per-packet` | 360 |
| `--context-interval` | 1 s |

It also takes `adsb`'s log options (`--log-level`, `--log-file`,
`--log-file-level`, `--verbosity`).

- **Packet size.** The default of 360 samples per packet keeps each datagram
  (1460 B) within one Ethernet MTU.
- **Timestamps.** Timestamps are anchored to the wall clock once, at the
  first packet. After that they advance by sample count, so they have no
  jitter.

```mermaid
flowchart LR
    subgraph source["source: one per subcommand"]
        direction TB
        RTL["<b>rtlsdr</b><br/>blocking thread<br/>librtlsdr callback,<br/>encodes VRT packets"]
        GEN["<b>sim</b><br/>sim_generate task<br/>renders into packet payloads"]
    end
    PAR["coro::parallel() parts<br/>--render-threads"]
    subgraph udp_send["udp_send task"]
        direction TB
        PACE["paced_sim_stream()<br/>holds each batch until due"] --> SEND["send_loop()<br/>one send per batch"]
    end
    MON["pace_monitor task<br/>drop warnings, once a second"]
    NET(["UDP to adsb vita49"])

    GEN <-- "fork / join" --> PAR
    GEN -- "mpsc, --queue-ms ahead" --> PACE
    RTL -- "mpsc, 1024 packets, drops when full" --> SEND
    SEND --> NET
    SEND -. "PaceStats, watch" .-> MON
    RTL & GEN & PACE -.-> MON
```

Each box is a task. The `rtlsdr` callback runs on coro's blocking pool
(`spawn_blocking`). `paced_sim_stream()` runs in the send task's polls, and
is there only with `sim`.

- **Packets.** Both sources interleave the IF Context packets with the data.
  The sim's batches are GSO batches of up to 44 packets. The `rtlsdr` source
  sends one packet per batch.
- **Drops.** Every place that drops samples records it on the pace
  statistics (a watch channel), and so does the send loop for what it sent.
  The pace monitor turns the changes into one warning a second.

The code in [src/apps/vita49_send](src/apps/vita49_send) is split
by concern:

- `rtlsdr_source` and `sim_source` are the two sources, each with its own
  subcommand options.
- `packet_sender` sends whichever source's stream it is given, and
  `pace_stats` reports what a source dropped on overrun.
- `main.cpp` holds the shared options and builds the chosen source's stream.

The packets and their timestamps come from
[src/vrt/encode.h](src/vrt/encode.h), and they go out in the GSO batches of
[src/net/datagram_batch.h](src/net/datagram_batch.h).

### `rtlsdr` source

The `rtlsdr` source takes `--device`, `--gain`, `--buf-num` and `--buf-len`,
as for `adsb rtlsdr`.

- The librtlsdr callback encodes each buffer into packets.
- It hands the packets to a send task through a 1024-slot channel, which
  absorbs a whole callback's burst.
- The callback never blocks. If the channel is full, it drops packets and
  warns.
- The VRT packet counter still advances for a dropped packet, so a receiver
  sees the gap.
- If the dongle is unplugged, it reconnects as `adsb rtlsdr` does. The
  timestamps skip the outage. A receiving `adsb vita49` stops after
  `--idle-timeout` seconds without packets (2 by default), so raise that, or
  set it to 0, for the receiver to ride out a reconnect.

### `sim` source

The `sim` source renders simulated ADS-B traffic in-process
([adsb_sim.h](src/sim/adsb_sim.h)) from a scenario file. Its options:

| Option | Meaning |
|---|---|
| `--scenario` | Scenario file, e.g. `scenarios/boston.json` (required) |
| `--snr-db` | SNR, default 30 |
| `--duration` | Seconds to run; 0 (the default) runs until Ctrl+C |
| `--seed` | Random seed |
| `--pulse-rise-ns`, `--pulse-fall-ns`, `--pulse-jitter-ns` | Pulse shape |
| `--rx-cutoff`, `--oversample` | Receiver filter |
| `--render-threads` | Parallel parts each render step is split into, on the runtime's workers, default 4 |
| `--queue-ms` | How far ahead of real time to render, default 20 |

The simulator models this traffic:

- **Messages.** It encodes DF17 identification, airborne position and
  velocity messages, plus DF11 squitters.
- **Aircraft.** The aircraft move at constant velocity.
- **Signal.** The rendering goes like this:
  1. Pulses are on-off keyed, with finite edges and jitter.
  2. They are rendered oversampled when the rate is too low to resolve the
     edges.
  3. The signal is filtered and decimated.
  4. AWGN is added.
- **Determinism.** The output is a pure function of the configuration and
  the sample index. The same seed gives the same IQ however the stream is
  chunked or paced.

The sim source paces itself against the wall clock like a capture device. It
works in two stages, both runtime tasks, so rendering and sending run in
parallel:

1. **Render.** A generator task renders batches of up to 44 packets.
   - It queues them up to `--queue-ms` (default 20) of signal ahead of real
     time.
   - The queue costs 4 B per sample, which is 16 MB for 20 ms at 200 Msps.
2. **Send.** The send task sends each batch with a single UDP GSO send, when
   the batch's last sample is due.
   - This keeps bursts short enough for a receiver's socket buffer.
   - GSO cuts the kernel's per-datagram cost about 3x compared with one send
     per packet.
   - After a stall, the send task drains the queue at send-only speed, like a
     capture device emptying its FIFO.

If either stage runs more than 100 ms late, it behaves like a capture device
that overruns:

- It drops the late samples. When rendering is what fell behind, the
  simulator skips the dropped span instead of rendering it, so it can catch
  up. Either way the IQ at every later timestamp is unchanged.
- The receiver sees a timestamp gap.
- It logs a `fell behind real time; dropped N samples` warning once a
  second while this happens.
- At exit it logs the rate achieved and the total dropped.

### High sample rates

At 50–200 Msps over loopback:

- **Give each program its own cores.** Pin `vita49_send` to one physical core
  and `adsb` to the rest.
  - For example, on a 6-core/12-thread CPU, run
    `taskset -c 0,6 vita49_send ...` with `taskset -c 1-5,7-11 adsb ...`.
  - If the sender shares a core with the demodulator's threads, it falls
    behind.
- **Raise the receive buffer.** When the sender is descheduled for a few ms,
  it catches up by sending its backlog back to back.
  - The default 212 KB receive buffer (about 1 ms at 50 Msps) can't absorb
    that burst.
  - Before starting `adsb`, run
    `sudo sysctl -w net.core.rmem_max=33554432 net.core.rmem_default=33554432`.
  - The change takes effect for new sockets immediately.

## Detection statistics (`adsb_sim_eval`)

```sh
adsb_sim_eval --scenario scenarios/boston.json [--snr-db 30] [--duration 60] [--rate 2.4e6]
              [--seed] [--preamble-min] [--slice-mag-min] [--block 131072] [--tolerance-us 3]
```

`adsb_sim_eval` feeds the simulator straight into `demodulate()`, block by
block, as `adsb` does, and `--block` sets the block size.

It then matches the output against the simulator's ground truth.

- It reports these rates:
  - preamble detected;
  - frame output;
  - raw CRC OK;
  - CRC fixed;
  - CRC OK;
  - exact match.
- It breaks the rates down by category:
  - all;
  - block-cut;
  - overlapping;
  - clean;
  - per DF;
  - per aircraft.
- It also breaks down the misses, the false candidates and the candidate
  timing error.

Compare its tables before and after a demodulator change.

## Tools

- **[tests/vrt_to_sigmf.py](tests/vrt_to_sigmf.py)** records a VRT
  stream to a SigMF file pair.
- **[tools/udp_bench.cpp](tools/udp_bench.cpp)**, built as
  `udp_bench`, is a loopback microbenchmark for `coro::UdpSocket`.
  - A paced sender and a receiver run in one process, optionally with GSO
    and GRO.
  - It reports loss, reordering, and CPU time per datagram delivered.
- **[tools/udp_sink.c](tools/udp_sink.c)** is a standalone receive-only
  UDP sink.
  - It prints once a second: packet rate, VRT counter gaps, timestamp faults,
    the longest gap between packets, and kernel drops.
  - Use it to tell a slow sender from a slow receiver.
  - It isn't built by CMake. Build it with
    `gcc -O2 tools/udp_sink.c -o udp_sink`.
- **[tools/debug_h5.jl](tools/debug_h5.jl)** reruns `--debug-h5`
  frames offline (see [Debug HDF5](#debug-hdf5)).

## Testing

- **`ctest`** runs five tests:
  - `adsb_sim_test`: simulator encoders, CRC, modulation round trip,
    scenario parsing and determinism;
  - `vita49_stream_test`: VRT reordering, gap filling and assembly;
  - `iq_block_chunker_test`: building padded blocks from appended samples;
  - `peak_select_test`;
  - `block_stitcher_test`: padding across contiguous, short and
    non-contiguous blocks, in each sample format, and a block's buffer going
    back to its pool only once its last share is dropped.
- **Reference capture.** `tests/data/adsb_cf32.sigmf-data` is a 12 Msps
  capture, not in git. It should decode 24 frames with
  `adsb --filter-taps 33 --filter-cutoff 6e6 file --iq tests/data/adsb_cf32.sigmf-data`.
- **Detection statistics.** Run
  `adsb_sim_eval --scenario scenarios/boston.json --snr-db 20 --duration 20`,
  and compare the tables before and after a change.
  - Adding `--block 1024` puts hundreds of frames across block boundaries,
    which exercises the stitching.

## Environment variables

For profiling and tuning. `adsb --help` lists them too, and each is logged
when it is first read, with its value.

| Variable | Effect |
|---|---|
| `ADSB_TIMING=1` | Logs per-stage timings for each block, plus a throughput and realtime-factor line every second; `0` turns it off |
| `ADSB_NTASKS=N` | Overrides the demodulator's part count (`exec()`) |

## Profiling

The apps carry [Tracy](https://github.com/wolfpld/tracy) trace points, built in only when coro is
built with its `tracing=tracy` option:

```sh
conan install . -o 'coro/*:tracing=tracy' --build=missing
cmake --preset conan-release
cmake --build build/Release -j
```

The option can go under `[options]` in a Conan profile instead. Without it, the trace points
compile to nothing. With it, nothing is recorded until a profiler connects, so a tracing build
runs like any other.

To record, run the Tracy profiler of the same version as the client (0.13.1, the version coro
pins) and connect it to the running app. The timeline shows:

- every poll of each coro task, under the task's name;
- the demodulator's stages, the sink's handling of each block, and the SQLite, HDF5 and WebSocket
  work;
- the simulator's rendering and the VRT receive path;
- plots of every drop and of the UDP send time.

The trace points are listed in the source: search for `TRACE_`. `src/common/trace.h` has the
macros, and [CLAUDE.md](CLAUDE.md#tracing) the rules for adding one.
