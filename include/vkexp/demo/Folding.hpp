#pragma once

// Folding figures (geometry mode 23): flat pieces moving on hinges or folding
// along creases, posed on the CPU every frame as a set of convex polygons the
// point shader fills (origami.glsl). Pure logic (no Vulkan) so it can be unit
// tested.
//
// Every figure has the same story, driven by the fold of origamiFold(): 1 is
// the closed state, 0 the open one, -1 (with fold through) its mirror.

#include "vkexp/demo/Origami.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <vector>

namespace vkexp {

enum class FoldFigure : int {
    PlatonicNet = 0, // a solid unfolding into its net (Origami.hpp)
    TesseractNet,    // the tesseract unfolding in 4D into Dali's cross
    Jitterbug,       // Fuller's jitterbug: octahedron, icosahedron, cuboctahedron
    Dudeney,         // the hinged dissection of a triangle into a square
    MiuraOri,        // a sheet folding along the Miura pattern
    Flasher,         // a disc wrapped round its hub, unfurling
    RotatingSquares, // kirigami: squares joined at corners, opening into windows
    RollingSheet,    // a strip bending into a cylinder, a cone, a Moebius band
};

inline constexpr int foldFigureCount = 8;
inline constexpr std::array<const char*, foldFigureCount> foldFigureNames{
    "Platonic solid net", "Tesseract into Dali's cross", "Jitterbug",
    "Triangle to square (Dudeney)", "Miura-ori", "Flasher", "Rotating squares (kirigami)",
    "Rolling sheet"};

inline constexpr int rollingShapeCount = 4;
inline constexpr std::array<const char*, rollingShapeCount> rollingShapeNames{
    "Cylinder", "Cone", "Moebius band", "All in turn"};

struct FoldParams {
    int figure{0};
    // Platonic net
    double sphere{0.0};
    // shared timing (origamiFold)
    bool mirror{true};
    double stagger{0.35};
    double holdClosed{0.2};
    double holdFlat{0.12};
    // tesseract
    double fourTilt{0.0};   // fixed XW turn, radians
    double fourTurn{0.0};   // XW turns per cycle
    double fourDistance{4.5};
    // Miura-ori
    int panelsX{14};
    int panelsY{10};
    double panelAngle{1.1}; // sector angle gamma, radians
    double panelAspect{1.0};
    // flasher
    int arms{6};
    int rings{8};
    double wrap{1.0};
    // rotating squares
    int squares{9};
    double dome{0.0};
    // rolling sheet
    int sheetShape{3};
    // Dudeney
    double lift{0.08};
};

// One posed piece: up to origamiMaxSides corners, convex, in order round it.
struct FoldPolygon {
    std::vector<Vec3d> corners;
    int colour{0};      // palette slot
    double share{0.0};  // 0 .. 1 along the near to far colour
    double glow{0.0};   // the whole piece lights up
    int hinge{-1};      // the edge from this corner to the next lights up ...
    double hingeGlow{0.0}; // ... this much
};

struct FoldScene {
    std::vector<FoldPolygon> faces;
    double flat{0.0};  // 1 while the figure lies open
    double swell{0.0}; // Platonic solids: how far the closed faces swell
};

namespace folding_detail {

using origami_detail::add;
using origami_detail::cross;
using origami_detail::dot;
using origami_detail::length;
using origami_detail::normalize;
using origami_detail::rotate;
using origami_detail::scale;
using origami_detail::sub;

constexpr double pi = 3.14159265358979323846;

// Hinges move most half way.
inline double activity(const double fold) {
    const double f = std::abs(fold);
    return 4.0 * f * (1.0 - f);
}

using Vec4 = std::array<double, 4>;

// A 4D rigid motion x -> rows * x + shift.
struct Rigid4 {
    std::array<Vec4, 4> rows{Vec4{1, 0, 0, 0}, Vec4{0, 1, 0, 0}, Vec4{0, 0, 1, 0}, Vec4{0, 0, 0, 1}};
    Vec4 shift{};

