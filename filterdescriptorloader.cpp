#include "filterdescriptorloader.h"

#include "document.h"
#include "vcgmesh.h"
#include <vcg/complex/algorithms/stat.h>
#include <vcg/complex/algorithms/update/selection.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <QColor>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <thread>

namespace {

MeshFilterInputDomain parseInputDomain(const QString &s)
{
    if (s == QStringLiteral("None"))
        return MeshFilterInputDomain::None;
    if (s == QStringLiteral("WholeDocument"))
        return MeshFilterInputDomain::WholeDocument;
    return MeshFilterInputDomain::SingleMesh;
}

MeshFilterOutputDomain parseOutputDomain(const QString &s)
{
    if (s == QStringLiteral("NewMeshes"))
        return MeshFilterOutputDomain::NewMeshes;
    if (s == QStringLiteral("ModifyCurrentMesh"))
        return MeshFilterOutputDomain::ModifyCurrentMesh;
    return MeshFilterOutputDomain::Information;
}

MeshFilterParameterType parseParamType(const QString &s)
{
    if (s == QStringLiteral("bool"))   return MeshFilterParameterType::Bool;
    if (s == QStringLiteral("int"))    return MeshFilterParameterType::Int;
    if (s == QStringLiteral("double")) return MeshFilterParameterType::Double;
    if (s == QStringLiteral("absperc")) return MeshFilterParameterType::AbsPerc;
    if (s == QStringLiteral("string")) return MeshFilterParameterType::String;
    if (s == QStringLiteral("enum"))   return MeshFilterParameterType::Enum;
    if (s == QStringLiteral("color"))  return MeshFilterParameterType::Color;
    return MeshFilterParameterType::String;
}

// A QVariant that is a QString starting with "@" is a symbolic token.
// Non-symbolic values are parsed from JSON numbers/booleans/strings.
QVariant parseJsonValue(const QJsonValue &v)
{
    if (v.isNull() || v.isUndefined())
        return QVariant();
    if (v.isBool())
        return v.toBool();
    if (v.isDouble())
        return v.toDouble();
    if (v.isString()) {
        const QString s = v.toString();
        // Keep symbolic tokens as-is so resolveSymbolicBounds can handle them.
        return s;
    }
    return QVariant();
}

MeshFilterParameterDescriptor parseParameter(const QJsonObject &obj)
{
    MeshFilterParameterDescriptor p;
    p.id           = obj.value(QStringLiteral("id")).toString();
    p.label        = obj.value(QStringLiteral("label")).toString();
    p.helpMarkdown = obj.value(QStringLiteral("help")).toString();
    p.group        = obj.value(QStringLiteral("group")).toString(QStringLiteral("main"));
    p.type         = parseParamType(obj.value(QStringLiteral("type")).toString());
    p.defaultValue = parseJsonValue(obj.value(QStringLiteral("default")));
    p.minValue     = parseJsonValue(obj.value(QStringLiteral("min")));
    p.maxValue     = parseJsonValue(obj.value(QStringLiteral("max")));
    p.decimals     = obj.value(QStringLiteral("decimals")).toInt(3);

    const QJsonArray opts = obj.value(QStringLiteral("enumOptions")).toArray();
    for (const QJsonValue &ov : opts) {
        const QJsonObject oo = ov.toObject();
        MeshFilterEnumOption opt;
        opt.id           = oo.value(QStringLiteral("id")).toString();
        opt.label        = oo.value(QStringLiteral("label")).toString();
        opt.helpMarkdown = oo.value(QStringLiteral("help")).toString();
        p.enumOptions.push_back(std::move(opt));
    }
    return p;
}

MeshFilterDescriptor parseFilter(const QJsonObject &obj)
{
    MeshFilterDescriptor d;
    d.id                    = obj.value(QStringLiteral("id")).toString();
    d.menuPath              = obj.value(QStringLiteral("menuPath")).toString();
    d.name                  = obj.value(QStringLiteral("name")).toString();
    d.shortDescription      = obj.value(QStringLiteral("shortDescription")).toString();
    d.longDescriptionMarkdown = obj.value(QStringLiteral("longDescriptionMarkdown")).toString();

    const QJsonArray tags = obj.value(QStringLiteral("tags")).toArray();
    for (const QJsonValue &t : tags)
        d.tags << t.toString();

    d.inputDomain  = parseInputDomain(obj.value(QStringLiteral("inputDomain")).toString(QStringLiteral("SingleMesh")));
    d.outputDomain = parseOutputDomain(obj.value(QStringLiteral("outputDomain")).toString(QStringLiteral("Information")));

    const QJsonObject req = obj.value(QStringLiteral("inputRequirements")).toObject();
    d.inputRequirements.requireVertices           = req.value(QStringLiteral("requireVertices")).toBool(false);
    d.inputRequirements.requireEdges              = req.value(QStringLiteral("requireEdges")).toBool(false);
    d.inputRequirements.requireFaces              = req.value(QStringLiteral("requireFaces")).toBool(false);
    d.inputRequirements.requireVertexColor        = req.value(QStringLiteral("requireVertexColor")).toBool(false);
    d.inputRequirements.requireFaceColor          = req.value(QStringLiteral("requireFaceColor")).toBool(false);
    d.inputRequirements.requireTextureCoordinates = req.value(QStringLiteral("requireTextureCoordinates")).toBool(false);
    d.inputRequirements.requireTextures           = req.value(QStringLiteral("requireTextures")).toBool(false);
    d.inputRequirements.requireVertexQuality      = req.value(QStringLiteral("requireVertexQuality")).toBool(false);
    d.inputRequirements.requireFaceQuality        = req.value(QStringLiteral("requireFaceQuality")).toBool(false);

    const QJsonArray params = obj.value(QStringLiteral("parameters")).toArray();
    for (const QJsonValue &pv : params) {
        d.parameters.push_back(parseParameter(pv.toObject()));
    }
    return d;
}

// Resolve a single QVariant that may be a symbolic "@token" string.
// Returns the original value if it is not a symbolic token.
QVariant resolveToken(const QVariant &v, double bboxDiag,
                      double qualityVMin, double qualityVMax,
                      double qualityFMin, double qualityFMax,
                      bool hasSelectedFaces, bool hasSelectedVerts,
                      int faceCount, int selFaces,
                      int hardwareThreads)
{
    if (v.userType() != QMetaType::QString)
        return v;
    const QString s = v.toString();
    if (!s.startsWith(QLatin1Char('@')))
        return v;

    const QString token = s.mid(1); // strip leading '@'

    if (token == QStringLiteral("bboxDiag"))           return bboxDiag;
    if (token == QStringLiteral("bboxDiag01"))         return bboxDiag * 0.01;
    if (token == QStringLiteral("bboxDiag001"))        return bboxDiag * 0.001;
    if (token == QStringLiteral("bboxDiag0001"))       return bboxDiag * 0.0001;
    if (token == QStringLiteral("bboxDiag00001"))      return bboxDiag * 0.00001;
    if (token == QStringLiteral("bboxDiag0005"))       return bboxDiag * 0.005;
    if (token == QStringLiteral("bboxDiag003"))        return bboxDiag * 0.03;
    if (token == QStringLiteral("bboxDiagTenth"))      return bboxDiag * 0.1;
    if (token == QStringLiteral("bboxDiagHalf"))       return bboxDiag * 0.5;
    if (token == QStringLiteral("bboxDiag5x"))         return bboxDiag * 5.0;
    if (token == QStringLiteral("qualityVMin"))        return qualityVMin;
    if (token == QStringLiteral("qualityVMax"))        return qualityVMax;
    if (token == QStringLiteral("qualityFMin"))        return qualityFMin;
    if (token == QStringLiteral("qualityFMax"))        return qualityFMax;
    if (token == QStringLiteral("hasSelectedFaces"))   return hasSelectedFaces;
    if (token == QStringLiteral("hasSelectedVerts"))   return hasSelectedVerts;
    if (token == QStringLiteral("faceCount"))          return std::max(1, faceCount);
    if (token == QStringLiteral("faceCountHalf"))      return std::max(1, faceCount / 2);
    if (token == QStringLiteral("selOrFaceCountHalf")) {
        const int base = (selFaces > 0 ? selFaces : faceCount);
        return std::max(1, base / 2);
    }
    if (token == QStringLiteral("hardwareThreads"))    return hardwareThreads;

    return v; // unknown token → leave as-is
}

} // namespace

