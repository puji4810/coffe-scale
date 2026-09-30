#pragma once

/// Filter primitives for the weight pipeline. All fixed-capacity, no heap.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <numbers>
#include <numeric>
#include <span>

namespace scale {

/// Circular window of the last `Cap` pushed samples. `at(0)` is the oldest
/// live sample, `at(size()-1)` the newest.
template <class T, std::size_t Cap>
class window {
public:
    void push(T v) {
        buf_[head_] = v;
        head_       = (head_ + 1) % Cap;
        if (n_ < Cap) {
            ++n_;
        }
    }

    void clear() { n_ = 0; head_ = 0; }

    [[nodiscard]] static constexpr std::size_t capacity() { return Cap; }
    [[nodiscard]] std::size_t                  size() const { return n_; }
    [[nodiscard]] bool                         full() const { return n_ == Cap; }

    /// i = 0 -> oldest live sample.
    [[nodiscard]] T at(std::size_t i) const { return buf_[(head_ + Cap - n_ + i) % Cap]; }
    [[nodiscard]] T latest() const { return n_ ? at(n_ - 1) : T{}; }

    /// Min/max over the last `k` samples (k clamped to live count).
    [[nodiscard]] T max_last(std::size_t k) const {
        return fold_last(k, [](T a, T b) { return std::max(a, b); });
    }
    [[nodiscard]] T min_last(std::size_t k) const {
        return fold_last(k, [](T a, T b) { return std::min(a, b); });
    }
    [[nodiscard]] T spread_last(std::size_t k) const {
        return n_ ? max_last(k) - min_last(k) : T{};
    }
    [[nodiscard]] T mean_last(std::size_t k) const {
        k = std::min(k, n_);
        return k ? fold_last(k, [](T a, T b) { return a + b; }) / static_cast<T>(k) : T{};
    }

private:
    template <class F>
    [[nodiscard]] T fold_last(std::size_t k, F f) const {
        k = std::min(k, n_);
        T acc{};
        for (std::size_t i = 0; i < k; ++i) {
            const T v = at(n_ - k + i);
            acc       = i ? f(acc, v) : v;
        }
        return acc;
    }

    std::array<T, Cap> buf_{};
    std::size_t        head_ = 0;
    std::size_t        n_    = 0;
};

/// Median of the last N (odd) samples — kills single-sample spikes that the
/// load cell / mains pickup occasionally produces.
template <std::size_t N>
    requires(N % 2 == 1)
class median_filter {
public:
    [[nodiscard]] std::int32_t push(std::int32_t v) {
        buf_[pos_] = v;
        pos_       = (pos_ + 1) % N;
        if (n_ < N) {
            ++n_;
        }
        std::array<std::int32_t, N> tmp{};
        std::copy_n(buf_.begin(), n_, tmp.begin());
        auto mid = tmp.begin() + static_cast<std::ptrdiff_t>(n_ / 2);
        std::nth_element(tmp.begin(), mid, tmp.begin() + static_cast<std::ptrdiff_t>(n_));
        return last_ = *mid;
    }

    [[nodiscard]] std::int32_t value() const { return last_; }
    [[nodiscard]] bool         primed() const { return n_ == N; }
    void                       clear() { n_ = 0; pos_ = 0; last_ = 0; }

private:
    std::array<std::int32_t, N> buf_{};
    std::size_t                 pos_ = 0;
    std::size_t                 n_   = 0;
    std::int32_t                last_ = 0;
};

/// Exponential moving average. y[n] = y[n-1] + alpha*(x - y[n-1]).
class ema {
public:
    explicit constexpr ema(float alpha) : alpha_(alpha) {}

    void set_alpha(float a) { alpha_ = a; }
    void reset(float v) {
        y_      = v;
        primed_ = true;
    }

    [[nodiscard]] float push(float v) {
        if (!primed_) {
            reset(v);
        } else {
            y_ += alpha_ * (v - y_);
        }
        return y_;
    }

