#include "selectfilterplugin.h"

#include "document.h"
#include "meshfilterpluginmanager.h"
#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/allocate.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <vcg/complex/algorithms/update/selection.h>
#include <algorithm>
#include <memory>

namespace {
constexpr QLatin1StringView kFilterSelectAll("select_all");
constexpr QLatin1StringView kFilterSelectNone("select_none");
constexpr QLatin1StringView kFilterSelectInvert("select_invert");
constexpr QLatin1StringView kFilterSelectErode("select_erode");
constexpr QLatin1StringView kFilterSelectDilate("select_dilate");
constexpr QLatin1StringView kFilterDeleteSelectedVerts("delete_selected_vertices");
constexpr QLatin1StringView kFilterDeleteSelectedFaces("delete_selected_faces");
constexpr QLatin1StringView kFilterDeleteSelectedFaceVerts("delete_selected_faces_and_vertices");

bool boolParameter(const MeshFilterParameterValues &params, const QString &id, bool fallback)
{
    const auto it = params.constFind(id);
    if (it == params.constEnd())
        return fallback;
    if (it.value().userType() == QMetaType::Bool)
        return it.value().toBool();
    const QString text = it.value().toString().trimmed().toLower();
    if (text == QStringLiteral("true") || text == QStringLiteral("1"))
        return true;
    if (text == QStringLiteral("false") || text == QStringLiteral("0"))
        return false;
    return fallback;
}

std::vector<MeshFilterDescriptor> buildDescriptors(const Document &doc)
{
    using Sel = vcg::tri::UpdateSelection<VCGMesh>;

    bool defaultInvertFaces = true;
    bool defaultInvertVerts = true;
    const int currentMeshIndex = doc.currentMeshIndex();
    if (currentMeshIndex >= 0 && currentMeshIndex < doc.meshCount()) {
        const VCGMesh &mesh = doc.mesh(currentMeshIndex).mesh;
        defaultInvertFaces = (Sel::FaceCount(mesh) > 0);
        defaultInvertVerts = (Sel::VertexCount(mesh) > 0);
    }

    std::vector<MeshFilterDescriptor> out;

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectAll);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select All");
        d.shortDescription = QObject::tr("Select all the faces/vertices of the current mesh.");
        d.longDescriptionMarkdown =
            QObject::tr("Select all the faces/vertices of the current mesh.");
        d.tags = { QStringLiteral("selection") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor pFaces;
        pFaces.id = QStringLiteral("allFaces");
        pFaces.label = QObject::tr("Select all Faces");
        pFaces.helpMarkdown =
            QObject::tr("If true the filter will select all the faces.");
        pFaces.group = QStringLiteral("main");
        pFaces.type = MeshFilterParameterType::Bool;
        pFaces.defaultValue = true;
        d.parameters.push_back(std::move(pFaces));

        MeshFilterParameterDescriptor pVerts;
        pVerts.id = QStringLiteral("allVerts");
        pVerts.label = QObject::tr("Select all Vertices");
        pVerts.helpMarkdown =
            QObject::tr("If true the filter will select all the vertices.");
        pVerts.group = QStringLiteral("main");
        pVerts.type = MeshFilterParameterType::Bool;
        pVerts.defaultValue = true;
        d.parameters.push_back(std::move(pVerts));

        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectNone);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select None");
        d.shortDescription = QObject::tr("Clear the current set of selected faces/vertices.");
        d.longDescriptionMarkdown =
            QObject::tr("Clear the current set of selected faces/vertices.");
        d.tags = { QStringLiteral("selection") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor pFaces;
        pFaces.id = QStringLiteral("allFaces");
        pFaces.label = QObject::tr("De-select all Faces");
        pFaces.helpMarkdown =
            QObject::tr("If true the filter will de-select all the faces.");
        pFaces.group = QStringLiteral("main");
        pFaces.type = MeshFilterParameterType::Bool;
        pFaces.defaultValue = true;
        d.parameters.push_back(std::move(pFaces));

        MeshFilterParameterDescriptor pVerts;
        pVerts.id = QStringLiteral("allVerts");
        pVerts.label = QObject::tr("De-select all Vertices");
        pVerts.helpMarkdown =
            QObject::tr("If true the filter will de-select all the vertices.");
        pVerts.group = QStringLiteral("main");
        pVerts.type = MeshFilterParameterType::Bool;
        pVerts.defaultValue = true;
        d.parameters.push_back(std::move(pVerts));

        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectInvert);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Invert Selection");
        d.shortDescription =
            QObject::tr("Invert the current set of selected faces/vertices.");
        d.longDescriptionMarkdown =
            QObject::tr("Invert the current set of selected faces/vertices.");
        d.tags = { QStringLiteral("selection") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor pFaces;
        pFaces.id = QStringLiteral("InvFaces");
        pFaces.label = QObject::tr("Invert Faces");
        pFaces.helpMarkdown =
            QObject::tr("If true the filter will invert the set of selected faces.");
        pFaces.group = QStringLiteral("main");
        pFaces.type = MeshFilterParameterType::Bool;
        pFaces.defaultValue = defaultInvertFaces;
        d.parameters.push_back(std::move(pFaces));

        MeshFilterParameterDescriptor pVerts;
        pVerts.id = QStringLiteral("InvVerts");
        pVerts.label = QObject::tr("Invert Vertices");
        pVerts.helpMarkdown =
            QObject::tr("If true the filter will invert the set of selected vertices.");
        pVerts.group = QStringLiteral("main");
        pVerts.type = MeshFilterParameterType::Bool;
        pVerts.defaultValue = defaultInvertVerts;
        d.parameters.push_back(std::move(pVerts));

        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectErode);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Erode Selection");
        d.shortDescription =
            QObject::tr("Erode (reduce) the current set of selected faces.");
        d.longDescriptionMarkdown =
            QObject::tr("Erode (reduce) the current set of selected faces.");
        d.tags = { QStringLiteral("selection") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectDilate);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Dilate Selection");
        d.shortDescription =
            QObject::tr("Dilate (expand) the current set of selected faces.");
        d.longDescriptionMarkdown =
            QObject::tr("Dilate (expand) the current set of selected faces.");
        d.tags = { QStringLiteral("selection") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterDeleteSelectedVerts);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Delete Selected Vertices");
        d.shortDescription = QObject::tr(
            "Delete the current set of selected vertices; incident faces are deleted too.");
        d.longDescriptionMarkdown = QObject::tr(
            "Delete the current set of selected vertices; faces that share one of the deleted "
            "vertices are deleted too.");
        d.tags = {
            QStringLiteral("selection"),
            QStringLiteral("delete"),
            QStringLiteral("vertex")
        };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterDeleteSelectedFaces);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Delete Selected Faces");
        d.shortDescription = QObject::tr(
            "Delete selected faces; unreferenced vertices are not deleted.");
        d.longDescriptionMarkdown = QObject::tr(
            "Delete the current set of selected faces, vertices that remains unreferenced are not "
            "deleted.");
        d.tags = {
            QStringLiteral("selection"),
            QStringLiteral("delete"),
            QStringLiteral("face")
        };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterDeleteSelectedFaceVerts);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Delete Selected Faces and Vertices");
        d.shortDescription = QObject::tr(
            "Delete selected faces and enclosed selected vertices.");
        d.longDescriptionMarkdown = QObject::tr(
            "Delete the current set of selected faces and all the vertices surrounded by that "
            "faces.");
        d.tags = {
            QStringLiteral("selection"),
            QStringLiteral("delete"),
            QStringLiteral("face"),
            QStringLiteral("vertex")
        };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    return out;
}
}

