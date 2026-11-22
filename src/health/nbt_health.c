/* nbt-health: live car health for the iDrive "Car health" page (PLAN-diagnostics.md).
 *
 * Asks the car's gateway (HSFZ, TCP 6801) for standard engine values (OBD-II service 01 through the DME), the odometer
 * (KOMBI) and every module's fault memory (UDS 19 02 FF), turns them into short "label value verdict" rows and writes
 * them to a RAM file the HMI plugin shows. Read-only: service 01, 22 and 19 only; it never writes, clears or resets.
 *
 * Quiet unless the Car health page is open: the plugin touches ACTIVE_FLAG every second while the page is shown, and
 * this program only talks to the car while that file is fresh. Each poll opens a connection and closes it again, so a
 * laptop on the OBD port can still connect between polls.
 *
 *   nbt-health                 run as the head-unit daemon (started by boot.sh)
 *   nbt-health --once [IP]     one poll, rows to stdout (testing from a Mac on the ENET cable)
 *
 * Plain C99 + BSD sockets: builds with the open QNX 6.5 gcc and with any desktop compiler.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define ACTIVE_FLAG "/fs/ram3/nbt-health-active"
#define OUT_FILE    "/fs/ram3/nbt-health.txt"
#define LOG_FILE    "/fs/ram3/nbt-health.log"
#define GATEWAY_FILE "/var/opt/hmi/data/rw/NbtAndroidAutoRT/gateway"
#define DEFAULT_GW  "169.254.101.24"   /* this F10's gateway (ZGW), found by broadcast on 2026-11-22 */
#define TESTER      0xF4
#define HSFZ_PORT   6801
#define IDENT_PORT  6811
#define ROWS        10
#define ROW_LEN     48

enum { DME = 0x12, KOMBI = 0x60 };

static const struct { unsigned char addr; const char *name; } MODULES[] = {
    {0x12, "DME"}, {0x18, "EGS"}, {0x29, "DSC"}, {0x30, "EPS"}, {0x01, "ACSM"},
    {0x40, "CAS"}, {0x72, "FRM"}, {0x60, "KOMBI"}, {0x56, "FZD"}, {0x64, "PDC"},
};
#define NMOD (int)(sizeof(MODULES) / sizeof(MODULES[0]))

static int g_once = 0;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void logf_(const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (g_once) { fprintf(stderr, "%s\n", line); return; }
    struct stat st;
    if (stat(LOG_FILE, &st) == 0 && st.st_size > 64 * 1024) rename(LOG_FILE, LOG_FILE ".old");
    FILE *f = fopen(LOG_FILE, "a");
    if (f) { fprintf(f, "%ld %s\n", (long)time(0), line); fclose(f); }
}

/* ------------------------------------------------------------------ HSFZ transport */
static int wait_fd(int fd, int for_write, double timeout) {
    fd_set s; struct timeval tv;
    FD_ZERO(&s); FD_SET(fd, &s);
    tv.tv_sec = (long)timeout; tv.tv_usec = (long)((timeout - (long)timeout) * 1e6);
    return select(fd + 1, for_write ? 0 : &s, for_write ? &s : 0, 0, &tv);
}

static int read_n(int fd, unsigned char *buf, int n, double deadline) {
    int got = 0;
    while (got < n) {
        double left = deadline - now_s();
        if (left <= 0 || wait_fd(fd, 0, left) <= 0) return -1;
        ssize_t r = recv(fd, buf + got, n - got, 0);
        if (r <= 0) return -1;
        got += (int)r;
    }
    return got;
}

static int hsfz_connect(const char *host) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(HSFZ_PORT);
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1) { close(fd); return -1; }
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int rc = connect(fd, (struct sockaddr *)&a, sizeof a);
    if (rc < 0 && errno != EINPROGRESS) { close(fd); return -1; }
    if (rc < 0) {
        int err = 0; socklen_t el = sizeof err;
        if (wait_fd(fd, 1, 3.0) <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) < 0 || err) { close(fd); return -1; }
    }
    fcntl(fd, F_SETFL, fl);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

