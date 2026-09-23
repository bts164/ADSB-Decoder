#pragma once

#include <chrono>

// Shared by CPR lat/lon math (aircraft.cpp), velocity heading (aircraft.cpp),
// and the spectrum Hann window (spectrum.cpp).
constexpr double kPi = 3.14159265358979323846;

// Rate-limits run_demod_loop's optional ADSB_TIMING throughput report (see
// pipeline.h) -- off by default (opt-in via the env var) since printing
// every block would be noisy and, for continuous --rtlsdr streaming, could
// itself become a bottleneck.
constexpr std::chrono::milliseconds kStatusReportInterval{1000};
