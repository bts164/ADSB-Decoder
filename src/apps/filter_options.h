#pragma once

#include <optional>
#include <stdexcept>
#include <string>

#include <argparse/argparse.hpp>

#include "dsp/demod.h"
#include "dsp/filter_bank.h"

/**
 * @file
 * The resampling-filter command-line options `adsb` and `adsb_sim_eval` share.
 */

/** The --filter* options: a filter bank to load, or the design of one. */
struct FilterOptions {
    /** Mode S pulse width, for --filter-matched. */
    static constexpr double kModeSPulseWidthS = 0.5e-6;

    std::optional<std::string> load_path;  /**< --filter: load this bank instead of designing one */
    std::optional<std::string> meta_path;  /**< --filter-meta, with load_path */
    FilterDesign design;

    /** Adds the --filter* options to `program`. */
    static void add_options(argparse::ArgumentParser& program) {
        program.add_argument("--filter-taps").scan<'i', int>().default_value(32).help(
            "designed resampling filter: taps per phase");
        program.add_argument("--filter-phases").scan<'i', int>().default_value(64).help(
            "designed resampling filter: number of phases");
        program.add_argument("--filter-cutoff").scan<'g', double>().default_value(3e6).help(
            "designed resampling filter: lowpass cutoff in Hz, clamped to the input and resampled Nyquist, so it "
            "also serves as the anti-alias filter when the input rate is above the resampled rate");
        program.add_argument("--filter-window").default_value(std::string("hann")).help(
            "designed resampling filter: lowpass window, hann, hamming, blackman or kaiser");
        program.add_argument("--filter-kaiser-beta").scan<'g', double>().default_value(8.0).help(
            "designed resampling filter: Kaiser window beta, with --filter-window kaiser");
        program.add_argument("--filter-matched").flag().help(
            "designed resampling filter: also match it to Mode S's 0.5 us pulses (lowpass convolved with a 0.5 us "
            "boxcar)");
        program.add_argument("--filter").help(
            "load the filter bank from this .bin (from filters/export_filter.jl) instead of designing it; needs "
            "--filter-meta");
        program.add_argument("--filter-meta").help("filter bank metadata file (.meta) for --filter");
    }

    /**
     * @throws std::invalid_argument if only one of --filter and --filter-meta is given, or for an unknown
     *         --filter-window
     */
    static FilterOptions from_options(const argparse::ArgumentParser& program) {
        if (program.is_used("--filter") != program.is_used("--filter-meta"))
            throw std::invalid_argument("--filter and --filter-meta go together");
        return {
            .load_path = program.present("--filter"),
            .meta_path = program.present("--filter-meta"),
            .design =
                {
                    .taps = program.get<int>("--filter-taps"),
                    .num_phases = program.get<int>("--filter-phases"),
                    .cutoff_hz = program.get<double>("--filter-cutoff"),
                    .window = parse_filter_window(program.get<std::string>("--filter-window")),
                    .kaiser_beta = program.get<double>("--filter-kaiser-beta"),
                    .pulse_width_s = program.get<bool>("--filter-matched") ? kModeSPulseWidthS : 0,
                },
        };
    }

    /**
     * Loads or designs the filter bank for an input at `rate_hz`.
     * @throws std::invalid_argument or std::runtime_error for a design that can't be built, or files that can't
     *         be read
     */
    FilterBank make(double rate_hz) const {
        if (load_path) return load_filter_bank(*load_path, *meta_path);
        return design_filter_bank(design, rate_hz, demod_output_rate_hz());
    }
};
