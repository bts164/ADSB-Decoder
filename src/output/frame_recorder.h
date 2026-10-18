#pragma once

#include <chrono>
#include <memory>
#include <span>
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

/** Records frames to an HDF5 debug file. Not thread-safe: one task uses it at a time. */
class FrameRecorder {
public:
    /**
     * Creates (truncating) `path` and writes the file-level attributes and `/filter`.
     * @param path HDF5 file to create
     * @param params the demodulator's demod_params()
     * @param filter the demodulator's filter bank
     * @param command_line the command line, stored for reference
     * @param failed_only record only frames that fail CRC
     * @throws HighFive::Exception if the file can't be created
     */
    FrameRecorder(const std::string& path, const AdsbDemodParams& params, const FilterBank& filter,
                  const std::string& command_line, bool failed_only);
    FrameRecorder(FrameRecorder&&) noexcept;
    ~FrameRecorder();

    /**
     * Records one frame as the group `/frames/<sample_index>`.
     * @param block the block `f` came from
     * @param envelope the DemodSnapshot::envelope of the block `f` came from
     * @param level that DemodSnapshot's level
     * @param f the frame
     * @param v `f` after validation (compute_frame_view)
     */
    void record(const IqBlock& block, std::span<const float> envelope, float level, const AdsbFrame& f,
                const DecodedFrameView& v);

private:
    std::unique_ptr<HighFive::File> m_file;
    AdsbDemodParams m_params;
    int m_taps;
    bool m_failed_only;
    std::chrono::steady_clock::time_point m_next_flush;
};
