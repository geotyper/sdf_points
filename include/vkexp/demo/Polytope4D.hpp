#pragma once

// Edges of 4D polytopes and their compounds, built once on the CPU and read by
// the point shaders from a storage buffer. Pure logic (no Vulkan) so it can be
// unit tested.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <set>
#include <vector>

namespace vkexp {

enum class Polytope4D : int {
    Simplex = 0,         // 5-cell
    Tesseract,           // 8-cell
    Orthoplex,           // 16-cell
    TwentyFourCell,      // 24-cell
    SixHundredCell,      // 600-cell
    HundredTwentyCell,   // 120-cell
    Duoprism,            // p-gon x q-gon
    TesseractOrthoplex,  // tesseract with its dual 16-cell
    ThreeOrthoplexes,    // the three 16-cells hidden in a 24-cell
    NestedTesseracts,    // a tesseract inside a tesseract
    DualTwentyFourCells, // a 24-cell with its dual
};

inline constexpr int polytope4DCount = 11;

using Vec4d = std::array<double, 4>;

struct Edge4D {
    Vec4d start{};
    Vec4d end{};
    int part{};      // which polytope of a compound, or ring family of a duoprism
    int direction{}; // class of edges parallel in 4D
};

namespace polytope_detail {

inline double distanceSquared(const Vec4d& a, const Vec4d& b) {
    double sum = 0.0;
    for (std::size_t i = 0; i < 4; ++i) {
        sum += (a[i] - b[i]) * (a[i] - b[i]);
    }
    return sum;
}

inline double norm(const Vec4d& a) {
    return std::sqrt(distanceSquared(a, Vec4d{}));
}

inline Vec4d scaled(Vec4d a, const double factor) {
    for (auto& value : a) {
        value *= factor;
    }
    return a;
}

// Every sign choice of the non-zero coordinates.
inline void addSigned(std::vector<Vec4d>& out, const Vec4d& base) {
    for (int signs = 0; signs < 16; ++signs) {
        Vec4d value = base;
        bool duplicate = false;
        for (std::size_t i = 0; i < 4; ++i) {
            if ((signs >> i) & 1) {
                if (value[i] == 0.0) {
                    duplicate = true;
                }
                value[i] = -value[i];
            }
        }
        if (!duplicate) {
            out.push_back(value);
        }
    }
}

// All distinct permutations (or only the even ones) of the coordinates, signed.
inline void addPermuted(std::vector<Vec4d>& out, const Vec4d& base, const bool evenOnly) {
    std::array<int, 4> order{0, 1, 2, 3};
    std::set<Vec4d> seen;
    do {
        int inversions = 0;
        for (int i = 0; i < 4; ++i) {
            for (int j = i + 1; j < 4; ++j) {
                inversions += order[i] > order[j] ? 1 : 0;
            }
        }
        if (evenOnly && inversions % 2 != 0) {
            continue;
        }
        Vec4d value{};
        for (std::size_t i = 0; i < 4; ++i) {
            value[i] = base[static_cast<std::size_t>(order[i])];
        }
        if (seen.insert(value).second) {
            addSigned(out, value);
        }
    } while (std::next_permutation(order.begin(), order.end()));
}

// Pairs of vertices at the shortest distance: the edges of a regular polytope.
inline std::vector<std::array<std::size_t, 2>> shortestPairs(const std::vector<Vec4d>& vertices) {
    double shortest = 1e300;
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        for (std::size_t j = i + 1; j < vertices.size(); ++j) {
            const double d = distanceSquared(vertices[i], vertices[j]);
            if (d > 1e-12) {
                shortest = std::min(shortest, d);
            }
        }
    }
    std::vector<std::array<std::size_t, 2>> pairs;
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        for (std::size_t j = i + 1; j < vertices.size(); ++j) {
            if (std::abs(distanceSquared(vertices[i], vertices[j]) - shortest) < 1e-6 * shortest) {
                pairs.push_back({i, j});
            }
        }
    }
    return pairs;
}

