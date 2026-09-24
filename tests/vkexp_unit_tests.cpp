#include "vkexp/capture/CaptureSettings.hpp"
#include "vkexp/capture/CaptureWriter.hpp"
#include "vkexp/compute/ComputeResources.hpp"
#include "vkexp/core/FrameClock.hpp"
#include "vkexp/presets/PresetRegistry.hpp"
#include "vkexp/profiling/CpuProfiler.hpp"
#include "vkexp/profiling/ProfilerTypes.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

bool closeTo(const float left, const float right) { return std::abs(left - right) < 0.0001F; }

void testTimingSeries() {
    vkexp::TimingSeries series;
    series.add(1.0);
    series.add(2.0);
    series.add(3.0);
    series.add(4.0);

    const auto statistics = series.statistics();
    check(statistics.sampleCount == 4, "TimingSeries sample count");
    check(closeTo(statistics.currentMs, 4.0F), "TimingSeries current value");
    check(closeTo(statistics.averageMs, 2.5F), "TimingSeries average");
    check(closeTo(statistics.minimumMs, 1.0F), "TimingSeries minimum");
    check(closeTo(statistics.maximumMs, 4.0F), "TimingSeries maximum");
    check(closeTo(statistics.percentile95Ms, 4.0F), "TimingSeries p95");
}

void testCpuProfiler() {
    constexpr vkexp::ProfileMetricId frameMetric = 0;
    constexpr vkexp::ProfileMetricId cpuWorkMetric = 1;
    constexpr vkexp::ProfileMetricId customMetric = 2;
    vkexp::CpuProfiler profiler;
    profiler.beginFrame();
    profiler.addDuration(customMetric, 1.25);
    const vkexp::CpuProfiler::FrameSample sample = profiler.endFrame(frameMetric, cpuWorkMetric, 3);

    check(sample.wallMilliseconds >= 0.0, "CPU profiler wall time");
    check(sample.cpuMilliseconds >= 0.0, "CPU profiler process time");
    check(profiler.series(frameMetric).statistics().sampleCount == 1, "CPU frame sample");
    check(profiler.series(cpuWorkMetric).statistics().sampleCount == 1, "CPU work sample");
    check(closeTo(profiler.series(customMetric).statistics().currentMs, 1.25F),
          "CPU custom duration");
}

void testPresetRegistry() {
    vkexp::PresetRegistry registry;
    check(registry.all().size() == 4, "Built-in preset count");
    check(registry.require("mixed").graphicsEnabled, "Mixed preset graphics");
    check(registry.require("mixed").computeEnabled, "Mixed preset compute");
    check(registry.require("nested-spheres").initialGeometryMode == 4,
          "Nested spheres initial geometry");

    registry.loadWindowPreset(VKEXP_TEST_WINDOW_PRESET);
    for (const auto& preset : registry.all()) {
        check(preset.windowWidth >= 320 && preset.windowWidth <= 7680, "Loaded preset width range");
        check(preset.windowHeight >= 240 && preset.windowHeight <= 4320,
              "Loaded preset height range");
    }

    bool rejectedUnknown = false;
    try {
        static_cast<void>(registry.require("does-not-exist"));
    } catch (const std::exception&) {
        rejectedUnknown = true;
    }
    check(rejectedUnknown, "Unknown preset rejection");
}

