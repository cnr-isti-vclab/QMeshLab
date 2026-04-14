#include "meshgpuresourcecache.h"
#include "linerenderer.h"

#include <wrap/io_trimesh/io_mask.h>
#include <rhi/qrhi.h>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImageReader>
#include <QString>
#include <QVector3D>

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>

namespace {
constexpr int kFillVertexStrideFloats = 13;
constexpr int kPointsVertexStrideFloats = 11;

int fillVariantIndex(MeshGpuResourceCache::FillVariant variant)
{
    switch (variant) {
    case MeshGpuResourceCache::FillVariant::Constant: return 0;
    case MeshGpuResourceCache::FillVariant::PerVertex: return 1;
    case MeshGpuResourceCache::FillVariant::PerFace: return 2;
    case MeshGpuResourceCache::FillVariant::PerVertexQuality: return 3;
    case MeshGpuResourceCache::FillVariant::PerFaceQuality: return 4;
    case MeshGpuResourceCache::FillVariant::Texture: return 5;
    }
    return 0;
}

int pointVariantIndex(MeshGpuResourceCache::PointVariant variant)
{
    switch (variant) {
    case MeshGpuResourceCache::PointVariant::Constant: return 0;
    case MeshGpuResourceCache::PointVariant::PerVertex: return 1;
    case MeshGpuResourceCache::PointVariant::PerVertexQuality: return 2;
    }
    return 0;
}

struct QualityRange {
    float minV = 0.0f;
    float maxV = 1.0f;
    bool valid = false;
};

float normalizedQuality(float q, const QualityRange &range)
{
    if (!range.valid)
        return 0.5f;
    const float den = range.maxV - range.minV;
    if (std::abs(den) <= 1e-12f)
        return 0.5f;
    return std::clamp((q - range.minV) / den, 0.0f, 1.0f);
}

bool isPerVertexQualityFillVariant(MeshGpuResourceCache::FillVariant variant)
{
    return variant == MeshGpuResourceCache::FillVariant::PerVertexQuality;
}

bool isPerFaceQualityFillVariant(MeshGpuResourceCache::FillVariant variant)
{
    return variant == MeshGpuResourceCache::FillVariant::PerFaceQuality;
}
}

struct MeshGpuResourceCache::CacheState
{
    struct FillBatchGpu {
        std::unique_ptr<QRhiBuffer> vbuf;
        std::unique_ptr<QRhiBuffer> ibuf;
        std::unique_ptr<QRhiTexture> texture;
        int vertexCount = 0;
        int indexCount = 0;
    };

    struct FillVariantGpu {
        std::uint64_t geometryRevision = 0;
        std::uint64_t materialRevision = 0;
        bool qualityFixedRange = false;
        float qualityRangeMin = 0.0f;
        float qualityRangeMax = 1.0f;
        bool valid = false;
        std::vector<FillBatchGpu> batches;
        mutable std::vector<FillBatchView> viewBatches;
    };

    struct WireGpu {
        std::uint64_t geometryRevision = 0;
        bool valid = false;
        std::unique_ptr<QRhiBuffer> vbuf;
        int vertexCount = 0;
    };

    struct EdgeGpu {
        std::uint64_t geometryRevision = 0;
        bool valid = false;
        std::unique_ptr<QRhiBuffer> vbuf;
        int vertexCount = 0;
        std::unique_ptr<QRhiBuffer> fatVbuf;
        int fatVertexCount = 0;
    };

    struct PointsVariantGpu {
        std::uint64_t geometryRevision = 0;
        bool qualityFixedRange = false;
        float qualityRangeMin = 0.0f;
        float qualityRangeMax = 1.0f;
        bool valid = false;
        std::unique_ptr<QRhiBuffer> vbuf;
        int vertexCount = 0;
    };

    struct BBoxGpu {
        std::uint64_t geometryRevision = 0;
        bool valid = false;
        std::unique_ptr<QRhiBuffer> vbuf;
        int vertexCount = 0;
    };

    struct DecoratorNormalsGpu {
        std::uint64_t geometryRevision = 0;
        bool valid = false;
        std::unique_ptr<QRhiBuffer> vertexNormalsVbuf;
        int vertexNormalsVertexCount = 0;
        std::unique_ptr<QRhiBuffer> faceNormalsVbuf;
        int faceNormalsVertexCount = 0;
    };

    struct DecoratorBoundaryGpu {
        std::uint64_t geometryRevision = 0;
        bool valid = false;
        std::unique_ptr<QRhiBuffer> boundaryEdgesVbuf;
        int boundaryEdgesVertexCount = 0;
        std::unique_ptr<QRhiBuffer> boundaryEdgesFatVbuf;
        int boundaryEdgesFatVertexCount = 0;
        std::unique_ptr<QRhiBuffer> textureSeamsVbuf;
        int textureSeamsVertexCount = 0;
        std::unique_ptr<QRhiBuffer> textureSeamsFatVbuf;
        int textureSeamsFatVertexCount = 0;
    };

    struct MeshGpu {
        std::array<FillVariantGpu, 6> fill;
        std::array<PointsVariantGpu, 3> points;
        WireGpu wire;
        EdgeGpu edges;
        BBoxGpu bbox;
        DecoratorNormalsGpu decoratorNormals;
        DecoratorBoundaryGpu decoratorBoundaries;
    };

    std::unordered_map<QRhi *, std::unordered_map<std::uint64_t, MeshGpu>> byRhi;
};

MeshGpuResourceCache::MeshGpuResourceCache()
    : m_state(std::make_unique<CacheState>())
{
}

MeshGpuResourceCache::~MeshGpuResourceCache() = default;

