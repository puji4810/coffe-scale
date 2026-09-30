#pragma once

namespace scale {

enum class unit { gram, ounce };

[[nodiscard]] constexpr float convert(float grams, unit u) {
    switch (u) {
        case unit::gram:  return grams;
        case unit::ounce: return grams / 28.349523125f;
    }
    return grams;
}

[[nodiscard]] constexpr const char* label(unit u) {
    return u == unit::gram ? "g" : "oz";
}

} // namespace scale
