#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <coro/coro_stream.h>

#include "dsp/iq_block.h"

namespace argparse {
class ArgumentParser;
}

/**
 * @file
 * `adsb sidekiq` source: streams from an Epiq Sidekiq card, such as the Matchstiq X40's. See
 * doc/design/sidekiq_source.md.
 *
 * libsidekiq is loaded at run time with dlopen(), not linked: libsidekiq.so brings its own gRPC and protobuf,
 * whose symbols would clash with the protobuf linked into adsb. Loaded with RTLD_LOCAL, it resolves them from
 * the copies beside it, and adsb uses only its C API. Only the SDK's headers are needed to build.
 */

/** A receive handle. A1 and A2 are the two the X40's FPGA decimates on. */
enum class SidekiqHandle { A1, A2 };

/** `adsb sidekiq`'s options: card selection and tuning. */
struct SidekiqStreamParams {
    /** Block size for sidekiq_iq_stream(), the same as FileStreamParams::kChunkSamples. */
    static constexpr size_t kChunkSamples = 128 * 1024;

    std::string lib_path;            ///< libsidekiq.so to load
    int card;
    SidekiqHandle handle;
    std::optional<std::string> port; ///< RF port, such as "J2"; unset for libsidekiq's choice
    double rate_hz;
    double bandwidth_hz;             ///< RF bandwidth
    double freq_hz;
    std::optional<int> gain_index;   ///< manual gain index; unset for the radio's AGC
    /// preselect filter, by its passband in MHz such as "810-1170"; unset for libsidekiq's choice
    std::optional<std::string> preselect_filter;

    static void add_options(argparse::ArgumentParser& cmd);
    /** @throws std::runtime_error if --handle or --gain isn't one of the values they take */
    static SidekiqStreamParams from_options(const argparse::ArgumentParser& program,
                                           const argparse::ArgumentParser& cmd);
};

/**
 * Streams IqBuffers from a Sidekiq card until the stream is dropped.
 *
 * A spawn_blocking thread loads libsidekiq, initializes the card, and sets I/Q order, RF port, rate and bandwidth,
 * frequency, preselect filter, gain and a receive timeout, then logs the settings in effect and the front end's
 * state. A bandwidth or filter that differs from the one asked for is logged as a warning, and so is a rate within
 * 100 ppm of it; the samples are labeled with `params.rate_hz` either way. libsidekiq's own log messages go to the
 * log, and a critical error it reports is logged before the program exits, which is what libsidekiq does by
 * default.
 *
 * The thread then reads, gathering the packets, about a thousand samples each, into guard-padded blocks of
 * `chunk_samples` pushed into a small channel the stream drains. Samples the radio lost, which show as a jump in the
 * packets' RF timestamps, are filled with zeros, so stream time keeps pace with the radio's. If the demod falls behind
 * and the channel fills, blocks are dropped rather than stalling the read, which would make the radio overrun. Dropping
 * the stream cancels the read; the thread then stops streaming and releases the card with skiq_exit(), so it is free
 * for the next run on every exit path.
 * @param params the card and its tuning
 * @param pad guard padding to allocate around each block (demod_padding())
 * @param chunk_samples real samples per block
 * @throws std::runtime_error from the stream, after its last block, if libsidekiq can't be loaded, the card can't
 *         be initialized, the handle has no such port or preselect filter, a setting is rejected, the rate is more
 *         than 100 ppm from the one asked for, the card sends its test counter instead of samples, or receiving
 *         fails
 */
coro::CoroStream<IqBuffer> sidekiq_iq_stream(SidekiqStreamParams params, IqPadding pad, size_t chunk_samples);
