// Phase-0 CLI: reads IQ from a file, filter coefficients from a file, runs
// the ispc-vectorized demodulator, prints decoded frames. See README.md
// "Implementation phasing" for what this is and isn't (no shared library,
// no SDR plugins yet — that comes later).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <argparse/argparse.hpp>
#include <fftw3.h>
#include <nlohmann/json.hpp>
#include <rtl-sdr.h>

#include <coro/co_invoke.h>
#include <coro/coro.h>
#include <coro/io/ws_listener.h>
#include <coro/io/ws_stream.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/timeout.h>
#include <coro/task/spawn_blocking.h>

#include "demod.h"
#include "filter_bank.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

// Minimal, deliberately non-general extraction of "core:sample_rate" from a
// .sigmf-meta JSON file — avoids pulling in a JSON library for phase-0.
// Falls back to std::nullopt if not found; caller must supply --rate then.
std::optional<double> read_sigmf_sample_rate(const std::string& meta_path) {
    std::ifstream in(meta_path);
    if (!in) return std::nullopt;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string key = "\"core:sample_rate\"";
    auto pos = content.find(key);
    if (pos == std::string::npos) return std::nullopt;
    pos = content.find(':', pos + key.size());
    if (pos == std::string::npos) return std::nullopt;
    return std::stod(content.substr(pos + 1));
}

// Mode S / ADS-B CRC-24 remainder, ported bit-for-bit from adsb_detect.jl's
// `crc` function. Operates on a right-justified message of up to 112 bits
// (short 56-bit messages must be pre-shifted so their real content sits in
// the low bits -- see print_frame's `m2` below); the loop always runs the
// same fixed 88 iterations regardless of actual message width; leading
// positions with no real data above the message just shift the generator
// into alignment without touching M, which is a no-op for this style of
// non-shifting LFSR division.
unsigned __int128 mode_s_crc(unsigned __int128 m) {
    unsigned __int128 g = static_cast<unsigned __int128>(0x1fff409) << (112 - 25);
    unsigned __int128 mask0 = static_cast<unsigned __int128>(0x1000000) << (112 - 25);
    unsigned __int128 mask1 = static_cast<unsigned __int128>(0x1FFFFFF) << (112 - 25);
    for (int i = 112; i >= 25; i--) {
        if (m & mask0) {
            m = (~mask1 & m) + ((mask1 & m) ^ g);
        }
        mask0 >>= 1;
        mask1 >>= 1;
        g >>= 1;
    }
    return m;
}

// Frames confirmed by a CRC-valid DF11/17/18 (whose AA field is the ICAO
// address in the clear) accumulate here, so later short replies (DF0/4/5/16/
// 20/21, whose AP field is ICAO XOR parity rather than a plain ICAO) can be
// validated against a previously-seen address -- same two-tier scheme as
// adsb_detect.jl's known_icaos, but built up online as frames arrive rather
// than in a separate first pass, since streaming mode has no "whole batch"
// to pre-scan. Shared across both call sites below (file mode and --rtlsdr
// streaming are mutually exclusive within a single run, and print_frame is
// only ever called from one thread at a time in either mode), hence a
// function-local static rather than a variable threaded through both call
// sites.
std::unordered_set<uint32_t>& known_icaos() {
    static std::unordered_set<uint32_t> icaos;
    return icaos;
}

// Fields extracted/derived from an AdsbFrame once (idx refinement, df, crc,
// icao), shared by both the stdout printer and the JSON serializer below so
// neither duplicates the CRC/ICAO logic.
struct DecodedFrameView {
    uint64_t idx;
    unsigned df;
    bool crc_ok;
    bool icao_known;
    uint32_t icao;
    unsigned __int128 payload;
    unsigned num_bits;
    float confidence;
};

// Shared by both file mode (base_sample_index=0, exec() sees the whole file
// as one block) and --rtlsdr mode (base_sample_index = running count of
// samples from prior blocks, since each block is its own exec() call with
// block-relative indices -- see run_rtlsdr_stream).
DecodedFrameView compute_frame_view(const AdsbFrame& f, double rate_hz, uint64_t base_sample_index) {
    // f.sample_index only reflects the coarse preamble-peak m; refine it here
    // (presentation only, not part of exec()) by converting f.ic0 -- a y[]-
    // domain sub-chip offset -- into raw input-sample units and adding it in.
    // See demod.h's AdsbFrame::ic0 comment.
    double ic0_raw_samples = static_cast<double>(f.ic0) * rate_hz / (ADSB_SAMPLES_PER_SYMBOL * 1e6);
    uint64_t idx = base_sample_index + f.sample_index + static_cast<uint64_t>(std::llround(ic0_raw_samples));

    unsigned df = static_cast<unsigned>((f.payload >> 107) & 0x1F);
    bool is_short = df == 0 || df == 4 || df == 5 || df == 11;
    // Short (56-bit) replies decode into the top 56 bits of the 112-bit
    // payload (see demod.cpp: kNumBits is always 112); shift the real
    // content down so mode_s_crc sees it right-justified, matching Julia's
    // `m2 = m >> 56`.
    unsigned __int128 m2 = is_short ? (f.payload >> 56) : f.payload;
    unsigned __int128 crc_val = mode_s_crc(m2);
    uint32_t aa_field = static_cast<uint32_t>((f.payload >> 80) & 0xFFFFFF);

    bool crc_ok = false;
    uint32_t icao = 0;
    bool icao_known = false;
    if (df == 11) {
        crc_ok = crc_val < 63;
        if (crc_ok) {
            icao = aa_field;
            icao_known = true;
            known_icaos().insert(icao);
        }
    } else if (df == 17 || df == 18) {
        crc_ok = crc_val == 0;
        if (crc_ok) {
            icao = aa_field;
            icao_known = true;
            known_icaos().insert(icao);
        }
    } else if (df == 0 || df == 4 || df == 5 || df == 16 || df == 20 || df == 21) {
        // AP field is ICAO XOR parity, not a plain address -- crc_val
        // *is* the ICAO here when it matches a previously confirmed one.
        uint32_t candidate = static_cast<uint32_t>(crc_val & 0xFFFFFF);
        crc_ok = known_icaos().count(candidate) > 0;
        if (crc_ok) {
            icao = candidate;
            icao_known = true;
        }
    }

    return DecodedFrameView{idx, df, crc_ok, icao_known, icao, f.payload, f.num_bits, f.confidence};
}

