#include "vkexp/graphics/GraphicsModule.hpp"

#include "vkexp/core/VulkanContext.hpp"
#include "vkexp/demo/DemoState.hpp"
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

    const VkDescriptorSetLayoutBinding depthBinding{
        0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
        VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = 1;
    setInfo.pBindings = &depthBinding;
    if (vkCreateDescriptorSetLayout(device, &setInfo, nullptr, depthSetLayout_.put(device)) !=
        VK_SUCCESS) {
        throw std::runtime_error("Unable to create point visibility descriptor layout");
    }
    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
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
        if (state_.braid.geometryMode == 4) {
            nestedSphereTime_ += static_cast<float>(recording->phaseStep);
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
        const auto& braid = state_.braid;
        std::array<float, 32> pushConstants{};
        if (braid.geometryMode == 4) {
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
                static_cast<float>(braid.geometryMode),
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
        }
        vkCmdPushConstants(commands, pipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           static_cast<std::uint32_t>(sizeof(pushConstants)), pushConstants.data());
        // Both passes use the same periodic surface and flow phase. The closed
        // depth-only skin hides rear points even in the gaps between sprites.
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, surfacePipeline_);
        const auto objectCount = static_cast<std::uint32_t>(
            braid.geometryMode == 4 ? state_.nestedSpheres.sphereCount : braid.strands);
        vkCmdDraw(commands, 720U * 64U * 6U, objectCount, 0, 0);
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
}

} // namespace vkexp
