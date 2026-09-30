#pragma once

/// Pour/flow estimator: constant-velocity Kalman filter on a low-passed
/// weight signal, with the impact detector and the tracking speed kept
/// on separate evidence paths so each can be tuned on its own.
///
/// Two observations of the same grams input:
///   pre  = 2x cascaded cutoff_hz Butterworth -> Kalman measurement
///   fast = single fast_hz Butterworth        -> detection only (earlier)
///
/// Tracking speed (three densities, state preserved throughout):
///   rest   -> q = cfg.q            (quiet readout)
///   track  -> q = cfg.q_track      (a boost-confirmed slope persists
///              briefly, so a long pour doesn't pump in and out of boost)
///   boost  -> q = cfg.q_boost      (the flow itself changed)
///
/// Boost entry needs a sustained same-sign innovation AND a clean line
/// fit on `fast` whose slope disagrees with f_ in the innovation's
/// direction — steep claims (>boost_steep_gps, i.e. a slam not a pour)
/// need boost_steep_s of proof instead of boost_min_s. Slope-sign
/// flips suppress entry for boost_flip_s (oscillation never reads as a
/// pour) and each lost-evidence drop lengthens the re-entry hold-off.
/// Entry snaps f_ to the fitted slope bounded by boost_snap_gps; while
/// boosted, f_ is capped on the pour side only — the state still has
/// to climb to the slope itself. If the signal is already flat while
/// the estimate trails it, the change was a gently-placed mass: the
/// boost aborts into the gate (rewinding past the boost start).
/// feed_accel(|delta| mg) only vetoes boost — motion can prevent a
/// false speed-up, never fabricate flow.
///
/// Impact gate:
///   |inn| > gate_g (pre) or |inn_fast| > gate_fast_g or a windowed-mean
///   jump > jump_g on fast that fails a "still one clean line" residual
///   test (a sustained steep pour is linear, a step is not) -> gate:
///   rewind to the newest history state at least rewind_s old, predict
///   forward to now, hold the captured flow decaying linearly to 0 over
///   hold_s. The gate holds at least gate_min_s, then un-gates once the
///   trailing calm_s fit of `pre` AND its newest half are both calm.
///   f0 is the half-window slope when it is flat or agrees with the
///   full fit (bounded by fit_slope_max_gps) — pouring through an
///   impact keeps pouring, a stop re-anchors at ~0 — else the held
///   flow if it still predicts the level within step_g, else 0.
///
/// A dt beyond gap_s re-primes the prefilters AND drops boost/track
/// evidence — the Kalman sees the real dt, and a level jump across the
/// gap lands in the ordinary gate path.
///
/// float32 throughout, fixed-capacity rings, no heap. Time is rebased
/// per anchor so pushes can run for hours without losing float
/// precision. configure() resets; set_tracking() retunes q levels
/// without touching state, covariance or history.

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "scale/clock.hpp"
#include "scale/filters.hpp"

