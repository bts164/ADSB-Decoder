#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

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

/** A capture point: how its recordings are labelled. */
struct CapturePoint {
    /** Longest capture a call may ask for: 24 GB of cf32 at 50 Msps. */
    static constexpr double kMaxDurationS = 60;

    const char* name;               /**< "input", "envelope": in the logs, and the SigMF description */
    const char* gap_plot;           /**< a string literal: the TRACE_PLOT of the samples missing from captures */
    double rate_hz;                 /**< the point's sample rate */
    std::optional<double> freq_hz;  /**< center frequency, for IQ */
};

/**
 * Records one capture of the input: the own samples of the blocks from `in` (not their padding), in their own
 * format, cf32_le or ci16_be.
 *
 * The capture is `<name>.sigmf-data` in `dir`, with its `.sigmf-meta`, written when the first block arrives, at
 * each new segment, and at the end. It starts at the first block `in` receives and covers `duration_s` of stream
 * time. Samples that don't arrive (behind a slow disk, as `in` lags, or lost by the source) start a new SigMF
 * capture segment at the next that do, logged at WARNING and plotted. Logs the capture's start and end at INFO.
 *
 * A writer task, held in the stream's frame, writes the capture, and never waits on the stream's reader, so a slow
 * client loses progress reports but no samples. The stream yields the progress about twice a second, then a last
 * report with its `capture` set. Dropping the stream, as a cancelled call does, cancels the writer, and what it
 * has written stays, with its metadata.
 * @param point the input point, with the input rate and center frequency
 * @param in a receiver activated for this capture
 * @param request checked by the server, all but the file not existing yet
 * @param dir the capture directory when the call was made (resolve_capture_dir())
 * @param receiver the receiver's position when the call was made: with a fix, the metadata's `core:geolocation`
 * @throws wsrpc::CallError for a file that exists or can't be created, or a write that fails
 */
coro::CoroStream<api::RecordCaptureProgress> run_capture(CapturePoint point, coro::BroadcastReceiver<IqBlock> in,
                                                         api::RecordCaptureRequest request, std::string dir,
                                                         api::ReceiverPosition receiver);

/** As the input's run_capture(), writing the envelope runs from `in` as rf32_le. */
coro::CoroStream<api::RecordCaptureProgress> run_capture(CapturePoint point,
                                                         coro::BroadcastReceiver<EnvelopeShare> in,
                                                         api::RecordCaptureRequest request, std::string dir,
                                                         api::ReceiverPosition receiver);

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
