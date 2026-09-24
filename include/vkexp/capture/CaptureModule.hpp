#pragma once

#include "vkexp/capture/CaptureSettings.hpp"
#include "vkexp/capture/CaptureWriter.hpp"
#include "vkexp/core/Module.hpp"
#include "vkexp/core/VulkanResource.hpp"
#include "vkexp/profiling/ProfilerTypes.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace vkexp {

struct DemoState;
class Profiler;

struct CaptureOptions {
    CaptureSize size{};
    std::uint32_t fps{60};
    VideoCodec codec{defaultVideoCodec()};
    std::filesystem::path outputDirectory{"captures"};
    std::size_t writerQueueCapacity{6};
    // Command-line automation: record this many frames at startup, then quit.
    std::uint64_t exitAfterFrames{};
    // Command-line automation: take one screenshot at startup, then quit.
    bool exitAfterScreenshot{};
};

// Copies the scene render target (never the ImGui swapchain image) into a
// capture image of a fixed size, reads it back through a ring of host-visible
// buffers without stalling the frame loop, and hands frames to CaptureWriter.
class CaptureModule final : public Module {
public:
    CaptureModule(DemoState& state, Profiler& profiler, CaptureOptions options = {});

    void onAttach(AppContext& context) override;
    void onFrameBegin(AppContext& context, const FrameInfo& frame) override;
    void onUpdate(AppContext& context, const FrameInfo& frame) override;
    void onRender(AppContext& context, const FrameInfo& frame) override;
    void onFrameEnd(AppContext& context, const FrameInfo& frame) override;
    void onDetach(AppContext& context) override;

    // Requests take effect at the start of the next frame.
    void toggleRecording() { toggleRequested_ = true; }
    void requestScreenshot() { screenshotRequested_ = true; }
    [[nodiscard]] bool recording() const { return phase_ == Phase::Recording; }
    // True when a command-line capture could not produce its file.
    [[nodiscard]] bool automationFailed() const { return automationFailed_; }

private:
    // A frame is read back once its fence has signalled. With one frame in flight
    // that is the previous frame; three slots also cover two frames in flight.
    static constexpr std::size_t stagingSlotCount = 3;

    enum class Phase { Idle, Recording, Finishing };

    struct StagingSlot {
        BufferResource buffer;
        void* mapped{};
        bool coherent{};
        bool busy{};
        bool video{};
        bool screenshot{};
        std::uint64_t serial{};
    };

    [[nodiscard]] VkExtent2D captureExtent(const AppContext& context) const;
    [[nodiscard]] bool anySlotBusy() const;
    [[nodiscard]] RawPixelFormat pixelFormat() const;
    void ensureTargets(AppContext& context, VkExtent2D extent);
    void destroyTargets(VkDevice device);
    void startRecording(AppContext& context);
    void stopRecording(AppContext& context);
    void recordCopy(VkCommandBuffer commands, StagingSlot& slot);
    void drainCompleted(AppContext& context);
    void consumeSlot(AppContext& context, StagingSlot& slot);
    void noteBackpressure(double waitedMs);
    void handleAutomation(AppContext& context);
    void drawPanel(const AppContext& context);
    void drawSizeCombo(const AppContext& context);

    DemoState& state_;
    CaptureOptions options_;
    ProfileMetricId copyMetric_{invalidProfileMetric};
    ProfileMetricId writeMetric_{invalidProfileMetric};
    ProfileMetricId waitMetric_{invalidProfileMetric};
    CaptureWriter writer_;
    VkFormat format_{VK_FORMAT_UNDEFINED};
    ImageResource target_;
    std::array<StagingSlot, stagingSlotCount> ring_{};
    std::size_t nextSlot_{};
    Phase phase_{Phase::Idle};
    bool toggleRequested_{};
    bool screenshotRequested_{};
    bool screenshotPending_{};
    std::uint64_t framesRecorded_{};
    std::uint64_t backpressureFrames_{};
    std::uint64_t screenshotsQueued_{};
    CaptureResolution customResolution_{1920, 1080};
    bool customSize_{};
    bool automationStarted_{};
    bool automationFailed_{};
    std::filesystem::path videoPath_;
    std::string error_;
};

} // namespace vkexp
