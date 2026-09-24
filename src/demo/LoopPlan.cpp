#include "vkexp/demo/LoopPlan.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vkexp {
namespace {

constexpr double tau = 6.28318530717958647692;
constexpr double maximumLoopSeconds = 600.0;

LoopPlan invalid(std::string problem) {
    LoopPlan plan;
    plan.problem = std::move(problem);
    return plan;
}

} // namespace

bool loopDriverAvailable(const int geometryMode, const LoopDriver driver, const bool autoRotate) {
    switch (driver) {
    case LoopDriver::Wave:
        return geometryMode >= 0 && geometryMode <= 2;
    case LoopDriver::Flow:
        return true;
    case LoopDriver::Rotation:
        return geometryMode != 4 && autoRotate;
    }
    return false;
}

LoopDriver defaultLoopDriver(const int geometryMode) {
    return geometryMode <= 2 ? LoopDriver::Wave : LoopDriver::Flow;
}

std::string_view loopDriverLabel(const int geometryMode, const LoopDriver driver) {
    switch (driver) {
    case LoopDriver::Wave:
        return "Wave laps";
    case LoopDriver::Flow:
        if (geometryMode == 4) {
            return "Sphere turns";
        }
        return geometryMode == 3 ? "Orbit cycles" : "Braid cycles";
    case LoopDriver::Rotation:
        return "Object rotations";
    }
    return "";
}

LoopPlan planLoop(const LoopInputs& inputs) {
    if (!loopDriverAvailable(inputs.geometryMode, inputs.driver, inputs.autoRotate)) {
        return invalid(std::string{loopDriverLabel(inputs.geometryMode, inputs.driver)} +
                       " are not available for this geometry");
    }
    if (inputs.cycles < 1) {
        return invalid("The loop needs at least one cycle");
    }
    if (inputs.paused) {
        return invalid("Animation is paused");
    }
    if (!(inputs.animationSpeed > 0.0)) {
        return invalid("Animation speed is zero");
    }

    const double cycles = static_cast<double>(inputs.cycles);
    const bool rotates = inputs.geometryMode != 4 && inputs.autoRotate;
    // Object rotation measured per unit of flow phase.
    const double rotationRate = rotates ? inputs.rotationSpeed / inputs.animationSpeed : 0.0;
    double phaseCycles = cycles;
    switch (inputs.driver) {
    case LoopDriver::Wave:
        if (!(inputs.releaseSpeed > 0.0)) {
            return invalid("Wave travel speed is zero");
        }
        phaseCycles = cycles / inputs.releaseSpeed;
        break;
    case LoopDriver::Flow:
        break;
    case LoopDriver::Rotation:
        if (rotationRate == 0.0) {
            return invalid("Rotation speed is zero");
        }
        phaseCycles = cycles / std::abs(rotationRate);
        break;
    }

    LoopPlan plan;
    plan.phaseCycles = phaseCycles;
    plan.phasePeriod = tau * phaseCycles;
    plan.durationSeconds = plan.phasePeriod / inputs.animationSpeed;
    if (plan.durationSeconds > maximumLoopSeconds) {
        char message[96]{};
        std::snprintf(message, sizeof(message), "The loop would last %.0f s (limit %.0f s)",
                      plan.durationSeconds, maximumLoopSeconds);
        return invalid(message);
    }
    // Whole turns over the loop: k turns take phaseCycles * 2*pi of flow phase.
    const double turns = inputs.driver == LoopDriver::Rotation
                             ? std::copysign(cycles, rotationRate)
                             : std::round(rotationRate * phaseCycles);
    plan.rotationPerPhase = turns / phaseCycles;
    plan.valid = true;
    return plan;
}

std::uint64_t loopFrameCount(const LoopPlan& plan, const double fps) {
    return static_cast<std::uint64_t>(std::max(2.0, std::round(plan.durationSeconds * fps)));
}

} // namespace vkexp
