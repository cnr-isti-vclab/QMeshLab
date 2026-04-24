#ifndef QMESH_POISSON_UTILS_H
#define QMESH_POISSON_UTILS_H

#ifdef WIN32
#include <windows.h>
#include <Psapi.h>
#endif

#include "document.h"
#include "vcgmesh.h"

#include <QDebug>
#include <QMatrix4x4>
#include <QVector4D>

#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/allocate.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <vcg/space/box3.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <limits>
#include <memory>
#include <string>
#include <stdexcept>
#include <vector>

#include "Src/MyTime.h"
#include "Src/MarchingCubes.h"
#include "Src/Octree.h"
#include "Src/SparseMatrix.h"
#include "Src/CmdLineParser.h"
#include "Src/PPolynomial.h"
using CMeshO = VCGMesh;
using Scalarm = float;
using MESHLAB_SCALAR = float;
using Point3m = vcg::Point3f;
using Point4m = vcg::Point4f;
using Box3m = vcg::Box3f;

inline std::string &LastPoissonErrorMessage()
{
    static thread_local std::string message;
    return message;
}

inline std::atomic<bool> &PoissonIsoSurfaceFailureFlag()
{
    static std::atomic<bool> failed(false);
    return failed;
}

inline void ResetPoissonIsoSurfaceFailure()
{
    PoissonIsoSurfaceFailureFlag().store(false, std::memory_order_relaxed);
}

inline bool HasPoissonIsoSurfaceFailure()
{
    return PoissonIsoSurfaceFailureFlag().load(std::memory_order_relaxed);
}

inline void SetPoissonIsoSurfaceFailure(const std::string &message)
{
    if (!message.empty())
        LastPoissonErrorMessage() = message;
    PoissonIsoSurfaceFailureFlag().store(true, std::memory_order_relaxed);
}

#include "Src/MultiGridOctreeData.h"

inline void DumpOutput(const char *format, ...)
{
    char buf[4096];
    va_list marker;
    va_start(marker, format);
    vsnprintf(buf, sizeof(buf), format, marker);
    va_end(marker);
    qDebug().noquote() << QString::fromLocal8Bit(buf).trimmed();
}

inline void DumpOutput2(std::vector<char *> &, const char *format, ...)
{
    char buf[4096];
    va_list marker;
    va_start(marker, format);
    vsnprintf(buf, sizeof(buf), format, marker);
    va_end(marker);
    qDebug().noquote() << QString::fromLocal8Bit(buf).trimmed();
}

#if defined(_WIN32) || defined(_WIN64)
inline double PeakMemoryUsageMB(void)
{
    HANDLE h = GetCurrentProcess();
    PROCESS_MEMORY_COUNTERS pmc;
    return GetProcessMemoryInfo(h, &pmc, sizeof(pmc)) ? (static_cast<double>(pmc.PeakWorkingSetSize)) / (1 << 20) : 0;
}

inline double to_seconds(const FILETIME &ft)
{
    const double low_to_sec = 100e-9;
    const double high_to_sec = low_to_sec * 4294967296.0;
    return ft.dwLowDateTime * low_to_sec + ft.dwHighDateTime * high_to_sec;
}
#endif

template<class Real>
struct OctreeProfiler
{
    Octree<Real> &tree;
    double t;

    explicit OctreeProfiler(Octree<Real> &treeRef)
        : tree(treeRef)
        , t(0.0)
    {}

    void start(void)
    {
        t = Time();
        tree.resetLocalMemoryUsage();
    }

    void dumpOutput2(std::vector<char *> &comments, const char *header) const
    {
        tree.memoryUsage();
#if defined(_WIN32) || defined(_WIN64)
        if (header)
            DumpOutput2(comments, "%s %9.1f (s), %9.1f (MB) / %9.1f (MB) / %9.1f (MB)\n", header, Time() - t, tree.localMemoryUsage(), tree.maxMemoryUsage(), PeakMemoryUsageMB());
        else
            DumpOutput2(comments, "%9.1f (s), %9.1f (MB) / %9.1f (MB) / %9.1f (MB)\n", Time() - t, tree.localMemoryUsage(), tree.maxMemoryUsage(), PeakMemoryUsageMB());
#else
        if (header)
            DumpOutput2(comments, "%s %9.1f (s), %9.1f (MB) / %9.1f (MB)\n", header, Time() - t, tree.localMemoryUsage(), tree.maxMemoryUsage());
        else
            DumpOutput2(comments, "%9.1f (s), %9.1f (MB) / %9.1f (MB)\n", Time() - t, tree.localMemoryUsage(), tree.maxMemoryUsage());
#endif
    }