    [[nodiscard]] Vec4 linear(const Vec4& v) const {
        Vec4 out{};
        for (std::size_t j = 0; j < 4; ++j) {
            for (std::size_t i = 0; i < 4; ++i) out[j] += rows[j][i] * v[i];
        }
        return out;
    }
    [[nodiscard]] Vec4 apply(const Vec4& v) const {
        Vec4 out = linear(v);
        for (std::size_t i = 0; i < 4; ++i) out[i] += shift[i];
        return out;
    }
};

inline Rigid4 compose4(const Rigid4& first, const Rigid4& second) {
    Rigid4 out;
    for (std::size_t j = 0; j < 4; ++j) {
        for (std::size_t i = 0; i < 4; ++i) {
            double sum = 0.0;
            for (std::size_t k = 0; k < 4; ++k) sum += first.rows[j][k] * second.rows[k][i];
            out.rows[j][i] = sum;
        }
    }
    out.shift = first.apply(second.shift);
    return out;
}

// Turn about the plane through `point` orthogonal to the unit axes u and p:
// u goes towards -p.
inline Rigid4 planeTurn(const Vec4& point, const int u, const double uSign, const int p,
                        const double pSign, const double angle) {
    Rigid4 turn;
    const double c = std::cos(angle), s = std::sin(angle);
    // In the (u', p') basis with u' = uSign e_u, p' = pSign e_p:
    // u' -> c u' - s p', p' -> s u' + c p'.
    const auto U = static_cast<std::size_t>(u), P = static_cast<std::size_t>(p);
    turn.rows[U][U] = c;
    turn.rows[P][P] = c;
    turn.rows[P][U] = -s * uSign * pSign;
    turn.rows[U][P] = s * uSign * pSign;
    const Vec4 moved = turn.linear(point);
    for (std::size_t i = 0; i < 4; ++i) turn.shift[i] = point[i] - moved[i];
    return turn;
}

inline FoldScene platonic(const OrigamiNet& net, const FoldParams& p, const double phase) {
    std::vector<double> folds(net.corners.size(), 1.0);
    double opened = 0.0, closed = 1.0;
    for (std::size_t f = 1; f < folds.size(); ++f) {
        folds[f] = origamiFold(phase, net.depth[f], net.maxDepth, p.stagger, p.holdClosed,
                               p.holdFlat, p.mirror);
        opened += 1.0 - std::abs(folds[f]);
        closed = std::min(closed, std::abs(folds[f]));
    }
    FoldScene scene;
    scene.flat = folds.size() > 1 ? opened / static_cast<double>(folds.size() - 1) : 0.0;
    scene.swell = p.sphere * closed * closed * closed;
    const OrigamiPose pose = origamiPose(net, folds);
    for (std::size_t f = 0; f < pose.corners.size(); ++f) {
        FoldPolygon face;
        face.corners = pose.corners[f];
        face.colour = static_cast<int>(f);
        face.share = static_cast<double>(net.depth[f]) / std::max(net.maxDepth, 1);
        face.hinge = net.hingeCorner[f];
        face.hingeGlow = f == 0 ? 0.0 : activity(folds[f]);
        scene.faces.push_back(face);
    }
    return scene;
}

// The tesseract's eight cubes: the root at w = -1, its six neighbours round
// it, the far cube hung on the +y one: Dali's cross once flat. Each cube
// turns about the square it shares with its parent, in the plane of the two
// directions leaving that square.
inline FoldScene tesseract(const FoldParams& p, const double phase) {
    struct Cell {
        int axis;
        double sign;
        int parent;
    };
    const std::array<Cell, 8> cells{{{3, -1.0, -1},
                                     {0, 1.0, 0},
                                     {0, -1.0, 0},
                                     {1, 1.0, 0},
                                     {1, -1.0, 0},
                                     {2, 1.0, 0},
                                     {2, -1.0, 0},
                                     {3, 1.0, 3}}};
    std::array<Rigid4, 8> motion{};
    std::array<double, 8> folds{};
    FoldScene scene;
    double opened = 0.0;
    for (std::size_t c = 1; c < cells.size(); ++c) {
        const Cell& cell = cells[c];
        const Cell& parent = cells[static_cast<std::size_t>(cell.parent)];
        const int depth = cell.parent == 0 ? 1 : 2;
        folds[c] = origamiFold(phase, depth, 2, p.stagger, p.holdClosed, p.holdFlat, p.mirror);
        opened += 1.0 - std::abs(folds[c]);
        Vec4 hinge{};
        hinge[static_cast<std::size_t>(cell.axis)] = cell.sign;
        hinge[static_cast<std::size_t>(parent.axis)] = parent.sign;
        // Into the child is along the parent's axis, into the parent along the child's.
        motion[c] = compose4(motion[static_cast<std::size_t>(cell.parent)],
                             planeTurn(hinge, parent.axis, -parent.sign, cell.axis, -cell.sign,
                                       0.5 * pi * (1.0 - folds[c])));
    }
    scene.flat = opened / 7.0;
    const double turn = p.fourTilt + p.fourTurn * phase;
    const double distance = std::max(p.fourDistance, 3.2);
    for (std::size_t c = 0; c < cells.size(); ++c) {
        const Cell& cell = cells[c];
        for (int a = 0; a < 4; ++a) {
            if (a == cell.axis) continue;
            int b = -1, d = -1;
            for (int k = 0; k < 4; ++k) {
                if (k == cell.axis || k == a) continue;
                (b < 0 ? b : d) = k;
            }
            for (const double side : {-1.0, 1.0}) {
                FoldPolygon face;
                constexpr std::array<std::array<double, 2>, 4> square{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
                for (const auto& corner : square) {
                    Vec4 q{};
                    q[static_cast<std::size_t>(cell.axis)] = cell.sign;
                    q[static_cast<std::size_t>(a)] = side;
                    q[static_cast<std::size_t>(b)] = corner[0];
                    q[static_cast<std::size_t>(d)] = corner[1];
                    q = motion[c].apply(q);
                    const double x = std::cos(turn) * q[0] - std::sin(turn) * q[3];
                    const double w = std::sin(turn) * q[0] + std::cos(turn) * q[3];
                    // The camera out along -w: the root cube is the near one.
                    const double perspective = (distance - 1.0) / std::max(distance + w, 0.05);
                    face.corners.push_back({perspective * x, perspective * q[1], perspective * q[2]});
                }
                face.colour = static_cast<int>(c);
                face.share = c == 0 ? 0.0 : (cell.parent == 0 ? 0.5 : 1.0);
                const bool hingeSquare =
                    c > 0 && a == cells[static_cast<std::size_t>(cell.parent)].axis &&
                    side == cells[static_cast<std::size_t>(cell.parent)].sign;
                face.glow = hingeSquare ? activity(folds[c]) : 0.0;
                scene.faces.push_back(face);
            }
        }
    }
    return scene;
}

// Fuller's jitterbug: eight triangles on the octant directions, turning about
// them by phi and sliding in so that every corner stays on its coordinate
// plane, shared with the mirror triangle across it. phi 0 is the
// cuboctahedron (the six squares open), +-60 degrees the octahedron, and the
// icosahedron lies between, where the squares' short diagonals reach the
// edge length; those are drawn as the twelve gap triangles.
inline FoldScene jitterbug(const FoldParams& p, const double phase) {
    const double fold = origamiFold(phase, 1, 1, 0.0, p.holdClosed, p.holdFlat, p.mirror);
    const double phi = pi / 3.0 * fold;
    const double a = 1.0 / std::sqrt(2.0);
    const Vec3d n = normalize({1.0, 1.0, 1.0});
    const std::array<Vec3d, 3> start{Vec3d{a, a, 0.0}, Vec3d{a, 0.0, a}, Vec3d{0.0, a, a}};
    const Vec3d centre = scale(add(add(start[0], start[1]), start[2]), 1.0 / 3.0);
    std::array<Vec3d, 3> arm{};
    for (std::size_t k = 0; k < 3; ++k) arm[k] = rotate(sub(start[k], centre), n, phi);
    // The first corner stays on z = 0.
    const double r = -arm[0][2] / n[2];
    std::array<Vec3d, 3> base{};
    for (std::size_t k = 0; k < 3; ++k) base[k] = add(scale(n, r), arm[k]);
    FoldScene scene;
    scene.flat = 1.0 - std::abs(fold);
    // Corner k of octant o, and where it started.
    const auto octant = [](const int o) {
        return Vec3d{(o & 1) ? -1.0 : 1.0, (o & 2) ? -1.0 : 1.0, (o & 4) ? -1.0 : 1.0};
    };
    const auto mirror = [](const Vec3d& v, const Vec3d& s) { return Vec3d{v[0] * s[0], v[1] * s[1], v[2] * s[2]}; };
    for (int o = 0; o < 8; ++o) {
        FoldPolygon face;
        for (const auto& corner : base) face.corners.push_back(mirror(corner, octant(o)));
        face.colour = o;
        const Vec3d s = octant(o);
        face.share = s[0] * s[1] * s[2] > 0.0 ? 0.0 : 1.0;
        scene.faces.push_back(face);
    }
    // The square round each half axis: the corners that began on it.
    for (int axis = 0; axis < 3; ++axis) {
        for (const double side : {1.0, -1.0}) {
            std::vector<std::pair<double, Vec3d>> ring;
            for (int o = 0; o < 8; ++o) {
                for (std::size_t k = 0; k < 3; ++k) {
                    const Vec3d began = mirror(start[k], octant(o));
                    if (std::abs(began[static_cast<std::size_t>(axis)] - side * a) > 1e-9) continue;
                    const Vec3d now = mirror(base[k], octant(o));
                    bool known = false;
                    for (const auto& r2 : ring) known = known || length(sub(r2.second, now)) < 1e-9;
                    if (known) continue;
                    const double u = began[static_cast<std::size_t>((axis + 1) % 3)];
                    const double v = began[static_cast<std::size_t>((axis + 2) % 3)];
                    ring.push_back({std::atan2(v, u), now});
                }
            }
            if (ring.size() != 4) continue;
            std::sort(ring.begin(), ring.end(), [](const auto& l, const auto& r2) { return l.first < r2.first; });
            const bool first = length(sub(ring[0].second, ring[2].second)) <=
                               length(sub(ring[1].second, ring[3].second));
            const int k = first ? 0 : 1;
            const double diagonal = length(sub(ring[static_cast<std::size_t>(k)].second,
                                               ring[static_cast<std::size_t>(k + 2)].second));
            // Lit as they become the icosahedron's faces.
            const double ico = std::exp(-std::pow((diagonal - 1.0) / 0.06, 2.0));
            for (int half = 0; half < 2; ++half) {
                FoldPolygon face;
                face.corners = {ring[static_cast<std::size_t>(k)].second,
                                ring[static_cast<std::size_t>(k + 1 + 2 * half) % 4].second,
                                ring[static_cast<std::size_t>(k + 2)].second};
                face.colour = 8 + axis;
                face.share = 0.5;
                face.glow = ico;
                scene.faces.push_back(face);
            }
        }
    }
    return scene;
}

// Dudeney's haberdasher: an equilateral triangle cut into four pieces hinged
// at three points; turning the hinges half a turn makes the square.
inline FoldScene dudeney(const FoldParams& p, const double phase) {
    using P2 = std::array<double, 2>;
    const double root3 = std::sqrt(3.0);
    const P2 A{0.0, 0.0}, B{1.0, 0.0}, C{0.5, 0.5 * root3};
    const P2 D{0.5, 0.0}, E{0.75, 0.25 * root3};
    const double side = std::sqrt(0.25 * root3);
    // J on AC with EJ the square's side, K half a side further on.
    const double ux = C[0] - A[0], uy = C[1] - A[1];
    const double qa = ux * ux + uy * uy, qb = 2.0 * (ux * (A[0] - E[0]) + uy * (A[1] - E[1]));
    const double qc = (A[0] - E[0]) * (A[0] - E[0]) + (A[1] - E[1]) * (A[1] - E[1]) - side * side;
    const double t = (-qb - std::sqrt(qb * qb - 4.0 * qa * qc)) / (2.0 * qa);
    const P2 J{A[0] + t * ux, A[1] + t * uy}, K{A[0] + (t + 0.5) * ux, A[1] + (t + 0.5) * uy};
    const auto foot = [&](const P2& q) {
        const double dx = E[0] - J[0], dy = E[1] - J[1];
        const double s = ((q[0] - J[0]) * dx + (q[1] - J[1]) * dy) / (dx * dx + dy * dy);
        return P2{J[0] + s * dx, J[1] + s * dy};
    };
    const P2 L = foot(D), M = foot(K);
    // The chain AJLD -D- DBEL -E- MECK -K- JMK, DBEL fixed.
    const std::array<std::vector<P2>, 4> pieces{
        std::vector<P2>{A, J, L, D}, std::vector<P2>{D, B, E, L}, std::vector<P2>{M, E, C, K},
        std::vector<P2>{J, M, K}};
    const double fold = origamiFold(phase, 1, 1, 0.0, p.holdClosed, p.holdFlat, p.mirror);
    const double angle = pi * (1.0 - fold);
    const auto turn = [](const P2& q, const P2& centre, const double a) {
        const double c = std::cos(a), s = std::sin(a);
        const double x = q[0] - centre[0], y = q[1] - centre[1];
        return P2{centre[0] + c * x - s * y, centre[1] + s * x + c * y};
    };
    const P2 Kturned = turn(K, E, -angle);
    const std::array<std::function<P2(const P2&)>, 4> place{
        [&](const P2& q) { return turn(q, D, angle); },
        [](const P2& q) { return q; },
        [&](const P2& q) { return turn(q, E, -angle); },
        [&](const P2& q) { return turn(turn(q, E, -angle), Kturned, angle); }};
    FoldScene scene;
    scene.flat = 1.0 - std::abs(fold);
    const double size = 1.7;
    const P2 middle{0.5, root3 / 6.0};
    for (std::size_t piece = 0; piece < pieces.size(); ++piece) {
        FoldPolygon face;
        // Turning pieces lift off the table so they pass over one another.
        const double height = p.lift * std::sin(0.5 * angle) * (static_cast<double>(piece) - 1.0);
        for (const auto& q : pieces[piece]) {
            const P2 r = place[piece](q);
            face.corners.push_back({size * (r[0] - middle[0]), height, -size * (r[1] - middle[1])});
        }
        face.colour = static_cast<int>(piece);
        face.share = static_cast<double>(piece) / 3.0;
        scene.faces.push_back(face);
    }
    return scene;
}

// Miura-ori (Schenk and Guest): parallelograms with sides a and b at angle
// gamma; folded by theta the zigzag creases stay level, so every vertex is a
// sum of two alternating steps.
inline FoldScene miura(const FoldParams& p, const double phase) {
    const double fold = origamiFold(phase, 1, 1, 0.0, p.holdClosed, p.holdFlat, p.mirror);
    const double theta = (0.5 * pi - 0.12) * std::abs(fold);
    const double gamma = std::clamp(p.panelAngle, 0.35, 1.45);
    const double a = std::clamp(p.panelAspect, 0.3, 3.0), b = 1.0;
    const double tanGamma = std::tan(gamma), sinGamma = std::sin(gamma);
    const double root = std::sqrt(1.0 + std::pow(std::cos(theta) * tanGamma, 2.0));
    const double H = a * std::sin(theta) * sinGamma * (fold < 0.0 ? -1.0 : 1.0);
    const double S = b * std::cos(theta) * tanGamma / root;
    const double Lstep = a * std::sqrt(1.0 - std::pow(std::sin(theta) * sinGamma, 2.0));
    const double V = b / root;
    const int nx = std::clamp(p.panelsX, 2, 40), ny = std::clamp(p.panelsY, 2, 40);
    // Flat it spans nx b sin(gamma) by ny a: scaled to fit 2 x 2.
    const double size = 2.0 / std::max(nx * b * sinGamma, ny * a);
    const auto vertex = [&](const int i, const int j) {
        // x along the zigzags, y across them, z up; z becomes the height.
        const double x = j * S, y = i * Lstep + (j % 2) * V, z = (i % 2) * H;
        return Vec3d{size * x, size * z, size * y};
    };
    FoldScene scene;
    scene.flat = 1.0 - std::abs(fold);
    for (int i = 0; i < ny; ++i) {
        for (int j = 0; j < nx; ++j) {
            FoldPolygon face;
            face.corners = {vertex(i, j), vertex(i, j + 1), vertex(i + 1, j + 1), vertex(i + 1, j)};
            face.colour = (i + j) % 2 + 2 * ((i / 2 + j / 2) % 2);
            face.share = static_cast<double>(i) / (ny - 1);
            face.hinge = 0;
            face.hingeGlow = activity(fold) * 0.5;
            scene.faces.push_back(face);
        }
    }
    return scene;
}

// A flasher, freely drawn: a hub with arms of panels round it; folding pleats
// the rings up and down and winds the arms round the hub in a spiral.
inline FoldScene flasher(const FoldParams& p, const double phase) {
    const double fold = origamiFold(phase, 1, 1, 0.0, p.holdClosed, p.holdFlat, p.mirror);
    const double closed = std::abs(fold), sense = fold < 0.0 ? -1.0 : 1.0;
    const int arms = std::clamp(p.arms, 3, 12), rings = std::clamp(p.rings, 2, 24);
    const int perArm = 4, around = arms * perArm;
    const double hub = 0.22;
    const auto vertex = [&](const int ring, const int spoke) {
        const double along = static_cast<double>(ring) / rings;
        const double radius = hub + (1.0 - hub) * along;
        const double pleat = ring == 0 ? 0.0 : (ring % 2 == 0 ? 1.0 : -1.0);
        const double angle = 2.0 * pi * spoke / around +
                             sense * closed * p.wrap * pi * along +
                             0.3 * closed * pleat * (spoke % 2 == 0 ? 1.0 : -1.0) * 2.0 * pi / around;
        const double drawn = hub + (radius - hub) * (1.0 - 0.72 * closed);
        const double height = closed * 0.16 * pleat * (0.4 + along) + closed * 0.5 * along * along * sense;
        return Vec3d{drawn * std::cos(angle), height, drawn * std::sin(angle)};
    };
    FoldScene scene;
    scene.flat = 1.0 - closed;
    for (int spoke = 0; spoke < around; ++spoke) {
        FoldPolygon centre;
        centre.corners = {Vec3d{0.0, 0.0, 0.0}, vertex(0, spoke), vertex(0, spoke + 1)};
        centre.colour = 5;
        scene.faces.push_back(centre);
        for (int ring = 0; ring < rings; ++ring) {
            FoldPolygon face;
            face.corners = {vertex(ring, spoke), vertex(ring + 1, spoke), vertex(ring + 1, spoke + 1),
                            vertex(ring, spoke + 1)};
            face.colour = spoke / perArm;
            face.share = static_cast<double>(ring) / std::max(rings - 1, 1);
            face.hinge = 1;
            face.hingeGlow = activity(fold) * 0.5;
            scene.faces.push_back(face);
        }
    }
    return scene;
}

// Kirigami of rotating squares: unit squares joined at their corners, turned
// alternately by +-alpha; their centres sit cos(alpha) + sin(alpha) apart.
// Closed they tile the sheet, at 45 degrees the windows are widest, at 90
// they tile it again.
inline FoldScene rotatingSquares(const FoldParams& p, const double phase) {
    const double fold = origamiFold(phase, 1, 1, 0.0, p.holdClosed, p.holdFlat, p.mirror);
    const double alpha = 0.25 * pi * (1.0 - fold);
    const int n = std::clamp(p.squares, 2, 40);
    const double spacing = std::cos(alpha) + std::sin(alpha);
    const double size = 2.0 / (n * std::sqrt(2.0));
    const double open = std::sin(2.0 * alpha);
    const double reach = 0.5 * (n - 1) * std::sqrt(2.0);
    FoldScene scene;
    scene.flat = open;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            const double x = (i - 0.5 * (n - 1)) * spacing, z = (j - 0.5 * (n - 1)) * spacing;
            const double r = std::sqrt(x * x + z * z) / reach;
            const double y = p.dome * open * std::max(1.0 - r * r, 0.0) * reach * 0.6;
            const double turn = ((i + j) % 2 == 0 ? 1.0 : -1.0) * alpha;
            const double c = std::cos(turn), s = std::sin(turn);
            FoldPolygon face;
            constexpr std::array<std::array<double, 2>, 4> square{
                {{-0.5, -0.5}, {0.5, -0.5}, {0.5, 0.5}, {-0.5, 0.5}}};
            for (const auto& q : square) {
                face.corners.push_back({size * (x + c * q[0] - s * q[1]), size * y,
                                        size * (z + s * q[0] + c * q[1])});
            }
            face.colour = (i + j) % 2 + 2 * ((std::abs(i - n / 2) + std::abs(j - n / 2)) % 2);
            face.share = std::min(r, 1.0);
            scene.faces.push_back(face);
        }
    }
    return scene;
}

// sin(k u) / k and (1 - cos(k u)) / k, also as k goes to 0.
inline double bendAlong(const double k, const double u) {
    return std::abs(k) < 1e-6 ? u : std::sin(k * u) / k;
}
inline double bendUp(const double k, const double u) {
    return std::abs(k) < 1e-6 ? 0.5 * k * u * u : (1.0 - std::cos(k * u)) / k;
}

// A strip 2 pi long and 1.3 wide bending without stretching into a cylinder
// or a cone, or (stretching a little) a Moebius band.
inline FoldScene rollingSheet(const FoldParams& p, const double phase) {
    const double fold = origamiFold(phase, 1, 1, 0.0, p.holdClosed, p.holdFlat, p.mirror);
    const double closed = std::abs(fold), sense = fold < 0.0 ? -1.0 : 1.0;
    int shape = std::clamp(p.sheetShape, 0, rollingShapeCount - 1);
    if (shape == 3) {
        // A new shape every time the strip lies flat.
        shape = static_cast<int>(std::floor((phase + 0.5 * pi) / pi)) % 3;
        if (shape < 0) shape += 3;
    }
    const int along = 72, across = 10;
    const double width = 2.0 * pi, height = 1.3;
    const double k = closed * 2.0 * pi / width;
    const double apex = 0.9; // the cone's apex beyond the near edge
    const double spread = std::atan(0.5 * width / apex);
    const double sinBeta = 1.0 - closed * (1.0 - spread / pi);
    const double cosBeta = std::sqrt(std::max(1.0 - sinBeta * sinBeta, 0.0));
    const auto point = [&](const double u, const double v) -> Vec3d {
        if (shape == 0) return {bendAlong(k, u), sense * bendUp(k, u), v};
        if (shape == 1) {
            const double x = u, z = v + 0.5 * height + apex;
            const double rho = std::sqrt(x * x + z * z), psi = std::atan2(x, z) / sinBeta;
            const Vec3d axis{0.0, sense * sinBeta, cosBeta};
            const Vec3d first{0.0, -sense * cosBeta, sinBeta};
            const Vec3d ruling = add(scale(axis, cosBeta), scale(add(scale(first, std::cos(psi)),
                                                                     Vec3d{std::sin(psi), 0.0, 0.0}),
                                                                 sinBeta));
            return add(Vec3d{0.0, 0.0, -0.5 * height - apex}, scale(ruling, rho));
        }
        const double angle = k * u;
        const Vec3d centre{bendAlong(k, u), sense * bendUp(k, u), 0.0};
        const Vec3d radial{std::sin(angle), -sense * std::cos(angle), 0.0};
        const double twist = 0.5 * closed * angle;
        return add(centre, scale(add(scale(Vec3d{0.0, 0.0, 1.0}, std::cos(twist)), scale(radial, std::sin(twist))), v));
    };
    FoldScene scene;
    scene.flat = 1.0 - closed;
    for (int i = 0; i < along; ++i) {
        for (int j = 0; j < across; ++j) {
            const double u0 = width * (static_cast<double>(i) / along - 0.5);
            const double u1 = width * (static_cast<double>(i + 1) / along - 0.5);
            const double v0 = height * (static_cast<double>(j) / across - 0.5);
            const double v1 = height * (static_cast<double>(j + 1) / across - 0.5);
            FoldPolygon face;
            face.corners = {point(u0, v0), point(u1, v0), point(u1, v1), point(u0, v1)};
            for (auto& c : face.corners) c = scale(c, 0.55);
            face.colour = (i * 5) / along;
            face.share = static_cast<double>(i) / (along - 1);
            scene.faces.push_back(face);
        }
    }
    return scene;
}

} // namespace folding_detail

