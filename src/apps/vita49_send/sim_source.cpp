#include "apps/vita49_send/sim_source.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>


#include <absl/strings/str_format.h>

#include "apps/vita49_send/pace_stats.h"
#include "apps/vita49_send/packet_batch.h"
#include "common/log.h"
#include "common/trace.h"
#include "net/datagram_batch.h"
#include "vrt/encode.h"

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/io/byte_buffer.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/sleep.h>
#include <coro/sync/timeout.h>
#include <coro/sync/watch.h>
#include <coro/task/spawn_builder.h>

namespace {

using namespace std::chrono;
using namespace std::chrono_literals;

// One step of the simulated stream: rendered ahead of time by sim_generate(), released by sim_packet_stream()
// once due.
struct SimItem {
    std::optional<vrt::Packet> context;  // IF Context packet, sent before the batch
    PacketBatch batch;                 // data packets (one GSO send)
    steady_clock::time_point due;      // wall time of the batch's last sample
};

// Data packets a step of sim_generate() renders: a whole number of batches, one, or with several render threads
// as many as fit in the block the simulator splits between them (see Simulator::block_samples()), so that each
// thread has enough to do per call. The IQ content does not depend on the block size.
size_t render_batches(const adsb_sim::Simulator& sim, const SimStreamParams& params, size_t batch_packets) {
    const size_t batch_samples = batch_packets * params.samples_per_packet;
    return params.sim.render_threads > 1 ? std::max<size_t>(1, sim.block_samples() / batch_samples) : 1;
}

// Queue depth in items, enough for params.queue_s of signal. Items split at context packets hold fewer samples,
// so the real lead is slightly less. A deep queue means every send reads a cache-cold buffer; --queue-ms sets
// the tradeoff.
size_t queue_capacity(const SimStreamParams& params, size_t batch_packets) {
    const double rate = params.sim.sample_rate_hz;
    const auto batch_samples = static_cast<double>(batch_packets * params.samples_per_packet);
    const auto items = static_cast<size_t>(std::max(1.0, std::ceil(params.queue_s * rate / batch_samples)));
    LOGF(INFO, "render queue: %u batches of %u packets (%.1f ms, %.1f MB)", items, batch_packets,
         items * batch_samples / rate * 1e3,
         items * batch_packets * vrt::data_packet_bytes(params.samples_per_packet) / 1e6);
    return items;
}

// sim_generate()'s state, kept across its steps: the stream's settings, how far it has got, and what stamps
// its packets (clock, context sender, packet counter).
struct SimGenerator {
    std::unique_ptr<adsb_sim::Simulator> sim;
    coro::MpscSender<SimItem> tx;
    coro::WatchSender<PaceStats> pace_tx;
    uint32_t stream_id;
    double rate;
    size_t spp;
    size_t pkt_bytes;
    size_t batch_packets;
    size_t render_packets;   // data packets per step
    uint64_t samples_limit;  // UINT64_MAX: no --duration
    steady_clock::time_point t0;
    std::shared_ptr<net::BatchPool> pool;
    vrt::PacketClock clock;
    vrt::ContextSender context;
    uint32_t packet_count = 0;
    uint64_t pos = 0;  // stream position: samples laid out into items plus samples dropped on overrun
    // The step's items and their packets' payloads, in stream order; kept across steps so as not to reallocate.
    std::vector<SimItem> items{};
    std::vector<std::span<int16_t>> payloads{};

    bool done() const { return pos >= samples_limit; }

    // Wall time of the stream's `samples`th sample.
    steady_clock::time_point due_at(uint64_t samples) const {
        return t0 + duration_cast<steady_clock::duration>(duration<double>(static_cast<double>(samples) / rate));
    }