void print_frame_stdout(const DecodedFrameView& v) {
    uint64_t hi = static_cast<uint64_t>(v.payload >> 64);
    uint64_t lo = static_cast<uint64_t>(v.payload & 0xFFFFFFFFFFFFFFFFULL);
    char icao_str[7] = "??????";
    if (v.icao_known) std::snprintf(icao_str, sizeof(icao_str), "%06x", v.icao);

    std::printf("idx=%llu bits=%u confidence=%.1f df=%u icao=%s crc=%s payload=%016llx%016llx\n",
                static_cast<unsigned long long>(v.idx), v.num_bits, v.confidence, v.df, icao_str,
                v.crc_ok ? "ok" : "fail", static_cast<unsigned long long>(hi), static_cast<unsigned long long>(lo));
}

std::string frame_to_json(const DecodedFrameView& v) {
    uint64_t hi = static_cast<uint64_t>(v.payload >> 64);
    uint64_t lo = static_cast<uint64_t>(v.payload & 0xFFFFFFFFFFFFFFFFULL);
    char payload_hex[33];
    std::snprintf(payload_hex, sizeof(payload_hex), "%016llx%016llx", static_cast<unsigned long long>(hi),
                  static_cast<unsigned long long>(lo));
    char icao_str[7] = "";
    if (v.icao_known) std::snprintf(icao_str, sizeof(icao_str), "%06x", v.icao);

    nlohmann::json j;
    j["type"] = "frame";
    j["idx"] = v.idx;
    j["bits"] = v.num_bits;
    j["confidence"] = v.confidence;
    j["df"] = v.df;
    j["icao"] = v.icao_known ? nlohmann::json(icao_str) : nlohmann::json(nullptr);
    j["crc_ok"] = v.crc_ok;
    j["payload"] = payload_hex;
    return j.dump();
}

// --- Aircraft state tracking (DF17/18 extended-squitter decoding) ------
//
// Decodes lat/lon (via CPR even/odd pairing), barometric altitude,
// callsign, and ground velocity out of the ME field of CRC-valid DF17/18
// frames, and keeps one running AircraftState per ICAO address -- same
// function-local-static, single-writer-thread pattern as known_icaos()
// above (see that comment for why no locking is needed).
//
// Only the common subset of the ADS-B message set is handled: type codes
// 1-4 (identification), 9-18 (airborne position, barometric altitude
// only -- Gillham/Q=0 encoded altitudes are left unset), and 19 subtypes
// 1-2 (ground-speed velocity). Surface position, GNSS-height position,
// airspeed/heading velocity subtypes, and status messages are not
// decoded; this is deliberately the "basic" slice, matching the same
// scope philosophy as the frontend table (see ui/frontend/lib/main.dart).
struct AircraftState {
    uint32_t icao = 0;
    std::optional<std::string> callsign;
    std::optional<int> altitude_ft;
    std::optional<double> lat;
    std::optional<double> lon;
    std::optional<double> ground_speed_kt;
    std::optional<double> track_deg;
    std::optional<int> vertical_rate_fpm;
    double last_seen_s = 0;

    // Most recent even/odd CPR-encoded airborne position reports, held
    // until a same-parity-mismatched pair arrives close enough in time to
    // resolve into an unambiguous lat/lon (global CPR decode).
    struct CprSlot {
        uint32_t lat_cpr = 0;
        uint32_t lon_cpr = 0;
        double t = 0;
        bool have = false;
    };
    CprSlot cpr_even;
    CprSlot cpr_odd;
};

std::unordered_map<uint32_t, AircraftState>& aircraft_table() {
    static std::unordered_map<uint32_t, AircraftState> table;
    return table;
}

std::optional<std::string> decode_callsign(uint64_t me56) {
    auto ch = [](unsigned v) -> char {
        if (v >= 1 && v <= 26) return static_cast<char>('A' + (v - 1));
        if (v == 32) return ' ';
        if (v >= 48 && v <= 57) return static_cast<char>('0' + (v - 48));
        return '?';
    };
    std::string s;
    for (int i = 0; i < 8; i++) {
        unsigned v = (me56 >> (42 - 6 * i)) & 0x3F;
        s += ch(v);
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.empty()) return std::nullopt;
    return s;
}

// 12-bit AC-style altitude field from an airborne-position ME (TC 9-18).
// Only the Q=1 (25ft-increment) encoding is handled; Q=0 is legacy
// Gillham/Mode-C encoding, rare in modern ADS-B and not decoded here.
std::optional<int> decode_alt12(unsigned alt_field) {
    unsigned q = (alt_field >> 4) & 0x1;
    if (!q) return std::nullopt;
    unsigned n = ((alt_field >> 5) << 4) | (alt_field & 0xF);
    return static_cast<int>(n) * 25 - 1000;
}

// Number of longitude zones for a given latitude, per the standard CPR
// formula (RTCA DO-260 / ICAO Annex 10). NZ=15 for the 360/4NZ latitude
// zone size ADS-B uses.
int cpr_nl(double lat) {
    if (lat == 0) return 59;
    if (lat == 87.0 || lat == -87.0) return 2;
    if (std::abs(lat) > 87.0) return 1;
    constexpr double kNz = 15.0;
    double a = 1 - std::cos(kPi / (2 * kNz));
    double b = std::cos(kPi / 180.0 * std::abs(lat));
    double val = 1 - a / (b * b);
    val = std::clamp(val, -1.0, 1.0);
    return static_cast<int>(std::floor(2 * kPi / std::acos(val)));
}

