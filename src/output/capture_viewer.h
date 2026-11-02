#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/oneshot.h>
#include <coro/sync/watch.h>

#include "api/convert.h"
#include "output/capture_summary.h"
#include "wsrpc/service.h"

/**
 * @file
 * The IQ recording viewer's server side (doc/design/iq_viewer.md): the UI API's OpenCapture, which makes sure a
 * recording in the capture directory has a current summary (capture_summary.h), and GetCaptureView, which reduces
 * a span of it to columns, or returns its samples. Neither keeps a file open between calls.
 */

/** The `.sigmf-data` files RecordCapture is writing, by path, which OpenCapture refuses. */
using WritingCaptures = std::set<std::string>;

/**
 * A handle to the viewer task, which owns the summary builds in progress, so two opens of one recording share a
 * build. For one task at a time, like the coro::MpscSender it wraps: give each task its own clone(). The builds
 * are cancelled when the last handle goes.
 */
class CaptureViewer {
public:
    /** Most columns a view may ask for. */
    static constexpr uint32_t kMaxColumns = 8192;
    /** Samples per column at or under which a view is the samples themselves. */
    static constexpr double kSampleViewMax = 4;

    /** Starts the viewer task. Call from within the coro runtime. */
    static CaptureViewer spawn();

    CaptureViewer clone() const { return CaptureViewer(m_tx.clone()); }

    /**
     * OpenCapture: builds the recording's summary if it has none or a stale one, sharing a build in progress,
     * yielding its progress about twice a second, then the last reply with what a plot needs. Logs the recording
     * opened, and the build, at INFO. This handle must outlive the stream.
     * @param dir the capture directory (resolve_capture_dir())
     * @param writing the recordings being written, which are refused
     * @throws wsrpc::CallError as doc/design/iq_viewer.md's "Limits and errors" lists
     */
    coro::CoroStream<api::OpenCaptureProgress> open(std::string dir, api::OpenCaptureRequest request,
                                                    WritingCaptures writing);

    /** How a build is going, as the viewer task's watch of it says. */
    struct BuildProgress {
        uint64_t done = 0;                       /**< samples summarised */
        uint64_t total = 0;                      /**< in the recording */
        bool finished = false;
        std::optional<wsrpc::CallError> failed;  /**< with `finished` */
    };

private:
    // The one request: the build of a recording's summary, or the one in progress, by its data path. The reply is
    // a watch of it.
    struct Build {
        std::string dir;
        std::string name;
        std::string data_path;
        coro::OneshotSender<coro::WatchReceiver<BuildProgress>> reply;
    };

    explicit CaptureViewer(coro::MpscSender<Build> tx) : m_tx(std::move(tx)) {}

    static coro::Coro<void> run(coro::MpscReceiver<Build> rx);

    coro::MpscSender<Build> m_tx;
};

/**
 * GetCaptureView: one view of a recording in `dir`, read from what's on disk now: its samples for a fine view, and
 * its summary, which must be current, for a coarse one. Logs each at VLOGF(1).
 * @throws wsrpc::CallError as doc/design/iq_viewer.md's "Limits and errors" lists
 */
coro::Coro<api::CaptureView> capture_view(std::string dir, api::GetCaptureViewRequest request);
