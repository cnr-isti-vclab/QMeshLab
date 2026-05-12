#include "texturedefragfilterplugin.h"

#include "document.h"
#include "filterparam.h"
#include "meshfilterpluginmanager.h"
#include "textureassociationutils.h"
#include "vcgmesh.h"

#include "upstream/TextureDefragmentation/src/mesh.h"
#include "upstream/TextureDefragmentation/src/mesh_attribute.h"
#include "upstream/TextureDefragmentation/src/mesh_graph.h"
#include "upstream/TextureDefragmentation/src/packing.h"
#include "upstream/TextureDefragmentation/src/seam_remover.h"
#include "upstream/TextureDefragmentation/src/texture_object.h"
#include "upstream/TextureDefragmentation/src/texture_optimization.h"
#include "upstream/TextureDefragmentation/src/texture_rendering.h"

#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/algorithms/clean.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/color.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <vcg/complex/algorithms/update/topology.h>
#include <vcg/complex/append.h>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <map>
#include <memory>
#include <vector>

namespace {

constexpr QLatin1StringView kFilterTextureDefrag("apply_texmap_defragmentation");
using Mask = vcg::tri::io::Mask;
namespace Tex = TextureAssociationUtils;

MeshFilterRunResult fail(const QString &message)
{
    MeshFilterRunResult result;
    result.success = false;
    result.documentModified = false;
    result.errorMessage = message;
    return result;
}

MeshFilterRunResult success(const QStringList &info, int newMeshIndex)
{
    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.infoMessages = info;
    if (newMeshIndex >= 0)
        result.newMeshIndices.push_back(newMeshIndex);
    return result;
}

bool copyCurrentMeshForDefrag(const VCGMesh &source, VCGMesh &copy)
{
    if (vcg::tri::HasPerWedgeTexCoord(source))
        copy.face.EnableWedgeTexCoord();
    vcg::tri::Append<VCGMesh, VCGMesh>::MeshCopyConst(copy, source);
    copy.textures = source.textures;
    return true;
}

bool buildDefragMesh(const VCGMesh &source, Mesh &defragMesh, QString &error)
{
    defragMesh.Clear();
    if (!vcg::tri::HasPerWedgeTexCoord(source)) {
        error = QObject::tr("Texture defragmentation requires per-wedge texture coordinates.");
        return false;
    }

    auto vi = vcg::tri::Allocator<Mesh>::AddVertices(defragMesh, source.VN());
    for (int i = 0; i < source.VN(); ++i, ++vi) {
        const VCGVertex &srcV = source.vert[size_t(i)];
        vi->P() = Mesh::CoordType(srcV.cP().X(), srcV.cP().Y(), srcV.cP().Z());
        vi->N() = Mesh::VertexType::NormalType(srcV.cN().X(), srcV.cN().Y(), srcV.cN().Z());
        vi->C() = srcV.cC();
    }

    auto fi = vcg::tri::Allocator<Mesh>::AddFaces(defragMesh, source.FN());
    for (int i = 0; i < source.FN(); ++i, ++fi) {
        const VCGFace &srcF = source.face[size_t(i)];
        for (int k = 0; k < 3; ++k) {
            const int vertexIndex = vcg::tri::Index(source, srcF.cV(k));
            if (vertexIndex < 0 || vertexIndex >= source.VN()) {
                error = QObject::tr("Texture defragmentation found an invalid face vertex reference.");
                return false;
            }
            fi->V(k) = &defragMesh.vert[size_t(vertexIndex)];
            fi->WT(k).U() = srcF.cWT(k).U();
            fi->WT(k).V() = srcF.cWT(k).V();
            fi->WT(k).N() = srcF.cWT(k).N();
        }
        fi->SetMesh();
    }

    return true;
}

void copyDefragTexcoordsToMesh(const Mesh &defragMesh, VCGMesh &target)
{
    target.face.EnableWedgeTexCoord();
    const int faceCount = std::min(target.FN(), defragMesh.FN());
    for (int i = 0; i < faceCount; ++i) {
        const MeshFace &srcF = defragMesh.face[size_t(i)];
        VCGFace &dstF = target.face[size_t(i)];
        for (int k = 0; k < 3; ++k) {
            dstF.WT(k).U() = float(srcF.cWT(k).U());
            dstF.WT(k).V() = float(srcF.cWT(k).V());
            dstF.WT(k).N() = srcF.cWT(k).N();
        }
    }
}

QString textureAssetName(const QString &meshName, int index)
{
    QString base = meshName.trimmed();
    if (base.isEmpty())
        base = QStringLiteral("texdefrag");
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_\\-]+")), QStringLiteral("_"));
    return QStringLiteral("%1_optimized_texture_%2.png").arg(base).arg(index);
}

} // namespace

QString TextureDefragFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.texture_defragmentation");
}

QString TextureDefragFilterPlugin::name() const
{
    return QObject::tr("Texture Defragmentation Filters");
}

