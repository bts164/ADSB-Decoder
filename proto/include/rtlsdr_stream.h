#pragma once

#include <cstdint>

#include <coro/coro_stream.h>

#include "iq_block.h"

// --- --rtlsdr streaming mode -------------------------------------------
//
// Constructs a coro::CoroStream<IqBlock> source off rtl-sdr device
// `device_index`: librtlsdr's own event-handling thread (inside
// rtlsdr_read_async, which blocks until the device is closed -- see
// RtlSdr's SIGINT-triggered close() in rtlsdr_stream.cpp -- or a read
// error) delivers raw uint8 I/Q buffers via callback; the callback converts
// each buffer to a padded complex<float> block (same guard-padding contract
// exec() already requires for whole-file mode, see demod.h) and pushes it
// into an mpsc channel this generator drains and re-yields.
//
// The caller decides what to do with the resulting stream -- typically
// handing it to run_demod_loop() (pipeline.h) on a coro::spawn_blocking
// thread, same as any other Stream<IqBlock> source (see main.cpp).
coro::CoroStream<IqBlock> rtlsdr_iq_stream(int device_index, double rate_hz, double freq_hz, double gain_db,
                                            int filter_taps, uint32_t buf_num, uint32_t buf_len);
