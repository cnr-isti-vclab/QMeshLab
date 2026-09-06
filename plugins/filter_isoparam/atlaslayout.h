#pragma once

// Atlas layout for the isoparametrization: turns the abstract domain into charts and packs
// them into a single UV square. Included from isoparamfilterplugin.cpp *after* the vendored
// headers, because it builds directly on IsoParametrization and DiamondParametrizator.

#include "uvpacker.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

#include <vcg/complex/append.h>

namespace atlaslayout {

enum class ChartShape {
    Square,   // each diamond flattened onto the unit square, as the reference code does
    Rhombus,  // each diamond as the 60 degree rhombus it actually is
    Hexagon,  // the six diamonds around a regular domain vertex unfolded as one chart
};

struct Params
{
    ChartShape shape = ChartShape::Square;
    // Hexagon charts normally merge only around regular (valence-six) domain vertices,
    // whose six triangles unfold into a regular hexagon of equilateral triangles. With this
    // on, valence five and seven are merged too, into a pentagon and a heptagon.
    bool mergeIrregularStars = false;
    float border = 0.1f;
    uvpacker::Params packing;
};

struct Stats
{
    int starCharts = 0;
    int irregularStarCharts = 0;
    int mergedDiamonds = 0;
    int diamondCharts = 0;
    int unplacedCharts = 0;
    double coverage = 0.0; // fraction of the UV square the charts actually cover
};

namespace detail {

// One chart: either the six diamonds around a valence-six domain vertex, unfolded together
// through that vertex's star map, or a single diamond unfolded through the quad map.
struct Chart
{
    int star = -1;    // abstract vertex index, or -1 for a plain diamond chart
    int valence = 0;  // triangles around that vertex: six is the regular, hexagonal case
    std::vector<int> diamonds;
};

// GE1Quad flattens the diamond domain -- a 60 degree rhombus -- onto the unit square. This
// puts it back, so the two halves come out as equilateral triangles of side one, the same
// size as the triangles of a star chart.
inline vcg::Point2f shearToRhombus(const vcg::Point2f &quad)
{
    const float kHeight = 0.86602540378f; // sqrt(3)/2
    return vcg::Point2f(quad.X() + 0.5f * quad.Y(), kHeight * quad.Y());
}

// The abstract faces around a domain vertex: the faces of its star.
inline std::vector<int> starFacesOf(AbstractMesh &abstractMesh, int center)
{
    std::vector<int> faces;
    for (std::size_t i = 0; i < abstractMesh.face.size(); ++i) {
        AbstractFace &f = abstractMesh.face[i];
        if (f.IsD())
            continue;
        for (int k = 0; k < 3; ++k)
            if (int(vcg::tri::Index(abstractMesh, f.V(k))) == center) {
                faces.push_back(int(i));
                break;
            }
    }
    return faces;
}

// A param vertex on the boundary of its diamond may name an abstract face outside the star:
// a diamond has four outer edges and only two of them lead back in, and a vertex sitting on
// a domain vertex can be named by any face around it. Renaming the point into a face of the
// star moves it onto the boundary it strayed over -- by at most the overshoot the split
// already allows, and by the same amount for every face that shares the vertex, so the chart
// stays watertight. Whether it also stays fold-free is checked by the caller.
inline bool remapIntoStar(AbstractMesh &abstractMesh,
                          IsoParametrization &iso,
                          int star,
                          const std::vector<int> &starFaces,
                          float tolerance,
                          int &I,
                          vcg::Point2f &UV)
{
    vcg::Point2f resolved;
    if (iso.GE0(I, UV, star, resolved))
        return true;

    // The domain vertices that actually carry the point, with their weights.
    const float bary[3] = {UV.X(), UV.Y(), 1.0f - UV.X() - UV.Y()};
    int carrier[3] = {-1, -1, -1};
    float weight[3] = {0.0f, 0.0f, 0.0f};
    int carriers = 0;
    float total = 0.0f;
    for (int k = 0; k < 3; ++k) {
        if (bary[k] <= tolerance)
            continue;
        carrier[carriers] = int(vcg::tri::Index(abstractMesh, abstractMesh.face[I].V(k)));
        weight[carriers] = bary[k];
        total += bary[k];
        ++carriers;
    }
    if (carriers == 3 || total <= 0.0f)
        return false;

    for (int candidate : starFaces) {
        AbstractFace &f = abstractMesh.face[std::size_t(candidate)];
        float moved[3] = {0.0f, 0.0f, 0.0f};
        int matched = 0;
        for (int c = 0; c < carriers; ++c)
            for (int k = 0; k < 3; ++k)
                if (int(vcg::tri::Index(abstractMesh, f.V(k))) == carrier[c]) {
                    moved[k] = weight[c] / total;
                    ++matched;
                }
        if (matched != carriers)
            continue;
        const vcg::Point2f uv(moved[0], moved[1]);
        if (iso.GE0(candidate, uv, star, resolved)) {
            I = candidate;
            UV = uv;
            return true;
        }
    }
    return false;
}

// The chart's own boundary, as one loop of UVs: the edges used by a single one of its
// faces. Beats the convex hull for the star charts, whose six outer notches belong to
// neighbouring diamonds and are pure waste inside a hull. Returns nothing when the faces do
// not chain into a single loop, and the caller falls back to the hull.
inline std::vector<vcg::Point2f> boundaryLoop(ParamMesh &paramMesh,
                                              const std::vector<int> &faces)
{
    std::map<std::pair<int, int>, int> uses;
    std::map<int, vcg::Point2f> uv;
    for (int fi : faces) {
        ParamFace &f = paramMesh.face[std::size_t(fi)];
        for (int j = 0; j < 3; ++j) {
            const int a = int(vcg::tri::Index(paramMesh, f.V(j)));
            const int b = int(vcg::tri::Index(paramMesh, f.V((j + 1) % 3)));
            uv[a] = f.WT(j).P();
            ++uses[std::pair<int, int>(std::min(a, b), std::max(a, b))];
        }
    }

    std::map<int, int> next;
    for (int fi : faces) {
        ParamFace &f = paramMesh.face[std::size_t(fi)];
        for (int j = 0; j < 3; ++j) {
            const int a = int(vcg::tri::Index(paramMesh, f.V(j)));
            const int b = int(vcg::tri::Index(paramMesh, f.V((j + 1) % 3)));
            if (uses[std::pair<int, int>(std::min(a, b), std::max(a, b))] != 1)
                continue;
            if (!next.insert({a, b}).second)
                return {}; // a vertex with two outgoing boundary edges: pinched, not a disk
        }
    }
    if (next.empty())
        return {};

    std::vector<vcg::Point2f> loop;
    loop.reserve(next.size());
    const int start = next.begin()->first;
    int at = start;
    do {
        loop.push_back(uv[at]);
        const auto step = next.find(at);
        if (step == next.end())
            return {};
        at = step->second;
    } while (at != start && loop.size() <= next.size());
    if (at != start || loop.size() != next.size())
        return {}; // more than one loop: the chart has a hole or a detached piece

    // Counter-clockwise, to match what convexHull hands the packers.
    double twiceArea = 0.0;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const vcg::Point2f &p = loop[i];
        const vcg::Point2f &q = loop[(i + 1) % loop.size()];
        twiceArea += double(p.X()) * double(q.Y()) - double(q.X()) * double(p.Y());
    }
    if (twiceArea < 0.0)
        std::reverse(loop.begin(), loop.end());
    return loop;
}

// Merges the diamonds around every eligible domain vertex it can into one star chart. The
// six diamonds of a valence-six vertex tile a hexagon, so that is the shape when all six are
// available -- but a face is only allowed to stray out of its diamond by so much before it
// is split, and a face that strays out of the *star* cannot be unfolded with it, so a
// diamond whose faces overshoot stays behind and the chart comes back a diamond short.
//
// Two adjacent regular vertices share a diamond and cannot both have it, so the stars that
// stand to gain most are served first: a greedy maximal independent set, weighted.
inline void claimStarCharts(IsoParametrization &iso,
                            ParamMesh &paramMesh,
                            float border,
                            int minValence,
                            int maxValence,
                            const std::map<int, std::vector<int>> &facesOfDiamond,
                            std::vector<Chart> &charts,
                            std::map<int, int> &chartOfDiamond,
                            Stats &stats)
{
    AbstractMesh &abstractMesh = *iso.AbsMesh();
    const std::size_t vertexCount = abstractMesh.vert.size();
    std::vector<std::set<int>> spokes(vertexCount);
    std::vector<int> incidentFaces(vertexCount, 0);

    for (std::size_t fi = 0; fi < abstractMesh.face.size(); ++fi) {
        AbstractFace &f = abstractMesh.face[fi];
        if (f.IsD())
            continue;
        for (int k = 0; k < 3; ++k) {
            ++incidentFaces[vcg::tri::Index(abstractMesh, f.V(k))];
            if (f.FFp(k) == &f) // a border edge has no diamond
                continue;
            // A point solidly inside the third of this face that getHDiamIndex sends to
            // edge k: the weight sits on the two endpoints of that edge.
            float bary[3] = {0.1f, 0.1f, 0.1f};
            bary[k] = 0.45f;
            bary[(k + 1) % 3] = 0.45f;
            const int diamond = iso.getHDiamIndex(int(fi), vcg::Point2f(bary[0], bary[1]));
            spokes[vcg::tri::Index(abstractMesh, f.V(k))].insert(diamond);
            spokes[vcg::tri::Index(abstractMesh, f.V1(k))].insert(diamond);
        }
    }

    std::vector<Chart> candidates;
    for (std::size_t v = 0; v < vertexCount; ++v) {
        // A closed domain has as many spokes as incident faces; anything else is a border
        // vertex, whose star is not a closed fan and has no diamond on every spoke.
        const int valence = incidentFaces[v];
        if (abstractMesh.vert[v].IsD() || valence < minValence || valence > maxValence
            || int(spokes[v].size()) != valence)
            continue;

        const std::vector<int> starFaces = starFacesOf(abstractMesh, int(v));
        // A face is allowed to stray out of its diamond by `border` in quad coordinates,
        // which spans the diamond's two domain triangles, so twice that bounds how far into
        // a neighbouring triangle a stray vertex can be.
        const float tolerance = std::max(1e-3f, 2.0f * border);
        Chart candidate;
        candidate.star = int(v);
        candidate.valence = valence;
        int orientation = 0;
        for (int diamond : spokes[v]) {
            const auto faces = facesOfDiamond.find(diamond);
            if (faces == facesOfDiamond.end())
                continue;
            bool usable = true;
            for (int fi : faces->second) {
                ParamFace &f = paramMesh.face[std::size_t(fi)];
                vcg::Point2f uv[3];
                for (int j = 0; j < 3 && usable; ++j) {
                    int I = f.V(j)->T().N();
                    vcg::Point2f UV = f.V(j)->T().P();
                    usable = remapIntoStar(abstractMesh, iso, int(v), starFaces, tolerance,
                                           I, UV)
                             && iso.GE0(I, UV, int(v), uv[j]);
                }
                if (!usable)
                    break;
                // Clamping a stray vertex onto the star boundary could fold the chart over
                // itself, which would make the parametrization useless to bake against. The
                // sign of the unfolded area says whether it did.
                const vcg::Point2f a = uv[1] - uv[0];
                const vcg::Point2f b = uv[2] - uv[0];
                const float area = a.X() * b.Y() - a.Y() * b.X();
                const int sign = area > 0.0f ? 1 : (area < 0.0f ? -1 : 0);
                if (orientation == 0)
                    orientation = sign;
                usable = sign != 0 && sign == orientation;
                if (!usable)
                    break;
            }
            if (usable)
                candidate.diamonds.push_back(diamond);
        }
        // Merging a single diamond is just a diamond chart with extra steps.
        if (candidate.diamonds.size() >= 2)
            candidates.push_back(std::move(candidate));
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Chart &a, const Chart &b) {
                         return a.diamonds.size() > b.diamonds.size();
                     });

