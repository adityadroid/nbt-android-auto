/* nbt-health: live car health for the iDrive "Car health" page (PLAN-diagnostics.md).
 *
 * Asks the car's gateway (HSFZ, TCP 6801) for the values listed in a table (health.tab: OBD-II service 01 PIDs and
 * BMW measurement DIDs, with scaling and OK/watch/act limits), the odometer and every module's fault memory, and
 * writes up to ROWS short rows to a RAM file the HMI plugin shows: an overall line first, then the values, then the
 * faults (named from faults.tab). Read-only: services 01, 19 and 22 only; it never writes, clears or resets.
 *
 * Quiet unless the Car health page is open: the plugin touches ACTIVE_FLAG every second while the page is shown, and
 * this program only talks to the car while that file is fresh. Each poll opens a connection and closes it again.
 *
 *   nbt-health                              run as the head-unit daemon (started by boot.sh)
 *   nbt-health --once IP [TABLE] [FAULTS]   one poll, rows to stdout (testing from a Mac on the ENET cable)
 *
 * health.tab, one value per line ('#' comments), '|' separated:
 *   label|src|module|id|offset|length|signed|factor|add|unit|decimals|ok_lo|ok_hi|watch_lo|watch_hi|when
 *   src: obd (service 01 PID) or did (service 22 DID) on module (hex); offset/length: bytes of the data after the
 *   echoed PID/DID, big-endian; value = raw*factor+add; limits may be empty; when: run (limits only with the engine
 *   running), off (only with it stopped) or any. A label listed twice (run and off) shows the line for the state now.
 * faults.tab: "MODULE DTC text" per line, DTC = all three bytes, e.g. "DME 24F700 Fuel filter heater circuit open".
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

#define PKG          "/var/opt/hmi/data/rw/NbtAndroidAutoRT"
#define ACTIVE_FLAG  "/fs/ram3/nbt-health-active"
#define OUT_FILE     "/fs/ram3/nbt-health.txt"
#define LOG_FILE     "/fs/ram3/nbt-health.log"
#define TABLE_FILE   PKG "/health.tab"
#define FAULTS_FILE  PKG "/faults.tab"
#define GATEWAY_FILE PKG "/gateway"
#define DEFAULT_GW   "169.254.101.24"   /* this F10's gateway (ZGW), found by broadcast on 2026-11-22 */
#define TESTER       0xF5   /* not F4: the Mac tools use F4, and the gateway drops a connection when another opens
                               with the same tester address (car 2026-11-22). Only F4 and F5 are accepted. */
#define HSFZ_PORT    6801
#define IDENT_PORT   6811
#define ROWS         24
#define ROW_LEN      48
#define MAX_ITEMS    48
#define MAX_FAULTNAMES 256

enum { DME = 0x12, KOMBI = 0x60 };
enum { V_NONE, V_OK, V_WATCH, V_ACT };
static const char *VERDICT[] = {"", "OK", "watch", "ACT"};

static const struct { unsigned char addr; const char *name; } MODULES[] = {
    {0x12, "DME"}, {0x18, "EGS"}, {0x29, "DSC"}, {0x30, "EPS"}, {0x01, "ACSM"}, {0x40, "CAS"}, {0x72, "FRM"},
    {0x60, "KOMBI"}, {0x56, "FZD"}, {0x64, "PDC"}, {0x78, "IHKA"}, {0x17, "EKPS"}, {0x1C, "ICM"}, {0x5F, "FLA"},
    {0x63, "NBT"}, {0x10, "ZGW"},
};
#define NMOD (int)(sizeof(MODULES) / sizeof(MODULES[0]))

typedef struct {
    char label[24], unit[8], when[4];
    int obd, module, id, off, len, sgn, dec;
    double factor, add, ok_lo, ok_hi, w_lo, w_hi;
    int has[4];               /* ok_lo ok_hi watch_lo watch_hi present */
    int valid; double value;
} Item;

typedef struct { char module[8]; unsigned code; char text[40]; } FaultName;

static Item g_items[MAX_ITEMS];
static int g_n;
static FaultName g_fn[MAX_FAULTNAMES];
static int g_nfn;
static int g_once;

