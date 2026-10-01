#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include "scale/clock.hpp"
#include "scale/filters.hpp"

namespace scale {

/// Timestamped weight evidence, fixed capacity. Regress relative grams
/// and relative integer time so large loads and long uptime stay precise.
template <std::size_t Cap>
class weight_window {
public:
    struct sample { clock_ms t; float g; };
    struct fit {
        float mean = 0, spread = 0, slope = 0, worst = 0;
        bool turning = false;
        bool ready = false;
    };
    void clear() { samples_.clear(); }
    void push(float g, clock_ms now) { samples_.push({now, g}); }
    [[nodiscard]] fit read(clock_ms span) const {
        fit out;
        if (samples_.size() < 3 || span.count() <= 0) return out;
        const auto end = samples_.latest().t;
        std::size_t first = 0;
        while (first + 1 < samples_.size() &&
               end - samples_.at(first).t > span) ++first;
        const auto origin = samples_.at(first);
        const auto n = samples_.size() - first;
        if (n < 3) return out;
        float st = 0, sy = 0, stt = 0, sty = 0;
        float lo = std::numeric_limits<float>::max(), hi = -lo;
        for (std::size_t i = first; i < samples_.size(); ++i) {
            const auto p = samples_.at(i);
            const float t = (p.t - origin.t).count() * .001f;
            const float y = p.g - origin.g;
            st += t; sy += y; stt += t*t; sty += t*y;
            lo = std::min(lo, y); hi = std::max(hi, y);
        }
        const float nf = static_cast<float>(n);
        const float den = nf*stt - st*st;
        if (den <= 0) return out;
        out.mean = origin.g + sy/nf;
        out.spread = hi - lo;
        const float last = samples_.latest().g - origin.g;
        const float leg = out.spread*.25f;
        out.turning = out.spread > .002f &&
            ((hi > leg && hi-last > leg) || (-lo > leg && last-lo > leg));
        out.slope = (nf*sty - st*sy)/den;
        const float intercept = (sy - out.slope*st)/nf;
        for (std::size_t i = first; i < samples_.size(); ++i) {
            const auto p = samples_.at(i);
            const float t = (p.t - origin.t).count() * .001f;
            out.worst = std::max(out.worst,
                std::fabs((p.g-origin.g) - intercept - out.slope*t));
        }
        out.ready = (end - origin.t).count() >= span.count() * .85f;
        return out;
    }
private:
    window<sample, Cap> samples_;
};

} // namespace scale
