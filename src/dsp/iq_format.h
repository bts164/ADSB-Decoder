#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

/**
 * @file
 * The sample formats: how a component of an I/Q pair, or a real sample, is stored, in blocks (iq_block.h), on the
 * wire (IqResampler, vrt) and in recordings (SigMF). A source builds its blocks in the format its input arrives in,
 * and the resampler reads that format directly (kernels.ispc), so no source spends a pass over its samples
 * converting them to float.
 *
 * A format is an empty tag type, the same members on every one:
 *  - `Sample`: the type of one component as stored;
 *  - `complex`: whether a sample is an I and a Q, rather than one real;
 *  - `integer`: whether the stored value is an integer, rather than an IEEE float;
 *  - `byte_order`: the stored value's byte order;
 *  - `sigmf`: the SigMF core:datatype name, which is also what vita49_send's --stream-format takes;
 *  - `zero`: the stored value of a zero sample, used for guard padding and gap fill;
 *  - `full_scale`: the magnitude to_float() gives for a full-scale input, so to_float(s) / full_scale is
 *    in [-1, 1];
 *  - `to_float(s)`: the value of a stored component, unscaled.
 * Tags compare equal to themselves, so two format variants compare by the format they hold.
 *
 * A FormatList is a set of them as a variant, `Format`, for a format chosen at run time; std::visit then gives
 * the tag, and with it the format's members at compile time.
 *
 * To add a format a block can hold:
 *  1. define its tag here and add it to iq::Formats and iq::RecordingFormats;
 *  2. in kernels.ispc, add its tag, its load(), a RESAMPLE_FORMAT line and a RESAMPLE_IQ line for each output
 *     format;
 *  3. in demod.cpp, add its ResampleKernel specialization, and in iq_resampler.cpp its IqResampleKernel ones;
 *  4. in capture_summary.cpp, give it a summary header number.
 * Nothing else names the formats: IqBlock's storage, every dispatch on it, what `adsb file` reads and what a
 * capture records follow from the lists, and the build fails if step 3 or 4 is missed.
 */

namespace iq {

/** Native float, as in a cf32_le recording. */
struct F32 {
    using Sample = float;
    static constexpr bool complex = true;
    static constexpr bool integer = false;
    static constexpr std::endian byte_order = std::endian::little;
    static constexpr std::string_view sigmf = "cf32_le";
    static constexpr Sample zero = 0.0f;
    static constexpr float full_scale = 1.0f;
    static float to_float(Sample s) { return s; }
    bool operator==(const F32&) const = default;
};

/** Big-endian int16, as a VITA-49 data packet carries it. `Sample` holds the bytes in wire order. */
struct I16Be {
    using Sample = int16_t;
    static constexpr bool complex = true;
    static constexpr bool integer = true;
    static constexpr std::endian byte_order = std::endian::big;
    static constexpr std::string_view sigmf = "ci16_be";
    static constexpr Sample zero = 0;
    static constexpr float full_scale = 32768.0f;
    static float to_float(Sample s) {
        const auto wire = static_cast<uint16_t>(s);
        const uint16_t host = std::endian::native == std::endian::big ? wire : std::byteswap(wire);
        return static_cast<float>(static_cast<int16_t>(host));
    }
    bool operator==(const I16Be&) const = default;
};

/**
 * Big-endian IEEE float, as a VITA-49 data packet can carry it. An output format only: IqResampler writes it, but
 * it is not one of iq::Formats, so no block holds it. `Sample` holds a float's bytes in wire order.
 */
struct F32Be {
    using Sample = uint32_t;
    static constexpr bool complex = true;
    static constexpr bool integer = false;
    static constexpr std::endian byte_order = std::endian::big;
    static constexpr std::string_view sigmf = "cf32_be";
    static constexpr Sample zero = 0;
    static constexpr float full_scale = 1.0f;
    static float to_float(Sample s) {
        return std::bit_cast<float>(std::endian::native == std::endian::big ? s : std::byteswap(s));
    }
    bool operator==(const F32Be&) const = default;
};

/** Native real float, as the envelope's captures record it. No block holds it. */
struct RF32 {
    using Sample = float;
    static constexpr bool complex = false;
    static constexpr bool integer = false;
    static constexpr std::endian byte_order = std::endian::little;
    static constexpr std::string_view sigmf = "rf32_le";
    static constexpr Sample zero = 0.0f;
    static constexpr float full_scale = 1.0f;
    static float to_float(Sample s) { return s; }
    bool operator==(const RF32&) const = default;
};

static_assert(std::endian::native == F32::byte_order && std::endian::native == RF32::byte_order,
              "F32 and RF32 hold the host's floats");

/** Bytes of one sample in format F: an I and a Q, or one real. */
template<class F>
constexpr size_t bytes_per_sample() {
    return (F::complex ? 2 : 1) * sizeof(typename F::Sample);
}

// The format a variant of formats holds, such as a FormatList's `Format`, as its members.

/** The SigMF name of the format `format` holds. */
template<class... F>
constexpr std::string_view sigmf_name(const std::variant<F...>& format) {
    return std::visit([]<class G>(G) { return G::sigmf; }, format);
}

/** bytes_per_sample() of the format `format` holds. */
template<class... F>
constexpr size_t bytes_per_sample(const std::variant<F...>& format) {
    return std::visit([]<class G>(G) { return bytes_per_sample<G>(); }, format);
}

/** Whether the format `format` holds is complex. */
template<class... F>
constexpr bool is_complex(const std::variant<F...>& format) {
    return std::visit([]<class G>(G) { return G::complex; }, format);
}

/** A list of formats, and the types generated from it. */
template<class... F>
struct FormatList {
    /** A format of the list, chosen at run time. */
    using Format = std::variant<F...>;
    /** One `T<F>` for any format F of the list, such as a sample buffer in it. */
    template<template<class> class T>
    using Each = std::variant<T<F>...>;

    /** The first format whose tag `pred` holds for, called as `pred.template operator()<F>()`; nullopt if none. */
    template<class Pred>
    static constexpr std::optional<Format> find(Pred&& pred) {
        std::optional<Format> found;
        (void)((pred.template operator()<F>() ? (found = F{}, true) : false) || ...);
        return found;
    }

    /** The format SigMF's core:datatype `name` names; nullopt for one not in the list. */
    static constexpr std::optional<Format> from_sigmf(std::string_view name) {
        return find([&]<class G>() { return G::sigmf == name; });
    }

    /** The list's SigMF names, comma-separated, for an error that says what is accepted. */
    static std::string sigmf_names() {
        std::string names;
        ((names += (names.empty() ? "" : ", "), names += F::sigmf), ...);
        return names;
    }
};

/** Every format an IqBlock can hold. The first is what a default-constructed block holds. */
using Formats = FormatList<F32, I16Be>;

/** Every format IqResampler can write, and so every format a VITA-49 stream carries. */
using OutputFormats = FormatList<I16Be, F32Be>;
using OutputFormat = OutputFormats::Format;

/** Every format a capture writes, and so every format the IQ viewer reads: a block's, or the envelope's. */
using RecordingFormats = FormatList<F32, I16Be, RF32>;
using RecordingFormat = RecordingFormats::Format;

}  // namespace iq