QString SelectFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.select");
}

QString SelectFilterPlugin::name() const
{
    return QObject::tr("QMeshLab Selection Filters");
}

std::vector<MeshFilterDescriptor> SelectFilterPlugin::filters(const Document &doc) const
{
    return buildDescriptors(doc);
}

MeshFilterRunResult SelectFilterPlugin::runFilter(
    const QString &filterId,
    const MeshFilterParameterValues &parameters,
    Document &doc) const
{
    using Mask = vcg::tri::io::Mask;
    using Sel = vcg::tri::UpdateSelection<VCGMesh>;

    auto fail = [](const QString &msg) {
        MeshFilterRunResult result;
        result.success = false;
        result.documentModified = false;
        result.errorMessage = msg;
        return result;
    };

    const int meshIndex = doc.currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));

    Document::MeshEntry &entry = doc.mesh(meshIndex);
    VCGMesh &mesh = entry.mesh;

    auto selectionSummaryMessage = [&]() {
        return QObject::tr("Selection now contains %1 / %2 vertices and %3 / %4 faces.")
            .arg(Sel::VertexCount(mesh))
            .arg(mesh.VN())
            .arg(Sel::FaceCount(mesh))
            .arg(mesh.FN());
    };

    auto selectionResult = [&](const QString &changeMessage) {
        entry.ioMask |= (Mask::IOM_VERTFLAGS | Mask::IOM_FACEFLAGS);
        doc.markMeshMaterialChanged(meshIndex, changeMessage);
        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = { selectionSummaryMessage() };
        return result;
    };

    auto updateGeometryAfterDeletion = [&]() {
        vcg::tri::Allocator<VCGMesh>::CompactEveryVector(mesh);
        vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
        if (mesh.FN() > 0)
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
    };

    if (filterId == QString::fromLatin1(kFilterSelectAll)) {
        if (boolParameter(parameters, QStringLiteral("allVerts"), true))
            Sel::VertexAll(mesh);
        if (boolParameter(parameters, QStringLiteral("allFaces"), true))
            Sel::FaceAll(mesh);
        return selectionResult(
            QObject::tr("Select all on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectNone)) {
        if (boolParameter(parameters, QStringLiteral("allVerts"), true))
            Sel::VertexClear(mesh);
        if (boolParameter(parameters, QStringLiteral("allFaces"), true))
            Sel::FaceClear(mesh);
        return selectionResult(
            QObject::tr("Clear selection on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectInvert)) {
        if (boolParameter(parameters, QStringLiteral("InvVerts"), true))
            Sel::VertexInvert(mesh);
        if (boolParameter(parameters, QStringLiteral("InvFaces"), true))
            Sel::FaceInvert(mesh);
        return selectionResult(
            QObject::tr("Invert selection on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectErode)) {
        Sel::VertexFromFaceStrict(mesh);
        Sel::FaceFromVertexStrict(mesh);
        return selectionResult(
            QObject::tr("Erode selection on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectDilate)) {
        Sel::VertexFromFaceLoose(mesh);
        Sel::FaceFromVertexLoose(mesh);
        return selectionResult(
            QObject::tr("Dilate selection on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterDeleteSelectedVerts)) {
        const int selectedVerts = Sel::VertexCount(mesh);
        if (selectedVerts == 0) {
            MeshFilterRunResult result;
            result.success = true;
            result.documentModified = false;
            result.infoMessages = { QObject::tr("Nothing done: no vertex selected.") };
            return result;
        }

        const int beforeV = mesh.VN();
        const int beforeF = mesh.FN();
        Sel::FaceClear(mesh);
        Sel::FaceFromVertexLoose(mesh);
        for (auto fi = mesh.face.begin(); fi != mesh.face.end(); ++fi) {
            if (!fi->IsD() && fi->IsS())
                vcg::tri::Allocator<VCGMesh>::DeleteFace(mesh, *fi);
        }
        for (auto vi = mesh.vert.begin(); vi != mesh.vert.end(); ++vi) {
            if (!vi->IsD() && vi->IsS())
                vcg::tri::Allocator<VCGMesh>::DeleteVertex(mesh, *vi);
        }
        updateGeometryAfterDeletion();
        doc.markMeshGeometryChanged(
            meshIndex,
            QObject::tr("Deleted selected vertices from '%1'.").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = {
            QObject::tr("Deleted %1 vertices, %2 faces.")
                .arg(beforeV - mesh.VN())
                .arg(beforeF - mesh.FN())
        };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterDeleteSelectedFaces)) {
        const int selectedFaces = Sel::FaceCount(mesh);
        if (selectedFaces == 0) {
            MeshFilterRunResult result;
            result.success = true;
            result.documentModified = false;
            result.infoMessages = { QObject::tr("Nothing done: no faces selected.") };
            return result;
        }

        const int beforeF = mesh.FN();
        for (auto fi = mesh.face.begin(); fi != mesh.face.end(); ++fi) {
            if (!fi->IsD() && fi->IsS())
                vcg::tri::Allocator<VCGMesh>::DeleteFace(mesh, *fi);
        }
        updateGeometryAfterDeletion();
        doc.markMeshGeometryChanged(
            meshIndex,
            QObject::tr("Deleted selected faces from '%1'.").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = {
            QObject::tr("Deleted %1 faces.")
                .arg(beforeF - mesh.FN())
        };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterDeleteSelectedFaceVerts)) {
        const int selectedFaces = Sel::FaceCount(mesh);
        if (selectedFaces == 0) {
            MeshFilterRunResult result;
            result.success = true;
            result.documentModified = false;
            result.infoMessages = { QObject::tr("Nothing done: no faces selected.") };
            return result;
        }

        const int beforeV = mesh.VN();
        const int beforeF = mesh.FN();
        Sel::VertexClear(mesh);
        Sel::VertexFromFaceStrict(mesh);
        for (auto fi = mesh.face.begin(); fi != mesh.face.end(); ++fi) {
            if (!fi->IsD() && fi->IsS())
                vcg::tri::Allocator<VCGMesh>::DeleteFace(mesh, *fi);
        }
        for (auto vi = mesh.vert.begin(); vi != mesh.vert.end(); ++vi) {
            if (!vi->IsD() && vi->IsS())
                vcg::tri::Allocator<VCGMesh>::DeleteVertex(mesh, *vi);
        }
        updateGeometryAfterDeletion();
        doc.markMeshGeometryChanged(
            meshIndex,
            QObject::tr("Deleted selected faces and vertices from '%1'.").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = {
            QObject::tr("Deleted %1 faces, %2 vertices.")
                .arg(beforeF - mesh.FN())
                .arg(beforeV - mesh.VN())
        };
        return result;
    }

    return fail(QObject::tr("Unknown filter id: %1").arg(filterId));
}

void registerSelectFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<SelectFilterPlugin>());
}

