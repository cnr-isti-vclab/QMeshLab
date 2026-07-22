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

#ifndef TEXTURE_OPTIMIZATION_H
#define TEXTURE_OPTIMIZATION_H

#include "mesh.h"

#include <utility>
#include <vcg/space/point2.h>


struct Point2iHasher {
    std::size_t operator()(const vcg::Point2i& p) const noexcept
    {
        std::size_t seed = 0;
        seed ^= std::hash<int>()(p[0]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= std::hash<int>()(p[1]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

/*!
 * Given a UV layout represented as a graph, it guarantees that all charts within are oriented consistently (i.e., their
 * coordinate axis start from the same origin point).
 *
 * @param graph: the UV layout instance.
 */
void ReorientCharts(GraphHandle graph);


/* Given a chart, compute both the set of 2D orientation of each initial component
 * and its 3D area. Then rotate the chart according to the largest surface area contribution
 * of the initial components that have been clustered in the chart.
 * Returns the index of an anchor face, i.e. a face that does not belong to the
 * change set and is inside the largest initial component that induced the rotation
 */

/*!
 * Consider a chart that has been obtained by a merge operation, at its core it fuses two smaller charts by removing
 * seams and optimizes area near a deleted cut (to decrease the distortion introduced);
 *
 * During this process, it could happen that a portion of the obtained chart has remained unchanged, albeit some displacement
 * caused by the operations done on the rest of the chart. Resampling this fixed area will wast time and introduce noise.
 *
 * This algorithm aims to individualize the greatest fixed area within a merged chart and align it to the original
 * texel grid to avoid resampling it.
 *
 * @param chart : the optimized UV-island.
 * @param changeSet : a set containing all faces that have been displaced by the defragmentation process.
 * @param flippedInput : a map data structure specifying for each region if its coordinates are inverted.
 * @param colorize : if set to true, the process will color the selected region that has been aligned.
 * @param zeroResamplingArea : final size of the aligned fixed region.
 * @return the face representative of the found fixed area. The face representative is the index of
 * a face within the unchanged area, which identifies it. As a side effect, it also stores the extension of the found
 * fixed area in the parameter `fixedArea`.
 */
int RotateChartForResampling(ChartHandle chart, const std::set<Mesh::FacePointer> &changeSet, const std::map<RegionID, bool>& flippedInput, bool colorize, double *zeroResamplingArea);

/* Texture trimming to remove unused space */
void TrimTexture(Mesh& m, std::vector<TextureSize>& texszVec, bool unsafeMip);

#endif // TEXTURE_OPTIMIZATION_H
