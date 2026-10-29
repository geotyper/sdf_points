#pragma once

// Metaballs (geometry mode 25): balls whose fields add up, drawn as the
// surface where the sum reaches a threshold. A "ball" may also be a capsule
// (a segment's field) or a ring (a torus); capsules of one group, as a
// worm's segments, take the strongest of their fields rather than the sum,
// so a chain is one smooth tube. The points are particles living
// on the surface, carried along with the balls and kept evenly spread by
// metaballs.comp. The CPU moves the balls each frame; this header is pure
// logic (no Vulkan) so it can be unit tested.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vkexp {

using Vec3d = std::array<double, 3>;

inline constexpr int metaballLimit = 24;
// vec4 per ball in the storage buffer (metaballs.glsl): centre and radius,
// colour and where its share of the area starts, extent and kind, centre and
// radius the frame before, extent the frame before and group. The particles'
// settings follow the last ball's slots.
inline constexpr std::size_t metaballVec4 = 5;
inline constexpr std::size_t metaballHeaderVec4 = 3;
// Particles and neighbour table cells (a power of two) in the shared storage
// buffer; metaballs.glsl holds the same numbers.
inline constexpr int metaballParticleLimit = 600000;
inline constexpr std::uint32_t metaballTableCells = 1U << 19U;

inline constexpr int metaballMotionCount = 6;
inline constexpr std::array<const char*, metaballMotionCount> metaballMotionNames{
    "Orbits", "Cell division", "Lava lamp", "Worms", "Rings", "Ring fountain"};
// Segments per worm in the Worms motion.
inline constexpr int metaballWormSegments = 4;

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

