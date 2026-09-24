#pragma once

#include "vkexp/core/Module.hpp"
#include "vkexp/core/VulkanResource.hpp"
#include "vkexp/profiling/ProfilerTypes.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace vkexp {

struct DemoState;
class Profiler;

struct CaptureOptions {
    VkExtent2D resolution{}; // {0, 0}: the window framebuffer size
    std::uint32_t fps{60};
};

// Copies the scene render target (never the ImGui swapchain image) into a
// capture image of a fixed size.
class CaptureModule final : public Module {
public:
    CaptureModule(DemoState& state, Profiler& profiler, CaptureOptions options = {});

    void onAttach(AppContext& context) override;
    void onFrameBegin(AppContext& context, const FrameInfo& frame) override;
    void onRender(AppContext& context, const FrameInfo& frame) override;
    void onDetach(AppContext& context) override;

    void setActive(bool active) { requestedActive_ = active; }

private:
    [[nodiscard]] VkExtent2D captureExtent(const AppContext& context) const;
    void ensureTarget(AppContext& context, VkExtent2D extent);
    void recordBlit(VkCommandBuffer commands);

    DemoState& state_;
    CaptureOptions options_;
    ProfileMetricId copyMetric_{invalidProfileMetric};
    VkFormat format_{VK_FORMAT_UNDEFINED};
    ImageResource target_;
    bool requestedActive_{};
    bool active_{};
};

} // namespace vkexp