MeshGpuResourceCache::EnsureStats MeshGpuResourceCache::ensureMeshResources(
    QRhi *rhi,
    QRhiCommandBuffer *cb,
    const MeshSource &source,
    FillVariant fillVariant,
    PointVariant pointVariant,
    bool needFill,
    bool needWire,
    bool needEdges,
    bool needPoints,
    bool needBoundingBox,
    bool needDecoratorNormals,
    bool needDecoratorBoundaries)
{
    EnsureStats stats;
    if (!m_state || !rhi || !cb || !source.mesh || source.meshId == 0)
        return stats;

    QElapsedTimer timer;
    timer.start();

    const VCGMesh &meshData = *source.mesh;
    static const QStringList kEmptyTexturePaths;
    const QStringList &texturePaths =
        source.textureFilePaths ? *source.textureFilePaths : kEmptyTexturePaths;
    auto &meshCache = m_state->byRhi[rhi][source.meshId];

    QRhiResourceUpdateBatch *updates = nullptr;
    auto ensureUpdates = [&]() -> QRhiResourceUpdateBatch * {
        if (!updates)
            updates = rhi->nextResourceUpdateBatch();
        return updates;
    };

    auto rebuildFillVariant =
        [&](CacheState::FillVariantGpu &dst, FillVariant variant) -> bool {
            const bool qualityVariant =
                isPerVertexQualityFillVariant(variant)
                || isPerFaceQualityFillVariant(variant);
            float requestedRangeMin = source.qualityRangeMin;
            float requestedRangeMax = source.qualityRangeMax;
            if (requestedRangeMin > requestedRangeMax)
                std::swap(requestedRangeMin, requestedRangeMax);
            const bool requestedRangeFinite =
                std::isfinite(requestedRangeMin) && std::isfinite(requestedRangeMax);
            const bool useFixedRange = qualityVariant && source.qualityFixedRange && requestedRangeFinite;

            if (dst.valid
                && dst.geometryRevision == source.geometryRevision
                && dst.materialRevision == source.materialRevision) {
                if (!qualityVariant)
                    return false;
                if (dst.qualityFixedRange == useFixedRange
                    && (!useFixedRange
                        || (dst.qualityRangeMin == requestedRangeMin
                            && dst.qualityRangeMax == requestedRangeMax))) {
                    return false;
                }
            }

            dst.valid = true;
            dst.geometryRevision = source.geometryRevision;
            dst.materialRevision = source.materialRevision;
            dst.qualityFixedRange = useFixedRange;
            dst.qualityRangeMin = requestedRangeMin;
            dst.qualityRangeMax = requestedRangeMax;
            dst.batches.clear();
            dst.viewBatches.clear();

            if (meshData.FN() <= 0)
                return true;

            const bool meshHasFaceColor = (source.ioMask & vcg::tri::io::Mask::IOM_FACECOLOR) != 0;
            const bool meshHasVertexColor = (source.ioMask & vcg::tri::io::Mask::IOM_VERTCOLOR) != 0;
            const bool meshHasVertexQuality =
                (source.ioMask & vcg::tri::io::Mask::IOM_VERTQUALITY) != 0;
            const bool meshHasFaceQuality =
                (source.ioMask & vcg::tri::io::Mask::IOM_FACEQUALITY) != 0;
            const bool meshHasVertexTexcoord =
                (source.ioMask & vcg::tri::io::Mask::IOM_VERTTEXCOORD) != 0;
            const bool meshHasWedgeTexcoord =
                (source.ioMask & vcg::tri::io::Mask::IOM_WEDGTEXCOORD) != 0;

            const bool useFaceColor = (variant == FillVariant::PerFace) && meshHasFaceColor;
            const bool useVertexColor = (variant == FillVariant::PerVertex) && meshHasVertexColor;
            const bool useVertexQuality = isPerVertexQualityFillVariant(variant) && meshHasVertexQuality;
            const bool useFaceQuality = isPerFaceQualityFillVariant(variant) && meshHasFaceQuality;
            const bool hasTextureCoords = meshHasWedgeTexcoord || meshHasVertexTexcoord;
            const bool hasTextureSlots = hasTextureCoords && !texturePaths.isEmpty();
            const bool useTextureColor = (variant == FillVariant::Texture) && hasTextureSlots;
            const bool useVertexStyleColor = useVertexColor || useVertexQuality;
            const bool useFaceStyleColor = useFaceColor || useFaceQuality;
            const bool expandTriangles = useFaceStyleColor || useTextureColor;

            QualityRange vertexQualityRange;
            if (useVertexQuality) {
                if (useFixedRange) {
                    vertexQualityRange.minV = requestedRangeMin;
                    vertexQualityRange.maxV = requestedRangeMax;
                    vertexQualityRange.valid = true;
                } else {
                    float minQ = std::numeric_limits<float>::max();
                    float maxQ = -std::numeric_limits<float>::max();
                    for (int vi = 0; vi < meshData.VN(); ++vi) {
                        const auto &v = meshData.vert[vi];
                        if (v.IsD())
                            continue;
                        const float q = static_cast<float>(v.cQ());
                        if (!std::isfinite(q))
                            continue;
                        minQ = std::min(minQ, q);
                        maxQ = std::max(maxQ, q);
                    }
                    if (minQ <= maxQ) {
                        vertexQualityRange.minV = minQ;
                        vertexQualityRange.maxV = maxQ;
                        vertexQualityRange.valid = true;
                    }
                }
            }

            QualityRange faceQualityRange;
            if (useFaceQuality) {
                if (useFixedRange) {
                    faceQualityRange.minV = requestedRangeMin;
                    faceQualityRange.maxV = requestedRangeMax;
                    faceQualityRange.valid = true;
                } else {
                    float minQ = std::numeric_limits<float>::max();
                    float maxQ = -std::numeric_limits<float>::max();
                    for (int fi = 0; fi < meshData.FN(); ++fi) {
                        const auto &f = meshData.face[fi];
                        if (f.IsD())
                            continue;
                        const float q = static_cast<float>(f.cQ());
                        if (!std::isfinite(q))
                            continue;
                        minQ = std::min(minQ, q);
                        maxQ = std::max(maxQ, q);
                    }
                    if (minQ <= maxQ) {
                        faceQualityRange.minV = minQ;
                        faceQualityRange.maxV = maxQ;
                        faceQualityRange.valid = true;
                    }
                }
            }

            if (expandTriangles) {
                struct PreparedTexture {
                    bool attempted = false;
                    bool ready = false;
                    std::unique_ptr<QRhiTexture> texture;
                    QImage image;
                };

                std::map<int, std::vector<float>> groupedTriangles;
                std::unordered_map<int, PreparedTexture> preparedTextures;

                auto ensureTexturePrepared = [&](int textureIndex) -> bool {
                    if (textureIndex < 0 || textureIndex >= texturePaths.size())
                        return false;

                    PreparedTexture &prepared = preparedTextures[textureIndex];
                    if (prepared.attempted)
                        return prepared.ready;

                    prepared.attempted = true;
                    const QString &texturePath = texturePaths.at(textureIndex);
                    if (!QFileInfo::exists(texturePath))
                        return false;

                    QImageReader reader(texturePath);
                    QImage image = reader.read();
                    if (image.isNull())
                        return false;

                    image = image.convertToFormat(QImage::Format_RGBA8888);
                    prepared.texture.reset(
                        rhi->newTexture(QRhiTexture::RGBA8, image.size(), 1));
                    if (!prepared.texture || !prepared.texture->create()) {
                        prepared.texture.reset();
                        return false;
                    }

                    prepared.image = std::move(image);
                    prepared.ready = true;
                    return true;
                };

                for (int fi = 0; fi < meshData.FN(); ++fi) {
                    const auto &f = meshData.face[fi];
                    QVector3D faceRgb(1.0f, 1.0f, 1.0f);
                    float faceQualityT = 0.5f;
                    if (useFaceColor) {
                        const auto fc = f.cC();
                        faceRgb = QVector3D(
                            static_cast<float>(fc[0]) / 255.0f,
                            static_cast<float>(fc[1]) / 255.0f,
                            static_cast<float>(fc[2]) / 255.0f);
                    } else if (useFaceQuality) {
                        const float fq = static_cast<float>(f.cQ());
                        faceQualityT = normalizedQuality(fq, faceQualityRange);
                    }

                    int textureGroup = -1;
                    bool useTextureForFace = false;
                    if (useTextureColor) {
                        int textureIndex = 0;
                        if (meshHasWedgeTexcoord) {
                            textureIndex = static_cast<int>(f.cWT(0).N());
                        } else if (meshHasVertexTexcoord) {
                            textureIndex = static_cast<int>(f.cV(0)->cT().N());
                        }

                        if (ensureTexturePrepared(textureIndex)) {
                            textureGroup = textureIndex;
                            useTextureForFace = true;
                        } else if (ensureTexturePrepared(0)) {
                            textureGroup = 0;
                            useTextureForFace = true;
                        }
                    }

                    std::vector<float> &groupData = groupedTriangles[textureGroup];
                    const int startBase = static_cast<int>(groupData.size());
                    groupData.resize(groupData.size() + (3 * kFillVertexStrideFloats));
                    for (int corner = 0; corner < 3; ++corner) {
                        const auto *vertex = f.cV(corner);
                        QVector3D vertexRgb(1.0f, 1.0f, 1.0f);
                        float vertexQualityT = 0.5f;
                        if (useVertexColor) {
                            const auto vc = vertex->cC();
                            vertexRgb = QVector3D(
                                static_cast<float>(vc[0]) / 255.0f,
                                static_cast<float>(vc[1]) / 255.0f,
                                static_cast<float>(vc[2]) / 255.0f);
                        } else if (useVertexQuality) {
                            const float vq = static_cast<float>(vertex->cQ());
                            vertexQualityT = normalizedQuality(vq, vertexQualityRange);
                        }
                        const int base = startBase + (corner * kFillVertexStrideFloats);

                        groupData[base + 0] = vertex->cP()[0];
                        groupData[base + 1] = vertex->cP()[1];
                        groupData[base + 2] = vertex->cP()[2];
                        groupData[base + 3] = vertex->cN()[0];
                        groupData[base + 4] = vertex->cN()[1];
                        groupData[base + 5] = vertex->cN()[2];
                        if (useFaceQuality) {
                            groupData[base + 6] = faceQualityT;
                            groupData[base + 7] = 0.0f;
                            groupData[base + 8] = 0.0f;
                            groupData[base + 9] = -1.0f; // quality LUT lookup
                        } else if (useVertexQuality) {
                            groupData[base + 6] = vertexQualityT;
                            groupData[base + 7] = 0.0f;
                            groupData[base + 8] = 0.0f;
                            groupData[base + 9] = -1.0f; // quality LUT lookup
                        } else {
                            groupData[base + 6] = useFaceStyleColor
                                ? faceRgb.x()
                                : (useVertexStyleColor ? vertexRgb.x() : 1.0f);
                            groupData[base + 7] = useFaceStyleColor
                                ? faceRgb.y()
                                : (useVertexStyleColor ? vertexRgb.y() : 1.0f);
                            groupData[base + 8] = useFaceStyleColor
                                ? faceRgb.z()
                                : (useVertexStyleColor ? vertexRgb.z() : 1.0f);
                            groupData[base + 9] = (useFaceStyleColor || useVertexStyleColor) ? 1.0f : 0.0f;
                        }

                        if (useTextureForFace) {
                            if (meshHasWedgeTexcoord) {
                                const auto &wt = f.cWT(corner);
                                groupData[base + 10] = wt.U();
                                groupData[base + 11] = wt.V();
                            } else if (meshHasVertexTexcoord) {
                                const auto &vt = vertex->cT();
                                groupData[base + 10] = vt.U();
                                groupData[base + 11] = vt.V();
                            } else {
                                groupData[base + 10] = 0.0f;
                                groupData[base + 11] = 0.0f;
                            }
                            groupData[base + 12] = 1.0f;
                        } else {
                            groupData[base + 10] = 0.0f;
                            groupData[base + 11] = 0.0f;
                            groupData[base + 12] = 0.0f;
                        }
                    }
                }

                for (auto &groupEntry : groupedTriangles) {
                    if (groupEntry.second.empty())
                        continue;

                    CacheState::FillBatchGpu batch;
                    batch.vertexCount =
                        static_cast<int>(groupEntry.second.size() / kFillVertexStrideFloats);
                    batch.indexCount = 0;
                    batch.vbuf.reset(
                        rhi->newBuffer(
                            QRhiBuffer::Immutable,
                            QRhiBuffer::VertexBuffer,
                            static_cast<quint32>(groupEntry.second.size() * sizeof(float))));
                    if (!batch.vbuf || !batch.vbuf->create()) {
                        batch.vbuf.reset();
                        continue;
                    }
                    ensureUpdates()->uploadStaticBuffer(batch.vbuf.get(), groupEntry.second.data());

                    QImage textureUploadImage;
                    if (groupEntry.first >= 0) {
                        auto it = preparedTextures.find(groupEntry.first);
                        if (it != preparedTextures.end() && it->second.ready) {
                            batch.texture = std::move(it->second.texture);
                            textureUploadImage = std::move(it->second.image);
                        }
                    }

                    if (batch.texture && !textureUploadImage.isNull()) {
                        QRhiTextureUploadEntry textureEntry(
                            0, 0, QRhiTextureSubresourceUploadDescription(textureUploadImage));
                        ensureUpdates()->uploadTexture(
                            batch.texture.get(),
                            QRhiTextureUploadDescription({ textureEntry }));
                    }

                    dst.batches.push_back(std::move(batch));
                }
            } else {
                CacheState::FillBatchGpu batch;
                const int vertexCount = meshData.VN();
                const int indexCount = meshData.FN() * 3;
                if (vertexCount <= 0 || indexCount <= 0)
                    return true;

                std::vector<float> vdata(vertexCount * kFillVertexStrideFloats);
                const float useMeshColor = useVertexStyleColor ? 1.0f : 0.0f;
                for (int vi = 0; vi < vertexCount; ++vi) {
                    const auto &v = meshData.vert[vi];
                    QVector3D vertexRgb(1.0f, 1.0f, 1.0f);
                    float vertexQualityT = 0.5f;
                    if (useVertexColor) {
                        const auto vc = v.cC();
                        vertexRgb = QVector3D(
                            static_cast<float>(vc[0]) / 255.0f,
                            static_cast<float>(vc[1]) / 255.0f,
                            static_cast<float>(vc[2]) / 255.0f);
                    } else if (useVertexQuality) {
                        const float vq = static_cast<float>(v.cQ());
                        vertexQualityT = normalizedQuality(vq, vertexQualityRange);
                    }
                    const int base = vi * kFillVertexStrideFloats;
                    vdata[base + 0] = v.cP()[0];
                    vdata[base + 1] = v.cP()[1];
                    vdata[base + 2] = v.cP()[2];
                    vdata[base + 3] = v.cN()[0];
                    vdata[base + 4] = v.cN()[1];
                    vdata[base + 5] = v.cN()[2];
                    if (useVertexQuality) {
                        vdata[base + 6] = vertexQualityT;
                        vdata[base + 7] = 0.0f;
                        vdata[base + 8] = 0.0f;
                        vdata[base + 9] = -1.0f; // quality LUT lookup
                    } else {
                        vdata[base + 6] = vertexRgb.x();
                        vdata[base + 7] = vertexRgb.y();
                        vdata[base + 8] = vertexRgb.z();
                        vdata[base + 9] = useMeshColor;
                    }
                    vdata[base + 10] = 0.0f;
                    vdata[base + 11] = 0.0f;
                    vdata[base + 12] = 0.0f;
                }

                std::vector<quint32> idata(indexCount);
                for (int fi = 0; fi < meshData.FN(); ++fi) {
                    const auto &f = meshData.face[fi];
                    idata[fi * 3 + 0] = static_cast<quint32>(vcg::tri::Index(meshData, f.cV(0)));
                    idata[fi * 3 + 1] = static_cast<quint32>(vcg::tri::Index(meshData, f.cV(1)));
                    idata[fi * 3 + 2] = static_cast<quint32>(vcg::tri::Index(meshData, f.cV(2)));
                }

                batch.vbuf.reset(
                    rhi->newBuffer(
                        QRhiBuffer::Immutable,
                        QRhiBuffer::VertexBuffer,
                        static_cast<quint32>(vdata.size() * sizeof(float))));
                batch.ibuf.reset(
                    rhi->newBuffer(
                        QRhiBuffer::Immutable,
                        QRhiBuffer::IndexBuffer,
                        static_cast<quint32>(idata.size() * sizeof(quint32))));
                    if (!batch.vbuf || !batch.ibuf || !batch.vbuf->create() || !batch.ibuf->create()) {
                        batch.vbuf.reset();
                        batch.ibuf.reset();
                        return true;
                    }

                ensureUpdates()->uploadStaticBuffer(batch.vbuf.get(), vdata.data());
                ensureUpdates()->uploadStaticBuffer(batch.ibuf.get(), idata.data());
                batch.vertexCount = vertexCount;
                batch.indexCount = indexCount;
                dst.batches.push_back(std::move(batch));
            }
            return true;
        };

    auto rebuildWire = [&](CacheState::WireGpu &dst) -> bool {
        if (dst.valid && dst.geometryRevision == source.geometryRevision)
            return false;

        dst.valid = true;
        dst.geometryRevision = source.geometryRevision;
        dst.vbuf.reset();
        dst.vertexCount = 0;

        if (meshData.FN() <= 0)
            return true;

        const int vertexCount = meshData.FN() * 3;
        std::vector<float> vdata(vertexCount * 6);
        static constexpr float barycentrics[3][3] = {
            { 1.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f }
        };
        for (int fi = 0; fi < meshData.FN(); ++fi) {
            const auto &f = meshData.face[fi];
            for (int corner = 0; corner < 3; ++corner) {
                const auto *vertex = f.cV(corner);
                const int base = (fi * 3 + corner) * 6;
                vdata[base + 0] = vertex->cP()[0];
                vdata[base + 1] = vertex->cP()[1];
                vdata[base + 2] = vertex->cP()[2];
                vdata[base + 3] = barycentrics[corner][0];
                vdata[base + 4] = barycentrics[corner][1];
                vdata[base + 5] = barycentrics[corner][2];
            }
        }

        dst.vbuf.reset(
            rhi->newBuffer(
                QRhiBuffer::Immutable,
                QRhiBuffer::VertexBuffer,
                static_cast<quint32>(vdata.size() * sizeof(float))));
        if (!dst.vbuf || !dst.vbuf->create()) {
            dst.vbuf.reset();
            return true;
        }

        ensureUpdates()->uploadStaticBuffer(dst.vbuf.get(), vdata.data());
        dst.vertexCount = vertexCount;
        return true;
    };

    auto rebuildPointsVariant =
        [&](CacheState::PointsVariantGpu &dst, PointVariant variant) -> bool {
            const bool qualityVariant = (variant == PointVariant::PerVertexQuality);
            float requestedRangeMin = source.qualityRangeMin;
            float requestedRangeMax = source.qualityRangeMax;
            if (requestedRangeMin > requestedRangeMax)
                std::swap(requestedRangeMin, requestedRangeMax);
            const bool requestedRangeFinite =
                std::isfinite(requestedRangeMin) && std::isfinite(requestedRangeMax);
            const bool useFixedRange = qualityVariant && source.qualityFixedRange && requestedRangeFinite;

            if (dst.valid && dst.geometryRevision == source.geometryRevision) {
                if (!qualityVariant)
                    return false;
                if (dst.qualityFixedRange == useFixedRange
                    && (!useFixedRange
                        || (dst.qualityRangeMin == requestedRangeMin
                            && dst.qualityRangeMax == requestedRangeMax))) {
                    return false;
                }
            }

            dst.valid = true;
            dst.geometryRevision = source.geometryRevision;
            dst.qualityFixedRange = useFixedRange;
            dst.qualityRangeMin = requestedRangeMin;
            dst.qualityRangeMax = requestedRangeMax;
            dst.vbuf.reset();
            dst.vertexCount = 0;

            if (meshData.VN() <= 0)
                return true;

            const bool meshHasVertexColor = (source.ioMask & vcg::tri::io::Mask::IOM_VERTCOLOR) != 0;
            const bool meshHasVertexQuality =
                (source.ioMask & vcg::tri::io::Mask::IOM_VERTQUALITY) != 0;
            const bool meshHasVertexNormal =
                (source.ioMask & vcg::tri::io::Mask::IOM_VERTNORMAL) != 0;
            const bool useVertexColor = (variant == PointVariant::PerVertex) && meshHasVertexColor;
            const bool useVertexQuality =
                (variant == PointVariant::PerVertexQuality) && meshHasVertexQuality;
            const bool useVertexStyleColor = useVertexColor || useVertexQuality;
            const float useMeshColor = useVertexStyleColor ? 1.0f : 0.0f;

            QualityRange vertexQualityRange;
            if (useVertexQuality) {
                if (useFixedRange) {
                    vertexQualityRange.minV = requestedRangeMin;
                    vertexQualityRange.maxV = requestedRangeMax;
                    vertexQualityRange.valid = true;
                } else {
                    float minQ = std::numeric_limits<float>::max();
                    float maxQ = -std::numeric_limits<float>::max();
                    for (int vi = 0; vi < meshData.VN(); ++vi) {
                        const auto &v = meshData.vert[vi];
                        if (v.IsD())
                            continue;
                        const float q = static_cast<float>(v.cQ());
                        if (!std::isfinite(q))
                            continue;
                        minQ = std::min(minQ, q);
                        maxQ = std::max(maxQ, q);
                    }
                    if (minQ <= maxQ) {
                        vertexQualityRange.minV = minQ;
                        vertexQualityRange.maxV = maxQ;
                        vertexQualityRange.valid = true;
                    }
                }
            }

            std::vector<float> pdata(meshData.VN() * kPointsVertexStrideFloats);
            for (int vi = 0; vi < meshData.VN(); ++vi) {
                const auto &v = meshData.vert[vi];
                QVector3D vertexRgb(1.0f, 1.0f, 1.0f);
                float vertexQualityT = 0.5f;
                if (useVertexColor) {
                    const auto vc = v.cC();
                    vertexRgb = QVector3D(
                        static_cast<float>(vc[0]) / 255.0f,
                        static_cast<float>(vc[1]) / 255.0f,
                        static_cast<float>(vc[2]) / 255.0f);
                } else if (useVertexQuality) {
                    const float vq = static_cast<float>(v.cQ());
                    vertexQualityT = normalizedQuality(vq, vertexQualityRange);
                }
                const int base = vi * kPointsVertexStrideFloats;
                pdata[base + 0] = v.cP()[0];
                pdata[base + 1] = v.cP()[1];
                pdata[base + 2] = v.cP()[2];
                if (useVertexQuality) {
                    pdata[base + 3] = vertexQualityT;
                    pdata[base + 4] = 0.0f;
                    pdata[base + 5] = 0.0f;
                    pdata[base + 6] = -1.0f; // quality LUT lookup
                } else {
                    pdata[base + 3] = vertexRgb.x();
                    pdata[base + 4] = vertexRgb.y();
                    pdata[base + 5] = vertexRgb.z();
                    pdata[base + 6] = useMeshColor;
                }
                const float nx = v.cN()[0];
                const float ny = v.cN()[1];
                const float nz = v.cN()[2];
                const float nLen2 = nx * nx + ny * ny + nz * nz;
                const bool normalFinite = std::isfinite(nx) && std::isfinite(ny) && std::isfinite(nz);
                const bool hasUsableNormal =
                    meshHasVertexNormal && normalFinite && nLen2 > 1e-12f && nLen2 < 1e12f;
                pdata[base + 7] = hasUsableNormal ? nx : 0.0f;
                pdata[base + 8] = hasUsableNormal ? ny : 0.0f;
                pdata[base + 9] = hasUsableNormal ? nz : 1.0f;
                pdata[base + 10] = hasUsableNormal ? 1.0f : 0.0f;
            }

            dst.vbuf.reset(
                rhi->newBuffer(
                    QRhiBuffer::Immutable,
                    QRhiBuffer::VertexBuffer,
                    static_cast<quint32>(pdata.size() * sizeof(float))));
            if (!dst.vbuf || !dst.vbuf->create()) {
                dst.vbuf.reset();
                return true;
            }
            ensureUpdates()->uploadStaticBuffer(dst.vbuf.get(), pdata.data());
            dst.vertexCount = meshData.VN();
            return true;
        };

    auto rebuildEdges = [&](CacheState::EdgeGpu &dst) -> bool {
        if (dst.valid && dst.geometryRevision == source.geometryRevision)
            return false;

        dst.valid = true;
        dst.geometryRevision = source.geometryRevision;
        dst.vbuf.reset();
        dst.vertexCount = 0;
        dst.fatVbuf.reset();
        dst.fatVertexCount = 0;

        if (meshData.EN() <= 0)
            return true;

        std::vector<float> vdata;
        std::vector<float> fatVdata;
        vdata.reserve(static_cast<size_t>(meshData.EN()) * 6);
        fatVdata.reserve(static_cast<size_t>(meshData.EN()) * 6 * LineRenderer::kFatLineStrideFloats);
        for (int ei = 0; ei < meshData.EN(); ++ei) {
            const auto &e = meshData.edge[ei];
            if (e.IsD())
                continue;
            const auto *v0 = e.cV(0);
            const auto *v1 = e.cV(1);
            if (!v0 || !v1)
                continue;
            const auto &p0 = v0->cP();
            const auto &p1 = v1->cP();
            vdata.push_back(p0[0]);
            vdata.push_back(p0[1]);
            vdata.push_back(p0[2]);
            vdata.push_back(p1[0]);
            vdata.push_back(p1[1]);
            vdata.push_back(p1[2]);
            LineRenderer::appendFatLineSegmentVertices(
                fatVdata, p0[0], p0[1], p0[2], p1[0], p1[1], p1[2]);
        }

        if (vdata.empty())
            return true;

        dst.vbuf.reset(
            rhi->newBuffer(
                QRhiBuffer::Immutable,
                QRhiBuffer::VertexBuffer,
                static_cast<quint32>(vdata.size() * sizeof(float))));
        if (!dst.vbuf || !dst.vbuf->create()) {
            dst.vbuf.reset();
            return true;
        }

        ensureUpdates()->uploadStaticBuffer(dst.vbuf.get(), vdata.data());
        dst.vertexCount = static_cast<int>(vdata.size() / 3);

        if (!fatVdata.empty()) {
            dst.fatVbuf.reset(
                rhi->newBuffer(
                    QRhiBuffer::Immutable,
                    QRhiBuffer::VertexBuffer,
                    static_cast<quint32>(fatVdata.size() * sizeof(float))));
            if (!dst.fatVbuf || !dst.fatVbuf->create()) {
                dst.fatVbuf.reset();
            } else {
                ensureUpdates()->uploadStaticBuffer(dst.fatVbuf.get(), fatVdata.data());
                dst.fatVertexCount =
                    static_cast<int>(fatVdata.size() / LineRenderer::kFatLineStrideFloats);
            }
        }
        return true;
    };

    auto rebuildBBox = [&](CacheState::BBoxGpu &dst) -> bool {
        if (dst.valid && dst.geometryRevision == source.geometryRevision)
            return false;

        dst.valid = true;
        dst.geometryRevision = source.geometryRevision;
        dst.vbuf.reset();
        dst.vertexCount = 0;

        if (meshData.bbox.IsNull())
            return true;

        const auto &mn = meshData.bbox.min;
        const auto &mx = meshData.bbox.max;
        std::vector<float> bd = {
            mn[0],mn[1],mn[2],  mx[0],mn[1],mn[2],
            mx[0],mn[1],mn[2],  mx[0],mx[1],mn[2],
            mx[0],mx[1],mn[2],  mn[0],mx[1],mn[2],
            mn[0],mx[1],mn[2],  mn[0],mn[1],mn[2],
            mn[0],mn[1],mx[2],  mx[0],mn[1],mx[2],
            mx[0],mn[1],mx[2],  mx[0],mx[1],mx[2],
            mx[0],mx[1],mx[2],  mn[0],mx[1],mx[2],
            mn[0],mx[1],mx[2],  mn[0],mn[1],mx[2],
            mn[0],mn[1],mn[2],  mn[0],mn[1],mx[2],
            mx[0],mn[1],mn[2],  mx[0],mn[1],mx[2],
            mx[0],mx[1],mn[2],  mx[0],mx[1],mx[2],
            mn[0],mx[1],mn[2],  mn[0],mx[1],mx[2],
        };

        dst.vbuf.reset(
            rhi->newBuffer(
                QRhiBuffer::Immutable,
                QRhiBuffer::VertexBuffer,
                static_cast<quint32>(bd.size() * sizeof(float))));
        if (!dst.vbuf || !dst.vbuf->create()) {
            dst.vbuf.reset();
            return true;
        }
        ensureUpdates()->uploadStaticBuffer(dst.vbuf.get(), bd.data());
        dst.vertexCount = 24;
        return true;
    };

    auto uploadLineBuffer = [&](const std::vector<float> &lineData,
                                std::unique_ptr<QRhiBuffer> &dstBuffer,
                                int &dstVertexCount) {
        dstBuffer.reset();
        dstVertexCount = 0;
        if (lineData.empty())
            return;

        dstBuffer.reset(
            rhi->newBuffer(
                QRhiBuffer::Immutable,
                QRhiBuffer::VertexBuffer,
                static_cast<quint32>(lineData.size() * sizeof(float))));
        if (!dstBuffer || !dstBuffer->create()) {
            dstBuffer.reset();
            return;
        }

        ensureUpdates()->uploadStaticBuffer(dstBuffer.get(), lineData.data());
        dstVertexCount = static_cast<int>(lineData.size() / 3);
    };

    auto uploadFatLineBuffer = [&](const std::vector<float> &lineData,
                                   std::unique_ptr<QRhiBuffer> &dstBuffer,
                                   int &dstVertexCount) {
        dstBuffer.reset();
        dstVertexCount = 0;
        if (lineData.size() < LineRenderer::kLineStrideFloats)
            return;

        std::vector<float> fatData = LineRenderer::buildFatLineVertices(lineData);
        if (fatData.empty())
            return;

        dstBuffer.reset(
            rhi->newBuffer(
                QRhiBuffer::Immutable,
                QRhiBuffer::VertexBuffer,
                static_cast<quint32>(fatData.size() * sizeof(float))));
        if (!dstBuffer || !dstBuffer->create()) {
            dstBuffer.reset();
            return;
        }

        ensureUpdates()->uploadStaticBuffer(dstBuffer.get(), fatData.data());
        dstVertexCount = static_cast<int>(fatData.size() / LineRenderer::kFatLineStrideFloats);
    };

    auto rebuildDecoratorNormals = [&](CacheState::DecoratorNormalsGpu &dst) -> bool {
        if (dst.valid && dst.geometryRevision == source.geometryRevision)
            return false;

        dst.valid = true;
        dst.geometryRevision = source.geometryRevision;
        dst.vertexNormalsVbuf.reset();
        dst.vertexNormalsVertexCount = 0;
        dst.faceNormalsVbuf.reset();
        dst.faceNormalsVertexCount = 0;

        if (meshData.VN() <= 0)
            return true;

        const float diag = meshData.bbox.Diag();
        const float normalLength = std::max(1e-4f, diag * 0.02f);

        std::vector<float> vertexNormalLines;
        vertexNormalLines.reserve(static_cast<size_t>(meshData.VN()) * 6);
        for (int vi = 0; vi < meshData.VN(); ++vi) {
            const auto &v = meshData.vert[vi];
            const float nx = v.cN()[0];
            const float ny = v.cN()[1];
            const float nz = v.cN()[2];
            const bool finite =
                std::isfinite(nx) && std::isfinite(ny) && std::isfinite(nz);
            const float nLen2 = nx * nx + ny * ny + nz * nz;
            if (!finite || nLen2 < 1e-12f || nLen2 > 1e12f)
                continue;

            const float invLen = 1.0f / std::sqrt(nLen2);
            const float dx = nx * invLen * normalLength;
            const float dy = ny * invLen * normalLength;
            const float dz = nz * invLen * normalLength;
            const float px = v.cP()[0];
            const float py = v.cP()[1];
            const float pz = v.cP()[2];
            vertexNormalLines.push_back(px);
            vertexNormalLines.push_back(py);
            vertexNormalLines.push_back(pz);
            vertexNormalLines.push_back(px + dx);
            vertexNormalLines.push_back(py + dy);
            vertexNormalLines.push_back(pz + dz);
        }
        uploadLineBuffer(
            vertexNormalLines, dst.vertexNormalsVbuf, dst.vertexNormalsVertexCount);

        std::vector<float> faceNormalLines;
        faceNormalLines.reserve(static_cast<size_t>(meshData.FN()) * 6);
        for (int fi = 0; fi < meshData.FN(); ++fi) {
            const auto &f = meshData.face[fi];
            const auto *v0 = f.cV(0);
            const auto *v1 = f.cV(1);
            const auto *v2 = f.cV(2);
            const float cx = (v0->cP()[0] + v1->cP()[0] + v2->cP()[0]) / 3.0f;
            const float cy = (v0->cP()[1] + v1->cP()[1] + v2->cP()[1]) / 3.0f;
            const float cz = (v0->cP()[2] + v1->cP()[2] + v2->cP()[2]) / 3.0f;

            float nx = f.cN()[0];
            float ny = f.cN()[1];
            float nz = f.cN()[2];
            const float nLen2 = nx * nx + ny * ny + nz * nz;
            if (!(std::isfinite(nx) && std::isfinite(ny) && std::isfinite(nz))
                || nLen2 < 1e-12f || nLen2 > 1e12f) {
                const float e1x = v1->cP()[0] - v0->cP()[0];
                const float e1y = v1->cP()[1] - v0->cP()[1];
                const float e1z = v1->cP()[2] - v0->cP()[2];
                const float e2x = v2->cP()[0] - v0->cP()[0];
                const float e2y = v2->cP()[1] - v0->cP()[1];
                const float e2z = v2->cP()[2] - v0->cP()[2];
                nx = e1y * e2z - e1z * e2y;
                ny = e1z * e2x - e1x * e2z;
                nz = e1x * e2y - e1y * e2x;
            }
            const float finalLen2 = nx * nx + ny * ny + nz * nz;
            if (finalLen2 < 1e-12f || finalLen2 > 1e12f)
                continue;
            const float invLen = 1.0f / std::sqrt(finalLen2);
            const float dx = nx * invLen * normalLength;
            const float dy = ny * invLen * normalLength;
            const float dz = nz * invLen * normalLength;
            faceNormalLines.push_back(cx);
            faceNormalLines.push_back(cy);
            faceNormalLines.push_back(cz);
            faceNormalLines.push_back(cx + dx);
            faceNormalLines.push_back(cy + dy);
            faceNormalLines.push_back(cz + dz);
        }
        uploadLineBuffer(faceNormalLines, dst.faceNormalsVbuf, dst.faceNormalsVertexCount);
        return true;
    };

    auto rebuildDecoratorBoundaries = [&](CacheState::DecoratorBoundaryGpu &dst) -> bool {
        if (dst.valid && dst.geometryRevision == source.geometryRevision)
            return false;

        dst.valid = true;
        dst.geometryRevision = source.geometryRevision;
        dst.boundaryEdgesVbuf.reset();
        dst.boundaryEdgesVertexCount = 0;
        dst.boundaryEdgesFatVbuf.reset();
        dst.boundaryEdgesFatVertexCount = 0;
        dst.textureSeamsVbuf.reset();
        dst.textureSeamsVertexCount = 0;
        dst.textureSeamsFatVbuf.reset();
        dst.textureSeamsFatVertexCount = 0;

        if (meshData.VN() <= 0)
            return true;

        std::vector<float> boundaryEdgeLines;
        std::vector<float> textureSeamLines;
        boundaryEdgeLines.reserve(static_cast<size_t>(meshData.FN()) * 6);
        textureSeamLines.reserve(static_cast<size_t>(meshData.FN()) * 6);

        const bool hasWedgeTex =
            (source.ioMask & vcg::tri::io::Mask::IOM_WEDGTEXCOORD) != 0;
        const bool hasVertTex =
            !hasWedgeTex && (source.ioMask & vcg::tri::io::Mask::IOM_VERTTEXCOORD) != 0;
        const bool hasTexCoords = hasWedgeTex || hasVertTex;

        struct EdgeUvSample {
            float u0 = 0.0f;
            float v0 = 0.0f;
            float u1 = 0.0f;
            float v1 = 0.0f;
            int tex0 = 0;
            int tex1 = 0;
        };
        struct EdgeAccum {
            std::array<float, 3> p0 { 0.0f, 0.0f, 0.0f };
            std::array<float, 3> p1 { 0.0f, 0.0f, 0.0f };
            int incidentCount = 0;
            std::vector<EdgeUvSample> uvSamples;
        };

        auto makeEdgeKey = [](int a, int b) -> std::uint64_t {
            return (std::uint64_t(std::uint32_t(a)) << 32) | std::uint64_t(std::uint32_t(b));
        };
        auto readCornerUv = [&](const VCGFace &f, int corner, float &u, float &v, int &texId) -> bool {
            if (hasWedgeTex) {
                const auto &wt = f.cWT(corner);
                u = wt.U();
                v = wt.V();
                texId = int(wt.N());
                return std::isfinite(u) && std::isfinite(v);
            }
            if (hasVertTex) {
                const auto *vv = f.cV(corner);
                if (!vv)
                    return false;
                const auto &vt = vv->cT();
                u = vt.U();
                v = vt.V();
                texId = int(vt.N());
                return std::isfinite(u) && std::isfinite(v);
            }
            return false;
        };
        auto uvSampleDiffers = [](const EdgeUvSample &a, const EdgeUvSample &b) {
            constexpr float kUvEps = 1e-6f;
            auto diff = [](float x, float y) {
                return std::abs(x - y) > kUvEps;
            };
            return a.tex0 != b.tex0
                || a.tex1 != b.tex1
                || diff(a.u0, b.u0)
                || diff(a.v0, b.v0)
                || diff(a.u1, b.u1)
                || diff(a.v1, b.v1);
        };

        std::unordered_map<std::uint64_t, EdgeAccum> edges;
        edges.reserve(static_cast<size_t>(meshData.FN()) * 3);

        for (int fi = 0; fi < meshData.FN(); ++fi) {
            const auto &f = meshData.face[fi];
            if (f.IsD())
                continue;
            for (int i = 0; i < 3; ++i) {
                const int next = (i + 1) % 3;
                const auto *v0 = f.cV(i);
                const auto *v1 = f.cV(next);
                if (!v0 || !v1)
                    continue;

                int a = vcg::tri::Index(meshData, v0);
                int b = vcg::tri::Index(meshData, v1);
                if (a < 0 || b < 0 || a == b)
                    continue;

                bool swapped = false;
                std::array<float, 3> p0 = { v0->cP()[0], v0->cP()[1], v0->cP()[2] };
                std::array<float, 3> p1 = { v1->cP()[0], v1->cP()[1], v1->cP()[2] };
                if (a > b) {
                    std::swap(a, b);
                    std::swap(p0, p1);
                    swapped = true;
                }

                EdgeAccum &acc = edges[makeEdgeKey(a, b)];
                if (acc.incidentCount == 0) {
                    acc.p0 = p0;
                    acc.p1 = p1;
                }
                ++acc.incidentCount;

                if (!hasTexCoords)
                    continue;

                float u0 = 0.0f;
                float v0uv = 0.0f;
                float u1 = 0.0f;
                float v1uv = 0.0f;
                int t0 = 0;
                int t1 = 0;
                if (!readCornerUv(f, i, u0, v0uv, t0) || !readCornerUv(f, next, u1, v1uv, t1))
                    continue;
                if (swapped) {
                    std::swap(u0, u1);
                    std::swap(v0uv, v1uv);
                    std::swap(t0, t1);
                }
                acc.uvSamples.push_back({ u0, v0uv, u1, v1uv, t0, t1 });
            }
        }

        for (const auto &kv : edges) {
            const EdgeAccum &acc = kv.second;
            if (acc.incidentCount == 1) {
                boundaryEdgeLines.push_back(acc.p0[0]);
                boundaryEdgeLines.push_back(acc.p0[1]);
                boundaryEdgeLines.push_back(acc.p0[2]);
                boundaryEdgeLines.push_back(acc.p1[0]);
                boundaryEdgeLines.push_back(acc.p1[1]);
                boundaryEdgeLines.push_back(acc.p1[2]);
            }

            if (!hasTexCoords)
                continue;

            bool isTextureBorder = false;
            if (acc.incidentCount == 1) {
                // Keep previous behavior: texture-border also includes geometric boundaries
                // when texture coordinates are available.
                isTextureBorder = !acc.uvSamples.empty();
            } else if (acc.uvSamples.size() < static_cast<size_t>(acc.incidentCount)) {
                // Missing/invalid UVs on at least one incident face -> treat as seam.
                isTextureBorder = true;
            } else if (!acc.uvSamples.empty()) {
                const EdgeUvSample &ref = acc.uvSamples.front();
                for (size_t si = 1; si < acc.uvSamples.size(); ++si) {
                    if (uvSampleDiffers(acc.uvSamples[si], ref)) {
                        isTextureBorder = true;
                        break;
                    }
                }
            }

            if (!isTextureBorder)
                continue;

            textureSeamLines.push_back(acc.p0[0]);
            textureSeamLines.push_back(acc.p0[1]);
            textureSeamLines.push_back(acc.p0[2]);
            textureSeamLines.push_back(acc.p1[0]);
            textureSeamLines.push_back(acc.p1[1]);
            textureSeamLines.push_back(acc.p1[2]);
        }

        uploadLineBuffer(
            boundaryEdgeLines, dst.boundaryEdgesVbuf, dst.boundaryEdgesVertexCount);
        uploadFatLineBuffer(
            boundaryEdgeLines, dst.boundaryEdgesFatVbuf, dst.boundaryEdgesFatVertexCount);
        uploadLineBuffer(
            textureSeamLines, dst.textureSeamsVbuf, dst.textureSeamsVertexCount);
        uploadFatLineBuffer(
            textureSeamLines, dst.textureSeamsFatVbuf, dst.textureSeamsFatVertexCount);
        return true;
    };

    if (needFill) {
        auto &fill = meshCache.fill[fillVariantIndex(fillVariant)];
        stats.rebuiltFill = rebuildFillVariant(fill, fillVariant);
    }
    if (needWire)
        stats.rebuiltWire = rebuildWire(meshCache.wire);
    if (needEdges)
        stats.rebuiltEdges = rebuildEdges(meshCache.edges);
    if (needPoints) {
        auto &points = meshCache.points[pointVariantIndex(pointVariant)];
        stats.rebuiltPoints = rebuildPointsVariant(points, pointVariant);
    }
    if (needBoundingBox)
        stats.rebuiltBoundingBox = rebuildBBox(meshCache.bbox);
    if (needDecoratorNormals)
        stats.rebuiltDecoratorNormals = rebuildDecoratorNormals(meshCache.decoratorNormals);
    if (needDecoratorBoundaries)
        stats.rebuiltDecoratorBoundaries =
            rebuildDecoratorBoundaries(meshCache.decoratorBoundaries);

    if (updates) {
        stats.uploadedResources = true;
        cb->resourceUpdate(updates);
    }

    stats.elapsedMs = float(timer.nsecsElapsed() / 1000000.0);
    return stats;
}

