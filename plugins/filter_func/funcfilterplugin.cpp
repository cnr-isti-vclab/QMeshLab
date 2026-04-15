#include "funcfilterplugin.h"

#include "document.h"
#include "filter_refine.h"
#include "meshfilterpluginmanager.h"
#include <muParser.h>
#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/allocate.h>
#include <vcg/complex/algorithms/create/marching_cubes.h>
#include <vcg/complex/algorithms/create/mc_trivial_walker.h>
#include <vcg/complex/algorithms/create/platonic.h>
#include <vcg/complex/algorithms/refine.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/color.h>
#include <vcg/complex/algorithms/update/flag.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <vcg/complex/algorithms/update/position.h>
#include <vcg/complex/algorithms/update/quality.h>
#include <vcg/complex/algorithms/update/selection.h>
#include <vcg/complex/algorithms/update/topology.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
constexpr QLatin1StringView kFilterVertSelection("conditional_vertex_selection");
constexpr QLatin1StringView kFilterFaceSelection("conditional_face_selection");
constexpr QLatin1StringView kFilterGeomFunc("per_vertex_geometric_function");
constexpr QLatin1StringView kFilterFaceColor("per_face_color_function");
constexpr QLatin1StringView kFilterFaceQuality("per_face_quality_function");
constexpr QLatin1StringView kFilterVertColor("per_vertex_color_function");
constexpr QLatin1StringView kFilterVertQuality("per_vertex_quality_function");
constexpr QLatin1StringView kFilterVertTex("per_vertex_texture_function");
constexpr QLatin1StringView kFilterWedgeTex("per_wedge_texture_function");
constexpr QLatin1StringView kFilterVertNormal("per_vertex_normal_function");
constexpr QLatin1StringView kFilterFaceNormal("per_face_normal_function");
constexpr QLatin1StringView kFilterDefVertScalar("define_per_vertex_scalar_attribute");
constexpr QLatin1StringView kFilterDefFaceScalar("define_per_face_scalar_attribute");
constexpr QLatin1StringView kFilterDefVertPoint("define_per_vertex_point_attribute");
constexpr QLatin1StringView kFilterDefFacePoint("define_per_face_point_attribute");
constexpr QLatin1StringView kFilterGrid("grid_generator");
constexpr QLatin1StringView kFilterRefine("refine_user_defined");
constexpr QLatin1StringView kFilterIso("implicit_surface");

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

QString stringParameter(const MeshFilterParameterValues &params, const QString &id, const QString &fallback)
{
    const auto it = params.constFind(id);
    if (it == params.constEnd())
        return fallback;
    return it.value().toString();
}

uint8_t clampToByte(double value)
{
    if (!std::isfinite(value))
        return 0;
    return static_cast<uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

QString parserErrorString(const mu::Parser::exception_type &e)
{
    return QString::fromStdString(qmeshlab::filters::parserStringToStd(e.GetMsg()));
}

bool isValidIdentifier(const std::string &name)
{
    if (name.empty())
        return false;
    const unsigned char first = static_cast<unsigned char>(name.front());
    if (!(std::isalpha(first) || first == '_'))
        return false;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || u == '_'))
            return false;
    }
    return true;
}

const std::unordered_set<std::string> &reservedVariables()
{
    static const std::unordered_set<std::string> kReserved = {
        "x",
        "y",
        "z",
        "nx",
        "ny",
        "nz",
        "r",
        "g",
        "b",
        "a",
        "q",
        "vi",
        "vtu",
        "vtv",
        "vsel",
        "x0",
        "y0",
        "z0",
        "x1",
        "y1",
        "z1",
        "x2",
        "y2",
        "z2",
        "nx0",
        "ny0",
        "nz0",
        "nx1",
        "ny1",
        "nz1",
        "nx2",
        "ny2",
        "nz2",
        "r0",
        "g0",
        "b0",
        "a0",
        "r1",
        "g1",
        "b1",
        "a1",
        "r2",
        "g2",
        "b2",
        "a2",
        "q0",
        "q1",
        "q2",
        "fr",
        "fg",
        "fb",
        "fa",
        "fnx",
        "fny",
        "fnz",
        "fq",
        "fi",
        "vi0",
        "vi1",
        "vi2",
        "wtu0",
        "wtv0",
        "wtu1",
        "wtv1",
        "wtu2",
        "wtv2",
        "fsel",
        "vsel0",
        "vsel1",
        "vsel2",
        "ti",
        "xmin",
        "ymin",
        "zmin",
        "xmax",
        "ymax",
        "zmax",
        "xmid",
        "ymid",
        "zmid",
        "xdim",
        "ydim",
        "zdim",
        "bbdiag",
        "rnd",
        "randInt"
    };
    return kReserved;
}

struct ParserRuntime
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double nx = 0.0;
    double ny = 0.0;
    double nz = 0.0;
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 0.0;
    double q = 0.0;
    double vi = 0.0;
    double vtu = 0.0;
    double vtv = 0.0;
    double vsel = 0.0;

    double x0 = 0.0;
    double y0 = 0.0;
    double z0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    double z1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
    double z2 = 0.0;
    double nx0 = 0.0;
    double ny0 = 0.0;
    double nz0 = 0.0;
    double nx1 = 0.0;
    double ny1 = 0.0;
    double nz1 = 0.0;
    double nx2 = 0.0;
    double ny2 = 0.0;
    double nz2 = 0.0;
    double r0 = 0.0;
    double g0 = 0.0;
    double b0 = 0.0;
    double a0 = 0.0;
    double r1 = 0.0;
    double g1 = 0.0;
    double b1 = 0.0;
    double a1 = 0.0;
    double r2 = 0.0;
    double g2 = 0.0;
    double b2 = 0.0;
    double a2 = 0.0;
    double q0 = 0.0;
    double q1 = 0.0;
    double q2 = 0.0;
    double fr = 255.0;
    double fg = 255.0;
    double fb = 255.0;
    double fa = 255.0;
    double fnx = 0.0;
    double fny = 0.0;
    double fnz = 0.0;
    double fq = 0.0;
    double fi = 0.0;
    double vi0 = 0.0;
    double vi1 = 0.0;
    double vi2 = 0.0;
    double wtu0 = 0.0;
    double wtv0 = 0.0;
    double wtu1 = 0.0;
    double wtv1 = 0.0;
    double wtu2 = 0.0;
    double wtv2 = 0.0;
    double fsel = 0.0;
    double vsel0 = 0.0;
    double vsel1 = 0.0;
    double vsel2 = 0.0;
    double ti = 0.0;

    double xmin = 0.0;
    double ymin = 0.0;
    double zmin = 0.0;
    double xmax = 0.0;
    double ymax = 0.0;
    double zmax = 0.0;
    double xmid = 0.0;
    double ymid = 0.0;
    double zmid = 0.0;
    double xdim = 0.0;
    double ydim = 0.0;
    double zdim = 0.0;
    double bbdiag = 0.0;

    std::vector<VCGMesh::PerVertexAttributeHandle<float>> vScalarHandles;
    std::vector<double> vScalarValues;
    std::vector<VCGMesh::PerVertexAttributeHandle<vcg::Point3f>> vPointHandles;
    std::vector<double> vPointValues;

    std::vector<VCGMesh::PerFaceAttributeHandle<float>> fScalarHandles;
    std::vector<double> fScalarValues;
    std::vector<VCGMesh::PerFaceAttributeHandle<vcg::Point3f>> fPointHandles;
    std::vector<double> fPointValues;
};

struct CurrentMeshRef
{
    int index = -1;
    Document::MeshEntry *entry = nullptr;
};

std::optional<CurrentMeshRef> currentMesh(Document &doc, QString &error)
{
    const int index = doc.currentMeshIndex();
    if (index < 0 || index >= doc.meshCount()) {
        error = QObject::tr("No current mesh selected.");
        return std::nullopt;
    }
    return CurrentMeshRef{ index, &doc.mesh(index) };
}

void setBBoxRuntime(ParserRuntime &runtime, const VCGMesh &mesh)
{
    runtime.xmin = mesh.bbox.min.X();
    runtime.ymin = mesh.bbox.min.Y();
    runtime.zmin = mesh.bbox.min.Z();
    runtime.xmax = mesh.bbox.max.X();
    runtime.ymax = mesh.bbox.max.Y();
    runtime.zmax = mesh.bbox.max.Z();
    runtime.xdim = mesh.bbox.DimX();
    runtime.ydim = mesh.bbox.DimY();
    runtime.zdim = mesh.bbox.DimZ();
    runtime.bbdiag = mesh.bbox.Diag();
    const vcg::Point3f center = mesh.bbox.Center();
    runtime.xmid = center.X();
    runtime.ymid = center.Y();
    runtime.zmid = center.Z();
}

void defineCommonBBoxVars(mu::Parser &parser, ParserRuntime &runtime)
{
    parser.DefineVar("xmin", &runtime.xmin);
    parser.DefineVar("ymin", &runtime.ymin);
    parser.DefineVar("zmin", &runtime.zmin);
    parser.DefineVar("xmax", &runtime.xmax);
    parser.DefineVar("ymax", &runtime.ymax);
    parser.DefineVar("zmax", &runtime.zmax);
    parser.DefineVar("xmid", &runtime.xmid);
    parser.DefineVar("ymid", &runtime.ymid);
    parser.DefineVar("zmid", &runtime.zmid);
    parser.DefineVar("xdim", &runtime.xdim);
    parser.DefineVar("ydim", &runtime.ydim);
    parser.DefineVar("zdim", &runtime.zdim);
    parser.DefineVar("bbdiag", &runtime.bbdiag);
}

