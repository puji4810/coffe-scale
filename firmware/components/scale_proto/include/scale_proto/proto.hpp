#pragma once

/// coffee-scale wire protocol — the single ABI source shared by the
/// scale firmware (NimBLE peripheral), the web WASM bindings and the
/// Korvo remote. Header-only, std-only, C++23.
///
/// GATT layout (service + characteristics below are 128-bit UUIDs):
///   state   char: read + notify — one 20-byte v1 frame per snapshot,
///           pushed at ~20 Hz to every subscribed connection
///   command char: write / write-without-response — 1..5 byte commands
///
/// State frame v1 (exactly 20 bytes, little-endian — fits the default
/// 23-byte ATT MTU so no MTU exchange is needed):
///   off 0  u8   protocol version = 1
///   off 1  u8   flags: b0 stable, b1 tared, b2 calibrated, b3 charging,
///               b4 unit (0 g / 1 oz), b5 mode (0 weigh / 1 brew),
///               b6-7 timer_state
///   off 2  i8   battery %, -1 = unknown
///   off 3  u8   frame sequence (incremented per push)
///   off 4  i32  grams x 100
///   off 8  i32  display value x 1000 (latched, unit-quantised readout)
///   off 12 i16  flow g/s x 100
///   off 14 i16  pitch deg x 10
///   off 16 i16  roll deg x 10
///   off 18 u16  brew timer in 0.1 s (floor(ms/100), saturates 65535)
/// Decoders must accept longer frames — later versions may append fields.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace proto {

// ---- UUIDs -----------------------------------------------------------------

inline constexpr std::string_view kServiceUuid =
    "c0ffee00-5ca1-4e5a-9b1e-7d2f3a6b8c01";
inline constexpr std::string_view kStateUuid =
    "c0ffee00-5ca1-4e5a-9b1e-7d2f3a6b8c02";
inline constexpr std::string_view kCommandUuid =
    "c0ffee00-5ca1-4e5a-9b1e-7d2f3a6b8c03";

namespace detail {

constexpr int hex_val(char c) {
    return c >= '0' && c <= '9' ? c - '0'
         : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10
         : -1;
}

/// Canonical "xxxxxxxx-xxxx-..." text -> 16 bytes in text (big-endian)
/// order.
constexpr std::array<std::uint8_t, 16> uuid_be(std::string_view s) {
    std::array<std::uint8_t, 16> b{};
    std::size_t                  nibble = 0;
    for (std::size_t i = 0; i + 1 < s.size() && nibble < 32; ++i) {
        if (s[i] == '-') {
            continue;
        }
        const int hi = hex_val(s[i]);
        const int lo = hex_val(s[i + 1]);
        if (hi < 0 || lo < 0) {
            break;
        }
        b[nibble / 2] = static_cast<std::uint8_t>(hi * 16 + lo);
        nibble += 2;
        ++i;
    }
    return b;
}

constexpr std::array<std::uint8_t, 16>
reverse(const std::array<std::uint8_t, 16>& b) {
    std::array<std::uint8_t, 16> r{};
    for (std::size_t i = 0; i < 16; ++i) {
        r[i] = b[15 - i];
    }
    return r;
}

/// BLE / NimBLE byte order: LSB-first — the text-order bytes reversed
/// (what BLE_UUID128_INIT expects in .value128 / uuid128.value).
constexpr std::array<std::uint8_t, 16> uuid128(std::string_view s) {
    return reverse(uuid_be(s));
}

} // namespace detail

/// 16-byte UUID arrays in NimBLE's LSB-first order.
inline constexpr auto kServiceUuid128 = detail::uuid128(kServiceUuid);
inline constexpr auto kStateUuid128   = detail::uuid128(kStateUuid);
inline constexpr auto kCommandUuid128 = detail::uuid128(kCommandUuid);

// The byte arrays are derived from the strings — prove it: reversed back,
// they must re-parse to the same text-order bytes.
static_assert(detail::reverse(kServiceUuid128) ==
              detail::uuid_be(kServiceUuid));
static_assert(detail::reverse(kStateUuid128) == detail::uuid_be(kStateUuid));
static_assert(detail::reverse(kCommandUuid128) ==
              detail::uuid_be(kCommandUuid));

// ---- state frame ------------------------------------------------------------

