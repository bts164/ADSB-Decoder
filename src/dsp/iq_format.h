#pragma once

#include <bit>
#include <cstdint>
#include <variant>

#include <xtensor/xtensor.hpp>

/**
 * @file
 * The sample formats an IqBlock (iq_block.h) can hold. A source builds its blocks in the format its input
 * arrives in, and the resampler reads that format directly (kernels.ispc), so no source spends a pass over
 * its samples converting them to float.
 *
 * A format is a tag type with these members:
 *  - `Sample`: the type of one I or Q component as stored;
 *  - `zero`: the stored value of a zero sample, used for guard padding and gap fill;
 *  - `full_scale`: the magnitude to_float() gives for a full-scale input, so to_float(s) / full_scale is
 *    in [-1, 1];
 *  - `to_float(s)`: the value of a stored component, unscaled.
 *
 * To add a format:
 *  1. define its tag here and add it to iq::Formats;
 *  2. in kernels.ispc, add its tag, its load() and a RESAMPLE_FORMAT line;
 *  3. in demod.cpp, add its ResampleKernel specialization.
 * Nothing else names the formats: IqBlock's storage and every dispatch on it follow from iq::Formats, and
 * the build fails in demod.cpp if step 3 is missed.
 */

namespace iq {

/** Native float, as in a cf32 recording. */
struct F32 {
    using Sample = float;
    static constexpr Sample zero = 0.0f;
    static constexpr float full_scale = 1.0f;
    static float to_float(Sample s) { return s; }
};

/** Big-endian int16, as a VITA-49 data packet carries it. `Sample` holds the bytes in wire order. */
struct I16Be {
    using Sample = int16_t;
    static constexpr Sample zero = 0;
    static constexpr float full_scale = 32768.0f;
    static float to_float(Sample s) {
        const auto wire = static_cast<uint16_t>(s);
        const uint16_t host = std::endian::native == std::endian::big ? wire : std::byteswap(wire);
        return static_cast<float>(static_cast<int16_t>(host));
    }
};

/**
 * A block's sample buffer in format F: a (samples, 2) array, one row per sample with I in column 0 and Q in
 * column 1. Tagged by the format and not just by `F::Sample`, so two formats that store the same type (a
 * native and a big-endian int16, say) stay distinct.
 */
template<class F>
struct Samples {
    xt::xtensor<typename F::Sample, 2> data;
};

/** A list of formats, and the types generated from it. */
template<class... F>
struct FormatList {
    /** A sample buffer in any one of the formats. */
    using SamplesVariant = std::variant<Samples<F>...>;
};

/** Every format an IqBlock can hold. The first is what a default-constructed block holds. */
using Formats = FormatList<F32, I16Be>;
using SamplesVariant = Formats::SamplesVariant;

}  // namespace iq
