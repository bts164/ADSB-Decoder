#include "input/sidekiq_stream.h"

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <absl/strings/match.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/str_join.h>
#include <argparse/argparse.hpp>
#include <sidekiq_api.h>

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/sync/mpsc.h>
#include <coro/task/spawn_blocking.h>

#include "common/log.h"
#include "common/trace.h"
#include "dsp/iq_block.h"
#include "input/iq_block_chunker.h"

// Every libsidekiq function adsb calls. Each is looked up by name in the loaded library and typed from the SDK's
// header, so a signature that doesn't match the header fails to compile.
#define SIDEKIQ_FUNCTIONS(X)                     \
    X(skiq_register_logging)                     \
    X(skiq_register_critical_error_callback)     \
    X(skiq_init)                                 \
    X(skiq_exit)                                 \
    X(skiq_is_card_avail)                        \
    X(skiq_read_parameters)                      \
    X(skiq_write_iq_order_mode)                  \
    X(skiq_write_rx_sample_rate_and_bandwidth)   \
    X(skiq_read_rx_sample_rate_and_bandwidth)    \
    X(skiq_write_rx_LO_freq)                     \
    X(skiq_read_rx_LO_freq)                      \
    X(skiq_read_rx_gain_index_range)             \
    X(skiq_write_rx_gain_mode)                   \
    X(skiq_write_rx_gain)                        \
    X(skiq_read_rx_gain)                         \
    X(skiq_read_rx_rf_ports_avail_for_hdl)       \
    X(skiq_read_rx_rf_port_for_hdl)              \
    X(skiq_write_rx_rf_port_for_hdl)             \
    X(skiq_rf_port_string)                       \
    X(skiq_read_rf_port_config)                  \
    X(skiq_read_rx_filters_avail)                \
    X(skiq_read_filter_range)                    \
    X(skiq_read_rx_preselect_filter_path)        \
    X(skiq_write_rx_preselect_filter_path)       \
    X(skiq_read_rx_analog_filter_bandwidth)      \
    X(skiq_read_rfic_rx_fir_config)              \
    X(skiq_read_rx_fir_gain)                     \
    X(skiq_read_rx_dc_offset_corr)               \
    X(skiq_read_rx_cal_mode)                     \
    X(skiq_read_rx_cal_type_mask)                \
    X(skiq_read_rx_cal_types_avail)              \
    X(skiq_read_rx_iq_resolution)                \
    X(skiq_read_rx_data_src)                     \
    X(skiq_set_rx_transfer_timeout)              \
    X(skiq_start_rx_streaming)                   \
    X(skiq_stop_rx_streaming)                    \
    X(skiq_receive)

namespace {

/** One receive packet, valid only during the Sidekiq::read() callback it is passed to. */
struct SidekiqPacket {
    uint64_t rf_timestamp;           ///< the RF timestamp of the first sample
    bool overload;                   ///< the RF input was overloaded
    std::span<const int16_t> iq;     ///< interleaved I/Q, I first
};

/**
 * RAII wrapper around one card -- see coro's doc/guidelines.md CA.1 (RAII for all resources owned by a coroutine
 * frame): the constructor initializes and tunes it, the destructor releases it with skiq_exit(), so the card is
 * free for the next run on every exit path.
 *
 * read() streams until its spawn_blocking task is cancelled, so the thread that runs it should own the Sidekiq:
 * then it is destroyed, and the card released, only once the read has ended.
 */
class Sidekiq {
public:
    using OnPacket = std::function<void(const SidekiqPacket&)>;

    /** Opens and tunes the card, as sidekiq_iq_stream() says. */
    explicit Sidekiq(const SidekiqStreamParams& params);

    ~Sidekiq();

    Sidekiq(const Sidekiq&) = delete;
    Sidekiq& operator=(const Sidekiq&) = delete;

    /**
     * Streams from the handle, calling `on_packet` on this thread with each packet. Blocks, so call it on a
     * coro::spawn_blocking thread. Runs until that task is cancelled, which it sees within the receive timeout,
     * and returns only by throwing: coro::TaskCancelled then, std::runtime_error if receiving fails. Streaming
     * is stopped either way.
     */
    void read(const OnPacket& on_packet);

private:
    struct Api;

    const SidekiqStreamParams m_params;
    std::unique_ptr<const Api> m_api;
};

struct Sidekiq::Api {
#define SIDEKIQ_MEMBER(name) decltype(&::name) name = nullptr;
    SIDEKIQ_FUNCTIONS(SIDEKIQ_MEMBER)
#undef SIDEKIQ_MEMBER

