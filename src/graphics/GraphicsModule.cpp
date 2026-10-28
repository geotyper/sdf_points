#include "vkexp/graphics/GraphicsModule.hpp"

#include "vkexp/core/VulkanContext.hpp"
#include "vkexp/demo/ArchLattice.hpp"
#include "vkexp/demo/DemoState.hpp"
#include "vkexp/demo/NestedCubes.hpp"
#include "vkexp/demo/Polytope4D.hpp"
#include "vkexp/demo/PointLattice.hpp"
#include "vkexp/demo/TentaclePatch.hpp"
#include "vkexp/profiling/Profiler.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace vkexp {

GraphicsModule::GraphicsModule(DemoState& state, Profiler& profiler)
    : state_(state), metric_(profiler.registerMetric("Graphics")) {}

void GraphicsModule::onAttach(AppContext& context) {
    const VkDevice device = context.vulkan.device();
    const auto vertex = loadShaderModule(device, VKEXP_SHADER_DIR "/braid_points.vert.spv");
    const auto fragment = loadShaderModule(device, VKEXP_SHADER_DIR "/braid_points.frag.spv");
    const auto surfaceVertex = loadShaderModule(device, VKEXP_SHADER_DIR "/braid_surface.vert.spv");
    const auto surfaceFragment =
        loadShaderModule(device, VKEXP_SHADER_DIR "/braid_surface.frag.spv");
    for (VkFormat format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(context.vulkan.physicalDevice(), format, &properties);
        constexpr VkFormatFeatureFlags required =
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((properties.optimalTilingFeatures & required) == required) {
            depthFormat_ = format;
            break;
        }
    }
    if (depthFormat_ == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("No supported depth attachment format");
    }

    const std::array setBindings{
        VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                     VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                     VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        VkDescriptorSetLayoutBinding{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                     VK_SHADER_STAGE_VERTEX_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = static_cast<std::uint32_t>(setBindings.size());
    setInfo.pBindings = setBindings.data();
    if (vkCreateDescriptorSetLayout(device, &setInfo, nullptr, depthSetLayout_.put(device)) !=
        VK_SUCCESS) {
        throw std::runtime_error("Unable to create point visibility descriptor layout");
    }
    const std::array poolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
    };
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, depthPool_.put(device)) != VK_SUCCESS) {
        throw std::runtime_error("Unable to create point visibility descriptor pool");
    }
    const VkDescriptorSetLayout depthLayout = depthSetLayout_.get();
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = depthPool_.get();
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &depthLayout;
    if (vkAllocateDescriptorSets(device, &allocate, &depthSet_) != VK_SUCCESS) {
        throw std::runtime_error("Unable to allocate point visibility descriptor");
    }
    // Point effects live in a small uniform buffer: the push constant block is full.
    effects_.create(context.vulkan.physicalDevice(), device,
                    BufferResourceConfig{sizeof(EffectsBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT});
    const EffectsBlock noEffects{};
    effects_.write(&noEffects, sizeof(noEffects));
    const VkDescriptorBufferInfo effectsInfo{effects_.buffer(), 0, sizeof(EffectsBlock)};
    VkWriteDescriptorSet effectsWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    effectsWrite.dstSet = depthSet_;
    effectsWrite.dstBinding = 1;
    effectsWrite.descriptorCount = 1;
    effectsWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    effectsWrite.pBufferInfo = &effectsInfo;
    // Two vec4 of shaping, then room for the largest figure: the 120-cell's
    // 1200 edges, three vec4 each.
    constexpr VkDeviceSize polytopeBytes = (8U + 2048U * 12U) * sizeof(float);
    polytope_.create(context.vulkan.physicalDevice(), device,
                     BufferResourceConfig{polytopeBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT});
    const VkDescriptorBufferInfo polytopeInfo{polytope_.buffer(), 0, polytopeBytes};
    VkWriteDescriptorSet polytopeWrite = effectsWrite;
    polytopeWrite.dstBinding = 2;
    polytopeWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    polytopeWrite.pBufferInfo = &polytopeInfo;
    const std::array bufferWrites{effectsWrite, polytopeWrite};
    vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(bufferWrites.size()),
                           bufferWrites.data(), 0, nullptr);

    constexpr VkPushConstantRange pushConstants{
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32U * sizeof(float)};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &depthLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstants;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, pipelineLayout_.put(device)) !=
        VK_SUCCESS) {
        throw std::runtime_error("Unable to create graphics pipeline layout");
    }

    std::array stages{
        VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                        nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertex.get(),
                                        "main", nullptr},
        VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                        nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragment.get(),
                                        "main", nullptr},
    };
    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterizer{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0F;
    VkPipelineMultisampleStateCreateInfo multisampling{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineDepthStencilStateCreateInfo depthState{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthState.depthTestEnable = VK_FALSE;
    depthState.depthWriteEnable = VK_FALSE;
    depthState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendStateCreateInfo blending{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1;
    blending.pAttachments = &blendAttachment;
    constexpr std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    constexpr VkFormat colorFormat = targetFormat;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    rendering.depthAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.pNext = &rendering;
    pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &assembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthState;
    pipelineInfo.pColorBlendState = &blending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                  pipeline_.put(device)) != VK_SUCCESS) {
        throw std::runtime_error("Unable to create graphics pipeline");
    }
    stages[0].module = surfaceVertex.get();
    stages[1].module = surfaceFragment.get();
    blendAttachment.colorWriteMask = 0;
    blendAttachment.blendEnable = VK_FALSE;
    rendering.depthAttachmentFormat = depthFormat_;
    depthState.depthTestEnable = VK_TRUE;
    depthState.depthWriteEnable = VK_TRUE;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                  surfacePipeline_.put(device)) != VK_SUCCESS) {
        throw std::runtime_error("Unable to create braid depth pipeline");
    }
    createRenderTarget(context, state_.viewport.extent);
}

