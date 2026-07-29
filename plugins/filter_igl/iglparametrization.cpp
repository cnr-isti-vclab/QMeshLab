#include "iglparametrization.h"

#include "document.h"
#include "filterparam.h"
#include "libiglmeshadapter.h"
#include "meshfilterpluginmanager.h"

#include <igl/boundary_loop.h>
#include <igl/harmonic.h>
#include <igl/lscm.h>
#include <igl/map_vertices_to_circle.h>
#include <wrap/io_trimesh/io_mask.h>
#include <QObject>
#include <algorithm>
#include <exception>
#include <memory>

namespace {

constexpr QLatin1StringView kHarmonic("compute_texcoord_parametrization_harmonic");
constexpr QLatin1StringView kLscm("compute_texcoord_parametrization_least_squares_conformal_maps");
using Mask = vcg::tri::io::Mask;
namespace IglAdapter = qmeshlab::libigl;

MeshFilterRunResult fail(const QString &message)
{
    return { false, false, message };
}

MeshFilterRunResult success(const QStringList &info)
{
    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.infoMessages = info;
    return result;
}

bool writeVertexTexcoords(
    Document::MeshEntry &entry,
    const IglAdapter::EigenMesh &source,
    const Eigen::MatrixXd &uv,
    QString &error)
{
    if (uv.rows() != source.vertices.rows() || uv.cols() < 2) {
        error = QObject::tr("libigl returned an unexpected UV matrix.");
        return false;
    }

    VCGMesh &mesh = entry.mesh;
    mesh.vert.EnableTexCoord();
    for (Eigen::Index row = 0; row < uv.rows(); ++row) {
        const int sourceIndex = source.vertexToSourceIndex[size_t(row)];
        if (sourceIndex < 0 || size_t(sourceIndex) >= mesh.vert.size())
            continue;
        VCGVertex &vertex = mesh.vert[size_t(sourceIndex)];
        if (vertex.IsD())
            continue;
        vertex.T().U() = float(uv(row, 0));
        vertex.T().V() = float(uv(row, 1));
        vertex.T().N() = 0;
    }

    if ((entry.ioMask & Mask::IOM_WEDGTEXCOORD) != 0) {
        mesh.face.EnableWedgeTexCoord();
        for (VCGFace &face : mesh.face) {
            if (face.IsD())
                continue;
            for (int corner = 0; corner < 3; ++corner) {
                if (const VCGVertex *vertex = face.cV(corner)) {
                    face.WT(corner).U() = vertex->cT().U();
                    face.WT(corner).V() = vertex->cT().V();
                    face.WT(corner).N() = vertex->cT().N();
                }
            }
        }
    }

    entry.ioMask |= Mask::IOM_VERTTEXCOORD;
    return true;
}

} // namespace

