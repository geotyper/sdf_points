#include "vkexp/presets/PresetRegistry.hpp"

#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace vkexp {

PresetRegistry::PresetRegistry()
    : presets_{
          Preset{"graphics", "Graphics pipeline", true, false, {0.004F, 0.0003F, 0.004F, 1.0F}},
          Preset{"compute", "Compute dispatch", false, true, {0.025F, 0.025F, 0.025F, 1.0F}},
          Preset{"mixed",
                 "Graphics and compute pipelines together",
                 true,
                 true,
                 {0.004F, 0.0003F, 0.004F, 1.0F}},
          Preset{"nested-spheres",
                 "Nested rotating point spheres with circular openings",
                 true,
                 false,
                 {0.002F, 0.003F, 0.012F, 1.0F},
                 1280,
                 720,
                 4},
          Preset{"punctured-sphere",
                 "Point sphere with one opening turning inside out",
                 true,
                 false,
                 {0.002F, 0.003F, 0.012F, 1.0F},
                 1280,
                 720,
                 5},
          Preset{"sleeve",
                 "Point sleeve endlessly turning inside out",
                 true,
                 false,
                 {0.002F, 0.003F, 0.012F, 1.0F},
                 1280,
                 720,
                 6},
          Preset{"morphing-sleeve",
                 "Everting point sleeve that keeps changing shape",
                 true,
                 false,
                 {0.002F, 0.003F, 0.012F, 1.0F},
                 1280,
                 720,
                 7},
          Preset{"vortex-ring",
                 "Twisted point strands rolling inwards through the hole of their ring",
                 true,
                 false,
                 {0.002F, 0.003F, 0.012F, 1.0F},
                 1280,
                 720,
                 8},
          Preset{"ridged-torus",
                 "Point torus rolling through its hole while twisted ridges rise and sink",
                 true,
                 false,
                 {0.002F, 0.003F, 0.012F, 1.0F},
                 1280,
                 720,
                 9},
          Preset{"ridged-braid",
                 "Braided point tubes with ridges travelling along them",
                 true,
                 false,
                 {0.004F, 0.0003F, 0.004F, 1.0F},
                 1280,
                 720,
                 10},
      } {}

void PresetRegistry::loadWindowPreset(const std::string_view path) {
    std::ifstream input{std::string{path}};
    if (!input) {
        throw std::runtime_error("Unable to open window preset: " + std::string{path});
    }

    int width = 1280;
    int height = 720;
    std::string line;
    while (std::getline(input, line)) {
        const std::string_view text{line};
        if (text.empty() || text.front() == '#') {
            continue;
        }
        const std::size_t separator = text.find('=');
        if (separator == std::string_view::npos) {
            continue;
        }
        const std::string_view key = text.substr(0, separator);
        const std::string_view value = text.substr(separator + 1);
        int parsed = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
            throw std::runtime_error("Invalid value in window preset: " + line);
        }
        if (key == "width") {
            width = parsed;
        } else if (key == "height") {
            height = parsed;
        }
    }
    if (width < 320 || height < 240 || width > 7680 || height > 4320) {
        throw std::runtime_error("Window preset size is outside the supported range");
    }
    for (auto& preset : presets_) {
        preset.windowWidth = width;
        preset.windowHeight = height;
    }
}

const Preset& PresetRegistry::require(const std::string_view name) const {
    for (const auto& preset : presets_) {
        if (preset.name == name) {
            return preset;
        }
    }
    throw std::runtime_error("Unknown preset: " + std::string{name});
}

} // namespace vkexp
