// See aa_mic.hpp.
#include "aa_mic.hpp"
#include "a2i_capture.hpp"
#include "aa_audio.hpp"
#include "aa_session.hpp"
#include "audio/voice_scenario.hpp"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace aa_mic {

namespace {

// One way to read the car microphone: endpoint, its native format, and which channel is the mic.
struct Source {
    const char* endpoint;
    unsigned rate, channels, pick;   // pick: 1-based channel taken; 0 = average of all channels
};

nbt::AASession* g_session = 0;
nbt::A2iCapture* g_capture = 0;
nbt::VoiceScenario* g_voice = 0;
Source g_src[2];             // [0] configured / default, [1] fallback when [0] cannot be opened
int g_nsrc = 0;
Source g_cur;                // the one in use (capture thread reads it)
float g_gain = 1.0f;
bool g_open_failed = false;
int g_cur_index = -1;        // index into g_src of the running capture
int g_next_index = 0;        // where the next start begins (1 after a switch to the fallback)
bool g_switched = false;     // already moved to the fallback this session
volatile unsigned g_zero_frames = 0;   // input frames in a row that were exactly 0 while the phone had the mic open
const char* const kUseFallback = "/fs/ram3/nbt-aa-native/mic-fallback";   // RAM: until the next reboot

pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
int g_pending = 0;   // 1 = phone opened the mic, 2 = closed it (latest wins)

// Conversion to 16 kHz mono (capture thread only): channel pick -> low-pass (biquad, ~7 kHz, against aliasing when
// downsampling from 22.05 / 44.1 kHz) -> linear interpolation -> gain.
double g_pos = 0;          // fractional read position into the next chunk
float g_last = 0;          // last filtered input sample of the previous chunk
float g_b0 = 1, g_b1 = 0, g_b2 = 0, g_a1 = 0, g_a2 = 0, g_z1 = 0, g_z2 = 0;
float g_buf[8192];
int16_t g_out[4096];
// Level meter: one line per ~1 s of audio while the phone has the mic open (silence = wrong endpoint / no scenario).
unsigned g_meter_n = 0, g_meter_peak = 0, g_meter_lines = 0;

unsigned envU(const char* name, unsigned def) {
    const char* v = getenv(name);
    if (!v || !*v) return def;
    char* end = 0;
    unsigned long x = strtoul(v, &end, 0);
    return (end != v && !*end) ? (unsigned)x : def;
}

bool validRate(unsigned r) {
    return r == 8000 || r == 11025 || r == 16000 || r == 22050 || r == 24000 || r == 32000 || r == 44100 || r == 48000;
}

void setLowpass(unsigned rate) {
    g_z1 = g_z2 = 0;
    if (rate <= 16000) { g_b0 = 1; g_b1 = g_b2 = g_a1 = g_a2 = 0; return; }
    // RBJ biquad low-pass, Butterworth Q, cut-off 7 kHz
    const double w = 2.0 * 3.14159265358979 * 7000.0 / rate, c = cos(w), al = sin(w) / (2.0 * 0.7071);
    const double a0 = 1 + al;
    g_b0 = (float)((1 - c) / 2 / a0); g_b1 = (float)((1 - c) / a0); g_b2 = g_b0;
    g_a1 = (float)(-2 * c / a0); g_a2 = (float)((1 - al) / a0);
}

void meter(const int16_t* s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        unsigned a = (unsigned)(s[i] < 0 ? -(int)s[i] : s[i]);
        if (a > g_meter_peak) g_meter_peak = a;
    }
    g_meter_n += (unsigned)n;
    if (g_meter_n >= 16000) {
        if (g_meter_lines < 120 || g_meter_lines % 60 == 0)
            printf("AA_MIC level: peak %u of 32767 over 1 s%s\n", g_meter_peak,
                   g_meter_peak < 8 ? " (silence: wrong endpoint, or the mic scenario is not active)" : "");
        ++g_meter_lines;
        g_meter_n = g_meter_peak = 0;
    }
}

void post(const int16_t* s, size_t n) {
    if (!n || !g_session) return;
    g_session->postMicData(reinterpret_cast<const uint8_t*>(s), n * 2);
    if (g_session->micOpen()) meter(s, n);
}

int16_t clip(float v) {
    v *= g_gain;
    return (int16_t)(v > 32767.f ? 32767 : (v < -32768.f ? -32768 : (int)v));
}

