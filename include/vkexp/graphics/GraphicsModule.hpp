#pragma once

#include "vkexp/core/Module.hpp"
#include "vkexp/core/VulkanResource.hpp"
#include "vkexp/demo/Origami.hpp"
#include "vkexp/profiling/ProfilerTypes.hpp"

#include <array>
#include <vector>

namespace vkexp {

struct DemoState;
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
    // Rays times crossings the 4D sections may use.
    static constexpr VkDeviceSize sectionInstanceLimit = 2'000'000;

    // Mirrors the PointEffects uniform block of the point shader (std140).
    struct EffectsBlock {
        float drift[4];
        float shed[4];
        float view[4];
    };

    void createRenderTarget(AppContext& context, VkExtent2D extent);
    void destroyRenderTarget();
    [[nodiscard]] float latticeStep(int pointCount);

    DemoState& state_;
    ProfileMetricId metric_{invalidProfileMetric};
    UniqueDescriptorSetLayout depthSetLayout_;
    UniqueDescriptorPool depthPool_;
    VkDescriptorSet depthSet_{};
    UniquePipelineLayout pipelineLayout_;
    UniquePipeline pipeline_;
    UniquePipeline surfacePipeline_;
    UniquePipeline tubePipeline_; // the skin with colour, for coloured polytope tubes
    // Ray marching of the 4D sections and the Julia set, once per point.
    UniquePipelineLayout sectionLayout_;
    UniquePipeline sectionPipeline_;
    BufferResource sections_;
    ImageResource target_;
    ImageResource depth_;
    BufferResource effects_;
    // Edges of the current 4D polytope: start, end, (colour, part) per edge.
    BufferResource polytope_;
    std::vector<float> polytopeData_;
    std::vector<std::array<int, 2>> polytopeClasses_; // part and direction of every edge
    std::array<int, 3> polytopeKey_{-1, -1, -1};
    int polytopeEdgeCount_{};
    // Origami: the solid's cut, made again when the solid or net changes.
    OrigamiNet origamiNet_;
    std::array<int, 2> origamiKey_{-1, -1};
    double origamiReach_{1.0};
    VkFormat depthFormat_{VK_FORMAT_UNDEFINED};
    VkImageLayout depthLayout_{VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageLayout targetLayout_{VK_IMAGE_LAYOUT_UNDEFINED};
    float weaveTime_{};
    float rotationTime_{};
    float nestedSphereTime_{};
    float eversionTime_{};
    int latticeCount_{};
    int latticeStep_{};
    float loopCycles_{};
};

} // namespace vkexp
