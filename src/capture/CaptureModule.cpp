#include "vkexp/capture/CaptureModule.hpp"

#include "vkexp/core/FrameClock.hpp"
#include "vkexp/core/VulkanContext.hpp"
#include "vkexp/core/Window.hpp"
#include "vkexp/demo/DemoState.hpp"
#include "vkexp/profiling/Profiler.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
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
    // Same 64..4096 limits as the GraphicsModule target.
    const VkExtent2D framebuffer = context.vulkan.extent();
    const CaptureResolution resolved = resolveCaptureSize(
        options_.size, {framebuffer.width, framebuffer.height},
        {state_.viewport.requestedWidth, state_.viewport.requestedHeight});
    return {resolved.width, resolved.height};
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
                   // SAMPLED only because ImageResource always creates a view.
                   ImageResourceConfig{extent, format_,
                                       VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                           VK_IMAGE_USAGE_SAMPLED_BIT});
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

void CaptureModule::onFrameBegin(AppContext& context, const FrameInfo& frame) {
    context.profiler.cpu().addDuration(writeMetric_, writer_.takeWriteMilliseconds());
    // Wait until the UI has laid out once so the Viewport panel size is known.
    if (!automationStarted_ && frame.frameNumber >= 2 &&
        (options_.exitAfterFrames > 0 || options_.exitAfterScreenshot)) {
        automationStarted_ = true;
        toggleRequested_ = options_.exitAfterFrames > 0;
        screenshotRequested_ = options_.exitAfterScreenshot;
    }
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

void CaptureModule::onFrameEnd(AppContext& context, const FrameInfo&) {
    const bool videoInFlight = std::any_of(ring_.begin(), ring_.end(), [](const StagingSlot& slot) {
        return slot.busy && slot.video;
    });
    if (phase_ == Phase::Finishing && !videoInFlight) {
        writer_.closeVideo();
        phase_ = Phase::Idle;
        std::cout << "[capture] Stopped after " << framesRecorded_
                  << " frames; ffmpeg finishes in the background\n";
    }
    handleAutomation(context);
}

void CaptureModule::handleAutomation(AppContext& context) {
    if (!automationStarted_) {
        return;
    }
    if (phase_ == Phase::Recording && options_.exitAfterFrames > 0 &&
        framesRecorded_ >= options_.exitAfterFrames) {
        stopRecording(context);
    }
    const bool videoDone =
        options_.exitAfterFrames == 0 || (phase_ == Phase::Idle && !toggleRequested_);
    const bool screenshotDone = !options_.exitAfterScreenshot || screenshotsQueued_ > 0;
    if (videoDone && screenshotDone) {
        automationFailed_ = automationFailed_ || !error_.empty();
        context.window.requestClose();
    }
}

void CaptureModule::onUpdate(AppContext& context, const FrameInfo&) {
    // io.WantCaptureKeyboard is also true whenever keyboard navigation has a
    // focused window (NavEnableKeyboard), i.e. nearly always in this UI-only
    // window. Yield the hotkeys only while a widget is actually being edited.
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) {
            toggleRecording();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F10, false)) {
            requestScreenshot();
        }
    }
    drawPanel(context);
}

void CaptureModule::drawSizeCombo(const AppContext& context) {
    const auto label = [&](const CaptureSize& size) {
        const CaptureResolution framebuffer{context.vulkan.extent().width,
                                            context.vulkan.extent().height};
        const CaptureResolution viewport{state_.viewport.requestedWidth,
                                         state_.viewport.requestedHeight};
        const CaptureResolution resolved = resolveCaptureSize(size, framebuffer, viewport);
        std::array<char, 48> text{};
        switch (size.mode) {
        case CaptureSize::Mode::Framebuffer:
            std::snprintf(text.data(), text.size(), "Framebuffer (%ux%u)", resolved.width,
                          resolved.height);
            break;
        case CaptureSize::Mode::Viewport:
            std::snprintf(text.data(), text.size(), "Viewport x%u (%ux%u)", size.viewportScale,
                          resolved.width, resolved.height);
            break;
        case CaptureSize::Mode::Fixed:
            std::snprintf(text.data(), text.size(), "%ux%u", resolved.width, resolved.height);
            break;
        }
        return text;
    };
    const auto fixed = [](const std::uint32_t width, const std::uint32_t height) {
        return CaptureSize{CaptureSize::Mode::Fixed, {width, height}, 1};
    };
    const auto same = [](const CaptureSize& left, const CaptureSize& right) {
        return left.mode == right.mode &&
               (left.mode != CaptureSize::Mode::Fixed ||
                (left.fixed.width == right.fixed.width && left.fixed.height == right.fixed.height)) &&
               (left.mode != CaptureSize::Mode::Viewport ||
                left.viewportScale == right.viewportScale);
    };
    const auto option = [&](const CaptureSize& size) {
        if (ImGui::Selectable(label(size).data(), !customSize_ && same(size, options_.size))) {
            options_.size = size;
            customSize_ = false;
        }
    };

    const auto preview = customSize_ ? std::array<char, 48>{"Custom"} : label(options_.size);
    if (ImGui::BeginCombo("Resolution", preview.data())) {
        option(CaptureSize{});
        option(CaptureSize{CaptureSize::Mode::Viewport, {}, 1});
        option(CaptureSize{CaptureSize::Mode::Viewport, {}, 2});
        ImGui::SeparatorText("16:9");
        option(fixed(1280, 720));
        option(fixed(1920, 1080));
        option(fixed(2560, 1440));
        option(fixed(3840, 2160));
        ImGui::SeparatorText("Square");
        option(fixed(640, 640));
        option(fixed(1080, 1080));
        option(fixed(1280, 1280));
        option(fixed(2048, 2048));
        ImGui::Separator();
        if (ImGui::Selectable("Custom", customSize_)) {
            customSize_ = true;
        }
        ImGui::EndCombo();
    }
    if (customSize_) {
        std::array<int, 2> custom{static_cast<int>(customResolution_.width),
                                  static_cast<int>(customResolution_.height)};
        if (ImGui::InputInt2("Width x height", custom.data())) {
            customResolution_ = {static_cast<std::uint32_t>(std::clamp(custom[0], 64, 4096)),
                                 static_cast<std::uint32_t>(std::clamp(custom[1], 64, 4096))};
        }
        options_.size = fixed(customResolution_.width, customResolution_.height);
    }
}

