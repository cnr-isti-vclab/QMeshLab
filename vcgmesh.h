#pragma once

#include <vcg/complex/complex.h>

class VCGVertex;
class VCGEdge;
class VCGFace;

struct VCGUsedTypes : public vcg::UsedTypes<
    vcg::Use<VCGVertex>::AsVertexType,
    vcg::Use<VCGEdge>::AsEdgeType,
    vcg::Use<VCGFace>::AsFaceType> {};

// Fixed (always allocated): Coord, Normal, Color, Quality, BitFlags
// OCF storable (allocated when data is loaded/computed, preserved in undo):
//   TexCoordfOcf, CurvatureDirfOcf
// OCF ancillary (allocated on demand by algorithms, discarded after use):
//   VFAdjOcf
class VCGVertex : public vcg::Vertex<VCGUsedTypes,
    vcg::vertex::InfoOcf,
    vcg::vertex::Coord3f,
    vcg::vertex::Normal3f,
    vcg::vertex::Color4b,
    vcg::vertex::Qualityf,
    vcg::vertex::BitFlags,
    vcg::vertex::TexCoordfOcf,
    vcg::vertex::CurvatureDirfOcf,
    vcg::vertex::VFAdjOcf> {};

class VCGEdge : public vcg::Edge<VCGUsedTypes,
    vcg::edge::VertexRef,
    vcg::edge::BitFlags> {};

// Fixed (always allocated): VertexRef, Normal, Color, Quality, BitFlags
// OCF storable (allocated when data is loaded/computed, preserved in undo):
//   WedgeTexCoordfOcf
// OCF ancillary (allocated on demand by algorithms, discarded after use):
//   FFAdjOcf, VFAdjOcf, MarkOcf
class VCGFace : public vcg::Face<VCGUsedTypes,
    vcg::face::InfoOcf,
    vcg::face::VertexRef,
    vcg::face::Normal3f,
    vcg::face::Color4b,
    vcg::face::Qualityf,
    vcg::face::BitFlags,
    vcg::face::WedgeTexCoordfOcf,
    vcg::face::FFAdjOcf,
    vcg::face::VFAdjOcf,
    vcg::face::MarkOcf> {};

class VCGMesh : public vcg::tri::TriMesh<
    vcg::vertex::vector_ocf<VCGVertex>,
    std::vector<VCGEdge>,
    vcg::face::vector_ocf<VCGFace>> {};

// ---------------------------------------------------------------------------
// RAII helpers for ancillary OCF fields.
// These fields are computed by algorithms and must be discarded after use —
// never stored in undo snapshots or deepCopy.
// ---------------------------------------------------------------------------

// Enables face-face adjacency for the scope of the object.
struct VCGMeshFFAdjScope {
    VCGMesh &m;
    explicit VCGMeshFFAdjScope(VCGMesh &m) : m(m) { m.face.EnableFFAdjacency(); }
    ~VCGMeshFFAdjScope() { m.face.DisableFFAdjacency(); }
    VCGMeshFFAdjScope(const VCGMeshFFAdjScope &) = delete;
    VCGMeshFFAdjScope &operator=(const VCGMeshFFAdjScope &) = delete;
};

// Enables vertex-face adjacency (both vert and face vectors) for the scope.
struct VCGMeshVFAdjScope {
    VCGMesh &m;
    explicit VCGMeshVFAdjScope(VCGMesh &m) : m(m) {
        m.vert.EnableVFAdjacency();
        m.face.EnableVFAdjacency();
    }
    ~VCGMeshVFAdjScope() {
        m.vert.DisableVFAdjacency();
        m.face.DisableVFAdjacency();
    }
    VCGMeshVFAdjScope(const VCGMeshVFAdjScope &) = delete;
    VCGMeshVFAdjScope &operator=(const VCGMeshVFAdjScope &) = delete;
};

// Enables face Mark for the scope (used by Tmark / spatial queries).
struct VCGMeshMarkScope {
    VCGMesh &m;
    explicit VCGMeshMarkScope(VCGMesh &m) : m(m) { m.face.EnableMark(); }
    ~VCGMeshMarkScope() { m.face.DisableMark(); }
    VCGMeshMarkScope(const VCGMeshMarkScope &) = delete;
    VCGMeshMarkScope &operator=(const VCGMeshMarkScope &) = delete;
};