    void dumpOutput(const char *header) const
    {
        tree.memoryUsage();
#if defined(_WIN32) || defined(_WIN64)
        if (header)
            DumpOutput("%s %9.1f (s), %9.1f (MB) / %9.1f (MB) / %9.1f (MB)\n", header, Time() - t, tree.localMemoryUsage(), tree.maxMemoryUsage(), PeakMemoryUsageMB());
        else
            DumpOutput("%9.1f (s), %9.1f (MB) / %9.1f (MB) / %9.1f (MB)\n", Time() - t, tree.localMemoryUsage(), tree.maxMemoryUsage(), PeakMemoryUsageMB());
#else
        if (header)
            DumpOutput("%s %9.1f (s), %9.1f (MB) / %9.1f (MB)\n", header, Time() - t, tree.localMemoryUsage(), tree.maxMemoryUsage());
        else
            DumpOutput("%9.1f (s), %9.1f (MB) / %9.1f (MB)\n", Time() - t, tree.localMemoryUsage(), tree.maxMemoryUsage());
#endif
    }
};

template<class Real>
class PoissonParam
{
public:
    int MaxDepthVal;
    int MaxSolveDepthVal;
    int KernelDepthVal;
    int MinDepthVal;
    int FullDepthVal;
    Real SamplesPerNodeVal;
    Real ScaleVal;
    bool ConfidenceFlag;
    bool CleanFlag;
    bool DensityFlag;
    Real PointWeightVal;
    int AdaptiveExponentVal;
    int BoundaryTypeVal;
    bool CompleteFlag;
    bool NonManifoldFlag;
    bool ShowResidualFlag;
    int CGDepthVal;
    int ItersVal;
    Real CSSolverAccuracyVal;

    bool VerboseFlag;
    int ThreadsVal;
    bool LinearFitFlag;
    float LowResIterMultiplierVal;
    float ColorVal;

    PoissonParam()
    {
        MaxDepthVal = 8;
        MaxSolveDepthVal = -1;
        KernelDepthVal = -1;
        MinDepthVal = 0;
        FullDepthVal = 5;
        SamplesPerNodeVal = 1.5f;
        ScaleVal = 1.1f;
        ConfidenceFlag = false;
        CleanFlag = false;
        DensityFlag = false;
        PointWeightVal = 4.0f;
        AdaptiveExponentVal = 1;
        BoundaryTypeVal = 1;
        CompleteFlag = false;
        NonManifoldFlag = false;
        ShowResidualFlag = false;
        CGDepthVal = 0;
        ItersVal = 8;
        CSSolverAccuracyVal = 1e-3f;

        VerboseFlag = false;
        ThreadsVal = omp_get_num_procs();
        LinearFitFlag = false;
        LowResIterMultiplierVal = 1.f;
        ColorVal = 16.0f;
    }
};

template<class Real>
inline vcg::Point3<Real> qMatrixMapPoint(const QMatrix4x4 &matrix, const vcg::Point3<Real> &point)
{
    const QVector4D mapped = matrix * QVector4D(point[0], point[1], point[2], 1.0f);
    return vcg::Point3<Real>(Real(mapped.x()), Real(mapped.y()), Real(mapped.z()));
}

template<class Real>
inline vcg::Point3<Real> qMatrixMapDirection(const QMatrix4x4 &matrix, const vcg::Point3<Real> &direction)
{
    const QVector4D mapped = matrix * QVector4D(direction[0], direction[1], direction[2], 0.0f);
    return vcg::Point3<Real>(Real(mapped.x()), Real(mapped.y()), Real(mapped.z()));
}

template<class Real>
class DocumentMeshPointStream : public OrientedPointStreamWithData<Real, Point3D<Real>>
{
public:
    DocumentMeshPointStream(const Document &doc, std::vector<int> meshIndices)
        : m_doc(doc)
        , m_meshIndices(std::move(meshIndices))
        , m_meshCursor(0)
        , m_vertexCursor(0)
    {}

    void reset(void) override
    {
        m_meshCursor = 0;
        m_vertexCursor = 0;
    }