namespace scale {

struct flow_kf_config {
    float q          = 5.0f;    // flow random-walk spectral density (g^2/s^3)
    float q_boost    = 80.0f;   // density while a flow change is confirmed
    float r          = 1.0f;    // measurement variance (g^2)
    float cutoff_hz  = 8.0f;    // measurement prefilter: 2x Butterworth
    float fast_hz    = 16.0f;   // detection channel: 1x Butterworth
    float sample_hz  = 80.0f;   // biquad design rate
    float gap_s      = 0.04f;   // dt beyond this re-primes the prefilters
    float gate_g     = 4.0f;    // |innovation| on pre that trips the gate
    float gate_fast_g = 3.0f;   // |innovation| on fast that trips the gate
    float jump_g     = 1.5f;    // fast windowed-mean step that trips it
    float jump_line_g = 0.4f;   // max fit residual for "still a ramp"
    int   jump_n     = 4;       // samples per side of the jump test
    float rewind_s   = 0.1f;    // how far back the gate restores state
    float gate_min_s = 0.2f;    // the gate holds at least this long
    float calm_s     = 0.3f;    // trailing window for the un-gate line fit
    float calm_g     = 1.5f;    // max |residual| of that fit to un-gate
    float hold_s     = 1.0f;    // decay of the held flow to 0 while gated
    float step_g     = 5.0f;    // |level - predicted| bound for keeping f0
    float fit_slope_max_gps  = 35.0f;  // sanity bound on the resume flow
    float boost_snap_gps     = 15.0f;  // boost entry never snaps past this
    float slope_consist_gps  = 1.5f;   // full/half-window slope agreement
    float jump_cool_s = 0.4f;    // jump detector re-arms this long after un-gate
    float boost_win_s   = 0.12f;  // line-fit window on `fast`
    float boost_min_s   = 0.12f;  // same-sign innovation persistence
    float boost_inn_g   = 0.25f;  // |innovation| that builds the run
    float boost_resid_g = 0.5f;   // max |residual| of the boost fit
    float boost_quiet_gps = 0.4f; // |slope| under this reads as "flat"
    float boost_dslope_gps = 1.0f;// |fit slope - f_| that means "changed"
    float boost_settle_g = 0.2f;  // |inn| below this -> caught up
    float boost_drop_s   = 0.10f; // evidence lost this long -> base q
    float boost_hold_s   = 0.05f; // min boost duration
    float boost_suppress_s = 0.4f;// no re-boost this long after a lost-evidence drop
    float boost_flip_s   = 0.50f; // no boost this long after a slope-sign flip
    float boost_band_gps = 0.25f; // boosted flow stays within fit slope ± this
    float boost_steep_gps = 12.f; // beyond this |slope| evidence must persist
    float boost_steep_s   = 0.35f;// ...this long instead of boost_min_s
    float q_track      = 30.0f;   // post-settle tracking density on a slope
    float track_slope_gps = 0.8f; // clean |slope| that keeps tracking alive
    float track_hold_s = 0.3f;    // tracking persists this long after it
    float abort_inn_g  = 1.2f;    // boost abort: flat signal + this inn
    float abort_s      = 0.08f;   // ...sustained this long -> gate as a step
    float motion_veto_mg = 120.f; // accel |delta| EMA that blocks boost
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
        fb_.configure(cfg.fast_hz, cfg.sample_hz);
        reset();
    }

    /// Retune the follow speeds without touching the estimate — unlike
    /// configure() this keeps state, covariance and history.
    void set_tracking(float q_base, float q_boost) {
        cfg_.q       = q_base;
        cfg_.q_boost = q_boost;
    }

    /// Next push re-primes prefilters and state.
    void reset() {
        b1_.reset();
        b2_.reset();
        fb_.reset();
        primed_     = false;
        base_set_   = false;
        gated_      = false;
        boosting_   = false;
        run_sign_   = 0;
        run_t_      = 0.0f;
        run_slope_max_ = 0.0f;
        track_t_    = -1.0f;
        lost_t_     = 0.0f;
        abort_t_    = 0.0f;
        boost_t_    = 0.0f;
        suppress_t_ = -1.0f;
        sup_streak_ = 0.0f;
        ungate_t_   = -1.0f;
        flip_t_     = -1.0f;
        slope_sign_ = 0.0f;
        motion_ema_ = 0.0f;
        acc_primed_ = false;
        hist_.clear();
        buf_.clear();
        fbuf_.clear();
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
        float dt = primed_ ? t - tp_ : 0.0f;
        // Gap: prefilters are designed for sample_hz — re-prime them at
        // the new sample instead of letting the stall time-warp them.
        // The Kalman still sees the real dt, and a level jump across the
        // gap lands in the ordinary gate path.
        if (primed_ && dt > cfg_.gap_s) {
            b1_.reset();
            b2_.reset();
            fb_.reset();
            // Fresh filters make the stored evidence stale — drop the
            // boost/track state too so recovery re-earns its speed.
            boosting_      = false;
            track_t_       = -1.0f;
            run_t_         = 0.0f;
            run_slope_max_ = 0.0f;
        }
        const float pre  = b2_.push(b1_.push(g));
        const float fast = fb_.push(g);
        buf_.push({t, pre});
        fbuf_.push({t, fast});

        if (!primed_) {
            anchor(pre, 0.0f);
            tp_     = t;
            primed_ = true;
            hist_.push({t, w_, f_, p00_, p01_, p10_, p11_});
            return;
        }

        if (dt < 1e-4f) dt = 1e-4f;
        tp_ = t;
        predict(dt);

        last_inn_          = pre - w_;
        const float inn    = last_inn_;
        const float inn_f  = fast - w_;
        last_fast_         = fast;
        const bool  trip   = std::fabs(inn) > cfg_.gate_g ||
                             std::fabs(inn_f) > cfg_.gate_fast_g ||
                             jump_detected();
        if (!gated_ && trip) {
            trip_gate(t, cfg_.rewind_s);
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
            const float q00 = p00_, q01 = p01_;
            p00_ -= k0 * q00;
            p01_ -= k0 * q01;
            p10_ -= k1 * q00;
            p11_ -= k1 * q01;
            boost_update(dt, inn);
        }

        hist_.push({t, w_, f_, p00_, p01_, p10_, p11_});
        rebase(t);
    }

