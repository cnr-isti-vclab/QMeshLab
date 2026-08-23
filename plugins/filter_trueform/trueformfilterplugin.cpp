#include "plugins/filter_trueform/trueformfilterplugin.h"

#include "document.h"
#include "filterparam.h"
#include "meshfilterpluginmanager.h"
#include "vcgmesh.h"

#include <QMatrix3x3>
#include <QMatrix4x4>
#include <QObject>
#include <QStringList>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <exception>
#include <vector>

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
    return fail(QObject::tr("Unknown filter id: %1").arg(filterId));
}

void registerTrueFormFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<TrueFormFilterPlugin>());
}