struct state {
    float          grams = 0.0f;
    float          flow_gps = 0.0f;
    float          display_value = 0.0f;
    float          pitch_deg = 0.0f;
    float          roll_deg = 0.0f;
    std::uint32_t  timer_ms = 0;
    std::int8_t    battery_pct = -1;
    std::uint8_t   unit = 0;          // 0 g / 1 oz
    std::uint8_t   mode = 0;          // 0 weigh / 1 brew
    std::uint8_t   timer_state = 0;
    std::uint8_t   seq = 0;
    bool           stable = false;
    bool           tared = false;
    bool           calibrated = false;
    bool           charging = false;
};

inline constexpr std::size_t   kStateLen     = 20;
inline constexpr std::uint8_t  kStateVersion = 1;

namespace detail {

constexpr std::int64_t lround_clamped(float v, std::int64_t lo,
                                      std::int64_t hi) {
    // lround isn't constexpr-friendly pre-C++23 in all stdlibs; encode()
    // is a runtime path anyway — keep it a plain function semantic.
    return std::clamp<std::int64_t>(std::llround(v), lo, hi);
}

constexpr void put_u8(std::span<std::uint8_t> f, std::size_t off,
                      std::uint8_t v) {
    f[off] = v;
}
constexpr void put_i16(std::span<std::uint8_t> f, std::size_t off,
                       std::int16_t v) {
    const auto u = static_cast<std::uint16_t>(v);
    f[off]       = static_cast<std::uint8_t>(u & 0xff);
    f[off + 1]   = static_cast<std::uint8_t>(u >> 8);
}
constexpr void put_i32(std::span<std::uint8_t> f, std::size_t off,
                       std::int32_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    for (int i = 0; i < 4; ++i) {
        f[off + i] = static_cast<std::uint8_t>(u >> (i * 8));
    }
}
constexpr void put_u16(std::span<std::uint8_t> f, std::size_t off,
                       std::uint16_t v) {
    put_i16(f, off, static_cast<std::int16_t>(v));
}

constexpr std::int16_t get_i16(std::span<const std::uint8_t> f,
                               std::size_t off) {
    return static_cast<std::int16_t>(f[off] | (f[off + 1] << 8));
}
constexpr std::int32_t get_i32(std::span<const std::uint8_t> f,
                               std::size_t off) {
    const std::uint32_t u = static_cast<std::uint32_t>(f[off]) |
                            (static_cast<std::uint32_t>(f[off + 1]) << 8) |
                            (static_cast<std::uint32_t>(f[off + 2]) << 16) |
                            (static_cast<std::uint32_t>(f[off + 3]) << 24);
    return static_cast<std::int32_t>(u);
}
constexpr std::uint16_t get_u16(std::span<const std::uint8_t> f,
                                std::size_t off) {
    return static_cast<std::uint16_t>(get_i16(f, off));
}

} // namespace detail

/// Serialise one snapshot into its 20-byte v1 frame.
inline std::array<std::uint8_t, kStateLen> encode(const state& s) {
    std::array<std::uint8_t, kStateLen> f{};
    f[0] = kStateVersion;
    f[1] = static_cast<std::uint8_t>(
        (s.stable ? 0x01 : 0) | (s.tared ? 0x02 : 0) |
        (s.calibrated ? 0x04 : 0) | (s.charging ? 0x08 : 0) |
        ((s.unit & 1) << 4) | ((s.mode & 1) << 5) |
        ((s.timer_state & 3) << 6));
    f[2] = static_cast<std::uint8_t>(s.battery_pct);
    f[3] = s.seq;
    detail::put_i32(f, 4, static_cast<std::int32_t>(detail::lround_clamped(
                            s.grams * 100.0f, INT32_MIN, INT32_MAX)));
    detail::put_i32(f, 8, static_cast<std::int32_t>(detail::lround_clamped(
                            s.display_value * 1000.0f, INT32_MIN,
                            INT32_MAX)));
    detail::put_i16(f, 12, static_cast<std::int16_t>(detail::lround_clamped(
                             s.flow_gps * 100.0f, INT16_MIN, INT16_MAX)));
    detail::put_i16(f, 14, static_cast<std::int16_t>(detail::lround_clamped(
                             s.pitch_deg * 10.0f, INT16_MIN, INT16_MAX)));
    detail::put_i16(f, 16, static_cast<std::int16_t>(detail::lround_clamped(
                             s.roll_deg * 10.0f, INT16_MIN, INT16_MAX)));
    // timer: floor to 0.1 s and saturate — never rounds up a partial tick
    const std::uint64_t tenths = s.timer_ms / 100;
    detail::put_u16(f, 18,
                    static_cast<std::uint16_t>(std::min<std::uint64_t>(
                        tenths, 65535)));
    return f;
}

