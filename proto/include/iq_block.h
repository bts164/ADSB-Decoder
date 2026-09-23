#pragma once

#include <complex>
#include <cstddef>
#include <vector>

// One buffer's worth of raw (pre-resample) IQ samples, guard-padded on both
// sides so it can be fed directly to AdsbDemod::exec() -- see demod.h's
// adsb_leading_pad/adsb_trailing_pad. Shared by every live-streaming source
// (rtlsdr today, other SDRs later) that feeds pipeline.h's run_demod_loop.
struct IqBlock {
    std::vector<std::complex<float>> samples;  // guard-padded: [lead][n][trail]
    size_t n = 0;
    int lead = 0;
};
