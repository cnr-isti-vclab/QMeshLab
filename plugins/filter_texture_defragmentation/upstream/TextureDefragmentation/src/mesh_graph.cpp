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

#include "mesh_graph.h"

#include "mesh.h"
#include "math_utils.h"
#include "mesh_attribute.h"
#include "timer.h"
#include "utils.h"
#include "logging.h"

#include <vcg/complex/complex.h>
#include <vcg/complex/algorithms/parametrization/distortion.h>
#include <vcg/math/histogram.h>
#include <vcg/complex/algorithms/stat.h>
#include <vcg/complex/algorithms/update/color.h>
#include <vcg/complex/algorithms/attribute_seam.h>

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <memory>

#include <QImage>


void CopyToMesh(FaceGroup& fg, Mesh& m)
{
    // The mesh `m` will be re-constructed from zero, adding all faces coming
    // from the face group `fg`. To start, we clear it of any invalid data.
    m.Clear();

    auto ia = GetFaceIndexAttribute(m);

    // Multiple faces share the same vertices. When constructing our mesh, we
    // need to make sure that we copy a vertex only once. To guarantee it, we
    // construct the map data structure `vpmap`, which associated distinct
    // vertices from the face group (keys) to their unique copy in the mesh
    // `m` (values).
    //
    // At the start this map will have entries for each distinct vertex
    // in the face group, all pointing to an empty value, indicating that
    // we still haven't copied any of them into the mesh.
    std::unordered_map<Mesh::VertexPointer, Mesh::VertexPointer> vpmap;
    vpmap.reserve(fg.FN() * 3);
    std::size_t vn = 0;
    for (auto fptr : fg.fpVec) {
        for (int i = 0; i < 3; ++i) {
            if (vpmap.count(fptr->V(i)) == 0) {
                vn++;
                vpmap[fptr->V(i)] = nullptr;
            }
        }
    }


    // Now the copy happens. We first allocate in the mesh `m` the necessary space
    // for copying all faces and distinct vertices from the face group. Then we
    // iterate over each face `f` in the face group. For each face we need to copy
    // its index attribute, vertex corners and UV Wedge position.
    //
    // When iterating over its vertices `vi`, we need to make sure to check if it
    // has been already explicitly copied within the mesh or not. Recall that we check
    // this property by accessing the entry `vi` of `vpmap`: if its empty, we need to
    // explicitly copy it, otherwise we just retrieve its reference. An explicit copy
    // retrieves its position, texture coordinates and color.
    auto mvi = tri::Allocator<Mesh>::AddVertices(m, vn);
    auto mfi = tri::Allocator<Mesh>::AddFaces(m, fg.FN());
    for (auto fptr : fg.fpVec) {
        Mesh::FacePointer mfp = &*mfi++;
        ia[mfp] = tri::Index(fg.mesh, fptr);
        for (int i = 0; i < 3; ++i) {
            Mesh::VertexPointer vp = fptr->V(i);
            typename Mesh::VertexPointer& mvp = vpmap[vp];
            if (mvp == nullptr) {
                mvp = &*mvi++;
                mvp->P() = vp->P();
                mvp->T() = vp->T();
                mvp->C() = vp->C();
            }
            mfp->V(i) = mvp;
            mfp->WT(i) = fptr->WT(i);
        }
        mfp->SetMesh();
    }

    LOG_DEBUG << "Built mesh has " << m.FN() << " faces";
}

// FaceGroup class implementation
// ==============================

FaceGroup::FaceGroup(Mesh& m, const RegionID id_)
    : mesh{m},
      id{id_},
      fpVec{},
      adj{},
      numMerges{0},
      minMappedFaceValue{-1},
      maxMappedFaceValue{-1},
      error{0},
      dirty{false},
      cache{}
{
}

void FaceGroup::Clear()
{
    id = INVALID_ID;
    fpVec.clear();
    adj.clear();
    numMerges = 0;
    minMappedFaceValue = -1;
    maxMappedFaceValue = -1;
    error = 0;
    dirty = false;
    cache = {};
}