void CaptureModule::drawPanel(const AppContext& context) {
    ImGui::SetNextWindowPos(ImVec2(20.0F, 650.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350.0F, 320.0F), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Capture")) {
        ImGui::End();
        return;
    }
    const CaptureWriter::Status writer = writer_.status();
    const double seconds =
        static_cast<double>(framesRecorded_) / static_cast<double>(options_.fps);
    const int minutes = static_cast<int>(seconds / 60.0);
    switch (phase_) {
    case Phase::Recording:
        ImGui::TextColored(ImVec4(1.0F, 0.22F, 0.18F, 1.0F), "REC");
        ImGui::SameLine();
        ImGui::Text("frame %llu  %02d:%05.2f", static_cast<unsigned long long>(framesRecorded_),
                    minutes, seconds - minutes * 60.0);
        break;
    case Phase::Finishing:
        ImGui::TextUnformatted("Finishing...");
        break;
    case Phase::Idle:
        ImGui::TextDisabled("Idle");
        break;
    }

    ImGui::SeparatorText("Settings");
    ImGui::BeginDisabled(phase_ != Phase::Idle);
    drawSizeCombo(context);
    constexpr std::array<std::uint32_t, 5> rates{24, 30, 50, 60, 120};
    if (ImGui::BeginCombo("FPS", std::to_string(options_.fps).c_str())) {
        for (const std::uint32_t rate : rates) {
            if (ImGui::Selectable(std::to_string(rate).c_str(), rate == options_.fps)) {
                options_.fps = rate;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::BeginCombo("Codec", videoCodecName(options_.codec).data())) {
        for (const VideoCodec codec : {VideoCodec::Hevc, VideoCodec::ProRes, VideoCodec::H264}) {
            if (ImGui::Selectable(videoCodecName(codec).data(), codec == options_.codec)) {
                options_.codec = codec;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    const VkExtent2D output = phase_ == Phase::Idle ? captureExtent(context) : target_.extent();
    ImGui::Text("Output: %ux%u (%.3g:1) @ %u fps, %s", output.width, output.height,
                static_cast<double>(output.width) / static_cast<double>(output.height),
                options_.fps, videoFileExtension(options_.codec).data());

    ImGui::SeparatorText("Controls");
    ImGui::BeginDisabled(phase_ == Phase::Finishing);
    if (ImGui::Button(phase_ == Phase::Recording ? "Stop (F9)" : "Record (F9)")) {
        toggleRecording();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Screenshot (F10)")) {
        requestScreenshot();
    }
    ImGui::Text("Writer queue: %zu / %zu", writer.queueDepth, writer.queueCapacity);
    ImGui::ProgressBar(static_cast<float>(writer.queueDepth) /
                           static_cast<float>(std::max<std::size_t>(writer.queueCapacity, 1)),
                       ImVec2(-1.0F, 0.0F), "");
    if (backpressureFrames_ > 0) {
        ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.2F, 1.0F), "Encoder backpressure: %llu frames",
                           static_cast<unsigned long long>(backpressureFrames_));
    }
    if (!writer.lastFile.empty()) {
        ImGui::TextUnformatted("Last file:");
        ImGui::TextWrapped("%s", writer.lastFile.string().c_str());
    }
    const std::string& error = !error_.empty() ? error_ : writer.error;
    if (!error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.35F, 0.3F, 1.0F));
        ImGui::TextWrapped("%s", error.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::End();
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
        ++screenshotsQueued_;
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
    if (automationStarted_) {
        const CaptureWriter::Status status = writer_.status();
        automationFailed_ = automationFailed_ || status.failed || !status.error.empty();
    }
    state_.viewport.lockedExtent.reset();
    destroyTargets(context.vulkan.device());
}

} // namespace vkexp