void GraphicsModule::createRenderTarget(AppContext& context, const VkExtent2D extent) {
    target_.create(context.vulkan.physicalDevice(), context.vulkan.device(),
                   ImageResourceConfig{
                       extent,
                       targetFormat,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                           VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                   });

    depth_.create(context.vulkan.physicalDevice(), context.vulkan.device(),
                  ImageResourceConfig{extent, depthFormat_,
                                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                          VK_IMAGE_USAGE_SAMPLED_BIT,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_FILTER_NEAREST,
                                      VK_IMAGE_ASPECT_DEPTH_BIT});
    depthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    const VkDescriptorImageInfo depthImage{depth_.sampler(), depth_.view(),
                                           VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = depthSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &depthImage;
    vkUpdateDescriptorSets(context.vulkan.device(), 1, &write, 0, nullptr);
    targetLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    state_.viewport.image = target_.image();
    state_.viewport.imageView = target_.view();
    state_.viewport.sampler = target_.sampler();
    state_.viewport.extent = extent;
    ++state_.viewport.generation;
}

void GraphicsModule::destroyRenderTarget() {
    state_.viewport.image = VK_NULL_HANDLE;
    state_.viewport.imageView = VK_NULL_HANDLE;
    state_.viewport.sampler = VK_NULL_HANDLE;
    target_.reset();
    depth_.reset();
    depthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    targetLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
}

void GraphicsModule::onUpdate(AppContext& context, const FrameInfo& frame) {
    auto cpuScope = context.profiler.cpu().scope(metric_);
    const auto& recording = state_.loop.recording;
    loopCycles_ = 0.0F;
    if (recording) {
        // Exact steps: after the recorded frames the scene is back at its start.
        loopCycles_ = static_cast<float>(recording->phaseCycles);
        if (state_.braid.geometryMode == 4 || state_.braid.geometryMode == 16) {
            nestedSphereTime_ += static_cast<float>(recording->phaseStep);
        } else if ((state_.braid.geometryMode >= 5 && state_.braid.geometryMode <= 9) ||
                   state_.braid.geometryMode >= 11) {
            eversionTime_ += static_cast<float>(recording->phaseStep);
        } else {
            weaveTime_ += static_cast<float>(recording->phaseStep);
            rotationTime_ += static_cast<float>(recording->rotationStep);
        }
    } else {
        // Loop preview: the same rounded frequencies, advancing in real time.
        const LoopPlan plan = state_.loop.enabled ? planLoop(loopInputs(state_)) : LoopPlan{};
        if (plan.valid) {
            loopCycles_ = static_cast<float>(plan.phaseCycles);
        }
        if (state_.braid.geometryMode == 4) {
            if (!state_.nestedSpheres.paused) {
                nestedSphereTime_ += frame.deltaSeconds * state_.nestedSpheres.animationSpeed;
            }
        } else if (state_.braid.geometryMode == 16) {
            if (!state_.nestedCubes.paused) {
                nestedSphereTime_ += frame.deltaSeconds * state_.nestedCubes.animationSpeed;
            }
        } else if (state_.braid.geometryMode == 19) {
            if (!state_.polytope.paused) {
                eversionTime_ += frame.deltaSeconds * state_.polytope.animationSpeed;
            }
        } else if (state_.braid.geometryMode == 18) {
            if (!state_.tesseract.paused) {
                eversionTime_ += frame.deltaSeconds * state_.tesseract.animationSpeed;
            }
        } else if (state_.braid.geometryMode == 17) {
            if (!state_.hopf.paused) {
                eversionTime_ += frame.deltaSeconds * state_.hopf.animationSpeed;
            }
        } else if (state_.braid.geometryMode == 5) {
            if (!state_.eversion.paused) {
                eversionTime_ += frame.deltaSeconds * state_.eversion.animationSpeed;
            }
        } else if (state_.braid.geometryMode == 6) {
            if (!state_.sleeve.paused) {
                eversionTime_ += frame.deltaSeconds * state_.sleeve.animationSpeed;
            }
        } else if (state_.braid.geometryMode == 7) {
            if (!state_.morph.paused) {
                eversionTime_ += frame.deltaSeconds * state_.morph.animationSpeed;
            }
        } else if (state_.braid.geometryMode == 8) {
            if (!state_.vortex.paused) {
                eversionTime_ += frame.deltaSeconds * state_.vortex.animationSpeed;
            }
        } else if (state_.braid.geometryMode == 9) {
            if (!state_.ridgedTorus.paused) {
                eversionTime_ += frame.deltaSeconds * state_.ridgedTorus.animationSpeed;
            }
        } else if (state_.braid.geometryMode == 11) {
            if (!state_.tentacle.paused) {
                eversionTime_ += frame.deltaSeconds * state_.tentacle.animationSpeed;
            }
        } else if (state_.braid.geometryMode == 12) {
            if (!state_.bumpyTorus.paused) {
                eversionTime_ += frame.deltaSeconds * state_.bumpyTorus.animationSpeed;
            }
        } else if (state_.braid.geometryMode == 13) {
            if (!state_.torusChain.paused) {
                eversionTime_ += frame.deltaSeconds * state_.torusChain.animationSpeed;
            }
        } else if (state_.braid.geometryMode == 14) {
            if (!state_.sphereEversion.paused) {
                eversionTime_ += frame.deltaSeconds * state_.sphereEversion.animationSpeed;
            }
        } else if (state_.braid.geometryMode == 15) {
            if (!state_.archedSphere.paused) {
                eversionTime_ += frame.deltaSeconds * state_.archedSphere.animationSpeed;
            }
        } else if (!state_.braid.paused) {
            const float phaseStep = frame.deltaSeconds * state_.braid.animationSpeed;
            weaveTime_ += phaseStep;
            if (state_.braid.autoRotate) {
                rotationTime_ += plan.valid
                                     ? phaseStep * static_cast<float>(plan.rotationPerPhase)
                                     : frame.deltaSeconds * state_.braid.rotationSpeed;
            }
        }
    }
    const VkExtent2D wanted = state_.viewport.lockedExtent.value_or(
        VkExtent2D{state_.viewport.requestedWidth, state_.viewport.requestedHeight});
    const VkExtent2D requested{
        std::clamp(wanted.width, 64U, 4096U),
        std::clamp(wanted.height, 64U, 4096U),
    };
    if (requested.width == state_.viewport.extent.width &&
        requested.height == state_.viewport.extent.height) {
        return;
    }
    context.vulkan.waitIdle();
    destroyRenderTarget();
    createRenderTarget(context, requested);
}

float GraphicsModule::latticeStep(const int pointCount) {
    // The search runs only when the point count slider moves.
    if (pointCount != latticeCount_) {
        latticeCount_ = pointCount;
        latticeStep_ = closedLatticeStep(pointCount);
    }
    return static_cast<float>(latticeStep_);
}

void GraphicsModule::onRender(AppContext& context, const FrameInfo&) {
    auto cpuScope = context.profiler.cpu().scope(metric_);
    auto gpuScope = context.profiler.gpu().scope(context.vulkan.commandBuffer(), metric_);
    const VkCommandBuffer commands = context.vulkan.commandBuffer();
    VkImageMemoryBarrier2 toAttachment{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toAttachment.srcStageMask = targetLayout_ == VK_IMAGE_LAYOUT_UNDEFINED
                                    ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
                                    : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toAttachment.srcAccessMask =
        targetLayout_ == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    toAttachment.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toAttachment.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toAttachment.oldLayout = targetLayout_;
    toAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toAttachment.image = target_.image();
    toAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toAttachment.subresourceRange.levelCount = 1;
    toAttachment.subresourceRange.layerCount = 1;
    VkImageMemoryBarrier2 toDepth{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toDepth.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toDepth.srcAccessMask =
        depthLayout_ == VK_IMAGE_LAYOUT_UNDEFINED
            ? 0
            : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toDepth.dstStageMask =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    toDepth.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toDepth.oldLayout = depthLayout_;
    toDepth.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    toDepth.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDepth.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDepth.image = depth_.image();
    toDepth.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    const std::array attachmentBarriers{toAttachment, toDepth};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = static_cast<std::uint32_t>(attachmentBarriers.size());
    dependency.pImageMemoryBarriers = attachmentBarriers.data();
    vkCmdPipelineBarrier2(commands, &dependency);

    const auto& color = state_.preset.clearColor;
    const VkClearColorValue clear{{color.r, color.g, color.b, color.a}};
    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = target_.view();
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = clear;
    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAttachment.imageView = depth_.view();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0F, 0};
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = state_.viewport.extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    rendering.pDepthAttachment = &depthAttachment;
    vkCmdBeginRendering(commands, &rendering);
    if (state_.preset.graphicsEnabled) {
        const VkExtent2D extent = state_.viewport.extent;
        const VkViewport viewport{
            0.0F, 0.0F, static_cast<float>(extent.width), static_cast<float>(extent.height),
            0.0F, 1.0F};
        const VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(commands, 0, 1, &viewport);
        vkCmdSetScissor(commands, 0, 1, &scissor);
        const auto& fx = state_.effects;
        const EffectsBlock effectsBlock{
            {fx.drift ? fx.driftAmount : 0.0F, fx.driftScale, fx.driftSpeed,
             fx.drift ? fx.jitter : 0.0F},
            {fx.shed ? fx.shedShare : 0.0F, fx.shedDistance, fx.shedRate, fx.shedFall},
            {fx.seeThrough ? 1.0F : 0.0F, 0.0F, 0.0F, 0.0F},
        };
        effects_.write(&effectsBlock, sizeof(effectsBlock));
        const auto& braid = state_.braid;
        const auto& tentacle = state_.tentacle;
        // Narrow enough for the tentacles' patches to sit side by side.
        const float tentacleWidth =
            std::min(tentacle.width, tentacleWidthLimit(tentacle.tentacles));
        const int tentaclePoints =
            tentaclePatchPoints(tentacle.spherePoints, tentacle.radius, tentacle.length,
                                tentacleWidth, tentacle.roundness);
        const auto& arched = state_.archedSphere;
        const ArchLattice archPoints =
            archLattice(arched.spherePoints, arched.radius, arched.archHeight, arched.tubeRadius,
                        arched.arches, arched.twist);
        const auto& cubes = state_.nestedCubes;
        const auto cubeRatio = static_cast<float>(
            nestedCubeRatio(cubes.sizeRatio, cubes.roundness, cubes.keepApart));
        const auto& polytope = state_.polytope;
        int polytopeAlong = 1;
        if (braid.geometryMode == 19) {
            // Rebuild the edges only when the figure changes; the colours are
            // rewritten every frame so palette edits show at once.
            const std::array key{polytope.shape, polytope.duoprismP, polytope.duoprismQ};
            if (key != polytopeKey_) {
                polytopeKey_ = key;
                const auto edges = polytopeEdges(static_cast<Polytope4D>(polytope.shape),
                                                 polytope.duoprismP, polytope.duoprismQ);
                polytopeEdgeCount_ = static_cast<int>(std::min<std::size_t>(edges.size(), 2048U));
                polytopeData_.assign(8U + static_cast<std::size_t>(polytopeEdgeCount_) * 12U, 0.0F);
                polytopeClasses_.resize(static_cast<std::size_t>(polytopeEdgeCount_));
                for (std::size_t edge = 0; edge < polytopeClasses_.size(); ++edge) {
                    const auto& source = edges[edge];
                    float* out = polytopeData_.data() + 8U + edge * 12U;
                    for (std::size_t i = 0; i < 4; ++i) {
                        out[i] = static_cast<float>(source.start[i]);
                        out[4 + i] = static_cast<float>(source.end[i]);
                    }
                    out[11] = static_cast<float>(source.part);
                    polytopeClasses_[edge] = {source.part, source.direction};
                }
            }
            // Parts or 4D directions pick their palette colour.
            const auto& colors = state_.palette.colors;
            const bool byDirection = polytope.colorMode == 2;
            for (std::size_t edge = 0; edge < polytopeClasses_.size(); ++edge) {
                const auto group = static_cast<std::size_t>(polytopeClasses_[edge][byDirection ? 1 : 0]);
                const auto& color = colors[group % colors.size()];
                std::copy(color.begin(), color.end(),
                          polytopeData_.begin() + static_cast<std::ptrdiff_t>(8U + edge * 12U + 8U));
            }
            const std::array shaping{polytope.edgeBend,
                                     polytope.coil,
                                     polytope.coilTurns,
                                     polytope.coilFlow,
                                     polytope.noise ? polytope.noiseAmount : 0.0F,
                                     polytope.noiseScale,
                                     polytope.noiseSpeed,
                                     0.0F};
            std::copy(shaping.begin(), shaping.end(), polytopeData_.begin());
            if (polytopeEdgeCount_ > 0) {
                polytope_.write(polytopeData_.data(), polytopeData_.size() * sizeof(float));
            }
            polytopeAlong = polytopePointsAlong(polytope.pointBudget, polytopeEdgeCount_,
                                                polytope.pointsAround);
        }
        std::array<float, 32> pushConstants{};
        if (braid.geometryMode == 16) {
            pushConstants = {
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                nestedSphereTime_,
                0.0F,
                cubes.outerSize,
                cubeRatio,
                cubes.border,
                cubes.roundness,
                static_cast<float>(cubes.pointCount),
                1.0F,
                cubes.pointSize,
                static_cast<float>(cubes.cubeCount),
                cubes.tilt,
                cubes.glow,
                cubes.brightness,
                0.0F,
                -1.0F,
                -1.0F,
                -1.0F,
                16.0F,
                cubes.speedVariation,
                cubes.zoom ? 1.0F : 0.0F,
                cubes.zoomRate,
                cubes.zoomTwist,
                static_cast<float>(cubes.directionSeed),
                0.0F,
                0.0F,
                0.0F,
                loopCycles_,
                cubes.perspective ? cubes.cameraDistance : 0.0F,
                cubes.sync && !cubes.zoom ? 1.0F + cubes.syncHold : 0.0F,
                cubes.syncFlash,
            };
        } else if (braid.geometryMode == 19) {
            pushConstants = {
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                eversionTime_,
                0.0F,
                polytope.size,
                polytope.tubeRadius,
                polytope.edgeBend > 0.5F ? polytope.cameraDistance
                                         : std::max(polytope.cameraDistance, 1.3F),
                polytope.turnXW,
                static_cast<float>(polytopeAlong),
                static_cast<float>(polytope.pointsAround),
                polytope.pointSize,
                static_cast<float>(polytopeEdgeCount_),
                polytope.tilt,
                polytope.glow,
                polytope.brightness,
                polytope.turnYZ,
                polytope.nearColor[0],
                polytope.nearColor[1],
                polytope.nearColor[2],
                19.0F,
                polytope.farColor[0],
                polytope.farColor[1],
                polytope.farColor[2],
                polytope.spin,
                polytope.turnZW,
                static_cast<float>(polytope.colorMode),
                polytope.slice,
                polytope.sliceWidth,
                loopCycles_,
                polytope.lightStrength,
                polytope.counterTurn ? 2.0F : 0.0F,
                polytope.lightFlow,
            };
        } else if (braid.geometryMode == 18) {
            const auto& tesseract = state_.tesseract;
            pushConstants = {
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                eversionTime_,
                0.0F,
                tesseract.size,
                tesseract.tubeRadius,
                tesseract.cameraDistance,
                tesseract.turnXW,
                static_cast<float>(tesseract.pointsAlong),
                static_cast<float>(tesseract.pointsAround),
                tesseract.pointSize,
                32.0F,
                tesseract.tilt,
                tesseract.glow,
                tesseract.brightness,
                tesseract.turnYZ,
                -1.0F,
                -1.0F,
                -1.0F,
                18.0F,
                tesseract.nearColor[0],
                tesseract.nearColor[1],
                tesseract.nearColor[2],
                tesseract.spin,
                tesseract.farColor[0],
                tesseract.farColor[1],
                tesseract.farColor[2],
                tesseract.lightFlow,
                loopCycles_,
                tesseract.lightStrength,
                0.0F,
                0.0F,
            };
        } else if (braid.geometryMode == 17) {
            const auto& hopf = state_.hopf;
            pushConstants = {
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                eversionTime_,
                0.0F,
                hopf.scale,
                hopf.tubeRadius,
                hopf.tumble,
                hopf.tumbleRate,
                static_cast<float>(hopf.pointsAlong),
                static_cast<float>(hopf.pointsAround),
                hopf.pointSize,
                static_cast<float>(hopf.fibers),
                hopf.tilt,
                hopf.glow,
                hopf.brightness,
                static_cast<float>(hopf.tori),
                -1.0F,
                -1.0F,
                -1.0F,
                17.0F,
                hopf.innerLatitude,
                hopf.outerLatitude,
                hopf.flow,
                hopf.drift,
                hopf.spin,
                0.0F,
                hopf.breathing,
                0.0F,
                loopCycles_,
                0.0F,
                0.0F,
                0.0F,
            };
        } else if (braid.geometryMode == 4) {
            const auto& spheres = state_.nestedSpheres;
            pushConstants = {
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                nestedSphereTime_,
                0.0F,
                spheres.outerRadius,
                spheres.minimumRadiusRatio,
                spheres.holeAngle,
                static_cast<float>(spheres.holeCount),
                static_cast<float>(spheres.pointCount),
                1.0F,
                spheres.pointSize,
                static_cast<float>(spheres.sphereCount),
                spheres.tilt,
                spheres.glow,
                spheres.brightness,
                spheres.radiusCurve,
                -1.0F,
                -1.0F,
                -1.0F,
                4.0F,
                spheres.speedVariation,
                0.0F,
                0.0F,
                0.0F,
                static_cast<float>(spheres.directionSeed),
                0.0F,
                0.0F,
                spheres.offsetCenters ? spheres.centerOffset : 0.0F,
                loopCycles_,
                0.0F,
                0.0F,
                0.0F,
            };
        } else if (braid.geometryMode == 5) {
            const auto& eversion = state_.eversion;
            pushConstants = {
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                eversionTime_,
                0.0F,
                eversion.radius,
                eversion.holeAngle,
                eversion.endHold,
                eversion.spin,
                static_cast<float>(eversion.pointCount),
                1.0F,
                eversion.pointSize,
                1.0F,
                eversion.tilt,
                eversion.glow,
                eversion.brightness,
                0.0F,
                -1.0F,
                -1.0F,
                -1.0F,
                5.0F,
                eversion.outerColor[0],
                eversion.outerColor[1],
                eversion.outerColor[2],
                0.0F,
                eversion.innerColor[0],
                eversion.innerColor[1],
                eversion.innerColor[2],
                0.0F,
                loopCycles_,
                0.0F,
                0.0F,
                0.0F,
            };
        } else if (braid.geometryMode == 6) {
            const auto& sleeve = state_.sleeve;
            pushConstants = {
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                eversionTime_,
                0.0F,
                sleeve.outerRadius,
                sleeve.halfLength,
                sleeve.innerRatio,
                static_cast<float>(sleeve.bands),
                static_cast<float>(sleeve.pointCount),
                1.0F,
                sleeve.pointSize,
                1.0F,
                sleeve.tilt,
                sleeve.glow,
                sleeve.brightness,
                0.0F,
                -1.0F,
                -1.0F,
                -1.0F,
                6.0F,
                sleeve.firstColor[0],
                sleeve.firstColor[1],
                sleeve.firstColor[2],
                sleeve.spin,
                sleeve.secondColor[0],
                sleeve.secondColor[1],
                sleeve.secondColor[2],
                0.0F,
                loopCycles_,
                0.0F,
                latticeStep(sleeve.pointCount),
                0.0F,
            };
        } else if (braid.geometryMode == 7) {
            const auto& morph = state_.morph;
            pushConstants = {
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                eversionTime_,
                0.0F,
                morph.size,
                morph.holeRatio,
                morph.roundness,
                static_cast<float>(morph.bands),
                static_cast<float>(morph.pointCount),
                morph.halfLength,
                morph.pointSize,
                1.0F,
                morph.tilt,
                morph.glow,
                morph.brightness,
                morph.morphAmount,
                -1.0F,
                -1.0F,
                -1.0F,
                7.0F,
                morph.firstColor[0],
                morph.firstColor[1],
                morph.firstColor[2],
                morph.spin,
                morph.secondColor[0],
                morph.secondColor[1],
                morph.secondColor[2],
                morph.morphRate,
                loopCycles_,
                0.0F,
                latticeStep(morph.pointCount),
                0.0F,
            };
        } else if (braid.geometryMode == 8) {
            const auto& vortex = state_.vortex;
            pushConstants = {
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                eversionTime_,
                0.0F,
                vortex.ringRadius,
                vortex.coilRadius,
                vortex.tubeRadius,
                static_cast<float>(vortex.twist),
                static_cast<float>(vortex.majorPointCount),
                static_cast<float>(vortex.minorPointCount),
                vortex.pointSize,
                static_cast<float>(vortex.strands),
                vortex.tilt,
                vortex.glow,
                vortex.brightness,
                vortex.twistWave,
                -1.0F,
                -1.0F,
                -1.0F,
                8.0F,
                vortex.holeColor[0],
                vortex.holeColor[1],
                vortex.holeColor[2],
                vortex.spin,
                vortex.rimColor[0],
                vortex.rimColor[1],
                vortex.rimColor[2],
                0.0F,
                loopCycles_,
                vortex.ridges.packed()[0],
                vortex.ridges.packed()[1],
                vortex.ridges.packed()[2],
            };
        } else if (braid.geometryMode == 15) {
            pushConstants = {
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                eversionTime_,
                0.0F,
                arched.radius,
                arched.archHeight,
                arched.tubeRadius,
                static_cast<float>(arched.arches),
                static_cast<float>(arched.spherePoints),
                static_cast<float>(archPoints.rows) +
                    static_cast<float>(archPoints.columns) / 128.0F,
                arched.pointSize,
                latticeStep(arched.spherePoints),
                arched.tilt,
                arched.glow,
                arched.brightness,
                arched.twist,
                -1.0F,
                -1.0F,
                -1.0F,
                15.0F,
                arched.firstColor[0],
                arched.firstColor[1],
                arched.firstColor[2],
                arched.spin,
                arched.secondColor[0],
                arched.secondColor[1],
                arched.secondColor[2],
                0.0F,
                loopCycles_,
                static_cast<float>(arched.bands),
                arched.checker ? static_cast<float>(arched.sectors) : 0.0F,
                0.0F,
            };
        } else if (braid.geometryMode == 14) {
            const auto& sphere = state_.sphereEversion;
            pushConstants = {
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                eversionTime_,
                0.0F,
                sphere.radius,
                sphere.endHold,
                0.0F,
                0.0F,
                static_cast<float>(sphere.pointCount),
                1.0F,
                sphere.pointSize,
                1.0F,
                sphere.tilt,
                sphere.glow,
                sphere.brightness,
                0.0F,
                -1.0F,
                -1.0F,
                -1.0F,
                14.0F,
                sphere.firstColor[0],
                sphere.firstColor[1],
                sphere.firstColor[2],
                sphere.spin,
                sphere.secondColor[0],
                sphere.secondColor[1],
                sphere.secondColor[2],
                0.0F,
                loopCycles_,
                0.0F,
                0.0F,
                0.0F,
            };
        } else if (braid.geometryMode == 12 || braid.geometryMode == 13) {
            // Drawn by the ridged torus shader, with bumps in place of ridges;
            // the chain draws it twice, as two links.
            const bool chain = braid.geometryMode == 13;
            const auto& torus = chain ? state_.torusChain : state_.bumpyTorus;
            pushConstants = {
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                eversionTime_,
                0.0F,
                torus.ringRadius,
                chain ? std::min(torus.tubeRadius, torusChainTubeLimit(torus)) : torus.tubeRadius,
                torus.bumpHeight,
                static_cast<float>(torus.bumpsAlong),
                static_cast<float>(torus.pointCount),
                static_cast<float>(torus.rowShift),
                torus.pointSize,
                torus.roundness,
                torus.tilt,
                torus.glow,
                torus.brightness,
                torus.bumpSize,
                chain ? 0.0F : -1.0F,
                -1.0F,
                -1.0F,
                9.0F,
                torus.bumpColor[0],
                torus.bumpColor[1],
                torus.bumpColor[2],
                torus.spin,
                torus.bodyColor[0],
                torus.bodyColor[1],
                torus.bodyColor[2],
                torus.pulseRate,
                loopCycles_,
                torus.pulseDepth,
                latticeStep(torus.pointCount),
                static_cast<float>(torus.bumpsAround),
            };
        } else if (braid.geometryMode == 11) {
            pushConstants = {
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                eversionTime_,
                0.0F,
                tentacle.radius,
                tentacle.length,
                tentacleWidth,
                static_cast<float>(tentacle.tentacles),
                static_cast<float>(tentacle.spherePoints),
                static_cast<float>(tentaclePoints),
                tentacle.pointSize,
                tentacle.roundness,
                tentacle.tilt,
                tentacle.glow,
                tentacle.brightness,
                tentacle.rippleHeight,
                -1.0F,
                -1.0F,
                -1.0F,
                11.0F,
                tentacle.tipColor[0],
                tentacle.tipColor[1],
                tentacle.tipColor[2],
                tentacle.spin,
                tentacle.bodyColor[0],
                tentacle.bodyColor[1],
                tentacle.bodyColor[2],
                0.0F,
                loopCycles_,
                tentacle.ripples,
                tentacle.swirl,
                tentacle.sway,
            };
        } else if (braid.geometryMode == 9) {
            const auto& torus = state_.ridgedTorus;
            pushConstants = {
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                eversionTime_,
                0.0F,
                torus.ringRadius,
                torus.tubeRadius,
                torus.ridgeHeight,
                static_cast<float>(torus.ridges),
                static_cast<float>(torus.pointCount),
                static_cast<float>(torus.twist),
                torus.pointSize,
                torus.sharpness,
                torus.tilt,
                torus.glow,
                torus.brightness,
                torus.twistWave,
                -1.0F,
                -1.0F,
                -1.0F,
                9.0F,
                torus.ridgeColor[0],
                torus.ridgeColor[1],
                torus.ridgeColor[2],
                torus.spin,
                torus.bodyColor[0],
                torus.bodyColor[1],
                torus.bodyColor[2],
                torus.pulseRate,
                loopCycles_,
                torus.pulseDepth,
                latticeStep(torus.pointCount),
                0.0F,
            };
        } else {
            pushConstants = {
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                weaveTime_,
                rotationTime_,
                braid.majorRadius,
                braid.weaveRadius,
                braid.tubeRadius,
                static_cast<float>(braid.twists),
                static_cast<float>(braid.majorPointCount),
                static_cast<float>(braid.minorPointCount),
                braid.pointSize,
                static_cast<float>(braid.strands),
                braid.tilt,
                braid.glow,
                braid.brightness,
                braid.radiusVariation,
                -1.0F,
                -1.0F,
                -1.0F,
                static_cast<float>(motionGeometryMode(state_)),
                braid.releaseStrength,
                braid.releaseWidth,
                braid.releaseSpeed,
                braid.squareness,
                braid.wholeLoopTorsion,
                braid.torsionCompression,
                braid.materialCirculation,
                braid.limitTubeOverlap ? 1.0F : 0.0F,
                loopCycles_,
                0.0F,
                0.0F,
                0.0F,
            };
            if (braid.geometryMode == 10) {
                const auto ridges = state_.ridgedBraid.ridges.packed();
                std::copy(ridges.begin(), ridges.end(), pushConstants.begin() + 29);
            }
        }
        vkCmdPushConstants(commands, pipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           static_cast<std::uint32_t>(sizeof(pushConstants)), pushConstants.data());
        // Both passes use the same periodic surface and flow phase. The closed
        // depth-only skin hides rear points even in the gaps between sprites.
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, surfacePipeline_);
        // The polytope's skin reads its edges; the depth image in the same set
        // is not touched by the skin shaders.
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                                &depthSet_, 0, nullptr);
        const auto objectCount = static_cast<std::uint32_t>(
            braid.geometryMode == 4    ? state_.nestedSpheres.sphereCount
            : braid.geometryMode == 16 ? cubes.cubeCount + (cubes.zoom ? 1 : 0)
            : braid.geometryMode == 17 ? state_.hopf.tori * state_.hopf.fibers
            : braid.geometryMode == 18 ? 32
            : braid.geometryMode == 19 ? polytopeEdgeCount_
            : braid.geometryMode == 11 ? tentacle.tentacles + 1
            : braid.geometryMode == 15 ? arched.arches + 1
            : braid.geometryMode == 8  ? state_.vortex.strands
            : braid.geometryMode == 13 ? 2
            : (braid.geometryMode >= 5 && braid.geometryMode <= 9) || braid.geometryMode == 12 ||
                      braid.geometryMode == 14
                ? 1
                                      : braid.strands);
        // See-through leaves the depth clear, so nothing hides any point.
        if (!fx.seeThrough) {
            // A tube of the Hopf fibration or the tesseract needs far fewer
            // cells than the default grid.
            vkCmdDraw(commands,
                      braid.geometryMode == 19 ? static_cast<std::uint32_t>(polytopeAlong) * 12U * 6U
                      : braid.geometryMode == 17 || braid.geometryMode == 18 ? 240U * 24U * 6U
                                                                             : 720U * 64U * 6U,
                      objectCount, 0, 0);
        }
        vkCmdEndRendering(commands);

        VkImageMemoryBarrier2 sampleDepth{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        sampleDepth.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        sampleDepth.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sampleDepth.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        sampleDepth.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        sampleDepth.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        sampleDepth.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        sampleDepth.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        sampleDepth.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        sampleDepth.image = depth_.image();
        sampleDepth.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        // Preserve the colour clear across the two separate rendering scopes.
        VkImageMemoryBarrier2 preserveColor = toAttachment;
        preserveColor.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        preserveColor.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        preserveColor.dstAccessMask =
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        preserveColor.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        const std::array sampleBarriers{sampleDepth, preserveColor};
        dependency.imageMemoryBarrierCount = static_cast<std::uint32_t>(sampleBarriers.size());
        dependency.pImageMemoryBarriers = sampleBarriers.data();
        vkCmdPipelineBarrier2(commands, &dependency);
        depthLayout_ = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        rendering.pDepthAttachment = nullptr;
        vkCmdBeginRendering(commands, &rendering);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                                &depthSet_, 0, nullptr);
        const auto drawPoints = [&] {
        if (braid.geometryMode == 4) {
            const auto& spheres = state_.nestedSpheres;
            for (std::uint32_t sphere = 0; sphere < objectCount; ++sphere) {
                const float sequence = static_cast<float>(sphere) /
                                       static_cast<float>(std::max(spheres.sphereCount - 1, 1));
                const float radiusRatio = 1.0F - (1.0F - spheres.minimumRadiusRatio) *
                                                     std::pow(sequence, spheres.radiusCurve);
                const auto spherePointCount = static_cast<std::uint32_t>(
                    std::max(1L, std::lround(static_cast<float>(spheres.pointCount) * radiusRatio *
                                             radiusRatio)));
                const auto& tint = state_.palette.colors[sphere % state_.palette.colors.size()];
                vkCmdPushConstants(commands, pipelineLayout_,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   16U * sizeof(float), 3U * sizeof(float), tint.data());
                const std::array sphereDraw{static_cast<float>(sphere),
                                            static_cast<float>(spherePointCount)};
                vkCmdPushConstants(
                    commands, pipelineLayout_,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 25U * sizeof(float),
                    static_cast<std::uint32_t>(sizeof(sphereDraw)), sphereDraw.data());
                vkCmdDraw(commands, 6, spherePointCount, 0, 0);
            }
        } else if (braid.geometryMode == 16) {
            // One draw per cube, like the spheres: its index, count and palette tint.
            // While zooming every cube carries the outer one's points and takes
            // its colour from its depth, so the colours flow outwards with it.
            const double zoomOffset =
                cubes.zoom ? cubeZoomOffset(nestedSphereTime_, cubes.zoomRate, loopCycles_) : 0.0;
            const auto& colors = state_.palette.colors;
            for (std::uint32_t cube = 0; cube < objectCount; ++cube) {
                const auto cubePointCount = static_cast<std::uint32_t>(nestedCubePoints(
                    cubes.pointCount, cubes.border,
                    cubes.zoom ? 1.0
                               : std::pow(static_cast<double>(cubeRatio),
                                          static_cast<double>(cube))));
                const double place = std::fmod(static_cast<double>(cube) - zoomOffset +
                                                    static_cast<double>(colors.size()),
                                                static_cast<double>(colors.size()));
                const auto lower = static_cast<std::size_t>(place);
                const auto& from = colors[lower % colors.size()];
                const auto& to = colors[(lower + 1) % colors.size()];
                const auto blend = static_cast<float>(place - std::floor(place));
                const std::array tint{from[0] + (to[0] - from[0]) * blend,
                                      from[1] + (to[1] - from[1]) * blend,
                                      from[2] + (to[2] - from[2]) * blend};
                vkCmdPushConstants(commands, pipelineLayout_,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   16U * sizeof(float), 3U * sizeof(float), tint.data());
                const std::array cubeDraw{static_cast<float>(cube),
                                          static_cast<float>(cubePointCount)};
                vkCmdPushConstants(
                    commands, pipelineLayout_,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 25U * sizeof(float),
                    static_cast<std::uint32_t>(sizeof(cubeDraw)), cubeDraw.data());
                vkCmdDraw(commands, 6, cubePointCount, 0, 0);
            }
        } else if (braid.geometryMode == 19) {
            vkCmdDraw(commands, 6,
                      static_cast<std::uint32_t>(polytopeEdgeCount_ * polytopeAlong *
                                                 polytope.pointsAround),
                      0, 0);
        } else if (braid.geometryMode == 18) {
            const auto& tesseract = state_.tesseract;
            vkCmdDraw(commands, 6,
                      static_cast<std::uint32_t>(32 * tesseract.pointsAlong *
                                                 tesseract.pointsAround),
                      0, 0);
        } else if (braid.geometryMode == 17) {
            // One draw per torus with its palette colour; the shader reads which.
            const auto& hopf = state_.hopf;
            const auto torusPoints =
                static_cast<std::uint32_t>(hopf.fibers * hopf.pointsAlong * hopf.pointsAround);
            for (int torus = 0; torus < hopf.tori; ++torus) {
                const auto& tint =
                    state_.palette.colors[static_cast<std::size_t>(torus) %
                                          state_.palette.colors.size()];
                vkCmdPushConstants(commands, pipelineLayout_,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   16U * sizeof(float), 3U * sizeof(float), tint.data());
                const auto torusIndex = static_cast<float>(torus);
                vkCmdPushConstants(commands, pipelineLayout_,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   25U * sizeof(float), sizeof(float), &torusIndex);
                vkCmdDraw(commands, 6, torusPoints, 0, 0);
            }
        } else if (braid.geometryMode == 5) {
            vkCmdDraw(commands, 6, static_cast<std::uint32_t>(state_.eversion.pointCount), 0, 0);
        } else if (braid.geometryMode == 6) {
            vkCmdDraw(commands, 6, static_cast<std::uint32_t>(state_.sleeve.pointCount), 0, 0);
        } else if (braid.geometryMode == 7) {
            vkCmdDraw(commands, 6, static_cast<std::uint32_t>(state_.morph.pointCount), 0, 0);
        } else if (braid.geometryMode == 12) {
            vkCmdDraw(commands, 6, static_cast<std::uint32_t>(state_.bumpyTorus.pointCount), 0, 0);
        } else if (braid.geometryMode == 15) {
            // Sphere first, then the arches; one push constant says which part.
            vkCmdDraw(commands, 6, static_cast<std::uint32_t>(arched.spherePoints), 0, 0);
            const float archPart = 1.0F;
            vkCmdPushConstants(commands, pipelineLayout_,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               27U * sizeof(float), sizeof(float), &archPart);
            vkCmdDraw(commands, 6,
                      static_cast<std::uint32_t>(archPoints.rows * archPoints.columns *
                                                 arched.arches),
                      0, 0);
        } else if (braid.geometryMode == 14) {
            vkCmdDraw(commands, 6, static_cast<std::uint32_t>(state_.sphereEversion.pointCount), 0,
                      0);
        } else if (braid.geometryMode == 13) {
            const auto linkPoints = static_cast<std::uint32_t>(state_.torusChain.pointCount);
            vkCmdDraw(commands, 6, linkPoints, 0, 0);
            const float secondLink = 1.0F;
            vkCmdPushConstants(commands, pipelineLayout_,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               16U * sizeof(float), sizeof(float), &secondLink);
            vkCmdDraw(commands, 6, linkPoints, 0, 0);
        } else if (braid.geometryMode == 11) {
            // Body first, then every tentacle's own lattice; the shader reads
            // which part it is drawing from one push constant.
            vkCmdDraw(commands, 6, static_cast<std::uint32_t>(tentacle.spherePoints), 0, 0);
            const float tentaclePart = 1.0F;
            vkCmdPushConstants(commands, pipelineLayout_,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               27U * sizeof(float), sizeof(float), &tentaclePart);
            vkCmdDraw(commands, 6,
                      static_cast<std::uint32_t>(tentaclePoints * tentacle.tentacles), 0, 0);
        } else if (braid.geometryMode == 9) {
            vkCmdDraw(commands, 6, static_cast<std::uint32_t>(state_.ridgedTorus.pointCount), 0,
                      0);
        } else if (braid.geometryMode == 8) {
            const auto& vortex = state_.vortex;
            vkCmdDraw(commands, 6,
                      static_cast<std::uint32_t>(vortex.majorPointCount * vortex.minorPointCount *
                                                 vortex.strands),
                      0, 0);
        } else if (state_.palette.enabled) {
            const auto pointsPerStrand =
                static_cast<std::uint32_t>(braid.majorPointCount * braid.minorPointCount);
            // Draw each stable strand ID with its palette entry. firstInstance
            // preserves the original material coordinates in the vertex shader.
            // Reuse the existing style RGB push constants (no larger GPU block).
            for (std::uint32_t strand = 0; strand < objectCount; ++strand) {
                const auto& tint = state_.palette.colors[strand % state_.palette.colors.size()];
                vkCmdPushConstants(commands, pipelineLayout_,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   16U * sizeof(float), 3U * sizeof(float), tint.data());
                vkCmdDraw(commands, 6, pointsPerStrand, 0, strand * pointsPerStrand);
            }
        } else {
            const auto pointsPerStrand =
                static_cast<std::uint32_t>(braid.majorPointCount * braid.minorPointCount);
            vkCmdDraw(commands, 6, pointsPerStrand * static_cast<std::uint32_t>(braid.strands), 0,
                      0);
        }
        };
        // Trails: the same points at earlier moments, oldest first, each smaller
        // and dimmer; the present is drawn last, on top. Every pass starts from
        // the whole block because the draws above overwrite parts of it.
        const auto& effects = state_.effects;
        const int ghosts = effects.trails ? effects.trailCount : 0;
        for (int ghost = ghosts; ghost >= 0; --ghost) {
            auto pass = pushConstants;
            const auto age = static_cast<float>(ghost);
            pass[2] -= age * effects.trailSpacing;
            pass[10] *= 1.0F - age / static_cast<float>(ghosts + 1);
            pass[14] *= std::pow(effects.trailFade, age);
            vkCmdPushConstants(commands, pipelineLayout_,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               static_cast<std::uint32_t>(sizeof(pass)), pass.data());
            drawPoints();
        }
    }
    vkCmdEndRendering(commands);
    if (!state_.preset.graphicsEnabled) {
        depthLayout_ = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    VkImageMemoryBarrier2 toSampled{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toSampled.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toSampled.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toSampled.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toSampled.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    toSampled.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toSampled.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toSampled.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSampled.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSampled.image = target_.image();
    toSampled.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toSampled.subresourceRange.levelCount = 1;
    toSampled.subresourceRange.layerCount = 1;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &toSampled;
    vkCmdPipelineBarrier2(commands, &dependency);
    targetLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void GraphicsModule::onDetach(AppContext&) {
    destroyRenderTarget();
    pipeline_.reset();
    surfacePipeline_.reset();
    pipelineLayout_.reset();
    depthSet_ = VK_NULL_HANDLE;
    depthPool_.reset();
    depthSetLayout_.reset();
    effects_.reset();
    polytope_.reset();
}

} // namespace vkexp