// The figure at flow phase `phase`; `net` is the Platonic solid's cut.
[[nodiscard]] inline FoldScene foldScene(const FoldParams& p, const OrigamiNet& net, const double phase) {
    using namespace folding_detail;
    switch (static_cast<FoldFigure>(p.figure)) {
    case FoldFigure::PlatonicNet: return platonic(net, p, phase);
    case FoldFigure::TesseractNet: return tesseract(p, phase);
    case FoldFigure::Jitterbug: return jitterbug(p, phase);
    case FoldFigure::Dudeney: return dudeney(p, phase);
    case FoldFigure::MiuraOri: return miura(p, phase);
    case FoldFigure::Flasher: return flasher(p, phase);
    case FoldFigure::RotatingSquares: return rotatingSquares(p, phase);
    case FoldFigure::RollingSheet: return rollingSheet(p, phase);
    }
    return platonic(net, p, phase);
}

// Centre of a polygon's corners, and its normal (Newell).
[[nodiscard]] inline Vec3d foldCentre(const FoldPolygon& face) {
    Vec3d centre{};
    for (const auto& c : face.corners) centre = origami_detail::add(centre, c);
    return origami_detail::scale(centre, 1.0 / static_cast<double>(std::max<std::size_t>(face.corners.size(), 1)));
}

