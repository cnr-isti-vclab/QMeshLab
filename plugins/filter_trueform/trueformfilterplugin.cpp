#include "plugins/filter_trueform/trueformfilterplugin.h"

#include "document.h"
#include "filterparam.h"
#include "meshfilterpluginmanager.h"
#include "vcgmesh.h"

#include <QMatrix3x3>
#include <QMatrix4x4>
#include <QObject>
#include <QList>
#include <QStringList>
#include <QVector3D>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <exception>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <vcg/complex/append.h>
#include <vcg/complex/allocate.h>
#include <wrap/io_trimesh/io_mask.h>

// Qt's keyword macros collide with ordinary identifiers inside TrueForm and oneTBB:
// `emit` hits tbb::profiling::event::emit(), and `slots` hits local variables named
// slots. Both expand to nothing, so the declarations become syntactically invalid far
// from the real cause. Hide all three keyword macros across the include.
#pragma push_macro("emit")
#pragma push_macro("slots")
#pragma push_macro("signals")
#undef emit
#undef slots
#undef signals
#include <trueform/trueform.hpp>
#pragma pop_macro("signals")
#pragma pop_macro("slots")
#pragma pop_macro("emit")

namespace {

constexpr QLatin1StringView kFilterAlignObb("compute_matrix_by_obb_alignment");
constexpr QLatin1StringView kFilterAlignIcp("compute_matrix_by_icp_trueform");
constexpr QLatin1StringView kFilterAlignCorresponding("compute_matrix_by_corresponding_points");
constexpr QLatin1StringView kFilterUnion("generate_boolean_union_trueform");
constexpr QLatin1StringView kFilterIntersection("generate_boolean_intersection_trueform");
constexpr QLatin1StringView kFilterDifference("generate_boolean_difference_trueform");
constexpr QLatin1StringView kFilterSymmetricDifference("generate_boolean_xor_trueform");
constexpr QLatin1StringView kFilterCsg("generate_csg_expression");
constexpr QLatin1StringView kFilterOuterShell("generate_outer_shell");
constexpr QLatin1StringView kFilterSelfIntersectionCurves("generate_polyline_from_self_intersections");
constexpr QLatin1StringView kFilterIntersectionCurves("generate_polyline_from_mesh_intersection");
constexpr QLatin1StringView kFilterIsocurves("generate_polyline_from_scalar_isocontour");
constexpr QLatin1StringView kFilterTube("generate_tube_from_polyline");
constexpr QLatin1StringView kFilterSignedDistance("compute_scalar_by_signed_distance_per_vertex");
constexpr QLatin1StringView kFilterSelectInside("select_vertices_inside_mesh");
constexpr QLatin1StringView kFilterChamfer("compute_chamfer_distance");
constexpr QLatin1StringView kFilterLaplacian("apply_laplacian_smoothing_trueform");
constexpr QLatin1StringView kFilterTaubin("apply_taubin_smoothing_trueform");
constexpr QLatin1StringView kFilterCurvature("compute_scalar_by_curvature_trueform");
constexpr QLatin1StringView kFilterNormals("compute_normals_trueform");
constexpr QLatin1StringView kFilterIsotropic("remeshing_isotropic_trueform");
constexpr QLatin1StringView kFilterSimplify("simplification_by_error_trueform");
constexpr QLatin1StringView kFilterDecimate("simplification_by_decimation_trueform");
constexpr QLatin1StringView kFilterOrientCoherent("orient_faces_coherently_trueform");
constexpr QLatin1StringView kFilterOrientOutward("orient_faces_outward_trueform");
constexpr QLatin1StringView kFilterSelectCrease("select_crease_edges_trueform");
constexpr QLatin1StringView kFilterSelectNonManifold("select_non_manifold_edges_trueform");
constexpr QLatin1StringView kFilterRepairSelfIntersections("repair_self_intersections");
constexpr QLatin1StringView kFilterCutIsocontour("cut_along_scalar_isocontour");
constexpr QLatin1StringView kFilterClean("remove_duplicate_vertices_trueform");
constexpr QLatin1StringView kFilterImprove("improve_triangulation_trueform");

using Mask = vcg::tri::io::Mask;
using TfPoints = tf::points_buffer<float, 3>;
using TfTree = tf::aabb_tree<int, float, 3>;

MeshFilterRunResult fail(const QString &message)
{
    MeshFilterRunResult result;
    result.success = false;
    result.documentModified = false;
    result.errorMessage = message;
    return result;
}

MeshFilterRunResult success(const QStringList &info)
{
    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.infoMessages = info;
    return result;
}

// Collect a layer's live vertices in world space. Alignment is only meaningful across
// layers, so the layer matrix has to be applied before anything is compared.
TfPoints worldPoints(const Document::MeshEntry &entry)
{
    TfPoints points;
    const QMatrix4x4 &m = entry.transform;
    for (const VCGVertex &v : entry.mesh.vert) {
        if (v.IsD())
            continue;
        const QVector3D p = m.map(QVector3D(v.cP().X(), v.cP().Y(), v.cP().Z()));
        points.emplace_back(p.x(), p.y(), p.z());
    }
    return points;
}

// The reference layer's own vertex normals, rotated into world space. Point-to-plane
// ICP needs a normal per reference point; QMeshLab already maintains these, and the user
// controls them through the Compute/Orient normal filters, so they are a better source
// than recomputing from the faces here.
tf::unit_vectors_buffer<float, 3> worldNormals(const Document::MeshEntry &entry)
{
    tf::unit_vectors_buffer<float, 3> normals;
    const QMatrix3x3 n = entry.transform.normalMatrix();
    for (const VCGVertex &v : entry.mesh.vert) {
        if (v.IsD())
            continue;
        const vcg::Point3f &s = v.cN();
        QVector3D t(
            n(0, 0) * s.X() + n(0, 1) * s.Y() + n(0, 2) * s.Z(),
            n(1, 0) * s.X() + n(1, 1) * s.Y() + n(1, 2) * s.Z(),
            n(2, 0) * s.X() + n(2, 1) * s.Y() + n(2, 2) * s.Z());
        if (t.lengthSquared() < 1e-20f)
            t = QVector3D(0.0f, 0.0f, 1.0f); // degenerate normal: pick something unit
        t.normalize();
        normals.emplace_back(t.x(), t.y(), t.z());
    }
    return normals;
}

// TrueForm stores an affine transform as Dims x (Dims+1): the last column is the
// translation. Widen it to the 4x4 QMeshLab keeps on a layer.
template <typename Transformation>
QMatrix4x4 toQMatrix(const Transformation &t)
{
    QMatrix4x4 m; // identity
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c)
            m(r, c) = float(t(std::size_t(r), std::size_t(c)));
    }
    return m;
}

struct AlignmentInputs
{
    int sourceIndex = -1;
    int referenceIndex = -1;
};

// Resolve and validate the two layers every alignment filter works on.
bool resolveInputs(const FilterParams &params, Document &doc, AlignmentInputs &out, QString &error)
{
    out.sourceIndex = params.getMesh(QStringLiteral("sourceMesh"), doc.currentMeshIndex());
    out.referenceIndex = params.getMesh(QStringLiteral("referenceMesh"), -1);

    const auto valid = [&doc](int i) { return i >= 0 && i < doc.meshCount(); };
    if (!valid(out.sourceIndex) || !valid(out.referenceIndex)) {
        error = QObject::tr("Both a source and a reference layer must be selected.");
        return false;
    }
    if (out.sourceIndex == out.referenceIndex) {
        error = QObject::tr("The source and reference layers must be different.");
        return false;
    }
    if (doc.mesh(out.sourceIndex).mesh.VN() < 3 || doc.mesh(out.referenceIndex).mesh.VN() < 3) {
        error = QObject::tr("Both layers need at least 3 vertices.");
        return false;
    }
    return true;
}

// Compose the newly found world-space alignment onto the source layer's existing matrix
// and store it. Alignment moves the layer, it never touches vertex coordinates.
MeshFilterRunResult applyAlignment(
    Document &doc, int sourceIndex, const QMatrix4x4 &worldDelta, QStringList info)
{
    const QMatrix4x4 updated = worldDelta * doc.mesh(sourceIndex).transform;
    doc.setMeshTransform(sourceIndex, updated);
    doc.finishFilterProgress(true, QObject::tr("Aligned layer."));
    return success(info);
}

