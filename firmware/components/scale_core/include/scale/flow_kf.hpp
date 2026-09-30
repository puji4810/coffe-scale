#pragma once

/// Pour/flow estimator: constant-velocity Kalman filter on a low-passed
/// weight signal with an innovation gate for impact rejection.
///
/// Ported 1:1 from the settled reference prototype (FlowKF):
///   pre = LP8(4th order = two cascaded Butterworth biquads) of the input
///   CV Kalman, state [w, f], real dt, Q = q*[[dt^3/3, dt^2/2],[dt^2/2, dt]]
///   |innovation| > gate_g   -> gate: rewind to the newest history state at
///     least rewind_s old, predict it forward to now, hold the flow
///   while gated            -> no update; once the trailing calm_s of `pre`
///     line-fits with max residual < calm_g, re-anchor x=[level, f0]
///     (f0 = held flow if it still predicts the level within step_g, else 0)
///   output while gated     -> held_f for hold_s, then 0
///
/// float32 throughout, fixed-capacity rings, no heap. Time is rebased per
/// anchor so pushes can run for hours without losing float precision.

#include <cmath>
#include <cstddef>

#include "scale/clock.hpp"
#include "scale/filters.hpp"

namespace scale {

struct flow_kf_config {
    float q          = 5.0f;    // flow random-walk spectral density (g^2/s^3)
    float r          = 1.0f;    // measurement variance (g^2)
    float cutoff_hz  = 8.0f;    // prefilter: 2x cascaded Butterworth LP
    float sample_hz  = 80.0f;   // biquad design rate
    float gate_g     = 4.0f;    // |innovation| that trips the gate
    float rewind_s   = 0.1f;    // how far back the gate restores state
    float calm_s     = 0.3f;    // trailing window for the un-gate line fit
    float calm_g     = 1.5f;    // max |residual| of that fit to un-gate
    float hold_s     = 1.0f;    // how long the held flow is reported
    float step_g     = 5.0f;    // |level - predicted| bound for keeping f0
};

class flow_kf {
public:
    explicit flow_kf(const flow_kf_config& cfg = flow_kf_config{}) {
        configure(cfg);
    }

    void configure(const flow_kf_config& cfg) {
        cfg_ = cfg;
        b1_.configure(cfg.cutoff_hz, cfg.sample_hz);
        b2_.configure(cfg.cutoff_hz, cfg.sample_hz);
        reset();
    }

    /// Next push re-primes prefilters and state.
    void reset() {
        b1_.reset();
        b2_.reset();
        primed_   = false;
        base_set_ = false;
        gated_    = false;
        hist_.clear();
        buf_.clear();
    }

    /// One weight sample; `now` a real monotone millisecond timestamp
    /// (integer — stays exact at any uptime, unlike a float of seconds).
    void push(clock_ms now, float g) {
        if (!primed_ && !base_set_) {
            base_     = now;
            base_set_ = true;
        }
        const float t =
            static_cast<float>((now - base_).count()) * 1e-3f;
        const float pre = b2_.push(b1_.push(g));
        buf_.push({t, pre});

        if (!primed_) {
            anchor(pre, 0.0f);
            tp_     = t;
            primed_ = true;
            hist_.push({t, w_, f_, p00_, p01_, p10_, p11_});
            return;
        }

        float dt = t - tp_;
        if (dt < 1e-4f) dt = 1e-4f;
        tp_ = t;
        predict(dt);

        const float inn = pre - w_;
        if (!gated_ && std::fabs(inn) > cfg_.gate_g) {
            gated_ = true;
            // Rewind: restore the newest history state at least rewind_s
            // old and predict it forward to now (drops the onset sample).
            for (std::size_t i = hist_.size(); i-- > 0;) {
                const auto& h = hist_.at(i);
                if (t - h.t < cfg_.rewind_s) continue;
                const float d = t - h.t;
                restore(h);
                predict(d);
                break;
            }
            held_f_ = f_;
            gate_t_ = t;
            gate_w_ = w_;
        }

        if (gated_) {
            ungate_check(t);
        } else {
            // K = P[:,0] / (P00 + r); x += K*inn; P -= K x P[0,:]
            const float s  = p00_ + cfg_.r;
            const float k0 = p00_ / s;
            const float k1 = p10_ / s;
            w_ += k0 * inn;
            f_ += k1 * inn;
            const float q00 = p00_, q01 = p01_, q10 = p10_, q11 = p11_;
            p00_ -= k0 * q00;
            p01_ -= k0 * q01;
            p10_ -= k1 * q00;
            p11_ -= k1 * q01;
        }

        hist_.push({t, w_, f_, p00_, p01_, p10_, p11_});
        rebase(t);
    }

    /// Output flow, including the gated hold logic: the flow captured at
    /// the gate trip for hold_s, then 0 until un-gating.
    [[nodiscard]] float rate() const {
        if (!primed_) return 0.0f;
        if (gated_) {
            return (tp_ - gate_t_ < cfg_.hold_s) ? held_f_ : 0.0f;
        }
        return f_;
    }

    /// Kalman weight state — a lag-free estimate of the current level.
    [[nodiscard]] float weight() const { return primed_ ? w_ : 0.0f; }
    /// Impact gate engaged.
    [[nodiscard]] bool disturbed() const { return gated_; }
    /// False until the first push primes the state.
    [[nodiscard]] bool primed() const { return primed_; }

private:
    struct hist_pt {   // history entry for the rewind
        float t, w, f, p00, p01, p10, p11;
    };
    struct buf_pt {    // trailing prefiltered sample for the calm fit
        float t, pre;
    };

