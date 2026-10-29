#include "vkexp/capture/CaptureSettings.hpp"
#include "vkexp/capture/CaptureWriter.hpp"
#include "vkexp/compute/ComputeResources.hpp"
#include "vkexp/core/FrameClock.hpp"
#include "vkexp/demo/ArchLattice.hpp"
#include "vkexp/demo/DemoState.hpp"
#include "vkexp/demo/FourSurfaces.hpp"
#include "vkexp/demo/LoopPlan.hpp"
#include "vkexp/demo/NestedCubes.hpp"
#include "vkexp/demo/Folding.hpp"
#include "vkexp/demo/Metaballs.hpp"
#include "vkexp/demo/PointLattice.hpp"
#include "vkexp/demo/AlgebraicKnot.hpp"
#include "vkexp/demo/HigherCubes.hpp"
#include "vkexp/demo/HyperbolicHoneycomb.hpp"
#include "vkexp/demo/Polytope4D.hpp"
#include "vkexp/demo/Quasicrystal.hpp"
#include "vkexp/demo/TentaclePatch.hpp"
#include "vkexp/presets/PresetRegistry.hpp"
#include "vkexp/profiling/CpuProfiler.hpp"
#include "vkexp/profiling/ProfilerTypes.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
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
    check(registry.all().size() == 25, "Built-in preset count");
    check(registry.require("mixed").graphicsEnabled, "Mixed preset graphics");
    check(registry.require("mixed").computeEnabled, "Mixed preset compute");
    check(registry.require("nested-spheres").initialGeometryMode == 4,
          "Nested spheres initial geometry");
    check(registry.require("punctured-sphere").initialGeometryMode == 5,
          "Punctured sphere initial geometry");
    check(registry.require("sleeve").initialGeometryMode == 6, "Sleeve initial geometry");
    check(registry.require("morphing-sleeve").initialGeometryMode == 7,
          "Morphing sleeve initial geometry");
    check(registry.require("vortex-ring").initialGeometryMode == 8, "Vortex ring initial geometry");
    check(registry.require("ridged-torus").initialGeometryMode == 9,
          "Ridged torus initial geometry");
    check(registry.require("ridged-braid").initialGeometryMode == 10,
          "Ridged braid initial geometry");
    vkexp::DemoState ridged{registry.require("ridged-braid")};
    ridged.ridgedBraid.braid = 2;
    check(vkexp::motionGeometryMode(ridged) == 2 && vkexp::loopInputs(ridged).geometryMode == 2,
          "Ridged braid moves like its braid");
    check(vkexp::TubeRidges{0.5F, 1.0F, 3, -18}.packed()[2] == 3.0F + 14.0F / 128.0F,
          "Tube ridge counts pack exactly");

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
    constexpr std::string_view tags =
        ",setparams=color_primaries=bt709:color_trc=iec61966-2-1:colorspace=bt709:range=tv";
    check(contains(arguments, "-vf",
                   "scale=out_color_matrix=bt709:out_range=tv,format=yuv420p" + std::string{tags}),
          "h264 BT.709 4:2:0 conversion with sRGB transfer tags");
    check(arguments.back() == "captures/out.mp4", "ffmpeg output is last");

    settings.pixelFormat = vkexp::RawPixelFormat::Rgba;
    settings.codec = vkexp::VideoCodec::ProRes;
    arguments = vkexp::ffmpegArguments(settings);
    check(contains(arguments, "-pix_fmt", "rgba"), "ffmpeg RGBA input");
    check(contains(arguments, "-c:v", "prores_ks") && contains(arguments, "-profile:v", "3"),
          "ProRes HQ encoder");
    check(contains(arguments, "-vf",
                   "scale=out_color_matrix=bt709:out_range=tv,format=yuv422p10le" +
                       std::string{tags}),
          "ProRes 4:2:2 10-bit with sRGB transfer tags");
    check(std::find(arguments.begin(), arguments.end(), "-movflags") == arguments.end(),
          "ProRes .mov without faststart");

    settings.codec = vkexp::VideoCodec::ProRes4444;
    arguments = vkexp::ffmpegArguments(settings);
    check(contains(arguments, "-profile:v", "4") &&
              contains(arguments, "-vf",
                       "scale=out_color_matrix=bt709:out_range=tv,format=yuv444p10le" +
                           std::string{tags}),
          "ProRes 4444 keeps full chroma");
    check(arguments.back() == "captures/out.mp4", "Output path is passed through");

    settings.codec = vkexp::VideoCodec::Hevc;
    arguments = vkexp::ffmpegArguments(settings);
    check(contains(arguments, "-tag:v", "hvc1"), "HEVC tagged hvc1");
#ifdef __APPLE__
    check(contains(arguments, "-c:v", "hevc_videotoolbox") &&
              contains(arguments, "-profile:v", "main42210") && contains(arguments, "-q:v", "75"),
          "HEVC 4:2:2 VideoToolbox encoder");
    check(contains(arguments, "-vf",
                   "scale=out_color_matrix=bt709:out_range=tv,format=p210le" + std::string{tags}),
          "HEVC 4:2:2 10-bit input");
    check(vkexp::defaultVideoCodec() == vkexp::VideoCodec::Hevc, "macOS default codec");
