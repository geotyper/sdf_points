#include "vkexp/compute/HeadlessComputeContext.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace {

struct GridSize {
    std::uint32_t width;
    std::uint32_t height;
};

void runGameOfLife(vkexp::HeadlessComputeContext& context) {
    constexpr GridSize grid{16, 16};
    constexpr std::size_t cellCount = grid.width * grid.height;
    constexpr VkDeviceSize byteSize = cellCount * sizeof(std::uint32_t);
    std::array<std::uint32_t, cellCount> initial{};
    const std::size_t center = (grid.height / 2) * grid.width + grid.width / 2;
    initial[center - 1] = 1;
    initial[center] = 1;
    initial[center + 1] = 1;

    vkexp::PingPongBuffer state;
    state.create(context.physicalDevice(), context.device(),
                 {byteSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT});
    context.immediate().uploadBuffer(state.read(), initial.data(), byteSize);

    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    for (std::uint32_t index = 0; index < bindings.size(); ++index) {
        bindings[index].binding = index;
        bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[index].descriptorCount = 1;
        bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkexp::UniqueDescriptorSetLayout setLayout;
    if (vkCreateDescriptorSetLayout(context.device(), &layoutInfo, nullptr,
                                    setLayout.put(context.device())) != VK_SUCCESS) {
        throw std::runtime_error("Unable to create smoke descriptor set layout");
    }

    vkexp::DescriptorAllocator descriptors{context.device(),
                                           {2, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}}}};
    vkexp::PingPongDescriptorSets descriptorSets;
    descriptorSets.createStorageBuffers(context.physicalDevice(), context.device(), descriptors,
                                        setLayout.get(), state);

    constexpr std::uint32_t aliveValue = 1;
    const vkexp::ComputePipeline pipeline =
        vkexp::ComputePipelineBuilder{context.physicalDevice(), context.device()}
            .shader(VKEXP_SHADER_DIR "/game_of_life.comp.spv")
            .addDescriptorSetLayout(setLayout.get())
            .addPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(GridSize))
            .specializationConstant(0, aliveValue)
            .build();

    context.immediate().execute([&](const VkCommandBuffer commands) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
        const VkPipelineLayout pipelineLayout = pipeline.layout();
        vkCmdPushConstants(commands, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(GridSize), &grid);
        for (int step = 0; step < 2; ++step) {
            const VkDescriptorSet descriptorSet = descriptorSets.current(state);
            vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1,
                                    &descriptorSet, 0, nullptr);
            const std::array<VkDeviceSize, 2> storageRanges{state.read().size(),
                                                            state.write().size()};
            const vkexp::DispatchSize groups = vkexp::checkedDispatchSize(
                context.physicalDevice(),
                {{grid.width, grid.height, 1}, {8, 8, 1}, sizeof(GridSize), storageRanges});
            vkCmdDispatch(commands, groups.x, groups.y, groups.z);
            vkexp::cmdComputePingPongBarrier(commands, state);
            state.swap();
        }
    });

    std::array<std::uint32_t, cellCount> result{};
    context.immediate().readbackBuffer(state.read(), result.data(), byteSize);
    if (result != initial) {
        throw std::runtime_error("Two Game of Life GPU steps did not restore the blinker");
    }
}

void runImageRoundTrip(vkexp::HeadlessComputeContext& context) {
    constexpr VkExtent2D extent{4, 4};
    constexpr std::size_t byteCount = extent.width * extent.height * 4;
    std::array<std::uint8_t, byteCount> pixels{};
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        pixels[index] = static_cast<std::uint8_t>(index * 3);
    }

    vkexp::PingPongImage images;
    images.create(context.physicalDevice(), context.device(),
                  {extent, VK_FORMAT_R8G8B8A8_UNORM,
                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT});
    context.immediate().uploadImage(images.read(), pixels.data(), pixels.size());

    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    for (std::uint32_t index = 0; index < bindings.size(); ++index) {
        bindings[index].binding = index;
        bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[index].descriptorCount = 1;
        bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkexp::UniqueDescriptorSetLayout setLayout;
    if (vkCreateDescriptorSetLayout(context.device(), &layoutInfo, nullptr,
                                    setLayout.put(context.device())) != VK_SUCCESS) {
        throw std::runtime_error("Unable to create image descriptor set layout");
    }
    vkexp::DescriptorAllocator descriptors{context.device(),
                                           {2, {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4}}}};
    vkexp::PingPongDescriptorSets descriptorSets;
    descriptorSets.createStorageImages(context.device(), descriptors, setLayout.get(), images);
    const VkDescriptorSet firstSet = descriptorSets.current(images);
    images.swap();
    const VkDescriptorSet secondSet = descriptorSets.current(images);
    if (firstSet == secondSet) {
        throw std::runtime_error("Image ping-pong descriptors did not switch sets");
    }
    images.swap();

    std::array<std::uint8_t, byteCount> downloaded{};
    context.immediate().readbackImage(images.read(), downloaded.data(), downloaded.size());
    if (downloaded != pixels) {
        throw std::runtime_error("GPU image upload/readback did not preserve RGBA8 data");
    }
}

