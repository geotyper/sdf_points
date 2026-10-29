#pragma once

// Metaballs (geometry mode 25): balls whose fields add up, drawn as the
// surface where the sum reaches a threshold. Every ball carries its own
// sphere of points; a point keeps its direction from the ball's centre and
// slides out along it to the surface, and shows only where its ball's field
// is the strongest, so merging balls share the skin between them. The CPU
// moves the balls each frame; this header is pure logic (no Vulkan) so it can
// be unit tested.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vkexp {

using Vec3d = std::array<double, 3>;

inline constexpr int metaballLimit = 24;
// vec4 per ball in the storage buffer (metaballs.glsl): centre and radius,
// colour and first point, (points, field reach, 0, 0).
inline constexpr std::size_t metaballVec4 = 3;

inline constexpr int metaballMotionCount = 3;
inline constexpr std::array<const char*, metaballMotionCount> metaballMotionNames{
    "Orbits", "Cell division", "Lava lamp"};

struct MetaballParams {
    int motion{0};
    int balls{7};
    double radius{0.32};   // radius of a ball standing alone
    double variation{0.35}; // spread of the radii, 0 all equal
    double reach{2.0};     // field reach in radii: higher, the balls reach out further
    double spread{0.75};   // how far the balls travel
    double breathe{0.12};  // radii swelling and easing
    int seed{1};
};

struct Metaball {
    Vec3d centre{};
    double radius{};
};

