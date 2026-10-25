#include "input/uhd_stream.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <absl/strings/str_format.h>
#include <absl/strings/str_join.h>
#include <argparse/argparse.hpp>
#include <uhd/exception.hpp>
#include <uhd/stream.hpp>
#include <uhd/types/metadata.hpp>
#include <uhd/types/stream_cmd.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/usrp/subdev_spec.hpp>
#include <uhd/utils/log.hpp>
#include <uhd/utils/log_add.hpp>
#include <uhd/version.hpp>

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/sync/mpsc.h>
#include <coro/task/spawn_blocking.h>

#include "common/log.h"
#include "common/trace.h"
#include "dsp/iq_block.h"
#include "input/iq_block_chunker.h"

namespace {

/** One receive packet, valid only during the Usrp::read() callback it is passed to. */
struct UsrpPacket {
    /** The first sample's device time, in samples at the rate the device runs at; unset if the packet has none. */
    std::optional<int64_t> time;
    std::span<const float> iq;          ///< interleaved I/Q, I first, in [-1, 1)
};

/**
 * RAII wrapper around one USRP -- see coro's doc/guidelines.md CA.1 (RAII for all resources owned by a coroutine
 * frame): the constructor opens and tunes it and makes its receive streamer, and the device is released when the
 * Usrp is destroyed.
 *
 * read() streams until its spawn_blocking task is cancelled, so the thread that runs it should own the Usrp: then
 * it is destroyed, and the device released, only once the read has ended.
 */
class Usrp {
public:
    using OnPacket = std::function<void(const UsrpPacket&)>;

    /** Opens and tunes the device, as uhd_iq_stream() says. */
    explicit Usrp(const UhdStreamParams& params);

    Usrp(const Usrp&) = delete;
    Usrp& operator=(const Usrp&) = delete;

