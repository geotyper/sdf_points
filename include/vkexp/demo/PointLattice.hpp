#pragma once

// Point lattices that close on themselves. A golden-angle spiral spreads points
// evenly, but on a surface whose material coordinate is periodic its last turn
// does not meet the first, which leaves a seam of mismatched points. Stepping
// the angle by a whole number of the lattice's own steps instead brings point
// `count` exactly onto point 0, so the spiral closes without a seam.
// Pure logic (no Vulkan) so it can be unit tested.

#include <cmath>
#include <limits>

namespace vkexp {

// Point i sits at longitude 2*pi * ((i * step) mod count) / count. The step is
// coprime to the count and close to the golden ratio of it, chosen so that the
// continued fraction of step / count has the smallest possible terms: that is
// what keeps the lattice even at every scale and stretch of the surface.
[[nodiscard]] inline int closedLatticeStep(const int count) {
    constexpr double golden = 0.6180339887498949;
    if (count < 8) {
        return 1;
    }
    int best = 1;
    int bestTerm = std::numeric_limits<int>::max();
    double bestOffset = 1.0;
    const int first = static_cast<int>(std::floor(0.55 * count));
    const int last = static_cast<int>(std::ceil(0.70 * count));
    for (int step = first; step <= last; ++step) {
        int larger = count;
        int smaller = step;
        int largestTerm = 0;
        while (smaller != 0) {
            largestTerm = largestTerm > larger / smaller ? largestTerm : larger / smaller;
            const int remainder = larger % smaller;
            larger = smaller;
            smaller = remainder;
        }
        if (larger != 1) {
            continue; // shares a factor with the count: points would line up
        }
        const double offset = std::abs(static_cast<double>(step) / count - golden);
        if (largestTerm < bestTerm || (largestTerm == bestTerm && offset < bestOffset)) {
            best = step;
            bestTerm = largestTerm;
            bestOffset = offset;
        }
    }
    return best;
}

} // namespace vkexp
