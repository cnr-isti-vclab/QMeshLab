#pragma once

// The four vcglib UV packers behind one call, so every filter that lays charts out in an
// atlas offers the same choice and the same knobs.

#include <QString>

#include <vector>

// point2/point3 first: similarity2.h and the packers use them without including them.
#include <vcg/space/point2.h>
#include <vcg/space/point3.h>
#include <vcg/math/similarity2.h>

namespace uvpacker {

// One closed polygon per chart, in whatever units the caller works in: the packers
// normalize internally and report the transform that lands each chart in the container.
using Outline = std::vector<vcg::Point2f>;

struct Params
{
    // One of "rasterized_scaled", "rasterized_best_effort", "axis_aligned_rect",
    // "object_oriented_rect" -- the enum values the filter descriptors offer.
    QString algorithm = QStringLiteral("rasterized_scaled");
    int textureSize = 1024;
    int gutterWidth = 4;
    int rotationNum = 4;
    bool permutations = false;
    unsigned int randomSeed = 0;
};

// Packs every outline into a single square container of params.textureSize and returns how
// many were placed. `transforms` is sized to the outlines; `outlineToContainer` holds 0 for
// a placed chart and -1 for one that was left out.
//
// Deliberately single-container, unlike the upstream Pack(): that one grows the container
// and spills into further textures, which makes two algorithms incomparable because they
// end up with different atlas counts. Three of the four scale the layout to fit, so a fixed
// target is the fair comparison; the fourth reports what it could place.
// `outlines` is taken by value because the vcglib packers rewrite the polygons they are
// handed; move into it when the caller has no further use for them.
int packOutlines(std::vector<Outline> outlines,
                 const Params &params,
                 std::vector<vcg::Similarity2f> &transforms,
                 std::vector<int> &outlineToContainer);

// Counter-clockwise convex hull of `points`. Charts whose shape is known to be convex get a
// tight outline out of this without anyone having to walk their boundary.
Outline convexHull(std::vector<vcg::Point2f> points);

} // namespace uvpacker
