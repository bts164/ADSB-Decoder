#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <coro/coro_stream.h>
#include <coro/sync/broadcast.h>

#include "api/convert.h"
#include "dsp/demod.h"
#include "dsp/iq_block.h"
#include "wsrpc/service.h"

/**
 * @file
 * Captures: SigMF recordings of one point in the pipeline, each made by a UI API RecordCapture call
 * (api_server.h) for a stretch of stream time, from a receiver of the point's broadcast that the call activates.
 */

/**
 * One block's frames, as the sink publishes them: for every block while the broadcast has receivers, with or
 * without frames, so a reader knows how far the decoding has reached. Shared, since a broadcast keeps each value
 * in its queue for the other receivers, so every receive copies it out: the shared pointer makes that copy a
 * reference count rather than a deep copy.
 */
struct FrameBlock {
    uint64_t end = 0;                     /**< the input's stream index just past the block's own samples */
    uint64_t envelope_end = 0;            /**< the envelope's stream index just past the block's own outputs */
    std::vector<api::Frame> frames;       /**< in sample order */
    std::vector<uint64_t> envelope_index; /**< each frame's preamble in the envelope's stream index */
};
using LiveFrames = std::shared_ptr<const FrameBlock>;

/** A capture point: how its recordings are labelled. */
struct CapturePoint {
    /** Longest capture a call may ask for: 24 GB of cf32 at 50 Msps. */
    static constexpr double kMaxDurationS = 60;

    const char* name;               /**< "input", "envelope": in the logs, and the SigMF description */
    const char* gap_plot;           /**< a string literal: the TRACE_PLOT of the samples missing from captures */
    double rate_hz;                 /**< the point's sample rate */
    /** Whether a frame is placed by its FrameBlock::envelope_index rather than its Frame::idx, the input's. */
    bool envelope_indexed;
    std::optional<double> freq_hz;  /**< center frequency, for IQ */
    /** For float IQ: the magnitude of I or Q the source clips at, when it's known; SigMF's `adsb:full_scale`. */
    std::optional<double> full_scale = {};
};

/**
 * Records one capture of the input: the own samples of the blocks from `in` (not their padding), in their own
 * format, one of iq::Formats, with the frames from `frames` that start in them as SigMF annotations.
 *
 * The capture is `<name>.sigmf-data` in `dir`, with its `.sigmf-meta`, written when the first block arrives, at
 * each new segment, and at the end. It starts at the first block `in` receives and covers `duration_s` of stream
 * time. Samples that don't arrive (behind a slow disk, as `in` lags, or lost by the source) start a new SigMF
 * capture segment at the next that do, logged at WARNING and plotted. Logs the capture's start and end at INFO.
 *
 * The frames come a block or more after their samples, so once the samples are written, the capture waits for
 * the frames to pass its end, or for them to end. Each annotation spans the frame's preamble and bits, with its
 * summary as `core:label` and its fields as `adsb:` ones. Frames the writer lags behind are lost, and logged at
 * WARNING and plotted, and their count is the metadata's `adsb:frame_blocks_lost`.
 *
 * A writer task, held in the stream's frame, writes the capture, and never waits on the stream's reader, so a slow
 * client loses progress reports but no samples. The stream yields the progress about twice a second, then a last
 * report with its `capture` set. Dropping the stream, as a cancelled call does, cancels the writer, and what it
 * has written stays, with its metadata.
 * @param point the input point, with the input rate and center frequency
 * @param in a receiver activated for this capture
 * @param frames the sink's, activated for this capture with `in`
 * @param request checked by the server, all but the file not existing yet
 * @param dir the capture directory when the call was made (resolve_capture_dir())
 * @param receiver the receiver's position when the call was made: with a fix, the metadata's `core:geolocation`
 * @throws wsrpc::CallError for a file that exists or can't be created, or a write that fails
 */
coro::CoroStream<api::RecordCaptureProgress> run_capture(CapturePoint point, coro::BroadcastReceiver<IqBlock> in,
                                                         coro::BroadcastReceiver<LiveFrames> frames,
                                                         api::RecordCaptureRequest request, std::string dir,
                                                         api::ReceiverPosition receiver);

/**
 * As the input's run_capture(), writing the envelope runs from `in` as rf32_le. A frame's annotation starts at
 * its envelope index, which its `adsb:envelope_index` records beside the input's.
 */
coro::CoroStream<api::RecordCaptureProgress> run_capture(CapturePoint point,
                                                         coro::BroadcastReceiver<EnvelopeShare> in,
                                                         coro::BroadcastReceiver<LiveFrames> frames,
                                                         api::RecordCaptureRequest request, std::string dir,
                                                         api::ReceiverPosition receiver);

/** Whether `name` names a recording in the capture directory: not empty, no '/' or NUL, not starting with '.'. */
bool is_capture_name(std::string_view name);

/**
 * Reads a recording's `.sigmf-meta`. Blocks on the filesystem, so call it on a coro::spawn_blocking() thread.
 * @param data_bytes the `.sigmf-data`'s size, for the duration
 * @return what it says, or nullopt for one missing or invalid
 */
std::optional<api::CaptureMeta> read_capture_meta(const std::string& meta_path, uint64_t data_bytes);

/** What read_capture_annotations() found. */
struct CaptureAnnotations {
    std::vector<api::CaptureAnnotation> annotations;  /**< in sample order */
    size_t unreadable = 0;                            /**< those it skipped, invalid or past the end */
};

/**
 * Reads a recording's SigMF annotations from its `.sigmf-meta`: any tool's, and with the `adsb:` fields of those
 * run_capture() wrote, their frames. Blocks on the filesystem, so call it on a coro::spawn_blocking() thread.
 * @param samples the recording's, which an annotation without a count runs to
 * @return them, or nullopt for a metadata file missing or invalid
 */
std::optional<CaptureAnnotations> read_capture_annotations(const std::string& meta_path, uint64_t samples);

/**
 * Checks that `dir` is a directory the process can write into, and makes it absolute, so the logs and the UI
 * name it the same whatever the working directory.
 * @return the absolute path, or why it can't be the capture directory
 */
std::expected<std::string, std::string> resolve_capture_dir(const std::string& dir);

/**
 * Lists the SigMF recordings in `dir`: every `.sigmf-data`, newest first, with what its `.sigmf-meta` says.
 * Blocks on the filesystem, so call it on a coro::spawn_blocking() thread.
 * @return the listing, or why `dir` couldn't be read
 */
std::expected<api::ListCapturesResponse, std::string> list_capture_files(const std::string& dir);
