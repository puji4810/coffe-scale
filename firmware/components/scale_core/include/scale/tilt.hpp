#pragma once

/// Level + motion detection from a 3-axis accelerometer.
///
/// pitch/roll come from gravity only while the unit is quiet — i.e. the
/// low-passed magnitude sits within `g_tol` of 1 g AND the raw magnitude
/// spread over the recent window stays under `quiet_spread`. Pouring,
/// bumps and button presses all break `quiet()`, which is exactly what the
/// scale uses to freeze zero tracking / stability decisions.
///
/// Never fed -> quiet() reports true (a missing sensor must not block the
/// rest of the pipeline).

#include <cmath>

#include "scale/clock.hpp"
#include "scale/filters.hpp"

namespace scale {

struct tilt_config {
    float    lpf_alpha     = 0.15f;    // per-axis EMA on mg
    float    g_nominal_mg  = 1000.f;   // 1 g reference
    float    g_tol_mg      = 80.f;     // ||a| - g| trust window
    float    quiet_spread_mg = 30.f;   // |a| max-min over the quiet window
    std::size_t quiet_window   = 16;   // samples (~0.8 s at 20 Hz)
};

class tilt {
public:
    struct vec3 {
        float x, y, z;    // milli-g
    };

    explicit tilt(tilt_config cfg = {}) : cfg_(cfg) {}

    void configure(const tilt_config& cfg) {
        cfg_ = cfg;
        ax_.set_alpha(cfg.lpf_alpha);
        ay_.set_alpha(cfg.lpf_alpha);
        az_.set_alpha(cfg.lpf_alpha);
        mag_.clear();
        quiet_ = false;
    }

    /// Feed one accel sample in mg (~10-25 Hz is plenty).
    void feed(float x, float y, float z) {
        fed_ = true;
        const float fx = ax_.push(x), fy = ay_.push(y), fz = az_.push(z);
        const float mag = std::sqrt(x * x + y * y + z * z);
        mag_.push(mag);
        const std::size_t k = cfg_.quiet_window;
        quiet_ = mag_.size() >= 2 &&
                 mag_.spread_last(k) <= cfg_.quiet_spread_mg &&
                 std::fabs(mag_.mean_last(k) - cfg_.g_nominal_mg)
                     <= cfg_.g_tol_mg;
        if (quiet_) {
            // Only trust orientation while still.
            pitch_deg_ = std::atan2(fx, std::sqrt(fy * fy + fz * fz))
                         * 57.29577951308232f;
            roll_deg_  = std::atan2(fy, std::sqrt(fx * fx + fz * fz))
                         * 57.29577951308232f;
        }
    }

    /// Low motion right now. True until the first samples arrive.
    [[nodiscard]] bool quiet() const { return !fed_ || quiet_; }
    [[nodiscard]] bool fed() const { return fed_; }

    /// Last trusted orientation. Only meaningful while quiet().
    [[nodiscard]] float pitch_deg() const { return pitch_deg_; }
    [[nodiscard]] float roll_deg() const { return roll_deg_; }

    /// Level check, valid only when quiet.
    [[nodiscard]] bool level(float tol_deg = 1.0f) const {
        return quiet() && std::fabs(pitch_deg_) <= tol_deg &&
               std::fabs(roll_deg_) <= tol_deg;
    }

    /// Filtered acceleration (mg).
    [[nodiscard]] vec3 accel() const {
        return {ax_.value(), ay_.value(), az_.value()};
    }

private:
    tilt_config          cfg_;
    ema                  ax_{0.15f}, ay_{0.15f}, az_{0.15f};
    window<float, 64>    mag_{};
    float                pitch_deg_ = 0.0f;
    float                roll_deg_  = 0.0f;
    bool                 fed_       = false;
    bool                 quiet_     = false;
};

} // namespace scale
