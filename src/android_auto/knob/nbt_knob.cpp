// nbt_knob - iDrive controller -> Android Auto key events for the original NBT (in the installed package, car-proven
// since 2026-11-07).
//
// Observe-only: it registers one DSI client on the MOST server and receives COPIES of the controller's status messages
// (FBlock 0xE5). Nothing is ever sent to the controller or any other function block; the HMI keeps receiving every event.
// The MOST/DSI client and the decoder are the NBT-AAD reference code (src/android_auto/knob/ref, PolyForm Noncommercial,
// notices kept), adapted for this NBT: libsys_dsi_colibry.so.3, MOSTSubSystemIntel.DSYSMOSTServer 4.4.
//
// Mapping wanted by the user (2026-11-06):
//   turn            -> AA rotary (keycode 65536, delta)   = focus forward / back in Android Auto
//   tilt up/down/left/right -> DPAD_UP/DOWN/LEFT/RIGHT (19..22)
//   push            -> DPAD_CENTER (23)
//   BACK key        -> BACK (4)
//   MENU, OPTION, COM, MEDIA, NAV, MAP -> NOT forwarded: they stay with iDrive (they leave the Android Auto page)
// Each event is one text line written to the FIFO the AA session reads (oneplus_native --capture-video, live mode):
//   "R <keycode> <delta>\n"   or   "K <keycode> <1 down|0 up>\n"
// While /fs/ram3/nbt-aa-hidden exists (Android Auto page not on screen) presses are not forwarded; releases always are,
// so the phone never keeps a key held down.
//
// usage: nbt_knob --probe                 only dlopen the DSI library and resolve its symbols (no connection)
//        nbt_knob [--seconds N] [--fifo PATH]   observe and forward (default FIFO /fs/ram3/nbt-aa-native/input.fifo)
// Environment: NBT_MOST_ROLE / NBT_MOST_MAJOR / NBT_MOST_MINOR, NBT_DSI_LIB, NBT_KNOB_INST, NBT_KNOB_CCS_INST,
//              NBT_KNOB_INVERT=1, NBT_KEY_<MENU|BACK|OPTIONS|COM|MEDIA|NAV|MAP>=keycode (0 = keep for iDrive)
#include "most/client.hpp"
#include "car/ccs_config.hpp"
#include "car/ccs_input.hpp"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {
volatile sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }
const char* g_fifo = "/fs/ram3/nbt-aa-native/input.fifo";
int g_fd = -1;
pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
unsigned g_sent = 0, g_dropped = 0;

void forward(const char* line, size_t n) {
    // DSI callback thread: never block (non-blocking FIFO, a full or absent reader just drops the event)
    pthread_mutex_lock(&g_mx);
    if (g_fd < 0) g_fd = open(g_fifo, O_WRONLY | O_NONBLOCK);
    if (g_fd >= 0 && write(g_fd, line, n) == (ssize_t)n) ++g_sent;
    else {
        ++g_dropped;
        if (g_fd >= 0 && errno != EAGAIN) { close(g_fd); g_fd = -1; }
    }
    pthread_mutex_unlock(&g_mx);
}

void onKnob(const nbt::car::CcsEvent& ev, void*) {
    char line[48];
    int n;
    // a hard key that stays with iDrive (MENU, OPTION, COM, MEDIA, NAV, MAP: keycode 0) while Android Auto is on
    // screen: tell the HMI page (aa_entry_plugin) that the driver wants iDrive, so it does not pull the page back
    if (!ev.keycode && ev.kind == 1 && ev.down && !ev.cancel && access("/fs/ram3/nbt-aa-hidden", F_OK) != 0) {
        int fd = open("/fs/ram3/nbt-aa-leave", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) close(fd);
        // and hide the picture at once: the plugin only reacts to a focus CHANGE of its hold state, which does not come
        // when the state had already lost the focus (car 2026-11-07: MENU without effect after a while)
        fd = open("/fs/ram3/nbt-aa-hidden", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) close(fd);
        printf("[Knob] %s: leave Android Auto (iDrive key)\n", ev.what);
        return;
    }
    if (!ev.keycode) return;
    const bool release = ev.kind == 1 && !ev.down;
    if (!release && access("/fs/ram3/nbt-aa-hidden", F_OK) == 0) {
        printf("[Knob] %s ignored: Android Auto page not on screen\n", ev.what);
        return;
    }
    if (ev.kind == 2) n = snprintf(line, sizeof(line), "R %u %d\n", (unsigned)ev.keycode, ev.delta);
    else n = snprintf(line, sizeof(line), "K %u %d\n", (unsigned)ev.keycode, ev.down ? 1 : 0);
    if (n <= 0 || n >= (int)sizeof(line)) return;
    printf("[Knob->AA] %s %s", ev.what, line);
    forward(line, (size_t)n);
}

