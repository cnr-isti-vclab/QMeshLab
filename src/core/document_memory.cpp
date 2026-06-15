#include "document_internal.h"
#include "documentundomanager.h"

using namespace DocumentInternal;

namespace {

template <typename Vector>
qint64 vectorStorageBytes(const Vector &v)
{
    return qint64(v.capacity()) * qint64(sizeof(typename Vector::value_type));
}

qint64 vcgVertexOcfBytes(const VCGMesh &mesh)
{
    return vectorStorageBytes(mesh.vert.CV)
         + vectorStorageBytes(mesh.vert.CuV)
         + vectorStorageBytes(mesh.vert.CuDV)
         + vectorStorageBytes(mesh.vert.MV)
         + vectorStorageBytes(mesh.vert.NV)
         + vectorStorageBytes(mesh.vert.QV)
         + vectorStorageBytes(mesh.vert.RadiusV)
         + vectorStorageBytes(mesh.vert.TV)
         + vectorStorageBytes(mesh.vert.AV);
}

qint64 vcgFaceOcfBytes(const VCGMesh &mesh)
{
    return vectorStorageBytes(mesh.face.CV)
         + vectorStorageBytes(mesh.face.CDV)
         + vectorStorageBytes(mesh.face.MV)
         + vectorStorageBytes(mesh.face.NV)
         + vectorStorageBytes(mesh.face.QV)
         + vectorStorageBytes(mesh.face.WCV)
         + vectorStorageBytes(mesh.face.WNV)
         + vectorStorageBytes(mesh.face.WTV)
         + vectorStorageBytes(mesh.face.AV)
         + vectorStorageBytes(mesh.face.AF);
}

qint64 vcgMeshCpuBytes(const VCGMesh &mesh)
{
    return qint64(mesh.vert.capacity()) * sizeof(VCGVertex)
         + vcgVertexOcfBytes(mesh)
         + qint64(mesh.edge.capacity()) * sizeof(VCGEdge)
         + qint64(mesh.face.capacity()) * sizeof(VCGFace)
         + vcgFaceOcfBytes(mesh);
}

qint64 meshEntryCpuBytes(const Document::MeshEntry &entry)
{
    return vcgMeshCpuBytes(entry.mesh);
}

} // namespace

std::vector<Document::CpuMeshMemoryStats> Document::cpuMeshMemoryStats() const
{
    std::vector<CpuMeshMemoryStats> result;
    result.reserve(m_meshes.size());
    for (int i = 0; i < meshCount(); ++i) {
        const MeshEntry &entry = mesh(i);
        CpuMeshMemoryStats s;
        s.meshId = entry.meshId;
        s.meshIndex = i;
        s.name = entry.name;
        s.vertexCapacity = static_cast<int>(entry.mesh.vert.capacity());
        s.edgeCapacity   = static_cast<int>(entry.mesh.edge.capacity());
        s.faceCapacity   = static_cast<int>(entry.mesh.face.capacity());
        s.vertexBytes    = qint64(entry.mesh.vert.capacity()) * sizeof(VCGVertex);
        s.vertexOcfBytes = vcgVertexOcfBytes(entry.mesh);
        s.edgeBytes      = qint64(entry.mesh.edge.capacity()) * sizeof(VCGEdge);
        s.faceBytes      = qint64(entry.mesh.face.capacity()) * sizeof(VCGFace);
        s.faceOcfBytes   = vcgFaceOcfBytes(entry.mesh);
        result.push_back(s);
    }
    return result;
}

Document::UndoMemoryStats Document::undoMemoryStats() const
{
    return m_undoManager->memoryStats();
}

std::vector<MeshGpuResourceCache::GpuMeshMemoryStats> Document::gpuMemoryStats() const
{
    return m_gpuCache->gpuMemoryStats();
}
