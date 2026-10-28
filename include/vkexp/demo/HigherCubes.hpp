#pragma once

// The 5-cube (penteract) and 6-cube (hexeract), turned in the planes that reach
// into their extra dimensions and brought down to 4D by perspective, one
// dimension at a time; the 4D polytope shaders take it from there, so the
// figure is seen through two (or three) perspectives in a row. A perspective
// keeps straight lines straight, so the edges stay edges. Pure logic (no
// Vulkan) so it can be unit tested.

#include "vkexp/demo/Polytope4D.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace vkexp {

// Edges of the `dimensions`-cube (5 or 6), circumradius 1, at turn `angle`
// (radians; every plane turns a whole number of times per turn of it), seen
// from a camera `distance` out along each extra axis. Part says which of the
// extra-dimensional layers an edge belongs to, or which extra axis it runs
// along; direction is the axis it runs along.
[[nodiscard]] inline std::vector<Edge4D> higherCubeEdges(const int dimensions, const double angle,
                                                         const double distance) {
    const int n = std::clamp(dimensions, 5, 6);
    const double half = 1.0 / std::sqrt(static_cast<double>(n));
    const double camera = std::max(distance, 1.2);
    // Turns into the extra dimensions: two planes per extra axis.
    struct Turn {
        int a, b, rate;
    };
    std::vector<Turn> turns{{0, 4, 1}, {3, 4, 2}};
    if (n == 6) {
        turns.push_back({1, 5, 1});
        turns.push_back({2, 5, -1});
    }
    const auto place = [&](const int corner) {
        std::array<double, 6> x{};
        for (int i = 0; i < n; ++i) {
            x[static_cast<std::size_t>(i)] = ((corner >> i) & 1) ? half : -half;
        }
        for (const auto& turn : turns) {
            const double c = std::cos(turn.rate * angle), s = std::sin(turn.rate * angle);
            auto& u = x[static_cast<std::size_t>(turn.a)];
            auto& v = x[static_cast<std::size_t>(turn.b)];
            const double nu = c * u - s * v, nv = s * u + c * v;
            u = nu;
            v = nv;
        }
        // Down one dimension at a time, the camera out along the last axis.
        for (int k = n - 1; k >= 4; --k) {
            const double scale = camera / (camera - x[static_cast<std::size_t>(k)]);
            for (int i = 0; i < k; ++i) x[static_cast<std::size_t>(i)] *= scale;
        }
        return Vec4d{x[0], x[1], x[2], x[3]};
    };
    std::vector<Edge4D> edges;
    for (int corner = 0; corner < (1 << n); ++corner) {
        for (int axis = 0; axis < n; ++axis) {
            if ((corner >> axis) & 1) continue;
            Edge4D edge{place(corner), place(corner | (1 << axis)), 0, axis};
            // Layers by the extra coordinates, then the edges joining them.
            edge.part = axis >= 4 ? (n == 6 ? 4 : 2) + (axis - 4)
                                  : ((corner >> 4) & 1) + (n == 6 ? 2 * ((corner >> 5) & 1) : 0);
            edges.push_back(edge);
        }
    }
    return edges;
}

} // namespace vkexp