void testDispatchSize() {
    check(vkexp::divideRoundUp(17, 8) == 3, "Rounded-up integer division");
    check(vkexp::divideRoundUp(16, 8) == 2, "Exact integer division");

    const vkexp::DispatchSize groups = vkexp::dispatchSize({1921, 1081, 1}, {8, 8, 1});
    check(groups.x == 241, "Dispatch width");
    check(groups.y == 136, "Dispatch height");
    check(groups.z == 1, "Dispatch depth");

    bool rejectedZero = false;
    try {
        static_cast<void>(vkexp::dispatchSize({1, 1, 1}, {0, 1, 1}));
    } catch (const std::exception&) {
        rejectedZero = true;
    }
    check(rejectedZero, "Zero local size rejection");

    VkPhysicalDeviceLimits limits{};
    limits.maxComputeWorkGroupCount[0] = 1024;
    limits.maxComputeWorkGroupCount[1] = 1024;
    limits.maxComputeWorkGroupCount[2] = 64;
    limits.maxComputeWorkGroupSize[0] = 1024;
    limits.maxComputeWorkGroupSize[1] = 1024;
    limits.maxComputeWorkGroupSize[2] = 64;
    limits.maxComputeWorkGroupInvocations = 1024;
    limits.maxPushConstantsSize = 128;
    limits.maxStorageBufferRange = 4096;
    const std::array<VkDeviceSize, 2> validRanges{1024, 2048};
    vkexp::validateComputeLimits(limits, groups, {8, 8, 1}, 16, validRanges);

    bool rejectedGroupCount = false;
    try {
        vkexp::validateComputeLimits(limits, {1025, 1, 1}, {8, 8, 1});
    } catch (const std::exception&) {
        rejectedGroupCount = true;
    }
    check(rejectedGroupCount, "Dispatch group limit rejection");

    bool rejectedInvocations = false;
    try {
        vkexp::validateComputeLimits(limits, {1, 1, 1}, {64, 64, 1});
    } catch (const std::exception&) {
        rejectedInvocations = true;
    }
    check(rejectedInvocations, "Local invocation limit rejection");

    bool rejectedPushConstants = false;
    try {
        vkexp::validateComputeLimits(limits, {1, 1, 1}, {8, 8, 1}, 132);
    } catch (const std::exception&) {
        rejectedPushConstants = true;
    }
    check(rejectedPushConstants, "Push constant limit rejection");

    const std::array<VkDeviceSize, 1> oversizedRange{8192};
    bool rejectedStorageRange = false;
    try {
        vkexp::validateComputeLimits(limits, {1, 1, 1}, {8, 8, 1}, 0, oversizedRange);
    } catch (const std::exception&) {
        rejectedStorageRange = true;
    }
    check(rejectedStorageRange, "Storage buffer range limit rejection");
}

void testComputeResourceValidation() {
    check(vkexp::tightlyPackedImageSize(VK_FORMAT_R8G8B8A8_UNORM, {4, 4}) == 64,
          "RGBA8 tightly-packed image size");
    check(vkexp::tightlyPackedImageSize(VK_FORMAT_R32G32_SFLOAT, {3, 2}) == 48,
          "RG32 tightly-packed image size");

    bool rejectedUnsupportedFormat = false;
    try {
        static_cast<void>(vkexp::tightlyPackedImageSize(VK_FORMAT_D32_SFLOAT, {4, 4}));
    } catch (const std::exception&) {
        rejectedUnsupportedFormat = true;
    }
    check(rejectedUnsupportedFormat, "Unsupported image transfer format rejection");

    vkexp::ComputePipelineBuilder builder{VK_NULL_HANDLE, VK_NULL_HANDLE};
    builder.specializationConstant(7, std::uint32_t{42});
    bool rejectedDuplicateConstant = false;
    try {
        builder.specializationConstant(7, std::uint32_t{43});
    } catch (const std::exception&) {
        rejectedDuplicateConstant = true;
    }
    check(rejectedDuplicateConstant, "Duplicate specialization constant rejection");

    bool rejectedUnavailableDescriptorSet = false;
    try {
        static_cast<void>(vkexp::PingPongDescriptorSets{}.forReadIndex(0));
    } catch (const std::exception&) {
        rejectedUnavailableDescriptorSet = true;
    }
    check(rejectedUnavailableDescriptorSet, "Unavailable ping-pong descriptor rejection");
}

void testPingPongState() {
    vkexp::PingPongBuffer buffers;
    check(buffers.readIndex() == 0 && buffers.writeIndex() == 1, "Initial ping-pong indices");
    buffers.swap();
    check(buffers.readIndex() == 1 && buffers.writeIndex() == 0, "Swapped ping-pong indices");
    buffers.swap();
    check(buffers.readIndex() == 0 && buffers.writeIndex() == 1, "Restored ping-pong indices");
}

bool contains(const std::vector<std::string>& arguments, const std::string_view option,
              const std::string_view value) {
    const auto found = std::find(arguments.begin(), arguments.end(), option);
    return found != arguments.end() && std::next(found) != arguments.end() &&
           *std::next(found) == value;
}

