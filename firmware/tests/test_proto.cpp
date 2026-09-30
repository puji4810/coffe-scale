#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "scale_proto/proto.hpp"

using namespace proto;

namespace {

/// Parse "c0ffee00-5ca1-..." text bytes -> 16-byte array in text order.
std::array<std::uint8_t, 16> uuid_text_order(std::string_view s) {
    std::array<std::uint8_t, 16> b{};
    std::size_t                  n = 0;
    auto                         hv = [](char c) {
        return c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10;
    };
    for (std::size_t i = 0; i + 1 < s.size() && n < 16; ++i) {
        if (s[i] == '-') continue;
        b[n++] = static_cast<std::uint8_t>(hv(s[i]) * 16 + hv(s[i + 1]));
        ++i;
    }
    return b;
}

} // namespace

TEST_CASE("proto: UUID byte arrays match the text forms (LSB-first)") {
    // NimBLE wants the bytes reversed vs the text representation.
    for (int i = 0; i < 16; ++i) {
        CHECK(kServiceUuid128[i] == uuid_text_order(kServiceUuid)[15 - i]);
        CHECK(kStateUuid128[i] == uuid_text_order(kStateUuid)[15 - i]);
        CHECK(kCommandUuid128[i] == uuid_text_order(kCommandUuid)[15 - i]);
    }
    // spot-check the literal ends: text starts c0 ff ee 00..., ends ...8c01
    CHECK(kServiceUuid128[0] == 0x01);
    CHECK(kServiceUuid128[1] == 0x8c);
    CHECK(kServiceUuid128[14] == 0xff);
    CHECK(kServiceUuid128[15] == 0xc0);
    CHECK(kStateUuid128[0] == 0x02);
    CHECK(kCommandUuid128[0] == 0x03);
}

