#pragma once

#include "vcgmesh.h"
#include <cstdint>
#include <memory>
#include <QStringList>

class QRhi;
class QRhiBuffer;
class QRhiTexture;
class QRhiCommandBuffer;

class MeshGpuResourceCache
{
public:
    enum class FillVariant : int {
        Constant = 0,
        PerVertex = 1,
        PerFace = 2
    };

    enum class PointVariant : int {
        Constant = 0,
        PerVertex = 1
    };

    struct MeshSource {
        std::uint64_t meshId = 0;
        std::uint64_t geometryRevision = 0;
        std::uint64_t materialRevision = 0;
        int ioMask = 0;
        const VCGMesh *mesh = nullptr;
        const QStringList *textureFilePaths = nullptr;
    };

    struct FillBatchView {
        QRhiBuffer *vertexBuffer = nullptr;
        QRhiBuffer *indexBuffer = nullptr;
        QRhiTexture *texture = nullptr;
        int vertexCount = 0;
        int indexCount = 0;
    };

    struct FillPassView {
        const FillBatchView *batches = nullptr;
        int batchCount = 0;
        bool valid = false;
    };

    struct WirePassView {
        QRhiBuffer *vertexBuffer = nullptr;
        int vertexCount = 0;
        bool valid = false;
    };

    struct PointsPassView {
        QRhiBuffer *vertexBuffer = nullptr;
        int vertexCount = 0;
        bool valid = false;
    };

    struct BBoxPassView {
        QRhiBuffer *vertexBuffer = nullptr;
        int vertexCount = 0;
        bool valid = false;
    };

    struct EnsureStats {
        bool rebuiltFill = false;
        bool rebuiltWire = false;
        bool rebuiltPoints = false;
        bool rebuiltBoundingBox = false;
        bool uploadedResources = false;
        float elapsedMs = 0.0f;

        bool anyRebuilt() const
        {
            return rebuiltFill || rebuiltWire || rebuiltPoints || rebuiltBoundingBox;
        }
    };

    MeshGpuResourceCache();
    ~MeshGpuResourceCache();

    EnsureStats ensureMeshResources(QRhi *rhi,
                                    QRhiCommandBuffer *cb,
                                    const MeshSource &source,
                                    FillVariant fillVariant,
                                    PointVariant pointVariant,
                                    bool needFill,
                                    bool needWire,
                                    bool needPoints,
                                    bool needBoundingBox);

    FillPassView fillPassView(QRhi *rhi, std::uint64_t meshId, FillVariant variant) const;
    WirePassView wirePassView(QRhi *rhi, std::uint64_t meshId) const;
    PointsPassView pointsPassView(QRhi *rhi, std::uint64_t meshId, PointVariant variant) const;
    BBoxPassView bboxPassView(QRhi *rhi, std::uint64_t meshId) const;

    void purgeMesh(std::uint64_t meshId);
    void releaseRhiResources(QRhi *rhi);
    void clearAll();

private:
    struct CacheState;
    std::unique_ptr<CacheState> m_state;
};