    /// Feed one accelerometer sample (mg) — feeds only the motion veto:
    /// handling suppresses a false boost, it can never create flow.
    void feed_accel(float x_mg, float y_mg, float z_mg) {
        if (acc_primed_) {
            const float d = std::fabs(x_mg - ax_) + std::fabs(y_mg - ay_) +
                            std::fabs(z_mg - az_);
            motion_ema_ += 0.2f * (d - motion_ema_);
        }
        ax_ = x_mg;
        ay_ = y_mg;
        az_ = z_mg;
        acc_primed_ = true;
    }

    /// Output flow: the tracked estimate, or while gated the captured
    /// flow decaying linearly to 0 over hold_s.
    [[nodiscard]] float rate() const {
        if (!primed_) return 0.0f;
        if (gated_) {
            const float d = std::clamp(
                1.0f - (tp_ - gate_t_) / cfg_.hold_s, 0.0f, 1.0f);
            return held_f_ * d;
        }
        return f_;
    }

    /// Kalman weight state — a lag-free estimate of the current level.
    [[nodiscard]] float weight() const { return primed_ ? w_ : 0.0f; }
    /// Impact gate engaged.
    [[nodiscard]] bool disturbed() const { return gated_; }
    /// Tracking a confirmed flow change at q_boost.
    [[nodiscard]] bool boosting() const { return boosting_; }
    /// Latest measurement innovation (g) — diagnostic aid.
    [[nodiscard]] float innovation() const { return last_inn_; }
    /// Latest `fast` channel level and windowed-mean jump — diagnostic.
    [[nodiscard]] float fast_level() const { return last_fast_; }
    [[nodiscard]] float jump_g_diff() const { return last_jump_; }
    /// Accel motion EMA (mg) — diagnostic aid.
    [[nodiscard]] float motion_mg() const { return motion_ema_; }
    /// False until the first push primes the state.
    [[nodiscard]] bool primed() const { return primed_; }

private:
    struct hist_pt {   // history entry for the rewind
        float t, w, f, p00, p01, p10, p11;
    };
    struct buf_pt {    // trailing samples for the line fits
        float t, v;
    };
    struct fit_res {
        bool  ok = false;
        float slope = 0.0f, level = 0.0f, worst = 0.0f;
        int   n = 0;
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

    /// x = F x; P = F P F^T + Q, F = [[1,dt],[0,1]]; Q density follows
    /// the boost state so a confirmed change is tracked hard while the
    /// resting estimate stays quiet.
    void predict(float dt) {
        w_ += dt * f_;
        // gated -> stay conservative; boosting -> q_boost; a clean steep
        // window keeps mid-level q_track briefly after the boost drops.
        const float q =
            gated_    ? cfg_.q
            : boosting_ ? cfg_.q_boost
            : (track_t_ >= 0.0f && tp_ - track_t_ <= cfg_.track_hold_s
                   ? cfg_.q_track
                   : cfg_.q);
        p00_ += dt * (p01_ + p10_) + dt * dt * p11_ + q * dt * dt * dt / 3.0f;
        const float n01 = p01_ + dt * p11_ + q * dt * dt / 2.0f;
        p01_ = p10_ = n01;
        p11_ += q * dt;
    }

