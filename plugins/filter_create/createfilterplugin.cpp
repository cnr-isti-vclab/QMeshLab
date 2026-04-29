#include "createfilterplugin.h"

#include "document.h"
#include "meshfilterpluginmanager.h"
#include "vcgmesh.h"
#include <vcg/complex/algorithms/create/platonic.h>
#include <vcg/complex/algorithms/point_sampling.h>
#include <vcg/complex/algorithms/smooth.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <vcg/complex/algorithms/update/selection.h>
#include <vcg/math/gen_normal.h>
#include <vcg/space/fitting3.h>

namespace {
constexpr QLatin1StringView kFilterCreateBox("create_box");
constexpr QLatin1StringView kFilterCreateAnnulus("create_annulus");
constexpr QLatin1StringView kFilterCreateSphere("create_sphere");
constexpr QLatin1StringView kFilterCreateSphereCap("create_sphere_cap");
constexpr QLatin1StringView kFilterCreateSpherePoints("create_sphere_points");
constexpr QLatin1StringView kFilterCreateIcosahedron("create_icosahedron");
constexpr QLatin1StringView kFilterCreateDodecahedron("create_dodecahedron");
constexpr QLatin1StringView kFilterCreateDodecahedronSym("create_dodecahedron_sym");
constexpr QLatin1StringView kFilterCreateTetrahedron("create_tetrahedron");
constexpr QLatin1StringView kFilterCreateOctahedron("create_octahedron");
constexpr QLatin1StringView kFilterCreateCone("create_cone");
constexpr QLatin1StringView kFilterCreateTorus("create_torus");
constexpr QLatin1StringView kFilterFitPlane("fit_plane_to_selection");

MeshFilterRunResult success(const QString &name, int newIndex)
{
    MeshFilterRunResult r;
    r.success = true;
    r.documentModified = true;
    r.newMeshIndices = { newIndex };
    r.infoMessages = { QObject::tr("Created mesh '%1'").arg(name) };
    return r;
}
} // namespace

QString CreateFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.create");
}

QString CreateFilterPlugin::name() const
{
    return QObject::tr("Create Meshes");
}

