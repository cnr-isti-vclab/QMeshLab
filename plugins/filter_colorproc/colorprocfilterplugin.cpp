#include "colorprocfilterplugin.h"

#include "document.h"
#include "filterparam.h"
#include "meshfilterpluginmanager.h"
#include "textureassociationutils.h"
#include "vcgmesh.h"
#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/algorithms/bitquad_support.h>
#include <vcg/complex/algorithms/clean.h>
#include <vcg/complex/algorithms/parametrization/distortion.h>
#include <vcg/complex/algorithms/polygon_support.h>
#include <vcg/complex/algorithms/smooth.h>
#include <vcg/complex/algorithms/stat.h>
#include <vcg/complex/algorithms/update/color.h>
#include <vcg/complex/algorithms/update/curvature.h>
#include <vcg/complex/algorithms/update/flag.h>
#include <vcg/complex/algorithms/update/quality.h>
#include <vcg/complex/algorithms/update/selection.h>
#include <vcg/complex/algorithms/update/topology.h>
#include <vcg/math/histogram.h>
#include <vcg/math/random_generator.h>
#include <vcg/space/color4.h>
#include <vcg/space/colorspace.h>
#include <vcg/space/fitting3.h>
#include <vcg/space/intersection3.h>
#include <vcg/space/plane3.h>
#include <vcg/space/triangle3.h>
#include <QImage>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <optional>
#include <vector>

namespace {
constexpr QLatin1StringView kFilterFilling("set_color_per_vertex");
constexpr QLatin1StringView kFilterThresholding("apply_color_thresholding_per_vertex");
constexpr QLatin1StringView kFilterContrastBright("apply_color_brightness_contrast_gamma_per_vertex");
constexpr QLatin1StringView kFilterInvert("apply_color_inverse_per_vertex");
constexpr QLatin1StringView kFilterLevels("apply_color_level_adjustment_per_vertex");
constexpr QLatin1StringView kFilterColourisation("apply_color_intensity_colourisation_per_vertex");
constexpr QLatin1StringView kFilterDesaturation("apply_color_desaturation_per_vertex");
constexpr QLatin1StringView kFilterEqualize("apply_color_equalization_per_vertex");
constexpr QLatin1StringView kFilterWhiteBalance("apply_color_white_balance_per_vertex");
constexpr QLatin1StringView kFilterPerlinColor("compute_color_perlin_noise_per_vertex");
constexpr QLatin1StringView kFilterColorNoise("apply_color_noising_per_vertex");
constexpr QLatin1StringView kFilterScatterPerMesh("compute_color_scattering_per_mesh");
constexpr QLatin1StringView kFilterClampQuality("apply_scalar_clamping_per_vertex");
constexpr QLatin1StringView kFilterSaturateQuality("apply_scalar_saturation_per_vertex");
constexpr QLatin1StringView kFilterMapVQuality("compute_color_from_scalar_per_vertex");
constexpr QLatin1StringView kFilterMapFQuality("compute_color_from_scalar_per_face");
constexpr QLatin1StringView kFilterDiscreteCurvature("compute_scalar_by_discrete_curvature_per_vertex");
constexpr QLatin1StringView kFilterTriangleQuality("compute_scalar_by_aspect_ratio_per_face");
constexpr QLatin1StringView kFilterVertexSmooth("apply_color_laplacian_smoothing_per_vertex");
constexpr QLatin1StringView kFilterFaceSmooth("apply_color_laplacian_smoothing_per_face");
constexpr QLatin1StringView kFilterVertexToFace("compute_color_transfer_vertex_to_face");
constexpr QLatin1StringView kFilterMeshToFace("compute_color_transfer_mesh_to_face");
constexpr QLatin1StringView kFilterFaceToVertex("compute_color_transfer_face_to_vertex");
constexpr QLatin1StringView kFilterTextureToVertex("compute_color_from_texture_per_vertex");
constexpr QLatin1StringView kFilterRandomFace("compute_color_random_per_face");
constexpr QLatin1StringView kFilterRandomConnected("compute_color_by_conntected_component_per_face");
constexpr QLatin1StringView kFilterVertexToFaceQuality("compute_scalar_transfer_vertex_to_face");
constexpr QLatin1StringView kFilterFaceToVertexQuality("compute_scalar_transfer_face_to_vertex");

using Mask = vcg::tri::io::Mask;
namespace Tex = TextureAssociationUtils;
using Scalar = VCGMesh::ScalarType;
using Point = vcg::Point3f;
using Histogramf = vcg::Histogram<float>;
using Distributionf = vcg::Distribution<float>;

struct CurrentMeshRef {
    int index = -1;
    Document::MeshEntry *entry = nullptr;
};

MeshFilterRunResult fail(const QString &message)
{
    MeshFilterRunResult result;
    result.success = false;
    result.documentModified = false;
    result.errorMessage = message;
    return result;
}

MeshFilterRunResult success(const QStringList &info = {})
{
    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.infoMessages = info;
    return result;
}

MeshFilterRunResult qualitySuccess(
    int meshIndex,
    MeshFilterVisualizationAttribute attribute,
    const QStringList &info = {})
{
    MeshFilterRunResult result = success(info);
    result.visualizationHints.push_back({ meshIndex, attribute });
    return result;
}

std::optional<CurrentMeshRef> currentMesh(Document &doc, QString &errorMessage)
{
    const int meshIndex = doc.currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= doc.meshCount()) {
        errorMessage = QObject::tr("No current mesh selected.");
        return std::nullopt;
    }
    return CurrentMeshRef{ meshIndex, &doc.mesh(meshIndex) };
}

