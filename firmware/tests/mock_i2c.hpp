#pragma once

/// Register-file I2C device mock: writes land in `regs`, reads auto-increment
/// out of `regs`. `on_write` emulates device side effects (status bits, cal
/// completion, ...). Every transaction is logged for assertions.

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <span>
#include <utility>
#include <vector>

#include "bus/i2c.hpp"

class mock_i2c {
public:
    std::map<std::uint8_t, std::uint8_t> regs;
    /// (register, first data byte) of every write transaction.
    std::vector<std::pair<std::uint8_t, std::uint8_t>> writes;
    /// Side-effect hook invoked after each register write.
    std::function<void(std::uint8_t reg, std::uint8_t val)> on_write;

    std::expected<void, bus::i2c_errc> transmit(std::span<const std::byte> tx) {
        if (tx.size() < 2) {
            return std::unexpected(bus::i2c_errc::io);
        }
        const auto reg = std::to_integer<std::uint8_t>(tx[0]);
        for (std::size_t i = 1; i < tx.size(); ++i) {
            regs[static_cast<std::uint8_t>(reg + i - 1)] =
                std::to_integer<std::uint8_t>(tx[i]);
        }
        const auto first = std::to_integer<std::uint8_t>(tx[1]);
        writes.emplace_back(reg, first);
        if (on_write) {
            on_write(reg, first);
        }
        return {};
    }

    std::expected<void, bus::i2c_errc> receive(std::span<std::byte> rx) {
        std::fill(rx.begin(), rx.end(), std::byte{0});
        return {};
    }

    std::expected<void, bus::i2c_errc> transmit_receive(std::span<const std::byte> tx,
                                                        std::span<std::byte>       rx) {
        if (tx.empty()) {
            return std::unexpected(bus::i2c_errc::io);
        }
        auto reg = std::to_integer<std::uint8_t>(tx[0]);
        for (auto& b : rx) {
            b = std::byte{regs[reg++]};   // sequential, wraps through the reg file
        }
        return {};
    }
};

static_assert(bus::i2c_device<mock_i2c>);
