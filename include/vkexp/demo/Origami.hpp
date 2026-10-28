#pragma once

// Origami (geometry mode 23): a Platonic solid cut along a spanning tree of
// its faces and unfolded on the hinges that are left, into a flat net and on
// through it into the mirror solid on the other side. The CPU poses every face
// each frame; the point shader fills the posed faces. Pure logic (no Vulkan)
// so it can be unit tested.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vkexp {

enum class OrigamiSolid : int {
    Tetrahedron = 0,
    Cube,
    Octahedron,
    Dodecahedron,
    Icosahedron,
};

inline constexpr int origamiSolidCount = 5;
inline constexpr std::array<const char*, origamiSolidCount> origamiSolidNames{
    "Tetrahedron", "Cube", "Octahedron", "Dodecahedron", "Icosahedron"};

// Faces the storage buffer holds, and vec4 per face (origami.glsl): centre and
// fold, normal and depth, colour and hinge side, then up to five corners.
inline constexpr int origamiMaxSides = 5;
inline constexpr std::size_t origamiFaceVec4 = 3 + origamiMaxSides;

using Vec3d = std::array<double, 3>;

namespace origami_detail {

inline Vec3d add(const Vec3d& a, const Vec3d& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
inline Vec3d sub(const Vec3d& a, const Vec3d& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline Vec3d scale(const Vec3d& a, const double s) { return {a[0] * s, a[1] * s, a[2] * s}; }
inline double dot(const Vec3d& a, const Vec3d& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline Vec3d cross(const Vec3d& a, const Vec3d& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline double length(const Vec3d& a) { return std::sqrt(dot(a, a)); }
inline Vec3d normalize(const Vec3d& a) {
    const double l = length(a);
    return l > 0.0 ? scale(a, 1.0 / l) : Vec3d{0.0, 1.0, 0.0};
}

// Rotation by `angle` about the unit `axis` (Rodrigues).
inline Vec3d rotate(const Vec3d& v, const Vec3d& axis, const double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    return add(add(scale(v, c), scale(cross(axis, v), s)), scale(axis, dot(axis, v) * (1.0 - c)));
}

// A rigid motion x -> rows * x + shift.
struct Rigid {
    std::array<Vec3d, 3> rows{Vec3d{1, 0, 0}, Vec3d{0, 1, 0}, Vec3d{0, 0, 1}};
    Vec3d shift{};

    [[nodiscard]] Vec3d linear(const Vec3d& v) const {
        return {dot(rows[0], v), dot(rows[1], v), dot(rows[2], v)};
    }
    [[nodiscard]] Vec3d apply(const Vec3d& v) const { return add(linear(v), shift); }
};

// The turn by `angle` about the line through `point` along the unit `axis`.
inline Rigid hingeTurn(const Vec3d& point, const Vec3d& axis, const double angle) {
    Rigid turn;
    for (int i = 0; i < 3; ++i) {
        Vec3d unit{};
        unit[static_cast<std::size_t>(i)] = 1.0;
        const Vec3d column = rotate(unit, axis, angle);
        for (int j = 0; j < 3; ++j) {
            turn.rows[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] =
                column[static_cast<std::size_t>(j)];
        }
    }
    turn.shift = sub(point, turn.linear(point));
    return turn;
}

// first after second.
inline Rigid compose(const Rigid& first, const Rigid& second) {
    Rigid out;
    for (int j = 0; j < 3; ++j) {
        for (int i = 0; i < 3; ++i) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) {
                sum += first.rows[static_cast<std::size_t>(j)][static_cast<std::size_t>(k)] *
                       second.rows[static_cast<std::size_t>(k)][static_cast<std::size_t>(i)];
            }
            out.rows[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] = sum;
        }
    }
    out.shift = first.apply(second.shift);
    return out;
}

inline std::vector<Vec3d> solidVertices(const int solid) {
    const double phi = (1.0 + std::sqrt(5.0)) / 2.0;
    std::vector<Vec3d> v;
    const auto cyclic = [&](const double a, const double b) {
        for (const double sa : {-1.0, 1.0}) {
            for (const double sb : {-1.0, 1.0}) {
                v.push_back({0.0, sa * a, sb * b});
                v.push_back({sa * a, sb * b, 0.0});
                v.push_back({sb * b, 0.0, sa * a});
            }
        }
    };
    switch (static_cast<OrigamiSolid>(solid)) {
    case OrigamiSolid::Tetrahedron:
        v = {{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}};
        break;
    case OrigamiSolid::Cube:
        for (int i = 0; i < 8; ++i) {
            v.push_back({(i & 1) ? 1.0 : -1.0, (i & 2) ? 1.0 : -1.0, (i & 4) ? 1.0 : -1.0});
        }
        break;
    case OrigamiSolid::Octahedron:
        v = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        break;
    case OrigamiSolid::Dodecahedron:
        for (int i = 0; i < 8; ++i) {
            v.push_back({(i & 1) ? 1.0 : -1.0, (i & 2) ? 1.0 : -1.0, (i & 4) ? 1.0 : -1.0});
        }
        cyclic(1.0 / phi, phi);
        break;
    case OrigamiSolid::Icosahedron:
        cyclic(1.0, phi);
        break;
    }
    for (auto& p : v) p = normalize(p);
    return v;
}

// Faces of the hull of points on a sphere: the planes with every point on one
// side, corners counter-clockwise seen from outside.
inline std::vector<std::vector<int>> hullFaces(const std::vector<Vec3d>& v) {
    constexpr double eps = 1e-7;
    std::vector<Vec3d> normals;
    std::vector<std::vector<int>> faces;
    const auto n = static_cast<int>(v.size());
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            for (int k = j + 1; k < n; ++k) {
                const auto& a = v[static_cast<std::size_t>(i)];
                Vec3d normal = cross(sub(v[static_cast<std::size_t>(j)], a),
                                     sub(v[static_cast<std::size_t>(k)], a));
                if (length(normal) < eps) continue;
                normal = normalize(normal);
                if (dot(normal, a) < 0.0) normal = scale(normal, -1.0);
                const double offset = dot(normal, a);
                bool outside = false;
                for (const auto& p : v) outside = outside || dot(normal, p) > offset + eps;
                if (outside) continue;
                bool known = false;
                for (const auto& m : normals) known = known || dot(m, normal) > 1.0 - 1e-6;
                if (known) continue;
                normals.push_back(normal);
                std::vector<int> face;
                for (int p = 0; p < n; ++p) {
                    if (std::abs(dot(normal, v[static_cast<std::size_t>(p)]) - offset) < 1e-6) {
                        face.push_back(p);
                    }
                }
                Vec3d centre{};
                for (const int p : face) centre = add(centre, v[static_cast<std::size_t>(p)]);
                centre = scale(centre, 1.0 / static_cast<double>(face.size()));
                const Vec3d first = sub(v[static_cast<std::size_t>(face[0])], centre);
                const auto angle = [&](const int p) {
                    const Vec3d w = sub(v[static_cast<std::size_t>(p)], centre);
                    return std::atan2(dot(cross(first, w), normal), dot(first, w));
                };
                std::sort(face.begin(), face.end(),
                          [&](const int a1, const int b1) { return angle(a1) < angle(b1); });
                faces.push_back(face);
            }
        }
    }
    return faces;
}

// Convex polygons in the plane overlap (more than touching).
inline bool overlap2D(const std::vector<std::array<double, 2>>& a,
                      const std::vector<std::array<double, 2>>& b) {
    const auto separated = [](const std::vector<std::array<double, 2>>& p,
                              const std::vector<std::array<double, 2>>& q) {
        for (std::size_t i = 0; i < p.size(); ++i) {
            const auto& s = p[i];
            const auto& e = p[(i + 1) % p.size()];
            const double nx = e[1] - s[1], ny = s[0] - e[0];
            double minP = 1e30, maxP = -1e30, minQ = 1e30, maxQ = -1e30;
            for (const auto& r : p) {
                const double d = nx * r[0] + ny * r[1];
                minP = std::min(minP, d);
                maxP = std::max(maxP, d);
            }
            for (const auto& r : q) {
                const double d = nx * r[0] + ny * r[1];
                minQ = std::min(minQ, d);
                maxQ = std::max(maxQ, d);
            }
            const double slack = 1e-6 * std::sqrt(nx * nx + ny * ny);
            if (maxP <= minQ + slack || maxQ <= minP + slack) return true;
        }
        return false;
    };
    return !separated(a, b) && !separated(b, a);
}

} // namespace origami_detail

