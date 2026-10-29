#include "vkexp/demo/DemoUiModule.hpp"

#include "vkexp/demo/DemoState.hpp"
#include "vkexp/demo/FourSurfaces.hpp"
#include "vkexp/demo/NestedCubes.hpp"
#include "vkexp/demo/Folding.hpp"
#include "vkexp/demo/Metaballs.hpp"
#include "vkexp/demo/Polytope4D.hpp"
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

// Trail of the 4D figures, shared by all of them.
void fourDTrailControls(FourDTrailSettings& trail) {
    ImGui::Checkbox("Trail", &trail.enabled);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Fading copies at earlier moments paint the paths of the motion.");
    }
    if (!trail.enabled) return;
    ImGui::SliderInt("Trail copies", &trail.copies, 1, 32);
    ImGui::SliderFloat("Trail spacing", &trail.spacing, 0.002F, 0.15F, "%.3f",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Trail fade", &trail.fade, 0.3F, 4.0F, "%.2f");
    ImGui::SliderFloat("Trail point size", &trail.shrink, 0.1F, 1.0F, "%.2f");
}



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
                                       "Bumpy torus",   "Torus chain",     "Sphere eversion",
                                       "Arched sphere", "Nested cubes",    "Hopf fibration",
                                       "Tesseract",     "4D polytopes",    "4D cross-sections",
                                       "Quaternion Julia", "4D surfaces", "Origami",
                                       "Flowing shells", "Metaballs"};
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
        group("Spheres", {4, 16, 24, 23, 25, 11, 15});
        group("Four dimensions", {18, 19, 17, 22, 20, 21});
        group("Eversion", {5, 14, 6, 7, 8, 9, 12, 13});
        ImGui::EndCombo();
    }
    if (braid.geometryMode == 16) {
        auto& cubes = state_.nestedCubes;
        ImGui::Checkbox("Pause", &cubes.paused);
        ImGui::SliderFloat("Rotation speed", &cubes.animationSpeed, 0.0F, 1.5F, "%.2f");
        ImGui::Checkbox("Infinite zoom", &cubes.zoom);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Cubes grow out past the camera while new ones are born inside.");
        }
        if (cubes.zoom) {
            ImGui::SliderFloat("Zoom rate", &cubes.zoomRate, 0.0F, 12.0F, "%.1f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Levels passed per flow cycle.");
            }
            ImGui::SliderAngle("Twist per level", &cubes.zoomTwist, -90.0F, 90.0F);
        } else {
            ImGui::Checkbox("Turn in sync", &cubes.sync);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Whole turns per cycle: once a cycle every cube lines up at the same moment.");
            }
            if (cubes.sync) {
                ImGui::SliderFloat("Hold aligned", &cubes.syncHold, 0.0F, 0.95F, "%.2f");
                ImGui::SliderFloat("Alignment flash", &cubes.syncFlash, 0.0F, 2.0F, "%.2f");
            } else {
                ImGui::SliderFloat("Speed variation", &cubes.speedVariation, 0.0F, 1.0F, "%.2f");
            }
        }
        ImGui::Checkbox("Perspective", &cubes.perspective);
        ImGui::BeginDisabled(!cubes.perspective);
        ImGui::SliderFloat("Camera distance", &cubes.cameraDistance, 1.4F, 6.0F, "%.2f");
        ImGui::EndDisabled();
        ImGui::SliderInt("Cubes", &cubes.cubeCount, 1, 8);
        ImGui::SliderFloat("Outer size", &cubes.outerSize, 0.40F, 1.10F, "%.2f");
        ImGui::SliderFloat("Size ratio", &cubes.sizeRatio, 0.25F, 0.95F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Size of each cube relative to the one around it.");
        }
        ImGui::Checkbox("Keep cubes apart", &cubes.keepApart);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Hold the ratio low enough that no cube ever touches the next.");
        }
        const double ratioLimit = nestedCubeRatioLimit(cubes.roundness);
        if (cubes.keepApart && cubes.sizeRatio > 0.97 * ratioLimit) {
            ImGui::TextDisabled("Ratio held at %.2f", 0.97 * ratioLimit);
        }
        ImGui::SliderFloat("Border width", &cubes.border, 0.02F, 1.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Width of the frame around every face; 1 fills the faces.");
        }
        ImGui::SliderFloat("Cube to sphere", &cubes.roundness, 0.0F, 1.0F, "%.2f");
        ImGui::SliderInt("Solid cube points", &cubes.pointCount, 4000, 80000);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Points a solid outer cube would carry; frames keep the spacing.");
        }
        ImGui::SliderFloat("Point radius (700px)", &cubes.pointSize, 0.6F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &cubes.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &cubes.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &cubes.tilt, -35.0F, 35.0F);
        if (ImGui::Button("Randomize directions")) {
            cubes.directionSeed = cubes.directionSeed % 997 + 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset cubes")) {
            cubes = NestedCubeSettings{};
        }
        ImGui::SeparatorText("Cube palette");
        for (std::size_t i = 0; i < state_.palette.colors.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("Color %d", static_cast<int>(i + 1));
            ImGui::SameLine();
            ImGui::ColorEdit3("##Cube color", state_.palette.colors[i].data());
            ImGui::PopID();
        }
        if (ImGui::Button("Reset palette")) {
            state_.palette.colors = StrandPalette{}.colors;
        }
        if (cubes.cubeCount > static_cast<int>(state_.palette.colors.size())) {
            ImGui::TextDisabled("The palette repeats after color 5.");
        }
    } else if (braid.geometryMode == 19) {
        auto& polytope = state_.polytope;
        constexpr std::array shapeNames{"5-cell (simplex)",   "Tesseract",
                                        "16-cell",            "24-cell",
                                        "600-cell",           "120-cell",
                                        "Duoprism",           "Tesseract + 16-cell",
                                        "Three 16-cells",     "Tesseract in tesseract",
                                        "24-cell + dual",     "Torus knot",
                                        "Quasicrystal (6D)",  "Algebraic knot",
                                        "Hyperbolic cubes {4,3,5}", "5-cube (penteract)",
                                        "6-cube (hexeract)"};
        static_assert(shapeNames.size() == polytope4DCount);
        polytope.shape = std::clamp(polytope.shape, 0, polytope4DCount - 1);
        ImGui::Combo("Figure", &polytope.shape, shapeNames.data(), polytope4DCount);
        if (polytope.shape == static_cast<int>(Polytope4D::Duoprism)) {
            ImGui::SliderInt("First polygon", &polytope.duoprismP, 3, 32);
            ImGui::SliderInt("Second polygon", &polytope.duoprismQ, 3, 32);
        } else if (polytope.shape == static_cast<int>(Polytope4D::TorusKnot)) {
            ImGui::SliderInt("Turns one way (p)", &polytope.duoprismP, 1, 16);
            ImGui::SliderInt("Turns other way (q)", &polytope.duoprismQ, 1, 16);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Coprime p and q make one knot, otherwise a link of loops.");
            }
        } else if (polytope.shape == static_cast<int>(Polytope4D::Quasicrystal)) {
            ImGui::SliderFloat("Cluster radius", &polytope.crystalRadius, 1.5F, 6.0F, "%.1f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("In edge lengths. The 6D lattice points whose shadow falls in "
                                  "the window, seen by their 3D shadow.");
            }
            ImGui::SliderFloat("Window", &polytope.crystalWindow, 0.5F, 1.6F, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("1 is the true quasicrystal; smaller thins it out, larger "
                                  "crowds in overlapping tiles.");
            }
            ImGui::SliderFloat("Phason shift", &polytope.phasonShift, 0.0F, 0.9F, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("How far the window slides round its loop: vertices flip in "
                                  "and out in waves.");
            }
            ImGui::SliderFloat("Phason speed", &polytope.phasonSpeed, -3.0F, 3.0F, "%.2f");
            ImGui::SliderFloat("Flip softness", &polytope.crystalSoftness, 0.02F, 0.5F, "%.2f");
        } else if (polytope.shape == static_cast<int>(Polytope4D::HyperbolicCubes)) {
            ImGui::SliderFloat("Flow", &polytope.hyperbolicFlow, -3.0F, 3.0F, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Cubes, five round every edge, in the Poincare ball; the "
                                  "honeycomb slides through it one cube pair per loop. The 4D "
                                  "turns, bend, coils and noise do not apply.");
            }
        } else if (polytope.shape == static_cast<int>(Polytope4D::Penteract) ||
                   polytope.shape == static_cast<int>(Polytope4D::Hexeract)) {
            ImGui::SliderFloat("Higher turn", &polytope.higherTurn, -3.0F, 3.0F, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Turns into the 5th (and 6th) dimension, seen through a "
                                  "perspective per extra dimension: inner cubes come out "
                                  "through the outer ones. Parts colour the layers along the "
                                  "extra axes and the edges joining them.");
            }
            ImGui::SliderFloat("Higher camera distance", &polytope.higherDistance, 1.5F, 6.0F,
                               "%.2f");
            ImGui::SliderInt("Power of z1 (p)", &polytope.knotP, 1, 7);
            ImGui::SliderInt("Roots of g", &polytope.knotRoots, 1, 8);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("The curve z1^p = g(z2) on the 3-sphere. With the roots together "
                                  "it is the (p, roots) torus knot.");
            }
            ImGui::SliderFloat("Root spread", &polytope.knotSpread, 0.0F, 0.8F, "%.2f");
            ImGui::SliderFloat("Root wander", &polytope.knotWander, 0.0F, 0.6F, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Each root circles on its own; where roots meet, the curve is "
                                  "singular and the knot changes.");
            }
            ImGui::SliderFloat("Wander speed", &polytope.knotSpeed, -3.0F, 3.0F, "%.2f");
            ImGui::SliderFloat("Gain", &polytope.knotGain, 0.2F, 4.0F, "%.2f",
                               ImGuiSliderFlags_Logarithmic);
        }
        constexpr std::array colorNames{"4D depth", "Parts", "4D directions",
                                        "Parts shaded by depth"};
        ImGui::Combo("Colour by", &polytope.colorMode, colorNames.data(),
                     static_cast<int>(colorNames.size()));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Parts are the polytopes of a compound or the rings of a duoprism; "
                              "directions group edges parallel in 4D.");
        }
        ImGui::SliderFloat("Slice glow", &polytope.slice, 0.0F, 1.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("A hyperplane sweeps through the figure and lights up the 3D "
                              "cross-section it cuts.");
        }
        ImGui::BeginDisabled(polytope.slice <= 0.0F);
        ImGui::SliderFloat("Slice width", &polytope.sliceWidth, 0.02F, 0.5F, "%.2f");
        ImGui::EndDisabled();
        ImGui::SeparatorText("Curves");
        ImGui::SliderFloat("Edge bend", &polytope.edgeBend, -1.5F, 2.5F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 straight, 1 great circles on the 3-sphere (stereographic at "
                              "camera distance 1), more bulges out, below 0 caves in.");
        }
        ImGui::SliderFloat("Coil", &polytope.coil, 0.0F, 0.4F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Edges wind around themselves through the fourth dimension.");
        }
        ImGui::BeginDisabled(polytope.coil <= 0.0F);
        ImGui::SliderFloat("Coil turns", &polytope.coilTurns, 0.5F, 8.0F, "%.1f");
        ImGui::SliderFloat("Coil flow", &polytope.coilFlow, -4.0F, 4.0F, "%.1f");
        const char* coilWaves[] = {"Steady", "Along edges", "Across the figure"};
        ImGui::Combo("Coil wave", &polytope.coilWave, coilWaves, IM_ARRAYSIZE(coilWaves));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The coil swells and shrinks in waves that run along every "
                              "edge, or sweep across the whole figure.");
        }
        ImGui::BeginDisabled(polytope.coilWave == 0);
        ImGui::SliderFloat("Wave length", &polytope.coilWaveLength, 0.1F, 2.0F, "%.2f");
        ImGui::SliderFloat("Wave speed", &polytope.coilWaveSpeed, -4.0F, 4.0F, "%.1f");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::Checkbox("4D noise", &polytope.noise);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("A drifting 4D field displaces every point before the projection.");
        }
        ImGui::BeginDisabled(!polytope.noise);
        ImGui::SliderFloat("Noise amount", &polytope.noiseAmount, 0.0F, 0.4F, "%.3f");
        ImGui::SliderFloat("Noise scale", &polytope.noiseScale, 0.5F, 8.0F, "%.2f");
        ImGui::SliderFloat("Noise speed", &polytope.noiseSpeed, -3.0F, 3.0F, "%.2f");
        ImGui::EndDisabled();
        ImGui::SliderFloat("Moebius flow", &polytope.mobius, 0.0F, 2.5F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("A conformal map of the 3-sphere: the figure pours towards a moving pole and "
                              "out again; circles stay circles.");
        }
        ImGui::SliderFloat("Moebius rate", &polytope.mobiusRate, -3.0F, 3.0F, "%.2f");
        ImGui::SeparatorText("Tubes");
        const char* tubeStyles[] = {"Black", "Point colour", "Point colour to black"};
        ImGui::Combo("Tube colour", &polytope.tubes, tubeStyles, IM_ARRAYSIZE(tubeStyles));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Paint the tubes under the points in their colour, flat or "
                              "fading to black towards the rim.");
        }
        ImGui::BeginDisabled(polytope.tubes == 0);
        ImGui::SliderFloat("Tube darkness", &polytope.tubeDarkness, 0.0F, 1.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 the point colour itself, 1 black.");
        }
        ImGui::EndDisabled();
        ImGui::SeparatorText("Motion");
        ImGui::Checkbox("Parts turn against each other", &polytope.counterTurn);
        ImGui::Checkbox("Pause", &polytope.paused);
        ImGui::SliderFloat("Speed", &polytope.animationSpeed, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("4D turn (XW)", &polytope.turnXW, -3.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("4D turn (ZW)", &polytope.turnZW, -3.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("Ordinary turn (YZ)", &polytope.turnYZ, -3.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("Spin", &polytope.spin, -2.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("4D camera distance", &polytope.cameraDistance,
                           polytope.edgeBend > 0.5F ? 1.0F : 1.3F, 8.0F, "%.2f");
        ImGui::SliderFloat("Size", &polytope.size, 0.2F, 1.5F, "%.2f");
        ImGui::SliderFloat("Tube radius", &polytope.tubeRadius, 0.003F, 0.06F, "%.3f");
        ImGui::SliderFloat("Light flow", &polytope.lightFlow, -6.0F, 6.0F, "%.1f");
        ImGui::SliderFloat("Light strength", &polytope.lightStrength, 0.0F, 2.0F, "%.2f");
        ImGui::SliderInt("Point budget", &polytope.pointBudget, 20000, 2000000, "%d",
                         ImGuiSliderFlags_Logarithmic);
        ImGui::SliderInt("Points around", &polytope.pointsAround, 3, 16);
        ImGui::SliderFloat("Point radius (700px)", &polytope.pointSize, 0.6F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &polytope.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &polytope.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &polytope.tilt, -90.0F, 90.0F);
        ImGui::SliderFloat("Zoom", &polytope.zoom, 0.25F, 4.0F, "%.2f", ImGuiSliderFlags_Logarithmic);
        fourDTrailControls(state_.fourDTrail);
        ImGui::ColorEdit3("Near color", polytope.nearColor.data());
        ImGui::ColorEdit3("Far color", polytope.farColor.data());
        if (ImGui::Button("Reset figure")) {
            polytope = PolytopeSettings{};
        }
        ImGui::SeparatorText("Parts / directions palette");
        for (std::size_t i = 0; i < state_.palette.colors.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("Color %d", static_cast<int>(i + 1));
            ImGui::SameLine();
            ImGui::ColorEdit3("##Part color", state_.palette.colors[i].data());
            ImGui::PopID();
        }
        if (ImGui::Button("Reset palette")) {
            state_.palette.colors = StrandPalette{}.colors;
        }
    } else if (braid.geometryMode == 20) {
        auto& slice = state_.slice;
        constexpr std::array bodyNames{"5-cell",      "Tesseract", "16-cell",     "24-cell",
                                       "Duocylinder", "Tiger",     "Spheritorus", "Ditorus"};
        slice.body = std::clamp(slice.body, 0, static_cast<int>(bodyNames.size()) - 1);
        ImGui::Combo("Body", &slice.body, bodyNames.data(), static_cast<int>(bodyNames.size()));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("A solid 4D body; we see where our 3D space cuts it.");
        }
        constexpr std::array sliceColors{"Cells", "Depth in 4D", "Cells shaded by depth"};
        ImGui::Combo("Colour by", &slice.colorMode, sliceColors.data(),
                     static_cast<int>(sliceColors.size()));
        if (slice.body >= 5) {
            ImGui::SliderFloat("Thickness", &slice.thickness, 0.06F, 0.4F, "%.2f");
        }
        ImGui::SeparatorText("Slice");
        ImGui::SliderFloat("Slice sweep", &slice.sliceSweep, 0.0F, 1.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How far the cutting hyperplane swings through the body.");
        }
        ImGui::SliderFloat("Sweep rate", &slice.sliceRate, -3.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("Slice offset", &slice.sliceOffset, -1.0F, 1.0F, "%.2f");
        ImGui::SeparatorText("Motion");
        ImGui::Checkbox("Pause", &slice.paused);
        ImGui::SliderFloat("Speed", &slice.animationSpeed, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("4D turn (XW)", &slice.turnXW, -3.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("4D turn (YW)", &slice.turnYW, -3.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("4D turn (ZW)", &slice.turnZW, -3.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("Spin", &slice.spin, -2.0F, 2.0F, "%.2f");
        ImGui::SeparatorText("Points");
        ImGui::SliderInt("Rays per side", &slice.grid, 40, 400);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Point density: rays along each axis of the body find the surface.");
        }
        ImGui::SliderInt("Crossings per ray", &slice.crossings, 1, 6);
        ImGui::SliderFloat("Point radius (700px)", &slice.pointSize, 0.4F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &slice.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &slice.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderFloat("Size", &slice.size, 0.3F, 1.5F, "%.2f");
        ImGui::SliderAngle("Tilt", &slice.tilt, -90.0F, 90.0F);
        ImGui::SliderFloat("Zoom", &slice.zoom, 0.25F, 4.0F, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::ColorEdit3("Near color", slice.nearColor.data());
        ImGui::ColorEdit3("Far color", slice.farColor.data());
        if (ImGui::Button("Reset body")) {
            slice = SliceSettings{};
        }
        ImGui::SeparatorText("Cell palette");
        for (std::size_t i = 0; i < state_.palette.colors.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::ColorEdit3("##Cell color", state_.palette.colors[i].data());
            ImGui::PopID();
        }
    } else if (braid.geometryMode == 22) {
        auto& surface = state_.fourSurface;
        int chosen = std::clamp(surface.surface, 0, fourSurfaceCount - 1);
        if (ImGui::Combo("Surface", &chosen, fourSurfaceNames.data(), fourSurfaceCount)) {
            // Each surface comes with the motion that shows it best; colours stay.
            const auto near = surface.nearColor;
            const auto far = surface.farColor;
            surface = fourSurfaceDefaults(chosen);
            surface.nearColor = near;
            surface.farColor = far;
        }
        if (surface.surface == 0) {
            ImGui::SliderInt("Power n", &surface.power, 2, 7);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("z1^n + z2^n = 1: n x n patches meeting at the branch points.");
            }
            ImGui::SliderFloat("Reach", &surface.reach, 0.3F, 1.8F, "%.2f");
        }
        if (surface.surface == 4) {
            ImGui::SliderInt("Knot p", &surface.power, 2, 5);
            ImGui::SliderInt("Knot q", &surface.powerQ, 2, 7);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Sheets spanning the (p, q) torus knot: z1^p + z2^q = 0 on the "
                                  "3-sphere. Coprime p, q give one knot, others a link.");
            }
            ImGui::SliderInt("Sheets", &surface.fibres, 1, 8);
            ImGui::SliderFloat("Reach", &surface.reach, 1.0F, 5.0F, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("How close the sheets come to the knot they span.");
            }
            ImGui::SliderFloat("Sheet turn", &surface.flow, -3.0F, 3.0F, "%.2f");
        }
        if (surface.surface == 0 || surface.surface == 4) {
            ImGui::SliderAngle("View angle", &surface.viewAngle, -180.0F, 180.0F);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("A fixed turn in the ZW plane before the moving ones.");
            }
        }
        constexpr std::array surfaceColors{"Patches", "Near to far", "Patches by 4D depth"};
        ImGui::Combo("Colour by", &surface.colorMode, surfaceColors.data(),
                     static_cast<int>(surfaceColors.size()));
        ImGui::SeparatorText("Motion");
        ImGui::Checkbox("Pause", &surface.paused);
        ImGui::SliderFloat("Speed", &surface.animationSpeed, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("4D turn (XW)", &surface.turnXW, -3.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("4D turn (YW)", &surface.turnYW, -3.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("4D turn (ZW)", &surface.turnZW, -3.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("4D camera distance", &surface.cameraDistance, 0.0F, 6.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 is a flat projection; near 1 the surface turns inside out "
                              "through the camera (stereographic for the torus).");
        }
        if (surface.surface != 0 && surface.surface != 4) {
            ImGui::SliderFloat("Material flow", &surface.flow, -3.0F, 3.0F, "%.2f");
        }
        ImGui::SliderFloat("Pulse", &surface.pulse, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Pulse rate", &surface.pulseRate, -4.0F, 4.0F, "%.2f");
        ImGui::SliderInt("Pulse waves", &surface.pulseWaves, 1, 8);
        ImGui::SliderFloat("Spin", &surface.spin, -2.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Moebius flow", &surface.mobius, 0.0F, 2.5F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("A conformal map of the 3-sphere: the figure pours towards a moving pole and "
                              "out again; circles stay circles.");
        }
        ImGui::SliderFloat("Moebius rate", &surface.mobiusRate, -3.0F, 3.0F, "%.2f");
        ImGui::SeparatorText("Points");
        ImGui::SliderInt("Point budget", &surface.pointBudget, 20000, 2000000, "%d",
                         ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Point radius (700px)", &surface.pointSize, 0.4F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &surface.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &surface.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &surface.tilt, -90.0F, 90.0F);
        ImGui::SliderFloat("Zoom", &surface.zoom, 0.25F, 4.0F, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::ColorEdit3("Near color", surface.nearColor.data());
        ImGui::ColorEdit3("Far color", surface.farColor.data());
        if (ImGui::Button("Reset surface")) {
            surface = fourSurfaceDefaults(surface.surface);
        }
        ImGui::SeparatorText("Patch palette");
        for (std::size_t i = 0; i < state_.palette.colors.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::ColorEdit3("##Patch color", state_.palette.colors[i].data());
            ImGui::PopID();
        }
    } else if (braid.geometryMode == 23) {
        auto& origami = state_.origami;
        int figure = std::clamp(origami.figure, 0, foldFigureCount - 1);
        if (ImGui::Combo("Figure", &figure, foldFigureNames.data(), foldFigureCount)) {
            // Each figure comes with the settings that show it best; colours stay.
            const auto near = origami.nearColor;
            const auto far = origami.farColor;
            origami = origamiDefaults(figure);
            origami.nearColor = near;
            origami.farColor = far;
        }
        switch (static_cast<FoldFigure>(origami.figure)) {
        case FoldFigure::PlatonicNet:
            ImGui::Combo("Solid", &origami.solid, origamiSolidNames.data(), origamiSolidCount);
            ImGui::SliderInt("Net", &origami.variant, 0, 11);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Which way the solid is cut open: every number is another net "
                                  "that lies flat without overlapping.");
            }
            ImGui::SliderFloat("Sphere", &origami.sphere, 0.0F, 1.0F, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Closed, the faces swell into a sphere through the corners.");
            }
            break;
        case FoldFigure::TesseractNet:
            ImGui::SliderAngle("4D tilt (XW)", &origami.fourTilt, -90.0F, 90.0F);
            ImGui::SliderFloat("4D turn (XW)", &origami.fourTurn, -2.0F, 2.0F, "%.2f");
            ImGui::SliderFloat("4D camera distance", &origami.fourDistance, 3.2F, 10.0F, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("The eight cubes unfold in 4D about the squares they share into "
                                  "Dali's cross; seen from out along -w the first cube is the outer one.");
            }
            break;
        case FoldFigure::Jitterbug:
            ImGui::TextWrapped("Octahedron (closed) - icosahedron (the gaps light up) - "
                               "cuboctahedron (open) - the other-handed icosahedron and octahedron.");
            break;
        case FoldFigure::Dudeney:
            ImGui::SliderFloat("Lift", &origami.lift, 0.0F, 0.4F, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Turning pieces rise off the table so they pass over each other.");
            }
            break;
        case FoldFigure::MiuraOri:
            ImGui::SliderInt("Panels across", &origami.panelsX, 2, 40);
            ImGui::SliderInt("Panels along", &origami.panelsY, 2, 40);
            ImGui::SliderAngle("Panel angle", &origami.panelAngle, 25.0F, 80.0F);
            ImGui::SliderFloat("Panel aspect", &origami.panelAspect, 0.3F, 3.0F, "%.2f");
            break;
        case FoldFigure::Flasher:
            ImGui::SliderInt("Arms", &origami.arms, 3, 12);
            ImGui::SliderInt("Rings", &origami.rings, 2, 24);
            ImGui::SliderFloat("Wrap", &origami.wrap, 0.0F, 3.0F, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("How far the arms wind round the hub when closed, in half turns.");
            }
            break;
        case FoldFigure::RotatingSquares:
            ImGui::SliderInt("Squares", &origami.squares, 2, 40);
            ImGui::SliderFloat("Dome", &origami.dome, -1.5F, 1.5F, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("The open sheet rises into a dome (or sinks into a bowl).");
            }
            break;
        case FoldFigure::RollingSheet:
            ImGui::Combo("Shape", &origami.sheetShape, rollingShapeNames.data(), rollingShapeCount);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("All in turn changes shape whenever the strip lies flat: "
                                  "loop 3 cycles to close the clip.");
            }
            break;
        }
        ImGui::SliderFloat("Frame width", &origami.border, 0.05F, 1.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("1 fills the pieces; less leaves a frame round a window.");
        }
        constexpr std::array origamiColors{"Pieces", "Bands", "Near to far"};
        ImGui::Combo("Colour by", &origami.colorMode, origamiColors.data(),
                     static_cast<int>(origamiColors.size()));
        ImGui::SeparatorText("Motion");
        ImGui::Checkbox("Pause", &origami.paused);
        ImGui::SliderFloat("Speed", &origami.animationSpeed, 0.0F, 1.5F, "%.2f");
        ImGui::Checkbox("Fold through", &origami.mirror);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Past the open state the figure goes on into its mirror; "
                              "off, it closes the way it came.");
        }
        if (origami.figure <= 1) {
            ImGui::SliderFloat("Stagger", &origami.stagger, 0.0F, 1.0F, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Outer hinges follow later: a wave of folding running out from the base.");
            }
        }
        ImGui::SliderFloat("Hold closed", &origami.holdClosed, 0.0F, 0.45F, "%.2f");
        ImGui::SliderFloat("Hold open", &origami.holdFlat, 0.0F, 0.45F, "%.2f");
        ImGui::SliderFloat("Open swirl", &origami.swirl, -6.0F, 6.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("While the figure lies open it twists about its centre, more at the rim.");
        }
        ImGui::SliderFloat("Hinge glow", &origami.hingeGlow, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Open flash", &origami.flatFlash, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Spin", &origami.spin, -2.0F, 2.0F, "%.2f");
        ImGui::SeparatorText("Points");
        ImGui::SliderInt("Point budget", &origami.pointBudget, 10000, 2000000, "%d",
                         ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Point radius (700px)", &origami.pointSize, 0.4F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &origami.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &origami.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &origami.tilt, -90.0F, 90.0F);
        ImGui::SliderFloat("Zoom", &origami.zoom, 0.25F, 4.0F, "%.2f", ImGuiSliderFlags_Logarithmic);
        if (ImGui::Button("Reset figure")) {
            origami = origamiDefaults(origami.figure);
        }
        if (origami.colorMode == 2) {
            ImGui::ColorEdit3("Near color", origami.nearColor.data());
            ImGui::ColorEdit3("Far color", origami.farColor.data());
        } else {
            ImGui::SeparatorText("Piece palette");
            for (std::size_t i = 0; i < state_.palette.colors.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::ColorEdit3("##Piece color", state_.palette.colors[i].data());
                ImGui::PopID();
            }
        }
    } else if (braid.geometryMode == 25) {
        auto& balls = state_.metaballs;
        ImGui::Combo("Motion", &balls.motion, metaballMotionNames.data(),
                     static_cast<int>(metaballMotionNames.size()));
        ImGui::SliderInt("Balls", &balls.balls, 1, metaballLimit);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Worms: the worms (up to %d). Rings: the rings. Ring fountain: the "
                              "ring and the balls winding through it. Cube block: the cubes, filling "
                              "a block row by row (8 or 27 make it whole). Holes: a third are the "
                              "blob, the rest carve. Peristalsis: the capsule and its rings. Cube "
                              "carpet: the cubes, rounded up to a square (up to 8 by 8).",
                              metaballWormLimit);
        }
        if (balls.motion == 6 || balls.motion == 7 || balls.motion == 10) {
            ImGui::SliderFloat("Roundness", &balls.roundness, 0.08F, 1.0F, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("How rounded the cubes are: 1 is a ball.");
            }
        }
        ImGui::SliderFloat("Ball radius", &balls.radius, 0.1F, 0.7F, "%.2f");
        ImGui::SliderFloat("Size variation", &balls.variation, 0.0F, 0.7F, "%.2f");
        ImGui::SliderFloat("Field reach", &balls.reach, 1.2F, 3.5F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How far a ball's field reaches, in radii: higher, the balls reach out "
                              "for each other from further and merge softer.");
        }
        ImGui::SliderFloat("Spread", &balls.spread, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Breathe", &balls.breathe, 0.0F, 0.4F, "%.2f");
        ImGui::SliderInt("Seed", &balls.seed, 1, 99);
        constexpr std::array ballColors{"Blended balls", "Own ball", "Bottom to top"};
        ImGui::Combo("Colour by", &balls.colorMode, ballColors.data(), static_cast<int>(ballColors.size()));
        ImGui::SliderFloat("Seam glow", &balls.seamGlow, 0.0F, 2.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Lights up the seams where balls merge.");
        }
        ImGui::SliderFloat("Seam sharpness", &balls.seamSharpness, 0.5F, 12.0F, "%.1f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How narrow the glowing seam between merging balls is.");
        }
        ImGui::SeparatorText("Motion");
        ImGui::Checkbox("Pause", &balls.paused);
        ImGui::SliderFloat("Speed", &balls.animationSpeed, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Spin", &balls.spin, -3.0F, 3.0F, "%.2f");
        ImGui::SeparatorText("Points");
        ImGui::SliderInt("Point budget", &balls.pointBudget, 10000, vkexp::metaballParticleLimit * 2 / 3, "%d",
                         ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Particles over the balls' area with the balls apart: sets the density "
                              "the particles keep. Changing it seeds them afresh.");
        }
        ImGui::SliderFloat("Push apart", &balls.push, 0.0F, 1.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How hard neighbouring particles push each other apart every frame: "
                              "evens the spacing out, too much makes them jitter.");
        }
        ImGui::SliderFloat("Density tolerance", &balls.tolerance, 0.02F, 0.4F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How far the density may drift from the even one before crowded "
                              "particles fade out or new ones are born in gaps. Lower is more even, "
                              "but more particles come and go.");
        }
        ImGui::SliderFloat("Fade time", &balls.fadeSeconds, 0.05F, 2.0F, "%.2f s");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How long a crowded particle takes to fade out and a new one in a gap "
                              "to fade in.");
        }
        if (ImGui::Button("Seed afresh")) {
            ++balls.restart;
        }
        ImGui::SliderFloat("Point radius (700px)", &balls.pointSize, 0.4F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &balls.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &balls.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderFloat("Size", &balls.size, 0.3F, 2.0F, "%.2f");
        ImGui::SliderFloat("Zoom", &balls.zoom, 0.5F, 3.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &balls.tilt, -90.0F, 90.0F);
        if (balls.colorMode == 2) {
            ImGui::ColorEdit3("Bottom color", balls.bottomColor.data());
            ImGui::ColorEdit3("Top color", balls.topColor.data());
        } else {
            ImGui::SeparatorText("Ball palette");
            for (std::size_t i = 0; i < state_.palette.colors.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::ColorEdit3("##Ball color", state_.palette.colors[i].data());
                ImGui::PopID();
            }
        }
    } else if (braid.geometryMode == 24) {
        auto& shells = state_.shells;
        ImGui::SliderInt("Shells", &shells.shells, 2, 8);
        ImGui::SliderFloat("Outer size", &shells.outerSize, 0.3F, 1.2F, "%.2f");
        ImGui::SliderFloat("Size ratio", &shells.sizeRatio, 0.3F, 0.9F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Held small enough that turning shells never touch.");
        }
        ImGui::SliderFloat("Frame width", &shells.border, 0.05F, 1.0F, "%.2f");
        ImGui::SliderFloat("Roundness", &shells.roundness, 0.0F, 1.0F, "%.2f");
        constexpr std::array shellColors{"Palette per shell", "Outer to inner", "One colour"};
        ImGui::Combo("Colour by", &shells.colorMode, shellColors.data(),
                     static_cast<int>(shellColors.size()));
        ImGui::SeparatorText("Motion");
        ImGui::Checkbox("Pause", &shells.paused);
        ImGui::SliderFloat("Speed", &shells.animationSpeed, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Hops per cycle", &shells.hops, -3.0F, 3.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Every hop moves each point one shell in; negative plays it backwards: "
                              "shells crumble outward and the fountain runs into the centre.");
        }
        ImGui::SliderFloat("Rest", &shells.rest, 0.0F, 0.9F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Share of each hop with every point at home.");
        }
        ImGui::SliderFloat("Crumble spread", &shells.spread, 0.0F, 0.95F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0: a shell lets go all at once; more: point by point.");
        }
        ImGui::SliderFloat("Cascade", &shells.cascade, 0.0F, 1.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0: every shell at once; 1: the outer shell crumbles first, "
                              "the fountain from the centre comes last.");
        }
        ImGui::SliderFloat("Top first", &shells.order, 0.0F, 1.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0: points let go at random; 1: from the top of their shell down.");
        }
        ImGui::SliderFloat("Fall sag", &shells.sag, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Scatter", &shells.scatter, 0.0F, 0.5F, "%.2f");
        ImGui::SliderFloat("Fountain height", &shells.fountain, 0.0F, 2.5F, "%.2f");
        ImGui::SliderFloat("Shell turn", &shells.turnRate, -3.0F, 3.0F, "%.2f");
        ImGui::SeparatorText("Points");
        ImGui::SliderInt("Points per shell", &shells.pointsPerShell, 2400, 200000, "%d",
                         ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Point radius (700px)", &shells.pointSize, 0.4F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &shells.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &shells.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &shells.tilt, -90.0F, 90.0F);
        if (shells.colorMode == 0) {
            ImGui::SeparatorText("Shell palette");
            for (std::size_t i = 0; i < state_.palette.colors.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::ColorEdit3("##Shell color", state_.palette.colors[i].data());
                ImGui::PopID();
            }
        } else {
            ImGui::ColorEdit3("Outer color", shells.outerColor.data());
            if (shells.colorMode == 1) {
                ImGui::ColorEdit3("Inner color", shells.innerColor.data());
            }
        }
    } else if (braid.geometryMode == 21) {
        auto& julia = state_.julia;
        ImGui::SliderInt("Power", &julia.power, 2, 3);
        ImGui::SliderFloat4("c", julia.c.data(), -1.0F, 1.0F, "%.3f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("z -> z^power + c on quaternions; small changes reshape the set.");
        }
        ImGui::SliderFloat("c orbit", &julia.orbit, 0.0F, 0.3F, "%.3f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("c circles round this loop: the fractal swells, splits and heals.");
        }
        ImGui::SliderFloat("Orbit rate", &julia.orbitRate, -3.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("Slice (w)", &julia.sliceW, -1.0F, 1.0F, "%.3f");
        constexpr std::array juliaColors{"Palette bands", "Near to far"};
        ImGui::Combo("Colour by", &julia.colorMode, juliaColors.data(),
                     static_cast<int>(juliaColors.size()));
        ImGui::SeparatorText("Motion");
        ImGui::Checkbox("Pause", &julia.paused);
        ImGui::SliderFloat("Speed", &julia.animationSpeed, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("4D turn (XW)", &julia.turnXW, -3.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("Spin", &julia.spin, -2.0F, 2.0F, "%.2f");
        ImGui::SeparatorText("Points");
        ImGui::SliderInt("Rays per side", &julia.grid, 40, 400);
        ImGui::SliderFloat("Point radius (700px)", &julia.pointSize, 0.4F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &julia.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &julia.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderFloat("Size", &julia.size, 0.3F, 1.5F, "%.2f");
        ImGui::SliderAngle("Tilt", &julia.tilt, -90.0F, 90.0F);
        ImGui::SliderFloat("Zoom", &julia.zoom, 0.25F, 4.0F, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::ColorEdit3("Near color", julia.nearColor.data());
        ImGui::ColorEdit3("Far color", julia.farColor.data());
        if (ImGui::Button("Reset fractal")) {
            julia = JuliaSettings{};
        }
        ImGui::SeparatorText("Band palette");
        for (std::size_t i = 0; i < state_.palette.colors.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::ColorEdit3("##Band color", state_.palette.colors[i].data());
            ImGui::PopID();
        }
    } else if (braid.geometryMode == 18) {
        auto& tesseract = state_.tesseract;
        ImGui::Checkbox("Pause", &tesseract.paused);
        ImGui::SliderFloat("Speed", &tesseract.animationSpeed, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("4D turn (XW)", &tesseract.turnXW, -3.0F, 3.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Turn through the fourth dimension: the inner cube comes out.");
        }
        ImGui::SliderFloat("Second turn (YZ)", &tesseract.turnYZ, -3.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("Spin", &tesseract.spin, -2.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("4D camera distance", &tesseract.cameraDistance, 2.5F, 8.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Closer makes the inner cube smaller against the outer one.");
        }
        ImGui::SliderFloat("Size", &tesseract.size, 0.15F, 0.80F, "%.2f");
        ImGui::SliderFloat("Tube radius", &tesseract.tubeRadius, 0.005F, 0.08F, "%.3f");
        ImGui::SliderFloat("Light flow", &tesseract.lightFlow, -6.0F, 6.0F, "%.1f");
        ImGui::SliderFloat("Light strength", &tesseract.lightStrength, 0.0F, 2.0F, "%.2f");
        ImGui::SliderInt("Points along", &tesseract.pointsAlong, 40, 400);
        ImGui::SliderInt("Points around", &tesseract.pointsAround, 3, 20);
        ImGui::SliderFloat("Point radius (700px)", &tesseract.pointSize, 0.6F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &tesseract.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &tesseract.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &tesseract.tilt, -90.0F, 90.0F);
        ImGui::SliderFloat("Zoom", &tesseract.zoom, 0.25F, 4.0F, "%.2f", ImGuiSliderFlags_Logarithmic);
        fourDTrailControls(state_.fourDTrail);
        ImGui::ColorEdit3("Near color", tesseract.nearColor.data());
        ImGui::ColorEdit3("Far color", tesseract.farColor.data());
        if (ImGui::Button("Reset tesseract")) {
            tesseract = TesseractSettings{};
        }
    } else if (braid.geometryMode == 17) {
        auto& hopf = state_.hopf;
        ImGui::Checkbox("Pause", &hopf.paused);
        ImGui::SliderFloat("Speed", &hopf.animationSpeed, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Light flow", &hopf.flow, -6.0F, 6.0F, "%.1f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Lights running along every circle, per flow cycle.");
        }
        ImGui::SliderFloat("Fibre drift", &hopf.drift, -3.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("Spin", &hopf.spin, -2.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Moebius flow", &hopf.mobius, 0.0F, 2.5F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("A conformal map of the 3-sphere: the figure pours towards a moving pole and "
                              "out again; circles stay circles.");
        }
        ImGui::SliderAngle("4D tumble", &hopf.tumble, 0.0F, 90.0F);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Tilt in four dimensions: the tori roll through each other.");
        }
        ImGui::SliderFloat("Tumble rate", &hopf.tumbleRate, 0.0F, 4.0F, "%.2f");
        ImGui::SliderFloat("Breathing", &hopf.breathing, 0.0F, 0.25F, "%.3f");
        ImGui::SliderInt("Tori", &hopf.tori, 1, 6);
        ImGui::SliderInt("Circles per torus", &hopf.fibers, 3, 32);
        ImGui::SliderInt("Fibre knot p", &hopf.knotP, 1, 5);
        ImGui::SliderInt("Fibre knot q", &hopf.knotQ, 1, 5);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("1, 1 gives circles; otherwise every fibre is a (p, q) torus knot "
                              "(2, 3: trefoils), still all linked. Coprime p, q; raise Points "
                              "along, knots are longer.");
        }
        ImGui::SliderFloat("Inner latitude", &hopf.innerLatitude, 0.05F, 1.40F, "%.2f");
        ImGui::SliderFloat("Outer latitude", &hopf.outerLatitude, 0.05F, 1.40F, "%.2f");
        ImGui::SliderFloat("Scale", &hopf.scale, 0.25F, 1.00F, "%.2f");
        ImGui::SliderFloat("Tube radius", &hopf.tubeRadius, 0.005F, 0.06F, "%.3f");
        ImGui::SliderInt("Points along", &hopf.pointsAlong, 60, 600);
        ImGui::SliderInt("Points around", &hopf.pointsAround, 2, 16);
        ImGui::SliderFloat("Point radius (700px)", &hopf.pointSize, 0.6F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &hopf.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &hopf.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &hopf.tilt, -90.0F, 90.0F);
        ImGui::SliderFloat("Zoom", &hopf.zoom, 0.25F, 4.0F, "%.2f", ImGuiSliderFlags_Logarithmic);
        fourDTrailControls(state_.fourDTrail);
        if (ImGui::Button("Reset fibration")) {
            hopf = HopfSettings{};
        }
        ImGui::SeparatorText("Torus palette");
        for (std::size_t i = 0; i < state_.palette.colors.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("Color %d", static_cast<int>(i + 1));
            ImGui::SameLine();
            ImGui::ColorEdit3("##Torus color", state_.palette.colors[i].data());
            ImGui::PopID();
        }
        if (ImGui::Button("Reset palette")) {
            state_.palette.colors = StrandPalette{}.colors;
        }
    } else if (braid.geometryMode == 15) {
        auto& arched = state_.archedSphere;
        ImGui::Checkbox("Pause", &arched.paused);
        ImGui::SliderFloat("Flow speed", &arched.animationSpeed, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Spin", &arched.spin, -1.0F, 1.0F, "%.2f");
        ImGui::SliderInt("Arches", &arched.arches, 1, 16);
        ImGui::SliderFloat("Arch height", &arched.archHeight, 0.10F, 1.00F, "%.2f");
        ImGui::SliderFloat("Tube radius", &arched.tubeRadius, 0.03F, 0.20F, "%.3f");
        ImGui::SliderFloat("Twist", &arched.twist, -1.0F, 1.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Turns around the sphere an arch makes on its way down.");
        }
        ImGui::SliderInt("Bands", &arched.bands, 1, 24);
        ImGui::Checkbox("Checkerboard", &arched.checker);
        ImGui::BeginDisabled(!arched.checker);
        ImGui::SliderInt("Sectors", &arched.sectors, 2, 32);
        ImGui::EndDisabled();
        ImGui::SliderFloat("Sphere radius", &arched.radius, 0.35F, 0.90F, "%.2f");
        ImGui::SliderInt("Sphere points", &arched.spherePoints, 2000, 40000);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Arches get rows and columns of points with the same spacing.");
        }
        ImGui::SliderFloat("Point radius (700px)", &arched.pointSize, 0.6F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &arched.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &arched.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &arched.tilt, -90.0F, 90.0F);
        ImGui::ColorEdit3("First color", arched.firstColor.data());
        ImGui::ColorEdit3("Second color", arched.secondColor.data());
        if (ImGui::Button("Reset arches")) {
            arched = ArchedSphereSettings{};
        }
    } else if (braid.geometryMode == 14) {
        auto& sphere = state_.sphereEversion;
        ImGui::TextDisabled("Bednorz & Bednorz, arXiv:1711.10466");
        ImGui::Checkbox("Pause", &sphere.paused);
        ImGui::SliderFloat("Eversion speed", &sphere.animationSpeed, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("End hold", &sphere.endHold, 0.0F, 1.0F, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How long the two round spheres linger before turning back.");
        }
        ImGui::SliderFloat("Spin", &sphere.spin, -1.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Radius", &sphere.radius, 0.35F, 0.80F, "%.2f");
        ImGui::SliderInt("Points", &sphere.pointCount, 2000, 60000);
        ImGui::SliderFloat("Point radius (700px)", &sphere.pointSize, 0.6F, 3.0F, "%.2f");
        ImGui::SliderFloat("Glow", &sphere.glow, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Brightness", &sphere.brightness, 0.5F, 2.0F, "%.2f");
        ImGui::SliderAngle("Tilt", &sphere.tilt, -90.0F, 90.0F);
        ImGui::ColorEdit3("First side", sphere.firstColor.data());
        ImGui::ColorEdit3("Second side", sphere.secondColor.data());
        if (ImGui::Button("Reset eversion")) {
            sphere = SphereEversionSettings{};
        }
    } else if (braid.geometryMode == 12 || braid.geometryMode == 13) {
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

    ImGui::SetNextWindowPos(ImVec2(840.0F, 60.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320.0F, 420.0F), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Effects")) {
        auto& effects = state_.effects;
        ImGui::TextDisabled("Applied to the points of every geometry.");
        ImGui::Checkbox("See-through", &effects.seeThrough);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Drops the hidden black skin behind the points:\n"
                              "every point is drawn and they simply overlap.");
        }
        ImGui::SeparatorText("Drift");
        ImGui::Checkbox("Drift and jitter", &effects.drift);
        ImGui::BeginDisabled(!effects.drift);
        ImGui::SliderFloat("Drift amount", &effects.driftAmount, 0.0F, 0.15F, "%.3f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Neighbouring points wander together in a slow flow.");
        }
        ImGui::SliderFloat("Drift scale", &effects.driftScale, 0.5F, 12.0F, "%.1f");
        ImGui::SliderFloat("Jitter", &effects.jitter, 0.0F, 0.05F, "%.3f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Every point trembles on its own.");
        }
        ImGui::SliderFloat("Drift speed", &effects.driftSpeed, 0.0F, 6.0F, "%.2f");
        ImGui::EndDisabled();
        ImGui::SeparatorText("Shedding");
        ImGui::Checkbox("Shed points", &effects.shed);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Some points leave the surface, shrink away\n"
                              "and grow back where they started.");
        }
        ImGui::BeginDisabled(!effects.shed);
        ImGui::SliderFloat("Share of points", &effects.shedShare, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Flight distance", &effects.shedDistance, 0.0F, 1.2F, "%.2f");
        ImGui::SliderFloat("Fall", &effects.shedFall, -0.8F, 0.8F, "%.2f");
        ImGui::SliderFloat("Shed rate", &effects.shedRate, 0.0F, 6.0F, "%.2f");
        ImGui::EndDisabled();
        ImGui::SeparatorText("Trails");
        ImGui::Checkbox("Trails", &effects.trails);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Each point is followed by smaller, dimmer copies\n"
                              "of itself from a moment ago. Costs one more pass\n"
                              "over the points per copy.");
        }
        ImGui::BeginDisabled(!effects.trails);
        ImGui::SliderInt("Trail length", &effects.trailCount, 1, 16);
        ImGui::SliderFloat("Trail spacing", &effects.trailSpacing, 0.002F, 0.10F, "%.3f");
        ImGui::SliderFloat("Trail fade", &effects.trailFade, 0.40F, 0.98F, "%.2f");
        ImGui::EndDisabled();
        if (ImGui::Button("Reset effects")) {
            effects = PointEffects{};
        }
    }
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
