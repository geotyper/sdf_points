#pragma once

#include <chrono>
#include <optional>
#include <stdexcept>

namespace vkexp {

// Source of per-frame simulation time. Real (steady_clock) time by default; a
// fixed step, e.g. 1/60 s while recording video, makes the animation
// independent of how long each frame really took.
class FrameClock {
public:
    using Clock = std::chrono::steady_clock;

    struct Tick {
        double deltaSeconds{};
        double elapsedSeconds{};
        double realDeltaSeconds{};
    };

    void start(const Clock::time_point now) {
        previous_ = now;
        elapsed_ = 0.0;
    }

    [[nodiscard]] Tick tick(const Clock::time_point now) {
        const double real = std::chrono::duration<double>(now - previous_).count();
        previous_ = now;
        const double delta = fixedStep_.value_or(real);
        elapsed_ += delta;
        return {delta, elapsed_, real};
    }

    void setFixedStep(const double seconds) {
        if (!(seconds > 0.0)) {
            throw std::invalid_argument("Fixed time step must be positive");
        }
        fixedStep_ = seconds;
    }
    void clearFixedStep() { fixedStep_.reset(); }
    [[nodiscard]] std::optional<double> fixedStep() const { return fixedStep_; }

private:
    Clock::time_point previous_{};
    double elapsed_{};
    std::optional<double> fixedStep_;
};

} // namespace vkexp
