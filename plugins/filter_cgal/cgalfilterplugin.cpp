#include "cgalfilterplugin.h"

#include "document.h"
#include "meshfilterpluginmanager.h"
#include "vcgmesh.h"

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/alpha_wrap_3.h>
#include <CGAL/boost/graph/iterator.h>
#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <vcg/complex/allocate.h>
#include <QObject>
#include <QStringList>
#include <array>
#include <cmath>
#include <exception>
#include <vector>

namespace {

constexpr QLatin1StringView kFilterAlphaWrap("generate_alpha_wrap");
using Mask = vcg::tri::io::Mask;
using Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
using CgalPoint = Kernel::Point_3;
using CgalMesh = CGAL::Surface_mesh<CgalPoint>;
using Triangle = std::array<std::size_t, 3>;

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

bool buildTriangleSoup(const VCGMesh &mesh,
                       std::vector<CgalPoint> &points,
                       std::vector<Triangle> &triangles,
                       int &skippedFaces,
                       QString &error)
{
    points.clear();
    triangles.clear();
    skippedFaces = 0;

    std::vector<std::size_t> vertexMap(mesh.vert.size(), std::size_t(-1));
    points.reserve(std::max(0, mesh.VN()));
    for (std::size_t i = 0; i < mesh.vert.size(); ++i) {
        const VCGVertex &v = mesh.vert[i];
        if (v.IsD())
            continue;
        vertexMap[i] = points.size();
        points.emplace_back(v.cP().X(), v.cP().Y(), v.cP().Z());
    }

    const VCGVertex *vertexBase = mesh.vert.empty() ? nullptr : &mesh.vert.front();
    triangles.reserve(std::max(0, mesh.FN()));
    for (const VCGFace &face : mesh.face) {
        if (face.IsD())
            continue;

        Triangle tri{};
        bool valid = true;
        for (int k = 0; k < 3; ++k) {
            const VCGVertex *v = face.cV(k);
            if (!v || !vertexBase) {
                valid = false;
                break;
            }
            const ptrdiff_t rawIndex = v - vertexBase;
            if (rawIndex < 0 || std::size_t(rawIndex) >= vertexMap.size()
                || vertexMap[std::size_t(rawIndex)] == std::size_t(-1)) {
                valid = false;
                break;
            }
            tri[std::size_t(k)] = vertexMap[std::size_t(rawIndex)];
        }

        if (!valid) {
            ++skippedFaces;
            continue;
        }
        if (tri[0] == tri[1] || tri[1] == tri[2] || tri[2] == tri[0]) {
            ++skippedFaces;
            continue;
        }
        triangles.push_back(tri);
    }

    if (points.empty()) {
        error = QObject::tr("Alpha Wrap requires at least one valid vertex.");
        return false;
    }
    if (triangles.empty()) {
        error = QObject::tr("Alpha Wrap requires at least one valid triangular face.");
        return false;
    }
    return true;
}

bool copyCgalMeshToVcg(const CgalMesh &source, VCGMesh &target, QString &error)
{
    target.Clear();
    if (source.number_of_vertices() == 0 || source.number_of_faces() == 0) {
        error = QObject::tr("CGAL Alpha Wrap produced an empty mesh.");
        return false;
    }

    std::vector<int> vertexMap(source.number_of_vertices(), -1);
    vcg::tri::Allocator<VCGMesh>::AddVertices(target, int(source.number_of_vertices()));
    int dstIndex = 0;
    for (CgalMesh::Vertex_index v : source.vertices()) {
        const CgalPoint &p = source.point(v);
        target.vert[std::size_t(dstIndex)].P() = vcg::Point3f(float(p.x()), float(p.y()), float(p.z()));
        if (std::size_t(v.idx()) >= vertexMap.size()) {
            error = QObject::tr("CGAL Alpha Wrap returned a mesh with unexpected vertex indexing.");
            return false;
        }
        vertexMap[std::size_t(v.idx())] = dstIndex;
        ++dstIndex;
    }

    for (CgalMesh::Face_index f : source.faces()) {
        std::array<int, 3> faceVertices{};
        int count = 0;
        for (CgalMesh::Vertex_index v : CGAL::vertices_around_face(source.halfedge(f), source)) {
            if (count >= 3) {
                error = QObject::tr("CGAL Alpha Wrap returned a non-triangular face.");
                return false;
            }
            const std::size_t raw = std::size_t(v.idx());
            if (raw >= vertexMap.size() || vertexMap[raw] < 0) {
                error = QObject::tr("CGAL Alpha Wrap returned an invalid face vertex reference.");
                return false;
            }
            faceVertices[std::size_t(count)] = vertexMap[raw];
            ++count;
        }
        if (count != 3) {
            error = QObject::tr("CGAL Alpha Wrap returned a degenerate face.");
            return false;
        }
        vcg::tri::Allocator<VCGMesh>::AddFace(
            target,
            faceVertices[0],
            faceVertices[1],
            faceVertices[2]);
    }

    vcg::tri::Allocator<VCGMesh>::CompactEveryVector(target);
    vcg::tri::UpdateBounding<VCGMesh>::Box(target);
    vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(target);
    return true;
}

} // namespace