    bool nextPoint(OrientedPoint3D<Real> &pt, Point3D<Real> &d) override
    {
        while (m_meshCursor < m_meshIndices.size()) {
            const Document::MeshEntry &entry = m_doc.mesh(m_meshIndices[m_meshCursor]);
            const auto &vertices = entry.mesh.vert;
            while (m_vertexCursor < vertices.size()) {
                const VCGVertex &vertex = vertices[m_vertexCursor++];
                if (vertex.IsD())
                    continue;

                const vcg::Point3<Real> pos = qMatrixMapPoint<Real>(entry.renderTransform, vcg::Point3<Real>(vertex.cP()));
                const vcg::Point3<Real> normal = qMatrixMapDirection<Real>(entry.renderTransform, vcg::Point3<Real>(vertex.cN()));
                pt.p[0] = pos[0];
                pt.p[1] = pos[1];
                pt.p[2] = pos[2];
                pt.n[0] = normal[0];
                pt.n[1] = normal[1];
                pt.n[2] = normal[2];
                d[0] = Real(vertex.C()[0]);
                d[1] = Real(vertex.C()[1]);
                d[2] = Real(vertex.C()[2]);
                return true;
            }
            ++m_meshCursor;
            m_vertexCursor = 0;
        }
        return false;
    }

private:
    const Document &m_doc;
    std::vector<int> m_meshIndices;
    std::size_t m_meshCursor;
    std::size_t m_vertexCursor;
};

template<class Real>
Box3m ComputePointStreamBounds(const Document &doc, const std::vector<int> &meshIndices)
{
    Box3m bb;
    for (int meshIndex : meshIndices) {
        if (meshIndex < 0 || meshIndex >= doc.meshCount())
            continue;
        const Document::MeshEntry &entry = doc.mesh(meshIndex);
        for (const VCGVertex &vertex : entry.mesh.vert) {
            if (vertex.IsD())
                continue;
            bb.Add(qMatrixMapPoint<Real>(entry.renderTransform, vcg::Point3<Real>(vertex.cP())));
        }
    }
    return bb;
}

template<class Real>
XForm4x4<Real> GetPointStreamScale(vcg::Box3<Real> &bb, float expFact)
{
    DumpOutput("bbox %f %f %f - %f %f %f", bb.min[0], bb.min[1], bb.min[2], bb.max[0], bb.max[1], bb.max[2]);
    Real scale = bb.Dim()[bb.MaxDim()] * expFact;
    vcg::Point3<Real> center = bb.Center();
    for (int i = 0; i < 3; ++i)
        center[i] -= scale / 2;
    XForm4x4<Real> tXForm = XForm4x4<Real>::Identity(), sXForm = XForm4x4<Real>::Identity();
    for (int i = 0; i < 3; ++i) {
        sXForm(i, i) = Real(1.0 / scale);
        tXForm(3, i) = -center[i];
    }
    return sXForm * tXForm;
}

