# Loopback UDP microbenchmark for coro::UdpSocket.
add_executable(udp_bench udp_bench.cpp)
target_link_libraries(udp_bench PRIVATE coro::coro argparse::argparse Threads::Threads)