// Global CPR decode: resolves an unambiguous lat/lon from one even and one
// odd airborne-position CPR report (raw 17-bit fields), given which of the
// two is the more recent (used as the position/zone-count reference).
// Standard algorithm; see e.g. "The 1090MHz Riddle" sec. on CPR decoding.
std::optional<std::pair<double, double>> cpr_resolve_global(uint32_t lat_cpr_e_raw, uint32_t lon_cpr_e_raw,
                                                              uint32_t lat_cpr_o_raw, uint32_t lon_cpr_o_raw,
                                                              bool even_is_newer) {
    double cprlat_e = lat_cpr_e_raw / 131072.0;
    double cprlon_e = lon_cpr_e_raw / 131072.0;
    double cprlat_o = lat_cpr_o_raw / 131072.0;
    double cprlon_o = lon_cpr_o_raw / 131072.0;

    auto pmod = [](double a, double b) {
        double r = std::fmod(a, b);
        return r < 0 ? r + b : r;
    };

    constexpr double d_lat_even = 360.0 / 60.0;
    constexpr double d_lat_odd = 360.0 / 59.0;

    double j = std::floor(59 * cprlat_e - 60 * cprlat_o + 0.5);
    double lat_even = d_lat_even * (pmod(j, 60.0) + cprlat_e);
    double lat_odd = d_lat_odd * (pmod(j, 59.0) + cprlat_o);
    if (lat_even >= 270) lat_even -= 360;
    if (lat_odd >= 270) lat_odd -= 360;

    if (cpr_nl(lat_even) != cpr_nl(lat_odd)) return std::nullopt;

    double lat, lon;
    if (even_is_newer) {
        lat = lat_even;
        int nl = cpr_nl(lat_even);
        int ni = std::max(nl, 1);
        double m = std::floor(cprlon_e * (nl - 1) - cprlon_o * nl + 0.5);
        lon = (360.0 / ni) * (pmod(m, static_cast<double>(ni)) + cprlon_e);
    } else {
        lat = lat_odd;
        int nl = cpr_nl(lat_odd);
        int ni = std::max(nl - 1, 1);
        double m = std::floor(cprlon_e * (nl - 1) - cprlon_o * nl + 0.5);
        lon = (360.0 / ni) * (pmod(m, static_cast<double>(ni)) + cprlon_o);
    }
    if (lon > 180) lon -= 360;
    if (lat < -90 || lat > 90) return std::nullopt;
    return std::make_pair(lat, lon);
}

void handle_position(AircraftState& ac, uint64_t me56, double t, bool& changed) {
    unsigned alt_field = (me56 >> 36) & 0xFFF;
    unsigned f_bit = (me56 >> 34) & 0x1;  // 0=even, 1=odd
    uint32_t lat_cpr = (me56 >> 17) & 0x1FFFF;
    uint32_t lon_cpr = me56 & 0x1FFFF;

    auto alt = decode_alt12(alt_field);
    if (alt && ac.altitude_ft != alt) {
        ac.altitude_ft = alt;
        changed = true;
    }

    auto& slot = f_bit ? ac.cpr_odd : ac.cpr_even;
    slot = {lat_cpr, lon_cpr, t, true};

    if (ac.cpr_even.have && ac.cpr_odd.have && std::abs(ac.cpr_even.t - ac.cpr_odd.t) <= 10.0) {
        bool even_is_newer = ac.cpr_even.t >= ac.cpr_odd.t;
        auto pos = cpr_resolve_global(ac.cpr_even.lat_cpr, ac.cpr_even.lon_cpr, ac.cpr_odd.lat_cpr, ac.cpr_odd.lon_cpr,
                                       even_is_newer);
        if (pos && (ac.lat != pos->first || ac.lon != pos->second)) {
            ac.lat = pos->first;
            ac.lon = pos->second;
            changed = true;
        }
    }
}

// Ground-speed velocity (TC 19, subtypes 1-2 only; airspeed/heading
// subtypes 3-4 are not decoded).
void handle_velocity(AircraftState& ac, uint64_t me56, bool& changed) {
    unsigned st = (me56 >> 48) & 0x7;
    if (st != 1 && st != 2) return;

    unsigned ew_sign = (me56 >> 42) & 0x1;
    unsigned ew_raw = (me56 >> 32) & 0x3FF;
    unsigned ns_sign = (me56 >> 31) & 0x1;
    unsigned ns_raw = (me56 >> 21) & 0x3FF;
    unsigned vr_sign = (me56 >> 19) & 0x1;
    unsigned vr_raw = (me56 >> 10) & 0x1FF;

    if (ew_raw != 0 && ns_raw != 0) {
        int vx = static_cast<int>(ew_raw - 1) * (ew_sign ? -1 : 1);
        int vy = static_cast<int>(ns_raw - 1) * (ns_sign ? -1 : 1);
        double speed = std::sqrt(static_cast<double>(vx) * vx + static_cast<double>(vy) * vy);
        double track = std::atan2(static_cast<double>(vx), static_cast<double>(vy)) * 180.0 / kPi;
        if (track < 0) track += 360;
        if (!ac.ground_speed_kt || std::abs(*ac.ground_speed_kt - speed) > 0.05) {
            ac.ground_speed_kt = speed;
            changed = true;
        }
        if (!ac.track_deg || std::abs(*ac.track_deg - track) > 0.05) {
            ac.track_deg = track;
            changed = true;
        }
    }
    if (vr_raw != 0) {
        int vr = static_cast<int>(vr_raw - 1) * 64 * (vr_sign ? -1 : 1);
        if (ac.vertical_rate_fpm != vr) {
            ac.vertical_rate_fpm = vr;
            changed = true;
        }
    }
}

