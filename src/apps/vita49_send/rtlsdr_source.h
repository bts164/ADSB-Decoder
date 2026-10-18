#pragma once

// vita49_send's live source: streams an RTL-SDR's IQ as VRT packets.

#include <cstddef>
#include <cstdint>

#include <argparse/argparse.hpp>

#include "apps/vita49_send/pace_stats.h"
#include "apps/vita49_send/packet_batch.h"

#include <coro/coro_stream.h>
#include <coro/sync/watch.h>

struct RtlSdrSourceParams {
    int device_index;
    double rate_hz;
    double freq_hz;
    double gain_db;  // < 0: auto gain
    unsigned buf_num;  // librtlsdr async transfer buffers; 0 = its default
    unsigned buf_len;  // bytes per transfer buffer; 0 = its default
    size_t samples_per_packet;
    uint32_t stream_id;
    double context_interval_s;

    // The rtlsdr subcommand's options: device, gain and transfer buffers.
    static void add_options(argparse::ArgumentParser& cmd);
    // `program` is the top-level parser, for the options every source shares; `cmd` is the rtlsdr subcommand's.
    static RtlSdrSourceParams from_options(const argparse::ArgumentParser& program,
                                           const argparse::ArgumentParser& cmd);
};

// A stream of an RTL-SDR's samples as VRT packets, one per batch, until it is dropped. Opens and
// tunes the device when first polled; throws std::runtime_error from there if it can't be opened. The
// librtlsdr callback thread encodes each chunk into packets and queues them for the stream. The callback
// never blocks, so when the consumer falls behind it drops packets, counted on `pace_tx`
// (PaceStats::overrun_queue_full), rather than stall USB.
coro::CoroStream<PacketBatch> rtlsdr_packet_stream(RtlSdrSourceParams params, coro::WatchSender<PaceStats> pace_tx);
