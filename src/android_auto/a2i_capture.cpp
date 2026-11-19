// See a2i_capture.hpp. Based on: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto),
// PolyForm-Noncommercial-1.0.0 (milestone-7 src/audio/a2i_stream.cpp, include/a2i_client.h).
#include "a2i_capture.hpp"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace nbt {

namespace {

// liba2i_client.so configuration, 76 bytes (NBT-AAD include/a2i_client.h; same struct as audio_a2i/asound_device.cpp).
#pragma pack(push, 4)
struct A2iCfg {
    uint32_t magic, direction, field_08, mode, buffer_size, channels, bytes_per_sample, sample_rate, field_20;
    const char* endpoint_name;
    uint32_t channel_type, field_2c, sample_bits, field_34, timeout_ms, reserved[4];
};
#pragma pack(pop)

enum { F_OPEN = 0, F_CLOSE, F_START, F_STOP, F_NOTIFY, F_GETBUF, F_ERR, F_COUNT };
const char* const kFnNames[F_COUNT] = {
    "A2I_Client_Open", "A2I_Client_Close", "A2I_Client_Start", "A2I_Client_Stop", "A2I_Client_GetNotification",
    "A2I_Client_GetBuffer", "A2I_Client_GetErrorDescription"
};
typedef int (*FnOpen)(void**, const A2iCfg*);
typedef int (*FnHandleP)(void**);
typedef int (*FnHandle)(void*);
typedef int (*FnGetBuf)(void*, void**, size_t*);
typedef const char* (*FnErr)(int);

const uint32_t kCookies[2] = { 0x00000000u, 0x0ABABABAu };   // legacy capture, factory CarPlay

} // namespace

A2iCapture::A2iCapture(const char* log_name)
    : name_(log_name), lib_(NULL), handle_(NULL), channels_(1), thread_valid_(false), stop_(false), running_(false),
      finished_(false), abandoned_(false), started_(false), cb_(NULL), user_(NULL), chunks_(0) {
    memset(fn_, 0, sizeof(fn_));
}

A2iCapture::~A2iCapture() {
    stop();
    if (lib_ && !abandoned_) dlclose(lib_);
}

bool A2iCapture::load() {
    if (lib_) return true;
    const char* path = getenv("NBT_A2I_LIB");
    if (!path || !*path) path = "/opt/sys/lib/liba2i_client.so";
    lib_ = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib_) {
        printf("[%s] could not load %s: %s\n", name_, path, dlerror());
        return false;
    }
    for (int i = 0; i < F_COUNT; ++i) {
        fn_[i] = dlsym(lib_, kFnNames[i]);
        if (!fn_[i] && i != F_ERR) {
            printf("[%s] %s has no %s\n", name_, path, kFnNames[i]);
            dlclose(lib_);
            lib_ = NULL;
            return false;
        }
    }
    return true;
}

bool A2iCapture::open(const char* endpoint, uint32_t rate, uint32_t channels, uint32_t chunk_bytes) {
    if (handle_) return true;
    if (!endpoint || !*endpoint || !load()) return false;
    if (channels < 1 || channels > 8) channels = 1;
    while (chunk_bytes & (chunk_bytes - 1)) chunk_bytes &= chunk_bytes - 1;   // a power of two (liba2i check)
    if (chunk_bytes < 64) chunk_bytes = 512;
    FnErr err = (FnErr)fn_[F_ERR];
    for (int c = 0; c < 2; ++c) {
        A2iCfg cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.magic = 0x02000000; cfg.direction = 1; cfg.field_08 = 1; cfg.mode = 2; cfg.buffer_size = chunk_bytes;
        cfg.channels = channels; cfg.bytes_per_sample = 2; cfg.sample_rate = rate; cfg.endpoint_name = endpoint;
        cfg.channel_type = channels == 1 ? 0 : 1; cfg.sample_bits = 0; cfg.field_34 = kCookies[c]; cfg.timeout_ms = 2000;
        void* h = NULL;
        int rc = ((FnOpen)fn_[F_OPEN])(&h, &cfg);
        if (rc == 0 && h) {
            handle_ = h;
            channels_ = channels;
            printf("[%s] A2I capture open: %s, %u Hz x%u, %u-byte chunks, cookie 0x%08X\n", name_, endpoint, rate,
                   channels, chunk_bytes, kCookies[c]);
            return true;
        }
        printf("[%s] A2I_Client_Open('%s', capture, %u Hz x%u, cookie 0x%08X) failed: %d %s\n", name_, endpoint, rate,
               channels, kCookies[c], rc, err ? err(rc) : "");
    }
    return false;
}

