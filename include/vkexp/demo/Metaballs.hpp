#pragma once

// Metaballs (geometry mode 25): balls whose fields add up, drawn as the
// surface where the sum reaches a threshold. A "ball" may also be a capsule
// (a segment's field), a ring (a torus) or a turned cube (a rounded cube); capsules of one group, as a
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

inline constexpr int metaballLimit = 64;
// vec4 per ball in the storage buffer (metaballs.glsl): centre and radius,
// colour and where its share of the area starts, extent and kind, centre and
// radius the frame before, extent the frame before and group, a cube's half
// size now and the frame before and the field's weight. The particles'
// settings follow the last ball's slots.
inline constexpr std::size_t metaballVec4 = 6;
inline constexpr std::size_t metaballHeaderVec4 = 3;
// Particles and neighbour table cells (a power of two) in the shared storage
// buffer; metaballs.glsl holds the same numbers.
inline constexpr int metaballParticleLimit = 600000;
inline constexpr std::uint32_t metaballTableCells = 1U << 18U;

inline constexpr int metaballMotionCount = 11;
inline constexpr std::array<const char*, metaballMotionCount> metaballMotionNames{
    "Orbits", "Cell division", "Lava lamp", "Worms", "Rings", "Ring fountain",
    "Dice", "Cube block", "Holes", "Peristalsis", "Cube carpet"};
// Capsules tracing each worm in the Worms motion, and the most worms.
inline constexpr int metaballWormSegments = 10;
inline constexpr int metaballWormLimit = metaballLimit / metaballWormSegments;

struct MetaballParams {
    int motion{0};
    int balls{7};
    double radius{0.32};   // radius of a ball standing alone
    double variation{0.35}; // spread of the radii, 0 all equal
    double reach{2.0};     // field reach in radii: higher, the balls reach out further
    double spread{0.75};   // how far the balls travel
    double breathe{0.12};  // radii swelling and easing
    double roundness{0.3}; // a cube's rounding, as a share of its half size
    int seed{1};
};