void bindVertexCustomAttributes(mu::Parser &parser, ParserRuntime &runtime, VCGMesh &mesh)
{
    runtime.vScalarHandles.clear();
    runtime.vScalarValues.clear();
    runtime.vPointHandles.clear();
    runtime.vPointValues.clear();

    std::vector<std::string> names;
    vcg::tri::Allocator<VCGMesh>::GetAllPerVertexAttribute<float>(mesh, names);
    runtime.vScalarHandles.reserve(names.size());
    runtime.vScalarValues.reserve(names.size());
    for (const std::string &name : names) {
        if (!isValidIdentifier(name) || reservedVariables().count(name) > 0)
            continue;
        const auto handle = vcg::tri::Allocator<VCGMesh>::GetPerVertexAttribute<float>(mesh, name);
        if (!vcg::tri::Allocator<VCGMesh>::IsValidHandle(mesh, handle))
            continue;
        runtime.vScalarHandles.push_back(handle);
        runtime.vScalarValues.push_back(0.0);
        parser.DefineVar(name, &runtime.vScalarValues.back());
    }

    names.clear();
    vcg::tri::Allocator<VCGMesh>::GetAllPerVertexAttribute<vcg::Point3f>(mesh, names);
    runtime.vPointHandles.reserve(names.size());
    runtime.vPointValues.reserve(names.size() * 3);
    for (const std::string &name : names) {
        if (!isValidIdentifier(name))
            continue;
        const std::array<std::string, 3> components = {
            name + "_x",
            name + "_y",
            name + "_z"
        };
        if (reservedVariables().count(components[0]) > 0
            || reservedVariables().count(components[1]) > 0
            || reservedVariables().count(components[2]) > 0) {
            continue;
        }
        const auto handle =
            vcg::tri::Allocator<VCGMesh>::GetPerVertexAttribute<vcg::Point3f>(mesh, name);
        if (!vcg::tri::Allocator<VCGMesh>::IsValidHandle(mesh, handle))
            continue;
        runtime.vPointHandles.push_back(handle);
        for (int c = 0; c < 3; ++c) {
            runtime.vPointValues.push_back(0.0);
            parser.DefineVar(components[size_t(c)], &runtime.vPointValues.back());
        }
    }
}

void bindFaceCustomAttributes(mu::Parser &parser, ParserRuntime &runtime, VCGMesh &mesh)
{
    runtime.fScalarHandles.clear();
    runtime.fScalarValues.clear();
    runtime.fPointHandles.clear();
    runtime.fPointValues.clear();

    std::vector<std::string> names;
    vcg::tri::Allocator<VCGMesh>::GetAllPerFaceAttribute<float>(mesh, names);
    runtime.fScalarHandles.reserve(names.size());
    runtime.fScalarValues.reserve(names.size());
    for (const std::string &name : names) {
        if (!isValidIdentifier(name) || reservedVariables().count(name) > 0)
            continue;
        const auto handle = vcg::tri::Allocator<VCGMesh>::GetPerFaceAttribute<float>(mesh, name);
        if (!vcg::tri::Allocator<VCGMesh>::IsValidHandle(mesh, handle))
            continue;
        runtime.fScalarHandles.push_back(handle);
        runtime.fScalarValues.push_back(0.0);
        parser.DefineVar(name, &runtime.fScalarValues.back());
    }

    names.clear();
    vcg::tri::Allocator<VCGMesh>::GetAllPerFaceAttribute<vcg::Point3f>(mesh, names);
    runtime.fPointHandles.reserve(names.size());
    runtime.fPointValues.reserve(names.size() * 3);
    for (const std::string &name : names) {
        if (!isValidIdentifier(name))
            continue;
        const std::array<std::string, 3> components = {
            name + "_x",
            name + "_y",
            name + "_z"
        };
        if (reservedVariables().count(components[0]) > 0
            || reservedVariables().count(components[1]) > 0
            || reservedVariables().count(components[2]) > 0) {
            continue;
        }
        const auto handle = vcg::tri::Allocator<VCGMesh>::GetPerFaceAttribute<vcg::Point3f>(mesh, name);
        if (!vcg::tri::Allocator<VCGMesh>::IsValidHandle(mesh, handle))
            continue;
        runtime.fPointHandles.push_back(handle);
        for (int c = 0; c < 3; ++c) {
            runtime.fPointValues.push_back(0.0);
            parser.DefineVar(components[size_t(c)], &runtime.fPointValues.back());
        }
    }
}

void setPerVertexVariables(mu::Parser &parser, ParserRuntime &runtime, VCGMesh &mesh)
{
    parser.DefineVar("x", &runtime.x);
    parser.DefineVar("y", &runtime.y);
    parser.DefineVar("z", &runtime.z);
    parser.DefineVar("nx", &runtime.nx);
    parser.DefineVar("ny", &runtime.ny);
    parser.DefineVar("nz", &runtime.nz);
    parser.DefineVar("r", &runtime.r);
    parser.DefineVar("g", &runtime.g);
    parser.DefineVar("b", &runtime.b);
    parser.DefineVar("a", &runtime.a);
    parser.DefineVar("q", &runtime.q);
    parser.DefineVar("vi", &runtime.vi);
    parser.DefineVar("vtu", &runtime.vtu);
    parser.DefineVar("vtv", &runtime.vtv);
    parser.DefineVar("ti", &runtime.ti);
    parser.DefineVar("vsel", &runtime.vsel);
    defineCommonBBoxVars(parser, runtime);
    qmeshlab::filters::defineParserCustomFunctions(parser);
    bindVertexCustomAttributes(parser, runtime, mesh);
}

void setPerFaceVariables(mu::Parser &parser, ParserRuntime &runtime, VCGMesh &mesh)
{
    parser.DefineVar("x0", &runtime.x0);
    parser.DefineVar("y0", &runtime.y0);
    parser.DefineVar("z0", &runtime.z0);
    parser.DefineVar("x1", &runtime.x1);
    parser.DefineVar("y1", &runtime.y1);
    parser.DefineVar("z1", &runtime.z1);
    parser.DefineVar("x2", &runtime.x2);
    parser.DefineVar("y2", &runtime.y2);
    parser.DefineVar("z2", &runtime.z2);
    parser.DefineVar("nx0", &runtime.nx0);
    parser.DefineVar("ny0", &runtime.ny0);
    parser.DefineVar("nz0", &runtime.nz0);
    parser.DefineVar("nx1", &runtime.nx1);
    parser.DefineVar("ny1", &runtime.ny1);
    parser.DefineVar("nz1", &runtime.nz1);
    parser.DefineVar("nx2", &runtime.nx2);
    parser.DefineVar("ny2", &runtime.ny2);
    parser.DefineVar("nz2", &runtime.nz2);
    parser.DefineVar("r0", &runtime.r0);
    parser.DefineVar("g0", &runtime.g0);
    parser.DefineVar("b0", &runtime.b0);
    parser.DefineVar("a0", &runtime.a0);
    parser.DefineVar("r1", &runtime.r1);
    parser.DefineVar("g1", &runtime.g1);
    parser.DefineVar("b1", &runtime.b1);
    parser.DefineVar("a1", &runtime.a1);
    parser.DefineVar("r2", &runtime.r2);
    parser.DefineVar("g2", &runtime.g2);
    parser.DefineVar("b2", &runtime.b2);
    parser.DefineVar("a2", &runtime.a2);
    parser.DefineVar("q0", &runtime.q0);
    parser.DefineVar("q1", &runtime.q1);
    parser.DefineVar("q2", &runtime.q2);
    parser.DefineVar("fr", &runtime.fr);
    parser.DefineVar("fg", &runtime.fg);
    parser.DefineVar("fb", &runtime.fb);
    parser.DefineVar("fa", &runtime.fa);
    parser.DefineVar("fnx", &runtime.fnx);
    parser.DefineVar("fny", &runtime.fny);
    parser.DefineVar("fnz", &runtime.fnz);
    parser.DefineVar("fq", &runtime.fq);
    parser.DefineVar("fi", &runtime.fi);
    parser.DefineVar("vi0", &runtime.vi0);
    parser.DefineVar("vi1", &runtime.vi1);
    parser.DefineVar("vi2", &runtime.vi2);
    parser.DefineVar("wtu0", &runtime.wtu0);
    parser.DefineVar("wtv0", &runtime.wtv0);
    parser.DefineVar("wtu1", &runtime.wtu1);
    parser.DefineVar("wtv1", &runtime.wtv1);
    parser.DefineVar("wtu2", &runtime.wtu2);
    parser.DefineVar("wtv2", &runtime.wtv2);
    parser.DefineVar("fsel", &runtime.fsel);
    parser.DefineVar("vsel0", &runtime.vsel0);
    parser.DefineVar("vsel1", &runtime.vsel1);
    parser.DefineVar("vsel2", &runtime.vsel2);
    parser.DefineVar("ti", &runtime.ti);
    defineCommonBBoxVars(parser, runtime);
    qmeshlab::filters::defineParserCustomFunctions(parser);
    bindFaceCustomAttributes(parser, runtime, mesh);
}

void setVertexRuntime(
    ParserRuntime &runtime,
    VCGMesh::VertexIterator vi,
    VCGMesh &mesh)
{
    runtime.x = vi->P()[0];
    runtime.y = vi->P()[1];
    runtime.z = vi->P()[2];
    runtime.nx = vi->N()[0];
    runtime.ny = vi->N()[1];
    runtime.nz = vi->N()[2];
    runtime.r = vi->C()[0];
    runtime.g = vi->C()[1];
    runtime.b = vi->C()[2];
    runtime.a = vi->C()[3];
    runtime.q = vi->Q();
    runtime.vi = double(vi - mesh.vert.begin());
    runtime.vsel = vi->IsS() ? 1.0 : 0.0;

    if (vcg::tri::HasPerVertexTexCoord(mesh)) {
        runtime.vtu = vi->T().U();
        runtime.vtv = vi->T().V();
        runtime.ti = vi->T().N();
    } else {
        runtime.vtu = runtime.vtv = runtime.ti = 0.0;
    }

    for (size_t i = 0; i < runtime.vScalarHandles.size(); ++i)
        runtime.vScalarValues[i] = runtime.vScalarHandles[i][vi];
    for (size_t i = 0; i < runtime.vPointHandles.size(); ++i) {
        const vcg::Point3f p = runtime.vPointHandles[i][vi];
        runtime.vPointValues[i * 3 + 0] = p.X();
        runtime.vPointValues[i * 3 + 1] = p.Y();
        runtime.vPointValues[i * 3 + 2] = p.Z();
    }
}

