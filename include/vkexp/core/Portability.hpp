#pragma once

// Vulkan portability helpers (MoltenVK on macOS, other layered implementations).
// Everything here is a no-op on native drivers that don't expose these extensions,
// so the same code path is used on Linux and macOS.

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace vkexp::portability {

// Not in core vulkan.h (lives in vulkan_beta.h), so spell it out.
inline constexpr const char* kPortabilitySubsetExtension = "VK_KHR_portability_subset";

inline bool instanceExtensionAvailable(const char* wanted) {
    std::uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    return std::any_of(extensions.begin(), extensions.end(),
                       [wanted](const VkExtensionProperties& extension) {
                           return std::strcmp(extension.extensionName, wanted) == 0;
                       });
}

inline bool deviceExtensionAvailable(const VkPhysicalDevice device, const char* wanted) {
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    return std::any_of(extensions.begin(), extensions.end(),
                       [wanted](const VkExtensionProperties& extension) {
                           return std::strcmp(extension.extensionName, wanted) == 0;
                       });
}

// Adds VK_KHR_portability_enumeration + the matching create flag when the loader
// offers it. Without this the loader hides MoltenVK and vkCreateInstance /
// vkEnumeratePhysicalDevices report VK_ERROR_INCOMPATIBLE_DRIVER or zero devices.
inline void enableInstancePortability(std::vector<const char*>& extensions,
                                      VkInstanceCreateFlags& flags) {
    if (instanceExtensionAvailable(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
}

// The spec requires enabling VK_KHR_portability_subset whenever a device exposes it.
inline void enableDevicePortability(const VkPhysicalDevice device,
                                    std::vector<const char*>& extensions) {
    if (deviceExtensionAvailable(device, kPortabilitySubsetExtension)) {
        extensions.push_back(kPortabilitySubsetExtension);
    }
}

} // namespace vkexp::portability