    /// Gate entry — shared by the innovation/jump trips and the boost
    /// abort. Rewind to the newest history state at least age_s old and
    /// predict it forward to now (drops the onset sample). The abort
    /// path rewinds past the boost start so the held flow isn't the
    /// false slope it was just chasing.
    void trip_gate(float t, float age_s) {
        gated_    = true;
        boosting_ = false;
        track_t_  = -1.0f;
        for (std::size_t i = hist_.size(); i-- > 0;) {
            const auto& h = hist_.at(i);
            if (t - h.t < age_s) continue;
            const float d = t - h.t;
            restore(h);
            predict(d);
            break;
        }
        held_f_ = f_;
        gate_t_ = t;
        gate_w_ = w_;
    }

    /// Least-squares line fit of the ring samples in
    /// [newest.t - off_s - span_s, newest.t - off_s]. ok requires min_n
    /// samples and min_span of coverage; level is evaluated at the
    /// window's newest edge.
    template <class Ring>
    [[nodiscard]] fit_res line_fit(const Ring& r, float span_s,
                                   int min_n, float min_span,
                                   float off_s = 0.0f) const {
        fit_res f;
        if (r.size() == 0) return f;
        const buf_pt newest = r.at(r.size() - 1);
        const float edge = newest.t - off_s;
        float st = 0.0f, sy = 0.0f, stt = 0.0f, sty = 0.0f;
        int   n = 0;
        float t_first = 0.0f;
        for (std::size_t i = r.size(); i-- > 0;) {
            const buf_pt b = r.at(i);
            if (newest.t - b.t < off_s) continue;
            if (newest.t - b.t > off_s + span_s) break;
            if (n == 0 || b.t < t_first) t_first = b.t;
            const float x = b.t - edge;
            st += x;
            sy += b.v;
            stt += x * x;
            sty += x * b.v;
            ++n;
        }
        if (n < min_n || edge - t_first < min_span) return f;
        const float nf  = static_cast<float>(n);
        const float den = nf * stt - st * st;
        if (den <= 0.0f) return f;
        f.slope = (nf * sty - st * sy) / den;
        f.level = (sy - f.slope * st) / nf;    // intercept at the edge
        for (std::size_t i = r.size(); i-- > 0;) {
            const buf_pt b = r.at(i);
            if (newest.t - b.t < off_s) continue;
            if (newest.t - b.t > off_s + span_s) break;
            const float x   = b.t - edge;
            const float res = std::fabs(b.v - (f.level + f.slope * x));
            if (res > f.worst) f.worst = res;
        }
        f.ok = true;
        f.n  = n;
        return f;
    }

    /// Windowed-mean step test on `fast`: the mean of the last jump_n
    /// samples minus the mean of the previous jump_n. A mass step moves
    /// the whole window; pours (<~30 g/s) never reach jump_g. Disarmed
    /// briefly after an un-gate so the recovered ramp can't re-trip it.
    [[nodiscard]] bool jump_detected() {
        const std::size_t n = static_cast<std::size_t>(cfg_.jump_n);
        if (n == 0 || fbuf_.size() < 2 * n) {
            last_jump_ = 0.0f;
            return false;
        }
        float a = 0.0f, b = 0.0f;
        const std::size_t sz = fbuf_.size();
        for (std::size_t i = 0; i < n; ++i) {
            a += fbuf_.at(sz - 1 - i).v;
            b += fbuf_.at(sz - 1 - n - i).v;
        }
        last_jump_ = std::fabs(a - b) / static_cast<float>(n);
        if (last_jump_ <= cfg_.jump_g) return false;
        // A sustained steep pour is still a clean line across the whole
        // window; a step has a kink inside it. Only the kink trips.
        const fit_res f =
            line_fit(fbuf_, 2.0f * n / cfg_.sample_hz, 8,
                     1.5f * n / cfg_.sample_hz);
        if (f.ok && f.worst <= cfg_.jump_line_g) return false;
        if (ungate_t_ >= 0.0f && tp_ - ungate_t_ < cfg_.jump_cool_s)
            return false;
        return true;
    }

