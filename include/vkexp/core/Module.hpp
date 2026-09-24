#pragma once

#include <cstdint>

namespace vkexp {

class VulkanContext;
class Window;
class Profiler;
class FrameClock;

struct FrameInfo {
    // Simulation time: fixed while FrameClock has a fixed step (video capture).
    float deltaSeconds{};
    float elapsedSeconds{};
    std::uint64_t frameNumber{};
    // Wall-clock time of the last frame, for fps readouts.
    float realDeltaSeconds{};
};

struct AppContext {
    Window& window;
    VulkanContext& vulkan;
    Profiler& profiler;
    FrameClock& clock;
};

class Module {
public:
    virtual ~Module() = default;

    virtual void onAttach(AppContext&) {}
    virtual void onFrameBegin(AppContext&, const FrameInfo&) {}
    virtual void onUpdate(AppContext&, const FrameInfo&) {}
    virtual void onRender(AppContext&, const FrameInfo&) {}
    virtual void onFrameEnd(AppContext&, const FrameInfo&) {}
    virtual void onDetach(AppContext&) {}
};

} // namespace vkexp
