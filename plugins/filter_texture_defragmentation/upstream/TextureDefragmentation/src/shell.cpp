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

#include "shell.h"
#include "mesh.h"
#include "utils.h"
#include "logging.h"
#include "mesh_graph.h"
#include "mesh_attribute.h"

#include "timer.h"

#include <vcg/complex/algorithms/hole.h>

#include <vector>

static bool Build(Mesh& shell, FaceGroup& fg);

bool BuildShellWithTargetsFromUV(Mesh& shell, FaceGroup& fg, double downsamplingFactor)
{

    // We construct a shell mesh, a 2D representation of the region that
    // will be manipulated by the re-optimization procedure.
    // To be valid, a shell mesh needs to guarantee that its number of connected
    // components is exactly one.
    // We store the built shell mesh in the variable `shell`.
    bool singleComponent = Build(shell, fg);

    // We must be sure that the constructed shell mesh keeps its faces in the
    // same order as the original face group. This is necessary because the
    // optimization procedure will access both sets in parallel, assuming
    // that at each step the retrieved references point to the same face.
    auto ia_ = GetFaceIndexAttribute(shell);
    for (unsigned i = 0; i < fg.FN(); ++i) {
        ensure(tri::Index(fg.mesh, fg.fpVec[i]) == (unsigned) ia_[shell.face[i]]);
    }

    Mesh& m = fg.mesh;

    double targetArea = 0;

    // We define four data structures:
    // * sa: stores the 3D shape of each shell face.
    // * ia: map data structure having as keys each shell face and as their values
    //       their original mesh face index.
    // * tsa: stores the target UV shape for the optimizer.
    // * wtcsa: stores the UV coordinates before the latest merge operation.
    //
    // These data structures are used to measure how much distortion the current UV layout
    // has relative to the 3D geometry.
    auto sa = GetShell3DShapeAttribute(shell);
    auto ia = GetFaceIndexAttribute(shell);
    auto tsa = GetTargetShapeAttribute(shell);
    auto wtcsa = GetWedgeTexCoordStorageAttribute(m);

    // We iterate over each face `f` in the shell, considering also their corresponding
    // original mesh face.
    //
    // Recall that UV Wedges store the texture positions before the merge, while the
    // UV Vertex coordinates store the texture positions after the merge.
    //
    // The original UV area is computed by computing the 2D cross-product. The result is
    // stored in the variable `area`.
    //
    // If the face has zero UV area, no meaningful Single Value Decomposition (SVD) can be
    // computed. Then the target UV shape is set as the current UV position, scaled by a
    // downsampling factor (passed as a user-defined parameter).
    //
    // Otherwise, we compute the 2x2 Jacobian `A` matrix. This matrix maps the 3D projection
    // of face `f` (in the bidimensional local coordinate system) to the original UV shape.
    // Then we apply a Single Value Decomposition of A such that A = U S V^{T}. From S we
    // retrieve the singular values s0 > s1. Recall that the singular values represent the
    // stretch factors of the parametrization.
    //
    // We define two downscaling strategies, both depending on the value of downscaleFactor:
    //
    // 1. downscaleFactor defines a `linear' downscaling on the largest singular
    //    value, the generator F = USV of the new target shape uses S={s0', s1'}
    //    such that:
    //    - s0' = k * s0 (we scale down s0 linearly by downsamplingFactor)
    //    - s1' = min(s1, s0') (s1 is capped by s0')
    //
    // 2. downscaleFactor defines the scaling factor of the parametrization, and we compute
    //    the singular values accordingly. Essentially, we can find a scaling threshold
    //    such that, above this threshold we shrink only the largest sing.val. s1, below
    //    this threshold we set s1' = s0' = k * s0 for a suitable k.
    //    INTUITION: a square mapped to a rectangle, we can reduce the area of the
    //    rectangle by making it more and more like a square, shrinking only
    //    one dimension. At some point the rectangle becomes a square, and we
    //    start making it smaller until we reach the target area.
    //
    // From the newly computed (s0', s1') we define the new matrix 2x2 S' (it has them
    // in the main diagonal, the rest of the values are set to zero). Finally, we compute
    // the generator transformation matrix Gen = U S' V^{T}. This matrix defines a new
    // parametrization map, which is less distorted compared to the original A.
    //
    // Applying Gen to the 3D flattened-edges vectors give us the target UV positions
    // (t10, t20).
    //
    // Finally, the computed target shape is stored in the shell face's field `f.tsa`.
    // It will be used by the ARAP optimizer. We also accumulate the total target UV area
    // as `targetArea` and we store in sa the current shell face's 3D positions.
    for (auto& sf : shell.face) {
        CoordStorage target;
        auto& f = m.face[ia[sf]];

        // Interpolate between texture and mesh face shapes to mitigate distortion
        const Point2d& u0 = wtcsa[f].tc[0].P();
        const Point2d& u1 = wtcsa[f].tc[1].P();
        const Point2d& u2 = wtcsa[f].tc[2].P();
        Point2d u10 = u1 - u0;
        Point2d u20 = u2 - u0;
        double area = std::abs(u10 ^ u20) / 2.0;

        if (area == 0) {
            // just scale everything by the linear scaling factor
            target.P[0] = vcg::Point3d(u0.X(), u0.Y(), 0) * downsamplingFactor;
            target.P[1] = vcg::Point3d(u1.X(), u1.Y(), 0) * downsamplingFactor;
            target.P[2] = vcg::Point3d(u2.X(), u2.Y(), 0) * downsamplingFactor;
        } else {
            // Compute the matrix of the input mapping and its SVD
            Point2d x10;
            Point2d x20;
            LocalIsometry(f.P(1) - f.P(0), f.P(2) - f.P(0), x10, x20);
            Eigen::Matrix2d A = ComputeTransformationMatrix(x10, x20, u10, u20);

            Eigen::Matrix2d U;
            Eigen::Matrix2d V;
            Eigen::Vector2d s;
            Eigen::JacobiSVD<Eigen::Matrix2d> svd;
            svd.compute(A, Eigen::ComputeFullU | Eigen::ComputeFullV);
            U = svd.matrixU();
            V = svd.matrixV();
            s = svd.singularValues();
            ensure(s[0] >= s[1]);

            // Compute the 'generator' matrix  and the target shape
            Eigen::Vector2d sNew;

            // First strategy - capping the texel-per-dim allocation
            sNew[0] = s[0] * downsamplingFactor;
            sNew[1] = std::min(sNew[0], s[1]);

            Eigen::Matrix2d gen = U * sNew.asDiagonal() * V.transpose();

            Eigen::Vector2d t10 = gen * Eigen::Vector2d(x10[0], x10[1]);
            Eigen::Vector2d t20 = gen * Eigen::Vector2d(x20[0], x20[1]);

            target.P[0] = Point3d(0, 0, 0);
            target.P[1] = Point3d(t10[0], t10[1], 0);
            target.P[2] = Point3d(t20[0], t20[1], 0);
        }

        tsa[sf] = target;
        targetArea += ((target.P[1] - target.P[0]) ^ (target.P[2] - target.P[0])).Norm() / 2.0;

        ensure(std::isfinite(targetArea));

        sa[sf].P[0] = sf.P(0);
        sa[sf].P[1] = sf.P(1);
        sa[sf].P[2] = sf.P(2);
    }

    return singleComponent;
}

