#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include "scale/weight_window.hpp"

namespace scale {

struct zero_track_config {
    bool enabled = true;
    float band_g = .5f;
    float rate = .008f;
    clock_ms hold{1500};
    float max_g = 2.f;
    float acquire_g = .05f;       // initial physical empty evidence
    float step_g = .08f;          // new small load edge
    float drift_gps = .04f;       // only slow trends are zero drift
    float return_band_g = .25f;   // bounded residual after a large unload
    clock_ms return_hold{300};
};

/// Physical empty-pan drift, independent of tare. New small loads freeze
/// correction; small and stable alone cannot authorize zero. A large
/// unload near the saved empty reference can restore a bounded residual,
/// including in brew mode. Its small residual is inherently ambiguous.
class zero_tracker {
public:
    explicit zero_tracker(zero_track_config cfg = {})
        : cfg_(cfg), drift_enabled_(cfg.enabled) {}
    // Brew freezes slow drift; configuration disabled also disables
    // unload restoration. Mode changes retain physical load context.
    void set_enabled(bool on) { drift_enabled_ = on; cancel_pending(); }
    float apply(float net_g, bool stable, clock_ms now) {
        return apply(net_g, net_g, stable, now);
    }
    // gross_g is filtered physical weight before tare/offset; input_g is
    // its median observation, so the LPF cannot hide a placement edge.
    float apply(float gross_g, float input_g, bool stable, clock_ms now) {
        const bool gap = primed_ && (now <= last_time_ ||
                                    now - last_time_ > clock_ms{250});
        const float before = last_input_ - off_;
        const float current = input_g - off_;
        if (gap) {
            evidence_.clear(); cancel_pending(); wide_return_ = false;
            if (std::fabs(current) >= cfg_.acquire_g) {
                empty_ = false; loaded_ = true;
            }
        }
        const bool edge = primed_ && !gap &&
            std::fabs(input_g-last_input_) >= cfg_.step_g;
        if (edge) {
            const auto previous = evidence_.read(cfg_.return_hold);
            if (std::fabs(before) > 2.f && previous.ready &&
                std::fabs(previous.mean-off_) > 2.f &&
                std::fabs(current) < cfg_.return_band_g)
                wide_return_ = true;
            else if (std::fabs(current) >= cfg_.acquire_g)
                wide_return_ = false;
            if (std::fabs(current) >= cfg_.acquire_g) {
                empty_ = false; loaded_ = true; cancel_pending();
            }
        }
        if (std::fabs(current) > cfg_.band_g) {
            loaded_ = true; empty_ = false; cancel_pending();
            wide_return_ = false;
        }
        evidence_.push(input_g, now);
        const auto fit = evidence_.read(cfg_.return_hold);
        const auto trend = evidence_.read(clock_ms{1000});
        last_input_ = input_g; last_time_ = now; primed_ = true;
        // A bounded settling oscillation may have a nonzero fitted slope.
        // Require a visible turn within the window before accepting it;
        // a monotone low-rate tail still needs the strict drift slope.
        const bool calm = stable && fit.ready && fit.spread <= .10f &&
                          (std::fabs(fit.slope) <= cfg_.drift_gps || fit.turning);
        // Smooth real loading also freezes drift without needing an edge.
        if (empty_ && trend.ready && std::fabs(trend.mean-off_) >= cfg_.step_g &&
            std::fabs(trend.slope) > cfg_.drift_gps) {
            empty_ = false; loaded_ = true; cancel_pending();
        }
        const float return_band = wide_return_ ? cfg_.return_band_g : cfg_.acquire_g;
        const bool returned = loaded_ && calm &&
            std::fabs(fit.mean-off_) < return_band &&
            std::fabs(current) < return_band;
        if (returned && cfg_.enabled) {
            off_ = std::clamp(fit.mean, -cfg_.max_g, cfg_.max_g);
            empty_ = true; loaded_ = false; wide_return_ = false;
            cancel_pending();
        }
        const bool acquire = !loaded_ && !empty_ && calm &&
            std::fabs(current) < cfg_.acquire_g;
        const bool drift = empty_ && stable && trend.ready &&
            std::fabs(trend.slope) <= cfg_.drift_gps &&
            std::fabs(gross_g-off_) < cfg_.band_g;
        if (cfg_.enabled && drift_enabled_ && (acquire || drift)) {
            if (!since_) since_ = now;
            if (now - *since_ >= cfg_.hold) {
                empty_ = true;
                engaged_ = true;
                off_ = std::clamp(off_ + cfg_.rate*(gross_g-off_),
                                  -cfg_.max_g, cfg_.max_g);
            }
        } else { since_.reset(); engaged_ = false; }
        return gross_g - off_;
    }
    [[nodiscard]] float offset() const { return off_; }
    [[nodiscard]] bool tracking() const { return engaged_; }
    [[nodiscard]] bool empty() const { return empty_; }
    void cancel_pending() { since_.reset(); engaged_ = false; }
    void reset() {
        cancel_pending(); evidence_.clear(); off_ = last_input_ = 0;
        empty_ = loaded_ = wide_return_ = primed_ = false;
        last_time_ = clock_ms{0};
    }
private:
    zero_track_config cfg_;
    weight_window<128> evidence_;
    std::optional<clock_ms> since_;
    clock_ms last_time_{0};
    float off_ = 0, last_input_ = 0;
    bool drift_enabled_ = true, empty_ = false, loaded_ = false;
    bool wide_return_ = false, primed_ = false, engaged_ = false;
};

} // namespace scale
