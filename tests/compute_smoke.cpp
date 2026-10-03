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
                              .addPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 32 * sizeof(float))
                              .build();
    std::array<float, 32> settings{700,   700,   0,     0,     1.23F, 0.38F, 0.195F, 3, 360, 40,
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

    // Punctured sphere eversion: half a cycle later every material point must sit
    // mirrored across the equatorial plane, and the cloth must keep its area and
    // stay inside the projection bound while it is pulled through the hole.
    constexpr float radius = 1.25F;
    constexpr float holeAngle = 0.35F;
    constexpr float pi = 3.14159265359F;
    settings[19] = 5.0F;
    settings[4] = radius;
    settings[5] = holeAngle;
    settings[6] = 0.6F; // end hold
    settings[7] = 0.0F; // spin
    settings[2] = 0.0F;
    const auto closed = sample();
    settings[2] = pi;
    const auto everted = sample();
    for (std::size_t value = 0; value < floatCount; value += 8) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const float mirrored = axis == 1 ? -closed[value + axis] : closed[value + axis];
            if (!(std::abs(everted[value + axis] - mirrored) < 0.001F)) {
                throw std::runtime_error("Eversion does not mirror the punctured sphere");
            }
        }
    }
    // The probe samples 64 area fractions k/64 along the meridian at longitude 0.
    const float sampledArea = 2.0F * radius * radius * (1.0F + std::cos(holeAngle)) * 63.0F / 64.0F;
    for (float phase : {0.0F, 0.7F, 1.2F, 1.5F, 1.7F, 2.0F, 2.6F, pi}) {
        settings[2] = phase;
        const auto cloth = sample();
        float area = 0.0F;
        for (std::size_t ring = 0; ring < 64; ++ring) {
            const std::size_t at = ring * 16 * 8;
            if (!(std::hypot(cloth[at], cloth[at + 1]) < radius * 1.251F)) {
                throw std::runtime_error("Eversion leaves its projection bound");
            }
            if (ring + 1 < 64) {
                const std::size_t next = at + 16 * 8;
                area += (cloth[at] + cloth[next]) *
                        std::hypot(cloth[next] - cloth[at], cloth[next + 1] - cloth[at + 1]);
            }
        }
        if (!(std::abs(area / sampledArea - 1.0F) < 0.03F)) {
            throw std::runtime_error("Eversion changes the area of the cloth (phase " +
                                     std::to_string(phase) + ", ratio " +
                                     std::to_string(area / sampledArea) + ")");
        }
    }

    // Everting sleeve: the cloth keeps its area on both walls and around the lips
    // while it flows, and returns to its start after a whole lap.
    constexpr float outer = 0.72F;
    constexpr float halfLength = 0.80F;
    constexpr float innerRatio = 0.55F;
    settings[19] = 6.0F;
    settings[4] = outer;
    settings[5] = halfLength;
    settings[6] = innerRatio;
    settings[23] = 0.0F; // spin
    const float centre = 0.5F * outer * (1.0F + innerRatio);
    const float lip = 0.5F * outer * (1.0F - innerRatio);
    const float sleeveArea = 2.0F * centre * (2.0F * halfLength + pi * lip);
    settings[2] = 0.0F;
    const auto lapStart = sample();
    settings[2] = 2.0F * pi;
    const auto lapEnd = sample();
    for (std::size_t value = 0; value < floatCount; value += 8) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!(std::abs(lapEnd[value + axis] - lapStart[value + axis]) < 0.001F)) {
                throw std::runtime_error("Sleeve does not return after a whole lap");
            }
        }
    }
    for (float phase : {0.0F, 0.9F, 2.4F, 4.1F}) {
        settings[2] = phase;
        const auto cloth = sample();
        float area = 0.0F;
        for (std::size_t ring = 0; ring < 64; ++ring) {
            const std::size_t at = ring * 16 * 8;
            const std::size_t next = ((ring + 1) % 64) * 16 * 8;
            const float step =
                std::hypot(cloth[next] - cloth[at], cloth[next + 1] - cloth[at + 1]);
            area += (cloth[at] + cloth[next]) * step;
        }
        if (!(std::abs(area / (2.0F * sleeveArea) - 1.0F) < 0.03F)) {
            throw std::runtime_error("Sleeve changes the area of the cloth (ratio " +
                                     std::to_string(area / (2.0F * sleeveArea)) + ")");
        }
    }

    // Morphing sleeve: whatever the shape, the cloth keeps the area set by its size
    // and stays inside the projection bound.
    constexpr float size = 1.45F;
    settings[19] = 7.0F;
    settings[4] = size;
    settings[5] = 0.55F;  // hole ratio
    settings[6] = 6.0F;   // corner squareness
    settings[9] = 1.20F;  // half length
    settings[15] = 1.0F;  // morph amount
    settings[27] = 0.50F; // morph rate
    for (float phase : {0.0F, 1.1F, 2.9F, 4.4F, 7.3F, 10.6F}) {
        settings[2] = phase;
        const auto cloth = sample();
        float area = 0.0F;
        for (std::size_t ring = 0; ring < 64; ++ring) {
            const std::size_t at = ring * 16 * 8;
            const std::size_t next = ((ring + 1) % 64) * 16 * 8;
            if (!(cloth[at] > 0.0F) || !(std::hypot(cloth[at], cloth[at + 1]) < size * 1.14F)) {
                throw std::runtime_error("Morphing sleeve leaves its projection bound");
            }
            area += (cloth[at] + cloth[next]) *
                    std::hypot(cloth[next] - cloth[at], cloth[next + 1] - cloth[at + 1]);
        }
        if (!(std::abs(area / (2.0F * size * size) - 1.0F) < 0.005F)) {
            throw std::runtime_error("Morphing sleeve changes the area of the cloth (ratio " +
                                     std::to_string(area / (2.0F * size * size)) + ")");
        }
    }

    // Vortex ring: strands keep a circular section on the coil around the ring,
    // thin only towards the hole, and one roll brings every point back.
    constexpr float ringRadius = 0.95F;
    constexpr float coilRadius = 0.48F;
    constexpr float strandRadius = 0.17F;
    settings[19] = 8.0F;
    settings[4] = ringRadius;
    settings[5] = coilRadius;
    settings[6] = strandRadius;
    settings[7] = 2.0F;  // twist
    settings[11] = 5.0F; // strands
    settings[15] = 0.7F; // twist wave
    settings[23] = 0.0F; // spin
    settings[2] = 0.0F;
    const auto rollStart = sample();
    settings[2] = 2.0F * pi;
    const auto rollEnd = sample();
    for (float phase : {0.0F, 1.3F, 3.7F, 5.2F}) {
        settings[2] = phase;
        const auto strands = sample();
        for (std::size_t value = 0; value < floatCount; value += 8) {
            const float coilDistance = std::hypot(
                std::hypot(strands[value], strands[value + 2]) - ringRadius, strands[value + 1]);
            const float radius = strands[value + 3];
            if (!(strands[value + 7] < 0.0001F) || !(radius < strandRadius * 1.26F) ||
                !(radius > strandRadius * 0.74F) || !(coilDistance < coilRadius + radius + 0.001F) ||
                !(coilDistance > coilRadius - radius - 0.001F)) {
                throw std::runtime_error("Vortex ring strand leaves its coil or loses its section");
            }
        }
    }
    for (std::size_t value = 0; value < floatCount; value += 8) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!(std::abs(rollEnd[value + axis] - rollStart[value + axis]) < 0.002F)) {
                throw std::runtime_error("Vortex ring does not return after one roll");
            }
        }
    }

    // Ridged torus: with a full pulse it starts as a plain torus, its ridges stay
    // between the thinned body and the crest height, and one roll brings every
    // point back.
    constexpr float bodyRadius = 0.40F;
    settings[19] = 9.0F;
    settings[5] = bodyRadius;
    settings[6] = 1.0F;  // ridge height
    settings[7] = 5.0F;  // ridges
    settings[9] = 2.0F;  // twist
    settings[11] = 1.6F; // ridge sharpness
    settings[27] = 1.0F; // pulse rate
    settings[29] = 1.0F; // pulse depth
    const auto bodyDistance = [&](const std::array<float, floatCount>& cloth, const std::size_t at) {
        return std::hypot(std::hypot(cloth[at], cloth[at + 2]) - ringRadius, cloth[at + 1]);
    };
    settings[2] = 0.0F;
    const auto plain = sample();
    settings[2] = 2.0F * pi;
    const auto rolled = sample();
    settings[2] = pi;
    const auto ridged = sample();
    bool crestSeen = false;
    for (std::size_t value = 0; value < floatCount; value += 8) {
        if (!(std::abs(bodyDistance(plain, value) - bodyRadius) < 0.001F)) {
            throw std::runtime_error("Ridged torus does not start as a plain torus");
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!(std::abs(rolled[value + axis] - plain[value + axis]) < 0.002F)) {
                throw std::runtime_error("Ridged torus does not return after one roll");
            }
        }
        const float distance = bodyDistance(ridged, value);
        if (!(distance > bodyRadius * 0.549F) || !(distance < bodyRadius * 1.551F)) {
            throw std::runtime_error("Ridged torus ridges leave their height range");
        }
        crestSeen = crestSeen || distance > bodyRadius * 1.3F;
    }
    if (!crestSeen) {
        throw std::runtime_error("Ridged torus raises no ridges at mid pulse");
    }

    // Ridged braid: ridges only carve into the tube, between its radius on the
    // crests and the valley depth.
    constexpr float braidTube = 0.195F;
    constexpr float ridgeDepth = 0.8F;
    settings = {700,   700, 0,     0, 1.23F, 0.38F, braidTube,  3,
                360,   40,  1.65F, 3, 0.10F, 0.15F, 1.15F,      0,
                0,     0,   0,     1, 0,     0.65F, 1.15F,      4,
                0.85F, 0,   0.18F, 0, 0,     ridgeDepth, 1.5F, 3.0F + (6.0F + 32.0F) / 128.0F};
    bool crest = false;
    bool valley = false;
    for (float phase : {0.0F, 2.1F, 5.5F}) {
        settings[2] = phase;
        const auto tubes = sample();
        for (std::size_t value = 0; value < floatCount; value += 8) {
            const float distance = tubes[value + 3];
            if (!(distance < braidTube + 0.0002F) ||
                !(distance > braidTube * (1.0F - 0.6F * ridgeDepth) - 0.0002F)) {
                throw std::runtime_error("Ridged braid ridges leave the tube's depth range");
            }
            crest = crest || distance > braidTube * 0.95F;
            valley = valley || distance < braidTube * 0.60F;
        }
    }
    if (!crest || !valley) {
        throw std::runtime_error("Ridged braid shows no ridges");
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