    void anchor(float w, float f) {
        w_ = w;
        f_ = f;
        p00_ = cfg_.r;
        p01_ = p10_ = 0.0f;
        p11_ = 1.0f;
    }

    void restore(const hist_pt& h) {
        w_ = h.w;
        f_ = h.f;
        p00_ = h.p00;
        p01_ = h.p01;
        p10_ = h.p10;
        p11_ = h.p11;
    }

    /// x = F x; P = F P F^T + Q, F = [[1,dt],[0,1]].
    void predict(float dt) {
        w_ += dt * f_;
        const float q = cfg_.q;
        p00_ += dt * (p01_ + p10_) + dt * dt * p11_ + q * dt * dt * dt / 3.0f;
        const float n01 = p01_ + dt * p11_ + q * dt * dt / 2.0f;
        p01_ = p10_ = n01;
        p11_ += q * dt;
    }

    /// Trailing calm_s line fit of `pre` (t relative to the newest
    /// sample); un-gates when the buffer spans ~the window with >= 6
    /// samples and max |residual| < calm_g.
    void ungate_check(float t) {
        const buf_pt newest = buf_.latest();
        float st = 0.0f, sy = 0.0f, stt = 0.0f, sty = 0.0f;
        int    n = 0;
        float  t_first = 0.0f;
        for (std::size_t i = buf_.size(); i-- > 0;) {
            const buf_pt b = buf_.at(i);
            if (newest.t - b.t > cfg_.calm_s) break;
            if (n == 0 || b.t < t_first) t_first = b.t;
            const float x = b.t - newest.t;
            st += x;
            sy += b.pre;
            stt += x * x;
            sty += x * b.pre;
            ++n;
        }
        if (n < 6 || newest.t - t_first < cfg_.calm_s * 0.9f) return;
        const float nf  = static_cast<float>(n);
        const float den = nf * stt - st * st;
        if (den <= 0.0f) return;
        const float slope = (nf * sty - st * sy) / den;
        const float level = (sy - slope * st) / nf;   // intercept at newest t
        // max |residual|
        float worst = 0.0f;
        for (std::size_t i = buf_.size(); i-- > 0;) {
            const buf_pt b = buf_.at(i);
            if (newest.t - b.t > cfg_.calm_s) break;
            const float x   = b.t - newest.t;
            const float res = std::fabs(b.pre - (level + slope * x));
            if (res > worst) worst = res;
        }
        if (worst >= cfg_.calm_g) return;
        const float pred = gate_w_ + held_f_ * (t - gate_t_);
        const float f0   = std::fabs(level - pred) < cfg_.step_g
                               ? held_f_ : 0.0f;
        gated_ = false;
        anchor(level, f0);
    }

    /// Rebase time when the offset grows past ~2 min so the float32 math
    /// stays precise: advance base_ by a whole number of milliseconds and
    /// subtract the same amount (in seconds) from every stored time —
    /// history and the calm buffer stay usable across the boundary.
    void rebase(float t) {
        if (t <= 120.0f) return;
        const auto  delta_ms = static_cast<std::int64_t>(t * 1000.0f);
        const float delta_s  = static_cast<float>(delta_ms) * 1e-3f;
        base_   += clock_ms{delta_ms};
        tp_     -= delta_s;
        gate_t_ -= delta_s;
        for (std::size_t i = 0; i < hist_.size(); ++i) {
            hist_.at(i).t -= delta_s;
        }
        for (std::size_t i = 0; i < buf_.size(); ++i) {
            buf_.at(i).t -= delta_s;
        }
    }

    // Rings need mutation on rebase: window::at is const-returning.
    // Use plain arrays + counts instead.
    struct ring_hist {
        hist_pt buf[96];
        std::size_t head = 0, n = 0;
        void push(hist_pt p) {
            buf[head] = p;
            head = (head + 1) % 96;
            if (n < 96) ++n;
        }
        void clear() { n = 0; head = 0; }
        [[nodiscard]] std::size_t size() const { return n; }
        hist_pt&       at(std::size_t i)       { return buf[(head + 96 - n + i) % 96]; }
        const hist_pt& at(std::size_t i) const { return buf[(head + 96 - n + i) % 96]; }
    };
    struct ring_buf {
        buf_pt buf[64];
        std::size_t head = 0, n = 0;
        void push(buf_pt p) {
            buf[head] = p;
            head = (head + 1) % 64;
            if (n < 64) ++n;
        }
        void clear() { n = 0; head = 0; }
        [[nodiscard]] std::size_t size() const { return n; }
        buf_pt&        at(std::size_t i)       { return buf[(head + 64 - n + i) % 64]; }
        const buf_pt&  at(std::size_t i) const { return buf[(head + 64 - n + i) % 64]; }
        [[nodiscard]] buf_pt latest() const { return n ? at(n - 1) : buf_pt{}; }
    };

    flow_kf_config cfg_{};
    butter2_lpf    b1_, b2_;
    ring_hist      hist_{};
    ring_buf       buf_{};
    clock_ms       base_{0};       // integer anchor — subtracted from now
    bool           base_set_ = false;
    float          tp_   = 0.0f;   // last processed (rebased) time, s
    float          w_ = 0.0f, f_ = 0.0f;
    float          p00_ = 0.0f, p01_ = 0.0f, p10_ = 0.0f, p11_ = 0.0f;
    bool           primed_ = false;
    bool           gated_  = false;
    float          held_f_ = 0.0f;  // flow at the gate trip
    float          gate_t_ = 0.0f;  // gate start (rebased time)
    float          gate_w_ = 0.0f;  // weight state at gate start
};

} // namespace scale