// The core is the centre alone, the segment centre +- extent, with `ring`
// the circle of radius |extent| about the extent's axis, or with `cube` the
// cube of half size `half` turned by the unit quaternion whose x, y, z are
// the extent (w >= 0); the surface lies `radius` out from it. Balls of one group (>= 0, consecutive) take the
// strongest of their fields; a group of -1 is the ball's own. The field
// counts times `weight`: a negative one carves dents, holes and waists.
struct Metaball {
    Vec3d centre{};
    double radius{};
    Vec3d extent{};
    bool ring{false};
    bool cube{false};
    double half{};
    int group{-1};
    double weight{1.0};
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

// `v` turned by the unit quaternion with x, y, z `turn` and w >= 0 (`back`
// turns the other way).
inline Vec3d turn(const Vec3d& turn, const Vec3d& v, const bool back) {
    const double w = std::sqrt(std::max(0.0, 1.0 - dot(turn, turn)));
    const Vec3d axis = back ? Vec3d{-turn[0], -turn[1], -turn[2]} : turn;
    return add(v, cross(axis, add(cross(axis, v), v, w)), 2.0);
}

// The x, y, z of the quaternion turning by `angle` about the unit `axis`,
// signed so that its w is not negative.
inline Vec3d turnAbout(const Vec3d& axis, const double angle) {
    const double s = std::sin(0.5 * angle) * (std::cos(0.5 * angle) < 0.0 ? -1.0 : 1.0);
    return {s * axis[0], s * axis[1], s * axis[2]};
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
    if (ball.cube) {
        Vec3d local = turn(ball.extent, v, true);
        for (double& x : local) {
            x = std::clamp(x, -ball.half, ball.half);
        }
        return add(ball.centre, turn(ball.extent, local, false));
    }
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
    double weight = 1.0;
    for (std::size_t i = 0; i < balls.size(); ++i) {
        const Metaball& ball = balls[i];
        if (i == 0 || ball.group < 0 || ball.group != balls[i - 1].group) {
            sum += weight * best;
            best = 0.0;
            weight = ball.weight;
        }
        const double far = reach * ball.radius;
        const Vec3d d = metaball_detail::add(p, metaballCore(ball, p), -1.0);
        const double q = 1.0 - metaball_detail::dot(d, d) / (far * far);
        if (q > 0.0) {
            best = std::max(best, q * q * q);
        }
    }
    return sum + weight * best;
}

// How many balls the motion makes of `params.balls`: Worms take that many
// worms, Cube carpet the square it fills.
[[nodiscard]] inline int metaballSceneCount(const MetaballParams& params) {
    const int asked = std::clamp(params.balls, 1, metaballLimit);
    if (params.motion == 3) {
        return std::min(asked, metaballWormLimit) * metaballWormSegments;
    }
    if (params.motion == 10) {
        const int side = std::clamp(static_cast<int>(std::ceil(std::sqrt(static_cast<double>(asked)) - 1e-9)), 1, 8);
        return side * side;
    }
    return asked;
}

// The balls at `phase` (radians, one cycle per 2 pi): every motion runs whole
// turns per cycle, so the loop closes.
[[nodiscard]] inline std::vector<Metaball> metaballScene(const MetaballParams& params, const double phase) {
    using metaball_detail::hash;
    constexpr double tau = 6.28318530717958647692;
    const int count = metaballSceneCount(params);
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
            // Worms: smooth tubes along a curve given by a formula: the head
            // on a Lissajous path, the body following it, and a wave of
            // bends running from head to tail. The worm's capsules, one
            // group, trace the curve, so the tube has no joints.
            using namespace metaball_detail;
            const int worm = i / metaballWormSegments;
            const int link = i % metaballWormSegments;
            const auto wormHash = [&](const int salt) { return hash(worm, seed + salt); };
            const auto wormTurns = [&](const int salt) {
                return (1.0 + std::floor(3.0 * wormHash(salt))) * (wormHash(salt + 50) < 0.5 ? -1.0 : 1.0);
            };
            const std::array<double, 3> spin{wormTurns(20), wormTurns(22), wormTurns(24)};
            const std::array<double, 3> shift{tau * wormHash(21), tau * wormHash(23), tau * wormHash(25)};
            const std::array<double, 3> reach{params.spread, 0.8 * params.spread, params.spread};
            const auto path = [&](const double t) {
                return Vec3d{reach[0] * std::sin(spin[0] * t + shift[0]), reach[1] * std::sin(spin[1] * t + shift[1]),
                             reach[2] * std::sin(spin[2] * t + shift[2])};
            };
            const auto heading = [&](const double t) {
                const Vec3d v{reach[0] * spin[0] * std::cos(spin[0] * t + shift[0]),
                              reach[1] * spin[1] * std::cos(spin[1] * t + shift[1]),
                              reach[2] * spin[2] * std::cos(spin[2] * t + shift[2])};
                const double l = length(v);
                return l > 1e-9 ? Vec3d{v[0] / l, v[1] / l, v[2] / l} : Vec3d{1.0, 0.0, 0.0};
            };
            const double thick = 0.55 * params.radius *
                                 std::max(1.0 + params.variation * (2.0 * wormHash(26) - 1.0), 0.2) *
                                 (1.0 + params.breathe * std::sin(2.0 * phase + tau * wormHash(27)));
            const double speed = std::max(params.spread, 0.05) *
                                 std::sqrt(0.5 * (spin[0] * spin[0] + 0.64 * spin[1] * spin[1] + spin[2] * spin[2]));
            const double behind = 9.0 * thick / speed;
            const double wiggle = 6.0 + std::floor(4.0 * wormHash(28));
            const double up = 2.0 * wormHash(29) - 1.0;
            const Vec3d bend{std::sqrt(1.0 - up * up) * std::cos(tau * wormHash(30)), up,
                             std::sqrt(1.0 - up * up) * std::sin(tau * wormHash(30))};
            const auto body = [&](const double s) {
                const double t = phase - s * behind;
                const Vec3d along = heading(t);
                Vec3d side = cross(along, bend);
                const double l = length(side);
                side = l > 1e-6 ? Vec3d{side[0] / l, side[1] / l, side[2] / l} : across(along);
                const double sway = thick * (0.3 + 1.1 * s) * std::sin(tau * 1.25 * s - wiggle * phase);
                return add(path(t), side, sway);
            };
            const Vec3d front = body(static_cast<double>(link) / metaballWormSegments);
            const Vec3d back = body(static_cast<double>(link + 1) / metaballWormSegments);
            ball.centre = {0.5 * (front[0] + back[0]), 0.5 * (front[1] + back[1]), 0.5 * (front[2] + back[2])};
            ball.extent = {0.5 * (front[0] - back[0]), 0.5 * (front[1] - back[1]), 0.5 * (front[2] - back[2])};
            ball.radius = thick * (1.0 - 0.45 * (link + 0.5) / metaballWormSegments);
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
        // Cubes: rounded, `half` the core and `radius` the rounding.
        const auto shape = [&](Metaball& cube, const double outer) {
            const double round = std::clamp(params.roundness, 0.08, 1.0);
            cube.cube = true;
            cube.radius = outer * round;
            cube.half = outer * (1.0 - round);
        };
        if (params.motion == 8) {
            // Holes: a blob of a few big balls, and balls of negative weight
            // flying through it, carving dents, tunnels and bubbles.
            const int solid = std::clamp(count / 3, 1, 4);
            if (i < solid) {
                ball.radius *= 1.3;
                ball.centre = {0.3 * params.spread * std::sin(turns(10) * phase + offset(11)),
                               0.3 * params.spread * std::sin(turns(12) * phase + offset(13)),
                               0.3 * params.spread * std::sin(turns(14) * phase + offset(15))};
            } else {
                ball.radius *= 0.55;
                ball.weight = -2.0;
                ball.centre = {1.2 * params.spread * std::sin(turns(10) * phase + offset(11)),
                               0.9 * params.spread * std::sin(turns(12) * phase + offset(13)),
                               1.2 * params.spread * std::sin(turns(14) * phase + offset(15))};
            }
            ball.group = i;
            continue;
        }
        if (params.motion == 9) {
            // Peristalsis: a capsule, slowly swaying, and rings sliding up
            // and down it one after another: rings of negative weight pinch
            // a waist into it, the others stand round it as collars webbed
            // to it.
            using namespace metaball_detail;
            const double body = 0.9 * params.radius * (1.0 + params.breathe * std::sin(2.0 * phase));
            const double halfLength = 0.75 + 0.6 * params.spread;
            Vec3d axis{0.3 * std::sin(phase), 1.0, 0.3 * std::sin(2.0 * phase + 1.0)};
            const double l = length(axis);
            axis = {axis[0] / l, axis[1] / l, axis[2] / l};
            if (i == 0) {
                ball.centre = {0.0, 0.0, 0.0};
                ball.extent = {halfLength * axis[0], halfLength * axis[1], halfLength * axis[2]};
                ball.radius = body;
            } else {
                const bool squeeze = i % 2 == 1;
                const double along =
                    0.85 * halfLength * std::sin(std::abs(turns(16)) * phase + tau * (i - 1) / std::max(count - 1, 1));
                const double major = (squeeze ? 1.4 : 1.7) * body;
                ball.centre = {along * axis[0], along * axis[1], along * axis[2]};
                ball.extent = {major * axis[0], major * axis[1], major * axis[2]};
                ball.ring = true;
                ball.radius = (squeeze ? 0.5 : 0.3) * body;
                ball.weight = squeeze ? -1.0 : 1.0;
            }
            ball.group = i;
            continue;
        }
        if (params.motion == 10) {
            // Cube carpet: a square of cubes face to face, a wave running
            // across it lifting them and tilting them with its slope; the
            // crests melt into ridges.
            using namespace metaball_detail;
            const int side = static_cast<int>(std::lround(std::sqrt(static_cast<double>(count))));
            const double outer = 2.2 * params.radius / side;
            const double step = 2.0 * outer;
            const double x = step * (i % side - 0.5 * (side - 1));
            const double z = step * (i / side - 0.5 * (side - 1));
            const double direction = tau * hash(0, seed + 60);
            const double waves = 1.0 + std::floor(2.0 * hash(0, seed + 61));
            const double k = tau * waves / (side * step);
            const double beat = 1.0 + std::floor(3.0 * hash(0, seed + 62));
            const double arg = k * (x * std::cos(direction) + z * std::sin(direction)) - beat * phase;
            const double height = 0.35 * params.spread;
            shape(ball, outer);
            ball.centre = {x, height * std::sin(arg), z};
            ball.extent = turnAbout({-std::sin(direction), 0.0, std::cos(direction)},
                                    std::atan(height * k * std::cos(arg)));
            ball.group = i;
            continue;
        }
        if (params.motion == 6 || params.motion == 7) {
            const auto axisOf = [&](const int salt) {
                const double y = 2.0 * hash(i, seed + salt) - 1.0;
                const double around = tau * hash(i, seed + salt + 1);
                const double flat = std::sqrt(std::max(0.0, 1.0 - y * y));
                return Vec3d{flat * std::cos(around), y, flat * std::sin(around)};
            };
            if (params.motion == 6) {
                // Dice: tumbling on Lissajous paths, melting where they meet.
                shape(ball, 0.8 * ball.radius);
                ball.extent = metaball_detail::turnAbout(axisOf(40), turns(42) * phase + offset(43));
                ball.centre = {params.spread * std::sin(turns(10) * phase + offset(11)),
                               0.8 * params.spread * std::sin(turns(12) * phase + offset(13)),
                               params.spread * std::sin(turns(14) * phase + offset(15))};
            } else {
                // Cube block: one block of cubes face to face, parting while
                // each tumbles a whole number of turns, and closing again
                // square as it was.
                const int side = static_cast<int>(std::ceil(std::cbrt(static_cast<double>(count)) - 1e-9));
                const double outer = 0.8 * params.radius * (1.0 + params.breathe * std::sin(2.0 * phase));
                shape(ball, outer);
                const double part = 0.5 - 0.5 * std::cos(phase);
                const double eased = part * part * (3.0 - 2.0 * part);
                const double step = 2.0 * outer + 1.2 * params.spread * eased;
                const std::array<int, 3> at{i % side, (i / side) % side, i / (side * side)};
                for (std::size_t k = 0; k < 3; ++k) {
                    ball.centre[k] = step * (at[k] - 0.5 * (side - 1));
                }
                ball.extent = metaball_detail::turnAbout(axisOf(40), std::abs(turns(42)) * (phase - std::sin(phase)) *
                                                                         (hash(i, seed + 44) < 0.5 ? -1.0 : 1.0));
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
// and middle, six points round a ring, a cube's corners; none for a ball of
// negative weight, which only carves.
[[nodiscard]] inline std::vector<Vec3d> metaballCorePoints(const Metaball& ball) {
    using namespace metaball_detail;
    if (ball.weight <= 0.0) {
        return {};
    }
    if (ball.cube) {
        std::vector<Vec3d> corners;
        for (int k = 0; k < 8; ++k) {
            const Vec3d corner{(k & 1) != 0 ? ball.half : -ball.half, (k & 2) != 0 ? ball.half : -ball.half,
                               (k & 4) != 0 ? ball.half : -ball.half};
            corners.push_back(add(ball.centre, turn(ball.extent, corner, false)));
        }
        return corners;
    }
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
        // The surface lies within the field's reach of a core, so only the
        // core points that could reach past the bound so far are marched
        // from, furthest first, outwards.
        std::vector<std::pair<double, std::pair<Vec3d, double>>> starts;
        for (const Metaball& ball : balls) {
            for (const Vec3d& c : metaballCorePoints(ball)) {
                starts.push_back({metaball_detail::length(c) + params.reach * ball.radius, {c, ball.radius}});
            }
        }
        std::sort(starts.begin(), starts.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [most, start] : starts) {
            if (most <= bound) {
                break;
            }
            const auto& [c, radius] = start;
            for (const Vec3d& d : directions) {
                if (metaball_detail::dot(d, c) < 0.0) {
                    continue;
                }
                const double t = metaballRay(balls, params.reach, c, radius, d);
                if (t > 0.0) {
                    bound = std::max(bound, metaball_detail::length(metaball_detail::add(c, d, t)));
                }
            }
        }
    }
    return bound;
}

// The area of a ball standing alone: a sphere, a capsule, a torus or a
// rounded cube; none for one of negative weight.
[[nodiscard]] inline double metaballArea(const Metaball& ball) {
    constexpr double pi = 3.14159265358979323846;
    if (ball.weight <= 0.0) {
        return 0.0;
    }
    if (ball.cube) {
        return 24.0 * ball.half * ball.half + 12.0 * pi * ball.half * ball.radius +
               4.0 * pi * ball.radius * ball.radius;
    }
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