/* built-in table: the OBD values this car's DME answers (car 2026-11-22), used when health.tab is missing */
static const char *BUILTIN =
    "Coolant|obd|12|05|0|1|0|1|-40|C|0|70|105|50|112|run\n"
    "Battery|obd|12|42|0|2|0|0.001|0|V|1|13.2|15.0|12.8|15.5|run\n"
    "Battery|obd|12|42|0|2|0|0.001|0|V|1|12.4||12.0||off\n"
    "Engine speed|obd|12|0C|0|2|0|0.25|0|rpm|0|||||any\n"
    "Engine load|obd|12|04|0|1|0|0.392157|0|%|0|||||any\n"
    "Intake air|obd|12|0F|0|1|0|1|-40|C|0||60||75|run\n"
    "Boost (MAP)|obd|12|0B|0|1|0|1|0|kPa|0|||||any\n";

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

/* ------------------------------------------------------------------ tables */
static char *field(char **p) {
    char *s = *p, *e = strchr(s, '|');
    if (e) { *e = 0; *p = e + 1; } else *p = s + strlen(s);
    return s;
}

static int parse_item(char *line, Item *it) {
    memset(it, 0, sizeof *it);
    char *p = line, *f[16];
    for (int i = 0; i < 16; ++i) f[i] = field(&p);
    if (!f[0][0] || !f[3][0]) return 0;
    snprintf(it->label, sizeof it->label, "%s", f[0]);
    it->obd = !strcmp(f[1], "obd");
    it->module = (int)strtol(f[2], 0, 16);
    it->id = (int)strtol(f[3], 0, 16);
    it->off = atoi(f[4]); it->len = atoi(f[5]); it->sgn = atoi(f[6]);
    it->factor = f[7][0] ? atof(f[7]) : 1.0; it->add = atof(f[8]);
    snprintf(it->unit, sizeof it->unit, "%s", f[9]);
    it->dec = atoi(f[10]);
    double *lim[4] = {&it->ok_lo, &it->ok_hi, &it->w_lo, &it->w_hi};
    for (int i = 0; i < 4; ++i) if (f[11 + i][0]) { *lim[i] = atof(f[11 + i]); it->has[i] = 1; }
    snprintf(it->when, sizeof it->when, "%s", f[15][0] ? f[15] : "any");
    return it->len > 0 && it->len <= 4;
}

static void load_tables(const char *table, const char *faults) {
    static char buf[16384];
    size_t n = 0;
    FILE *f = fopen(table, "r");
    if (f) { n = fread(buf, 1, sizeof buf - 1, f); fclose(f); }
    if (!n) { n = strlen(BUILTIN); memcpy(buf, BUILTIN, n); }
    buf[n] = 0;
    g_n = 0;
    for (char *line = strtok(buf, "\n"); line && g_n < MAX_ITEMS; line = strtok(0, "\n")) {
        line[strcspn(line, "\r")] = 0;
        if (line[0] == '#' || !line[0]) continue;
        if (parse_item(line, &g_items[g_n])) g_n++;
    }
    g_nfn = 0;
    f = fopen(faults, "r");
    if (f) {
        char line[160];
        while (g_nfn < MAX_FAULTNAMES && fgets(line, sizeof line, f)) {
            char mod[8]; unsigned code; int used = 0;
            if (line[0] == '#' || sscanf(line, "%7s %x %n", mod, &code, &used) < 2) continue;
            line[strcspn(line, "\r\n")] = 0;
            snprintf(g_fn[g_nfn].module, sizeof g_fn[g_nfn].module, "%s", mod);
            g_fn[g_nfn].code = code;
            snprintf(g_fn[g_nfn].text, sizeof g_fn[g_nfn].text, "%s", line + used);
            g_nfn++;
        }
        fclose(f);
    }
    logf_("table: %d values, %d fault names", g_n, g_nfn);
}