// ---- steering wheel: volume and media keys (user report 2026-11-19: "steering-mounted controls for audio and track
// change don't work") ---------------------------------------------------------------------------------------------------
// Volume: Android Auto sound plays on the browser connection, which the car's master volume does not attenuate. The car's
// master level is observed (FBlock 0x20 AudioMaster inst 00, fkt 0xC08 Status, 5 bytes 01 00 LEVEL 00 MAX; car-verified
// in NBT-AAD car/volume_level.hpp: one message ~6 ms after each steering-wheel volume step) and sent to the session as
// "V <percent> 0", which sets the Android Auto media gain. Copies only: the HMI still draws its own volume bar.
// The last level is also kept in volume (RAM) so a new session starts at the right volume.
const char* const kRouteBuilt = "/fs/ram3/nbt-aa-native/audio-route.built";
const char* const kVolumeFile = "/fs/ram3/nbt-aa-native/volume";
int g_volLevel = -1, g_volMax = -1;
uint32_t g_volMs = 0;
void onVolume(const nbt::most::View& m, void*) {
    // main volume only (inst 00): other instances (rear-seat headphones of the 6FH entertainment, call volume, ...) must
    // not change the Android Auto gain. Logged once each so the car log shows them.
    static unsigned otherLogged = 0;
    if (m.inst != 0x00) {
        if (otherLogged < 5 && m.len >= 5) { ++otherLogged; printf("[Volume] 20/%02X level %u/%u ignored (not the main volume)\n", m.inst, m.data[2], m.data[4]); }
        return;
    }
    if (m.len < 5) return;
    const int lv = m.data[2], mx = m.data[4];
    if (mx <= 0 || lv > mx) return;
    if (lv == g_volLevel && mx == g_volMax && m.rxMs - g_volMs < 40) return;   // the same step, repeated within ms
    g_volLevel = lv; g_volMax = mx; g_volMs = m.rxMs;
    const int pct = lv * 100 / mx;
    char line[32];
    int n = snprintf(line, sizeof(line), "V %d 0\n", pct);
    printf("[Volume] car level %d/%d -> Android Auto %d%%\n", lv, mx, pct);
    forward(line, (size_t)n);
    FILE* f = fopen("/fs/ram3/nbt-aa-native/volume.tmp", "w");
    if (f) { fprintf(f, "%d\n", pct); fclose(f); rename("/fs/ram3/nbt-aa-native/volume.tmp", kVolumeFile); }
}