    /**
     * Streams from the channel, calling `on_packet` on this thread with each packet. Blocks, so call it on a
     * coro::spawn_blocking thread. Runs until that task is cancelled, which it sees within the receive timeout,
     * and returns only by throwing: coro::TaskCancelled then, std::runtime_error if receiving fails. Streaming
     * is stopped either way.
     */
    void read(const OnPacket& on_packet);

private:
    const UhdStreamParams m_params;
    uhd::usrp::multi_usrp::sptr m_usrp;
    // After m_usrp, so it is destroyed first.
    uhd::rx_streamer::sptr m_rx;
    double m_rate_hz = 0;  // the rate the device runs at, which its packets' times count in
};

// How long recv() waits for a packet. It bounds how long read() takes to see that it was cancelled; a packet
// arrives every few hundred microseconds at the rates adsb uses, so it is reached only when the device stops
// sending.
constexpr double kReceiveTimeoutS = 0.1;

// How long to wait for the LO to lock after tuning. UHD's own examples wait a second.
constexpr auto kLockTimeout = std::chrono::seconds(1);

// How far the rate the device runs at may be from the one asked for. Within it, the error is no more than a
// crystal's, and the samples are labeled with the rate asked for; beyond it, that label would make every sample's
// time wrong.
constexpr double kRateTolerancePpm = 100;

// UHD's messages, which it would otherwise print to the console. It calls this from its own logging thread.
void log_from_uhd(const uhd::log::logging_info& info) {
    std::string_view msg = info.message;
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.remove_suffix(1);
    switch (info.verbosity) {
        case uhd::log::fatal:
        case uhd::log::error: LOGF(ERROR, "uhd [%s] %s", info.component, msg); break;
        case uhd::log::warning: LOGF(WARNING, "uhd [%s] %s", info.component, msg); break;
        case uhd::log::info: LOGF(INFO, "uhd [%s] %s", info.component, msg); break;
        case uhd::log::debug: VLOGF(1, "uhd [%s] %s", info.component, msg); break;
        default: VLOGF(2, "uhd [%s] %s", info.component, msg); break;
    }
}

// UHD's logger is process-wide, so it is added once however many devices are opened.
void route_uhd_log() {
    static std::once_flag once;
    std::call_once(once, [] {
        uhd::log::add_logger("adsb", log_from_uhd);
        uhd::log::set_console_level(uhd::log::off);
    });
}

// The value UHD reports for `read`, or "not reported" for a device or daughterboard that doesn't implement it.
template<typename Read>
std::string reported(Read&& read) {
    try {
        return read();
    } catch (const uhd::not_implemented_error&) {
        return "not reported";
    }
}

Usrp::Usrp(const UhdStreamParams& params) : m_params(params) {
    route_uhd_log();
    const size_t chan = params.channel;
    try {
        m_usrp = uhd::usrp::multi_usrp::make(params.args);
    } catch (const uhd::exception& e) {
        throw std::runtime_error(absl::StrFormat("opening a USRP with --args '%s' failed: %s", params.args,
                                                 e.what()));
    }

    // The subdevice decides which channels there are, so it goes first; the antenna goes before the rate and
    // frequency, as some daughterboards tune per antenna.
    if (params.subdev) m_usrp->set_rx_subdev_spec(uhd::usrp::subdev_spec_t(*params.subdev));
    const size_t num_channels = m_usrp->get_rx_num_channels();
    if (chan >= num_channels)
        throw std::runtime_error(absl::StrFormat("the USRP has %u receive channel(s), so no channel %u",
                                                 num_channels, chan));
    const std::vector<std::string> antennas = m_usrp->get_rx_antennas(chan);
    const std::string antenna_names = antennas.empty() ? "none" : absl::StrJoin(antennas, ", ");
    if (params.antenna) {
        if (std::ranges::find(antennas, *params.antenna) == antennas.end())
            throw std::runtime_error(absl::StrFormat("USRP channel %u has no antenna %s; its antennas are %s", chan,
                                                     *params.antenna, antenna_names));
        m_usrp->set_rx_antenna(*params.antenna, chan);
    }

    m_usrp->set_rx_rate(params.rate_hz, chan);
    m_rate_hz = m_usrp->get_rx_rate(chan);
    m_usrp->set_rx_freq(uhd::tune_request_t(params.freq_hz), chan);
    const double freq_hz = m_usrp->get_rx_freq(chan);
    if (params.bandwidth_hz) m_usrp->set_rx_bandwidth(*params.bandwidth_hz, chan);

    const uhd::gain_range_t gains = m_usrp->get_rx_gain_range(chan);
    if (params.gain_db) {
        // UHD would clip it to the range with only a warning of its own.
        if (*params.gain_db < gains.start() || *params.gain_db > gains.stop())
            throw std::runtime_error(absl::StrFormat("gain %g dB is outside USRP channel %u's range, %g to %g dB",
                                                     *params.gain_db, chan, gains.start(), gains.stop()));
        m_usrp->set_rx_gain(*params.gain_db, chan);
    } else {
        try {
            m_usrp->set_rx_agc(true, chan);
        } catch (const uhd::not_implemented_error&) {
            throw std::runtime_error(absl::StrFormat("USRP channel %u has no AGC; give --gain in dB, from %g to %g",
                                                     chan, gains.start(), gains.stop()));
        }
    }

    // Samples taken before the LO locks are at the wrong frequency. A device without the sensor locks
    // immediately or doesn't say, so it isn't waited for.
    const std::vector<std::string> sensors = m_usrp->get_rx_sensor_names(chan);
    std::string lock_desc = "no lock sensor";
    if (std::ranges::find(sensors, "lo_locked") != sensors.end()) {
        const auto deadline = std::chrono::steady_clock::now() + kLockTimeout;
        while (!m_usrp->get_rx_sensor("lo_locked", chan).to_bool()) {
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error(absl::StrFormat("USRP channel %u's LO didn't lock at %.0f Hz", chan,
                                                         freq_hz));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        lock_desc = "locked";
    }

    uhd::stream_args_t stream_args("fc32", "sc16");
    stream_args.channels = {chan};
    m_rx = m_usrp->get_rx_stream(stream_args);

    const uhd::dict<std::string, std::string> info = m_usrp->get_usrp_rx_info(chan);
    const auto field = [&](const std::string& key) { return std::string(info.get(key, "?")); };
    LOGF(INFO, "opened USRP %s (serial %s, --args '%s'), channel %u: %s, subdevice %s", field("mboard_id"),
         field("mboard_serial"), params.args, chan, field("rx_id"), field("rx_subdev_name"));
    VLOGF(1, "USRP:\n%s", m_usrp->get_pp_string());

    const uhd::meta_range_t rates = m_usrp->get_rx_rates(chan);
    const std::string bandwidth_desc =
        reported([&] { return absl::StrFormat("%.0f Hz", m_usrp->get_rx_bandwidth(chan)); });
    std::string gain_desc = absl::StrFormat("%g dB (of %g to %g)", m_usrp->get_rx_gain(chan), gains.start(),
                                            gains.stop());
    if (!params.gain_db) gain_desc = "auto, " + gain_desc + " now";
    std::string antenna_desc = m_usrp->get_rx_antenna(chan);
    if (!params.antenna) antenna_desc += absl::StrFormat(" (UHD's choice, of %s)", antenna_names);
    LOGF(INFO, "streaming from USRP channel %u: antenna=%s, rate=%.0f Hz (of %g to %g), bandwidth=%s%s, "
         "freq=%.0f Hz, LO %s, gain=%s, master clock=%.0f Hz, samples per packet=%u (Ctrl+C to stop)", chan,
         antenna_desc, m_rate_hz, rates.start(), rates.stop(), bandwidth_desc,
         params.bandwidth_hz ? "" : " (UHD's choice)", freq_hz, lock_desc, gain_desc,
         m_usrp->get_master_clock_rate(), m_rx->get_max_num_samps());
    if (std::abs(m_rate_hz - params.rate_hz) > kRateTolerancePpm * 1e-6 * params.rate_hz)
        throw std::runtime_error(absl::StrFormat("USRP channel %u runs at %.0f Hz, more than %g ppm from the %g Hz "
                                                 "asked for; give --rate %.0f, or one that divides the master clock, "
                                                 "%.0f Hz", chan, m_rate_hz, kRateTolerancePpm, params.rate_hz,
                                                 m_rate_hz, m_usrp->get_master_clock_rate()));
    if (std::abs(m_rate_hz - params.rate_hz) >= 1.0)
        LOGF(WARNING, "USRP channel %u runs at %.0f Hz, within %g ppm of the %g Hz asked for, which the samples are "
             "labeled with", chan, m_rate_hz, kRateTolerancePpm, params.rate_hz);
    if (std::abs(freq_hz - params.freq_hz) >= 1.0)
        LOGF(WARNING, "USRP channel %u is tuned to %.0f Hz, not the %g Hz asked for", chan, freq_hz,
             params.freq_hz);
    if (params.bandwidth_hz) {
        try {
            if (const double bw = m_usrp->get_rx_bandwidth(chan); std::abs(bw - *params.bandwidth_hz) >= 1.0)
                LOGF(WARNING, "USRP channel %u's bandwidth is %.0f Hz, not the %g Hz asked for", chan, bw,
                     *params.bandwidth_hz);
        } catch (const uhd::not_implemented_error&) {
            LOGF(WARNING, "USRP channel %u doesn't report its bandwidth, so --bandwidth may not have been applied",
                 chan);
        }
    }
}

void Usrp::read(const OnPacket& on_packet) {
    std::vector<float> buffer(2 * m_rx->get_max_num_samps());
    uhd::stream_cmd_t start(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    start.stream_now = true;
    m_rx->issue_stream_cmd(start);
    // The loop ends only by throwing, on cancellation or a receive error; streaming stops either way.
    struct StopStreaming {
        uhd::rx_streamer& rx;
        size_t chan;
        ~StopStreaming() {
            try {
                rx.issue_stream_cmd(uhd::stream_cmd_t(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS));
            } catch (const std::exception& e) {
                LOGF(WARNING, "USRP channel %u failed to stop streaming: %s", chan, e.what());
            }
        }
    } stop{*m_rx, m_params.channel};

    uint64_t overflows = 0;
    uhd::rx_metadata_t md;
    for (;;) {
        coro::blocking_cancellation_point();
        const size_t n = m_rx->recv(buffer.data(), buffer.size() / 2, md, kReceiveTimeoutS, true);
        switch (md.error_code) {
            case uhd::rx_metadata_t::ERROR_CODE_NONE: break;
            case uhd::rx_metadata_t::ERROR_CODE_TIMEOUT: continue;
            case uhd::rx_metadata_t::ERROR_CODE_OVERFLOW:
                // The samples lost show as a jump in the next packet's time, which the caller accounts for.
                overflows++;
                TRACE_PLOT("uhd overflows", static_cast<int64_t>(overflows));
                LOGF_EVERY_N_SEC(WARNING, 1, "USRP channel %u %s: samples were lost before they were read (%u so "
                                 "far)", m_params.channel, md.out_of_sequence ? "dropped packets" : "overflowed",
                                 overflows);
                continue;
            default:
                throw std::runtime_error(absl::StrFormat("USRP channel %u: receiving failed: %s", m_params.channel,
                                                         md.strerror()));
        }
        if (n == 0) continue;
        on_packet({.time = md.has_time_spec ? std::optional<int64_t>(md.time_spec.to_ticks(m_rate_hz))
                                            : std::nullopt,
                   .iq = std::span<const float>(buffer).first(2 * n)});
    }
}

// The longest run of lost samples that is filled with zeros, in blocks. Anything longer, such as a device time
// that jumped rather than samples that were lost, isn't filled, so it can't flood the channel with zeros; the
// blocks after it are then early by the gap.
constexpr uint64_t kMaxFillBlocks = 8;

// Gathers packets into blocks, filling the samples the device lost with zeros, and sends each block as it fills.
class PacketGatherer {
public:
    PacketGatherer(coro::MpscSender<IqBuffer> tx, IqPadding pad, size_t chunk_samples)
        : m_tx(std::move(tx)), m_chunker(pad, chunk_samples), m_chunk_samples(chunk_samples) {}

    void add(const UsrpPacket& packet) {
        const size_t n = packet.iq.size() / 2;
        if (n == 0) return;
        if (packet.time) {
            fill_gap(*packet.time);
            m_next = *packet.time + static_cast<int64_t>(n);
        } else if (m_next) {
            m_next = *m_next + static_cast<int64_t>(n);
        }
        m_chunker.append(packet.iq);
        while (m_chunker.ready() > 0) send(m_chunker.pop());
    }

private:
    // Before a packet at `time` is added: fills the samples lost since the last packet with zeros.
    void fill_gap(int64_t time) {
        if (!m_next) return;
        // A device time converted to samples at a rate that doesn't divide the master clock exactly can round
        // either way, so a difference of one sample is the same packet boundary.
        if (time >= *m_next - 1 && time <= *m_next + 1) return;
        if (time < *m_next) {
            LOGF_EVERY_N_SEC(WARNING, 1, "USRP time went back from sample %d to %d; the blocks after it are late "
                             "by the difference", *m_next, time);
            return;
        }
        const auto lost = static_cast<uint64_t>(time - *m_next);
        m_lost += lost;
        TRACE_PLOT("uhd samples lost", static_cast<int64_t>(m_lost));
        if (lost > kMaxFillBlocks * m_chunk_samples) {
            LOGF(WARNING, "USRP time jumped by %u samples; too many to fill, so the blocks after it are early by "
                 "them", lost);
            return;
        }
        LOGF_EVERY_N_SEC(WARNING, 1, "USRP lost %u samples, filled with zeros (%u lost so far)", lost, m_lost);
        m_chunker.append_zeros(lost);
    }

    void send(IqBuffer block) {
        if (m_tx.try_send(std::move(block))) return;
        // Channel full -- the compute thread can't keep up with this block. Drop it rather than block: the read
        // would fall behind the device, which overflows and loses samples anyway. The block's samples still
        // count, so the drop is a gap and not a shift.
        m_dropped++;
        TRACE_PLOT("uhd blocks dropped", static_cast<int64_t>(m_dropped));
        LOGF_EVERY_N_SEC(WARNING, 1, "compute thread falling behind, %u block(s) dropped so far", m_dropped);
    }

    coro::MpscSender<IqBuffer> m_tx;
    IqBlockChunker<iq::F32> m_chunker;
    size_t m_chunk_samples;
    std::optional<int64_t> m_next;  // the device time, in samples, the next packet should start at
    uint64_t m_lost = 0;
    uint64_t m_dropped = 0;
};

}  // namespace

coro::CoroStream<IqBuffer> uhd_iq_stream(UhdStreamParams params, IqPadding pad, size_t chunk_samples) {
    auto channel = coro::mpsc_channel<IqBuffer>(4);
    coro::MpscReceiver<IqBuffer> rx = std::move(channel.second);

    // The thread opens the device, which blocks, and owns it, so it is released only once the read has ended,
    // however the stream is dropped.
    auto read_handle = coro::spawn_blocking(
        [params = std::move(params), tx = std::move(channel.first), pad, chunk_samples]() mutable {
            // Locals, not captures, so they go when the read ends rather than when the task is freed, in
            // reverse order: the device is released, then the channel closes and the stream sees its end.
            PacketGatherer gatherer(std::move(tx), pad, chunk_samples);
            Usrp device(params);
            device.read([&](const UsrpPacket& packet) { gatherer.add(packet); });
        });

    while (auto block = co_await coro::next(rx)) {
        co_yield std::move(*block);
    }
    // The read ends only when cancelled, which this stream's being dropped does, or when opening the device or
    // receiving fails; this rethrows that failure.
    co_await read_handle;
}

std::string uhd_library_version() { return uhd::get_version_string(); }

void UhdStreamParams::add_options(argparse::ArgumentParser& cmd) {
    cmd.add_description("Stream from an Ettus USRP through UHD.");
    cmd.add_argument("--args").default_value(std::string("")).help(
        "UHD device address, such as type=b200, serial=..., or addr=192.168.10.2 (default: the first USRP found)");
    cmd.add_argument("--subdev").help("receive subdevice spec, such as A:A (default: UHD's)");
    cmd.add_argument("--channel").scan<'u', size_t>().default_value(size_t{0}).help("receive channel");
    cmd.add_argument("--antenna").help("antenna, such as RX2 (default: UHD's; the log names it and the others)");
    cmd.add_argument("--gain").default_value(std::string("auto")).help(
        "receive gain in dB, or auto for the device's AGC (the log gives the range)");
    cmd.add_argument("--rate").scan<'g', double>().default_value(8e6).help(
        "sample rate in Hz; it must divide the device's master clock to within 100 ppm, or it is an error (which "
        "gives the rate the device would run at)");
    cmd.add_argument("--bandwidth").scan<'g', double>().help("analog bandwidth in Hz (default: UHD's)");
    cmd.add_argument("--freq").scan<'g', double>().default_value(1090e6).help("center frequency in Hz to tune to");
}

UhdStreamParams UhdStreamParams::from_options(const argparse::ArgumentParser& /*program*/,
                                              const argparse::ArgumentParser& cmd) {
    const std::string gain = cmd.get<std::string>("--gain");
    std::optional<double> gain_db;
    if (gain != "auto") {
        double db = 0;
        const auto [end, ec] = std::from_chars(gain.data(), gain.data() + gain.size(), db);
        if (ec != std::errc{} || end != gain.data() + gain.size())
            throw std::runtime_error("--gain must be auto or a gain in dB, not " + gain);
        gain_db = db;
    }

    return {
        .args = cmd.get<std::string>("--args"),
        .subdev = cmd.present<std::string>("--subdev"),
        .channel = cmd.get<size_t>("--channel"),
        .antenna = cmd.present<std::string>("--antenna"),
        .rate_hz = cmd.get<double>("--rate"),
        .bandwidth_hz = cmd.present<double>("--bandwidth"),
        .freq_hz = cmd.get<double>("--freq"),
        .gain_db = gain_db,
    };
}