// Dispatches on the ME field's type code (top 5 bits). Returns true if any
// displayed field of `ac` changed, so the caller knows whether to publish
// an update (aircraft state is only published on change, not per-frame).
bool update_aircraft_from_me(AircraftState& ac, unsigned __int128 payload, double t) {
    uint64_t me56 = static_cast<uint64_t>((payload >> 24) & ((static_cast<unsigned __int128>(1) << 56) - 1));
    unsigned tc = static_cast<unsigned>((me56 >> 51) & 0x1F);
    bool changed = false;
    if (tc >= 1 && tc <= 4) {
        auto cs = decode_callsign(me56);
        if (cs && ac.callsign != cs) {
            ac.callsign = cs;
            changed = true;
        }
    } else if (tc >= 9 && tc <= 18) {
        handle_position(ac, me56, t, changed);
    } else if (tc == 19) {
        handle_velocity(ac, me56, changed);
    }
    return changed;
}

std::string aircraft_to_json(const AircraftState& ac) {
    char icao_str[7];
    std::snprintf(icao_str, sizeof(icao_str), "%06x", ac.icao);

    nlohmann::json j;
    j["type"] = "aircraft";
    j["icao"] = icao_str;
    j["callsign"] = ac.callsign ? nlohmann::json(*ac.callsign) : nlohmann::json(nullptr);
    j["altitude_ft"] = ac.altitude_ft ? nlohmann::json(*ac.altitude_ft) : nlohmann::json(nullptr);
    j["lat"] = ac.lat ? nlohmann::json(*ac.lat) : nlohmann::json(nullptr);
    j["lon"] = ac.lon ? nlohmann::json(*ac.lon) : nlohmann::json(nullptr);
    j["ground_speed_kt"] = ac.ground_speed_kt ? nlohmann::json(*ac.ground_speed_kt) : nlohmann::json(nullptr);
    j["track_deg"] = ac.track_deg ? nlohmann::json(*ac.track_deg) : nlohmann::json(nullptr);
    j["vertical_rate_fpm"] = ac.vertical_rate_fpm ? nlohmann::json(*ac.vertical_rate_fpm) : nlohmann::json(nullptr);
    j["last_seen_s"] = ac.last_seen_s;
    return j.dump();
}

void print_aircraft_stdout(const AircraftState& ac) {
    char icao_str[7];
    std::snprintf(icao_str, sizeof(icao_str), "%06x", ac.icao);
    std::string line = std::string("aircraft icao=") + icao_str;
    line += " callsign=" + (ac.callsign ? *ac.callsign : std::string("?"));
    char buf[64];
    if (ac.altitude_ft) line += " alt=" + std::to_string(*ac.altitude_ft) + "ft";
    if (ac.lat && ac.lon) {
        std::snprintf(buf, sizeof(buf), " lat=%.5f lon=%.5f", *ac.lat, *ac.lon);
        line += buf;
    }
    if (ac.ground_speed_kt) {
        std::snprintf(buf, sizeof(buf), " gs=%.0fkt", *ac.ground_speed_kt);
        line += buf;
    }
    if (ac.track_deg) {
        std::snprintf(buf, sizeof(buf), " trk=%.0f", *ac.track_deg);
        line += buf;
    }
    if (ac.vertical_rate_fpm) line += " vr=" + std::to_string(*ac.vertical_rate_fpm) + "fpm";
    std::snprintf(buf, sizeof(buf), " t=%.1fs", ac.last_seen_s);
    line += buf;
    std::printf("%s\n", line.c_str());
}