MeshGpuResourceCache::FillPassView MeshGpuResourceCache::fillPassView(
    QRhi *rhi, std::uint64_t meshId, FillVariant variant) const
{
    if (!m_state || !rhi || meshId == 0)
        return {};

    const auto rhiIt = m_state->byRhi.find(rhi);
    if (rhiIt == m_state->byRhi.end())
        return {};
    const auto meshIt = rhiIt->second.find(meshId);
    if (meshIt == rhiIt->second.end())
        return {};

    const CacheState::FillVariantGpu &fill = meshIt->second.fill[fillVariantIndex(variant)];
    if (!fill.valid)
        return {};

    fill.viewBatches.clear();
    fill.viewBatches.reserve(fill.batches.size());
    for (const auto &batch : fill.batches) {
        fill.viewBatches.push_back({
            batch.vbuf.get(),
            batch.ibuf.get(),
            batch.texture.get(),
            batch.vertexCount,
            batch.indexCount
        });
    }

    FillPassView view;
    view.valid = true;
    view.batchCount = static_cast<int>(fill.viewBatches.size());
    view.batches = fill.viewBatches.empty() ? nullptr : fill.viewBatches.data();
    return view;
}

MeshGpuResourceCache::WirePassView MeshGpuResourceCache::wirePassView(QRhi *rhi, std::uint64_t meshId) const
{
    if (!m_state || !rhi || meshId == 0)
        return {};

    const auto rhiIt = m_state->byRhi.find(rhi);
    if (rhiIt == m_state->byRhi.end())
        return {};
    const auto meshIt = rhiIt->second.find(meshId);
    if (meshIt == rhiIt->second.end())
        return {};

    const auto &wire = meshIt->second.wire;
    if (!wire.valid)
        return {};

    return { wire.vbuf.get(), wire.vertexCount, true };
}