    [[nodiscard]] float value() const { return y_; }
    [[nodiscard]] bool  primed() const { return primed_; }

private:
    float alpha_;
    float y_      = 0.0f;
    bool  primed_ = false;
};

/// EMA whose alpha adapts to the prediction error |x - y|: a real step
/// tracks fast, a quiet signal gets heavy smoothing. `bands` are matched
/// highest error threshold first, so order them descending; the last entry
/// (err_g = 0) is the resting alpha.
///
/// Thresholds are in grams: err_g = |x - y| / counts_per_gram. Call
/// set_counts_per_gram() whenever calibration changes. Uncalibrated
/// (cpg = 1) the thresholds are effectively in counts, so any real signal
/// selects the fast band.
class adaptive_ema {
public:
    struct band {
        float err_g;    // select this alpha when |err_g| >= threshold
        float alpha;
    };
    static constexpr std::size_t max_bands = 4;

    adaptive_ema() = default;
    adaptive_ema(std::initializer_list<band> bands) { set_bands(bands); }

    void set_bands(std::initializer_list<band> bands) {
        set_bands(std::span<const band>{bands.begin(), bands.size()});
    }
    void set_bands(std::span<const band> bands) {
        n_ = 0;
        for (const band& b : bands) {
            if (n_ < max_bands) bands_[n_++] = b;
        }
    }
    void set_counts_per_gram(float cpg) { cpg_ = cpg > 0.0f ? cpg : 1.0f; }
    void reset(float v) {
        y_      = v;
        primed_ = true;
    }

    [[nodiscard]] float push(float v) {
        if (!primed_) {
            reset(v);
            return y_;
        }
        const float err_g = std::fabs(v - y_) / cpg_;
        float       a     = n_ ? bands_[n_ - 1].alpha : 1.0f;
        for (std::size_t i = 0; i < n_; ++i) {
            if (err_g >= bands_[i].err_g) {
                a = bands_[i].alpha;
                break;
            }
        }
        y_ += a * (v - y_);
        return y_;
    }

    [[nodiscard]] float value() const { return y_; }
    [[nodiscard]] bool  primed() const { return primed_; }

private:
    std::array<band, max_bands> bands_{};
    std::size_t                 n_      = 0;
    float                       cpg_    = 1.0f;
    float                       y_      = 0.0f;
    bool                        primed_ = false;
};

/// 2nd-order Bessel low-pass as a single biquad. Analog 2nd-order Bessel
/// poles (-3 dB normalised) sit at -1.1016 +/- j0.6360, i.e. natural
/// frequency w0 = 1.2720*w_3dB and Q = 1/sqrt(3) — those constants are all
/// the biquad needs. Maximally flat group delay: smooth step response with
/// no overshoot, at the cost of a gentler rolloff than Butterworth.
class bessel2_lpf {
public:
    bessel2_lpf() = default;
    bessel2_lpf(float cutoff_hz, float sample_hz) { configure(cutoff_hz, sample_hz); }

    /// `cutoff_hz` is the -3 dB point. Clamped to < 0.45*sample_hz so the
    /// bilinear prewarp stays sane.
    void configure(float cutoff_hz, float sample_hz) {
        constexpr float kQ  = 0.5773502691896258f;   // 1/sqrt(3)
        constexpr float kW0 = 1.2720196495140690f;   // |pole| of norm. Bessel
        if (sample_hz <= 0.0f) {
            return;
        }
        const float fc = std::clamp(cutoff_hz * kW0, 0.0f, 0.45f * sample_hz);
        const float k  = std::tan(std::numbers::pi_v<float> * fc / sample_hz);
        const float kk = k * k;
        const float n  = 1.0f / (1.0f + k / kQ + kk);
        b0_ = kk * n;
        b1_ = 2.0f * b0_;
        b2_ = b0_;
        a1_ = 2.0f * (kk - 1.0f) * n;
        a2_ = (1.0f - k / kQ + kk) * n;
        x1_ = x2_ = y1_ = y2_ = 0.0f;
    }

    [[nodiscard]] float push(float v) {
        const float y =
            b0_ * v + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = v;
        y2_ = y1_;
        y1_ = y;
        return y;
    }

    /// Prime the delay lines so the output rests at `v`.
    void reset(float v) { x1_ = x2_ = v; y1_ = y2_ = v; }