void FaceGroup::UpdateCache() const
{
    using ::AreaUV;
    using ::Area3D;

    double areaUV = 0;
    double area3D = 0;
    vcg::Point3d weightedSumNormal = vcg::Point3d::Zero();
    for (auto fptr : fpVec) {
        areaUV += AreaUV(*fptr);
        area3D += Area3D(*fptr);
        weightedSumNormal += (fptr->P(1) - fptr->P(0)) ^ (fptr->P(2) ^ fptr->P(0));
    }

    double border3D = 0.0;
    double borderUV = 0.0;
    for (auto fptr : fpVec) {
        for (int i = 0; i < 3; ++i) {
            if (face::IsBorder(*fptr, i)) {
                border3D += EdgeLength(*fptr, i);
                borderUV += EdgeLengthUV(*fptr, i);
            }
        }
    }

    cache.area3D = area3D;
    cache.areaUV = std::abs(areaUV);
    cache.borderUV = borderUV;
    cache.border3D = border3D;
    cache.weightedSumNormal = weightedSumNormal;
    cache.uvFlipped = (areaUV < 0);

    dirty = false;
}

vcg::Point3d FaceGroup::AverageNormal() const
{
    if (dirty)
        UpdateCache();
    vcg::Point3d avgN = ((cache.weightedSumNormal) / (2.0 * cache.area3D));
    return avgN.Normalize();
}

void FaceGroup::AddFace(const Mesh::FacePointer fptr)
{
    fpVec.push_back(fptr);
    dirty = true;
}

double FaceGroup::OriginalAreaUV() const
{
    ensure(HasWedgeTexCoordStorageAttribute(mesh));
    auto wtcsattr = GetWedgeTexCoordStorageAttribute(mesh);

    double doubleAreaUV = 0;
    for (auto fptr : fpVec) {
        const TexCoordStorage& tcs = wtcsattr[fptr];
        doubleAreaUV += std::abs((tcs.tc[1].P() - tcs.tc[0].P()) ^ (tcs.tc[2].P() - tcs.tc[0].P()));
    }
    return 0.5 * doubleAreaUV;
}

double FaceGroup::AreaUV() const
{
    if (dirty)
        UpdateCache();
    return cache.areaUV;
}

double FaceGroup::Area3D() const
{
    if (dirty)
        UpdateCache();
    return cache.area3D;
}

double FaceGroup::BorderUV() const
{
    if (dirty)
        UpdateCache();
    return cache.borderUV;
}

double FaceGroup::Border3D() const
{
    if (dirty)
        UpdateCache();
    return cache.border3D;
}

bool FaceGroup::UVFlipped() const
{
    if (dirty)
        UpdateCache();
    return cache.uvFlipped;
}

vcg::Box2d FaceGroup::UVBox() const
{
    vcg::Box2d box;
    for (auto fptr : fpVec) {
        box.Add(fptr->WT(0).P());
        box.Add(fptr->WT(1).P());
        box.Add(fptr->WT(2).P());
    }
    return box;
}

bool FaceGroup::UVFlipped()
{
    if (dirty)
        UpdateCache();
    return cache.uvFlipped;
}

void FaceGroup::ParameterizationChanged()
{
    dirty = true;
}

Mesh::FacePointer FaceGroup::Fp()
{
    ensure(!fpVec.empty()); return fpVec[0];
}

std::size_t FaceGroup::FN() const
{
    return fpVec.size();
}

std::size_t FaceGroup::NumAdj() const
{
    return adj.size();
}

void FaceGroup::UpdateBorder() const
{
    if (dirty)
        UpdateCache();
}

// MeshGraph class implementation
// ==============================

MeshGraph::MeshGraph(Mesh& m)
    : mesh{m}
{
}

MeshGraph::~MeshGraph()
{
    textureObject = nullptr;
    // Explicitly remove adjacency handles to remove dangling references
    for (auto& entry: charts) {
        entry.second->adj.clear();
    }
    charts.clear();
}

