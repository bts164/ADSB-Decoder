#pragma once

#include <string>

#include <argparse/argparse.hpp>

#include "dsp/demod.h"
#include "dsp/filter_bank.h"

/**
 * @file
 * The resampling-filter command-line options `adsb` and `adsb_sim_eval` share.
 */

/** Mode S pulse width, for --filter-matched. */
constexpr double kModeSPulseWidthS = 0.5e-6;

/** Adds the --filter* options to `program`. */
inline void add_filter_options(argparse::ArgumentParser& program) {
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
 * Loads (--filter) or designs the filter bank the --filter* options describe.
 * @param rate_hz input sample rate
 * @throws std::invalid_argument or std::runtime_error on bad options or unreadable files
 */
inline FilterBank make_filter(const argparse::ArgumentParser& program, double rate_hz) {
    if (program.is_used("--filter") != program.is_used("--filter-meta"))
        throw std::invalid_argument("--filter and --filter-meta go together");
    if (program.is_used("--filter"))
        return load_filter_bank(program.get<std::string>("--filter"), program.get<std::string>("--filter-meta"));
    FilterDesign d;
    d.taps = program.get<int>("--filter-taps");
    d.num_phases = program.get<int>("--filter-phases");
    d.cutoff_hz = program.get<double>("--filter-cutoff");
    d.window = parse_filter_window(program.get<std::string>("--filter-window"));
    d.kaiser_beta = program.get<double>("--filter-kaiser-beta");
    d.pulse_width_s = program.get<bool>("--filter-matched") ? kModeSPulseWidthS : 0;
    return design_filter_bank(d, rate_hz, AdsbDemod::output_rate_hz());
}
