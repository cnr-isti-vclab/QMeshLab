#include "uvpacker.h"

#include <algorithm>

#include <vcg/space/outline2_packer.h>
#include <vcg/space/rasterized_outline2_packer.h>
#include <wrap/qt/outline2_rasterizer.h>

namespace uvpacker {

int packOutlines(std::vector<Outline> outlines,
                 const Params &params,
                 std::vector<vcg::Similarity2f> &transforms,
                 std::vector<int> &outlineToContainer)
{
    typedef vcg::RasterizedOutline2Packer<float, QtOutline2Rasterizer> RasterPacker;

    const std::vector<vcg::Point2i> containers{
        vcg::Point2i(params.textureSize, params.textureSize)};
    transforms.clear();
    outlineToContainer.assign(outlines.size(), -1);
    if (outlines.empty())
        return 0;

    int placed = 0;
    if (params.algorithm == QLatin1String("rasterized_scaled")
        || params.algorithm == QLatin1String("rasterized_best_effort")) {
        RasterPacker::Parameters par;
        par.costFunction = RasterPacker::Parameters::LowestHorizon;
        par.doubleHorizon = false;
        par.innerHorizon = true;
        // Upstream hard-codes this as (chartCount < 50). It is the single biggest cost in
        // the run -- it multiplies the packing work by five times the number of similarly
        // sized charts -- so here it is the user's call.
        par.permutations = params.permutations;
        // The packer rasterizes rotationNum/4 base orientations and derives four slots
        // from each, so anything that is not a multiple of four leaves slots unwritten.
        // Round to the nearest usable count rather than handing it a value it cannot use.
        par.rotationNum = std::max(4, ((params.rotationNum + 2) / 4) * 4);
        par.gutterWidth = params.gutterWidth;
        par.minmax = false;
        par.randomSeed = params.randomSeed;

        if (params.algorithm == QLatin1String("rasterized_scaled")) {
            if (RasterPacker::Pack(outlines, containers, transforms, outlineToContainer, par)) {
                placed = int(outlines.size());
                std::fill(outlineToContainer.begin(), outlineToContainer.end(), 0);
            }
        } else {
            placed = RasterPacker::PackBestEffort(outlines, containers, transforms,
                                                  outlineToContainer, par);
        }
    } else {
        typedef vcg::PolyPacker<float> RectPacker;
        vcg::Point2f covered;
        const bool ok = (params.algorithm == QLatin1String("axis_aligned_rect"))
            ? RectPacker::PackAsAxisAlignedRect(outlines, containers[0], transforms, covered)
            : RectPacker::PackAsObjectOrientedRect(outlines, containers[0], transforms,
                                                   covered, float(params.gutterWidth));
        if (ok) {
            placed = int(outlines.size());
            std::fill(outlineToContainer.begin(), outlineToContainer.end(), 0);
        }
    }
    return placed;
}

Outline convexHull(std::vector<vcg::Point2f> points)
{
    // Andrew's monotone chain. Fewer than three points cannot bound an area, but the
    // packers still want a polygon, so they come back as they are.
    if (points.size() < 3)
        return points;

    std::sort(points.begin(), points.end(), [](const vcg::Point2f &a, const vcg::Point2f &b) {
        return (a.X() < b.X()) || (a.X() == b.X() && a.Y() < b.Y());
    });
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if (points.size() < 3)
        return points;

    const auto cross = [](const vcg::Point2f &o, const vcg::Point2f &a, const vcg::Point2f &b) {
        return double(a.X() - o.X()) * double(b.Y() - o.Y())
             - double(a.Y() - o.Y()) * double(b.X() - o.X());
    };

    Outline hull(2 * points.size());
    std::size_t k = 0;
    for (const vcg::Point2f &p : points) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], p) <= 0.0)
            --k;
        hull[k++] = p;
    }
    const std::size_t lower = k + 1;
    for (std::size_t i = points.size() - 1; i-- > 0;) {
        while (k >= lower && cross(hull[k - 2], hull[k - 1], points[i]) <= 0.0)
            --k;
        hull[k++] = points[i];
    }
    hull.resize(k - 1);
    return hull;
}

} // namespace uvpacker
