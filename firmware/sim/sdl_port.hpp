#pragma once

/// SDL-backed LVGL display for the desktop preview. Implements ui::port_init()
/// (in sdl_port.cpp) plus event polling for the sim harness.

#include <cstdint>

namespace sim {

/// Pump SDL events once per frame. Returns a bitmask of sim_key.
enum sim_key : std::uint32_t {
    key_none  = 0,
    key_tare  = 1u << 0,   // T key — short tare
    key_long  = 1u << 1,   // L key — long tare (timer toggle in brew mode)
    key_mode  = 1u << 2,   // M key — mode switch
    key_quit  = 1u << 31,  // window closed / Esc
};

std::uint32_t sdl_poll();

} // namespace sim