/* One UDS request to tgt. Returns the response length (bytes after src/tgt) in resp, or -1. Skips HSFZ acks and waits
 * through 7F xx 78 (response pending). */
static int uds(int fd, unsigned char tgt, const unsigned char *req, int len, unsigned char *resp, int cap, double timeout) {
    unsigned char frame[64];
    if (len + 8 > (int)sizeof frame) return -1;
    unsigned plen = (unsigned)len + 2;
    frame[0] = plen >> 24; frame[1] = plen >> 16; frame[2] = plen >> 8; frame[3] = plen;
    frame[4] = 0x00; frame[5] = 0x01; frame[6] = TESTER; frame[7] = tgt;
    memcpy(frame + 8, req, len);
    if (send(fd, frame, len + 8, 0) != len + 8) return -1;
    double deadline = now_s() + timeout;
    for (;;) {
        unsigned char hdr[6];
        if (read_n(fd, hdr, 6, deadline) < 0) return -1;
        unsigned n = ((unsigned)hdr[0] << 24) | (hdr[1] << 16) | (hdr[2] << 8) | hdr[3];
        unsigned ctrl = (hdr[4] << 8) | hdr[5];
        if (n > 65536) return -1;
        unsigned char *p = malloc(n ? n : 1);
        if (!p) return -1;
        if (n && read_n(fd, p, (int)n, deadline) < 0) { free(p); return -1; }
        if (ctrl != 0x0001 || n < 3 || p[0] != tgt) { free(p); continue; }   /* ack echo or another module */
        int rl = (int)n - 2;
        if (rl >= 3 && p[2] == 0x7F && p[4] == 0x78) { free(p); deadline = now_s() + timeout; continue; }
        if (rl >= 1 && p[2] != 0x7F && (p[2] & 0x3F) != (req[0] & 0x3F)) { free(p); continue; }   /* stale answer */
        if (rl > cap) rl = cap;
        memcpy(resp, p + 2, rl);
        free(p);
        return rl;
    }
}

static int discover_at(const char *bcast, char *out, size_t cap) {
    int s = socket(AF_INET, SOCK_DGRAM, 0), ok = 0;
    if (s < 0) return 0;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(IDENT_PORT);
    inet_pton(AF_INET, bcast, &a.sin_addr);
    unsigned char ident[6] = {0, 0, 0, 0, 0x00, 0x11};
    if (sendto(s, ident, sizeof ident, 0, (struct sockaddr *)&a, sizeof a) == sizeof ident && wait_fd(s, 0, 2.0) > 0) {
        unsigned char buf[256]; struct sockaddr_in from; socklen_t fl = sizeof from;
        if (recvfrom(s, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl) > 0) {
            inet_ntop(AF_INET, &from.sin_addr, out, cap);
            ok = 1;
        }
    }
    close(s);
    return ok;
}

/* Gateway address, as nbt_reset finds it (car 2026-11-12: from the head unit 169.254.255.255 gives "No route to host",
 * 255.255.255.255 works): the package's "gateway" file, then the HSFZ identification broadcast, then the known one. */
static void find_gateway(char *out, size_t cap) {
    FILE *f = fopen(GATEWAY_FILE, "r");
    if (f) {
        char line[64] = "";
        if (fgets(line, sizeof line, f)) line[strcspn(line, " \r\n")] = 0;
        fclose(f);
        struct in_addr t;
        if (inet_pton(AF_INET, line, &t) == 1) { snprintf(out, cap, "%s", line); return; }
    }
    if (discover_at("169.254.255.255", out, cap) || discover_at("255.255.255.255", out, cap)) return;
    snprintf(out, cap, "%s", DEFAULT_GW);
}