MeshFilterRunResult CreateFilterPlugin::runFilter(
    const QString &filterId,
    const FilterParams &params,
    Document &doc) const
{
    if (filterId == QString::fromLatin1(kFilterCreateTetrahedron)) {
        VCGMesh m;
        vcg::tri::Tetrahedron<VCGMesh>(m);
        vcg::tri::UpdateBounding<VCGMesh>::Box(m);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(m);
        const int idx = doc.addMesh(m, QStringLiteral("Tetrahedron"));
        return success(doc.mesh(idx).name, idx);
    }

    if (filterId == QString::fromLatin1(kFilterCreateIcosahedron)) {
        VCGMesh m;
        vcg::tri::Icosahedron<VCGMesh>(m);
        vcg::tri::UpdateBounding<VCGMesh>::Box(m);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(m);
        const int idx = doc.addMesh(m, QStringLiteral("Icosahedron"));
        return success(doc.mesh(idx).name, idx);
    }

    if (filterId == QString::fromLatin1(kFilterCreateDodecahedron)) {
        VCGMesh m;
        vcg::tri::Dodecahedron<VCGMesh>(m);
        vcg::tri::UpdateBounding<VCGMesh>::Box(m);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(m);
        const int idx = doc.addMesh(m, QStringLiteral("Dodecahedron"));
        return success(doc.mesh(idx).name, idx);
    }

    if (filterId == QString::fromLatin1(kFilterCreateDodecahedronSym)) {
        VCGMesh m;
        vcg::tri::DodecahedronSym<VCGMesh>(m);
        vcg::tri::UpdateBounding<VCGMesh>::Box(m);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(m);
        const int idx = doc.addMesh(m, QStringLiteral("Dodecahedron (sym)"));
        return success(doc.mesh(idx).name, idx);
    }

    if (filterId == QString::fromLatin1(kFilterCreateOctahedron)) {
        VCGMesh m;
        vcg::tri::Octahedron<VCGMesh>(m);
        vcg::tri::UpdateBounding<VCGMesh>::Box(m);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(m);
        const int idx = doc.addMesh(m, QStringLiteral("Octahedron"));
        return success(doc.mesh(idx).name, idx);
    }

    if (filterId == QString::fromLatin1(kFilterCreateBox)) {
        const double sz = params.getDouble(QStringLiteral("size"));
        const float hsz = float(sz) * 0.5f;
        VCGMesh m;
        vcg::Box3f b(vcg::Point3f(-hsz, -hsz, -hsz), vcg::Point3f(hsz, hsz, hsz));
        vcg::tri::Box<VCGMesh>(m, b);
        vcg::tri::UpdateBounding<VCGMesh>::Box(m);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(m);
        const int idx = doc.addMesh(m, QStringLiteral("Box"));
        return success(doc.mesh(idx).name, idx);
    }

    if (filterId == QString::fromLatin1(kFilterCreateAnnulus)) {
        const float inner = float(params.getDouble(QStringLiteral("inner_radius")));
        const float outer = float(params.getDouble(QStringLiteral("outer_radius")));
        const int sides  = params.getInt(QStringLiteral("sides"));
        VCGMesh m;
        vcg::tri::Annulus<VCGMesh>(m, inner, outer, sides);
        vcg::tri::UpdateBounding<VCGMesh>::Box(m);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(m);
        const int idx = doc.addMesh(m, QStringLiteral("Annulus"));
        return success(doc.mesh(idx).name, idx);
    }

    if (filterId == QString::fromLatin1(kFilterCreateSphere)) {
        const float radius = float(params.getDouble(QStringLiteral("radius")));
        const int subdiv   = params.getInt(QStringLiteral("subdiv"));
        VCGMesh m;
        vcg::tri::Sphere<VCGMesh>(m, subdiv);
        vcg::tri::UpdatePosition<VCGMesh>::Scale(m, radius);
        vcg::tri::UpdateBounding<VCGMesh>::Box(m);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(m);
        const int idx = doc.addMesh(m, QStringLiteral("Sphere"));
        return success(doc.mesh(idx).name, idx);
    }

    if (filterId == QString::fromLatin1(kFilterCreateSphereCap)) {
        const float angleDeg = float(params.getDouble(QStringLiteral("angle")));
        const int subdiv     = params.getInt(QStringLiteral("subdiv"));
        VCGMesh m;
        vcg::tri::UpdateTopology<VCGMesh>::FaceFace(m);
        vcg::tri::SphericalCap(m, vcg::math::ToRad(angleDeg), subdiv);
        vcg::tri::UpdateBounding<VCGMesh>::Box(m);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(m);
        const int idx = doc.addMesh(m, QStringLiteral("Sphere Cap"));
        return success(doc.mesh(idx).name, idx);
    }

    if (filterId == QString::fromLatin1(kFilterCreateSpherePoints)) {
        const int pointNum = params.getInt(QStringLiteral("point_num"));
        const QString tech = params.getEnum(QStringLiteral("technique"));

        std::vector<vcg::Point3f> sampleVec;
        vcg::math::MarsenneTwisterRNG rng;

        if (tech == QLatin1StringView("montecarlo")) {
            for (int i = 0; i < pointNum; ++i)
                sampleVec.push_back(vcg::math::GeneratePointOnUnitSphereUniform<float>(rng));
        } else if (tech == QLatin1StringView("discoball")) {
            vcg::GenNormal<float>::DiscoBall(pointNum, sampleVec);
        } else if (tech == QLatin1StringView("octahedron")) {
            vcg::GenNormal<float>::RecursiveOctahedron(pointNum, sampleVec);
        } else { // fibonacci (default)
            vcg::GenNormal<float>::Fibonacci(pointNum, sampleVec);
        }

        VCGMesh m;
        vcg::tri::Allocator<VCGMesh>::AddVertices(m, int(sampleVec.size()));
        for (int i = 0; i < int(sampleVec.size()); ++i) {
            m.vert[size_t(i)].P() = sampleVec[size_t(i)];
            m.vert[size_t(i)].N() = sampleVec[size_t(i)];
        }
        vcg::tri::UpdateBounding<VCGMesh>::Box(m);
        const int idx = doc.addMesh(m, QStringLiteral("Points on Sphere"));
        return success(doc.mesh(idx).name, idx);
    }

    if (filterId == QString::fromLatin1(kFilterCreateCone)) {
        const float r0    = float(params.getDouble(QStringLiteral("r0")));
        const float r1    = float(params.getDouble(QStringLiteral("r1")));
        const float h     = float(params.getDouble(QStringLiteral("h")));
        const int subdiv  = params.getInt(QStringLiteral("subdiv"));
        VCGMesh m;
        vcg::tri::Cone<VCGMesh>(m, r0, r1, h, subdiv);
        vcg::tri::UpdateBounding<VCGMesh>::Box(m);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(m);
        const int idx = doc.addMesh(m, QStringLiteral("Cone"));
        return success(doc.mesh(idx).name, idx);
    }

    if (filterId == QString::fromLatin1(kFilterCreateTorus)) {
        const float hRadius = float(params.getDouble(QStringLiteral("h_radius")));
        const float vRadius = float(params.getDouble(QStringLiteral("v_radius")));
        const int hSubdiv   = params.getInt(QStringLiteral("h_subdiv"));
        const int vSubdiv   = params.getInt(QStringLiteral("v_subdiv"));
        VCGMesh m;
        vcg::tri::Torus(m, hRadius, vRadius, hSubdiv, vSubdiv);
        vcg::tri::UpdateBounding<VCGMesh>::Box(m);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(m);
        const int idx = doc.addMesh(m, QStringLiteral("Torus"));
        return success(doc.mesh(idx).name, idx);
    }

    if (filterId == QString::fromLatin1(kFilterFitPlane)) {
        const int meshIndex = doc.currentMeshIndex();
        if (meshIndex < 0 || meshIndex >= doc.meshCount())
            return { false, false, QObject::tr("No current mesh selected.") };

        const VCGMesh &src = doc.mesh(meshIndex).mesh;

        // Collect selected vertices (or all vertices from selected faces if no vertex selection)
        std::vector<vcg::Point3f> pts;
        int svn = 0;
        int sfn = 0;
        for (const auto &v : src.vert)
            if (!v.IsD() && v.IsS()) ++svn;
        for (const auto &f : src.face)
            if (!f.IsD() && f.IsS()) ++sfn;

        if (svn == 0 && sfn == 0)
            return { false, false, QObject::tr("No selection: select vertices or faces first.") };

        vcg::Point3f Naccum(0.f, 0.f, 0.f);
        if (svn > 0) {
            // Use selected vertices directly
            for (const auto &v : src.vert)
                if (!v.IsD() && v.IsS()) {
                    pts.push_back(v.cP());
                    Naccum += v.cN();
                }
        } else {
            // Expand face selection to vertices
            for (const auto &f : src.face)
                if (!f.IsD() && f.IsS())
                    for (int i = 0; i < 3; ++i) {
                        pts.push_back(f.cV(i)->cP());
                        Naccum += f.cV(i)->cN();
                    }
        }

        if (pts.size() < 3)
            return { false, false, QObject::tr("Need at least 3 selected points to fit a plane.") };

        vcg::Plane3f plane;
        vcg::FitPlaneToPointSet(pts, plane);
        plane.Normalize();
        Naccum.Normalize();
        if ((plane.Direction() * Naccum) < 0.f)
            plane.Set(-plane.Direction(), -plane.Offset());

        // Centre of projected selection
        vcg::Point3f centerP(0.f, 0.f, 0.f);
        for (const auto &p : pts)
            centerP += plane.Projection(p);
        centerP /= float(pts.size());

        // Horizontal axis: pick the world axis least parallel to plane normal
        const vcg::Point3f &n = plane.Direction();
        vcg::Point3f dirH;
        if (std::abs(n.X()) <= std::abs(n.Y()) && std::abs(n.X()) <= std::abs(n.Z()))
            dirH = vcg::Point3f(1.f, 0.f, 0.f) ^ n;
        else if (std::abs(n.Y()) <= std::abs(n.X()) && std::abs(n.Y()) <= std::abs(n.Z()))
            dirH = vcg::Point3f(0.f, 1.f, 0.f) ^ n;
        else
            dirH = vcg::Point3f(0.f, 0.f, 1.f) ^ n;
        dirH.Normalize();
        vcg::Point3f dirV = dirH ^ n;
        dirV.Normalize();

        // Extent
        const float extent = float(params.getDouble(QStringLiteral("extent")));
        float dimH = 0.f, dimV = 0.f;
        for (const auto &p : pts) {
            vcg::Point3f pp = plane.Projection(p);
            dimH = std::max(dimH, std::abs((pp - centerP) * dirH));
            dimV = std::max(dimV, std::abs((pp - centerP) * dirV));
        }
        dimH *= extent;
        dimV *= extent;

        const int subdiv = std::max(2, params.getInt(QStringLiteral("subdiv")) + 1);

        VCGMesh m;
        for (int ir = 0; ir < subdiv; ++ir)
            for (int ic = 0; ic < subdiv; ++ic) {
                vcg::Point3f p = (centerP + (dirV * -dimV) + (dirH * -dimH))
                    + dirH * (ic * 2.f * dimH / (subdiv - 1))
                    + dirV * (ir * 2.f * dimV / (subdiv - 1));
                vcg::tri::Allocator<VCGMesh>::AddVertex(m, p, n);
            }
        vcg::tri::FaceGrid(m, subdiv, subdiv);
        vcg::tri::UpdateBounding<VCGMesh>::Box(m);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(m);

        const int idx = doc.addMesh(m, QStringLiteral("Fitted Plane"));
        return success(doc.mesh(idx).name, idx);
    }

    return { false, false, QObject::tr("Unknown filter id: %1").arg(filterId) };
}

void registerCreateFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<CreateFilterPlugin>());
}