MeshFilterRunResult TextureDefragFilterPlugin::runFilter(
    const QString &filterId,
    const FilterParams &params,
    Document &doc) const
{
    if (filterId != QString::fromLatin1(kFilterTextureDefrag))
        return fail(QObject::tr("Unknown filter id: %1").arg(filterId));

    const int meshIndex = doc.currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));

    const Document::MeshEntry &sourceEntry = doc.mesh(meshIndex);
    const VCGMesh &sourceMesh = sourceEntry.mesh;
    if (sourceMesh.VN() <= 0 || sourceMesh.FN() <= 0)
        return fail(QObject::tr("Texture defragmentation requires a non-empty triangular mesh."));
    if (!vcg::tri::HasPerWedgeTexCoord(sourceMesh))
        return fail(QObject::tr("Texture defragmentation requires per-wedge texture coordinates."));
    if (Document::meshTextureAssociationCount(sourceEntry) <= 0)
        return fail(QObject::tr("Texture defragmentation requires at least one associated texture image."));

    doc.beginFilterProgress(QObject::tr("Texture Map Defragmentation"));
    auto progress = [&](int pct, const char *label) {
        if (vcg::CallBackPos *cb = doc.progressCallback())
            (*cb)(pct, label);
    };

    VCGMesh outputMesh;
    copyCurrentMeshForDefrag(sourceMesh, outputMesh);
    const int removedZeroFaces = vcg::tri::Clean<VCGMesh>::RemoveZeroAreaFace(outputMesh);
    const int removedDuplicateVertices = vcg::tri::Clean<VCGMesh>::RemoveDuplicateVertex(outputMesh);
    vcg::tri::Allocator<VCGMesh>::CompactEveryVector(outputMesh);
    vcg::tri::UpdateBounding<VCGMesh>::Box(outputMesh);

    QString textureError;
    TextureObjectHandle textureObject = std::make_shared<TextureObject>();
    const int textureCount = Document::meshTextureAssociationCount(sourceEntry);
    for (int textureIndex = 0; textureIndex < textureCount; ++textureIndex) {
        QImage image;
        if (!Tex::loadAssociatedTextureImage(sourceEntry, textureIndex, image, textureError)) {
            doc.finishFilterProgress(false, textureError);
            return fail(textureError);
        }
        if (!textureObject->AddImage(image)) {
            const QString message = QObject::tr("Failed to add source texture %1 to defragmentation input.")
                .arg(textureIndex + 1);
            doc.finishFilterProgress(false, message);
            return fail(message);
        }
    }

    Mesh defragMesh;
    QString buildError;
    if (!buildDefragMesh(outputMesh, defragMesh, buildError)) {
        doc.finishFilterProgress(false, buildError);
        return fail(buildError);
    }

    AlgoParameters ap;
    ap.matchingThreshold = params.getDouble(QStringLiteral("matchingThreshold"), 2.0);
    ap.boundaryTolerance = params.getDouble(QStringLiteral("boundaryTolerance"), 0.2);
    ap.distortionTolerance = params.getDouble(QStringLiteral("distortionTolerance"), 0.5);
    ap.globalDistortionThreshold = params.getDouble(QStringLiteral("globalDistortionTolerance"), 0.025);
    ap.UVBorderLengthReduction = params.getDouble(QStringLiteral("uvReductionLimit"), 0.0) / 100.0;
    ap.offsetFactor = params.getDouble(QStringLiteral("offsetFactor"), 5.0);
    ap.timelimit = params.getDouble(QStringLiteral("timelimit"), 0.0);

    progress(5, "Texture defrag: preparing topology...");
    vcg::tri::UpdateTopology<Mesh>::FaceFace(defragMesh);
    vcg::tri::UpdateNormal<Mesh>::PerFaceNormalized(defragMesh);
    vcg::tri::UpdateNormal<Mesh>::PerVertexNormalized(defragMesh);
    ScaleTextureCoordinatesToImage(defragMesh, textureObject);

    Compute3DFaceAdjacencyAttribute(defragMesh);
    CutAlongSeams(defragMesh);
    GraphHandle graph = ComputeGraph(defragMesh, textureObject);

    while (vcg::tri::Clean<Mesh>::SplitNonManifoldVertex(defragMesh, 0))
        ;
    vcg::tri::Allocator<Mesh>::CompactEveryVector(defragMesh);

    DisconnectCharts(graph);
    vcg::tri::UpdateTopology<Mesh>::FaceFace(defragMesh);
    vcg::tri::UpdateTopology<Mesh>::VertexFace(defragMesh);
    ComputeWedgeTexCoordStorageAttribute(defragMesh);

    std::map<RegionID, bool> flipped;
    for (auto& c : graph->charts)
        flipped[c.first] = c.second->UVFlipped();
    ReorientCharts(graph);

    progress(20, "Defragmenting atlas...");
    AlgoStateHandle state = InitializeState(graph, ap);
    GreedyOptimization(graph, state, ap);

    int duplicatedVertices = 0;
    Finalize(graph, &duplicatedVertices);

    const bool colorize = false;
    std::map<ChartHandle, int> anchorMap;
    if (colorize)
        vcg::tri::UpdateColor<Mesh>::PerFaceConstant(defragMesh, vcg::Color4b(91, 130, 200, 255));
    for (auto& entry : graph->charts) {
        ChartHandle chart = entry.second;
        double zeroResamplingChartArea = 0.0;
        int anchor = RotateChartForResampling(chart, state->changeSet, flipped, colorize, &zeroResamplingChartArea);
        if (anchor != -1)
            anchorMap[chart] = anchor;
    }

    progress(70, "Packing atlas...");
    std::vector<ChartHandle> chartsToPack;
    for (auto& entry : graph->charts) {
        if (entry.second->AreaUV() != 0) {
            chartsToPack.push_back(entry.second);
        } else {
            for (auto fptr : entry.second->fpVec) {
                for (int j = 0; j < fptr->VN(); ++j) {
                    fptr->V(j)->T().P() = Point2d::Zero();
                    fptr->V(j)->T().N() = 0;
                    fptr->WT(j).P() = Point2d::Zero();
                    fptr->WT(j).N() = 0;
                }
            }
        }
    }

    for (auto& f : graph->mesh.face) {
        for (int i = 0; i < 3; ++i) {
            if (!vcg::face::IsManifold(f, i)) {
                f.FFp(i) = &f;
                f.FFi(i) = i;
            }
        }
    }

    std::vector<TextureSize> texszVec;
    const int packedCount = Pack(chartsToPack, textureObject, texszVec);
    if (packedCount < int(chartsToPack.size())) {
        const QString message = QObject::tr("Texture defragmentation packing failed before all charts were packed.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    TrimTexture(defragMesh, texszVec, false);
    IntegerShift(defragMesh, chartsToPack, texszVec, anchorMap, flipped);

    progress(85, "Resampling textures...");
    std::vector<std::shared_ptr<QImage>> renderedTextures =
        RenderTexture(defragMesh, textureObject, texszVec, true, Linear);
    if (renderedTextures.empty()) {
        const QString message = QObject::tr("Texture defragmentation produced no output textures.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    if (outputMesh.FN() != defragMesh.FN()) {
        const QString message = QObject::tr("Texture defragmentation face count mismatch after optimization.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    copyDefragTexcoordsToMesh(defragMesh, outputMesh);
    outputMesh.textures.clear();
    for (int i = 0; i < int(renderedTextures.size()); ++i)
        outputMesh.textures.push_back(textureAssetName(sourceEntry.name, i).toStdString());

    const QString newName = QObject::tr("texdefrag_%1").arg(sourceEntry.name);
    const int outputMask = (sourceEntry.ioMask | Mask::IOM_WEDGTEXCOORD) & ~Mask::IOM_VERTTEXCOORD;
    const int newIndex = doc.addMesh(outputMesh, newName, outputMask);
    if (newIndex < 0) {
        const QString message = QObject::tr("Failed to add texture-defragmented mesh to the document.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    Document::MeshEntry &newEntry = doc.mesh(newIndex);
    newEntry.transform = sourceEntry.transform;
    std::vector<MeshIOTextureAsset> outputAssets;
    outputAssets.reserve(renderedTextures.size());
    for (int i = 0; i < int(renderedTextures.size()); ++i) {
        outputAssets.push_back(Tex::makeTextureAssetFromImage(
            *renderedTextures[size_t(i)],
            textureAssetName(sourceEntry.name, i)));
    }
    Tex::replaceTextureAssociations(newEntry, outputAssets);
    doc.markMeshMaterialChanged(newIndex, QObject::tr("Created texture-defragmented mesh '%1'.").arg(newEntry.name));

    progress(100, "Done.");
    doc.finishFilterProgress(true, QObject::tr("Texture map defragmentation completed."));

    QStringList info;
    info << QObject::tr("Created texture-defragmented mesh '%1'.").arg(newEntry.name)
         << QObject::tr("Output textures: %1").arg(renderedTextures.size())
         << QObject::tr("Charts packed: %1").arg(chartsToPack.size())
         << QObject::tr("Duplicated vertices introduced by seam processing: %1").arg(duplicatedVertices);
    if (removedZeroFaces > 0)
        info << QObject::tr("Removed %1 zero-area face(s) before defragmentation.").arg(removedZeroFaces);
    if (removedDuplicateVertices > 0)
        info << QObject::tr("Removed %1 duplicate vertex/vertices before defragmentation.").arg(removedDuplicateVertices);

    return success(info, newIndex);
}

void registerTextureDefragFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<TextureDefragFilterPlugin>());
}
