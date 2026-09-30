#pragma once

/// TMP102 temperature sensor (I2C 0x48), sits near the load cell for
/// thermal-drift analysis.

#include <array>
#include <cstdint>
#include <expected>
#include <span>

#include "bus/i2c.hpp"

namespace drv {

template <bus::i2c_device I2c>
class tmp102 {
public:
    explicit constexpr tmp102(I2c& dev) : dev_(dev) {}

    /// Temperature register 0x00: 12-bit, MSB-aligned, 0.0625 C/LSB.
    std::expected<float, bus::i2c_errc> read_celsius() {
        std::array<std::byte, 2> b{};
        if (auto r = bus::reg_read(dev_, 0x00, b); !r) {
            return std::unexpected(r.error());
        }
        const auto raw = static_cast<std::int16_t>(
            (std::to_integer<std::uint16_t>(b[0]) << 8) |
            std::to_integer<std::uint16_t>(b[1]));
        return static_cast<float>(raw >> 4) * 0.0625f;
    }

private:
    I2c& dev_;
};

} // namespace drv