static const char *fault_text(const char *mod, unsigned code) {
    for (int i = 0; i < g_nfn; ++i)
        if (g_fn[i].code == code && !strcmp(g_fn[i].module, mod)) return g_fn[i].text;
    return "";
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

/* One UDS request to tgt; response (after src/tgt) into resp. Skips acks, waits through 7F xx 78, drops stale answers. */
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
        if (ctrl != 0x0001 || n < 3 || p[0] != tgt) { free(p); continue; }
        int rl = (int)n - 2;
        if (rl >= 3 && p[2] == 0x7F && p[4] == 0x78) { free(p); deadline = now_s() + timeout; continue; }
        if (rl >= 1 && p[2] != 0x7F && (p[2] & 0x3F) != (req[0] & 0x3F)) { free(p); continue; }
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
    int link, engine_answer, running;
    long odo_km;
    int faults_read, active, stored;
    char active_list[ROWS][ROW_LEN];
    int nactive_rows;
    double faults_at;
} Health;

static void read_items(int fd, Health *h) {
    h->engine_answer = 0;
    for (int i = 0; i < g_n; ++i) {
        Item *it = &g_items[i];
        unsigned char req[3], r[256];
        int n, base;
        if (it->obd) {
            req[0] = 0x01; req[1] = (unsigned char)it->id; base = 2;
            n = uds(fd, (unsigned char)it->module, req, 2, r, sizeof r, 2.0);
            if (n < 2 || r[0] != 0x41 || r[1] != (unsigned char)it->id) n = -1;
        } else {
            req[0] = 0x22; req[1] = (unsigned char)(it->id >> 8); req[2] = (unsigned char)it->id; base = 3;
            n = uds(fd, (unsigned char)it->module, req, 3, r, sizeof r, 2.0);
            if (n < 3 || r[0] != 0x62) n = -1;
        }
        it->valid = 0;
        if (n < 0 || base + it->off + it->len > n) continue;
        unsigned long raw = 0;
        for (int b = 0; b < it->len; ++b) raw = (raw << 8) | r[base + it->off + b];
        double v = (double)raw;
        if (it->sgn) { unsigned long top = 1UL << (8 * it->len - 1); if (raw & top) v = (double)raw - 2.0 * (double)top; }
        it->value = v * it->factor + it->add;
        it->valid = 1;
        if (it->module == DME) h->engine_answer = 1;
        if (it->obd && it->id == 0x0C) h->running = it->value > 400;
    }
    unsigned char req[3] = {0x22, 0xD1, 0x0D}, r[32];
    int n = uds(fd, KOMBI, req, 3, r, sizeof r, 2.0);
    h->odo_km = (n >= 7 && r[0] == 0x62) ? ((long)r[3] << 24) | (r[4] << 16) | (r[5] << 8) | r[6] : -1;
}

static void read_faults(int fd, Health *h) {
    static unsigned char r[4096];
    unsigned char req[3] = {0x19, 0x02, 0xFF};
    int active = 0, stored = 0, answered = 0;
    h->nactive_rows = 0;
    for (int m = 0; m < NMOD; ++m) {
        int n = uds(fd, MODULES[m].addr, req, 3, r, sizeof r, 4.0);
        if (n < 3 || r[0] != 0x59) continue;
        answered++;
        for (int i = 3; i + 4 <= n; i += 4) {
            if (r[i + 3] & 0x03) {           /* bit0 failing now, bit1 failed during this drive */
                active++;
                if (h->nactive_rows < ROWS) {
                    unsigned code = ((unsigned)r[i] << 16) | (r[i + 1] << 8) | r[i + 2];
                    snprintf(h->active_list[h->nactive_rows++], ROW_LEN, "%s %06X %s", MODULES[m].name, code,
                             fault_text(MODULES[m].name, code));
                }
            } else stored++;
        }
    }
    h->faults_read = answered > 0;
    h->active = active; h->stored = stored;
    h->faults_at = now_s();
}

static int applies(const Item *it, int running) {
    if (!strcmp(it->when, "run")) return running;
    if (!strcmp(it->when, "off")) return !running;
    return 1;
}