// Background WebSocket publisher: owns a dedicated OS thread running its own
// single-threaded coro::Runtime (kept separate from the main pipeline's
// runtime/threads so publishing a frame never blocks demod/decode), a
// WsListener bound at construction time, and a broadcast loop that fans out
// each publish()'d JSON line to every currently-connected client. A single
// coroutine alternates between accepting new connections and draining the
// mpsc queue via coro::select -- since the underlying Runtime is single-
// threaded, that coroutine is the only thing that ever touches `conns`, so
// no locking is needed around it.
//
// Caveat (file mode only): a client that connects after file processing has
// finished and drained the queue misses that backlog entirely -- select()
// resolves whichever branch is ready first, so a buffered recv() always wins
// over a still-pending accept(). Not a concern for --rtlsdr mode, which runs
// until Ctrl+C.
class WsPublisher {
public:
    explicit WsPublisher(uint16_t port) {
        auto channel = coro::mpsc_channel<std::string>(64);
        m_tx = std::move(channel.first);
        coro::MpscReceiver<std::string> rx = std::move(channel.second);

        m_thread = std::thread([port, rx = std::move(rx)]() mutable {
            coro::Runtime rt(1);
            rt.block_on(coro::co_invoke([&]() -> coro::Coro<void> {
                coro::WsListener listener = co_await coro::WsListener::bind("0.0.0.0", port);
                std::cerr << "[ws] listening on port " << port << "\n";
                std::vector<coro::WsStream> conns;
                for (;;) {
                    try {
                        auto result = co_await coro::select(listener.accept(), rx.recv());
                        if (result.index() == 0) {
                            conns.push_back(std::move(std::get<0>(result).value));
                            std::cerr << "[ws] client connected (" << conns.size() << " total)\n";
                        } else {
                            auto& msg = std::get<1>(result).value;
                            if (!msg) break;  // sender dropped, channel closed -- shut down
                            for (size_t i = 0; i < conns.size();) {
                                bool ok = false;
                                try {
                                    auto send_result =
                                        co_await coro::timeout(std::chrono::seconds(2), conns[i].send(*msg));
                                    ok = send_result.index() == 0;
                                } catch (const std::exception&) {
                                    ok = false;
                                }
                                if (ok) {
                                    i++;
                                } else {
                                    conns.erase(conns.begin() + static_cast<ptrdiff_t>(i));
                                }
                            }
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "[ws] error: " << e.what() << "\n";
                    }
                }
            }));
        });
    }

    ~WsPublisher() {
        // Reset (not just clear) the sender first so rx.recv() in the
        // publisher thread observes channel closure and the select loop
        // above breaks out and returns, letting the thread join.
        m_tx.reset();
        if (m_thread.joinable()) m_thread.join();
    }

    WsPublisher(const WsPublisher&) = delete;
    WsPublisher& operator=(const WsPublisher&) = delete;

    void publish(std::string json_line) {
        if (m_tx) m_tx->try_send(std::move(json_line));
    }

private:
    std::optional<coro::MpscSender<std::string>> m_tx;
    std::thread m_thread;
};

// --- Spectrum / waterfall -------------------------------------------------
//
// A side channel off the raw pre-resample IQ blocks handed to the compute
// thread (see run_rtlsdr_stream) -- before AdsbDemod's own resampling to
// its fixed internal ADS-B rate, which would distort the frequency picture
// this is for (true center-frequency offset, out-of-band noise). Runs far
// below the full sample rate: one short windowed FFT out of one block every
// kSpectrumInterval, not a continuous transform over everything, since a
// waterfall display only needs a handful of updates per second.
constexpr int kSpectrumFftSize = 1024;
constexpr std::chrono::milliseconds kSpectrumInterval{150};

class SpectrumComputer {
public:
    SpectrumComputer() {
        in_ = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * kSpectrumFftSize));
        out_ = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * kSpectrumFftSize));
        plan_ = fftwf_plan_dft_1d(kSpectrumFftSize, in_, out_, FFTW_FORWARD, FFTW_MEASURE);
        window_.resize(kSpectrumFftSize);
        for (int i = 0; i < kSpectrumFftSize; i++) {
            window_[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(kPi) * i / (kSpectrumFftSize - 1)));
        }
    }
    ~SpectrumComputer() {
        fftwf_destroy_plan(plan_);
        fftwf_free(in_);
        fftwf_free(out_);
    }
    SpectrumComputer(const SpectrumComputer&) = delete;
    SpectrumComputer& operator=(const SpectrumComputer&) = delete;

    // Windows and transforms exactly kSpectrumFftSize samples starting at
    // `x`, returning fftshifted magnitude in dB: bin 0 is the most negative
    // frequency (-rate_hz/2 relative to center) and the last bin the most
    // positive (+rate_hz/2), so callers can plot straight against a linear
    // frequency axis with no reordering.
    std::vector<float> compute(const std::complex<float>* x) {
        for (int i = 0; i < kSpectrumFftSize; i++) {
            in_[i][0] = x[i].real() * window_[i];
            in_[i][1] = x[i].imag() * window_[i];
        }
        fftwf_execute(plan_);
        std::vector<float> mag_db(kSpectrumFftSize);
        for (int i = 0; i < kSpectrumFftSize; i++) {
            int src = (i + kSpectrumFftSize / 2) % kSpectrumFftSize;  // fftshift
            float re = out_[src][0], im = out_[src][1];
            float mag2 = re * re + im * im;
            mag_db[i] = 10.0f * std::log10(std::max(mag2, 1e-20f));
        }
        return mag_db;
    }

private:
    fftwf_complex* in_;
    fftwf_complex* out_;
    fftwf_plan plan_;
    std::vector<float> window_;
};

std::string spectrum_to_json(const std::vector<float>& mag_db, double rate_hz, double freq_hz) {
    nlohmann::json j;
    j["type"] = "spectrum";
    j["freq_hz"] = freq_hz;
    j["rate_hz"] = rate_hz;
    j["bins"] = mag_db;
    return j.dump();
}

void handle_frame(const AdsbFrame& f, double rate_hz, uint64_t base_sample_index, bool print_stdout,
                   WsPublisher* ws_pub) {
    DecodedFrameView v = compute_frame_view(f, rate_hz, base_sample_index);
    if (print_stdout) print_frame_stdout(v);
    if (ws_pub) ws_pub->publish(frame_to_json(v));

    if (v.crc_ok && v.icao_known && (v.df == 17 || v.df == 18)) {
        double t_s = static_cast<double>(v.idx) / rate_hz;
        AircraftState& ac = aircraft_table()[v.icao];
        ac.icao = v.icao;
        bool changed = update_aircraft_from_me(ac, f.payload, t_s);
        ac.last_seen_s = t_s;
        if (changed) {
            if (print_stdout) print_aircraft_stdout(ac);
            if (ws_pub) ws_pub->publish(aircraft_to_json(ac));
        }
    }
}

// --- --rtlsdr streaming mode -------------------------------------------
//
// Pipeline: librtlsdr's own event-handling thread (inside rtlsdr_read_async,
// which blocks until rtlsdr_cancel_async()) delivers raw uint8 I/Q buffers
// via callback; the callback converts each buffer to a padded
// complex<float> block (same guard-padding contract exec() already requires
// for whole-file mode, see demod.h) and pushes it into an mpsc channel.
// A second thread pulls blocks off the channel and runs them through
// AdsbDemod::exec() one at a time. Both threads are coro::spawn_blocking
// pool threads so a single coroutine can co_await both to completion and
// propagate any exception; there's no other use of coroutines/futures here,
// this is just how coro's blocking-thread bookkeeping works.

struct IqBlock {
    std::vector<std::complex<float>> samples;  // guard-padded: [lead][n][trail]
    size_t n = 0;
    int lead = 0;
};

