#include "net/gso.h"

#include <algorithm>
#include <system_error>

#include "common/log.h"

namespace net {

size_t enable_gso(coro::UdpSocket& sock, size_t datagram_bytes) {
    const size_t batch_datagrams = std::clamp<size_t>(kMaxGsoBytes / datagram_bytes, 1, kMaxGsoSegments);
    if (batch_datagrams == 1) {
        LOGF(INFO, "UDP GSO not used: a %u-byte datagram leaves no room for a second in one send", datagram_bytes);
        return 1;
    }
    try {
        sock.set_segment_size(datagram_bytes);
    } catch (const std::system_error& err) {
        LOGF(WARNING, "UDP GSO unavailable (%s), sending one datagram per send", err.what());
        return 1;
    }
    LOGF(INFO, "UDP GSO on: up to %u datagrams of %u bytes per send", batch_datagrams, datagram_bytes);
    return batch_datagrams;
}

}  // namespace net
