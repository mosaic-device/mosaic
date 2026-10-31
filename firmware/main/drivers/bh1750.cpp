#include "bh1750.hpp"
#include <cstdint>
#include "driver/i2c_master.h"
#include "sdkconfig.h"
namespace mosaic::drivers::bh1750 {
namespace {
    // Its own bus, apart from the BSP's touch and audio bus. Pull the lines up to
    // 3.3V only: the breakout's pull-ups go to its VCC, which must be 3.3V.
    constexpr i2c_port_num_t kPort = I2C_NUM_0;
    static_assert(kPort != CONFIG_BSP_I2C_NUM, "the BH1750 must not share the touch and audio bus");
    constexpr gpio_num_t kSda = GPIO_NUM_21;
    constexpr gpio_num_t kScl = GPIO_NUM_22;
    constexpr uint32_t kSpeedHz = 100'000;
    constexpr uint16_t kAddress = 0x23; // ADDR pin low or unconnected
    constexpr int kTimeoutMs = 50;
    constexpr uint8_t kPowerOn = 0x01;
    constexpr uint8_t kContinuousHighResolution = 0x10;
    // Datasheet: lux = count / 1.2 at the default measurement time.
    constexpr float kCountsPerLux = 1.2F;

    i2c_master_bus_handle_t s_bus = nullptr;
    i2c_master_dev_handle_t s_device = nullptr;

    esp_err_t command(uint8_t code) {
        return i2c_master_transmit(s_device, &code, 1, kTimeoutMs);
    }
}
esp_err_t start() {
    if (s_bus == nullptr) {
        i2c_master_bus_config_t bus { };
        bus.i2c_port = kPort;
        bus.sda_io_num = kSda;
        bus.scl_io_num = kScl;
        bus.clk_source = I2C_CLK_SRC_DEFAULT;
        bus.glitch_ignore_cnt = 7;
        // A weak backstop to the breakout's own pull-ups.
        bus.flags.enable_internal_pullup = true;
        if (const esp_err_t err = i2c_new_master_bus(&bus, &s_bus); err != ESP_OK)
            return err;
    }
    if (const esp_err_t err = i2c_master_probe(s_bus, kAddress, kTimeoutMs); err != ESP_OK)
        return err;
    if (s_device == nullptr) {
        i2c_device_config_t device { };
        device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device.device_address = kAddress;
        device.scl_speed_hz = kSpeedHz;
        if (const esp_err_t err = i2c_master_bus_add_device(s_bus, &device, &s_device); err != ESP_OK)
            return err;
    }
    const esp_err_t err = command(kPowerOn);
    return err == ESP_OK ? command(kContinuousHighResolution) : err;
}
esp_err_t read(float& lux) {
    if (s_device == nullptr)
        return ESP_ERR_INVALID_STATE;
    uint8_t bytes[2] = { };
    if (const esp_err_t err = i2c_master_receive(s_device, bytes, sizeof(bytes), kTimeoutMs); err != ESP_OK)
        return err;
    lux = static_cast<float>(bytes[0] << 8 | bytes[1]) / kCountsPerLux;
    return ESP_OK;
}
}