QString meshLabel(const Document::MeshEntry &entry, int meshIndex)
{
    const QString trimmed = entry.name.trimmed();
    if (!trimmed.isEmpty())
        return trimmed;
    return QObject::tr("Mesh %1").arg(meshIndex + 1);
}

vcg::ColorMap colorMapFromId(const QString &id)
{
    if (id == QStringLiteral("viridis")) return vcg::ColorMap::Viridis;
    if (id == QStringLiteral("plasma"))  return vcg::ColorMap::Plasma;
    if (id == QStringLiteral("cividis")) return vcg::ColorMap::Cividis;
    if (id == QStringLiteral("turbo"))   return vcg::ColorMap::Turbo;
    if (id == QStringLiteral("rdpu"))    return vcg::ColorMap::RdPu;
    return vcg::ColorMap::RGB;
}

unsigned char rgbMaskFromParams(const FilterParams &params)
{
    unsigned char mask = vcg::tri::UpdateColor<VCGMesh>::NO_CHANNELS;
    if (params.getBool(QStringLiteral("rCh"), false))
        mask = mask | vcg::tri::UpdateColor<VCGMesh>::RED_CHANNEL;
    if (params.getBool(QStringLiteral("gCh"), false))
        mask = mask | vcg::tri::UpdateColor<VCGMesh>::GREEN_CHANNEL;
    if (params.getBool(QStringLiteral("bCh"), false))
        mask = mask | vcg::tri::UpdateColor<VCGMesh>::BLUE_CHANNEL;
    if (mask == vcg::tri::UpdateColor<VCGMesh>::NO_CHANNELS)
        mask = vcg::tri::UpdateColor<VCGMesh>::ALL_CHANNELS;
    return mask;
}

QColor colorParam(const FilterParams &params, const QString &id)
{
    return params.getColor(id, QColor(Qt::white));
}

vcg::Color4b toColor4b(const QColor &color)
{
    return vcg::Color4b(color.red(), color.green(), color.blue(), color.alpha());
}

void markGeometry(Document &doc, int meshIndex, const QString &message)
{
    if (meshIndex >= 0 && meshIndex < doc.meshCount())
        doc.markMeshGeometryChanged(meshIndex, message);
}

void ensureVertexColor(Document::MeshEntry &entry)
{
    entry.ioMask |= Mask::IOM_VERTCOLOR;
}

void ensureFaceColor(Document::MeshEntry &entry)
{
    entry.ioMask |= Mask::IOM_FACECOLOR;
}

void ensureVertexQuality(Document::MeshEntry &entry)
{
    entry.ioMask |= Mask::IOM_VERTQUALITY;
}

void ensureFaceQuality(Document::MeshEntry &entry)
{
    entry.ioMask |= Mask::IOM_FACEQUALITY;
}

QRgb sampleWrappedTexture(const QImage &image, const vcg::Point2f &uv)
{
    if (image.isNull() || image.width() <= 0 || image.height() <= 0)
        return qRgb(255, 255, 255);

    const float wrappedU = uv.X() - std::floor(uv.X());
    const float wrappedV = uv.Y() - std::floor(uv.Y());
    const int x = std::clamp(int(wrappedU * float(image.width())), 0, image.width() - 1);
    const int y = std::clamp(int((1.0f - wrappedV) * float(image.height()) - 1.0f), 0, image.height() - 1);
    return image.pixel(x, y);
}

} // namespace

QString ColorProcFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.colorproc");
}

QString ColorProcFilterPlugin::name() const
{
    return QObject::tr("QMeshLab Color Processing Filters");
}