// Track keys: the steering-wheel buttons arrive on FBlock 0x15 (MFL) inst 02 (voice button fkt 0x512 and volume
// fkt 0x401 are known; the skip keys' function ids are not yet known for this car). Every other 0x15 message is logged
// (first 300) so one short drive with the button pressed shows the ids, and mfl.map in the package directory maps them:
//   <fkt hex> <first data byte hex> <Android keycode>     e.g.  "0402 01 87"  (87 NEXT, 88 PREVIOUS, 85 PLAY/PAUSE)
// A mapped media key is sent to the phone as a key tap only while Android Auto is the car's audio source (audio route
// built); otherwise the button stays with iDrive alone (the phone already gets Bluetooth AVRCP in Bluetooth mode).
// Call keys (5 CALL, 6 ENDCALL) and 84 (Assistant) are sent whenever Android Auto runs. They are copies: iDrive still
// gets the button, and its Bluetooth hands-free answers / ends the call itself, so map them only if that does not work.
struct MflKey { unsigned fkt, value, keycode; };
MflKey g_mfl[16];
int g_mflN = 0;
unsigned g_mflLogged = 0;
void loadMflMap() {
    FILE* f = fopen("/var/opt/hmi/data/rw/NbtAndroidAutoRT/mfl.map", "r");
    if (!f) { printf("[MFL] no mfl.map: steering-wheel track keys only logged\n"); return; }
    char l[96];
    while (g_mflN < 16 && fgets(l, sizeof(l), f)) {
        unsigned a, b, c;
        if (l[0] == '#' || sscanf(l, "%x %x %u", &a, &b, &c) != 3) continue;
        if (c != 85 && c != 87 && c != 88 && c != 5 && c != 6 && c != 84) {   // 126 / 127 dropped sessions
            printf("[MFL] keycode %u not allowed\n", c);
            continue;
        }
        g_mfl[g_mflN].fkt = a; g_mfl[g_mflN].value = b; g_mfl[g_mflN].keycode = c; ++g_mflN;
        printf("[MFL] fkt %03X value %02X -> Android key %u\n", a, b, c);
    }
    fclose(f);
}
// Steering-wheel roller (ported from valinet/nbt-android-auto; car trace on an original NBT, F30, 2026-11-12/17):
// FBlock 0x15 inst 02, op Status, one byte; every message arrives twice (once per HMI receiver, ~1 ms apart):
//   fkt C30  0x80 = one step up (NEXT 87), 0x7e = one step down (PREVIOUS 88), 0x7f = no step
//   fkt C31  01 pressed / 00 released (PLAY_PAUSE 85)
//   fkt 400  phone button: iDrive opens its contact list, scrolled with the same roller, so the roller stays with
//            iDrive until it has been still for 4 s (the list's own timeout; nothing on MOST marks it closing).
// The first C30 after start is a status report, not a turn (valinet: it skipped a song at boot). Keys go to the phone
// only while Android Auto is the car's audio source (audio route built); iDrive always gets the roller too.
// File wheel-off in the package folder switches this off.
bool g_wheelOn = true, g_wheelPushDown = false, g_wheelSeenC30 = false;
uint16_t g_wheelLastFkt = 0;
uint8_t g_wheelLastVal = 0;
uint32_t g_wheelLastMs = 0, g_listUntilMs = 0;
const uint32_t kListHoldMs = 4000;
void wheelKey(unsigned key, int down, const char* what) {
    char line[32];
    int n = snprintf(line, sizeof(line), "K %u %d\n", key, down);
    printf("[Wheel->AA] %s %s", what, line);
    forward(line, (size_t)n);
}
// true when the message was a roller / push / phone button message (handled here)
bool onWheel(const nbt::most::View& m) {
    if (!g_wheelOn || m.inst != 0x02 || m.op != 0xC || !m.len) return false;
    const uint32_t t = m.rxMs;
    if (m.fkt == 0x400) {
        if (m.data[0] == 1) {
            if ((int32_t)(g_listUntilMs - t) < (int32_t)kListHoldMs - 200)
                printf("[Wheel] phone button: roller stays with iDrive (phone list) until 4 s without a turn\n");
            g_listUntilMs = t + kListHoldMs;
        }
        return true;
    }
    if (m.fkt != 0xC30 && m.fkt != 0xC31) return false;
    const uint8_t v = m.data[0];
    if (m.fkt == g_wheelLastFkt && v == g_wheelLastVal && t - g_wheelLastMs < 40) return true;   // the second copy
    g_wheelLastFkt = m.fkt; g_wheelLastVal = v; g_wheelLastMs = t;
    if (m.fkt == 0xC30 && !g_wheelSeenC30) {
        g_wheelSeenC30 = true;
        printf("[Wheel] first roller status %02x after start: baseline only\n", v);
        return true;
    }
    if ((int32_t)(g_listUntilMs - t) > 0) {
        if (m.fkt == 0xC30) g_listUntilMs = t + kListHoldMs;
        printf("[Wheel] %s %02x: phone list open, left to iDrive\n", m.fkt == 0xC30 ? "roller" : "push", v);
        return true;
    }
    const bool aaSound = access(kRouteBuilt, F_OK) == 0;
    if (m.fkt == 0xC30) {
        const unsigned key = v == 0x80 ? 87 : (v == 0x7e ? 88 : 0);
        if (!key) return true;
        if (!aaSound) { printf("[Wheel] roller: Android Auto is not the audio source, left to iDrive\n"); return true; }
        wheelKey(key, 1, key == 87 ? "WHEEL_UP" : "WHEEL_DOWN");
        wheelKey(key, 0, key == 87 ? "WHEEL_UP" : "WHEEL_DOWN");
    } else if (v == 1 && !g_wheelPushDown) {
        if (!aaSound) return true;
        g_wheelPushDown = true;
        wheelKey(85, 1, "WHEEL_PUSH");
    } else if (v == 0 && g_wheelPushDown) {
        g_wheelPushDown = false;
        wheelKey(85, 0, "WHEEL_PUSH");
    }
    return true;
}

