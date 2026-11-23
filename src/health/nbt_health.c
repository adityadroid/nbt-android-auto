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
 *   label|src|module|id|offset|length|signed|factor|add|unit|decimals|ok_lo|ok_hi|watch_lo|watch_hi|when[|route]
 *   src: obd (service 01 PID) or did (service 22 DID) on module (hex); offset/length: bytes of the data after the
 *   echoed PID/DID, big-endian; value = raw*factor+add; limits may be empty; when: run (limits only with the engine
 *   running), off (only with it stopped) or any. A label listed twice (run and off) shows the line for the state now.
 *   route (optional): ext = read in the extended session (10 03), dyn = through a temporary dynamic DID
 *   (2C 01 F300 <id> 01 <len>, 22 F300, 2C 03 F300); the module goes back to the default session after the poll.
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
#define DETAIL_FILE  "/fs/ram3/nbt-health-detail.txt"   /* DLINES lines per row: the page a row click opens */
#define LOG_FILE     "/fs/ram3/nbt-health.log"
#define TABLE_FILE   PKG "/health.tab"
#define FAULTS_FILE  PKG "/faults.tab"
#define GATEWAY_FILE PKG "/gateway"
#define DEFAULT_GW   "169.254.101.24"   /* this F10's gateway (ZGW), found by broadcast on 2026-11-22 */
#define TESTER       0xF5   /* not F4: the Mac tools use F4, and the gateway drops a connection when another opens
                               with the same tester address (car 2026-11-22). Only F4 and F5 are accepted. */
#define HSFZ_PORT    6801
#define IDENT_PORT   6811
#define ROWS         40
#define ROW_LEN      48
#define DLINES       8
#define MAX_FAULTS   64
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
    char label[24], unit[8], when[4], note[160];
    int obd, module, id, off, len, sgn, dec, route;   /* route: 0 plain, 1 extended session, 2 dynamic DID */
    double factor, add, ok_lo, ok_hi, w_lo, w_hi;
    int has[4];               /* ok_lo ok_hi watch_lo watch_hi present */
    int valid; double value;
} Item;

typedef struct { char module[8]; unsigned code; char text[40], full[200]; } FaultName;

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
    char *p = line, *f[18];
    for (int i = 0; i < 18; ++i) f[i] = field(&p);
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
    it->route = !strcmp(f[16], "ext") ? 1 : !strcmp(f[16], "dyn") ? 2 : 0;
    snprintf(it->note, sizeof it->note, "%s", f[17]);
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
        char line[320];
        while (g_nfn < MAX_FAULTNAMES && fgets(line, sizeof line, f)) {
            char mod[8]; unsigned code; int used = 0;
            if (line[0] == '#' || sscanf(line, "%7s %x %n", mod, &code, &used) < 2) continue;
            line[strcspn(line, "\r\n")] = 0;
            snprintf(g_fn[g_nfn].module, sizeof g_fn[g_nfn].module, "%s", mod);
            g_fn[g_nfn].code = code;
            char *full = strchr(line + used, '|');                  /* "short|full text" */
            if (full) *full++ = 0;
            snprintf(g_fn[g_nfn].text, sizeof g_fn[g_nfn].text, "%s", line + used);
            snprintf(g_fn[g_nfn].full, sizeof g_fn[g_nfn].full, "%s", full ? full : line + used);
            g_nfn++;
        }
        fclose(f);
    }
    logf_("table: %d values, %d fault names", g_n, g_nfn);
}

static const FaultName *fault_name(const char *mod, unsigned code) {
    for (int i = 0; i < g_nfn; ++i)
        if (g_fn[i].code == code && !strcmp(g_fn[i].module, mod)) return &g_fn[i];
    return 0;
}