void runBraidPeriodicity(vkexp::HeadlessComputeContext& context) {
    // The default frequencies repeat every 20 turns. After 100 such cycles,
    // material positions AND normals must still agree. This catches the
    // nested finite-difference/large-phase error which removed visible dots.
    constexpr std::size_t floatCount = 1024 * 2 * 4;
    constexpr VkDeviceSize byteSize = floatCount * sizeof(float);
    vkexp::BufferResource output;
    output.create(
        context.physicalDevice(), context.device(),
        {byteSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT});
    const VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                               VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = 1;
    setInfo.pBindings = &binding;
    vkexp::UniqueDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(context.device(), &setInfo, nullptr,
                                    layout.put(context.device())) != VK_SUCCESS) {
        throw std::runtime_error("Unable to create braid probe layout");
    }
    vkexp::DescriptorAllocator descriptors{context.device(),
                                           {1, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}}}};
    const VkDescriptorSet set = descriptors.allocate(layout.get());
    const VkDescriptorBufferInfo bufferInfo{output.buffer(), 0, byteSize};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(context.device(), 1, &write, 0, nullptr);
    const auto pipeline = vkexp::ComputePipelineBuilder{context.physicalDevice(), context.device()}
                              .shader(VKEXP_SHADER_DIR "/braid_geometry_probe.comp.spv")
                              .addDescriptorSetLayout(layout.get())
                              .addPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 28 * sizeof(float))
                              .build();
    std::array<float, 28> settings{700,   700,   0,     0,     1.23F, 0.38F, 0.195F, 3, 360, 40,
                                   1.65F, 3,     0.10F, 0.15F, 1.15F, 0.36F, 0,      0, 0,   0,
                                   0.78F, 0.65F, 1.15F, 4,     0.85F, 0.85F, 0.18F,  0};
    auto sample = [&]() {
        context.immediate().execute([&](VkCommandBuffer commands) {
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
            vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                    1, &set, 0, nullptr);
            vkCmdPushConstants(commands, pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               sizeof(settings), settings.data());
            vkCmdDispatch(commands, 16, 1, 1);
        });
        std::array<float, floatCount> result{};
        context.immediate().readbackBuffer(output, result.data(), byteSize);
        return result;
    };
    for (int mode = 0; mode < 4; ++mode) {
        settings[19] = static_cast<float>(mode);
        settings[6] = 0.195F;
        settings[2] = 0;
        const auto initial = sample();
        settings[2] = 6.28318530718F * 2000.0F;
        const auto repeated = sample();
        for (std::size_t i = 0; i < floatCount; ++i) {
            const float tolerance = i % 8 < 4 ? 0.01F : 0.08F;
            if (!std::isfinite(initial[i]) || !std::isfinite(repeated[i]) ||
                std::abs(initial[i] - repeated[i]) > tolerance) {
                throw std::runtime_error(
                    "Braid positions/normals drift after long animation (mode " +
                    std::to_string(mode) + ", value " + std::to_string(i) + ")");
            }
        }
        // For each animated section, sample 16 points around the actual
        // surface. They must form a circle in the centreline's normal plane,
        // and changing the radius must scale it without moving its centre.
        for (float phase : {0.0F, 1.3F, 4.7F, 9.2F}) {
            settings[2] = phase;
            settings[6] = 0.12F;
            const auto small = sample();
            settings[6] = 0.32F;
            const auto large = sample();
            for (std::size_t point = 0; point < 1024; ++point) {
                const std::size_t at = point * 8;
                const std::size_t first = (point / 16) * 16 * 8;
                const float smallRadius = small[at + 3];
                const float largeRadius = large[at + 3];
                if (!std::isfinite(smallRadius) || !std::isfinite(largeRadius) ||
                    !std::isfinite(small[at + 7]) || !std::isfinite(large[at + 7]) ||
                    small[at + 7] > 0.00002F || large[at + 7] > 0.00002F ||
                    std::abs(smallRadius - small[first + 3]) > 0.00002F ||
                    std::abs(largeRadius - large[first + 3]) > 0.00002F ||
                    std::abs(largeRadius - smallRadius * (0.32F / 0.12F)) > 0.00002F) {
                    throw std::runtime_error(
                        "Tube section is not circular or radius is capped (mode " +
                        std::to_string(mode) + ")");
                }
                const std::size_t opposite = first + ((point % 16 + 8) % 16) * 8;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    const float centerShift = 0.5F * (large[at + axis] + large[opposite + axis] -
                                                      small[at + axis] - small[opposite + axis]);
                    if (!std::isfinite(centerShift) || std::abs(centerShift) > 0.00002F) {
                        throw std::runtime_error("Tube radius unexpectedly moves the centreline");
                    }
                }
            }
        }
    }
}

int run() {
    vkexp::HeadlessComputeContext context{{"vkexp compute smoke"}};
    runGameOfLife(context);
    runImageRoundTrip(context);
    runBraidPeriodicity(context);
    std::cout << "Headless compute smoke test passed on " << context.deviceName() << '\n';
    return 0;
}

} // namespace

int main() {
    try {
        return run();
    } catch (const vkexp::HeadlessComputeUnavailable& error) {
        std::cout << "SKIPPED: " << error.what() << '\n';
        return 77;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
