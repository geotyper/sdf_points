#include "vkexp/demo/DemoUiModule.hpp"

#include "vkexp/demo/DemoState.hpp"
#include "vkexp/demo/TentaclePatch.hpp"
#include "vkexp/profiling/Profiler.hpp"
#include "vkexp/ui/ImGuiModule.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>

namespace vkexp {
namespace {

void drawTubeRidges(TubeRidges& ridges) {
    ImGui::SliderFloat("Ridge depth", &ridges.depth, 0.0F, 1.0F, "%.2f");
    ImGui::BeginDisabled(ridges.depth <= 0.0F);
    ImGui::SliderInt("Ridges around", &ridges.around, 0, 8);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("0 with ridges along: beads running down the tube.");
    }
    ImGui::SliderInt("Ridges along", &ridges.along, -32, 64);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Crests per lap of the ring; with ridges around\n"
                          "they become screw threads. Negative reverses the screw.");
    }
    ImGui::SliderFloat("Ridge travel", &ridges.travelRate, -4.0F, 4.0F, "%.2f");
    ImGui::EndDisabled();
}

} // namespace

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
    ImGui::Text("%.2f ms (%.1f FPS)", frame.realDeltaSeconds * 1000.0F,
                frame.realDeltaSeconds > 0.0F ? 1.0F / frame.realDeltaSeconds : 0.0F);
    if (frame.deltaSeconds != frame.realDeltaSeconds) {
        ImGui::SameLine();
        ImGui::TextDisabled("(sim step %.2f ms)", frame.deltaSeconds * 1000.0F);
    }
    ImGui::Separator();
    ImGui::Checkbox("Graphics pipeline", &state_.preset.graphicsEnabled);
    ImGui::Checkbox("Compute pipeline", &state_.preset.computeEnabled);
    ImGui::ColorEdit3("Clear color", &state_.preset.clearColor.x);

    ImGui::SeparatorText("Point geometry");
    auto& braid = state_.braid;
    constexpr std::array geometryNames{"Plait",         "Twisted bundle", "Torsion loop",
                                       "Orbital bloom", "Nested spheres", "Punctured sphere",
                                       "Sleeve",        "Morphing sleeve", "Vortex ring",
                                       "Ridged torus",  "Ridged braid",    "Tentacle sphere",
                                       "Bumpy torus",   "Torus chain"};
    const auto geometryCount = static_cast<int>(geometryNames.size());
    braid.geometryMode = std::clamp(braid.geometryMode, 0, geometryCount - 1);
    if (ImGui::BeginCombo("Geometry",
                          geometryNames[static_cast<std::size_t>(braid.geometryMode)])) {
        const auto group = [&](const char* title, const std::initializer_list<int> modes) {
            ImGui::SeparatorText(title);
            for (const int mode : modes) {
                if (ImGui::Selectable(geometryNames[static_cast<std::size_t>(mode)],
                                      braid.geometryMode == mode)) {
                    braid.geometryMode = mode;
                }
            }
        };
        group("Braids", {0, 1, 2, 3, 10});
        group("Spheres", {4, 11});
        group("Eversion", {5, 6, 7, 8, 9, 12, 13});
        ImGui::EndCombo();
    }
    if (braid.geometryMode == 12 || braid.geometryMode == 13) {
        const bool chain = braid.geometryMode == 13;
        auto& torus = chain ? state_.torusChain : state_.bumpyTorus;
        ImGui::Checkbox("Pause", &torus.paused);
        ImGui::SliderFloat("Roll speed", &torus.animationSpeed, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Spin", &torus.spin, -1.0F, 1.0F, "%.2f");
        ImGui::SliderInt("Bumps around", &torus.bumpsAround, 2, 16);
        // Rows are staggered by half a bump, which closes only for an even count.
        torus.bumpsAround += torus.bumpsAround % 2;
        ImGui::SliderInt("Bumps along", &torus.bumpsAlong, 4, 48);
        ImGui::SliderInt("Row shift", &torus.rowShift, -6, 6);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Bumps the rows drift along the ring over one turn\n"
                              "of the tube, which lines them up in spirals.");
        }
        ImGui::SliderFloat("Bump height", &torus.bumpHeight, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Bump size", &torus.bumpSize, 0.40F, 1.20F, "%.2f");
        ImGui::SliderFloat("Bump roundness", &torus.roundness, 0.6F, 4.0F, "%.2f");
        ImGui::SliderFloat("Pulse depth", &torus.pulseDepth, 0.0F, 1.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0: bumps stay at full height.\n"
                              "1: they sink back into the plain torus every pulse.");
        }
        ImGui::SliderFloat("Pulse speed", &torus.pulseRate, 0.0F, 4.0F, "%.2f");
        ImGui::SliderFloat("Ring radius", &torus.ringRadius, 0.80F, 1.30F, "%.2f");
        ImGui::SliderFloat("Tube radius", &torus.tubeRadius, chain ? 0.10F : 0.20F, 0.48F, "%.2f");
        if (chain) {
            // Each link has to pass through the other's hole, bumps included.
            torus.tubeRadius = std::min(torus.tubeRadius, torusChainTubeLimit(torus));
        }
        ImGui::SliderInt(chain ? "Points per link" : "Points", &torus.pointCount, 2000, 60000);
        ImGui::SliderFloat("Point radius (700px)", &torus.pointSize, 0.6F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &torus.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &torus.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &torus.tilt, -90.0F, 90.0F);
        ImGui::ColorEdit3("Bump color", torus.bumpColor.data());
        ImGui::ColorEdit3("Body color", torus.bodyColor.data());
        if (ImGui::Button(chain ? "Reset chain" : "Reset torus")) {
            torus = chain ? torusChainDefaults() : BumpyTorusSettings{};
        }
    } else if (braid.geometryMode == 11) {
        auto& tentacle = state_.tentacle;
        ImGui::Checkbox("Pause", &tentacle.paused);
        ImGui::SliderFloat("Speed", &tentacle.animationSpeed, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Spin", &tentacle.spin, -1.0F, 1.0F, "%.2f");
        ImGui::SliderInt("Tentacles", &tentacle.tentacles, 1, 32);
        ImGui::SliderFloat("Tentacle length", &tentacle.length, 0.0F, 1.20F, "%.2f");
        ImGui::SliderAngle("Tentacle width", &tentacle.width, 3.0F, 20.0F, "%.1f deg");
        // Narrow enough for the tentacles' patches to sit side by side.
        tentacle.width = std::min(tentacle.width, tentacleWidthLimit(tentacle.tentacles));
        ImGui::SliderFloat("Tentacle roundness", &tentacle.roundness, 2.0F, 6.0F, "%.1f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("2: a tapering spike. Higher: a blunt finger with steep walls.");
        }
        ImGui::SliderFloat("Swirl", &tentacle.swirl, 0.0F, 2.5F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("All tentacles are wrung around the vertical axis and back.");
        }
        ImGui::SliderFloat("Sway", &tentacle.sway, 0.0F, 1.2F, "%.2f");
        ImGui::SliderFloat("Ripple height", &tentacle.rippleHeight, 0.0F, 0.08F, "%.3f");
        ImGui::SliderFloat("Ripples", &tentacle.ripples, 1.0F, 14.0F, "%.1f");
        ImGui::SliderFloat("Sphere radius", &tentacle.radius, 0.35F, 0.90F, "%.2f");
        ImGui::SliderInt("Sphere points", &tentacle.spherePoints, 2000, 40000);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Tentacles get as many points as keeps their density the same.");
        }
        ImGui::SliderFloat("Point radius (700px)", &tentacle.pointSize, 0.6F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &tentacle.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &tentacle.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &tentacle.tilt, -90.0F, 90.0F);
        ImGui::ColorEdit3("Tip color", tentacle.tipColor.data());
        ImGui::ColorEdit3("Body color", tentacle.bodyColor.data());
        if (ImGui::Button("Reset tentacles")) {
            tentacle = TentacleSettings{};
        }
    } else if (braid.geometryMode == 9) {
        auto& torus = state_.ridgedTorus;
        ImGui::Checkbox("Pause", &torus.paused);
        ImGui::SliderFloat("Roll speed", &torus.animationSpeed, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Pulse depth", &torus.pulseDepth, 0.0F, 1.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0: ridges stay at full height.\n"
                              "1: they sink back into the plain torus every pulse.");
        }
        ImGui::SliderFloat("Pulse speed", &torus.pulseRate, 0.0F, 4.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Pulses per roll of the ring.");
        }
        ImGui::SliderFloat("Spin", &torus.spin, -1.0F, 1.0F, "%.2f");
        ImGui::SliderInt("Ridges", &torus.ridges, 1, 9);
        ImGui::SliderInt("Twist", &torus.twist, -5, 5);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Turns of the ridges around the tube per lap of the ring.");
        }
        ImGui::SliderFloat("Twist wave", &torus.twistWave, 0.0F, 2.5F, "%.2f");
        ImGui::SliderFloat("Ridge height", &torus.ridgeHeight, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Ridge sharpness", &torus.sharpness, 0.6F, 5.0F, "%.2f");
        ImGui::SliderFloat("Ring radius", &torus.ringRadius, 0.80F, 1.30F, "%.2f");
        ImGui::SliderFloat("Tube radius", &torus.tubeRadius, 0.20F, 0.48F, "%.2f");
        ImGui::SliderInt("Points", &torus.pointCount, 2000, 60000);
        ImGui::SliderFloat("Point radius (700px)", &torus.pointSize, 0.6F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &torus.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &torus.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &torus.tilt, -90.0F, 90.0F);
        ImGui::ColorEdit3("Ridge color", torus.ridgeColor.data());
        ImGui::ColorEdit3("Body color", torus.bodyColor.data());
        if (ImGui::Button("Reset torus")) {
            torus = RidgedTorusSettings{};
        }
    } else if (braid.geometryMode == 8) {
        auto& vortex = state_.vortex;
        ImGui::Checkbox("Pause", &vortex.paused);
        ImGui::SliderFloat("Roll speed", &vortex.animationSpeed, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Spin", &vortex.spin, -1.0F, 1.0F, "%.2f");
        ImGui::SliderInt("Strands", &vortex.strands, 1, 9);
        ImGui::SliderInt("Twist", &vortex.twist, -5, 5);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Strand positions each strand advances per lap of the ring.");
        }
        ImGui::SliderFloat("Twist wave", &vortex.twistWave, 0.0F, 2.5F, "%.2f");
        drawTubeRidges(vortex.ridges);
        ImGui::SliderFloat("Ring radius", &vortex.ringRadius, 0.80F, 1.30F, "%.2f");
        ImGui::SliderFloat("Coil radius", &vortex.coilRadius, 0.25F, 0.65F, "%.2f");
        ImGui::SliderFloat("Tube radius", &vortex.tubeRadius, 0.05F, 0.30F, "%.3f");
        // Keep the hole open: strands must not cross the ring's axis.
        vortex.coilRadius =
            std::min(vortex.coilRadius, vortex.ringRadius - vortex.tubeRadius - 0.05F);
        ImGui::SliderInt("Points along", &vortex.majorPointCount, 120, 720);
        ImGui::SliderInt("Points around", &vortex.minorPointCount, 12, 64);
        // Even column counts close the staggered sampling pattern without a seam.
        vortex.minorPointCount += vortex.minorPointCount % 2;
        ImGui::SliderFloat("Point radius (700px)", &vortex.pointSize, 0.6F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &vortex.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &vortex.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &vortex.tilt, -90.0F, 90.0F);
        ImGui::ColorEdit3("Hole color", vortex.holeColor.data());
        ImGui::ColorEdit3("Rim color", vortex.rimColor.data());
        if (ImGui::Button("Reset vortex")) {
            vortex = VortexSettings{};
        }
    } else if (braid.geometryMode == 7) {
        auto& morph = state_.morph;
        ImGui::Checkbox("Pause", &morph.paused);
        ImGui::SliderFloat("Flow speed", &morph.animationSpeed, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Morph speed", &morph.morphRate, 0.0F, 2.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shape changes per lap of the material.");
        }
        ImGui::SliderFloat("Morph amount", &morph.morphAmount, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Spin", &morph.spin, -1.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Size", &morph.size, 0.80F, 2.00F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Sets the area of the cloth, which every shape shares.");
        }
        ImGui::SliderFloat("Length", &morph.halfLength, 0.50F, 1.80F, "%.2f");
        ImGui::SliderFloat("Hole", &morph.holeRatio, 0.25F, 0.75F, "%.2f");
        ImGui::SliderFloat("Square corners", &morph.roundness, 2.0F, 8.0F, "%.1f");
        ImGui::SliderInt("Color bands", &morph.bands, 1, 8);
        ImGui::SliderInt("Points", &morph.pointCount, 2000, 60000);
        ImGui::SliderFloat("Point radius (700px)", &morph.pointSize, 0.6F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &morph.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &morph.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &morph.tilt, -90.0F, 90.0F);
        ImGui::ColorEdit3("First color", morph.firstColor.data());
        ImGui::ColorEdit3("Second color", morph.secondColor.data());
        if (ImGui::Button("Reset morph")) {
            morph = MorphSettings{};
        }
    } else if (braid.geometryMode == 6) {
        auto& sleeve = state_.sleeve;
        ImGui::Checkbox("Pause", &sleeve.paused);
        ImGui::SliderFloat("Flow speed", &sleeve.animationSpeed, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Spin", &sleeve.spin, -1.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Outer radius", &sleeve.outerRadius, 0.40F, 1.10F, "%.2f");
        ImGui::SliderFloat("Length", &sleeve.halfLength, 0.0F, 1.40F, "%.2f");
        ImGui::SliderFloat("Inner radius", &sleeve.innerRatio, 0.15F, 0.90F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Radius of the inner wall as a fraction of the outer one.");
        }
        ImGui::SliderInt("Color bands", &sleeve.bands, 1, 8);
        ImGui::SliderInt("Points", &sleeve.pointCount, 2000, 60000);
        ImGui::SliderFloat("Point radius (700px)", &sleeve.pointSize, 0.6F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &sleeve.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &sleeve.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &sleeve.tilt, -90.0F, 90.0F);
        ImGui::ColorEdit3("First color", sleeve.firstColor.data());
        ImGui::ColorEdit3("Second color", sleeve.secondColor.data());
        if (ImGui::Button("Reset sleeve")) {
            sleeve = SleeveSettings{};
        }
    } else if (braid.geometryMode == 5) {
        auto& eversion = state_.eversion;
        ImGui::Checkbox("Pause", &eversion.paused);
        ImGui::SliderFloat("Eversion speed", &eversion.animationSpeed, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("End hold", &eversion.endHold, 0.0F, 1.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How long the two closed spheres linger before turning back.");
        }
        ImGui::SliderFloat("Spin", &eversion.spin, -1.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Radius", &eversion.radius, 0.85F, 1.65F, "%.2f");
        ImGui::SliderAngle("Hole radius", &eversion.holeAngle, 5.0F, 60.0F, "%.1f deg");
        ImGui::SliderInt("Points", &eversion.pointCount, 2000, 60000);
        ImGui::SliderFloat("Point radius (700px)", &eversion.pointSize, 0.6F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &eversion.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &eversion.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &eversion.tilt, -90.0F, 90.0F);
        ImGui::ColorEdit3("Outer side", eversion.outerColor.data());
        ImGui::ColorEdit3("Inner side", eversion.innerColor.data());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Each side of the material keeps its color,\n"
                              "so the sphere changes color once it is inside out.");
        }
        if (ImGui::Button("Reset eversion")) {
            eversion = EversionSettings{};
        }
    } else if (braid.geometryMode == 4) {
        auto& spheres = state_.nestedSpheres;
        ImGui::Checkbox("Pause", &spheres.paused);
        ImGui::SliderFloat("Rotation speed", &spheres.animationSpeed, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Speed variation", &spheres.speedVariation, 0.0F, 1.0F, "%.2f");
        ImGui::SliderInt("Spheres", &spheres.sphereCount, 2, 12);
        ImGui::SliderFloat("Outer radius", &spheres.outerRadius, 0.85F, 1.65F, "%.2f");
        ImGui::SliderFloat("Radius spacing", &spheres.radiusCurve, 0.35F, 2.5F, "%.2f");
        ImGui::TextDisabled("Innermost radius: 10%% of outer radius");
        ImGui::Checkbox("Offset sphere centers", &spheres.offsetCenters);
        ImGui::BeginDisabled(!spheres.offsetCenters);
        ImGui::SliderFloat("Center offset", &spheres.centerOffset, 0.0F, 1.50F, "%.2f");
        ImGui::EndDisabled();
        ImGui::SliderInt("Holes per sphere", &spheres.holeCount, 8, 32);
        ImGui::SliderAngle("Hole radius", &spheres.holeAngle, 6.0F, 28.0F, "%.1f deg");
        ImGui::SliderInt("Outer sphere points", &spheres.pointCount, 2000, 30000);
        ImGui::SliderFloat("Point radius (700px)", &spheres.pointSize, 0.6F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &spheres.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &spheres.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &spheres.tilt, -35.0F, 35.0F);
        if (ImGui::Button("Randomize directions")) {
            spheres.directionSeed = spheres.directionSeed % 997 + 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset spheres")) {
            spheres = NestedSphereSettings{};
        }
        ImGui::SeparatorText("Sphere palette");
        for (std::size_t i = 0; i < state_.palette.colors.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("Color %d", static_cast<int>(i + 1));
            ImGui::SameLine();
            ImGui::ColorEdit3("##Sphere color", state_.palette.colors[i].data());
            ImGui::PopID();
        }
        if (ImGui::Button("Reset palette")) {
            state_.palette.colors = StrandPalette{}.colors;
        }
        if (spheres.sphereCount > static_cast<int>(state_.palette.colors.size())) {
            ImGui::TextDisabled("The palette repeats after color 5.");
        }
    } else {
        const bool ridged = braid.geometryMode == 10;
        if (ridged) {
            auto& ridgedBraid = state_.ridgedBraid;
            ImGui::Combo("Braid", &ridgedBraid.braid,
                         "Plait\0Twisted bundle\0Torsion loop\0Orbital bloom\0");
            drawTubeRidges(ridgedBraid.ridges);
            if (ImGui::Button("Screw")) {
                ridgedBraid.ridges = TubeRidges{0.80F, 1.5F, 2, 30};
            }
            ImGui::SameLine();
            if (ImGui::Button("Beads")) {
                ridgedBraid.ridges = TubeRidges{0.9F, 2.0F, 0, 24};
            }
            ImGui::SameLine();
            if (ImGui::Button("Flutes")) {
                ridgedBraid.ridges = TubeRidges{0.7F, 1.0F, 6, 0};
            }
            ImGui::SameLine();
            if (ImGui::Button("Counter screw")) {
                ridgedBraid.ridges = TubeRidges{0.80F, 2.0F, 3, -30};
            }
            ImGui::Separator();
        }
        const int braidMode = motionGeometryMode(state_);
        if (!ridged && ImGui::Button("Orbital bloom preset")) {
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
        if (braidMode != 3) {
            ImGui::SliderFloat("Square shape", &braid.squareness, 2.0F, 6.0F, "%.2f");
        }
        ImGui::SliderFloat("Weave radius", &braid.weaveRadius, 0.15F, 0.52F, "%.2f");
        ImGui::SliderFloat("Tube radius", &braid.tubeRadius, 0.12F, 0.32F, "%.3f");
        if (braidMode != 0) {
            ImGui::Checkbox("Limit tube overlap", &braid.limitTubeOverlap);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Caps thickness using strand spacing and twist.\n"
                    "Off: Tube radius controls thickness directly; large tubes may overlap.");
            }
        }
        ImGui::SliderFloat("Radius variation", &braid.radiusVariation, 0.0F, 0.55F, "%.2f");
        if (braidMode == 2) {
            ImGui::SliderFloat("Whole-loop twist", &braid.wholeLoopTorsion, 0.0F, 1.5F, "%.2f");
            ImGui::SliderFloat("Compression wave", &braid.torsionCompression, 0.0F, 1.2F, "%.2f");
            ImGui::SliderFloat("Circulation", &braid.materialCirculation, -0.4F, 0.4F, "%.2f");
        } else if (braidMode != 3) {
            ImGui::SliderFloat("Unravel wave", &braid.releaseStrength, 0.0F, 0.95F, "%.2f");
            ImGui::SliderFloat("Wave width", &braid.releaseWidth, 0.3F, 1.1F, "%.2f");
        }
        if (braidMode != 3) {
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
            const int geometryMode = braid.geometryMode;
            braid = BraidSettings{};
            braid.geometryMode = geometryMode;
        }
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
        if (state_.viewport.lockedExtent) {
            // Capture renders at a fixed size; letterbox it instead of stretching.
            const VkExtent2D locked = *state_.viewport.lockedExtent;
            const float scale = std::min(available.x / static_cast<float>(locked.width),
                                         available.y / static_cast<float>(locked.height));
            const ImVec2 imageSize{static_cast<float>(locked.width) * scale,
                                   static_cast<float>(locked.height) * scale};
            const ImVec2 cursor = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(cursor.x + (available.x - imageSize.x) * 0.5F,
                                       cursor.y + (available.y - imageSize.y) * 0.5F));
            ImGui::Image(texture, imageSize);
        } else {
            ImGui::Image(texture, available);
        }
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