template<class Real, int Degree, BoundaryType BType, class Vertex>
int _Execute(
    OrientedPointStream<Real> *pointStream,
    Box3m bb,
    CMeshO &pm,
    PoissonParam<Real> &pp,
    vcg::CallBackPos *cb)
{
    typedef typename Octree<Real>::template DensityEstimator<WEIGHT_DEGREE> DensityEstimator;
    typedef typename Octree<Real>::template InterpolationInfo<false> InterpolationInfo;
    typedef OrientedPointStreamWithData<Real, Point3D<Real>> PointStreamWithData;
    typedef TransformedOrientedPointStreamWithData<Real, Point3D<Real>> XPointStreamWithData;

    Reset<Real>();
    LastPoissonErrorMessage().clear();
    std::vector<char *> comments;

    if (cb && !(*cb)(1, "Preparing reconstruction"))
        return 0;

    XForm4x4<Real> xForm = GetPointStreamScale(bb, pp.ScaleVal);
    XForm4x4<Real> iXForm = xForm.inverse();
    DumpOutput2(comments, "Running Screened Poisson Reconstruction (Version 9.0)\n");
    double startTime = Time();

    OctNode<TreeNodeData>::SetAllocator(MEMORY_ALLOCATOR_BLOCK_SIZE);
    Octree<Real> tree;
    OctreeProfiler<Real> profiler(tree);
    tree.threads = pp.ThreadsVal;
    if (pp.MaxSolveDepthVal < 0)
        pp.MaxSolveDepthVal = pp.MaxDepthVal;

    DumpOutput("Using %i threads", pp.ThreadsVal);
    if (pp.KernelDepthVal < 0)
        pp.KernelDepthVal = pp.MaxDepthVal - 2;
    if (pp.KernelDepthVal > pp.MaxDepthVal) {
        DumpOutput("kernelDepth cannot be greater than depth");
        return 0;
    }

    int pointCount;
    Real pointWeightSum;
    auto *samples = new std::vector<typename Octree<Real>::PointSample>();
    std::vector<ProjectiveData<Point3D<Real>, Real>> *sampleData = nullptr;
    DensityEstimator *density = nullptr;
    SparseNodeData<Point3D<Real>, NORMAL_DEGREE> *normalInfo = nullptr;
    Real targetValue = Real(0.5);

    {
        profiler.start();
        sampleData = new std::vector<ProjectiveData<Point3D<Real>, Real>>();
        XPointStreamWithData _pointStream(xForm, (PointStreamWithData &)*pointStream);
        pointCount = tree.template init<Point3D<Real>>(_pointStream, pp.MaxDepthVal, pp.ConfidenceFlag, *samples, sampleData);

#pragma omp parallel for num_threads(pp.ThreadsVal)
        for (int i = 0; i < static_cast<int>(samples->size()); ++i)
            (*samples)[i].sample.data.n *= Real(-1);

        DumpOutput("Input Points / Samples: %d / %d", pointCount, static_cast<int>(samples->size()));
        profiler.dumpOutput2(comments, "# Read input into tree:");
    }

    DenseNodeData<Real, Degree> solution;

    {
        DenseNodeData<Real, Degree> constraints;
        InterpolationInfo *iInfo = nullptr;
        int solveDepth = pp.MaxSolveDepthVal;

        tree.resetNodeIndices();

        {
            profiler.start();
            density = tree.template setDensityEstimator<WEIGHT_DEGREE>(*samples, pp.KernelDepthVal, pp.SamplesPerNodeVal);
            profiler.dumpOutput2(comments, "#   Got kernel density:");
        }

        {
            profiler.start();
            normalInfo = new SparseNodeData<Point3D<Real>, NORMAL_DEGREE>();
            *normalInfo = tree.template setNormalField<NORMAL_DEGREE>(*samples, *density, pointWeightSum, BType == BOUNDARY_NEUMANN);
            profiler.dumpOutput2(comments, "#     Got normal field:");
        }

        if (!pp.DensityFlag) {
            delete density;
            density = nullptr;
        }

        {
            profiler.start();
            std::vector<int> indexMap;
            constexpr int MAX_DEGREE = NORMAL_DEGREE > Degree ? NORMAL_DEGREE : Degree;
            tree.template inalizeForBroodedMultigrid<MAX_DEGREE, Degree, BType>(
                pp.FullDepthVal,
                typename Octree<Real>::template HasNormalDataFunctor<NORMAL_DEGREE>(*normalInfo),
                &indexMap);

            if (normalInfo)
                normalInfo->remapIndices(indexMap);
            if (density)
                density->remapIndices(indexMap);
            profiler.dumpOutput2(comments, "#       Finalized tree:");
        }

        {
            profiler.start();
            constraints = tree.template initDenseNodeData<Degree>();
            tree.template addFEMConstraints<Degree, BType, NORMAL_DEGREE, BType>(
                FEMVFConstraintFunctor<NORMAL_DEGREE, BType, Degree, BType>(1., 0.),
                *normalInfo,
                constraints,
                solveDepth);
            profiler.dumpOutput2(comments, "#  Set FEM constraints:");
        }

        delete normalInfo;
        normalInfo = nullptr;

        if (pp.PointWeightVal > 0) {
            profiler.start();
            iInfo = new InterpolationInfo(tree, *samples, targetValue, pp.AdaptiveExponentVal, Real(pp.PointWeightVal) * pointWeightSum, Real(0));
            tree.template addInterpolationConstraints<Degree, BType>(*iInfo, constraints, solveDepth);
            profiler.dumpOutput2(comments, "#Set point constraints:");
        }

        DumpOutput(
            "Leaf Nodes / Active Nodes / Ghost Nodes: %d / %d / %d",
            static_cast<int>(tree.leaves()),
            static_cast<int>(tree.nodes()),
            static_cast<int>(tree.ghostNodes()));

        {
            profiler.start();
            typename Octree<Real>::SolverInfo solverInfo;
            solverInfo.cgDepth = pp.CGDepthVal;
            solverInfo.iters = pp.ItersVal;
            solverInfo.cgAccuracy = pp.CSSolverAccuracyVal;
            solverInfo.verbose = pp.VerboseFlag;
            solverInfo.showResidual = pp.ShowResidualFlag;
            solverInfo.lowResIterMultiplier = std::max<double>(1., pp.LowResIterMultiplierVal);
            solution = tree.template solveSystem<Degree, BType>(
                FEMSystemFunctor<Degree, BType>(0, 1., 0),
                iInfo,
                constraints,
                solveDepth,
                solverInfo);
            profiler.dumpOutput2(comments, "# Linear system solved:");
            if (iInfo)
                delete iInfo, iInfo = nullptr;
        }
    }

    if (cb && !(*cb)(75, "Extracting surface"))
        return 0;

    constexpr int kInMemoryIsoSurfaceSampleThreshold = 100000;
    const bool useInMemoryIsoSurfaceMesh = static_cast<int>(samples->size()) <= kInMemoryIsoSurfaceSampleThreshold;

    std::unique_ptr<CoredVectorMeshData<Vertex>> vectorMesh;
    std::unique_ptr<CoredFileMeshData<Vertex>> fileMesh;
    CoredMeshData<Vertex> *mesh = nullptr;

    if (useInMemoryIsoSurfaceMesh) {
        vectorMesh = std::make_unique<CoredVectorMeshData<Vertex>>();
        mesh = vectorMesh.get();
        DumpOutput(
            "Using in-memory iso-surface mesh storage (%d samples <= %d threshold)",
            static_cast<int>(samples->size()),
            kInMemoryIsoSurfaceSampleThreshold);
    }
    else {
        fileMesh = std::make_unique<CoredFileMeshData<Vertex>>();
        mesh = fileMesh.get();
        DumpOutput(
            "Using file-backed iso-surface mesh storage (%d samples > %d threshold)",
            static_cast<int>(samples->size()),
            kInMemoryIsoSurfaceSampleThreshold);
#if !defined(NDEBUG)
        const bool logTemporaryMeshFiles = true;
#else
        const bool logTemporaryMeshFiles = pp.VerboseFlag;
#endif
        if (logTemporaryMeshFiles) {
            DumpOutput("Temporary Poisson point file: %s", fileMesh->outOfCorePointFileName());
            DumpOutput("Temporary Poisson polygon file: %s", fileMesh->polygonFileNameStr());
        }
    }

    {
        profiler.start();
        double valueSum = 0, weightSum = 0;
        typename Octree<Real>::template MultiThreadedEvaluator<Degree, BType> evaluator(&tree, solution, pp.ThreadsVal);
#pragma omp parallel for num_threads(pp.ThreadsVal) reduction(+ : valueSum, weightSum)
        for (int j = 0; j < static_cast<int>(samples->size()); ++j) {
            ProjectiveData<OrientedPoint3D<Real>, Real> &sample = (*samples)[j].sample;
            Real w = sample.weight;
            if (w > 0)
                weightSum += w, valueSum += evaluator.value(sample.data.p / sample.weight, omp_get_thread_num(), (*samples)[j].node) * w;
        }
        Real isoValue = Real(valueSum / weightSum);
        profiler.dumpOutput("Got average:");
        DumpOutput("Iso-Value: %e", isoValue);

        profiler.start();
        SparseNodeData<ProjectiveData<Point3D<Real>, Real>, DATA_DEGREE> *colorData = nullptr;
        if (sampleData) {
            colorData = new SparseNodeData<ProjectiveData<Point3D<Real>, Real>, DATA_DEGREE>();
            *colorData = tree.template setDataField<DATA_DEGREE, false>(*samples, *sampleData, static_cast<DensityEstimator *>(nullptr));
            delete sampleData, sampleData = nullptr;
            for (const OctNode<TreeNodeData> *n = tree.tree().nextNode(); n; n = tree.tree().nextNode(n)) {
                ProjectiveData<Point3D<Real>, Real> *clr = (*colorData)(n);
                if (clr)
                    (*clr) *= Real(pow(pp.ColorVal, tree.depth(n)));
            }
        }
        const int previousTreeThreads = tree.threads;
        tree.threads = 1;
        if (previousTreeThreads > 1)
            DumpOutput("Using %i threads for solve, forcing 1 thread for legacy iso-surface extraction stability", previousTreeThreads);
        tree.template getMCIsoSurface<Degree, BType, WEIGHT_DEGREE, DATA_DEGREE>(
            density,
            colorData,
            solution,
            isoValue,
            *mesh,
            !pp.LinearFitFlag,
            !pp.NonManifoldFlag,
            false);
        tree.threads = previousTreeThreads;
        if (HasPoissonIsoSurfaceFailure() || !LastPoissonErrorMessage().empty()) {
            if (LastPoissonErrorMessage().empty())
                LastPoissonErrorMessage() = "Unknown screened Poisson extraction failure";
            DumpOutput("Screened Poisson extraction failed: %s", LastPoissonErrorMessage().c_str());
            delete colorData;
            if (density)
                delete density, density = nullptr;
            delete sampleData, sampleData = nullptr;
            delete samples;
            return 0;
        }
        DumpOutput(
            "Vertices / Polygons: %d / %d",
            static_cast<int>(mesh->outOfCorePointCount() + mesh->inCorePoints.size()),
            mesh->polygonCount());
        profiler.dumpOutput2(comments, "#        Got triangles:");
        delete colorData;
    }

    if (cb && !(*cb)(90, "Creating Mesh"))
        return 0;

    mesh->resetIterator();
    for (auto pt = mesh->inCorePoints.begin(); pt != mesh->inCorePoints.end(); ++pt) {
        Point3D<Real> outPoint = iXForm * pt->point;
        vcg::tri::Allocator<CMeshO>::AddVertex(pm, Point3m(outPoint[0], outPoint[1], outPoint[2]));
        pm.vert.back().Q() = pt->value;
        pm.vert.back().C()[0] = pt->color[0];
        pm.vert.back().C()[1] = pt->color[1];
        pm.vert.back().C()[2] = pt->color[2];
    }
    for (int ii = 0; ii < mesh->outOfCorePointCount(); ++ii) {
        Vertex pt;
        mesh->nextOutOfCorePoint(pt);
        Point3D<Real> outPoint = iXForm * pt.point;
        vcg::tri::Allocator<CMeshO>::AddVertex(pm, Point3m(outPoint[0], outPoint[1], outPoint[2]));
        pm.vert.back().Q() = pt.value;
        pm.vert.back().C()[0] = pt.color[0];
        pm.vert.back().C()[1] = pt.color[1];
        pm.vert.back().C()[2] = pt.color[2];
    }

    std::vector<CoredVertexIndex> polygon;
    while (mesh->nextPolygon(polygon)) {
        if (polygon.size() != 3)
            continue;
        int indV[3];
        for (int i = 0; i < static_cast<int>(polygon.size()); ++i) {
            if (polygon[i].inCore)
                indV[i] = polygon[i].idx;
            else
                indV[i] = polygon[i].idx + static_cast<int>(mesh->inCorePoints.size());
        }
        vcg::tri::Allocator<CMeshO>::AddFace(pm, &pm.vert[indV[0]], &pm.vert[indV[1]], &pm.vert[indV[2]]);
    }

    if (cb)
        (*cb)(100, "Done");

    if (density)
        delete density, density = nullptr;
    delete samples;
    DumpOutput2(comments, "#          Total Solve: %9.1f (s), %9.1f (MB)", Time() - startTime, tree.maxMemoryUsage());

    return 1;
}

