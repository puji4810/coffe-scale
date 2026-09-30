#pragma once

/// NAU7802 24-bit bridge-sensor ADC driver (Nuvoton DS rev 2.6).
/// I2C 7-bit address 0x2A. Template over bus::i2c_device — runs on the
/// ESP-IDF master driver and on host mocks alike.
///
/// Init sequence follows datasheet section 9.1 power-on sequencing:
///   reset -> PUD/PUA up, wait PUR -> CS -> AVDD source (VLDO/AVDDS) ->
///   gain/SPS -> chopper clock off -> PGA cap on -> settle -> flush ->
///   AFE cal.

#include <array>
#include <concepts>
#include <cstdint>
#include <expected>
#include <span>

#include "bus/i2c.hpp"

namespace drv {

namespace nau7802_reg {
inline constexpr std::uint8_t pu_ctrl  = 0x00;
inline constexpr std::uint8_t ctrl1    = 0x01;
inline constexpr std::uint8_t ctrl2    = 0x02;
inline constexpr std::uint8_t adco_b2  = 0x12;  // result MSB first, 3 bytes
inline constexpr std::uint8_t adc      = 0x15;
inline constexpr std::uint8_t pga      = 0x1B;
inline constexpr std::uint8_t pga_pwr  = 0x1C;
inline constexpr std::uint8_t rev_id   = 0x1F;
} // namespace nau7802_reg

// PU_CTRL bits
namespace nau7802_bit {
inline constexpr std::uint8_t rr     = 1u << 0;  // register reset
inline constexpr std::uint8_t pud    = 1u << 1;  // power-up digital
inline constexpr std::uint8_t pua    = 1u << 2;  // power-up analog
inline constexpr std::uint8_t pur    = 1u << 3;  // power-up ready (RO)
inline constexpr std::uint8_t cs     = 1u << 4;  // cycle start
inline constexpr std::uint8_t cr     = 1u << 5;  // cycle ready (RO)
inline constexpr std::uint8_t oscs   = 1u << 6;  // oscillator select (0 = internal RC)
inline constexpr std::uint8_t avdds  = 1u << 7;  // AVDD source: 1 = internal LDO,
                                               // 0 = AVDD pin is a supply input
} // namespace nau7802_bit

// CTRL1[5:3]: internal LDO voltage (AVDD). `external` keeps the internal
// LDO off (AVDDS = 0): the AVDD pin is then a supply input — s3.1 feeds it
// from the external VDD_ADC rail (U7 HT7533, 3.3 V), which also excites
// the load cells via J2/J11 E+ and drives REFP for ratiometric measure.
enum class nau7802_ldo : std::uint8_t {
    v4_5 = 0, v4_2 = 1, v3_9 = 2, v3_6 = 3,
    v3_3 = 4, v3_0 = 5, v2_7 = 6, v2_4 = 7,
    external = 8,
};
// CTRL1[2:0]: PGA gain
enum class nau7802_gain : std::uint8_t {
    x1 = 0, x2, x4, x8, x16, x32, x64, x128,
};
// CTRL2[6:4]: conversion rate
enum class nau7802_sps : std::uint8_t {
    s10 = 0, s20 = 1, s40 = 2, s80 = 3, s320 = 7,
};
// CTRL2[1:0]: AFE calibration mode
enum class nau7802_calmod : std::uint8_t {
    internal = 0, offset = 2, gain = 3,
};

template <bus::i2c_device I2c>
class nau7802 {
public:
    struct config {
        nau7802_ldo  ldo  = nau7802_ldo::external;  // s3.1: AVDD = VDD_ADC 3.3 V
        nau7802_gain gain = nau7802_gain::x128;
        nau7802_sps  rate = nau7802_sps::s80;
    };

    explicit constexpr nau7802(I2c& dev) : dev_(dev) {}

    /// Full init. `delay_ms` must block for the given milliseconds.
    std::expected<void, bus::i2c_errc>
    init(config cfg, std::invocable<std::uint32_t> auto delay_ms) {
        if (auto r = reset(); !r) return r;
        delay_ms(1);
        if (auto r = power_up(delay_ms); !r) return r;
        if (auto r = set_ldo(cfg.ldo); !r) return r;
        if (auto r = set_gain(cfg.gain); !r) return r;
        if (auto r = set_sample_rate(cfg.rate); !r) return r;
        // Datasheet 9.1: disable the PGA chopper clock (ADC reg bits [5:4]).
        if (auto r = bus::reg_update(dev_, nau7802_reg::adc, 0x30, 0x30); !r) return r;
        // PGA_PWR[7]: enable the 330 pF PGA output cap (app circuit note).
        if (auto r = bus::reg_update(dev_, nau7802_reg::pga_pwr, 0x80, 0x80); !r) return r;
        // PGA[5] LDOMODE = 0: improved accuracy / higher DC gain.
        if (auto r = bus::reg_update(dev_, nau7802_reg::pga, 0x20, 0x00); !r) return r;
        delay_ms(250);  // analog/AVDD settle before calibration
        if (auto r = flush(4, delay_ms); !r) return r;
        return calibrate_afe(nau7802_calmod::internal, delay_ms);
    }

    std::expected<void, bus::i2c_errc> reset() {
        if (auto r = bus::reg_bit(dev_, nau7802_reg::pu_ctrl, 0, true); !r) return r;  // RR
        return bus::reg_bit(dev_, nau7802_reg::pu_ctrl, 0, false);
    }