std::filesystem::path makeTemporaryDirectory(const std::string_view name) {
    const auto directory = std::filesystem::temp_directory_path() / std::string{name};
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

void testFfmpegArguments() {
    vkexp::VideoEncodeSettings settings;
    settings.width = 1920;
    settings.height = 1080;
    settings.fps = 60;
    settings.pixelFormat = vkexp::RawPixelFormat::Bgra;
    settings.codec = vkexp::VideoCodec::H264;
    settings.output = "captures/out.mp4";
    auto arguments = vkexp::ffmpegArguments(settings);
    check(contains(arguments, "-f", "rawvideo"), "ffmpeg raw input");
    check(contains(arguments, "-pix_fmt", "bgra"), "ffmpeg input pixel format");
    check(contains(arguments, "-video_size", "1920x1080"), "ffmpeg input size");
    check(contains(arguments, "-framerate", "60"), "ffmpeg input rate");
    check(contains(arguments, "-i", "-"), "ffmpeg reads stdin");
    check(contains(arguments, "-c:v", "libx264"), "h264 encoder");
    check(contains(arguments, "-crf", "16") && contains(arguments, "-preset", "slow"),
          "h264 quality");
    check(contains(arguments, "-vf", "scale=out_color_matrix=bt709:out_range=tv,format=yuv420p"),
          "h264 BT.709 4:2:0 conversion");
    check(arguments.back() == "captures/out.mp4", "ffmpeg output is last");

    settings.pixelFormat = vkexp::RawPixelFormat::Rgba;
    settings.codec = vkexp::VideoCodec::ProRes;
    arguments = vkexp::ffmpegArguments(settings);
    check(contains(arguments, "-pix_fmt", "rgba"), "ffmpeg RGBA input");
    check(contains(arguments, "-c:v", "prores_ks") && contains(arguments, "-profile:v", "3"),
          "ProRes HQ encoder");
    check(contains(arguments, "-vf", "scale=out_color_matrix=bt709:out_range=tv,format=yuv422p10le"),
          "ProRes 4:2:2 10-bit");
    check(std::find(arguments.begin(), arguments.end(), "-movflags") == arguments.end(),
          "ProRes .mov without faststart");

    settings.codec = vkexp::VideoCodec::Hevc;
    arguments = vkexp::ffmpegArguments(settings);
    check(contains(arguments, "-tag:v", "hvc1"), "HEVC tagged hvc1");
#ifdef __APPLE__
    check(contains(arguments, "-c:v", "hevc_videotoolbox") && contains(arguments, "-q:v", "65"),
          "HEVC VideoToolbox encoder");
    check(vkexp::defaultVideoCodec() == vkexp::VideoCodec::Hevc, "macOS default codec");
#else
    check(contains(arguments, "-c:v", "libx265"), "HEVC software encoder");
    check(vkexp::defaultVideoCodec() == vkexp::VideoCodec::H264, "Linux default codec");
#endif

    check(vkexp::videoFileExtension(vkexp::VideoCodec::ProRes) == ".mov", "ProRes extension");
    check(vkexp::videoFileExtension(vkexp::VideoCodec::Hevc) == ".mp4", "HEVC extension");
    for (const auto codec :
         {vkexp::VideoCodec::Hevc, vkexp::VideoCodec::ProRes, vkexp::VideoCodec::H264}) {
        check(vkexp::parseVideoCodec(vkexp::videoCodecName(codec)) == codec, "Codec name round trip");
    }
    check(!vkexp::parseVideoCodec("vp9"), "Unknown codec rejection");
}

void testShellCommand() {
#ifndef _WIN32
    check(vkexp::shellQuote("plain") == "'plain'", "Plain argument quoting");
    check(vkexp::shellQuote("it's $HOME") == "'it'\\''s $HOME'", "Quote and expansion escaping");
    check(vkexp::shellCommand("/opt/homebrew/bin/ffmpeg", {"-i", "-", "my file.mp4"}) ==
              "'/opt/homebrew/bin/ffmpeg' '-i' '-' 'my file.mp4'",
          "Shell command assembly");
#endif
}

void testCaptureFileNames() {
    std::tm time{};
    time.tm_year = 2026 - 1900;
    time.tm_mon = 8;
    time.tm_mday = 24;
    time.tm_hour = 7;
    time.tm_min = 5;
    time.tm_sec = 9;
    check(vkexp::captureFileName(time, ".mp4") == "sdf_points_20260924_070509.mp4",
          "Capture file name");

    const auto directory = makeTemporaryDirectory("vkexp_capture_names");
    const auto first = vkexp::uniqueCapturePath(directory, time, ".png");
    check(first.filename() == "sdf_points_20260924_070509.png", "First capture path");
    std::ofstream{first}.put('x');
    const auto second = vkexp::uniqueCapturePath(directory, time, ".png");
    check(second.filename() == "sdf_points_20260924_070509_2.png", "Capture path collision suffix");
    std::filesystem::remove_all(directory);
}

void testCaptureSizes() {
    using Mode = vkexp::CaptureSize::Mode;
    const auto hd = vkexp::parseCaptureSize("1920x1080");
    check(hd && hd->mode == Mode::Fixed && hd->fixed.width == 1920 && hd->fixed.height == 1080,
          "Parse 1920x1080");
    const auto framebuffer = vkexp::parseCaptureSize("framebuffer");
    check(framebuffer && framebuffer->mode == Mode::Framebuffer, "Parse framebuffer size");
    const auto viewport = vkexp::parseCaptureSize("viewport");
    check(viewport && viewport->mode == Mode::Viewport && viewport->viewportScale == 1,
          "Parse viewport size");
    const auto viewport2x = vkexp::parseCaptureSize("viewport2x");
    check(viewport2x && viewport2x->viewportScale == 2, "Parse viewport2x size");
    check(!vkexp::parseCaptureSize("viewport5x"), "Reject viewport scale above 4");
    check(!vkexp::parseCaptureSize("viewportx"), "Reject viewport scale without number");
    check(!vkexp::parseCaptureSize("32x32"), "Reject too small resolution");
    check(!vkexp::parseCaptureSize("8192x4320"), "Reject too large resolution");
    check(!vkexp::parseCaptureSize("1920"), "Reject resolution without height");
    check(!vkexp::parseCaptureSize("1920x1080p"), "Reject trailing characters");

    const vkexp::CaptureResolution window{1570, 920};
    const vkexp::CaptureResolution panel{1001, 641};
    const auto resolve = [&](const vkexp::CaptureSize& size) {
        return vkexp::resolveCaptureSize(size, window, panel);
    };
    auto size = resolve({});
    check(size.width == 1570 && size.height == 920, "Framebuffer capture size");
    size = resolve({Mode::Viewport, {}, 1});
    check(size.width == 1000 && size.height == 640, "Viewport size rounded to even");
    size = resolve({Mode::Viewport, {}, 2});
    check(size.width == 2002 && size.height == 1282, "Viewport x2 size");
    size = resolve({Mode::Fixed, {640, 640}, 1});
    check(size.width == 640 && size.height == 640, "Square capture size");
    size = vkexp::resolveCaptureSize({Mode::Viewport, {}, 4}, window, {2000, 1000});
    check(size.width == 4096 && size.height == 2048, "Oversized viewport keeps aspect ratio");
}

void testFindExecutable() {
#ifndef _WIN32
    const auto directory = makeTemporaryDirectory("vkexp_find_executable");
    const auto tool = directory / "fake-ffmpeg";
    std::ofstream{tool} << "#!/bin/sh\n";
    check(!vkexp::findExecutable("fake-ffmpeg", "/nonexistent:" + directory.string()),
          "Non-executable file is skipped");
    std::filesystem::permissions(tool, std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::add);
    const auto found = vkexp::findExecutable("fake-ffmpeg", "/nonexistent::" + directory.string());
    check(found && *found == tool, "Executable found on search path");
    std::filesystem::remove_all(directory);
#endif
}

void testFrameClock() {
    using namespace std::chrono_literals;
    const vkexp::FrameClock::Clock::time_point start{};
    vkexp::FrameClock clock;
    clock.start(start);
    auto tick = clock.tick(start + 16ms);
    check(std::abs(tick.deltaSeconds - 0.016) < 1e-9, "Real frame delta");
    check(std::abs(tick.realDeltaSeconds - 0.016) < 1e-9, "Real frame wall delta");

    clock.setFixedStep(1.0 / 60.0);
    tick = clock.tick(start + 116ms); // a slow 100 ms frame while recording
    check(std::abs(tick.deltaSeconds - 1.0 / 60.0) < 1e-12, "Fixed frame delta");
    check(std::abs(tick.realDeltaSeconds - 0.100) < 1e-9, "Fixed step keeps wall delta");
    check(std::abs(tick.elapsedSeconds - (0.016 + 1.0 / 60.0)) < 1e-9, "Fixed step elapsed");
    double elapsed = tick.elapsedSeconds;
    for (int frame = 0; frame < 299; ++frame) {
        elapsed = clock.tick(start + 116ms + (frame + 1) * 1ms).elapsedSeconds;
    }
    check(std::abs(elapsed - (0.016 + 300.0 / 60.0)) < 1e-9, "300 fixed frames are 5 seconds");

    clock.clearFixedStep();
    check(!clock.fixedStep(), "Fixed step cleared");
    tick = clock.tick(start + 116ms + 299ms + 20ms);
    check(std::abs(tick.deltaSeconds - 0.020) < 1e-9, "Real time resumes without a jump");

    bool rejectedStep = false;
    try {
        clock.setFixedStep(0.0);
    } catch (const std::exception&) {
        rejectedStep = true;
    }
    check(rejectedStep, "Non-positive fixed step rejection");
}

void testCaptureWriter() {
    const auto directory = makeTemporaryDirectory("vkexp_capture_writer");
#ifndef _WIN32
    {
        // A shell pipe stands in for ffmpeg: frames must arrive complete and in order.
        const auto raw = directory / "frames.raw";
        vkexp::CaptureWriter writer{2};
        std::string error;
        check(writer.openVideo("cat > " + vkexp::shellQuote(raw.string()), raw, error),
              "Writer opens pipe");
        for (std::uint8_t frame = 0; frame < 5; ++frame) {
            auto pixels = writer.acquireBuffer(16);
            std::fill(pixels.begin(), pixels.end(), frame);
            static_cast<void>(writer.pushVideoFrame(std::move(pixels)));
        }
        writer.closeVideo();
        writer.shutdown();
        const auto status = writer.status();
        check(!status.failed && status.framesWritten == 5, "Writer wrote every frame");
        check(status.lastFile == raw, "Writer reports the finished file");
        std::ifstream input{raw, std::ios::binary};
        const std::vector<char> bytes{std::istreambuf_iterator<char>{input}, {}};
        check(bytes.size() == 80 && bytes.front() == 0 && bytes.back() == 4,
              "Piped frames are complete and ordered");
    }
    {
        vkexp::CaptureWriter writer{2};
        std::string error;
        check(writer.openVideo("exit 3", directory / "never.mp4", error), "Writer starts failing command");
        static_cast<void>(writer.pushVideoFrame(writer.acquireBuffer(16)));
        writer.closeVideo();
        writer.shutdown();
        check(writer.status().failed && !writer.status().error.empty(),
              "Encoder failure is reported");
    }
#endif
    {
        const auto png = directory / "shot.png";
        vkexp::CaptureWriter writer{1};
        vkexp::CaptureWriter::Pixels pixels{0, 0, 255, 0, 255, 0, 0, 0}; // BGRA blue, red
        static_cast<void>(writer.pushScreenshot(std::move(pixels), 2, 1,
                                                vkexp::RawPixelFormat::Bgra, png));
        writer.shutdown();
        std::ifstream input{png, std::ios::binary};
        char signature[8]{};
        input.read(signature, sizeof(signature));
        check(input && std::string_view(signature + 1, 3) == "PNG", "Screenshot PNG written");
        check(writer.status().lastFile == png, "Screenshot reported as last file");
    }
    std::filesystem::remove_all(directory);
}

} // namespace

int main() {
    testTimingSeries();
    testCpuProfiler();
    testPresetRegistry();
    testDispatchSize();
    testComputeResourceValidation();
    testPingPongState();
    testFfmpegArguments();
    testShellCommand();
    testCaptureFileNames();
    testCaptureSizes();
    testFindExecutable();
    testFrameClock();
    testCaptureWriter();
    return failures == 0 ? 0 : 1;
}
