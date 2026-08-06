#include "cgalfilterplugin.h"

#include "document.h"
#include "meshfilterpluginmanager.h"
#include "vcgmesh.h"

#include <CGAL/Alpha_shape_3.h>
#include <CGAL/Alpha_shape_cell_base_3.h>
#include <CGAL/Alpha_shape_vertex_base_3.h>
#include <CGAL/Delaunay_triangulation_3.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Triangulation_data_structure_3.h>
#include <CGAL/Triangulation_vertex_base_with_info_3.h>
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
#include <limits>
#include <unordered_map>
#include <vector>

namespace {

constexpr QLatin1StringView kFilterAlphaWrap("generate_alpha_wrap");
constexpr QLatin1StringView kFilterAlphaShape("generate_alpha_shape");
constexpr QLatin1StringView kFilterVoronoiFiltering("generate_voronoi_filtering");
using Mask = vcg::tri::io::Mask;
using Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
using CgalPoint = Kernel::Point_3;
using CgalMesh = CGAL::Surface_mesh<CgalPoint>;
using Triangle = std::array<std::size_t, 3>;

// Alpha shapes need their own triangulation types: the cell and vertex bases carry the
// per-simplex alpha intervals that classify each simplex as the alpha value sweeps.
using AsVertexBase = CGAL::Alpha_shape_vertex_base_3<Kernel>;
using AsCellBase = CGAL::Alpha_shape_cell_base_3<Kernel>;
using AsTds = CGAL::Triangulation_data_structure_3<AsVertexBase, AsCellBase>;
using AsTriangulation = CGAL::Delaunay_triangulation_3<Kernel, AsTds>;
using AlphaShape = CGAL::Alpha_shape_3<AsTriangulation>;

// Plain Delaunay for the pole-finding pass, and an info-carrying one for the second pass
// where sample points and poles must be told apart.
using PlainDelaunay = CGAL::Delaunay_triangulation_3<Kernel>;
using CrustVertexBase = CGAL::Triangulation_vertex_base_with_info_3<bool, Kernel>;
using CrustTds = CGAL::Triangulation_data_structure_3<CrustVertexBase>;
using CrustDelaunay = CGAL::Delaunay_triangulation_3<Kernel, CrustTds>;

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
    // An empty triangle list is allowed: the caller then wraps the bare point set.
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

// Alpha complex / alpha shape of the current layer's vertices.
//
// CGAL's alpha values are *squared* radii, so the user-facing radius is squared on the
// way in. The facet classification is what separates the two outputs: REGULAR facets are
// on the boundary of the alpha complex — the alpha shape proper — while SINGULAR facets
// are the lower-dimensional bits the complex keeps and the shape drops.
MeshFilterRunResult runAlphaShape(const FilterParams &params, Document &doc)
{
    const int meshIndex = doc.currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));

    const VCGMesh &mesh = doc.mesh(meshIndex).mesh;
    if (mesh.VN() < 4)
        return fail(QObject::tr("An alpha shape needs at least 4 vertices."));

    const double alpha = params.getDouble(QStringLiteral("alpha"), 0.0);
    if (!std::isfinite(alpha) || alpha <= 0.0)
        return fail(QObject::tr("Alpha must be a finite value larger than zero."));
    const bool wantComplex = params.getEnum(QStringLiteral("output")) == QStringLiteral("complex");

    std::vector<CgalPoint> points;
    points.reserve(std::size_t(std::max(0, mesh.VN())));
    for (const VCGVertex &v : mesh.vert) {
        if (v.IsD())
            continue;
        points.emplace_back(v.cP().X(), v.cP().Y(), v.cP().Z());
    }
    if (points.size() < 4)
        return fail(QObject::tr("An alpha shape needs at least 4 live vertices."));

    doc.beginFilterProgress(QObject::tr("Reconstruct Surface by Alpha Shape (CGAL)"));
    if (vcg::CallBackPos *cb = doc.progressCallback())
        (*cb)(10, "Building Delaunay triangulation...");

    // Everything that touches a Cell_handle must stay inside this scope: the handles point
    // into `shape`'s compact container, so consuming them after it dies is a use-after-free.
    VCGMesh output;
    try {
        AlphaShape shape(points.begin(), points.end(), Kernel::FT(alpha * alpha),
                         AlphaShape::GENERAL);
        if (vcg::CallBackPos *cb = doc.progressCallback())
            (*cb)(60, "Classifying facets...");

        std::vector<AlphaShape::Facet> facets;
        shape.get_alpha_shape_facets(std::back_inserter(facets), AlphaShape::REGULAR);
        if (wantComplex)
            shape.get_alpha_shape_facets(std::back_inserter(facets), AlphaShape::SINGULAR);

        if (facets.empty()) {
            const QString message = QObject::tr(
                "The alpha shape is empty at alpha = %1. Try a larger value.")
                    .arg(QString::number(alpha, 'g', 6));
            doc.finishFilterProgress(false, message);
            return fail(message);
        }

        if (vcg::CallBackPos *cb = doc.progressCallback())
            (*cb)(85, "Building output mesh...");

        std::unordered_map<const void *, int> vertexIndex;
        for (const AlphaShape::Facet &facet : facets) {
            const AlphaShape::Cell_handle cell = facet.first;
            const int opposite = facet.second;
            int corner[3];
            bool allFinite = true;
            for (int k = 0; k < 3 && allFinite; ++k) {
                auto handle = cell->vertex((opposite + k + 1) % 4);
                if (shape.is_infinite(handle)) {
                    allFinite = false;
                    break;
                }
                const void *key = &*handle;
                auto it = vertexIndex.find(key);
                if (it == vertexIndex.end()) {
                    const CgalPoint &p = handle->point();
                    auto vi = vcg::tri::Allocator<VCGMesh>::AddVertex(
                        output, vcg::Point3f(float(p.x()), float(p.y()), float(p.z())));
                    it = vertexIndex.emplace(key, int(vcg::tri::Index(output, *vi))).first;
                }
                corner[k] = it->second;
            }
            if (!allFinite)
                continue;
            if (corner[0] == corner[1] || corner[1] == corner[2] || corner[0] == corner[2])
                continue;
            vcg::tri::Allocator<VCGMesh>::AddFace(output, corner[0], corner[1], corner[2]);
        }
    } catch (const std::exception &e) {
        const QString message =
            QObject::tr("CGAL alpha shape failed: %1").arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    if (output.FN() <= 0) {
        const QString message = QObject::tr("The alpha shape produced no faces.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    // MeshLab stores the facet circumradius in face quality; keep that, it is what makes
    // the result explorable with the scalar histogram.
    for (VCGFace &f : output.face) {
        const float a = (f.cV(1)->cP() - f.cV(0)->cP()).Norm();
        const float b = (f.cV(2)->cP() - f.cV(1)->cP()).Norm();
        const float c = (f.cV(0)->cP() - f.cV(2)->cP()).Norm();
        const float area = vcg::DoubleArea(f) * 0.5f;
        f.Q() = (area > std::numeric_limits<float>::epsilon()) ? (a * b * c) / (4.0f * area) : 0.0f;
    }

    vcg::tri::Allocator<VCGMesh>::CompactEveryVector(output);
    vcg::tri::UpdateBounding<VCGMesh>::Box(output);
    vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(output);

    const int ioMask = Mask::IOM_VERTCOORD | Mask::IOM_VERTNORMAL | Mask::IOM_FACENORMAL
        | Mask::IOM_FACEQUALITY;
    const int newIndex = doc.addMesh(
        output,
        wantComplex ? QObject::tr("Alpha Complex") : QObject::tr("Alpha Shape"),
        ioMask);
    if (newIndex < 0) {
        const QString message = QObject::tr("Failed to add the alpha shape layer.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
    doc.finishFilterProgress(true, QObject::tr("Generated alpha shape."));

    QStringList info;
    info << QObject::tr("Created mesh '%1'.").arg(doc.mesh(newIndex).name)
         << QObject::tr("Alpha: %1").arg(QString::number(alpha, 'g', 6))
         << QObject::tr("Input: %1 points.").arg(points.size())
         << QObject::tr("Output mesh: %1 vertices, %2 faces.").arg(output.VN()).arg(output.FN());
    return success(info, newIndex);
}

// Voronoi filtering — the Amenta/Bern "crust". Two Delaunay passes:
//
//  1. Triangulate the samples. For each sample p, its *poles* are the two Voronoi
//     vertices of its cell farthest from it, one on each side: p+ is the farthest one,
//     p- the farthest lying in the opposite half-space. The poles approximate the medial
//     axis, so they sit far from the surface.
//  2. Triangulate samples *and* poles together. A Delaunay triangle whose three corners
//     are all samples cannot span the medial axis, so those triangles are the surface.
//
// The guarantee assumes a closed, well-sampled surface. Points on the convex hull have
// unbounded Voronoi cells and thus no finite outer pole, so boundaries stay ragged — a
// property of the algorithm, not of this implementation.
MeshFilterRunResult runVoronoiFiltering(const FilterParams &params, Document &doc)
{
    const int meshIndex = doc.currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));

    const VCGMesh &mesh = doc.mesh(meshIndex).mesh;
    if (mesh.VN() < 4)
        return fail(QObject::tr("Voronoi filtering needs at least 4 vertices."));

    const double threshold = params.getDouble(QStringLiteral("threshold"), 10.0);
    if (!std::isfinite(threshold) || threshold <= 0.0)
        return fail(QObject::tr("Threshold must be a finite value larger than zero."));

    std::vector<CgalPoint> samples;
    samples.reserve(std::size_t(std::max(0, mesh.VN())));
    for (const VCGVertex &v : mesh.vert) {
        if (v.IsD())
            continue;
        samples.emplace_back(v.cP().X(), v.cP().Y(), v.cP().Z());
    }
    if (samples.size() < 4)
        return fail(QObject::tr("Voronoi filtering needs at least 4 live vertices."));

    // Voronoi vertices of an almost-degenerate cell shoot off to infinity; anything
    // farther than this from its sample is discarded rather than used as a pole.
    const double diagonal = double(mesh.bbox.Diag());
    const double maxPoleDistance = (diagonal > 0.0 ? diagonal : 1.0) * threshold;

    doc.beginFilterProgress(QObject::tr("Reconstruct Surface by Voronoi Filtering (CGAL)"));
    if (vcg::CallBackPos *cb = doc.progressCallback())
        (*cb)(10, "Triangulating samples...");

    VCGMesh output;
    int poleCount = 0;
    try {
        PlainDelaunay delaunay(samples.begin(), samples.end());
        if (vcg::CallBackPos *cb = doc.progressCallback())
            (*cb)(35, "Selecting poles...");

        std::vector<CgalPoint> poles;
        std::vector<PlainDelaunay::Cell_handle> cells;
        for (auto v = delaunay.finite_vertices_begin(); v != delaunay.finite_vertices_end(); ++v) {
            const CgalPoint &p = v->point();
            cells.clear();
            delaunay.finite_incident_cells(v, std::back_inserter(cells));

            // First pole: farthest Voronoi vertex of this cell.
            CgalPoint firstPole;
            double bestSquared = -1.0;
            for (const auto &cell : cells) {
                const CgalPoint dual = delaunay.dual(cell);
                const double squared = CGAL::squared_distance(p, dual);
                if (squared > maxPoleDistance * maxPoleDistance)
                    continue;
                if (squared > bestSquared) {
                    bestSquared = squared;
                    firstPole = dual;
                }
            }
            if (bestSquared <= 0.0)
                continue; // unbounded or degenerate cell: no usable pole
            poles.push_back(firstPole);

            // Second pole: farthest Voronoi vertex on the other side of the surface.
            const Kernel::Vector_3 outward = firstPole - p;
            CgalPoint secondPole;
            double bestOpposite = -1.0;
            for (const auto &cell : cells) {
                const CgalPoint dual = delaunay.dual(cell);
                if ((dual - p) * outward >= 0.0)
                    continue;
                const double squared = CGAL::squared_distance(p, dual);
                if (squared > maxPoleDistance * maxPoleDistance)
                    continue;
                if (squared > bestOpposite) {
                    bestOpposite = squared;
                    secondPole = dual;
                }
            }
            if (bestOpposite > 0.0)
                poles.push_back(secondPole);
        }
        poleCount = int(poles.size());
        if (poles.empty()) {
            const QString message = QObject::tr(
                "No poles could be selected. The point set may be too small, degenerate, or "
                "entirely on its convex hull.");
            doc.finishFilterProgress(false, message);
            return fail(message);
        }

        if (vcg::CallBackPos *cb = doc.progressCallback())
            (*cb)(60, "Triangulating samples and poles...");

        // Second pass. Samples are flagged true, poles false.
        std::vector<std::pair<CgalPoint, bool>> tagged;
        tagged.reserve(samples.size() + poles.size());
        for (const CgalPoint &p : samples)
            tagged.emplace_back(p, true);
        for (const CgalPoint &p : poles)
            tagged.emplace_back(p, false);
        CrustDelaunay crust(tagged.begin(), tagged.end());

        if (vcg::CallBackPos *cb = doc.progressCallback())
            (*cb)(85, "Extracting the crust...");

        std::unordered_map<const void *, int> vertexIndex;
        for (auto facet = crust.finite_facets_begin(); facet != crust.finite_facets_end(); ++facet) {
            const CrustDelaunay::Cell_handle cell = facet->first;
            const int opposite = facet->second;
            int corner[3];
            bool allSamples = true;
            for (int k = 0; k < 3 && allSamples; ++k) {
                auto handle = cell->vertex((opposite + k + 1) % 4);
                if (crust.is_infinite(handle) || !handle->info()) {
                    allSamples = false;
                    break;
                }
                const void *key = &*handle;
                auto it = vertexIndex.find(key);
                if (it == vertexIndex.end()) {
                    const CgalPoint &p = handle->point();
                    auto vi = vcg::tri::Allocator<VCGMesh>::AddVertex(
                        output, vcg::Point3f(float(p.x()), float(p.y()), float(p.z())));
                    it = vertexIndex.emplace(key, int(vcg::tri::Index(output, *vi))).first;
                }
                corner[k] = it->second;
            }
            if (!allSamples)
                continue;
            if (corner[0] == corner[1] || corner[1] == corner[2] || corner[0] == corner[2])
                continue;
            vcg::tri::Allocator<VCGMesh>::AddFace(output, corner[0], corner[1], corner[2]);
        }
    } catch (const std::exception &e) {
        const QString message =
            QObject::tr("CGAL Voronoi filtering failed: %1").arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    if (output.FN() <= 0) {
        const QString message = QObject::tr(
            "Voronoi filtering produced no faces. The sampling may be too sparse or too noisy.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    vcg::tri::Allocator<VCGMesh>::CompactEveryVector(output);
    vcg::tri::UpdateBounding<VCGMesh>::Box(output);
    vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(output);

    const int ioMask = Mask::IOM_VERTCOORD | Mask::IOM_VERTNORMAL | Mask::IOM_FACENORMAL;
    const int newIndex = doc.addMesh(output, QObject::tr("Voronoi Filtering"), ioMask);
    if (newIndex < 0) {
        const QString message = QObject::tr("Failed to add the Voronoi filtering layer.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
    doc.finishFilterProgress(true, QObject::tr("Generated crust surface."));

    QStringList info;
    info << QObject::tr("Created mesh '%1'.").arg(doc.mesh(newIndex).name)
         << QObject::tr("Input: %1 samples, %2 poles.").arg(samples.size()).arg(poleCount)
         << QObject::tr("Output mesh: %1 vertices, %2 faces.").arg(output.VN()).arg(output.FN());
    return success(info, newIndex);
}

} // namespace

QString CgalFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.cgal");
}

QString CgalFilterPlugin::name() const
{
    return QObject::tr("CGAL Filters");
}

MeshFilterRunResult CgalFilterPlugin::runFilter(
    const QString &filterId,
    const FilterParams &params,
    Document &doc) const
{
    if (filterId == QString::fromLatin1(kFilterAlphaShape))
        return runAlphaShape(params, doc);
    if (filterId == QString::fromLatin1(kFilterVoronoiFiltering))
        return runVoronoiFiltering(params, doc);
    if (filterId != QString::fromLatin1(kFilterAlphaWrap))
        return fail(QObject::tr("Unknown filter id: %1").arg(filterId));

    const int meshIndex = doc.currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));

    const Document::MeshEntry &entry = doc.mesh(meshIndex);
    const VCGMesh &mesh = entry.mesh;
    // Faces are optional: without them the point set itself is wrapped.
    if (mesh.VN() <= 0)
        return fail(QObject::tr("Alpha Wrap requires a mesh or point cloud with vertices."));

    const double alpha = params.getDouble(QStringLiteral("Alpha"), 0.0);
    const double offset = params.getDouble(QStringLiteral("Offset"), 0.0);
    if (!std::isfinite(alpha) || alpha <= 0.0)
        return fail(QObject::tr("Alpha must be a finite value larger than zero."));
    if (!std::isfinite(offset) || offset <= 0.0)
        return fail(QObject::tr("Offset must be a finite value larger than zero."));

    doc.beginFilterProgress(QObject::tr("Reconstruct Surface by Alpha Wrapping"));
    if (vcg::CallBackPos *cb = doc.progressCallback())
        (*cb)(5, "Preparing alpha wrap input...");

    std::vector<CgalPoint> points;
    std::vector<Triangle> triangles;
    int skippedFaces = 0;
    QString error;
    if (!buildTriangleSoup(mesh, points, triangles, skippedFaces, error)) {
        doc.finishFilterProgress(false, error);
        return fail(error);
    }
    const bool pointSetInput = triangles.empty();

    CgalMesh wrapResult;
    try {
        if (vcg::CallBackPos *cb = doc.progressCallback())
            (*cb)(15, "Running CGAL Alpha Wrap...");
        // CGAL wraps a bare point set as happily as a triangle soup; no normals needed,
        // since the strictly positive offset is what defines the envelope.
        if (pointSetInput)
            CGAL::alpha_wrap_3(points, alpha, offset, wrapResult);
        else
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
         << (pointSetInput
                 ? QObject::tr("Input point set: %1 points.").arg(points.size())
                 : QObject::tr("Input triangle soup: %1 vertices, %2 faces.")
                       .arg(points.size()).arg(triangles.size()))
         << QObject::tr("Output mesh: %1 vertices, %2 faces.").arg(output.VN()).arg(output.FN());
    if (skippedFaces > 0)
        info << QObject::tr("Skipped %1 invalid or degenerate input face(s).").arg(skippedFaces);

    return success(info, newIndex);
}

void registerCgalFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<CgalFilterPlugin>());
}