    [[nodiscard]] float value() const { return y1_; }

private:
    float b0_ = 0.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;
    float x1_ = 0.0f, x2_ = 0.0f;
    float y1_ = 0.0f, y2_ = 0.0f;
};

/// Savitzky–Golay FIR smoother: a least-squares polynomial of `Order` over
/// the last N samples, evaluated at `eval` (centred units: 0 = window
/// centre, +(N-1)/2 = newest sample = zero added latency). Coefficients are
/// solved once in the constructor; push() is a plain FIR convolution.
template <std::size_t N, int Order = 2>
    requires(N % 2 == 1 && Order >= 0 && N > static_cast<std::size_t>(Order))
class savitzky_golay {
public:
    static constexpr std::size_t P = static_cast<std::size_t>(Order);

    explicit savitzky_golay(float eval = static_cast<float>(N - 1) / 2.0f) {
        // Gram matrix of the window Vandermonde: G[r][c] = sum_j u_j^(r+c).
        std::array<std::array<float, P + 1>, P + 1> g{};
        std::array<float, 2 * P + 1>                uk{};
        for (std::size_t j = 0; j < N; ++j) {
            const float u = static_cast<float>(j) - (N - 1) / 2.0f;
            uk[0]         = 1.0f;
            for (std::size_t k = 1; k <= 2 * P; ++k) uk[k] = uk[k - 1] * u;
            for (std::size_t r = 0; r <= P; ++r) {
                for (std::size_t c = 0; c <= P; ++c) g[r][c] += uk[r + c];
            }
        }

        // Invert G via Gauss-Jordan with partial pivoting (P+1 <= ~5).
        std::array<std::array<float, P + 1>, P + 1> inv{};
        for (std::size_t i = 0; i <= P; ++i) inv[i][i] = 1.0f;
        for (std::size_t col = 0; col <= P; ++col) {
            std::size_t piv = col;
            for (std::size_t r = col + 1; r <= P; ++r) {
                if (std::fabs(g[r][col]) > std::fabs(g[piv][col])) piv = r;
            }
            std::swap(g[piv], g[col]);
            std::swap(inv[piv], inv[col]);
            const float d = g[col][col];
            if (std::fabs(d) < 1e-9f) {
                return;    // degenerate window -> coefs_ stay 0, passthrough
            }
            for (std::size_t c = 0; c <= P; ++c) {
                g[col][c] /= d;
                inv[col][c] /= d;
            }
            for (std::size_t r = 0; r <= P; ++r) {
                if (r == col) continue;
                const float f = g[r][col];
                for (std::size_t c = 0; c <= P; ++c) {
                    g[r][c] -= f * g[col][c];
                    inv[r][c] -= f * inv[col][c];
                }
            }
        }

        // c_j = sum_k eval^k * sum_m Ginv[k][m] * u_j^m
        for (std::size_t j = 0; j < N; ++j) {
            const float u = static_cast<float>(j) - (N - 1) / 2.0f;
            float       c = 0.0f, ek = 1.0f;
            for (std::size_t k = 0; k <= P; ++k, ek *= eval) {
                float um = 1.0f, s = 0.0f;
                for (std::size_t m = 0; m <= P; ++m, um *= u) {
                    s += inv[k][m] * um;
                }
                c += ek * s;
            }
            coefs_[j] = c;
        }
        valid_ = true;
    }

    /// Push a sample; returns the polynomial estimate at `eval`. Until the
    /// window is full (or if coefficients failed) the input passes through.
    [[nodiscard]] float push(float v) {
        w_.push(v);
        if (!valid_ || !w_.full()) {
            return last_ = v;
        }
        float acc = 0.0f;
        for (std::size_t j = 0; j < N; ++j) acc += coefs_[j] * w_.at(j);
        return last_ = acc;
    }

    [[nodiscard]] float value() const { return last_; }
    [[nodiscard]] bool  primed() const { return w_.full(); }
    void                clear() { w_.clear(); }
    [[nodiscard]] float coef(std::size_t j) const { return coefs_[j]; }

private:
    window<float, N>      w_{};
    std::array<float, N>  coefs_{};
    float                 last_  = 0.0f;
    bool                  valid_ = false;
};

} // namespace scale