MeshFilterRunResult ColorProcFilterPlugin::runFilter(
    const QString &filterId,
    const FilterParams &params,
    Document &doc) const
{
    vcg::CallBackPos *cb = doc.progressCallback();

    if (filterId == QString::fromLatin1(kFilterScatterPerMesh)) {
        std::vector<int> visibleMeshes;
        visibleMeshes.reserve(size_t(doc.meshCount()));
        for (int i = 0; i < doc.meshCount(); ++i) {
            if (doc.mesh(i).visible)
                visibleMeshes.push_back(i);
        }
        if (visibleMeshes.empty())
            return fail(QObject::tr("No visible mesh layers available."));

        const int seedParam = params.getInt(QStringLiteral("seed"), 0);
        const bool automaticSeed = (seedParam == 0);
        int seed = seedParam;
        if (automaticSeed)
            seed = int(std::time(nullptr));
        vcg::math::MarsenneTwisterRNG rng{ unsigned(seed) };
        int colorIndex = rng.generate(int(visibleMeshes.size()));
        for (int meshIndex : visibleMeshes) {
            Document::MeshEntry &entry = doc.mesh(meshIndex);
            entry.mesh.C() = vcg::Color4b::Scatter(int(visibleMeshes.size()), colorIndex);
            markGeometry(doc, meshIndex, QObject::tr("Assigned scattered mesh colors"));
            ++colorIndex;
            colorIndex %= int(visibleMeshes.size());
        }
        return success({
            QObject::tr("Assigned scattered colors to %1 visible mesh layers.").arg(visibleMeshes.size()),
            automaticSeed
                ? QObject::tr("Random seed: automatic")
                : QObject::tr("Random seed: %1").arg(seed)
        });
    }

    QString errorMessage;
    auto current = currentMesh(doc, errorMessage);
    if (!current)
        return fail(errorMessage);

    const int meshIndex = current->index;
    Document::MeshEntry &entry = *current->entry;
    VCGMesh &mesh = entry.mesh;

    if (filterId == QString::fromLatin1(kFilterFilling)) {
        vcg::tri::UpdateColor<VCGMesh>::PerVertexConstant(
            mesh,
            toColor4b(colorParam(params, QStringLiteral("color1"))),
            params.getBool(QStringLiteral("onSelected"), false));
        ensureVertexColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Filled vertex colors of '%1'").arg(meshLabel(entry, meshIndex)));
        return success();
    }

    if (filterId == QString::fromLatin1(kFilterThresholding)) {
        const Scalar threshold = vcg::math::Clamp<Scalar>(Scalar(params.getDouble(QStringLiteral("threshold"), 128.0)), Scalar(0), Scalar(255));
        vcg::tri::UpdateColor<VCGMesh>::PerVertexThresholding(
            mesh,
            threshold,
            toColor4b(colorParam(params, QStringLiteral("color1"))),
            toColor4b(colorParam(params, QStringLiteral("color2"))),
            params.getBool(QStringLiteral("onSelected"), false));
        ensureVertexColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Applied vertex color thresholding to '%1'").arg(meshLabel(entry, meshIndex)));
        return success();
    }

    if (filterId == QString::fromLatin1(kFilterContrastBright)) {
        const Scalar gamma = vcg::math::Clamp<Scalar>(Scalar(params.getDouble(QStringLiteral("gamma"), 1.0)), Scalar(0.1), Scalar(5.0));
        const Scalar brightness = Scalar(params.getDouble(QStringLiteral("brightness"), 0.0));
        const Scalar contrast = Scalar(params.getDouble(QStringLiteral("contrast"), 0.0));
        const bool selected = params.getBool(QStringLiteral("onSelected"), false);
        vcg::tri::UpdateColor<VCGMesh>::PerVertexGamma(mesh, gamma, selected);
        vcg::tri::UpdateColor<VCGMesh>::PerVertexBrightnessContrast(mesh, brightness / 256.0f, contrast / 256.0f, selected);
        ensureVertexColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Adjusted vertex color brightness/contrast/gamma on '%1'").arg(meshLabel(entry, meshIndex)));
        return success();
    }

    if (filterId == QString::fromLatin1(kFilterInvert)) {
        vcg::tri::UpdateColor<VCGMesh>::PerVertexInvert(mesh, params.getBool(QStringLiteral("onSelected"), false));
        ensureVertexColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Inverted vertex colors of '%1'").arg(meshLabel(entry, meshIndex)));
        return success();
    }

    if (filterId == QString::fromLatin1(kFilterLevels)) {
        auto applyLevels = [&](Document::MeshEntry &targetEntry) {
            vcg::tri::UpdateColor<VCGMesh>::PerVertexLevels(
                targetEntry.mesh,
                Scalar(params.getDouble(QStringLiteral("gamma"), 1.0)),
                Scalar(params.getDouble(QStringLiteral("in_min"), 0.0) / 255.0),
                Scalar(params.getDouble(QStringLiteral("in_max"), 255.0) / 255.0),
                Scalar(params.getDouble(QStringLiteral("out_min"), 0.0) / 255.0),
                Scalar(params.getDouble(QStringLiteral("out_max"), 255.0) / 255.0),
                rgbMaskFromParams(params),
                params.getBool(QStringLiteral("onSelected"), false));
            ensureVertexColor(targetEntry);
        };

        QStringList touched;
        if (params.getBool(QStringLiteral("apply_to_all"), false)) {
            for (int i = 0; i < doc.meshCount(); ++i) {
                Document::MeshEntry &targetEntry = doc.mesh(i);
                if (!targetEntry.visible)
                    continue;
                applyLevels(targetEntry);
                markGeometry(doc, i, QObject::tr("Adjusted vertex color levels on '%1'").arg(meshLabel(targetEntry, i)));
                touched << meshLabel(targetEntry, i);
            }
        } else {
            applyLevels(entry);
            markGeometry(doc, meshIndex, QObject::tr("Adjusted vertex color levels on '%1'").arg(meshLabel(entry, meshIndex)));
            touched << meshLabel(entry, meshIndex);
        }
        return success({ QObject::tr("Adjusted color levels on %1 mesh layer(s).").arg(touched.size()) });
    }

    if (filterId == QString::fromLatin1(kFilterColourisation)) {
        const Scalar hue = vcg::math::Clamp<Scalar>(Scalar(params.getDouble(QStringLiteral("hue"), 0.0) / 360.0), Scalar(0), Scalar(1));
        const Scalar saturation = vcg::math::Clamp<Scalar>(Scalar(params.getDouble(QStringLiteral("saturation"), 100.0) / 100.0), Scalar(0), Scalar(1));
        const Scalar luminance = vcg::math::Clamp<Scalar>(Scalar(params.getDouble(QStringLiteral("luminance"), 50.0) / 100.0), Scalar(0), Scalar(1));
        const Scalar intensity = vcg::math::Clamp<Scalar>(Scalar(params.getDouble(QStringLiteral("intensity"), 50.0) / 100.0), Scalar(0), Scalar(1));
        double r = 0.0, g = 0.0, b = 0.0;
        vcg::ColorSpace<unsigned char>::HSLtoRGB(double(hue), double(saturation), double(luminance), r, g, b);
        vcg::tri::UpdateColor<VCGMesh>::PerVertexColourisation(
            mesh,
            vcg::Color4b(int(r * 255.0), int(g * 255.0), int(b * 255.0), 255),
            intensity,
            params.getBool(QStringLiteral("onSelected"), false));
        ensureVertexColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Colourised vertex colors of '%1'").arg(meshLabel(entry, meshIndex)));
        return success();
    }

    if (filterId == QString::fromLatin1(kFilterDesaturation)) {
        int method = 0;
        const QString methodId = params.getEnum(QStringLiteral("method"), QStringLiteral("lightness"));
        if (methodId == QStringLiteral("luminosity")) method = 1;
        else if (methodId == QStringLiteral("average")) method = 2;
        vcg::tri::UpdateColor<VCGMesh>::PerVertexDesaturation(mesh, method, params.getBool(QStringLiteral("onSelected"), false));
        ensureVertexColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Desaturated vertex colors of '%1'").arg(meshLabel(entry, meshIndex)));
        return success();
    }

    if (filterId == QString::fromLatin1(kFilterEqualize)) {
        vcg::tri::UpdateColor<VCGMesh>::PerVertexEqualize(mesh, rgbMaskFromParams(params), params.getBool(QStringLiteral("onSelected"), false));
        ensureVertexColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Equalized vertex colors of '%1'").arg(meshLabel(entry, meshIndex)));
        return success();
    }

    if (filterId == QString::fromLatin1(kFilterWhiteBalance)) {
        vcg::tri::UpdateColor<VCGMesh>::PerVertexWhiteBalance(
            mesh,
            toColor4b(colorParam(params, QStringLiteral("color"))),
            params.getBool(QStringLiteral("onSelected"), false));
        ensureVertexColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Applied white balance to '%1'").arg(meshLabel(entry, meshIndex)));
        return success();
    }

    if (filterId == QString::fromLatin1(kFilterPerlinColor)) {
        const Scalar freq = Scalar(params.getDouble(QStringLiteral("freq"), 10.0));
        const Scalar period = Scalar(std::max(1e-9, double(doc.mesh(meshIndex).mesh.bbox.Diag())) / std::max(1e-6, double(freq)));
        const QVector3D offsetVec = params.getPoint3f(QStringLiteral("offset"), QVector3D(0, 0, 0));
        vcg::tri::UpdateColor<VCGMesh>::PerVertexPerlinColoring(
            mesh,
            period,
            Point(offsetVec.x(), offsetVec.y(), offsetVec.z()),
            toColor4b(colorParam(params, QStringLiteral("color1"))),
            toColor4b(colorParam(params, QStringLiteral("color2"))),
            params.getBool(QStringLiteral("onSelected"), false));
        ensureVertexColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Applied Perlin color to '%1'").arg(meshLabel(entry, meshIndex)));
        return success();
    }

    if (filterId == QString::fromLatin1(kFilterColorNoise)) {
        vcg::tri::UpdateColor<VCGMesh>::PerVertexAddNoise(mesh, params.getInt(QStringLiteral("noiseBits"), 1), params.getBool(QStringLiteral("onSelected"), false));
        ensureVertexColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Added color noise to '%1'").arg(meshLabel(entry, meshIndex)));
        return success();
    }

    if (filterId == QString::fromLatin1(kFilterSaturateQuality)) {
        vcg::tri::UpdateQuality<VCGMesh>::VertexSaturate(mesh, Scalar(params.getDouble(QStringLiteral("gradientThr"), 1.0)));
        ensureVertexQuality(entry);
        QStringList info;
        if (params.getBool(QStringLiteral("updateColor"), false)) {
            Histogramf hist;
            vcg::tri::Stat<VCGMesh>::ComputePerVertexQualityHistogram(mesh, hist);
            vcg::tri::UpdateColor<VCGMesh>::PerVertexQualityRamp(mesh, hist.Percentile(0.1f), hist.Percentile(0.9f));
            ensureVertexColor(entry);
            info << QObject::tr("Updated vertex color ramp from saturated quality values.");
        }
        markGeometry(doc, meshIndex, QObject::tr("Saturated vertex quality of '%1'").arg(meshLabel(entry, meshIndex)));
        return qualitySuccess(meshIndex, MeshFilterVisualizationAttribute::VertexQuality, info);
    }

    if (filterId == QString::fromLatin1(kFilterMapVQuality)) {
        Histogramf hist;
        vcg::tri::Stat<VCGMesh>::ComputePerVertexQualityHistogram(mesh, hist);
        Scalar rangeMin = Scalar(params.getDouble(QStringLiteral("minVal"), hist.MinV()));
        Scalar rangeMax = Scalar(params.getDouble(QStringLiteral("maxVal"), hist.MaxV()));
        Scalar perc = Scalar(params.getDouble(QStringLiteral("perc"), 0.0));
        Scalar percLo = hist.Percentile(perc / 100.0f);
        Scalar percHi = hist.Percentile(1.0f - perc / 100.0f);
        if (params.getBool(QStringLiteral("zeroSym"), false)) {
            rangeMin = std::min(rangeMin, -vcg::math::Abs(rangeMax));
            rangeMax = std::max(vcg::math::Abs(rangeMin), rangeMax);
            percLo = std::min(percLo, -vcg::math::Abs(percHi));
            percHi = std::max(vcg::math::Abs(percLo), percHi);
        }
        const vcg::ColorMap cmap = colorMapFromId(params.getEnum(QStringLiteral("colorMap"), QStringLiteral("rgb")));
        if (perc > 0)
            vcg::tri::UpdateColor<VCGMesh>::PerVertexQualityRamp(mesh, percLo, percHi, cmap);
        else
            vcg::tri::UpdateColor<VCGMesh>::PerVertexQualityRamp(mesh, rangeMin, rangeMax, cmap);
        ensureVertexColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Mapped vertex quality into color on '%1'").arg(meshLabel(entry, meshIndex)));
        return success({
            QObject::tr("Quality Range: %1 %2").arg(hist.MinV()).arg(hist.MaxV())
        });
    }

    if (filterId == QString::fromLatin1(kFilterClampQuality)) {
        Histogramf hist;
        vcg::tri::Stat<VCGMesh>::ComputePerVertexQualityHistogram(mesh, hist);
        Scalar rangeMin = Scalar(params.getDouble(QStringLiteral("minVal"), hist.MinV()));
        Scalar rangeMax = Scalar(params.getDouble(QStringLiteral("maxVal"), hist.MaxV()));
        Scalar perc = Scalar(params.getDouble(QStringLiteral("perc"), 0.0));
        Scalar percLo = hist.Percentile(perc / 100.0f);
        Scalar percHi = hist.Percentile(1.0f - perc / 100.0f);
        if (params.getBool(QStringLiteral("zeroSym"), false)) {
            rangeMin = std::min(rangeMin, -vcg::math::Abs(rangeMax));
            rangeMax = std::max(vcg::math::Abs(rangeMin), rangeMax);
            percLo = std::min(percLo, -vcg::math::Abs(percHi));
            percHi = std::max(vcg::math::Abs(percLo), percHi);
        }
        if (perc > 0)
            vcg::tri::UpdateQuality<VCGMesh>::VertexClamp(mesh, percLo, percHi);
        else
            vcg::tri::UpdateQuality<VCGMesh>::VertexClamp(mesh, rangeMin, rangeMax);
        ensureVertexQuality(entry);
        markGeometry(doc, meshIndex, QObject::tr("Clamped vertex quality of '%1'").arg(meshLabel(entry, meshIndex)));
        return qualitySuccess(meshIndex, MeshFilterVisualizationAttribute::VertexQuality);
    }

    if (filterId == QString::fromLatin1(kFilterMapFQuality)) {
        Histogramf hist;
        vcg::tri::Stat<VCGMesh>::ComputePerFaceQualityHistogram(mesh, hist);
        Scalar rangeMin = Scalar(params.getDouble(QStringLiteral("minVal"), hist.MinV()));
        Scalar rangeMax = Scalar(params.getDouble(QStringLiteral("maxVal"), hist.MaxV()));
        Scalar perc = Scalar(params.getDouble(QStringLiteral("perc"), 0.0));
        Scalar percLo = hist.Percentile(perc / 100.0f);
        Scalar percHi = hist.Percentile(1.0f - perc / 100.0f);
        if (params.getBool(QStringLiteral("zeroSym"), false)) {
            rangeMin = std::min(rangeMin, -vcg::math::Abs(rangeMax));
            rangeMax = std::max(vcg::math::Abs(rangeMin), rangeMax);
            percLo = std::min(percLo, -vcg::math::Abs(percHi));
            percHi = std::max(vcg::math::Abs(percLo), percHi);
        }
        const vcg::ColorMap cmap = colorMapFromId(params.getEnum(QStringLiteral("colorMap"), QStringLiteral("rgb")));
        if (perc > 0)
            vcg::tri::UpdateColor<VCGMesh>::PerFaceQualityRamp(mesh, percLo, percHi, false, cmap);
        else
            vcg::tri::UpdateColor<VCGMesh>::PerFaceQualityRamp(mesh, rangeMin, rangeMax, false, cmap);
        ensureFaceColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Mapped face quality into color on '%1'").arg(meshLabel(entry, meshIndex)));
        return success();
    }

    if (filterId == QString::fromLatin1(kFilterDiscreteCurvature)) {
        vcg::tri::UpdateFlags<VCGMesh>::FaceBorderFromFF(mesh);
        if (vcg::tri::Clean<VCGMesh>::CountNonManifoldEdgeFF(mesh) > 0)
            return fail(QObject::tr("Mesh has some not 2-manifold faces; curvature computation requires manifoldness."));
        vcg::tri::UpdateCurvature<VCGMesh>::MeanAndGaussian(mesh);
        const QString curvatureType = params.getEnum(QStringLiteral("CurvatureType"), QStringLiteral("mean"));
        if (curvatureType == QStringLiteral("mean"))
            vcg::tri::UpdateQuality<VCGMesh>::VertexFromAttributeName(mesh, "KH");
        else if (curvatureType == QStringLiteral("gaussian"))
            vcg::tri::UpdateQuality<VCGMesh>::VertexFromAttributeName(mesh, "KG");
        else if (curvatureType == QStringLiteral("rms"))
            vcg::tri::UpdateQuality<VCGMesh>::VertexRMSCurvatureFromHGAttribute(mesh);
        else
            vcg::tri::UpdateQuality<VCGMesh>::VertexAbsoluteCurvatureFromHGAttribute(mesh);

        ensureVertexQuality(entry);
        Histogramf hist;
        vcg::tri::Stat<VCGMesh>::ComputePerVertexQualityHistogram(mesh, hist);
        markGeometry(doc, meshIndex, QObject::tr("Computed discrete curvature on '%1'").arg(meshLabel(entry, meshIndex)));
        return qualitySuccess(meshIndex, MeshFilterVisualizationAttribute::VertexQuality, {
            QObject::tr("Curvature Range: %1 %2 (Used 10/90 percentiles %3 %4)")
                .arg(hist.MinV())
                .arg(hist.MaxV())
                .arg(hist.Percentile(0.1f))
                .arg(hist.Percentile(0.9f))
        });
    }

    if (filterId == QString::fromLatin1(kFilterTriangleQuality)) {
        ensureFaceQuality(entry);
        Scalar minV = 0;
        Scalar maxV = 1;
        Distributionf distrib;
        const QString metricId = params.getEnum(QStringLiteral("Metric"), QStringLiteral("area_max_side"));
        const bool useWedgeTex = (entry.ioMask & Mask::IOM_WEDGTEXCOORD) != 0;
        const bool hasAnyTex = (entry.ioMask & (Mask::IOM_WEDGTEXCOORD | Mask::IOM_VERTTEXCOORD)) != 0;

        if ((metricId == QStringLiteral("texture_angle_distortion") || metricId == QStringLiteral("texture_area_distortion")) && !hasAnyTex)
            return fail(QObject::tr("This metric requires texture coordinates."));
        if ((metricId == QStringLiteral("polygonal_planarity_max") || metricId == QStringLiteral("polygonal_planarity_relative"))
            && (entry.ioMask & Mask::IOM_BITPOLYGONAL) == 0) {
            return fail(QObject::tr("This metric is meaningless for triangle-only meshes (all faces are planar by definition)."));
        }

        for (VCGFace &face : mesh.face) {
            if (face.IsD())
                continue;
            if (metricId == QStringLiteral("area_max_side"))
                face.Q() = vcg::Quality(face.P(0), face.P(1), face.P(2));
            else if (metricId == QStringLiteral("inradius_circumradius"))
                face.Q() = vcg::QualityRadii(face.P(0), face.P(1), face.P(2));
            else if (metricId == QStringLiteral("mean_ratio"))
                face.Q() = vcg::QualityMeanRatio(face.P(0), face.P(1), face.P(2));
            else if (metricId == QStringLiteral("area"))
                face.Q() = vcg::DoubleArea(face) * 0.5f;
            else if (metricId == QStringLiteral("texture_angle_distortion"))
                face.Q() = useWedgeTex
                    ? vcg::tri::Distortion<VCGMesh, true>::AngleDistortion(&face)
                    : vcg::tri::Distortion<VCGMesh, false>::AngleDistortion(&face);
            else if (metricId == QStringLiteral("texture_area_distortion")) {
                Scalar areaScaleVal = 0, edgeScaleVal = 0;
                if (useWedgeTex) {
                    vcg::tri::Distortion<VCGMesh, true>::MeshScalingFactor(mesh, areaScaleVal, edgeScaleVal);
                    face.Q() = vcg::tri::Distortion<VCGMesh, true>::AreaDistortion(&face, areaScaleVal);
                } else {
                    vcg::tri::Distortion<VCGMesh, false>::MeshScalingFactor(mesh, areaScaleVal, edgeScaleVal);
                    face.Q() = vcg::tri::Distortion<VCGMesh, false>::AreaDistortion(&face, areaScaleVal);
                }
            }
        }

        if (metricId == QStringLiteral("area")) {
            vcg::tri::Stat<VCGMesh>::ComputePerFaceQualityMinMax(mesh, minV, maxV);
        } else if (metricId == QStringLiteral("polygonal_planarity_max") || metricId == QStringLiteral("polygonal_planarity_relative")) {
            vcg::tri::UpdateFlags<VCGMesh>::FaceClearV(mesh);
            std::vector<VCGMesh::VertexPointer> vertVec;
            std::vector<VCGMesh::FacePointer> faceVec;
            for (size_t i = 0; i < mesh.face.size(); ++i) {
                if (mesh.face[i].IsV())
                    continue;
                vcg::tri::PolygonSupport<VCGMesh, VCGMesh>::ExtractPolygon(&mesh.face[i], vertVec, faceVec);
                std::vector<VCGMesh::CoordType> points;
                points.reserve(vertVec.size());
                for (VCGMesh::VertexPointer vp : vertVec)
                    points.push_back(vp->P());
                vcg::Plane3f plane;
                vcg::FitPlaneToPointSet(points, plane);
                float maxDist = 0.0f;
                float sumDist = 0.0f;
                float halfPerim = 0.0f;
                for (size_t j = 0; j < points.size(); ++j) {
                    const float d = std::fabs(vcg::SignedDistancePlanePoint(plane, points[j]));
                    sumDist += d;
                    maxDist = std::max(maxDist, d);
                    halfPerim += vcg::Distance(points[j], points[(j + 1) % points.size()]);
                }
                const float avgDist = sumDist / std::max<size_t>(1, points.size());
                for (VCGMesh::FacePointer fp : faceVec)
                    fp->Q() = (metricId == QStringLiteral("polygonal_planarity_max")) ? maxDist : (avgDist / std::max(1e-12f, halfPerim));
            }
            vcg::tri::Stat<VCGMesh>::ComputePerFaceQualityDistribution(mesh, distrib);
            minV = distrib.Percentile(0.05f);
            maxV = distrib.Percentile(0.95f);
        } else {
            vcg::tri::Stat<VCGMesh>::ComputePerFaceQualityDistribution(mesh, distrib);
            minV = distrib.Percentile(0.05f);
            maxV = distrib.Percentile(0.95f);
        }

        markGeometry(doc, meshIndex, QObject::tr("Computed per-face quality on '%1'").arg(meshLabel(entry, meshIndex)));
        return qualitySuccess(meshIndex, MeshFilterVisualizationAttribute::FaceQuality);
    }

    if (filterId == QString::fromLatin1(kFilterRandomConnected)) {
        vcg::tri::UpdateColor<VCGMesh>::PerFaceRandomConnectedComponent(mesh);
        ensureFaceColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Assigned random connected-component colors on '%1'").arg(meshLabel(entry, meshIndex)));
        return success();
    }

    if (filterId == QString::fromLatin1(kFilterRandomFace)) {
        vcg::tri::UpdateColor<VCGMesh>::PerFaceRandom(mesh);
        ensureFaceColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Assigned random face colors on '%1'").arg(meshLabel(entry, meshIndex)));
        return success();
    }

    if (filterId == QString::fromLatin1(kFilterVertexSmooth)) {
        vcg::tri::Smooth<VCGMesh>::VertexColorLaplacian(mesh, params.getInt(QStringLiteral("iteration"), 1), false, cb);
        ensureVertexColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Smoothed vertex colors on '%1'").arg(meshLabel(entry, meshIndex)));
        return success();
    }

    if (filterId == QString::fromLatin1(kFilterFaceSmooth)) {
        vcg::tri::Smooth<VCGMesh>::FaceColorLaplacian(mesh, params.getInt(QStringLiteral("iteration"), 1), false, cb);
        ensureFaceColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Smoothed face colors on '%1'").arg(meshLabel(entry, meshIndex)));
        return success();
    }

    if (filterId == QString::fromLatin1(kFilterFaceToVertex)) {
        vcg::tri::UpdateColor<VCGMesh>::PerVertexFromFace(mesh);
        ensureVertexColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Transferred face colors to vertices on '%1'").arg(meshLabel(entry, meshIndex)));
        return success();
    }

    if (filterId == QString::fromLatin1(kFilterMeshToFace)) {
        const bool allVisible = params.getBool(QStringLiteral("allVisibleMesh"), false);
        int changed = 0;
        for (int i = 0; i < doc.meshCount(); ++i) {
            if (!allVisible && i != meshIndex)
                continue;
            Document::MeshEntry &targetEntry = doc.mesh(i);
            if (allVisible && !targetEntry.visible)
                continue;
            vcg::tri::UpdateColor<VCGMesh>::PerFaceConstant(targetEntry.mesh, targetEntry.mesh.C());
            ensureFaceColor(targetEntry);
            markGeometry(doc, i, QObject::tr("Transferred mesh color to faces on '%1'").arg(meshLabel(targetEntry, i)));
            ++changed;
        }
        return success({ QObject::tr("Transferred mesh colors to faces on %1 mesh layer(s).").arg(changed) });
    }

    if (filterId == QString::fromLatin1(kFilterVertexToFace)) {
        vcg::tri::UpdateColor<VCGMesh>::PerFaceFromVertex(mesh);
        ensureFaceColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Transferred vertex colors to faces on '%1'").arg(meshLabel(entry, meshIndex)));
        return success();
    }

    if (filterId == QString::fromLatin1(kFilterTextureToVertex)) {
        const int textureCount = Document::meshTextureAssociationCount(entry);
        if (textureCount <= 0)
            return fail(QObject::tr("Current mesh has no associated textures."));

        const int requestedTextureSlot = params.getTextureRef(QStringLiteral("sourceTexture"), 0);
        if (requestedTextureSlot < 0)
            return fail(QObject::tr("Source Texture must be automatic or a positive selection."));
        if (requestedTextureSlot > textureCount) {
            return fail(
                QObject::tr("Source Texture %1 is out of range. The current mesh has %2 associated texture(s).")
                    .arg(requestedTextureSlot)
                    .arg(textureCount));
        }

        std::vector<QImage> images(static_cast<size_t>(textureCount), QImage());
        for (int textureIndex = 0; textureIndex < textureCount; ++textureIndex) {
            QString textureError;
            if (!Tex::loadAssociatedTextureImage(entry, textureIndex, images[size_t(textureIndex)], textureError))
                return fail(textureError);
        }

        for (VCGFace &face : mesh.face) {
            if (face.IsD())
                continue;
            for (int k = 0; k < 3; ++k) {
                const int texIndex = requestedTextureSlot > 0
                    ? requestedTextureSlot - 1
                    : int(face.WT(k).N());
                if (texIndex >= 0 && texIndex < textureCount) {
                    const QRgb value = sampleWrappedTexture(images[size_t(texIndex)], face.WT(k).P());
                    face.V(k)->C() = vcg::Color4b(qRed(value), qGreen(value), qBlue(value), 255);
                } else {
                    face.V(k)->C() = vcg::Color4b(255, 255, 255, 255);
                }
            }
        }
        ensureVertexColor(entry);
        markGeometry(doc, meshIndex, QObject::tr("Transferred texture colors to vertices on '%1'").arg(meshLabel(entry, meshIndex)));
        if (requestedTextureSlot > 0) {
            return success({
                QObject::tr("Transferred texture slot %1 to vertex colors.").arg(requestedTextureSlot)
            });
        }
        return success({
            QObject::tr("Transferred texture colors to vertices using per-face texture assignments.")
        });
    }

    if (filterId == QString::fromLatin1(kFilterVertexToFaceQuality)) {
        vcg::tri::UpdateQuality<VCGMesh>::FaceFromVertex(mesh);
        ensureFaceQuality(entry);
        markGeometry(doc, meshIndex, QObject::tr("Transferred vertex quality to faces on '%1'").arg(meshLabel(entry, meshIndex)));
        return qualitySuccess(meshIndex, MeshFilterVisualizationAttribute::FaceQuality);
    }

    if (filterId == QString::fromLatin1(kFilterFaceToVertexQuality)) {
        vcg::tri::UpdateQuality<VCGMesh>::VertexFromFace(mesh, params.getBool(QStringLiteral("areaWeight"), true));
        ensureVertexQuality(entry);
        markGeometry(doc, meshIndex, QObject::tr("Transferred face quality to vertices on '%1'").arg(meshLabel(entry, meshIndex)));
        return qualitySuccess(meshIndex, MeshFilterVisualizationAttribute::VertexQuality);
    }

    return fail(QObject::tr("Unknown filter id: %1").arg(filterId));
}

void registerColorProcFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<ColorProcFilterPlugin>());
}