/* ------------------------------------------------------------------ readings */
typedef struct {
    int ok;                  /* engine values answered */
    int coolant, intake, rpm, load, map, baro, speed, run_s;
    double volt;
    long odo_km;
    int faults_read, active, stored;
    char active_list[96];
    double faults_at;
} Health;

static int pid(int fd, unsigned char p, unsigned char *d, int need) {
    unsigned char req[2] = {0x01, p}, r[16];
    int n = uds(fd, DME, req, 2, r, sizeof r, 2.0);
    if (n < 2 + need || r[0] != 0x41 || r[1] != p) return 0;
    memcpy(d, r + 2, need);
    return 1;
}

static void read_engine(int fd, Health *h) {
    unsigned char d[4];
    h->ok = 0;
    if (pid(fd, 0x05, d, 1)) { h->coolant = d[0] - 40; h->ok = 1; } else h->coolant = -999;
    h->rpm = pid(fd, 0x0C, d, 2) ? ((d[0] << 8) | d[1]) / 4 : -1;
    h->load = pid(fd, 0x04, d, 1) ? d[0] * 100 / 255 : -1;
    h->intake = pid(fd, 0x0F, d, 1) ? d[0] - 40 : -999;
    h->volt = pid(fd, 0x42, d, 2) ? ((d[0] << 8) | d[1]) / 1000.0 : -1;
    h->map = pid(fd, 0x0B, d, 1) ? d[0] : -1;
    h->baro = pid(fd, 0x33, d, 1) ? d[0] : -1;
    h->speed = pid(fd, 0x0D, d, 1) ? d[0] : -1;
    h->run_s = pid(fd, 0x1F, d, 2) ? (d[0] << 8) | d[1] : -1;
    unsigned char req[3] = {0x22, 0xD1, 0x0D}, r[32];
    int n = uds(fd, KOMBI, req, 3, r, sizeof r, 2.0);
    h->odo_km = (n >= 7 && r[0] == 0x62) ? ((long)r[3] << 24) | (r[4] << 16) | (r[5] << 8) | r[6] : -1;
}

static void read_faults(int fd, Health *h) {
    static unsigned char r[4096];
    unsigned char req[3] = {0x19, 0x02, 0xFF};
    int active = 0, stored = 0, answered = 0;
    h->active_list[0] = 0;
    for (int m = 0; m < NMOD; ++m) {
        int n = uds(fd, MODULES[m].addr, req, 3, r, sizeof r, 4.0);
        if (n < 3 || r[0] != 0x59) continue;
        answered++;
        for (int i = 3; i + 4 <= n; i += 4) {
            if (r[i + 3] & 0x03) {           /* bit0 failing now, bit1 failed during this drive */
                active++;
                size_t l = strlen(h->active_list);
                if (l + 16 < sizeof h->active_list)
                    snprintf(h->active_list + l, sizeof h->active_list - l, "%s%s %02X%02X",
                             l ? ", " : "", MODULES[m].name, r[i], r[i + 1]);
            } else stored++;
        }
    }
    h->faults_read = answered > 0;
    h->active = active; h->stored = stored;
    h->faults_at = now_s();
}

/* ------------------------------------------------------------------ verdicts (tune after real drives) */
static const char *v_coolant(const Health *h) {
    if (h->coolant == -999) return "?";
    if (h->coolant > 112) return "ACT";
    if (h->coolant > 105) return "watch";
    if (h->coolant < 70) return (h->run_s >= 0 && h->run_s < 900) ? "warming" : "watch";
    return "OK";
}
static const char *v_volt(const Health *h) {
    if (h->volt < 0) return "?";
    if (h->rpm > 400) {                       /* engine running: the alternator should be charging */
        if (h->volt < 12.8 || h->volt > 15.5) return "ACT";
        if (h->volt < 13.2 || h->volt > 15.0) return "watch";
        return "OK";
    }
    if (h->volt < 12.0) return "ACT";
    if (h->volt < 12.4) return "watch";
    return "OK";
}

