#pragma once

// UDP GSO (generic segmentation offload): one send of a buffer of back-to-back datagrams, which the kernel
// splits into one datagram per segment. The kernel's per-datagram work, not the syscall, is what limits a
// small-datagram sender, and GSO does that work once per send.

#include <cstddef>

#include <coro/io/udp_socket.h>

namespace net {

// One send carries at most a maximal UDP payload and 64 segments (the kernel's UDP_MAX_SEGMENTS; 128 on newer
// kernels).
constexpr size_t kMaxGsoBytes = 65507;
constexpr size_t kMaxGsoSegments = 64;

// Turns on UDP GSO for `sock` with segments of `datagram_bytes` and returns how many datagrams of that size
// may go in one send; 1, with a warning, if GSO is unavailable.
size_t enable_gso(coro::UdpSocket& sock, size_t datagram_bytes);

}  // namespace net
