/*******************************************************************************
    Copyright (c) 2021, Andrea Maggiordomo, Paolo Cignoni and Marco Tarini

    This file is part of TextureDefrag, a reference implementation for
    the paper ``Texture Defragmentation for Photo-Reconstructed 3D Models''
    by Andrea Maggiordomo, Paolo Cignoni and Marco Tarini.

    QMeshLab adaptation note: the original standalone file also implemented mesh
    file IO. QMeshLab feeds the algorithm from its in-memory document model, so
    only the mesh utility routines used by the defragmentation pipeline are kept.
*******************************************************************************/

#include <vcg/complex/complex.h>
#include <unordered_map>
#include <vcg/complex/algorithms/attribute_seam.h>

#include "mesh.h"
#include "texture_object.h"
#include "utils.h"

bool LoadMesh(const char *, Mesh &, TextureObjectHandle &, int &)
{
    return false;
}

bool SaveMesh(const char *, Mesh &, const std::vector<std::shared_ptr<QImage>> &, bool)
{
    return false;
}

void ScaleTextureCoordinatesToImage(Mesh& m, TextureObjectHandle textureObject)
{
    for (auto& f : m.face) {
        int ti = f.WT(0).N();
        for (int i = 0; i < f.VN(); ++i) {
            f.WT(i).P().X() *= (ti < (int) textureObject->ArraySize()) ? textureObject->TextureWidth(ti) : 1.0;
            f.WT(i).P().Y() *= (ti < (int) textureObject->ArraySize()) ? textureObject->TextureHeight(ti) : 1.0;
        }
    }
}

void ScaleTextureCoordinatesToParameterArea(Mesh& m, TextureObjectHandle textureObject)
{
    for (auto& f : m.face) {
        int ti = f.WT(0).N();
        for (int i = 0; i < f.VN(); ++i) {
            f.WT(i).P().X() /= (ti < (int) textureObject->ArraySize()) ? textureObject->TextureWidth(ti) : 1.0;
            f.WT(i).P().Y() /= (ti < (int) textureObject->ArraySize()) ? textureObject->TextureHeight(ti) : 1.0;
        }
    }
}

Box2d UVBox(const Mesh& m)
{
    Box2d uvbox;
    for(auto const& f : m.face) {
        for (int i = 0; i < 3; ++i) {
            uvbox.Add(f.cWT(i).P());
        }
    }
    return uvbox;
}

Box2d UVBoxVertex(const Mesh& m)
{
    Box2d uvbox;
    for(auto const& f : m.face) {
        for (int i = 0; i < 3; ++i) {
            uvbox.Add(f.cV(i)->T().P());
        }
    }
    return uvbox;
}

static inline void vExt(const Mesh&, const MeshFace& f, int k, const Mesh&, MeshVertex& v)
{
    v.ImportData(*(f.cV(k)));
    v.T() = f.cWT(k);
}

static inline bool vCmp(const Mesh&, const MeshVertex& v1, const MeshVertex& v2)
{
    return v1.T() == v2.T();
}

void CutAlongSeams(Mesh& m)
{
    tri::AttributeSeam::SplitVertex(m, vExt, vCmp);
    tri::Allocator<Mesh>::CompactVertexVector(m);
    tri::UpdateTopology<Mesh>::FaceFace(m);
    tri::UpdateTopology<Mesh>::VertexFace(m);
}

void MeshFromFacePointers(const std::vector<Mesh::FacePointer>& vfp, Mesh& out)
{
    out.Clear();
    std::unordered_map<Mesh::VertexPointer, Mesh::VertexPointer> vpmap;
    vpmap.reserve(vfp.size() * 2);
    std::size_t vn = 0;
    for (auto fptr : vfp) {
        for (int i = 0; i < 3; ++i) {
            if (vpmap.count(fptr->V(i)) == 0) {
                vn++;
                vpmap[fptr->V(i)] = nullptr;
            }
        }
    }
    auto mvi = tri::Allocator<Mesh>::AddVertices(out, vn);
    auto mfi = tri::Allocator<Mesh>::AddFaces(out, vfp.size());
    for (auto fptr : vfp) {
        Mesh::FacePointer mfp = &*mfi++;
        for (int i = 0; i < 3; ++i) {
            Mesh::VertexPointer vp = fptr->V(i);
            typename Mesh::VertexPointer& mvp = vpmap[vp];
            if (mvp == nullptr) {
                mvp = &*mvi++;
                mvp->P() = vp->P();
            }
            mfp->V(i) = mvp;
            mfp->WT(i) = fptr->WT(i);
        }
        mfp->SetMesh();
    }
}
