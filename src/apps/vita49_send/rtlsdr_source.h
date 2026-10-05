#pragma once

// vita49_send's live source: streams an RTL-SDR's IQ as VRT packets.

#include <cstddef>
#include <cstdint>

#include <coro/coro.h>
#include <coro/io/udp_socket.h>

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

// Opens and tunes the device, then streams its samples to `sock` (already connected) until SIGINT or the
// device stops. The librtlsdr callback thread encodes each chunk into VRT packets and hands them to this task
// through a channel; the callback never blocks, so when the send side falls behind it drops packets (and says
// so) rather than stall USB. Throws std::runtime_error if the device can't be opened.
coro::Coro<void> rtlsdr_send_loop(coro::UdpSocket sock, RtlSdrSourceParams params);

}  // namespace vita49_send
