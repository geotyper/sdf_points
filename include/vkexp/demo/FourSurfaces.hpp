#pragma once

// Surfaces that live in 4D (geometry mode 22): a slice of the Calabi-Yau
// quintic, the Klein bottle, the Veronese surface and the Clifford torus. The
// shader (surfaces4d.glsl) puts the points on a grid of every patch's own
// coordinates (s, t) in [0, 1]^2, turns the surface in 4D and projects it, so
// the points ride the surface. Here: how many patches, and how the point
// budget and the skin cells are shared out. Pure logic (no Vulkan) so it can
// be unit tested.

#include <algorithm>
#include <array>
#include <cmath>

namespace vkexp {

enum class FourSurface : int {
    CalabiYau = 0,   // z1^n + z2^n = 1, n^2 patches
    KleinBottle = 1, // without self-crossing, as only 4D allows
    Veronese = 2,    // the real projective plane
    CliffordTorus = 3, // the flat torus on the 3-sphere
};

inline constexpr int fourSurfaceCount = 4;
inline constexpr std::array<const char*, fourSurfaceCount> fourSurfaceNames{
    "Calabi-Yau (Fermat)", "Klein bottle", "Veronese surface", "Clifford torus"};

[[nodiscard]] inline int fourSurfacePatches(const int surface, const int power) {
    return surface == static_cast<int>(FourSurface::CalabiYau) ? power * power : 1;
}

struct FourSurfaceGrid {
    int columns{1}; // along s
    int rows{1};    // along t
};

// About how much longer a patch is along s than along t, once drawn.
[[nodiscard]] inline double fourSurfaceAspect(const int surface, const double reach) {
    switch (static_cast<FourSurface>(surface)) {
    case FourSurface::CalabiYau: return 1.5707963267948966 / std::max(2.0 * reach, 0.1);
    case FourSurface::KleinBottle: return 2.2;
    case FourSurface::Veronese: return 3.0;
    case FourSurface::CliffordTorus: return 1.0;
    }
    return 1.0;
}

// Points of one patch, so all patches together stay within `budget`.
[[nodiscard]] inline FourSurfaceGrid fourSurfaceGrid(const int surface, const int power,
                                                     const double reach, const int budget) {
    const double perPatch =
        std::max(static_cast<double>(budget) / fourSurfacePatches(surface, power), 4.0);
    const double aspect = fourSurfaceAspect(surface, reach);
    FourSurfaceGrid grid;
    grid.columns = std::max(static_cast<int>(std::sqrt(perPatch * aspect)), 2);
    grid.rows = std::max(static_cast<int>(perPatch / grid.columns), 2);
    return grid;
}

// Cells of one patch's depth skin; the shader's fourSurfaceSkinGrid matches.
[[nodiscard]] inline FourSurfaceGrid fourSurfaceSkinGrid(const int surface) {
    return surface == static_cast<int>(FourSurface::CalabiYau) ? FourSurfaceGrid{96, 96}
                                                               : FourSurfaceGrid{256, 192};
}

} // namespace vkexp
