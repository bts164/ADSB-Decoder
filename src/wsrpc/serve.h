#pragma once

#include <chrono>
#include <cstddef>
#include <vector>

#include <coro/coro.h>
#include <coro/io/ws_stream.h>

#include "wsrpc/service.h"

namespace wsrpc {

/** One connection's limits. */
struct Limits {
    size_t client_calls = 16;  // calls in progress at once; one more is refused with RESOURCE_EXHAUSTED
    // A client stream's requests waiting for its handler. One more fails the call, once it has handled these.
    size_t call_input_queue = 16;
    size_t write_queue = 16;  // serialized frames waiting for the connection's writer, from all its calls
    // A send that takes longer closes the connection. Long enough never to fire on a client that's just slow:
    // a frame isn't dropped instead, since a dropped `end` would leave the client waiting forever.
    std::chrono::seconds send_timeout{20};
};

/**
 * Serves one client on `ws`, until it disconnects or a send times out: answers its hello with the names of
 * `services`, and runs each call in a task of its own. The services must outlive this.
 */
coro::Coro<void> serve(coro::WsStream ws, std::vector<Service*> services, Limits limits = {});

}  // namespace wsrpc