    // Lays the step's packets out into items, in stream order, writing each one's header (timestamps, packet
    // counts, splits at context packets), and lists their payloads for the simulator to fill. The sequential
    // part of a step, but only bookkeeping.
    void layout_step() {
        TRACE_ZONE("sim_layout");
        items.clear();
        payloads.clear();
        // Packets left, rounded up; pos < samples_limit, so this can't overflow (samples_limit may be UINT64_MAX).
        const size_t n = static_cast<size_t>(std::min<uint64_t>(render_packets, 1 + (samples_limit - pos - 1) / spp));
        for (size_t i = 0; i < n; i++) {
            auto ctx_pkt = context.next_if_due(clock);
            // Keep the stream in order: the data packets before a context packet go out in an item of their own.
            if (ctx_pkt || items.empty() || items.back().batch.datagrams.used == batch_packets * pkt_bytes) {
                items.emplace_back().batch.datagrams = pool->acquire();
                if (ctx_pkt) items.back().context = std::move(*ctx_pkt);
            }
            SimItem& item = items.back();
            net::DatagramBatch& datagrams = item.batch.datagrams;
            vrt::write_data_header(datagrams.bytes.data() + datagrams.used, stream_id, packet_count, clock.next(spp),
                                   spp);
            datagrams.used += pkt_bytes;
            item.batch.samples += spp;
            pos += spp;
            item.due = due_at(pos);  // when its last sample is
            packet_count = (packet_count + 1) & 0xF;
        }
        // The payloads, now that the items are all there.
        for (SimItem& item : items) {
            net::DatagramBatch& datagrams = item.batch.datagrams;
            for (size_t off = 0; off < datagrams.used; off += pkt_bytes) {
                payloads.emplace_back(reinterpret_cast<int16_t*>(datagrams.bytes.data() + off + vrt::kHeaderBytes),
                                      2 * spp);
            }
        }
    }

    // Normally the generator is ahead of real time, waiting for the sender to make room in the queue. If it
    // falls behind instead (too slow, or its workers busy), what it renders next would be too late to send:
    // drop whole packets covering the lag. The signal went on meanwhile, so skip the simulator past it (which
    // keeps the IQ at every timestamp independent of pacing, without rendering what is dropped: rendering it
    // would only put the generator further behind) and advance the timestamps and the packet counter past it,
    // so a receiver sees a gap, as it would from a device.
    void drop_overrun() {
        const double late_s = duration<double>(steady_clock::now() - due_at(pos)).count();
        if (late_s <= PaceStats::kOverrunS) return;
        const uint64_t lost_packets =
            std::min<uint64_t>(static_cast<uint64_t>(late_s * rate) / spp, (samples_limit - pos) / spp);
        if (lost_packets == 0) return;
        const uint64_t lost = lost_packets * spp;
        VLOGF(2, "sim: rendering %.1f ms behind real time; skipping %u samples", late_s * 1e3, lost);
        pos += lost;
        sim->skip(lost);
        clock.next(static_cast<size_t>(lost));
        packet_count = static_cast<uint32_t>((packet_count + lost_packets) & 0xF);
        count_dropped(pace_tx, &PaceStats::overrun_render, lost);
    }
};

SimGenerator make_sim_generator(std::unique_ptr<adsb_sim::Simulator> sim, const SimStreamParams& params,
                                size_t batch_packets, coro::MpscSender<SimItem> tx,
                                coro::WatchSender<PaceStats> pace_tx) {
    const double rate = params.sim.sample_rate_hz;
    const size_t pkt_bytes = vrt::data_packet_bytes(params.samples_per_packet);
    const size_t render_packets = batch_packets * render_batches(*sim, params, batch_packets);
    return SimGenerator{
        .sim = std::move(sim),
        .tx = std::move(tx),
        .pace_tx = std::move(pace_tx),
        .stream_id = params.stream_id,
        .rate = rate,
        .spp = params.samples_per_packet,
        .pkt_bytes = pkt_bytes,
        .batch_packets = batch_packets,
        .render_packets = render_packets,
        .samples_limit = params.duration_s > 0 ? static_cast<uint64_t>(params.duration_s * rate) : UINT64_MAX,
        .t0 = steady_clock::now(),
        .pool = net::BatchPool::create(batch_packets * pkt_bytes),
        .clock = vrt::PacketClock{params.sim.sample_rate_hz},
        .context = vrt::ContextSender(params.stream_id, vrt::StreamConfig{rate, params.rf_freq_hz, rate},
                                      params.context_interval_s),
    };
}

// Renders the simulated stream into SimItems, as a task of its own on the runtime, and queues them for
// sim_packet_stream(). The queue's capacity is how far ahead of real time it may get. Returns when the stream
// ends or the receiver is dropped. Knows nothing about Ctrl+C: when sim_packet_stream() is dropped, its handle
// to this task cancels it, which takes effect at its next wait (for queue space, or for the parallel parts
// rendering). Takes the generator by value, so the task's frame owns everything it touches.
coro::Coro<void> sim_generate(SimGenerator gen) {
    while (!gen.done()) {
        gen.layout_step();
        // Straight into the packets' payloads, big-endian as on the wire.
        co_await gen.sim->generate(gen.payloads, std::endian::big);
        VLOGF(2, "sim: rendered %u packets in %u batches, %.1f ms ahead of real time", gen.payloads.size(),
              gen.items.size(), duration<double, std::milli>(gen.due_at(gen.pos) - steady_clock::now()).count());

        for (SimItem& item : gen.items) {
            // Wait for queue space only until the item is kOverrunS late. Past that the consumer is stuck and
            // would drop the item as an overrun anyway, so drop it here and carry on rendering: the generator
            // keeps up with real time through a stall, instead of having the whole gap to render once the
            // consumer is back.
            const uint64_t samples = item.batch.samples;
            const auto deadline =
                time_point_cast<steady_clock::duration>(item.due + duration<double>(PaceStats::kOverrunS));
            auto queued = co_await coro::timeout_at(deadline, gen.tx.send(std::move(item)));
            if (queued.index() == 1) {
                // The item was destroyed with the send future, which put its buffer back in the pool.
                VLOGF(2, "sim: send queue still full %g ms after a batch was due; dropping %u samples",
                      PaceStats::kOverrunS * 1e3, samples);
                count_dropped(gen.pace_tx, &PaceStats::overrun_queue_full, samples);
                continue;
            }
            const bool consumer_gone = !std::get<0>(queued).value.has_value();
            if (consumer_gone) co_return;
        }
        gen.drop_overrun();
    }
    // gen.tx is dropped here, which ends sim_packet_stream() once it has released what is queued.
}

// sim_packet_stream(), once the simulator is built.
coro::CoroStream<PacketBatch> paced_sim_stream(std::unique_ptr<adsb_sim::Simulator> sim, SimStreamParams params,
                                               size_t batch_packets, coro::WatchSender<PaceStats> pace_tx) {
    auto [tx, rx] = coro::mpsc_channel<SimItem>(queue_capacity(params, batch_packets));
    // Dropped with this stream if the consumer stops early (Ctrl+C in main()), which cancels the task.
    auto generator = coro::build_task().name("sim_generate").spawn(
        sim_generate(make_sim_generator(std::move(sim), params, batch_packets, std::move(tx), pace_tx.clone())));

    {
        // Scoped, so that leaving the loop drops the receiver and releases a generator waiting for queue space.
        coro::MpscReceiver<SimItem> queue = std::move(rx);
        while (auto item = co_await coro::next(queue)) {
            // Hold each batch back until the wall clock reaches its last sample. Coro timers bound the
            // IoDriver's epoll_pwait2 at nanosecond resolution, fine enough for a batch. Already due (the
            // consumer is behind): ready at once, no timer queued.
            co_await coro::sleep_until(item->due);
            const bool too_late = duration<double>(steady_clock::now() - item->due).count() > PaceStats::kOverrunS;
            // Context packets are tiny and repeated; they go out even when late.
            if (item->context) co_yield single_packet_batch(std::move(*item->context));
            if (too_late) {
                // The consumer was stalled that long: drop the batch, like a FIFO overrun. Its buffer goes
                // back to the pool as the item is destroyed.
                VLOGF(2, "sim: sender asked for a batch more than %g ms after it was due; dropping %u samples",
                      PaceStats::kOverrunS * 1e3, item->batch.samples);
                count_dropped(pace_tx, &PaceStats::overrun_late_send, item->batch.samples);
                continue;
            }
            co_yield std::move(item->batch);
        }
    }
    co_await generator;
}

}  // namespace

