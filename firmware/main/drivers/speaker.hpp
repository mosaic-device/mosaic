#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
namespace mosaic::drivers::speaker {
// Sets up I2S and the ES8311 with the amplifier off until open(). Call once.
esp_err_t start();
// Turns the amplifier on for this format. False after logging the failure.
bool open(uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample);
// 0-100. False after logging the failure.
bool set_volume(uint8_t percent);
// Plays bytes, which the codec may scale in place. False after logging the failure.
bool write(uint8_t* bytes, size_t size);
// Turns the amplifier off.
void close();
}
