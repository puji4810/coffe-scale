#pragma once

// Real-time flow: one trailing weight regression, followed by a fixed EMA.
// There is no stop latch, restart proof, boost, adopted resume rate or gate.
// A new measured trend always contributes, including a slow pour after rest.

#include <algorithm>
#include <cmath>
#include "scale/clock.hpp"
#include "scale/filters.hpp"

namespace scale {

struct flow_config {
    clock_ms window{1200};      // trailing regression window at the default 80 SPS
    clock_ms min_span{100};     // enough time to form a slope after reset/gaps
    clock_ms gap{250};          // discard evidence older than a sampling outage
    float smoothing_s = .10f;   // fixed publication EMA; <=0 publishes the fit
    float step_g = 20.f;        // a gross level jump must exceed BOTH bounds
    float step_gps = 60.f;      // to be treated as a placement/removal
};

class flow_rate {
public:
    explicit flow_rate(const flow_config& cfg = {}) { configure(cfg); }
    void configure(const flow_config& cfg) {
        cfg_ = cfg;
        cfg_.window = clock_ms{std::clamp<clock_ms::rep>(cfg.window.count(), 100, 1500)};
        cfg_.min_span = clock_ms{std::clamp<clock_ms::rep>(cfg.min_span.count(), 1, cfg_.window.count())};
        cfg_.gap = clock_ms{std::max<clock_ms::rep>(cfg.gap.count(), 1)};
        reset();
    }
    void reset() {
        samples_.clear();
        primed_ = false;
        discontinuity_ = false;
        slope_ = published_ = weight_ = offset_ = last_g_ = 0.f;
    }
    void push(clock_ms now, float grams) {
        if (!std::isfinite(grams)) return;
        if (primed_ && now <= last_t_) return;
        discontinuity_ = false;
        if (!primed_ || now - last_t_ > cfg_.gap) {
            reset();
            samples_.push({now, grams});
            last_t_ = now;
            last_g_ = weight_ = grams;
            primed_ = true;
            return;
        }
        const float dt = (now - last_t_).count() * .001f;
        const float delta = grams - last_g_;
        if (std::fabs(delta) > cfg_.step_g && std::fabs(delta) > cfg_.step_gps * dt) {
            // Translate the observation origin across a gross load step.
            // Keep the current slope rather than suppressing or reactivating
            // the pour. Subsequent weight samples update it normally.
            offset_ += delta - std::clamp(slope_, -30.f, 30.f) * dt;
            discontinuity_ = true;
        }
        last_t_ = now;
        last_g_ = grams;
        samples_.push({now, grams - offset_});
        weight_ = grams;

        std::size_t first = 0;
        while (first + 1 < samples_.size() && now - samples_.at(first).t > cfg_.window) ++first;
        const auto origin = samples_.at(first);
        const std::size_t n = samples_.size() - first;
        if (n < 3 || now - origin.t < cfg_.min_span) return;
        float st = 0, sy = 0, stt = 0, sty = 0;
        for (std::size_t i = first; i < samples_.size(); ++i) {
            const auto p = samples_.at(i);
            // Relative integer time and weight keep long uptime and loaded
            // pans precise. There are no rebasing timers.
            const float t = (p.t - now).count() * .001f, y = p.g - origin.g;
            st += t; sy += y; stt += t*t; sty += t*y;
        }
        const float nf = static_cast<float>(n), den = nf*stt - st*st;
        if (den <= 1e-9f) return;
        slope_ = (nf*sty - st*sy) / den;
        const float mean = origin.g + sy/nf + offset_;
        const float fitted = origin.g + (sy - slope_*st) / nf + offset_;
        // The fitted level is used only for weight-display lag compensation.
        // A line through a placement kink can extrapolate past the new load;
        // keep that correction between the measured mean and latest weight.
        // This bound never changes the flow slope or its publication.
        weight_ = std::clamp(fitted, std::min(mean, grams), std::max(mean, grams));
        const float alpha = cfg_.smoothing_s > 0.f
            ? -std::expm1(-dt / cfg_.smoothing_s) : 1.f;
        published_ += alpha * (slope_ - published_);
    }

    [[nodiscard]] float rate() const { return slope_; }
    [[nodiscard]] float readout_rate() const { return published_; }
    [[nodiscard]] float weight() const { return weight_; }
    [[nodiscard]] bool primed() const { return primed_; }
    // A one-sample observation flag for diagnostics, never a flow gate.
    [[nodiscard]] bool discontinuity() const { return discontinuity_; }

private:
    struct sample { clock_ms t; float g; };
    flow_config cfg_{};
    window<sample, 160> samples_; // >=1.5 seconds at 80 SPS, no heap
    clock_ms last_t_{};
    float slope_ = 0, published_ = 0, weight_ = 0, offset_ = 0, last_g_ = 0;
    bool primed_ = false, discontinuity_ = false;
};

} // namespace scale