/// Parse a state frame. Longer frames are accepted (forward-compatible
/// appended fields); shorter or wrong-version frames are rejected.
inline std::optional<state> decode(std::span<const std::uint8_t> f) {
    if (f.size() < kStateLen || f[0] != kStateVersion) {
        return std::nullopt;
    }
    state                s{};
    const std::uint8_t   fl = f[1];
    s.stable     = (fl & 0x01) != 0;
    s.tared      = (fl & 0x02) != 0;
    s.calibrated = (fl & 0x04) != 0;
    s.charging   = (fl & 0x08) != 0;
    s.unit         = static_cast<std::uint8_t>((fl >> 4) & 1);
    s.mode         = static_cast<std::uint8_t>((fl >> 5) & 1);
    s.timer_state  = static_cast<std::uint8_t>((fl >> 6) & 3);
    s.battery_pct  = static_cast<std::int8_t>(f[2]);
    s.seq          = f[3];
    s.grams          = detail::get_i32(f, 4) / 100.0f;
    s.display_value  = detail::get_i32(f, 8) / 1000.0f;
    s.flow_gps       = detail::get_i16(f, 12) / 100.0f;
    s.pitch_deg      = detail::get_i16(f, 14) / 10.0f;
    s.roll_deg       = detail::get_i16(f, 16) / 10.0f;
    s.timer_ms = static_cast<std::uint32_t>(detail::get_u16(f, 18)) * 100;
    return s;
}

// ---- commands ---------------------------------------------------------------

enum class op : std::uint8_t {
    tare = 1,
    timer_toggle = 2,     // same as a long tare press
    timer_reset = 3,
    mode = 4,
    unit = 5,             // + u8 unit (0 g / 1 oz)
    cal_zero = 6,
    cal_span = 7,         // + i32 LE mass in centigrams
    sleep = 8,
};

struct command {
    op           o;
    std::int32_t arg = 0;
};

/// Encoded command: 1..5 bytes in `bytes`, `size` valid.
struct command_frame {
    std::array<std::uint8_t, 5> bytes{};
    std::size_t                 size = 0;
};

inline command_frame encode_command(const command& c) {
    command_frame f{};
    f.bytes[0] = static_cast<std::uint8_t>(c.o);
    switch (c.o) {
        case op::unit:
            f.bytes[1] = static_cast<std::uint8_t>(c.arg);
            f.size     = 2;
            break;
        case op::cal_span: {
            const auto u = static_cast<std::uint32_t>(c.arg);
            for (int i = 0; i < 4; ++i) {
                f.bytes[1 + i] = static_cast<std::uint8_t>(u >> (i * 8));
            }
            f.size = 5;
            break;
        }
        default:
            f.size = 1;
            break;
    }
    return f;
}

/// Parse a command write. Rejects unknown ops, wrong lengths for the op,
/// unit values above 1 and non-positive cal_span masses.
inline std::optional<command>
decode_command(std::span<const std::uint8_t> f) {
    if (f.empty()) {
        return std::nullopt;
    }
    const auto o = static_cast<op>(f[0]);
    switch (o) {
        case op::tare:
        case op::timer_toggle:
        case op::timer_reset:
        case op::mode:
        case op::cal_zero:
        case op::sleep:
            if (f.size() != 1) {
                return std::nullopt;
            }
            return command{o, 0};
        case op::unit:
            if (f.size() != 2 || f[1] > 1) {
                return std::nullopt;
            }
            return command{o, f[1]};
        case op::cal_span: {
            if (f.size() != 5) {
                return std::nullopt;
            }
            const std::int32_t cg = detail::get_i32(f.subspan(1), 0);
            if (cg <= 0) {
                return std::nullopt;
            }
            return command{o, cg};
        }
        default:
            return std::nullopt;
    }
}

} // namespace proto
