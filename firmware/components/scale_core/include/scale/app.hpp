#pragma once

/// Application model: scale pipeline + brew timer + display mode + unit.
/// Platform code feeds samples and button events; the UI renders `snapshot()`.
/// Pure C++, fully host-testable.

#include <cstdint>

#include "scale/brew_timer.hpp"
#include "scale/scale.hpp"
#include "scale/units.hpp"

namespace scale {

class app {
public:
    enum class mode { weigh, brew };

    struct snapshot {
        float             grams;
        float             flow_gps;   // pour rate, g/s (0 until window fills)
        bool              stable;     // load-cell quiet AND no IMU motion
        bool              tared;
        bool              calibrated;
        unit              u;
        mode              m;
        brew_timer::state timer_state;
        clock_ms          timer_elapsed;
        float             pitch_deg;  // trusted only while the unit is still
        float             roll_deg;
    };

    /// One ADC conversion. `now` is the same injected time base used by the
    /// timer APIs (e.g. esp_timer_get_time()/1000 on target, steady_clock in
    /// tests/sim). Also feeds the flow/zero-track time axis — keep it real.
    void feed(std::int32_t counts, clock_ms now) {
        scale_.push(counts, now);
        now_ = now;
    }

    /// One accelerometer sample in mg (LIS2DW12, ~10-25 Hz).
    void feed_accel(float x_mg, float y_mg, float z_mg) {
        scale_.feed_accel(x_mg, y_mg, z_mg);
    }

    /// TARE (short press): zero in any mode.
    void tare() { scale_.tare(); }

    /// TARE (long press): in brew mode toggles the timer; elsewhere ignored.
    void tare_long() {
        if (mode_ == mode::brew) {
            timer_.toggle(now_);
        }
    }

    /// Reset the brew timer to 0:0.0 in any mode (does not touch tare).
    void timer_reset() { timer_.reset(); }

    /// MODE (short press): weigh <-> brew.
    void next_mode() {
        mode_ = mode_ == mode::weigh ? mode::brew : mode::weigh;
        if (mode_ == mode::weigh) {
            timer_.reset();
        }
        // Freeze slow drift for the brew session — dripping/evaporation
        // remains real mass. Proven physical unloads may still restore zero.
        scale_.set_zero_tracking(mode_ == mode::weigh);
    }

    void               set_unit(unit u) { unit_ = u; }
    [[nodiscard]] unit current_unit() const { return unit_; }
    [[nodiscard]] mode current_mode() const { return mode_; }

    // Calibration passthroughs (UI/menu layer decides when to call).
    void cal_zero() { scale_.cal_zero(); }
    bool cal_span(float mass_g) { return scale_.cal_span(mass_g); }
    void load_calibration(const calibration& c) { scale_.load_calibration(c); }

    /// Feed the ambient/sensor temperature (TMP102) — drives drift comp.
    void set_temperature(float temp_c) { scale_.set_temperature(temp_c); }

    [[nodiscard]] scale&       inner() { return scale_; }
    [[nodiscard]] const scale& inner() const { return scale_; }

    /// Weight for the readout: display-quantised grams (hysteresis) in
    /// the current unit. snapshot().grams stays continuous.
    [[nodiscard]] float display_value() const {
        return convert(scale_.display_grams(), unit_);
    }

    [[nodiscard]] snapshot state() const {
        return {.grams         = scale_.grams(),
                .flow_gps      = scale_.flow_gps(),
                .stable        = scale_.system_stable(),
                .tared         = scale_.tared(),
                .calibrated    = scale_.calibrated(),
                .u             = unit_,
                .m             = mode_,
                .timer_state   = timer_.current(),
                .timer_elapsed = timer_.elapsed(now_),
                .pitch_deg     = scale_.pitch_deg(),
                .roll_deg      = scale_.roll_deg()};
    }

private:
    scale      scale_;
    brew_timer timer_;
    unit       unit_ = unit::gram;
    mode       mode_ = mode::weigh;
    clock_ms   now_{0};
};

} // namespace scale