void onMfl(const nbt::most::View& m, void*) {
    if (m.fkt == 0x512 || m.fkt == 0x401) return;          // voice button (ccs_input) and volume (level above)
    if (onWheel(m)) return;
    if (g_mflLogged < 300) {
        ++g_mflLogged;
        printf("[MFL] 15/%02X fkt=%03X op=%X len=%u:", m.inst, m.fkt, m.op, (unsigned)m.len);
        for (size_t i = 0; i < m.len && i < 12; ++i) printf(" %02x", m.data[i]);
        printf("\n");
    }
    if (m.op != 0xC || !m.len) return;                      // Status only
    for (int i = 0; i < g_mflN; ++i) {
        if (g_mfl[i].fkt != m.fkt || g_mfl[i].value != m.data[0]) continue;
        const bool media = g_mfl[i].keycode != 5 && g_mfl[i].keycode != 6 && g_mfl[i].keycode != 84;
        if (media && access(kRouteBuilt, F_OK) != 0) { printf("[MFL] key %u: Android Auto is not the audio source, left to iDrive\n", g_mfl[i].keycode); return; }
        char line[48];
        int n = snprintf(line, sizeof(line), "K %u 1\nK %u 0\n", g_mfl[i].keycode, g_mfl[i].keycode);
        printf("[MFL->AA] key %u\n", g_mfl[i].keycode);
        forward(line, (size_t)n);
        return;
    }
}

// --trace: observe-only log of EVERY MOST message this client receives (header + first 48 payload bytes), to learn what the
// HMI sends to VideoApp's VideoConnectionMaster / VideoSink when a stock video page opens. Nothing is ever sent.
unsigned g_traced = 0;
int g_filter[8], g_filter_n = 0;          // --filter D7,D8: only these FBlocks (plus everything to/from our device)
unsigned g_mydev = 0;
void onTap(const nbt::most::View& m, nbt::most::MsgClass, void*) {
    char line[360]; int n;
    if (g_filter_n) {
        bool hit = g_mydev && (m.dst == g_mydev || m.src == g_mydev);
        for (int i = 0; i < g_filter_n && !hit; ++i) hit = m.fblock == g_filter[i];
        if (!hit) return;
    }
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    n = snprintf(line, sizeof(line), "%ld.%03ld %04X->%04X %02X/%02X fkt=%03X op=%X tx=%u var=%u/%u tag=%u len=%u:", (long)t.tv_sec,
                 t.tv_nsec / 1000000, m.src, m.dst, m.fblock, m.inst, m.fkt, m.op, m.txType, m.varTag, m.varVal, m.hdrTag,
                 (unsigned)m.len);
    for (size_t i = 0; i < m.len && i < 48 && n < (int)sizeof(line) - 4; ++i) n += snprintf(line + n, sizeof(line) - n, " %02x", m.data[i]);
    printf("%s\n", line);
    ++g_traced;
}