// A solid with its cut tree. Face 0 is the root: it lies at the bottom, its
// outward normal straight down, and never moves.
struct OrigamiNet {
    int sides{4};
    std::vector<std::vector<Vec3d>> corners; // folded, counter-clockwise from outside
    std::vector<Vec3d> normals;              // outward, folded
    std::vector<int> parent;                 // -1 for the root
    std::vector<int> depth;                  // hinges between a face and the root
    std::vector<int> hingeCorner;            // the face's hinge runs from this corner to the next
    std::vector<Vec3d> hingePoint;           // a point of the hinge line, folded
    std::vector<Vec3d> hingeAxis;            // its direction: turning by unfoldAngle flattens
    std::vector<double> unfoldAngle;         // the outward normals' angle
    int maxDepth{0};
};

// Flat (fold 0) every face lies in the root's plane; inside every hinge turns
// back by (1 - fold) of its angle, so at fold -1 the solid stands mirrored
// below that plane.
struct OrigamiPose {
    std::vector<std::vector<Vec3d>> corners;
    std::vector<Vec3d> centres;
    std::vector<Vec3d> normals;
};

[[nodiscard]] inline OrigamiPose origamiPose(const OrigamiNet& net, const std::vector<double>& folds) {
    using namespace origami_detail;
    const std::size_t count = net.corners.size();
    std::vector<Rigid> motion(count);
    OrigamiPose pose;
    pose.corners.resize(count);
    pose.centres.resize(count);
    pose.normals.resize(count);
    // Parents come before their children.
    for (std::size_t f = 0; f < count; ++f) {
        if (net.parent[f] >= 0) {
            const double fold = f < folds.size() ? folds[f] : 1.0;
            motion[f] = compose(motion[static_cast<std::size_t>(net.parent[f])],
                                hingeTurn(net.hingePoint[f], net.hingeAxis[f],
                                          (1.0 - fold) * net.unfoldAngle[f]));
        }
        Vec3d centre{};
        for (const auto& c : net.corners[f]) {
            const Vec3d posed = motion[f].apply(c);
            pose.corners[f].push_back(posed);
            centre = add(centre, posed);
        }
        pose.centres[f] = scale(centre, 1.0 / static_cast<double>(net.corners[f].size()));
        pose.normals[f] = motion[f].linear(net.normals[f]);
    }
    return pose;
}

