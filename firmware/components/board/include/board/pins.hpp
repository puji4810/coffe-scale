#pragma once

/// Pin map for pcb/scale-adc-s3 rev C — the single place that binds firmware
/// to the PCB. Keep in sync with pcb/scale-adc-s3/README.md.
#include <cstdint>

namespace board::pins {

// SPI LCD (ST7789, J7). RST is a local RC power-on reset — no GPIO drives it.
inline constexpr int lcd_sck  = 4;
inline constexpr int lcd_mosi = 5;
inline constexpr int lcd_cs   = 6;
inline constexpr int lcd_dc   = 7;
inline constexpr int lcd_bl   = 16;

inline constexpr int chrg_stat  = 8;   // TP4057 open-drain charge indicator
inline constexpr int vbat_sense = 9;   // R13/R14 100k/100k divider, ADC1_CH8

// Shared I2C bus: NAU7802 (0x2A), LIS2DW12 (0x18), TMP102 (0x48)
inline constexpr int i2c_sda = 10;
inline constexpr int i2c_scl = 11;

inline constexpr int adc_drdy  = 12;   // NAU7802 conversion-ready
inline constexpr int accel_int1 = 13;  // LIS2DW12 wake/tap/drdy

inline constexpr int btn_mode = 18;
inline constexpr int btn_tare = 48;    // not a strapping pin, external pull-up

inline constexpr int usb_dm = 19;      // native USB-Serial/JTAG
inline constexpr int usb_dp = 20;

inline constexpr int buzz = 21;        // Q2 low-side switch

} // namespace board::pins

namespace board::i2c_addr {
inline constexpr std::uint8_t nau7802  = 0x2A;
inline constexpr std::uint8_t lis2dw12 = 0x18;  // SA0 = 0
inline constexpr std::uint8_t tmp102   = 0x48;
} // namespace board::i2c_addr
