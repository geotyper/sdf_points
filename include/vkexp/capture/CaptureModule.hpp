#pragma once

#include "vkexp/core/Module.hpp"
#include "vkexp/core/VulkanResource.hpp"
#include "vkexp/profiling/ProfilerTypes.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace vkexp {

struct DemoState;
class Profiler;

struct CaptureOptions {
    VkExtent2D resolution{}; // {0, 0}: the window framebuffer size
    std::uint32_t fps{60};
};

// Copies the scene render target (never the ImGui swapchain image) into a
// capture image of a fixed size, then reads it back through a ring of
// host-visible buffers without stalling the frame loop.
class CaptureModule final : public Module {
public:
    CaptureModule(DemoState& state, Profiler& profiler, CaptureOptions options = {});

    void onAttach(AppContext& context) override;
    void onFrameBegin(AppContext& context, const FrameInfo& frame) override;
    void onRender(AppContext& context, const FrameInfo& frame) override;
    void onDetach(AppContext& context) override;

    void setActive(bool active) { requestedActive_ = active; }

private:
    // A frame is read back once its fence has signalled. With one frame in flight
    // that is the previous frame; three slots also cover two frames in flight.
    static constexpr std::size_t stagingSlotCount = 3;

    struct StagingSlot {
        BufferResource buffer;
        void* mapped{};
        bool coherent{};
        bool busy{};
        std::uint64_t serial{};
    };

    [[nodiscard]] VkExtent2D captureExtent(const AppContext& context) const;
    [[nodiscard]] bool anySlotBusy() const;
    void ensureTargets(AppContext& context, VkExtent2D extent);
    void destroyTargets(VkDevice device);
    void recordCopy(VkCommandBuffer commands, StagingSlot& slot);
    void drainCompleted(AppContext& context);
    void consumeSlot(AppContext& context, StagingSlot& slot);

    DemoState& state_;
    CaptureOptions options_;
    ProfileMetricId copyMetric_{invalidProfileMetric};
    VkFormat format_{VK_FORMAT_UNDEFINED};
    ImageResource target_;
    std::array<StagingSlot, stagingSlotCount> ring_{};
    std::size_t nextSlot_{};
    std::uint64_t framesRead_{};
    bool requestedActive_{};
    bool active_{};
};

} // namespace vkexp