// Set right after a successful rtlsdr_open() and cleared before
// rtlsdr_close(); only ever written from the main thread. std::atomic here
// is about giving the signal handler a well-defined read of the pointer,
// not about arbitrating concurrent writers (there are none).
std::atomic<rtlsdr_dev_t*> g_rtlsdr_dev{nullptr};

// rtlsdr_cancel_async() is what librtlsdr's own CLI tools (rtl_sdr.c et al.)
// call directly from a SIGINT handler -- not strictly POSIX
// async-signal-safe, but it's the established pattern for this library and
// matches its documented cancellation contract (the only way to stop
// rtlsdr_read_async's blocking loop).
void handle_sigint(int) {
    if (rtlsdr_dev_t* dev = g_rtlsdr_dev.load()) rtlsdr_cancel_async(dev);
}

struct StreamCallbackCtx {
    coro::MpscSender<IqBlock> tx;
    int lead;
    int trail;
};

void rtlsdr_stream_cb(unsigned char* buf, uint32_t len, void* ctx_ptr) {
    auto* ctx = static_cast<StreamCallbackCtx*>(ctx_ptr);
    const size_t n = len / 2;  // interleaved uint8 I/Q pairs
    if (n == 0) return;

    IqBlock block;
    block.samples.assign(static_cast<size_t>(ctx->lead) + n + static_cast<size_t>(ctx->trail),
                          std::complex<float>{0.0f, 0.0f});
    block.n = n;
    block.lead = ctx->lead;
    std::complex<float>* out = block.samples.data() + ctx->lead;
    for (size_t i = 0; i < n; i++) {
        // librtlsdr delivers unsigned 8-bit offset-binary samples (128 ==
        // zero) -- recenter to signed. AdsbDemod::exec's per-block RMS
        // normalization (see demod.cpp) makes the exact input scale
        // otherwise unimportant.
        float i_val = (static_cast<float>(buf[2 * i]) - 127.5f) / 127.5f;
        float q_val = (static_cast<float>(buf[2 * i + 1]) - 127.5f) / 127.5f;
        out[i] = std::complex<float>(i_val, q_val);
    }

    if (!ctx->tx.try_send(std::move(block))) {
        // Buffer full -- the compute thread can't keep up with this block.
        // Drop it rather than blocking_send(): blocking here would stall
        // librtlsdr's USB callback thread and desync its internal transfer
        // resubmission, losing far more data than one dropped block does.
        static uint64_t dropped = 0;
        if (++dropped % 100 == 1) {
            std::cerr << "[rtlsdr] warning: compute thread falling behind, " << dropped
                      << " block(s) dropped so far\n";
        }
    }
}

int run_rtlsdr_stream(FilterBank filter, double rate_hz, float preamble_min, float slice_mag_min, int device_index,
                       double freq_hz, double gain_db, uint32_t buf_num, uint32_t buf_len, bool print_stdout,
                       WsPublisher* ws_pub, bool enable_spectrum) {
    rtlsdr_dev_t* dev = nullptr;
    if (rtlsdr_open(&dev, static_cast<uint32_t>(device_index)) != 0) {
        std::cerr << "failed to open rtl-sdr device " << device_index << "\n";
        return 1;
    }
    rtlsdr_set_sample_rate(dev, static_cast<uint32_t>(rate_hz));
    rtlsdr_set_center_freq(dev, static_cast<uint32_t>(freq_hz));
    if (gain_db < 0.0) {
        rtlsdr_set_tuner_gain_mode(dev, 0);  // auto
    } else {
        rtlsdr_set_tuner_gain_mode(dev, 1);  // manual
        rtlsdr_set_tuner_gain(dev, static_cast<int>(std::lround(gain_db * 10.0)));
    }
    rtlsdr_reset_buffer(dev);

    g_rtlsdr_dev.store(dev);
    struct sigaction sa {};
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);

    std::cerr << "streaming from rtl-sdr device " << device_index << ": rate=" << rate_hz << " Hz, freq=" << freq_hz
              << " Hz, gain=" << (gain_db < 0.0 ? std::string("auto") : std::to_string(gain_db) + " dB")
              << " (Ctrl+C to stop)\n";

    const int lead = adsb_leading_pad(filter.taps);
    const int trail = adsb_trailing_pad(filter.taps);

    // Structured bindings would work here (C++23), but named variables keep
    // the capture-by-move below unambiguous.
    auto channel = coro::mpsc_channel<IqBlock>(4);
    coro::MpscSender<IqBlock> tx = std::move(channel.first);
    coro::MpscReceiver<IqBlock> rx = std::move(channel.second);

    coro::Runtime rt(1);

    // co_invoke, not a bare invoked lambda: per doc/guidelines.md CS.3, a
    // capturing lambda coroutine invoked directly ([&]() -> Coro<int> {...}())
    // leaves the coroutine frame holding a `this` into a temporary that's
    // destroyed the instant the call returns the Coro<int> handle.
    // co_invoke heap-allocates the lambda so its address stays valid for the
    // whole coroutine's lifetime -- see that doc for the full explanation.
    int rc = rt.block_on(coro::co_invoke([&]() -> coro::Coro<int> {
        auto compute_handle = coro::spawn_blocking(
            [filter = std::move(filter), rate_hz, preamble_min, slice_mag_min, rx = std::move(rx), print_stdout,
             ws_pub, freq_hz, enable_spectrum]() mutable {
                AdsbDemod demod(filter, rate_hz, preamble_min, slice_mag_min);
                // Only constructed when actually wanted: building the FFTW
                // plan isn't free, and the whole point of --spectrum being
                // opt-in is to not pay for this when nothing's listening.
                std::optional<SpectrumComputer> spectrum;
                if (enable_spectrum) spectrum.emplace();
                auto next_spectrum_at = std::chrono::steady_clock::now();
                uint64_t stream_offset = 0;
                uint64_t frame_count = 0;
                while (auto block = rx.blocking_recv()) {
                    if (spectrum && ws_pub && block->n >= static_cast<size_t>(kSpectrumFftSize)) {
                        auto now = std::chrono::steady_clock::now();
                        if (now >= next_spectrum_at) {
                            auto mag_db = spectrum->compute(block->samples.data() + block->lead);
                            ws_pub->publish(spectrum_to_json(mag_db, rate_hz, freq_hz));
                            next_spectrum_at = now + kSpectrumInterval;
                        }
                    }

                    auto frames = demod.exec(block->samples.data() + block->lead, block->n);
                    for (const auto& f : frames) handle_frame(f, rate_hz, stream_offset, print_stdout, ws_pub);
                    frame_count += frames.size();
                    stream_offset += block->n;
                }
                return frame_count;
            });

        int io_rc;
        {
            // cb_ctx (and its owned sender tx) must be destroyed before we
            // co_await compute_handle below: dropping the last sender is
            // what makes rx.blocking_recv() in the compute thread return
            // nullopt and let it finish once the buffer drains, instead of
            // blocking forever.
            StreamCallbackCtx cb_ctx{std::move(tx), lead, trail};
            auto io_handle = coro::spawn_blocking([dev, buf_num, buf_len, ctx = &cb_ctx]() {
                return rtlsdr_read_async(dev, rtlsdr_stream_cb, ctx, buf_num, buf_len);
            });
            io_rc = co_await io_handle;
        }

        uint64_t frame_count = co_await compute_handle;
        std::cerr << "stream ended (rtlsdr_read_async rc=" << io_rc << "), " << frame_count << " frame(s) decoded\n";
        co_return io_rc == 0 ? 0 : 1;
    }));

    g_rtlsdr_dev.store(nullptr);
    rtlsdr_close(dev);
    return rc;
}

}  // namespace

