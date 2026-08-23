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
#include <cmath>
#include <exception>
#include <optional>
#include <utility>
#include <vector>

#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/normal.h>
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
    return fail(QObject::tr("Unknown filter id: %1").arg(filterId));
}

void registerTrueFormFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<TrueFormFilterPlugin>());
}
