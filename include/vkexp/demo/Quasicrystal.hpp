#pragma once

// Icosahedral quasicrystal by cut and project: the points of the 6D integer
// lattice whose shadow in the 3D "perpendicular" space falls inside a window
// (the rhombic triacontahedron, the shadow of the unit 6-cube), shown by their
// shadow in the "parallel" space. Lattice neighbours give the edges of the
// Ammann rhombohedra. Sliding the window (a phason shift) makes vertices flip
// in and out in waves. Pure logic (no Vulkan) so it can be unit tested.

#include "vkexp/demo/Polytope4D.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <queue>
#include <vector>

namespace vkexp {

struct QuasicrystalShape {
    double radius{3.5};   // cluster radius in edge lengths
    double window{1.0};   // window scale; 1 is the true quasicrystal
    double softness{0.12}; // window and cluster edges fade over this much
    std::array<double, 3> shift{}; // phason shift of the window, in window units
};

namespace quasicrystal_detail {

using Vec3d = std::array<double, 3>;
using Point6 = std::array<int, 6>;

inline double dot(const Vec3d& a, const Vec3d& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline Vec3d cross(const Vec3d& a, const Vec3d& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// The six icosahedral axes in parallel space and their Galois conjugates
// (golden ratio -> -1 / golden ratio) in perpendicular space, scaled so the
// lattice basis splits orthogonally.
struct Basis {
    std::array<Vec3d, 6> parallel{};
    std::array<Vec3d, 6> perpendicular{};
    std::vector<Vec3d> faceNormals; // the triacontahedron's, one per pair of axes
    std::vector<double> faceReach;  // its support along each normal
};

inline const Basis& basis() {
    static const Basis value = [] {
        Basis b;
        const double phi = (1.0 + std::sqrt(5.0)) / 2.0, psi = -1.0 / phi;
        const auto axes = [](const double g) {
            return std::array<Vec3d, 6>{{{1.0, g, 0.0}, {-1.0, g, 0.0}, {0.0, 1.0, g},
                                         {0.0, -1.0, g}, {g, 0.0, 1.0}, {-g, 0.0, 1.0}}};
        };
        b.parallel = axes(phi);
        b.perpendicular = axes(psi);
        for (auto* set : {&b.parallel, &b.perpendicular}) {
            for (auto& v : *set) {
                const double scale = 1.0 / std::sqrt(2.0 * dot(v, v));
                for (auto& c : v) c *= scale;
            }
        }
        for (std::size_t i = 0; i < 6; ++i) {
            for (std::size_t j = i + 1; j < 6; ++j) {
                Vec3d n = cross(b.perpendicular[i], b.perpendicular[j]);
                const double length = std::sqrt(dot(n, n));
                for (auto& c : n) c /= length;
                double reach = 0.0;
                for (const auto& v : b.perpendicular) reach += 0.5 * std::abs(dot(v, n));
                b.faceNormals.push_back(n);
                b.faceReach.push_back(reach);
            }
        }
        return b;
    }();
    return value;
}

inline Vec3d project(const Point6& n, const std::array<Vec3d, 6>& axes) {
    Vec3d out{};
    for (std::size_t i = 0; i < 6; ++i) {
        for (std::size_t c = 0; c < 3; ++c) out[c] += n[i] * axes[i][c];
    }
    return out;
}

// How far out of the window a perpendicular shadow lies: 1 on its faces.
inline double windowGauge(const Vec3d& x) {
    const auto& b = basis();
    double gauge = 0.0;
    for (std::size_t f = 0; f < b.faceNormals.size(); ++f) {
        gauge = std::max(gauge, std::abs(dot(x, b.faceNormals[f])) / b.faceReach[f]);
    }
    return gauge;
}

inline double smoothstep(const double edge0, const double edge1, const double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace quasicrystal_detail

// Edges of the quasicrystal cluster, scaled into the unit ball, w = 0 except
// for a hint of the window depth (how near a vertex is to flipping). Every
// edge carries how present it is, so vertices fade in and out at the window.
[[nodiscard]] inline std::vector<Edge4D> quasicrystalEdges(const QuasicrystalShape& shape,
                                                           const std::size_t maxEdges = 8192) {
    using namespace quasicrystal_detail;
    const auto& b = basis();
    const double edge = std::sqrt(0.5);
    const double radius = std::max(shape.radius, 1.0) * edge;
    const double window = std::max(shape.window, 0.2);
    const double soft = std::clamp(shape.softness, 0.01, 0.5);
    // The window's centre in perpendicular space; the shift is in window units
    // of its inradius.
    double inradius = 1e9;
    for (const double reach : b.faceReach) inradius = std::min(inradius, reach);
    const Vec3d centre{shape.shift[0] * inradius * window, shape.shift[1] * inradius * window,
                       shape.shift[2] * inradius * window};

    struct Vertex {
        Vec3d at;
        double presence;
        double depth;
    };
    const auto vertex = [&](const Point6& n, Vertex& out) {
        const Vec3d par = project(n, b.parallel);
        Vec3d perp = project(n, b.perpendicular);
        for (std::size_t c = 0; c < 3; ++c) perp[c] -= centre[c];
        const double gauge = windowGauge(perp) / window;
        const double reach = std::sqrt(dot(par, par)) / radius;
        if (gauge > 1.0 + 0.5 * soft || reach > 1.0) return false;
        out.at = par;
        out.presence = smoothstep(1.0 + 0.5 * soft, 1.0 - 0.5 * soft, gauge) *
                       smoothstep(1.0, 1.0 - soft * 4.0 * edge / radius, reach);
        out.depth = gauge;
        return out.presence > 1e-3;
    };

    // Start from the accepted point nearest the middle, then walk the lattice.
    Point6 start{};
    double best = 1e9;
    Vertex probe{};
    for (int code = 0; code < 15625; ++code) {
        Point6 n{};
        int rest = code;
        for (auto& v : n) {
            v = rest % 5 - 2;
            rest /= 5;
        }
        if (vertex(n, probe) && probe.presence > 0.5) {
            const double d = dot(probe.at, probe.at);
            if (d < best) {
                best = d;
                start = n;
            }
        }
    }
    std::vector<Edge4D> edges;
    if (best > 1e8) return edges;

    std::map<Point6, Vertex> seen;
    std::queue<Point6> open;
    Vertex first{};
    vertex(start, first);
    seen.emplace(start, first);
    open.push(start);
    while (!open.empty() && edges.size() < maxEdges) {
        const Point6 n = open.front();
        open.pop();
        const Vertex here = seen.at(n);
        for (int axis = 0; axis < 6; ++axis) {
            for (const int step : {1, -1}) {
                Point6 m = n;
                m[static_cast<std::size_t>(axis)] += step;
                auto found = seen.find(m);
                if (found == seen.end()) {
                    Vertex next{};
                    if (!vertex(m, next)) continue;
                    found = seen.emplace(m, next).first;
                    open.push(m);
                }
                // Each edge once: from its lower end along +axis.
                if (step < 0 || edges.size() >= maxEdges) continue;
                const Vertex& there = found->second;
                const auto lift = [&](const Vertex& v) {
                    return Vec4d{v.at[0] / radius, v.at[1] / radius, v.at[2] / radius,
                                 0.35 * (v.depth - 0.5)};
                };
                Edge4D e{lift(here), lift(there), 0, axis};
                e.presence = std::min(here.presence, there.presence);
                edges.push_back(e);
            }
        }
    }
    return edges;
}

} // namespace vkexp
