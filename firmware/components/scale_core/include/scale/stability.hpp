#pragma once

/// Stability detection: the weight is "stable" when the spread (max-min) of
/// the last `window` samples stays within `tolerance` grams.

#include "scale/filters.hpp"

namespace scale {

template <std::size_t Cap = 64>
class stability_detector {
public:
    constexpr stability_detector(std::size_t window_samples, float tolerance_g)
        : window_(window_samples), tol_(tolerance_g) {}

    void configure(std::size_t window_samples, float tolerance_g) {
        window_ = window_samples;
        tol_    = tolerance_g;
        reset();
    }

    bool push(float grams) {
        w_.push(grams);
        if (w_.size() < window_) {
            return stable_ = false;
        }
        return stable_ = w_.spread_last(window_) <= tol_;
    }

    [[nodiscard]] bool stable() const { return stable_; }

    void reset() {
        w_.clear();
        stable_ = false;
    }

private:
    window<float, Cap> w_;
    std::size_t        window_;
    float              tol_;
    bool               stable_ = false;
};

} // namespace scale