static int verdict(const Item *it, int running) {
    if (!it->valid || !applies(it, running) || (!it->has[0] && !it->has[1])) return V_NONE;
    double v = it->value;
    if ((it->has[0] && v < it->ok_lo) || (it->has[1] && v > it->ok_hi)) {
        if ((it->has[2] && v < it->w_lo) || (it->has[3] && v > it->w_hi)) return V_ACT;
        return V_WATCH;
    }
    return V_OK;
}

static void make_rows(const Health *h, char rows[ROWS][ROW_LEN]) {
    for (int i = 0; i < ROWS; ++i) rows[i][0] = 0;
    if (!h->link) {
        snprintf(rows[0], ROW_LEN, "No link to the car");
        snprintf(rows[1], ROW_LEN, "(gateway did not answer)");
        return;
    }
    int r = 1, watch = 0, act = 0;
    for (int i = 0; i < g_n && r < ROWS; ++i) {
        const Item *it = &g_items[i];
        if (!it->valid) continue;
        int twin = 0, shown = 0;          /* a label listed for run and for off: show only the line for the state now */
        for (int j = 0; j < g_n; ++j) {
            if (j == i || strcmp(g_items[j].label, it->label)) continue;
            twin = 1;
            if (j < i && g_items[j].valid && applies(&g_items[j], h->running)) shown = 1;
        }
        if (shown || (twin && !applies(it, h->running))) continue;
        int v = verdict(it, h->running);
        if (v == V_WATCH) watch++;
        if (v == V_ACT) act++;
        snprintf(rows[r++], ROW_LEN, "%s %.*f %s%s%s", it->label, it->dec, it->value, it->unit, v ? "  " : "", VERDICT[v]);
    }
    if (!h->engine_answer && r < ROWS) snprintf(rows[r++], ROW_LEN, "Engine: no answer (ignition on?)");
    if (h->odo_km >= 0 && r < ROWS) snprintf(rows[r++], ROW_LEN, "Odometer %ld km", h->odo_km);
    if (h->faults_read) {
        if (h->active) watch++;
        if (r < ROWS) snprintf(rows[r++], ROW_LEN, h->active ? "Faults this drive: %d  watch" : "Faults this drive: none  OK", h->active);
        for (int i = 0; i < h->nactive_rows && r < ROWS - 1; ++i) snprintf(rows[r++], ROW_LEN, "%s", h->active_list[i]);
        if (r < ROWS) snprintf(rows[r++], ROW_LEN, "Older faults stored: %d", h->stored);
    } else if (r < ROWS) snprintf(rows[r++], ROW_LEN, "Faults: reading ...");
    if (act) snprintf(rows[0], ROW_LEN, "Check: %d to act, %d to watch", act, watch);
    else if (watch) snprintf(rows[0], ROW_LEN, "Overall: %d to watch", watch);
    else snprintf(rows[0], ROW_LEN, "Overall: all OK");
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
    h->link = fd >= 0;
    if (fd >= 0) {
        read_items(fd, h);
        if (faults_due) read_faults(fd, h);
        close(fd);
    }
    make_rows(h, rows);
    write_rows(rows);
}

int main(int argc, char **argv) {
    char gw[64];
    Health h;
    memset(&h, 0, sizeof h);
    h.faults_at = -1e9;
    if (argc > 1 && !strcmp(argv[1], "--once")) {
        g_once = 1;
        load_tables(argc > 3 ? argv[3] : TABLE_FILE, argc > 4 ? argv[4] : FAULTS_FILE);
        if (argc > 2) snprintf(gw, sizeof gw, "%s", argv[2]); else find_gateway(gw, sizeof gw);
        fprintf(stderr, "gateway %s\n", gw);
        poll_once(gw, &h, 1);
        return h.engine_answer ? 0 : 1;
    }
    logf_("start");
    load_tables(TABLE_FILE, FAULTS_FILE);
    gw[0] = 0;
    for (;;) {
        if (!page_open()) { gw[0] = 0; sleep(1); continue; }
        if (!gw[0]) { find_gateway(gw, sizeof gw); logf_("page open, gateway %s", gw); }
        poll_once(gw, &h, now_s() - h.faults_at > 30.0);
        sleep(2);
    }
}