// The flat net's faces overlap somewhere.
[[nodiscard]] inline bool origamiNetOverlaps(const OrigamiNet& net) {
    using namespace origami_detail;
    const OrigamiPose flat = origamiPose(net, std::vector<double>(net.corners.size(), 0.0));
    std::vector<std::vector<std::array<double, 2>>> polygons;
    for (std::size_t f = 0; f < flat.corners.size(); ++f) {
        std::vector<std::array<double, 2>> polygon;
        for (const auto& c : flat.corners[f]) polygon.push_back({c[0], c[2]});
        polygons.push_back(polygon);
    }
    for (std::size_t a = 0; a < polygons.size(); ++a) {
        for (std::size_t b = a + 1; b < polygons.size(); ++b) {
            if (overlap2D(polygons[a], polygons[b])) return true;
        }
    }
    return false;
}

namespace origami_detail {

// Grows a spanning tree of the faces from the root: seed 0 is breadth first,
// others pick the next hinge at random.
inline OrigamiNet growNet(const std::vector<std::vector<Vec3d>>& faces, const std::vector<Vec3d>& normals,
                          const std::uint32_t seed) {
    const std::size_t count = faces.size();
    const auto shared = [&](const std::size_t a, const std::size_t b, int& corner) {
        const auto& p = faces[a];
        const auto& q = faces[b];
        for (std::size_t i = 0; i < p.size(); ++i) {
            const Vec3d& s = p[i];
            const Vec3d& e = p[(i + 1) % p.size()];
            for (std::size_t j = 0; j < q.size(); ++j) {
                if (length(sub(q[j], e)) < 1e-6 && length(sub(q[(j + 1) % q.size()], s)) < 1e-6) {
                    corner = static_cast<int>(j);
                    return true;
                }
            }
        }
        return false;
    };
    OrigamiNet net;
    net.sides = static_cast<int>(faces[0].size());
    std::vector<int> order{0};
    std::vector<int> parent(count, -2), corner(count, 0);
    parent[0] = -1;
    std::uint32_t state = seed * 747796405U + 2891336453U;
    const auto random = [&] {
        state = state * 1664525U + 1013904223U;
        return state >> 8;
    };
    while (order.size() < count) {
        std::vector<std::array<int, 3>> frontier; // tree face, new face, corner
        for (const int t : order) {
            for (std::size_t f = 0; f < count; ++f) {
                int c = 0;
                if (parent[f] == -2 && shared(static_cast<std::size_t>(t), f, c)) {
                    frontier.push_back({t, static_cast<int>(f), c});
                }
            }
        }
        const auto& pick = frontier[seed == 0 ? 0 : random() % frontier.size()];
        parent[static_cast<std::size_t>(pick[1])] = pick[0];
        corner[static_cast<std::size_t>(pick[1])] = pick[2];
        order.push_back(pick[1]);
    }
    // Stored in growing order, so parents come first.
    std::vector<int> place(count);
    for (std::size_t i = 0; i < count; ++i) place[static_cast<std::size_t>(order[i])] = static_cast<int>(i);
    for (const int f : order) {
        const auto face = static_cast<std::size_t>(f);
        net.corners.push_back(faces[face]);
        net.normals.push_back(normals[face]);
        const int up = parent[face];
        net.parent.push_back(up < 0 ? -1 : place[static_cast<std::size_t>(up)]);
        net.depth.push_back(up < 0 ? 0 : net.depth[static_cast<std::size_t>(place[static_cast<std::size_t>(up)])] + 1);
        net.maxDepth = std::max(net.maxDepth, net.depth.back());
        net.hingeCorner.push_back(up < 0 ? -1 : corner[face]);
        if (up < 0) {
            net.hingePoint.push_back({});
            net.hingeAxis.push_back({1.0, 0.0, 0.0});
            net.unfoldAngle.push_back(0.0);
            continue;
        }
        const auto& s = faces[face][static_cast<std::size_t>(corner[face])];
        const auto& e = faces[face][static_cast<std::size_t>(corner[face] + 1) % faces[face].size()];
        Vec3d axis = normalize(sub(e, s));
        const Vec3d& mine = normals[face];
        const Vec3d& theirs = normals[static_cast<std::size_t>(up)];
        const double angle = std::acos(std::clamp(dot(mine, theirs), -1.0, 1.0));
        // Turned the right way the normal lands on the parent's.
        if (dot(rotate(mine, axis, angle), theirs) < dot(rotate(mine, axis, -angle), theirs)) {
            axis = scale(axis, -1.0);
        }
        net.hingePoint.push_back(s);
        net.hingeAxis.push_back(axis);
        net.unfoldAngle.push_back(angle);
    }
    return net;
}

} // namespace origami_detail

