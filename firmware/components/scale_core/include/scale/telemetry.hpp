#pragma once

/// CSV serialisation of scale::diag — one row per ADC sample for offline
/// characterization (drift, repeatability, hysteresis, filter comparison).
/// Pure snprintf; host-testable, no heap.

#include <cstdio>

#include "scale/scale.hpp"

namespace scale {

/// Column order — keep in sync with diag_csv().
inline constexpr const char* kDiagCsvHeader =
    "t_ms,raw,median,lpf,g,zero_off,temp_c,drift_c,"
    "stable,quiet,flow_gps,ax_mg,ay_mg,az_mg,pitch,roll,disturbed";

/// Write one CSV row (with trailing newline). Returns snprintf length.
inline int diag_csv(char* buf, std::size_t cap, const diag& d) {
    return std::snprintf(
        buf, cap,
        "%lld,%ld,%ld,%.1f,%.3f,%.4f,%.2f,%.2f,%d,%d,%.3f,%.1f,%.1f,%.1f,%.2f,%.2f,%d\n",
        static_cast<long long>(d.t.count()),
        static_cast<long>(d.raw_counts),
        static_cast<long>(d.median_counts),
        static_cast<double>(d.lpf_counts),
        static_cast<double>(d.grams),
        static_cast<double>(d.zero_offset_g),
        static_cast<double>(d.temp_c),
        static_cast<double>(d.drift_counts),
        d.stable ? 1 : 0,
        d.tilt_quiet ? 1 : 0,
        static_cast<double>(d.flow_gps),
        static_cast<double>(d.accel_mg.x),
        static_cast<double>(d.accel_mg.y),
        static_cast<double>(d.accel_mg.z),
        static_cast<double>(d.pitch_deg),
        static_cast<double>(d.roll_deg),
        d.disturbed ? 1 : 0);
}

} // namespace scale
