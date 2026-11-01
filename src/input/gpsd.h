#pragma once

#include <cstdint>
#include <string>

#include <coro/coro.h>
#include <coro/sync/watch.h>

#include "api/convert.h"

/**
 * @file
 * The receiver's position, from `gpsd`: what `adsb sidekiq` publishes on the position watch. See
 * doc/design/gps.md, "The receiver's position".
 */

/** Where `gpsd` listens. */
struct GpsdAddress {
    std::string host;
    uint16_t port;
};

/**
 * The position of a receiver with no GPS, in the state NO_GPS, stamped now.
 * @param summary why there's none, such as "no GPS: the file input has none"
 */
api::ReceiverPosition receiver_without_gps(std::string summary);

/** The position before the first connection to `gpsd` at `address`: GPSD_UNREACHABLE, "connecting", stamped now. */
api::ReceiverPosition gpsd_connecting(const GpsdAddress& address);

/**
 * Publishes the receiver's position from `gpsd` on `position` until cancelled.
 *
 * Connects to `address`, asks for its JSON reports, and sets the watch from each `TPV` (the fix) and `SKY` (the
 * satellites), stamped with the system clock, as the aircraft updates are. A value is sent when anything but its
 * time changes, so with a fix about once a second. While `gpsd` doesn't answer, or goes silent for 10 s, the
 * state is GPSD_UNREACHABLE, and it reconnects every 5 s.
 *
 * Logs at INFO the address at the start, connecting, losing `gpsd`, the first fix, each change of fix, and a move
 * of more than 50 m. Logs a WARNING when there's no fix 2 minutes after starting or after losing one, saying what's
 * missing, and when the system clock is more than a second off GPS time.
 * @param address where `gpsd` listens
 * @param position the watch to set; its value until the first connection is gpsd_connecting()
 */
[[nodiscard]] coro::Coro<void> watch_gpsd(GpsdAddress address, coro::WatchSender<api::ReceiverPosition> position);