#else
    check(contains(arguments, "-c:v", "libx265"), "HEVC software encoder");
    check(contains(arguments, "-vf",
                   "scale=out_color_matrix=bt709:out_range=tv,format=yuv444p" + std::string{tags}),
          "HEVC 4:4:4 input");
    check(vkexp::defaultVideoCodec() == vkexp::VideoCodec::H264, "Linux default codec");
#endif
    settings.codec = vkexp::VideoCodec::Hevc420;
    arguments = vkexp::ffmpegArguments(settings);
    check(contains(arguments, "-vf",
                   "scale=out_color_matrix=bt709:out_range=tv,format=yuv420p" + std::string{tags}),
          "Compatible HEVC stays 4:2:0");

    check(vkexp::videoFileExtension(vkexp::VideoCodec::ProRes) == ".mov", "ProRes extension");
    check(vkexp::videoFileExtension(vkexp::VideoCodec::ProRes4444) == ".mov", "ProRes 4444 extension");
    check(vkexp::videoFileExtension(vkexp::VideoCodec::Hevc) == ".mp4", "HEVC extension");
    for (const auto codec : vkexp::allVideoCodecs) {
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

void testLoopPlan() {
    constexpr double tau = 6.28318530717958647692;
    const auto near = [](const double left, const double right) {
        return std::abs(left - right) < 1e-9;
    };
    vkexp::LoopInputs inputs;
    inputs.geometryMode = 0;
    inputs.driver = vkexp::LoopDriver::Wave;
    inputs.cycles = 1;
    inputs.animationSpeed = 0.42;
    inputs.releaseSpeed = 1.15;
    auto plan = vkexp::planLoop(inputs);
    check(plan.valid, "Wave loop is valid");
    check(near(plan.phaseCycles, 1.0 / 1.15), "One wave lap in flow cycles");
    check(near(plan.durationSeconds, tau / 1.15 / 0.42), "Wave loop duration");
    check(near(plan.phaseCycles * 1.15, 1.0), "Wave completes a whole lap");
    check(vkexp::loopFrameCount(plan, 60.0) == 781, "Wave loop frame count at 60 fps");

    inputs.driver = vkexp::LoopDriver::Flow;
    inputs.cycles = 2;
    plan = vkexp::planLoop(inputs);
    check(plan.valid && near(plan.phaseCycles, 2.0), "Two flow cycles");

    // Auto rotation is rounded to whole turns over the loop.
    inputs.autoRotate = true;
    inputs.rotationSpeed = 0.05;
    inputs.driver = vkexp::LoopDriver::Wave;
    inputs.cycles = 3;
    plan = vkexp::planLoop(inputs);
    const double turns = plan.rotationPerPhase * plan.phaseCycles;
    check(plan.valid && near(turns, std::round(turns)), "Rotation rounded to whole turns");

    inputs.driver = vkexp::LoopDriver::Rotation;
    inputs.cycles = 1;
    plan = vkexp::planLoop(inputs);
    check(plan.valid && near(plan.durationSeconds, tau / 0.05), "One object rotation lasts 2pi/speed");
    check(near(plan.rotationPerPhase * plan.phasePeriod, tau), "Rotation loop turns exactly once");
    inputs.rotationSpeed = -0.05;
    plan = vkexp::planLoop(inputs);
    check(plan.valid && near(plan.rotationPerPhase * plan.phasePeriod, -tau),
          "Reverse rotation loop turns exactly once backwards");

    inputs.autoRotate = false;
    check(!vkexp::planLoop(inputs).valid, "Rotation loop needs auto rotate");
    inputs.driver = vkexp::LoopDriver::Wave;
    inputs.geometryMode = 3;
    check(!vkexp::planLoop(inputs).valid, "Orbital bloom has no wave loop");
    check(vkexp::defaultLoopDriver(3) == vkexp::LoopDriver::Flow, "Orbital bloom loops its orbit");
    check(vkexp::loopDriverLabel(4, vkexp::LoopDriver::Flow) == "Sphere turns", "Sphere loop label");
    check(vkexp::defaultLoopDriver(5) == vkexp::LoopDriver::Flow, "Eversion loops its cycle");
    check(vkexp::loopDriverLabel(5, vkexp::LoopDriver::Flow) == "Eversion cycles",
          "Eversion loop label");
    check(!vkexp::loopDriverAvailable(5, vkexp::LoopDriver::Rotation, true),
          "Eversion has no object rotation loop");
    check(vkexp::loopDriverLabel(6, vkexp::LoopDriver::Flow) == "Sleeve laps", "Sleeve loop label");
    check(vkexp::loopDriverLabel(8, vkexp::LoopDriver::Flow) == "Ring rolls", "Vortex loop label");

    inputs.geometryMode = 0;
    inputs.paused = true;
    check(!vkexp::planLoop(inputs).valid, "Paused animation cannot loop");
    inputs.paused = false;
    inputs.animationSpeed = 0.0;
    check(!vkexp::planLoop(inputs).valid, "Zero speed cannot loop");
    inputs.animationSpeed = 0.01;
    inputs.cycles = 8;
    plan = vkexp::planLoop(inputs);
    check(!plan.valid && !plan.problem.empty(), "Overlong loop rejected");
    plan.durationSeconds = 0.001;
    check(vkexp::loopFrameCount(plan, 60.0) == 2, "Loop has at least two frames");
}

void testClosedLattice() {
    check(vkexp::closedLatticeStep(987) == 610, "Fibonacci count steps by its neighbour");
    for (const int count : {2000, 16000, 24000, 31337, 32000, 59999, 60000}) {
        const int step = vkexp::closedLatticeStep(count);
        check(step > count / 2 && step < count, "Lattice step near the golden ratio");
        check(std::gcd(step, count) == 1, "Lattice step visits every longitude once");
        // Closed: one step past the last point lands on the first one.
        check(static_cast<long long>(count) * step % count == 0, "Lattice closes on itself");
        // No two points nearer than 0.7 of the mean spacing, on a square or a stretched patch.
        for (const double stretch : {0.4, 1.0, 2.5}) {
            double nearest = 1.0;
            for (int point = 1; point < 3000; ++point) {
                const double along = static_cast<double>(point) / count;
                double around = static_cast<double>(static_cast<long long>(point) * step % count) /
                                count;
                around = std::min(around, 1.0 - around);
                nearest = std::min(nearest, std::hypot(along * stretch, around / stretch));
            }
            check(nearest * std::sqrt(static_cast<double>(count)) > 0.7, "Lattice stays even");
        }
    }
}

void testTentaclePatch() {
    // Without a tentacle the patch is a plain cap of the sphere.
    const double width = 0.13;
    const double cap = 0.5 * (1.0 - std::cos(vkexp::tentaclePatchWidths * width));
    check(std::abs(vkexp::tentaclePatchAreaShare(0.62, 0.0, width, 3.0) - cap) < 1e-5,
          "Flat tentacle patch is a spherical cap");
    check(vkexp::tentaclePatchAreaShare(0.62, 0.75, width, 3.0) > 2.0 * cap,
          "A tentacle adds its walls to the patch area");
    check(vkexp::tentaclePatchPoints(16000, 0.62, 0.0, width, 3.0) ==
              static_cast<int>(std::lround(16000 * cap)),
          "Patch points follow the area share");
    check(vkexp::tentacleWidthLimit(32) < vkexp::tentacleWidthLimit(8),
          "More tentacles must be narrower");
    check(vkexp::loopDriverLabel(11, vkexp::LoopDriver::Flow) == "Tentacle swings",
          "Tentacle loop label");
    vkexp::PresetRegistry registry;
    check(registry.require("tentacle-sphere").initialGeometryMode == 11,
          "Tentacle sphere initial geometry");
    check(registry.require("bumpy-torus").initialGeometryMode == 12,
          "Bumpy torus initial geometry");
    check(vkexp::loopDriverLabel(12, vkexp::LoopDriver::Flow) == "Ring rolls",
          "Bumpy torus loop label");
    check(registry.require("torus-chain").initialGeometryMode == 13,
          "Torus chain initial geometry");
    check(registry.require("sphere-eversion").initialGeometryMode == 14,
          "Sphere eversion initial geometry");
    check(vkexp::loopDriverLabel(14, vkexp::LoopDriver::Flow) == "Eversion cycles",
          "Sphere eversion loop label");
    check(registry.require("arched-sphere").initialGeometryMode == 15,
          "Arched sphere initial geometry");
    check(vkexp::loopDriverLabel(15, vkexp::LoopDriver::Flow) == "Fountain laps",
          "Arched sphere loop label");
    // A flat, untwisted arch runs along a meridian between its two feet.
    const double foot = vkexp::archFootAngle(0.6, 0.09, 6);
    check(std::abs(vkexp::archLength(0.6, 0.0, 0.09, 6, 0.0) -
                   0.6 * (3.14159265358979323846 - 2.0 * foot)) < 1e-3,
          "Flat arch follows a meridian");
    check(vkexp::archLength(0.6, 0.55, 0.09, 6, 0.25) > vkexp::archLength(0.6, 0.0, 0.09, 6, 0.0),
          "Height and twist lengthen an arch");
    const vkexp::ArchLattice arch = vkexp::archLattice(16000, 0.6, 0.55, 0.09, 6, 0.25);
    check(arch.columns % 2 == 0 && arch.columns >= 6 && arch.rows >= 16,
          "Arch lattice is staggered and populated");
    check(registry.require("nested-cubes").initialGeometryMode == 16,
          "Nested cubes initial geometry");
    check(vkexp::loopDriverLabel(16, vkexp::LoopDriver::Flow) == "Cube turns",
          "Nested cubes loop label");
    // A cube's far corner reaches sqrt(3) times its face centre; a sphere is round.
    check(std::abs(vkexp::nestedCubeRatioLimit(1.0) - 1.0) < 1e-12, "Spheres nest up to touching");
    check(std::abs(vkexp::nestedCubeRatioLimit(0.0) - std::pow(3.0, 1.0 / 40.0) / std::sqrt(3.0)) <
              1e-12,
          "Cubes nest below 1 / sqrt(3)");
    check(vkexp::nestedCubeRatio(0.9, 0.0, true) < vkexp::nestedCubeRatioLimit(0.0) &&
              vkexp::nestedCubeRatio(0.9, 0.0, false) == 0.9,
          "Keeping cubes apart holds the ratio");
    const vkexp::NestedCubeSettings cubeDefaults;
    check(cubeDefaults.sizeRatio < 0.97 * vkexp::nestedCubeRatioLimit(cubeDefaults.roundness),
          "Default cubes stay apart");
    check(vkexp::nestedCubePoints(24000, 1.0, 1.0) == 24000, "Solid faces take every point");
    check(vkexp::nestedCubePoints(24000, 0.25, 1.0) == 10512 &&
              vkexp::nestedCubePoints(24000, 0.25, 0.5) == 2616,
          "Frames and smaller cubes keep the spacing");
    check(vkexp::nestedCubePoints(24000, 0.0, 0.1) == 24, "Every strip keeps a point");
    // The zoom offset wraps once per level and, in a loop, the whole number of
    // levels makes the last frame meet the first.
    check(std::abs(vkexp::cubeZoomOffset(3.14159265358979323846, 1.0, 0.0) - 0.5) < 1e-12,
          "Half a flow cycle is half a level");
    const double loopEnd = vkexp::cubeZoomOffset(6.28318530717958647692 * 3.0, 4.3, 3.0);
    check(std::min(loopEnd, 1.0 - loopEnd) < 1e-9,
          "A zoom loop returns to its start");
    check(registry.require("hopf-fibration").initialGeometryMode == 17,
          "Hopf fibration initial geometry");
    check(vkexp::loopDriverLabel(17, vkexp::LoopDriver::Flow) == "Fibre flows",
          "Hopf loop label");
    check(registry.require("tesseract").initialGeometryMode == 18, "Tesseract initial geometry");
    check(vkexp::loopDriverLabel(18, vkexp::LoopDriver::Flow) == "Tesseract turns",
          "Tesseract loop label");
    check(registry.require("polytopes-4d").initialGeometryMode == 19, "4D polytopes geometry");
    check(registry.require("sections-4d").initialGeometryMode == 20 &&
              registry.require("quaternion-julia").initialGeometryMode == 21,
          "4D sections and Julia geometry");
    check(vkexp::loopDriverLabel(20, vkexp::LoopDriver::Flow) == "Slice sweeps",
          "4D sections loop label");
    check(registry.require("surfaces-4d").initialGeometryMode == 22, "4D surfaces geometry");
    check(vkexp::loopDriverLabel(22, vkexp::LoopDriver::Flow) == "Surface turns",
          "4D surfaces loop label");
    check(registry.require("origami").initialGeometryMode == 23 &&
              registry.require("flowing-shells").initialGeometryMode == 24,
          "Origami and flowing shells geometry");
    check(vkexp::loopDriverLabel(23, vkexp::LoopDriver::Flow) == "Fold cycles" &&
              vkexp::loopDriverLabel(24, vkexp::LoopDriver::Flow) == "Shell hops",
          "Origami and shells loop labels");
    check(registry.require("metaballs").initialGeometryMode == 25 &&
              vkexp::loopDriverLabel(25, vkexp::LoopDriver::Flow) == "Metaball cycles",
          "Metaballs preset and loop label");
    const vkexp::BumpyTorusSettings chain = vkexp::torusChainDefaults();
    check(chain.tubeRadius <= vkexp::torusChainTubeLimit(chain),
          "Chain links fit through each other by default");
}

void testPolytopes() {
    using vkexp::Polytope4D;
    // Edge counts of the regular polytopes, the duoprism and the compounds.
    const std::array<std::pair<Polytope4D, std::size_t>, 12> counts{{
        {Polytope4D::Simplex, 10},
        {Polytope4D::Tesseract, 32},
        {Polytope4D::Orthoplex, 24},
        {Polytope4D::TwentyFourCell, 96},
        {Polytope4D::SixHundredCell, 720},
        {Polytope4D::HundredTwentyCell, 1200},
        {Polytope4D::Duoprism, 72},
        {Polytope4D::TesseractOrthoplex, 56},
        {Polytope4D::ThreeOrthoplexes, 72},
        {Polytope4D::NestedTesseracts, 64},
        {Polytope4D::DualTwentyFourCells, 192},
        {Polytope4D::TorusKnot, 360},
    }};
    for (const auto& [shape, expected] : counts) {
        const auto edges = vkexp::polytopeEdges(shape, 6, 6);
        const auto name = std::to_string(static_cast<int>(shape));
        check(edges.size() == expected, "Polytope edge count " + name);
        double reach = 0.0;
        for (const auto& edge : edges) {
            reach = std::max(reach, vkexp::polytope_detail::norm(edge.start));
        }
        check(std::abs(reach - 1.0) < 1e-9, "Polytope on the unit 3-sphere " + name);
    }
    const auto tesseract = vkexp::polytopeEdges(Polytope4D::Tesseract);
    int directions = 0;
    for (const auto& edge : tesseract) {
        directions = std::max(directions, edge.direction + 1);
    }
    check(directions == 4, "Tesseract edges run in four directions");
    const auto three = vkexp::polytopeEdges(Polytope4D::ThreeOrthoplexes);
    check(std::count_if(three.begin(), three.end(), [](const auto& e) { return e.part == 2; }) == 24,
          "Each of the three 16-cells keeps its 24 edges");
    check(vkexp::polytopePointsAlong(160000, 1200, 6) == 22 &&
              vkexp::polytopePointsAlong(160000, 10, 6) == 400,
          "Point budget spreads over the edges");
    check(vkexp::polytopeEdges(Polytope4D::Quasicrystal).empty() &&
              vkexp::polytopeIsAnimated(static_cast<int>(Polytope4D::AlgebraicKnot)),
          "Changing figures are built outside polytopeEdges");
}

void testQuasicrystal() {
    const auto edges = vkexp::quasicrystalEdges({});
    check(edges.size() > 100, "Quasicrystal has a cluster of edges");
    bool inside = true, unitEdges = true;
    int axes = 0;
    for (const auto& edge : edges) {
        const vkexp::Vec4d a{edge.start[0], edge.start[1], edge.start[2], 0.0};
        const vkexp::Vec4d b{edge.end[0], edge.end[1], edge.end[2], 0.0};
        inside = inside && vkexp::polytope_detail::norm(a) <= 1.0 + 1e-9;
        // Every edge is one lattice step: the same length in parallel space.
        const double length = std::sqrt(vkexp::polytope_detail::distanceSquared(a, b));
        unitEdges = unitEdges && std::abs(length - 1.0 / 3.5) < 1e-9;
        axes |= 1 << edge.direction;
    }
    check(inside, "Quasicrystal stays inside the unit ball");
    check(unitEdges, "Quasicrystal edges are single lattice steps");
    check(axes == 63, "Quasicrystal edges run along all six icosahedral axes");
    vkexp::QuasicrystalShape shifted;
    shifted.shift = {0.4, 0.0, 0.0};
    const auto moved = vkexp::quasicrystalEdges(shifted);
    check(!moved.empty() && moved.size() != edges.size(), "Phason shift flips vertices");
}

void testHoneycomb() {
    using vkexp::honeycomb_detail::minkowski;
    const auto edges = vkexp::honeycombEdges(2000);
    check(edges.size() == 2000, "Honeycomb fills its edge budget");
    bool onHyperboloid = true, equalEdges = true;
    for (const auto& edge : edges) {
        onHyperboloid = onHyperboloid && std::abs(minkowski(edge.start, edge.start) + 1.0) < 1e-9;
        // Every edge of {4,3,5} has cosh(length) = golden ratio squared.
        equalEdges = equalEdges && std::abs(-minkowski(edge.start, edge.end) - 2.6180339887) < 1e-8;
    }
    check(onHyperboloid, "Honeycomb vertices lie on the hyperboloid");
    check(equalEdges, "Honeycomb edges all have the same length");
    check(std::abs(vkexp::honeycombPeriod() - 2.0 * std::atanh(0.4858682718)) < 1e-6,
          "Honeycomb flow period is two face distances");
}

void testAlgebraicKnot() {
    // z1^2 = z2^3: the trefoil, one loop on the 3-sphere.
    vkexp::AlgebraicKnotShape trefoil;
    trefoil.p = 2;
    trefoil.roots.assign(3, {0.0, 0.0});
    const auto edges = vkexp::algebraicKnotEdges(trefoil);
    check(edges.size() > 50, "Trefoil has edges");
    bool onSphere = true;
    int parts = 0;
    for (const auto& edge : edges) {
        onSphere = onSphere && std::abs(vkexp::polytope_detail::norm(edge.start) - 1.0) < 1e-6;
        parts = std::max(parts, edge.part + 1);
    }
    check(onSphere, "Algebraic knot lies on the unit 3-sphere");
    check(parts == 1, "z1^2 = z2^3 is a single knot");
    // z1^2 = z2^2: the Hopf link, two loops.
    vkexp::AlgebraicKnotShape hopf;
    hopf.p = 2;
    hopf.roots.assign(2, {0.0, 0.0});
    int hopfParts = 0;
    for (const auto& edge : vkexp::algebraicKnotEdges(hopf)) {
        hopfParts = std::max(hopfParts, edge.part + 1);
    }
    check(hopfParts == 2, "z1^2 = z2^2 is a link of two loops");
}

void testHigherCubes() {
    const auto penteract = vkexp::higherCubeEdges(5, 0.7, 2.5);
    const auto hexeract = vkexp::higherCubeEdges(6, 0.7, 2.5);
    check(penteract.size() == 80 && hexeract.size() == 192, "5-cube and 6-cube edge counts");
    int joins = 0;
    for (const auto& edge : penteract) {
        joins += edge.part == 2 ? 1 : 0;
        check((edge.part == 2) == (edge.direction == 4), "5-cube parts: two layers and their joins");
    }
    check(joins == 16, "Sixteen edges join the 5-cube's two tesseracts");
    // Unturned, the layer nearer the 5D camera is the larger.
    const double half = std::sqrt(0.2);
    for (const auto& edge : vkexp::higherCubeEdges(5, 0.0, 2.5)) {
        if (edge.part < 2) {
            const double expected = half * 2.5 / (2.5 + (edge.part == 1 ? -half : half));
            check(std::abs(std::abs(edge.start[0]) - expected) < 1e-9, "5-cube layers in perspective");
        }
    }
}

void testOrigami() {
    constexpr std::array faces{4, 6, 8, 12, 20};
    constexpr std::array sides{3, 4, 3, 5, 3};
    for (int solid = 0; solid < vkexp::origamiSolidCount; ++solid) {
        const auto net = vkexp::origamiNet(solid, 0);
        const auto count = net.corners.size();
        check(static_cast<int>(count) == faces[static_cast<std::size_t>(solid)] &&
                  net.sides == sides[static_cast<std::size_t>(solid)],
              "Origami solids have their faces");
        check(!vkexp::origamiNetOverlaps(net), "Origami nets lie flat without overlapping");
        bool tree = net.parent[0] == -1;
        for (std::size_t f = 1; f < count; ++f) {
            tree = tree && net.parent[f] >= 0 && static_cast<std::size_t>(net.parent[f]) < f;
        }
        check(tree, "Origami faces hang from earlier faces");
        const double floor = net.corners[0][0][1];
        const auto closed = vkexp::origamiPose(net, std::vector<double>(count, 1.0));
        const auto flat = vkexp::origamiPose(net, std::vector<double>(count, 0.0));
        const auto mirror = vkexp::origamiPose(net, std::vector<double>(count, -1.0));
        bool onSphere = true, level = true, above = true, below = true;
        for (std::size_t f = 0; f < count; ++f) {
            for (std::size_t c = 0; c < closed.corners[f].size(); ++c) {
                const auto& p = closed.corners[f][c];
                onSphere = onSphere && std::abs(p[0] * p[0] + p[1] * p[1] + p[2] * p[2] - 1.0) < 1e-9;
                above = above && p[1] > floor - 1e-9;
                level = level && std::abs(flat.corners[f][c][1] - floor) < 1e-9;
                below = below && mirror.corners[f][c][1] < floor + 1e-9;
            }
        }
        check(onSphere && above, "Folded, the net is the solid on the floor");
        check(level, "Flat, the net lies on the floor");
        check(below, "Folded through, the mirror solid hangs below the floor");
        check(vkexp::origamiReach(net) >= 1.0, "Origami reach holds the solid");
    }
    const auto first = vkexp::origamiNet(1, 0), second = vkexp::origamiNet(1, 1);
    check(first.parent != second.parent || first.hingeCorner != second.hingeCorner,
          "Origami net variants differ");
    // The breadth-first cube net is the cross: the base and its four sides,
    // the lid on one of them.
    check(first.maxDepth == 2, "Cube net is a cross");
    constexpr double pi = 3.14159265358979323846;
    check(std::abs(vkexp::origamiFold(0.0, 1, 2, 0.0, 0.2, 0.1, true) - 1.0) < 1e-12 &&
              std::abs(vkexp::origamiFold(pi, 1, 2, 0.0, 0.2, 0.1, true) + 1.0) < 1e-12 &&
              std::abs(vkexp::origamiFold(pi, 1, 2, 0.0, 0.2, 0.1, false) - 1.0) < 1e-12 &&
              std::abs(vkexp::origamiFold(0.5 * pi, 1, 2, 0.0, 0.2, 0.1, true)) < 1e-12,
          "Origami folds: closed, mirrored, flat");
}

void testMetaballs() {
    constexpr double tau = 6.28318530717958647692;
    // A ball alone ends at its radius, whatever the reach.
    for (const double reach : {1.3, 2.0, 3.2}) {
        const std::vector<vkexp::Metaball> one{{{0.1, -0.2, 0.3}, 0.4}};
        check(std::abs(vkexp::metaballRay(one, reach, 0, {0.0, 0.0, 1.0}) - 0.4) < 1e-3,
              "Lone metaball ends at its radius");
    }
    // Two close balls merge: the neck between them is filled, the far
    // sides bulge past the radius; a ray into the partner exits beyond it.
    const std::vector<vkexp::Metaball> two{{{-0.3, 0.0, 0.0}, 0.4}, {{0.3, 0.0, 0.0}, 0.4}};
    check(vkexp::metaballField(two, 2.0, {0.0, 0.0, 0.0}) > vkexp::metaballThreshold(2.0), "Neck filled");
    check(vkexp::metaballRay(two, 2.0, 0, {-1.0, 0.0, 0.0}) > 0.4, "Merged balls bulge");
    check(vkexp::metaballRay(two, 2.0, 0, {1.0, 0.0, 0.0}) > 0.95, "Ray runs through the partner");
    // Far apart they keep to themselves.
    const std::vector<vkexp::Metaball> apart{{{-2.0, 0.0, 0.0}, 0.4}, {{2.0, 0.0, 0.0}, 0.4}};
    check(std::abs(vkexp::metaballRay(apart, 2.0, 0, {1.0, 0.0, 0.0}) - 0.4) < 1e-3, "Apart balls stay round");
    // A capsule and a ring alone end their radius out from their cores; a
    // chain of capsules in one group has no bulge at the joint, while
    // separate ones add up there.
    {
        vkexp::Metaball capsule{{0.0, 0.0, 0.0}, 0.2};
        capsule.extent = {0.5, 0.0, 0.0};
        check(std::abs(vkexp::metaballRay({capsule}, 2.0, {0.3, 0.0, 0.0}, 0.2, {0.0, 1.0, 0.0}) - 0.2) < 1e-3 &&
                  std::abs(vkexp::metaballRay({capsule}, 2.0, {0.5, 0.0, 0.0}, 0.2, {1.0, 0.0, 0.0}) - 0.2) < 1e-3,
              "Lone capsule ends at its radius");
        vkexp::Metaball ring{{0.1, 0.0, 0.0}, 0.15};
        ring.extent = {0.0, 0.0, 0.6};
        ring.ring = true;
        check(std::abs(vkexp::metaballRay({ring}, 2.0, {0.1, 0.6, 0.0}, 0.15, {0.0, 1.0, 0.0}) - 0.15) < 1e-3 &&
                  std::abs(vkexp::metaballRay({ring}, 2.0, {0.1, 0.6, 0.0}, 0.15, {0.0, -1.0, 0.0}) - 0.15) < 1e-3 &&
                  vkexp::metaballField({ring}, 2.0, {0.1, 0.0, 0.0}) == 0.0,
              "Lone ring ends at its radius, its hole open");
        vkexp::Metaball left{{-0.25, 0.0, 0.0}, 0.2}, right{{0.25, 0.0, 0.0}, 0.2};
        left.extent = right.extent = {0.25, 0.0, 0.0};
        left.group = right.group = 0;
        check(std::abs(vkexp::metaballRay({left, right}, 2.0, {0.0, 0.0, 0.0}, 0.2, {0.0, 0.0, 1.0}) - 0.2) < 1e-3,
              "Chained capsules meet without a bulge");
        left.group = -1;
        right.group = -1;
        check(vkexp::metaballRay({left, right}, 2.0, {0.0, 0.0, 0.0}, 0.2, {0.0, 0.0, 1.0}) > 0.22,
              "Separate capsules bulge where they meet");
    }
    // Every motion closes its loop, and the bound holds the surface.
    for (int motion = 0; motion < vkexp::metaballMotionCount; ++motion) {
        vkexp::MetaballParams params;
        params.motion = motion;
        const auto start = vkexp::metaballScene(params, 0.0);
        const auto end = vkexp::metaballScene(params, tau);
        double drift = 0.0;
        for (std::size_t i = 0; i < start.size(); ++i) {
            for (std::size_t k = 0; k < 3; ++k) {
                drift = std::max(drift, std::abs(start[i].centre[k] - end[i].centre[k]));
            }
            for (std::size_t k = 0; k < 3; ++k) {
                drift = std::max(drift, std::abs(start[i].extent[k] - end[i].extent[k]));
            }
            drift = std::max(drift, std::abs(start[i].radius - end[i].radius));
        }
        check(drift < 1e-9, "Metaball motion loops");
        const double bound = vkexp::metaballBound(params);
        check(bound > params.radius && bound < 4.0, "Metaball bound");
        const auto starts = vkexp::metaballAreaStarts(start);
        check(starts.size() == start.size() && starts.front() == 0.0 &&
                  std::is_sorted(starts.begin(), starts.end()) && starts.back() < 1.0,
              "Metaball area shares");
        // Four balls of radius 1 hold 4 * 4 pi points at spacing 1.
        const std::vector<vkexp::Metaball> unit(4, vkexp::Metaball{{0.0, 0.0, 0.0}, 1.0});
        check(std::abs(vkexp::metaballSpacing(unit, static_cast<int>(std::lround(16.0 * 3.14159265358979))) - 1.0) <
                  1e-2,
              "Metaball spacing");
    }
}

void testFolding() {
    constexpr double pi = 3.14159265358979323846;
    const auto net = vkexp::origamiNet(1, 0);
    vkexp::FoldParams p;
    p.stagger = 0.0;
    p.holdClosed = 0.0;
    p.holdFlat = 0.0;
    const auto distance = [](const vkexp::Vec3d& a, const vkexp::Vec3d& b) {
        return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) +
                         (a[2] - b[2]) * (a[2] - b[2]));
    };
    const auto edges = [&](const vkexp::FoldScene& scene) {
        std::vector<double> out;
        for (const auto& face : scene.faces) {
            for (std::size_t i = 0; i < face.corners.size(); ++i) {
                out.push_back(distance(face.corners[i], face.corners[(i + 1) % face.corners.size()]));
            }
        }
        return out;
    };
    // Rigid figures keep their edges between closed and open.
    for (const int figure : {0, 3, 4, 6}) {
        p.figure = figure;
        const auto closed = edges(vkexp::foldScene(p, net, 0.0));
        const auto open = edges(vkexp::foldScene(p, net, 0.5 * pi));
        bool same = closed.size() == open.size();
        for (std::size_t i = 0; same && i < closed.size(); ++i) same = std::abs(closed[i] - open[i]) < 1e-9;
        check(same, "Folding figures keep their edges");
    }
    // The tesseract lies flat as Dali's cross: every square the same.
    p.figure = 1;
    for (const double e : edges(vkexp::foldScene(p, net, 0.5 * pi))) {
        check(std::abs(e - 2.0) < 1e-9, "Tesseract unfolds into equal cubes");
    }
    check(vkexp::foldScene(p, net, 0.5 * pi).faces.size() == 48, "Tesseract: eight cubes of six squares");
    // The jitterbug closes into the octahedron, corners on the axes.
    p.figure = 2;
    const auto octahedron = vkexp::foldScene(p, net, 0.0);
    for (const auto& c : octahedron.faces[0].corners) {
        const int zeros = (std::abs(c[0]) < 1e-9) + (std::abs(c[1]) < 1e-9) + (std::abs(c[2]) < 1e-9);
        check(zeros == 2, "Jitterbug closes into the octahedron");
    }
    check(vkexp::foldScene(p, net, 0.5 * pi).faces.size() == 20, "Open jitterbug: eight triangles and six split squares");
    // Dudeney, lying flat: the triangle's side, then the square's diagonal.
    p.figure = 3;
    p.lift = 0.0;
    const auto widest = [&](const vkexp::FoldScene& scene) {
        double most = 0.0;
        for (const auto& a : scene.faces) {
            for (const auto& b : scene.faces) {
                for (const auto& x : a.corners) {
                    for (const auto& y : b.corners) most = std::max(most, distance(x, y));
                }
            }
        }
        return most;
    };
    check(std::abs(widest(vkexp::foldScene(p, net, 0.0)) - 1.7) < 1e-9, "Dudeney starts as the triangle");
    check(std::abs(widest(vkexp::foldScene(p, net, 0.5 * pi)) - 1.7 * std::sqrt(0.5 * std::sqrt(3.0))) < 1e-6,
          "Dudeney turns into the square");
    // Rotating squares stay joined at their corners.
    p.figure = 6;
    const auto squares = vkexp::foldScene(p, net, 0.5 * pi);
    double touch = 1e9;
    for (const auto& x : squares.faces[0].corners) {
        for (const auto& y : squares.faces[1].corners) touch = std::min(touch, distance(x, y));
    }
    check(touch < 1e-9, "Rotating squares stay joined");
    // The rolled strip closes its seam: straight for the cylinder, flipped for Moebius.
    p.figure = 7;
    p.sheetShape = 0;
    auto rolled = vkexp::foldScene(p, net, 0.0);
    check(distance(rolled.faces.front().corners[0], rolled.faces[71 * 10].corners[1]) < 1e-9,
          "Cylinder closes");
    p.sheetShape = 2;
    rolled = vkexp::foldScene(p, net, 0.0);
    check(distance(rolled.faces.front().corners[0], rolled.faces[71 * 10 + 9].corners[2]) < 1e-9,
          "Moebius band closes with a half twist");
    for (int figure = 0; figure < vkexp::foldFigureCount; ++figure) {
        p.figure = figure;
        const auto scene = vkexp::foldScene(p, net, 1.0);
        bool fits = scene.faces.size() * vkexp::origamiFaceVec4 * 4U <=
                    vkexp::polytopeHeaderFloats + vkexp::polytopeEdgeLimit * 12U;
        for (const auto& face : scene.faces) {
            fits = fits && face.corners.size() >= 3 &&
                   face.corners.size() <= static_cast<std::size_t>(vkexp::origamiMaxSides);
        }
        check(fits, "Folding faces fit the buffer");
    }
}

void testFourSurfaces() {
    check(vkexp::fourSurfacePatches({0, 5}) == 25 && vkexp::fourSurfacePatches({3, 5}) == 1,
          "Calabi-Yau has n^2 patches, the others one");
    check(vkexp::fourSurfacePatches({4, 2, 3, 4}) == 24, "Milnor: p q patches per sheet");
    for (int surface = 0; surface < vkexp::fourSurfaceCount; ++surface) {
        const vkexp::FourSurfaceShape shape{surface, 5, 3, 4, 1.0};
        const auto grid = vkexp::fourSurfaceGrid(shape, 500000);
        const int points = grid.columns * grid.rows * vkexp::fourSurfacePatches(shape);
        check(points <= 500000 && points > 450000, "4D surface points fill their budget");
        const auto defaults = vkexp::fourSurfaceDefaults(surface);
        check(defaults.surface == surface, "4D surface defaults keep the surface");
    }
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
    testLoopPlan();
    testClosedLattice();
    testTentaclePatch();
    testPolytopes();
    testQuasicrystal();
    testAlgebraicKnot();
    testHoneycomb();
    testFourSurfaces();
    testHigherCubes();
    testOrigami();
    testFolding();
    testMetaballs();
    return failures == 0 ? 0 : 1;
}
