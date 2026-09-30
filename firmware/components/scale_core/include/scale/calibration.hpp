#pragma once

/// Two-point linear calibration in raw-count domain:
///     grams = (counts - zero_counts) / counts_per_gram
/// Captured with `scale::begin_calibration` / `finish_calibration` and meant
/// to be persisted to NVS on the device.

namespace scale {

struct calibration {
    float zero_counts     = 0.0f;
    float counts_per_gram = 1.0f;

    [[nodiscard]] constexpr float to_grams(float counts) const {
        return (counts - zero_counts) / counts_per_gram;
    }
    [[nodiscard]] constexpr bool valid() const { return counts_per_gram != 0.0f; }
};

[[nodiscard]] constexpr calibration two_point(float raw_zero,
                                              float raw_loaded,
                                              float mass_g) {
    return {.zero_counts     = raw_zero,
            .counts_per_gram = (raw_loaded - raw_zero) / mass_g};
}

} // namespace scale
