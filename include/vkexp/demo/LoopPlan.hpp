#pragma once

// Loop capture: pick a motion ("driver") and a whole number of its cycles; the
// recording then lasts exactly that long, and every other periodic motion is
// rounded to a whole number of cycles over it, so the clip loops seamlessly.
// Pure logic (no Vulkan) so it can be unit tested.

#include <cstdint>
#include <string>
#include <string_view>

namespace vkexp {

enum class LoopDriver {
    Wave,     // the travelling wave / pulse goes round the ring (braid geometries 0..2)
    Flow,     // the braid / orbit phase, or the reference sphere turn (nested spheres)
    Rotation, // the whole object turns (Auto rotate)
};

struct LoopInputs {
    int geometryMode{};
    LoopDriver driver{LoopDriver::Wave};
    int cycles{1};
    bool paused{};
    double animationSpeed{}; // flow-phase units per second
    double releaseSpeed{};   // wave frequency relative to the flow phase
    bool autoRotate{};
    double rotationSpeed{}; // radians per second
};

struct LoopPlan {
    bool valid{};
    std::string problem;
    // Loop length in the flow phase, in units of 2*pi. Shader frequencies are
    // rounded to multiples of 1 / phaseCycles.
    double phaseCycles{};
    double phasePeriod{}; // = 2*pi*phaseCycles
    double durationSeconds{};
    // Object rotation (radians) per unit of flow phase, rounded to whole turns.
    double rotationPerPhase{};
};

[[nodiscard]] bool loopDriverAvailable(int geometryMode, LoopDriver driver, bool autoRotate);
[[nodiscard]] LoopDriver defaultLoopDriver(int geometryMode);
[[nodiscard]] std::string_view loopDriverLabel(int geometryMode, LoopDriver driver);
[[nodiscard]] LoopPlan planLoop(const LoopInputs& inputs);
// At least two frames; the frame after the last one equals the first.
[[nodiscard]] std::uint64_t loopFrameCount(const LoopPlan& plan, double fps);

} // namespace vkexp
