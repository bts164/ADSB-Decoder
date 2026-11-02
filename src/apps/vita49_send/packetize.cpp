#include "apps/vita49_send/packetize.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "common/log.h"
#include "common/trace.h"
#include "net/datagram_batch.h"
#include "vrt/encode.h"

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/runtime/runtime.h>
#include <coro/task/parallel.h>
#include <coro/task/spawn_builder.h>

namespace {

// Batches packetize() may get ahead of its consumer: two blocks' worth at 50 Msps with no resampling.
constexpr size_t kBufferBatches = 16;

// The filter options that need --stream-rate.
constexpr const char* kFilterOptions[] = {"--stream-taps", "--stream-phases", "--stream-cutoff", "--stream-window",
                                          "--stream-kaiser-beta"};

// `hz` as a whole number of Hz, for vrt::PacketClock and IqResampler.
uint32_t whole_hz(double hz, const char* what) {
    if (!(hz > 0) || hz > UINT32_MAX || hz != std::round(hz))
        throw std::invalid_argument(std::string(what) + " must be a positive whole number of Hz, up to 4.29 GHz");
    return static_cast<uint32_t>(hz);
}

// One run of outputs to write: a packet's payload, or the part of it a block produces.
struct Span {
    uint8_t* dst;
    size_t bytes;
    uint64_t first;  // the stream-rate output it starts at
};

// packetize()'s state, kept across blocks: the packet being filled, the batch it is in, and what stamps the
// packets. For each block, lay_out() writes the headers of the packets its outputs go in and lists their
// payload spans, fill() resamples into those, and then the batches that are complete are in ready().
class Packetizer {
public:
    Packetizer(const PacketizeParams& params, IqResampler resampler, coro::WatchSender<PaceStats> pace_tx)
        : m_params(params),
          m_resampler(std::move(resampler)),
          m_pace_tx(std::move(pace_tx)),
          m_format(m_resampler.format()),
          m_batch_bytes(params.batch_packets * vrt::data_packet_bytes(params.samples_per_packet, m_format)),
          m_pool(net::BatchPool::create(m_batch_bytes)),
          m_clock{m_resampler.output_rate_hz()},
          m_context(params.stream_id, context_config(params, m_resampler, m_format), params.context_interval_s) {}

    void lay_out(const IqBlock& block) {
        m_spans.clear();
        const IqResampler::Range range = m_resampler.outputs(block);
        if (!m_started) {
            LOGF(INFO, "stream: the source's blocks are %s, sent as %s",
                 block.visit([]<class F>(IqView<F>) { return F::sigmf; }), iq::sigmf_name(m_format));
            m_started = true;
            m_next_output = range.first;
        }
        if (range.first < m_next_output) throw std::runtime_error("packetize: the source's blocks overlap");
        if (range.first > m_next_output) skip(range.first - m_next_output);
        const size_t spp = m_params.samples_per_packet;
        for (uint64_t k = range.first; k < range.end;) {
            if (m_filled == 0) open_packet();
            const size_t take = static_cast<size_t>(std::min<uint64_t>(spp - m_filled, range.end - k));
            m_spans.push_back({packet() + vrt::kHeaderBytes + m_filled * m_resampler.sample_bytes(),
                               take * m_resampler.sample_bytes(), k});
            m_clock.next(take);
            m_filled += take;
            k += take;
            if (m_filled == spp) close_packet();
        }
        m_next_output = range.end;
    }

    coro::Coro<void> fill(const IqBlock& block) {
        if (m_spans.empty()) co_return;
        const size_t k = std::min(coro::worker_count(), m_spans.size());
        std::vector<size_t> clipped(k, 0);
        co_await coro::parallel(k, [&](coro::TeamMember part) -> coro::Coro<void> {
            TRACE_ZONE("resample_iq");
            const size_t n = m_spans.size();
            size_t part_clipped = 0;
            for (size_t i = part.rank() * n / part.size(); i < (part.rank() + 1) * n / part.size(); i++) {
                const Span& s = m_spans[i];
                part_clipped += m_resampler.run(block, s.first, {s.dst, s.bytes});
            }
            clipped[part.rank()] = part_clipped;
            co_return;
        });
        const size_t block_clipped = std::accumulate(clipped.begin(), clipped.end(), size_t{0});
        if (block_clipped > 0) count_clipped(block_clipped);
    }

    // At the end of the stream: the part-filled packet and batch are complete as they are.
    void finish() {
        if (m_filled > 0) close_packet();
        close_batch();
    }

    // Each batch complete since the caller last cleared it, in stream order.
    std::vector<PacketBatch>& ready() { return m_ready; }

private:
    static vrt::StreamConfig context_config(const PacketizeParams& params, const IqResampler& resampler,
                                            const iq::OutputFormat& format) {
        const double rate = resampler.output_rate_hz();
        return {
            .sample_rate_hz = rate,
            .rf_freq_hz = params.rf_freq_hz,
            .bandwidth_hz = resampler.resamples() ? 2 * resampler.filter().cutoff_hz : rate,
            .payload_format = params.payload_format ? std::optional(format) : std::nullopt,
        };
    }

