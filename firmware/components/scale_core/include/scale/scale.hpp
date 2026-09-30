#pragma once

/// Streaming scale pipeline:
///     raw counts -> median(3) -> LPF -> plateau snap -> filtered counts
///       -> grams = (counts - tare - zero - drift(T)) / counts_per_gram
///       -> stability -> zero-track -> deadband -> grams
///       -> flow rate (g/s, median-domain regression)
///       -> display_grams: pour-lag comp -> latch -> quantise+hysteresis
///
/// Feed one push() per ADC conversion (e.g. 80 Hz at NAU7802 80 SPS) with a
/// real monotone timestamp — flow regression and the zero-track hold timer
/// run on it, so a dropped DRDY stretches the time axis instead of
/// silently corrupting rates. The filter chain itself does not care.
///
/// Tare and calibration act in the filtered-count domain so zero/span
/// capture benefits from the same smoothing as the displayed value; the
/// thermal drift model is subtracted in the same domain, before /cpg.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

#include "scale/calibration.hpp"
#include "scale/clock.hpp"
#include "scale/filters.hpp"
#include "scale/flow_kf.hpp"
#include "scale/stability.hpp"
#include "scale/thermal.hpp"
#include "scale/tilt.hpp"
#include "scale/zero_track.hpp"

namespace scale {

enum class lpf_kind : std::uint8_t {
    adaptive_ema,   // tracking fast on steps, tight when still (default)
    bessel2,        // fixed-bandwidth biquad, no overshoot
    savgol,         // Savitzky-Golay 7-pt quadratic at the newest sample
    none,           // median only — bring-up / tests
};

struct scale_config {
    lpf_kind    lpf              = lpf_kind::adaptive_ema;
    float       sample_hz        = 80.0f;   // bessel design + doc; ADC SPS
    /// Adaptive-EMA bands, descending err threshold (grams); last = resting.
    /// The resting band is heavy enough that bench vibration (a fan, a
    /// shared counter) stays invisible, while the plateau snap + pour lag
    /// compensation keep real loads responsive.
    std::array<adaptive_ema::band, 4> ema_bands{{
        {5.0f, 0.60f}, {1.0f, 0.28f}, {0.15f, 0.10f}, {0.0f, 0.05f}}};
    float       bessel_cutoff_hz = 2.0f;
    std::size_t stability_window = 16;      // 16 @ 80 SPS = 200 ms
    float       stability_tol_g  = 0.5f;    // max spread inside window
    /// Flow estimator: gated constant-velocity Kalman on an 8 Hz
    /// Butterworth-prefiltered weight signal (see flow_kf.hpp).
    flow_kf_config flow_kf{};
    /// Plateau snap: once the median output itself has been quiet for
    /// `snap_window` samples (spread under `snap_spread_g` AND
    /// least-squares slope under `snap_max_gps` — the rate gate keeps
    /// slow pours, which still creep under the spread bound, from
    /// triggering it) while the LPF still trails it by more than
    /// `snap_min_g`, the filter state jumps to the input mean — lands
    /// the display on the true plateau instead of crawling there on the
    /// resting alpha. Applies to the lagging filters (adaptive_ema,
    /// bessel2); savgol/none have no tail to skip.
    std::size_t snap_window   = 8;        // 8 @ 80 SPS = 100 ms of quiet input
    float       snap_spread_g = 0.20f;
    float       snap_min_g    = 0.05f;
    float       snap_max_gps  = 0.5f;     // max input slope for "quiet"
    /// Display deadband: |grams| below this reads as exactly 0 — hides
    /// sub-half-division noise and the "-0.0" sign flicker. Persistent
    /// residuals get pulled inside the band by the zero tracker anyway.
    float       display_deadband_g = 0.05f;
    /// Display quantiser with hysteresis: the shown weight moves in
    /// `display_res_g` steps and only re-rounds once the continuous value
    /// leaves the shown step by more than half a step + `display_hyst_g`,
    /// so a reading parked on a rounding boundary holds its digit instead
    /// of flickering. Affects display_grams() only; grams() stays
    /// continuous for flow/telemetry.
    float       display_res_g  = 0.1f;
    float       display_hyst_g = 0.02f;
    /// Lag compensation: while |flow| is up the LPF trails the true
    /// weight — blend the measured lag (regression fit at the newest
    /// timestamp minus the filtered readout) into the display, ramping
    /// in over [lag_min_gps, lag_full_gps]. Self-dissolving: fit and
    /// filter re-converge when the pour stops.
    float       lag_min_gps  = 0.3f;
    float       lag_full_gps = 1.0f;
    /// Display latch: once the system has been continuously stable for
    /// `latch_hold` — or, covering vibration the stability detector can't
    /// bless (a bench fan), once the quantised readout itself has held a
    /// step that long — the shown weight freezes. Released the moment the
    /// continuous value drifts past the quantiser's hysteresis bound
    /// (half a step + display_hyst_g), so a stale digit can never survive
    /// the drift it hides — e.g. the residual the zero tracker just
    /// pulled back to 0. latch_hold <= 0 disables the latch.
    clock_ms    latch_hold{300};
    /// Same for the flow readout: the KF's resting estimate otherwise
    /// flickers ±0.x g/s. Real pours are >= 1 g/s.
    float       flow_deadband_gps  = 0.3f;
    /// Display clamp for the flow readout: real pours are < ~15 g/s, so
    /// anything past this is a step-load artifact, not pour dynamics.
    float       flow_clip_gps      = 30.0f;
    zero_track_config zero_track{};
    tilt_config       tilt{};
};

/// Per-sample diagnostic snapshot — one row of the telemetry stream.
struct diag {
    clock_ms    t{0};
    std::int32_t raw_counts    = 0;
    std::int32_t median_counts = 0;
    float        lpf_counts    = 0.0f;
    float        grams         = 0.0f;   // displayed (post zero-track)
    float        zero_offset_g = 0.0f;
    float        temp_c        = 0.0f;
    float        drift_counts  = 0.0f;
    float        flow_gps      = 0.0f;
    bool         disturbed     = false;  // flow KF impact gate engaged
    bool         stable        = false;  // load-cell window
    bool         tilt_quiet    = true;   // accelerometer motion gate
    tilt::vec3   accel_mg{};
    float        pitch_deg     = 0.0f;
    float        roll_deg      = 0.0f;
};

class scale {
public:
    using config = scale_config;

