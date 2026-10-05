# REPL sessions to copy from. Not meant to be run top to bottom: each section stands on its own after
#
#   include("prototype/prototype.jl")
#   plotlyjs()                       # optional: zoomable plots
#
# Sections:
#   A. Debug one frame from a debug HDF5 (`adsb --debug-h5 frames.h5 ...`)
#   B. Demodulate a SigMF or raw IQ recording and debug one frame
#   C. Look at a whole block: what the peak picker saw
#   D. Try a different filter or thresholds on the same recording
#   E. Check that this code and the C++ agree on a debug HDF5
#   F. Export a filter bank for `adsb --filter`

# ----------------------------------------------------------------------------------------------------------
# A. Debug one frame from a debug HDF5
# ----------------------------------------------------------------------------------------------------------
# The file holds the IQ around every frame the run recorded (add --debug-h5-failed-only to `adsb` to keep
# only CRC failures), the filter and the demod parameters, so nothing else is needed.

f = h5open("frames.h5")
frame_table(f)                                  # name, DF, ICAO, CRC status, confidence of every frame
fr = load_frame(f, frame_names(f)[1])           # or load_frame(f, "000000002031805")
v = frame_view(f, fr)                           # every stage recomputed from the stored IQ
r = validate_frame(v.payload)                   # DF, CRC remainder, corrected bit

plot_frame(v; bits = payload_bits(r.payload), title = "$(fr["name"]) crc=$(r.crc)")
plot_iq(v)

v.bits == fr["bits"]                            # same bits as the C++ sliced?
findall(abs.(v.d) ./ v.level .< 0.5)            # the weakest bit decisions (bit 1 is the first received)
r.fixed_bit, fr["fixed_bit"]                    # the bit the CRC corrected here and in the C++ (or nothing)

# How would it slice from a neighbouring position? (e.g. suspecting the peak picker took a sidelobe)
v2 = frame_view(f, fr; k = fr["output_index"] + 1)
validate_frame(v2.payload).crc

# ----------------------------------------------------------------------------------------------------------
# B. Demodulate a recording and debug one frame
# ----------------------------------------------------------------------------------------------------------

x, rate, meta = read_sigmf("tests/data/adsb_cf32.sigmf-data")
# or raw IQ, saying what it is:
#   x = read_iq("capture.bin"; datatype = "cu8"); rate = 2.4e6
# or part of a big file (in samples):
#   x = read_iq("capture.cf32"; start = 10_000_000, count = 4_000_000)

p = design_filter_bank(rate)                    # adsb's default filter for this rate
hk = kernel_taps(p)
P = DemodParams(hk, rate)                       # default thresholds: preamble_min = 3, slice_mag_min = 112

frames = validate_frames(demod_stream(P, hk, x))
print_frames(frames)                            # same fields as adsb's frame log lines; idx= counts from 0 as adsb does

fr = frames[1]
# or the frame adsb logged as idx=2031805 (fr.sample_index indexes x, so it is one more):
#   fr = frames[findfirst(fr -> fr.sample_index == 2031805 + 1, frames)]
v = frame_view(P, hk, x, fr)
plot_frame(v; bits = payload_bits(fr.payload), title = "idx=$(fr.sample_index) df=$(fr.df) crc=$(fr.crc)")

failed = filter(fr -> fr.crc == "fail", frames) # the ones worth looking at

# ----------------------------------------------------------------------------------------------------------
# C. Look at a whole block
# ----------------------------------------------------------------------------------------------------------
# demod_block keeps the envelope and preamble score of the whole block, the candidates the peak picker
# took and which of them passed the slice-magnitude threshold.

r = demod_block(P, hk, x, fr.block_start, fr.block_n)
r.candidates                                    # picked preamble positions (outputs of the block)
r.score[r.candidates] ./ r.level                # their scores, comparable with P.preamble_score_min
plot_block(P, r)
plot_block(P, r; outputs = fr.output_index-2000:fr.output_index+4000)

# A candidate that did not become a frame:
k = r.candidates[1]
vk = frame_view(P, hk, x, k, r.level; block_start = r.start)
vk.confidence, P.slice_magnitude_min            # below the threshold?

# ----------------------------------------------------------------------------------------------------------
# D. Try a different filter or thresholds
# ----------------------------------------------------------------------------------------------------------

p2 = design_filter_bank(rate; window = :kaiser, kaiser_beta = 6.0, cutoff_hz = 2e6,
                        pulse_width_s = MODE_S_PULSE_WIDTH_S)   # --filter-matched
plot_filter(p2, rate)
hk2 = kernel_taps(p2)
P2 = DemodParams(hk2, rate; preamble_min = 2.5, slice_mag_min = 90)
frames2 = validate_frames(demod_stream(P2, hk2, x))
count(fr -> fr.crc_ok, frames), count(fr -> fr.crc_ok, frames2)

# The original Hann bank with derivative planes (not something adsb uses):
pd = PolyphaseFilterBank(Float32, 33, 64, 3)

# ----------------------------------------------------------------------------------------------------------
# E. Check this code against the C++ on a debug HDF5
# ----------------------------------------------------------------------------------------------------------

f = h5open("frames.h5")
for name in frame_names(f)
    local fr = load_frame(f, name)
    local v = frame_view(f, fr)
    err = maximum(abs.(v.y .- fr["envelope"])) / maximum(fr["envelope"])
    (v.bits == fr["bits"] && err < 1e-4) || println(name, ": differs (envelope rel err ", err, ")")
end

# ----------------------------------------------------------------------------------------------------------
# F. Export a filter bank for `adsb --filter prefix.bin --filter-meta prefix.meta`
# ----------------------------------------------------------------------------------------------------------

save_filter_bank(PolyphaseFilterBank(Float32, 33, 64), "filters/filter")
