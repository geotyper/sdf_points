#include "vkexp/capture/CaptureModule.hpp"

#include "vkexp/core/VulkanContext.hpp"
#include "vkexp/demo/DemoState.hpp"
#include "vkexp/profiling/Profiler.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <stdexcept>
#include <utility>

namespace vkexp {
namespace {

bool isEightBitColor(const VkFormat format) {
    return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_B8G8R8A8_UNORM ||
           format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_R8G8B8A8_UNORM;
}

// The scene target is UNORM and is shown through the swapchain, which may be
// sRGB. Blitting into a capture image of the swapchain's format applies the same
// encoding, so the video matches what is on screen.
VkFormat selectCaptureFormat(const VkPhysicalDevice physicalDevice, const VkFormat swapchain) {
    std::array candidates{swapchain, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB,
                          VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
    constexpr VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    for (const VkFormat format : candidates) {
        if (!isEightBitColor(format)) {
            continue;
        }
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
        if ((properties.optimalTilingFeatures & required) == required) {
            return format;
        }
    }
    throw std::runtime_error("No 8-bit colour format supports blit and copy for capture");
}

VkImageMemoryBarrier2 imageBarrier(const VkImage image, const VkImageLayout oldLayout,
                                   const VkImageLayout newLayout,
                                   const VkPipelineStageFlags2 sourceStage,
                                   const VkAccessFlags2 sourceAccess,
                                   const VkPipelineStageFlags2 destinationStage,
                                   const VkAccessFlags2 destinationAccess) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = sourceStage;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstStageMask = destinationStage;
    barrier.dstAccessMask = destinationAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return barrier;
}

void pipelineBarrier(const VkCommandBuffer commands,
                     const std::initializer_list<VkImageMemoryBarrier2> barriers) {
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
    dependency.pImageMemoryBarriers = barriers.begin();
    vkCmdPipelineBarrier2(commands, &dependency);
}

} // namespace

CaptureModule::CaptureModule(DemoState& state, Profiler& profiler, CaptureOptions options)
    : state_(state), options_(options), copyMetric_(profiler.registerMetric("Capture copy")) {}

void CaptureModule::onAttach(AppContext& context) {
    format_ = selectCaptureFormat(context.vulkan.physicalDevice(), context.vulkan.colorFormat());
}

VkExtent2D CaptureModule::captureExtent(const AppContext& context) const {
    VkExtent2D extent = options_.resolution;
    if (extent.width == 0 || extent.height == 0) {
        extent = context.vulkan.extent();
    }
    // Matches the GraphicsModule target limits; even sizes keep 4:2:0 encoders happy.
    extent.width = std::clamp(extent.width, 64U, 4096U) & ~1U;
    extent.height = std::clamp(extent.height, 64U, 4096U) & ~1U;
    return extent;
}

void CaptureModule::ensureTarget(AppContext& context, const VkExtent2D extent) {
    if (target_ && target_.extent().width == extent.width &&
        target_.extent().height == extent.height) {
        return;
    }
    target_.create(context.vulkan.physicalDevice(), context.vulkan.device(),
                   ImageResourceConfig{extent, format_,
                                       VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
}

void CaptureModule::onFrameBegin(AppContext& context, const FrameInfo&) {
    if (requestedActive_ == active_) {
        return;
    }
    active_ = requestedActive_;
    if (active_) {
        const VkExtent2D extent = captureExtent(context);
        ensureTarget(context, extent);
        // Applied by GraphicsModule::onUpdate later in this frame.
        state_.viewport.lockedExtent = extent;
    } else {
        state_.viewport.lockedExtent.reset();
    }
}

void CaptureModule::onRender(AppContext& context, const FrameInfo&) {
    if (!active_ || state_.viewport.image == VK_NULL_HANDLE ||
        state_.viewport.extent.width != target_.extent().width ||
        state_.viewport.extent.height != target_.extent().height) {
        return;
    }
    auto cpuScope = context.profiler.cpu().scope(copyMetric_);
    auto gpuScope = context.profiler.gpu().scope(context.vulkan.commandBuffer(), copyMetric_);
    recordBlit(context.vulkan.commandBuffer());
}

void CaptureModule::recordBlit(const VkCommandBuffer commands) {
    const VkImage scene = state_.viewport.image;
    // GraphicsModule and ComputeModule leave the scene sampled-readable for the
    // fragment stage (ImGui). Reads need only an execution dependency (WAR).
    pipelineBarrier(
        commands,
        {
            imageBarrier(scene, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         0, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT),
            // The previous contents were fully consumed by an earlier, fenced frame.
            imageBarrier(target_.image(), VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, 0,
                         VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT),
        });

    const VkExtent2D source = state_.viewport.extent;
    const VkExtent2D destination = target_.extent();
    VkImageBlit2 region{VK_STRUCTURE_TYPE_IMAGE_BLIT_2};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[1] = {static_cast<std::int32_t>(source.width),
                            static_cast<std::int32_t>(source.height), 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffsets[1] = {static_cast<std::int32_t>(destination.width),
                            static_cast<std::int32_t>(destination.height), 1};
    VkBlitImageInfo2 blit{VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2};
    blit.srcImage = scene;
    blit.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blit.dstImage = target_.image();
    blit.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blit.regionCount = 1;
    blit.pRegions = &region;
    blit.filter = VK_FILTER_NEAREST; // sizes match; the blit only converts the encoding
    vkCmdBlitImage2(commands, &blit);

    pipelineBarrier(commands,
                    {
                        imageBarrier(scene, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                     VK_PIPELINE_STAGE_2_BLIT_BIT, 0,
                                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                     VK_ACCESS_2_SHADER_SAMPLED_READ_BIT),
                        imageBarrier(target_.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                     VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                     VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT),
                    });
}

void CaptureModule::onDetach(AppContext&) {
    state_.viewport.lockedExtent.reset();
    target_.reset();
}

} // namespace vkexp
