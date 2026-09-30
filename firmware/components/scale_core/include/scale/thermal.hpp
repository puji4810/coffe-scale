#pragma once

/// First-order thermal-drift model for the load-cell zero. Measure the
/// zero counts at a few temperatures, then fit zero(T) = a*T + b. The model
/// is applied as a *relative* counts offset, drift(T) = a*(T - ref_c), so
/// it composes with the two-point calibration: pick ref_c near the
/// temperature the calibration was captured at and the correction is ~0
/// there.
///
/// Offset-only on purpose — a gain(T) term can be added the same way once
/// measured data shows it matters.

#include <cstddef>
#include <span>

namespace scale {

class thermal_model {
public:
    struct point {
        float temp_c;
        float zero_counts;
    };
    static constexpr std::size_t max_points = 8;

    /// Least-squares slope fit over `pts` (>= 2 needed for a slope; fewer
    /// or degenerate points -> flat model, no correction). `ref_c` is the
    /// anchor temperature where drift = 0; pass 0 to use the mean temp.
    bool fit(std::span<const point> pts, float ref_c = 0.0f) {
        const std::size_t n = pts.size() > max_points ? max_points : pts.size();
        if (n < 2) {
            fitted_ = false;
            a_      = 0.0f;
            return false;
        }
        float mt = 0.0f, mz = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            mt += pts[i].temp_c;
            mz += pts[i].zero_counts;
        }
        mt /= static_cast<float>(n);
        mz /= static_cast<float>(n);
        float num = 0.0f, den = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            const float dt = pts[i].temp_c - mt;
            num += dt * (pts[i].zero_counts - mz);
            den += dt * dt;
        }
        ref_    = ref_c != 0.0f ? ref_c : mt;
        a_      = den > 0.0f ? num / den : 0.0f;
        fitted_ = den > 0.0f;
        return fitted_;
    }

    /// Counts-domain zero correction at `temp_c`, 0 at the reference temp.
    [[nodiscard]] float drift_counts(float temp_c) const {
        return fitted_ ? a_ * (temp_c - ref_) : 0.0f;
    }

    [[nodiscard]] bool  fitted() const { return fitted_; }
    [[nodiscard]] float slope() const { return a_; }       // counts / degC
    [[nodiscard]] float ref_c() const { return ref_; }

private:
    float a_      = 0.0f;
    float ref_    = 0.0f;
    bool  fitted_ = false;
};

} // namespace scale