inline std::vector<Vec4d> simplexVertices() {
    const double a = 1.0 / std::sqrt(10.0), b = 1.0 / std::sqrt(6.0), c = 1.0 / std::sqrt(3.0);
    return {{a, b, c, 1.0},
            {a, b, c, -1.0},
            {a, b, -2.0 * c, 0.0},
            {a, -3.0 * b, 0.0, 0.0},
            {-4.0 * a, 0.0, 0.0, 0.0}};
}

inline std::vector<Vec4d> tesseractVertices() {
    std::vector<Vec4d> out;
    addSigned(out, {1.0, 1.0, 1.0, 1.0});
    return out;
}

inline std::vector<Vec4d> orthoplexVertices() {
    std::vector<Vec4d> out;
    addPermuted(out, {1.0, 0.0, 0.0, 0.0}, false);
    return out;
}

inline std::vector<Vec4d> twentyFourCellVertices() {
    std::vector<Vec4d> out;
    addPermuted(out, {1.0, 1.0, 0.0, 0.0}, false);
    return out;
}

inline std::vector<Vec4d> sixHundredCellVertices() {
    const double phi = 0.5 * (1.0 + std::sqrt(5.0));
    std::vector<Vec4d> out;
    addPermuted(out, {1.0, 0.0, 0.0, 0.0}, false);
    addSigned(out, {0.5, 0.5, 0.5, 0.5});
    addPermuted(out, {0.5 * phi, 0.5, 0.5 / phi, 0.0}, true);
    return out;
}

// The 120-cell is the 600-cell's dual: a vertex at the centre of each of its
// 600 tetrahedra.
inline std::vector<Vec4d> hundredTwentyCellVertices() {
    const auto vertices = sixHundredCellVertices();
    const auto pairs = shortestPairs(vertices);
    std::vector<std::vector<bool>> adjacent(vertices.size(),
                                            std::vector<bool>(vertices.size(), false));
    for (const auto& pair : pairs) {
        adjacent[pair[0]][pair[1]] = adjacent[pair[1]][pair[0]] = true;
    }
    std::vector<Vec4d> centres;
    const std::size_t n = vertices.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (!adjacent[i][j]) continue;
            for (std::size_t k = j + 1; k < n; ++k) {
                if (!adjacent[i][k] || !adjacent[j][k]) continue;
                for (std::size_t l = k + 1; l < n; ++l) {
                    if (!adjacent[i][l] || !adjacent[j][l] || !adjacent[k][l]) continue;
                    Vec4d centre{};
                    for (std::size_t c = 0; c < 4; ++c) {
                        centre[c] = vertices[i][c] + vertices[j][c] + vertices[k][c] +
                                    vertices[l][c];
                    }
                    centres.push_back(scaled(centre, 1.0 / norm(centre)));
                }
            }
        }
    }
    return centres;
}

inline void addPolytope(std::vector<Edge4D>& edges, const std::vector<Vec4d>& vertices,
                        const int part, const double radius) {
    const double scale = radius / norm(vertices.front());
    for (const auto& pair : shortestPairs(vertices)) {
        edges.push_back({scaled(vertices[pair[0]], scale), scaled(vertices[pair[1]], scale), part, 0});
    }
}

inline void addDuoprism(std::vector<Edge4D>& edges, const int p, const int q) {
    constexpr double tau = 6.28318530717958647692;
    const double r = 1.0 / std::sqrt(2.0);
    const auto vertex = [&](const int i, const int j) {
        const double a = tau * i / p, b = tau * j / q;
        return Vec4d{r * std::cos(a), r * std::sin(a), r * std::cos(b), r * std::sin(b)};
    };
    for (int i = 0; i < p; ++i) {
        for (int j = 0; j < q; ++j) {
            edges.push_back({vertex(i, j), vertex((i + 1) % p, j), 0, 0});
            edges.push_back({vertex(i, j), vertex(i, (j + 1) % q), 1, 0});
        }
    }
}