    explicit scale(const config& cfg = config{}) { apply_config(cfg); }

    /// Push one raw conversion (signed counts; NAU7802 gives 24-bit).
    /// `now` must be a real monotone timestamp (esp_timer on target).
    void push(std::int32_t counts, clock_ms now) {
        const float med = static_cast<float>(median_.push(counts));
        med_hist_.push(med);
        switch (lpf_) {
            case lpf_kind::bessel2: filtered_ = bessel_.push(med); break;
            case lpf_kind::savgol:  filtered_ = sg_.push(med); break;
            case lpf_kind::none:    filtered_ = med; break;
            case lpf_kind::adaptive_ema:
            default:                filtered_ = ema_.push(med); break;
        }
        snap_to_plateau();
        const float net = cal_.to_grams(filtered_ - tare_counts_
                                        - thermal_.drift_counts(temp_c_));
        stab_.push(net);
        zt_.apply(net, system_stable(), now);
        // Flow runs on the median-domain weight: linear, so the Kalman
        // sees the raw pour slope — the adaptive EMA's nonlinear tracking
        // would distort it.
        const float flow_g = cal_.to_grams(med - tare_counts_
                                           - thermal_.drift_counts(temp_c_))
                             - zt_.offset();
        kf_.push(now, flow_g);
        update_latch(now);

        diag_.t             = now;
        diag_.raw_counts    = counts;
        diag_.median_counts = median_.value();
        diag_.lpf_counts    = filtered_;
        diag_.grams         = grams();
        diag_.zero_offset_g = zt_.offset();
        diag_.temp_c        = temp_c_;
        diag_.drift_counts  = thermal_.drift_counts(temp_c_);
        diag_.flow_gps      = flow_gps();
        diag_.stable        = stab_.stable();
        diag_.disturbed     = kf_.disturbed();
        diag_.tilt_quiet    = tilt_.quiet();
        diag_.accel_mg      = tilt_.accel();
        diag_.pitch_deg     = tilt_.pitch_deg();
        diag_.roll_deg      = tilt_.roll_deg();
        fed_ = true;
    }