// Capture thread: endpoint format -> 16 kHz mono -> the session (it drops the audio while the phone has the mic closed).
void onCapture(const int16_t* pcm, size_t frames, void*) {
    const unsigned ch = g_cur.channels;
    if (g_session && g_session->micOpen()) {
        bool zero = true;
        for (size_t i = 0; i < frames * ch && zero; ++i) zero = pcm[i] == 0;
        g_zero_frames = zero ? g_zero_frames + (unsigned)frames : 0;
    }
    while (frames) {
        const size_t n = frames > sizeof(g_buf) / sizeof(g_buf[0]) ? sizeof(g_buf) / sizeof(g_buf[0]) : frames;
        for (size_t i = 0; i < n; ++i) {
            float x;
            if (g_cur.pick) x = pcm[i * ch + g_cur.pick - 1];
            else { int s = 0; for (unsigned k = 0; k < ch; ++k) s += pcm[i * ch + k]; x = (float)s / (float)ch; }
            const float y = g_b0 * x + g_z1;   // transposed direct form II
            g_z1 = g_b1 * x - g_a1 * y + g_z2;
            g_z2 = g_b2 * x - g_a2 * y;
            g_buf[i] = y;
        }
        size_t o = 0;
        if (g_cur.rate == 16000) {
            for (size_t i = 0; i < n; ++i) {
                g_out[o++] = clip(g_buf[i]);
                if (o == sizeof(g_out) / sizeof(g_out[0])) { post(g_out, o); o = 0; }
            }
        } else {
            const double step = (double)g_cur.rate / 16000.0;
            while (g_pos < (double)n) {
                const size_t i = (size_t)g_pos;
                const float f = (float)(g_pos - (double)i);
                const float a = i ? g_buf[i - 1] : g_last, b = g_buf[i];
                g_out[o++] = clip(a + (b - a) * f);
                if (o == sizeof(g_out) / sizeof(g_out[0])) { post(g_out, o); o = 0; }
                g_pos += step;
            }
            g_pos -= (double)n;
            g_last = g_buf[n - 1];
        }
        post(g_out, o);
        pcm += n * ch;
        frames -= n;
    }
}

// Session thread: never blocks; poll() does the work.
void onRequest(bool open, void*) {
    pthread_mutex_lock(&g_lock);
    g_pending = open ? 1 : 2;
    pthread_mutex_unlock(&g_lock);
}

bool readSource(Source& s, const char* ep, const char* rate, const char* channels, const char* pick) {
    const char* e = getenv(ep);
    if (!e || !*e) return false;
    s.endpoint = e;
    s.rate = envU(rate, 16000);
    if (!validRate(s.rate)) s.rate = 16000;
    s.channels = envU(channels, 1);
    if (s.channels < 1 || s.channels > 8) s.channels = 1;
    s.pick = envU(pick, s.channels == 1 ? 1 : 0);
    if (s.pick > s.channels) s.pick = 0;
    return true;
}

// Opens and starts g_src[first..]: the first that works is used. Main loop thread.
bool startFrom(int first) {
    for (int i = first; i < g_nsrc; ++i) {
        const Source& src = g_src[i];
        nbt::A2iCapture* c = new nbt::A2iCapture("AA_MIC");
        g_cur = src;
        g_pos = 0;
        g_last = 0;
        g_zero_frames = 0;
        setLowpass(src.rate);
        const unsigned chunk = (src.rate / 50) * 2 * src.channels;   // ~20 ms, rounded down to a power of two by open()
        if (c->open(src.endpoint, src.rate, src.channels, chunk) && c->start(&onCapture, 0)) {
            g_capture = c;
            g_cur_index = i;
            printf("AA_MIC capture started from %s (kept open until the session ends)\n", src.endpoint);
            return true;
        }
        c->stop();
        if (!c->abandoned()) delete c;
    }
    return false;
}

} // namespace

