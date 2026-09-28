#pragma once

#include <string>

#include <xtensor/xtensor.hpp>

/**
 * @file
 * Polyphase resampling filter bank: designed at startup for the input sample rate (the default), or loaded
 * from files exported by filters/export_filter.jl.
 */

/** A polyphase FIR bank: `num_phases` sub-filters of `taps` coefficients each. */
struct FilterBank {
    /**
     * {num_phases, taps}: coeffs(phase, tap), with each phase's taps in the order the kernel applies them
     * against increasing sample index (reversed relative to polyphase.jl's layout).
     */
    xt::xtensor<float, 2> coeffs;
    /**
     * {num_phases, kernel_stride}: the layout resample_block (kernels.ispc) consumes. Every tap appears
     * twice in a row (h0, h0, h1, h1, ...) to line up with interleaved I/Q samples, and each row is
     * zero-padded to kernel_stride floats, a multiple of the widest ispc gang.
     */
    xt::xtensor<float, 2> kernel_coeffs;
    int taps = 0;
    int num_phases = 0;
    int kernel_stride = 0;
    double cutoff_hz = 0;  /**< design_filter_bank's cutoff after clamping; 0 for a loaded bank */
    std::string description;  /**< how it was made, e.g. `hann, cutoff 1.2 MHz` or `loaded <path>` */
};

/** Window applied to design_filter_bank's windowed-sinc lowpass. */
enum class FilterWindow { hann, hamming, blackman, kaiser };

/** Parses `hann`, `hamming`, `blackman` or `kaiser`. @throws std::invalid_argument otherwise */
FilterWindow parse_filter_window(const std::string& name);

/** Parameters for design_filter_bank(). */
struct FilterDesign {
    int taps = 32;          /**< taps per phase */
    int num_phases = 64;    /**< number of phases (sub-sample positions) */
    double cutoff_hz = 3e6; /**< requested lowpass cutoff (one-sided, complex baseband) */
    FilterWindow window = FilterWindow::hann;
    double kaiser_beta = 8.0;  /**< Kaiser window shape, for FilterWindow::kaiser */
    /**
     * If positive, the lowpass is convolved with a boxcar this long, making the bank a filter matched to
     * rectangular pulses of this width (0.5 us for Mode S) as well as the resampling interpolator.
     */
    double pulse_width_s = 0;
};

/**
 * Designs a polyphase resampling filter bank that also band-limits the signal.
 *
 * The prototype is a windowed sinc lowpass at `num_phases` times the input rate, optionally convolved with a
 * `pulse_width_s` boxcar, `taps * num_phases` coefficients in all, scaled to unity DC gain. Phase p takes
 * every num_phases-th coefficient from p. With the Hann window and no pulse this is PolyphaseFilterBank in
 * polyphase.jl, which it matches exactly when the cutoff is the input Nyquist. The cutoff is clamped to the
 * input and output Nyquist frequencies: at an input rate above the output rate the lowpass is the
 * anti-alias filter; below it, the clamp to the input Nyquist makes it a plain interpolator.
 * @param design taps, phases, cutoff, window and pulse width
 * @param input_rate_hz input sample rate
 * @param output_rate_hz resampled rate (AdsbDemod::output_rate_hz())
 * @throws std::invalid_argument on non-positive taps, phases, rates or cutoff, a negative pulse width or
 * Kaiser beta, or a pulse longer than the filter
 */
FilterBank design_filter_bank(const FilterDesign& design, double input_rate_hz, double output_rate_hz);

/**
 * Loads a filter bank exported by filters/export_filter.jl. Only plane 0 (the filter itself, not the
 * derivative planes) is used.
 * @param bin_path flat float32 array of taps * Np * planes, Julia column-major
 * @param meta_path `key=value` text sidecar with taps, Np, planes and dtype
 * @throws std::runtime_error if a file can't be read, is too short, or has an unsupported dtype
 */
FilterBank load_filter_bank(const std::string& bin_path, const std::string& meta_path);