    /// Feed one accelerometer sample (mg) — 10-25 Hz suffices.
    void feed_accel(float x_mg, float y_mg, float z_mg) {
        tilt_.feed(x_mg, y_mg, z_mg);
        kf_.feed_accel(x_mg, y_mg, z_mg);   // motion veto on boost evidence
    }

    /// Load-cell signal stable (spread window quiet).
    [[nodiscard]] bool stable() const { return stab_.stable(); }
    /// Load-cell stable AND no motion on the accelerometer — the signal
    /// zero tracking / captures should trust.
    [[nodiscard]] bool system_stable() const {
        return stab_.stable() && tilt_.quiet();
    }

    /// Filtered raw counts (post median + LPF, pre tare/cal/drift).
    [[nodiscard]] float filtered_counts() const { return filtered_; }
    /// Latest median output — unsmoothed but spike-free, useful for debug.
    [[nodiscard]] std::int32_t median_counts() const { return median_.value(); }

    /// Net weight in grams after calibration, tare, thermal drift comp and
    /// zero tracking — this is the displayed value. |value| under
    /// display_deadband_g collapses to exactly 0. Reads 0 until the first
    /// sample primes the filters (an unprimed pipeline would report a full
    /// tare's worth of negative offset).
    [[nodiscard]] float grams() const {
        if (!fed_) return 0.0f;
        const float g = cal_.to_grams(filtered_ - tare_counts_
                                      - thermal_.drift_counts(temp_c_))
                        - zt_.offset();
        return std::fabs(g) < deadband_g_ ? 0.0f : g;
    }

    /// Display weight: grams() + pour-lag compensation, quantised to
    /// display_res_g steps with hysteresis — the digit only re-rounds
    /// once the value has moved half a step + display_hyst_g away from
    /// the shown step, so a reading parked near a rounding boundary
    /// holds its last digit instead of flickering. Once the system has
    /// been stable for latch_hold the value is frozen outright until
    /// the continuous value drifts past that same hysteresis bound.
    /// Stateful display cache; logically const — shared by the LCD
    /// refresh and the web snapshot under g_mtx.
    [[nodiscard]] float display_grams() const {
        if (latched_) {
            return shown_g_ == 0.0f ? 0.0f : shown_g_;   // no "-0.0"
        }
        const float g = grams() + lag_comp_g();
        if (!shown_init_ ||
            std::fabs(g - shown_g_) > res_g_ * 0.5f + hyst_g_) {
            shown_g_    = quantize_g(g);
            shown_init_ = true;
        }
        return shown_g_ == 0.0f ? 0.0f : shown_g_;
    }

    /// Pour rate in g/s from the gated Kalman estimator. |rate| under
    /// flow_deadband_gps reads 0, and the readout is clipped to
    /// ±flow_clip_gps. While disturbed() the estimator holds the
    /// pre-impact flow briefly, then 0.
    [[nodiscard]] float flow_gps() const {
        if (!fed_) return 0.0f;
        const float c = std::clamp(kf_.rate(), -flow_clip_gps_, flow_clip_gps_);
        return std::fabs(c) < flow_deadband_gps_ ? 0.0f : c;
    }

    /// Impact gate engaged — the estimator detected a disturbance
    /// (cup set down, knock, stirring) and is holding/rejecting it.
    [[nodiscard]] bool disturbed() const { return kf_.disturbed(); }