    std::expected<void, bus::i2c_errc>
    power_up(std::invocable<std::uint32_t> auto delay_ms, int timeout_ms = 100) {
        if (auto r = bus::reg_update(dev_, nau7802_reg::pu_ctrl,
                                     nau7802_bit::pud | nau7802_bit::pua,
                                     nau7802_bit::pud | nau7802_bit::pua);
            !r) {
            return r;
        }
        for (int i = 0; i < timeout_ms; ++i) {
            const auto v = bus::reg_read_u8(dev_, nau7802_reg::pu_ctrl);
            if (!v) return std::unexpected(v.error());
            if (*v & nau7802_bit::pur) {
                return bus::reg_bit(dev_, nau7802_reg::pu_ctrl, 4, true);  // CS
            }
            delay_ms(1);
        }
        return std::unexpected(bus::i2c_errc::timeout);
    }

    std::expected<void, bus::i2c_errc> power_down() {
        return bus::reg_update(dev_, nau7802_reg::pu_ctrl,
                               nau7802_bit::pud | nau7802_bit::pua, 0x00);
    }

    /// AVDD source select. `external` clears AVDDS — the AVDD pin becomes a
    /// supply input and VLDO is don't-care, so CTRL1[5:3] is left untouched.
    std::expected<void, bus::i2c_errc> set_ldo(nau7802_ldo v) {
        if (v == nau7802_ldo::external) {
            return bus::reg_bit(dev_, nau7802_reg::pu_ctrl, 7, false);  // AVDDS
        }
        if (auto r = bus::reg_update(dev_, nau7802_reg::ctrl1, 0x38,
                                     static_cast<std::uint8_t>(v) << 3);
            !r) {
            return r;
        }
        return bus::reg_bit(dev_, nau7802_reg::pu_ctrl, 7, true);  // AVDDS
    }

    std::expected<void, bus::i2c_errc> set_gain(nau7802_gain g) {
        return bus::reg_update(dev_, nau7802_reg::ctrl1, 0x07,
                               static_cast<std::uint8_t>(g));
    }

    std::expected<void, bus::i2c_errc> set_sample_rate(nau7802_sps s) {
        return bus::reg_update(dev_, nau7802_reg::ctrl2, 0x70,
                               static_cast<std::uint8_t>(s) << 4);
    }

    /// True when a fresh conversion is latched (mirrors the DRDY pin).
    std::expected<bool, bus::i2c_errc> data_ready() {
        const auto v = bus::reg_read_u8(dev_, nau7802_reg::pu_ctrl);
        if (!v) return std::unexpected(v.error());
        return (*v & nau7802_bit::cr) != 0;
    }

    /// Signed 24-bit conversion result. Call when data_ready() / DRDY.
    std::expected<std::int32_t, bus::i2c_errc> read() {
        std::array<std::byte, 3> b{};
        if (auto r = bus::reg_read(dev_, nau7802_reg::adco_b2, b); !r) {
            return std::unexpected(r.error());
        }
        const std::uint32_t u = (std::to_integer<std::uint32_t>(b[0]) << 16) |
                                (std::to_integer<std::uint32_t>(b[1]) << 8) |
                                std::to_integer<std::uint32_t>(b[2]);
        return static_cast<std::int32_t>(u & 0x0080'0000u ? u | 0xFF00'0000u : u);
    }

    /// AFE calibration (~344 ms typical). Returns error on CAL_ERR/timeout.
    std::expected<void, bus::i2c_errc>
    calibrate_afe(nau7802_calmod mode, std::invocable<std::uint32_t> auto delay_ms,
                  int timeout_ms = 1000) {
        if (auto r = bus::reg_update(dev_, nau7802_reg::ctrl2, 0x03,
                                     static_cast<std::uint8_t>(mode));
            !r) {
            return r;
        }
        if (auto r = bus::reg_bit(dev_, nau7802_reg::ctrl2, 2, true); !r) return r;  // CALS
        for (int i = 0; i < timeout_ms; ++i) {
            const auto v = bus::reg_read_u8(dev_, nau7802_reg::ctrl2);
            if (!v) return std::unexpected(v.error());
            if (!(*v & 0x04)) {          // CALS cleared -> done
                return (*v & 0x08) ? std::unexpected(bus::i2c_errc::io)   // CAL_ERR
                                   : std::expected<void, bus::i2c_errc>{};
            }
            delay_ms(1);
        }
        return std::unexpected(bus::i2c_errc::timeout);
    }

    /// Discard `n` conversions (junk while the filter settles).
    std::expected<void, bus::i2c_errc>
    flush(int n, std::invocable<std::uint32_t> auto delay_ms) {
        for (int i = 0; i < n; ++i) {
            for (int tries = 0; tries < 100; ++tries) {   // <= ~1 s per sample
                const auto rdy = data_ready();
                if (!rdy) return std::unexpected(rdy.error());
                if (*rdy) break;
                if (tries == 99) return std::unexpected(bus::i2c_errc::timeout);
                delay_ms(10);
            }
            if (auto r = read(); !r) return std::unexpected(r.error());
        }
        return {};
    }

    /// Chip revision (low nibble of 0x1F; expected 0x0F on current silicon).
    std::expected<std::uint8_t, bus::i2c_errc> revision() {
        const auto v = bus::reg_read_u8(dev_, nau7802_reg::rev_id);
        if (!v) return std::unexpected(v.error());
        return static_cast<std::uint8_t>(*v & 0x0F);
    }

private:
    I2c& dev_;
};

} // namespace drv