std::vector<MeshFilterDescriptor> FilterDescriptorLoader::load(
    const QString &resourcePath,
    QString &errorMessage)
{
    QFile f(resourcePath);
    if (!f.open(QIODevice::ReadOnly)) {
        errorMessage = QStringLiteral("Cannot open filter descriptor file: %1").arg(resourcePath);
        return {};
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (doc.isNull()) {
        errorMessage = QStringLiteral("JSON parse error in %1: %2")
                           .arg(resourcePath, err.errorString());
        return {};
    }

    const QJsonObject root = doc.object();
    const QJsonArray filters = root.value(QStringLiteral("filters")).toArray();

    std::vector<MeshFilterDescriptor> result;
    result.reserve(static_cast<size_t>(filters.size()));
    for (const QJsonValue &fv : filters)
        result.push_back(parseFilter(fv.toObject()));

    return result;
}

void FilterDescriptorLoader::resolveSymbolicBounds(
    std::vector<MeshFilterDescriptor> &descriptors,
    const Document &doc)
{
    double bboxDiag = 1.0;
    double qualityVMin = 0.0, qualityVMax = 1.0;
    double qualityFMin = 0.0, qualityFMax = 1.0;
    bool hasSelectedFaces = false;
    bool hasSelectedVerts = false;
    int faceCount = 0;
    int selFaces = 0;

    const int ci = doc.currentMeshIndex();
    if (ci >= 0 && ci < doc.meshCount()) {
        const VCGMesh &mesh = doc.mesh(ci).mesh;
        bboxDiag = std::max(1e-9, double(mesh.bbox.Diag()));
        faceCount = mesh.FN();
        selFaces = vcg::tri::UpdateSelection<VCGMesh>::FaceCount(mesh);
        const int selVerts = vcg::tri::UpdateSelection<VCGMesh>::VertexCount(mesh);
        hasSelectedFaces = (selFaces > 0);
        hasSelectedVerts = (selVerts > 0);
        if (mesh.VN() > 0) {
            const auto vr = vcg::tri::Stat<VCGMesh>::ComputePerVertexQualityMinMax(mesh);
            qualityVMin = std::min(double(vr.first), double(vr.second));
            qualityVMax = std::max(double(vr.first), double(vr.second));
        }
        if (mesh.FN() > 0) {
            const auto fr = vcg::tri::Stat<VCGMesh>::ComputePerFaceQualityMinMax(mesh);
            qualityFMin = std::min(double(fr.first), double(fr.second));
            qualityFMax = std::max(double(fr.first), double(fr.second));
        }
    }

    const unsigned int hw = std::thread::hardware_concurrency();
    const int hardwareThreads = static_cast<int>(hw > 0 ? hw : 8);

    for (MeshFilterDescriptor &fd : descriptors) {
        for (MeshFilterParameterDescriptor &p : fd.parameters) {
            p.defaultValue = resolveToken(p.defaultValue, bboxDiag,
                qualityVMin, qualityVMax, qualityFMin, qualityFMax,
                hasSelectedFaces, hasSelectedVerts, faceCount, selFaces, hardwareThreads);
            p.minValue = resolveToken(p.minValue, bboxDiag,
                qualityVMin, qualityVMax, qualityFMin, qualityFMax,
                hasSelectedFaces, hasSelectedVerts, faceCount, selFaces, hardwareThreads);
            p.maxValue = resolveToken(p.maxValue, bboxDiag,
                qualityVMin, qualityVMax, qualityFMin, qualityFMax,
                hasSelectedFaces, hasSelectedVerts, faceCount, selFaces, hardwareThreads);
        }
    }
}
