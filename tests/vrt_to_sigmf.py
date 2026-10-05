#!/usr/bin/env python3
"""Record a VRT/UDP IQ stream (as sent by vita49_send) to a SigMF file pair.

    vrt_to_sigmf.py --port 47051 --output capture

writes capture.sigmf-data and capture.sigmf-meta. Start this first, then
vita49_send (which aborts if nothing is listening). Capture ends on Ctrl+C,
after --duration seconds of signal, or when packets stop arriving for
--idle-timeout seconds.

Each packet is placed in the file by its timestamp. UDP may reorder packets
(seen on WSL2 loopback), so packets are re-sorted by timestamp in a small
window, and a packet that never arrives is replaced by zeros (and marked with a
SigMF annotation) so sample positions stay time-aligned.

The sample rate and RF frequency come from the stream's IF Context packets
(which vita49_send repeats every --context-interval seconds). If the stream has
none, the rate is inferred from the data packets' timestamps or taken from
--sample-rate, and the frequency from --frequency. Whichever of these sources
are available must agree on the rate (a mismatch is an error, since the file
would otherwise be full of bogus zero-filled gaps); --frequency, if given,
overrides the stream's.

Requires numpy (install into the workspace venv: `pip install numpy`).
"""

import argparse
import datetime
import heapq
import json
import os
import socket
import struct
import sys

import numpy as np

HEADER_BYTES = 20  # header, stream id, seconds, picoseconds (hi, lo)
PS_PER_S = 10**12
REORDER_WINDOW = 256  # packets held back to restore timestamp order
MAX_GAP_S = 10.0  # a bigger timestamp jump is treated as a restarted sender

DATATYPES = {"ci16_be": 4, "cf32_le": 8}  # bytes per complex sample


# IF Context CIF0 fields, bit -> size in 32-bit words (doc/design/vita49-format.md).
CIF0_WORDS = {30: 1, 29: 2, 28: 2, 27: 2, 26: 2, 25: 2, 24: 1, 23: 1, 22: 1, 21: 2, 20: 2, 19: 1, 18: 1, 17: 2, 16: 1, 15: 2}
CIF0_Q44_20 = {29: "bandwidth_hz", 27: "rf_freq_hz", 21: "sample_rate_hz"}  # the fields we read


def parse_context(d):
    """Decodes the fields of an IF Context packet body: {name: value in Hz}."""
    (cif0,) = struct.unpack_from(">I", d, HEADER_BYTES)
    info = {}
    offset = HEADER_BYTES + 4
    for bit in range(30, 14, -1):
        if not (cif0 >> bit) & 1:
            continue
        words = CIF0_WORDS[bit]
        if offset + 4 * words > len(d):
            return None
        if bit in CIF0_Q44_20:
            (raw,) = struct.unpack_from(">q", d, offset)  # signed Q44.20
            info[CIF0_Q44_20[bit]] = raw / (1 << 20)
        offset += 4 * words
    return info


def parse_packet(d):
    """Classifies a packet of the form vita49_send emits: ("data", stream_id,
    timestamp_ps, iq_payload) or ("context", stream_id, timestamp_ps, fields),
    else None."""
    if len(d) < HEADER_BYTES or len(d) % 4:
        return None
    hdr, stream_id, sec, ps_hi, ps_lo = struct.unpack_from(">5I", d)
    packet_type = hdr >> 28
    has_class_id = (hdr >> 27) & 1
    has_trailer = (hdr >> 26) & 1  # for context packets this bit is a different flag; also 0 here
    tsi = (hdr >> 22) & 3  # 1 = UTC
    tsf = (hdr >> 20) & 3  # 2 = real-time picoseconds
    size_words = hdr & 0xFFFF
    if has_class_id or has_trailer or tsi != 1 or tsf != 2 or size_words * 4 != len(d):
        return None
    ts_ps = sec * PS_PER_S + ((ps_hi << 32) | ps_lo)
    if packet_type == 1:
        return "data", stream_id, ts_ps, d[HEADER_BYTES:]
    if packet_type == 4 and len(d) >= HEADER_BYTES + 4:
        info = parse_context(d)
        return ("context", stream_id, ts_ps, info) if info is not None else None
    return None


class CaptureError(Exception):
    pass


