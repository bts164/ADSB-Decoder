# prototype

A Julia reference implementation of the `adsb` demodulator, for debugging individual frames. It computes
what the C++ demodulator computes, stage by stage, and keeps every intermediate (resampled IQ, envelope,
preamble score, picked peaks, the two samples compared for each bit) so it can be plotted.

It is not a program. There is no command line and nothing to run from start to finish: load it into a Julia
REPL and call the functions one line at a time.

## Using it

Start `julia` in the `adsb` root and load everything:

```julia
include("prototype/prototype.jl")
plotlyjs()          # optional: zoomable plots instead of the default GR backend
```

Then work from [examples.jl](examples.jl). It is a set of labelled REPL sessions, one per use case, meant to
be copied or sent to the REPL a line at a time (in VS Code, Ctrl+Enter on a line). Do not `include` it: each
section stands on its own and the paths in it are placeholders for your own files.

| Section | Use case |
|---|---|
| A | Debug one frame from a debug HDF5 (`adsb --debug-h5 frames.h5 ...`) |
| B | Demodulate a SigMF or raw IQ recording and debug one frame |
| C | Look at a whole block: what the peak picker saw |
| D | Try a different filter or thresholds on the same recording |
| E | Check that this code and the C++ agree on a debug HDF5 |
| F | Export a filter bank for `adsb --filter` |

A typical session:

```julia
x, rate, meta = read_sigmf("tests/data/adsb_cf32.sigmf-data")
p  = design_filter_bank(rate)           # adsb's default filter for this rate
hk = kernel_taps(p)
P  = DemodParams(hk, rate)

frames = validate_frames(demod_stream(P, hk, x))
print_frames(frames)                    # same fields as adsb's frame log lines

fr = frames[1]
v  = frame_view(P, hk, x, fr)           # every stage recomputed around this frame
plot_frame(v; bits = payload_bits(fr.payload))
```

Every function has a docstring, so `?frame_view` at the REPL shows its arguments and what it returns.

## Files

Each file starts with a numbered list of its sections.

| File | Contents |
|---|---|
| [prototype.jl](prototype.jl) | Entry point; includes the files below |
| [polyphase.jl](polyphase.jl) | Filter bank design: the original `PolyphaseFilterBank`, and `design_filter_bank` with `adsb`'s `--filter-*` options (window, cutoff, matched pulse). Load and save of the `.bin`/`.meta` pair |
| [iq_io.jl](iq_io.jl) | Reading IQ: `read_iq` (raw files), `read_sigmf`, and the debug HDF5 (`frame_table`, `load_frame`) |
| [demod.jl](demod.jl) | The demodulator, one function per stage: `resample_at`, `preamble_scores`, `select_peaks`, `slice_bits`. `demod_block` and `demod_stream` run them all; `frame_view` recomputes everything around one preamble position |
| [decode.jl](decode.jl) | CRC-24 and frame validation, including single-bit correction. Message content (callsign, position) is not decoded |
| [plots.jl](plots.jl) | `plot_frame`, `plot_iq`, `plot_block`, `plot_filter` |
| [examples.jl](examples.jl) | The REPL sessions described above |

## Things to know

- **Indices start at 1.** Julia arrays are indexed from 1, not from 0 as in C++ or Python, and this code
  follows that: the first sample of a recording is `x[1]`, the first output of a block is `r.y[1]`, the first
  bit of a frame is `v.bits[1]`. The C++ app counts all of these from 0, so its numbers are one less:
  - `idx=N` in `adsb`'s log (`--verbosity 2`) is the frame whose `sample_index` is `N + 1`. `print_frames` prints `idx=`
    the way `adsb` does, so its output can be compared with the app's line for line.
  - The debug HDF5 stores 0-based indices. `load_frame` converts them, so everything in the Dict it returns
    is 1-based. The frame names are left as they are in the file.
  - `fixed_bit` is `nothing` when no bit was corrected, where the C++ has -1.

  Numbers that are distances or counts, not indices, start at 0 where that is natural (for example the chip
  number within the preamble). Those places are commented in the code.
- **Blocks matter.** The C++ computes the signal level and picks peaks per block of 131072 input samples.
  `demod_stream` uses the same blocks, so it finds the same frames; `demod_block` on an arbitrary range may
  not.
- **Packages.** DSP, SpecialFunctions, HDF5, JSON and Plots (PlotlyJS for `plotlyjs()`).

The `*.jl` files in the `adsb` root are the original prototype this was cleaned up from.