std::pair<float,float> MeshGraph::DistortionRange() const
{
    std::pair<float,float> range = std::make_pair(std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest());
    for (const auto& c : charts) {
        range.first = std::min(c.second->minMappedFaceValue, range.first);
        range.second = std::max(c.second->maxMappedFaceValue, range.second);
    }
    return range;
}

std::shared_ptr<FaceGroup> MeshGraph::GetChart(RegionID i)
{
    //ensure(charts.find(i) != charts.end() && "Chart does not exist");
    auto e = charts.find(i);
    if (e != charts.end()) return e->second;
    else return nullptr;
}

std::shared_ptr<FaceGroup> MeshGraph::GetChart_Insert(RegionID i)
{
    if (charts.find(i) == charts.end()) charts.insert(std::make_pair(i, std::make_shared<FaceGroup>(mesh, i)));
    return charts[i];
}

std::size_t MeshGraph::Count() const
{
    return charts.size();
}

int MeshGraph::MergeCount() const
{
    int n = 0;
    for (const auto& c : charts) n += c.second->numMerges;
    return n;

}

double MeshGraph::Area3D() const
{
    double area3D = 0;
    for (const auto& c : charts) area3D += c.second->Area3D();
    return area3D;
}

double MeshGraph::MappedFraction() const
{
    double area3D = 0;
    double mappedArea3D = 0;
    for (const auto& c : charts) {
        area3D += c.second->Area3D();
        if (c.second->AreaUV() > 0)
            mappedArea3D += c.second->Area3D();
    }
    return mappedArea3D / area3D;
}

double MeshGraph::AreaUV() const
{
    double areaUV = 0;
    for (const auto& c : charts) areaUV += c.second->AreaUV();
    return areaUV;
}

double MeshGraph::SignedAreaUV() const
{
    double areaUV = 0;
    for (const auto& c : charts)
        areaUV += (c.second->UVFlipped() ? -1 : 1) * c.second->AreaUV();
    return areaUV;
}

double MeshGraph::BorderUV() const
{
    double borderUV = 0;
    for (const auto& c : charts) borderUV += c.second->BorderUV();
    return borderUV;
}

GraphHandle ComputeGraph(Mesh &m, TextureObjectHandle textureObject)
{
    // Identify for each face of the mesh its associated UV island.
    // A face's island is determined by its connected component (i.e., the closed subset of faces reachable through
    // FACE-FACE adjacency). The region's identifiers are assigned in increasing order, starting from zero.
    //
    // Note that here we are using the current topology which has duplicated vertices at seams. Only in this way we
    // can find the separate regions.
    tri::UpdateFlags<Mesh>::FaceClearV(m);
    RegionID id = 0;
    for (auto& f : m.face) {
        if (!f.IsV()) {
            // visit the connected component
            std::stack<Mesh::FacePointer> s;
            s.push(&f);
            while (!s.empty()) {
                Mesh::FacePointer fp = s.top();
                s.pop();
                fp->SetV();
                fp->id = id;
                fp->initialId = id;
                for (int i = 0; i < 3; ++i) {
                    Mesh::FacePointer ffp = fp->FFp(i);
                    if (!ffp->IsV())
                        s.push(ffp);
                }
            }
            id++;
        }
    }

    // Construct the graph instance of the parameterization. Within we have:
    // - nodes: the regions of our UV layout. Each node stores the faces belonging to it.
    // - edges: connects charts whose associated 3D areas touch each other, meaning that they share at least one seam.
    //
    // Our algorithm fills the graph by visiting each face and storing a reference to it in the associated node.
    // Then we check the adjacent faces sharing a common mesh's edge. If one of those belongs to a different region, we
    // find two adjacent graphs, and we connect their nodes through a graph edge.
    //
    // Note that here we are using the original pre-split geometry, in which all faces are connected to each
    // other. Only by comparing separated region with the original topology we can identify cuts.
    GraphHandle graph = std::make_shared<MeshGraph>(m);
    graph->textureObject = textureObject;

    auto ffadj = Get3DFaceAdjacencyAttribute(m);

    tri::UpdateTopology<Mesh>::FaceFace(m);
    for (auto &f : m.face) {
        RegionID regionId = f.id;
        graph->GetChart_Insert(regionId)->AddFace(&f);
        for (int i = 0; i < f.VN(); ++i) {
            if (IsEdgeManifold3D(m, f, i, ffadj)) {
                RegionID adjId = m.face[ffadj[f].f[i]].id;
                if (regionId != adjId) {
                    (graph->GetChart_Insert(regionId)->adj).insert(graph->GetChart_Insert(adjId));
                }
            }
        }
    }

    return graph;
}

