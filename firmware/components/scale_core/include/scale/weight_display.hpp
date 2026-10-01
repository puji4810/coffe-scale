#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include "scale/weight_window.hpp"

namespace scale {

struct weight_display_config {
    float resolution_g = .1f;
    float hysteresis_g = .02f;
    clock_ms hold{300};
};

/// Display-only platform hold. A short flat window settles placements;
/// a longer stable mean covers bounded vibration. The anchor is a
/// continuous platform, independent of its rounded digit. Edges, clean
/// trends and sustained mean changes release it; measurements keep flowing.
class weight_display {
public:
    void configure(weight_display_config cfg) { cfg_ = cfg; reset(); }
    void reset() {
        weights_.clear(); means_.clear(); flat_since_.reset(); release_since_.reset();
        primed_ = held_ = false; shown_ = center_ = noise_ = last_input_ = 0;
        last_time_ = clock_ms{0};
    }
    void push(float input, float compensated, float rate,
              bool quiet, std::optional<float> empty_value, clock_ms now) {
        const float band = cfg_.resolution_g*.5f + cfg_.hysteresis_g;
        const bool gap = primed_ && (now <= last_time_ ||
                                    now - last_time_ > clock_ms{250});
        const bool edge = primed_ && std::fabs(input-last_input_) >
            std::max(cfg_.resolution_g*.6f, noise_ + cfg_.resolution_g*.5f);
        if (gap || edge) {
            held_ = false; weights_.clear(); means_.clear();
            noise_ = 0;
            flat_since_.reset(); release_since_.reset();
        }
        last_input_ = input; last_time_ = now;
        weights_.push(input, now);
        if (cfg_.resolution_g <= 0) {
            shown_ = compensated; primed_ = true; return;
        }
        if (cfg_.hold.count() <= 0) {
            live(compensated); primed_ = true; return;
        }
        const auto short_span = std::min(cfg_.hold, clock_ms{200});
        const auto short_fit = weights_.read(short_span);
        const auto long_fit = weights_.read(clock_ms{2000});
        if (long_fit.ready) means_.push(long_fit.mean, now);
        const auto mean_fit = means_.read(cfg_.hold);
        const bool flat = quiet && short_fit.ready &&
            short_fit.spread <= cfg_.resolution_g*.3f &&
            std::fabs(short_fit.slope) <= .05f;
        if (!flat) flat_since_.reset();
        else if (!flat_since_) flat_since_ = now;
        if (empty_value) {
            center_ = *empty_value;
            shown_ = quantize(center_); held_ = primed_ = true;
            noise_ = std::max({noise_, short_fit.spread, long_fit.spread});
            return;
        }
        if (held_) {
            const bool changed = short_fit.ready &&
                std::fabs(short_fit.mean-center_) > band;
            const bool low_noise = noise_ <= cfg_.resolution_g*.5f;
            const bool trend = changed && low_noise &&
                short_fit.worst <= cfg_.resolution_g*.03f;
            const bool mean_changed = long_fit.ready &&
                std::fabs(long_fit.mean-center_) > band;
            if (!(changed && flat &&
                  (low_noise || short_fit.spread <= cfg_.resolution_g*.02f)))
                release_since_.reset();
            else if (!release_since_) release_since_ = now;
            const bool new_flat = release_since_ &&
                now - *release_since_ >= std::min(cfg_.hold, clock_ms{80});
            const bool large = std::fabs(input-center_) >
                std::max(cfg_.resolution_g*3.f, noise_ + band*2.f);
            if (trend || mean_changed || new_flat || large ||
                (low_noise && std::fabs(rate) >= .5f && std::fabs(input-center_) > band)) {
                held_ = false; flat_since_.reset(); release_since_.reset();
            } else return;
        }
        live(compensated);
        const bool short_ready = flat_since_ &&
            now - *flat_since_ >= cfg_.hold;
        const bool clean_trend = short_fit.ready &&
            std::fabs(short_fit.slope) > .05f &&
            short_fit.worst <= cfg_.resolution_g*.03f;
        const bool mean_ready = quiet && !clean_trend &&
            long_fit.ready && mean_fit.ready &&
            long_fit.spread <= cfg_.resolution_g*2.2f &&
            mean_fit.spread <= cfg_.resolution_g*.3f &&
            std::fabs(mean_fit.slope) <= .05f;
        if (short_ready || mean_ready) {
            center_ = short_ready ? short_fit.mean : long_fit.mean;
            noise_ = short_ready ? short_fit.spread : long_fit.spread;
            // Keep the existing hysteretic digit on a rounding boundary.
            if (std::fabs(center_-shown_) > band) shown_ = quantize(center_);
            held_ = true;
        }
        primed_ = true;
    }
    [[nodiscard]] float value() const { return shown_ == 0 ? 0.f : shown_; }
private:
    float quantize(float g) const {
        return cfg_.resolution_g > 0 ? std::round(g/cfg_.resolution_g)*cfg_.resolution_g : g;
    }
    void live(float g) {
        if (!primed_ || std::fabs(g-shown_) > cfg_.resolution_g*.5f + cfg_.hysteresis_g)
            shown_ = quantize(g);
    }
    weight_display_config cfg_;
    weight_window<160> weights_;
    weight_window<64> means_;
    std::optional<clock_ms> flat_since_, release_since_;
    clock_ms last_time_{0};
    float shown_ = 0, center_ = 0, noise_ = 0, last_input_ = 0;
    bool primed_ = false, held_ = false;
};

} // namespace scale