void CloseHoles3D(Mesh& shell)
{
    Timer t;

    // We retrieve the current number of faces in the variable `startFN`.
    // This number will help us distinguish between original and added faces.
    // A new face always has its index major or equal than the original number
    // of faces (`startFN`).
    int startFN = shell.FN();

    auto ia = GetFaceIndexAttribute(shell);
    auto tsa = GetTargetShapeAttribute(shell);

    // Use the target area of the original faces (which should have been already computed) to
    // compute the scaling factors for the target triangles of the hole-filling faces
    //
    // Recall that in each shell face its attribute `tsa` stores the precomputed target shape
    // (i.e., the ideal UV positions after downscaling).
    //
    // Consider the following data that we will compute:
    //
    // * targetArea: the total UV area of all target shapes.
    //
    // * surfaceArea: the total 3D surface area of the shell faces.
    //
    // The ratio between targetArea and surfaceArea determines how much the target UV space has
    // been scaled relative to the 3D space. We take the square root of this value to get a
    // linear scale factor (this works since the area scales as the square of linear dimensions).
    //
    // `scale` will be used to make the target shape of the new faces consistent with the original ones.
    double surfaceArea = 0;
    double targetArea = 0;
    for (auto& sf : shell.face) {
        targetArea += ((tsa[sf].P[1] - tsa[sf].P[0]) ^ (tsa[sf].P[2] - tsa[sf].P[0])).Norm() / 2.0;
        surfaceArea += vcg::DoubleArea<MeshFace>(sf) / 2.0;
    }
    double scale = std::sqrt(targetArea / surfaceArea);

    // We will analyze the shell mesh's boundary structure (via `ComputeBoundaryInfoAttribute`), the
    // result will be stored as an attribute of the model.
    //
    // The borders of the shell mesh are stored as the vector of faces `vBoundaryFaces` in which each
    // element is a face having a boundary edge associated to a distinct loop.
    ComputeBoundaryInfoAttribute(shell);
    BoundaryInfo& info = GetBoundaryInfoAttribute(shell)();

    // Between all the retrieved boundaries, there is also the outer boundary
    // of the chart, which must remain open. The outer border is also the longest
    // boundary. We iterate over the border faces, marking as SELECTED only those
    // referring to a boundary that is not the longest.
    //
    // Having identified the holes, we proceed to fill then using the ear-cutting
    // triangulation algorithm (via `EarCuttingFill`).
    tri::UpdateFlags<Mesh>::FaceClearS(shell);
    ensure(info.vBoundaryFaces.size() > 0 && "Mesh has no boundaries");
    if (info.vBoundaryFaces.size() > 1) {
        std::size_t k = info.LongestBoundary();
        // select all the boundary faces
        for (std::size_t i = 0; i < info.vBoundaryFaces.size(); ++i) {
            if (i == k) continue;
            for (auto j : info.vBoundaryFaces[i]) {
                ensure(face::IsBorder(shell.face[j], 0) || face::IsBorder(shell.face[j], 1) || face::IsBorder(shell.face[j], 2));
                shell.face[j].SetS();
            }
        }
        tri::Hole<Mesh>::EarCuttingFill<tri::TrivialEar<Mesh>>(shell, shell.FN(), true);
    }

    // After the hole filling, some original faces may have been virtually deleted.
    // We proceed to remove them definitively.
    tri::Allocator<Mesh>::CompactFaceVector(shell);
    ensure(shell.FN() == (int) shell.face.size());

    // For each newly created face (identifiable by having index major or equal
    // than the original number of faces), we need to set its attributes:
    //
    // * mark them a hole filling faces.
    //
    // * since they have no corresponding face in the original mesh, set their
    //   face index attribute to `-1`.
    //
    // * since they have no corresponding face in the original mesh, set its
    //   per-Wedge UV coordinate (usually used for storing the original positions)
    //   same as the per-Vertex UV coordinates.
    //
    // * set the target shape, depending on the face's area:
    //   - If its area is zero, then the target is set directly from the UV
    //     coordinates, embedding them as 3D points with the z-coordinate
    //     equal to zero.
    //   - If its area is major than zero, then the target is the face's actual
    //     3D positions scaled by `scale`.
    for (auto& sf : shell.face) {
        if (int(tri::Index(shell, sf)) >= startFN) {
            sf.SetHoleFilling();
            ia[sf] = -1;
            double area = Area3D(sf);
            for (int i = 0; i < 3; ++i) {
                vcg::Point2d wti = sf.V(i)->T().P();
                sf.WT(i).P() = wti;

                if (area == 0)
                    tsa[sf].P[i] = vcg::Point3d(wti.X(), wti.Y(), 0);
                else
                    tsa[sf].P[i] = sf.P(i) * scale;
            }
        }
    }

    // Finally, we must rebuild the VERTEX-FACE and FACE-FACE topologies
    // to take into account the newly added faces.
    tri::UpdateTopology<Mesh>::FaceFace(shell);
    tri::UpdateTopology<Mesh>::VertexFace(shell);
}