    /// Last pushed sample's diagnostic record.
    [[nodiscard]] const diag& last_diag() const { return diag_; }

    /// Orientation snapshot for a level indicator. Only meaningful while
    /// tilt is quiet.
    [[nodiscard]] float pitch_deg() const { return tilt_.pitch_deg(); }
    [[nodiscard]] float roll_deg()  const { return tilt_.roll_deg(); }
    [[nodiscard]] bool  level()     const { return tilt_.level(); }

    /// Latest temperature input — drives the drift model. Feed from TMP102
    /// whenever it is read (a few Hz is plenty).
    void set_temperature(float temp_c) { temp_c_ = temp_c; }

    /// Runtime gate for the zero tracker — freeze it during brew sessions.
    void set_zero_tracking(bool on) { zt_.set_enabled(on); }

    /// Zero the display at whatever is on the pan right now. The snapshot
    /// includes the current drift correction so the display lands on 0.000
    /// exactly and stays compensated as temperature moves afterwards.
    void tare() {
        tare_counts_ =
            filtered_ - cal_.zero_counts - thermal_.drift_counts(temp_c_);
        stab_.reset();
        zt_.reset();
        kf_.reset();
        latched_ = false;
        stable_since_.reset();
    }
    void               clear_tare() { tare_counts_ = 0.0f; }
    [[nodiscard]] bool tared() const { return tare_counts_ != 0.0f; }
    /// Raw tare offset in the filtered-counts domain (for the raw
    /// capture header; replay applies it as the starting tare).
    [[nodiscard]] float tare_counts() const { return tare_counts_; }

    // --- calibration ---------------------------------------------------
    // Two-step: clear pan -> cal_zero(); place known mass -> cal_span(mass).

    /// Capture the current filtered counts as the 0 g reference.
    void cal_zero() {
        cal_.zero_counts = filtered_;
        tare_counts_     = 0.0f;
        stab_.reset();
        zt_.reset();
        kf_.reset();
        latched_ = false;
        stable_since_.reset();
    }

    /// Capture span from a known mass currently on the pan.
    /// Returns false for a nonsensical input (zero/negative mass or no delta).
    bool cal_span(float known_mass_g) {
        const float span = filtered_ - cal_.zero_counts;
        if (known_mass_g <= 0.0f || span <= 0.0f) {
            return false;
        }
        cal_.counts_per_gram = span / known_mass_g;
        ema_.set_counts_per_gram(cal_.counts_per_gram);
        stab_.reset();
        zt_.reset();
        kf_.reset();
        latched_ = false;
        stable_since_.reset();
        return true;
    }

    [[nodiscard]] const calibration& calibration_data() const { return cal_; }
    void load_calibration(const calibration& c) {
        cal_ = c;
        ema_.set_counts_per_gram(c.counts_per_gram);
    }
    [[nodiscard]] bool calibrated() const { return cal_.valid(); }

    /// Thermal drift model, fitted offline from measured (temp, zero) pairs.
    void set_thermal_model(const thermal_model& m) { thermal_ = m; }

    /// Zero-track offset currently applied to the display (grams).
    [[nodiscard]] float zero_offset_g() const { return zt_.offset(); }

    /// Reconfigure without reconstruction (e.g. after an SPS change).
    /// Resets filter/history state — the pipeline re-primes.
    void configure(const config& cfg) { apply_config(cfg); }

private:
    /// True once the spike-free input has been quiet for snap_window_
    /// samples: spread under snap_spread_g_ AND least-squares slope of
    /// the window under snap_max_gps_ — a slow pour still creeps under
    /// the spread bound, so the rate gate decides "quiet". Gates the
    /// plateau snap.
    [[nodiscard]] bool input_quiet() const {
        const float cpg =
            cal_.counts_per_gram > 0.0f ? cal_.counts_per_gram : 1.0f;
        if (med_hist_.size() < snap_window_ ||
            med_hist_.spread_last(snap_window_) > snap_spread_g_ * cpg) {
            return false;
        }
        const std::size_t n0 = med_hist_.size() - snap_window_;
        float st = 0.0f, sw = 0.0f, stt = 0.0f, stw = 0.0f;
        for (std::size_t i = 0; i < snap_window_; ++i) {
            const float x = static_cast<float>(i) / sample_hz_;
            const float y = (med_hist_.at(n0 + i) - med_hist_.at(n0)) / cpg;
            st += x;
            sw += y;
            stt += x * x;
            stw += x * y;
        }
        const float n   = static_cast<float>(snap_window_);
        const float den = n * stt - st * st;
        if (den <= 0.0f) {
            return true;    // fixed time grid: unreachable
        }
        const float slope = (n * stw - st * sw) / den;
        return std::fabs(slope) <= snap_max_gps_;
    }

