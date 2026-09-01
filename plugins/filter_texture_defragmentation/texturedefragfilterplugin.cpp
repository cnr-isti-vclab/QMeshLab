#include "texturedefragfilterplugin.h"

#include "document.h"
#include "filterparam.h"
#include "meshfilterpluginmanager.h"
#include "textureassociationutils.h"
#include "vcgmesh.h"
#include "../filter_texture/texture_packer.hpp"

#include "upstream/TextureDefragmentation/src/mesh.h"
#include "upstream/TextureDefragmentation/src/mesh_attribute.h"
#include "upstream/TextureDefragmentation/src/mesh_graph.h"
#include "upstream/TextureDefragmentation/src/packing.h"
#include "upstream/TextureDefragmentation/src/seam_remover.h"
#include "upstream/TextureDefragmentation/src/texture_object.h"
#include "upstream/TextureDefragmentation/src/texture_optimization.h"
#include "upstream/TextureDefragmentation/src/texture_rendering.h"

#include <algorithm>
#include <map>
#include <memory>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QStringList>
#include <vector>
#include <vcg/complex/append.h>
#include <vcg/complex/algorithms/clean.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/color.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <vcg/complex/algorithms/update/topology.h>
#include <wrap/io_trimesh/io_mask.h>

namespace {

constexpr QLatin1StringView kFilterTextureDefrag("apply_texmap_defragmentation");
constexpr QLatin1StringView kFilterSmallIslandsRemover("apply_small_islands_remover");

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
    if (vcg::tri::HasPerWedgeTexCoord(source)) {
        copy.face.EnableWedgeTexCoord();
    }
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
    // Core checks before starting the process.
    // 1 - ensure that the requested filter was either Defragment Texture Atlas or Merge Small Texture Islands.
    // 2 - ensure that a mesh was selected.
    // 3 - ensure that the mesh provided has faces, per-wedge texture coordinates and has at least one texture image.
    if (filterId != QString::fromLatin1(kFilterTextureDefrag) &&
        filterId != QString::fromLatin1(kFilterSmallIslandsRemover)) {
        return fail(QObject::tr("Unknown filter id: %1").arg(filterId));
    }