template<class MeshType>
void PoissonClean(MeshType &m, bool scaleNormal, bool cleanFlag)
{
    vcg::tri::UpdateNormal<MeshType>::NormalizePerVertex(m);

    if (cleanFlag) {
        for (auto vi = m.vert.begin(); vi != m.vert.end(); ++vi) {
            if (vcg::SquaredNorm(vi->N()) < std::numeric_limits<MESHLAB_SCALAR>::min() * 10.0)
                vcg::tri::Allocator<MeshType>::DeleteVertex(m, *vi);
        }

        for (auto fi = m.face.begin(); fi != m.face.end(); ++fi)
            if (fi->V(0)->IsD() || fi->V(1)->IsD() || fi->V(2)->IsD())
                vcg::tri::Allocator<MeshType>::DeleteFace(m, *fi);
    }

    vcg::tri::Allocator<MeshType>::CompactEveryVector(m);
    if (scaleNormal) {
        for (auto vi = m.vert.begin(); vi != m.vert.end(); ++vi)
            vi->N() *= vi->Q();
    }
}

inline bool HasGoodNormal(CMeshO &m)
{
    for (auto vi = m.vert.begin(); vi != m.vert.end(); ++vi)
        if (vcg::SquaredNorm(vi->N()) < std::numeric_limits<float>::min() * 10.0)
            return false;

    return true;
}

#endif
