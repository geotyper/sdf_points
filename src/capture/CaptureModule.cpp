#include "vkexp/capture/CaptureModule.hpp"

#include "vkexp/core/FrameClock.hpp"
#include "vkexp/core/VulkanContext.hpp"
#include "vkexp/demo/DemoState.hpp"
#include "vkexp/profiling/Profiler.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <initializer_list>
#include <iostream>
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

// Prefer cached memory: CPU reads from uncached (write-combined) memory are slow.
BufferResource createStagingBuffer(const VkPhysicalDevice physicalDevice, const VkDevice device,
                                   const VkDeviceSize size) {
    constexpr std::array preferences{
        VkMemoryPropertyFlags{VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                              VK_MEMORY_PROPERTY_HOST_CACHED_BIT},
        VkMemoryPropertyFlags{VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_CACHED_BIT},
        VkMemoryPropertyFlags{VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT},
    };
    for (const VkMemoryPropertyFlags properties : preferences) {
        try {
            BufferResource buffer;
            buffer.create(physicalDevice, device,
                          BufferResourceConfig{size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, properties});
            return buffer;
        } catch (const std::runtime_error&) {
            // Try the next memory type preference.
        }
    }
    throw std::runtime_error("No host-visible memory available for capture readback");
}

} // namespace

CaptureModule::CaptureModule(DemoState& state, Profiler& profiler, CaptureOptions options)
    : state_(state), options_(std::move(options)),
      copyMetric_(profiler.registerMetric("Capture copy")),
      writeMetric_(profiler.registerMetric("Capture write")),
      waitMetric_(profiler.registerMetric("Capture wait")), writer_(options_.writerQueueCapacity) {}

void CaptureModule::onAttach(AppContext& context) {
    format_ = selectCaptureFormat(context.vulkan.physicalDevice(), context.vulkan.colorFormat());
}

VkExtent2D CaptureModule::captureExtent(const AppContext& context) const {
    VkExtent2D extent{options_.resolution.width, options_.resolution.height};
    if (extent.width == 0 || extent.height == 0) {
        extent = context.vulkan.extent();
    }
    // Matches the GraphicsModule target limits; even sizes keep 4:2:0 encoders happy.
    extent.width = std::clamp(extent.width, 64U, 4096U) & ~1U;
    extent.height = std::clamp(extent.height, 64U, 4096U) & ~1U;
    return extent;
}

bool CaptureModule::anySlotBusy() const {
    return std::any_of(ring_.begin(), ring_.end(),
                       [](const StagingSlot& slot) { return slot.busy; });
}

