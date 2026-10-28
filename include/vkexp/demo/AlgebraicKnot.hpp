#pragma once

// Links of complex plane curves: where the curve z1^p = g(z2) meets the unit
// 3-sphere |z1|^2 + |z2|^2 = 1. With g = z2^q that is the (p, q) torus knot;
// as the roots of g move apart or collide the link passes through singular
// curves and turns into other knots and links. Pure logic (no Vulkan) so it
// can be unit tested.
//
// On the sphere |z1| = sqrt(1 - |z2|^2), so z2 runs along the level curve
// |g(z2)|^(2/p) + |z2|^2 = 1 inside the unit disk, traced by marching squares;
// over every point of it lie the p values of z1, the p-th roots of g(z2).

#include "vkexp/demo/Polytope4D.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <map>
#include <numeric>
#include <utility>
#include <vector>

namespace vkexp {

struct AlgebraicKnotShape {
    int p{2};                             // power of z1
    std::vector<std::complex<double>> roots{}; // roots of g, inside the unit disk
    double gain{1.0};                     // g is scaled by this
    int grid{220};                        // marching squares cells across the disk
    double segment{0.035};                // target segment length in z2, before lifting
};

namespace knot_detail {

using Complex = std::complex<double>;

inline Complex g(const AlgebraicKnotShape& shape, const Complex z) {
    Complex value = shape.gain;
    for (const auto& root : shape.roots) value *= z - root;
    return value;
}

inline double level(const AlgebraicKnotShape& shape, const Complex z) {
    return std::pow(std::abs(g(shape, z)), 2.0 / shape.p) + std::norm(z) - 1.0;
}

// Closed (or, at the grid edge, open) polylines of the level curve in z2.
inline std::vector<std::vector<Complex>> levelCurves(const AlgebraicKnotShape& shape) {
    const int n = std::max(shape.grid, 16);
    const double lo = -1.02, step = 2.04 / n;
    std::vector<double> value(static_cast<std::size_t>((n + 1) * (n + 1)));
    const auto at = [&](const int i, const int j) -> double& {
        return value[static_cast<std::size_t>(j * (n + 1) + i)];
    };
    for (int j = 0; j <= n; ++j) {
        for (int i = 0; i <= n; ++i) {
            double v = level(shape, {lo + i * step, lo + j * step});
            if (v == 0.0) v = 1e-12;
            at(i, j) = v;
        }
    }
    // Crossings are keyed by grid edge: 2 * (vertex index) + (0 along x, 1 along y).
    const auto key = [&](const int i, const int j, const int axis) {
        return 2L * (static_cast<long>(j) * (n + 1) + i) + axis;
    };
    std::map<long, Complex> point;
    const auto crossing = [&](const int i, const int j, const int axis) {
        const long k = key(i, j, axis);
        if (!point.count(k)) {
            const double a = at(i, j), b = axis == 0 ? at(i + 1, j) : at(i, j + 1);
            const double t = a / (a - b);
            point[k] = axis == 0 ? Complex{lo + (i + t) * step, lo + j * step}
                                 : Complex{lo + i * step, lo + (j + t) * step};
        }
        return k;
    };
    std::map<long, std::vector<long>> links;
    const auto join = [&](const long a, const long b) {
        links[a].push_back(b);
        links[b].push_back(a);
    };
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const bool c0 = at(i, j) < 0, c1 = at(i + 1, j) < 0, c2 = at(i + 1, j + 1) < 0,
                       c3 = at(i, j + 1) < 0;
            // Cell edges: bottom, right, top, left.
            std::vector<long> cut;
            if (c0 != c1) cut.push_back(crossing(i, j, 0));
            if (c1 != c2) cut.push_back(crossing(i + 1, j, 1));
            if (c3 != c2) cut.push_back(crossing(i, j + 1, 0));
            if (c0 != c3) cut.push_back(crossing(i, j, 1));
            if (cut.size() == 2) {
                join(cut[0], cut[1]);
            } else if (cut.size() == 4) {
                // Saddle: the centre decides which corners connect.
                const double centre = 0.25 * (at(i, j) + at(i + 1, j) + at(i + 1, j + 1) + at(i, j + 1));
                if ((centre < 0) == c0) {
                    join(cut[0], cut[1]);
                    join(cut[2], cut[3]);
                } else {
                    join(cut[0], cut[3]);
                    join(cut[1], cut[2]);
                }
            }
        }
    }
    std::vector<std::vector<Complex>> curves;
    std::map<long, bool> used;
    const auto walk = [&](const long from) {
        std::vector<Complex> curve{point[from]};
        used[from] = true;
        long previous = -1, here = from;
        while (true) {
            long next = -1;
            for (const long candidate : links[here]) {
                if (candidate != previous && (!used[candidate] || (candidate == from && curve.size() > 2))) {
                    next = candidate;
                    break;
                }
            }
            if (next < 0) break;
            curve.push_back(point[next]);
            if (next == from) break;
            used[next] = true;
            previous = here;
            here = next;
        }
        return curve;
    };
    // Open curves first (from their ends), then the closed loops.
    for (const auto& [k, neighbours] : links) {
        if (neighbours.size() == 1 && !used[k]) curves.push_back(walk(k));
    }
    for (const auto& [k, neighbours] : links) {
        if (!used[k]) curves.push_back(walk(k));
    }
    return curves;
}

