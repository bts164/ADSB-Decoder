#include "output/frame_recorder.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <vector>

#include <highfive/H5File.hpp>

namespace {
constexpr int kFormatVersion = 1;
// Flushing at most this often keeps the file readable after Ctrl+C or a crash without an HDF5 flush per frame.
constexpr std::chrono::seconds kFlushInterval{1};

std::string payload_hex(unsigned __int128 p) {
    char buf[29];
    std::snprintf(buf, sizeof buf, "%012llx%016llx", static_cast<unsigned long long>(p >> 64) & 0xffffffffffffULL,
                  static_cast<unsigned long long>(p));
    return buf;
}
}  // namespace

FrameRecorder::FrameRecorder(const std::string& path, const AdsbDemodParams& params, const FilterBank& filter,
                             const std::string& command_line, bool failed_only)
    : file_(std::make_unique<HighFive::File>(path, HighFive::File::Overwrite)),
      params_(params),
      taps_(filter.taps),
      failed_only_(failed_only),
      next_flush_(std::chrono::steady_clock::now() + kFlushInterval) {
    HighFive::File& f = *file_;
    f.createAttribute("format_version", kFormatVersion);
    f.createAttribute("command_line", command_line);
    f.createAttribute("sample_rate_hz", params.sample_rate_hz);
    f.createAttribute("output_rate_hz", params.output_rate_hz);
    f.createAttribute("samples_per_symbol", params.samples_per_symbol);
    f.createAttribute("num_bits", params.num_bits);
    f.createAttribute("preamble_pattern",
                      std::vector<float>(params.preamble_pattern.begin(), params.preamble_pattern.end()));
    f.createAttribute("preamble_spacing", params.preamble_spacing);
    f.createAttribute("bit_offset", params.bit_offset);
    f.createAttribute("exclusion_radius", params.exclusion_radius);
    f.createAttribute("max_lookahead", params.max_lookahead);
    f.createAttribute("preamble_score_min", params.preamble_score_min);
    f.createAttribute("slice_magnitude_min", params.slice_magnitude_min);
    f.createAttribute("pos_frac_bits", params.pos_frac_bits);
    f.createAttribute("pos_step", params.pos_step);
    f.createAttribute("pos_offset", params.pos_offset);
    f.createAttribute("index_offset", params.index_offset);
    f.createAttribute("window_lead", params.window_lead);

    // coeffs(phase, tap) in the order the kernel applies them against increasing sample index; row-major, so
    // column-major readers (Julia) see it as h[tap, phase].
    HighFive::DataSet h = f.createDataSet<float>(
        "filter", HighFive::DataSpace({static_cast<size_t>(filter.num_phases), static_cast<size_t>(filter.taps)}));
    h.write_raw(filter.coeffs.data());
    h.createAttribute("taps", filter.taps);
    h.createAttribute("num_phases", filter.num_phases);
    h.createAttribute("cutoff_hz", filter.cutoff_hz);
    h.createAttribute("description", filter.description);

    f.createGroup("frames");
    f.flush();
}

FrameRecorder::~FrameRecorder() = default;

void FrameRecorder::record(const AdsbDemod& demod, const IqBlock& block, const AdsbFrame& fr,
                           const DecodedFrameView& v) {
    if (failed_only_ && (v.crc_ok || !v.crc_checked)) return;
    const AdsbDemodParams& p = params_;

    // Envelope from 16 us before the preamble to 8 us past the frame's last sample, clipped to what exec()
    // computed; the IQ is exactly the samples those outputs' tap windows read.
    std::span<const float> y = demod.last_envelope();
    const int pre = 16 * p.samples_per_symbol;
    const int post = 8 * p.samples_per_symbol;
    const int e0 = std::max(0, fr.output_index - pre);
    const int e1 = std::min(static_cast<int>(y.size()), fr.output_index + p.max_lookahead + post);
    auto base = [&](int m) { return (static_cast<int64_t>(m) * p.pos_step + p.pos_offset) >> p.pos_frac_bits; };
    const int64_t i0 = base(e0) - p.window_lead;
    const int64_t i1 = base(e1 - 1) - p.window_lead + taps_;
    if (i0 < -block.lead || i1 > static_cast<int64_t>(block.n + block.trail()))
        throw std::logic_error("FrameRecorder: frame's IQ span outside the block");

    char name[32];
    std::snprintf(name, sizeof name, "%015llu", static_cast<unsigned long long>(fr.sample_index));
    HighFive::Group g = file_->getGroup("frames").createGroup(name);
    g.createAttribute("sample_index", fr.sample_index);
    g.createAttribute("block_start", block.start);
    g.createAttribute("block_n", static_cast<uint64_t>(block.n));
    g.createAttribute("output_index", fr.output_index);
    g.createAttribute("envelope_start", e0);
    g.createAttribute("iq_start", i0);
    g.createAttribute("level", demod.last_level());
    g.createAttribute("preamble_score", fr.preamble_score);
    g.createAttribute("confidence", fr.confidence);
    g.createAttribute("df", v.df);
    g.createAttribute("icao_known", static_cast<uint8_t>(v.icao_known));
    g.createAttribute("icao", v.icao);
    g.createAttribute("crc", std::string(crc_status(v)));
    g.createAttribute("crc_ok", static_cast<uint8_t>(v.crc_ok));
    g.createAttribute("fixed_bit", v.fixed_bit);
    g.createAttribute("num_bits", v.num_bits);
    g.createAttribute("payload_raw", payload_hex(fr.payload));
    g.createAttribute("payload", payload_hex(v.payload));

    const std::complex<float>* x = block.data();
    g.createDataSet("iq", std::vector<std::complex<float>>(x + i0, x + i1));
    g.createDataSet("envelope", std::vector<float>(y.begin() + e0, y.begin() + e1));
    std::vector<uint8_t> bits(static_cast<size_t>(p.num_bits));
    for (int i = 0; i < p.num_bits; i++) bits[static_cast<size_t>(i)] = static_cast<uint8_t>((fr.payload >> (p.num_bits - 1 - i)) & 1);
    g.createDataSet("bits", bits);

    if (auto now = std::chrono::steady_clock::now(); now >= next_flush_) {
        file_->flush();
        next_flush_ = now + kFlushInterval;
    }
}