int main(int argc, char** argv) {
    // TEMP: librtlsdr link/call smoke test -- confirmed we can include the
    // header, link against librtlsdr::librtlsdr, and call into it (ran
    // successfully: rtlsdr_get_device_count() = 0, rtlsdr_open(0) rc = -1,
    // as expected with no dongle attached). Not wired into the actual
    // pipeline yet -- left commented out until that happens for real.
    // {
    //     uint32_t count = rtlsdr_get_device_count();
    //     std::cerr << "[rtlsdr smoke test] rtlsdr_get_device_count() = " << count << "\n";
    //     if (count > 0) {
    //         std::cerr << "[rtlsdr smoke test] device 0 name: " << rtlsdr_get_device_name(0) << "\n";
    //     }
    //     rtlsdr_dev_t* dev = nullptr;
    //     int rc = rtlsdr_open(&dev, 0);
    //     std::cerr << "[rtlsdr smoke test] rtlsdr_open(0) rc = " << rc << "\n";
    //     if (rc == 0 && dev) rtlsdr_close(dev);
    //     return 0;
    // }

    argparse::ArgumentParser program("adsb");
    program.add_argument("--iq").default_value(std::string{}).help(
        "input SigMF IQ data file (.sigmf-data); required unless --rtlsdr is given");
    program.add_argument("--filter").required().help("filter bank coefficients file (.bin)");
    program.add_argument("--filter-meta").required().help("filter bank metadata file (.meta)");
    program.add_argument("--rate").scan<'g', double>().default_value(0.0).help(
        "sample rate in Hz (file mode default: read from --meta / <iq>'s .sigmf-meta sibling; "
        "--rtlsdr mode default: 2.4e6)");
    program.add_argument("--meta").default_value(std::string{}).help(
        "SigMF metadata file (.sigmf-meta); defaults to <iq>'s .sigmf-meta sibling");
    program.add_argument("--preamble-min").scan<'g', float>().default_value(3.0f).help(
        "preamble correlation score threshold");
    program.add_argument("--slice-mag-min").scan<'g', float>().default_value(2.0f * 56.0f).help(
        "bit-slice magnitude threshold");
    program.add_argument("--rtlsdr").flag().help(
        "stream and demodulate live from an RTL-SDR device instead of reading --iq");
    program.add_argument("--device").scan<'d', int>().default_value(0).help(
        "RTL-SDR device index (--rtlsdr mode)");
    program.add_argument("--freq").scan<'g', double>().default_value(1090000000.0).help(
        "center frequency in Hz (--rtlsdr mode; default: 1090 MHz)");
    program.add_argument("--gain").scan<'g', double>().default_value(-1.0).help(
        "tuner gain in dB, or negative for auto gain (--rtlsdr mode)");
    program.add_argument("--rtlsdr-buf-num").scan<'u', unsigned>().default_value(0u).help(
        "number of librtlsdr async transfer buffers in flight (--rtlsdr mode; 0 = librtlsdr default: 15)");
    program.add_argument("--rtlsdr-buf-len").scan<'u', unsigned>().default_value(0u).help(
        "librtlsdr async transfer buffer length in bytes, multiple of 512 (--rtlsdr mode; "
        "0 = librtlsdr default: 16*32*512 = 262144 bytes, i.e. 131072 IQ sample pairs, "
        "~54.6 ms at 2.4 Msps)");
    program.add_argument("--ws-port").scan<'u', unsigned>().default_value(0u).help(
        "publish decoded frames as JSON over a WebSocket server on this port (0 = disabled)");
    program.add_argument("--no-stdout").flag().help(
        "suppress the per-frame stdout print (useful with --ws-port to publish only)");
    program.add_argument("--spectrum").flag().help(
        "compute and publish a periodic FFT of the raw pre-resample IQ over --ws-port, for a UI "
        "waterfall (--rtlsdr mode; off by default -- the FFT is wasted work when nothing's "
        "listening for it)");

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& err) {
        std::cerr << err.what() << "\n" << program;
        return 1;
    }

    std::string iq_path = program.get<std::string>("--iq");
    std::string filter_bin_path = program.get<std::string>("--filter");
    std::string filter_meta_path = program.get<std::string>("--filter-meta");
    std::string sigmf_meta_path = program.get<std::string>("--meta");
    double rate_hz = program.get<double>("--rate");
    float preamble_min = program.get<float>("--preamble-min");
    float slice_mag_min = program.get<float>("--slice-mag-min");
    bool use_rtlsdr = program.get<bool>("--rtlsdr");
    unsigned ws_port = program.get<unsigned>("--ws-port");
    bool print_stdout = !program.get<bool>("--no-stdout");
    bool enable_spectrum = program.get<bool>("--spectrum");

    // Scoped to the rest of main() (not just one branch) so its destructor
    // -- which flushes buffered messages before joining the publisher
    // thread -- runs after either mode finishes, regardless of which one
    // was taken.
    std::optional<WsPublisher> ws_pub;
    if (ws_port != 0) ws_pub.emplace(static_cast<uint16_t>(ws_port));

    if (use_rtlsdr) {
        if (rate_hz == 0.0) rate_hz = 2.4e6;

        FilterBank filter;
        try {
            filter = load_filter_bank(filter_bin_path, filter_meta_path);
        } catch (const std::exception& e) {
            std::cerr << "failed to load filter bank: " << e.what() << "\n";
            return 1;
        }

        return run_rtlsdr_stream(std::move(filter), rate_hz, preamble_min, slice_mag_min,
                                  program.get<int>("--device"), program.get<double>("--freq"),
                                  program.get<double>("--gain"), program.get<unsigned>("--rtlsdr-buf-num"),
                                  program.get<unsigned>("--rtlsdr-buf-len"), print_stdout,
                                  ws_pub ? &*ws_pub : nullptr, enable_spectrum);
    }

    if (iq_path.empty()) {
        std::cerr << "--iq is required unless --rtlsdr is given\n" << program;
        return 1;
    }

    if (rate_hz == 0.0) {
        std::string meta_path = sigmf_meta_path.empty() ? iq_path + "-meta" : sigmf_meta_path;
        // .sigmf-data -> .sigmf-meta convention
        if (sigmf_meta_path.empty()) {
            auto pos = iq_path.rfind(".sigmf-data");
            if (pos != std::string::npos) meta_path = iq_path.substr(0, pos) + ".sigmf-meta";
        }
        if (auto r = read_sigmf_sample_rate(meta_path)) {
            rate_hz = *r;
        } else {
            std::cerr << "could not determine sample rate from " << meta_path << "; pass --rate <hz>\n";
            return 1;
        }
    }

    FilterBank filter;
    try {
        filter = load_filter_bank(filter_bin_path, filter_meta_path);
    } catch (const std::exception& e) {
        std::cerr << "failed to load filter bank: " << e.what() << "\n";
        return 1;
    }

    const bool timing = std::getenv("ADSB_TIMING") != nullptr;
    auto stage_t = std::chrono::steady_clock::now();
    auto mark = [&](const char* label) {
        if (!timing) return;
        auto now = std::chrono::steady_clock::now();
        std::cerr << "[timing] " << label << ": " << std::chrono::duration<double>(now - stage_t).count() * 1e3
                  << " ms\n";
        stage_t = now;
    };

    std::ifstream iq_in(iq_path, std::ios::binary);
    if (!iq_in) {
        std::cerr << "cannot open IQ file: " << iq_path << "\n";
        return 1;
    }
    // Read the whole file as interleaved complex<float32>, directly into
    // the middle of a buffer that already reserves AdsbDemod::exec's
    // required guard band on each side (see demod.h: adsb_leading_pad/
    // adsb_trailing_pad) -- so there's no separate padding copy at all,
    // not even a small one: the guard samples are just left zero-
    // initialized by the vector's construction, and the real data lands
    // exactly where exec() needs it via this one read() call.
    iq_in.seekg(0, std::ios::end);
    std::streamoff bytes = iq_in.tellg();
    iq_in.seekg(0, std::ios::beg);
    size_t n = static_cast<size_t>(bytes) / sizeof(std::complex<float>);
    const int lead = adsb_leading_pad(filter.taps);
    const int trail = adsb_trailing_pad(filter.taps);
    std::vector<std::complex<float>> xbuf(static_cast<size_t>(lead) + n + static_cast<size_t>(trail));
    std::complex<float>* x = xbuf.data() + lead;
    iq_in.read(reinterpret_cast<char*>(x), static_cast<std::streamsize>(n * sizeof(std::complex<float>)));
    mark("file read");

    // RMS normalization now happens inside AdsbDemod::exec, per block, on
    // the raw (unnormalized) samples passed in here — see demod.cpp.
    std::cerr << "loaded " << n << " IQ samples, rate=" << rate_hz << " Hz, filter taps=" << filter.taps
              << " Np=" << filter.num_phases << "\n";

    AdsbDemod demod(filter, rate_hz, preamble_min, slice_mag_min);
    auto t_start = std::chrono::steady_clock::now();
    auto frames = demod.exec(x, n);
    auto t_end = std::chrono::steady_clock::now();

    double elapsed_s = std::chrono::duration<double>(t_end - t_start).count();
    double samples_per_s = static_cast<double>(n) / elapsed_s;
    double ns_per_sample = elapsed_s * 1e9 / static_cast<double>(n);

    std::cerr << "found " << frames.size() << " frames\n";
    std::cerr << "demod: wall=" << elapsed_s << " s, " << samples_per_s / 1e6 << " Msamples/s, " << ns_per_sample
               << " ns/sample, realtime factor=" << samples_per_s / rate_hz << "x\n";
    for (const auto& f : frames) handle_frame(f, rate_hz, 0, print_stdout, ws_pub ? &*ws_pub : nullptr);

    return 0;
}