// Even spacing of `segment` along a polyline; a closed one stays closed.
inline std::vector<Complex> resample(const std::vector<Complex>& curve, const double segment) {
    if (curve.size() < 2) return curve;
    std::vector<double> length{0.0};
    for (std::size_t i = 1; i < curve.size(); ++i) {
        length.push_back(length.back() + std::abs(curve[i] - curve[i - 1]));
    }
    const int count = std::max(3, static_cast<int>(std::lround(length.back() / segment)));
    std::vector<Complex> out;
    std::size_t at = 1;
    for (int k = 0; k <= count; ++k) {
        const double target = length.back() * k / count;
        while (at + 1 < curve.size() && length[at] < target) ++at;
        const double span = std::max(length[at] - length[at - 1], 1e-12);
        const double t = std::clamp((target - length[at - 1]) / span, 0.0, 1.0);
        out.push_back(curve[at - 1] + t * (curve[at] - curve[at - 1]));
    }
    return out;
}

} // namespace knot_detail

// Edges of the link on the unit 3-sphere as (Re z1, Im z1, Re z2, Im z2). The
// part of an edge is its link component, so parts colour the components.
[[nodiscard]] inline std::vector<Edge4D> algebraicKnotEdges(const AlgebraicKnotShape& shape,
                                                            const std::size_t maxEdges = 8192) {
    using namespace knot_detail;
    constexpr double tau = 6.28318530717958647692;
    const int p = std::max(shape.p, 1);
    std::vector<Edge4D> edges;
    int component = 0;
    for (const auto& raw : levelCurves(shape)) {
        const auto curve = resample(raw, std::max(shape.segment, 0.005));
        if (curve.size() < 2) continue;
        // Unwrapped argument of g along the curve: the lifts follow it.
        std::vector<double> angle{std::arg(g(shape, curve.front()))};
        for (std::size_t i = 1; i < curve.size(); ++i) {
            double delta = std::arg(g(shape, curve[i])) - std::arg(g(shape, curve[i - 1]));
            delta -= tau * std::round(delta / tau);
            angle.push_back(angle.back() + delta);
        }
        const bool closed = std::abs(curve.front() - curve.back()) < 1e-9;
        // A closed curve around which g winds w times: lift k continues as lift
        // k + w, so the p lifts join into gcd(w, p) loops.
        const int winding = closed ? static_cast<int>(std::lround((angle.back() - angle.front()) / tau)) : 0;
        const int loops = closed ? std::gcd(((winding % p) + p) % p, p) : p;
        const int loopsHere = loops == 0 ? p : loops;
        for (int k = 0; k < p; ++k) {
            for (std::size_t i = 0; i + 1 < curve.size(); ++i) {
                if (edges.size() >= maxEdges) return edges;
                const auto lift = [&](const std::size_t at) {
                    const double r = std::sqrt(std::max(1.0 - std::norm(curve[at]), 0.0));
                    const double a = (angle[at] + tau * k) / p;
                    return Vec4d{r * std::cos(a), r * std::sin(a), curve[at].real(), curve[at].imag()};
                };
                Edge4D e{lift(i), lift(i + 1), component + k % loopsHere, 0};
                edges.push_back(e);
            }
        }
        component += loopsHere;
    }
    return edges;
}

} // namespace vkexp
