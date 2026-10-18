#pragma once

/// LIS2DW12 3-axis accelerometer (I2C 0x18, SA0=0) for level, vibration and
/// tap detection. Register layout per ST datasheet DS11811 and AN5038.

#include <array>
#include <cstdint>
#include <expected>
#include <span>

#include "bus/i2c.hpp"

namespace drv {

namespace lis2dw12_reg {
inline constexpr std::uint8_t who_am_i = 0x0F;
inline constexpr std::uint8_t ctrl1    = 0x20;
inline constexpr std::uint8_t ctrl2    = 0x21;
inline constexpr std::uint8_t ctrl3    = 0x22;  // interrupt control (LIR latch)
inline constexpr std::uint8_t ctrl4    = 0x23;  // INT1 routing
inline constexpr std::uint8_t ctrl6    = 0x25;
inline constexpr std::uint8_t status   = 0x27;
inline constexpr std::uint8_t out_x_l  = 0x28;  // .. out_z_h = 0x2D
inline constexpr std::uint8_t tap_ths_x  = 0x30;
inline constexpr std::uint8_t tap_ths_y  = 0x31;
inline constexpr std::uint8_t tap_ths_z  = 0x32;
inline constexpr std::uint8_t int_dur    = 0x33;  // tap latency/quiet/shock
inline constexpr std::uint8_t wake_up_ths = 0x34; // single_double_tap + wk_ths
inline constexpr std::uint8_t tap_src    = 0x39;
inline constexpr std::uint8_t all_int_src = 0x3B;
inline constexpr std::uint8_t ctrl7      = 0x3F;  // interrupts_enable
} // namespace lis2dw12_reg

// CTRL6[5:4] full scale
enum class lis2dw12_fs : std::uint8_t { g2 = 0, g4 = 1, g8 = 2, g16 = 3 };
// CTRL1[7:4] high-performance output data rate; code 1 is LP-only 1.6 Hz.
enum class lis2dw12_odr : std::uint8_t {
    off = 0, hz12_5 = 2, hz25 = 3, hz50 = 4,
    hz100 = 5, hz200 = 6, hz400 = 7, hz800 = 8, hz1600 = 9,
};

template <bus::i2c_device I2c>
class lis2dw12 {
public:
    static constexpr std::uint8_t kWhoAmI = 0x44;

    struct vec3 {
        float x, y, z;  // milli-g
    };

    explicit constexpr lis2dw12(I2c& dev) : dev_(dev) {}

    /// Verify chip id, then: BDU + address auto-increment on, FS=2 g,
    /// low-noise, high-performance mode, continuous conversion at `odr`.
    std::expected<void, bus::i2c_errc> init(lis2dw12_odr odr = lis2dw12_odr::hz100,
                                            lis2dw12_fs  fs  = lis2dw12_fs::g2) {
        const auto id = bus::reg_read_u8(dev_, lis2dw12_reg::who_am_i);
        if (!id) return std::unexpected(id.error());
        if (*id != kWhoAmI) return std::unexpected(bus::i2c_errc::io);

        fs_ = fs;
        odr_ = odr;
        if (auto r = bus::reg_write_u8(dev_, lis2dw12_reg::ctrl2, 0x0C); !r) return r;  // BDU|IF_ADD_INC
        if (auto r = bus::reg_write_u8(dev_, lis2dw12_reg::ctrl6,
                                       static_cast<std::uint8_t>(fs) << 4 | 0x04);
            !r) {
            return r;  // LOW_NOISE=1
        }
        // CTRL1[3:2] MODE = 0b01 (high performance), LP_MODE = 0.
        return bus::reg_write_u8(dev_, lis2dw12_reg::ctrl1,
                                 static_cast<std::uint8_t>(
                                     static_cast<std::uint8_t>(odr) << 4 | 0x04));
    }

    /// Latest XYZ sample in milli-g (14-bit, left-justified output regs).
    std::expected<vec3, bus::i2c_errc> read_mg() {
        std::array<std::byte, 6> b{};
        if (auto r = bus::reg_read(dev_, lis2dw12_reg::out_x_l, b); !r) {
            return std::unexpected(r.error());
        }
        const float k = mg_per_lsb(fs_);
        auto        axis = [&b, k](int i) {
            const auto raw = static_cast<std::int16_t>(
                (std::to_integer<std::uint16_t>(b[i + 1]) << 8) |
                std::to_integer<std::uint16_t>(b[i]));
            return static_cast<float>(raw >> 2) * k;  // 14-bit left-justified
        };
        return vec3{axis(0), axis(2), axis(4)};
    }

    /// STATUS[0] DRDY.
    std::expected<bool, bus::i2c_errc> data_ready() {
        const auto v = bus::reg_read_u8(dev_, lis2dw12_reg::status);
        if (!v) return std::unexpected(v.error());
        return (*v & 0x01) != 0;
    }

