#pragma once

#include "vcgmesh.h"
#include <cstdint>
#include <memory>
#include <QString>
#include <QStringList>

class QRhi;
class QRhiBuffer;
class QRhiTexture;
class QRhiCommandBuffer;
struct MeshIOMaterialSet;

class MeshGpuResourceCache
{
public:
    enum class FillVariant : int {
        Constant = 0,
        PerVertex = 1,
        PerFace = 2,
        PerVertexQuality = 3,
        PerFaceQuality = 4,
        Texture = 5
    };

    enum class PointVariant : int {
        Constant = 0,
        PerVertex = 1,
        PerVertexQuality = 2
    };

    struct MeshSource {
        std::uint64_t meshId = 0;
        std::uint64_t geometryRevision = 0;
        std::uint64_t materialRevision = 0;
        int ioMask = 0;
        bool qualityFixedRange = false;
        float qualityRangeMin = 0.0f;
        float qualityRangeMax = 1.0f;
        const VCGMesh *mesh = nullptr;
        const QStringList *textureFilePaths = nullptr;
        const MeshIOMaterialSet *materialSet = nullptr;
    };

    struct FillBatchView {
        QRhiBuffer *vertexBuffer = nullptr;
        QRhiBuffer *indexBuffer = nullptr;
        QRhiTexture *baseColorTexture = nullptr;
        QRhiTexture *normalTexture = nullptr;
        QRhiTexture *occlusionTexture = nullptr;
        QRhiTexture *roughnessTexture = nullptr;
        QString baseColorTexturePath;
        QString normalTexturePath;
        QString occlusionTexturePath;
        QString roughnessTexturePath;
        float normalScale = 1.0f;
        float occlusionStrength = 1.0f;
        float roughnessFactor = 1.0f;
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

    struct EdgePassView {
        QRhiBuffer *vertexBuffer = nullptr;
        int vertexCount = 0;
        bool valid = false;
    };

    struct EdgeFatPassView {
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

    struct DecoratorPassView {
        QRhiBuffer *vertexNormalsBuffer = nullptr;
        int vertexNormalsVertexCount = 0;
        QRhiBuffer *faceNormalsBuffer = nullptr;
        int faceNormalsVertexCount = 0;
        QRhiBuffer *boundaryEdgesBuffer = nullptr;
        int boundaryEdgesVertexCount = 0;
        QRhiBuffer *boundaryEdgesFatBuffer = nullptr;
        int boundaryEdgesFatVertexCount = 0;
        QRhiBuffer *textureSeamsBuffer = nullptr;
        int textureSeamsVertexCount = 0;
        QRhiBuffer *textureSeamsFatBuffer = nullptr;
        int textureSeamsFatVertexCount = 0;
        bool valid = false;
    };

    struct SelectionPassView {
        QRhiBuffer *selectedFacesBuffer = nullptr;
        int selectedFacesVertexCount = 0;
        QRhiBuffer *selectedVerticesBuffer = nullptr;
        int selectedVerticesVertexCount = 0;
        bool valid = false;
    };

    struct EnsureStats {
        bool rebuiltFill = false;
        bool rebuiltWire = false;
        bool rebuiltEdges = false;
        bool rebuiltPoints = false;
        bool rebuiltBoundingBox = false;
        bool rebuiltSelection = false;
        bool rebuiltDecoratorNormals = false;
        bool rebuiltDecoratorBoundaries = false;
        bool uploadedResources = false;
        float elapsedMs = 0.0f;

        bool anyRebuilt() const
        {
            return rebuiltFill || rebuiltWire || rebuiltEdges || rebuiltPoints || rebuiltBoundingBox
                || rebuiltSelection || rebuiltDecoratorNormals || rebuiltDecoratorBoundaries;
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
                                    bool needEdges,
                                    bool needPoints,
                                    bool needBoundingBox,
                                    bool needSelection,
                                    bool needDecoratorNormals,
                                    bool needDecoratorBoundaries);

    FillPassView fillPassView(QRhi *rhi, std::uint64_t meshId, FillVariant variant) const;
    WirePassView wirePassView(QRhi *rhi, std::uint64_t meshId) const;
    EdgePassView edgePassView(QRhi *rhi, std::uint64_t meshId) const;
    EdgeFatPassView edgeFatPassView(QRhi *rhi, std::uint64_t meshId) const;
    PointsPassView pointsPassView(QRhi *rhi, std::uint64_t meshId, PointVariant variant) const;
    BBoxPassView bboxPassView(QRhi *rhi, std::uint64_t meshId) const;
    SelectionPassView selectionPassView(QRhi *rhi, std::uint64_t meshId) const;
    DecoratorPassView decoratorPassView(QRhi *rhi, std::uint64_t meshId) const;

    void purgeMesh(std::uint64_t meshId);
    void releaseRhiResources(QRhi *rhi);
    void clearAll();

private:
    struct CacheState;
    std::unique_ptr<CacheState> m_state;
};
