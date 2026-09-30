#include <doctest/doctest.h>

#include "lis2dw12/lis2dw12.hpp"
#include "mock_i2c.hpp"
#include "nau7802/nau7802.hpp"
#include "tmp102/tmp102.hpp"

using namespace drv;

namespace {

void no_delay(std::uint32_t) {}

/// Wire NAU7802 side effects into the mock: PUR after PUD|PUA, CR after CS,
/// CALS auto-clears (calibration completes, no error).
void fake_nau7802(mock_i2c& m) {
    m.on_write = [&m](std::uint8_t reg, std::uint8_t val) {
        if (reg == 0x00) {   // PU_CTRL
            if ((val & 0x06) == 0x06) m.regs[0x00] |= 0x08;   // PUR
            if (val & 0x10) m.regs[0x00] |= 0x20;             // CR -> data ready
        }
        if (reg == 0x02 && (val & 0x04)) {                    // CALS set
            m.regs[0x02] &= 0xFB;                             // chip clears CALS
        }
    };
}

} // namespace

TEST_CASE("nau7802 init: writes the datasheet power-up sequence") {
    mock_i2c m;
    fake_nau7802(m);
    nau7802 adc{m};
    const auto r = adc.init({}, no_delay);
    REQUIRE(r.has_value());

    // CTRL1: VLDO[5:3] untouched (external AVDD), GA[2:0]=0b111 (x128)
    CHECK(m.regs[0x01] == 0x07);
    // CTRL2 = CRS[6:4]=0b011 (80 SPS); CALS cleared after cal
    CHECK(m.regs[0x02] == 0x30);
    // ADC[5:4] chopper clock off, PGA_PWR[7] cap on, PGA[5] LDOMODE off
    CHECK((m.regs[0x15] & 0x30) == 0x30);
    CHECK((m.regs[0x1C] & 0x80) == 0x80);
    CHECK((m.regs[0x1B] & 0x20) == 0x00);
    // PU_CTRL: PUD|PUA|CS on, AVDDS clear = external AVDD (CR/PUR by mock)
    CHECK((m.regs[0x00] & 0x96) == 0x16);
    // reset was issued: first PU_CTRL write had RR set
    REQUIRE(!m.writes.empty());
    CHECK(m.writes.front() == std::pair<std::uint8_t, std::uint8_t>{0x00, 0x01});
}

TEST_CASE("nau7802 set_ldo: internal LDO programs VLDO + AVDDS") {
    mock_i2c m;
    nau7802  adc{m};
    REQUIRE(adc.set_ldo(nau7802_ldo::v2_7).has_value());
    CHECK((m.regs[0x01] & 0x38) == 0x30);   // VLDO = 0b110
    CHECK((m.regs[0x00] & 0x80) == 0x80);   // AVDDS = internal LDO
    REQUIRE(adc.set_ldo(nau7802_ldo::external).has_value());
    CHECK((m.regs[0x00] & 0x80) == 0x00);   // back to external supply input
    CHECK((m.regs[0x01] & 0x38) == 0x30);   // VLDO left alone
}

TEST_CASE("nau7802 read: 24-bit sign extension") {
    mock_i2c m;
    nau7802  adc{m};
    m.regs[0x12] = 0x80; m.regs[0x13] = 0x00; m.regs[0x14] = 0x01;
    const auto v = adc.read();
    REQUIRE(v.has_value());
    CHECK(*v == static_cast<std::int32_t>(0xFF80'0001));

    m.regs[0x12] = 0x00; m.regs[0x13] = 0x10; m.regs[0x14] = 0x00;
    CHECK(*adc.read() == 0x1000);
}

TEST_CASE("nau7802 data_ready mirrors CR bit") {
    mock_i2c m;
    nau7802  adc{m};
    CHECK(*adc.data_ready() == false);
    m.regs[0x00] = 0x20;
    CHECK(*adc.data_ready() == true);
}

TEST_CASE("nau7802 calibrate_afe: timeout when CALS never clears") {
    mock_i2c m;   // no side effects -> CALS stays set
    nau7802  adc{m};
    const auto r = adc.calibrate_afe(nau7802_calmod::internal, no_delay, 5);
    REQUIRE(!r.has_value());
    CHECK(r.error() == bus::i2c_errc::timeout);
}

TEST_CASE("nau7802 calibrate_afe: CAL_ERR propagates") {
    mock_i2c m;
    m.on_write = [&m](std::uint8_t reg, std::uint8_t) {
        if (reg == 0x02) m.regs[0x02] = 0x08;   // CALS cleared + CAL_ERR
    };
    nau7802 adc{m};
    const auto r = adc.calibrate_afe(nau7802_calmod::offset, no_delay, 5);
    REQUIRE(!r.has_value());
    CHECK(r.error() == bus::i2c_errc::io);
}

TEST_CASE("tmp102 temperature conversion") {
    mock_i2c m;
    tmp102   t{m};
    m.regs[0x00] = 0x19; m.regs[0x01] = 0x00;   // 25 C
    CHECK(*t.read_celsius() == doctest::Approx(25.0f));
    m.regs[0x00] = 0xE7; m.regs[0x01] = 0x00;   // -25 C
    CHECK(*t.read_celsius() == doctest::Approx(-25.0f));
}

TEST_CASE("lis2dw12 init checks WHO_AM_I and programs control regs") {
    mock_i2c m;
    lis2dw12 a{m};
    m.regs[0x0F] = 0x44;
    REQUIRE(a.init().has_value());
    CHECK(m.regs[0x21] == 0x0C);   // CTRL2: BDU | IF_ADD_INC
    CHECK(m.regs[0x25] == 0x04);   // CTRL6: FS 2g, low noise
    CHECK(m.regs[0x20] == 0x44);   // CTRL1: ODR 100 Hz, high-perf
}

TEST_CASE("lis2dw12 init rejects a wrong chip id") {
    mock_i2c m;
    lis2dw12 a{m};
    m.regs[0x0F] = 0x00;
    const auto r = a.init();
    REQUIRE(!r.has_value());
    CHECK(r.error() == bus::i2c_errc::io);
}

TEST_CASE("lis2dw12 read_mg converts 14-bit left-justified data") {
    mock_i2c m;
    lis2dw12 a{m};
    m.regs[0x0F] = 0x44;
    REQUIRE(a.init().has_value());
    // X: 0x1000 >> 2 = 1024 * 0.244 = 249.86 mg (~0.25 g)
    m.regs[0x28] = 0x00; m.regs[0x29] = 0x10;
    const auto v = a.read_mg();
    REQUIRE(v.has_value());
    CHECK(v->x == doctest::Approx(249.856f));
    CHECK(v->y == doctest::Approx(0.f));
    CHECK(v->z == doctest::Approx(0.f));
}