void SimStreamParams::add_options(argparse::ArgumentParser& cmd) {
    cmd.add_description("Stream simulated ADS-B traffic generated in-process.");
    cmd.add_argument("--scenario").required().help(
        "JSON file describing the simulated aircraft (see scenarios/boston.json)");
    cmd.add_argument("--duration").scan<'g', double>().default_value(0.0).help(
        "stop after this many seconds of signal (0 = run until Ctrl+C)");
    cmd.add_argument("--seed").scan<'u', uint64_t>().default_value(uint64_t{1}).help(
        "RNG seed for the noise, initial message timing and carrier phases; same seed, same IQ");
    cmd.add_argument("--snr-db").scan<'g', double>().default_value(30.0).help(
        "pulse power of a full-amplitude aircraft over AWGN power, per complex sample, in dB");
    cmd.add_argument("--pulse-rise-ns").scan<'g', double>().default_value(75.0).help(
        "transmit pulse rise time, 0-100% of a linear edge, in ns");
    cmd.add_argument("--pulse-fall-ns").scan<'g', double>().default_value(100.0).help(
        "transmit pulse fall time in ns");
    cmd.add_argument("--pulse-jitter-ns").scan<'g', double>().default_value(25.0).help(
        "every pulse edge is displaced by a uniform random error of up to +/- this many ns (0 = exact)");
    cmd.add_argument("--rx-cutoff").scan<'g', double>().default_value(0.45).help(
        "receiver anti-alias filter -6 dB cutoff as a fraction of the sample rate (<= 0.5); only "
        "applied at sample rates below 16 MHz, where the signal is rendered oversampled and decimated");
    cmd.add_argument("--oversample").scan<'u', unsigned>().default_value(0u).help(
        "render at this integer multiple of the sample rate before filtering (0 = automatic)");
    cmd.add_argument("--render-threads").scan<'u', unsigned>().default_value(unsigned(coro::worker_count())).help(
        "parallel parts the signal is rendered in, on the runtime's workers. add more when the log reports samples "
        "dropped as \"render too slow\". The signal is the same whatever the count");
    cmd.add_argument("--queue-ms").scan<'g', double>().default_value(20.0).help(
        "how far ahead of real time the signal is rendered, in ms: packets queued for sending, so the sender "
        "catches up after a stall at send-only speed. Costs rate * 4 B per second of queue (80 MB per 100 ms at "
        "200 Msps)");
}