    /// If the input has already settled while the LPF output still trails
    /// it, jump the filter state onto the plateau. No-op for lag-free
    /// modes and before the window fills.
    void snap_to_plateau() {
        if (lpf_ != lpf_kind::adaptive_ema && lpf_ != lpf_kind::bessel2) {
            return;
        }
        if (!input_quiet()) {
            return;   // input still moving: pour, step in flight, vibration
        }
        const float cpg =
            cal_.counts_per_gram > 0.0f ? cal_.counts_per_gram : 1.0f;
        const float mean = med_hist_.mean_last(snap_window_);
        if (std::fabs(filtered_ - mean) <= snap_min_g_ * cpg) {
            return;   // already on the plateau
        }
        filtered_ = mean;
        ema_.reset(mean);
        bessel_.reset(mean);
    }

    /// Measured filter lag in grams — the gap between the Kalman's
    /// lag-free weight state and the LPF output. Gated by pour rate so
    /// resting jitter is never amplified; ramps in over
    /// [lag_min_gps_, lag_full_gps_]. Only applied when the estimate leads
    /// the filter in the direction of the measured flow — after a step the
    /// estimate is the laggard, and adding it back would drag the readout
    /// below the true weight.
    [[nodiscard]] float lag_comp_g() const {
        const float r = flow_gps();
        const float w = std::clamp(
            (std::fabs(r) - lag_min_gps_) / (lag_full_gps_ - lag_min_gps_),
            0.0f, 1.0f);
        if (w <= 0.0f) {
            return 0.0f;
        }
        const float g = grams();
        const float d = (kf_.primed() ? kf_.weight() : g) - g;
        return r * d > 0.0f ? w * d : 0.0f;
    }

    [[nodiscard]] float quantize_g(float g) const {
        return res_g_ > 0.0f ? std::round(g / res_g_) * res_g_ : g;
    }

    /// Freeze the displayed weight once the system has been stable for
    /// latch_hold_, or once the quantised readout itself has been calm for
    /// that long (vibration keeps the stability window open even when the
    /// shown digit isn't moving — latch anyway so the readout doesn't
    /// flicker). Release when the continuous value drifts past the
    /// quantiser's hysteresis bound — the same bound the unlatched
    /// readout obeys, so the latch can neither hold a digit the honest
    /// readout wouldn't show nor flicker on a rounding boundary.
    void update_latch(clock_ms now) {
        if (latch_hold_.count() <= 0) {
            return;   // latch disabled
        }
        if (latched_) {
            if (std::fabs(grams() - shown_g_) > res_g_ * 0.5f + hyst_g_) {
                latched_      = false;
                stable_since_ = now;
            }
            return;
        }
        const float shown = display_grams();
        if (shown != last_shown_) {
            last_shown_  = shown;
            quiet_since_ = now;   // value-calm arm: shown step just changed
        }
        if (!system_stable()) {
            stable_since_.reset();
        } else if (!stable_since_) {
            stable_since_ = now;
        }
        // The |flow| gate on the value-calm path keeps the latch from
        // grabbing a stale step between the quantiser's ticks of a slow
        // pour — there is no lag compensation on a latched readout.
        const bool held =
            (stable_since_ && now - *stable_since_ >= latch_hold_) ||
            (now - quiet_since_ >= latch_hold_ &&
             std::fabs(flow_gps()) < 0.5f);
        if (held) {
            latched_ = true;   // shown_g_ already holds the displayed step
        }
    }

