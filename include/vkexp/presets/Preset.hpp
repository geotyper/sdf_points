#pragma once

#include <glm/vec4.hpp>

#include <string>

namespace vkexp {

struct Preset {
    std::string name;
    std::string description;
    bool graphicsEnabled{true};
    bool computeEnabled{true};
    glm::vec4 clearColor{0.004F, 0.0003F, 0.004F, 1.0F};
    int windowWidth{1280};
    int windowHeight{720};
    int initialGeometryMode{};
};

} // namespace vkexp