    /// Boost bookkeeping. The flow itself changed when the innovation
    /// holds one sign for boost_min_s AND the `fast` window pair shows a
    /// plausible shape — a sustained slope (windows agree), an onset
    /// (previous window flat), or an accel/decel (same sign, magnitude
    /// moved) — AND the fitted slope disagrees with f_ on the
    /// innovation's side. On entry f_ snaps to that slope: the low-q
    /// state was the lag, not the pour. Boost ends when the innovation
    /// settles; on stale evidence it drops and stays suppressed briefly
    /// so handling wiggles can't re-latch every half cycle. If the
    /// signal goes flat while the state still trails it, the change was
    /// a gently-placed mass — handed to the gate instead.
    void boost_update(float dt, float inn) {
        const float s = inn > 0.0f ? 1.0f : (inn < 0.0f ? -1.0f : 0.0f);
        if (std::fabs(inn) > cfg_.boost_inn_g && s == run_sign_) {
            run_t_ += dt;
        } else if (std::fabs(inn) > cfg_.boost_inn_g) {
            run_sign_ = s;
            run_t_    = dt;
        } else {
            run_t_ = 0.0f;
        }

        const fit_res now =
            line_fit(fbuf_, cfg_.boost_win_s, 6, cfg_.boost_win_s * 0.8f);
        const fit_res prev =
            line_fit(fbuf_, cfg_.boost_win_s, 6, cfg_.boost_win_s * 0.8f,
                     cfg_.boost_win_s);
        const bool clean  = now.ok && now.worst <= cfg_.boost_resid_g;
        const bool pclean = prev.ok && prev.worst <= cfg_.boost_resid_g;
        // Slope-sign flips (with a quiet hysteresis so noise can't flip)
        // mark oscillation: handling wiggles flip every half cycle and
        // spend the flip guard suppressed, while a real pour transition
        // never flips.
        if (clean && std::fabs(now.slope) >= cfg_.boost_quiet_gps) {
            const float ss = now.slope > 0.0f ? 1.0f : -1.0f;
            if (slope_sign_ != 0.0f && ss != slope_sign_) flip_t_ = tp_;
            slope_sign_ = ss;
        }
        bool shape = false;
        if (clean) {
            const float sn = std::fabs(now.slope), sp = std::fabs(prev.slope);
            if (sn < cfg_.boost_quiet_gps) {
                shape = true;   // signal went flat: a stop, given f_ high
            } else if (pclean) {
                shape = std::fabs(now.slope - prev.slope) <=
                            cfg_.slope_consist_gps ||  // sustained
                        sp < cfg_.boost_quiet_gps ||   // onset from flat
                        now.slope * prev.slope > 0.0f; // same-sign change
            } else {
                // Previous window mid-transition: trust a clean,
                // clearly-sloped current window on its own.
                shape = sn >= cfg_.boost_dslope_gps;
            }
        }
        const float dsl = shape ? now.slope - f_ : 0.0f;
        // Lost-evidence drops lengthen the hold-off each time (oscillating
        // handling keeps failing the evidence test), a clean settle resets it.
        const float sup_hold = cfg_.boost_suppress_s * (1.0f + sup_streak_);
        // Steep claims need longer proof at entry: real pours sustain a
        // slope, wiggles reverse it within a half cycle. Judged on the
        // steepest clean slope across the run, not the current sample.
        if (run_t_ > 0.0f) {
            if (clean) {
                run_slope_max_ =
                    std::max(run_slope_max_, std::fabs(now.slope));
            }
        } else {
            run_slope_max_ = clean ? std::fabs(now.slope) : 0.0f;
        }
        const float need_s = run_slope_max_ > cfg_.boost_steep_gps
                                 ? cfg_.boost_steep_s
                                 : cfg_.boost_min_s;
        const bool  ev = shape &&
                         std::fabs(dsl) >= cfg_.boost_dslope_gps &&
                         dsl * run_sign_ > 0.0f &&
                         motion_ema_ <= cfg_.motion_veto_mg &&
                         tp_ - suppress_t_ >= sup_hold &&
                         tp_ - flip_t_ >= cfg_.boost_flip_s;
        // A clean steep window also keeps a mid-level tracking density
        // for a while after a boost settles — without it a long pour
        // pumps in and out of boost. Entry requires the boost to have
        // confirmed the slope first (or an already-live track): a tap
        // or a placed cup shows a clean slope too, but never earned it.
        if (clean && std::fabs(now.slope) > cfg_.track_slope_gps &&
            tp_ - flip_t_ >= cfg_.boost_flip_s &&
            (boosting_ || (track_t_ >= 0.0f &&
                           tp_ - track_t_ <= cfg_.track_hold_s))) {
            track_t_ = tp_;
        }

        if (!boosting_) {
            if (!(ev && run_t_ >= need_s)) return;
            boosting_ = true;
            boost_t_  = tp_;
            lost_t_   = 0.0f;
            abort_t_  = 0.0f;
            // Fresh boost evidence may be a handling slam — cap the
            // snap; the un-gate resume path (already vetted by fit
            // quality) is where a legit steep pour re-acquires.
            f_ = std::clamp(now.slope, -cfg_.boost_snap_gps,
                            cfg_.boost_snap_gps);
            return;
        }
        if (std::fabs(inn) <= cfg_.boost_settle_g) {
            boosting_  = false;
            sup_streak_ = 0.0f;
            return;
        }
        // While boosted the Kalman gain can grow the flow past the
        // measured slope on the lagging innovation — cap it on the pour
        // side only. The state still has to climb to the slope itself,
        // so a handling slam (brief steep ramp) only ever shows the
        // bounded snap value while a real pour tracks normally.
        if (clean) {
            if (now.slope >= 0.0f)
                f_ = std::min(f_, now.slope + cfg_.boost_band_gps);
            else
                f_ = std::max(f_, now.slope - cfg_.boost_band_gps);
        }
        // Abort: the signal already sits flat while the estimate still
        // trails it — a mass placed gently, not a pour. Rewind to just
        // before the boost started so the held flow isn't the false
        // slope it was chasing, then the gate resolves the level.
        if (clean && std::fabs(now.slope) < cfg_.boost_quiet_gps &&
            std::fabs(inn) > cfg_.abort_inn_g) {
            abort_t_ += dt;
            if (abort_t_ >= cfg_.abort_s) {
                trip_gate(tp_, tp_ - boost_t_ + 0.05f);
                return;
            }
        } else {
            abort_t_ = 0.0f;
        }
        lost_t_ = ev ? 0.0f : lost_t_ + dt;
        if (lost_t_ > cfg_.boost_drop_s &&
            tp_ - boost_t_ > cfg_.boost_hold_s) {
            boosting_   = false;
            suppress_t_ = tp_;
            sup_streak_ = std::min(sup_streak_ + 1.0f, 6.0f);
        }
    }

