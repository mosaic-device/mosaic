#include "service.hpp"
#include "cues.hpp"
#include "wav.hpp"
#include "../../events/bus.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/queue.h"
#include "freertos/task.h"

extern const uint8_t notification_wav_start[] asm("_binary_notification_wav_start");
extern const uint8_t notification_wav_end[] asm("_binary_notification_wav_end");
extern const uint8_t connected_wav_start[] asm("_binary_connected_wav_start");
extern const uint8_t connected_wav_end[] asm("_binary_connected_wav_end");
extern const uint8_t error_wav_start[] asm("_binary_error_wav_start");
extern const uint8_t error_wav_end[] asm("_binary_error_wav_end");

namespace mosaic::features::audio_cues {
namespace {
    constexpr const char* kTag = "mosaic_audio_cues";
    // Cues wait here while one plays; a burst beyond this is dropped, not queued up.
    constexpr UBaseType_t kQueueDepth = 4;
    // Written in small pieces so muting stops a cue within about 10 ms.
    constexpr size_t kChunkBytes = 512;
    // Enough silence to push the cue's tail out of the I2S DMA buffers (about 65 ms
    // at 22050 Hz) before the amplifier turns off.
    constexpr uint32_t kTailMs = 80;

    struct Sound {
        const uint8_t* start;
        const uint8_t* end;
        Pcm pcm;
        bool valid = false;
    };
    // Indexed by Cue.
    std::array<Sound, 3> s_sounds { {
        { notification_wav_start, notification_wav_end, { } },
        { connected_wav_start, connected_wav_end, { } },
        { error_wav_start, error_wav_end, { } },
    } };
    esp_codec_dev_handle_t s_speaker = nullptr;
    QueueHandle_t s_queue = nullptr;
    std::atomic<uint8_t> s_volume = kDefaultVolume;
    std::atomic<bool> s_muted = false;

    Output output() {
        return { .volume = s_volume.load(), .muted = s_muted.load() };
    }
    void enqueue(Cue cue) {
        if (!output().audible())
            return;
        if (xQueueSend(s_queue, &cue, 0) != pdTRUE)
            ESP_LOGW(kTag, "Cue dropped: still playing earlier cues");
    }
    void on_status(void*, esp_event_base_t, int32_t, void* data) {
        if (const auto cue = cue_for(*static_cast<const events::Status*>(data)))
            enqueue(*cue);
    }
    void on_notification(void*, esp_event_base_t, int32_t, void*) {
        enqueue(kNotificationCue);
    }
    // Writes bytes through a scratch copy: the codec may scale samples in place.
    bool write(const uint8_t* bytes, size_t size) {
        std::array<uint8_t, kChunkBytes> chunk;
        for (size_t at = 0; at < size; at += chunk.size()) {
            if (!output().audible())
                return false;
            const size_t count = std::min(chunk.size(), size - at);
            if (bytes)
                std::memcpy(chunk.data(), bytes + at, count);
            else
                chunk.fill(0);
            const int err = esp_codec_dev_write(s_speaker, chunk.data(), static_cast<int>(count));
            if (err != ESP_CODEC_DEV_OK) {
                ESP_LOGE(kTag, "Speaker write failed: %d", err);
                return false;
            }
        }
        return true;
    }
    void play(const Pcm& pcm) {
        const Output level = output();
        if (!level.audible())
            return;
        esp_codec_dev_sample_info_t format { };
        format.bits_per_sample = static_cast<uint8_t>(pcm.bits_per_sample);
        format.channel = static_cast<uint8_t>(pcm.channels);
        format.sample_rate = pcm.sample_rate;
        // Opening the codec turns the amplifier on and closing it turns it off.
        int err = esp_codec_dev_open(s_speaker, &format);
        if (err != ESP_CODEC_DEV_OK) {
            ESP_LOGE(kTag, "Cannot open speaker: %d", err);
            return;
        }
        err = esp_codec_dev_set_out_vol(s_speaker, level.volume);
        if (err != ESP_CODEC_DEV_OK)
            ESP_LOGE(kTag, "Cannot set volume: %d", err);
        else if (write(pcm.samples.data(), pcm.samples.size())) {
            const size_t frame = static_cast<size_t>(pcm.channels) * pcm.bits_per_sample / 8;
            write(nullptr, pcm.sample_rate * kTailMs / 1000 * frame);
        }
        err = esp_codec_dev_close(s_speaker);
        if (err != ESP_CODEC_DEV_OK)
            ESP_LOGE(kTag, "Cannot close speaker: %d", err);
    }
    void run(void*) {
        Cue cue;
        while (true) {
            if (xQueueReceive(s_queue, &cue, portMAX_DELAY) != pdTRUE)
                continue;
            const Sound& sound = s_sounds[static_cast<size_t>(cue)];
            if (sound.valid)
                play(sound.pcm);
        }
    }
}
esp_err_t start() {
    for (Sound& sound : s_sounds) {
        sound.valid = parse_wav({ sound.start, static_cast<size_t>(sound.end - sound.start) }, sound.pcm);
        if (!sound.valid)
            ESP_LOGE(kTag, "Embedded cue is not a PCM WAV file; it will not play");
    }
    // Sets up I2S and the ES8311 with its amplifier off until a cue opens the codec.
    s_speaker = bsp_audio_codec_speaker_init();
    if (s_speaker == nullptr)
        return ESP_FAIL;
    s_queue = xQueueCreate(kQueueDepth, sizeof(Cue));
    if (s_queue == nullptr)
        return ESP_ERR_NO_MEM;
    if (xTaskCreate(run, "mosaic_cues", 4096, nullptr, 3, nullptr) != pdPASS)
        return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(events::subscribe(MOSAIC_EVENT_STATUS_CHANGED, on_status, nullptr, nullptr), kTag, "Status");
    ESP_RETURN_ON_ERROR(
        events::subscribe(MOSAIC_EVENT_NOTIFICATION_RECEIVED, on_notification, nullptr, nullptr), kTag, "Notification");
    ESP_LOGI(kTag, "Speaker ready: volume %u%%%s", s_volume.load(), s_muted.load() ? ", muted" : "");
    return ESP_OK;
}
void set_volume(uint8_t percent) {
    s_volume = clamp_volume(percent);
}
uint8_t volume() {
    return s_volume.load();
}
void set_muted(bool muted) {
    s_muted = muted;
}
bool muted() {
    return s_muted.load();
}
}