void setFaceRuntime(
    ParserRuntime &runtime,
    VCGMesh::FaceIterator fi,
    VCGMesh &mesh)
{
    runtime.x0 = fi->V(0)->P()[0];
    runtime.y0 = fi->V(0)->P()[1];
    runtime.z0 = fi->V(0)->P()[2];
    runtime.x1 = fi->V(1)->P()[0];
    runtime.y1 = fi->V(1)->P()[1];
    runtime.z1 = fi->V(1)->P()[2];
    runtime.x2 = fi->V(2)->P()[0];
    runtime.y2 = fi->V(2)->P()[1];
    runtime.z2 = fi->V(2)->P()[2];

    runtime.nx0 = fi->V(0)->N()[0];
    runtime.ny0 = fi->V(0)->N()[1];
    runtime.nz0 = fi->V(0)->N()[2];
    runtime.nx1 = fi->V(1)->N()[0];
    runtime.ny1 = fi->V(1)->N()[1];
    runtime.nz1 = fi->V(1)->N()[2];
    runtime.nx2 = fi->V(2)->N()[0];
    runtime.ny2 = fi->V(2)->N()[1];
    runtime.nz2 = fi->V(2)->N()[2];

    runtime.r0 = fi->V(0)->C()[0];
    runtime.g0 = fi->V(0)->C()[1];
    runtime.b0 = fi->V(0)->C()[2];
    runtime.a0 = fi->V(0)->C()[3];
    runtime.r1 = fi->V(1)->C()[0];
    runtime.g1 = fi->V(1)->C()[1];
    runtime.b1 = fi->V(1)->C()[2];
    runtime.a1 = fi->V(1)->C()[3];
    runtime.r2 = fi->V(2)->C()[0];
    runtime.g2 = fi->V(2)->C()[1];
    runtime.b2 = fi->V(2)->C()[2];
    runtime.a2 = fi->V(2)->C()[3];

    runtime.q0 = fi->V(0)->Q();
    runtime.q1 = fi->V(1)->Q();
    runtime.q2 = fi->V(2)->Q();

    runtime.fq = vcg::tri::HasPerFaceQuality(mesh) ? fi->Q() : 0.0;
    if (vcg::tri::HasPerFaceColor(mesh)) {
        runtime.fr = fi->C()[0];
        runtime.fg = fi->C()[1];
        runtime.fb = fi->C()[2];
        runtime.fa = fi->C()[3];
    } else {
        runtime.fr = runtime.fg = runtime.fb = runtime.fa = 255.0;
    }
    runtime.fnx = fi->N()[0];
    runtime.fny = fi->N()[1];
    runtime.fnz = fi->N()[2];

    runtime.fi = double(fi - mesh.face.begin());
    runtime.vi0 = double(fi->V(0) - &mesh.vert[0]);
    runtime.vi1 = double(fi->V(1) - &mesh.vert[0]);
    runtime.vi2 = double(fi->V(2) - &mesh.vert[0]);

    if (vcg::tri::HasPerWedgeTexCoord(mesh)) {
        runtime.wtu0 = fi->WT(0).U();
        runtime.wtv0 = fi->WT(0).V();
        runtime.wtu1 = fi->WT(1).U();
        runtime.wtv1 = fi->WT(1).V();
        runtime.wtu2 = fi->WT(2).U();
        runtime.wtv2 = fi->WT(2).V();
        runtime.ti = fi->WT(0).N();
    } else {
        runtime.wtu0 = runtime.wtv0 = runtime.wtu1 = runtime.wtv1 = runtime.wtu2 = runtime.wtv2 = 0.0;
        runtime.ti = 0.0;
    }

    runtime.fsel = fi->IsS() ? 1.0 : 0.0;
    runtime.vsel0 = fi->V(0)->IsS() ? 1.0 : 0.0;
    runtime.vsel1 = fi->V(1)->IsS() ? 1.0 : 0.0;
    runtime.vsel2 = fi->V(2)->IsS() ? 1.0 : 0.0;

    for (size_t i = 0; i < runtime.fScalarHandles.size(); ++i)
        runtime.fScalarValues[i] = runtime.fScalarHandles[i][fi];
    for (size_t i = 0; i < runtime.fPointHandles.size(); ++i) {
        const vcg::Point3f p = runtime.fPointHandles[i][fi];
        runtime.fPointValues[i * 3 + 0] = p.X();
        runtime.fPointValues[i * 3 + 1] = p.Y();
        runtime.fPointValues[i * 3 + 2] = p.Z();
    }
}

bool ensureVertexSelectionReady(VCGMesh &mesh, QString &error)
{
    const size_t selectedVertices = vcg::tri::UpdateSelection<VCGMesh>::VertexCount(mesh);
    const size_t selectedFaces = vcg::tri::UpdateSelection<VCGMesh>::FaceCount(mesh);
    if (selectedVertices == 0 && selectedFaces == 0) {
        error = QObject::tr("Cannot apply only on selection: there is no selection.");
        return false;
    }
    if (selectedVertices == 0 && selectedFaces > 0) {
        vcg::tri::UpdateSelection<VCGMesh>::VertexClear(mesh);
        vcg::tri::UpdateSelection<VCGMesh>::VertexFromFaceLoose(mesh);
    }
    return true;
}

bool ensureFaceSelectionReady(VCGMesh &mesh, QString &error)
{
    const size_t selectedFaces = vcg::tri::UpdateSelection<VCGMesh>::FaceCount(mesh);
    if (selectedFaces == 0) {
        error = QObject::tr("Cannot apply only on selection: there is no face selection.");
        return false;
    }
    return true;
}

bool checkCustomAttributeName(const QString &name, QString &error)
{
    const std::string n = name.trimmed().toStdString();
    if (!isValidIdentifier(n)) {
        error = QObject::tr(
            "Invalid attribute name: only letters, numbers and underscores are allowed, and the name must not start with a number.");
        return false;
    }
    if (reservedVariables().count(n) > 0) {
        error = QObject::tr("Attribute name '%1' is reserved by filter variables.").arg(name);
        return false;
    }
    return true;
}

