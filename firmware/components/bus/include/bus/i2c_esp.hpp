#pragma once

/// ESP-IDF i2c_master implementation of bus::i2c_device.
/// Only compiled in the ESP-IDF build (see components/bus/CMakeLists.txt).

#include "bus/i2c.hpp"

#include "driver/i2c_master.h"

namespace bus {

/// One bound device on an i2c_master bus. Construct via `create()`.
class i2c_dev_esp {
public:
    i2c_dev_esp() = default;
    i2c_dev_esp(i2c_dev_esp&& o) noexcept : dev_(o.dev_) { o.dev_ = nullptr; }
    i2c_dev_esp& operator=(i2c_dev_esp&& o) noexcept {
        if (this != &o) {
            if (dev_) {
                i2c_master_bus_rm_device(dev_);
            }
            dev_   = o.dev_;
            o.dev_ = nullptr;
        }
        return *this;
    }
    i2c_dev_esp(const i2c_dev_esp&)            = delete;
    i2c_dev_esp& operator=(const i2c_dev_esp&) = delete;

    ~i2c_dev_esp() {
        if (dev_) {
            i2c_master_bus_rm_device(dev_);
        }
    }

    [[nodiscard]] static std::expected<i2c_dev_esp, i2c_errc>
    create(i2c_master_bus_handle_t bus, std::uint8_t addr,
           std::uint32_t scl_hz = 400'000) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = addr,
            .scl_speed_hz    = scl_hz,
            .scl_wait_us     = 0,
            .flags           = {},
        };
        i2c_dev_esp d;
        if (i2c_master_bus_add_device(bus, &cfg, &d.dev_) != ESP_OK || d.dev_ == nullptr) {
            return std::unexpected(i2c_errc::io);
        }
        return d;
    }

    std::expected<void, i2c_errc> transmit(std::span<const std::byte> tx) {
        return map(i2c_master_transmit(dev_,
                                       reinterpret_cast<const std::uint8_t*>(tx.data()),
                                       static_cast<int>(tx.size()), kTimeoutMs));
    }

    std::expected<void, i2c_errc> receive(std::span<std::byte> rx) {
        return map(i2c_master_receive(dev_,
                                      reinterpret_cast<std::uint8_t*>(rx.data()),
                                      static_cast<int>(rx.size()), kTimeoutMs));
    }

    std::expected<void, i2c_errc> transmit_receive(std::span<const std::byte> tx,
                                                   std::span<std::byte>       rx) {
        return map(i2c_master_transmit_receive(
            dev_, reinterpret_cast<const std::uint8_t*>(tx.data()),
            static_cast<int>(tx.size()), reinterpret_cast<std::uint8_t*>(rx.data()),
            static_cast<int>(rx.size()), kTimeoutMs));
    }

private:
    static constexpr int kTimeoutMs = 50;

    static std::expected<void, i2c_errc> map(esp_err_t e) {
        switch (e) {
            case ESP_OK:                 return {};
            case ESP_ERR_TIMEOUT:        return std::unexpected(i2c_errc::timeout);
            case ESP_FAIL:               return std::unexpected(i2c_errc::nack);
            case ESP_ERR_INVALID_STATE:  return std::unexpected(i2c_errc::busy);
            default:                     return std::unexpected(i2c_errc::io);
        }
    }

    i2c_master_dev_handle_t dev_ = nullptr;
};

} // namespace bus