MeshFilterRunResult runIglParametrizationFilter(
    const QString &filterId,
    const FilterParams &params,
    Document &doc)
{
    const int meshIndex = doc.currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));

    Document::MeshEntry &entry = doc.mesh(meshIndex);
    if (entry.mesh.VN() <= 0 || entry.mesh.FN() <= 0)
        return fail(QObject::tr("Parametrization requires a mesh with vertices and faces."));

    IglAdapter::EigenMesh eigenMesh;
    QString error;
    if (!IglAdapter::meshToEigen(entry.mesh, eigenMesh, error))
        return fail(error);

    Eigen::VectorXi boundary;
    Eigen::MatrixXd uv;
    QString label;

    try {
        if (filterId == QString::fromLatin1(kHarmonic)) {
            const int harmonicOrder = std::max(1, params.getInt(QStringLiteral("harm_function")));
            label = QObject::tr("Harmonic Parametrization");
            doc.beginFilterProgress(label);
            if (vcg::CallBackPos *cb = doc.progressCallback())
                (*cb)(10, "Finding boundary loop...");

            igl::boundary_loop(eigenMesh.faces, boundary);
            if (boundary.size() == 0) {
                error = QObject::tr("Harmonic Parametrization can be applied only to meshes that have a boundary.");
                doc.finishFilterProgress(false, error);
                return fail(error);
            }
            if (boundary.size() >= eigenMesh.vertices.rows()) {
                error = QObject::tr(
                    "Harmonic Parametrization requires at least one non-boundary vertex.");
                doc.finishFilterProgress(false, error);
                return fail(error);
            }

            Eigen::MatrixXd boundaryUv;
            if (vcg::CallBackPos *cb = doc.progressCallback())
                (*cb)(30, "Mapping boundary to circle...");
            igl::map_vertices_to_circle(eigenMesh.vertices, boundary, boundaryUv);

            if (vcg::CallBackPos *cb = doc.progressCallback())
                (*cb)(55, "Solving harmonic map...");
            if (!igl::harmonic(
                    eigenMesh.vertices,
                    eigenMesh.faces,
                    boundary,
                    boundaryUv,
                    harmonicOrder,
                    uv)) {
                error = QObject::tr("Harmonic Parametrization failed.");
                doc.finishFilterProgress(false, error);
                return fail(error);
            }
        } else if (filterId == QString::fromLatin1(kLscm)) {
            label = QObject::tr("Least Squares Conformal Maps Parametrization");
            doc.beginFilterProgress(label);
            if (vcg::CallBackPos *cb = doc.progressCallback())
                (*cb)(10, "Finding boundary loop...");

            igl::boundary_loop(eigenMesh.faces, boundary);
            if (boundary.size() == 0) {
                error = QObject::tr(
                    "Least Squares Conformal Maps Parametrization can be applied only to meshes that have a boundary.");
                doc.finishFilterProgress(false, error);
                return fail(error);
            }

            Eigen::VectorXi boundaryPoints(2);
            boundaryPoints(0) = boundary(0);
            boundaryPoints(1) = boundary(boundary.size() / 2);

            Eigen::MatrixXd boundaryConditions(2, 2);
            boundaryConditions << 0.0, 0.0,
                                  1.0, 0.0;

            if (vcg::CallBackPos *cb = doc.progressCallback())
                (*cb)(55, "Solving LSCM map...");
            if (!igl::lscm(
                    eigenMesh.vertices,
                    eigenMesh.faces,
                    boundaryPoints,
                    boundaryConditions,
                    uv)) {
                error = QObject::tr("Least Squares Conformal Maps Parametrization failed.");
                doc.finishFilterProgress(false, error);
                return fail(error);
            }
        } else {
            return fail(QObject::tr("Unknown filter id: %1").arg(filterId));
        }
    } catch (const std::exception &e) {
        error = QObject::tr("libigl parametrization failed: %1").arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, error);
        return fail(error);
    } catch (...) {
        error = QObject::tr("libigl parametrization failed with an unknown error.");
        doc.finishFilterProgress(false, error);
        return fail(error);
    }

    if (vcg::CallBackPos *cb = doc.progressCallback())
        (*cb)(90, "Writing texture coordinates...");
    if (!writeVertexTexcoords(entry, eigenMesh, uv, error)) {
        doc.finishFilterProgress(false, error);
        return fail(error);
    }

    doc.markMeshGeometryChanged(
        meshIndex,
        QObject::tr("Computed %1 for '%2'").arg(label, entry.name));
    doc.finishFilterProgress(true, QObject::tr("Computed UV parametrization."));

    QStringList info;
    info << QObject::tr("Computed %1 for '%2'.").arg(label, entry.name)
         << QObject::tr("Parameterized vertices: %1").arg(eigenMesh.vertexToSourceIndex.size())
         << QObject::tr("Faces: %1").arg(eigenMesh.faces.rows());
    if (eigenMesh.skippedFaces > 0)
        info << QObject::tr("Skipped %1 invalid or degenerate face(s).").arg(eigenMesh.skippedFaces);

    return success(info);
}


bool isIglParametrizationFilter(const QString &filterId)
{
    return filterId == QString::fromLatin1(kHarmonic)
        || filterId == QString::fromLatin1(kLscm);
}