// The core is the centre alone, the segment centre +- extent, or with `ring`
// the circle of radius |extent| about the extent's axis; the surface lies
// `radius` out from it. Balls of one group (>= 0, consecutive) take the
// strongest of their fields; a group of -1 is the ball's own.
struct Metaball {
    Vec3d centre{};
    double radius{};
    Vec3d extent{};
    bool ring{false};
    int group{-1};
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
inline double dot(const Vec3d& a, const Vec3d& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline Vec3d add(const Vec3d& a, const Vec3d& b, const double s = 1.0) {
    return {a[0] + s * b[0], a[1] + s * b[1], a[2] + s * b[2]};
}
inline Vec3d cross(const Vec3d& a, const Vec3d& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// A unit vector square to the unit `axis`.
inline Vec3d across(const Vec3d& axis) {
    const Vec3d c = cross(axis, std::abs(axis[1]) < 0.9 ? Vec3d{0.0, 1.0, 0.0} : Vec3d{1.0, 0.0, 0.0});
    const double l = length(c);
    return {c[0] / l, c[1] / l, c[2] / l};
}

} // namespace metaball_detail

// Threshold of the summed field: a ball alone ends at its radius.
// Kernel (1 - d^2 / R^2)^3 inside the reach R = reach * radius.
[[nodiscard]] inline double metaballThreshold(const double reach) {
    const double q = 1.0 - 1.0 / std::max(reach * reach, 1.0001);
    return q * q * q;
}

// The point of the ball's core nearest `p`.
[[nodiscard]] inline Vec3d metaballCore(const Metaball& ball, const Vec3d& p) {
    using namespace metaball_detail;
    const Vec3d v = add(p, ball.centre, -1.0);
    if (ball.ring) {
        const double major = length(ball.extent);
        const Vec3d axis{ball.extent[0] / major, ball.extent[1] / major, ball.extent[2] / major};
        const Vec3d around = add(v, axis, -dot(v, axis));
        const double l = length(around);
        const Vec3d unit = l > 1e-9 ? Vec3d{around[0] / l, around[1] / l, around[2] / l} : across(axis);
        return add(ball.centre, unit, major);
    }
    const double e2 = dot(ball.extent, ball.extent);
    const double s = e2 > 1e-12 ? std::clamp(dot(v, ball.extent) / e2, -1.0, 1.0) : 0.0;
    return add(ball.centre, ball.extent, s);
}

[[nodiscard]] inline double metaballField(const std::vector<Metaball>& balls, const double reach,
                                          const Vec3d& p) {
    double sum = 0.0;
    double best = 0.0;
    for (std::size_t i = 0; i < balls.size(); ++i) {
        const Metaball& ball = balls[i];
        if (i == 0 || ball.group < 0 || ball.group != balls[i - 1].group) {
            sum += best;
            best = 0.0;
        }
        const double far = reach * ball.radius;
        const Vec3d d = metaball_detail::add(p, metaballCore(ball, p), -1.0);
        const double q = 1.0 - metaball_detail::dot(d, d) / (far * far);
        if (q > 0.0) {
            best = std::max(best, q * q * q);
        }
    }
    return sum + best;
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
        if (params.motion == 3) {
            // Worms: chains of capsules, each worm's head on a Lissajous path
            // and its segments on the same path a little behind.
            const int worm = i / metaballWormSegments;
            const int link = i % metaballWormSegments;
            const auto wormHash = [&](const int salt) { return hash(worm, seed + salt); };
            const auto wormTurns = [&](const int salt) {
                return (1.0 + std::floor(3.0 * wormHash(salt))) * (wormHash(salt + 50) < 0.5 ? -1.0 : 1.0);
            };
            const std::array<double, 3> spin{wormTurns(20), wormTurns(22), wormTurns(24)};
            const std::array<double, 3> shift{tau * wormHash(21), tau * wormHash(23), tau * wormHash(25)};
            const auto path = [&](const double t) {
                return Vec3d{params.spread * std::sin(spin[0] * t + shift[0]),
                             0.8 * params.spread * std::sin(spin[1] * t + shift[1]),
                             params.spread * std::sin(spin[2] * t + shift[2])};
            };
            const double thick = 0.6 * params.radius *
                                 std::max(1.0 + params.variation * (2.0 * wormHash(26) - 1.0), 0.2) *
                                 (1.0 + params.breathe * std::sin(2.0 * phase + tau * wormHash(27)));
            const double speed = std::max(params.spread, 0.05) *
                                 std::sqrt(0.5 * (spin[0] * spin[0] + 0.64 * spin[1] * spin[1] + spin[2] * spin[2]));
            const double lag = 1.8 * thick / speed;
            const Vec3d front = path(phase - link * lag);
            const Vec3d back = path(phase - (link + 1) * lag);
            ball.centre = {0.5 * (front[0] + back[0]), 0.5 * (front[1] + back[1]), 0.5 * (front[2] + back[2])};
            ball.extent = {0.5 * (front[0] - back[0]), 0.5 * (front[1] - back[1]), 0.5 * (front[2] - back[2])};
            ball.radius = thick * (1.0 - 0.12 * link);
            ball.group = worm;
            continue;
        }
        if (params.motion == 4) {
            // Rings: tori tumbling on Lissajous paths, melting into each
            // other where they cross.
            const double tilt = turns(30) * phase + offset(31);
            const double heading = turns(32) * phase + offset(33);
            const double major = 1.5 * ball.radius;
            ball.extent = {major * std::sin(tilt) * std::cos(heading), major * std::cos(tilt),
                           major * std::sin(tilt) * std::sin(heading)};
            ball.ring = true;
            ball.radius *= 0.38;
            ball.centre = {0.7 * params.spread * std::sin(turns(10) * phase + offset(11)),
                           0.56 * params.spread * std::sin(turns(12) * phase + offset(13)),
                           0.7 * params.spread * std::sin(turns(14) * phase + offset(15))};
            ball.group = i;
            continue;
        }
        if (params.motion == 5) {
            // Ring fountain: a level ring, and balls winding round its tube,
            // up through the hole and down outside, each at its own place
            // round the ring that drifts once round per cycle.
            const double major = 0.9 * std::max(params.spread, 0.25);
            if (i == 0) {
                ball.centre = {0.0, 0.0, 0.0};
                ball.extent = {0.0, major, 0.0};
                ball.ring = true;
                ball.radius = 0.45 * params.radius * swell;
            } else {
                const double place = tau * (i - 1) / std::max(count - 1, 1) + phase;
                const double wind = std::abs(turns(16)) * phase + offset(17);
                const double loop = 0.95 * major;
                const double out = major + loop * std::cos(wind);
                ball.centre = {out * std::cos(place), -1.1 * loop * std::sin(wind), out * std::sin(place)};
                ball.radius *= 0.7;
            }
            ball.group = i;
            continue;
        }
        ball.group = i;
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

// From `origin` on a ball's core (of `radius`) along `direction`: the first
// distance where the field falls below the threshold, or -1 if none is met
// within the march. A ball's own field already reaches the threshold at its
// radius, so the march starts there.
[[nodiscard]] inline double metaballRay(const std::vector<Metaball>& balls, const double reach, const Vec3d& origin,
                                        const double radius, const Vec3d& direction) {
    const double threshold = metaballThreshold(reach);
    const auto at = [&](const double t) { return metaballField(balls, reach, metaball_detail::add(origin, direction, t)); };
    double low = radius;
    const double step = 0.15 * radius;
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

// The same from the centre of ball `ball`.
[[nodiscard]] inline double metaballRay(const std::vector<Metaball>& balls, const double reach,
                                        const std::size_t ball, const Vec3d& direction) {
    return metaballRay(balls, reach, balls[ball].centre, balls[ball].radius, direction);
}

// Points of the ball's core to march out from: its centre, a segment's ends
// and middle, six points round a ring.
[[nodiscard]] inline std::vector<Vec3d> metaballCorePoints(const Metaball& ball) {
    using namespace metaball_detail;
    const double major = length(ball.extent);
    if (ball.ring) {
        const Vec3d axis{ball.extent[0] / major, ball.extent[1] / major, ball.extent[2] / major};
        const Vec3d u = across(axis), v = cross(axis, u);
        std::vector<Vec3d> points;
        for (int k = 0; k < 6; ++k) {
            const double turn = 6.28318530717958647692 * k / 6.0;
            points.push_back(add(add(ball.centre, u, major * std::cos(turn)), v, major * std::sin(turn)));
        }
        return points;
    }
    if (major > 1e-9) {
        return {add(ball.centre, ball.extent, -1.0), ball.centre, add(ball.centre, ball.extent)};
    }
    return {ball.centre};
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
        for (const Metaball& ball : balls) {
            for (const Vec3d& c : metaballCorePoints(ball)) {
                for (const Vec3d& d : directions) {
                    const double t = metaballRay(balls, params.reach, c, ball.radius, d);
                    if (t > 0.0) {
                        bound = std::max(bound, metaball_detail::length(metaball_detail::add(c, d, t)));
                    }
                }
            }
        }
    }
    return bound;
}

// The area of a ball standing alone: a sphere, a capsule or a torus.
[[nodiscard]] inline double metaballArea(const Metaball& ball) {
    constexpr double pi = 3.14159265358979323846;
    const double major = metaball_detail::length(ball.extent);
    if (ball.ring) {
        return 4.0 * pi * pi * major * ball.radius;
    }
    return 4.0 * pi * ball.radius * ball.radius + 4.0 * pi * ball.radius * major;
}

// Where each ball's share of the total area starts (0 .. 1): new particles
// pick their ball by it, so every stretch of surface is as likely.
[[nodiscard]] inline std::vector<double> metaballAreaStarts(const std::vector<Metaball>& balls) {
    double total = 0.0;
    for (const Metaball& ball : balls) {
        total += metaballArea(ball);
    }
    std::vector<double> starts;
    double running = 0.0;
    for (const Metaball& ball : balls) {
        starts.push_back(total > 0.0 ? running / total : 0.0);
        running += metaballArea(ball);
    }
    return starts;
}

// Spacing of the particles: `count` of them over the balls' area apart.
[[nodiscard]] inline double metaballSpacing(const std::vector<Metaball>& balls, const int count) {
    double area = 0.0;
    for (const Metaball& ball : balls) {
        area += metaballArea(ball);
    }
    return std::sqrt(area / std::max(count, 1));
}

} // namespace vkexp