static void make_rows(const Health *h, int link, char rows[ROWS][ROW_LEN]) {
    for (int i = 0; i < ROWS; ++i) rows[i][0] = 0;
    if (!link) {
        snprintf(rows[0], ROW_LEN, "No link to the car");
        snprintf(rows[1], ROW_LEN, "(gateway did not answer)");
        return;
    }
    if (!h->ok) {
        snprintf(rows[0], ROW_LEN, "Engine: no answer");
        snprintf(rows[1], ROW_LEN, "(ignition on?)");
    } else {
        snprintf(rows[0], ROW_LEN, "Coolant %d C  %s", h->coolant, v_coolant(h));
        if (h->volt >= 0) snprintf(rows[1], ROW_LEN, "Battery %.1f V  %s", h->volt, v_volt(h));
        else snprintf(rows[1], ROW_LEN, "Battery ?");
        if (h->rpm >= 0) snprintf(rows[2], ROW_LEN, h->rpm > 400 ? "Engine %d rpm, %d%% load" : "Engine off",
                                  h->rpm, h->load < 0 ? 0 : h->load);
        if (h->intake != -999) snprintf(rows[3], ROW_LEN, "Intake air %d C", h->intake);
        if (h->map >= 0 && h->baro >= 0) snprintf(rows[4], ROW_LEN, "Boost %+d kPa", h->map - h->baro);
    }
    if (h->odo_km >= 0) snprintf(rows[5], ROW_LEN, "Odometer %ld km", h->odo_km);
    if (h->faults_read) {
        snprintf(rows[6], ROW_LEN, h->active ? "Faults this drive: %d  watch" : "Faults this drive: none  OK", h->active);
        if (h->active) snprintf(rows[7], ROW_LEN, "%.40s", h->active_list);
        snprintf(rows[8], ROW_LEN, "Older faults stored: %d", h->stored);
    } else snprintf(rows[6], ROW_LEN, "Faults: reading ...");
    snprintf(rows[9], ROW_LEN, "Oil temp, fuel: not yet");
}

static void write_rows(char rows[ROWS][ROW_LEN]) {
    if (g_once) {
        for (int i = 0; i < ROWS; ++i) if (rows[i][0]) printf("%s\n", rows[i]);
        return;
    }
    FILE *f = fopen(OUT_FILE ".tmp", "w");
    if (!f) return;
    for (int i = 0; i < ROWS; ++i) fprintf(f, "%s\n", rows[i]);
    fclose(f);
    rename(OUT_FILE ".tmp", OUT_FILE);
}

static int page_open(void) {
    struct stat st;
    return stat(ACTIVE_FLAG, &st) == 0 && time(0) - st.st_mtime <= 5;
}

static void poll_once(const char *gw, Health *h, int faults_due) {
    char rows[ROWS][ROW_LEN];
    int fd = hsfz_connect(gw);
    if (fd < 0) { make_rows(h, 0, rows); write_rows(rows); return; }
    read_engine(fd, h);
    if (faults_due) read_faults(fd, h);
    close(fd);
    make_rows(h, 1, rows);
    write_rows(rows);
}

int main(int argc, char **argv) {
    char gw[64];
    Health h;
    memset(&h, 0, sizeof h);
    h.faults_at = -1e9;
    if (argc > 1 && !strcmp(argv[1], "--once")) {
        g_once = 1;
        if (argc > 2) snprintf(gw, sizeof gw, "%s", argv[2]); else find_gateway(gw, sizeof gw);
        fprintf(stderr, "gateway %s\n", gw);
        poll_once(gw, &h, 1);
        return h.ok ? 0 : 1;
    }
    logf_("start");
    gw[0] = 0;
    for (;;) {
        if (!page_open()) { gw[0] = 0; sleep(1); continue; }
        if (!gw[0]) { find_gateway(gw, sizeof gw); logf_("page open, gateway %s", gw); }
        poll_once(gw, &h, now_s() - h.faults_at > 30.0);
        sleep(2);
    }
}
