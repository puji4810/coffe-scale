#pragma once

/// Pour/flow rate via least-squares slope of weight vs time over a trailing
/// window — far quieter than differencing consecutive samples, which
/// amplifies quantisation noise into useless jitter.

#include <cstddef>
#include <optional>

#include "scale/filters.hpp"

namespace scale {

template <std::size_t Cap = 128>
class flow_rate {
public:
    struct point {
        float t_s;
        float g;
    };

    /// `span_s` regression window at rest; `min_pts` below this the rate
    /// is unknown. While a pour runs the lookback shrinks toward
    /// `min_span_s` for a faster estimate — `adapt_ref_gps` is the |rate|
    /// at which the window halves — and relaxes back as the rate decays.
    constexpr flow_rate(float span_s = 1.0f, std::size_t min_pts = 8,
                        float min_span_s = 1.0f,
                        float adapt_ref_gps = 5.0f)
        : span_s_(span_s), min_pts_(min_pts),
          min_span_s_(std::min(min_span_s, span_s)),
          adapt_ref_(adapt_ref_gps), span_now_(span_s) {}

    /// Push one filtered weight sample; `t_s` any monotone time in seconds.
    /// Returns the current slope estimate (g/s) once enough in-span data
    /// exists, std::nullopt otherwise. Discarding is fine — rate() has it.
    std::optional<float> push(float t_s, float g) {
        w_.push({t_s, g});
        last_ = compute(t_s);
        // Adapt the lookback for the next sample: fast pour -> short
        // window (responsive), quiet signal -> full window (smooth).
        const float mag    = std::fabs(last_.value_or(0.0f));
        const float shrink =
            adapt_ref_ > 0.0f ? 1.0f + mag / adapt_ref_ : 1.0f;
        span_now_ = std::clamp(span_s_ / shrink, min_span_s_, span_s_);
        return last_;
    }

    [[nodiscard]] std::optional<float> rate() const { return last_; }

    /// Regression line evaluated at the newest pushed timestamp — a
    /// lag-free estimate of the current weight. std::nullopt whenever
    /// rate() is.
    [[nodiscard]] std::optional<float> fit_now() const { return fit_; }

    void reset() {
        w_.clear();
        last_     = std::nullopt;
        fit_      = std::nullopt;
        span_now_ = span_s_;
    }

private:
    std::optional<float> compute(float t_now) {
        fit_              = std::nullopt;
        const float t_min = t_now - span_now_;
        float       st = 0.0f, sw = 0.0f, stt = 0.0f, stw = 0.0f;
        float       tb = 0.0f;
        std::size_t k  = 0;
        for (std::size_t i = 0; i < w_.size(); ++i) {
            const point p = w_.at(i);
            if (p.t_s < t_min) {
                continue;
            }
            if (k == 0) {
                tb = p.t_s;    // rebase: keep products small for float32
            }
            const float x = p.t_s - tb;
            st += x;
            sw += p.g;
            stt += x * x;
            stw += x * p.g;
            ++k;
        }
        if (k < min_pts_) {
            return std::nullopt;
        }
        const float n   = static_cast<float>(k);
        const float den = n * stt - st * st;
        if (den <= 0.0f) {
            return std::nullopt;    // no time spread -> slope undefined
        }
        const float slope  = (n * stw - st * sw) / den;
        const float mean_x = st / n;
        const float mean_g = sw / n;
        fit_ = mean_g + slope * ((t_now - tb) - mean_x);
        return slope;
    }

    window<point, Cap>        w_{};
    float                     span_s_;
    std::size_t               min_pts_;
    float                     min_span_s_;
    float                     adapt_ref_;
    float                     span_now_;
    std::optional<float>      last_;
    std::optional<float>      fit_;
};

} // namespace scale
