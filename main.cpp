// Multicast the default output device's audio to all other output devices.
//
// Windows : WASAPI loopback on the default playback device.
// Linux   : PulseAudio/PipeWire "Monitor of <default sink>" source.
//
// Requires miniaudio >= 0.11.x where ma_pcm_rb_commit_read/write take 2 args.
// ringbuf.cpp / ringbuf.h are no longer needed (ma_pcm_rb replaces them).

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

static const ma_format kFormat = ma_format_f32;
static const ma_uint32 kChannels = 2;
static const ma_uint32 kSampleRate = 48000;
static const ma_uint32 kCapturePeriod = 480;                    // 10 ms
static const ma_uint32 kRingFrames = kSampleRate / 2;           // 500 ms per output
static const ma_uint32 kMaxLatencyFrames = kSampleRate / 5;     // 200 ms: drift limit
static const ma_uint32 kTargetLatencyFrames = kSampleRate / 20; // 50 ms: where to resync to

// One output device + its own ring buffer (single producer / single consumer).
struct Sink
{
    ma_device device;
    ma_pcm_rb rb;
    std::string name;
    bool deviceInited = false;
    bool rbInited = false;
};

typedef std::vector<std::unique_ptr<Sink>> SinkList;
static SinkList g_sinks; // built before any device starts, never modified while running

static void rb_write(ma_pcm_rb *rb, const float *src, ma_uint32 frames)
{
    while (frames > 0)
    {
        ma_uint32 n = frames;
        void *dst = nullptr;
        if (ma_pcm_rb_acquire_write(rb, &n, &dst) != MA_SUCCESS || n == 0)
            break; // ring full -> drop the rest
        std::memcpy(dst, src, (size_t)n * kChannels * sizeof(float));
        ma_pcm_rb_commit_write(rb, n);
        src += (size_t)n * kChannels;
        frames -= n;
    }
}

// Loopback/monitor capture: fan the same audio out to every sink's ring buffer.
static void capture_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
    (void)pOutput;
    if (!pInput || frameCount == 0)
        return;

    SinkList *sinks = (SinkList *)pDevice->pUserData;
    for (auto &s : *sinks)
        rb_write(&s->rb, (const float *)pInput, frameCount);
}

// Each playback device drains only its own ring buffer.
static void playback_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
    (void)pInput;
    Sink *s = (Sink *)pDevice->pUserData;
    float *out = (float *)pOutput;

    // Devices run on different clocks (Bluetooth especially). If we fall too far
    // behind, skip ahead so latency doesn't grow without bound.
    ma_uint32 avail = ma_pcm_rb_available_read(&s->rb);
    if (avail > kMaxLatencyFrames)
        ma_pcm_rb_seek_read(&s->rb, avail - kTargetLatencyFrames);

    ma_uint32 done = 0;
    while (done < frameCount)
    {
        ma_uint32 n = frameCount - done;
        void *src = nullptr;
        if (ma_pcm_rb_acquire_read(&s->rb, &n, &src) != MA_SUCCESS || n == 0)
            break; // underrun
        std::memcpy(out + (size_t)done * kChannels, src, (size_t)n * kChannels * sizeof(float));
        ma_pcm_rb_commit_read(&s->rb, n);
        done += n;
    }

    if (done < frameCount)
        std::memset(out + (size_t)done * kChannels, 0, (size_t)(frameCount - done) * kChannels * sizeof(float));
}

#if !defined(_WIN32)
// PulseAudio/PipeWire: the monitor source of sink "X" has id "X.monitor".
static int find_monitor_source(const std::vector<ma_device_info> &capture, const ma_device_info &sink)
{
    std::string wantId = std::string(sink.id.pulse) + ".monitor";
    for (size_t i = 0; i < capture.size(); ++i)
        if (wantId == capture[i].id.pulse)
            return (int)i;

    // Fallback: description is usually "Monitor of <sink description>"
    std::string wantName = std::string("Monitor of ") + sink.name;
    for (size_t i = 0; i < capture.size(); ++i)
        if (wantName == capture[i].name)
            return (int)i;

    return -1;
}
#endif

static void cleanup_sinks()
{
    for (auto &s : g_sinks)
    {
        if (s->deviceInited)
            ma_device_uninit(&s->device);
        if (s->rbInited)
            ma_pcm_rb_uninit(&s->rb);
    }
    g_sinks.clear();
}