void SyncShellWithUV(Mesh& shell)
{
    for (auto& v : shell.vert) {
        v.P().X() = v.T().U();
        v.P().Y() = v.T().V();
        v.P().Z() = 0.0;
    }
    tri::UpdateBounding<Mesh>::Box(shell);
}

void SyncShellWith3D(Mesh& shell)
{
    auto sa = GetShell3DShapeAttribute(shell);
    for (auto& sf : shell.face) {
        ensure(sf.IsMesh());
        for (int i = 0; i < 3; ++i)
            sf.P(i) = sa[sf].P[i];
    }
    tri::UpdateBounding<Mesh>::Box(shell);
}

void ClearHoleFillingFaces(Mesh& shell, bool holefill, bool scaffold)
{
    for (auto& f : shell.face)
        if ((holefill && f.IsHoleFilling()) || (scaffold && f.IsScaffold()))
            tri::Allocator<Mesh>::DeleteFace(shell, f);

    tri::Clean<Mesh>::RemoveUnreferencedVertex(shell);
    tri::UpdateTopology<Mesh>::FaceFace(shell);
    tri::UpdateTopology<Mesh>::VertexFace(shell);
    tri::Allocator<Mesh>::CompactEveryVector(shell);
}

/*!
 * Construct a shell mesh, a bidimensional model having exactly one connected
 * component, copying data from a given face group.
 *
 * In practice a shell mesh is used as the domain of the current UV re-optimization
 * procedure, contains the UV parametrization of the merged pairs of charts.
 *
 * @param shell: the shell mesh to construct.
 * @param fg: the face group used for construction.
 *
 * @return a boolean indicating if we have constructed a good shell mesh
 * (i.e., one having exactly one connected component), or failed.
 */
static bool Build(Mesh& shell, FaceGroup& fg) {

    // Copies all the raw vertices and faces from the face group into
    // the new shell mesh.
    CopyToMesh(fg, shell);

    // Even though the previous process guarantees that we haven't
    // copy each distinct vertex more than once, we make a second
    // check. We also free any un-necessary data from the mesh
    // (i.e., remove extra empty entries from face and vertex vectors).
    tri::Clean<Mesh>::RemoveDuplicateVertex(shell);
    tri::Allocator<Mesh>::CompactEveryVector(shell);

    // We construct the shell mesh FACE-FACE (FF) topology and compute
    // its bounding box.
    tri::UpdateBounding<Mesh>::Box(shell);
    tri::UpdateTopology<Mesh>::FaceFace(shell);

    // Recall that a shell mesh is valid if and only if it has exactly
    // one connected component. We check if our mesh is valid and return
    // a boolean indicating if our build procedure succeeded or not.
    return tri::Clean<Mesh>::CountConnectedComponents(shell) == 1;
}
