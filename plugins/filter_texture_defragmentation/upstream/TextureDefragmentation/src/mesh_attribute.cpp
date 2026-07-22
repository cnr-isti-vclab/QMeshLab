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

#include "mesh_attribute.h"
#include "math_utils.h"
#include "logging.h"

void Compute3DFaceAdjacencyAttribute(Mesh& m)
{
    auto ffadj = Get3DFaceAdjacencyAttribute(m);
    tri::UpdateTopology<Mesh>::FaceFace(m);
    for (auto& f : m.face) {
        for (int i = 0; i < 3; ++i) {
            ffadj[f].f[i] = tri::Index(m, f.FFp(i));
            ffadj[f].e[i] = f.FFi(i);
        }
    }
}

void ComputeWedgeTexCoordStorageAttribute(Mesh& m)
{
    auto WTCSh = GetWedgeTexCoordStorageAttribute(m);
    for (auto &f : m.face) {
        for (int i = 0; i < 3; ++i) {
            WTCSh[&f].tc[i].P() = f.WT(i).P();
            WTCSh[&f].tc[i].N() = f.WT(i).N();
        }
    }
}

// assumes topology is updated (FaceFace)
void ComputeBoundaryInfoAttribute(Mesh& m)
{
    // Initialization step. We clean the input mesh local data regarding boundaries
    // (which is held as the attribute BoundaryInfo), since we are recomputing them.
    //
    // Additionally, we clear the `VISITED` face flag from all the faces. Within the function
    // we interpret faces marked as `VISITED` as adjacent to an already visited boundary
    // loop. In this way we make sure that loops aren't considered more than once.
    BoundaryInfo& info = (tri::Allocator<Mesh>::GetPerMeshAttribute<BoundaryInfo>(m, "MeshAttribute_BoundaryInfo"))();
    info.Clear();
    tri::UpdateFlags<Mesh>::FaceClearV(m);

    // We iterate over all mesh's faces, considering only faces at the border (i.e.,
    // having an edge incident to only one face) and not being marked as VISITED.
    //
    // Each of them identifies a newly found boundary loop. We visit it by walking
    // across adjacent boundary faces until we return back to the starting face.
    //
    // During the walk we compute the following data, which will be stored in a
    // corresponding field within the BoundaryInfo attribute:
    //
    // * the total border length of the hole. It will be stored in the field `vBoundaryLength`.
    //
    // * the faces belonging to the hole. It will be stored in the field `vBoundaryFaces`.
    //
    // * the number of faces belonging to the hole. It will be stored in the field `vBoundarySize`.
    //
    // * the local index of the current vertex being visited within the face (i.e., its corner). It
    // is used to quickly determine the border edge of the face.
    //
    // Finally, the face is marked to avoid visiting the same hole twice. This works because for a manifold
    // mesh, a face can be incident to at most one border.
    for (auto& f : m.face) {
        for (int i = 0; i < 3; ++i) {
            if (!f.IsV() && face::IsBorder(f, i)) {
                double totalBorderLength = 0;
                std::vector<std::size_t> borderFaces;
                std::vector<int> vi;

                face::Pos<Mesh::FaceType> p(&f, i);
                face::Pos<Mesh::FaceType> startPos = p;
                ensure(p.IsBorder());
                do {
                    ensure(p.IsManifold());
                    p.F()->SetV();
                    borderFaces.push_back(tri::Index(m, p.F()));
                    vi.push_back(p.VInd());
                    totalBorderLength += EdgeLength(*p.F(), p.VInd());
                    p.NextB();
                } while (p != startPos);

                info.vBoundaryLength.push_back(totalBorderLength);
                info.vBoundarySize.push_back(borderFaces.size());
                info.vBoundaryFaces.push_back(borderFaces);
                info.vVi.push_back(vi);
            }
        }
    }

    LOG_DEBUG << "Mesh has " << info.N() << " boundaries";
}