int probe() {
    const char* lib = getenv("NBT_DSI_LIB");
    if (!lib || !*lib) lib = "/opt/sys/lib/libsys_dsi_colibry.so.3";
    void* h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
    printf("dlopen(%s) -> %s\n", lib, h ? "OK" : dlerror());
    if (!h) return 3;
    // each line: one symbol, or "a|b" = either the Evo name or the original-NBT name
    static const char* const syms[] = {
        "_ZN14CDSIConnectionC1Ev", "_ZN14CDSIConnection3addER10CDSIClient", "_ZN14CDSIConnection5startEv",
        "_ZN10CDSIClientC1EPKcS1_ii", "_ZN10CDSIClient15setNotificationEj",
        "_ZN11CDSIOStreamC1ER11CDSIChannelP13CDSITCPSocketiN3DSI11RequestTypeENS4_7CommandEjRK8SPartyIDS9_i|"
        "_ZN11CDSIOStreamC1ER11CDSIChannelR13CDSITCPSocketN3DSI11RequestTypeENS4_7CommandEjRK8SPartyIDS9_i",
        "_ZN11CDSIOStream5writeEPKvj|_ZN11CDSIOStream5writeIiEEvRKT_",
        "_ZN11CDSIOStream4sendEi", "_ZN11CDSIOStreamD1Ev", "_ZTV10CDSIClient", 0
    };
    int missing = 0;
    for (int i = 0; syms[i]; ++i) {
        char name[200]; snprintf(name, sizeof(name), "%s", syms[i]);
        char* alt = strchr(name, '|'); if (alt) *alt++ = 0;
        void* p = dlsym(h, name);
        const char* used = name;
        if (!p && alt) { p = dlsym(h, alt); used = alt; }
        printf("  %-100s %s\n", used, p ? "ok" : "MISSING");
        if (!p) ++missing;
    }
    printf(missing ? "NBT_KNOB_PROBE_MISSING %d\n" : "NBT_KNOB_PROBE_OK\n", missing);
    return missing ? 4 : 0;
}
}

