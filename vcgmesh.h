#pragma once

#include <vcg/complex/complex.h>

class VCGVertex;
class VCGEdge;
class VCGFace;

struct VCGUsedTypes : public vcg::UsedTypes<
    vcg::Use<VCGVertex>::AsVertexType,
    vcg::Use<VCGEdge>::AsEdgeType,
    vcg::Use<VCGFace>::AsFaceType> {};

class VCGVertex : public vcg::Vertex<VCGUsedTypes,
    vcg::vertex::Coord3f,
    vcg::vertex::Normal3f,
    vcg::vertex::TexCoord2f,
    vcg::vertex::Color4b,
    vcg::vertex::Qualityf,
    vcg::vertex::BitFlags,
    vcg::vertex::VFAdj> {};

class VCGEdge : public vcg::Edge<VCGUsedTypes> {};

class VCGFace : public vcg::Face<VCGUsedTypes,
    vcg::face::VertexRef,
    vcg::face::Normal3f,
    vcg::face::WedgeTexCoord2f,
    vcg::face::Color4b,
    vcg::face::Qualityf,
    vcg::face::BitFlags,
    vcg::face::FFAdj,
    vcg::face::VFAdj> {};

class VCGMesh : public vcg::tri::TriMesh<
    std::vector<VCGVertex>,
    std::vector<VCGEdge>,
    std::vector<VCGFace>> {};