int main()
{
    ma_context context;
    ma_result result;

#if defined(_WIN32)
    result = ma_context_init(NULL, 0, NULL, &context);
#else
    ma_backend backends[] = {ma_backend_pulseaudio}; // PipeWire works via pipewire-pulse
    result = ma_context_init(backends, 1, NULL, &context);
#endif
    if (result != MA_SUCCESS)
    {
        printf("Failed to initialize context (on Linux, is PulseAudio/pipewire-pulse running?).\n");
        return -1;
    }
    printf("Backend: %s\n", ma_get_backend_name(context.backend));

    // Enumerate, then copy: the context-owned arrays are only valid until the next enumeration.
    ma_device_info *pPB = nullptr, *pCap = nullptr;
    ma_uint32 pbCount = 0, capCount = 0;
    result = ma_context_get_devices(&context, &pPB, &pbCount, &pCap, &capCount);
    if (result != MA_SUCCESS || pbCount == 0)
    {
        printf("No playback devices found.\n");
        ma_context_uninit(&context);
        return -1;
    }
    std::vector<ma_device_info> playback(pPB, pPB + pbCount);
    std::vector<ma_device_info> capture(pCap, pCap + capCount);

    printf("-------------------Playback Devices-------------------\n");
    int def = -1;
    for (size_t i = 0; i < playback.size(); ++i)
    {
        printf("  %zu: %s%s\n", i, playback[i].name, playback[i].isDefault ? "  [default]" : "");
        if (playback[i].isDefault)
            def = (int)i;
    }
    printf("------------------------------------------------------\n");
    if (def < 0)
    {
        printf("Warning: no device flagged as default, assuming device 0.\n");
        def = 0;
    }

    // 1. Build one sink (device + ring buffer) per output, EXCEPT the default one.
    //    The default output already plays the app's audio directly, and playing to it
    //    while capturing its loopback would create a feedback loop.
    for (size_t i = 0; i < playback.size(); ++i)
    {
        if ((int)i == def)
        {
            printf("Skipping default device (already played by the app): %s\n", playback[i].name);
            continue;
        }

        auto s = std::make_unique<Sink>();
        s->name = playback[i].name;

        if (ma_pcm_rb_init(kFormat, kChannels, kRingFrames, NULL, NULL, &s->rb) != MA_SUCCESS)
        {
            printf("Failed to create ring buffer for: %s\n", s->name.c_str());
            continue;
        }
        s->rbInited = true;

        ma_device_config pc = ma_device_config_init(ma_device_type_playback);
        pc.playback.pDeviceID = &playback[i].id;
        pc.playback.format = kFormat;
        pc.playback.channels = kChannels;
        pc.sampleRate = kSampleRate;
        pc.performanceProfile = ma_performance_profile_conservative; // friendlier to Bluetooth
        pc.dataCallback = playback_callback;
        pc.pUserData = s.get(); // stable address thanks to unique_ptr

        if (ma_device_init(&context, &pc, &s->device) != MA_SUCCESS)
        {
            printf("Failed to initialize playback device: %s\n", s->name.c_str());
            ma_pcm_rb_uninit(&s->rb);
            continue;
        }
        s->deviceInited = true;

        printf("Initialized playback device: %s\n", s->name.c_str());
        g_sinks.push_back(std::move(s));
    }

    if (g_sinks.empty())
    {
        printf("No other playback devices to multicast to.\n");
        ma_context_uninit(&context);
        return -1;
    }

    // 2. Loopback capture of the default output (NOT the microphone).
    ma_device_config cc;
#if defined(_WIN32)
    cc = ma_device_config_init(ma_device_type_loopback);
    cc.capture.pDeviceID = &playback[def].id; // a *playback* device id, per WASAPI loopback
    printf("Loopback source: %s\n", playback[def].name);
#else
    int mon = find_monitor_source(capture, playback[def]);
    if (mon < 0)
    {
        printf("Could not find a monitor source for '%s'. Capture sources seen:\n", playback[def].name);
        for (size_t i = 0; i < capture.size(); ++i)
            printf("  %zu: %s (%s)\n", i, capture[i].name, capture[i].id.pulse);
        cleanup_sinks();
        ma_context_uninit(&context);
        return -1;
    }
    cc = ma_device_config_init(ma_device_type_capture);
    cc.capture.pDeviceID = &capture[mon].id;
    printf("Monitor source: %s\n", capture[mon].name);
#endif
    cc.capture.format = kFormat;
    cc.capture.channels = kChannels;
    cc.sampleRate = kSampleRate;
    cc.periodSizeInFrames = kCapturePeriod;
    cc.dataCallback = capture_callback;
    cc.pUserData = &g_sinks;

    ma_device captureDevice;
    if (ma_device_init(&context, &cc, &captureDevice) != MA_SUCCESS)
    {
        printf("Failed to initialize loopback/monitor capture.\n");
        cleanup_sinks();
        ma_context_uninit(&context);
        return -1;
    }

    // 3. Start outputs first, then the tap.
    for (auto &s : g_sinks)
        ma_device_start(&s->device);
    ma_device_start(&captureDevice);

    printf("\nMulticasting to %zu extra device(s). Play something (e.g. in Firefox).\n", g_sinks.size());
    printf("Press Enter to stop...\n");
    getchar();

    // 4. Cleanup: stop the producer first.
    printf("Stopping...\n");
    ma_device_uninit(&captureDevice);
    cleanup_sinks();
    ma_context_uninit(&context);
    printf("Cleanup complete.\n");
    return 0;
}