// The solid, circumradius 1, cut along the `variant`-th tree whose flat net
// does not overlap itself (variant 0 grows breadth first when it can).
[[nodiscard]] inline OrigamiNet origamiNet(const int solid, const int variant) {
    using namespace origami_detail;
    const auto vertices = solidVertices(std::clamp(solid, 0, origamiSolidCount - 1));
    const auto indices = hullFaces(vertices);
    // Turn the solid so the first face lies at the bottom, an edge along x.
    std::vector<std::vector<Vec3d>> faces;
    std::vector<Vec3d> normals;
    Vec3d rootNormal{};
    for (const int i : indices[0]) rootNormal = add(rootNormal, vertices[static_cast<std::size_t>(i)]);
    rootNormal = normalize(rootNormal);
    const Vec3d down{0.0, -1.0, 0.0};
    Vec3d axis = cross(rootNormal, down);
    const double tilt = std::acos(std::clamp(dot(rootNormal, down), -1.0, 1.0));
    axis = length(axis) > 1e-9 ? normalize(axis) : Vec3d{1.0, 0.0, 0.0};
    const auto place = [&](const Vec3d& p) { return rotate(p, axis, tilt); };
    const Vec3d edge = sub(place(vertices[static_cast<std::size_t>(indices[0][1])]),
                           place(vertices[static_cast<std::size_t>(indices[0][0])]));
    const double heading = std::atan2(edge[2], edge[0]);
    const auto turn = [&](const Vec3d& p) { return rotate(place(p), {0.0, 1.0, 0.0}, heading); };
    for (const auto& face : indices) {
        std::vector<Vec3d> polygon;
        Vec3d centre{};
        for (const int i : face) {
            polygon.push_back(turn(vertices[static_cast<std::size_t>(i)]));
            centre = add(centre, polygon.back());
        }
        faces.push_back(polygon);
        normals.push_back(normalize(centre));
    }
    int found = -1;
    OrigamiNet best = growNet(faces, normals, 0);
    for (std::uint32_t seed = 0; seed < 20000U; ++seed) {
        OrigamiNet net = growNet(faces, normals, seed);
        if (!origamiNetOverlaps(net) && ++found == std::max(variant, 0)) return net;
        if (found < 0) best = net;
    }
    return best;
}

