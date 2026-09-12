#include "vkexp/demo/DemoUiModule.hpp"

#include "vkexp/demo/DemoState.hpp"
#include "vkexp/profiling/Profiler.hpp"
#include "vkexp/ui/ImGuiModule.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace vkexp {

DemoUiModule::DemoUiModule(DemoState& state, ImGuiModule& imgui, Profiler& profiler)
    : state_(state), imgui_(imgui), metric_(profiler.registerMetric("Demo UI")) {}

void DemoUiModule::onAttach(AppContext&) { syncTextures(); }

void DemoUiModule::syncTextures() {
    if (viewportGeneration_ != state_.viewport.generation) {
        imgui_.removeTexture(viewportDescriptor_);
        viewportDescriptor_ = imgui_.addTexture(state_.viewport.sampler, state_.viewport.imageView,
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        viewportGeneration_ = state_.viewport.generation;
    }
    if (blurGeneration_ != state_.blur.generation) {
        imgui_.removeTexture(blurDescriptor_);
        blurDescriptor_ = imgui_.addTexture(state_.blur.sampler, state_.blur.imageView,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        blurGeneration_ = state_.blur.generation;
    }
}

void DemoUiModule::onUpdate(AppContext& context, const FrameInfo& frame) {
    auto cpuScope = context.profiler.cpu().scope(metric_);
    syncTextures();

    ImGui::SetNextWindowPos(ImVec2(20.0F, 20.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350.0F, 610.0F), ImGuiCond_FirstUseEver);
    ImGui::Begin("Controls");
    ImGui::Text("Preset: %s", state_.preset.name.c_str());
    ImGui::Text("Frame: %llu", static_cast<unsigned long long>(frame.frameNumber));
    ImGui::Text("%.2f ms (%.1f FPS)", frame.deltaSeconds * 1000.0F,
                frame.deltaSeconds > 0.0F ? 1.0F / frame.deltaSeconds : 0.0F);
    ImGui::Separator();
    ImGui::Checkbox("Graphics pipeline", &state_.preset.graphicsEnabled);
    ImGui::Checkbox("Compute pipeline", &state_.preset.computeEnabled);
    ImGui::ColorEdit3("Clear color", &state_.preset.clearColor.x);

    ImGui::SeparatorText("Point braid");
    auto& braid = state_.braid;
    ImGui::Combo("Geometry", &braid.geometryMode,
                 "Plait\0Twisted bundle\0Torsion loop\0Orbital bloom\0");
    if (ImGui::Button("Orbital bloom preset")) {
        braid = BraidSettings{};
        braid.geometryMode = 3;
        braid.majorRadius = 1.20F;
        braid.weaveRadius = 0.25F;
        braid.tubeRadius = 0.145F;
        braid.twists = 2;
        braid.radiusVariation = 0.25F;
        braid.tilt = 0.28F;
        braid.animationSpeed = 0.55F;
    }
    ImGui::Checkbox("Pause", &braid.paused);
    ImGui::SameLine();
    ImGui::Checkbox("Auto rotate", &braid.autoRotate);
    ImGui::SliderFloat("Flow speed", &braid.animationSpeed, 0.0F, 2.0F, "%.2f");
    ImGui::BeginDisabled(!braid.autoRotate);
    ImGui::SliderFloat("Rotation speed", &braid.rotationSpeed, -0.5F, 0.5F, "%.2f");
    ImGui::EndDisabled();
    ImGui::SliderInt("Strands", &braid.strands, 2, 5);
    ImGui::SliderInt("Twists", &braid.twists, 1, 6);
    ImGui::SliderFloat("Ring radius", &braid.majorRadius, 0.9F, 1.5F, "%.2f");
    if (braid.geometryMode != 3) {
        ImGui::SliderFloat("Square shape", &braid.squareness, 2.0F, 6.0F, "%.2f");
    }
    ImGui::SliderFloat("Weave radius", &braid.weaveRadius, 0.15F, 0.52F, "%.2f");
    ImGui::SliderFloat("Tube radius", &braid.tubeRadius, 0.12F, 0.32F, "%.3f");
    if (braid.geometryMode != 0) {
        ImGui::Checkbox("Limit tube overlap", &braid.limitTubeOverlap);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Caps thickness using strand spacing and twist.\n"
                "Off: Tube radius controls thickness directly; large tubes may overlap.");
        }
    }
    ImGui::SliderFloat("Radius variation", &braid.radiusVariation, 0.0F, 0.55F, "%.2f");
    if (braid.geometryMode == 2) {
        ImGui::SliderFloat("Whole-loop twist", &braid.wholeLoopTorsion, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Compression wave", &braid.torsionCompression, 0.0F, 1.2F, "%.2f");
        ImGui::SliderFloat("Circulation", &braid.materialCirculation, -0.4F, 0.4F, "%.2f");
    } else if (braid.geometryMode != 3) {
        ImGui::SliderFloat("Unravel wave", &braid.releaseStrength, 0.0F, 0.95F, "%.2f");
        ImGui::SliderFloat("Wave width", &braid.releaseWidth, 0.3F, 1.1F, "%.2f");
    }
    if (braid.geometryMode != 3) {
        ImGui::SliderFloat("Wave travel", &braid.releaseSpeed, 0.3F, 2.0F, "%.2f");
    }
    ImGui::SliderInt("Points along", &braid.majorPointCount, 120, 720);
    ImGui::SliderInt("Points around", &braid.minorPointCount, 16, 64);
    // Even column counts close the staggered sampling pattern without a seam.
    braid.minorPointCount += braid.minorPointCount % 2;
    ImGui::SliderFloat("Point radius (700px)", &braid.pointSize, 0.6F, 3.0F, "%.2f");
    ImGui::SliderFloat("Glow", &braid.glow, 0.0F, 1.5F, "%.2f");
    ImGui::SliderFloat("Brightness", &braid.brightness, 0.5F, 2.0F, "%.2f");
    ImGui::Checkbox("Color per tube", &state_.palette.enabled);
    if (state_.palette.enabled) {
        for (std::size_t i = 0; i < state_.palette.colors.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("Tube %d", static_cast<int>(i + 1));
            ImGui::SameLine();
            ImGui::ColorEdit3("##Tube color", state_.palette.colors[i].data());
            ImGui::PopID();
        }
        if (ImGui::Button("Reset palette")) {
            state_.palette.colors = StrandPalette{}.colors;
        }
        if (braid.strands < 5) {
            ImGui::TextDisabled("Set Strands to 5 to use all five colors.");
        }
    }
    ImGui::SliderAngle("Tilt", &braid.tilt, -35.0F, 35.0F);
    if (ImGui::Button("Reset braid")) {
        braid = BraidSettings{};
    }

    ImGui::SeparatorText("Compute blur");
    ImGui::SliderInt("Blur radius", &state_.blur.radius, 1, 12);
    ImGui::BeginDisabled(!state_.preset.computeEnabled);
    if (ImGui::Button("Start")) {
        state_.blur.requested = true;
        state_.blur.ready = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextUnformatted(state_.blur.ready ? "Result ready" : "Waiting");
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(360.0F, 20.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430.0F, 640.0F), ImGuiCond_FirstUseEver);
    ImGui::Begin("Viewport");
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.x >= 64.0F && available.y >= 64.0F) {
        state_.viewport.requestedWidth = static_cast<std::uint32_t>(std::floor(available.x));
        state_.viewport.requestedHeight = static_cast<std::uint32_t>(std::floor(available.y));
        const ImTextureID texture =
            static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(viewportDescriptor_));
        ImGui::Image(texture, available);
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(810.0F, 20.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430.0F, 640.0F), ImGuiCond_FirstUseEver);
    ImGui::Begin("Blur Output");
    const ImVec2 availableBlur = ImGui::GetContentRegionAvail();
    if (availableBlur.x > 1.0F && availableBlur.y > 1.0F && state_.blur.ready &&
        blurDescriptor_ != VK_NULL_HANDLE && state_.blur.extent.width > 0 &&
        state_.blur.extent.height > 0) {
        const float scale =
            std::min(availableBlur.x / static_cast<float>(state_.blur.extent.width),
                     availableBlur.y / static_cast<float>(state_.blur.extent.height));
        const ImVec2 imageSize{
            static_cast<float>(state_.blur.extent.width) * scale,
            static_cast<float>(state_.blur.extent.height) * scale,
        };
        const ImTextureID texture =
            static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(blurDescriptor_));
        ImGui::Image(texture, imageSize);
    } else {
        ImGui::TextDisabled("Press Start to run the compute blur.");
    }
    ImGui::End();

    profilerPanel_.draw(context.profiler);
}

void DemoUiModule::onDetach(AppContext&) {
    imgui_.removeTexture(blurDescriptor_);
    imgui_.removeTexture(viewportDescriptor_);
    blurDescriptor_ = VK_NULL_HANDLE;
    viewportDescriptor_ = VK_NULL_HANDLE;
}

} // namespace vkexp
