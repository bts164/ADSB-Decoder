# Julia reference implementation of the adsb demodulator, for debugging individual frames at the REPL.
#
#   julia> include("prototype/prototype.jl")
#
# loads everything below. The functions compute what the C++ demodulator computes, stage by stage, and keep
# every intermediate so it can be plotted. See examples.jl for REPL sessions to copy from.
#
#   polyphase.jl   filter bank design (PolyphaseFilterBank, design_filter_bank), load/save
#   iq_io.jl       reading IQ: raw files, SigMF, the debug HDF5 `adsb --debug-h5` writes
#   demod.jl       resample, preamble correlation, peak picking, bit slicing; frame_view
#   decode.jl      CRC and frame validation
#   plots.jl       plot_frame, plot_block, plot_filter
#
# Needs DSP, SpecialFunctions, HDF5, JSON and Plots.

include("polyphase.jl")
include("iq_io.jl")
include("demod.jl")
include("decode.jl")
include("plots.jl")