/* fault areas, in the order the page lists them */
static const char *AREAS[] = {"Engine", "Chassis", "Body", "Climate", "Electronics"};
#define NAREA 5
static int area_of(const char *m) {
    static const char *map[][2] = {{"DME", "0"}, {"EGS", "0"}, {"EKPS", "0"}, {"DSC", "1"}, {"EPS", "1"}, {"ICM", "1"},
                                   {"ACSM", "1"}, {"PDC", "1"}, {"CAS", "2"}, {"FRM", "2"}, {"FZD", "2"}, {"FLA", "2"},
                                   {"IHKA", "3"}};
    for (unsigned i = 0; i < sizeof map / sizeof map[0]; ++i) if (!strcmp(map[i][0], m)) return map[i][1][0] - '0';
    return 4;   /* KOMBI, NBT, ZGW */
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
    int rpa_ok; unsigned char rpa[8];   /* DSC 22 4005: flat tyre monitor status (dsc_01 STATUS_REIFENPANNENANZEIGE) */
    long odo_km;
    int faults_read, active, stored;
    struct { char mod[8]; unsigned code; unsigned char st; } fault[MAX_FAULTS];
    int nfault;
    double faults_at;
} Health;

static void read_items(int fd, Health *h) {
    unsigned char ext_on[256];
    memset(ext_on, 0, sizeof ext_on);
    h->engine_answer = 0;
    for (int i = 0; i < g_n; ++i) {
        Item *it = &g_items[i];
        unsigned char req[8], r[256];
        int n, base;
        if (it->route && !ext_on[it->module & 0xFF]) {          /* extended session for routed values */
            unsigned char s3[2] = {0x10, 0x03};
            uds(fd, (unsigned char)it->module, s3, 2, r, sizeof r, 2.0);
            ext_on[it->module & 0xFF] = 1;
        }
        if (it->route == 2) {                                   /* temporary dynamic DID F300 */
            unsigned char clr[4] = {0x2C, 0x03, 0xF3, 0x00};
            unsigned char def[8] = {0x2C, 0x01, 0xF3, 0x00, (unsigned char)(it->id >> 8), (unsigned char)it->id, 0x01,
                                    (unsigned char)(it->off + it->len)};
            unsigned char rd[3] = {0x22, 0xF3, 0x00};
            uds(fd, (unsigned char)it->module, clr, 4, r, sizeof r, 2.0);
            uds(fd, (unsigned char)it->module, def, 8, r, sizeof r, 2.0);
            n = uds(fd, (unsigned char)it->module, rd, 3, r, sizeof r, 2.0);
            uds(fd, (unsigned char)it->module, clr, 4, req, sizeof req, 2.0);
            base = 3;
            if (n < 3 || r[0] != 0x62) n = -1;
            goto decode;
        }
        if (it->obd) {
            req[0] = 0x01; req[1] = (unsigned char)it->id; base = 2;
            n = uds(fd, (unsigned char)it->module, req, 2, r, sizeof r, 2.0);
            if (n < 2 || r[0] != 0x41 || r[1] != (unsigned char)it->id) n = -1;
        } else {
            req[0] = 0x22; req[1] = (unsigned char)(it->id >> 8); req[2] = (unsigned char)it->id; base = 3;
            n = uds(fd, (unsigned char)it->module, req, 3, r, sizeof r, 2.0);
            if (n < 3 || r[0] != 0x62) n = -1;
        }
    decode:
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
    for (int m = 0; m < 256; ++m)                               /* back to the default session */
        if (ext_on[m]) { unsigned char s1[2] = {0x10, 0x01}; unsigned char rr[16]; uds(fd, (unsigned char)m, s1, 2, rr, sizeof rr, 2.0); }
    unsigned char req[3] = {0x22, 0xD1, 0x0D}, r[32];
    int n = uds(fd, KOMBI, req, 3, r, sizeof r, 2.0);
    h->odo_km = (n >= 7 && r[0] == 0x62) ? ((long)r[3] << 24) | (r[4] << 16) | (r[5] << 8) | r[6] : -1;
}

/* flat tyre monitor (RPA) in the DSC: b0 bits 01 warning, 04 running, 08 calibrating; b2 % of the warning limit;
   b7 which wheel (1 front left, 2 front right, 3 rear left, 4 rear right). Car 2026-11-23: answers in the default session. */
static void read_rpa(int fd, Health *h) {
    unsigned char req[3] = {0x22, 0x40, 0x05}, r[64];
    int n = uds(fd, 0x29, req, 3, r, sizeof r, 2.0);
    h->rpa_ok = n >= 11 && r[0] == 0x62;
    if (h->rpa_ok) memcpy(h->rpa, r + 3, 8);
}

static const char *WHEEL[] = {"", "front left", "front right", "rear left", "rear right"};

static void read_faults(int fd, Health *h) {
    static unsigned char r[4096];
    unsigned char req[3] = {0x19, 0x02, 0xFF};
    int active = 0, stored = 0, answered = 0;
    h->nfault = 0;
    for (int m = 0; m < NMOD; ++m) {
        int n = uds(fd, MODULES[m].addr, req, 3, r, sizeof r, 4.0);
        if (n < 3 || r[0] != 0x59) continue;
        answered++;
        for (int i = 3; i + 4 <= n; i += 4) {
            unsigned char st = r[i + 3];
            int now = st & 0x03;             /* bit0 failing now, bit1 failed during this drive */
            if (!now && !(st & 0x08)) continue;   /* only confirmed ones count as stored (car: 1271 entries otherwise) */
            if (now) active++; else stored++;
            /* active ones always get a slot; stored ones only fill the first half */
            if (h->nfault < MAX_FAULTS && (now || h->nfault < MAX_FAULTS / 2)) {
                snprintf(h->fault[h->nfault].mod, sizeof h->fault[0].mod, "%s", MODULES[m].name);
                h->fault[h->nfault].code = ((unsigned)r[i] << 16) | (r[i + 1] << 8) | r[i + 2];
                h->fault[h->nfault].st = r[i + 3];
                h->nfault++;
            }
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

/* word-wrap s into detail lines d[*k..], ROW_LEN-1 wide, at most DLINES lines in all */
static void wrap(char d[DLINES][ROW_LEN], int *k, const char *s) {
    while (*s && *k < DLINES) {
        while (*s == ' ') ++s;
        int n = (int)strlen(s), cut = n;
        if (n > ROW_LEN - 1) {
            cut = ROW_LEN - 1;
            while (cut > 0 && s[cut] != ' ') --cut;
            if (cut == 0) cut = ROW_LEN - 1;
        }
        snprintf(d[(*k)++], ROW_LEN, "%.*s", cut, s);
        s += cut;
    }
}

static const char *status_text(unsigned char st) {
    if (st & 0x01) return "Failing right now";
    if (st & 0x02) return "Failed during this drive";
    if (st & 0x08) return "Stored from an earlier drive";
    return "Pending (seen once, not confirmed)";
}

static void fault_label(const Health *h, int i, char *out, int cap, int full) {
    const FaultName *fn = fault_name(h->fault[i].mod, h->fault[i].code);
    snprintf(out, cap, "%s %06X %s", h->fault[i].mod, h->fault[i].code,
             fn ? (full ? fn->full : fn->text) : (full ? "No name yet (not in faults.tab)" : ""));
}

static void make_rows(const Health *h, char rows[ROWS][ROW_LEN], char d[ROWS][DLINES][ROW_LEN]) {
    memset(rows, 0, ROWS * ROW_LEN);
    memset(d, 0, ROWS * DLINES * ROW_LEN);
    if (!h->link) {
        snprintf(rows[0], ROW_LEN, "No link to the car");
        snprintf(rows[1], ROW_LEN, "(gateway did not answer)");
        return;
    }
    int r = 1, watch = 0, act = 0, k0 = 0;
    char names[160] = "";
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
        int v = verdict(it, h->running), k = 0;
        if (v == V_WATCH) watch++;
        if (v == V_ACT) act++;
        if (v >= V_WATCH) {
            size_t l = strlen(names);
            snprintf(names + l, sizeof names - l, "%s%s", l ? ", " : "", it->label);
            if (k0 < DLINES) snprintf(d[0][k0++], ROW_LEN, "%s %.*f %s: %s", it->label, it->dec, it->value, it->unit, VERDICT[v]);
        }
        snprintf(rows[r], ROW_LEN, "%s %.*f %s%s%s", it->label, it->dec, it->value, it->unit, v ? "  " : "", VERDICT[v]);
        snprintf(d[r][k++], ROW_LEN, "%s: %.*f %s", it->label, it->dec, it->value, it->unit);
        snprintf(d[r][k++], ROW_LEN, "Status: %s", v == V_NONE ? "for information (no limits)" : v == V_OK ? "OK" :
                 v == V_WATCH ? "watch - outside the normal range" : "ACT - get it checked");
        if (it->has[0] || it->has[1]) {
            char lo[24] = "", hi[24] = "";
            if (it->has[0]) snprintf(lo, sizeof lo, "%.*f", it->dec, it->ok_lo);
            if (it->has[1]) snprintf(hi, sizeof hi, "%.*f", it->dec, it->ok_hi);
            snprintf(d[r][k++], ROW_LEN, "Normal: %s%s%s %s", it->has[0] ? lo : "up to ", it->has[0] && it->has[1] ? " to " : it->has[0] ? " or more" : "", hi, it->unit);
        }
        if (it->has[2] || it->has[3]) {
            char lo[24] = "", hi[24] = "";
            if (it->has[2]) snprintf(lo, sizeof lo, "below %.*f", it->dec, it->w_lo);
            if (it->has[3]) snprintf(hi, sizeof hi, "above %.*f", it->dec, it->w_hi);
            snprintf(d[r][k++], ROW_LEN, "Act: %s%s%s %s", lo, it->has[2] && it->has[3] ? " or " : "", hi, it->unit);
        }
        if (strcmp(it->when, "any")) snprintf(d[r][k++], ROW_LEN, "Judged with the engine %s", !strcmp(it->when, "run") ? "running" : "off");
        wrap(d[r], &k, it->note);
        r++;
    }
    if (h->rpa_ok && r < ROWS) {
        const unsigned char *t = h->rpa;
        int k = 0, pos = t[7] >= 1 && t[7] <= 4 ? t[7] : 0;
        if (t[0] & 0x01) {
            act++;
            size_t l = strlen(names);
            snprintf(names + l, sizeof names - l, "%sTyre %s", l ? ", " : "", pos ? WHEEL[pos] : "low");
            snprintf(rows[r], ROW_LEN, "Tyres: LOW %s  ACT", pos ? WHEEL[pos] : "(wheel unknown)");
        } else if (t[0] & 0x08) snprintf(rows[r], ROW_LEN, "Tyres: monitor learning, no warning");
        else snprintf(rows[r], ROW_LEN, "Tyres: no pressure loss  OK");
        snprintf(d[r][k++], ROW_LEN, "Flat tyre monitor (wheel-speed based)");
        snprintf(d[r][k++], ROW_LEN, "Warning: %s", t[0] & 0x01 ? "ON" : "off");
        snprintf(d[r][k++], ROW_LEN, "Low tyre: %s", pos ? WHEEL[pos] : "none flagged");
        snprintf(d[r][k++], ROW_LEN, "Closeness to warning limit: %d%%", t[2]);
        snprintf(d[r][k++], ROW_LEN, "Learning after reset: %s", t[0] & 0x08 ? "yes (drive ~20 min)" : "done");
        snprintf(d[r][k++], ROW_LEN, "No pressures: the car has no wheel sensors.");
        snprintf(d[r][k++], ROW_LEN, "Reset after inflating: Vehicle Info >");
        snprintf(d[r][k++], ROW_LEN, "Flat Tyre Monitor > Reset, then drive.");
        r++;
    }
    if (!h->engine_answer && r < ROWS) {
        snprintf(rows[r], ROW_LEN, "Engine: no answer (ignition on?)");
        snprintf(d[r][0], ROW_LEN, "The engine module did not answer.");
        snprintf(d[r][1], ROW_LEN, "Switch the ignition on and reopen the page.");
        r++;
    }
    if (h->odo_km >= 0 && r < ROWS) { snprintf(rows[r], ROW_LEN, "Odometer %ld km", h->odo_km); snprintf(d[r][0], ROW_LEN, "Odometer %ld km (from the cluster)", h->odo_km); r++; }
    if (h->faults_read) {
        int per[NAREA] = {0}, old[NAREA] = {0};
        for (int i = 0; i < h->nfault; ++i) (h->fault[i].st & 0x03 ? per : old)[area_of(h->fault[i].mod)]++;
        if (h->active) {
            watch++;
            size_t l = strlen(names);
            snprintf(names + l, sizeof names - l, "%s%d fault%s", l ? ", " : "", h->active, h->active > 1 ? "s" : "");
        }
        if (!h->active && r < ROWS) {
            snprintf(rows[r], ROW_LEN, "Faults this drive: none  OK");
            snprintf(d[r][0], ROW_LEN, "No module reported a fault during this drive.");
            r++;
        }
        for (int a = 0; a < NAREA; ++a) {
            if (!per[a] || r >= ROWS - 1) continue;
            int k = 0;
            snprintf(rows[r], ROW_LEN, "%s faults: %d  watch", AREAS[a], per[a]);
            for (int i = 0; i < h->nfault && k < DLINES; ++i)
                if (h->fault[i].st & 0x03 && area_of(h->fault[i].mod) == a) fault_label(h, i, d[r][k++], ROW_LEN, 0);
            r++;
            for (int i = 0; i < h->nfault && r < ROWS - 1; ++i) {
                if (!(h->fault[i].st & 0x03) || area_of(h->fault[i].mod) != a) continue;
                char full[260];
                fault_label(h, i, rows[r], ROW_LEN, 0);
                k = 0;
                snprintf(d[r][k++], ROW_LEN, "%s %06X  (%s)", h->fault[i].mod, h->fault[i].code, AREAS[a]);
                snprintf(d[r][k++], ROW_LEN, "%s", status_text(h->fault[i].st));
                const FaultName *fn = fault_name(h->fault[i].mod, h->fault[i].code);
                snprintf(full, sizeof full, "%s", fn ? fn->full : "No name yet: tell Claude the module and code.");
                wrap(d[r], &k, full);
                r++;
            }
        }
        if (r < ROWS) {
            char by[96] = "";
            for (int a = 0; a < NAREA; ++a) if (old[a]) {
                size_t l = strlen(by);
                snprintf(by + l, sizeof by - l, "%s%s %d", l ? ", " : "", AREAS[a], old[a]);
            }
            snprintf(rows[r], ROW_LEN, h->stored ? "Older faults: %d (%s)" : "Older faults: none", h->stored, by);
            int k = 0;
            snprintf(d[r][k++], ROW_LEN, "Stored from earlier drives (not active):");
            for (int i = 0; i < h->nfault && k < DLINES; ++i)
                if (!(h->fault[i].st & 0x03)) fault_label(h, i, d[r][k++], ROW_LEN, 0);
            if (!h->stored) snprintf(d[r][0], ROW_LEN, "No older faults stored.");
            r++;
        }
    } else if (r < ROWS) snprintf(rows[r++], ROW_LEN, "Faults: reading ...");
    if (act) snprintf(rows[0], ROW_LEN, "ACT: %s", names);
    else if (watch) snprintf(rows[0], ROW_LEN, "Watch: %s", names);
    else snprintf(rows[0], ROW_LEN, "Overall: all OK");
    if (!k0 && !h->active) snprintf(d[0][k0++], ROW_LEN, "Every value is in its normal range.");
    if (h->active && k0 < DLINES) snprintf(d[0][k0++], ROW_LEN, "%d fault%s this drive (see the fault rows)", h->active, h->active > 1 ? "s" : "");
}

static void write_detail(char d[ROWS][DLINES][ROW_LEN]) {
    if (g_once) {
        for (int i = 0; i < ROWS; ++i) for (int k = 0; k < DLINES; ++k) if (d[i][k][0]) printf("    [%d] %s\n", i, d[i][k]);
        return;
    }
    FILE *f = fopen(DETAIL_FILE ".tmp", "w");
    if (!f) return;
    for (int i = 0; i < ROWS; ++i) for (int k = 0; k < DLINES; ++k) fprintf(f, "%s\n", d[i][k]);
    fclose(f);
    rename(DETAIL_FILE ".tmp", DETAIL_FILE);
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
    static char rows[ROWS][ROW_LEN], detail[ROWS][DLINES][ROW_LEN];
    int fd = hsfz_connect(gw);
    h->link = fd >= 0;
    if (fd >= 0) {
        read_items(fd, h);
        read_rpa(fd, h);
        if (faults_due) read_faults(fd, h);
        close(fd);
    }
    make_rows(h, rows, detail);
    write_rows(rows);
    write_detail(detail);
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
