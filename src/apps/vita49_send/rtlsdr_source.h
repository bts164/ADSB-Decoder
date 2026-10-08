#pragma once

// vita49_send's live source: streams an RTL-SDR's IQ as VRT packets.

#include <cstddef>
#include <cstdint>

#include "apps/vita49_send/packet_sender.h"

#include <coro/coro_stream.h>
#include <coro/sync/watch.h>

namespace vita49_send {

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
};

// A stream of an RTL-SDR's samples as VRT packets, one per batch, until SIGINT or the device stops. Opens and
// tunes the device when first polled; throws std::runtime_error from there if it can't be opened. The
// librtlsdr callback thread encodes each chunk into packets and queues them for the stream. The callback
// never blocks, so when the consumer falls behind it drops packets, counted on `paceTx`
// (PaceStats::overrun_queue_full), rather than stall USB.
coro::CoroStream<PacketBatch> rtlsdr_packet_stream(RtlSdrSourceParams params, coro::WatchSender<PaceStats> paceTx);

}  // namespace vita49_send
