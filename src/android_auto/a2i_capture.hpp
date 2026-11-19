// a2i_capture: read PCM from an A2I capture (uplink) endpoint of the original HU_NBT, through /opt/sys/lib/liba2i_client.so
// loaded at run time (no load-time dependency, same as audio_a2i/asound_device.cpp).
//
// Protocol as in NBT-AAD milestone-7 src/audio/a2i_stream.cpp (A2ICaptureStream, car-verified on NBT Evo):
// Open(direction 1) -> Start -> loop { GetNotification; GetBuffer -> one chunk } -> Stop -> Close.
// Which endpoint carries the car microphone on the ORIGINAL NBT is not known yet: it is configured (mic-endpoint file,
// see CAR-TEST-aa-dev.md) after a check at the car with `micprobe`.
//
// Used by oneplus_native (aa_mic.cpp, Android Auto microphone channel) and by the micprobe test program.
#ifndef NBT_A2I_CAPTURE_HPP
#define NBT_A2I_CAPTURE_HPP

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

namespace nbt {

class A2iCapture {
public:
    // One chunk of captured PCM (S16LE, the endpoint's rate and channel count). Capture thread.
    typedef void (*Callback)(const int16_t* pcm, size_t frames, void* user);

    explicit A2iCapture(const char* log_name);
    ~A2iCapture();

    // Opens `endpoint` (e.g. "/output_mono_AirPlayUL") for capture: tries cookie 0x00000000, then 0x0ABABABA.
    bool open(const char* endpoint, uint32_t rate, uint32_t channels, uint32_t chunk_bytes);
    // Starts the capture thread. false when not open or the thread could not start.
    bool start(Callback cb, void* user);
    // Joins the thread, then Stop + Close. Safe to call repeatedly.
    void stop();

    bool isOpen() const { return handle_ != NULL; }
    bool isRunning() const { return running_; }
    uint32_t chunks() const { return chunks_; }
    // stop() gave up on a capture thread stuck in the library: the object must not be deleted.
    bool abandoned() const { return abandoned_; }

private:
    A2iCapture(const A2iCapture&);
    A2iCapture& operator=(const A2iCapture&);
    bool load();
    static void* threadStub(void* self);
    void loop();

    const char* name_;
    void* lib_;
    void* fn_[8];
    void* handle_;
    uint32_t channels_;
    pthread_t thread_;
    bool thread_valid_;
    volatile bool stop_;
    volatile bool running_;
    volatile bool finished_;
    bool abandoned_;
    bool started_;
    Callback cb_;
    void* user_;
    volatile uint32_t chunks_;
};

} // namespace nbt

#endif
