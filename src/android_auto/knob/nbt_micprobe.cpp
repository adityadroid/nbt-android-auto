// micprobe: find the original HU_NBT's car-microphone capture endpoint for Android Auto (aa_mic.cpp), at the car, by hand.
//
//   micprobe -e <endpoint> [-r 16000] [-c 1] [-s <natp scenario>] [-t 6] [-o /fs/ram3/nbt-aa-native/mic-test.raw]
//
// Optionally switches a natp audio scenario on (NBT-AAD audio/voice_scenario.cpp, handsfreed DSI), opens the A2I capture
// endpoint, records for -t seconds (talk into the mic), prints the level once per second and writes the raw PCM
// (S16LE, the endpoint's rate and channels) for listening on the Mac. Then switches the scenario off again.
// Silence (peak < ~10) = wrong endpoint or the scenario is missing. Never run it during a phone call.
// Exit: 0 = audio heard, 1 = only silence, 2 = could not open, 3 = bad arguments.
#include "a2i_capture.hpp"
#include "audio/voice_scenario.hpp"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace {

FILE* g_out = 0;
unsigned g_rate = 16000, g_channels = 1, g_n = 0, g_peak = 0, g_best = 0, g_second = 0;
volatile sig_atomic_t g_stop = 0;

void onStop(int) { g_stop = 1; }

void onCapture(const int16_t* pcm, size_t frames, void*) {
    const size_t samples = frames * g_channels;
    if (g_out) fwrite(pcm, 2, samples, g_out);
    for (size_t i = 0; i < samples; ++i) {
        unsigned a = (unsigned)(pcm[i] < 0 ? -(int)pcm[i] : pcm[i]);
        if (a > g_peak) g_peak = a;
    }
    g_n += (unsigned)frames;
    if (g_n >= g_rate) {
        printf("micprobe: second %u: peak %u of 32767%s\n", ++g_second, g_peak, g_peak < 10 ? " (silence)" : "");
        fflush(stdout);
        if (g_peak > g_best) g_best = g_peak;
        g_n = g_peak = 0;
    }
}

} // namespace

int main(int argc, char** argv) {
    const char* ep = 0;
    const char* scenario = 0;
    const char* out = "/fs/ram3/nbt-aa-native/mic-test.raw";
    unsigned seconds = 6;
    int o;
    while ((o = getopt(argc, argv, "e:r:c:s:t:o:")) != -1) {
        if (o == 'e') ep = optarg;
        else if (o == 'r') g_rate = (unsigned)strtoul(optarg, 0, 10);
        else if (o == 'c') g_channels = (unsigned)strtoul(optarg, 0, 10);
        else if (o == 's') scenario = optarg;
        else if (o == 't') seconds = (unsigned)strtoul(optarg, 0, 10);
        else if (o == 'o') out = optarg;
        else return 3;
    }
    if (!ep || g_rate < 8000 || g_rate > 48000 || (g_channels < 1 || g_channels > 8) || seconds < 1 || seconds > 30 ||
        strncmp(out, "/fs/ram3/", 9)) {
        fprintf(stderr, "usage: micprobe -e <endpoint> [-r rate] [-c 1..8] [-s scenario] [-t 1..30] [-o /fs/ram3/...]\n");
        return 3;
    }
    signal(SIGINT, onStop);
    signal(SIGTERM, onStop);
    nbt::VoiceScenario voice;
    if (scenario) {
        setenv("NBT_VOICE_SCENARIO", "1", 1);
        setenv("NBT_VOICE_SCENARIO_NAME", scenario, 1);
        voice.init();
        for (int i = 0; i < 30; ++i) usleep(100000);   // let the DSI client attach to handsfreed (up to 3 s)
        voice.activate("micprobe");
        usleep(300000);
    }
    nbt::A2iCapture cap("micprobe");
    if (!cap.open(ep, g_rate, g_channels, (g_rate / 50) * 2 * g_channels)) {
        voice.deactivate("micprobe: open failed");
        return 2;
    }
    g_out = fopen(out, "wb");
    printf("micprobe: recording %u s from %s (%u Hz x%u) to %s - talk now\n", seconds, ep, g_rate, g_channels, out);
    fflush(stdout);
    if (!cap.start(&onCapture, 0)) {
        cap.stop();
        voice.deactivate("micprobe: start failed");
        return 2;
    }
    for (unsigned i = 0; i < seconds * 10 && !g_stop; ++i) usleep(100000);
    cap.stop();
    if (g_out && !cap.abandoned()) fclose(g_out);   // a stuck capture thread may still write to it
    voice.deactivate("micprobe done");
    usleep(300000);
    if (g_peak > g_best) g_best = g_peak;
    printf("micprobe: %u chunks, loudest peak %u -> %s\n", (unsigned)cap.chunks(), g_best,
           g_best >= 10 ? "AUDIO (this endpoint carries sound)" : "SILENCE");
    return g_best >= 10 ? 0 : 1;
}