    /// Ungate once the gate has held gate_min_s and the trailing calm_s
    /// line fit of `pre` is calm — including its newest half, so a step
    /// still walking through the window can't release early. The resume
    /// flow is the fitted slope when the full window and its newer half
    /// agree — pouring through an impact keeps pouring, a stopped pour
    /// re-anchors at ~0 instead of being held for hold_s. Otherwise fall
    /// back to the held flow if it still predicts the level, else 0.
    void ungate_check(float t) {
        if (t - gate_t_ < cfg_.gate_min_s) return;
        const fit_res f =
            line_fit(buf_, cfg_.calm_s, 6, cfg_.calm_s * 0.9f);
        if (!f.ok || f.worst >= cfg_.calm_g) return;
        const fit_res h =
            line_fit(buf_, cfg_.calm_s * 0.5f, 4, cfg_.calm_s * 0.45f);
        if (!h.ok || h.worst >= cfg_.calm_g * 0.7f) return;
        float f0;
        // The newest half-window is the current slope: pouring through
        // an impact keeps pouring (h agrees with the full fit), a flat
        // signal re-anchors at ~0 (a poured-out stop never resurrects
        // the held flow through a stale level check). A clean but
        // disagreed non-flat h is a mid-transition — held-or-0 legacy.
        if (std::fabs(h.slope) <= cfg_.fit_slope_max_gps &&
            (std::fabs(h.slope) < cfg_.boost_quiet_gps ||
             std::fabs(f.slope - h.slope) <= cfg_.slope_consist_gps)) {
            f0 = h.slope;
        } else {
            const float pred = gate_w_ + held_f_ * (t - gate_t_);
            f0 = std::fabs(f.level - pred) < cfg_.step_g ? held_f_ : 0.0f;
        }
        gated_      = false;
        boosting_   = false;
        run_sign_   = 0;
        run_t_      = 0.0f;
        run_slope_max_ = 0.0f;
        lost_t_     = 0.0f;
        abort_t_    = 0.0f;
        slope_sign_ = 0.0f;
        ungate_t_   = t;
        anchor(f.level, f0);
    }

