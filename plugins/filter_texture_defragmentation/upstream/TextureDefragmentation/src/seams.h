/*******************************************************************************
    Copyright (c) 2021, Andrea Maggiordomo, Paolo Cignoni and Marco Tarini

    This file is part of TextureDefrag, a reference implementation for
    the paper ``Texture Defragmentation for Photo-Reconstructed 3D Models''
    by Andrea Maggiordomo, Paolo Cignoni and Marco Tarini.

    TextureDefrag is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    TextureDefrag is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with TextureDefrag. If not, see <https://www.gnu.org/licenses/>.
*******************************************************************************/

#ifndef SEAMS_H
#define SEAMS_H

#include <vector>
#include <memory>

#include "types.h"
#include "mesh_graph.h"

struct Seam {
    SeamMesh& sm;
    std::vector<int> edges; // the list of seam segment edges
    std::vector<int> endpoints; // the two endpoint vertices

    Seam(SeamMesh& m) : sm{m} {}
};

struct ClusteredSeam {
    SeamMesh& sm;
    std::vector<SeamHandle> seams;

    ClusteredSeam(SeamMesh& m) : sm{m} {}
    std::size_t size() { return seams.size(); }
    SeamHandle at(int i) { return seams.at(i); }
};

ChartPair GetCharts(ClusteredSeamHandle csh, GraphHandle graph, bool *swapped = nullptr);
std::set<int> GetEndpoints(ClusteredSeamHandle csh);

/*!
 * Given a set of seam chains, colors all faces touching its edges with the given color.
 *
 * @param csh: the set of seam chains.
 * @param color: the color to apply.
 */
void ColorizeSeam(ClusteredSeamHandle csh, const vcg::Color4b& color);

/*!
 *
 * Given a seam chain, colors all faces touching its edges with the given color.
 *
 * @param sh: the seam chain whose adjacent faces will be colorized.
 * @param color: the color to apply.
 */
void ColorizeSeam(SeamHandle sh, const vcg::Color4b& color);

double ComputeSeamLength3D(ClusteredSeamHandle csh);
double ComputeSeamLength3D(SeamHandle sh);

// a is a set of ids that logically describe one side of the seam (whose coordinates are inserted in uva)
void ExtractUVCoordinates(ClusteredSeamHandle csh, std::vector<Point2d>& uva, std::vector<Point2d>& uvb, const std::unordered_set<RegionID>& a);

/*!
 * Given a mesh and a parameterization, it constructs a data structure holding all seam edges.
 * For each incriminated edge it stores the charts sharing it, kept in canonical order in respect their ids.
 *
 * Note that the instance to build must be provided as a parameter.
 * @param m: the mesh instance.
 * @param seamMesh: pointer to an empty instance which will be constructed by the process.
 * @param graph: the UV parametrization.
 */
void BuildSeamMesh(Mesh& m, SeamMesh& seamMesh, GraphHandle graph);

/*!
 * Given a set of seams belonging to a mesh parametrization, it generates a vector of seam chains. Each chain is a set
 * of connected seam edges that consistently separate the same pair of charts.
 * @param seamMesh: the set of seams, stored in a mesh-like data structure.
 * @return the vector of seam chains.
 */
std::vector<SeamHandle> GenerateSeams(SeamMesh& seamMesh);

/*!
 *
 * Given a set of seams chains, in clusters them by their associated chart pair. The generated
 * vector will contain in each entry all the chains associated with the same two charts.
 *
 * Each entry in the output is a ClusteredSeam: a group of one or more seam chains that
 * all share the same ordered chart pair (lower ID first).
 *
 * For self-cut seams (chains whose two sides belong to the same chart), we treat them as isolated entries.
 *
 * @param seams: the seam chains to cluster.
 *
 * @return a vector of `ClusteredSeamHandles`, one per distinct chart pair (plus one per
 *         self-cut seam), in the order their chart pair is first encountered.
 */
std::vector<ClusteredSeamHandle> ClusterSeamsByChartId(const std::vector<SeamHandle>& seams);

ClusteredSeamHandle Flatten(const std::vector<ClusteredSeamHandle>& cshVec);

#endif // SEAMS_H