MeshGpuResourceCache::EdgePassView MeshGpuResourceCache::edgePassView(
    QRhi *rhi, std::uint64_t meshId) const
{
    if (!m_state || !rhi || meshId == 0)
        return {};

    const auto rhiIt = m_state->byRhi.find(rhi);
    if (rhiIt == m_state->byRhi.end())
        return {};
    const auto meshIt = rhiIt->second.find(meshId);
    if (meshIt == rhiIt->second.end())
        return {};

    const auto &edges = meshIt->second.edges;
    if (!edges.valid)
        return {};

    return { edges.vbuf.get(), edges.vertexCount, true };
}

MeshGpuResourceCache::EdgeFatPassView MeshGpuResourceCache::edgeFatPassView(
    QRhi *rhi, std::uint64_t meshId) const
{
    if (!m_state || !rhi || meshId == 0)
        return {};

    const auto rhiIt = m_state->byRhi.find(rhi);
    if (rhiIt == m_state->byRhi.end())
        return {};
    const auto meshIt = rhiIt->second.find(meshId);
    if (meshIt == rhiIt->second.end())
        return {};

    const auto &edges = meshIt->second.edges;
    if (!edges.valid)
        return {};

    const bool hasFatBuffer =
        edges.fatVbuf && edges.fatVertexCount > 0;
    return { edges.fatVbuf.get(), edges.fatVertexCount, hasFatBuffer };
}