    void apply_config(const config& cfg) {
        lpf_        = cfg.lpf;
        sample_hz_  = cfg.sample_hz > 0.0f ? cfg.sample_hz : 80.0f;
        ema_.set_bands(std::span<const adaptive_ema::band>{cfg.ema_bands});
        ema_.set_counts_per_gram(cal_.counts_per_gram);
        bessel_.configure(cfg.bessel_cutoff_hz, sample_hz_);
        stab_.configure(cfg.stability_window, cfg.stability_tol_g);
        tilt_.configure(cfg.tilt);
        kf_.configure(cfg.flow_kf);
        zt_    = zero_tracker{cfg.zero_track};
        deadband_g_      = cfg.display_deadband_g;
        flow_deadband_gps_ = cfg.flow_deadband_gps;
        flow_clip_gps_   = cfg.flow_clip_gps;
        snap_window_ = std::clamp(cfg.snap_window, std::size_t{2},
                                  med_hist_.capacity());
        snap_spread_g_ = cfg.snap_spread_g;
        snap_min_g_    = cfg.snap_min_g;
        snap_max_gps_  = cfg.snap_max_gps;
        res_g_         = cfg.display_res_g;
        hyst_g_        = cfg.display_hyst_g;
        lag_min_gps_   = cfg.lag_min_gps;
        lag_full_gps_  = cfg.lag_full_gps;
        latch_hold_    = cfg.latch_hold;
        med_hist_.clear();
        shown_init_    = false;
        latched_       = false;
        stable_since_.reset();
        quiet_since_   = clock_ms{0};
        last_shown_    = std::numeric_limits<float>::quiet_NaN();
        // Re-prime running filters at the current level so a mid-stream
        // filter switch doesn't make the display dive to zero and recover.
        // (Not on first configure: an unprimed EMA locks onto sample #1.)
        if (ema_.primed()) {
            ema_.reset(filtered_);
            bessel_.reset(filtered_);
        }
        sg_.clear();
    }

    median_filter<3>       median_;
    adaptive_ema           ema_;
    bessel2_lpf            bessel_;
    savitzky_golay<7, 2>   sg_;
    stability_detector<>   stab_{16, 0.3f};
    flow_kf                kf_{};
    zero_tracker           zt_{};
    thermal_model          thermal_{};
    tilt                   tilt_{};
    calibration            cal_{};
    diag                   diag_{};
    window<float, 16>      med_hist_{};   // median history for plateau snap
    lpf_kind               lpf_       = lpf_kind::adaptive_ema;
    float                  sample_hz_ = 80.0f;
    float                  temp_c_    = 25.0f;
    float                  filtered_  = 0.0f;
    float                  tare_counts_ = 0.0f;
    float                  deadband_g_  = 0.05f;
    float                  flow_deadband_gps_ = 0.3f;
    float                  flow_clip_gps_     = 30.0f;
    std::size_t            snap_window_   = 8;
    float                  snap_spread_g_ = 0.20f;
    float                  snap_min_g_    = 0.05f;
    float                  snap_max_gps_  = 0.5f;
    float                  res_g_         = 0.1f;
    float                  hyst_g_        = 0.02f;
    float                  lag_min_gps_   = 0.3f;
    float                  lag_full_gps_  = 1.0f;
    clock_ms               latch_hold_{300};
    std::optional<clock_ms> stable_since_{};
    clock_ms               quiet_since_{0};
    // NaN sentinel: the first push() always "changes" it, so the calm
    // timer starts on the first real sample rather than at time 0.
    float                  last_shown_ = std::numeric_limits<float>::quiet_NaN();
    bool                   latched_         = false;
    mutable float          shown_g_       = 0.0f;
    mutable bool           shown_init_    = false;
    bool                   fed_               = false;
};

} // namespace scale
