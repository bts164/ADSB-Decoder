#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "decode/frame_decode.h"
#include "dsp/demod.h"
#include "dsp/filter_bank.h"
#include "dsp/iq_block.h"

/**
 * @file
 * FrameRecorder: writes each demodulated frame, with the IQ and envelope it came from and everything needed to
 * rerun the demodulator on it offline, to an HDF5 file (`adsb --debug-h5`). The layout is documented in
 * README.md ("Debug HDF5").
 */

namespace HighFive {
class File;
}

/** Records frames to an HDF5 debug file. Not thread-safe; lives on run_demod_loop's thread. */
class FrameRecorder {
public:
    /**
     * Creates (truncating) `path` and writes the file-level attributes and `/filter`.
     * @param path HDF5 file to create
     * @param params the demodulator's AdsbDemod::params()
     * @param filter the demodulator's filter bank
     * @param command_line the command line, stored for reference
     * @param failed_only record only frames that fail CRC
     * @throws HighFive::Exception if the file can't be created
     */
    FrameRecorder(const std::string& path, const AdsbDemodParams& params, const FilterBank& filter,
                  const std::string& command_line, bool failed_only);
    ~FrameRecorder();

    /**
     * Records one frame as the group `/frames/<sample_index>`. Call right after the AdsbDemod::exec() that
     * returned `f`, before the next one, since it reads the demodulator's last envelope.
     * @param demod the demodulator that produced `f`
     * @param block the block `f` came from
     * @param f the frame
     * @param v `f` after validation (compute_frame_view)
     */
    void record(const AdsbDemod& demod, const IqBlock& block, const AdsbFrame& f, const DecodedFrameView& v);

private:
    std::unique_ptr<HighFive::File> file_;
    AdsbDemodParams params_;
    int taps_;
    bool failed_only_;
    std::chrono::steady_clock::time_point next_flush_;
};