TEST_CASE("proto: state frame round-trips a full snapshot") {
    state s;
    s.grams         = 123.456f;
    s.flow_gps      = -3.21f;
    s.display_value = 123.5f;
    s.pitch_deg     = -1.6f;
    s.roll_deg      = 2.4f;
    s.timer_ms      = 83'456;
    s.battery_pct   = 82;
    s.unit          = 1;
    s.mode          = 1;
    s.timer_state   = 2;
    s.seq           = 200;
    s.stable        = true;
    s.tared         = true;
    s.calibrated    = true;
    s.charging      = true;

    const auto f = encode(s);
    REQUIRE(f.size() == kStateLen);
    const auto d = decode(f);
    REQUIRE(d.has_value());
    CHECK(d->grams == doctest::Approx(123.456f).epsilon(0.001));
    CHECK(d->flow_gps == doctest::Approx(-3.21f).epsilon(0.001));
    CHECK(d->display_value == doctest::Approx(123.5f).epsilon(0.001));
    CHECK(d->pitch_deg == doctest::Approx(-1.6f).epsilon(0.01));
    CHECK(d->roll_deg == doctest::Approx(2.4f).epsilon(0.01));
    CHECK(d->timer_ms == 83'400);      // floored to 0.1 s
    CHECK(d->battery_pct == 82);
    CHECK(d->unit == 1);
    CHECK(d->mode == 1);
    CHECK(d->timer_state == 2);
    CHECK(d->seq == 200);
    CHECK(d->stable);
    CHECK(d->tared);
    CHECK(d->calibrated);
    CHECK(d->charging);
}

TEST_CASE("proto: state encode rounds and clamps at the extremes") {
    state s;
    s.flow_gps      = 400.0f;      // over i16 x100 range -> clamps
    s.grams         = -5.236f;     // negative grams survive signed
    s.display_value = 21'000'000.f;
    s.pitch_deg     = -4000.f;     // under i16 x10 range -> clamps
    s.timer_ms      = 700'000'000; // way past u16 tenths -> saturates
    s.battery_pct   = -1;          // unknown battery

    const auto d = decode(encode(s));
    REQUIRE(d.has_value());
    CHECK(d->flow_gps == doctest::Approx(327.67f));
    CHECK(d->grams == doctest::Approx(-5.24f));
    CHECK(d->pitch_deg == doctest::Approx(-3276.8f));
    CHECK(d->timer_ms == 6'553'500);
    CHECK(d->battery_pct == -1);
}

TEST_CASE("proto: state frame byte layout is exact") {
    state s;
    s.grams         = 18.05f;      // -> i32 1805
    s.display_value = 18.0f;       // -> i32 18000
    s.flow_gps      = 1.5f;        // -> i16 150
    s.pitch_deg     = 0.5f;        // -> i16 5
    s.roll_deg      = -0.3f;       // -> i16 -3
    s.timer_ms      = 61'234;      // -> u16 612
    s.battery_pct   = -1;          // -> 0xff
    s.seq           = 7;
    s.unit          = 1;           // b4
    s.mode          = 1;           // b5
    s.timer_state   = 1;           // b6
    s.stable        = true;        // b0
    s.calibrated    = true;        // b2

    const auto f = encode(s);
    CHECK(f[0] == 1);              // version
    CHECK(f[1] == 0x75);           // b0|b2|b4|b5|b6
    CHECK(f[2] == 0xff);
    CHECK(f[3] == 7);
    // grams x100 = 1805 = 0x070d LE
    CHECK(f[4] == 0x0d);
    CHECK(f[5] == 0x07);
    CHECK(f[6] == 0x00);
    CHECK(f[7] == 0x00);
    // display x1000 = 18000 = 0x4650 LE
    CHECK(f[8] == 0x50);
    CHECK(f[9] == 0x46);
    CHECK(f[10] == 0x00);
    CHECK(f[11] == 0x00);
    // flow x100 = 150 = 0x0096
    CHECK(f[12] == 0x96);
    CHECK(f[13] == 0x00);
    // pitch x10 = 5
    CHECK(f[14] == 0x05);
    CHECK(f[15] == 0x00);
    // roll x10 = -3 = 0xfffd
    CHECK(f[16] == 0xfd);
    CHECK(f[17] == 0xff);
    // timer 612 tenths = 0x0264
    CHECK(f[18] == 0x64);
    CHECK(f[19] == 0x02);
}

TEST_CASE("proto: state decode rejects short and wrong-version frames") {
    state s;
    s.grams = 1.0f;
    auto    f = encode(s);

    CHECK_FALSE(decode(std::span{f.data(), kStateLen - 1}));
    CHECK_FALSE(decode(std::span<const std::uint8_t>{}));
    f[0] = 2;
    CHECK_FALSE(decode(f));
    f[0] = 0;
    CHECK_FALSE(decode(f));
}

TEST_CASE("proto: longer state frames decode (forward compatibility)") {
    state s;
    s.grams = 42.0f;
    s.seq   = 9;
    auto    base = encode(s);
    std::array<std::uint8_t, kStateLen + 8> f{};
    std::copy(base.begin(), base.end(), f.begin());
    f[kStateLen]     = 0xaa;   // hypothetical appended field
    f[kStateLen + 7] = 0xbb;
    const auto d = decode(f);
    REQUIRE(d.has_value());
    CHECK(d->grams == doctest::Approx(42.0f));
    CHECK(d->seq == 9);
}

TEST_CASE("proto: every command op round-trips") {
    const op ops[] = {op::tare,      op::timer_toggle, op::timer_reset,
                      op::mode,      op::cal_zero,     op::sleep};
    for (const op o : ops) {
        const auto f = encode_command({o, 0});
        CHECK(f.size == 1);
        CHECK(f.bytes[0] == static_cast<std::uint8_t>(o));
        const auto c = decode_command(
            std::span<const std::uint8_t>{f.bytes.data(), f.size});
        REQUIRE(c.has_value());
        CHECK(c->o == o);
        CHECK(c->arg == 0);
    }

    const auto uf = encode_command({op::unit, 1});
    CHECK(uf.size == 2);
    const auto uc = decode_command(
        std::span<const std::uint8_t>{uf.bytes.data(), uf.size});
    REQUIRE(uc.has_value());
    CHECK(uc->o == op::unit);
    CHECK(uc->arg == 1);

    const auto sf = encode_command({op::cal_span, 12'345});
    CHECK(sf.size == 5);
    CHECK(sf.bytes[1] == 0x39);        // 12345 = 0x3039 LE
    CHECK(sf.bytes[2] == 0x30);
    CHECK(sf.bytes[3] == 0x00);
    CHECK(sf.bytes[4] == 0x00);
    const auto sc = decode_command(
        std::span<const std::uint8_t>{sf.bytes.data(), sf.size});
    REQUIRE(sc.has_value());
    CHECK(sc->o == op::cal_span);
    CHECK(sc->arg == 12'345);
}

TEST_CASE("proto: invalid commands are rejected") {
    CHECK_FALSE(decode_command(std::span<const std::uint8_t>{}));
    CHECK_FALSE(decode_command(std::array{std::uint8_t{0}}));        // op 0
    CHECK_FALSE(decode_command(std::array{std::uint8_t{9}}));        // op 9
    CHECK_FALSE(decode_command(std::array{std::uint8_t{0xff}}));
    // arg bytes tacked onto an arg-less op
    CHECK_FALSE(decode_command(std::array{std::uint8_t{1}, std::uint8_t{0}}));
    // unit arg > 1
    CHECK_FALSE(decode_command(std::array{std::uint8_t{5}, std::uint8_t{2}}));
    // cal_span: wrong length and non-positive masses
    CHECK_FALSE(decode_command(std::array{std::uint8_t{7}, std::uint8_t{1}}));
    const auto neg = encode_command({op::cal_span, 12'345});
    auto       bad = neg;
    bad.bytes[1] = 0; bad.bytes[2] = 0;   // arg = 0
    CHECK_FALSE(decode_command(
        std::span<const std::uint8_t>{bad.bytes.data(), bad.size}));
    bad.bytes[1] = 0xff; bad.bytes[2] = 0xff;
    bad.bytes[3] = 0xff; bad.bytes[4] = 0xff;   // arg = -1
    CHECK_FALSE(decode_command(
        std::span<const std::uint8_t>{bad.bytes.data(), bad.size}));
}
