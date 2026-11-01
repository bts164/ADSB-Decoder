#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "dsp/filter_bank.h"
#include "dsp/iq_block.h"
#include "dsp/iq_format.h"

/**
 * @file
 * Resampling a stream of IQ blocks to another rate and an output wire format, for a stage that sends the I/Q on
 * rather than demodulating it.
 */

/**
 * A polyphase low-pass resampler with I/Q out, evaluated only at the output positions. Its schedule is the
 * stream's, not the block's: output k of the stream sits at input sample k * input_rate / output_rate, whichever
 * block that falls in, so the outputs of consecutive blocks run on without a seam. A block whose real samples
 * are [start, start + n) produces the outputs whose positions fall in that range.
 *
 * It holds no state between calls, so run() may be called for different spans of outputs at once, from
 * parallel parts.
 */
class IqResampler {
public:
    /** Stream-rate output indices [first, end). */
    struct Range {
        uint64_t first = 0;
        uint64_t end = 0;
    };

    /**
     * @param filter a bank designed for these rates by design_filter_bank()
     * @param format the format run() writes
     * @throws std::invalid_argument for a zero rate
     */
    IqResampler(FilterBank filter, uint32_t input_rate_hz, uint32_t output_rate_hz, iq::OutputFormat format);

    /** No rate change: one phase of one unit tap, so run() only converts the samples to `format`. */
    static IqResampler identity(uint32_t rate_hz, iq::OutputFormat format);

    /** Guard padding the blocks need around them: the filter's reach either side of an output. */
    IqPadding padding() const { return m_pad; }

    /** The outputs `block` produces. Empty for an empty block. */
    Range outputs(const IqBlock& block) const;

    /**
     * Writes outputs `first` onward, as many as `out` holds, in the output format: interleaved I/Q, each
     * component as the format's `Sample` in wire order. `out` must be aligned for that `Sample`.
     * @return how many outputs saturated, in either component (only `I16Be` saturates)
     * @throws std::invalid_argument if `out` is not a whole number of outputs, or the block has less padding
     *         than padding()
     * @throws std::out_of_range if the outputs are not all inside outputs(block)
     * @throws std::length_error for a span too long for the kernel's fixed-point positions
     */
    size_t run(const IqBlock& block, uint64_t first, std::span<uint8_t> out) const;

    uint32_t input_rate_hz() const { return m_input_rate_hz; }
    uint32_t output_rate_hz() const { return m_output_rate_hz; }
    const FilterBank& filter() const { return m_filter; }
    iq::OutputFormat format() const { return m_format; }
    /** Whether run() filters, rather than only converting the format. */
    bool resamples() const { return m_filter.taps > 1 || m_input_rate_hz != m_output_rate_hz; }
    /** Bytes of one output I/Q pair. */
    size_t sample_bytes() const { return m_sample_bytes; }

private:
    FilterBank m_filter;
    uint32_t m_input_rate_hz;
    uint32_t m_output_rate_hz;
    iq::OutputFormat m_format;
    size_t m_sample_bytes;
    IqPadding m_pad;
    int64_t m_step;    // input samples per output, in kernels.ispc's fixed point
    int64_t m_centre;  // added to each span's offset: see the constructor
};