    /// Rebase time when the offset grows past ~2 min so the float32 math
    /// stays precise: advance base_ by a whole number of milliseconds and
    /// subtract the same amount (in seconds) from every stored time —
    /// history and the fit buffers stay usable across the boundary.
    void rebase(float t) {
        if (t <= 120.0f) return;
        const auto  delta_ms = static_cast<std::int64_t>(t * 1000.0f);
        const float delta_s  = static_cast<float>(delta_ms) * 1e-3f;
        base_   += clock_ms{delta_ms};
        tp_     -= delta_s;
        gate_t_ -= delta_s;
        boost_t_ -= delta_s;
        if (suppress_t_ >= 0.0f) suppress_t_ -= delta_s;
        if (ungate_t_ >= 0.0f)   ungate_t_   -= delta_s;
        if (flip_t_ >= 0.0f)     flip_t_     -= delta_s;
        if (track_t_ >= 0.0f)    track_t_    -= delta_s;
        for (std::size_t i = 0; i < hist_.size(); ++i) {
            hist_.at(i).t -= delta_s;
        }
        for (std::size_t i = 0; i < buf_.size(); ++i) {
            buf_.at(i).t -= delta_s;
        }
        for (std::size_t i = 0; i < fbuf_.size(); ++i) {
            fbuf_.at(i).t -= delta_s;
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
    };

    flow_kf_config cfg_{};
    butter2_lpf    b1_, b2_;   // measurement prefilter (Kalman input)
    butter2_lpf    fb_;        // detection channel (gate/jump/boost fits)
    ring_hist      hist_{};
    ring_buf       buf_{};     // trailing `pre` samples
    ring_buf       fbuf_{};    // trailing `fast` samples
    clock_ms       base_{0};       // integer anchor — subtracted from now
    bool           base_set_ = false;
    float          tp_   = 0.0f;   // last processed (rebased) time, s
    float          w_ = 0.0f, f_ = 0.0f;
    float          p00_ = 0.0f, p01_ = 0.0f, p10_ = 0.0f, p11_ = 0.0f;
    bool           primed_  = false;
    bool           gated_   = false;
    bool           boosting_ = false;
    float          held_f_ = 0.0f;  // flow at the gate trip
    float          gate_t_ = 0.0f;  // gate start (rebased time)
    float          gate_w_ = 0.0f;  // weight state at gate start
    float          last_inn_ = 0.0f;
    float          last_fast_ = 0.0f;
    float          last_jump_ = 0.0f;
    // boost evidence
    float          run_sign_ = 0.0f;  // sign of the innovation run
    float          run_t_    = 0.0f;  // its duration so far
    float          run_slope_max_ = 0.0f; // steepest clean slope in run
    float          track_t_  = -1.0f; // last clean steep window (q_track)
    float          lost_t_   = 0.0f;  // evidence stale time while boosted
    float          abort_t_  = 0.0f;  // flat-signal+inn time while boosted
    float          boost_t_  = 0.0f;  // boost start (rebased time)
    float          suppress_t_ = -1.0f; // evidence-loss drop -> re-boost hold-off
    float          sup_streak_ = 0.0f;  // consecutive lost-evidence drops
    float          ungate_t_   = -1.0f; // last un-gate (jump cooldown)
    float          flip_t_     = -1.0f; // last slope-sign flip
    float          slope_sign_ = 0.0f;  // last significant fit-slope sign
    // accel motion veto
    float          motion_ema_ = 0.0f;
    float          ax_ = 0.0f, ay_ = 0.0f, az_ = 0.0f;
    bool           acc_primed_ = false;
};

} // namespace scale