    /// AN5038 section 5.6: double-tap only on latched INT1, >=400 Hz.
    /// High-performance mode draws ~90 uA with low-noise disabled; this
    /// is deliberately not the sub-uA motion-only mode (which can't tap).
    std::expected<void, bus::i2c_errc> enable_tap_wake() {
        if (auto r = bus::reg_write_u8(dev_, lis2dw12_reg::ctrl4, 0); !r) return r;
        if (auto r = bus::reg_write_u8(dev_, lis2dw12_reg::ctrl6,
                                      static_cast<std::uint8_t>(fs_) << 4); !r) return r;
        if (auto r = bus::reg_write_u8(dev_, lis2dw12_reg::ctrl1, 0x74); !r) return r;
        // CTRL7.interrupts_enable (bit5 = 0x20) — master switch for the
        // tap/ff/wu detection block; without it no source bit ever sets.
        if (auto r = bus::reg_write_u8(dev_, lis2dw12_reg::ctrl7, 0x20); !r) {
            return r;
        }
        // ~0.6 g threshold on all axes (FS 2 g -> 62.5 mg/LSB -> ths 10).
        // TAP_THS_Z[7:5] are the per-axis tap ENABLE bits — 0xE0 | ths, or
        // tap detection never fires on any axis.
        for (const std::uint8_t reg :
             {lis2dw12_reg::tap_ths_x, lis2dw12_reg::tap_ths_y}) {
            if (auto r = bus::reg_write_u8(dev_, reg, 10); !r) return r;
        }
        if (auto r = bus::reg_write_u8(dev_, lis2dw12_reg::tap_ths_z,
                                     0xE0 | 10); !r) {
            return r;
        }
        // 400 Hz: latency 5*32/ODR = 400 ms, quiet 2*4/ODR = 20 ms,
        // shock 2*8/ODR = 40 ms. A single impact is not a double-tap.
        if (auto r = bus::reg_write_u8(dev_, lis2dw12_reg::int_dur, 0x5A); !r) {
            return r;
        }
        // Enable the double-tap recognizer; WU is NOT routed to INT1.
        if (auto r = bus::reg_write_u8(dev_, lis2dw12_reg::wake_up_ths, 0x80);
            !r) {
            return r;
        }
        // CTRL3.LIR (bit4 = 0x10) -> INT1 latches until src regs are read.
        if (auto r = bus::reg_write_u8(dev_, lis2dw12_reg::ctrl3, 0x10); !r) {
            return r;
        }
        // INT1_TAP (bit3) routes DOUBLE tap. SINGLE_TAP (bit6) and WU
        // (bit5) must remain off, otherwise a nudge lights the screen.
        return bus::reg_write_u8(dev_, lis2dw12_reg::ctrl4, 0x08);
    }

    /// Read the latched interrupt source registers — releases a LIR-latched
    /// INT1 line. Call before arming GPIO level wake so a stale tap event
    /// doesn't bounce the host straight out of sleep.
    std::expected<void, bus::i2c_errc> clear_wake_srcs() {
        if (auto r = bus::reg_read_u8(dev_, lis2dw12_reg::all_int_src); !r) {
            return std::unexpected(r.error());
        }
        if (auto r = bus::reg_read_u8(dev_, lis2dw12_reg::tap_src); !r) {
            return std::unexpected(r.error());
        }
        return {};
    }

    /// Undo enable_tap_wake() and clear the latched INT line (the source
    /// registers release LIR on read).
    std::expected<void, bus::i2c_errc> disable_tap_wake() {
        if (auto r = clear_wake_srcs(); !r) {
            return r;
        }
        if (auto r = bus::reg_write_u8(dev_, lis2dw12_reg::ctrl4, 0x00); !r) {
            return r;
        }
        if (auto r = bus::reg_write_u8(dev_, lis2dw12_reg::ctrl3, 0x00); !r) {
            return r;
        }
        if (auto r = bus::reg_write_u8(dev_, lis2dw12_reg::ctrl7, 0x00); !r) return r;
        if (auto r = bus::reg_write_u8(dev_, lis2dw12_reg::ctrl6,
                                      static_cast<std::uint8_t>(fs_) << 4 | 0x04); !r) return r;
        return bus::reg_write_u8(dev_, lis2dw12_reg::ctrl1,
                                static_cast<std::uint8_t>(odr_) << 4 | 0x04);
    }

    /// Sensitivity in mg/LSB of the 14-bit value (datasheet table 3).
    static constexpr float mg_per_lsb(lis2dw12_fs fs) {
        switch (fs) {
            case lis2dw12_fs::g2:  return 0.244f;
            case lis2dw12_fs::g4:  return 0.488f;
            case lis2dw12_fs::g8:  return 0.976f;
            case lis2dw12_fs::g16: return 1.952f;
        }
        return 0.244f;
    }

private:
    I2c&        dev_;
    lis2dw12_fs fs_ = lis2dw12_fs::g2;
    lis2dw12_odr odr_ = lis2dw12_odr::hz100;
};

} // namespace drv