namespace metaball_detail {

// Deterministic 0..1 per ball and stream.
inline double hash(const int index, const int salt) {
    std::uint32_t h = static_cast<std::uint32_t>(index) * 747796405U +
                      static_cast<std::uint32_t>(salt) * 2891336453U + 1U;
    h = ((h >> ((h >> 28U) + 4U)) ^ h) * 277803737U;
    h = (h >> 22U) ^ h;
    return static_cast<double>(h >> 8U) / 16777216.0;
}

inline double length(const Vec3d& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

} // namespace metaball_detail

// Threshold of the summed field: a ball alone ends at its radius.
// Kernel (1 - d^2 / R^2)^3 inside the reach R = reach * radius.
[[nodiscard]] inline double metaballThreshold(const double reach) {
    const double q = 1.0 - 1.0 / std::max(reach * reach, 1.0001);
    return q * q * q;
}

[[nodiscard]] inline double metaballField(const std::vector<Metaball>& balls, const double reach,
                                          const Vec3d& p) {
    double sum = 0.0;
    for (const Metaball& ball : balls) {
        const double far = reach * ball.radius;
        const Vec3d d{p[0] - ball.centre[0], p[1] - ball.centre[1], p[2] - ball.centre[2]};
        const double q = 1.0 - (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) / (far * far);
        if (q > 0.0) {
            sum += q * q * q;
        }
    }
    return sum;
}

// The balls at `phase` (radians, one cycle per 2 pi): every motion runs whole
// turns per cycle, so the loop closes.
[[nodiscard]] inline std::vector<Metaball> metaballScene(const MetaballParams& params, const double phase) {
    using metaball_detail::hash;
    constexpr double tau = 6.28318530717958647692;
    const int count = std::clamp(params.balls, 1, metaballLimit);
    const int seed = params.seed * 97;
    std::vector<Metaball> balls(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        Metaball& ball = balls[static_cast<std::size_t>(i)];
        const double size = 1.0 + params.variation * (2.0 * hash(i, seed + 1) - 1.0);
        const double swell = 1.0 + params.breathe * std::sin(2.0 * phase + tau * hash(i, seed + 2));
        ball.radius = params.radius * std::max(size, 0.2) * swell;
        // Whole turns per cycle, one to three, either way round.
        const auto turns = [&](const int salt) {
            return (1.0 + std::floor(3.0 * hash(i, seed + salt))) * (hash(i, seed + salt + 50) < 0.5 ? -1.0 : 1.0);
        };
        const auto offset = [&](const int salt) { return tau * hash(i, seed + salt); };
        if (params.motion == 1) {
            // Cell division: the balls sit together as one cell, part along
            // Fibonacci directions turning about the vertical, and come back.
            const double y = count > 1 ? 1.0 - 2.0 * (i + 0.5) / count : 0.0;
            const double around = 2.39996322973 * i;
            const double flat = std::sqrt(std::max(0.0, 1.0 - y * y));
            const double part = 0.5 - 0.5 * std::cos(phase - 0.6 * hash(i, seed + 3));
            const double eased = part * part * (3.0 - 2.0 * part);
            const double distance = params.spread * (0.12 + 0.88 * eased);
            const double turn = around + 0.8 * eased + phase;
            ball.centre = {distance * flat * std::cos(turn), distance * y, distance * flat * std::sin(turn)};
        } else if (params.motion == 2) {
            // Lava lamp: rising and sinking in a column, wobbling sideways.
            const double rise = std::sin(std::abs(turns(4)) * phase + offset(5));
            const double wobble = 0.3 * params.spread;
            ball.centre = {wobble * std::sin(turns(6) * phase + offset(7)),
                           1.3 * params.spread * rise,
                           wobble * std::cos(turns(8) * phase + offset(9))};
        } else {
            // Orbits: Lissajous paths through a shared box.
            ball.centre = {params.spread * std::sin(turns(10) * phase + offset(11)),
                           0.8 * params.spread * std::sin(turns(12) * phase + offset(13)),
                           params.spread * std::sin(turns(14) * phase + offset(15))};
        }
    }
    return balls;
}

// From the ball's centre along `direction`: the first distance where the
// field falls below the threshold, or -1 if none is met within the march.
// A ball's own field already reaches the threshold at its radius, so the
// march starts there.
[[nodiscard]] inline double metaballRay(const std::vector<Metaball>& balls, const double reach,
                                        const std::size_t ball, const Vec3d& direction) {
    const double threshold = metaballThreshold(reach);
    const Metaball& own = balls[ball];
    const auto at = [&](const double t) {
        return metaballField(balls, reach, {own.centre[0] + t * direction[0], own.centre[1] + t * direction[1],
                                            own.centre[2] + t * direction[2]});
    };
    double low = own.radius;
    const double step = 0.15 * own.radius;
    double high = -1.0;
    for (int k = 0; k < 64; ++k) {
        if (at(low + step) < threshold) {
            high = low + step;
            break;
        }
        low += step;
    }
    if (high < 0.0) {
        return -1.0;
    }
    for (int k = 0; k < 10; ++k) {
        const double middle = 0.5 * (low + high);
        (at(middle) < threshold ? high : low) = middle;
    }
    return 0.5 * (low + high);
}

// How far the surface reaches from the origin over a whole cycle, sampled:
// a camera span that does not breathe.
[[nodiscard]] inline double metaballBound(const MetaballParams& params) {
    constexpr double tau = 6.28318530717958647692;
    std::vector<Vec3d> directions;
    for (int i = 0; i < 26; ++i) {
        const double y = 1.0 - 2.0 * (i + 0.5) / 26.0;
        const double flat = std::sqrt(1.0 - y * y);
        directions.push_back({flat * std::cos(2.39996322973 * i), y, flat * std::sin(2.39996322973 * i)});
    }
    double bound = 0.1;
    for (int s = 0; s < 48; ++s) {
        const auto balls = metaballScene(params, tau * s / 48.0);
        for (std::size_t b = 0; b < balls.size(); ++b) {
            for (const Vec3d& d : directions) {
                const double t = metaballRay(balls, params.reach, b, d);
                if (t > 0.0) {
                    const Vec3d& c = balls[b].centre;
                    bound = std::max(bound, metaball_detail::length({c[0] + t * d[0], c[1] + t * d[1],
                                                                     c[2] + t * d[2]}));
                }
            }
        }
    }
    return bound;
}

// Points per ball by its area at rest; returns the running first indices,
// one more than there are balls.
[[nodiscard]] inline std::vector<int> metaballFirstPoints(const std::vector<Metaball>& balls,
                                                          const MetaballParams& params, const int budget) {
    double total = 0.0;
    std::vector<double> areas;
    for (std::size_t i = 0; i < balls.size(); ++i) {
        const double size = 1.0 + params.variation * (2.0 * metaball_detail::hash(static_cast<int>(i), params.seed * 97 + 1) - 1.0);
        areas.push_back(std::max(size, 0.2) * std::max(size, 0.2));
        total += areas.back();
    }
    std::vector<int> first{0};
    for (const double area : areas) {
        first.push_back(first.back() + std::max(static_cast<int>(std::lround(budget * area / total)), 16));
    }
    return first;
}

} // namespace vkexp