// Biggest distance of a corner from the centre of the corners' bounding box,
// flat and folded both ways: the scene holds this whatever the fold.
[[nodiscard]] inline double origamiReach(const OrigamiNet& net) {
    double reach = 1.0;
    for (const double fold : {0.0, 0.5, 1.0}) {
        const OrigamiPose pose = origamiPose(net, std::vector<double>(net.corners.size(), fold));
        Vec3d centre{};
        for (const auto& c : pose.centres) centre = origami_detail::add(centre, c);
        centre = origami_detail::scale(centre, 1.0 / static_cast<double>(pose.centres.size()));
        for (const auto& face : pose.corners) {
            for (const auto& c : face) reach = std::max(reach, origami_detail::length(origami_detail::sub(c, centre)));
        }
    }
    return reach;
}

// How folded a hinge `depth` levels out is at flow phase `phase`: cos(phase),
// later the further out (stagger: up to half a turn behind at the rim), eased
// to linger closed and flat. `mirror` folds through to the other side.
[[nodiscard]] inline double origamiFold(const double phase, const int depth, const int maxDepth,
                                        const double stagger, const double holdClosed,
                                        const double holdFlat, const bool mirror) {
    constexpr double pi = 3.14159265358979323846;
    const double lag = maxDepth > 1 ? stagger * pi * static_cast<double>(depth - 1) / (maxDepth - 1) : 0.0;
    const double c = std::cos(phase - lag);
    const double low = std::clamp(holdFlat, 0.0, 0.45), high = 1.0 - std::clamp(holdClosed, 0.0, 0.45);
    double s = std::clamp((std::abs(c) - low) / std::max(high - low, 1e-3), 0.0, 1.0);
    s = s * s * (3.0 - 2.0 * s);
    return mirror && c < 0.0 ? -s : s;
}

} // namespace vkexp
