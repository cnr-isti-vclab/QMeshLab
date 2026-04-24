#include "upstream_backend.h"

#include "document.h"
#include "upstream_qmeshlab_adapter.h"

#include "upstream/Src/MultiThreading.h"
#include "upstream/Src/Reconstructors.h"

#include <QByteArray>
#include <QFileInfo>

#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/allocate.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/normal.h>

#include <algorithm>
#include <memory>

namespace {

QString sourceRootPath()
{
    return QStringLiteral("plugins/filter_screened_poisson/upstream/Src");
}

QStringList keyEntryPoints()
{
    return {
        QStringLiteral("PoissonRecon.cpp"),
        QStringLiteral("Reconstructors.h"),
        QStringLiteral("Reconstructors.streams.h"),
        QStringLiteral("FEMTree.h"),
        QStringLiteral("DataStream.h"),
        QStringLiteral("MultiThreading.h"),
        QStringLiteral("../upstream_qmeshlab_adapter.h"),
        QStringLiteral("../upstream_qmeshlab_adapter.cpp")
    };
}

int intParameter(const MeshFilterParameterValues &params, const QString &id, int fallback)
{
    const auto it = params.constFind(id);
    if (it == params.constEnd())
        return fallback;
    bool ok = false;
    const int value = it.value().toInt(&ok);
    return ok ? value : fallback;
}

double doubleParameter(const MeshFilterParameterValues &params, const QString &id, double fallback)
{
    const auto it = params.constFind(id);
    if (it == params.constEnd())
        return fallback;
    bool ok = false;
    const double value = it.value().toDouble(&ok);
    return ok ? value : fallback;
}

bool envFlagDisabled(const char *name)
{
    const QByteArray value = qgetenv(name).trimmed().toLower();
    return value == "0" || value == "false" || value == "no" || value == "off";
}

void reportProgress(vcg::CallBackPos *cb, int pos, const QString &msg, bool replaceLast = false)
{
    if (!cb)
        return;
    const QByteArray raw = replaceLast
        ? (msg + QStringLiteral("\r")).toLocal8Bit()
        : msg.toLocal8Bit();
    cb(pos, raw.constData());
}

void configureThreadPool(int requestedThreads)
{
    using ThreadPool = PoissonRecon::ThreadPool;
    const unsigned int threads = static_cast<unsigned int>(std::max(1, requestedThreads));
    ThreadPool::SetNumThreads(threads);
    ThreadPool::ChunkSize = 128;
    ThreadPool::Schedule = ThreadPool::DYNAMIC;

#if defined(__APPLE__)
    ThreadPool::ParallelizationType =
        threads > 1 ? ThreadPool::ASYNC : ThreadPool::NONE;
#elif defined(_OPENMP)
    ThreadPool::ParallelizationType =
        threads > 1 ? ThreadPool::OPEN_MP : ThreadPool::NONE;
#else
    ThreadPool::ParallelizationType =
        threads > 1 ? ThreadPool::ASYNC : ThreadPool::NONE;
#endif
}

template<typename ImplicitT, typename VertexStreamT>
void extractLevelSet(
    const ImplicitT &implicit,
    bool linearFit,
    bool preserveDensity,
    int requestedThreads,
    VertexStreamT &vertexStream,
    ScreenedPoissonUpstream::VectorFaceStream &faceStream)
{
    // Upstream level-set extraction appears unstable on macOS when executed
    // with multiple workers. Keep the solve parallel, but force extraction to
    // a single worker for now.
    configureThreadPool(1);
    PoissonRecon::Reconstructor::LevelSetExtractionParameters params;
    params.linearFit = linearFit;
    params.outputGradients = true;
    params.forceManifold = true;
    params.polygonMesh = false;
    params.gridCoordinates = false;
    params.outputDensity = preserveDensity;
    params.verbose = false;
    implicit.extractLevelSet(vertexStream, faceStream, params);
    configureThreadPool(requestedThreads);
}

} // namespace

