#pragma once

/// Pourover timer. Time base is injected (`now` arguments) so it is fully
/// testable on the host and free of a particular clock source on target.

#include "scale/clock.hpp"

namespace scale {

class brew_timer {
public:
    enum class state { idle, running, paused };

    void start(clock_ms now) {
        acc_     = clock_ms{0};
        started_ = now;
        state_   = state::running;
    }

    void pause(clock_ms now) {
        if (state_ != state::running) {
            return;
        }
        acc_ += now - started_;
        state_ = state::paused;
    }

    void resume(clock_ms now) {
        if (state_ != state::paused) {
            return;
        }
        started_ = now;
        state_   = state::running;
    }

    /// idle -> start, running -> pause, paused -> resume.
    void toggle(clock_ms now) {
        switch (state_) {
            case state::idle:    start(now); break;
            case state::running: pause(now); break;
            case state::paused:  resume(now); break;
        }
    }

    void reset() {
        state_ = state::idle;
        acc_   = clock_ms{0};
    }

    [[nodiscard]] clock_ms elapsed(clock_ms now) const {
        return acc_ + (state_ == state::running ? now - started_ : clock_ms{0});
    }
    [[nodiscard]] state current() const { return state_; }

private:
    state    state_{state::idle};
    clock_ms started_{0};
    clock_ms acc_{0};
};

} // namespace scale
