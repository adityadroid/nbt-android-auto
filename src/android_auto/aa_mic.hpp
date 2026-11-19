// aa_mic: the car microphone for Android Auto (Google Assistant, "Hey Google", voice replies, voice search), part of
// oneplus_native. Android Auto channel 14: the phone opens the mic (AVInputOpenRequest), the NBT-AAD core answers and
// sends whatever PCM is posted (16 kHz mono S16LE); this module captures that PCM from the car.
//
// Phone calls do NOT use this path: Android Auto never carries call audio. Calls stay on the car's Bluetooth hands-free
// (HFP), which already uses the car microphone with the car's echo cancelling; Android Auto shows the call screen.
//
// Configuration (environment, set by native_service from files in the package folder):
//   NBT_MIC_ENDPOINT          A2I capture endpoint with the car mic (file mic-endpoint). Unset = mic off: the phone's
//                             open request is still answered, it just gets no audio (the behaviour before 2026-11-19).
//   NBT_MIC_RATE / _CHANNELS  endpoint format (files mic-rate, default 16000 / 1); converted to 16 kHz mono.
//   NBT_VOICE_SCENARIO(_NAME) natp audio scenario to switch the mic on while the phone has it open (file mic-scenario);
//                             NBT-AAD audio/voice_scenario.cpp (handsfreed DSI). Off unless the file names one.
// The endpoint is opened on the phone's first mic request and kept open until the session ends (NBT-AAD: opening and
// closing it per request preceded a head-unit reset on an NBT Evo), and never retried after a failed open.
#ifndef NBT_AA_MIC_HPP
#define NBT_AA_MIC_HPP

namespace nbt {
class AASession;
}

namespace aa_mic {

// Before session.start(): registers the mic request callback (live sessions only).
void setup(nbt::AASession& session);
// Main loop, often (every ~100 ms): acts on the phone's open / close requests outside the session thread.
void poll();
// After session.stop(): stops the capture and switches the scenario off.
void shutdown();

} // namespace aa_mic

#endif