// Edges parallel in 4D share a class; the classes are numbered as found.
inline void classifyDirections(std::vector<Edge4D>& edges) {
    std::vector<Vec4d> classes;
    for (auto& edge : edges) {
        Vec4d direction{};
        for (std::size_t i = 0; i < 4; ++i) {
            direction[i] = edge.end[i] - edge.start[i];
        }
        direction = scaled(direction, 1.0 / norm(direction));
        for (const double value : direction) {
            if (std::abs(value) > 1e-6) {
                if (value < 0.0) direction = scaled(direction, -1.0);
                break;
            }
        }
        std::size_t found = 0;
        while (found < classes.size() && distanceSquared(classes[found], direction) > 1e-8) {
            ++found;
        }
        if (found == classes.size()) {
            classes.push_back(direction);
        }
        edge.direction = static_cast<int>(found);
    }
}

} // namespace polytope_detail

// Edges of the chosen figure, scaled so the outermost vertices lie on the unit
// 3-sphere. `p` and `q` are the duoprism's polygons.
[[nodiscard]] inline std::vector<Edge4D> polytopeEdges(const Polytope4D shape, const int p = 6,
                                                       const int q = 6) {
    using namespace polytope_detail;
    std::vector<Edge4D> edges;
    switch (shape) {
    case Polytope4D::Simplex:
        addPolytope(edges, simplexVertices(), 0, 1.0);
        break;
    case Polytope4D::Tesseract:
        addPolytope(edges, tesseractVertices(), 0, 1.0);
        break;
    case Polytope4D::Orthoplex:
        addPolytope(edges, orthoplexVertices(), 0, 1.0);
        break;
    case Polytope4D::TwentyFourCell:
        addPolytope(edges, twentyFourCellVertices(), 0, 1.0);
        break;
    case Polytope4D::SixHundredCell:
        addPolytope(edges, sixHundredCellVertices(), 0, 1.0);
        break;
    case Polytope4D::HundredTwentyCell:
        addPolytope(edges, hundredTwentyCellVertices(), 0, 1.0);
        break;
    case Polytope4D::Duoprism:
        addDuoprism(edges, std::clamp(p, 3, 32), std::clamp(q, 3, 32));
        break;
    case Polytope4D::TesseractOrthoplex:
        addPolytope(edges, tesseractVertices(), 0, 1.0);
        addPolytope(edges, orthoplexVertices(), 1, 1.0);
        break;
    case Polytope4D::ThreeOrthoplexes: {
        // Split the 24-cell's vertices by which pair of coordinates is non-zero.
        const auto vertices = twentyFourCellVertices();
        constexpr std::array<std::array<std::array<std::size_t, 2>, 2>, 3> planes{{
            {{{0, 1}, {2, 3}}}, {{{0, 2}, {1, 3}}}, {{{0, 3}, {1, 2}}}}};
        for (std::size_t part = 0; part < planes.size(); ++part) {
            std::vector<Vec4d> own;
            for (const auto& vertex : vertices) {
                for (const auto& plane : planes[part]) {
                    if (vertex[plane[0]] != 0.0 && vertex[plane[1]] != 0.0) {
                        own.push_back(vertex);
                    }
                }
            }
            addPolytope(edges, own, static_cast<int>(part), 1.0);
        }
        break;
    }
    case Polytope4D::NestedTesseracts:
        addPolytope(edges, tesseractVertices(), 0, 1.0);
        addPolytope(edges, tesseractVertices(), 1, 0.5);
        break;
    case Polytope4D::DualTwentyFourCells: {
        addPolytope(edges, twentyFourCellVertices(), 0, 1.0);
        std::vector<Vec4d> dual = orthoplexVertices();
        const auto cube = tesseractVertices();
        dual.insert(dual.end(), cube.begin(), cube.end());
        for (auto& vertex : dual) {
            vertex = scaled(vertex, 1.0 / norm(vertex));
        }
        addPolytope(edges, dual, 1, 1.0);
        break;
    }
    }
    classifyDirections(edges);
    return edges;
}

// Samples along every edge for a total point budget, so big polytopes thin out.
[[nodiscard]] inline int polytopePointsAlong(const int budget, const int edges, const int around) {
    return std::clamp(budget / std::max(edges * around, 1), 6, 400);
}

} // namespace vkexp