[[nodiscard]] inline Vec3d foldNormal(const FoldPolygon& face) {
    Vec3d n{};
    const std::size_t count = face.corners.size();
    for (std::size_t i = 0; i < count; ++i) {
        const Vec3d& a = face.corners[i];
        const Vec3d& b = face.corners[(i + 1) % count];
        n[0] += (a[1] - b[1]) * (a[2] + b[2]);
        n[1] += (a[2] - b[2]) * (a[0] + b[0]);
        n[2] += (a[0] - b[0]) * (a[1] + b[1]);
    }
    return origami_detail::normalize(n);
}

[[nodiscard]] inline double foldArea(const FoldPolygon& face) {
    Vec3d n{};
    const std::size_t count = face.corners.size();
    for (std::size_t i = 0; i < count; ++i) {
        n = origami_detail::add(n, origami_detail::cross(face.corners[i], face.corners[(i + 1) % count]));
    }
    return 0.5 * origami_detail::length(n);
}

// The mean of the faces' centres, about which the scene is held.
[[nodiscard]] inline Vec3d foldMiddle(const FoldScene& scene) {
    Vec3d middle{};
    for (const auto& face : scene.faces) middle = origami_detail::add(middle, foldCentre(face));
    return origami_detail::scale(middle, 1.0 / static_cast<double>(std::max<std::size_t>(scene.faces.size(), 1)));
}

// Over a few cycles: the largest reach from the middle (so the camera holds
// still), and every face's largest area (so the points never thin out).
struct FoldExtent {
    double reach{0.1};
    std::vector<double> areas;
};

[[nodiscard]] inline FoldExtent foldExtent(const FoldParams& p, const OrigamiNet& net) {
    FoldExtent extent;
    constexpr int samples = 48;
    for (int i = 0; i < samples; ++i) {
        const double phase = 6.0 * folding_detail::pi * i / samples;
        const FoldScene scene = foldScene(p, net, phase);
        const Vec3d middle = foldMiddle(scene);
        extent.areas.resize(scene.faces.size(), 0.0);
        for (std::size_t f = 0; f < scene.faces.size(); ++f) {
            extent.areas[f] = std::max(extent.areas[f], foldArea(scene.faces[f]));
            for (const auto& c : scene.faces[f].corners) {
                extent.reach = std::max(extent.reach, origami_detail::length(origami_detail::sub(c, middle)));
            }
        }
    }
    return extent;
}

} // namespace vkexp
