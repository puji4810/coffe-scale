#pragma once

/// Slow zero-drift absorber. While the displayed weight sits near zero and
/// stable, the offset creeps toward it; any real load or motion freezes it
/// (the accumulated correction stays applied). This is deliberately strict:
/// a 0.3 g object must never be eaten by auto-zero, so tracking only arms
/// after the residual has been tiny AND stable for `hold` milliseconds.

#include <algorithm>
#include <cmath>

#include "scale/clock.hpp"

namespace scale {

struct zero_track_config {
    bool     enabled = true;
    float    band_g  = 0.5f;      // only track |residual| under this
    float    rate    = 0.008f;    // fraction of residual absorbed per sample
    clock_ms hold{1500};          // must stay in-band this long first
    float    max_g   = 2.0f;      // cap on accumulated |offset|
};

class zero_tracker {
public:
    explicit constexpr zero_tracker(zero_track_config cfg = {}) : cfg_(cfg) {}

    /// Runtime gate (brew session, calibration capture, ...). Disabling
    /// freezes the accumulated offset — it stays applied, just stops moving.
    void set_enabled(bool on) {
        cfg_.enabled = on;
        if (!on) {
            arming_  = false;
            engaged_ = false;
        }
    }

    /// `net_g` = weight before the zero-track offset. Returns the displayed
    /// weight (net - offset).
    float apply(float net_g, bool stable, clock_ms now) {
        const float disp = net_g - off_;
        if (cfg_.enabled && stable && std::fabs(disp) < cfg_.band_g) {
            if (!arming_) {
                arming_ = true;
                since_  = now;
            }
            if (now - since_ >= cfg_.hold) {
                engaged_ = true;
                off_     = std::clamp(off_ + cfg_.rate * disp,
                                      -cfg_.max_g, cfg_.max_g);
            }
        } else {
            arming_  = false;
            engaged_ = false;
        }
        return net_g - off_;
    }

    /// Accumulated offset in grams (what is being subtracted).
    [[nodiscard]] float offset() const { return off_; }
    /// True while a small residual has persisted past `hold`.
    [[nodiscard]] bool  tracking() const {
        return arming_ && engaged_;
    }

    /// Drop offset + timer. Call on tare, calibration, or mode changes
    /// where the residual is intentional.
    void reset() {
        off_     = 0.0f;
        arming_  = false;
        engaged_ = false;
    }

private:
    zero_track_config cfg_;
    float             off_     = 0.0f;
    bool              arming_  = false;
    bool              engaged_ = false;
    clock_ms          since_{0};
};

} // namespace scale
