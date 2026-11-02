#include "dsp/iq_resampler.h"

#include <cstdint>
#include <stdexcept>
#include <utility>
#include <variant>

#include "kernels_ispc.h"

namespace {

constexpr int kPosFracBits = 36;  // kernels.ispc's fixed-point position format (resample_pos_fixed)

// The I/Q resampling kernel for blocks in format F written as format G (kernels.ispc: RESAMPLE_IQ). This is the
// one place that pairs each of iq::Formats and each of iq::OutputFormat with its kernel; a pair without a
// specialization here fails to compile in run().
template <class F, class G>
struct IqResampleKernel;
template <>
struct IqResampleKernel<iq::F32, iq::I16Be> {
    static constexpr auto run = &ispc::resample_iq_f32_i16be;
};
template <>
struct IqResampleKernel<iq::F32, iq::F32Be> {
    static constexpr auto run = &ispc::resample_iq_f32_f32be;
};
template <>
struct IqResampleKernel<iq::I16Be, iq::I16Be> {
    static constexpr auto run = &ispc::resample_iq_i16be_i16be;
};
template <>
struct IqResampleKernel<iq::I16Be, iq::F32Be> {
    static constexpr auto run = &ispc::resample_iq_i16be_f32be;
};

// The first output whose position, k * in / out, is at or past input sample `pos`.
uint64_t first_output_at(uint64_t pos, uint32_t in, uint32_t out) {
    const unsigned __int128 num = static_cast<unsigned __int128>(pos) * out;
    return static_cast<uint64_t>((num + in - 1) / in);
}

}  // namespace

IqResampler::IqResampler(FilterBank filter, uint32_t input_rate_hz, uint32_t output_rate_hz,
                         iq::OutputFormat format)
    : m_filter(std::move(filter)),
      m_input_rate_hz(input_rate_hz),
      m_output_rate_hz(output_rate_hz),
      m_format(format),
      m_sample_bytes(std::visit([]<class G>(G) { return iq::bytes_per_sample<G>(); }, format)) {
    if (input_rate_hz == 0 || output_rate_hz == 0) throw std::invalid_argument("IqResampler: rates must be positive");
    // Output m's tap window is [base - lead, base - lead + kernel_stride / 2), and base is at most n (see the
    // centring below), so the trail covers the window of an output at the block's last sample.
    const int lead = (m_filter.taps - 1) / 2;
    m_pad = {lead, m_filter.kernel_stride / 2 - lead};
    m_step = static_cast<int64_t>(((static_cast<unsigned __int128>(input_rate_hz) << kPosFracBits) +
                                   output_rate_hz / 2) / output_rate_hz);
    // Puts each output at the phase nearest its position: half a sample for odd taps, whose window is
    // centred on base, and none for even taps, centred on base + 1/2. demodulate()'s pos_offset() is the same.
    m_centre = m_filter.taps % 2 == 1 ? int64_t{1} << (kPosFracBits - 1) : 0;
}

IqResampler IqResampler::identity(uint32_t rate_hz, iq::OutputFormat format) {
    FilterBank bank = design_filter_bank(
        FilterDesign{.taps = 1, .num_phases = 1, .cutoff_hz = rate_hz / 2.0}, rate_hz, rate_hz);
    bank.description = "none";
    return IqResampler(std::move(bank), rate_hz, rate_hz, format);
}

IqResampler::Range IqResampler::outputs(const IqBlock& block) const {
    return {first_output_at(block.start(), m_input_rate_hz, m_output_rate_hz),
            first_output_at(block.start() + block.n(), m_input_rate_hz, m_output_rate_hz)};
}

size_t IqResampler::run(const IqBlock& block, uint64_t first, std::span<uint8_t> out) const {
    const size_t count = out.size() / m_sample_bytes;
    if (count * m_sample_bytes != out.size())
        throw std::invalid_argument("IqResampler::run: output is not a whole number of samples");
    if (block.lead() < m_pad.lead || block.trail() < static_cast<size_t>(m_pad.trail))
        throw std::invalid_argument("IqResampler::run: block has less guard padding than padding()");
    const Range range = outputs(block);
    if (first < range.first || count > range.end - first)
        throw std::out_of_range("IqResampler::run: outputs outside the block's range");
    if (count == 0) return 0;

    // The first output's exact position, from the block's first real sample: the whole samples, which pick
    // where the kernel's input starts, and the fraction, which is its offset. Each span starts exact, so the
    // fixed-point step's rounding never accumulates past one span.
    const unsigned __int128 num = static_cast<unsigned __int128>(first) * m_input_rate_hz;
    const uint64_t whole = static_cast<uint64_t>(num / m_output_rate_hz) - block.start();
    const auto rem = static_cast<uint64_t>(num % m_output_rate_hz);
    const int64_t offset =
        static_cast<int64_t>((static_cast<unsigned __int128>(rem) << kPosFracBits) / m_output_rate_hz) + m_centre;
    if (count > INT32_MAX ||
        static_cast<unsigned __int128>(count) * static_cast<uint64_t>(m_step) + static_cast<uint64_t>(offset) >=
            (static_cast<unsigned __int128>(1) << 62))
        throw std::length_error("IqResampler::run: span too long");

    return std::visit(
        [&]<class G>(G) {
            auto* y = reinterpret_cast<typename G::Sample*>(out.data());
            return block.visit([&]<class F>(IqView<F> v) {
                const float scale = G::full_scale / F::full_scale;
                const auto* x = v.padded.subspan(2 * (static_cast<size_t>(v.lead - m_pad.lead) + whole)).data();
                return static_cast<size_t>(IqResampleKernel<F, G>::run(
                    x, m_filter.kernel_coeffs.data(), m_filter.kernel_stride, m_filter.num_phases, m_step, offset,
                    static_cast<int>(count), y, scale));
            });
        },
        m_format);
}
