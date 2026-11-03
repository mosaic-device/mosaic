#include "service.hpp"
#include "cues.hpp"
#include "playback.hpp"
#include "sounds.hpp"
#include "wav.hpp"
#include "../../drivers/speaker.hpp"
#include "../../events/bus.hpp"
#include <array>
#include <atomic>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/queue.h"
#include "freertos/task.h"

namespace mosaic::features::audio_cues {
namespace {
    constexpr const char* kTag = "mosaic_audio_cues";
    // Cues wait here while one plays; a burst beyond this is dropped, not queued up.
    constexpr UBaseType_t kQueueDepth = 4;

    struct Sound {
        Pcm pcm;
        bool valid = false;
    };
    // Indexed by Cue.
    std::array<Sound, 2> s_sounds;
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
    // Bus handlers run one at a time, so only the bus task touches this.
    StatusCues s_status_cues;
    void on_status(void*, esp_event_base_t, int32_t, void* data) {
        if (const auto cue = s_status_cues.on_status(*static_cast<const events::Status*>(data)))
            enqueue(*cue);
    }
    void on_notification(void*, esp_event_base_t, int32_t, void*) {
        enqueue(kNotificationCue);
    }
    bool audible() {
        return output().audible();
    }
    // Forwards playback to the speaker driver.
    class DriverSpeaker final : public Speaker {
    public:
        bool open(const Pcm& format) override {
            return drivers::speaker::open(format.sample_rate, format.channels, format.bits_per_sample);
        }
        bool set_volume(uint8_t percent) override {
            return drivers::speaker::set_volume(percent);
        }
        bool write(uint8_t* bytes, size_t size) override {
            return drivers::speaker::write(bytes, size);
        }
        void close() override {
            drivers::speaker::close();
        }
    };
    DriverSpeaker s_speaker;
    void run(void*) {
        Cue cue;
        while (true) {
            if (xQueueReceive(s_queue, &cue, portMAX_DELAY) != pdTRUE)
                continue;
            const Sound& sound = s_sounds[static_cast<size_t>(cue)];
            if (sound.valid)
                play(sound.pcm, output().volume, s_speaker, audible);
        }
    }
}
esp_err_t start() {
    for (size_t i = 0; i < s_sounds.size(); ++i) {
        Sound& sound = s_sounds[i];
        sound.valid = parse_wav(sound_file(static_cast<Cue>(i)), sound.pcm);
        if (!sound.valid)
            ESP_LOGE(kTag, "Embedded cue is not a PCM WAV file; it will not play");
    }
    ESP_RETURN_ON_ERROR(drivers::speaker::start(), kTag, "Speaker");
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