void setup(nbt::AASession& session) {
    g_session = &session;
    g_open_failed = false;
    g_switched = false;
    g_cur_index = -1;
    g_next_index = 0;
    g_zero_frames = 0;
    g_pending = 0;
    g_pos = 0;
    g_last = 0;
    g_meter_n = g_meter_peak = g_meter_lines = 0;
    session.setMicRequestCallback(&onRequest, 0);
    const char* off = getenv("NBT_MIC");
    g_nsrc = 0;
    if (!(off && !strcmp(off, "0"))) {
        if (readSource(g_src[g_nsrc], "NBT_MIC_ENDPOINT", "NBT_MIC_RATE", "NBT_MIC_CHANNELS", "NBT_MIC_PICK")) ++g_nsrc;
        if (g_nsrc && readSource(g_src[g_nsrc], "NBT_MIC_FALLBACK", "NBT_MIC_FALLBACK_RATE", "NBT_MIC_FALLBACK_CHANNELS",
                                 "NBT_MIC_FALLBACK_PICK"))
            ++g_nsrc;
    }
    if (!g_nsrc) {
        printf("AA_MIC car microphone: off (NBT_MIC=0 or no endpoint; the phone gets no mic audio)\n");
        return;
    }
    if (g_nsrc == 2 && access(kUseFallback, F_OK) == 0) {   // the primary was silent earlier since this boot
        g_src[0] = g_src[1];
        g_nsrc = 1;
        printf("AA_MIC primary source was silent earlier (mic-fallback): using the fallback directly\n");
    }
    const int db = atoi(getenv("NBT_MIC_GAIN_DB") ? getenv("NBT_MIC_GAIN_DB") : "0");
    g_gain = (float)pow(10.0, (db < -20 ? -20 : (db > 30 ? 30 : db)) / 20.0);
    for (int i = 0; i < g_nsrc; ++i)
        printf("AA_MIC car microphone %s: %s, %u Hz x%u, channel %u -> 16 kHz mono, gain %+d dB\n",
               i ? "fallback" : "source", g_src[i].endpoint, g_src[i].rate, g_src[i].channels, g_src[i].pick, db);
    if (!g_voice) {   // once per process: the DSI client it opens is never closed
        g_voice = new nbt::VoiceScenario;
        g_voice->init();   // NBT_VOICE_SCENARIO=0 unless a mic-scenario is configured (native_service)
    }
}

void poll() {
    // Pure digital silence (every sample exactly 0) for 4 s while the phone listens: the primary endpoint delivers
    // nothing (a live but noise-gated mic gives exact zeros only in short pauses). Move to the fallback once, and
    // remember it in RAM so later sessions start there (one switch per boot).
    if (g_capture && g_cur_index == 0 && g_nsrc > 1 && !g_switched && g_zero_frames >= g_cur.rate * 4) {
        g_switched = true;
        printf("AA_MIC %s gives only zeros: switching to %s\n", g_src[0].endpoint, g_src[1].endpoint);
        g_capture->stop();
        const bool stuck = g_capture->abandoned();
        if (!stuck) delete g_capture;
        g_capture = 0;
        if (stuck) {   // its thread may still call back into the shared conversion state: no second capture
            printf("AA_MIC capture thread stuck: no car mic for the rest of this session\n");
            g_open_failed = true;
            return;
        }
        g_next_index = 1;
        FILE* f = fopen(kUseFallback, "w");
        if (f) fclose(f);
        if (!startFrom(1)) {
            printf("AA_MIC fallback could not start: no car mic this session\n");
            g_open_failed = true;
        }
    }
    pthread_mutex_lock(&g_lock);
    int r = g_pending;
    g_pending = 0;
    pthread_mutex_unlock(&g_lock);
    if (!r) return;
    if (r == 2) {
        printf("AA_MIC phone closed the mic\n");
        // output_multich_0 is natp's hands-free input: never keep it between requests, a call may need it
        if (g_capture && strstr(g_cur.endpoint, "multich")) {
            g_capture->stop();
            if (g_capture->abandoned()) g_open_failed = true;   // stuck thread: never start a second one
            else delete g_capture;
            g_capture = 0;
            printf("AA_MIC %s released (shared with the car's hands-free)\n", g_cur.endpoint);
        }
        if (g_voice) g_voice->deactivate("phone closed the mic");
        aa_audio::onMic(false);
        return;
    }
    printf("AA_MIC phone opened the mic\n");
    if (!g_nsrc) return;
    if (g_voice) g_voice->activate("phone opened the mic");   // first: the scenario routes the mic to the endpoint
    aa_audio::onMic(true);
    if (g_capture || g_open_failed) return;
    if (!startFrom(g_next_index)) {
        printf("AA_MIC capture could not start: no car mic this session (not retried)\n");
        g_open_failed = true;
    }
}

void shutdown() {
    if (g_capture) {
        g_capture->stop();
        if (!g_capture->abandoned()) delete g_capture;   // a thread stuck in the library keeps its object
        g_capture = 0;
    }
    if (g_voice) g_voice->deactivate("session ended");
    g_session = 0;
}

} // namespace aa_mic