MeshGpuResourceCache::PointsPassView MeshGpuResourceCache::pointsPassView(
    QRhi *rhi, std::uint64_t meshId, PointVariant variant) const
{
    if (!m_state || !rhi || meshId == 0)
        return {};

    const auto rhiIt = m_state->byRhi.find(rhi);
    if (rhiIt == m_state->byRhi.end())
        return {};
    const auto meshIt = rhiIt->second.find(meshId);
    if (meshIt == rhiIt->second.end())
        return {};

    const auto &points = meshIt->second.points[pointVariantIndex(variant)];
    if (!points.valid)
        return {};

    return { points.vbuf.get(), points.vertexCount, true };
}

MeshGpuResourceCache::BBoxPassView MeshGpuResourceCache::bboxPassView(QRhi *rhi, std::uint64_t meshId) const
{
    if (!m_state || !rhi || meshId == 0)
        return {};

    const auto rhiIt = m_state->byRhi.find(rhi);
    if (rhiIt == m_state->byRhi.end())
        return {};
    const auto meshIt = rhiIt->second.find(meshId);
    if (meshIt == rhiIt->second.end())
        return {};

    const auto &bbox = meshIt->second.bbox;
    if (!bbox.valid)
        return {};

    return { bbox.vbuf.get(), bbox.vertexCount, true };
}

