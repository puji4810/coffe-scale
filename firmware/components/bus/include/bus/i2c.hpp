#pragma once

/// Minimal synchronous I2C abstraction.
///
/// A type modelling `i2c_device` represents one device on the bus (7-bit
/// address already bound by the implementation). Drivers (nau7802, tmp102,
/// lis2dw12) are templates over it, so the same code runs on the ESP-IDF
/// master driver (bus/i2c_esp.hpp) and on test mocks.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace bus {

enum class i2c_errc {
    nack,     ///< address or data byte NACKed
    timeout,  ///< transaction timed out
    busy,     ///< bus busy / arbitration lost
    io,       ///< other transport error
};

template <class D>
concept i2c_device = requires(D& d,
                              std::span<const std::byte> tx,
                              std::span<std::byte>       rx) {
    /// Write-only transaction: all of `tx` in one START..STOP.
    { d.transmit(tx) } -> std::same_as<std::expected<void, i2c_errc>>;
    /// Read-only transaction.
    { d.receive(rx) } -> std::same_as<std::expected<void, i2c_errc>>;
    /// Write `tx` then repeated-START read into `rx` (register read).
    { d.transmit_receive(tx, rx) } -> std::same_as<std::expected<void, i2c_errc>>;
};

// ---- 8-bit register helpers -------------------------------------------------

/// Write a byte to an 8-bit register.
template <i2c_device D>
std::expected<void, i2c_errc> reg_write_u8(D& dev, std::uint8_t reg, std::uint8_t v) {
    const std::array<std::byte, 2> buf{std::byte{reg}, std::byte{v}};
    return dev.transmit(buf);
}

/// Write `data` bytes starting at 8-bit register `reg` (auto-increment).
/// Max 8 payload bytes per call — plenty for these sensors.
template <i2c_device D>
std::expected<void, i2c_errc> reg_write(D&                    dev,
                                        std::uint8_t          reg,
                                        std::span<const std::byte> data) {
    if (data.size() > 8) {
        return std::unexpected(i2c_errc::io);
    }
    std::array<std::byte, 9> buf{};
    buf[0] = std::byte{reg};
    std::copy(data.begin(), data.end(), buf.begin() + 1);
    return dev.transmit(std::span{buf}.first(1 + data.size()));
}

/// Read `out.size()` bytes starting at 8-bit register `reg`.
template <i2c_device D>
std::expected<void, i2c_errc> reg_read(D&                 dev,
                                       std::uint8_t       reg,
                                       std::span<std::byte> out) {
    const std::array<std::byte, 1> w{std::byte{reg}};
    return dev.transmit_receive(w, out);
}

/// Read a byte from an 8-bit register.
template <i2c_device D>
std::expected<std::uint8_t, i2c_errc> reg_read_u8(D& dev, std::uint8_t reg) {
    std::byte b{};
    if (auto r = reg_read(dev, reg, std::span{&b, 1}); !r) {
        return std::unexpected(r.error());
    }
    return std::to_integer<std::uint8_t>(b);
}

/// Read-modify-write: reg = (reg & ~mask) | (value & mask).
template <i2c_device D>
std::expected<void, i2c_errc> reg_update(D&           dev,
                                         std::uint8_t reg,
                                         std::uint8_t mask,
                                         std::uint8_t value) {
    const auto cur = reg_read_u8(dev, reg);
    if (!cur) {
        return std::unexpected(cur.error());
    }
    return reg_write_u8(dev, reg,
                        static_cast<std::uint8_t>((*cur & ~mask) | (value & mask)));
}

/// Set / clear a single bit.
template <i2c_device D>
std::expected<void, i2c_errc> reg_bit(D& dev, std::uint8_t reg, std::uint8_t bit, bool on) {
    const std::uint8_t mask = static_cast<std::uint8_t>(1u << bit);
    return reg_update(dev, reg, mask, on ? mask : 0);
}

} // namespace bus
