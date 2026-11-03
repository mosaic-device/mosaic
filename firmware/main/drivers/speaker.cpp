#include "speaker.hpp"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"

namespace mosaic::drivers::speaker {
namespace {
    constexpr const char* kTag = "mosaic_speaker";
    esp_codec_dev_handle_t s_speaker = nullptr;
}
esp_err_t start() {
    s_speaker = bsp_audio_codec_speaker_init();
    return s_speaker ? ESP_OK : ESP_FAIL;
}
bool open(uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample) {
    esp_codec_dev_sample_info_t format { };
    format.bits_per_sample = static_cast<uint8_t>(bits_per_sample);
    format.channel = static_cast<uint8_t>(channels);
    format.sample_rate = sample_rate;
    const int err = esp_codec_dev_open(s_speaker, &format);
    if (err != ESP_CODEC_DEV_OK)
        ESP_LOGE(kTag, "Cannot open speaker: %d", err);
    return err == ESP_CODEC_DEV_OK;
}
bool set_volume(uint8_t percent) {
    const int err = esp_codec_dev_set_out_vol(s_speaker, percent);
    if (err != ESP_CODEC_DEV_OK)
        ESP_LOGE(kTag, "Cannot set volume: %d", err);
    return err == ESP_CODEC_DEV_OK;
}
bool write(uint8_t* bytes, size_t size) {
    const int err = esp_codec_dev_write(s_speaker, bytes, static_cast<int>(size));
    if (err != ESP_CODEC_DEV_OK)
        ESP_LOGE(kTag, "Speaker write failed: %d", err);
    return err == ESP_CODEC_DEV_OK;
}
void close() {
    const int err = esp_codec_dev_close(s_speaker);
    if (err != ESP_CODEC_DEV_OK)
        ESP_LOGE(kTag, "Cannot close speaker: %d", err);
}
}