void DisconnectCharts (GraphHandle graph) {

    // Defines a pair vertex id and associated region.
    typedef std::pair<int, RegionID> VertexRID;

    Mesh& m = graph->mesh;

    int numExtraVertices = 0;
    std::map<VertexRID, int> remap;

    // We visit the UV layout, keeping track of vertices that are shared among multiple charts.
    // The data is stored within the `remap` data structure s.t. each entry is represented in the following manner:
    // (vertex id, chart id) -> duplicate index = -1.
    // Since we haven't created the duplicates yet, we set their value to the sentinel value `-1`.
    //
    // Additionally, we count in `numExtraVertices` the total numbers vertices to duplicate that we have.
    tri::UpdateFlags<Mesh>::VertexClearV(m);
    for (auto& c : graph->charts) {
        std::set<Mesh::VertexPointer> vset;
        for (auto fptr : c.second->fpVec) {
            for (int i = 0; i < 3; ++i) {
                vset.insert(fptr->V(i));
            }
        }
        for (auto vp : vset) {
            if (vp->IsV()) {
                numExtraVertices++;
                remap[std::make_pair(tri::Index(m, vp), c.first)] = -1;
            }
            vp->SetV();
        }
    }

    // We allocate the necessary amount of duplicate vertices we need (i.e., `numExtraVertices`).
    // For each of them we copy the coordinates and additional data from the associated entry in "remap".
    // The entry is updated by setting as value the index of the newly created duplicated vertex.
    //
    // the VISITED marking now is given only to the original vertices that have been duplicated.
    auto vi = tri::Allocator<Mesh>::AddVertices(m, numExtraVertices);
    tri::UpdateFlags<Mesh>::VertexClearV(m);
    for (auto& entry : remap) {
        VertexRID vrid = entry.first;
        vi->ImportData(m.vert[vrid.first]);
        m.vert[vrid.first].SetV();

        ensure(entry.second == -1);
        entry.second = tri::Index(m, *vi);
        vi++;
    }

    // We need to update the corners of those faces now incident to a newly duplicated vertex. These must bu updated
    // to point to the newly created one. To do so we walk over every face corner of every chart.
    int updated = 0;
    int iters = 0;
    for (auto& c : graph->charts) {
        for (auto fptr : c.second->fpVec) {
            for (int i = 0; i < 3; ++i) {
                VertexRID vrid = std::make_pair(tri::Index(m, fptr->V(i)), c.first);
                iters++;
                if (fptr->V(i)->IsV() && remap.count(vrid) > 0) {
                    int vind = remap[vrid];
                    fptr->V(i) = &m.vert[vind];
                    updated++;
                }
            }
        }
    }

    // We check to be sure that there is no vertex is shared among more than one chart.
    // The loop is identical to the first one.
    tri::UpdateFlags<Mesh>::VertexClearV(m);
    for (auto& c : graph->charts) {
        std::set<Mesh::VertexPointer> vset;
        for (auto fptr : c.second->fpVec) {
            for (int i = 0; i < 3; ++i) {
                vset.insert(fptr->V(i));
            }
        }
        for (auto vp : vset) {
            ensure(!vp->IsV());
            vp->SetV();
        }
    }
}
