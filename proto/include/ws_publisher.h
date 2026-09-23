#pragma once

#include <cstdint>
#include <string>

// Background WebSocket publisher singleton: ws_publisher_init() spawns a
// dedicated OS thread running its own single-threaded coro::Runtime (kept
// separate from the main pipeline's runtime/threads so publishing a frame
// never blocks demod/decode), binds a WsListener on that thread, and runs a
// broadcast loop that fans out each ws_publisher_publish()'d JSON line to
// every currently-connected client. A single coroutine alternates between
// accepting new connections and draining the broadcast channel via
// coro::select -- since the underlying Runtime is single-threaded, that
// coroutine is the only thing that ever touches its connection set, so no
// locking is needed around it.
//
// Not initializing (ws_publisher_init() never called) is a supported,
// expected state -- ws_publisher_publish() is then simply a no-op, so
// callers don't need to guard every call site on whether publishing is
// enabled.
//
// Caveat (file mode only): a client that connects after file processing has
// finished and drained the queue misses that backlog entirely -- select()
// resolves whichever branch is ready first, so a buffered recv() always wins
// over a still-pending accept(). Not a concern for --rtlsdr mode, which runs
// until Ctrl+C.
void ws_publisher_init(uint16_t port);
void ws_publisher_publish(std::string json_line);
