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
///   |inn| > gate_g (pre) or |inn_fast| > gate_fast_g, a windowed-mean
///   jump whose EXCESS over the window's fitted ramp exceeds jump_g
///   (a sustained pour is one line — its slope already predicts the
///   mean difference; a step has a kink it cannot), or a clean fitted
///   slope past gate_slope_gps that contradicts the state by more than
///   gate_diff_gps while un-boosted — blind tracking must not chase a
///   slope no pour can sustain, so the gate captures it early, and the
///   margin keeps fast-fit jitter from re-tripping a healthy pour ->
///   gate: rewind to the
///   newest history state at least rewind_s old, predict forward to
///   now, hold the captured flow decaying linearly to 0 over hold_s.
///   The gate holds at least gate_min_s, then un-gates once the
///   trailing calm_s fit of `pre` AND its newest half are both calm.
///   f0 is the half-window slope when it is flat (a stop re-anchors at
///   ~0), or when the WHOLE gate window reads as one clean ramp whose
///   slope agrees with the half-window and the oscillation streak is
///   quiet (a pour running through the gate keeps pouring) — else the
///   held flow if it still predicts the level within step_g, else 0.
///   A sustained wiggle keeps flipping the fitted slope's sign across
///   gates (a dying ring stops flipping, and a ~12 Hz ring flaps too
///   densely to count), so its resume lands on 0 instead of the
///   sine's instantaneous slope.
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
    float gate_slope_gps = 22.f;// |fitted slope| beyond this while
                                //   un-boosted trips the gate too —
                                //   blind tracking must not chase a
                                //   slope a pour can't sustain; a real
                                //   steep pour re-acquires via the
                                //   un-gate fit. The contradiction
                                //   margin below must clear the fast
                                //   fit's slope jitter (~3 under real
                                //   noise) or a healthy pour re-trips
                                //   every release.
    float gate_diff_gps  = 6.0f;
    int   gate_steep_n   = 3;   // consecutive steep samples to trip —
                              //   edge spikes last 1-2, real slopes and
                              //   sine upswings sustain 40 ms+
    float jump_g     = 1.5f;    // fast windowed-mean step EXCESS over
                                //   the window's fitted ramp that trips
                                //   it (a real step has a kink the
                                //   single slope can't explain)
    int   jump_n     = 4;       // samples per side of the jump test
    float rewind_s   = 0.1f;    // how far back the gate restores state
    float gate_min_s = 0.2f;    // the gate holds at least this long
    float calm_s     = 0.3f;    // trailing window for the un-gate line fit
    float calm_g     = 1.5f;    // max |residual| of that fit to un-gate
    float hold_s     = 1.0f;    // decay of the held flow to 0 while gated
    float step_g     = 5.0f;    // |level - predicted| bound for keeping f0
    float fit_slope_max_gps  = 35.0f;  // sanity bound on the resume flow
    float boost_snap_gps     = 15.0f;  // boost entry never snaps past this
    float boost_snap_damp    = 0.85f;  // entry snaps to slope*this — the
                                       // transition fit reads high
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
    float boost_band_gps = 0.15f; // boosted flow stays within the shallower
                                  //   of the two window slopes ± this
    float gate_flip_reset_s = 0.45f; // flips older than this no longer
                                  //   count as "still oscillating"
    float band_hold_s   = 0.35f; // the pour-side cap applies for this
                                  //   long after boost ends — it exists
                                  //   to stop boost-exit momentum, NOT
                                  //   to clamp a running pour (min() of
                                  //   two noisy fits is biased low and
                                  //   would under-report continuously)
    float flip_min_gap_s    = 0.08f; // flips denser than this are a ring
                                  //   (~12 Hz), not a wiggle — ignored
    int   resume_flip_max   = 1;  // any counted (>=80 ms-spaced) flip
                                  //   means the signal genuinely
                                  //   reversed recently -> resume 0
    float osc_slope_gps     = 5.0f; // a flip counts only past this
                                  //   |slope| — rest-noise slope fits
                                  //   jitter ~2 g/s, a real wiggle
                                  //   swings 20+
    float rebound_gps       = 0.25f;// while the fitted signal is flat a
                                  //   deeply negative reading is settle
                                  //   momentum, not weight leaving — and
                                  //   the pin stays inside the 0.3 g/s
                                  //   display deadband so a pinned dip
                                  //   still reads as stopped
    float stop_flat_gps     = 1.5f; // |stop-window slope| under this the
                                  //   snap candidate builds — shrink
                                  //   threshold on residual flow, NOT
                                  //   the zero test (that is zero_slope)
    float stop_s            = 0.06f;// flat persistence before the snap
    float stop_win_s        = 0.15f;// stop/zero fit window on `pre`
    float zero_slope_gps    = 0.20f;// |slope| under this reads "still"
    float zero_confirm_s    = 0.04f;// consecutive stillness -> zero hold
    float restart_s         = 0.06f;// consecutive restart evidence to
                                  //   release the zero hold
    float restart_slope_gps = 0.20f;// |slope| past this is the signal
                                  //   moving again
    float restart_growth_g = 0.50f;// net pre growth since zero latched;
                                  //   supports noisy positive restarts
    float restart_var_gps2 = 16.0f;// flow variance floor on growth release
    float stop_veto_gps     = 2.0f; // a live fast-channel slope past this
                                  //   blocks the snap (re-start)
    float gate_skip_s       = 0.12f;// leading slice of a gate excluded
                                  //   from the post-transient ramp fit —
                                  //   it contains the trip's own impact
    float gate_early_skip_s = 0.08f;// shorter exclusion only with full/half
                                  //   pre agreement and a clean fast tail
    float stop_guard_s      = 0.45f;// rebound pin stays armed this long
                                  //   after each stop snap — the settle
                                  //   dip lands while the calm fit still
                                  //   contains the ramp tail
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
        steep_run_n_   = 0;
        steep_n_       = 0;
        stop_t_        = 0.0f;
        snap_t_        = -1e9f;
        pour_hi_t_     = -1e9f;
        zero_held_     = false;
        zero_level_    = 0.0f;
        zero_t_        = 0.0f;
        restart_t_     = 0.0f;
        track_t_    = -1e9f;
        lost_t_     = 0.0f;
        abort_t_    = 0.0f;
        boost_t_    = 0.0f;
        boost_end_t_ = -1e9f;
        suppress_t_ = -1e9f;
        sup_streak_ = 0.0f;
        ungate_t_   = -1e9f;
        flip_t_     = -1e9f;
        slope_sign_ = 0.0f;
        flip_candidate_sign_ = 0.0f;
        flip_candidate_s_    = 0.0f;
        osc_flips_  = 0;
        osc_sign_   = 0.0f;
        osc_flip_t_ = -1e9f;
        trip_why_   = 0;
        ungate_f0_  = 0.0f;
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
        // Callers promise monotone time; clamp anyway — a negative dt
        // would reverse the prediction and poison the covariance.
        float dt = primed_ ? std::max(t - tp_, 0.0f) : 0.0f;
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
            track_t_       = -1e9f;
            run_t_         = 0.0f;
            run_slope_max_ = 0.0f;
            steep_run_n_   = 0;
            steep_n_       = 0;
            stop_t_        = 0.0f;
            snap_t_        = -1e9f;
            pour_hi_t_     = -1e9f;
            flip_candidate_sign_ = 0.0f;
            flip_candidate_s_    = 0.0f;
            zero_held_     = false;
            zero_level_    = 0.0f;
            zero_t_        = 0.0f;
            restart_t_     = 0.0f;
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
        int trip_why = 0;
        if (std::fabs(inn) > cfg_.gate_g)         trip_why = 1;
        else if (std::fabs(inn_f) > cfg_.gate_fast_g) trip_why = 2;
        else if (jump_detected())                    trip_why = 3;
        if (!gated_ && trip_why) {
            trip_why_ = trip_why;
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
    /// Why the current/last gate tripped: 1 pre innovation,
    /// 2 fast innovation, 3 jump, 4 boost abort — diagnostic aid.
    [[nodiscard]] int trip_reason() const { return trip_why_; }
    /// Flow adopted by the last un-gate — diagnostic aid.
    [[nodiscard]] float ungate_f0() const { return ungate_f0_; }
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
        // Elapsed math only: a track_t_ that crossed zero in a rebase is
        // still valid, and the "never" sentinel sits far in the past.
        const float q =
            gated_    ? cfg_.q
            : boosting_ ? cfg_.q_boost
            : (tp_ - track_t_ <= cfg_.track_hold_s
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
        track_t_  = -1e9f;
        flip_candidate_sign_ = 0.0f;
        flip_candidate_s_    = 0.0f;
        stop_t_    = 0.0f;
        snap_t_    = -1e9f;
        pour_hi_t_ = -1e9f;
        zero_held_ = false;
        zero_level_ = 0.0f;
        zero_t_    = 0.0f;
        restart_t_ = 0.0f;
        // osc_* is deliberately NOT reset: the count must span gates —
        // a sustained wiggle trips, ungates and re-trips each half
        // cycle, and per-gate counting never reaches the threshold.
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
        // window: its fitted slope already predicts the mean difference
        // between the halves, so the *excess* beyond that prediction is
        // the step signature. A real step has a kink — the single slope
        // can't explain it. This is noise-robust where a residual cap
        // on the raw samples is not.
        const fit_res f =
            line_fit(fbuf_, 2.0f * n / cfg_.sample_hz, 8,
                     1.5f * n / cfg_.sample_hz);
        const float ramp_diff =
            f.ok ? std::fabs(f.slope) * n / cfg_.sample_hz : 0.0f;
        if (last_jump_ - ramp_diff <= cfg_.jump_g) return false;
        if (tp_ - ungate_t_ < cfg_.jump_cool_s)
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
    ///
    /// Oscillation bookkeeping, shared by the gated and un-gated paths:
    /// count fitted-slope sign flips with a hysteresis. A sustained
    /// wiggle flips every half cycle and keeps the streak alive; a dying
    /// impact ring flaps far denser than flip_min_gap_s and is ignored;
    /// a real pour transition never flips. The streak decays on elapsed
    /// time FIRST — quiet or same-direction input produces no flips, so
    /// without the time-based expiry a stale streak would sit forever
    /// and block the next pour's resume.
    void note_osc(float slope, float t) {
        if (t - osc_flip_t_ > cfg_.gate_flip_reset_s) osc_flips_ = 0;
        // Only a coherent reversal counts — rest noise flips sign
        // constantly at tiny magnitude; a real wiggle swings hard.
        if (std::fabs(slope) < cfg_.osc_slope_gps) return;
        const float ss = slope > 0.0f ? 1.0f : -1.0f;
        if (osc_sign_ != 0.0f && ss != osc_sign_) {
            const float gapf = t - osc_flip_t_;
            if (gapf >= cfg_.flip_min_gap_s) {
                osc_flips_ = gapf <= cfg_.gate_flip_reset_s
                                 ? osc_flips_ + 1
                                 : 1;
                osc_flip_t_ = t;
            }
        }
        osc_sign_ = ss;
    }

    /// Stop machinery, once per un-gated sample before the flip/boost
    /// bookkeeping; returns true while the zero hold owns the estimate.
    /// Candidate: a clean flat stop-window `pre` fit saturates stop_t_
    /// (leaks when invalid) — the snap is residual control only: it
    /// requires the candidate live, acts only on |f_| above
    /// stop_flat_gps, shrinks toward the fit floored at rebound_gps,
    /// and re-anchors w_. A tightly fitted longer positive trend vetoes
    /// the snap while fast is flat/recovering, so a pour's ripple cannot
    /// masquerade as a stop. Held: stillness on both channels, or a quiet
    /// calm-window pre fit with low residual flow just after a high
    /// pour, for zero_confirm_s latches f_ = 0 exactly with w_ riding
    /// the fitted level. Restart: a clean same-sign slope on both
    /// channels sustained restart_s clears the timers and releases the
    /// hold at f_ = 0 — genuine motion, either sign, earns tracking
    /// through the normal boost/gate paths rather than an adopted slope.
    /// The trend is certified beyond the newest window: the adjacent
    /// `fast` window agrees in direction, or it still reads flat (a real
    /// onset) while the smooth calm-window fit carries the same signed
    /// slope. While held, a second low-rate proof covers drifts the
    /// noisy fast fit can't certify: two adjacent calm-window `pre`
    /// fits agreeing on a signed pour-band slope. Positive restarts
    /// can also prove net growth from the level saved at zero latch,
    /// with a clean calm-window trend for the same restart duration.
    /// Growth releases widen flow uncertainty without adopting a rate
    /// or granting tracking density. Only a release relying on this
    /// fallback re-anchors weight to the latest observation; retaining
    /// the original weight on a clean restart preserves its boost proof.
    bool stop_update(float dt, float inn, const fit_res& now,
                     const fit_res& prev, const fit_res& short_pre,
                     const fit_res& pre_calm) {
        const bool pclean = prev.ok && prev.worst <= cfg_.boost_resid_g;
        const bool trend  =
            (pclean && now.slope * prev.slope > 0.0f &&
             std::fabs(prev.slope) > cfg_.restart_slope_gps) ||
            (pclean && std::fabs(prev.slope) < cfg_.boost_quiet_gps &&
             pre_calm.ok && pre_calm.worst <= cfg_.boost_resid_g &&
             now.slope * pre_calm.slope > 0.0f &&
             std::fabs(pre_calm.slope) > cfg_.restart_slope_gps);
        bool restart_ev =
            now.ok && now.worst <= cfg_.boost_resid_g &&
            short_pre.ok && short_pre.worst <= cfg_.boost_resid_g &&
            now.slope * short_pre.slope > 0.0f &&
            std::fabs(now.slope) > cfg_.restart_slope_gps &&
            std::fabs(short_pre.slope) > cfg_.restart_slope_gps &&
            trend;
        if (zero_held_ && !restart_ev) {
            const fit_res older_pre =
                line_fit(buf_, cfg_.calm_s, 8, cfg_.calm_s * 0.85f,
                         cfg_.calm_s);
            restart_ev =
                pre_calm.ok && older_pre.ok &&
                pre_calm.worst <= cfg_.boost_resid_g &&
                older_pre.worst <= cfg_.boost_resid_g &&
                pre_calm.slope * older_pre.slope > 0.0f &&
                std::fabs(pre_calm.slope) > cfg_.restart_slope_gps &&
                std::fabs(older_pre.slope) > cfg_.restart_slope_gps &&
                std::fabs(pre_calm.slope) <= cfg_.stop_flat_gps &&
                std::fabs(older_pre.slope) <= cfg_.stop_flat_gps &&
                std::fabs(pre_calm.slope - older_pre.slope) <=
                    cfg_.boost_band_gps;
        }
        const bool clean_restart_ev = restart_ev;
        const bool growth_ev = zero_held_ &&
            pre_calm.ok && pre_calm.worst <= cfg_.calm_g &&
            pre_calm.slope > cfg_.boost_quiet_gps &&
            pre_calm.level - zero_level_ >= cfg_.restart_growth_g;
        restart_ev = restart_ev || growth_ev;
        restart_t_ = restart_ev
                         ? std::min(cfg_.restart_s, restart_t_ + dt)
                         : 0.0f;
        if (restart_t_ >= cfg_.restart_s) {
            stop_t_ = 0.0f;
            zero_t_ = 0.0f;
            if (zero_held_) {
                zero_held_ = false;
                f_             = 0.0f;
                boosting_      = false;
                run_t_         = 0.0f;
                run_slope_max_ = 0.0f;
                steep_run_n_   = 0;
                track_t_       = -1e9f;
                if (growth_ev) {
                    if (!clean_restart_ev)
                        w_ = buf_.at(buf_.size() - 1).v;
                    p11_ = std::max(p11_, cfg_.restart_var_gps2);
                }
            }
            return false;
        }
        const bool quiet =
            short_pre.ok && short_pre.worst <= cfg_.boost_resid_g &&
            std::fabs(short_pre.slope) <= cfg_.zero_slope_gps &&
            now.ok && now.worst <= cfg_.boost_resid_g &&
            std::fabs(now.slope) <= cfg_.boost_quiet_gps;
        // The short windows can keep alternating through a settling
        // ring after the estimate already reaches zero. A longer quiet
        // pre fit can certify that stop, but only just after a high
        // pour, on the ring's falling side, while the state is already
        // low. Restart evidence vetoes it; a same-sign slow tail cannot
        // latch merely because it sits below the quiet threshold.
        const bool quiet_after_pour =
            tp_ - pour_hi_t_ <= cfg_.stop_guard_s &&
            pre_calm.ok && pre_calm.worst <= cfg_.boost_resid_g &&
            std::fabs(pre_calm.slope) <= cfg_.boost_quiet_gps &&
            short_pre.ok && short_pre.slope < -cfg_.zero_slope_gps &&
            f_ <= cfg_.boost_quiet_gps && f_ >= -cfg_.stop_flat_gps;
        const bool zero_cand = !restart_ev && (quiet || quiet_after_pour);
        zero_t_ = zero_cand
                      ? std::min(cfg_.zero_confirm_s, zero_t_ + dt)
                      : 0.0f;
        if (zero_t_ >= cfg_.zero_confirm_s && !zero_held_) {
            zero_held_ = true;
            // Weight, not time: keep this anchor fixed while held and
            // leave it untouched when timestamps rebase.
            zero_level_ = pre_calm.ok ? pre_calm.level : short_pre.level;
        }
        if (zero_held_) {
            f_ = 0.0f;
            if (short_pre.ok) w_ = short_pre.level;
            boosting_      = false;
            track_t_       = -1e9f;
            run_t_         = 0.0f;
            run_slope_max_ = 0.0f;
            steep_run_n_   = 0;
            // The early return below bypasses the un-boosted steep-slope
            // gate — keep it armed while held, with the same
            // contradiction test. A clean sustained slope releases via
            // the restart path first, so this only fires on a dirty
            // fast fit — a step/slam, not stillness.
            const bool steep =
                now.ok &&
                std::fabs(now.slope) > cfg_.gate_slope_gps &&
                std::fabs(now.slope - f_) > cfg_.gate_diff_gps;
            steep_n_ = steep ? steep_n_ + 1 : 0;
            if (steep_n_ >= cfg_.gate_steep_n) {
                steep_n_  = 0;
                trip_why_ = 5;
                trip_gate(tp_, cfg_.rewind_s);
            }
            return true;
        }
        const bool cand =
            short_pre.ok && short_pre.worst <= cfg_.boost_resid_g &&
            std::fabs(short_pre.slope) < cfg_.stop_flat_gps;
        stop_t_ = cand ? std::min(stop_t_ + dt, cfg_.stop_s)
                       : std::max(0.0f, stop_t_ - dt);
        const float tgt = short_pre.ok ? short_pre.slope : 0.0f;
        const bool pushing = std::fabs(inn) > cfg_.boost_inn_g &&
                             inn * (f_ - tgt) > 0.0f;
        // Short flat fits also occur in a continuous pour's weight
        // ripple. A tightly fitted longer positive trend contradicts
        // that stop while the fast channel is flat or recovering.
        // Require twice the usual fit precision for this veto; a noisy
        // ramp tail or a falling fast fit must still allow a real stop.
        const bool continuing_pour =
            pre_calm.ok && pre_calm.worst <= cfg_.boost_resid_g * 0.5f &&
            f_ > 0.0f && pre_calm.slope > cfg_.boost_quiet_gps &&
            (!now.ok || now.slope >= -cfg_.boost_quiet_gps);
        // Shrink-only is not enough on its own: under pour-rate noise the
        // fit reads low often enough to ratchet a genuine marginal pour
        // down. Snap only trims leftover momentum above stop_flat_gps;
        // low residual is the zero hold's job.
        if (cand && stop_t_ >= cfg_.stop_s &&
            (!now.ok || std::fabs(now.slope) < cfg_.stop_veto_gps) &&
            !pushing && !continuing_pour &&
            std::fabs(f_) > cfg_.stop_flat_gps &&
            std::fabs(tgt) < std::fabs(f_)) {
            f_ = std::max(tgt, -cfg_.rebound_gps);
            snap_t_ = tp_;
            w_ = short_pre.level;
        }
        return false;
    }

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
        // Same windows on the smooth pre channel: pre_short certifies a
        // direction flip (the fast fit alone flips on noise and onset
        // ringing), pre_calm certifies real growth for the cap below.
        const fit_res pre_short =
            line_fit(buf_, cfg_.boost_win_s, 6, cfg_.boost_win_s * 0.8f);
        const fit_res pre_calm =
            line_fit(buf_, cfg_.calm_s, 8, cfg_.calm_s * 0.85f);
        const bool clean  = now.ok && now.worst <= cfg_.boost_resid_g;
        const bool pclean = prev.ok && prev.worst <= cfg_.boost_resid_g;
        // Stop machinery's own window on `pre` (stop_win_s, not the
        // boost window) — runs before the flip/boost bookkeeping so a
        // held zero can't leak a rate through the band path below.
        const fit_res short_pre =
            line_fit(buf_, cfg_.stop_win_s, 8, cfg_.stop_win_s * 0.85f);
        if (stop_update(dt, inn, now, prev, short_pre, pre_calm))
            return;
        // Slope-sign flips mark oscillation — but only a CREDIBLE
        // direction may move slope_sign_/flip_t_: clean fast fit AND a
        // clean same-sign pre_short, both past their noise floors. A new
        // sign must persist 0.06 s consecutively before it confirms; a
        // sample that can't certify the candidate drops it rather than
        // touching the last credible sign — onset splash/ring noise then
        // never trips the boost_flip_s guard, while a genuine reversal
        // (a sustained wiggle) still flips every half cycle.
        const bool credible =
            clean && std::fabs(now.slope) >= cfg_.osc_slope_gps &&
            pre_short.ok && pre_short.worst <= cfg_.boost_resid_g &&
            now.slope * pre_short.slope > 0.0f &&
            std::fabs(pre_short.slope) >= cfg_.boost_dslope_gps;
        const float csign =
            credible ? (now.slope > 0.0f ? 1.0f : -1.0f) : 0.0f;
        if (csign != 0.0f && csign == flip_candidate_sign_) {
            flip_candidate_s_ =
                std::min(0.06f, flip_candidate_s_ + dt);
        } else {
            flip_candidate_sign_ = csign;
            flip_candidate_s_    = csign != 0.0f ? dt : 0.0f;
        }
        if (csign != 0.0f && flip_candidate_s_ >= 0.06f) {
            if (slope_sign_ != 0.0f && csign != slope_sign_) {
                flip_t_ = tp_;
            }
            slope_sign_ = csign;
        }
        if (clean && std::fabs(now.slope) >= cfg_.boost_quiet_gps) {
            note_osc(now.slope, tp_);
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
        // Pour-side bound on the fitted slope, two directions with
        // different lifetimes:
        //  - the CEILING (positive slope) exists only to stop boost/settle
        //    momentum overshoot and runs for band_hold_s after boost —
        //    applied chronically it biases a noisy running pour low
        //    (min() of two noisy fits is biased; clipping is instant,
        //    recovery is slow), so it MUST be a transition-only tool.
        //  - the FLOOR (negative slope) only stops the readout dipping
        //    below the measured slope — it can never under-report a real
        //    pour, so it stays armed whenever the fit is sane.
        // The FLOOR still uses the shallower of the two adjacent fast
        // windows; the CEILING rides max(that ref, the calm pre slope)
        // so a fast-fit valley can't cap below measured growth — either
        // way an onset can't extrapolate its steepest instant into a
        // spike.
        const auto apply_band = [&](bool pos_cap) {
            // Flat-signal rebound guard: a deeply negative f_ while the
            // signal reads flat (PRE channel), while the stop candidate
            // is warm, or within a short window after a snap/pour-end is
            // settle momentum, not weight leaving — pin it. The fast fit
            // spikes past quiet exactly when the dip develops, so the
            // guard keys on pre_calm. The pour_hi_t_ term covers the
            // settle transient itself: a boosted q can drag f_ below
            // zero in a single step right after the pour stops — a
            // sustained removal releases the guard through the
            // zero-hold restart path instead. (The residual snap moved
            // to stop_update; the hold path never reaches this lambda.)
            const bool flat_calm =
                pre_calm.ok &&
                std::fabs(pre_calm.slope) < cfg_.stop_flat_gps;
            if (f_ > 1.5f) pour_hi_t_ = tp_;
            if ((flat_calm || stop_t_ > 0.0f ||
                 tp_ - snap_t_ <= cfg_.stop_guard_s ||
                 tp_ - pour_hi_t_ <= cfg_.stop_guard_s) &&
                f_ < -cfg_.rebound_gps) {
                f_ = -cfg_.rebound_gps;
            }
            if (!now.ok ||
                std::fabs(now.slope) > cfg_.fit_slope_max_gps)
                return;
            float ref = now.slope;
            if (prev.ok &&
                (now.slope >= 0.0f) == (prev.slope >= 0.0f)) {
                ref = now.slope >= 0.0f
                          ? std::min(now.slope, prev.slope)
                          : std::max(now.slope, prev.slope);
            }
            if (now.slope >= 0.0f) {
                // Positive ceiling only while the smooth channel
                // certifies real growth: a fast-fit valley must not cap
                // f_ below the measured ramp, so the bound rides the
                // HIGHER of the fast ref and the calm pre slope. An
                // invalid pre reference means no cap at all — never a
                // fallback to the noisy min alone.
                if (pos_cap && clean && pre_calm.ok &&
                    pre_calm.worst <= cfg_.boost_resid_g &&
                    pre_calm.slope >= 0.0f) {
                    f_ = std::min(f_, std::max(ref, pre_calm.slope) +
                                      cfg_.boost_band_gps);
                }
            } else {
                f_ = std::max(f_, ref - cfg_.boost_band_gps);
            }
        };

        const float dsl = shape ? now.slope - f_ : 0.0f;
        // Lost-evidence drops lengthen the hold-off each time (oscillating
        // handling keeps failing the evidence test), a clean settle resets it.
        const float sup_hold = cfg_.boost_suppress_s * (1.0f + sup_streak_);
        // Steep claims need longer proof at entry: real pours sustain a
        // slope, wiggles reverse it within a half cycle. "Steep" must
        // itself persist a few samples — a single noisy fit spike must
        // not reclassify an ordinary run as steep and force it through
        // the 0.35 s proof (a slam still reads steep for ~0.15 s).
        if (run_t_ > 0.0f) {
            if (clean) {
                steep_run_n_ =
                    std::fabs(now.slope) > cfg_.boost_steep_gps
                        ? steep_run_n_ + 1
                        : 0;
                if (steep_run_n_ >= cfg_.gate_steep_n) {
                    run_slope_max_ =
                        std::max(run_slope_max_, std::fabs(now.slope));
                }
            }
        } else {
            run_slope_max_ = 0.0f;
            steep_run_n_   = 0;
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
            (boosting_ || tp_ - track_t_ <= cfg_.track_hold_s)) {
            track_t_ = tp_;
        }

        if (!boosting_) {
            if (!(ev && run_t_ >= need_s)) {
                // A slope past gate_slope_gps that contradicts the state
                // is handed to the gate — never chased blindly. A real
                // pour this steep comes back through the un-gate fit;
                // a wobble upswing is captured early and held instead
                // of tracked to the sine's peak rate. The claim must
                // persist ~40 ms: a pour-resume edge or splash spike
                // only exceeds the bound for a sample or two, while a
                // genuinely steep signal sustains it.
                const bool steep = now.ok &&
                    std::fabs(now.slope) > cfg_.gate_slope_gps &&
                    std::fabs(now.slope - f_) > cfg_.gate_diff_gps;
                steep_n_ = steep ? steep_n_ + 1 : 0;
                if (steep_n_ >= cfg_.gate_steep_n) {
                    steep_n_  = 0;
                    trip_why_ = 5;
                    trip_gate(tp_, cfg_.rewind_s);
                    return;
                }
                // Ceiling only inside the boost-exit momentum window;
                // the negative floor stays armed — it can only stop a
                // spurious rebound, never under-report a pour.
                apply_band(tp_ - boost_end_t_ <= cfg_.band_hold_s);
                return;
            }
            boosting_ = true;
            boost_t_  = tp_;
            lost_t_   = 0.0f;
            abort_t_  = 0.0f;
            // Fresh boost evidence may be a handling slam — cap the
            // snap; the un-gate resume path (already vetted by fit
            // quality) is where a legit steep pour re-acquires. The fit
            // reads high through the onset transition (filter lag still
            // climbing into the window), so snap slightly damped.
            f_ = std::clamp(now.slope * cfg_.boost_snap_damp,
                            -cfg_.boost_snap_gps, cfg_.boost_snap_gps);
            // The entry snap bypassed the band before: a negative slope
            // (post-stop slosh) would land below the rebound floor for
            // one sample before the pin armed next sample. Run the
            // floor/guard now — no positive cap on the snap itself.
            apply_band(false);
            return;
        }
        if (std::fabs(inn) <= cfg_.boost_settle_g) {
            boosting_   = false;
            boost_end_t_ = tp_;
            sup_streak_ = 0.0f;
            apply_band(true);
            return;
        }
        // While boosted the state still has to climb to the slope
        // itself, so a handling slam (brief steep ramp) only ever shows
        // the bounded snap value while a real pour tracks normally.
        apply_band(true);
        // Abort: the signal already sits flat while the estimate still
        // trails it — a mass placed gently, not a pour. Rewind to just
        // before the boost started so the held flow isn't the false
        // slope it was chasing, then the gate resolves the level.
        if (clean && std::fabs(now.slope) < cfg_.boost_quiet_gps &&
            std::fabs(inn) > cfg_.abort_inn_g) {
            abort_t_ += dt;
            if (abort_t_ >= cfg_.abort_s) {
                trip_why_ = 4;
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
            boost_end_t_ = tp_;
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
    ///
    /// A sustained oscillation breaks the slope-adoption path: a sine
    /// can still phase-align both fits at some instant (same residual,
    /// same sign, within consist) and would resume a high flow without
    /// any pour. Count recent `fast` slope-sign flips during the gate —
    /// a dying ring stops flipping well before the fits calm, while a
    /// wiggle keeps flipping — and refuse to adopt a slope while they
    /// are still recent.
    void ungate_check(float t) {
        const fit_res nf =
            line_fit(fbuf_, cfg_.boost_win_s, 6, cfg_.boost_win_s * 0.8f);
        if (nf.ok && nf.worst <= cfg_.boost_resid_g) {
            note_osc(nf.slope, t);
        }
        if (t - gate_t_ < cfg_.gate_min_s) return;
        const fit_res f =
            line_fit(buf_, cfg_.calm_s, 6, cfg_.calm_s * 0.9f);
        if (!f.ok || f.worst >= cfg_.calm_g) return;
        const fit_res h =
            line_fit(buf_, cfg_.calm_s * 0.5f, 4, cfg_.calm_s * 0.45f);
        if (!h.ok || h.worst >= cfg_.calm_g * 0.7f) return;
        // A young gate may release after the minimum hold when both
        // smooth windows clear the noise floor, agree, and a clean
        // positive fast tail confirms them below the boost snap bound.
        // A moderately accelerating ramp may disagree full vs half;
        // require tight full-pre/fast-tail and half-pre/pre-tail pairs
        // before allowing the wider full/half margin.
        // This stronger proof tolerates
        // a shorter impact slice; steep ramps and the normal
        // post-transient path still wait for their full span.
        const float early_span = (t - gate_t_) - cfg_.gate_early_skip_s;
        const bool early_ready = early_span >= 0.12f &&
                                 t - gate_t_ < cfg_.gate_skip_s + 0.12f;
        const fit_res ep = early_ready
            ? line_fit(buf_, early_span, 8, early_span * 0.7f) : fit_res{};
        const fit_res ef = early_ready
            ? line_fit(fbuf_, early_span, 8, early_span * 0.7f) : fit_res{};
        const bool early_ramp =
            early_ready &&
            f.slope > cfg_.osc_slope_gps &&
            h.slope > cfg_.osc_slope_gps &&
            h.slope <= cfg_.boost_snap_gps &&
            (std::fabs(f.slope - h.slope) <= cfg_.slope_consist_gps ||
             (std::fabs(f.slope - h.slope) <= cfg_.slope_consist_gps * 2.0f &&
              std::fabs(f.slope - ef.slope) <= cfg_.slope_consist_gps &&
              std::fabs(h.slope - ep.slope) <= cfg_.slope_consist_gps)) &&
            ep.ok && ep.slope > 0.0f && ep.worst <= cfg_.boost_resid_g &&
            ef.ok && ef.slope > 0.0f && ef.worst <= cfg_.calm_g &&
            ef.slope <= cfg_.fit_slope_max_gps &&
            std::fabs(ef.slope - std::min(f.slope, h.slope)) <=
                cfg_.slope_consist_gps * 2.0f &&
            osc_flips_ <= cfg_.resume_flip_max;
        if (!early_ramp && h.slope > cfg_.boost_quiet_gps &&
            h.slope <= cfg_.fit_slope_max_gps &&
            t - gate_t_ < cfg_.gate_skip_s + 0.12f) {
            return;
        }
        // Slope adoption demands the strongest evidence: the WHOLE gate
        // window must read as one clean ramp whose slope agrees with the
        // newest half. A pour running through the gate (e.g. the steep
        // pour that tripped itself) qualifies — but a sine cannot, since
        // the gate always contains its reversal; an impact ring in the
        // window fails the same way and falls back to the held flow,
        // which is the right answer there anyway.
        const float gspan = std::min(t - gate_t_, 0.6f);
        const fit_res fg = line_fit(buf_, gspan, 8, gspan * 0.75f);
        const bool gate_ramp = fg.ok && fg.worst <= cfg_.calm_g;
        // Post-transient ramp: the gate's own leading slice (the splash
        // or bump that tripped it) is excluded, so an onset impact can't
        // poison the resume evidence. Both channels must read one clean
        // positive ramp over the remaining window — a sustained wiggle
        // flips inside it and fails the residual bound — so one counted
        // flip from the trip's ring no longer vetoes a real pour.
        const float pspan = (t - gate_t_) - cfg_.gate_skip_s;
        const fit_res fp = pspan >= 0.12f
            ? line_fit(buf_, pspan, 8, pspan * 0.7f) : fit_res{};
        const fit_res ff = pspan >= 0.12f
            ? line_fit(fbuf_, pspan, 8, pspan * 0.7f) : fit_res{};
        const bool post_ramp =
            fp.ok && fp.worst <= cfg_.calm_g && fp.slope > 0.0f &&
            ff.ok && ff.worst <= cfg_.calm_g && ff.slope > 0.0f &&
            std::fabs(fp.slope - h.slope) <= cfg_.gate_diff_gps &&
            osc_flips_ <= cfg_.resume_flip_max;
        float f0;
        bool  resumed_ramp = false;   // f0 adopted via a transient-ramp proof
        if (std::fabs(h.slope) < cfg_.boost_quiet_gps) {
            f0 = h.slope;   // flat half-window: stopped
        } else if (early_ramp) {
            f0 = std::min({f.slope, h.slope, ef.slope}) * cfg_.boost_snap_damp;
            resumed_ramp = true;
        } else if (h.slope > 0.0f && gate_ramp &&
                   h.slope <= cfg_.fit_slope_max_gps &&
                   std::fabs(fg.slope - h.slope) <=
                       cfg_.slope_consist_gps &&
                   std::fabs(f.slope - h.slope) <=
                       cfg_.slope_consist_gps &&
                   osc_flips_ < cfg_.resume_flip_max) {
            // Only a positive slope is adoptable (a steep negative ramp
            // is a lift-off, not a pour), and it must agree with BOTH
            // the whole-gate ramp and the trailing calm fit — a sine
            // segment can pass either one alone.
            f0 = h.slope;
        } else if (h.slope > 0.0f && h.slope <= cfg_.fit_slope_max_gps &&
                   post_ramp) {
            // Pour resumed through its own splash: the strict triple
            // agreement can't hold (the gate window contains the trip's
            // kink). The cascaded pre filter can still ring downward,
            // biasing both smooth slopes low even on a real ramp. Blend
            // the conservative pre rate with the certified fast ramp
            // only when their slopes agree within the resume margin;
            // a discrepant or implausible fast slope cannot raise f0.
            const float pre_rate = std::min(h.slope, fp.slope);
            const bool fast_agrees =
                ff.slope <= cfg_.fit_slope_max_gps &&
                std::fabs(ff.slope - pre_rate) <= cfg_.gate_diff_gps;
            f0 = fast_agrees ? 0.5f * (pre_rate + ff.slope) : pre_rate;
            resumed_ramp = true;
        } else if (osc_flips_ >= cfg_.resume_flip_max) {
            f0 = 0.0f;      // the slope genuinely reversed recently —
                            // oscillation or removal, never a pour to
                            // resume (a ~12 Hz ring can't produce
                            // counted flips: its gaps are too dense)
        } else {
            const float pred = gate_w_ + held_f_ * (t - gate_t_);
            f0 = std::fabs(f.level - pred) < cfg_.step_g ? held_f_ : 0.0f;
        }
        gated_      = false;
        boosting_   = false;
        run_sign_   = 0;
        run_t_      = 0.0f;
        run_slope_max_ = 0.0f;
        steep_run_n_   = 0;
        steep_n_       = 0;
        stop_t_        = 0.0f;
        snap_t_        = -1e9f;
        pour_hi_t_     = -1e9f;
        zero_held_     = false;
        zero_level_    = 0.0f;
        zero_t_        = 0.0f;
        restart_t_     = 0.0f;
        lost_t_     = 0.0f;
        abort_t_    = 0.0f;
        slope_sign_ = 0.0f;
        flip_candidate_sign_ = 0.0f;
        flip_candidate_s_    = 0.0f;
        ungate_t_   = t;
        ungate_f0_  = f0;        // diagnostics: what the resume adopted
        // Onset-kink case: the gate contained the pour's own start, so
        // the whole-window ramp test failed even though the newest half
        // already reads a clean positive slope. Grant the mid tracking
        // density — the Kalman converges on its own (no slope is
        // fabricated) — while the oscillation streak is cold, or when
        // the resume was certified by the post-transient ramp (its own
        // flip budget already tolerates the trip's ring). A rejected
        // sine still never gets to track hard.
        if ((osc_flips_ == 0 || resumed_ramp) && h.ok &&
            h.worst <= cfg_.calm_g && h.slope > cfg_.boost_quiet_gps) {
            track_t_ = t;
        }
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
        // Subtract unconditionally: a valid stored time may cross zero,
        // and elapsed math (tp_ - t) stays exact on both sides. The "never
        // set" sentinels just go further negative — never treated as "now".
        suppress_t_ -= delta_s;
        ungate_t_   -= delta_s;
        flip_t_     -= delta_s;
        track_t_    -= delta_s;
        osc_flip_t_ -= delta_s;
        boost_end_t_ -= delta_s;
        snap_t_     -= delta_s;
        pour_hi_t_  -= delta_s;
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
    float          track_t_  = -1e9f; // last clean steep window (q_track)
    float          lost_t_   = 0.0f;  // evidence stale time while boosted
    float          abort_t_  = 0.0f;  // flat-signal+inn time while boosted
    float          boost_t_  = 0.0f;  // boost start (rebased time)
    float          boost_end_t_ = -1e9f; // boost end — band keeps running
                                    //   briefly to stop settle momentum
    float          suppress_t_ = -1e9f; // evidence-loss drop -> re-boost hold-off
    float          sup_streak_ = 0.0f;  // consecutive lost-evidence drops
    // "Never" sentinels sit at -1e9, not -1: every consumer compares
    // elapsed time (tp_ - t) so a stored value may cross zero in a rebase
    // and stay valid — while -1e9 computes "ages ago" at any tp_.
    float          ungate_t_   = -1e9f; // last un-gate (jump cooldown)
    float          flip_t_     = -1e9f; // last slope-sign flip
    float          slope_sign_ = 0.0f;  // last confirmed credible sign
    float          flip_candidate_sign_ = 0.0f; // pending credible sign
    float          flip_candidate_s_    = 0.0f; // its consecutive duration
    // Oscillation bookkeeping inside a gate: a sustained wiggle flips
    // its fitted slope sign every half cycle; a dying ring stops flipping
    // well before the window calms. Recent flips block slope adoption on
    // resume — the windows cannot certify a pour while oscillating.
    int            osc_flips_ = 0;
    float          osc_sign_   = 0.0f;
    float          osc_flip_t_ = -1e9f;
    int            trip_why_   = 0; // 1 pre inn, 2 fast inn, 3 jump,
                                  //   4 boost abort, 5 steep slope
    int            steep_n_    = 0; // consecutive steep-fit samples
    int            steep_run_n_ = 0;//   same, for steep-claim grading
    float          stop_t_     = 0.0f; // flat-slope persistence timer
    float          snap_t_     = -1e9f;// last stop snap (pin window)
    float          pour_hi_t_  = -1e9f;// last f_ above 1.5 (pin window)
    float          zero_level_ = 0.0f;// weight at zero latch, not a timestamp
    bool           zero_held_  = false;// zero hold owns f_/w_ output
    float          zero_t_     = 0.0f; // consecutive stillness duration
    float          restart_t_  = 0.0f; // consecutive restart evidence
    float          ungate_f0_  = 0.0f; // flow adopted at last un-gate
    // accel motion veto
    float          motion_ema_ = 0.0f;
    float          ax_ = 0.0f, ay_ = 0.0f, az_ = 0.0f;
    bool           acc_primed_ = false;
};

} // namespace scale