    // The packet being filled.
    uint8_t* packet() { return m_batch->datagrams.bytes.data() + m_batch->datagrams.used; }

    void open_packet() {
        if (auto ctx = m_context.next_if_due(m_clock)) {
            close_batch();
            m_ready.push_back(single_packet_batch(std::move(*ctx)));
        }
        if (!m_batch) m_batch.emplace(PacketBatch{.datagrams = m_pool->acquire()});
        m_packet_ts = m_clock.next(0);
        vrt::write_data_header(packet(), m_params.stream_id, m_packet_count, m_packet_ts, m_params.samples_per_packet,
                               m_format);
    }

    // A packet that isn't full is rewritten as a short one, and ends its batch: GSO allows only the last datagram
    // of a send to be short.
    void close_packet() {
        const bool is_short = m_filled < m_params.samples_per_packet;
        if (is_short)
            vrt::write_data_header(packet(), m_params.stream_id, m_packet_count, m_packet_ts, m_filled, m_format);
        m_batch->datagrams.used += vrt::data_packet_bytes(m_filled, m_format);
        m_batch->samples += m_filled;
        m_packet_count = (m_packet_count + 1) & 0xF;
        m_filled = 0;
        if (is_short || m_batch->datagrams.used == m_batch_bytes) close_batch();
    }

    void close_batch() {
        if (!m_batch || m_batch->datagrams.used == 0) return;
        m_ready.push_back(std::move(*m_batch));
        m_batch.reset();
    }

    // The source lost `gap` outputs' worth of samples: moves the timestamps and the packet counter past them,
    // so that a receiver sees the gap.
    void skip(uint64_t gap) {
        if (m_filled > 0) close_packet();
        VLOGF(2, "stream: %u samples missing from the source's stream", gap);
        m_clock.next(static_cast<size_t>(gap));
        const uint64_t spp = m_params.samples_per_packet;
        m_packet_count = static_cast<uint32_t>((m_packet_count + (gap + spp - 1) / spp) & 0xF);
        count_dropped(m_pace_tx, &PaceStats::overrun_gap, gap);
    }

    void count_clipped(size_t clipped) {
        m_clipped += clipped;
        m_pace_tx.send_if_modified([&](PaceStats& stats) {
            stats.clipped = m_clipped;
            return true;
        });
        TRACE_PLOT("samples clipped", static_cast<int64_t>(m_clipped));
        LOGF_EVERY_N_SEC(WARNING, 1, "stream: %u samples saturated as %s (%u so far): the source is near full scale",
                         clipped, iq::sigmf_name(m_format), m_clipped);
    }

    PacketizeParams m_params;
    IqResampler m_resampler;
    coro::WatchSender<PaceStats> m_pace_tx;
    iq::OutputFormat m_format;
    size_t m_batch_bytes;
    std::shared_ptr<net::BatchPool> m_pool;
    vrt::PacketClock m_clock;
    vrt::ContextSender m_context;
    bool m_started = false;
    uint64_t m_next_output = 0;  // the stream-rate output the next block should start at
    std::optional<PacketBatch> m_batch;  // the batch being filled
    size_t m_filled = 0;                 // outputs in the packet being filled; 0: no packet open
    vrt::Timestamp m_packet_ts{};
    uint32_t m_packet_count = 0;
    uint64_t m_clipped = 0;
    std::vector<Span> m_spans;
    std::vector<PacketBatch> m_ready;
};

coro::CoroStream<PacketBatch> packetize_blocks(coro::CoroStream<IqBlock> blocks, Packetizer packetizer) {
    while (auto block = co_await coro::next(blocks)) {
        packetizer.lay_out(*block);
        co_await packetizer.fill(*block);
        for (PacketBatch& batch : packetizer.ready()) co_yield std::move(batch);
        packetizer.ready().clear();
    }
    packetizer.finish();
    for (PacketBatch& batch : packetizer.ready()) co_yield std::move(batch);
}

}  // namespace