QString CgalFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.cgal");
}

QString CgalFilterPlugin::name() const
{
    return QObject::tr("CGAL Mesh Filters");
}

MeshFilterRunResult CgalFilterPlugin::runFilter(
    const QString &filterId,
    const FilterParams &params,
    Document &doc) const
{
    if (filterId != QString::fromLatin1(kFilterAlphaWrap))
        return fail(QObject::tr("Unknown filter id: %1").arg(filterId));

    const int meshIndex = doc.currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));

    const Document::MeshEntry &entry = doc.mesh(meshIndex);
    const VCGMesh &mesh = entry.mesh;
    if (mesh.VN() <= 0 || mesh.FN() <= 0)
        return fail(QObject::tr("Alpha Wrap requires a non-empty triangular mesh."));

    const double alpha = params.getDouble(QStringLiteral("Alpha"), 0.0);
    const double offset = params.getDouble(QStringLiteral("Offset"), 0.0);
    if (!std::isfinite(alpha) || alpha <= 0.0)
        return fail(QObject::tr("Alpha must be a finite value larger than zero."));
    if (!std::isfinite(offset) || offset <= 0.0)
        return fail(QObject::tr("Offset must be a finite value larger than zero."));

    doc.beginFilterProgress(QObject::tr("Alpha Wrap"));
    if (vcg::CallBackPos *cb = doc.progressCallback())
        (*cb)(5, "Preparing triangle soup...");

    std::vector<CgalPoint> points;
    std::vector<Triangle> triangles;
    int skippedFaces = 0;
    QString error;
    if (!buildTriangleSoup(mesh, points, triangles, skippedFaces, error)) {
        doc.finishFilterProgress(false, error);
        return fail(error);
    }

    CgalMesh wrapResult;
    try {
        if (vcg::CallBackPos *cb = doc.progressCallback())
            (*cb)(15, "Running CGAL Alpha Wrap...");
        CGAL::alpha_wrap_3(points, triangles, alpha, offset, wrapResult);
    } catch (const std::exception &e) {
        const QString message = QObject::tr("CGAL Alpha Wrap failed: %1").arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    } catch (...) {
        const QString message = QObject::tr("CGAL Alpha Wrap failed with an unknown error.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    if (vcg::CallBackPos *cb = doc.progressCallback())
        (*cb)(90, "Converting CGAL output mesh...");

    VCGMesh output;
    if (!copyCgalMeshToVcg(wrapResult, output, error)) {
        doc.finishFilterProgress(false, error);
        return fail(error);
    }

    const QString newName = QObject::tr("Alpha wrap");
    const int ioMask = Mask::IOM_VERTCOORD | Mask::IOM_VERTNORMAL | Mask::IOM_FACENORMAL;
    const int newIndex = doc.addMesh(output, newName, ioMask);
    if (newIndex < 0) {
        const QString message = QObject::tr("Failed to add Alpha Wrap result to the document.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    doc.finishFilterProgress(true, QObject::tr("Generated Alpha Wrap mesh."));

    QStringList info;
    info << QObject::tr("Created mesh '%1'.").arg(doc.mesh(newIndex).name)
         << QObject::tr("Alpha: %1").arg(QString::number(alpha, 'g', 6))
         << QObject::tr("Offset: %1").arg(QString::number(offset, 'g', 6))
         << QObject::tr("Input triangle soup: %1 vertices, %2 faces.").arg(points.size()).arg(triangles.size())
         << QObject::tr("Output mesh: %1 vertices, %2 faces.").arg(output.VN()).arg(output.FN());
    if (skippedFaces > 0)
        info << QObject::tr("Skipped %1 invalid or degenerate input face(s).").arg(skippedFaces);

    return success(info, newIndex);
}

void registerCgalFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<CgalFilterPlugin>());
}