MeshGpuResourceCache::DecoratorPassView MeshGpuResourceCache::decoratorPassView(
    QRhi *rhi, std::uint64_t meshId) const
{
    if (!m_state || !rhi || meshId == 0)
        return {};

    const auto rhiIt = m_state->byRhi.find(rhi);
    if (rhiIt == m_state->byRhi.end())
        return {};
    const auto meshIt = rhiIt->second.find(meshId);
    if (meshIt == rhiIt->second.end())
        return {};

    const auto &normalDecor = meshIt->second.decoratorNormals;
    const auto &boundaryDecor = meshIt->second.decoratorBoundaries;
    if (!normalDecor.valid && !boundaryDecor.valid)
        return {};

    DecoratorPassView view;
    view.valid = true;
    view.vertexNormalsBuffer = normalDecor.vertexNormalsVbuf.get();
    view.vertexNormalsVertexCount = normalDecor.vertexNormalsVertexCount;
    view.faceNormalsBuffer = normalDecor.faceNormalsVbuf.get();
    view.faceNormalsVertexCount = normalDecor.faceNormalsVertexCount;
    view.boundaryEdgesBuffer = boundaryDecor.boundaryEdgesVbuf.get();
    view.boundaryEdgesVertexCount = boundaryDecor.boundaryEdgesVertexCount;
    view.boundaryEdgesFatBuffer = boundaryDecor.boundaryEdgesFatVbuf.get();
    view.boundaryEdgesFatVertexCount = boundaryDecor.boundaryEdgesFatVertexCount;
    view.textureSeamsBuffer = boundaryDecor.textureSeamsVbuf.get();
    view.textureSeamsVertexCount = boundaryDecor.textureSeamsVertexCount;
    view.textureSeamsFatBuffer = boundaryDecor.textureSeamsFatVbuf.get();
    view.textureSeamsFatVertexCount = boundaryDecor.textureSeamsFatVertexCount;
    return view;
}

void MeshGpuResourceCache::purgeMesh(std::uint64_t meshId)
{
    if (!m_state || meshId == 0)
        return;
    for (auto &bucket : m_state->byRhi)
        bucket.second.erase(meshId);
}

void MeshGpuResourceCache::releaseRhiResources(QRhi *rhi)
{
    if (!m_state || !rhi)
        return;
    m_state->byRhi.erase(rhi);
}

void MeshGpuResourceCache::clearAll()
{
    if (!m_state)
        return;
    m_state->byRhi.clear();
}