bool A2iCapture::start(Callback cb, void* user) {
    if (!handle_ || thread_valid_ || !cb) return false;
    cb_ = cb;
    user_ = user;
    stop_ = false;
    finished_ = false;
    chunks_ = 0;
    if (pthread_create(&thread_, NULL, &A2iCapture::threadStub, this) != 0) {
        printf("[%s] could not start the capture thread\n", name_);
        return false;
    }
    thread_valid_ = true;
    return true;
}

void A2iCapture::stop() {
    if (thread_valid_) {
        stop_ = true;
        // GetNotification may block on an endpoint that delivers nothing: wait at most 2 s, then leave the thread and
        // the handle alone (never Close under a thread that is still inside the library; the process ends soon).
        for (int i = 0; i < 200 && !finished_; ++i) usleep(10000);
        if (!finished_) {
            pthread_detach(thread_);
            thread_valid_ = false;
            abandoned_ = true;
            printf("[%s] capture thread did not end within 2 s: left running, endpoint not closed\n", name_);
            handle_ = NULL;
            return;
        }
        pthread_join(thread_, NULL);
        thread_valid_ = false;
    }
    running_ = false;
    if (handle_) {
        if (started_) {
            int rc = ((FnHandle)fn_[F_STOP])(handle_);
            if (rc) printf("[%s] A2I_Client_Stop: %d\n", name_, rc);
        }
        int rc = ((FnHandleP)fn_[F_CLOSE])(&handle_);
        if (rc) printf("[%s] A2I_Client_Close: %d\n", name_, rc);
        handle_ = NULL;
        started_ = false;
        printf("[%s] A2I capture closed (%u chunks)\n", name_, (unsigned)chunks_);
    }
}

void* A2iCapture::threadStub(void* self) {
    A2iCapture* c = static_cast<A2iCapture*>(self);
    c->loop();
    c->finished_ = true;
    return NULL;
}

void A2iCapture::loop() {
    // the handle is read once: stop() may give up on this thread and forget handle_ while a library call blocks
    void* const h = handle_;
    int rc = ((FnHandle)fn_[F_START])(h);
    if (rc) {
        printf("[%s] A2I_Client_Start failed: %d\n", name_, rc);
        return;
    }
    started_ = true;
    running_ = true;
    unsigned notify_fail = 0, getbuf_fail = 0;
    while (!stop_) {
        if (((FnHandle)fn_[F_NOTIFY])(h) != 0) {
            ++notify_fail;
            usleep(1000);
            continue;
        }
        void* buf = NULL;
        size_t size = 0;
        if (stop_) break;   // a notification that came after stop(): never call back into a finished owner
        if (((FnGetBuf)fn_[F_GETBUF])(h, &buf, &size) != 0 || !buf || size < 2) {
            ++getbuf_fail;
            continue;
        }
        if (stop_) break;
        ++chunks_;
        cb_(static_cast<const int16_t*>(buf), size / (2 * channels_), user_);
    }
    printf("[%s] capture thread ended (chunks %u, notify failures %u, buffer failures %u)\n", name_, (unsigned)chunks_,
           notify_fail, getbuf_fail);
    running_ = false;
}

} // namespace nbt