std::vector<MeshFilterDescriptor> buildDescriptors(const Document &)
{
    std::vector<MeshFilterDescriptor> out;
    auto add = [&](MeshFilterDescriptor d) { out.push_back(std::move(d)); };

    auto addOnSelected = [](MeshFilterDescriptor &d, const QString &label) {
        MeshFilterParameterDescriptor p;
        p.id = QStringLiteral("onselected");
        p.label = label;
        p.helpMarkdown = QObject::tr("If enabled, the filter affects only selected elements.");
        p.group = QStringLiteral("main");
        p.type = MeshFilterParameterType::Bool;
        p.defaultValue = false;
        d.parameters.push_back(std::move(p));
    };

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterVertSelection);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Conditional Vertex Selection");
        d.shortDescription = QObject::tr("Selects vertices for which a boolean expression evaluates true.");
        d.longDescriptionMarkdown =
            QObject::tr("Expression variables include coordinates, normals, color, quality, selection state, "
                        "bounding-box values, and user-defined attributes.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("muparser"), QStringLiteral("vertex") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor p;
        p.id = QStringLiteral("condSelect");
        p.label = QObject::tr("Boolean Function");
        p.helpMarkdown = QObject::tr("Boolean expression evaluated per vertex.");
        p.group = QStringLiteral("main");
        p.type = MeshFilterParameterType::String;
        p.defaultValue = QStringLiteral("(q < 0)");
        d.parameters.push_back(std::move(p));
        add(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterFaceSelection);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Conditional Face Selection");
        d.shortDescription = QObject::tr("Selects faces for which a boolean expression evaluates true.");
        d.longDescriptionMarkdown =
            QObject::tr("Expression variables include face and vertex attributes, indices, selection state, "
                        "bounding-box values, and user-defined attributes.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("muparser"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor p;
        p.id = QStringLiteral("condSelect");
        p.label = QObject::tr("Boolean Function");
        p.helpMarkdown = QObject::tr("Boolean expression evaluated per face.");
        p.group = QStringLiteral("main");
        p.type = MeshFilterParameterType::String;
        p.defaultValue = QStringLiteral("(fi == 0)");
        d.parameters.push_back(std::move(p));
        add(std::move(d));
    }

    auto addXYZParams = [](MeshFilterDescriptor &d, const QString &dx, const QString &dy, const QString &dz) {
        MeshFilterParameterDescriptor px;
        px.id = QStringLiteral("x");
        px.label = QObject::tr("X Function");
        px.helpMarkdown = QObject::tr("Expression for X output.");
        px.group = QStringLiteral("main");
        px.type = MeshFilterParameterType::String;
        px.defaultValue = dx;
        d.parameters.push_back(std::move(px));

        MeshFilterParameterDescriptor py;
        py.id = QStringLiteral("y");
        py.label = QObject::tr("Y Function");
        py.helpMarkdown = QObject::tr("Expression for Y output.");
        py.group = QStringLiteral("main");
        py.type = MeshFilterParameterType::String;
        py.defaultValue = dy;
        d.parameters.push_back(std::move(py));

        MeshFilterParameterDescriptor pz;
        pz.id = QStringLiteral("z");
        pz.label = QObject::tr("Z Function");
        pz.helpMarkdown = QObject::tr("Expression for Z output.");
        pz.group = QStringLiteral("main");
        pz.type = MeshFilterParameterType::String;
        pz.defaultValue = dz;
        d.parameters.push_back(std::move(pz));
    };

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterGeomFunc);
        d.menuPath = QObject::tr("Compute/Geometry");
        d.name = QObject::tr("Per Vertex Geometric Function");
        d.shortDescription = QObject::tr("Computes new per-vertex coordinates from expressions.");
        d.tags = { QStringLiteral("geometry"), QStringLiteral("muparser"), QStringLiteral("vertex") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addXYZParams(d, QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("sin(x+y)"));
        addOnSelected(d, QObject::tr("Only On Selection"));
        add(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterVertNormal);
        d.menuPath = QObject::tr("Compute/Normals");
        d.name = QObject::tr("Per Vertex Normal Function");
        d.shortDescription = QObject::tr("Computes new per-vertex normals from expressions.");
        d.tags = { QStringLiteral("normal"), QStringLiteral("muparser"), QStringLiteral("vertex") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addXYZParams(d, QStringLiteral("-nx"), QStringLiteral("-ny"), QStringLiteral("-nz"));
        addOnSelected(d, QObject::tr("Only On Selection"));
        add(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterFaceNormal);
        d.menuPath = QObject::tr("Compute/Normals");
        d.name = QObject::tr("Per Face Normal Function");
        d.shortDescription = QObject::tr("Computes new per-face normals from expressions.");
        d.tags = { QStringLiteral("normal"), QStringLiteral("muparser"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addXYZParams(d, QStringLiteral("-fnx"), QStringLiteral("-fny"), QStringLiteral("-fnz"));
        addOnSelected(d, QObject::tr("Only On Selection"));
        add(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterVertColor);
        d.menuPath = QObject::tr("Compute/Color");
        d.name = QObject::tr("Per Vertex Color Function");
        d.shortDescription = QObject::tr("Computes per-vertex RGBA colors from expressions.");
        d.tags = { QStringLiteral("color"), QStringLiteral("muparser"), QStringLiteral("vertex") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addXYZParams(d, QStringLiteral("255"), QStringLiteral("255"), QStringLiteral("0"));
        MeshFilterParameterDescriptor pa;
        pa.id = QStringLiteral("a");
        pa.label = QObject::tr("Alpha Function");
        pa.helpMarkdown = QObject::tr("Expression for alpha output in range [0, 255].");
        pa.group = QStringLiteral("main");
        pa.type = MeshFilterParameterType::String;
        pa.defaultValue = QStringLiteral("255");
        d.parameters.push_back(std::move(pa));
        addOnSelected(d, QObject::tr("Only On Selection"));
        add(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterFaceColor);
        d.menuPath = QObject::tr("Compute/Color");
        d.name = QObject::tr("Per Face Color Function");
        d.shortDescription = QObject::tr("Computes per-face RGBA colors from expressions.");
        d.tags = { QStringLiteral("color"), QStringLiteral("muparser"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor pr;
        pr.id = QStringLiteral("r");
        pr.label = QObject::tr("Red Function");
        pr.helpMarkdown = QObject::tr("Expression for red output in range [0, 255].");
        pr.group = QStringLiteral("main");
        pr.type = MeshFilterParameterType::String;
        pr.defaultValue = QStringLiteral("255");
        d.parameters.push_back(std::move(pr));
        MeshFilterParameterDescriptor pg;
        pg.id = QStringLiteral("g");
        pg.label = QObject::tr("Green Function");
        pg.helpMarkdown = QObject::tr("Expression for green output in range [0, 255].");
        pg.group = QStringLiteral("main");
        pg.type = MeshFilterParameterType::String;
        pg.defaultValue = QStringLiteral("0");
        d.parameters.push_back(std::move(pg));
        MeshFilterParameterDescriptor pb;
        pb.id = QStringLiteral("b");
        pb.label = QObject::tr("Blue Function");
        pb.helpMarkdown = QObject::tr("Expression for blue output in range [0, 255].");
        pb.group = QStringLiteral("main");
        pb.type = MeshFilterParameterType::String;
        pb.defaultValue = QStringLiteral("255");
        d.parameters.push_back(std::move(pb));
        MeshFilterParameterDescriptor pa;
        pa.id = QStringLiteral("a");
        pa.label = QObject::tr("Alpha Function");
        pa.helpMarkdown = QObject::tr("Expression for alpha output in range [0, 255].");
        pa.group = QStringLiteral("main");
        pa.type = MeshFilterParameterType::String;
        pa.defaultValue = QStringLiteral("255");
        d.parameters.push_back(std::move(pa));
        addOnSelected(d, QObject::tr("Only On Selection"));
        add(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterVertQuality);
        d.menuPath = QObject::tr("Compute/Quality");
        d.name = QObject::tr("Per Vertex Quality Function");
        d.shortDescription = QObject::tr("Computes per-vertex scalar quality from an expression.");
        d.tags = { QStringLiteral("quality"), QStringLiteral("muparser"), QStringLiteral("vertex") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor pq;
        pq.id = QStringLiteral("q");
        pq.label = QObject::tr("Quality Function");
        pq.helpMarkdown = QObject::tr("Expression for quality output.");
        pq.group = QStringLiteral("main");
        pq.type = MeshFilterParameterType::String;
        pq.defaultValue = QStringLiteral("vi");
        d.parameters.push_back(std::move(pq));

        MeshFilterParameterDescriptor pnorm;
        pnorm.id = QStringLiteral("normalize");
        pnorm.label = QObject::tr("Normalize");
        pnorm.helpMarkdown = QObject::tr("Normalize computed quality into range [0, 1].");
        pnorm.group = QStringLiteral("main");
        pnorm.type = MeshFilterParameterType::Bool;
        pnorm.defaultValue = false;
        d.parameters.push_back(std::move(pnorm));

        MeshFilterParameterDescriptor pmap;
        pmap.id = QStringLiteral("map");
        pmap.label = QObject::tr("Map To Color");
        pmap.helpMarkdown = QObject::tr("Maps computed quality into per-vertex color.");
        pmap.group = QStringLiteral("main");
        pmap.type = MeshFilterParameterType::Bool;
        pmap.defaultValue = false;
        d.parameters.push_back(std::move(pmap));

        addOnSelected(d, QObject::tr("Only On Selection"));
        add(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterFaceQuality);
        d.menuPath = QObject::tr("Compute/Quality");
        d.name = QObject::tr("Per Face Quality Function");
        d.shortDescription = QObject::tr("Computes per-face scalar quality from an expression.");
        d.tags = { QStringLiteral("quality"), QStringLiteral("muparser"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor pq;
        pq.id = QStringLiteral("q");
        pq.label = QObject::tr("Quality Function");
        pq.helpMarkdown = QObject::tr("Expression for quality output.");
        pq.group = QStringLiteral("main");
        pq.type = MeshFilterParameterType::String;
        pq.defaultValue = QStringLiteral("x0+y0+z0");
        d.parameters.push_back(std::move(pq));

        MeshFilterParameterDescriptor pnorm;
        pnorm.id = QStringLiteral("normalize");
        pnorm.label = QObject::tr("Normalize");
        pnorm.helpMarkdown = QObject::tr("Normalize computed quality into range [0, 1].");
        pnorm.group = QStringLiteral("main");
        pnorm.type = MeshFilterParameterType::Bool;
        pnorm.defaultValue = false;
        d.parameters.push_back(std::move(pnorm));

        MeshFilterParameterDescriptor pmap;
        pmap.id = QStringLiteral("map");
        pmap.label = QObject::tr("Map To Color");
        pmap.helpMarkdown = QObject::tr("Maps computed quality into per-face color.");
        pmap.group = QStringLiteral("main");
        pmap.type = MeshFilterParameterType::Bool;
        pmap.defaultValue = false;
        d.parameters.push_back(std::move(pmap));

        addOnSelected(d, QObject::tr("Only On Selection"));
        add(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterVertTex);
        d.menuPath = QObject::tr("Compute/Texture");
        d.name = QObject::tr("Per Vertex Texture Function");
        d.shortDescription = QObject::tr("Computes per-vertex texture coordinates from expressions.");
        d.tags = { QStringLiteral("texture"), QStringLiteral("muparser"), QStringLiteral("vertex") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor pu;
        pu.id = QStringLiteral("u");
        pu.label = QObject::tr("U Function");
        pu.helpMarkdown = QObject::tr("Expression for U texture coordinate.");
        pu.group = QStringLiteral("main");
        pu.type = MeshFilterParameterType::String;
        pu.defaultValue = QStringLiteral("x");
        d.parameters.push_back(std::move(pu));
        MeshFilterParameterDescriptor pv;
        pv.id = QStringLiteral("v");
        pv.label = QObject::tr("V Function");
        pv.helpMarkdown = QObject::tr("Expression for V texture coordinate.");
        pv.group = QStringLiteral("main");
        pv.type = MeshFilterParameterType::String;
        pv.defaultValue = QStringLiteral("y");
        d.parameters.push_back(std::move(pv));
        addOnSelected(d, QObject::tr("Only On Selection"));
        add(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterWedgeTex);
        d.menuPath = QObject::tr("Compute/Texture");
        d.name = QObject::tr("Per Wedge Texture Function");
        d.shortDescription = QObject::tr("Computes per-wedge texture coordinates from expressions.");
        d.tags = { QStringLiteral("texture"), QStringLiteral("muparser"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        auto addFunc = [&](const char *id, const QString &label, const QString &defValue) {
            MeshFilterParameterDescriptor p;
            p.id = QString::fromLatin1(id);
            p.label = label;
            p.helpMarkdown = QObject::tr("Expression for wedge texture coordinate.");
            p.group = QStringLiteral("main");
            p.type = MeshFilterParameterType::String;
            p.defaultValue = defValue;
            d.parameters.push_back(std::move(p));
        };
        addFunc("u0", QObject::tr("U0 Function"), QStringLiteral("x0"));
        addFunc("v0", QObject::tr("V0 Function"), QStringLiteral("y0"));
        addFunc("u1", QObject::tr("U1 Function"), QStringLiteral("x1"));
        addFunc("v1", QObject::tr("V1 Function"), QStringLiteral("y1"));
        addFunc("u2", QObject::tr("U2 Function"), QStringLiteral("x2"));
        addFunc("v2", QObject::tr("V2 Function"), QStringLiteral("y2"));
        addOnSelected(d, QObject::tr("Only On Selection"));
        add(std::move(d));
    }

    auto addCustomAttributeParams = [](MeshFilterDescriptor &d, bool point, bool face) {
        MeshFilterParameterDescriptor pname;
        pname.id = QStringLiteral("name");
        pname.label = QObject::tr("Attribute Name");
        pname.helpMarkdown = QObject::tr("Name of the new custom attribute.");
        pname.group = QStringLiteral("main");
        pname.type = MeshFilterParameterType::String;
        pname.defaultValue = QStringLiteral("CustomAttrName");
        d.parameters.push_back(std::move(pname));

        if (!point) {
            MeshFilterParameterDescriptor pexpr;
            pexpr.id = QStringLiteral("expr");
            pexpr.label = QObject::tr("Scalar Function");
            pexpr.helpMarkdown = QObject::tr("Expression used to compute the scalar attribute.");
            pexpr.group = QStringLiteral("main");
            pexpr.type = MeshFilterParameterType::String;
            pexpr.defaultValue = face ? QStringLiteral("fi") : QStringLiteral("x");
            d.parameters.push_back(std::move(pexpr));
            return;
        }

        MeshFilterParameterDescriptor px;
        px.id = QStringLiteral("x_expr");
        px.label = QObject::tr("X Function");
        px.helpMarkdown = QObject::tr("Expression for X component.");
        px.group = QStringLiteral("main");
        px.type = MeshFilterParameterType::String;
        px.defaultValue = face ? QStringLiteral("x0") : QStringLiteral("x");
        d.parameters.push_back(std::move(px));

        MeshFilterParameterDescriptor py;
        py.id = QStringLiteral("y_expr");
        py.label = QObject::tr("Y Function");
        py.helpMarkdown = QObject::tr("Expression for Y component.");
        py.group = QStringLiteral("main");
        py.type = MeshFilterParameterType::String;
        py.defaultValue = face ? QStringLiteral("y0") : QStringLiteral("y");
        d.parameters.push_back(std::move(py));

        MeshFilterParameterDescriptor pz;
        pz.id = QStringLiteral("z_expr");
        pz.label = QObject::tr("Z Function");
        pz.helpMarkdown = QObject::tr("Expression for Z component.");
        pz.group = QStringLiteral("main");
        pz.type = MeshFilterParameterType::String;
        pz.defaultValue = face ? QStringLiteral("z0") : QStringLiteral("z");
        d.parameters.push_back(std::move(pz));
    };

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterDefVertScalar);
        d.menuPath = QObject::tr("Compute/Attributes");
        d.name = QObject::tr("Define New Per Vertex Custom Scalar Attribute");
        d.shortDescription = QObject::tr("Defines and fills a custom per-vertex scalar attribute.");
        d.tags = { QStringLiteral("attribute"), QStringLiteral("vertex"), QStringLiteral("muparser") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addCustomAttributeParams(d, false, false);
        add(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterDefFaceScalar);
        d.menuPath = QObject::tr("Compute/Attributes");
        d.name = QObject::tr("Define New Per Face Custom Scalar Attribute");
        d.shortDescription = QObject::tr("Defines and fills a custom per-face scalar attribute.");
        d.tags = { QStringLiteral("attribute"), QStringLiteral("face"), QStringLiteral("muparser") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addCustomAttributeParams(d, false, true);
        add(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterDefVertPoint);
        d.menuPath = QObject::tr("Compute/Attributes");
        d.name = QObject::tr("Define New Per Vertex Custom Point Attribute");
        d.shortDescription = QObject::tr("Defines and fills a custom per-vertex point attribute.");
        d.tags = { QStringLiteral("attribute"), QStringLiteral("vertex"), QStringLiteral("muparser") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addCustomAttributeParams(d, true, false);
        add(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterDefFacePoint);
        d.menuPath = QObject::tr("Compute/Attributes");
        d.name = QObject::tr("Define New Per Face Custom Point Attribute");
        d.shortDescription = QObject::tr("Defines and fills a custom per-face point attribute.");
        d.tags = { QStringLiteral("attribute"), QStringLiteral("face"), QStringLiteral("muparser") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addCustomAttributeParams(d, true, true);
        add(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterGrid);
        d.menuPath = QObject::tr("Create");
        d.name = QObject::tr("Grid Generator");
        d.shortDescription = QObject::tr("Generates a regular 2D grid mesh.");
        d.tags = { QStringLiteral("create"), QStringLiteral("grid"), QStringLiteral("mesh") };
        d.inputDomain = MeshFilterInputDomain::None;
        d.outputDomain = MeshFilterOutputDomain::NewMeshes;

        MeshFilterParameterDescriptor px;
        px.id = QStringLiteral("numVertX");
        px.label = QObject::tr("Vertices X");
        px.helpMarkdown = QObject::tr("Number of vertices along X.");
        px.group = QStringLiteral("main");
        px.type = MeshFilterParameterType::Int;
        px.defaultValue = 10;
        px.minValue = 2;
        px.maxValue = 100000;
        d.parameters.push_back(std::move(px));

        MeshFilterParameterDescriptor py;
        py.id = QStringLiteral("numVertY");
        py.label = QObject::tr("Vertices Y");
        py.helpMarkdown = QObject::tr("Number of vertices along Y.");
        py.group = QStringLiteral("main");
        py.type = MeshFilterParameterType::Int;
        py.defaultValue = 10;
        py.minValue = 2;
        py.maxValue = 100000;
        d.parameters.push_back(std::move(py));

        MeshFilterParameterDescriptor sx;
        sx.id = QStringLiteral("absScaleX");
        sx.label = QObject::tr("Scale X");
        sx.helpMarkdown = QObject::tr("Absolute scale along X.");
        sx.group = QStringLiteral("main");
        sx.type = MeshFilterParameterType::Double;
        sx.defaultValue = 0.3;
        sx.minValue = 1e-9;
        sx.maxValue = 1e9;
        sx.decimals = 6;
        d.parameters.push_back(std::move(sx));

        MeshFilterParameterDescriptor sy;
        sy.id = QStringLiteral("absScaleY");
        sy.label = QObject::tr("Scale Y");
        sy.helpMarkdown = QObject::tr("Absolute scale along Y.");
        sy.group = QStringLiteral("main");
        sy.type = MeshFilterParameterType::Double;
        sy.defaultValue = 0.3;
        sy.minValue = 1e-9;
        sy.maxValue = 1e9;
        sy.decimals = 6;
        d.parameters.push_back(std::move(sy));

        MeshFilterParameterDescriptor center;
        center.id = QStringLiteral("center");
        center.label = QObject::tr("Center On Origin");
        center.helpMarkdown = QObject::tr("Centers the generated grid on origin.");
        center.group = QStringLiteral("main");
        center.type = MeshFilterParameterType::Bool;
        center.defaultValue = false;
        d.parameters.push_back(std::move(center));
        add(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterIso);
        d.menuPath = QObject::tr("Create");
        d.name = QObject::tr("Implicit Surface");
        d.shortDescription = QObject::tr("Extracts an isosurface from an implicit scalar field.");
        d.tags = { QStringLiteral("create"), QStringLiteral("isosurface"), QStringLiteral("marching cubes") };
        d.inputDomain = MeshFilterInputDomain::None;
        d.outputDomain = MeshFilterOutputDomain::NewMeshes;

        auto addRange = [&](const char *id, const QString &label, double defVal) {
            MeshFilterParameterDescriptor p;
            p.id = QString::fromLatin1(id);
            p.label = label;
            p.helpMarkdown = QObject::tr("Sampling range parameter.");
            p.group = QStringLiteral("main");
            p.type = MeshFilterParameterType::Double;
            p.defaultValue = defVal;
            p.minValue = -1e9;
            p.maxValue = 1e9;
            p.decimals = 6;
            d.parameters.push_back(std::move(p));
        };

        MeshFilterParameterDescriptor voxel;
        voxel.id = QStringLiteral("voxelSize");
        voxel.label = QObject::tr("Voxel Size");
        voxel.helpMarkdown = QObject::tr("Sampling step used for volumetric evaluation.");
        voxel.group = QStringLiteral("main");
        voxel.type = MeshFilterParameterType::Double;
        voxel.defaultValue = 0.05;
        voxel.minValue = 1e-6;
        voxel.maxValue = 1e3;
        voxel.decimals = 6;
        d.parameters.push_back(std::move(voxel));

        addRange("minX", QObject::tr("Min X"), -1.0);
        addRange("minY", QObject::tr("Min Y"), -1.0);
        addRange("minZ", QObject::tr("Min Z"), -1.0);
        addRange("maxX", QObject::tr("Max X"), 1.0);
        addRange("maxY", QObject::tr("Max Y"), 1.0);
        addRange("maxZ", QObject::tr("Max Z"), 1.0);

        MeshFilterParameterDescriptor expr;
        expr.id = QStringLiteral("expr");
        expr.label = QObject::tr("Field Function");
        expr.helpMarkdown = QObject::tr("Scalar field expression f(x,y,z). The 0-isovalue is extracted.");
        expr.group = QStringLiteral("main");
        expr.type = MeshFilterParameterType::String;
        expr.defaultValue = QStringLiteral("x*x+y*y+z*z-0.5");
        d.parameters.push_back(std::move(expr));
        add(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterRefine);
        d.menuPath = QObject::tr("Remeshing");
        d.name = QObject::tr("Refine User-Defined");
        d.shortDescription = QObject::tr("Refines edges selected by an expression and places split points by expressions.");
        d.tags = { QStringLiteral("refine"), QStringLiteral("remeshing"), QStringLiteral("muparser") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor cond;
        cond.id = QStringLiteral("condSelect");
        cond.label = QObject::tr("Edge Predicate");
        cond.helpMarkdown = QObject::tr("Boolean expression used to decide whether an edge is refined.");
        cond.group = QStringLiteral("main");
        cond.type = MeshFilterParameterType::String;
        cond.defaultValue = QStringLiteral("(q0 >= 0 && q1 >= 0)");
        d.parameters.push_back(std::move(cond));
        addXYZParams(d, QStringLiteral("(x0+x1)/2"), QStringLiteral("(y0+y1)/2"), QStringLiteral("(z0+z1)/2"));
        add(std::move(d));
    }

    return out;
}
}

QString FuncFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.func");
}

QString FuncFilterPlugin::name() const
{
    return QObject::tr("QMeshLab Function Filters");
}

std::vector<MeshFilterDescriptor> FuncFilterPlugin::filters(const Document &doc) const
{
    return buildDescriptors(doc);
}

MeshFilterRunResult FuncFilterPlugin::runFilter(
    const QString &filterId,
    const MeshFilterParameterValues &parameters,
    Document &doc) const
{
    using Mask = vcg::tri::io::Mask;

    auto fail = [](const QString &msg) {
        MeshFilterRunResult r;
        r.success = false;
        r.errorMessage = msg;
        return r;
    };

    if (filterId == QString::fromLatin1(kFilterGrid)) {
        const int w = intParameter(parameters, QStringLiteral("numVertX"), 10);
        const int h = intParameter(parameters, QStringLiteral("numVertY"), 10);
        const double sx = doubleParameter(parameters, QStringLiteral("absScaleX"), 0.3);
        const double sy = doubleParameter(parameters, QStringLiteral("absScaleY"), 0.3);
        const bool center = boolParameter(parameters, QStringLiteral("center"), false);
        if (w <= 1 || h <= 1)
            return fail(QObject::tr("Grid vertex counts must be greater than 1."));
        if (!std::isfinite(sx) || !std::isfinite(sy) || sx <= 0.0 || sy <= 0.0)
            return fail(QObject::tr("Grid scale values must be finite and greater than zero."));

        VCGMesh generated;
        vcg::tri::Grid(generated, w, h, float(sx), float(sy));
        if (center) {
            const float halfW = float(w - 1) * 0.5f;
            const float halfH = float(h - 1) * 0.5f;
            const float stepX = float(sx) / float(w);
            const float stepY = float(sy) / float(h);
            for (auto vi = generated.vert.begin(); vi != generated.vert.end(); ++vi) {
                vi->P()[0] -= stepX * halfW;
                vi->P()[1] -= stepY * halfH;
            }
        }
        vcg::Matrix44f flip;
        flip.SetScale(-1.0f, 1.0f, -1.0f);
        vcg::tri::UpdatePosition<VCGMesh>::Matrix(generated, flip, false);
        vcg::tri::UpdateBounding<VCGMesh>::Box(generated);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(generated);

        const int newIndex = doc.addMesh(
            generated,
            QObject::tr("Grid"),
            Mask::IOM_VERTNORMAL | Mask::IOM_FACENORMAL);
        if (newIndex < 0)
            return fail(QObject::tr("Failed to add generated grid mesh."));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.newMeshIndices = { newIndex };
        result.infoMessages = {
            QObject::tr("Generated grid '%1' (%2 vertices, %3 faces).")
                .arg(doc.mesh(newIndex).name)
                .arg(doc.mesh(newIndex).mesh.VN())
                .arg(doc.mesh(newIndex).mesh.FN())
        };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterIso)) {
        const double voxelSize = doubleParameter(parameters, QStringLiteral("voxelSize"), 0.05);
        const double minX = doubleParameter(parameters, QStringLiteral("minX"), -1.0);
        const double minY = doubleParameter(parameters, QStringLiteral("minY"), -1.0);
        const double minZ = doubleParameter(parameters, QStringLiteral("minZ"), -1.0);
        const double maxX = doubleParameter(parameters, QStringLiteral("maxX"), 1.0);
        const double maxY = doubleParameter(parameters, QStringLiteral("maxY"), 1.0);
        const double maxZ = doubleParameter(parameters, QStringLiteral("maxZ"), 1.0);
        const QString expression = stringParameter(parameters, QStringLiteral("expr"), QStringLiteral("x*x+y*y+z*z-0.5"));
        if (!std::isfinite(voxelSize) || voxelSize <= 0.0)
            return fail(QObject::tr("Voxel size must be finite and greater than zero."));
        if (!(minX < maxX && minY < maxY && minZ < maxZ))
            return fail(QObject::tr("Min range values must be lower than max range values."));

        using Scalar = float;
        using VolumeType = vcg::SimpleVolume<vcg::SimpleVoxel<Scalar>>;
        using WalkerType = vcg::tri::TrivialWalker<VCGMesh, VolumeType>;
        using MarchingCubesType = vcg::tri::MarchingCubes<VCGMesh, WalkerType>;

        const vcg::Box3f range{
            vcg::Point3f(float(minX), float(minY), float(minZ)),
            vcg::Point3f(float(maxX), float(maxY), float(maxZ))
        };
        const vcg::Point3f dims = range.max - range.min;
        const vcg::Point3i size = vcg::Point3i::Construct(dims * float(1.0 / voxelSize));
        if (size[0] < 2 || size[1] < 2 || size[2] < 2)
            return fail(QObject::tr("Sampling volume is too small for marching cubes."));

        mu::Parser parser;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        qmeshlab::filters::defineParserCustomFunctions(parser);
        parser.DefineVar("x", &x);
        parser.DefineVar("y", &y);
        parser.DefineVar("z", &z);
        try {
            parser.SetExpr(expression.toStdString());
        } catch (mu::Parser::exception_type &e) {
            return fail(parserErrorString(e));
        }

        VolumeType volume;
        volume.Init(size, range);
        for (int i = 0; i < size[0]; ++i) {
            for (int j = 0; j < size[1]; ++j) {
                for (int k = 0; k < size[2]; ++k) {
                    x = minX + voxelSize * double(i);
                    y = minY + voxelSize * double(j);
                    z = minZ + voxelSize * double(k);
                    try {
                        volume.Val(i, j, k) = float(parser.Eval());
                    } catch (mu::Parser::exception_type &e) {
                        return fail(parserErrorString(e));
                    }
                }
            }
        }

        VCGMesh generated;
        WalkerType walker;
        MarchingCubesType mc(generated, walker);
        const int budgetHint = std::max(16, (size[0] * size[1]) / 10);
        walker.BuildMesh<MarchingCubesType>(generated, volume, mc, budgetHint, nullptr);
        if (generated.VN() <= 0 || generated.FN() <= 0)
            return fail(QObject::tr("Implicit surface extraction produced an empty mesh."));

        vcg::tri::Allocator<VCGMesh>::CompactEveryVector(generated);
        vcg::tri::UpdateBounding<VCGMesh>::Box(generated);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(generated);

        const int newIndex = doc.addMesh(
            generated,
            QObject::tr("Implicit Surface"),
            Mask::IOM_VERTNORMAL | Mask::IOM_FACENORMAL);
        if (newIndex < 0)
            return fail(QObject::tr("Failed to add generated implicit surface mesh."));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.newMeshIndices = { newIndex };
        result.infoMessages = {
            QObject::tr("Generated implicit surface '%1' (%2 vertices, %3 faces).")
                .arg(doc.mesh(newIndex).name)
                .arg(doc.mesh(newIndex).mesh.VN())
                .arg(doc.mesh(newIndex).mesh.FN())
        };
        return result;
    }

    QString meshError;
    const std::optional<CurrentMeshRef> current = currentMesh(doc, meshError);
    if (!current)
        return fail(meshError);

    const int meshIndex = current->index;
    Document::MeshEntry &entry = *current->entry;
    VCGMesh &mesh = entry.mesh;

    vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
    ParserRuntime runtime;
    setBBoxRuntime(runtime, mesh);

    if (filterId == QString::fromLatin1(kFilterVertSelection)) {
        const QString expr = stringParameter(parameters, QStringLiteral("condSelect"), QStringLiteral("(q < 0)"));
        mu::Parser parser;
        setPerVertexVariables(parser, runtime, mesh);
        try {
            parser.SetExpr(expr.toStdString());
        } catch (mu::Parser::exception_type &e) {
            return fail(parserErrorString(e));
        }

        int selectedCount = 0;
        for (auto vi = mesh.vert.begin(); vi != mesh.vert.end(); ++vi) {
            if (vi->IsD())
                continue;
            setVertexRuntime(runtime, vi, mesh);
            bool selected = false;
            try {
                selected = (parser.Eval() != 0.0);
            } catch (mu::Parser::exception_type &e) {
                return fail(parserErrorString(e));
            }
            if (selected) {
                vi->SetS();
                ++selectedCount;
            } else {
                vi->ClearS();
            }
        }

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = { QObject::tr("Selected %1 vertices.").arg(selectedCount) };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterFaceSelection)) {
        const QString expr = stringParameter(parameters, QStringLiteral("condSelect"), QStringLiteral("(fi == 0)"));
        mu::Parser parser;
        setPerFaceVariables(parser, runtime, mesh);
        try {
            parser.SetExpr(expr.toStdString());
        } catch (mu::Parser::exception_type &e) {
            return fail(parserErrorString(e));
        }

        int selectedCount = 0;
        for (auto fi = mesh.face.begin(); fi != mesh.face.end(); ++fi) {
            if (fi->IsD())
                continue;
            setFaceRuntime(runtime, fi, mesh);
            bool selected = false;
            try {
                selected = (parser.Eval() != 0.0);
            } catch (mu::Parser::exception_type &e) {
                return fail(parserErrorString(e));
            }
            if (selected) {
                fi->SetS();
                ++selectedCount;
            } else {
                fi->ClearS();
            }
        }

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = { QObject::tr("Selected %1 faces.").arg(selectedCount) };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterGeomFunc)
        || filterId == QString::fromLatin1(kFilterVertColor)
        || filterId == QString::fromLatin1(kFilterVertNormal)) {
        const QString exprX = stringParameter(parameters, QStringLiteral("x"), QStringLiteral("x"));
        const QString exprY = stringParameter(parameters, QStringLiteral("y"), QStringLiteral("y"));
        const QString exprZ = stringParameter(parameters, QStringLiteral("z"), QStringLiteral("z"));
        const QString exprA = stringParameter(parameters, QStringLiteral("a"), QStringLiteral("255"));
        const bool onSelected = boolParameter(parameters, QStringLiteral("onselected"), false);
        if (onSelected && !ensureVertexSelectionReady(mesh, meshError))
            return fail(meshError);

        mu::Parser px;
        mu::Parser py;
        mu::Parser pz;
        mu::Parser pa;
        setPerVertexVariables(px, runtime, mesh);
        setPerVertexVariables(py, runtime, mesh);
        setPerVertexVariables(pz, runtime, mesh);
        setPerVertexVariables(pa, runtime, mesh);
        try {
            px.SetExpr(exprX.toStdString());
            py.SetExpr(exprY.toStdString());
            pz.SetExpr(exprZ.toStdString());
            pa.SetExpr(exprA.toStdString());
        } catch (mu::Parser::exception_type &e) {
            return fail(parserErrorString(e));
        }

        int processed = 0;
        for (auto vi = mesh.vert.begin(); vi != mesh.vert.end(); ++vi) {
            if (vi->IsD())
                continue;
            if (onSelected && !vi->IsS())
                continue;
            setVertexRuntime(runtime, vi, mesh);
            double outX = 0.0;
            double outY = 0.0;
            double outZ = 0.0;
            double outA = 255.0;
            try {
                outX = px.Eval();
                outY = py.Eval();
                outZ = pz.Eval();
                outA = pa.Eval();
            } catch (mu::Parser::exception_type &e) {
                return fail(parserErrorString(e));
            }

            if (filterId == QString::fromLatin1(kFilterGeomFunc)) {
                vi->P() = vcg::Point3f(float(outX), float(outY), float(outZ));
            } else if (filterId == QString::fromLatin1(kFilterVertNormal)) {
                vi->N() = vcg::Point3f(float(outX), float(outY), float(outZ));
            } else {
                vi->C() = vcg::Color4b(clampToByte(outX), clampToByte(outY), clampToByte(outZ), clampToByte(outA));
            }
            ++processed;
        }

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = { QObject::tr("Processed %1 vertices.").arg(processed) };

        if (filterId == QString::fromLatin1(kFilterGeomFunc)) {
            vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
            entry.ioMask |= Mask::IOM_VERTNORMAL | Mask::IOM_FACENORMAL;
            doc.markMeshGeometryChanged(
                meshIndex,
                QObject::tr("Applied per-vertex geometric function to '%1'.").arg(entry.name));
        } else if (filterId == QString::fromLatin1(kFilterVertNormal)) {
            entry.ioMask |= Mask::IOM_VERTNORMAL;
            doc.markMeshGeometryChanged(
                meshIndex,
                QObject::tr("Applied per-vertex normal function to '%1'.").arg(entry.name));
        } else {
            entry.ioMask |= Mask::IOM_VERTCOLOR;
            doc.markMeshMaterialChanged(
                meshIndex,
                QObject::tr("Applied per-vertex color function to '%1'.").arg(entry.name));
        }
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterVertQuality)) {
        const QString exprQ = stringParameter(parameters, QStringLiteral("q"), QStringLiteral("vi"));
        const bool normalize = boolParameter(parameters, QStringLiteral("normalize"), false);
        const bool mapToColor = boolParameter(parameters, QStringLiteral("map"), false);
        const bool onSelected = boolParameter(parameters, QStringLiteral("onselected"), false);
        if (onSelected && !ensureVertexSelectionReady(mesh, meshError))
            return fail(meshError);

        mu::Parser parser;
        setPerVertexVariables(parser, runtime, mesh);
        try {
            parser.SetExpr(exprQ.toStdString());
        } catch (mu::Parser::exception_type &e) {
            return fail(parserErrorString(e));
        }

        int processed = 0;
        for (auto vi = mesh.vert.begin(); vi != mesh.vert.end(); ++vi) {
            if (vi->IsD())
                continue;
            if (onSelected && !vi->IsS())
                continue;
            setVertexRuntime(runtime, vi, mesh);
            try {
                vi->Q() = float(parser.Eval());
            } catch (mu::Parser::exception_type &e) {
                return fail(parserErrorString(e));
            }
            ++processed;
        }
        if (normalize)
            vcg::tri::UpdateQuality<VCGMesh>::VertexNormalize(mesh);
        if (mapToColor) {
            vcg::tri::UpdateColor<VCGMesh>::PerVertexQualityRamp(mesh);
            entry.ioMask |= Mask::IOM_VERTCOLOR;
        }
        entry.ioMask |= Mask::IOM_VERTQUALITY;
        doc.markMeshMaterialChanged(
            meshIndex,
            QObject::tr("Applied per-vertex quality function to '%1'.").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = { QObject::tr("Processed %1 vertices.").arg(processed) };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterVertTex)) {
        const QString exprU = stringParameter(parameters, QStringLiteral("u"), QStringLiteral("x"));
        const QString exprV = stringParameter(parameters, QStringLiteral("v"), QStringLiteral("y"));
        const bool onSelected = boolParameter(parameters, QStringLiteral("onselected"), false);
        if (onSelected && !ensureVertexSelectionReady(mesh, meshError))
            return fail(meshError);

        mu::Parser parserU;
        mu::Parser parserV;
        setPerVertexVariables(parserU, runtime, mesh);
        setPerVertexVariables(parserV, runtime, mesh);
        try {
            parserU.SetExpr(exprU.toStdString());
            parserV.SetExpr(exprV.toStdString());
        } catch (mu::Parser::exception_type &e) {
            return fail(parserErrorString(e));
        }

        int processed = 0;
        for (auto vi = mesh.vert.begin(); vi != mesh.vert.end(); ++vi) {
            if (vi->IsD())
                continue;
            if (onSelected && !vi->IsS())
                continue;
            setVertexRuntime(runtime, vi, mesh);
            try {
                vi->T().U() = float(parserU.Eval());
                vi->T().V() = float(parserV.Eval());
            } catch (mu::Parser::exception_type &e) {
                return fail(parserErrorString(e));
            }
            ++processed;
        }
        entry.ioMask |= Mask::IOM_VERTTEXCOORD;
        doc.markMeshGeometryChanged(
            meshIndex,
            QObject::tr("Applied per-vertex texture function to '%1'.").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = { QObject::tr("Processed %1 vertices.").arg(processed) };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterWedgeTex)) {
        const QString exprU0 = stringParameter(parameters, QStringLiteral("u0"), QStringLiteral("x0"));
        const QString exprV0 = stringParameter(parameters, QStringLiteral("v0"), QStringLiteral("y0"));
        const QString exprU1 = stringParameter(parameters, QStringLiteral("u1"), QStringLiteral("x1"));
        const QString exprV1 = stringParameter(parameters, QStringLiteral("v1"), QStringLiteral("y1"));
        const QString exprU2 = stringParameter(parameters, QStringLiteral("u2"), QStringLiteral("x2"));
        const QString exprV2 = stringParameter(parameters, QStringLiteral("v2"), QStringLiteral("y2"));
        const bool onSelected = boolParameter(parameters, QStringLiteral("onselected"), false);
        if (onSelected && !ensureFaceSelectionReady(mesh, meshError))
            return fail(meshError);

        mu::Parser pu0;
        mu::Parser pv0;
        mu::Parser pu1;
        mu::Parser pv1;
        mu::Parser pu2;
        mu::Parser pv2;
        setPerFaceVariables(pu0, runtime, mesh);
        setPerFaceVariables(pv0, runtime, mesh);
        setPerFaceVariables(pu1, runtime, mesh);
        setPerFaceVariables(pv1, runtime, mesh);
        setPerFaceVariables(pu2, runtime, mesh);
        setPerFaceVariables(pv2, runtime, mesh);
        try {
            pu0.SetExpr(exprU0.toStdString());
            pv0.SetExpr(exprV0.toStdString());
            pu1.SetExpr(exprU1.toStdString());
            pv1.SetExpr(exprV1.toStdString());
            pu2.SetExpr(exprU2.toStdString());
            pv2.SetExpr(exprV2.toStdString());
        } catch (mu::Parser::exception_type &e) {
            return fail(parserErrorString(e));
        }

        int processed = 0;
        for (auto fi = mesh.face.begin(); fi != mesh.face.end(); ++fi) {
            if (fi->IsD())
                continue;
            if (onSelected && !fi->IsS())
                continue;
            setFaceRuntime(runtime, fi, mesh);
            try {
                fi->WT(0).U() = float(pu0.Eval());
                fi->WT(0).V() = float(pv0.Eval());
                fi->WT(1).U() = float(pu1.Eval());
                fi->WT(1).V() = float(pv1.Eval());
                fi->WT(2).U() = float(pu2.Eval());
                fi->WT(2).V() = float(pv2.Eval());
            } catch (mu::Parser::exception_type &e) {
                return fail(parserErrorString(e));
            }
            ++processed;
        }
        entry.ioMask |= Mask::IOM_WEDGTEXCOORD;
        doc.markMeshGeometryChanged(
            meshIndex,
            QObject::tr("Applied per-wedge texture function to '%1'.").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = { QObject::tr("Processed %1 faces.").arg(processed) };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterFaceNormal)) {
        const QString exprX = stringParameter(parameters, QStringLiteral("x"), QStringLiteral("-fnx"));
        const QString exprY = stringParameter(parameters, QStringLiteral("y"), QStringLiteral("-fny"));
        const QString exprZ = stringParameter(parameters, QStringLiteral("z"), QStringLiteral("-fnz"));
        const bool onSelected = boolParameter(parameters, QStringLiteral("onselected"), false);
        if (onSelected && !ensureFaceSelectionReady(mesh, meshError))
            return fail(meshError);

        mu::Parser px;
        mu::Parser py;
        mu::Parser pz;
        setPerFaceVariables(px, runtime, mesh);
        setPerFaceVariables(py, runtime, mesh);
        setPerFaceVariables(pz, runtime, mesh);
        try {
            px.SetExpr(exprX.toStdString());
            py.SetExpr(exprY.toStdString());
            pz.SetExpr(exprZ.toStdString());
        } catch (mu::Parser::exception_type &e) {
            return fail(parserErrorString(e));
        }

        int processed = 0;
        for (auto fi = mesh.face.begin(); fi != mesh.face.end(); ++fi) {
            if (fi->IsD())
                continue;
            if (onSelected && !fi->IsS())
                continue;
            setFaceRuntime(runtime, fi, mesh);
            try {
                fi->N() = vcg::Point3f(float(px.Eval()), float(py.Eval()), float(pz.Eval()));
            } catch (mu::Parser::exception_type &e) {
                return fail(parserErrorString(e));
            }
            ++processed;
        }
        entry.ioMask |= Mask::IOM_FACENORMAL;
        doc.markMeshGeometryChanged(
            meshIndex,
            QObject::tr("Applied per-face normal function to '%1'.").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = { QObject::tr("Processed %1 faces.").arg(processed) };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterFaceColor)) {
        const QString exprR = stringParameter(parameters, QStringLiteral("r"), QStringLiteral("255"));
        const QString exprG = stringParameter(parameters, QStringLiteral("g"), QStringLiteral("0"));
        const QString exprB = stringParameter(parameters, QStringLiteral("b"), QStringLiteral("255"));
        const QString exprA = stringParameter(parameters, QStringLiteral("a"), QStringLiteral("255"));
        const bool onSelected = boolParameter(parameters, QStringLiteral("onselected"), false);
        if (onSelected && !ensureFaceSelectionReady(mesh, meshError))
            return fail(meshError);

        mu::Parser pr;
        mu::Parser pg;
        mu::Parser pb;
        mu::Parser pa;
        setPerFaceVariables(pr, runtime, mesh);
        setPerFaceVariables(pg, runtime, mesh);
        setPerFaceVariables(pb, runtime, mesh);
        setPerFaceVariables(pa, runtime, mesh);
        try {
            pr.SetExpr(exprR.toStdString());
            pg.SetExpr(exprG.toStdString());
            pb.SetExpr(exprB.toStdString());
            pa.SetExpr(exprA.toStdString());
        } catch (mu::Parser::exception_type &e) {
            return fail(parserErrorString(e));
        }

        int processed = 0;
        for (auto fi = mesh.face.begin(); fi != mesh.face.end(); ++fi) {
            if (fi->IsD())
                continue;
            if (onSelected && !fi->IsS())
                continue;
            setFaceRuntime(runtime, fi, mesh);
            try {
                fi->C() = vcg::Color4b(
                    clampToByte(pr.Eval()),
                    clampToByte(pg.Eval()),
                    clampToByte(pb.Eval()),
                    clampToByte(pa.Eval()));
            } catch (mu::Parser::exception_type &e) {
                return fail(parserErrorString(e));
            }
            ++processed;
        }
        entry.ioMask |= Mask::IOM_FACECOLOR;
        doc.markMeshMaterialChanged(
            meshIndex,
            QObject::tr("Applied per-face color function to '%1'.").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = { QObject::tr("Processed %1 faces.").arg(processed) };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterFaceQuality)) {
        const QString exprQ = stringParameter(parameters, QStringLiteral("q"), QStringLiteral("x0+y0+z0"));
        const bool normalize = boolParameter(parameters, QStringLiteral("normalize"), false);
        const bool mapToColor = boolParameter(parameters, QStringLiteral("map"), false);
        const bool onSelected = boolParameter(parameters, QStringLiteral("onselected"), false);
        if (onSelected && !ensureFaceSelectionReady(mesh, meshError))
            return fail(meshError);

        mu::Parser parser;
        setPerFaceVariables(parser, runtime, mesh);
        try {
            parser.SetExpr(exprQ.toStdString());
        } catch (mu::Parser::exception_type &e) {
            return fail(parserErrorString(e));
        }

        int processed = 0;
        for (auto fi = mesh.face.begin(); fi != mesh.face.end(); ++fi) {
            if (fi->IsD())
                continue;
            if (onSelected && !fi->IsS())
                continue;
            setFaceRuntime(runtime, fi, mesh);
            try {
                fi->Q() = float(parser.Eval());
            } catch (mu::Parser::exception_type &e) {
                return fail(parserErrorString(e));
            }
            ++processed;
        }
        if (normalize)
            vcg::tri::UpdateQuality<VCGMesh>::FaceNormalize(mesh);
        if (mapToColor) {
            vcg::tri::UpdateColor<VCGMesh>::PerFaceQualityRamp(mesh);
            entry.ioMask |= Mask::IOM_FACECOLOR;
        }
        entry.ioMask |= Mask::IOM_FACEQUALITY;
        doc.markMeshMaterialChanged(
            meshIndex,
            QObject::tr("Applied per-face quality function to '%1'.").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = { QObject::tr("Processed %1 faces.").arg(processed) };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterDefVertScalar)) {
        const QString name = stringParameter(parameters, QStringLiteral("name"), QStringLiteral("CustomAttrName")).trimmed();
        const QString expr = stringParameter(parameters, QStringLiteral("expr"), QStringLiteral("x"));
        if (!checkCustomAttributeName(name, meshError))
            return fail(meshError);

        const std::string stdName = name.toStdString();
        VCGMesh::PerVertexAttributeHandle<float> handle;
        if (vcg::tri::HasPerVertexAttribute(mesh, stdName)) {
            handle = vcg::tri::Allocator<VCGMesh>::FindPerVertexAttribute<float>(mesh, stdName);
            if (!vcg::tri::Allocator<VCGMesh>::IsValidHandle(mesh, handle))
                return fail(QObject::tr("Attribute '%1' already exists with a different type.").arg(name));
        } else {
            handle = vcg::tri::Allocator<VCGMesh>::AddPerVertexAttribute<float>(mesh, stdName);
        }

        mu::Parser parser;
        setPerVertexVariables(parser, runtime, mesh);
        try {
            parser.SetExpr(expr.toStdString());
        } catch (mu::Parser::exception_type &e) {
            return fail(parserErrorString(e));
        }

        int processed = 0;
        for (auto vi = mesh.vert.begin(); vi != mesh.vert.end(); ++vi) {
            if (vi->IsD())
                continue;
            setVertexRuntime(runtime, vi, mesh);
            try {
                handle[vi] = float(parser.Eval());
            } catch (mu::Parser::exception_type &e) {
                return fail(parserErrorString(e));
            }
            ++processed;
        }
        doc.markMeshMaterialChanged(
            meshIndex,
            QObject::tr("Defined custom per-vertex scalar attribute '%1' on '%2'.")
                .arg(name, entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = { QObject::tr("Processed %1 vertices.").arg(processed) };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterDefFaceScalar)) {
        const QString name = stringParameter(parameters, QStringLiteral("name"), QStringLiteral("CustomAttrName")).trimmed();
        const QString expr = stringParameter(parameters, QStringLiteral("expr"), QStringLiteral("fi"));
        if (!checkCustomAttributeName(name, meshError))
            return fail(meshError);

        const std::string stdName = name.toStdString();
        VCGMesh::PerFaceAttributeHandle<float> handle;
        if (vcg::tri::HasPerFaceAttribute(mesh, stdName)) {
            handle = vcg::tri::Allocator<VCGMesh>::FindPerFaceAttribute<float>(mesh, stdName);
            if (!vcg::tri::Allocator<VCGMesh>::IsValidHandle(mesh, handle))
                return fail(QObject::tr("Attribute '%1' already exists with a different type.").arg(name));
        } else {
            handle = vcg::tri::Allocator<VCGMesh>::AddPerFaceAttribute<float>(mesh, stdName);
        }

        mu::Parser parser;
        setPerFaceVariables(parser, runtime, mesh);
        try {
            parser.SetExpr(expr.toStdString());
        } catch (mu::Parser::exception_type &e) {
            return fail(parserErrorString(e));
        }

        int processed = 0;
        for (auto fi = mesh.face.begin(); fi != mesh.face.end(); ++fi) {
            if (fi->IsD())
                continue;
            setFaceRuntime(runtime, fi, mesh);
            try {
                handle[fi] = float(parser.Eval());
            } catch (mu::Parser::exception_type &e) {
                return fail(parserErrorString(e));
            }
            ++processed;
        }
        doc.markMeshMaterialChanged(
            meshIndex,
            QObject::tr("Defined custom per-face scalar attribute '%1' on '%2'.")
                .arg(name, entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = { QObject::tr("Processed %1 faces.").arg(processed) };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterDefVertPoint)) {
        const QString name = stringParameter(parameters, QStringLiteral("name"), QStringLiteral("CustomAttrName")).trimmed();
        const QString exprX = stringParameter(parameters, QStringLiteral("x_expr"), QStringLiteral("x"));
        const QString exprY = stringParameter(parameters, QStringLiteral("y_expr"), QStringLiteral("y"));
        const QString exprZ = stringParameter(parameters, QStringLiteral("z_expr"), QStringLiteral("z"));
        if (!checkCustomAttributeName(name, meshError))
            return fail(meshError);

        const std::string stdName = name.toStdString();
        VCGMesh::PerVertexAttributeHandle<vcg::Point3f> handle;
        if (vcg::tri::HasPerVertexAttribute(mesh, stdName)) {
            handle = vcg::tri::Allocator<VCGMesh>::FindPerVertexAttribute<vcg::Point3f>(mesh, stdName);
            if (!vcg::tri::Allocator<VCGMesh>::IsValidHandle(mesh, handle))
                return fail(QObject::tr("Attribute '%1' already exists with a different type.").arg(name));
        } else {
            handle = vcg::tri::Allocator<VCGMesh>::AddPerVertexAttribute<vcg::Point3f>(mesh, stdName);
        }

        mu::Parser px;
        mu::Parser py;
        mu::Parser pz;
        setPerVertexVariables(px, runtime, mesh);
        setPerVertexVariables(py, runtime, mesh);
        setPerVertexVariables(pz, runtime, mesh);
        try {
            px.SetExpr(exprX.toStdString());
            py.SetExpr(exprY.toStdString());
            pz.SetExpr(exprZ.toStdString());
        } catch (mu::Parser::exception_type &e) {
            return fail(parserErrorString(e));
        }

        int processed = 0;
        for (auto vi = mesh.vert.begin(); vi != mesh.vert.end(); ++vi) {
            if (vi->IsD())
                continue;
            setVertexRuntime(runtime, vi, mesh);
            try {
                handle[vi] = vcg::Point3f(float(px.Eval()), float(py.Eval()), float(pz.Eval()));
            } catch (mu::Parser::exception_type &e) {
                return fail(parserErrorString(e));
            }
            ++processed;
        }
        doc.markMeshMaterialChanged(
            meshIndex,
            QObject::tr("Defined custom per-vertex point attribute '%1' on '%2'.")
                .arg(name, entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = { QObject::tr("Processed %1 vertices.").arg(processed) };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterDefFacePoint)) {
        const QString name = stringParameter(parameters, QStringLiteral("name"), QStringLiteral("CustomAttrName")).trimmed();
        const QString exprX = stringParameter(parameters, QStringLiteral("x_expr"), QStringLiteral("x0"));
        const QString exprY = stringParameter(parameters, QStringLiteral("y_expr"), QStringLiteral("y0"));
        const QString exprZ = stringParameter(parameters, QStringLiteral("z_expr"), QStringLiteral("z0"));
        if (!checkCustomAttributeName(name, meshError))
            return fail(meshError);

        const std::string stdName = name.toStdString();
        VCGMesh::PerFaceAttributeHandle<vcg::Point3f> handle;
        if (vcg::tri::HasPerFaceAttribute(mesh, stdName)) {
            handle = vcg::tri::Allocator<VCGMesh>::FindPerFaceAttribute<vcg::Point3f>(mesh, stdName);
            if (!vcg::tri::Allocator<VCGMesh>::IsValidHandle(mesh, handle))
                return fail(QObject::tr("Attribute '%1' already exists with a different type.").arg(name));
        } else {
            handle = vcg::tri::Allocator<VCGMesh>::AddPerFaceAttribute<vcg::Point3f>(mesh, stdName);
        }

        mu::Parser px;
        mu::Parser py;
        mu::Parser pz;
        setPerFaceVariables(px, runtime, mesh);
        setPerFaceVariables(py, runtime, mesh);
        setPerFaceVariables(pz, runtime, mesh);
        try {
            px.SetExpr(exprX.toStdString());
            py.SetExpr(exprY.toStdString());
            pz.SetExpr(exprZ.toStdString());
        } catch (mu::Parser::exception_type &e) {
            return fail(parserErrorString(e));
        }

        int processed = 0;
        for (auto fi = mesh.face.begin(); fi != mesh.face.end(); ++fi) {
            if (fi->IsD())
                continue;
            setFaceRuntime(runtime, fi, mesh);
            try {
                handle[fi] = vcg::Point3f(float(px.Eval()), float(py.Eval()), float(pz.Eval()));
            } catch (mu::Parser::exception_type &e) {
                return fail(parserErrorString(e));
            }
            ++processed;
        }
        doc.markMeshMaterialChanged(
            meshIndex,
            QObject::tr("Defined custom per-face point attribute '%1' on '%2'.")
                .arg(name, entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = { QObject::tr("Processed %1 faces.").arg(processed) };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterRefine)) {
        const std::string condSelect =
            stringParameter(parameters, QStringLiteral("condSelect"), QStringLiteral("(q0 >= 0 && q1 >= 0)"))
                .toStdString();
        const std::string exprX =
            stringParameter(parameters, QStringLiteral("x"), QStringLiteral("(x0+x1)/2")).toStdString();
        const std::string exprY =
            stringParameter(parameters, QStringLiteral("y"), QStringLiteral("(y0+y1)/2")).toStdString();
        const std::string exprZ =
            stringParameter(parameters, QStringLiteral("z"), QStringLiteral("(z0+z1)/2")).toStdString();

        bool midpointError = false;
        bool edgeError = false;
        std::string message;
        qmeshlab::filters::MidPointCustom<VCGMesh> midpoint(mesh, exprX, exprY, exprZ, midpointError, message);
        qmeshlab::filters::CustomEdge<VCGMesh> edge(condSelect, edgeError, message);
        if (midpointError || edgeError)
            return fail(QString::fromStdString(message));

        vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
        vcg::tri::RefineE<
            VCGMesh,
            qmeshlab::filters::MidPointCustom<VCGMesh>,
            qmeshlab::filters::CustomEdge<VCGMesh>>(mesh, midpoint, edge, false, nullptr);
        vcg::tri::UpdateFlags<VCGMesh>::VertexClearV(mesh);
        vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
        entry.ioMask |= Mask::IOM_VERTNORMAL | Mask::IOM_FACENORMAL;
        doc.markMeshGeometryChanged(
            meshIndex,
            QObject::tr("Applied user-defined refine to '%1'.").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = {
            QObject::tr("Refined mesh now has %1 vertices and %2 faces.")
                .arg(mesh.VN())
                .arg(mesh.FN())
        };
        return result;
    }

    return fail(QObject::tr("Unknown filter id: %1").arg(filterId));
}

void registerFuncFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<FuncFilterPlugin>());
}