SimStreamParams SimStreamParams::from_options(const argparse::ArgumentParser& program,
                                              const argparse::ArgumentParser& cmd) {
    SimStreamParams params{};
    params.sim.sample_rate_hz = static_cast<uint32_t>(program.get<double>("--rate"));
    params.sim.seed = cmd.get<uint64_t>("--seed");
    params.sim.snr_db = cmd.get<double>("--snr-db");
    params.sim.pulse_rise_s = cmd.get<double>("--pulse-rise-ns") * 1e-9;
    params.sim.pulse_fall_s = cmd.get<double>("--pulse-fall-ns") * 1e-9;
    params.sim.pulse_jitter_s = cmd.get<double>("--pulse-jitter-ns") * 1e-9;
    params.sim.rx_cutoff = cmd.get<double>("--rx-cutoff");
    params.sim.oversample = cmd.get<unsigned>("--oversample");
    params.sim.render_threads = cmd.get<unsigned>("--render-threads");
    params.scenario = cmd.get<std::string>("--scenario");
    params.sim.aircraft = adsb_sim::load_scenario(params.scenario);
    params.duration_s = cmd.get<double>("--duration");
    params.queue_s = cmd.get<double>("--queue-ms") * 1e-3;
    if (!(params.queue_s > 0)) throw std::invalid_argument("--queue-ms must be positive");
    params.samples_per_packet = program.get<unsigned>("--samples-per-packet");
    params.stream_id = program.get<unsigned>("--stream-id");
    params.rf_freq_hz = program.get<double>("--freq");
    params.context_interval_s = program.get<double>("--context-interval");
    return params;
}

coro::CoroStream<PacketBatch> sim_packet_stream(SimStreamParams params, size_t batch_packets,
                                                coro::WatchSender<PaceStats> pace_tx) {
    // Built here rather than in the stream, so that a bad config throws from this call.
    auto sim = std::make_unique<adsb_sim::Simulator>(params.sim);
    const adsb_sim::Config& cfg = params.sim;
    LOGF(INFO, "simulating ADS-B from %s (%u aircraft): seed %u, snr %g dB, pulse rise/fall/jitter %g/%g/%g ns, "
         "rendered at %.1f MHz (oversample %u%s), %s, %u render threads, queue %g ms, %s",
         params.scenario, cfg.aircraft.size(), cfg.seed, cfg.snr_db, cfg.pulse_rise_s * 1e9, cfg.pulse_fall_s * 1e9,
         cfg.pulse_jitter_s * 1e9, sim->oversample() * cfg.sample_rate_hz / 1e6, sim->oversample(),
         cfg.oversample ? "" : ", auto",
         sim->rx_filter_taps() ? absl::StrFormat("rx filter %u taps, cutoff %g of the sample rate",
                                                 sim->rx_filter_taps(), cfg.rx_cutoff)
                               : std::string("no rx filter (rendered at the sample rate)"),
         cfg.render_threads, params.queue_s * 1e3,
         params.duration_s > 0 ? absl::StrFormat("for %g s", params.duration_s) : "until Ctrl+C");
    for (const auto& a : cfg.aircraft) {
        LOGF(INFO, "  icao=%06x callsign=%.8s lat=%.4f lon=%.4f alt=%dft gs=%.0fkt trk=%.0f vr=%dfpm", a.icao,
             a.callsign, a.lat_deg, a.lon_deg, a.alt_ft, a.ground_speed_kt, a.track_deg, a.vrate_fpm);
    }
    return paced_sim_stream(std::move(sim), std::move(params), batch_packets, std::move(pace_tx));
}
