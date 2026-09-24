#include "vkexp/capture/CaptureModule.hpp"
#include "vkexp/compute/ComputeModule.hpp"
#include "vkexp/core/Application.hpp"
#include "vkexp/demo/DemoState.hpp"
#include "vkexp/demo/DemoUiModule.hpp"
#include "vkexp/graphics/GraphicsModule.hpp"
#include "vkexp/presets/PresetRegistry.hpp"
#include "vkexp/ui/ImGuiModule.hpp"

#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {

void printHelp(const char* executable) {
    std::cout << "Usage: " << executable << " [--preset NAME] [--no-validation] [capture options]\n"
              << "       " << executable << " --list-presets\n"
              << "\nCapture (F9 record/stop, F10 screenshot):\n"
              << "  --capture-size SIZE             framebuffer (default), viewport, viewport2x\n"
              << "                                  (Viewport panel size and proportions) or WxH\n"
              << "  --capture-fps N                 video frame rate and fixed step (default 60)\n"
              << "  --capture-codec hevc|prores|h264\n"
              << "  --capture-frames N              record N frames at startup, then exit\n"
              << "  --screenshot                    save one PNG at startup, then exit\n";
}

std::string_view requireValue(const int argc, char** argv, int& index) {
    if (index + 1 >= argc) {
        throw std::runtime_error(std::string{argv[index]} + " requires a value");
    }
    return argv[++index];
}

std::uint64_t parseCount(const std::string_view option, const std::string_view text) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0) {
        throw std::runtime_error(std::string{option} + " expects a positive integer");
    }
    return value;
}

} // namespace

int main(const int argc, char** argv) {
    try {
        vkexp::PresetRegistry presets;
        std::string presetName = "mixed";
#ifdef VKEXP_ENABLE_VALIDATION
        bool validationEnabled = true;
#else
        bool validationEnabled = false;
#endif
        vkexp::CaptureOptions captureOptions;
        captureOptions.outputDirectory = VKEXP_CAPTURE_DIR;

        for (int i = 1; i < argc; ++i) {
            const std::string_view argument = argv[i];
            if (argument == "--preset") {
                if (++i >= argc) {
                    throw std::runtime_error("--preset requires a name");
                }
                presetName = argv[i];
            } else if (argument == "--no-validation") {
                validationEnabled = false;
            } else if (argument == "--capture-size") {
                const std::string_view value = requireValue(argc, argv, i);
                const auto size = vkexp::parseCaptureSize(value);
                if (!size) {
                    throw std::runtime_error(
                        "--capture-size expects framebuffer, viewport[Nx] or WxH (64..4096)");
                }
                captureOptions.size = *size;
            } else if (argument == "--capture-fps") {
                const std::uint64_t fps = parseCount(argument, requireValue(argc, argv, i));
                if (fps > 240) {
                    throw std::runtime_error("--capture-fps must be between 1 and 240");
                }
                captureOptions.fps = static_cast<std::uint32_t>(fps);
            } else if (argument == "--capture-codec") {
                const std::string_view value = requireValue(argc, argv, i);
                const auto codec = vkexp::parseVideoCodec(value);
                if (!codec) {
                    throw std::runtime_error("--capture-codec expects hevc, prores or h264");
                }
                captureOptions.codec = *codec;
            } else if (argument == "--capture-frames") {
                captureOptions.exitAfterFrames = parseCount(argument, requireValue(argc, argv, i));
            } else if (argument == "--screenshot") {
                captureOptions.exitAfterScreenshot = true;
            } else if (argument == "--list-presets") {
                for (const auto& preset : presets.all()) {
                    std::cout << preset.name << "\t" << preset.description << '\n';
                }
                return 0;
            } else if (argument == "--help" || argument == "-h") {
                printHelp(argv[0]);
                return 0;
            } else {
                throw std::runtime_error("Unknown argument: " + std::string{argument});
            }
        }

        presets.loadWindowPreset(VKEXP_WINDOW_PRESET);
        vkexp::DemoState state{presets.require(presetName)};
        vkexp::Application app{vkexp::ApplicationConfig{
            state.preset.windowWidth,
            state.preset.windowHeight,
            "sdf_points",
            validationEnabled,
        }};

        auto imgui = std::make_unique<vkexp::ImGuiModule>(app.profiler());
        auto& imguiBackend = *imgui;
        app.addModule(std::make_unique<vkexp::GraphicsModule>(state, app.profiler()));
        app.addModule(std::make_unique<vkexp::ComputeModule>(state, app.profiler()));
        // After Graphics/Compute (scene is final), before ImGui (never captured).
        auto capture = std::make_unique<vkexp::CaptureModule>(state, app.profiler(), captureOptions);
        const auto& captureModule = *capture;
        app.addModule(std::move(capture));
        app.addModule(std::move(imgui));
        app.addModule(std::make_unique<vkexp::DemoUiModule>(state, imguiBackend, app.profiler()));
        const int result = app.run();
        if (captureModule.automationFailed()) {
            std::cerr << "Error: command-line capture failed (see [capture] messages)\n";
            return 1;
        }
        return result;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
