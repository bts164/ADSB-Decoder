#pragma once

// What vita49_send's sources produce and send_loop() sends: a batch of VRT packets, and how many I/Q samples
// it carries.

#include <cstdint>
#include <utility>

#include "net/datagram_batch.h"
#include "vrt/encode.h"

struct PacketBatch {
    net::DatagramBatch datagrams;  // the packets, one datagram each, sent with one send
    uint64_t samples = 0;          // I/Q samples in the batch, for the statistics; 0 for a context packet
};

// A batch of one packet that isn't sample data (an IF Context packet). Its size differs from the data
// packets', so it can't share a GSO batch with them.
inline PacketBatch single_packet_batch(vrt::Packet pkt) {
    return PacketBatch{.datagrams = net::DatagramBatch(std::move(pkt.bytes))};
}
