#include "document_internal.h"
#include "documentundomanager.h"

using namespace DocumentInternal;

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

UndoMemoryStats Document::undoMemoryStats() const
{
    return m_undoManager->memoryStats();
}

std::vector<MeshGpuResourceCache::GpuMeshMemoryStats> Document::gpuMemoryStats() const
{
    return m_gpuCache->gpuMemoryStats();
}
