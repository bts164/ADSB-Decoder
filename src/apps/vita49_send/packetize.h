#pragma once

// vita49_send's packet stage for every source but the sim: resamples stitched IQ blocks straight into the
// payloads of VRT packets, in GSO batches. See doc/design/source_sink.md.

#include <cstddef>
#include <cstdint>
#include <optional>

#include <argparse/argparse.hpp>

#include "apps/vita49_send/pace_stats.h"
#include "apps/vita49_send/packet_batch.h"
#include "dsp/filter_bank.h"
#include "dsp/iq_block.h"
#include "dsp/iq_format.h"
#include "dsp/iq_resampler.h"
#include "vrt/format.h"

#include <coro/coro_stream.h>
#include <coro/sync/watch.h>

// The top-level --stream-* options: the stream's rate, its resampling filter and its wire format, and the
// packet size. The sim takes only the wire-format ones: it renders int16 at its own rate.
struct StreamOptions {
    // Packets that fit a 1500-byte MTU: 1460 bytes of VRT, as doc/design/vita49-format.md's "UDP framing" works
    // out.
    static constexpr size_t kI16SamplesPerPacket = 360;
    static constexpr size_t kF32SamplesPerPacket = 180;

    std::optional<double> rate_hz;  // nullopt: the source's rate, not filtered
    std::optional<int> taps;        // nullopt: from the rate ratio
    int num_phases;
    std::optional<double> cutoff_hz;  // nullopt: 0.4 of the stream rate
    FilterWindow window;
    double kaiser_beta;
    iq::OutputFormat format;
    bool payload_format;  // send the context's Data Payload Format field
    size_t samples_per_packet;

    static void add_options(argparse::ArgumentParser& program);
    // Throws std::invalid_argument for a bad option, a filter option without --stream-rate, or a format but ci16_be
    // with --no-payload-format, whose receiver couldn't tell the format.
    static StreamOptions from_options(const argparse::ArgumentParser& program);
    // Whether any option the sim can't take is given: the rate, the filter, or a format other than ci16_be.
    static bool resampling_options_used(const argparse::ArgumentParser& program);

    // The resampler from a source at `input_rate_hz` to the stream, logged. Throws std::invalid_argument for a
    // rate that isn't a positive whole number of Hz (vrt::PacketClock counts in whole Hz), or a filter that
    // can't be designed.
    IqResampler make_resampler(double input_rate_hz) const;
};

struct PacketizeParams {
    uint32_t stream_id;
    size_t samples_per_packet;
    size_t batch_packets;  // data packets per GSO batch, from net::enable_gso()
    double rf_freq_hz;     // the source's, advertised in the context packets
    double context_interval_s;
    bool payload_format;  // send the context's Data Payload Format field
};

// Resamples `blocks` (from stitch(), padded to resampler.padding()) into batches of VRT data packets at the
// resampler's output rate and format, with an IF Context packet in a batch of its own whenever one is due. A
// gap in the blocks' samples ends the packet being filled short, moves the timestamps and packet counter past
// it, and is counted on `pace_tx` as PaceStats::overrun_gap, as are the outputs that saturated, in
// PaceStats::clipped. A task of its own, a few batches ahead of the consumer, so that resampling overlaps the
// sends. Ends with `blocks`, after the part-filled batch.
coro::CoroStream<PacketBatch> packetize(coro::CoroStream<IqBlock> blocks, PacketizeParams params,
                                        IqResampler resampler, coro::WatchSender<PaceStats> pace_tx);