int main(int argc, char** argv) {
    long seconds = 0;
    bool trace = false;
    // --send dst,fblock,inst,fkt,op,tx,varTag,varVal,tag[,hexbytes]   (hex, may be given several times; sent in order
    // 300 ms apart once the MOST link is up). src = our own device id.
    std::vector<nbt::most::Msg> sends;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--probe")) return probe();
        if (!strcmp(argv[i], "--trace")) trace = true;
        else if (!strcmp(argv[i], "--filter") && i + 1 < argc) {
            char* s = argv[++i];
            while (*s && g_filter_n < 8) { g_filter[g_filter_n++] = (int)strtoul(s, &s, 16); if (*s == ',') ++s; else break; }
        }
        else if (!strcmp(argv[i], "--send") && i + 1 < argc) {
            unsigned v[9]; char hex[400] = "";
            int k = sscanf(argv[++i], "%x,%x,%x,%x,%x,%x,%x,%x,%x,%399s", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], hex);
            if (k < 9) { fprintf(stderr, "bad --send %s\n", argv[i]); return 2; }
            nbt::most::Msg m;
            m.dst = (uint16_t)v[0]; m.src = 0; m.fblock = (uint8_t)v[1]; m.inst = (uint8_t)v[2]; m.fkt = (uint16_t)v[3];
            m.opType = (uint8_t)v[4]; m.txType = v[5]; m.varTag = v[6]; m.varVal = v[7]; m.hdrTag = v[8];
            for (size_t j = 0; k == 10 && hex[j] && hex[j + 1]; j += 2) { unsigned b; sscanf(hex + j, "%2x", &b); m.data.push_back((uint8_t)b); }
            sends.push_back(m);
        }
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atol(argv[++i]);
        else if (!strcmp(argv[i], "--fifo") && i + 1 < argc) g_fifo = argv[++i];
        else { fprintf(stderr, "usage: %s --probe | [--seconds N] [--fifo /fs/ram3/...]\n", argv[0]); return 2; }
    }
    if (strncmp(g_fifo, "/fs/ram3/", 9) || strstr(g_fifo, "..") || seconds < 0) return 2;
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGTERM, onSignal);
    signal(SIGINT, onSignal);
    signal(SIGPIPE, SIG_IGN);
    // The user's mapping: only BACK of the hard keys goes to the phone (unless overridden in the environment).
    setenv("NBT_KEY_MENU", "0", 0);
    setenv("NBT_KEY_OPTIONS", "0", 0);
    setenv("NBT_KEY_BACK", "4", 0);
    setenv("NBT_KEY_COM", "0", 0);
    setenv("NBT_KEY_MEDIA", "0", 0);
    setenv("NBT_KEY_NAV", "0", 0);
    setenv("NBT_KEY_MAP", "0", 0);

    // Steering-wheel voice button: a long press (>= 750 ms) also sends the Assistant key (84) to the phone when the
    // package folder has ptt-assistant. Off by default: iDrive's own long press already starts the phone's voice
    // assistant over Bluetooth, and both together would start it twice.
    if (!access("/var/opt/hmi/data/rw/NbtAndroidAutoRT/ptt-assistant", F_OK)) setenv("NBT_PTT_ASSISTANT", "1", 1);
    g_wheelOn = access("/var/opt/hmi/data/rw/NbtAndroidAutoRT/wheel-off", F_OK) != 0;
    static nbt::most::Client most;
    static nbt::car::CcsInput ccs;
    if (!ccs.attach(most, nbt::car::readCcsConfig(), &onKnob, NULL)) return 3;
    loadMflMap();
    if (!most.addRoute(0x20, nbt::most::kAny, 0xC08, nbt::most::kOpStatus, &onVolume, NULL) ||
        !most.addRoute(0x15, nbt::most::kAny, nbt::most::kAny, nbt::most::kAny, &onMfl, NULL))
        printf("[Knob] no room on the MOST client: steering-wheel volume / track keys disabled\n");
    if (trace && !most.setTap(&onTap, NULL)) return 3;
    if (!most.start("NBTPROJ_KNOB")) return 3;
    printf("nbt_knob: observing the iDrive controller, forwarding to %s%s\n", g_fifo, seconds ? "" : " until SIGTERM");
    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    long last_report = 0;
    if (!sends.empty()) {
        for (int w = 0; w < 50 && most.state() != nbt::most::kUp; ++w) usleep(100000);
        g_mydev = most.deviceId();
        printf("nbt_knob: link %s, own device 0x%04X, sending %u message(s)\n", nbt::most::linkStateName(most.state()), g_mydev,
               (unsigned)sends.size());
        for (size_t i = 0; i < sends.size() && !g_stop; ++i) { most.send(sends[i]); usleep(300000); }
    } else {
        for (int w = 0; w < 50 && most.state() != nbt::most::kUp; ++w) usleep(100000);
        g_mydev = most.deviceId();
    }
    while (!g_stop) {
        usleep(200000);
        clock_gettime(CLOCK_MONOTONIC, &t);
        long el = (long)(t.tv_sec - t0.tv_sec);
        if (seconds && el >= seconds) break;
        if (el - last_report >= 30) {
            last_report = el;
            pthread_mutex_lock(&g_mx);
            printf("[Knob] t=%lds link=%s forwarded=%u dropped=%u traced=%u\n", el, nbt::most::linkStateName(most.state()), g_sent,
                   g_dropped, g_traced);
            pthread_mutex_unlock(&g_mx);
        }
    }
    printf("nbt_knob: end forwarded=%u dropped=%u\n", g_sent, g_dropped);
    fflush(stdout);
    _exit(0);   // the DSI library threads are not joined; leave without running their destructors
}