    const int meshIndex = doc.currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= doc.meshCount()) {
        return fail(QObject::tr("No current mesh selected."));
    }


    const Document::MeshEntry &sourceEntry = doc.mesh(meshIndex);
    const QMatrix4x4 sourceTransform = sourceEntry.transform;
    const VCGMesh &sourceMesh = sourceEntry.mesh;
    if (sourceMesh.VN() <= 0 || sourceMesh.FN() <= 0)
        return fail(QObject::tr("Texture defragmentation requires a non-empty triangular mesh."));
    if (!vcg::tri::HasPerWedgeTexCoord(sourceMesh))
        return fail(QObject::tr("Texture defragmentation requires per-wedge texture coordinates."));
    if (Document::meshTextureAssociationCount(sourceEntry) <= 0)
        return fail(QObject::tr("Texture defragmentation requires at least one associated texture image."));

    doc.beginFilterProgress(QObject::tr("Defragment Texture Atlas"));
    auto progress = [&](int pct, const char *label) {
        if (vcg::CallBackPos *cb = doc.progressCallback()) {
            (*cb)(pct, label);
        }
    };

    // We create a new MeshLab layer, containing a copy of the current model.
    // This filter will work on this duplicate rather than the original.
    //
    // Before processing, we need to clean the input mesh from:
    //
    //	* degenerate faces (i.e., triangles having zero area).
    //
    //	* sets of vertices sharing the same 3D position. These will be collapsed into a single one.
    //
    //	* vertices marked as `DELETED`. Their entries will be removed in the mesh's vertex data structure.
    //
    //	* Rebuilt the FACE-FACE adjacency topology to take into account the new changes.
    //
    // Note that the Texture Defragmentation filter assumes that the input mesh is manifold.
    // If a non-manifold edge is found, a warning is issued.
    VCGMesh outputMesh;
    copyCurrentMeshForDefrag(sourceMesh, outputMesh);
    const int removedZeroFaces = vcg::tri::Clean<VCGMesh>::RemoveZeroAreaFace(outputMesh);
    const int removedDuplicateVertices = vcg::tri::Clean<VCGMesh>::RemoveDuplicateVertex(outputMesh);
    vcg::tri::Allocator<VCGMesh>::CompactEveryVector(outputMesh);
    vcg::tri::UpdateBounding<VCGMesh>::Box(outputMesh);

    // All texture images referenced by the mesh are loaded into a TextureObject.
    // This instance provides direct access to the raw pixel data for UV-to-pixel
    // coordinate conversion. It will be also used by the final resampling phase.
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

    // Texture Defragmentation defines its own type (`Mesh`) for working with meshes.
    // We will construct an instance of the said type, denoted as `defragMesh` from our input MeshLab model.
    // The building process copies position-by-position the vertices from the source. Faces are set up with
    // their vertex pointers and wedge texture coordinates.
    //
    // From now on the filter will use only `defragMesh`, thus we will refer to it as the input mesh.
    Mesh defragMesh;
    QString buildError;
    if (!buildDefragMesh(outputMesh, defragMesh, buildError)) {
        doc.finishFilterProgress(false, buildError);
        return fail(buildError);
    }

    // For the TextureDefragmentation mesh instance, we build its FACE-FACE
    // adjacency topology and compute the normalized face and vertex normal.
    // The normals will be needed by the As-Rigid-As-Possible (ARAP) optimization.
    progress(5, "Texture defrag: preparing topology...");
    vcg::tri::UpdateTopology<Mesh>::FaceFace(defragMesh);
    vcg::tri::UpdateNormal<Mesh>::PerFaceNormalized(defragMesh);
    vcg::tri::UpdateNormal<Mesh>::PerVertexNormalized(defragMesh);

    // As of now the mesh stores the UV coordinates within the range [0,1] (i.e., normalized).
    // We need to convert them into the pixel space (i.e., in respect to the texture images'
    // resolution). This is done by multiplying each UV position by the texture's width and
    // height.
    ScaleTextureCoordinatesToImage(defragMesh, textureObject);

    // We apply three fundamental setup steps on the input mesh:
    //
    //	* Compute3DFace stores the original 3D mesh adjacency information before
    //	  our cutting of the seams modifies the topology.
    //
    //	* CutAlongSeam splits the mesh along the UV seams by duplicating
    //	  vertices, making Face-Face adjacency stop at the chart boundaries.
    //
    //	* ComputeGraph identifies the UV islands and builds the islands adjacency graph.
    Compute3DFaceAdjacencyAttribute(defragMesh);
    CutAlongSeams(defragMesh);
    GraphHandle graph = ComputeGraph(defragMesh, textureObject);

    unsigned long islandsBeforeDefrag = graph->charts.size();
    doc.writeLog(Document::tr("UV islands before defragmentation: %1").arg(islandsBeforeDefrag), Document::LogSource::VCG);

    // Recall that a non-manifold vertex is one which is incident to at least two
    // distinct sheets of faces. We resolve non-manifold vertices by duplicating
    // them such that each sheet has its own copy. These new vertices are then
    // displaced from one another.
    //
    // This fixing procedure is implemented by the function `SplitNonManifoldVertex`.
    // A call to the function only splits the vertex into two copies, meaning
    // that if the vertex is shared among more than two sheets, multiple calls
    // are necessary. For this reason we wrap the function inside a loop.
    //
    // After removing any non-manifold vertex, we compact the vertex data structure
    // of the input mesh.
    while (vcg::tri::Clean<Mesh>::SplitNonManifoldVertex(defragMesh, 0))
        ;
    vcg::tri::Allocator<Mesh>::CompactEveryVector(defragMesh);

    // `DisconnectCharts` gives each chart its own private copy of the vertices at the boundary.
    // In this way charts are fully topologically independent of one another. Since this
    // process increases drastically the number of vertices (for each seam across two charts, its
    // endpoints are duplicated), we need to rebuild both the FACE-FACE and VERTEX-FACE topology.
    DisconnectCharts(graph);
    vcg::tri::UpdateTopology<Mesh>::FaceFace(defragMesh);
    vcg::tri::UpdateTopology<Mesh>::VertexFace(defragMesh);

    // We snapshot the current per-Wedge UV coordinates into a separate
    // per-face attribute of the input mesh. This backup serves as the
    // baseline parametrization throughout the Texture Defragmentation.
    ComputeWedgeTexCoordStorageAttribute(defragMesh);

    // Some charts may have their UV triangles oriented clockwise, resulting flipped compared to
    // the standard counter-clockwise orientation. We detect these charts and reorient them to
    // ensure a consistent orientation across the atlas.
    //
    // The original flip states are recorded in the attribute `flipped`. When generating the final
    // optimized mesh, we need to roll back the original orientation.
    std::map<RegionID, bool> flipped;
    for (auto& c : graph->charts)
        flipped[c.first] = c.second->UVFlipped();
    ReorientCharts(graph);

    // Both filters end up in the same atlas packer, which tries randomized chart
    // permutations when the chart count is small.
    const RandomSeed seed = params.getRandomSeed();

    AlgoParameters ap;
    if (filterId == QString::fromLatin1(kFilterTextureDefrag)) {
        ap.filterType = FilterType::TextureDefrag;
        ap.matchingThreshold = params.getDouble(QStringLiteral("matchingThreshold"), 2.0);
        ap.boundaryTolerance = params.getDouble(QStringLiteral("boundaryTolerance"), 0.2);
        ap.distortionTolerance = params.getDouble(QStringLiteral("distortionTolerance"), 0.5);
        ap.globalDistortionThreshold = params.getDouble(QStringLiteral("globalDistortionTolerance"), 0.025);
        ap.UVBorderLengthReduction = params.getDouble(QStringLiteral("uvReductionLimit"), 0.0) / 100.0;
        ap.offsetFactor = params.getDouble(QStringLiteral("offsetFactor"), 5.0);
        ap.timelimit = params.getDouble(QStringLiteral("timelimit"), 0.0);
    }
    else if (filterId == QString::fromLatin1(kFilterSmallIslandsRemover)) {
        ap.filterType = FilterType::SmallIslandRemover;
        ap.timelimit = params.getDouble(QStringLiteral("timelimit"), 0.0);
        ap.reduce = true;
        ap.ignoreOnReject = params.getBool(QStringLiteral("quickRun"), false);
        ap.targetTexCount = params.getInt(QStringLiteral("targetTexCount"), 0);

        // Disable the boundary ratio check: small islands often have seams
        // covering only a tiny fraction of their boundary, which the default
        // check would reject. A negative value makes the check always false.
        ap.boundaryTolerance = -1.0;

        // Prevent early termination based on UV border reduction — we want to
        // attempt all eligible operations. A negative value makes the check
        // always false.
        ap.UVBorderLengthReduction = -1.0;

        // Eliminate the boundary-ratio exponent penalty in ComputeCost: small
        // islands are precisely the ones with poor seam-to-boundary ratios, so
        // penalizing them would deprioritize exactly the merges we want.
        ap.expb = 0.0;

        const double multiplier = params.getDouble("maxMultiplier", 1.0);
        // ========== TRANSLATION OVER MULTIPLIER AGAINST MEDIAN ISLAND SIZE ===========
        // We compute the median island size within the mesh. Then the user defines
        // a multiplier over it to determine the final threshold.
        // We use the UV Border since it guarantees that it scaled identically when resolution changes.
        //
        // Compute the set of all islands' border length and sort them in increasing order
        // Then compute the median and set the threshold.
        std::vector<double> borderLengths;
        borderLengths.reserve(graph->charts.size());
        for (const auto &ch : graph->charts) {
            borderLengths.push_back(ch.second->BorderUV());
        }
        std::sort(borderLengths.begin(), borderLengths.end());

        const size_t n = borderLengths.size();
        const double medianBorder = (n % 2 == 0) ?
            ( borderLengths [(n / 2) - 1] + borderLengths [n / 2] ) / 2.0 :
            borderLengths [ n / 2 ];

        ap.maxThreshold = multiplier > 0 ?
            multiplier * medianBorder :
            Infinity();

        // The Distortion Mode selected by the user will determine the values
        // the algorithm will use for `distortionTolerance` and `globalDistortionThreshold`,
        int distortionMode = params.getInt("distortionMode", 0);
        switch (distortionMode) {
            // STRICT MODE
            // More tolerant to distortion compared to standard Texture Defragmentation, still all distortion checks are done.
            case 0:
                ap.distortionTolerance       = 2.0;
                ap.globalDistortionThreshold = 0.1;
                ap.matchingThreshold         = 5.0;
                break;
                // LOOSE MODE:
                // All checks regarding the distortion introduced by a merge operation are ignored.
            case 1:
                ap.distortionTolerance = Infinity();
                ap.globalDistortionThreshold = Infinity();
                // To avoid the check of `avgError` in `ComputeCost`(seam_remover.cpp) at ~row 933.
                ap.matchingThreshold = Infinity();
                break;
                // UNSAFE MODE:
                // All checks regarding both distortions and fold/overlaps are ignored.
            case 2:
                ap.distortionTolerance = Infinity();
                ap.globalDistortionThreshold = Infinity();
                ap.matchingThreshold = Infinity();
                ap.skipOverlapChecks = true;
                break;
            default:
                ap.distortionTolerance       = 2.0;
                ap.globalDistortionThreshold = 0.1;
                ap.matchingThreshold         = 5.0;
                break;
        }
    }


    // The Texture Defragmentation main execution is divided in three phases:
    //
    //	* `InitializeState` constructs a mesh containing only edges belonging to a seam. After individualizing
    //	  them, it builds the seams, represented as continuous chains of edges. The seams are then clustered
    //	  by the pair of charts sharing them. Each group identifies a merge operation, and for each we compute
    //	  its initial cost. All clusters are pushed into a priority queue, ordering them from most convenient
    //	  to least according to their cost.
    //
    //	* `GreedyOptimization` runs a greedy merge loop, repeatedly merging the currently most convenient
    //	  merge operation. After a merge it tries to run an As-Rigid-As-Possible (ARAP) optimization to fix
    //	  the introduced distortion. Note that if the merge introduces too much distortion or unfixable
    //	  overlaps, it is rejected and its operations are rolled back. Note that the distortion checks are
    //	  skipped when running Merge Small Texture Islands with distortionMode set to LOOSE.
    //
    //	* `Finalize` prepares the now optimized input mesh to be returned, collapsing coincident duplicate
    //	  vertices, removing orphaned vertices and rebuilding topologies.
    progress(20, "Defragmenting atlas...");
    vcg::tri::UpdateTopology<Mesh>::FaceFace(defragMesh);

    AlgoStateHandle state = InitializeState(graph, ap);
    GreedyOptimization(graph, state, ap);
    int duplicatedVertices = 0;
    Finalize(graph, &duplicatedVertices);

	unsigned long islandsAfterDefrag = graph->charts.size();
    doc.writeLog(Document::tr("UV islands after defragmentation: %1").arg(islandsAfterDefrag), Document::LogSource::VCG);
    doc.writeLog(Document::tr("UV islands removed: %1").arg(islandsBeforeDefrag - islandsAfterDefrag), Document::LogSource::VCG);

    const bool colorize = false;
    std::map<ChartHandle, int> anchorMap;
    if (colorize)
        vcg::tri::UpdateColor<Mesh>::PerFaceConstant(defragMesh, vcg::Color4b(91, 130, 200, 255));

    // To diminish texture resampling as much as possible, each chart will be aligned optimally within
    // the final layout. This is handled by the `RotateChartForResampling` function which uses the flip
    // info computed early to ensure the computed rotations are applied consistently.
    //
    // Charts that can be anchored (i.e., pinned to a specific orientation) are recorded in `anchorMap`.
    // They will be used during the packing phase.
    for (auto& entry : graph->charts) {
        ChartHandle chart = entry.second;
        double zeroResamplingChartArea = 0.0;
        int anchor = RotateChartForResampling(chart, state->changeSet, flipped, colorize, &zeroResamplingChartArea);
        if (anchor != -1)
            anchorMap[chart] = anchor;
    }

    progress(70, "Packing atlas...");

    // Charts having UV area set to zero needs to be excluded from the packing phase.
    // This is done by zeroing their UV coordinates.
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

    // Our packer algorithm assumes manifold or boundary edges. In the presence of non-manifold
    // edges, we set them to reference themselves (i.e., they become borders).
    for (auto& f : graph->mesh.face) {
        for (int i = 0; i < 3; ++i) {
            if (!vcg::face::IsManifold(f, i)) {
                f.FFp(i) = &f;
                f.FFi(i) = i;
            }
        }
    }

    // The UV atlas packing procedure is managed by the `Pack` function, which
    // uses a bin-packing strategy. All charts are arranged into as few textures
    // as possible. Each output texture will have the resolution
    // specified in the corresponding input texture of `texszVec`.
    // The function returns the number of charts that have been packed. If this
    // number is not equal to the number of charts, then an error occurred.
    //
    // `TrimTexture` adjusts the generated textures sizes by removing unused space.
    //
    // To decrease resampling, all charts that have been aligned through
    // rigid transformations must move by an integer number of pixels (otherwise
    // subpixel bleeding could occur). This property is enforced by `IntegerShift`.
    std::vector<TextureSize> texszVec;
    const int packedCount = Pack(chartsToPack, textureObject, texszVec, seed.value);
    if (packedCount < int(chartsToPack.size())) {
        const QString message = QObject::tr("Texture defragmentation packing failed before all charts were packed.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
    TrimTexture(defragMesh, texszVec, false);
    IntegerShift(defragMesh, chartsToPack, texszVec, anchorMap, flipped);

    // The new texture images are rendered by rastering the mesh with the original textures
    // as input. The resampling of the original textures uses linear interpolation
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

    // The optimized Wedge UV coordinates are copied back into the MeshLab layer.
    // The texture index `N()` is also copied, since the packing step may have
    // redistributed charts across multiple textures.
    copyDefragTexcoordsToMesh(defragMesh, outputMesh);

    // If we are executing the variant `FP_SMALL_CHARTS_REMOVER` we need to guarantee
    // that the final number of output textures is minor or equal than the
    // user-provided `targetTexCount`.
    //
    // If so, we do not need to apply any further packing. Otherwise, we construct a
    // TexturePacker instance which will apply a greedy procedure to decrease the
    // output textures to the number requested.
    //
    // Note that if `targetTexCount` is set to zero, then the parameter is ignored, and
    // we entirely skip this step.
    if (ap.filterType == FilterType::SmallIslandRemover &&
        ap.targetTexCount > 0                           &&
        renderedTextures.size() > ap.targetTexCount) {
        std::vector<QImage> convertedTexs;
        for (auto &t: renderedTextures) {
            // TexturePacker works over reference_wrapper<const QImage>, while newTextures
            // hold shared_ptr<QImage> entries. We need to convert them before giving them
            // to the packer.
            convertedTexs.push_back(*(t.get()));
        }

        QString packingError;
        std::vector<QImage> packedTexs =
            TexturePacker::simplePacking(convertedTexs, ap.targetTexCount, 4, outputMesh, &packingError);
        if (packedTexs.empty()) {
            doc.finishFilterProgress(false, packingError);
            return fail(packingError);
        }

        // Replace the entries in newTextures with our new merged results
        renderedTextures.clear();
        for (auto &t : packedTexs) {
            renderedTextures.push_back(std::make_shared<QImage>(t));
        }
    }



    // The old textures are cleared and replaced with the new rendered ones.
    // Finally, the layer now references the new textures computed by the
    // filter.
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
    newEntry.transform = sourceTransform;
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
    info << seed.message();

    return success(info, newIndex);
}

void registerTextureDefragFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<TextureDefragFilterPlugin>());
}
