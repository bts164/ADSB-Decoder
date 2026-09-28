// Stand-alone UDP receiver for isolating "sender too slow" from "receiver too slow".
// Discards everything, but once a second prints what arrived and what the kernel
// dropped on the way. Build: gcc -O2 tools/udp_sink.c -o udp_sink ; run: ./udp_sink <port>
//
// Columns:
//   pkt/s      packets received this second
//   Mbit/s     payload rate
//   ctr_lost   VRT 4-bit packet-count gaps seen in IF Data packets (vita49_send's
//              counter advances per encoded packet, so gaps == lost/reordered
//              datagrams; 16 consecutive losses alias to zero, so treat as a floor)
//   max_gap_ms longest silence between two datagrams (sender stalls show up here)
//   ts_bad     IF Data packets whose timestamp step from the previous data packet
//              differs from the nominal step (learned from the first packets; the
//              packets are equal-sized and the sender's clock is sample-counted, so
//              the step should be constant to within a few ps). Timestamps are
//              generated before the socket, so this flags sender-side timestamp
//              faults or reordering, NOT network loss: a lost packet doubles one
//              step and is counted too, but with ctr_lost > 0 alongside.
//   rcvbuf_err kernel's UDP RcvbufErrors delta (datagrams dropped because THIS
//              socket's buffer was full: receiver side too slow). Linux only.
//   in_err     kernel's UDP InErrors delta
// Sender-side shortfall shows as low pkt/s with rcvbuf_err == 0 and ctr_lost == 0;
// loss on the wire/receiver shows as rcvbuf_err > 0 or ctr_lost > 0.
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

// Reads a column of the "Udp:" line of /proc/net/snmp by name.
static long snmp_udp(const char* name) {
    FILE* f = fopen("/proc/net/snmp", "r");
    if (!f) return 0;
    char hdr[1024], val[1024];
    long result = 0;
    while (fgets(hdr, sizeof hdr, f)) {
        if (strncmp(hdr, "Udp:", 4) != 0 || !fgets(val, sizeof val, f)) continue;
        char *h, *v, *hs, *vs;
        h = strtok_r(hdr, " \n", &hs);
        v = strtok_r(val, " \n", &vs);
        while ((h = strtok_r(NULL, " \n", &hs)) && (v = strtok_r(NULL, " \n", &vs))) {
            if (strcmp(h, name) == 0) result = atol(v);
        }
        break;
    }
    fclose(f);
    return result;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <port> [rcvbuf_MB=64]\n", argv[0]); return 1; }
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    int rb = (argc > 2 ? atoi(argv[2]) : 64) << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, &rb, sizeof rb);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons((uint16_t)atoi(argv[1])),
                            .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(s, (void*)&a, sizeof a) != 0) { perror("bind"); return 1; }

    static uint8_t buf[65536];
    unsigned long pkts = 0, bytes = 0, lost = 0;
    double gap_max = 0, last = now(), tick = last;
    long rcv0 = snmp_udp("RcvbufErrors"), in0 = snmp_udp("InErrors");
    int expect = -1;
    unsigned long ts_bad = 0, ts_first_bad = 0;
    long long prev_ps = 0, nominal = 0;  // picoseconds since the epoch; fits in 63 bits until year 2262
    int ts_seen = 0;
    printf("%8s %9s %9s %7s %11s %10s %7s\n", "pkt/s", "Mbit/s", "ctr_lost", "ts_bad", "max_gap_ms", "rcvbuf_err",
           "in_err");
    for (;;) {
        ssize_t n = recv(s, buf, sizeof buf, 0);
        if (n < 0) break;
        double t = now();
        if (t - last > gap_max) gap_max = t - last;
        last = t;
        pkts++;
        bytes += (unsigned long)n;
        if (n >= 4 && (buf[0] >> 4) == 0x1) {  // IF Data with Stream ID (context packets, type 4, are separate)
            int ctr = buf[1] & 0xF;
            if (expect >= 0) lost += (unsigned)((ctr - expect) & 0xF);
            expect = (ctr + 1) & 0xF;
            if (n >= 20) {
                unsigned long long sec = ((unsigned long long)buf[8] << 24) | (buf[9] << 16) | (buf[10] << 8) | buf[11];
                unsigned long long fr = 0;
                for (int i = 12; i < 20; i++) fr = (fr << 8) | buf[i];
                long long ps = (long long)(sec * 1000000000000ull + fr);
                if (ts_seen > 0) {
                    long long step = ps - prev_ps;
                    if (ts_seen == 1) nominal = step;
                    long long err = step - nominal;
                    if (err < 0) err = -err;
                    if (ts_seen > 1 && err > nominal / 100 + 2) {
                        ts_bad++;
                        if (ts_first_bad++ < 400)
                            fprintf(stderr, "ts step %lld ps, expected %lld (sec=%llu frac=%llu)\n", step, nominal, sec, fr);
                    }
                }
                prev_ps = ps;
                if (ts_seen < 2) ts_seen++;
            }
        }
        if (t - tick >= 1.0) {
            long rcv = snmp_udp("RcvbufErrors"), in = snmp_udp("InErrors");
            double dt = t - tick;
            printf("%8.0f %9.1f %9lu %7lu %11.2f %10ld %7ld\n", (double)pkts / dt, (double)bytes * 8 / dt / 1e6, lost,
                   ts_bad, gap_max * 1e3, rcv - rcv0, in - in0);
            fflush(stdout);
            pkts = bytes = lost = ts_bad = 0;
            gap_max = 0;
            rcv0 = rcv;
            in0 = in;
            tick = t;
        }
    }
    return 0;
}
