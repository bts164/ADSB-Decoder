# UDP sending helpers on top of coro::UdpSocket: GSO batches and the pool that recycles their buffers.
add_library(adsb_net STATIC gso.cpp)
target_link_libraries(adsb_net PUBLIC adsb_common coro::coro xtensor)