void StreamOptions::add_options(argparse::ArgumentParser& program) {
    program.add_argument("--stream-rate").scan<'g', double>().help(
        "stream at this sample rate in Hz, a whole number, resampled from the source's through a low-pass "
        "anti-alias filter (default: the source's rate, not filtered). Not for the sim source");
    program.add_argument("--stream-taps").scan<'i', int>().help(
        "with --stream-rate: the filter's taps per phase (default: 20 x the rate ratio, at least 16)");
    program.add_argument("--stream-phases").scan<'i', int>().default_value(64).help(
        "with --stream-rate: the filter's phases (sub-sample positions)");
    program.add_argument("--stream-cutoff").scan<'g', double>().help(
        "with --stream-rate: the filter's low-pass cutoff in Hz, clamped to the stream's Nyquist frequency "
        "(default: 0.4 x --stream-rate)");
    program.add_argument("--stream-window").default_value(std::string("kaiser")).help(
        "with --stream-rate: the filter's window, hann, hamming, blackman or kaiser");
    program.add_argument("--stream-kaiser-beta").scan<'g', double>().default_value(8.0).help(
        "with --stream-rate: the Kaiser window's beta");
    program.add_argument("--stream-format").default_value(std::string(iq::I16Be::sigmf)).help(
        "the samples' wire format, as SigMF names it: ci16_be (16-bit signed, full scale at the source's full "
        "scale) or cf32_be (32-bit IEEE float, full scale 1.0; not for the sim source)");
    program.add_argument("--no-payload-format").flag().help(
        "leave the Data Payload Format field out of the IF Context packets, for a receiver that doesn't take it; "
        "only with --stream-format ci16_be");
    program.add_argument("--samples-per-packet").scan<'u', unsigned>().help(
        "IQ sample pairs per VRT packet (default: 360 for ci16_be, 180 for cf32_be, to keep the packet under one "
        "Ethernet MTU -- see doc/design/vita49-format.md's \"UDP framing\" section)");
}

StreamOptions StreamOptions::from_options(const argparse::ArgumentParser& program) {
    const auto format_option = program.get<std::string>("--stream-format");
    const std::optional<iq::OutputFormat> parsed = iq::OutputFormats::from_sigmf(format_option);
    if (!parsed)
        throw std::invalid_argument("unknown --stream-format '" + format_option + "' (" +
                                    iq::OutputFormats::sigmf_names() + ")");
    const iq::OutputFormat format = *parsed;
    const bool payload_format = !program.get<bool>("--no-payload-format");
    if (!payload_format && !std::holds_alternative<iq::I16Be>(format))
        throw std::invalid_argument("--no-payload-format goes only with --stream-format ci16_be: without the field a "
                                    "receiver would read " + format_option + " samples as int16");
    if (!program.is_used("--stream-rate")) {
        for (const char* name : kFilterOptions)
            if (program.is_used(name)) throw std::invalid_argument(std::string(name) + " needs --stream-rate");
    }
    const auto samples_per_packet = program.present<unsigned>("--samples-per-packet");
    if (samples_per_packet == 0u) throw std::invalid_argument("--samples-per-packet must be positive");
    return {
        .rate_hz = program.present<double>("--stream-rate"),
        .taps = program.present<int>("--stream-taps"),
        .num_phases = program.get<int>("--stream-phases"),
        .cutoff_hz = program.present<double>("--stream-cutoff"),
        .window = parse_filter_window(program.get<std::string>("--stream-window")),
        .kaiser_beta = program.get<double>("--stream-kaiser-beta"),
        .format = format,
        .payload_format = payload_format,
        .samples_per_packet = samples_per_packet.value_or(
            std::holds_alternative<iq::F32Be>(format) ? kF32SamplesPerPacket : kI16SamplesPerPacket),
    };
}

bool StreamOptions::resampling_options_used(const argparse::ArgumentParser& program) {
    return program.is_used("--stream-rate") ||
           std::any_of(std::begin(kFilterOptions), std::end(kFilterOptions),
                       [&](const char* name) { return program.is_used(name); }) ||
           program.get<std::string>("--stream-format") != iq::I16Be::sigmf;
}

IqResampler StreamOptions::make_resampler(double input_rate_hz) const {
    const uint32_t in = whole_hz(input_rate_hz, "the source's sample rate");
    if (!rate_hz) {
        LOGF(INFO, "stream: %.3f Msps as %s, the source's rate, not filtered", in / 1e6, iq::sigmf_name(format));
        return IqResampler::identity(in, format);
    }
    const uint32_t out = whole_hz(*rate_hz, "--stream-rate");
    const FilterDesign design{
        .taps = taps.value_or(std::max(16, static_cast<int>(std::ceil(20.0 * in / out)))),
        .num_phases = num_phases,
        .cutoff_hz = cutoff_hz.value_or(0.4 * out),
        .window = window,
        .kaiser_beta = kaiser_beta,
    };
    IqResampler resampler(design_filter_bank(design, in, out), in, out, format);
    LOGF(INFO, "stream: %.3f Msps as %s, resampled from %.3f Msps: filter %d taps%s x %d phases, %s%s", out / 1e6,
         iq::sigmf_name(format), in / 1e6, resampler.filter().taps, taps ? "" : " (auto)",
         resampler.filter().num_phases, resampler.filter().description, cutoff_hz ? "" : " (auto cutoff)");
    return resampler;
}

coro::CoroStream<PacketBatch> packetize(coro::CoroStream<IqBlock> blocks, PacketizeParams params,
                                        IqResampler resampler, coro::WatchSender<PaceStats> pace_tx) {
    // A task of its own, so that resampling the next block overlaps sending this one's batches.
    auto batches = coro::build_task().name("packetize").buffer(kBufferBatches).spawn(
        packetize_blocks(std::move(blocks), Packetizer(params, std::move(resampler), std::move(pace_tx))));
    while (auto batch = co_await coro::next(batches)) co_yield std::move(*batch);
}