void CaptureModule::ensureTargets(AppContext& context, const VkExtent2D extent) {
    if (target_ && target_.extent().width == extent.width &&
        target_.extent().height == extent.height) {
        return;
    }
    if (anySlotBusy()) {
        throw std::logic_error("Capture targets cannot be resized while a readback is pending");
    }
    const VkDevice device = context.vulkan.device();
    destroyTargets(device);
    target_.create(context.vulkan.physicalDevice(), device,
                   ImageResourceConfig{extent, format_,
                                       VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    const VkDeviceSize frameBytes = VkDeviceSize{extent.width} * extent.height * 4U;
    for (StagingSlot& slot : ring_) {
        slot.buffer = createStagingBuffer(context.vulkan.physicalDevice(), device, frameBytes);
        slot.coherent =
            (slot.buffer.memoryProperties() & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0U;
        if (vkMapMemory(device, slot.buffer.memory(), 0, VK_WHOLE_SIZE, 0, &slot.mapped) !=
            VK_SUCCESS) {
            throw std::runtime_error("Unable to map capture readback memory");
        }
    }
    nextSlot_ = 0;
}

void CaptureModule::destroyTargets(const VkDevice device) {
    for (StagingSlot& slot : ring_) {
        if (slot.mapped != nullptr) {
            vkUnmapMemory(device, slot.buffer.memory());
        }
        slot = StagingSlot{};
    }
    target_.reset();
}

RawPixelFormat CaptureModule::pixelFormat() const {
    return format_ == VK_FORMAT_B8G8R8A8_SRGB || format_ == VK_FORMAT_B8G8R8A8_UNORM
               ? RawPixelFormat::Bgra
               : RawPixelFormat::Rgba;
}

void CaptureModule::onFrameBegin(AppContext& context, const FrameInfo&) {
    context.profiler.cpu().addDuration(writeMetric_, writer_.takeWriteMilliseconds());
    if (phase_ == Phase::Recording) {
        if (const auto status = writer_.status(); status.failed) {
            error_ = status.error;
            stopRecording(context);
        }
    }
    if (toggleRequested_) {
        if (phase_ == Phase::Recording) {
            toggleRequested_ = false;
            stopRecording(context);
        } else if (phase_ == Phase::Idle && !anySlotBusy()) {
            // While Finishing, the request waits until the previous video is flushed.
            toggleRequested_ = false;
            startRecording(context);
        }
    }
    if (screenshotRequested_ && !screenshotPending_ &&
        (phase_ == Phase::Recording || !anySlotBusy())) {
        screenshotRequested_ = false;
        if (phase_ != Phase::Recording) {
            ensureTargets(context, captureExtent(context));
        }
        screenshotPending_ = true;
    }
    // Applied by GraphicsModule::onUpdate later in this frame.
    if (phase_ == Phase::Recording || screenshotPending_) {
        state_.viewport.lockedExtent = target_.extent();
    } else {
        state_.viewport.lockedExtent.reset();
    }
}

void CaptureModule::startRecording(AppContext& context) {
    error_.clear();
    const auto ffmpeg = findFfmpeg();
    if (!ffmpeg) {
        error_ = "ffmpeg not found in PATH (install it: brew install ffmpeg / apt install ffmpeg)";
        std::cerr << "[capture] " << error_ << '\n';
        return;
    }
    std::error_code directoryError;
    std::filesystem::create_directories(options_.outputDirectory, directoryError);
    if (directoryError) {
        error_ = "Unable to create " + options_.outputDirectory.string() + ": " +
                 directoryError.message();
        std::cerr << "[capture] " << error_ << '\n';
        return;
    }

    const VkExtent2D extent = captureExtent(context);
    ensureTargets(context, extent);
    videoPath_ = uniqueCapturePath(options_.outputDirectory, localTimeNow(),
                                   videoFileExtension(options_.codec));
    const VideoEncodeSettings settings{
        extent.width, extent.height, options_.fps, pixelFormat(), options_.codec, videoPath_,
    };
    if (!writer_.openVideo(shellCommand(*ffmpeg, ffmpegArguments(settings)), videoPath_, error_)) {
        std::cerr << "[capture] " << error_ << '\n';
        return;
    }
    std::cout << "[capture] Recording " << extent.width << 'x' << extent.height << " @ "
              << options_.fps << " fps (" << videoCodecName(options_.codec) << ") -> "
              << videoPath_.string() << '\n';
    phase_ = Phase::Recording;
    framesRecorded_ = 0;
    backpressureFrames_ = 0;
    // Every later frame advances the animation by exactly one video frame.
    context.clock.setFixedStep(1.0 / static_cast<double>(options_.fps));
}

void CaptureModule::stopRecording(AppContext& context) {
    context.clock.clearFixedStep();
    if (phase_ == Phase::Recording) {
        // The last frames are still in the readback ring; onFrameEnd closes the
        // pipe once they have been handed to the writer.
        phase_ = Phase::Finishing;
    }
}

void CaptureModule::onRender(AppContext& context, const FrameInfo&) {
    drainCompleted(context);
    const bool video = phase_ == Phase::Recording;
    const bool screenshot = screenshotPending_;
    if ((!video && !screenshot) || state_.viewport.image == VK_NULL_HANDLE ||
        state_.viewport.extent.width != target_.extent().width ||
        state_.viewport.extent.height != target_.extent().height) {
        return;
    }

    auto cpuScope = context.profiler.cpu().scope(copyMetric_);
    StagingSlot& slot = ring_[nextSlot_];
    if (slot.busy) {
        // Unreachable while fewer than stagingSlotCount frames are in flight.
        std::cerr << "[capture] Readback ring exhausted; waiting for the GPU\n";
        context.vulkan.waitIdle();
        drainCompleted(context);
    }
    auto gpuScope = context.profiler.gpu().scope(context.vulkan.commandBuffer(), copyMetric_);
    recordCopy(context.vulkan.commandBuffer(), slot);
    slot.busy = true;
    slot.video = video;
    slot.screenshot = screenshot;
    slot.serial = context.vulkan.frameSerial();
    nextSlot_ = (nextSlot_ + 1) % ring_.size();
    screenshotPending_ = false;
    if (video) {
        ++framesRecorded_;
    }
}

void CaptureModule::onFrameEnd(AppContext&, const FrameInfo&) {
    const bool videoInFlight = std::any_of(ring_.begin(), ring_.end(), [](const StagingSlot& slot) {
        return slot.busy && slot.video;
    });
    if (phase_ == Phase::Finishing && !videoInFlight) {
        writer_.closeVideo();
        phase_ = Phase::Idle;
        std::cout << "[capture] Stopped after " << framesRecorded_
                  << " frames; ffmpeg finishes in the background\n";
    }
}

void CaptureModule::recordCopy(const VkCommandBuffer commands, StagingSlot& slot) {
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

    VkBufferImageCopy2 copyRegion{VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2};
    copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.imageExtent = {destination.width, destination.height, 1};
    VkCopyImageToBufferInfo2 copy{VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2};
    copy.srcImage = target_.image();
    copy.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    copy.dstBuffer = slot.buffer.buffer();
    copy.regionCount = 1;
    copy.pRegions = &copyRegion;
    vkCmdCopyImageToBuffer2(commands, &copy);

    // Make the copy visible to host reads once the frame fence has signalled.
    VkBufferMemoryBarrier2 toHost{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    toHost.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    toHost.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toHost.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    toHost.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHost.buffer = slot.buffer.buffer();
    toHost.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &toHost;
    vkCmdPipelineBarrier2(commands, &dependency);
}

void CaptureModule::drainCompleted(AppContext& context) {
    const std::uint64_t completed = context.vulkan.completedFrameSerial();
    // Consume in submission order so video frames stay ordered.
    for (;;) {
        StagingSlot* oldest = nullptr;
        for (StagingSlot& slot : ring_) {
            if (slot.busy && slot.serial <= completed &&
                (oldest == nullptr || slot.serial < oldest->serial)) {
                oldest = &slot;
            }
        }
        if (oldest == nullptr) {
            return;
        }
        consumeSlot(context, *oldest);
        oldest->busy = false;
    }
}

void CaptureModule::consumeSlot(AppContext& context, StagingSlot& slot) {
    if (!slot.coherent) {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = slot.buffer.memory();
        range.size = VK_WHOLE_SIZE;
        vkInvalidateMappedMemoryRanges(context.vulkan.device(), 1, &range);
    }
    using Clock = std::chrono::steady_clock;
    const VkExtent2D extent = target_.extent();
    const std::size_t bytes = std::size_t{extent.width} * extent.height * 4U;
    const auto copyOut = [&]() {
        const auto started = Clock::now();
        CaptureWriter::Pixels pixels = writer_.acquireBuffer(bytes);
        std::memcpy(pixels.data(), slot.mapped, bytes);
        context.profiler.cpu().addDuration(
            copyMetric_,
            std::chrono::duration<double, std::milli>(Clock::now() - started).count());
        return pixels;
    };
    if (slot.video) {
        const double waitedMs = writer_.pushVideoFrame(copyOut());
        context.profiler.cpu().addDuration(waitMetric_, waitedMs);
        noteBackpressure(waitedMs);
    }
    if (slot.screenshot) {
        std::error_code directoryError;
        std::filesystem::create_directories(options_.outputDirectory, directoryError);
        const double waitedMs = writer_.pushScreenshot(
            copyOut(), extent.width, extent.height, pixelFormat(),
            uniqueCapturePath(options_.outputDirectory, localTimeNow(), ".png"));
        context.profiler.cpu().addDuration(waitMetric_, waitedMs);
    }
}

void CaptureModule::noteBackpressure(const double waitedMs) {
    if (waitedMs <= 0.0) {
        return;
    }
    // Never drop frames: the frame loop waits for the encoder instead.
    ++backpressureFrames_;
    if (backpressureFrames_ == 1 || backpressureFrames_ % 120 == 0) {
        std::cerr << "[capture] Warning: writer queue full (" << writer_.status().queueCapacity
                  << " frames); rendering waits for ffmpeg (" << backpressureFrames_
                  << " frames delayed so far)\n";
    }
}

void CaptureModule::onDetach(AppContext& context) {
    // Application waits for the device before detaching, so every slot is complete.
    drainCompleted(context);
    if (phase_ != Phase::Idle) {
        writer_.closeVideo();
        phase_ = Phase::Idle;
    }
    context.clock.clearFixedStep();
    writer_.shutdown(); // flushes the queue and waits for ffmpeg to finish the file
    state_.viewport.lockedExtent.reset();
    destroyTargets(context.vulkan.device());
}

} // namespace vkexp
