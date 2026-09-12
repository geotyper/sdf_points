#pragma once

#include "vkexp/core/Module.hpp"
#include "vkexp/core/VulkanResource.hpp"
#include "vkexp/profiling/ProfilerTypes.hpp"

namespace vkexp {

class DemoState;
class Profiler;

class GraphicsModule final : public Module {
public:
    GraphicsModule(DemoState& state, Profiler& profiler);

    void onAttach(AppContext& context) override;
    void onUpdate(AppContext& context, const FrameInfo& frame) override;
    void onRender(AppContext& context, const FrameInfo& frame) override;
    void onDetach(AppContext& context) override;

private:
    static constexpr VkFormat targetFormat = VK_FORMAT_R8G8B8A8_UNORM;

    void createRenderTarget(AppContext& context, VkExtent2D extent);
    void destroyRenderTarget();

    DemoState& state_;
    ProfileMetricId metric_{invalidProfileMetric};
    UniqueDescriptorSetLayout depthSetLayout_;
    UniqueDescriptorPool depthPool_;
    VkDescriptorSet depthSet_{};
    UniquePipelineLayout pipelineLayout_;
    UniquePipeline pipeline_;
    UniquePipeline surfacePipeline_;
    ImageResource target_;
    ImageResource depth_;
    VkFormat depthFormat_{VK_FORMAT_UNDEFINED};
    VkImageLayout depthLayout_{VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageLayout targetLayout_{VK_IMAGE_LAYOUT_UNDEFINED};
    float weaveTime_{};
    float rotationTime_{};
    float nestedSphereTime_{};
};

} // namespace vkexp
