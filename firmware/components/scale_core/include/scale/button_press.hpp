#pragma once

#include "scale/clock.hpp"

namespace scale {

/// Active-low button timing, kept separate from GPIO and task scheduling.
class button_press {
public:
    enum class action { none, short_press, long_press };

    action push(bool down, clock_ms now) {
        if (have_edge_ && now - last_edge_ < clock_ms{30}) return action::none;
        have_edge_ = true;
        last_edge_ = now;
        if (blocked_) {
            if (!down) blocked_ = false;
            return action::none;
        }
        if (down) {
            if (!pressed_) {
                since_ = now;
                pressed_ = true;
            }
            return action::none;
        }
        if (!pressed_) return action::none;
        const auto held = now - since_;
        pressed_ = false;
        return held >= clock_ms{800} ? action::long_press : action::short_press;
    }

    /// Drop old evidence at a sleep boundary. A key already held on wake
    /// must first release; waking the device is not a new button command.
    void reset(bool down = false) {
        have_edge_ = false;
        pressed_ = false;
        blocked_ = down;
    }

private:
    clock_ms last_edge_{0};
    clock_ms since_{0};
    bool have_edge_ = false;
    bool pressed_ = false;
    bool blocked_ = false;
};

} // namespace scale