// Coarse alignment from the two point sets' oriented bounding boxes. Fast and needs no
// initial guess, but an OBB is only defined up to 180-degree flips about its axes; when
// a tree is supplied TrueForm tries the candidates and keeps the best.
MeshFilterRunResult runAlignObb(const FilterParams &params, Document &doc)
{
    AlignmentInputs in;
    QString error;
    if (!resolveInputs(params, doc, in, error))
        return fail(error);

    doc.beginFilterProgress(QObject::tr("Align by Bounding Box"));
    QMatrix4x4 delta;
    try {
        const TfPoints source = worldPoints(doc.mesh(in.sourceIndex));
        const TfPoints reference = worldPoints(doc.mesh(in.referenceIndex));
        TfTree tree(tf::make_points(reference), tf::config_tree(4, 4));
        const auto t = tf::fit_obb_alignment(
            tf::make_points(source), tf::make_points(reference) | tf::tag(tree));
        delta = toQMatrix(t);
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm bounding-box alignment failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    return applyAlignment(
        doc, in.sourceIndex, delta,
        { QObject::tr("Aligned '%1' to '%2' by oriented bounding box.")
              .arg(doc.mesh(in.sourceIndex).name, doc.mesh(in.referenceIndex).name) });
}

// Iterative closest point. Point-to-plane uses the reference's vertex normals and
// converges in far fewer iterations on smooth surfaces; point-to-point is the safer
// choice when those normals are unreliable.
MeshFilterRunResult runAlignIcp(const FilterParams &params, Document &doc)
{
    AlignmentInputs in;
    QString error;
    if (!resolveInputs(params, doc, in, error))
        return fail(error);

    const bool pointToPlane =
        params.getEnum(QStringLiteral("metric")) == QStringLiteral("point_to_plane");
    const bool coarseFirst = params.getBool(QStringLiteral("coarseInit"), true);
    tf::icp_config cfg;
    cfg.max_iterations = std::size_t(std::max(1, params.getInt(QStringLiteral("maxIterations"), 50)));
    cfg.n_samples = std::size_t(std::max(0, params.getInt(QStringLiteral("samples"), 1000)));
    cfg.min_relative_improvement =
        float(params.getDouble(QStringLiteral("minImprovement"), 0.001));
    cfg.outlier_proportion =
        float(std::clamp(params.getDouble(QStringLiteral("outlierProportion"), 0.0), 0.0, 0.9));

    doc.beginFilterProgress(QObject::tr("Align by ICP (TrueForm)"));
    QMatrix4x4 delta;
    double residual = 0.0;
    try {
        const TfPoints source = worldPoints(doc.mesh(in.sourceIndex));
        const TfPoints reference = worldPoints(doc.mesh(in.referenceIndex));
        auto sourcePts = tf::make_points(source);
        auto referencePts = tf::make_points(reference);
        TfTree tree(referencePts, tf::config_tree(4, 4));
        auto referenceWithTree = referencePts | tf::tag(tree);

        if (vcg::CallBackPos *cb = doc.progressCallback())
            (*cb)(20, coarseFirst ? "Coarse bounding-box alignment..." : "Running ICP...");

        // Start from the OBB fit when asked: ICP only refines locally, so a bad start
        // converges to a wrong local minimum rather than failing visibly.
        auto initial = coarseFirst ? tf::fit_obb_alignment(sourcePts, referenceWithTree)
                                   : tf::make_identity_transformation<float, 3>();

        if (vcg::CallBackPos *cb = doc.progressCallback())
            (*cb)(45, "Running ICP...");

        // fit_icp_alignment returns the delta relative to the initial guess.
        const auto referenceNormals = worldNormals(doc.mesh(in.referenceIndex));
        const auto refined = pointToPlane
            ? tf::fit_icp_alignment(
                  sourcePts | tf::tag(initial),
                  referenceWithTree | tf::tag_normals(referenceNormals.unit_vectors()),
                  cfg)
            : tf::fit_icp_alignment(sourcePts | tf::tag(initial), referenceWithTree, cfg);
        const auto total = tf::transformed(initial, refined);
        delta = toQMatrix(total);

        residual = double(tf::chamfer_error(sourcePts | tf::tag(total), referenceWithTree));
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm ICP failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    return applyAlignment(
        doc, in.sourceIndex, delta,
        { QObject::tr("Aligned '%1' to '%2'.")
              .arg(doc.mesh(in.sourceIndex).name, doc.mesh(in.referenceIndex).name),
          pointToPlane ? QObject::tr("Metric: point-to-plane.")
                       : QObject::tr("Metric: point-to-point."),
          QObject::tr("Chamfer residual: %1").arg(QString::number(residual, 'g', 6)) });
}

// Procrustes alignment of two layers whose vertices already correspond one to one, in
// order. Unlike ICP this can also solve for uniform scale, because the correspondences
// are given rather than guessed.
MeshFilterRunResult runAlignCorresponding(const FilterParams &params, Document &doc)
{
    AlignmentInputs in;
    QString error;
    if (!resolveInputs(params, doc, in, error))
        return fail(error);

    const bool allowScale = params.getBool(QStringLiteral("allowScale"), false);

    const TfPoints source = worldPoints(doc.mesh(in.sourceIndex));
    const TfPoints reference = worldPoints(doc.mesh(in.referenceIndex));
    if (source.size() != reference.size()) {
        return fail(QObject::tr(
            "This filter needs one-to-one correspondences: both layers must have the same "
            "number of live vertices, in matching order ('%1' has %2, '%3' has %4). Use "
            "'Align by ICP' when the correspondence is unknown.")
                        .arg(doc.mesh(in.sourceIndex).name)
                        .arg(source.size())
                        .arg(doc.mesh(in.referenceIndex).name)
                        .arg(reference.size()));
    }

    doc.beginFilterProgress(QObject::tr("Align to Corresponding Points"));
    QMatrix4x4 delta;
    double fittedScale = 1.0;
    try {
        auto sourcePts = tf::make_points(source);
        auto referencePts = tf::make_points(reference);
        const auto rigid = tf::fit_rigid_alignment(sourcePts, referencePts);
        if (!allowScale) {
            delta = toQMatrix(rigid);
        } else {
            // Deliberately not tf::fit_similarity_alignment: as of the pinned commit it
            // returns a scale n times too small. cross_covariance_of() divides H by the
            // point count, but the similarity fit divides trace(R^T H) by an
            // unnormalised sum of squares, so the two disagree by a factor of n.
            // fit_rigid_alignment is unaffected — rotation is scale-invariant, so the
            // 1/n cancels through the SVD — so the rotation is taken from there and only
            // scale and translation are recovered here. Revisit when upstream fixes it.
            delta = toQMatrix(rigid); // rotation only; scale/translation replaced below
            const std::size_t n = source.size();
            double cx[3] = { 0, 0, 0 };
            double cy[3] = { 0, 0, 0 };
            for (std::size_t i = 0; i < n; ++i) {
                for (int d = 0; d < 3; ++d) {
                    cx[d] += double(source[i][std::size_t(d)]);
                    cy[d] += double(reference[i][std::size_t(d)]);
                }
            }
            for (int d = 0; d < 3; ++d) {
                cx[d] /= double(n);
                cy[d] /= double(n);
            }

            // s = sum((y - cy) . R (x - cx)) / sum(||x - cx||^2)
            double numerator = 0.0;
            double denominator = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                double dx[3];
                double dy[3];
                for (int d = 0; d < 3; ++d) {
                    dx[d] = double(source[i][std::size_t(d)]) - cx[d];
                    dy[d] = double(reference[i][std::size_t(d)]) - cy[d];
                }
                for (int r = 0; r < 3; ++r) {
                    double rotated = 0.0;
                    for (int c = 0; c < 3; ++c)
                        rotated += double(delta(r, c)) * dx[c];
                    numerator += dy[r] * rotated;
                    denominator += dx[r] * dx[r];
                }
            }
            const double scale = (denominator > 1e-20) ? (numerator / denominator) : 1.0;

            for (int r = 0; r < 3; ++r) {
                double rotatedCx = 0.0;
                for (int c = 0; c < 3; ++c)
                    rotatedCx += double(delta(r, c)) * cx[c];
                for (int c = 0; c < 3; ++c)
                    delta(r, c) = float(scale * double(delta(r, c)));
                delta(r, 3) = float(cy[r] - scale * rotatedCx);
            }
            fittedScale = scale;
        }
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm alignment failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    return applyAlignment(
        doc, in.sourceIndex, delta,
        { QObject::tr("Aligned '%1' to '%2' over %3 corresponding point(s).")
              .arg(doc.mesh(in.sourceIndex).name, doc.mesh(in.referenceIndex).name)
              .arg(source.size()),
          allowScale
              ? QObject::tr("Similarity fit: rotation, translation and uniform scale %1.")
                    .arg(QString::number(fittedScale, 'g', 6))
              : QObject::tr("Rigid fit: rotation and translation only.") });
}

// ---------------------------------------------------------------------------
// Mesh conversion shared by the boolean/CSG filters
// ---------------------------------------------------------------------------

using TfMesh = tf::polygons_buffer<int, float, 3, 3>;

// A layer's triangles in world space. Booleans across layers are only meaningful once
// each layer's matrix has been applied.
TfMesh tfMeshFromLayer(const Document::MeshEntry &entry)
{
    TfMesh out;
    const VCGMesh &mesh = entry.mesh;
    const QMatrix4x4 &m = entry.transform;

    std::vector<int> remap(mesh.vert.size(), -1);
    auto &points = out.points_buffer();
    int next = 0;
    for (std::size_t i = 0; i < mesh.vert.size(); ++i) {
        if (mesh.vert[i].IsD())
            continue;
        const vcg::Point3f &p = mesh.vert[i].cP();
        const QVector3D w = m.map(QVector3D(p.X(), p.Y(), p.Z()));
        points.emplace_back(w.x(), w.y(), w.z());
        remap[i] = next++;
    }

    const VCGVertex *base = mesh.vert.empty() ? nullptr : &mesh.vert.front();
    auto &faces = out.faces_buffer();
    for (const VCGFace &f : mesh.face) {
        if (f.IsD() || !base)
            continue;
        int corner[3];
        bool ok = true;
        for (int k = 0; k < 3; ++k) {
            const ptrdiff_t raw = f.cV(k) - base;
            if (raw < 0 || std::size_t(raw) >= remap.size() || remap[std::size_t(raw)] < 0) {
                ok = false;
                break;
            }
            corner[k] = remap[std::size_t(raw)];
        }
        if (ok && corner[0] != corner[1] && corner[1] != corner[2] && corner[0] != corner[2])
            faces.emplace_back(corner[0], corner[1], corner[2]);
    }
    return out;
}

// Copy a TrueForm result into a fresh document layer. The result is already in world
// space, so the new layer keeps an identity matrix.
template <typename Buffer>
MeshFilterRunResult addResultLayer(
    Document &doc, const Buffer &buffer, const QString &layerName,
    const QString &emptyMessage, QStringList info)
{
    VCGMesh output;
    const auto points = buffer.points();
    const std::size_t pointCount = std::size_t(points.size());
    if (pointCount > 0) {
        vcg::tri::Allocator<VCGMesh>::AddVertices(output, int(pointCount));
        std::size_t vi = 0;
        for (const auto &p : points) {
            output.vert[vi].P() = vcg::Point3f(float(p[0]), float(p[1]), float(p[2]));
            ++vi;
        }
        for (const auto &face : buffer.faces()) {
            const std::size_t n = std::size_t(face.size());
            for (std::size_t k = 2; k < n; ++k) {
                const int a = int(face[0]), b = int(face[k - 1]), c = int(face[k]);
                if (a < 0 || b < 0 || c < 0)
                    continue;
                if (std::size_t(a) >= pointCount || std::size_t(b) >= pointCount
                    || std::size_t(c) >= pointCount)
                    continue;
                if (a == b || b == c || a == c)
                    continue;
                vcg::tri::Allocator<VCGMesh>::AddFace(output, a, b, c);
            }
        }
    }

    if (output.FN() <= 0) {
        doc.finishFilterProgress(false, emptyMessage);
        return fail(emptyMessage);
    }

    vcg::tri::Allocator<VCGMesh>::CompactEveryVector(output);
    vcg::tri::UpdateBounding<VCGMesh>::Box(output);
    vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(output);

    const int ioMask = Mask::IOM_VERTCOORD | Mask::IOM_VERTNORMAL | Mask::IOM_FACENORMAL;
    const int newIndex = doc.addMesh(output, layerName, ioMask);
    if (newIndex < 0) {
        const QString message = QObject::tr("Failed to add the %1 layer.").arg(layerName);
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
    doc.finishFilterProgress(true, QObject::tr("Created %1.").arg(layerName));

    info.prepend(QObject::tr("Created mesh '%1'.").arg(doc.mesh(newIndex).name));
    info << QObject::tr("Output: %1 vertices, %2 faces.").arg(output.VN()).arg(output.FN());
    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.infoMessages = info;
    result.newMeshIndices.push_back(newIndex);
    return result;
}

// Append a TrueForm triangle mesh to a VCG mesh, offsetting the indices. Used where one
// filter emits several pieces — sweeping each path of a polyline, for instance.
template <typename Buffer>
void appendTfMeshTo(const Buffer &buffer, VCGMesh &target)
{
    const auto points = buffer.points();
    const std::size_t pointCount = std::size_t(points.size());
    if (pointCount == 0)
        return;

    const int offset = target.VN();
    vcg::tri::Allocator<VCGMesh>::AddVertices(target, int(pointCount));
    std::size_t vi = 0;
    for (const auto &p : points) {
        target.vert[std::size_t(offset) + vi].P() =
            vcg::Point3f(float(p[0]), float(p[1]), float(p[2]));
        ++vi;
    }
    for (const auto &face : buffer.faces()) {
        const std::size_t n = std::size_t(face.size());
        for (std::size_t k = 2; k < n; ++k) {
            const int a = offset + int(face[0]);
            const int b = offset + int(face[k - 1]);
            const int c = offset + int(face[k]);
            if (a == b || b == c || a == c)
                continue;
            vcg::tri::Allocator<VCGMesh>::AddFace(target, a, b, c);
        }
    }
}

// ---------------------------------------------------------------------------
// Booleans
// ---------------------------------------------------------------------------

// The two-layer preamble every boolean shares.
bool resolveBooleanPair(const FilterParams &params, Document &doc, int &a, int &b, QString &error)
{
    a = params.getMesh(QStringLiteral("firstMesh"), doc.currentMeshIndex());
    b = params.getMesh(QStringLiteral("secondMesh"), -1);
    const auto valid = [&doc](int i) { return i >= 0 && i < doc.meshCount(); };
    if (!valid(a) || !valid(b)) {
        error = QObject::tr("Two layers must be selected.");
        return false;
    }
    if (a == b) {
        error = QObject::tr("The two layers must be different.");
        return false;
    }
    if (doc.mesh(a).mesh.FN() <= 0 || doc.mesh(b).mesh.FN() <= 0) {
        error = QObject::tr("Both layers need faces.");
        return false;
    }
    return true;
}

MeshFilterRunResult runBoolean(const QString &filterId, const FilterParams &params, Document &doc)
{
    int aIndex = -1, bIndex = -1;
    QString error;
    if (!resolveBooleanPair(params, doc, aIndex, bIndex, error))
        return fail(error);

    tf::boolean_op op = tf::boolean_op::merge;
    QString label;
    if (filterId == QString::fromLatin1(kFilterUnion)) {
        op = tf::boolean_op::merge;
        label = QObject::tr("Union");
    } else if (filterId == QString::fromLatin1(kFilterIntersection)) {
        op = tf::boolean_op::intersection;
        label = QObject::tr("Intersection");
    } else {
        op = tf::boolean_op::left_difference;
        label = QObject::tr("Difference");
    }

    doc.beginFilterProgress(QObject::tr("Mesh %1 (TrueForm)").arg(label));
    try {
        const TfMesh a = tfMeshFromLayer(doc.mesh(aIndex));
        const TfMesh b = tfMeshFromLayer(doc.mesh(bIndex));
        auto result = tf::make_boolean(a.polygons(), b.polygons(), op);
        return addResultLayer(
            doc, std::get<0>(result), label,
            QObject::tr("The %1 is empty.").arg(label),
            { QObject::tr("'%1' and '%2'.").arg(doc.mesh(aIndex).name, doc.mesh(bIndex).name) });
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm boolean failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
}

// ---------------------------------------------------------------------------
// CSG over many layers
// ---------------------------------------------------------------------------

// A small recursive-descent parser over layer indices and the four CSG operators.
//
//   expression := term (('|' | '-') term)*
//   term       := factor ('&' factor)*
//   factor     := '~' factor | number | '(' expression ')'
//
// Union and difference share the lowest precedence and associate left, intersection
// binds tighter, and complement binds tightest — the usual reading of "A | B - C".
class CsgExpressionParser
{
public:
    CsgExpressionParser(const QString &text, int meshCount)
        : m_text(text), m_meshCount(meshCount) {}

    bool parse(std::optional<tf::csg::expr> &out, QString &error)
    {
        m_pos = 0;
        m_error.clear();
        m_operands.clear();
        if (!parseExpression(out) || !out.has_value()) {
            error = m_error.isEmpty() ? QObject::tr("Could not parse the expression.") : m_error;
            return false;
        }
        skipSpace();
        if (m_pos != m_text.size()) {
            error = QObject::tr("Unexpected '%1' at position %2.")
                        .arg(m_text.mid(m_pos, 1)).arg(m_pos);
            return false;
        }
        if (m_operands.isEmpty()) {
            error = QObject::tr("The expression refers to no layers.");
            return false;
        }
        return true;
    }

    // Layer indices used, in first-seen order; this is the operand order of the graph.
    QList<int> operands() const { return m_operands; }

private:
    void skipSpace()
    {
        while (m_pos < m_text.size() && m_text.at(m_pos).isSpace())
            ++m_pos;
    }

    bool peek(QChar c)
    {
        skipSpace();
        return m_pos < m_text.size() && m_text.at(m_pos) == c;
    }

    bool parseExpression(std::optional<tf::csg::expr> &out)
    {
        if (!parseTerm(out) || !out.has_value())
            return false;
        while (true) {
            const bool isUnion = peek(QLatin1Char('|'));
            if (!isUnion && !peek(QLatin1Char('-')))
                return true;
            ++m_pos;
            std::optional<tf::csg::expr> rhs;
            if (!parseTerm(rhs) || !rhs.has_value())
                return false;
            out = isUnion ? (*out | *rhs) : (*out - *rhs);
        }
    }

    bool parseTerm(std::optional<tf::csg::expr> &out)
    {
        if (!parseFactor(out) || !out.has_value())
            return false;
        while (peek(QLatin1Char('&'))) {
            ++m_pos;
            std::optional<tf::csg::expr> rhs;
            if (!parseFactor(rhs) || !rhs.has_value())
                return false;
            out = *out & *rhs;
        }
        return true;
    }

    bool parseFactor(std::optional<tf::csg::expr> &out)
    {
        skipSpace();
        if (m_pos >= m_text.size()) {
            m_error = QObject::tr("The expression ends unexpectedly.");
            return false;
        }
        if (peek(QLatin1Char('~'))) {
            ++m_pos;
            std::optional<tf::csg::expr> inner;
            if (!parseFactor(inner) || !inner.has_value())
                return false;
            out = ~(*inner);
            return true;
        }
        if (peek(QLatin1Char('('))) {
            ++m_pos;
            if (!parseExpression(out))
                return false;
            if (!peek(QLatin1Char(')'))) {
                m_error = QObject::tr("Missing ')'.");
                return false;
            }
            ++m_pos;
            return true;
        }
        skipSpace();
        const int start = m_pos;
        while (m_pos < m_text.size() && m_text.at(m_pos).isDigit())
            ++m_pos;
        if (m_pos == start) {
            m_error = QObject::tr("Expected a layer number at position %1, found '%2'.")
                          .arg(start).arg(m_text.mid(start, 1));
            return false;
        }
        const int layer = m_text.mid(start, m_pos - start).toInt();
        if (layer < 0 || layer >= m_meshCount) {
            m_error = QObject::tr("Layer %1 does not exist; the document has %2.")
                          .arg(layer).arg(m_meshCount);
            return false;
        }
        // The graph is built from the operands in first-seen order, so an expression
        // referring to a layer twice must reuse the same operand id.
        int operandId = int(m_operands.indexOf(layer));
        if (operandId < 0) {
            operandId = int(m_operands.size());
            m_operands.append(layer);
        }
        out.emplace(operandId);
        return true;
    }

    QString m_text;
    int m_meshCount = 0;
    int m_pos = 0;
    QString m_error;
    QList<int> m_operands;
};

MeshFilterRunResult runCsgExpression(const FilterParams &params, Document &doc)
{
    const QString text = params.getString(QStringLiteral("expression")).trimmed();
    if (text.isEmpty())
        return fail(QObject::tr("The expression is empty."));

    CsgExpressionParser parser(text, doc.meshCount());
    std::optional<tf::csg::expr> expression;
    QString error;
    if (!parser.parse(expression, error))
        return fail(error);

    const QList<int> operands = parser.operands();
    for (int layer : operands) {
        if (doc.mesh(layer).mesh.FN() <= 0) {
            return fail(QObject::tr("Layer %1 ('%2') has no faces.")
                            .arg(layer).arg(doc.mesh(layer).name));
        }
    }

    doc.beginFilterProgress(QObject::tr("Mesh CSG Expression"));
    try {
        std::vector<TfMesh> meshes;
        meshes.reserve(std::size_t(operands.size()));
        for (int layer : operands)
            meshes.push_back(tfMeshFromLayer(doc.mesh(layer)));

        using FormView = decltype(std::declval<const TfMesh &>().polygons());
        std::vector<FormView> forms;
        forms.reserve(meshes.size());
        for (const TfMesh &m : meshes)
            forms.emplace_back(m.polygons());

        if (vcg::CallBackPos *cb = doc.progressCallback())
            (*cb)(30, "Building the arrangement...");
        auto graph = tf::make_csg_graph(tf::make_range(forms.data(), forms.size()));

        if (vcg::CallBackPos *cb = doc.progressCallback())
            (*cb)(70, "Evaluating the expression...");
        auto result = tf::make_csg_mesh(graph, *expression);

        QStringList used;
        for (int layer : operands)
            used << QObject::tr("%1 = '%2'").arg(layer).arg(doc.mesh(layer).name);
        return addResultLayer(
            doc, result, QObject::tr("CSG"),
            QObject::tr("The expression evaluates to an empty solid."),
            { QObject::tr("Expression: %1").arg(text), used.join(QStringLiteral(", ")) });
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm CSG failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
}

// Symmetric difference is not one of TrueForm's boolean_op values, so it goes through
// the CSG evaluator as (A - B) | (B - A).
MeshFilterRunResult runSymmetricDifference(const FilterParams &params, Document &doc)
{
    int aIndex = -1, bIndex = -1;
    QString error;
    if (!resolveBooleanPair(params, doc, aIndex, bIndex, error))
        return fail(error);

    doc.beginFilterProgress(QObject::tr("Mesh Symmetric Difference (TrueForm)"));
    try {
        const TfMesh a = tfMeshFromLayer(doc.mesh(aIndex));
        const TfMesh b = tfMeshFromLayer(doc.mesh(bIndex));
        using FormView = decltype(std::declval<const TfMesh &>().polygons());
        std::vector<FormView> forms;
        forms.emplace_back(a.polygons());
        forms.emplace_back(b.polygons());
        auto graph = tf::make_csg_graph(tf::make_range(forms.data(), forms.size()));

        const tf::csg::expr lhs(0);
        const tf::csg::expr rhs(1);
        auto result = tf::make_csg_mesh(graph, (lhs - rhs) | (rhs - lhs));
        return addResultLayer(
            doc, result, QObject::tr("Symmetric Difference"),
            QObject::tr("The symmetric difference is empty; the two layers may coincide."),
            { QObject::tr("'%1' and '%2'.").arg(doc.mesh(aIndex).name, doc.mesh(bIndex).name) });
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm symmetric difference failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
}

// The outermost boundary of a single layer, discarding internal shells and resolving
// self-intersections along the way. Useful before 3D printing, and as a repair step on
// geometry assembled from overlapping parts.
MeshFilterRunResult runOuterShell(const FilterParams &params, Document &doc)
{
    const int index = params.getMesh(QStringLiteral("sourceMesh"), doc.currentMeshIndex());
    if (index < 0 || index >= doc.meshCount())
        return fail(QObject::tr("No layer selected."));
    if (doc.mesh(index).mesh.FN() <= 0)
        return fail(QObject::tr("The layer needs faces."));

    doc.beginFilterProgress(QObject::tr("Extract Outer Shell"));
    try {
        const TfMesh source = tfMeshFromLayer(doc.mesh(index));
        auto result = tf::make_outer_shell(source.polygons());
        return addResultLayer(
            doc, result, QObject::tr("Outer Shell"),
            QObject::tr("No outer shell was found. The layer may be open rather than solid."),
            { QObject::tr("From '%1'.").arg(doc.mesh(index).name) });
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm outer shell failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
}

// ---------------------------------------------------------------------------
// Curves
// ---------------------------------------------------------------------------

// The return_curves overloads hand back a tuple whose trailing element is the curves;
// the leading elements are the cut mesh and per-face provenance, which vary in number by
// overload and are not surfaced yet.
template <typename Tuple>
const auto &curvesOf(const Tuple &result)
{
    return std::get<std::tuple_size_v<std::decay_t<Tuple>> - 1>(result);
}

// Turn a TrueForm curves_buffer into a QMeshLab polyline layer: an edge mesh, which is
// what the Create Polyline family already produces.
template <typename Curves>
MeshFilterRunResult addPolylineLayer(
    Document &doc, const Curves &curves, const QString &layerName,
    const QString &emptyMessage, QStringList info)
{
    VCGMesh output;
    const auto points = curves.points();
    const std::size_t pointCount = std::size_t(points.size());

    std::size_t segmentCount = 0;
    if (pointCount > 0) {
        vcg::tri::Allocator<VCGMesh>::AddVertices(output, int(pointCount));
        std::size_t vi = 0;
        for (const auto &p : points) {
            output.vert[vi].P() = vcg::Point3f(float(p[0]), float(p[1]), float(p[2]));
            ++vi;
        }
        for (const auto &path : curves.paths()) {
            const std::size_t n = std::size_t(path.size());
            for (std::size_t k = 1; k < n; ++k) {
                const int a = int(path[k - 1]);
                const int b = int(path[k]);
                if (a == b || a < 0 || b < 0)
                    continue;
                if (std::size_t(a) >= pointCount || std::size_t(b) >= pointCount)
                    continue;
                auto e = vcg::tri::Allocator<VCGMesh>::AddEdges(output, 1);
                e->V(0) = &output.vert[std::size_t(a)];
                e->V(1) = &output.vert[std::size_t(b)];
                ++segmentCount;
            }
        }
    }

    if (segmentCount == 0) {
        doc.finishFilterProgress(false, emptyMessage);
        return fail(emptyMessage);
    }

    vcg::tri::Allocator<VCGMesh>::CompactEveryVector(output);
    vcg::tri::UpdateBounding<VCGMesh>::Box(output);

    const int newIndex = doc.addMesh(output, layerName, Mask::IOM_EDGEINDEX);
    if (newIndex < 0) {
        const QString message = QObject::tr("Failed to add the %1 layer.").arg(layerName);
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
    doc.finishFilterProgress(true, QObject::tr("Created %1.").arg(layerName));

    info.prepend(QObject::tr("Created polyline '%1'.").arg(doc.mesh(newIndex).name));
    info << QObject::tr("%1 path(s), %2 segment(s).")
                .arg(curves.paths().size()).arg(segmentCount);
    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.infoMessages = info;
    result.newMeshIndices.push_back(newIndex);
    return result;
}

// Where a mesh passes through itself. Unlike Select Self Intersecting Faces, which marks
// the faces involved, this extracts the intersection curve itself.
MeshFilterRunResult runSelfIntersectionCurves(const FilterParams &params, Document &doc)
{
    const int index = params.getMesh(QStringLiteral("sourceMesh"), doc.currentMeshIndex());
    if (index < 0 || index >= doc.meshCount())
        return fail(QObject::tr("No layer selected."));
    if (doc.mesh(index).mesh.FN() <= 0)
        return fail(QObject::tr("The layer needs faces."));

    doc.beginFilterProgress(QObject::tr("Create Polyline from Self-Intersections"));
    try {
        const TfMesh source = tfMeshFromLayer(doc.mesh(index));
        auto curves = tf::embedded_self_intersection_curves(source.polygons(), tf::return_curves);
        return addPolylineLayer(
            doc, curvesOf(curves), QObject::tr("Self-Intersections"),
            QObject::tr("No self-intersections were found."),
            { QObject::tr("From '%1'.").arg(doc.mesh(index).name) });
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm self-intersection curves failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
}

// Where two layers cross. The curve is exact, so it is usable as a construction line
// rather than only as a diagnostic.
MeshFilterRunResult runIntersectionCurves(const FilterParams &params, Document &doc)
{
    int aIndex = -1, bIndex = -1;
    QString error;
    if (!resolveBooleanPair(params, doc, aIndex, bIndex, error))
        return fail(error);

    doc.beginFilterProgress(QObject::tr("Create Polyline from Mesh Intersection"));
    try {
        const TfMesh a = tfMeshFromLayer(doc.mesh(aIndex));
        const TfMesh b = tfMeshFromLayer(doc.mesh(bIndex));
        auto curves = tf::make_boolean(
            a.polygons(), b.polygons(), tf::boolean_op::intersection, tf::return_curves);
        return addPolylineLayer(
            doc, curvesOf(curves), QObject::tr("Intersection Curve"),
            QObject::tr("The two layers do not intersect."),
            { QObject::tr("'%1' against '%2'.")
                  .arg(doc.mesh(aIndex).name, doc.mesh(bIndex).name) });
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm intersection curves failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
}

// Contours of the per-vertex scalar field, as polylines lying on the surface. This is
// what makes every scalar QMeshLab computes — geodesic distance, curvature, ambient
// occlusion, raster coverage — into something with extractable level sets.
MeshFilterRunResult runIsocurves(const FilterParams &params, Document &doc)
{
    const int index = params.getMesh(QStringLiteral("sourceMesh"), doc.currentMeshIndex());
    if (index < 0 || index >= doc.meshCount())
        return fail(QObject::tr("No layer selected."));
    const VCGMesh &mesh = doc.mesh(index).mesh;
    if (mesh.FN() <= 0)
        return fail(QObject::tr("The layer needs faces."));

    const int count = std::max(1, params.getInt(QStringLiteral("contourCount"), 10));
    const bool useRange = params.getBool(QStringLiteral("useCustomRange"), false);
    double minValue = params.getDouble(QStringLiteral("minValue"), 0.0);
    double maxValue = params.getDouble(QStringLiteral("maxValue"), 0.0);

    // Gather the scalars in the same order tfMeshFromLayer emits points.
    std::vector<float> scalars;
    scalars.reserve(std::size_t(std::max(0, mesh.VN())));
    float observedMin = std::numeric_limits<float>::max();
    float observedMax = std::numeric_limits<float>::lowest();
    for (const VCGVertex &v : mesh.vert) {
        if (v.IsD())
            continue;
        const float q = v.cQ();
        scalars.push_back(q);
        if (std::isfinite(q)) {
            observedMin = std::min(observedMin, q);
            observedMax = std::max(observedMax, q);
        }
    }
    if (scalars.empty())
        return fail(QObject::tr("The layer has no live vertices."));
    if (!(observedMax > observedMin)) {
        return fail(QObject::tr(
            "The scalar field is constant, so it has no contours. Compute a per-vertex "
            "scalar first — a geodesic distance or a curvature, for instance."));
    }
    if (!useRange) {
        minValue = double(observedMin);
        maxValue = double(observedMax);
    }
    if (!(maxValue > minValue))
        return fail(QObject::tr("The maximum must be larger than the minimum."));

    // Contours strictly inside the range: a contour exactly at the extreme is either
    // empty or the whole boundary, neither of which is useful.
    std::vector<float> cutValues;
    cutValues.reserve(std::size_t(count));
    for (int i = 0; i < count; ++i) {
        const double t = (double(i) + 1.0) / (double(count) + 1.0);
        cutValues.push_back(float(minValue + t * (maxValue - minValue)));
    }

    doc.beginFilterProgress(QObject::tr("Create Polyline from Scalar Isocontour"));
    try {
        const TfMesh source = tfMeshFromLayer(doc.mesh(index));
        auto curves = tf::embedded_isocurves(
            source.polygons(),
            tf::make_range(scalars.data(), scalars.size()),
            tf::make_range(cutValues.data(), cutValues.size()),
            tf::return_curves);
        return addPolylineLayer(
            doc, curvesOf(curves), QObject::tr("Isocontours"),
            QObject::tr("No contours were produced at the requested values."),
            { QObject::tr("From '%1'.").arg(doc.mesh(index).name),
              QObject::tr("%1 contour(s) between %2 and %3.")
                  .arg(count)
                  .arg(QString::number(minValue, 'g', 6))
                  .arg(QString::number(maxValue, 'g', 6)) });
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm isocurves failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
}

// Sweep a circular profile along a polyline. The Create Polyline family produces edge
// meshes that render as hairlines; this turns one into geometry that can be shaded,
// exported or printed.
MeshFilterRunResult runTubeFromPolyline(const FilterParams &params, Document &doc)
{
    const int index = params.getMesh(QStringLiteral("sourceMesh"), doc.currentMeshIndex());
    if (index < 0 || index >= doc.meshCount())
        return fail(QObject::tr("No layer selected."));

    const Document::MeshEntry &entry = doc.mesh(index);
    const VCGMesh &mesh = entry.mesh;
    if (mesh.EN() <= 0) {
        return fail(QObject::tr(
            "'%1' has no edges. This filter sweeps a polyline layer, such as one made by "
            "the Create Polyline filters.").arg(entry.name));
    }

    const double radius = params.getDouble(QStringLiteral("radius"), 0.0);
    const int segments = std::clamp(params.getInt(QStringLiteral("segments"), 8), 3, 256);
    if (!std::isfinite(radius) || radius <= 0.0)
        return fail(QObject::tr("The radius must be a finite value larger than zero."));

    // Chain the edges into paths. TrueForm sweeps a curve, so a soup of unordered
    // segments has to be walked into runs first; each vertex may join at most two edges
    // for the chain to be unambiguous.
    const std::size_t vertexCount = mesh.vert.size();
    std::vector<std::array<int, 2>> incident(vertexCount, { -1, -1 });
    std::vector<std::array<int, 2>> edges;
    edges.reserve(std::size_t(std::max(0, mesh.EN())));
    const VCGVertex *base = mesh.vert.empty() ? nullptr : &mesh.vert.front();
    int branching = 0;
    for (const auto &e : mesh.edge) {
        if (e.IsD() || !base)
            continue;
        const ptrdiff_t v0 = e.cV(0) - base;
        const ptrdiff_t v1 = e.cV(1) - base;
        if (v0 < 0 || v1 < 0 || std::size_t(v0) >= vertexCount || std::size_t(v1) >= vertexCount)
            continue;
        const int id = int(edges.size());
        edges.push_back({ int(v0), int(v1) });
        for (int v : { int(v0), int(v1) }) {
            if (incident[std::size_t(v)][0] < 0)
                incident[std::size_t(v)][0] = id;
            else if (incident[std::size_t(v)][1] < 0)
                incident[std::size_t(v)][1] = id;
            else
                ++branching;
        }
    }
    if (edges.empty())
        return fail(QObject::tr("The layer has no usable edges."));

    doc.beginFilterProgress(QObject::tr("Create Tube from Polyline"));

    VCGMesh output;
    std::size_t tubeCount = 0;
    try {
        const QMatrix4x4 &m = entry.transform;
        std::vector<char> visited(edges.size(), 0);

        // Walk from every endpoint first so open runs come out whole, then mop up loops.
        const auto walk = [&](int startVertex, int startEdge) {
            std::vector<int> path{ startVertex };
            int current = startVertex;
            int edgeId = startEdge;
            while (edgeId >= 0 && !visited[std::size_t(edgeId)]) {
                visited[std::size_t(edgeId)] = 1;
                const auto &e = edges[std::size_t(edgeId)];
                const int next = (e[0] == current) ? e[1] : e[0];
                path.push_back(next);
                current = next;
                const auto &inc = incident[std::size_t(current)];
                edgeId = (inc[0] != -1 && inc[0] != edgeId) ? inc[0]
                       : (inc[1] != -1 && inc[1] != edgeId) ? inc[1]
                                                            : -1;
                if (current == startVertex)
                    break; // closed loop
            }
            return path;
        };

        std::vector<std::vector<int>> paths;
        for (std::size_t v = 0; v < vertexCount; ++v) {
            const auto &inc = incident[v];
            const bool endpoint = (inc[0] >= 0) != (inc[1] >= 0);
            if (endpoint && !visited[std::size_t(inc[0] >= 0 ? inc[0] : inc[1])])
                paths.push_back(walk(int(v), inc[0] >= 0 ? inc[0] : inc[1]));
        }
        for (std::size_t e = 0; e < edges.size(); ++e) {
            if (!visited[e])
                paths.push_back(walk(edges[e][0], int(e)));
        }

        for (const std::vector<int> &path : paths) {
            if (path.size() < 2)
                continue;
            tf::points_buffer<float, 3> pts;
            for (int v : path) {
                const vcg::Point3f &p = mesh.vert[std::size_t(v)].cP();
                const QVector3D w = m.map(QVector3D(p.X(), p.Y(), p.Z()));
                pts.emplace_back(w.x(), w.y(), w.z());
            }
            tf::curves_buffer<int, float, 3> cb;
            cb.points_buffer() = pts;
            std::vector<int> indices(path.size());
            for (std::size_t i = 0; i < path.size(); ++i)
                indices[i] = int(i);
            cb.paths_buffer().push_back(tf::make_range(indices.data(), indices.size()));

            auto tube = tf::make_tube_mesh(cb.curves()[0], float(radius), segments);
            appendTfMeshTo(tube, output);
            ++tubeCount;
        }
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm tube generation failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    if (output.FN() <= 0) {
        const QString message = QObject::tr("The sweep produced no geometry.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    vcg::tri::Allocator<VCGMesh>::CompactEveryVector(output);
    vcg::tri::UpdateBounding<VCGMesh>::Box(output);
    vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(output);

    const int ioMask = Mask::IOM_VERTCOORD | Mask::IOM_VERTNORMAL | Mask::IOM_FACENORMAL;
    const int newIndex = doc.addMesh(output, QObject::tr("Tube"), ioMask);
    if (newIndex < 0) {
        const QString message = QObject::tr("Failed to add the tube layer.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
    doc.finishFilterProgress(true, QObject::tr("Created tube."));

    QStringList info;
    info << QObject::tr("Created mesh '%1'.").arg(doc.mesh(newIndex).name)
         << QObject::tr("Swept %1 path(s) from '%2'.").arg(tubeCount).arg(entry.name)
         << QObject::tr("Output: %1 vertices, %2 faces.").arg(output.VN()).arg(output.FN());
    if (branching > 0) {
        info << QObject::tr(
            "%1 vertex junction(s) joined more than two edges; the polyline was split "
            "there rather than branched.").arg(branching);
    }
    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.infoMessages = info;
    result.newMeshIndices.push_back(newIndex);
    return result;
}

// ---------------------------------------------------------------------------
// Distance and containment
// ---------------------------------------------------------------------------

// signed_distance() needs the polygons tagged with a spatial index, face membership and
// the manifold edge link: the tree finds candidates, and the other two let it decide
// which side of the surface a point falls on near an edge or a vertex, where the closest
// face alone is ambiguous. The tags reference these objects, so they must outlive use.
struct SignedDistanceContext
{
    explicit SignedDistanceContext(const TfMesh &mesh)
        : membership(mesh.polygons())
        , edgeLink(mesh.polygons().faces(), membership)
        , tree(mesh.polygons(), tf::config_tree(4, 4))
    {}

    tf::face_membership<int> membership;
    tf::manifold_edge_link<int, 3> edgeLink;
    tf::aabb_tree<int, float, 3> tree;
};

// Signed distance from every vertex of one layer to the surface of another. Negative
// inside, positive outside — which is what makes it a field rather than a proximity
// readout, and what lets the containment filter share the same machinery.
MeshFilterRunResult runSignedDistance(const FilterParams &params, Document &doc)
{
    const int targetIndex = params.getMesh(QStringLiteral("sourceMesh"), doc.currentMeshIndex());
    const int referenceIndex = params.getMesh(QStringLiteral("referenceMesh"), -1);
    const auto valid = [&doc](int i) { return i >= 0 && i < doc.meshCount(); };
    if (!valid(targetIndex) || !valid(referenceIndex))
        return fail(QObject::tr("A layer and a reference layer must be selected."));
    if (targetIndex == referenceIndex)
        return fail(QObject::tr("The two layers must be different."));
    if (doc.mesh(referenceIndex).mesh.FN() <= 0)
        return fail(QObject::tr("The reference layer needs faces."));
    if (doc.mesh(targetIndex).mesh.VN() <= 0)
        return fail(QObject::tr("The layer needs vertices."));

    const bool absolute = params.getBool(QStringLiteral("unsigned"), false);

    doc.beginFilterProgress(QObject::tr("Compute Signed Distance to Mesh"));
    double minDistance = std::numeric_limits<double>::max();
    double maxDistance = std::numeric_limits<double>::lowest();
    int insideCount = 0;
    try {
        const TfMesh reference = tfMeshFromLayer(doc.mesh(referenceIndex));
        SignedDistanceContext ctx(reference);
        auto tagged = reference.polygons() | tf::tag(ctx.tree) | tf::tag(ctx.membership)
            | tf::tag(ctx.edgeLink);

        Document::MeshEntry &entry = doc.mesh(targetIndex);
        const QMatrix4x4 &m = entry.transform;
        for (VCGVertex &v : entry.mesh.vert) {
            if (v.IsD())
                continue;
            const vcg::Point3f &p = v.cP();
            const QVector3D w = m.map(QVector3D(p.X(), p.Y(), p.Z()));
            double d = tf::signed_distance(tagged, tf::make_point(w.x(), w.y(), w.z()));
            if (d < 0.0)
                ++insideCount;
            if (absolute)
                d = std::abs(d);
            v.Q() = float(d);
            minDistance = std::min(minDistance, d);
            maxDistance = std::max(maxDistance, d);
        }
        entry.ioMask |= Mask::IOM_VERTQUALITY;
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm signed distance failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    doc.markMeshGeometryChanged(
        targetIndex,
        QObject::tr("Computed distance from '%1' to '%2'")
            .arg(doc.mesh(targetIndex).name, doc.mesh(referenceIndex).name));
    doc.finishFilterProgress(true, QObject::tr("Computed signed distance."));

    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.infoMessages
        << QObject::tr("Range: %1 to %2")
               .arg(QString::number(minDistance, 'g', 6))
               .arg(QString::number(maxDistance, 'g', 6))
        << QObject::tr("%1 vertex(es) lie inside '%2'.")
               .arg(insideCount).arg(doc.mesh(referenceIndex).name);
    result.visualizationHints.push_back(
        { targetIndex, MeshFilterVisualizationAttribute::VertexQuality });
    return result;
}

// Select the vertices enclosed by another layer. Uses the sign of the distance rather
// than a ray-parity test, so a point exactly on the surface is decided consistently.
MeshFilterRunResult runSelectInsideMesh(const FilterParams &params, Document &doc)
{
    const int targetIndex = params.getMesh(QStringLiteral("sourceMesh"), doc.currentMeshIndex());
    const int referenceIndex = params.getMesh(QStringLiteral("referenceMesh"), -1);
    const auto valid = [&doc](int i) { return i >= 0 && i < doc.meshCount(); };
    if (!valid(targetIndex) || !valid(referenceIndex))
        return fail(QObject::tr("A layer and an enclosing layer must be selected."));
    if (targetIndex == referenceIndex)
        return fail(QObject::tr("The two layers must be different."));
    if (doc.mesh(referenceIndex).mesh.FN() <= 0)
        return fail(QObject::tr("The enclosing layer needs faces."));

    const bool invert = params.getBool(QStringLiteral("selectOutside"), false);
    const QString mode = params.getEnum(QStringLiteral("mode"));

    doc.beginFilterProgress(QObject::tr("Select Vertices Inside Mesh"));
    int selected = 0;
    try {
        const TfMesh reference = tfMeshFromLayer(doc.mesh(referenceIndex));
        SignedDistanceContext ctx(reference);
        auto tagged = reference.polygons() | tf::tag(ctx.tree) | tf::tag(ctx.membership)
            | tf::tag(ctx.edgeLink);

        Document::MeshEntry &entry = doc.mesh(targetIndex);
        const QMatrix4x4 &m = entry.transform;
        const bool replace = (mode != QStringLiteral("add") && mode != QStringLiteral("subtract"));
        for (VCGVertex &v : entry.mesh.vert) {
            if (v.IsD())
                continue;
            const vcg::Point3f &p = v.cP();
            const QVector3D w = m.map(QVector3D(p.X(), p.Y(), p.Z()));
            const double d = tf::signed_distance(tagged, tf::make_point(w.x(), w.y(), w.z()));
            const bool hit = invert ? (d > 0.0) : (d < 0.0);
            if (replace)
                hit ? v.SetS() : v.ClearS();
            else if (mode == QStringLiteral("add") && hit)
                v.SetS();
            else if (mode == QStringLiteral("subtract") && hit)
                v.ClearS();
            if (v.IsS())
                ++selected;
        }
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm containment test failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    doc.markMeshSelectionChanged(
        targetIndex,
        QObject::tr("Selected vertices of '%1' inside '%2'")
            .arg(doc.mesh(targetIndex).name, doc.mesh(referenceIndex).name));
    doc.finishFilterProgress(true, QObject::tr("Updated selection."));

    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.infoMessages << QObject::tr("%1 vertex(es) selected.").arg(selected);
    return result;
}

// Mean distance from one layer's vertices to the nearest point of another's. Unlike the
// Hausdorff distance, which reports the single worst correspondence, this averages — so
// it is stable against a lone outlier and useful as a fit score.
MeshFilterRunResult runChamferDistance(const FilterParams &params, Document &doc)
{
    const int aIndex = params.getMesh(QStringLiteral("sourceMesh"), doc.currentMeshIndex());
    const int bIndex = params.getMesh(QStringLiteral("referenceMesh"), -1);
    const auto valid = [&doc](int i) { return i >= 0 && i < doc.meshCount(); };
    if (!valid(aIndex) || !valid(bIndex))
        return fail(QObject::tr("Two layers must be selected."));
    if (aIndex == bIndex)
        return fail(QObject::tr("The two layers must be different."));
    if (doc.mesh(aIndex).mesh.VN() <= 0 || doc.mesh(bIndex).mesh.VN() <= 0)
        return fail(QObject::tr("Both layers need vertices."));

    const bool symmetric = params.getBool(QStringLiteral("symmetric"), true);
    const double outlier =
        std::clamp(params.getDouble(QStringLiteral("outlierProportion"), 0.0), 0.0, 0.9);

    doc.beginFilterProgress(QObject::tr("Measure Chamfer Distance"));
    double forward = 0.0;
    double backward = 0.0;
    try {
        const TfPoints a = worldPoints(doc.mesh(aIndex));
        const TfPoints b = worldPoints(doc.mesh(bIndex));
        auto pa = tf::make_points(a);
        auto pb = tf::make_points(b);

        TfTree treeB(pb, tf::config_tree(4, 4));
        forward = double(tf::chamfer_error(pa, pb | tf::tag(treeB), float(outlier)));
        if (symmetric) {
            TfTree treeA(pa, tf::config_tree(4, 4));
            backward = double(tf::chamfer_error(pb, pa | tf::tag(treeA), float(outlier)));
        }
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm chamfer distance failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
    doc.finishFilterProgress(true, QObject::tr("Measured chamfer distance."));

    const QString aName = doc.mesh(aIndex).name;
    const QString bName = doc.mesh(bIndex).name;
    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = false;
    result.infoMessages
        << QObject::tr("'%1' to '%2': %3").arg(aName, bName, QString::number(forward, 'g', 6));
    if (symmetric) {
        result.infoMessages
            << QObject::tr("'%1' to '%2': %3").arg(bName, aName, QString::number(backward, 'g', 6))
            << QObject::tr("Symmetric (max): %1")
                   .arg(QString::number(std::max(forward, backward), 'g', 6));
    }
    return result;
}

// ---------------------------------------------------------------------------
// Smoothing, curvature and normals
// ---------------------------------------------------------------------------

// tfMeshFromLayer() skips deleted vertices, so the nth TrueForm point is the nth *live*
// VCG vertex. Results have to be written back through that mapping, not by raw index.
std::vector<std::size_t> liveVertexIndices(const VCGMesh &mesh)
{
    std::vector<std::size_t> live;
    live.reserve(std::size_t(std::max(0, mesh.VN())));
    for (std::size_t i = 0; i < mesh.vert.size(); ++i) {
        if (!mesh.vert[i].IsD())
            live.push_back(i);
    }
    return live;
}

// Connectivity shared by the per-vertex operators. Built once per run; the tags below
// reference these, so they must outlive the call that uses them.
struct VertexConnectivity
{
    explicit VertexConnectivity(const TfMesh &mesh)
        : membership(mesh.polygons())
    {
        link.build(mesh.polygons(), membership);
    }

    tf::face_membership<int> membership;
    tf::vertex_link<int> link;
};

MeshFilterRunResult runSmooth(const QString &filterId, const FilterParams &params, Document &doc)
{
    const int index = doc.currentMeshIndex();
    if (index < 0 || index >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));
    if (doc.mesh(index).mesh.FN() <= 0)
        return fail(QObject::tr("The layer needs faces: smoothing follows the vertex link."));

    const bool taubin = (filterId == QString::fromLatin1(kFilterTaubin));
    const int iterations = std::max(1, params.getInt(QStringLiteral("iterations"), 10));
    const double lambda = std::clamp(params.getDouble(QStringLiteral("lambda"), 0.5), 0.0, 1.0);
    const double kpb = params.getDouble(QStringLiteral("kpb"), 0.1);
    const bool selectedOnly = params.getBool(QStringLiteral("selectedOnly"), false);

    doc.beginFilterProgress(taubin ? QObject::tr("Smooth Vertices by Taubin (TrueForm)")
                                   : QObject::tr("Smooth Vertices by Laplacian (TrueForm)"));
    try {
        Document::MeshEntry &entry = doc.mesh(index);
        const TfMesh source = tfMeshFromLayer(entry);
        VertexConnectivity conn(source);
        auto tagged = source.points() | tf::tag(conn.link);

        auto smoothed = taubin
            ? tf::taubin_smoothed(tagged, std::size_t(iterations), float(lambda), float(kpb))
            : tf::laplacian_smoothed(tagged, std::size_t(iterations), float(lambda));

        // The source was taken in world space, so the result comes back there too and
        // has to be mapped through the inverse layer matrix before it is stored.
        bool invertible = false;
        const QMatrix4x4 inverse = entry.transform.inverted(&invertible);
        if (!invertible)
            return fail(QObject::tr("The layer matrix is not invertible."));

        const std::vector<std::size_t> live = liveVertexIndices(entry.mesh);
        const auto points = smoothed.points();
        if (std::size_t(points.size()) != live.size())
            return fail(QObject::tr("Smoothing returned an unexpected number of points."));

        int moved = 0;
        for (std::size_t i = 0; i < live.size(); ++i) {
            VCGVertex &v = entry.mesh.vert[live[i]];
            if (selectedOnly && !v.IsS())
                continue;
            const auto &p = points[i];
            const QVector3D local = inverse.map(QVector3D(float(p[0]), float(p[1]), float(p[2])));
            v.P() = vcg::Point3f(local.x(), local.y(), local.z());
            ++moved;
        }

        vcg::tri::UpdateBounding<VCGMesh>::Box(entry.mesh);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(entry.mesh);
        doc.markMeshGeometryChanged(
            index, QObject::tr("Smoothed '%1'").arg(entry.name));
        doc.finishFilterProgress(true, QObject::tr("Smoothed the layer."));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages
            << QObject::tr("Moved %1 vertex(es) over %2 iteration(s).").arg(moved).arg(iterations);
        return result;
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm smoothing failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
}

MeshFilterRunResult runCurvature(const FilterParams &params, Document &doc)
{
    const int index = doc.currentMeshIndex();
    if (index < 0 || index >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));
    if (doc.mesh(index).mesh.FN() <= 0)
        return fail(QObject::tr("The layer needs faces."));

    const QString measure = params.getEnum(QStringLiteral("measure"));
    const int ring = std::max(1, params.getInt(QStringLiteral("ring"), 2));

    doc.beginFilterProgress(QObject::tr("Compute Curvature (TrueForm)"));
    try {
        Document::MeshEntry &entry = doc.mesh(index);
        const TfMesh source = tfMeshFromLayer(entry);
        const std::vector<std::size_t> live = liveVertexIndices(entry.mesh);

        std::vector<float> values;
        if (measure == QStringLiteral("shape_index")) {
            auto shape = tf::make_shape_index(source.polygons(), std::size_t(ring));
            values.assign(shape.begin(), shape.end());
        } else {
            auto [k0, k1] = tf::make_principal_curvatures(source.polygons(), std::size_t(ring));
            const std::size_t n = std::min(std::size_t(k0.size()), std::size_t(k1.size()));
            values.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                const double a = double(k0[i]);
                const double b = double(k1[i]);
                double value = 0.0;
                if (measure == QStringLiteral("gaussian")) value = a * b;
                else if (measure == QStringLiteral("min")) value = std::min(a, b);
                else if (measure == QStringLiteral("max")) value = std::max(a, b);
                else value = 0.5 * (a + b); // mean
                values.push_back(float(value));
            }
        }

        if (values.size() != live.size())
            return fail(QObject::tr("Curvature returned an unexpected number of values."));
        for (std::size_t i = 0; i < live.size(); ++i)
            entry.mesh.vert[live[i]].Q() = values[i];
        entry.ioMask |= Mask::IOM_VERTQUALITY;

        doc.markMeshGeometryChanged(
            index, QObject::tr("Computed curvature on '%1'").arg(entry.name));
        doc.finishFilterProgress(true, QObject::tr("Computed curvature."));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages << QObject::tr("Wrote %1 per-vertex value(s).").arg(values.size());
        result.visualizationHints.push_back(
            { index, MeshFilterVisualizationAttribute::VertexQuality });
        return result;
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm curvature failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
}

MeshFilterRunResult runNormals(const FilterParams &params, Document &doc)
{
    const int index = doc.currentMeshIndex();
    if (index < 0 || index >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));
    if (doc.mesh(index).mesh.FN() <= 0)
        return fail(QObject::tr("The layer needs faces."));

    const bool perFace = params.getEnum(QStringLiteral("target")) == QStringLiteral("face");

    doc.beginFilterProgress(QObject::tr("Compute Normals (TrueForm)"));
    try {
        Document::MeshEntry &entry = doc.mesh(index);
        const TfMesh source = tfMeshFromLayer(entry);

        // Normals are computed in world space, so they come back rotated by the layer
        // matrix and must be brought back into layer space before being stored.
        bool invertible = false;
        const QMatrix4x4 inverse = entry.transform.inverted(&invertible);
        if (!invertible)
            return fail(QObject::tr("The layer matrix is not invertible."));
        const QMatrix3x3 backToLayer = inverse.normalMatrix();

        const auto toLayer = [&backToLayer](float x, float y, float z) {
            QVector3D t(backToLayer(0, 0) * x + backToLayer(0, 1) * y + backToLayer(0, 2) * z,
                        backToLayer(1, 0) * x + backToLayer(1, 1) * y + backToLayer(1, 2) * z,
                        backToLayer(2, 0) * x + backToLayer(2, 1) * y + backToLayer(2, 2) * z);
            if (t.lengthSquared() > 1e-20f)
                t.normalize();
            return t;
        };

        std::size_t written = 0;
        if (perFace) {
            auto normals = tf::compute_normals(source.polygons());
            const auto vectors = normals.unit_vectors();
            std::size_t k = 0;
            for (VCGFace &f : entry.mesh.face) {
                if (f.IsD())
                    continue;
                if (k >= std::size_t(vectors.size()))
                    break;
                const auto &n = vectors[k++];
                const QVector3D t = toLayer(float(n[0]), float(n[1]), float(n[2]));
                f.N() = vcg::Point3f(t.x(), t.y(), t.z());
                ++written;
            }
            entry.ioMask |= Mask::IOM_FACENORMAL;
        } else {
            VertexConnectivity conn(source);
            auto normals = tf::compute_point_normals(source.polygons() | tf::tag(conn.membership));
            const auto vectors = normals.unit_vectors();
            const std::vector<std::size_t> live = liveVertexIndices(entry.mesh);
            if (std::size_t(vectors.size()) != live.size())
                return fail(QObject::tr("Normal computation returned an unexpected count."));
            for (std::size_t i = 0; i < live.size(); ++i) {
                const auto &n = vectors[i];
                const QVector3D t = toLayer(float(n[0]), float(n[1]), float(n[2]));
                entry.mesh.vert[live[i]].N() = vcg::Point3f(t.x(), t.y(), t.z());
                ++written;
            }
            entry.ioMask |= Mask::IOM_VERTNORMAL;
        }

        doc.markMeshGeometryChanged(
            index, QObject::tr("Computed normals on '%1'").arg(entry.name));
        doc.finishFilterProgress(true, QObject::tr("Computed normals."));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages << (perFace ? QObject::tr("Wrote %1 face normal(s).").arg(written)
                                        : QObject::tr("Wrote %1 vertex normal(s).").arg(written));
        return result;
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm normal computation failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
}

// ---------------------------------------------------------------------------
// Remeshing
// ---------------------------------------------------------------------------

// Replace a layer's geometry with a TrueForm result, keeping the layer and its matrix.
// The result arrives in world space, so it is mapped back through the inverse matrix.
MeshFilterRunResult replaceLayerGeometry(
    Document &doc, int index, const TfMesh &result, const QString &context, QStringList info)
{
    Document::MeshEntry &entry = doc.mesh(index);
    bool invertible = false;
    const QMatrix4x4 inverse = entry.transform.inverted(&invertible);
    if (!invertible) {
        const QString message = QObject::tr("The layer matrix is not invertible.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    VCGMesh output;
    const auto points = result.points();
    const std::size_t pointCount = std::size_t(points.size());
    if (pointCount > 0) {
        vcg::tri::Allocator<VCGMesh>::AddVertices(output, int(pointCount));
        std::size_t vi = 0;
        for (const auto &p : points) {
            const QVector3D local = inverse.map(QVector3D(float(p[0]), float(p[1]), float(p[2])));
            output.vert[vi].P() = vcg::Point3f(local.x(), local.y(), local.z());
            ++vi;
        }
        for (const auto &face : result.faces()) {
            const std::size_t n = std::size_t(face.size());
            for (std::size_t k = 2; k < n; ++k) {
                const int a = int(face[0]), b = int(face[k - 1]), c = int(face[k]);
                if (a < 0 || b < 0 || c < 0)
                    continue;
                if (std::size_t(a) >= pointCount || std::size_t(b) >= pointCount
                    || std::size_t(c) >= pointCount)
                    continue;
                if (a == b || b == c || a == c)
                    continue;
                vcg::tri::Allocator<VCGMesh>::AddFace(output, a, b, c);
            }
        }
    }

    if (output.FN() <= 0) {
        const QString message = QObject::tr("The operation produced no faces.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    const int beforeV = entry.mesh.VN();
    const int beforeF = entry.mesh.FN();

    entry.mesh.Clear();
    vcg::tri::Append<VCGMesh, VCGMesh>::MeshCopy(entry.mesh, output);
    vcg::tri::Allocator<VCGMesh>::CompactEveryVector(entry.mesh);
    vcg::tri::UpdateBounding<VCGMesh>::Box(entry.mesh);
    vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(entry.mesh);
    entry.ioMask |= Mask::IOM_VERTNORMAL | Mask::IOM_FACENORMAL;

    doc.markMeshGeometryChanged(index, context);
    doc.finishFilterProgress(true, context);

    MeshFilterRunResult r;
    r.success = true;
    r.documentModified = true;
    info << QObject::tr("%1 vertices, %2 faces -> %3 vertices, %4 faces.")
                .arg(beforeV).arg(beforeF).arg(entry.mesh.VN()).arg(entry.mesh.FN());
    r.infoMessages = info;
    return r;
}

MeshFilterRunResult runRemesh(const QString &filterId, const FilterParams &params, Document &doc)
{
    const int index = doc.currentMeshIndex();
    if (index < 0 || index >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));
    if (doc.mesh(index).mesh.FN() <= 0)
        return fail(QObject::tr("The layer needs faces."));

    const bool preserveBoundary = params.getBool(QStringLiteral("preserveBoundary"), true);
    const double featureAngle = params.getDouble(QStringLiteral("featureAngle"), -1.0);
    // A negative feature angle disables feature detection, which is TrueForm's own
    // convention; exposing it as "0 means off" would be a needless second convention.
    const auto featureRad = tf::rad<float>(
        featureAngle > 0.0 ? float(featureAngle * M_PI / 180.0) : -1.0f);

    doc.beginFilterProgress(QObject::tr("Remeshing (TrueForm)"));
    try {
        const TfMesh source = tfMeshFromLayer(doc.mesh(index));

        if (filterId == QString::fromLatin1(kFilterIsotropic)) {
            const double target = params.getDouble(QStringLiteral("targetLength"), 0.0);
            if (!std::isfinite(target) || target <= 0.0)
                return fail(QObject::tr("The target edge length must be larger than zero."));
            tf::isotropic_remesh_config<float> config{ float(target) };
            config.iterations = std::max(1, params.getInt(QStringLiteral("iterations"), 3));
            config.relaxation_iters =
                std::max(0, params.getInt(QStringLiteral("relaxationIterations"), 3));
            config.preserve_boundary = preserveBoundary;
            config.feature_angle = featureRad;
            auto [mesh, he] = tf::isotropic_remeshed(source.polygons(), config);
            (void) he;
            return replaceLayerGeometry(
                doc, index, mesh,
                QObject::tr("Remeshed '%1' isotropically").arg(doc.mesh(index).name),
                { QObject::tr("Target edge length: %1").arg(QString::number(target, 'g', 6)) });
        }

        if (filterId == QString::fromLatin1(kFilterSimplify)) {
            tf::simplify_config<float> config;
            config.error_rel = float(std::max(1e-9, params.getDouble(QStringLiteral("errorRelative"), 0.002)));
            config.iterations = std::max(1, params.getInt(QStringLiteral("iterations"), 1));
            config.preserve_boundary = preserveBoundary;
            config.feature_angle = featureRad;
            auto [mesh, he] = tf::simplified(source.polygons(), config);
            (void) he;
            return replaceLayerGeometry(
                doc, index, mesh,
                QObject::tr("Simplified '%1'").arg(doc.mesh(index).name),
                { QObject::tr("Relative error bound: %1").arg(config.error_rel) });
        }

        // Decimation to a target proportion of the original face count.
        const double proportion =
            std::clamp(params.getDouble(QStringLiteral("targetProportion"), 0.5), 0.001, 0.999);
        tf::decimate_config<float> config;
        config.preserve_boundary = preserveBoundary;
        config.feature_angle = featureRad;
        auto [mesh, he] = tf::decimated(source.polygons(), float(proportion), config);
        (void) he;
        return replaceLayerGeometry(
            doc, index, mesh,
            QObject::tr("Decimated '%1'").arg(doc.mesh(index).name),
            { QObject::tr("Target proportion: %1").arg(proportion) });
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm remeshing failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
}

// ---------------------------------------------------------------------------
// Orientation and edge selection
// ---------------------------------------------------------------------------

MeshFilterRunResult runOrient(const QString &filterId, const FilterParams &params, Document &doc)
{
    (void) params;
    const int index = doc.currentMeshIndex();
    if (index < 0 || index >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));
    if (doc.mesh(index).mesh.FN() <= 0)
        return fail(QObject::tr("The layer needs faces."));

    const bool outward = (filterId == QString::fromLatin1(kFilterOrientOutward));

    doc.beginFilterProgress(outward ? QObject::tr("Orient Faces Outward (TrueForm)")
                                    : QObject::tr("Orient Faces Coherently (TrueForm)"));
    int flipped = 0;
    int passes = 0;
    int remainingInconsistent = 0;
    try {
        Document::MeshEntry &entry = doc.mesh(index);
        TfMesh source = tfMeshFromLayer(entry);

        // One call to tf::orient_faces_consistently only partly repairs a badly mixed
        // winding: it reverses faces in place while indexing an edge link built from the
        // pre-flip winding, and reversing an n-gon permutes its edge slots (for a
        // triangle, slots 0 and 1 swap), so later lookups pair the wrong edge with the
        // wrong peer. Each call rebuilds the link from the current state, so iterating
        // converges — on a box with alternate faces flipped, 14 inconsistencies go
        // 8 -> 3 -> 0. Iterate to a fixed point rather than making the user click twice.
        const auto inconsistentCount = [](const TfMesh &mesh) {
            std::map<std::pair<int, int>, int> directed;
            for (const auto &f : mesh.polygons().faces()) {
                const std::size_t n = std::size_t(f.size());
                for (std::size_t k = 0; k < n; ++k)
                    ++directed[{ int(f[k]), int(f[(k + 1) % n]) }];
            }
            int bad = 0;
            for (const auto &entryPair : directed) {
                if (entryPair.second > 1)
                    bad += entryPair.second - 1;
            }
            return bad;
        };

        constexpr int kMaxPasses = 8;
        int previous = inconsistentCount(source);
        for (int pass = 0; pass < kMaxPasses && previous > 0; ++pass) {
            auto polys = source.polygons();
            tf::orient_faces_consistently(polys);
            const int current = inconsistentCount(source);
            passes = pass + 1;
            if (current == 0 || current >= previous)
                break; // converged, or no longer improving
            previous = current;
        }
        remainingInconsistent = inconsistentCount(source);

        if (outward) {
            auto polys = source.polygons();
            tf::ensure_positive_orientation(polys, /*is_consistent=*/true);
        }

        // Only the winding changed, so the faces are rewritten in place; positions and
        // the vertex numbering are untouched.
        const std::vector<std::size_t> live = liveVertexIndices(entry.mesh);
        std::size_t faceIndex = 0;
        const auto faces = source.faces();
        for (VCGFace &f : entry.mesh.face) {
            if (f.IsD())
                continue;
            if (faceIndex >= std::size_t(faces.size()))
                break;
            const auto &face = faces[faceIndex++];
            bool usable = true;
            std::size_t corner[3];
            for (int k = 0; k < 3; ++k) {
                const std::size_t id = std::size_t(face[std::size_t(k)]);
                if (id >= live.size()) {
                    usable = false;
                    break;
                }
                corner[k] = live[id];
            }
            if (!usable)
                continue;
            const bool changed = (f.cV(0) != &entry.mesh.vert[corner[0]])
                || (f.cV(1) != &entry.mesh.vert[corner[1]])
                || (f.cV(2) != &entry.mesh.vert[corner[2]]);
            if (!changed)
                continue;
            for (int k = 0; k < 3; ++k)
                f.V(k) = &entry.mesh.vert[corner[std::size_t(k)]];
            ++flipped;
        }

        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(entry.mesh);
        entry.ioMask |= Mask::IOM_VERTNORMAL | Mask::IOM_FACENORMAL;
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm orientation failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    doc.markMeshGeometryChanged(
        index, QObject::tr("Oriented faces of '%1'").arg(doc.mesh(index).name));
    doc.finishFilterProgress(true, QObject::tr("Oriented faces."));

    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.infoMessages << QObject::tr("Reoriented %1 face(s) over %2 pass(es).")
                               .arg(flipped).arg(passes);
    if (remainingInconsistent > 0) {
        result.infoMessages << QObject::tr(
            "%1 edge(s) remain inconsistently wound; the mesh may be non-orientable.")
                                   .arg(remainingInconsistent);
    }
    return result;
}

// Mark the edges reported by TrueForm on the VCG mesh's per-face edge selection, which is
// where QMeshLab keeps edge selections. The reported pairs are point indices, so they are
// mapped back through the live-vertex table and matched against each face's three edges.
int selectFaceEdges(
    VCGMesh &mesh,
    const std::vector<std::size_t> &live,
    const std::vector<std::pair<int, int>> &edges,
    bool clearFirst)
{
    std::set<std::pair<std::size_t, std::size_t>> wanted;
    for (const auto &e : edges) {
        if (e.first < 0 || e.second < 0)
            continue;
        if (std::size_t(e.first) >= live.size() || std::size_t(e.second) >= live.size())
            continue;
        auto a = live[std::size_t(e.first)];
        auto b = live[std::size_t(e.second)];
        wanted.insert(a < b ? std::make_pair(a, b) : std::make_pair(b, a));
    }

    const VCGVertex *base = mesh.vert.empty() ? nullptr : &mesh.vert.front();
    int marked = 0;
    for (VCGFace &f : mesh.face) {
        if (f.IsD() || !base)
            continue;
        for (int k = 0; k < 3; ++k) {
            if (clearFirst)
                f.ClearFaceEdgeS(k);
            const auto a = std::size_t(f.cV(k) - base);
            const auto b = std::size_t(f.cV((k + 1) % 3) - base);
            const auto key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
            if (wanted.count(key)) {
                f.SetFaceEdgeS(k);
                ++marked;
            }
        }
    }
    return marked;
}

MeshFilterRunResult runSelectEdges(const QString &filterId, const FilterParams &params, Document &doc)
{
    const int index = doc.currentMeshIndex();
    if (index < 0 || index >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));
    if (doc.mesh(index).mesh.FN() <= 0)
        return fail(QObject::tr("The layer needs faces."));

    const bool crease = (filterId == QString::fromLatin1(kFilterSelectCrease));
    const double angle = params.getDouble(QStringLiteral("angle"), 60.0);
    const bool clearFirst = params.getBool(QStringLiteral("replaceSelection"), true);

    doc.beginFilterProgress(crease ? QObject::tr("Select Crease Edges (TrueForm)")
                                   : QObject::tr("Select Non-Manifold Edges (TrueForm)"));
    int marked = 0;
    try {
        Document::MeshEntry &entry = doc.mesh(index);
        const TfMesh source = tfMeshFromLayer(entry);

        std::vector<std::pair<int, int>> pairs;
        if (crease) {
            auto sharp = tf::make_sharp_edges(source.polygons(), tf::deg<float>(float(angle)));
            for (const auto &e : sharp)
                pairs.emplace_back(int(e[0]), int(e[1]));
        } else {
            auto nm = tf::make_non_manifold_edges(source.polygons());
            for (const auto &e : nm)
                pairs.emplace_back(int(e[0]), int(e[1]));
        }

        marked = selectFaceEdges(entry.mesh, liveVertexIndices(entry.mesh), pairs, clearFirst);
        entry.ioMask |= Mask::IOM_FACEFLAGS;
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm edge selection failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    doc.markMeshSelectionChanged(
        index, QObject::tr("Selected edges on '%1'").arg(doc.mesh(index).name));
    doc.finishFilterProgress(true, QObject::tr("Updated edge selection."));

    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.infoMessages << QObject::tr("Marked %1 face-edge(s).").arg(marked);
    return result;
}

// ---------------------------------------------------------------------------
// Arrangement repair, isobands and cleaning
// ---------------------------------------------------------------------------

// Resolve a mesh against itself. Every self-crossing becomes a real edge and every
// crossed face is split along it, so the result has a well-defined inside and outside
// where the original had neither.
MeshFilterRunResult runRepairSelfIntersections(const FilterParams &params, Document &doc)
{
    const int index = params.getMesh(QStringLiteral("sourceMesh"), doc.currentMeshIndex());
    if (index < 0 || index >= doc.meshCount())
        return fail(QObject::tr("No layer selected."));
    if (doc.mesh(index).mesh.FN() <= 0)
        return fail(QObject::tr("The layer needs faces."));

    doc.beginFilterProgress(QObject::tr("Repair Self-Intersections"));
    try {
        const TfMesh source = tfMeshFromLayer(doc.mesh(index));
        using FormView = decltype(std::declval<const TfMesh &>().polygons());
        std::vector<FormView> forms;
        forms.emplace_back(source.polygons());
        auto arrangement = tf::make_mesh_arrangements(tf::make_range(forms.data(), forms.size()));

        return addResultLayer(
            doc, std::get<0>(arrangement), QObject::tr("Resolved"),
            QObject::tr("The arrangement is empty."),
            { QObject::tr("Resolved self-intersections of '%1'.").arg(doc.mesh(index).name) });
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm arrangement failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
}

// Split the surface along contours of the vertex scalar, so each band between successive
// contour values becomes its own set of faces.
MeshFilterRunResult runCutAlongIsocontour(const FilterParams &params, Document &doc)
{
    const int index = params.getMesh(QStringLiteral("sourceMesh"), doc.currentMeshIndex());
    if (index < 0 || index >= doc.meshCount())
        return fail(QObject::tr("No layer selected."));
    const VCGMesh &mesh = doc.mesh(index).mesh;
    if (mesh.FN() <= 0)
        return fail(QObject::tr("The layer needs faces."));

    const int count = std::max(1, params.getInt(QStringLiteral("contourCount"), 5));

    std::vector<float> scalars;
    float lo = std::numeric_limits<float>::max();
    float hi = std::numeric_limits<float>::lowest();
    for (const VCGVertex &v : mesh.vert) {
        if (v.IsD())
            continue;
        const float q = v.cQ();
        scalars.push_back(q);
        if (std::isfinite(q)) {
            lo = std::min(lo, q);
            hi = std::max(hi, q);
        }
    }
    if (scalars.empty())
        return fail(QObject::tr("The layer has no live vertices."));
    if (!(hi > lo)) {
        return fail(QObject::tr(
            "The scalar field is constant, so there is nothing to cut along. Compute a "
            "per-vertex scalar first."));
    }

    std::vector<float> cutValues;
    for (int i = 0; i < count; ++i) {
        const double t = (double(i) + 1.0) / (double(count) + 1.0);
        cutValues.push_back(float(double(lo) + t * (double(hi) - double(lo))));
    }
    // N cut values partition the range into N+1 bands; keep all of them, so the result is
    // the whole surface, cut rather than filtered.
    std::vector<int> bands(std::size_t(count) + 1);
    for (std::size_t i = 0; i < bands.size(); ++i)
        bands[i] = int(i);

    doc.beginFilterProgress(QObject::tr("Cut Along Scalar Isocontour"));
    try {
        const TfMesh source = tfMeshFromLayer(doc.mesh(index));
        auto banded = tf::make_isobands(
            source.polygons(),
            tf::make_range(scalars.data(), scalars.size()),
            tf::make_range(cutValues.data(), cutValues.size()),
            tf::make_range(bands.data(), bands.size()));
        return addResultLayer(
            doc, std::get<0>(banded), QObject::tr("Isobands"),
            QObject::tr("Cutting produced no faces."),
            { QObject::tr("From '%1'.").arg(doc.mesh(index).name),
              QObject::tr("%1 contour(s), %2 band(s), between %3 and %4.")
                  .arg(count).arg(bands.size())
                  .arg(QString::number(double(lo), 'g', 6))
                  .arg(QString::number(double(hi), 'g', 6)) });
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm isobands failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
}

// Weld coincident vertices and drop the degeneracies that welding exposes.
MeshFilterRunResult runCleanMesh(const FilterParams &params, Document &doc)
{
    const int index = doc.currentMeshIndex();
    if (index < 0 || index >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));
    if (doc.mesh(index).mesh.FN() <= 0)
        return fail(QObject::tr("The layer needs faces."));

    const double tolerance = params.getDouble(QStringLiteral("tolerance"), 0.0);

    doc.beginFilterProgress(QObject::tr("Remove Duplicate Vertices (TrueForm)"));
    try {
        const TfMesh source = tfMeshFromLayer(doc.mesh(index));
        const int beforeV = doc.mesh(index).mesh.VN();
        const int beforeF = doc.mesh(index).mesh.FN();

        auto cleaned = (tolerance > 0.0)
            ? tf::cleaned(source.polygons(), float(tolerance))
            : tf::cleaned(source.polygons());

        return replaceLayerGeometry(
            doc, index, cleaned,
            QObject::tr("Cleaned '%1'").arg(doc.mesh(index).name),
            { tolerance > 0.0
                  ? QObject::tr("Welded vertices closer than %1.")
                        .arg(QString::number(tolerance, 'g', 6))
                  : QObject::tr("Welded exactly coincident vertices."),
              QObject::tr("Was %1 vertices, %2 faces.").arg(beforeV).arg(beforeF) });
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm cleaning failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
}

// Improve triangle quality without changing the vertex count: interleaved rounds of edge
// flips and tangential relaxation. Connectivity and positions both change, but no vertex
// is added or removed, so it refines an existing tessellation rather than rebuilding it.
MeshFilterRunResult runImproveTriangulation(const FilterParams &params, Document &doc)
{
    const int index = doc.currentMeshIndex();
    if (index < 0 || index >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));
    if (doc.mesh(index).mesh.FN() <= 0)
        return fail(QObject::tr("The layer needs faces."));

    tf::improve_config<float> config;
    config.iterations = std::max(1, params.getInt(QStringLiteral("iterations"), 3));
    config.relaxation_iters = std::max(0, params.getInt(QStringLiteral("relaxationIterations"), 3));
    config.lambda = float(std::clamp(params.getDouble(QStringLiteral("lambda"), 0.5), 0.0, 1.0));
    config.check_normals = params.getBool(QStringLiteral("checkNormals"), true);
    config.flip = (params.getEnum(QStringLiteral("objective")) == QStringLiteral("min_angle"))
        ? tf::flip_objective::min_angle
        : tf::flip_objective::valence;
    // Zero lets relaxation move vertices freely along the surface; a positive bound caps
    // how far the surface may drift from where it started.
    config.max_deviation = float(std::max(0.0, params.getDouble(QStringLiteral("maxDeviation"), 0.0)));

    doc.beginFilterProgress(QObject::tr("Remesh by Edge Flipping (TrueForm)"));
    try {
        const TfMesh source = tfMeshFromLayer(doc.mesh(index));
        // No convenience wrapper for this one: extract the half-edge structure, operate on
        // it, and rebuild, which is what isotropic_remeshed() does internally.
        auto [he, points] = tf::remesh::extract_he_points(source.polygons());
        tf::improve_triangulation(he, tf::make_points(points), config);
        auto improved = tf::remesh::make_mesh(he, std::move(points));

        return replaceLayerGeometry(
            doc, index, improved,
            QObject::tr("Improved the triangulation of '%1'").arg(doc.mesh(index).name),
            { config.flip == tf::flip_objective::min_angle
                  ? QObject::tr("Flips chosen to maximise the smallest angle.")
                  : QObject::tr("Flips chosen to even out vertex valence.") });
    } catch (const std::exception &e) {
        const QString message = QObject::tr("TrueForm triangulation improvement failed: %1")
                                    .arg(QString::fromLocal8Bit(e.what()));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
}

} // namespace

QString TrueFormFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.trueform");
}

QString TrueFormFilterPlugin::name() const
{
    return QObject::tr("TrueForm Filters");
}

MeshFilterRunResult TrueFormFilterPlugin::runFilter(
    const QString &filterId,
    const FilterParams &params,
    Document &doc) const
{
    if (filterId == QString::fromLatin1(kFilterAlignObb))
        return runAlignObb(params, doc);
    if (filterId == QString::fromLatin1(kFilterAlignIcp))
        return runAlignIcp(params, doc);
    if (filterId == QString::fromLatin1(kFilterAlignCorresponding))
        return runAlignCorresponding(params, doc);
    if (filterId == QString::fromLatin1(kFilterUnion)
        || filterId == QString::fromLatin1(kFilterIntersection)
        || filterId == QString::fromLatin1(kFilterDifference))
        return runBoolean(filterId, params, doc);
    if (filterId == QString::fromLatin1(kFilterSymmetricDifference))
        return runSymmetricDifference(params, doc);
    if (filterId == QString::fromLatin1(kFilterCsg))
        return runCsgExpression(params, doc);
    if (filterId == QString::fromLatin1(kFilterOuterShell))
        return runOuterShell(params, doc);
    if (filterId == QString::fromLatin1(kFilterSelfIntersectionCurves))
        return runSelfIntersectionCurves(params, doc);
    if (filterId == QString::fromLatin1(kFilterIntersectionCurves))
        return runIntersectionCurves(params, doc);
    if (filterId == QString::fromLatin1(kFilterIsocurves))
        return runIsocurves(params, doc);
    if (filterId == QString::fromLatin1(kFilterTube))
        return runTubeFromPolyline(params, doc);
    if (filterId == QString::fromLatin1(kFilterSignedDistance))
        return runSignedDistance(params, doc);
    if (filterId == QString::fromLatin1(kFilterSelectInside))
        return runSelectInsideMesh(params, doc);
    if (filterId == QString::fromLatin1(kFilterChamfer))
        return runChamferDistance(params, doc);
    if (filterId == QString::fromLatin1(kFilterLaplacian)
        || filterId == QString::fromLatin1(kFilterTaubin))
        return runSmooth(filterId, params, doc);
    if (filterId == QString::fromLatin1(kFilterCurvature))
        return runCurvature(params, doc);
    if (filterId == QString::fromLatin1(kFilterNormals))
        return runNormals(params, doc);
    if (filterId == QString::fromLatin1(kFilterIsotropic)
        || filterId == QString::fromLatin1(kFilterSimplify)
        || filterId == QString::fromLatin1(kFilterDecimate))
        return runRemesh(filterId, params, doc);
    if (filterId == QString::fromLatin1(kFilterOrientCoherent)
        || filterId == QString::fromLatin1(kFilterOrientOutward))
        return runOrient(filterId, params, doc);
    if (filterId == QString::fromLatin1(kFilterSelectCrease)
        || filterId == QString::fromLatin1(kFilterSelectNonManifold))
        return runSelectEdges(filterId, params, doc);
    if (filterId == QString::fromLatin1(kFilterRepairSelfIntersections))
        return runRepairSelfIntersections(params, doc);
    if (filterId == QString::fromLatin1(kFilterCutIsocontour))
        return runCutAlongIsocontour(params, doc);
    if (filterId == QString::fromLatin1(kFilterClean))
        return runCleanMesh(params, doc);
    if (filterId == QString::fromLatin1(kFilterImprove))
        return runImproveTriangulation(params, doc);
    return fail(QObject::tr("Unknown filter id: %1").arg(filterId));
}

void registerTrueFormFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<TrueFormFilterPlugin>());
}
