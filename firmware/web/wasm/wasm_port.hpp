#pragma once

/// Emscripten side of the ui port — shared between ui_port_wasm.cpp (flush
/// producer) and bindings_screen.cpp (embind exports). Not for target/sim.

#include <cstddef>
#include <cstdint>

namespace ui::wasm {

/// Canvas-ready RGBA8888 frame, hor_res x ver_res, row-major.
[[nodiscard]] const std::uint32_t* framebuffer();

/// Pixel-buffer length in uint32 elements (hor_res * ver_res).
[[nodiscard]] std::size_t framebuffer_len();

/// True if a flush wrote pixels since the last take; clears the flag.
bool take_dirty();

} // namespace ui::wasm
