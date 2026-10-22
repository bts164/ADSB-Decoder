#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include <coro/coro.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/oneshot.h>
#include <coro/sync/watch.h>

#include "api/convert.h"
#include "dsp/iq_block.h"

/**
 * @file
 * Captures: SigMF recordings of one point in the pipeline, each started by a UI API capture call
 * (api_server.h) for a stretch of stream time. A point's capture task takes the point's samples and the calls
 * for it, and while no capture is in progress discards the samples.
 */

/** A capture call, on its way to the capture task for its point. */
struct CaptureCall {
    /** Longest capture a call may ask for: 24 GB of cf32 at 50 Msps. */
    static constexpr double kMaxDurationS = 60;
    /** The result of a capture, or why there is none. */
    using Reply = std::expected<api::CaptureResult, api::Error>;

    api::CaptureRequest request;  /**< checked by the server, all but the file not existing yet */
    /** The reply. The server drops it unsent when the client cancels, which ends the capture. */
    coro::OneshotSender<Reply> reply;
};

/** A capture point: what its capture task records, and how the recording is labelled. */
struct CapturePoint {
    const char* name;               /**< "input", "envelope": in the logs, and the SigMF description */
    const char* gap_plot;           /**< a string literal: the TRACE_PLOT of the samples missing from captures */
    double rate_hz;                 /**< the point's sample rate */
    std::optional<double> freq_hz;  /**< center frequency, for IQ */
};

/** One block's own envelope outputs (DemodBlock::envelope), for the envelope capture task. */
struct EnvelopeRun {
    uint64_t index;              /**< the envelope stream index of samples[0] */
    std::vector<float> samples;
};

/**
 * The input capture task: writes the own samples of the blocks from `in` (not their padding) in their own
 * format, cf32_le or ci16_be, for each call from `calls`. Ends when `in` closes, finishing any capture then.
 *
 * Each capture is `dir/<name>.sigmf-data` with its `.sigmf-meta`, written when the first block arrives, at
 * each new segment, and at the end. It starts at the first block after the call and covers `duration_s` of
 * stream time. Samples that don't arrive (behind a slow disk, as `in` is a lossy branch, or lost by the source)
 * start a new SigMF capture segment at the next that do, logged at WARNING and plotted. A call while one is in
 * progress, or for a file that exists, fails. Logs each capture's start and end at INFO.
 * @param dir --capture-dir
 * @param point the input point, with the input rate and center frequency
 */
coro::Coro<void> capture_input(std::string dir, CapturePoint point, coro::MpscReceiver<IqBlock> in,
                               coro::MpscReceiver<CaptureCall> calls);

/**
 * The envelope capture task: as capture_input(), writing the envelope runs from `in` as rf32_le.
 * @param active set true while a capture is in progress, so demodulate() copies the envelope only then
 */
coro::Coro<void> capture_envelope(std::string dir, CapturePoint point, coro::MpscReceiver<EnvelopeRun> in,
                                  coro::MpscReceiver<CaptureCall> calls, coro::WatchSender<bool> active);
