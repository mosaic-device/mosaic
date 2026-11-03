// The ES8311 and amplifier as an SDL audio device. Writes block like I2S DMA, so
// playback takes its real time and muting cuts a cue short as it does on the device.
#include "../device.hpp"
#include "drivers/speaker.hpp"
#include "esp_log.h"

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace {
constexpr const char* kTag = "mosaic_speaker";
// About the I2S DMA buffering ahead of the codec.
constexpr uint32_t kBufferedMs = 70;

std::atomic_bool g_audio_enabled { false };
std::atomic_bool g_on { false };
SDL_AudioDeviceID g_device = 0;
uint32_t g_bytes_per_ms = 0;
uint8_t g_volume = 0;

void wait(std::chrono::milliseconds time) {
    std::this_thread::sleep_for(time);
}
}

namespace mosaic::drivers::speaker {
esp_err_t start() {
    ESP_LOGI(kTag, "Speaker ready%s", g_audio_enabled.load() ? "" : " (silent: no audio output)");
    return ESP_OK;
}
bool open(uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample) {
    if (bits_per_sample != 16 || !channels || !sample_rate) {
        ESP_LOGE(kTag, "Cannot open speaker: unsupported %u-bit format", bits_per_sample);
        return false;
    }
    g_bytes_per_ms = sample_rate * channels * 2 / 1000;
    if (g_audio_enabled.load()) {
        SDL_AudioSpec wanted { };
        wanted.freq = static_cast<int>(sample_rate);
        wanted.format = AUDIO_S16LSB;
        wanted.channels = static_cast<uint8_t>(channels);
        wanted.samples = 512;
        g_device = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
        if (!g_device)
            ESP_LOGW(kTag, "Playing silently: %s", SDL_GetError());
        else
            SDL_PauseAudioDevice(g_device, 0);
    }
    g_on = true;
    return true;
}
bool set_volume(uint8_t percent) {
    g_volume = std::min<uint8_t>(percent, 100);
    return true;
}
bool write(uint8_t* bytes, size_t size) {
    // The codec scales samples in place for its digital volume.
    for (size_t at = 0; at + 1 < size; at += 2) {
        int16_t sample;
        std::memcpy(&sample, bytes + at, 2);
        sample = static_cast<int16_t>(sample * g_volume / 100);
        std::memcpy(bytes + at, &sample, 2);
    }
    if (!g_device) {
        wait(std::chrono::milliseconds(size / std::max<uint32_t>(g_bytes_per_ms, 1)));
        return true;
    }
    if (SDL_QueueAudio(g_device, bytes, static_cast<uint32_t>(size)) != 0) {
        ESP_LOGE(kTag, "Speaker write failed: %s", SDL_GetError());
        return false;
    }
    while (SDL_GetQueuedAudioSize(g_device) > kBufferedMs * g_bytes_per_ms)
        wait(std::chrono::milliseconds(2));
    return true;
}
void close() {
    if (g_device) {
        // The silent tail is already queued; let it play out before the amplifier stops.
        while (SDL_GetQueuedAudioSize(g_device) > 0)
            wait(std::chrono::milliseconds(2));
        SDL_CloseAudioDevice(g_device);
        g_device = 0;
    }
    g_on = false;
}
} // namespace mosaic::drivers::speaker

namespace mosaic::preview::device {
void enable_audio() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        ESP_LOGW(kTag, "Audio unavailable: %s", SDL_GetError());
        return;
    }
    g_audio_enabled = true;
}
bool speaker_on() {
    return g_on.load();
}
} // namespace mosaic::preview::device