    for (Chart &candidate : candidates) {
        std::vector<int> free;
        for (int diamond : candidate.diamonds)
            if (!chartOfDiamond.count(diamond))
                free.push_back(diamond);
        if (free.size() < 2)
            continue;
        candidate.diamonds = std::move(free);
        const int chart = int(charts.size());
        for (int diamond : candidate.diamonds)
            chartOfDiamond[diamond] = chart;
        stats.mergedDiamonds += int(candidate.diamonds.size());
        ++stats.starCharts;
        if (candidate.valence != 6)
            ++stats.irregularStarCharts;
        charts.push_back(std::move(candidate));
    }
}

} // namespace detail

// Lays the parametrized mesh out as a texture atlas and copies the result into `out`, whose
// wedge texture coordinates the caller has already enabled.
template <class MeshType>
bool build(IsoParametrization &iso, const Params &params, MeshType &out, Stats &stats)
{
    DiamondParametrizator diamond;
    diamond.Init(&iso);
    diamond.PrepareDiamonds(params.border);

    ParamMesh &paramMesh = *iso.ParaMesh();
    if (paramMesh.face.empty())
        return false;

    // PrepareDiamonds leaves every face inside a single diamond, named in WT(0).N().
    std::map<int, std::vector<int>> facesOfDiamond;
    for (std::size_t i = 0; i < paramMesh.face.size(); ++i)
        if (!paramMesh.face[i].IsD())
            facesOfDiamond[paramMesh.face[i].WT(0).N()].push_back(int(i));

    std::vector<detail::Chart> charts;
    std::map<int, int> chartOfDiamond;
    if (params.shape == ChartShape::Hexagon)
        detail::claimStarCharts(iso, paramMesh, params.border,
                                params.mergeIrregularStars ? 5 : 6,
                                params.mergeIrregularStars ? 7 : 6,
                                facesOfDiamond, charts, chartOfDiamond, stats);
    for (const auto &entry : facesOfDiamond) {
        if (chartOfDiamond.count(entry.first))
            continue;
        chartOfDiamond[entry.first] = int(charts.size());
        charts.push_back({-1, 0, {entry.first}});
        ++stats.diamondCharts;
    }

    // Unfold every chart into its own frame and take its outline from the result, so the
    // packer works on the shape the charts actually have rather than the one they would
    // have if every face reached the edge of its diamond.
    const float tolerance = std::max(1e-3f, 2.0f * params.border);
    std::vector<uvpacker::Outline> outlines(charts.size());
    std::vector<int> chartOfFace(paramMesh.face.size(), -1);
    for (std::size_t c = 0; c < charts.size(); ++c) {
        const detail::Chart &chart = charts[c];
        const std::vector<int> starFaces =
            chart.star >= 0 ? detail::starFacesOf(*iso.AbsMesh(), chart.star)
                            : std::vector<int>();
        std::vector<int> faces;
        std::vector<vcg::Point2f> points;
        for (int diamond : chart.diamonds) {
            for (int fi : facesOfDiamond.at(diamond)) {
                ParamFace &f = paramMesh.face[std::size_t(fi)];
                faces.push_back(fi);
                chartOfFace[std::size_t(fi)] = int(c);
                for (int j = 0; j < 3; ++j) {
                    vcg::Point2f uv;
                    if (chart.star >= 0) {
                        int I = f.V(j)->T().N();
                        vcg::Point2f UV = f.V(j)->T().P();
                        detail::remapIntoStar(*iso.AbsMesh(), iso, chart.star, starFaces,
                                              tolerance, I, UV);
                        iso.GE0(I, UV, chart.star, uv);
                    } else {
                        vcg::Point2f uvDiamond;
                        iso.GE1(f.V(j)->T().N(), f.V(j)->T().P(), diamond, uvDiamond);
                        iso.GE1Quad(diamond, uvDiamond, uv);
                        if (params.shape != ChartShape::Square)
                            uv = detail::shearToRhombus(uv);
                    }
                    f.WT(j).P() = uv;
                    points.push_back(uv);
                }
            }
        }
        outlines[c] = detail::boundaryLoop(paramMesh, faces);
        if (outlines[c].size() < 3)
            outlines[c] = uvpacker::convexHull(std::move(points));
    }

    std::vector<vcg::Similarity2f> transforms;
    std::vector<int> chartToContainer;
    const int placed =
        uvpacker::packOutlines(outlines, params.packing, transforms, chartToContainer);
    if (placed <= 0)
        return false;
    stats.unplacedCharts = int(charts.size()) - placed;

    const float scale = 1.0f / float(params.packing.textureSize);
    double covered = 0.0;
    for (std::size_t i = 0; i < paramMesh.face.size(); ++i) {
        ParamFace &f = paramMesh.face[i];
        if (f.IsD())
            continue;
        const int chart = chartOfFace[i];
        const bool ok = chart >= 0 && chart < int(chartToContainer.size())
                        && chartToContainer[std::size_t(chart)] >= 0;
        for (int j = 0; j < 3; ++j) {
            // Charts the packer could not fit collapse to the origin, as it does upstream.
            f.WT(j).P() = ok ? transforms[std::size_t(chart)] * f.WT(j).P() * scale
                             : vcg::Point2f(0.0f, 0.0f);
            // WT.N() carried the diamond index as scratch and would otherwise escape as
            // the wedge's texture id; the whole atlas is texture zero.
            f.WT(j).N() = 0;
        }
        const vcg::Point2f a = f.WT(1).P() - f.WT(0).P();
        const vcg::Point2f b = f.WT(2).P() - f.WT(0).P();
        covered += 0.5 * std::fabs(double(a.X()) * double(b.Y()) - double(a.Y()) * double(b.X()));
    }
    stats.coverage = covered;

    out.Clear();
    vcg::tri::Append<MeshType, ParamMesh>::Mesh(out, paramMesh);
    return true;
}

} // namespace atlaslayout