    static std::unique_ptr<const Api> load(const std::string& path);
};

// How long skiq_receive() waits for a packet. It bounds how long read() takes to see that it was cancelled; a
// packet arrives every 20 us or so at 50 Msps, so it is reached only when the radio stops sending.
constexpr int32_t kReceiveTimeoutUs = 25'000;

// How far the rate the device runs at may be from the one asked for. Within it, the error is no more than a
// crystal's, and the samples are labeled with the rate asked for; beyond it, that label would make every sample's
// time wrong.
constexpr double kRateTolerancePpm = 100;

struct SidekiqError {
    int32_t code;                     ///< libsidekiq error code
};
template<typename Sink>
void AbslStringify(Sink &sink, SidekiqError e) {
    absl::Format(&sink, "libsidekiq(%d, '%s')", e.code, strerror(-e.code));
}

skiq_rx_hdl_t to_skiq(SidekiqHandle handle) {
    return handle == SidekiqHandle::A1 ? skiq_rx_hdl_A1 : skiq_rx_hdl_A2;
}

const char* handle_name(SidekiqHandle handle) {
    return handle == SidekiqHandle::A1 ? "A1" : "A2";
}

// "J2" for skiq_rf_port_J2, whether or not libsidekiq's own name has the enum's prefix.
std::string port_name(decltype(&::skiq_rf_port_string) to_string, skiq_rf_port_t port) {
    const char* s = to_string(port);
    std::string_view name = s ? s : "unknown";
    constexpr std::string_view kPrefix = "skiq_rf_port_";
    if (name.starts_with(kPrefix)) name.remove_prefix(kPrefix.size());
    return std::string(name);
}

// "810-1170", the filter's passband in MHz, as --preselect-filter takes it.
std::string filter_name(decltype(&::skiq_read_filter_range) read_range, skiq_filt_t filter) {
    uint64_t start = 0;
    uint64_t end = 0;
    if (read_range(filter, &start, &end) != 0) return absl::StrFormat("filter %d", static_cast<int>(filter));
    return absl::StrFormat("%g-%g", static_cast<double>(start) / 1e6, static_cast<double>(end) / 1e6);
}

// "DC offset, quadrature" for a mask of skiq_rx_cal_type_t bits.
std::string cal_types(uint32_t mask) {
    std::string s;
    auto add = [&](uint32_t bit, std::string_view name) {
        if ((mask & bit) == 0) return;
        absl::StrAppend(&s, s.empty() ? "" : ", ", name);
        mask &= ~bit;
    };
    add(skiq_rx_cal_type_dc_offset, "DC offset");
    add(skiq_rx_cal_type_quadrature, "quadrature");
    if (mask != 0) absl::StrAppend(&s, s.empty() ? "" : ", ", absl::StrFormat("0x%x", mask));
    return s.empty() ? "none" : s;
}

// libsidekiq's messages, which it would otherwise print to the console and syslog. It may call this from its own
// threads.
void log_from_sidekiq(int32_t priority, const char* message) {
    std::string_view msg = message ? message : "";
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.remove_suffix(1);
    // syslog priorities: the lower, the more severe.
    if (priority <= SKIQ_LOG_ERROR) {
        LOGF(ERROR, "libsidekiq: %s", msg);
    } else if (priority <= SKIQ_LOG_WARNING) {
        LOGF(WARNING, "libsidekiq: %s", msg);
    } else if (priority <= SKIQ_LOG_INFO) {
        LOGF(INFO, "libsidekiq: %s", msg);
    } else {
        VLOGF(1, "libsidekiq: %s", msg);
    }
}

// libsidekiq can't be used after a critical error. Without a handler it calls exit() with nothing logged; this
// says why first.
void on_critical_error(int32_t status, void* /*user*/) {
    LOGF(ERROR, "libsidekiq reported a critical error (status %d); exiting", status);
    std::exit(EXIT_FAILURE);
}

std::unique_ptr<const Sidekiq::Api> Sidekiq::Api::load(const std::string& path) {
    // RTLD_LOCAL, so libsidekiq's own protobuf and gRPC stay its own; see sidekiq_stream.h. Never dlclose()d: gRPC
    // keeps threads running inside it until the process exits.
    void* lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (nullptr == lib)
        throw std::runtime_error(absl::StrFormat("failed to load libsidekiq (--lib): %s", dlerror()));
    auto api = std::make_unique<Api>();
#define SIDEKIQ_LOOKUP(name)                                                                              \
    api->name = reinterpret_cast<decltype(api->name)>(dlsym(lib, #name));                                \
    if (nullptr == api->name)                                                                             \
        throw std::runtime_error(absl::StrFormat("%s has no %s; is it libsidekiq 4.19 or newer?", path, #name));
    SIDEKIQ_FUNCTIONS(SIDEKIQ_LOOKUP)
#undef SIDEKIQ_LOOKUP
    LOGF(INFO, "loaded %s", path);
    return api;
}

Sidekiq::Sidekiq(const SidekiqStreamParams& params) : m_params(params), m_api(Api::load(params.lib_path)) {
    const Api& api = *m_api;
    api.skiq_register_logging(log_from_sidekiq);
    api.skiq_register_critical_error_callback(on_critical_error, nullptr);

    uint8_t card = static_cast<uint8_t>(params.card);
    if (const int32_t rc = api.skiq_init(skiq_xport_type_auto, skiq_xport_init_level_full, &card, 1); rc != 0) {
        pid_t owner = 0;
        if (std::abs(rc) == EBUSY && api.skiq_is_card_avail(card, &owner) != 0)
            throw std::runtime_error(absl::StrFormat("sidekiq card %d is in use by process %d", params.card, owner));
        throw std::runtime_error(absl::StrFormat("failed to initialize sidekiq card %d (status %d)", params.card, rc));
    }

    // The destructor won't run if the constructor throws, so the card is released here instead.
    try {
        const skiq_rx_hdl_t hdl = to_skiq(params.handle);
        auto fail = [&](const std::string& what, int32_t rc) {
            return std::runtime_error(absl::StrFormat("sidekiq card %d handle %s rejected %s (status %d)",
                                                      params.card, handle_name(params.handle), what, rc));
        };
        // libsidekiq's default is Q first.
        if (const int32_t rc = api.skiq_write_iq_order_mode(card, skiq_iq_order_iq); rc != 0)
            throw fail("I/Q order", rc);

        // The handle's ports, fixed and TRx, as names. The port is set before the rate and frequency, so they
        // are set for the port in use: libsidekiq keeps a calibration per port.
        std::vector<skiq_rf_port_t> ports;
        std::array<skiq_rf_port_t, skiq_rf_port_max> fixed{};
        std::array<skiq_rf_port_t, skiq_rf_port_max> trx{};
        uint8_t num_fixed = 0;
        uint8_t num_trx = 0;
        const int32_t ports_rc =
            api.skiq_read_rx_rf_ports_avail_for_hdl(card, hdl, &num_fixed, fixed.data(), &num_trx, trx.data());
        if (ports_rc == 0) {
            ports.assign(fixed.begin(), fixed.begin() + std::min<size_t>(num_fixed, fixed.size()));
            ports.insert(ports.end(), trx.begin(), trx.begin() + std::min<size_t>(num_trx, trx.size()));
        }
        std::string port_names;
        for (const skiq_rf_port_t port : ports)
            port_names += (port_names.empty() ? "" : ", ") + port_name(api.skiq_rf_port_string, port);
        if (params.port) {
            if (ports_rc != 0) throw fail("reading the RF ports, needed for --port,", ports_rc);
            const auto it = std::ranges::find_if(ports, [&](skiq_rf_port_t port) {
                return absl::EqualsIgnoreCase(port_name(api.skiq_rf_port_string, port), *params.port);
            });
            if (it == ports.end())
                throw std::runtime_error(absl::StrFormat("sidekiq card %d handle %s has no RF port %s; its ports "
                                                         "are %s", params.card, handle_name(params.handle),
                                                         *params.port, port_names.empty() ? "none" : port_names));
            if (const int32_t rc = api.skiq_write_rx_rf_port_for_hdl(card, hdl, *it); rc != 0)
                throw fail(absl::StrFormat("RF port %s", *params.port), rc);
        }
        if (const int32_t rc = api.skiq_write_rx_sample_rate_and_bandwidth(
                card, hdl, static_cast<uint32_t>(params.rate_hz), static_cast<uint32_t>(params.bandwidth_hz));
            rc != 0)
            throw fail(absl::StrFormat("sample rate %g Hz with bandwidth %g Hz", params.rate_hz,
                                       params.bandwidth_hz), rc);
        if (const int32_t rc = api.skiq_write_rx_LO_freq(card, hdl, static_cast<uint64_t>(params.freq_hz)); rc != 0)
            throw fail(absl::StrFormat("center frequency %g Hz", params.freq_hz), rc);

        // After the frequency: tuning makes libsidekiq choose the filter for it, replacing one chosen before.
        std::array<skiq_filt_t, skiq_filt_max> filters{};
        uint8_t num_filters = 0;
        const int32_t filters_rc = api.skiq_read_rx_filters_avail(card, hdl, filters.data(), &num_filters);
        std::vector<std::string> filter_names;
        if (filters_rc == 0)
            for (const skiq_filt_t filter : std::span(filters).first(std::min<size_t>(num_filters, filters.size())))
                filter_names.push_back(filter_name(api.skiq_read_filter_range, filter));
        const std::string filters_desc =
            filters_rc != 0       ? absl::StrFormat("unreported (status %d)", filters_rc)
            : filter_names.empty() ? std::string("none")
                                   : absl::StrJoin(filter_names, ", ") + " MHz";
        if (params.preselect_filter) {
            if (filters_rc != 0)
                throw fail("reading the preselect filters, needed for --preselect-filter,", filters_rc);
            const auto it = std::ranges::find(filter_names, *params.preselect_filter);
            if (it == filter_names.end())
                throw std::runtime_error(absl::StrFormat("sidekiq card %d handle %s has no preselect filter %s MHz; "
                                                         "its filters are %s", params.card,
                                                         handle_name(params.handle), *params.preselect_filter,
                                                         filters_desc));
            const skiq_filt_t filter = filters[it - filter_names.begin()];
            if (const int32_t rc = api.skiq_write_rx_preselect_filter_path(card, hdl, filter); rc != 0)
                throw fail(absl::StrFormat("preselect filter %s MHz", *params.preselect_filter), rc);
        }

        if (params.gain_index) {
            uint8_t lo = 0;
            uint8_t hi = 0;
            if (const int32_t rc = api.skiq_read_rx_gain_index_range(card, hdl, &lo, &hi); rc != 0)
                throw fail("reading the gain index range", rc);
            if (*params.gain_index < lo || *params.gain_index > hi)
                throw std::runtime_error(absl::StrFormat("gain index %d is outside sidekiq card %d's range, %u to %u",
                                                         *params.gain_index, params.card, lo, hi));
            if (api.skiq_write_rx_gain_mode(card, hdl, skiq_rx_gain_manual) != 0 ||
                api.skiq_write_rx_gain(card, hdl, static_cast<uint8_t>(*params.gain_index)) != 0)
                LOGF(WARNING, "sidekiq card %d rejected gain index %d; the gain is as it was", params.card,
                     *params.gain_index);
        } else if (api.skiq_write_rx_gain_mode(card, hdl, skiq_rx_gain_auto) != 0) {
            LOGF(WARNING, "sidekiq card %d rejected auto gain; the gain is as it was", params.card);
        }

        if (const int32_t rc = api.skiq_set_rx_transfer_timeout(card, kReceiveTimeoutUs); rc != 0)
            throw fail("a receive timeout, without which the read can't be stopped,", rc);

        // What the radio reports, which is what it uses: it can round the rate and bandwidth.
        uint32_t rate = 0;
        double rate_hz = 0;
        uint32_t bandwidth = 0;
        uint32_t actual_bandwidth = 0;
        if (const int32_t rc = api.skiq_read_rx_sample_rate_and_bandwidth(card, hdl, &rate, &rate_hz, &bandwidth,
                                                                          &actual_bandwidth);
            rc != 0)
            throw fail("reading back the sample rate", rc);
        // The handle's range, so the log answers whether a rate the radio didn't run at is one it could.
        std::string rate_range = "unknown";
        skiq_param_t card_params{};
        if (api.skiq_read_parameters(card, &card_params) == 0)
            rate_range = absl::StrFormat("%u to %u Hz", card_params.rx_param[hdl].sample_rate_min,
                                         card_params.rx_param[hdl].sample_rate_max);
        else
            LOGF(WARNING, "sidekiq card %d didn't report the sample rates it supports", params.card);
        uint64_t freq = 0;
        double freq_hz = 0;
        if (api.skiq_read_rx_LO_freq(card, hdl, &freq, &freq_hz) != 0) {
            LOGF(WARNING, "sidekiq card %d didn't report its center frequency; assuming the %g Hz asked for",
                 params.card, params.freq_hz);
            freq_hz = params.freq_hz;
        }
        uint8_t gain = 0;
        const bool have_gain = api.skiq_read_rx_gain(card, hdl, &gain) == 0;
        skiq_rf_port_t port = skiq_rf_port_unknown;
        std::string port_desc = "unknown";
        if (api.skiq_read_rx_rf_port_for_hdl(card, hdl, &port) == 0)
            port_desc = port_name(api.skiq_rf_port_string, port);
        else
            LOGF(WARNING, "sidekiq card %d didn't report handle %s's RF port", params.card,
                 handle_name(params.handle));
        if (!params.port)
            port_desc += absl::StrFormat(" (libsidekiq's choice, of %s)", port_names.empty() ? "?" : port_names);
        LOGF(INFO, "streaming from sidekiq card %d handle %s: port=%s, rate=%.0f Hz (of %s), bandwidth=%u Hz, "
             "freq=%.0f Hz, gain=%s (Ctrl+C to stop)", params.card, handle_name(params.handle), port_desc, rate_hz,
             rate_range, actual_bandwidth, freq_hz,
             params.gain_index ? absl::StrFormat("index %u", gain)
                               : have_gain ? absl::StrFormat("auto, index %u now", gain) : std::string("auto"));
        if (std::abs(rate_hz - params.rate_hz) > kRateTolerancePpm * 1e-6 * params.rate_hz)
            throw std::runtime_error(absl::StrFormat("sidekiq card %d runs at %.0f Hz, more than %g ppm from the %g "
                                                     "Hz asked for; give --rate %.0f (the card's range is %s)",
                                                     params.card, rate_hz, kRateTolerancePpm, params.rate_hz,
                                                     rate_hz, rate_range));
        if (std::abs(rate_hz - params.rate_hz) >= 1.0)
            LOGF(WARNING, "sidekiq card %d runs at %.0f Hz, within %g ppm of the %g Hz asked for, which the samples "
                 "are labeled with", params.card, rate_hz, kRateTolerancePpm, params.rate_hz);
        if (std::abs(actual_bandwidth - params.bandwidth_hz) >= 1.0)
            LOGF(WARNING, "sidekiq card %d's bandwidth is %u Hz, not the %g Hz asked for", params.card,
                 actual_bandwidth, params.bandwidth_hz);

        // The front end's state, each read on its own: a product that doesn't support one says "not reported".
        constexpr std::string_view kUnreported = "not reported";
        std::string filter_desc(kUnreported);
        if (skiq_filt_t filter{}; api.skiq_read_rx_preselect_filter_path(card, hdl, &filter) == 0) {
            const std::string name = filter_name(api.skiq_read_filter_range, filter);
            filter_desc = name + " MHz";
            if (params.preselect_filter && name != *params.preselect_filter)
                LOGF(WARNING, "sidekiq card %d handle %s uses preselect filter %s MHz, not the %s MHz asked for",
                     params.card, handle_name(params.handle), name, *params.preselect_filter);
        }
        if (!params.preselect_filter) filter_desc += " (libsidekiq's choice)";
        std::string analog_desc(kUnreported);
        if (uint32_t hz = 0; api.skiq_read_rx_analog_filter_bandwidth(card, hdl, &hz) == 0)
            analog_desc = absl::StrFormat("%u Hz", hz);
        std::string fir_desc(kUnreported);
        if (uint8_t taps = 0, decimation = 0; api.skiq_read_rfic_rx_fir_config(card, &taps, &decimation) == 0)
            fir_desc = absl::StrFormat("%u taps, decimating by %u", taps, decimation);
        std::string fir_gain_desc(kUnreported);
        if (skiq_rx_fir_gain_t fir_gain{}; api.skiq_read_rx_fir_gain(card, hdl, &fir_gain) == 0) {
            switch (fir_gain) {
                case skiq_rx_fir_gain_neg_12: fir_gain_desc = "-12 dB"; break;
                case skiq_rx_fir_gain_neg_6: fir_gain_desc = "-6 dB"; break;
                case skiq_rx_fir_gain_0: fir_gain_desc = "0 dB"; break;
                case skiq_rx_fir_gain_6: fir_gain_desc = "+6 dB"; break;
            }
        }
        std::string dc_desc(kUnreported);
        if (bool on = false; api.skiq_read_rx_dc_offset_corr(card, hdl, &on) == 0) dc_desc = on ? "on" : "off";
        std::string cal_desc(kUnreported);
        if (skiq_rx_cal_mode_t mode{}; api.skiq_read_rx_cal_mode(card, hdl, &mode) == 0) {
            cal_desc = mode == skiq_rx_cal_mode_auto ? "auto" : "manual";
            uint32_t enabled = 0;
            uint32_t avail = 0;
            if (api.skiq_read_rx_cal_type_mask(card, hdl, &enabled) == 0 &&
                api.skiq_read_rx_cal_types_avail(card, hdl, &avail) == 0)
                absl::StrAppend(&cal_desc, ", ", cal_types(enabled), " (of ", cal_types(avail), ")");
        }
        std::string adc_desc(kUnreported);
        if (uint8_t bits = 0; api.skiq_read_rx_iq_resolution(card, &bits) == 0)
            adc_desc = absl::StrFormat("%u bits", bits);
        std::string ports_desc(kUnreported);
        if (skiq_rf_port_config_t config{}; api.skiq_read_rf_port_config(card, &config) == 0)
            ports_desc = config == skiq_rf_port_config_fixed ? "fixed" : "TRx";
        LOGF(INFO, "sidekiq card %d handle %s front end: preselect filter=%s (of %s), analog filter "
             "bandwidth=%s, RFIC FIR=%s, FIR gain=%s, DC offset correction=%s, Rx calibration=%s, ADC=%s, RF port "
             "config=%s", params.card, handle_name(params.handle), filter_desc, filters_desc, analog_desc, fir_desc,
             fir_gain_desc, dc_desc, cal_desc, adc_desc, ports_desc);

        // The FPGA's test counter in place of the samples would be decoded as if it were signal.
        if (skiq_data_src_t src{}; api.skiq_read_rx_data_src(card, hdl, &src) == 0 && src != skiq_data_src_iq)
            throw std::runtime_error(absl::StrFormat("sidekiq card %d handle %s is sending its test counter, not "
                                                     "I/Q samples", params.card, handle_name(params.handle)));
    } catch (...) {
        api.skiq_exit();
        throw;
    }
}

Sidekiq::~Sidekiq() {
    if (const int32_t rc = m_api->skiq_exit(); rc != 0)
        LOGF(WARNING, "releasing sidekiq card %d failed (status %d)", m_params.card, rc);
}

void Sidekiq::read(const OnPacket& on_packet) {
    const Api& api = *m_api;
    const auto card = static_cast<uint8_t>(m_params.card);
    const skiq_rx_hdl_t hdl = to_skiq(m_params.handle);
    if (const int32_t rc = api.skiq_start_rx_streaming(card, hdl); rc != 0)
        throw std::runtime_error(absl::StrFormat("sidekiq card %d failed to start streaming (status %d)",
                                                 m_params.card, rc));
    // The loop ends only by throwing, on cancellation or a receive error; streaming stops either way.
    struct StopStreaming {
        const Api& api;
        uint8_t card;
        skiq_rx_hdl_t hdl;
        ~StopStreaming() {
            if (const int32_t rc = api.skiq_stop_rx_streaming(card, hdl); rc != 0)
                LOGF(WARNING, "sidekiq card %u failed to stop streaming (status %d)", card, rc);
        }
    } stop{api, card, hdl};

    uint64_t overruns = 0;
    for (;;) {
        coro::blocking_cancellation_point();
        skiq_rx_hdl_t from = hdl;
        skiq_rx_block_t* block = nullptr;
        uint32_t len = 0;
        const skiq_rx_status_t status = api.skiq_receive(card, &from, &block, &len);
        if (status == skiq_rx_status_no_data) continue;  // the timeout
        if (status == skiq_rx_status_error_overrun) {
            // The samples lost show as a jump in the next packet's RF timestamp, which the caller accounts for.
            overruns++;
            TRACE_PLOT("sidekiq overruns", static_cast<int64_t>(overruns));
            LOGF_EVERY_N_SEC(WARNING, 1, "sidekiq card %d overran: samples were lost before they were read (%u "
                             "overrun(s) so far)", m_params.card, overruns);
            continue;
        }
        if (status != skiq_rx_status_success)
            throw std::runtime_error(absl::StrFormat("sidekiq card %d: receiving failed (status %d)", m_params.card,
                                                     static_cast<int>(status)));
        if (from != hdl || len < SKIQ_RX_HEADER_SIZE_IN_BYTES) continue;

        const size_t n = (len - SKIQ_RX_HEADER_SIZE_IN_BYTES) / (2 * sizeof(int16_t));
        // The samples are DMA memory, read once in the callback before the next receive reuses them; volatile in
        // the SDK's type only keeps the compiler from caching them across receives.
        on_packet({.rf_timestamp = block->rf_timestamp,
                   .overload = block->overload != 0,
                   .iq = {const_cast<const int16_t*>(block->data), 2 * n}});
    }
}

// The X40's samples are 16-bit; this scales them to [-1, 1), as the other sources' are. The demodulator's
// per-block RMS normalization (see demod.cpp) makes the exact scale otherwise unimportant.
constexpr float kScale = 1.0f / 32768.0f;

// The longest run of lost samples that is filled with zeros, in blocks. Anything longer, such as a timestamp
// that jumped rather than samples that were lost, isn't filled, so it can't flood the channel with zeros; the
// blocks after it are then early by the gap.
constexpr uint64_t kMaxFillBlocks = 8;

// Gathers packets into blocks, filling the samples the radio lost with zeros, and sends each block as it fills.
class PacketGatherer {
public:
    PacketGatherer(coro::MpscSender<IqBuffer> tx, IqPadding pad, size_t chunk_samples)
        : m_tx(std::move(tx)), m_chunker(pad, chunk_samples), m_chunk_samples(chunk_samples) {}

    void add(const SidekiqPacket& packet) {
        const size_t n = packet.iq.size() / 2;
        if (n == 0) return;
        fill_gap(packet.rf_timestamp);
        m_last_ts = packet.rf_timestamp;
        m_last_n = n;

        if (packet.overload) {
            m_overloads++;
            LOGF_EVERY_N_SEC(WARNING, 1, "sidekiq RF input overloaded (%u packet(s) so far); try a lower --gain "
                             "index", m_overloads);
        }
        m_chunker.append_with(n, [&](std::span<float> dst, size_t offset) {
            const std::span<const int16_t> src = packet.iq.subspan(2 * offset, dst.size());
            for (size_t i = 0; i < dst.size(); i++) dst[i] = static_cast<float>(src[i]) * kScale;
        });
        while (m_chunker.ready() > 0) send(m_chunker.pop());
    }

private:
    // Before `ts`'s packet is added: fills the samples lost since the last packet with zeros. The RF timestamp's
    // step per sample is learned from the first two packets rather than assumed, as the SDK doesn't say whether
    // decimation changes it.
    void fill_gap(uint64_t ts) {
        if (!m_last_ts || m_gaps_off) return;
        if (m_ticks_per_sample == 0) {
            const uint64_t delta = ts - *m_last_ts;
            if (ts > *m_last_ts && delta % m_last_n == 0) {
                m_ticks_per_sample = delta / m_last_n;
                LOGF(INFO, "sidekiq RF timestamp advances %u per sample; samples lost are filled with zeros",
                     m_ticks_per_sample);
            } else {
                m_gaps_off = true;
                LOGF(WARNING, "sidekiq RF timestamp went from %u to %u over a packet of %u samples; lost samples "
                     "can't be told apart, so they aren't filled, and block times drift with every loss",
                     *m_last_ts, ts, m_last_n);
            }
            return;
        }
        const uint64_t expected = *m_last_ts + m_last_n * m_ticks_per_sample;
        if (ts == expected) return;
        if (ts < expected) {
            LOGF_EVERY_N_SEC(WARNING, 1, "sidekiq RF timestamp went back from %u to %u; the blocks after it are "
                             "late by the difference", expected, ts);
            return;
        }
        const uint64_t lost = (ts - expected) / m_ticks_per_sample;
        m_lost += lost;
        TRACE_PLOT("sidekiq samples lost", static_cast<int64_t>(m_lost));
        if (lost > kMaxFillBlocks * m_chunk_samples) {
            LOGF(WARNING, "sidekiq RF timestamp jumped by %u samples; too many to fill, so the blocks after it are "
                 "early by them", lost);
            return;
        }
        LOGF_EVERY_N_SEC(WARNING, 1, "sidekiq lost %u samples, filled with zeros (%u lost so far)", lost, m_lost);
        m_chunker.append_zeros(lost);
    }

    void send(IqBuffer block) {
        if (m_tx.try_send(std::move(block))) return;
        // Channel full -- the compute thread can't keep up with this block. Drop it rather than block: the read
        // would fall behind the radio, which overruns and loses samples anyway. The block's samples still count,
        // so the drop is a gap and not a shift.
        m_dropped++;
        TRACE_PLOT("sidekiq blocks dropped", static_cast<int64_t>(m_dropped));
        LOGF_EVERY_N_SEC(WARNING, 1, "compute thread falling behind, %u block(s) dropped so far", m_dropped);
    }

    coro::MpscSender<IqBuffer> m_tx;
    IqBlockChunker<iq::F32> m_chunker;
    size_t m_chunk_samples;
    std::optional<uint64_t> m_last_ts;  // the RF timestamp of the last packet
    size_t m_last_n = 0;                // samples in the last packet
    uint64_t m_ticks_per_sample = 0;    // 0 until learned
    bool m_gaps_off = false;            // the step couldn't be learned
    uint64_t m_lost = 0;
    uint64_t m_dropped = 0;
    uint64_t m_overloads = 0;
};

}  // namespace

coro::CoroStream<IqBuffer> sidekiq_iq_stream(SidekiqStreamParams params, IqPadding pad, size_t chunk_samples) {
    auto channel = coro::mpsc_channel<IqBuffer>(4);
    coro::MpscReceiver<IqBuffer> rx = std::move(channel.second);

    // The thread opens the card, which blocks, and owns it, so it is released only once the read has ended,
    // however the stream is dropped.
    auto read_handle = coro::spawn_blocking(
        [params = std::move(params), tx = std::move(channel.first), pad, chunk_samples]() mutable {
            // Locals, not captures, so they go when the read ends rather than when the task is freed, in
            // reverse order: the card is released, then the channel closes and the stream sees its end.
            PacketGatherer gatherer(std::move(tx), pad, chunk_samples);
            Sidekiq device(params);
            device.read([&](const SidekiqPacket& packet) { gatherer.add(packet); });
        });

    while (auto block = co_await coro::next(rx)) {
        co_yield std::move(*block);
    }
    // The read ends only when cancelled, which this stream's being dropped does, or when opening the card or
    // receiving fails; this rethrows that failure.
    co_await read_handle;
}

void SidekiqStreamParams::add_options(argparse::ArgumentParser& cmd) {
    cmd.add_description("Stream from an Epiq Sidekiq card, such as the Matchstiq X40's.");
    cmd.add_argument("--lib").default_value(std::string("/usr/lib/epiq/libsidekiq.so.4.19.1")).help(
        "libsidekiq to load");
    cmd.add_argument("--card").scan<'d', int>().default_value(0).help("libsidekiq card number");
    cmd.add_argument("--handle").default_value(std::string("A1")).help(
        "receive handle, A1 or A2: the two the X40's FPGA decimates on");
    cmd.add_argument("--port").help(
        "RF port for the handle, such as J2 (default: libsidekiq's choice; the log names it and the others)");
    cmd.add_argument("--gain").default_value(std::string("auto")).help(
        "receive gain index, or auto for the radio's AGC");
    cmd.add_argument("--rate").scan<'g', double>().default_value(50e6).help(
        "sample rate in Hz; one the card can't run at to within 100 ppm is an error. The X40's slowest profile is "
        "50 Msps, and only an FPGA with the decimator goes lower (the error gives the handle's range)");
    cmd.add_argument("--bandwidth").scan<'g', double>().default_value(20e6).help(
        "RF bandwidth in Hz; at 50 Msps the X40 has 20 or 41 MHz");
    cmd.add_argument("--preselect-filter").help(
        "RF preselect filter, by its passband in MHz such as 810-1170 (default: libsidekiq's choice for --freq; "
        "the log names it and the others)");
    cmd.add_argument("--freq").scan<'g', double>().default_value(1090e6).help("center frequency in Hz to tune to");
}

SidekiqStreamParams SidekiqStreamParams::from_options(const argparse::ArgumentParser& /*program*/,
                                                      const argparse::ArgumentParser& cmd) {
    const std::string handle = cmd.get<std::string>("--handle");
    if (handle != "A1" && handle != "A2")
        throw std::runtime_error("--handle must be A1 or A2, not " + handle);

    const std::string gain = cmd.get<std::string>("--gain");
    std::optional<int> gain_index;
    if (gain != "auto") {
        int index = 0;
        const auto [end, ec] = std::from_chars(gain.data(), gain.data() + gain.size(), index);
        if (ec != std::errc{} || end != gain.data() + gain.size() || index < 0)
            throw std::runtime_error("--gain must be auto or a gain index, not " + gain);
        gain_index = index;
    }

    return {
        .lib_path = cmd.get<std::string>("--lib"),
        .card = cmd.get<int>("--card"),
        .handle = handle == "A1" ? SidekiqHandle::A1 : SidekiqHandle::A2,
        .port = cmd.present<std::string>("--port"),
        .rate_hz = cmd.get<double>("--rate"),
        .bandwidth_hz = cmd.get<double>("--bandwidth"),
        .freq_hz = cmd.get<double>("--freq"),
        .gain_index = gain_index,
        .preselect_filter = cmd.present<std::string>("--preselect-filter"),
    };
}
