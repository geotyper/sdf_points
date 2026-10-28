#pragma once

// The order-5 cubic honeycomb {4,3,5} of hyperbolic space: cubes, five round
// every edge. Built by reflecting one cube in its faces, in the hyperboloid
// model (x, y, z, t), t^2 - x^2 - y^2 - z^2 = 1. The shader joins the ends of
// an edge along the geodesic, slides the whole honeycomb along x and maps it
// into the Poincare ball. Sliding by twice the distance from a cube's centre to
// its face carries the honeycomb onto itself, so the flow loops. Pure logic (no
// Vulkan) so it can be unit tested.

#include "vkexp/demo/Polytope4D.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <vector>

namespace vkexp {

namespace honeycomb_detail {

using Matrix4 = std::array<std::array<double, 4>, 4>;

// Klein coordinate of the base cube's faces: the dihedral angle is 2 pi / 5,
// cos = k^2 / (1 - k^2).
inline double faceKlein() {
    const double c = std::cos(2.0 * 3.14159265358979323846 / 5.0);
    return std::sqrt(c / (1.0 + c));
}

inline double minkowski(const Vec4d& a, const Vec4d& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] - a[3] * b[3];
}

inline Vec4d transformed(const Matrix4& m, const Vec4d& v) {
    Vec4d out{};
    for (std::size_t r = 0; r < 4; ++r) {
        for (std::size_t c = 0; c < 4; ++c) out[r] += m[r][c] * v[c];
    }
    return out;
}

inline Matrix4 times(const Matrix4& a, const Matrix4& b) {
    Matrix4 out{};
    for (std::size_t r = 0; r < 4; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            for (std::size_t k = 0; k < 4; ++k) out[r][c] += a[r][k] * b[k][c];
        }
    }
    return out;
}

// Reflection in the plane Minkowski-orthogonal to the spacelike normal n.
inline Matrix4 reflection(const Vec4d& n) {
    const double nn = minkowski(n, n);
    const Vec4d lowered{n[0], n[1], n[2], -n[3]};
    Matrix4 m{};
    for (std::size_t r = 0; r < 4; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            m[r][c] = (r == c ? 1.0 : 0.0) - 2.0 * n[r] * lowered[c] / nn;
        }
    }
    return m;
}

inline std::array<long, 4> key(const Vec4d& v) {
    std::array<long, 4> out{};
    for (std::size_t i = 0; i < 4; ++i) out[i] = std::lround(v[i] * 1e5);
    return out;
}

} // namespace honeycomb_detail

// How far the honeycomb slides in one loop: twice the distance from a cube's
// centre to its face.
[[nodiscard]] inline double honeycombPeriod() {
    return 2.0 * std::atanh(honeycomb_detail::faceKlein());
}

// Edges of the cubes nearest the path the flow takes (x from 0 to one period),
// nearest first, up to `maxEdges`. Part is how many reflections away a cube
// is, direction which axis of its cube an edge runs along.
[[nodiscard]] inline std::vector<Edge4D> honeycombEdges(const std::size_t maxEdges = 8192) {
    using namespace honeycomb_detail;
    const double k = faceKlein();
    const double lift = 1.0 / std::sqrt(1.0 - 3.0 * k * k);
    std::array<Matrix4, 6> mirrors{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        for (const int side : {0, 1}) {
            Vec4d n{};
            n[axis] = 1.0;
            n[3] = side == 0 ? k : -k;
            mirrors[2 * axis + static_cast<std::size_t>(side)] = reflection(n);
        }
    }
    // The base cube's twelve edges.
    std::vector<std::pair<Vec4d, Vec4d>> base;
    std::vector<int> baseAxis;
    for (int corner = 0; corner < 8; ++corner) {
        for (int axis = 0; axis < 3; ++axis) {
            if ((corner >> axis) & 1) continue;
            const auto vertex = [&](const int c) {
                return Vec4d{((c & 1) ? k : -k) * lift, ((c & 2) ? k : -k) * lift,
                             ((c & 4) ? k : -k) * lift, lift};
            };
            base.emplace_back(vertex(corner), vertex(corner | (1 << axis)));
            baseAxis.push_back(axis);
        }
    }
    // Distance of a cell centre to the flow's path, sampled along it.
    const double period = honeycombPeriod();
    const auto pathDistance = [&](const Vec4d& centre) {
        double best = 1e9;
        for (int i = 0; i <= 8; ++i) {
            const double u = period * i / 8.0;
            const double coshDistance = centre[3] * std::cosh(u) - centre[0] * std::sinh(u);
            best = std::min(best, std::acosh(std::max(coshDistance, 1.0)));
        }
        return best;
    };

    struct Cell {
        double distance;
        Matrix4 transform;
        int depth;
        bool operator<(const Cell& other) const { return distance > other.distance; }
    };
    Matrix4 identity{};
    for (std::size_t i = 0; i < 4; ++i) identity[i][i] = 1.0;
    std::priority_queue<Cell> open;
    std::set<std::array<long, 4>> seenCells;
    std::set<std::array<long, 4>> seenEdges;
    const Vec4d origin{0.0, 0.0, 0.0, 1.0};
    open.push({0.0, identity, 0});
    seenCells.insert(key(origin));
    std::vector<Edge4D> edges;
    while (!open.empty() && edges.size() < maxEdges) {
        const Cell cell = open.top();
        open.pop();
        for (std::size_t e = 0; e < base.size() && edges.size() < maxEdges; ++e) {
            const Vec4d a = transformed(cell.transform, base[e].first);
            const Vec4d b = transformed(cell.transform, base[e].second);
            Vec4d middle{};
            for (std::size_t i = 0; i < 4; ++i) middle[i] = a[i] + b[i];
            if (seenEdges.insert(key(middle)).second) {
                edges.push_back({a, b, cell.depth, baseAxis[e]});
            }
        }
        for (const auto& mirror : mirrors) {
            // Reflecting in a face of this cell: conjugate the base mirror.
            const Matrix4 next = times(cell.transform, mirror);
            const Vec4d centre = transformed(next, origin);
            if (seenCells.insert(key(centre)).second) {
                open.push({pathDistance(centre), next, cell.depth + 1});
            }
        }
    }
    return edges;
}

} // namespace vkexp