def estimate_sample_rate(entries):
    """Sample rate implied by (timestamp_ps, payload) entries: the median of
    samples-per-interval over adjacent packets in timestamp order, which
    ignores the intervals that span a dropped packet. None if undeterminable."""
    entries = sorted(entries, key=lambda e: e[0])
    rates = [(len(a[1]) // 4) * PS_PER_S / (b[0] - a[0]) for a, b in zip(entries, entries[1:]) if b[0] > a[0]]
    if not rates:
        return None
    rates.sort()
    return round(rates[len(rates) // 2])


class Recorder:
    def __init__(self, data_file, datatype, duration_s):
        self.data_file = data_file
        self.rate = None  # set by start()
        self.datatype = datatype
        self.duration_s = duration_s
        self.limit = None
        self.base_ps = None  # timestamp of sample 0
        self.next_idx = 0
        self.annotations = []
        self.done = False
        self.zero_filled_samples = 0
        self.late_packets = 0

    def start(self, sample_rate):
        self.rate = sample_rate
        self.limit = round(self.duration_s * sample_rate) if self.duration_s else None

    def _emit(self, payload):
        """Writes big-endian int16 IQ in the target datatype."""
        if self.datatype == "ci16_be":
            self.data_file.write(payload)
        else:
            iq = np.frombuffer(payload, dtype=">i2").astype("<f4") / np.float32(32768)
            self.data_file.write(iq.tobytes())

    def write(self, ts_ps, payload):
        if self.done:
            return
        if self.base_ps is None:
            self.base_ps = ts_ps
        idx = ((ts_ps - self.base_ps) * self.rate + PS_PER_S // 2) // PS_PER_S
        n = len(payload) // 4

        if idx < self.next_idx:  # duplicate, or arrived after its window
            self.late_packets += 1
            return
        gap = idx - self.next_idx
        if gap:
            if gap > MAX_GAP_S * self.rate:
                print(f"timestamp jumped by {gap / self.rate:.1f} s; sender restarted? stopping", file=sys.stderr)
                self.done = True
                return
            self._zeros(gap)
        if self.limit is not None and idx + n >= self.limit:
            n = max(self.limit - idx, 0)
            payload = payload[: 4 * n]
            self.done = True
        if n > 0:
            self._emit(payload)
        self.next_idx = idx + n

    def _zeros(self, gap):
        if self.limit is not None:
            gap = min(gap, self.limit - self.next_idx)
        bytes_per_sample = DATATYPES[self.datatype]
        self.data_file.write(bytes(gap * bytes_per_sample))
        self.annotations.append(
            {
                "core:sample_start": self.next_idx,
                "core:sample_count": gap,
                "core:label": "dropped packets (zero-filled)",
            }
        )
        self.zero_filled_samples += gap
        self.next_idx += gap


def record(args, data_file):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 32 * 1024 * 1024)  # kernel clamps to rmem_max
    sock.bind((args.bind, args.port))
    print(f"listening on {args.bind}:{args.port} (Ctrl+C to stop)", file=sys.stderr)

    rec = Recorder(data_file, args.datatype, args.duration)
    stats = {"packets": 0, "context": 0, "other_stream": 0, "invalid": 0}
    ctx = {}  # latest context fields from the recorded stream
    stream_id = args.stream_id
    pending = []  # heap of (timestamp_ps, arrival_seq, payload)
    seq = 0

    def flush(keep):
        """Writes packets in timestamp order until `keep` remain pending."""
        if rec.rate is None and len(pending) > keep:
            fix_sample_rate()
        while len(pending) > keep and not rec.done:
            ts_ps, _, payload = heapq.heappop(pending)
            rec.write(ts_ps, payload)

    def fix_sample_rate():
        # In priority order: the stream's own context packet, the user's flag,
        # then what the data packets' timestamps imply.
        ctx_rate = round(ctx["sample_rate_hz"]) if "sample_rate_hz" in ctx else None
        sources = {
            "context packet": ctx_rate,
            "--sample-rate": args.sample_rate,
            "packet timestamps": estimate_sample_rate([(e[0], e[2]) for e in pending]),
        }
        sources = {name: rate for name, rate in sources.items() if rate}
        if not sources:
            raise CaptureError("cannot determine the sample rate (no context packet, too few data packets); "
                               "pass --sample-rate")
        chosen_name, chosen = next(iter(sources.items()))
        if any(abs(rate - chosen) > 0.001 * chosen for rate in sources.values()):
            detail = ", ".join(f"{name}: {rate} Hz" for name, rate in sources.items())
            raise CaptureError(f"sample rate sources disagree ({detail}). The sender's --rate, this script's "
                               "--sample-rate and the packet timestamps must all match.")
        rec.start(chosen)
        print(f"sample rate {chosen} Hz (from {chosen_name})", file=sys.stderr)
    try:
        while not rec.done:
            try:
                d = sock.recv(65536)
            except socket.timeout:
                print(f"no packets for {args.idle_timeout} s; stopping", file=sys.stderr)
                break
            pkt = parse_packet(d)
            if pkt is None:
                stats["invalid"] += 1
                continue
            kind, sid, ts_ps, body = pkt
            if stream_id is None:
                stream_id = sid
                sock.settimeout(args.idle_timeout)
                print(f"recording stream id {sid}", file=sys.stderr)
            if sid != stream_id:
                stats["other_stream"] += 1
                continue
            if kind == "context":
                stats["context"] += 1
                if rec.rate is not None and "sample_rate_hz" in body and round(body["sample_rate_hz"]) != rec.rate:
                    print(f"warning: context now says {body['sample_rate_hz']:.0f} Hz; capture stays at {rec.rate} Hz",
                          file=sys.stderr)
                ctx.update(body)
                continue
            stats["packets"] += 1
            heapq.heappush(pending, (ts_ps, seq, body))
            seq += 1
            flush(REORDER_WINDOW)
    except KeyboardInterrupt:
        print("\ninterrupted", file=sys.stderr)
    flush(0)
    sock.close()
    return rec, stats, ctx


def datetime_of(ps_since_epoch):
    sec, ps = divmod(ps_since_epoch, PS_PER_S)
    t = datetime.datetime.fromtimestamp(sec, tz=datetime.timezone.utc)
    return t.strftime("%Y-%m-%dT%H:%M:%S") + f".{ps // 10**6:06d}Z"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port", type=int, required=True, help="UDP port to listen on (vita49_send --dest-port)")
    ap.add_argument("--sample-rate", type=lambda s: round(float(s)), default=None,
                    help="sample rate in Hz (vita49_send --rate). Normally not needed: the stream's context packets "
                         "(or, failing that, its timestamps) provide it. If given, it must agree with them")
    ap.add_argument("--output", required=True, help="output path; .sigmf-data and .sigmf-meta are appended")
    ap.add_argument("--bind", default="0.0.0.0", help="address to bind (default 0.0.0.0)")
    ap.add_argument("--stream-id", type=int, default=None,
                    help="only record this VRT stream id (default: the first one seen)")
    ap.add_argument("--datatype", choices=sorted(DATATYPES), default="ci16_be",
                    help="ci16_be stores the packets' samples unchanged; cf32_le is scaled to [-1, 1) "
                         "and is what file_stream.cpp reads (default ci16_be)")
    ap.add_argument("--frequency", type=float, default=None,
                    help="RF center frequency in Hz to record in the metadata; overrides the stream's context packet")
    ap.add_argument("--duration", type=float, default=0.0, help="stop after this many seconds of signal (0 = no limit)")
    ap.add_argument("--idle-timeout", type=float, default=2.0,
                    help="stop after this many seconds without packets, once the first has arrived (default 2)")
    args = ap.parse_args()

    base = args.output
    for ext in (".sigmf-data", ".sigmf-meta"):
        if base.endswith(ext):
            base = base[: -len(ext)]
    data_path, meta_path = base + ".sigmf-data", base + ".sigmf-meta"

    try:
        with open(data_path, "wb") as data_file:
            rec, stats, ctx = record(args, data_file)
    except CaptureError as err:
        os.remove(data_path)
        print(f"error: {err}", file=sys.stderr)
        sys.exit(2)

    if rec.base_ps is None:
        os.remove(data_path)
        print("no packets received; nothing recorded", file=sys.stderr)
        sys.exit(1)

    capture = {"core:sample_start": 0, "core:datetime": datetime_of(rec.base_ps)}
    frequency = args.frequency if args.frequency is not None else ctx.get("rf_freq_hz")
    if frequency is not None:
        capture["core:frequency"] = frequency
    meta = {
        "global": {
            "core:datatype": args.datatype,
            "core:sample_rate": rec.rate,
            "core:version": "1.0.0",
            "core:recorder": "vrt_to_sigmf.py",
            "core:description": "Recorded from a VITA-49/VRT UDP IQ stream",
        },
        "captures": [capture],
        "annotations": rec.annotations,
    }
    with open(meta_path, "w") as f:
        json.dump(meta, f, indent=2)
        f.write("\n")

    print(
        f"wrote {rec.next_idx} samples ({rec.next_idx / rec.rate:.3f} s) to {data_path}\n"
        f"  data packets {stats['packets']}, context packets {stats['context']}, zero-filled samples {rec.zero_filled_samples}, "
        f"late/duplicate packets dropped {rec.late_packets}, other-stream {stats['other_stream']}, "
        f"invalid {stats['invalid']}",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
