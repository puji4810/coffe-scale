#pragma once

/// Streaming scale pipeline:
///     raw counts -> median(3) -> LPF -> plateau snap -> filtered counts
///       -> calibrated gross weight -> stability -> physical zero tracker
///       -> tare + zero offset + deadband -> grams
///     median counts -> calibrated weight - tare -> Kalman flow rate
///     net median + compensated grams -> display platform + hysteresis
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
#include <optional>

#include "scale/calibration.hpp"
#include "scale/clock.hpp"
#include "scale/filters.hpp"
#include "scale/flow_kf.hpp"
#include "scale/stability.hpp"
#include "scale/thermal.hpp"
#include "scale/tilt.hpp"
#include "scale/zero_track.hpp"
#include "scale/weight_display.hpp"

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
    /// sub-half-division noise and the "-0.0" sign flicker. Physical empty
    /// evidence, rather than this band alone, authorizes zero correction.
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
    /// Display platform: a short flat window held for `latch_hold` settles
    /// clean placements. Bounded vibration uses a stable longer mean.
    /// The anchor is continuous, separate from the rounded digit; edges,
    /// clean trends or sustained growth release it. <= 0 disables hold.
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
    int          flow_trip     = 0;      // last gate reason (flow_kf)
    int          flow_boost    = 0;      // estimator tracking a change
    float        flow_resume   = 0.0f;   // flow adopted at last un-gate
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
        const float gross = cal_.to_grams(filtered_ - thermal_.drift_counts(temp_c_));
        const float input = cal_.to_grams(med - thermal_.drift_counts(temp_c_));
        stab_.push(gross);
        zt_.apply(gross, input, system_stable(), now);
        // Zero corrections translate the weight origin; they are not flow.
        const float flow_g = input - tare_counts_ / cal_.counts_per_gram;
        kf_.push(now, flow_g);
        fed_ = true;
        const float net = grams();
        const float display_input = input - tare_counts_ / cal_.counts_per_gram - zt_.offset();
        const std::optional<float> empty_value =
            zt_.empty() && std::fabs(input-zt_.offset()) < deadband_g_
                ? std::optional<float>{-tare_counts_ / cal_.counts_per_gram}
                : std::nullopt;
        display_.push(display_input, net + lag_comp_g(), flow_gps(),
                      tilt_.quiet(), empty_value, now);

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
        diag_.flow_trip     = kf_.trip_reason();
        diag_.flow_boost    = kf_.boosting() ? 1 : 0;
        diag_.flow_resume   = kf_.ungate_f0();
        diag_.tilt_quiet    = tilt_.quiet();
        diag_.accel_mg      = tilt_.accel();
        diag_.pitch_deg     = tilt_.pitch_deg();
        diag_.roll_deg      = tilt_.roll_deg();
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
    /// zero tracking, before display hold/rounding. |value| under
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

    /// Display weight: compensated grams, platform hold and hysteretic
    /// rounding. Updated once per push; LCD and web getters share the
    /// same cached digit without advancing its evidence/history.
    [[nodiscard]] float display_grams() const { return display_.value(); }

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

    /// Brew freezes slow empty-pan drift; proven unloads still restore
    /// the physical zero reference in either mode.
    void set_zero_tracking(bool on) { zt_.set_enabled(on); }

    /// Zero the display at whatever is on the pan right now. The snapshot
    /// includes the current drift correction so the display lands on 0.000
    /// exactly and stays compensated as temperature moves afterwards.
    void tare() {
        tare_counts_ =
            filtered_ - cal_.zero_counts - thermal_.drift_counts(temp_c_)
            - zt_.offset() * cal_.counts_per_gram;
        stab_.reset();
        zt_.cancel_pending();
        kf_.reset();
        display_.reset();
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
        display_.reset();
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
        display_.reset();
        return true;
    }

    [[nodiscard]] const calibration& calibration_data() const { return cal_; }
    void load_calibration(const calibration& c) {
        cal_ = c;
        ema_.set_counts_per_gram(c.counts_per_gram);
        stab_.reset();
        zt_.reset();
        kf_.reset();
        display_.reset();
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
        const float d = (kf_.primed() ? kf_.weight() - zt_.offset() : g) - g;
        return r * d > 0.0f ? w * d : 0.0f;
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
        display_.configure({cfg.display_res_g, cfg.display_hyst_g, cfg.latch_hold});
        lag_min_gps_   = cfg.lag_min_gps;
        lag_full_gps_  = cfg.lag_full_gps;
        med_hist_.clear();
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
    float                  lag_min_gps_   = 0.3f;
    float                  lag_full_gps_  = 1.0f;
    weight_display         display_{};
    bool                   fed_               = false;
};

} // namespace scale