namespace ScreenedPoissonUpstream
{

BackendStatus inspectBackend()
{
    BackendStatus status;
    status.sourceRoot = sourceRootPath();
    status.keyEntryPoints = keyEntryPoints();
    // If this translation unit is compiled into QMeshLab, the vendored
    // upstream sources were present at build time. Runtime filesystem checks
    // are unreliable once the app is launched from a bundle.
    status.vendoredSourcesPresent = true;
    status.summary =
        status.vendoredSourcesPresent
            ? QStringLiteral("Vendored upstream PoissonRecon sources are available for the next migration phase.")
            : QStringLiteral("Vendored upstream PoissonRecon sources are not complete yet.");
    return status;
}

QString placeholderErrorMessage()
{
    return QStringLiteral(
        "The upstream PoissonRecon backend is scaffolded but not wired yet. "
        "The plugin is still running on the stable legacy MeshLab backend for now.");
}

bool isEnabledByEnvironment()
{
    const QByteArray value = qgetenv("QMESHLAB_POISSON_UPSTREAM").trimmed();
    if (value.isEmpty())
        return true;
    return !envFlagDisabled("QMESHLAB_POISSON_UPSTREAM");
}

MeshFilterRunResult runSingleMeshFilter(
    Document &doc,
    const std::vector<int> &meshIndices,
    bool mergeVisible,
    const MeshFilterParameterValues &parameters)
{
    if (meshIndices.empty()) {
        return {
            false,
            false,
            mergeVisible
                ? QObject::tr("No visible meshes available for Screened Poisson reconstruction.")
                : QObject::tr("No current mesh selected.")
        };
    }
    for (int meshIndex : meshIndices) {
        if (meshIndex < 0 || meshIndex >= doc.meshCount()) {
            return { false, false, QObject::tr("Invalid mesh selection for Screened Poisson reconstruction.") };
        }
    }

    const auto status = inspectBackend();
    if (!status.vendoredSourcesPresent)
        return { false, false, placeholderErrorMessage() };

    const bool preserveColor = allSelectedMeshesHaveVertexColor(doc, meshIndices);
    const bool confidence = parameters.value(QStringLiteral("confidence"), false).toBool();
    const int requestedThreads = std::max(1, intParameter(parameters, QStringLiteral("threads"), 1));
    const qsizetype inputSampleCount = countInputSamples(doc, meshIndices);
    const QString progressLabel = QObject::tr("Surface Reconstruction: Screened Poisson");
    vcg::CallBackPos *cb = doc.progressCallback();
    doc.beginFilterProgress(progressLabel);
    reportProgress(
        cb,
        0,
        mergeVisible
            ? QObject::tr("Preparing upstream Screened Poisson input from %1 visible layers (%2 samples)...")
                  .arg(meshIndices.size())
                  .arg(inputSampleCount)
            : QObject::tr("Preparing upstream Screened Poisson input (%1 samples)...")
                  .arg(inputSampleCount),
        true);

    configureThreadPool(requestedThreads);

    namespace Reconstructor = PoissonRecon::Reconstructor;
    using Signature = PoissonRecon::IsotropicUIntPack<
        Dim,
        PoissonRecon::FEMDegreeAndBType<
            Reconstructor::Poisson::DefaultFEMDegree,
            Reconstructor::Poisson::DefaultFEMBoundary>::Signature>;

    Reconstructor::Poisson::SolutionParameters<Real> solveParams;
    solveParams.verbose = false;
    solveParams.dirichletErode = false;
    solveParams.exactInterpolation = false;
    solveParams.showResidual = false;
    solveParams.confidence = confidence;
    solveParams.scale = Real(std::max(0.1, doubleParameter(parameters, QStringLiteral("scale"), 1.1)));
    solveParams.width = Real(0);
    solveParams.lowDepthCutOff = Real(0);
    solveParams.samplesPerNode = Real(std::max(0.01, doubleParameter(parameters, QStringLiteral("samplesPerNode"), 1.5)));
    solveParams.cgSolverAccuracy = Real(1e-3);
    solveParams.perLevelDataScaleFactor = Real(32);
    solveParams.depth = static_cast<unsigned int>(std::max(1, intParameter(parameters, QStringLiteral("depth"), 8)));
    solveParams.solveDepth = solveParams.depth;
    solveParams.baseDepth = static_cast<unsigned int>(std::max(0, intParameter(parameters, QStringLiteral("cgDepth"), 0)));
    solveParams.fullDepth = static_cast<unsigned int>(std::clamp(intParameter(parameters, QStringLiteral("fullDepth"), 5), 1, int(solveParams.depth)));
    solveParams.kernelDepth = static_cast<unsigned int>(-1);
    solveParams.baseVCycles = 1;
    solveParams.iters = static_cast<unsigned int>(std::max(1, intParameter(parameters, QStringLiteral("iters"), 8)));
    solveParams.alignDir = 0;
    solveParams.pointWeight = Real(std::max(0.0, doubleParameter(parameters, QStringLiteral("pointWeight"), 4.0)));
    solveParams.valueInterpolationWeight = Real(0);

    SelectionOptions selection;
    selection.mergeVisible = mergeVisible;
    selection.confidenceFromQuality = confidence;

    VCGMesh outputMesh;
    QString finishMessage;
    try {
        if (doc.isOperationCancelRequested()) {
            finishMessage = QObject::tr("Filter interrupted by user.");
            doc.finishFilterProgress(false, finishMessage);
            return { false, false, finishMessage };
        }

        reportProgress(
            cb,
            10,
            QObject::tr("Upstream Screened Poisson: solving implicit field (%1 thread%2)...")
                .arg(requestedThreads)
                .arg(requestedThreads == 1 ? QString() : QStringLiteral("s")),
            true);
        if (preserveColor) {
            using Solver = Reconstructor::Poisson::Solver<Real, Dim, Signature, Color>;
            DocumentOrientedPointColorStream pointStream(doc, meshIndices, selection);
            std::unique_ptr<Reconstructor::Implicit<Real, Dim, Signature, Color>> implicit(
                Solver::Solve(pointStream, solveParams, Color()));
            if (doc.isOperationCancelRequested()) {
                finishMessage = QObject::tr("Filter interrupted by user.");
                doc.finishFilterProgress(false, finishMessage);
                return { false, false, finishMessage };
            }
            reportProgress(
                cb,
                75,
                QObject::tr("Upstream Screened Poisson: extracting iso-surface (forcing 1 thread for stability)..."),
                true);
            VectorLevelSetVertexColorStream vertexStream;
            VectorFaceStream faceStream;
            extractLevelSet(*implicit, false, true, requestedThreads, vertexStream, faceStream);
            reportProgress(cb, 92, QObject::tr("Upstream Screened Poisson: assembling mesh..."), true);
            appendToMesh(vertexStream.vertices(), faceStream.faces(), outputMesh);
        } else {
            using Solver = Reconstructor::Poisson::Solver<Real, Dim, Signature>;
            DocumentOrientedPointStream pointStream(doc, meshIndices, selection);
            std::unique_ptr<Reconstructor::Implicit<Real, Dim, Signature>> implicit(
                Solver::Solve(pointStream, solveParams));
            if (doc.isOperationCancelRequested()) {
                finishMessage = QObject::tr("Filter interrupted by user.");
                doc.finishFilterProgress(false, finishMessage);
                return { false, false, finishMessage };
            }
            reportProgress(
                cb,
                75,
                QObject::tr("Upstream Screened Poisson: extracting iso-surface (forcing 1 thread for stability)..."),
                true);
            VectorLevelSetVertexStream vertexStream;
            VectorFaceStream faceStream;
            extractLevelSet(*implicit, false, true, requestedThreads, vertexStream, faceStream);
            reportProgress(cb, 92, QObject::tr("Upstream Screened Poisson: assembling mesh..."), true);
            appendToMesh(vertexStream.vertices(), faceStream.faces(), outputMesh);
        }
    } catch (const std::exception &ex) {
        finishMessage = QObject::tr("Upstream Screened Poisson backend failed: %1").arg(QString::fromUtf8(ex.what()));
        doc.finishFilterProgress(false, finishMessage);
        return {
            false,
            false,
            finishMessage
        };
    }

    if (doc.isOperationCancelRequested()) {
        finishMessage = QObject::tr("Filter interrupted by user.");
        doc.finishFilterProgress(false, finishMessage);
        return { false, false, finishMessage };
    }
    if (outputMesh.VN() <= 0 || outputMesh.FN() <= 0) {
        finishMessage = QObject::tr("Upstream Screened Poisson backend produced an empty mesh.");
        doc.finishFilterProgress(false, finishMessage);
        return { false, false, finishMessage };
    }

    reportProgress(cb, 97, QObject::tr("Upstream Screened Poisson: finalizing mesh..."), true);
    vcg::tri::Allocator<VCGMesh>::CompactEveryVector(outputMesh);
    vcg::tri::UpdateBounding<VCGMesh>::Box(outputMesh);
    vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(outputMesh);

    int ioMask =
        vcg::tri::io::Mask::IOM_VERTQUALITY
        | vcg::tri::io::Mask::IOM_VERTNORMAL
        | vcg::tri::io::Mask::IOM_FACENORMAL;
    if (preserveColor)
        ioMask |= vcg::tri::io::Mask::IOM_VERTCOLOR;

    const int newIndex = doc.addMesh(outputMesh, QObject::tr("Poisson mesh"), ioMask);
    if (newIndex < 0) {
        finishMessage = QObject::tr("Failed to add reconstructed mesh to the document.");
        doc.finishFilterProgress(false, finishMessage);
        return { false, false, finishMessage };
    }

    MeshFilterRunResult result;
    finishMessage = mergeVisible
        ? QObject::tr("Created Poisson mesh from %1 visible layers").arg(meshIndices.size())
        : QObject::tr("Created Poisson mesh from current mesh");
    doc.finishFilterProgress(true, finishMessage);
    result.success = true;
    result.documentModified = true;
    result.newMeshIndices = { newIndex };
    result.infoMessages = {
        mergeVisible
            ? QObject::tr(
                  "Created '%1' with upstream PoissonRecon backend from %2 visible layers (%3 input samples, %4 vertices, %5 faces)")
                  .arg(doc.mesh(newIndex).name)
                  .arg(meshIndices.size())
                  .arg(inputSampleCount)
                  .arg(doc.mesh(newIndex).mesh.VN())
                  .arg(doc.mesh(newIndex).mesh.FN())
            : QObject::tr(
                  "Created '%1' with upstream PoissonRecon backend from current mesh (%2 input samples, %3 vertices, %4 faces)")
                  .arg(doc.mesh(newIndex).name)
                  .arg(inputSampleCount)
                  .arg(doc.mesh(newIndex).mesh.VN())
                  .arg(doc.mesh(newIndex).mesh.FN()),
        QObject::tr(
            "Upstream Screened Poisson used %1 thread%2 for the solve and forced 1 thread for iso-surface extraction stability.")
            .arg(requestedThreads)
            .arg(requestedThreads == 1 ? QString() : QStringLiteral("s"))
    };
    return result;
}

} // namespace ScreenedPoissonUpstream
