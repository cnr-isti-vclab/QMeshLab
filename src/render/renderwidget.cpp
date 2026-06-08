#include "renderwidget.h"
#include "colormap.h"
#include "document.h"
#include "qualityrange.h"
#include "renderoverlaypanel.h"
#include <wrap/io_trimesh/io_mask.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QtMath>
#include <QWheelEvent>
#include <QVector3D>
#include <QVector4D>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

bool fuzzyFloatEqual(float a, float b, float eps = 1e-6f)
{
    return std::abs(a - b) <= eps;
}

bool fuzzyVec3Equal(const QVector3D &a, const QVector3D &b, float eps = 1e-6f)
{
    return fuzzyFloatEqual(a.x(), b.x(), eps)
        && fuzzyFloatEqual(a.y(), b.y(), eps)
        && fuzzyFloatEqual(a.z(), b.z(), eps);
}

bool fuzzyQuatEqual(const QQuaternion &a, const QQuaternion &b, float eps = 1e-6f)
{
    return fuzzyFloatEqual(a.x(), b.x(), eps)
        && fuzzyFloatEqual(a.y(), b.y(), eps)
        && fuzzyFloatEqual(a.z(), b.z(), eps)
        && fuzzyFloatEqual(a.scalar(), b.scalar(), eps);
}

bool fuzzyStateEqual(const ViewTrackball::State &a, const ViewTrackball::State &b)
{
    return fuzzyVec3Equal(a.center, b.center)
        && fuzzyQuatEqual(a.rotation, b.rotation)
        && fuzzyFloatEqual(a.distance, b.distance)
        && fuzzyFloatEqual(a.radius, b.radius)
        && fuzzyFloatEqual(a.fovYDeg, b.fovYDeg)
        && fuzzyFloatEqual(a.nearClipRatio, b.nearClipRatio)
        && fuzzyFloatEqual(a.gizmoBaseRadius, b.gizmoBaseRadius)
        && fuzzyFloatEqual(a.gizmoReferenceDistance, b.gizmoReferenceDistance)
        && fuzzyFloatEqual(a.gizmoReferenceFovYDeg, b.gizmoReferenceFovYDeg);
}

QString quickHelpOverlayHtml()
{
    static QString cached;
    if (!cached.isEmpty())
        return cached;

    QFile file(QStringLiteral(":/resources/quick_help_overlay.html"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        cached = QString::fromUtf8(file.readAll());

    if (cached.isEmpty()) {
        cached = QStringLiteral(
            "<div style='font-size:11px; line-height:1.35;'>"
            "<div style='font-size:14px; font-weight:600; margin-bottom:6px;'>Quick Help</div>"
            "<div>Quick help resource could not be loaded.</div>"
            "</div>");
    }

    return cached;
}

RenderQualityRange automaticQualityRangeForCurrentMesh(
    const Document *doc,
    const RenderSettings &settings)
{
    if (!doc)
        return {};

    const int meshIndex = doc->currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= doc->meshCount())
        return {};

    const auto &entry = doc->mesh(meshIndex);
    const int mask = entry.ioMask;
    const bool hasVertexQuality = (mask & vcg::tri::io::Mask::IOM_VERTQUALITY) != 0;
    const bool hasFaceQuality = (mask & vcg::tri::io::Mask::IOM_FACEQUALITY) != 0;

    bool useVertexQuality = false;
    bool useFaceQuality = false;
    switch (settings.qualityHistogramSource) {
    case QualityHistogramSource::Auto:
        useVertexQuality = hasVertexQuality;
        useFaceQuality = !useVertexQuality && hasFaceQuality;
        break;
    case QualityHistogramSource::VertexQuality:
        useVertexQuality = hasVertexQuality;
        break;
    case QualityHistogramSource::FaceQuality:
        useFaceQuality = hasFaceQuality;
        break;
    }
    if (!useVertexQuality && !useFaceQuality)
        return {};

    const VCGMesh &mesh = entry.mesh;
    std::vector<float> values;
    values.reserve(useVertexQuality ? size_t(mesh.VN()) : size_t(mesh.FN()));
    if (useVertexQuality) {
        for (int vi = 0; vi < mesh.VN(); ++vi) {
            const auto &v = mesh.vert[vi];
            if (!v.IsD())
                values.push_back(static_cast<float>(v.cQ()));
        }
    } else {
        for (int fi = 0; fi < mesh.FN(); ++fi) {
            const auto &f = mesh.face[fi];
            if (!f.IsD())
                values.push_back(static_cast<float>(f.cQ()));
        }
    }

    return sampledRenderQualityRange(
        std::move(values),
        settings.qualityHistogramCenterOnZero,
        settings.qualityHistogramPercentileCrop);
}

QString qualityBakeFilterEnumColorMap(const QString &mapId)
{
    const QString id = mapId.trimmed().toLower();
    static const QStringList knownIds = {
        QStringLiteral("rgb"),
        QStringLiteral("rainbow"),
        QStringLiteral("gray"),
        QStringLiteral("viridis"),
        QStringLiteral("plasma"),
        QStringLiteral("cividis"),
        QStringLiteral("turbo"),
        QStringLiteral("rdpu"),
        QStringLiteral("constant")
    };
    return knownIds.contains(id) ? id : QStringLiteral("rainbow");
}

QJsonArray vec3ToJsonArray(const QVector3D &v)
{
    return QJsonArray{v.x(), v.y(), v.z()};
}

QJsonArray quatToJsonArray(const QQuaternion &q)
{
    return QJsonArray{q.x(), q.y(), q.z(), q.scalar()};
}

bool parseFloatValue(const QJsonValue &value, float &outValue)
{
    if (!value.isDouble())
        return false;
    outValue = float(value.toDouble());
    return std::isfinite(outValue);
}

bool parseVec3Value(const QJsonValue &value, QVector3D &outValue)
{
    if (!value.isArray())
        return false;
    const QJsonArray arr = value.toArray();
    if (arr.size() != 3)
        return false;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (!parseFloatValue(arr[0], x) || !parseFloatValue(arr[1], y) || !parseFloatValue(arr[2], z))
        return false;
    outValue = QVector3D(x, y, z);
    return true;
}

bool parseQuatXyzwValue(const QJsonValue &value, QQuaternion &outValue)
{
    if (!value.isArray())
        return false;
    const QJsonArray arr = value.toArray();
    if (arr.size() != 4)
        return false;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
    if (!parseFloatValue(arr[0], x)
        || !parseFloatValue(arr[1], y)
        || !parseFloatValue(arr[2], z)
        || !parseFloatValue(arr[3], w)) {
        return false;
    }
    outValue = QQuaternion(w, x, y, z);
    return true;
}

QJsonArray colorToJsonArray(const QColor &c)
{
    return QJsonArray{c.red(), c.green(), c.blue(), c.alpha()};
}

bool parseColorArray(const QJsonValue &value, QColor &outValue)
{
    if (!value.isArray())
        return false;
    const QJsonArray arr = value.toArray();
    if (arr.size() != 4)
        return false;
    int rgba[4] = {};
    for (int i = 0; i < 4; ++i) {
        if (!arr[i].isDouble())
            return false;
        const int v = int(std::lround(arr[i].toDouble()));
        if (v < 0 || v > 255)
            return false;
        rgba[i] = v;
    }
    outValue = QColor(rgba[0], rgba[1], rgba[2], rgba[3]);
    return true;
}

QString viewModeToJson(RenderWidget::ViewMode mode)
{
    switch (mode) {
    case RenderWidget::ViewMode::Scene3D:
        return QStringLiteral("Scene3D");
    case RenderWidget::ViewMode::ParametrizationUV:
        return QStringLiteral("ParametrizationUV");
    case RenderWidget::ViewMode::RasterImage:
        return QStringLiteral("RasterImage");
    }
    return QStringLiteral("Scene3D");
}

bool parseViewMode(const QJsonValue &value, RenderWidget::ViewMode &outMode)
{
    if (!value.isString())
        return false;
    const QString s = value.toString();
    if (s == QStringLiteral("Scene3D")) {
        outMode = RenderWidget::ViewMode::Scene3D;
        return true;
    }
    if (s == QStringLiteral("ParametrizationUV")) {
        outMode = RenderWidget::ViewMode::ParametrizationUV;
        return true;
    }
    if (s == QStringLiteral("RasterImage")) {
        outMode = RenderWidget::ViewMode::RasterImage;
        return true;
    }
    return false;
}

QString layerKindToJson(Document::CurrentLayerKind kind)
{
    switch (kind) {
    case Document::CurrentLayerKind::Mesh:
        return QStringLiteral("Mesh");
    case Document::CurrentLayerKind::Raster:
        return QStringLiteral("Raster");
    case Document::CurrentLayerKind::None:
        return QStringLiteral("None");
    }
    return QStringLiteral("None");
}

bool parseLayerKind(const QJsonValue &value, Document::CurrentLayerKind &outKind)
{
    if (!value.isString())
        return false;
    const QString s = value.toString();
    if (s == QStringLiteral("Mesh")) {
        outKind = Document::CurrentLayerKind::Mesh;
        return true;
    }
    if (s == QStringLiteral("Raster")) {
        outKind = Document::CurrentLayerKind::Raster;
        return true;
    }
    if (s == QStringLiteral("None")) {
        outKind = Document::CurrentLayerKind::None;
        return true;
    }
    return false;
}

template <typename EnumT>
int enumToInt(EnumT e)
{
    return static_cast<int>(e);
}

template <typename EnumT>
bool parseEnumInt(const QJsonObject &obj, const QString &key, EnumT &outValue)
{
    if (!obj.contains(key))
        return true;
    const QJsonValue value = obj.value(key);
    if (!value.isDouble())
        return false;
    outValue = static_cast<EnumT>(value.toInt());
    return true;
}

QJsonObject trackballStateToJsonObject(
    const ViewTrackball::State &state,
    const ViewTrackball::State *defaults = nullptr)
{
    QJsonObject trackball;
    auto putVec3 = [&](const QString &key, const QVector3D &value, const QVector3D &def) {
        if (!defaults || !fuzzyVec3Equal(value, def))
            trackball.insert(key, vec3ToJsonArray(value));
    };
    auto putQuat = [&](const QString &key, const QQuaternion &value, const QQuaternion &def) {
        if (!defaults || !fuzzyQuatEqual(value, def))
            trackball.insert(key, quatToJsonArray(value));
    };
    auto putFloat = [&](const QString &key, float value, float def) {
        if (!defaults || !fuzzyFloatEqual(value, def))
            trackball.insert(key, value);
    };

    const ViewTrackball::State def = defaults ? *defaults : ViewTrackball::State{};
    putVec3(QStringLiteral("center"), state.center, def.center);
    putQuat(QStringLiteral("rotation_xyzw"), state.rotation, def.rotation);
    putFloat(QStringLiteral("distance"), state.distance, def.distance);
    putFloat(QStringLiteral("radius"), state.radius, def.radius);
    putFloat(QStringLiteral("fov_y_degrees"), state.fovYDeg, def.fovYDeg);
    putFloat(QStringLiteral("near_clip_ratio"), state.nearClipRatio, def.nearClipRatio);
    putFloat(QStringLiteral("gizmo_base_radius"), state.gizmoBaseRadius, def.gizmoBaseRadius);
    putFloat(
        QStringLiteral("gizmo_reference_distance"),
        state.gizmoReferenceDistance,
        def.gizmoReferenceDistance);
    putFloat(
        QStringLiteral("gizmo_reference_fov_y_degrees"),
        state.gizmoReferenceFovYDeg,
        def.gizmoReferenceFovYDeg);
    return trackball;
}

bool parseTrackballStateObject(const QJsonObject &obj, ViewTrackball::State &outState, QString *error)
{
    auto fail = [&](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };

    if (obj.contains(QStringLiteral("center"))
        && !parseVec3Value(obj.value(QStringLiteral("center")), outState.center))
        return fail(QObject::tr("Invalid render-state JSON: 'trackball.center' must be [x, y, z]."));
    if (obj.contains(QStringLiteral("rotation_xyzw"))
        && !parseQuatXyzwValue(obj.value(QStringLiteral("rotation_xyzw")), outState.rotation)) {
        return fail(QObject::tr("Invalid render-state JSON: 'trackball.rotation_xyzw' must be [x, y, z, w]."));
    }
    if (obj.contains(QStringLiteral("distance"))
        && !parseFloatValue(obj.value(QStringLiteral("distance")), outState.distance)) {
        return fail(QObject::tr("Invalid render-state JSON: 'trackball.distance' must be a number."));
    }
    if (obj.contains(QStringLiteral("radius"))
        && !parseFloatValue(obj.value(QStringLiteral("radius")), outState.radius)) {
        return fail(QObject::tr("Invalid render-state JSON: 'trackball.radius' must be a number."));
    }
    if (obj.contains(QStringLiteral("fov_y_degrees"))
        && !parseFloatValue(obj.value(QStringLiteral("fov_y_degrees")), outState.fovYDeg)) {
        return fail(QObject::tr("Invalid render-state JSON: 'trackball.fov_y_degrees' must be a number."));
    }
    if (obj.contains(QStringLiteral("near_clip_ratio"))
        && !parseFloatValue(obj.value(QStringLiteral("near_clip_ratio")), outState.nearClipRatio)) {
        return fail(QObject::tr("Invalid render-state JSON: 'trackball.near_clip_ratio' must be a number."));
    }
    if (obj.contains(QStringLiteral("gizmo_base_radius"))
        && !parseFloatValue(obj.value(QStringLiteral("gizmo_base_radius")), outState.gizmoBaseRadius)) {
        return fail(QObject::tr("Invalid render-state JSON: 'trackball.gizmo_base_radius' must be a number."));
    }
    if (obj.contains(QStringLiteral("gizmo_reference_distance"))
        && !parseFloatValue(obj.value(QStringLiteral("gizmo_reference_distance")), outState.gizmoReferenceDistance)) {
        return fail(QObject::tr("Invalid render-state JSON: 'trackball.gizmo_reference_distance' must be a number."));
    }
    if (obj.contains(QStringLiteral("gizmo_reference_fov_y_degrees"))
        && !parseFloatValue(obj.value(QStringLiteral("gizmo_reference_fov_y_degrees")), outState.gizmoReferenceFovYDeg)) {
        return fail(QObject::tr("Invalid render-state JSON: 'trackball.gizmo_reference_fov_y_degrees' must be a number."));
    }
    return true;
}

QJsonObject renderSettingsToJsonObject(const RenderSettings &s, const RenderSettings *defaults = nullptr)
{
    QJsonObject o;
    const RenderSettings def = defaults ? *defaults : RenderSettings{};
    auto putBool = [&](const QString &key, bool value, bool defaultValue) {
        if (!defaults || value != defaultValue)
            o.insert(key, value);
    };
    auto putInt = [&](const QString &key, int value, int defaultValue) {
        if (!defaults || value != defaultValue)
            o.insert(key, value);
    };
    auto putFloat = [&](const QString &key, float value, float defaultValue) {
        if (!defaults || !fuzzyFloatEqual(value, defaultValue))
            o.insert(key, value);
    };
    auto putEnum = [&](const QString &key, int value, int defaultValue) {
        if (!defaults || value != defaultValue)
            o.insert(key, value);
    };
    auto putColor = [&](const QString &key, const QColor &value, const QColor &defaultValue) {
        if (!defaults || value != defaultValue)
            o.insert(key, colorToJsonArray(value));
    };
    auto putString = [&](const QString &key, const QString &value, const QString &defaultValue) {
        if (!defaults || value != defaultValue)
            o.insert(key, value);
    };

    putBool(QStringLiteral("highlight_current_mesh"), s.highlightCurrentMesh, def.highlightCurrentMesh);
    putBool(QStringLiteral("show_trackball_gizmo"), s.showTrackballGizmo, def.showTrackballGizmo);
    putBool(QStringLiteral("show_bounding_box_corners"), s.showBoundingBoxCorners, def.showBoundingBoxCorners);
    putBool(
        QStringLiteral("show_bounding_box_dimensions"),
        s.showBoundingBoxDimensions,
        def.showBoundingBoxDimensions);
    putColor(
        QStringLiteral("current_mesh_outline_color"),
        s.currentMeshOutlineColor,
        def.currentMeshOutlineColor);
    putFloat(
        QStringLiteral("current_mesh_outline_width"),
        s.currentMeshOutlineWidth,
        def.currentMeshOutlineWidth);
    putFloat(
        QStringLiteral("current_mesh_dilate_radius"),
        s.currentMeshDilateRadius,
        def.currentMeshDilateRadius);
    putFloat(
        QStringLiteral("current_mesh_erode_radius"),
        s.currentMeshErodeRadius,
        def.currentMeshErodeRadius);
    putEnum(
        QStringLiteral("current_mesh_debug_view"),
        enumToInt(s.currentMeshDebugView),
        enumToInt(def.currentMeshDebugView));
    putBool(QStringLiteral("settings_panel_visible"), s.settingsPanelVisible, def.settingsPanelVisible);
    putEnum(QStringLiteral("current_pass"), enumToInt(s.currentPass), enumToInt(def.currentPass));
    putBool(QStringLiteral("show_quality_histogram"), s.showQualityHistogram, def.showQualityHistogram);
    putBool(
        QStringLiteral("uv_show_reference_frame"),
        s.uvShowReferenceFrame,
        def.uvShowReferenceFrame);
    putBool(QStringLiteral("uv_show_full_texture"), s.uvShowFullTexture, def.uvShowFullTexture);
    putInt(QStringLiteral("uv_texture_index"), s.uvTextureIndex, def.uvTextureIndex);
    putBool(
        QStringLiteral("uv_texture_nearest_sampling"),
        s.uvTextureNearestSampling,
        def.uvTextureNearestSampling);
    putColor(
        QStringLiteral("scene_background_top_color"),
        s.sceneBackgroundTopColor,
        def.sceneBackgroundTopColor);
    putColor(
        QStringLiteral("scene_background_bottom_color"),
        s.sceneBackgroundBottomColor,
        def.sceneBackgroundBottomColor);
    putInt(QStringLiteral("quality_histogram_bins"), s.qualityHistogramBins, def.qualityHistogramBins);
    putEnum(
        QStringLiteral("quality_histogram_source"),
        enumToInt(s.qualityHistogramSource),
        enumToInt(def.qualityHistogramSource));
    putBool(
        QStringLiteral("quality_histogram_fixed_range"),
        s.qualityHistogramFixedRange,
        def.qualityHistogramFixedRange);
    putBool(
        QStringLiteral("quality_histogram_center_on_zero"),
        s.qualityHistogramCenterOnZero,
        def.qualityHistogramCenterOnZero);
    putFloat(
        QStringLiteral("quality_histogram_percentile_crop"),
        s.qualityHistogramPercentileCrop,
        def.qualityHistogramPercentileCrop);
    putFloat(QStringLiteral("quality_histogram_min"), s.qualityHistogramMin, def.qualityHistogramMin);
    putFloat(QStringLiteral("quality_histogram_max"), s.qualityHistogramMax, def.qualityHistogramMax);
    putString(
        QStringLiteral("quality_histogram_colormap_id"),
        s.qualityHistogramColorMapId,
        def.qualityHistogramColorMapId);
    putBool(
        QStringLiteral("quality_histogram_invert_colormap"),
        s.qualityHistogramInvertColorMap,
        def.qualityHistogramInvertColorMap);
    putBool(
        QStringLiteral("quality_isolines_enabled"),
        s.qualityIsolinesEnabled,
        def.qualityIsolinesEnabled);
    putInt(QStringLiteral("quality_isoline_count"), s.qualityIsolineCount, def.qualityIsolineCount);
    return o;
}

bool parseRenderSettingsObject(const QJsonObject &obj, RenderSettings &out, QString *error)
{
    auto fail = [&](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    auto parseBoolField = [&](const char *key, bool &dst) {
        const QString k = QString::fromLatin1(key);
        if (!obj.contains(k))
            return true;
        if (!obj.value(k).isBool())
            return false;
        dst = obj.value(k).toBool();
        return true;
    };
    auto parseIntField = [&](const char *key, int &dst) {
        const QString k = QString::fromLatin1(key);
        if (!obj.contains(k))
            return true;
        if (!obj.value(k).isDouble())
            return false;
        dst = obj.value(k).toInt();
        return true;
    };
    auto parseFloatField = [&](const char *key, float &dst) {
        const QString k = QString::fromLatin1(key);
        if (!obj.contains(k))
            return true;
        return parseFloatValue(obj.value(k), dst);
    };
    auto parseColorField = [&](const char *key, QColor &dst) {
        const QString k = QString::fromLatin1(key);
        if (!obj.contains(k))
            return true;
        return parseColorArray(obj.value(k), dst);
    };

    if (!parseBoolField("highlight_current_mesh", out.highlightCurrentMesh)
        || !parseBoolField("show_trackball_gizmo", out.showTrackballGizmo)
        || !parseBoolField("show_bounding_box_corners", out.showBoundingBoxCorners)
        || !parseBoolField("show_bounding_box_dimensions", out.showBoundingBoxDimensions)
        || !parseColorField("current_mesh_outline_color", out.currentMeshOutlineColor)
        || !parseFloatField("current_mesh_outline_width", out.currentMeshOutlineWidth)
        || !parseFloatField("current_mesh_dilate_radius", out.currentMeshDilateRadius)
        || !parseFloatField("current_mesh_erode_radius", out.currentMeshErodeRadius)
        || !parseBoolField("settings_panel_visible", out.settingsPanelVisible)
        || !parseBoolField("show_quality_histogram", out.showQualityHistogram)
        || !parseBoolField("uv_show_reference_frame", out.uvShowReferenceFrame)
        || !parseBoolField("uv_show_full_texture", out.uvShowFullTexture)
        || !parseIntField("uv_texture_index", out.uvTextureIndex)
        || !parseBoolField("uv_texture_nearest_sampling", out.uvTextureNearestSampling)
        || !parseColorField("scene_background_top_color", out.sceneBackgroundTopColor)
        || !parseColorField("scene_background_bottom_color", out.sceneBackgroundBottomColor)
        || !parseIntField("quality_histogram_bins", out.qualityHistogramBins)
        || !parseBoolField("quality_histogram_fixed_range", out.qualityHistogramFixedRange)
        || !parseBoolField("quality_histogram_center_on_zero", out.qualityHistogramCenterOnZero)
        || !parseFloatField("quality_histogram_percentile_crop", out.qualityHistogramPercentileCrop)
        || !parseFloatField("quality_histogram_min", out.qualityHistogramMin)
        || !parseFloatField("quality_histogram_max", out.qualityHistogramMax)
        || !parseBoolField("quality_histogram_invert_colormap", out.qualityHistogramInvertColorMap)
        || !parseBoolField("quality_isolines_enabled", out.qualityIsolinesEnabled)
        || !parseIntField("quality_isoline_count", out.qualityIsolineCount)) {
        return fail(QObject::tr("Invalid render-state JSON: one or more render_settings fields have invalid types."));
    }

    if (obj.contains(QStringLiteral("quality_histogram_colormap_id"))) {
        const QJsonValue value = obj.value(QStringLiteral("quality_histogram_colormap_id"));
        if (!value.isString()) {
            return fail(QObject::tr("Invalid render-state JSON: 'render_settings.quality_histogram_colormap_id' must be a string."));
        }
        out.qualityHistogramColorMapId = value.toString();
    }

    if (!parseEnumInt(obj, QStringLiteral("current_mesh_debug_view"), out.currentMeshDebugView)
        || !parseEnumInt(obj, QStringLiteral("current_pass"), out.currentPass)
        || !parseEnumInt(obj, QStringLiteral("quality_histogram_source"), out.qualityHistogramSource)) {
        return fail(QObject::tr("Invalid render-state JSON: one or more render_settings enum fields have invalid types."));
    }

    return true;
}

QJsonObject perMeshSettingsToJsonObject(
    const PerMeshRenderSettings &s,
    const PerMeshRenderSettings *defaults = nullptr)
{
    QJsonObject o;
    const PerMeshRenderSettings def = defaults ? *defaults : PerMeshRenderSettings{};
    auto putBool = [&](const QString &key, bool value, bool defaultValue) {
        if (!defaults || value != defaultValue)
            o.insert(key, value);
    };
    auto putInt = [&](const QString &key, int value, int defaultValue) {
        if (!defaults || value != defaultValue)
            o.insert(key, value);
    };
    auto putFloat = [&](const QString &key, float value, float defaultValue) {
        if (!defaults || !fuzzyFloatEqual(value, defaultValue))
            o.insert(key, value);
    };
    auto putEnum = [&](const QString &key, int value, int defaultValue) {
        if (!defaults || value != defaultValue)
            o.insert(key, value);
    };
    auto putColor = [&](const QString &key, const QColor &value, const QColor &defaultValue) {
        if (!defaults || value != defaultValue)
            o.insert(key, colorToJsonArray(value));
    };

    putBool(QStringLiteral("show_bounding_box"), s.showBoundingBox, def.showBoundingBox);
    putBool(QStringLiteral("show_points"), s.showPoints, def.showPoints);
    putBool(QStringLiteral("show_edges"), s.showEdges, def.showEdges);
    putBool(QStringLiteral("show_wire"), s.showWire, def.showWire);
    putBool(QStringLiteral("show_fill"), s.showFill, def.showFill);
    putBool(QStringLiteral("show_selection"), s.showSelection, def.showSelection);
    putBool(
        QStringLiteral("show_selection_vertices"),
        s.showSelectionVertices,
        def.showSelectionVertices);
    putBool(QStringLiteral("show_selection_faces"), s.showSelectionFaces, def.showSelectionFaces);
    putBool(
        QStringLiteral("decorator_vertex_normals"),
        s.decoratorVertexNormals,
        def.decoratorVertexNormals);
    putBool(
        QStringLiteral("decorator_face_normals"),
        s.decoratorFaceNormals,
        def.decoratorFaceNormals);
    putBool(
        QStringLiteral("decorator_boundary_edges"),
        s.decoratorBoundaryEdges,
        def.decoratorBoundaryEdges);
    putBool(
        QStringLiteral("decorator_texture_seams"),
        s.decoratorTextureSeams,
        def.decoratorTextureSeams);
    putBool(
        QStringLiteral("decorator_non_manifold_edges"),
        s.decoratorNonManifoldEdges,
        def.decoratorNonManifoldEdges);
    putBool(
        QStringLiteral("decorator_non_manifold_vertices"),
        s.decoratorNonManifoldVertices,
        def.decoratorNonManifoldVertices);
    putBool(
        QStringLiteral("decorator_curvature_dir"),
        s.decoratorCurvatureDir,
        def.decoratorCurvatureDir);
    putBool(QStringLiteral("point_lighting"), s.pointLighting, def.pointLighting);
    putBool(QStringLiteral("wire_lighting"), s.wireLighting, def.wireLighting);
    putBool(
        QStringLiteral("wire_backface_culling"),
        s.wireBackfaceCulling,
        def.wireBackfaceCulling);
    putBool(QStringLiteral("wire_respect_faux"), s.wireRespectFaux, def.wireRespectFaux);
    putBool(QStringLiteral("fill_lighting"), s.fillLighting, def.fillLighting);
    putBool(
        QStringLiteral("fill_backface_culling"),
        s.fillBackfaceCulling,
        def.fillBackfaceCulling);
    putEnum(
        QStringLiteral("fill_material"),
        enumToInt(s.fillMaterial),
        enumToInt(def.fillMaterial));
    putEnum(
        QStringLiteral("point_color_source"),
        enumToInt(s.pointColorSource),
        enumToInt(def.pointColorSource));
    putColor(
        QStringLiteral("decorator_vertex_normal_color"),
        s.decoratorVertexNormalColor,
        def.decoratorVertexNormalColor);
    putColor(
        QStringLiteral("decorator_face_normal_color"),
        s.decoratorFaceNormalColor,
        def.decoratorFaceNormalColor);
    putColor(
        QStringLiteral("decorator_boundary_edge_color"),
        s.decoratorBoundaryEdgeColor,
        def.decoratorBoundaryEdgeColor);
    putColor(
        QStringLiteral("decorator_texture_seam_color"),
        s.decoratorTextureSeamColor,
        def.decoratorTextureSeamColor);
    putColor(
        QStringLiteral("decorator_non_manifold_edge_color"),
        s.decoratorNonManifoldEdgeColor,
        def.decoratorNonManifoldEdgeColor);
    putColor(
        QStringLiteral("decorator_non_manifold_vertex_color"),
        s.decoratorNonManifoldVertexColor,
        def.decoratorNonManifoldVertexColor);
    putColor(
        QStringLiteral("decorator_curvature_dir_pd1_color"),
        s.decoratorCurvatureDirPD1Color,
        def.decoratorCurvatureDirPD1Color);
    putColor(
        QStringLiteral("decorator_curvature_dir_pd2_color"),
        s.decoratorCurvatureDirPD2Color,
        def.decoratorCurvatureDirPD2Color);
    putFloat(
        QStringLiteral("decorator_boundary_width"),
        s.decoratorBoundaryWidth,
        def.decoratorBoundaryWidth);
    putColor(QStringLiteral("bbox_wire_color"), s.bboxWireColor, def.bboxWireColor);
    putColor(QStringLiteral("point_color"), s.pointColor, def.pointColor);
    putFloat(QStringLiteral("point_size"), s.pointSize, def.pointSize);
    putColor(QStringLiteral("edge_color"), s.edgeColor, def.edgeColor);
    putFloat(QStringLiteral("edge_size"), s.edgeSize, def.edgeSize);
    putColor(QStringLiteral("wire_color"), s.wireColor, def.wireColor);
    putFloat(QStringLiteral("wire_size"), s.wireSize, def.wireSize);
    putColor(QStringLiteral("fill_color"), s.fillColor, def.fillColor);

    QJsonObject plain;
    if (!defaults || s.fillPlain.shading != def.fillPlain.shading)
        plain.insert(QStringLiteral("shading"), enumToInt(s.fillPlain.shading));
    if (!defaults || s.fillPlain.colorSource != def.fillPlain.colorSource)
        plain.insert(QStringLiteral("color_source"), enumToInt(s.fillPlain.colorSource));
    if (!defaults || s.fillPlain.textureIndex != def.fillPlain.textureIndex)
        plain.insert(QStringLiteral("texture_index"), s.fillPlain.textureIndex);
    if (!plain.isEmpty())
        o.insert(QStringLiteral("fill_plain"), plain);

    QJsonObject pbr;
    if (!defaults || s.fillPbr.shading != def.fillPbr.shading)
        pbr.insert(QStringLiteral("shading"), enumToInt(s.fillPbr.shading));
    if (!defaults || s.fillPbr.albedoSource != def.fillPbr.albedoSource)
        pbr.insert(QStringLiteral("albedo_source"), enumToInt(s.fillPbr.albedoSource));
    if (!defaults || s.fillPbr.albedoIndex != def.fillPbr.albedoIndex)
        pbr.insert(QStringLiteral("albedo_index"), s.fillPbr.albedoIndex);
    if (!defaults || s.fillPbr.normalSource != def.fillPbr.normalSource)
        pbr.insert(QStringLiteral("normal_source"), enumToInt(s.fillPbr.normalSource));
    if (!defaults || s.fillPbr.normalIndex != def.fillPbr.normalIndex)
        pbr.insert(QStringLiteral("normal_index"), s.fillPbr.normalIndex);
    if (!defaults || s.fillPbr.normalMapSpace != def.fillPbr.normalMapSpace)
        pbr.insert(QStringLiteral("normal_map_space"), enumToInt(s.fillPbr.normalMapSpace));
    if (!defaults || s.fillPbr.occlusionSource != def.fillPbr.occlusionSource)
        pbr.insert(QStringLiteral("occlusion_source"), enumToInt(s.fillPbr.occlusionSource));
    if (!defaults || s.fillPbr.occlusionIndex != def.fillPbr.occlusionIndex)
        pbr.insert(QStringLiteral("occlusion_index"), s.fillPbr.occlusionIndex);
    if (!defaults || s.fillPbr.roughnessSource != def.fillPbr.roughnessSource)
        pbr.insert(QStringLiteral("roughness_source"), enumToInt(s.fillPbr.roughnessSource));
    if (!defaults || s.fillPbr.roughnessIndex != def.fillPbr.roughnessIndex)
        pbr.insert(QStringLiteral("roughness_index"), s.fillPbr.roughnessIndex);
    if (!defaults || !fuzzyFloatEqual(s.fillPbr.normalScale, def.fillPbr.normalScale))
        pbr.insert(QStringLiteral("normal_scale"), s.fillPbr.normalScale);
    if (!defaults || !fuzzyFloatEqual(s.fillPbr.occlusionStrength, def.fillPbr.occlusionStrength))
        pbr.insert(QStringLiteral("occlusion_strength"), s.fillPbr.occlusionStrength);
    if (!defaults || !fuzzyFloatEqual(s.fillPbr.roughnessFactor, def.fillPbr.roughnessFactor))
        pbr.insert(QStringLiteral("roughness_factor"), s.fillPbr.roughnessFactor);
    if (!pbr.isEmpty())
        o.insert(QStringLiteral("fill_pbr"), pbr);

    QJsonObject rs;
    if (!defaults || s.fillRs.shading != def.fillRs.shading)
        rs.insert(QStringLiteral("shading"), enumToInt(s.fillRs.shading));
    if (!defaults || !fuzzyFloatEqual(s.fillRs.enhancement, def.fillRs.enhancement))
        rs.insert(QStringLiteral("enhancement"), s.fillRs.enhancement);
    if (!defaults || s.fillRs.displayMode != def.fillRs.displayMode)
        rs.insert(QStringLiteral("display_mode"), s.fillRs.displayMode);
    if (!defaults || s.fillRs.invert != def.fillRs.invert)
        rs.insert(QStringLiteral("invert"), s.fillRs.invert);
    if (!rs.isEmpty())
        o.insert(QStringLiteral("fill_rs"), rs);

    return o;
}

bool parsePerMeshSettingsObject(const QJsonObject &obj, PerMeshRenderSettings &out, QString *error)
{
    auto fail = [&](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    auto parseBoolField = [&](const char *key, bool &dst) {
        const QString k = QString::fromLatin1(key);
        if (!obj.contains(k))
            return true;
        if (!obj.value(k).isBool())
            return false;
        dst = obj.value(k).toBool();
        return true;
    };
    auto parseIntField = [&](const char *key, int &dst) {
        const QString k = QString::fromLatin1(key);
        if (!obj.contains(k))
            return true;
        if (!obj.value(k).isDouble())
            return false;
        dst = obj.value(k).toInt();
        return true;
    };
    auto parseFloatField = [&](const char *key, float &dst) {
        const QString k = QString::fromLatin1(key);
        if (!obj.contains(k))
            return true;
        return parseFloatValue(obj.value(k), dst);
    };
    auto parseColorField = [&](const char *key, QColor &dst) {
        const QString k = QString::fromLatin1(key);
        if (!obj.contains(k))
            return true;
        return parseColorArray(obj.value(k), dst);
    };

    if (!parseBoolField("show_bounding_box", out.showBoundingBox)
        || !parseBoolField("show_points", out.showPoints)
        || !parseBoolField("show_edges", out.showEdges)
        || !parseBoolField("show_wire", out.showWire)
        || !parseBoolField("show_fill", out.showFill)
        || !parseBoolField("show_selection", out.showSelection)
        || !parseBoolField("show_selection_vertices", out.showSelectionVertices)
        || !parseBoolField("show_selection_faces", out.showSelectionFaces)
        || !parseBoolField("decorator_vertex_normals", out.decoratorVertexNormals)
        || !parseBoolField("decorator_face_normals", out.decoratorFaceNormals)
        || !parseBoolField("decorator_boundary_edges", out.decoratorBoundaryEdges)
        || !parseBoolField("decorator_texture_seams", out.decoratorTextureSeams)
        || !parseBoolField("decorator_non_manifold_edges", out.decoratorNonManifoldEdges)
        || !parseBoolField("decorator_non_manifold_vertices", out.decoratorNonManifoldVertices)
        || !parseBoolField("decorator_curvature_dir", out.decoratorCurvatureDir)
        || !parseBoolField("point_lighting", out.pointLighting)
        || !parseBoolField("wire_lighting", out.wireLighting)
        || !parseBoolField("wire_backface_culling", out.wireBackfaceCulling)
        || !parseBoolField("wire_respect_faux", out.wireRespectFaux)
        || !parseBoolField("fill_lighting", out.fillLighting)
        || !parseBoolField("fill_backface_culling", out.fillBackfaceCulling)
        || !parseColorField("decorator_vertex_normal_color", out.decoratorVertexNormalColor)
        || !parseColorField("decorator_face_normal_color", out.decoratorFaceNormalColor)
        || !parseColorField("decorator_boundary_edge_color", out.decoratorBoundaryEdgeColor)
        || !parseColorField("decorator_texture_seam_color", out.decoratorTextureSeamColor)
        || !parseColorField("decorator_non_manifold_edge_color", out.decoratorNonManifoldEdgeColor)
        || !parseColorField("decorator_non_manifold_vertex_color", out.decoratorNonManifoldVertexColor)
        || !parseColorField("decorator_curvature_dir_pd1_color", out.decoratorCurvatureDirPD1Color)
        || !parseColorField("decorator_curvature_dir_pd2_color", out.decoratorCurvatureDirPD2Color)
        || !parseFloatField("decorator_boundary_width", out.decoratorBoundaryWidth)
        || !parseColorField("bbox_wire_color", out.bboxWireColor)
        || !parseColorField("point_color", out.pointColor)
        || !parseFloatField("point_size", out.pointSize)
        || !parseColorField("edge_color", out.edgeColor)
        || !parseFloatField("edge_size", out.edgeSize)
        || !parseColorField("wire_color", out.wireColor)
        || !parseFloatField("wire_size", out.wireSize)
        || !parseColorField("fill_color", out.fillColor)) {
        return fail(QObject::tr("Invalid render-state JSON: one or more mesh_render_modes fields have invalid types."));
    }

    if (!parseEnumInt(obj, QStringLiteral("fill_material"), out.fillMaterial)
        || !parseEnumInt(obj, QStringLiteral("point_color_source"), out.pointColorSource)) {
        return fail(QObject::tr("Invalid render-state JSON: one or more mesh_render_modes enum fields have invalid types."));
    }

    if (obj.contains(QStringLiteral("fill_plain"))) {
        const QJsonValue plainVal = obj.value(QStringLiteral("fill_plain"));
        if (!plainVal.isObject())
            return fail(QObject::tr("Invalid render-state JSON: 'fill_plain' must be an object."));
        const QJsonObject plainObj = plainVal.toObject();
        if (!parseEnumInt(plainObj, QStringLiteral("shading"), out.fillPlain.shading)
            || !parseEnumInt(plainObj, QStringLiteral("color_source"), out.fillPlain.colorSource)) {
            return fail(QObject::tr("Invalid render-state JSON: one or more fill_plain fields have invalid types."));
        }
        if (plainObj.contains(QStringLiteral("texture_index"))) {
            if (!plainObj.value(QStringLiteral("texture_index")).isDouble()) {
                return fail(QObject::tr("Invalid render-state JSON: 'fill_plain.texture_index' must be a number."));
            }
            out.fillPlain.textureIndex = plainObj.value(QStringLiteral("texture_index")).toInt();
        }
    }

    if (obj.contains(QStringLiteral("fill_pbr"))) {
        const QJsonValue pbrVal = obj.value(QStringLiteral("fill_pbr"));
        if (!pbrVal.isObject())
            return fail(QObject::tr("Invalid render-state JSON: 'fill_pbr' must be an object."));
        const QJsonObject pbrObj = pbrVal.toObject();
        if (!parseEnumInt(pbrObj, QStringLiteral("shading"), out.fillPbr.shading)
            || !parseEnumInt(pbrObj, QStringLiteral("albedo_source"), out.fillPbr.albedoSource)
            || !parseEnumInt(pbrObj, QStringLiteral("normal_source"), out.fillPbr.normalSource)
            || !parseEnumInt(pbrObj, QStringLiteral("normal_map_space"), out.fillPbr.normalMapSpace)
            || !parseEnumInt(pbrObj, QStringLiteral("occlusion_source"), out.fillPbr.occlusionSource)
            || !parseEnumInt(pbrObj, QStringLiteral("roughness_source"), out.fillPbr.roughnessSource)) {
            return fail(QObject::tr("Invalid render-state JSON: one or more fill_pbr enum fields have invalid types."));
        }
        if (pbrObj.contains(QStringLiteral("albedo_index")))
            out.fillPbr.albedoIndex = pbrObj.value(QStringLiteral("albedo_index")).toInt(out.fillPbr.albedoIndex);
        if (pbrObj.contains(QStringLiteral("normal_index")))
            out.fillPbr.normalIndex = pbrObj.value(QStringLiteral("normal_index")).toInt(out.fillPbr.normalIndex);
        if (pbrObj.contains(QStringLiteral("occlusion_index")))
            out.fillPbr.occlusionIndex = pbrObj.value(QStringLiteral("occlusion_index")).toInt(out.fillPbr.occlusionIndex);
        if (pbrObj.contains(QStringLiteral("roughness_index")))
            out.fillPbr.roughnessIndex = pbrObj.value(QStringLiteral("roughness_index")).toInt(out.fillPbr.roughnessIndex);
        if (pbrObj.contains(QStringLiteral("normal_scale"))
            && !parseFloatValue(pbrObj.value(QStringLiteral("normal_scale")), out.fillPbr.normalScale)) {
            return fail(QObject::tr("Invalid render-state JSON: 'fill_pbr.normal_scale' must be a number."));
        }
        if (pbrObj.contains(QStringLiteral("occlusion_strength"))
            && !parseFloatValue(pbrObj.value(QStringLiteral("occlusion_strength")), out.fillPbr.occlusionStrength)) {
            return fail(QObject::tr("Invalid render-state JSON: 'fill_pbr.occlusion_strength' must be a number."));
        }
        if (pbrObj.contains(QStringLiteral("roughness_factor"))
            && !parseFloatValue(pbrObj.value(QStringLiteral("roughness_factor")), out.fillPbr.roughnessFactor)) {
            return fail(QObject::tr("Invalid render-state JSON: 'fill_pbr.roughness_factor' must be a number."));
        }
    }

    if (obj.contains(QStringLiteral("fill_rs"))) {
        const QJsonValue rsVal = obj.value(QStringLiteral("fill_rs"));
        if (!rsVal.isObject())
            return fail(QObject::tr("Invalid render-state JSON: 'fill_rs' must be an object."));
        const QJsonObject rsObj = rsVal.toObject();
        if (!parseEnumInt(rsObj, QStringLiteral("shading"), out.fillRs.shading)) {
            return fail(QObject::tr("Invalid render-state JSON: 'fill_rs.shading' has invalid type."));
        }
        if (rsObj.contains(QStringLiteral("enhancement"))
            && !parseFloatValue(rsObj.value(QStringLiteral("enhancement")), out.fillRs.enhancement)) {
            return fail(QObject::tr("Invalid render-state JSON: 'fill_rs.enhancement' must be a number."));
        }
        if (rsObj.contains(QStringLiteral("display_mode"))) {
            if (!rsObj.value(QStringLiteral("display_mode")).isDouble())
                return fail(QObject::tr("Invalid render-state JSON: 'fill_rs.display_mode' must be a number."));
            out.fillRs.displayMode = rsObj.value(QStringLiteral("display_mode")).toInt();
        }
        if (rsObj.contains(QStringLiteral("invert"))) {
            if (!rsObj.value(QStringLiteral("invert")).isBool())
                return fail(QObject::tr("Invalid render-state JSON: 'fill_rs.invert' must be a bool."));
            out.fillRs.invert = rsObj.value(QStringLiteral("invert")).toBool();
        }
    }

    return true;
}

double niceTickStep(double roughStep)
{
    if (!std::isfinite(roughStep) || roughStep <= 0.0)
        return 0.0;
    const double exponent = std::floor(std::log10(roughStep));
    const double base = std::pow(10.0, exponent);
    const double fraction = roughStep / base;
    double niceFraction = 1.0;
    if (fraction < 1.5) {
        niceFraction = 1.0;
    } else if (fraction < 2.25) {
        niceFraction = 2.0;
    } else if (fraction < 3.75) {
        niceFraction = 2.5;
    } else if (fraction < 7.5) {
        niceFraction = 5.0;
    } else {
        niceFraction = 10.0;
    }
    return niceFraction * base;
}

struct NiceTickValues {
    std::vector<double> values;
    double step = 0.0;
};

NiceTickValues buildNiceTickValues(double minValue, double maxValue, int targetCount)
{
    NiceTickValues out;
    if (!std::isfinite(minValue) || !std::isfinite(maxValue))
        return out;
    if (targetCount < 2)
        targetCount = 2;
    if (maxValue < minValue)
        std::swap(minValue, maxValue);

    const double range = maxValue - minValue;
    if (!(range > 0.0)) {
        out.values = {minValue, maxValue};
        return out;
    }

    const double roughStep = range / double(std::max(1, targetCount - 1));
    const double step = niceTickStep(roughStep);
    out.step = (step > 0.0) ? step : roughStep;

    std::vector<double> interior;
    const double eps = range * 1e-9 + 1e-12;
    if (out.step > 0.0) {
        double v = std::ceil((minValue + eps) / out.step) * out.step;
        for (; v < maxValue - eps; v += out.step) {
            interior.push_back(v);
            if (interior.size() > 4096)
                break;
        }
    }

    out.values.reserve(64);
    out.values.push_back(minValue);
    if (!interior.empty())
        out.values.insert(out.values.end(), interior.begin(), interior.end());
    out.values.push_back(maxValue);
    return out;
}

int decimalsForStep(double step)
{
    if (!std::isfinite(step) || step <= 0.0)
        return 3;
    step = std::abs(step);
    for (int decimals = 0; decimals <= 5; ++decimals) {
        const double scaled = step * std::pow(10.0, decimals);
        if (std::abs(scaled - std::round(scaled)) < 1e-6)
            return decimals;
    }
    return 4;
}
}

RenderWidget::RenderWidget(Document *doc, QWidget *parent)
    : QRhiWidget(parent), m_doc(doc)
{
    m_currentViewIndicator = new QWidget(this);
    m_currentViewIndicator->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_currentViewIndicator->setStyleSheet(QStringLiteral(
        "QWidget {"
        "  background: transparent;"
        "  border: 1px solid rgba(0,174,255,240);"
        "  border-radius: 4px;"
        "}"));
    m_currentViewIndicator->hide();

    createOverlayButtons();
    ensureVisibilitySize();
    syncPerMeshRenderModesWithDocument();
    syncOverlaySettingsToCurrentMesh();
    refreshColorSourceAvailability();

    connect(m_doc, &Document::meshAdded, this, [this](int index) {
        if (index >= 0 && index <= int(m_meshVisibility.size()))
            m_meshVisibility.insert(m_meshVisibility.begin() + index, true);
        else
            ensureVisibilitySize();
        if (!m_doc->isRestoringUndoRedo())
            m_reframeCameraRequested = true;
        syncPerMeshRenderModesWithDocument();
        syncOverlaySettingsToCurrentMesh();
        refreshColorSourceAvailability();
        m_textureSrbs.clear();
        syncUvCacheWithDocument();
        m_uvFitRequested = true;
        m_qualityHistogram.valid = false;
        updateBoundingBoxCornersOverlay();
        updateQualityHistogramOverlay();
        layoutOverlayButtons();
        update();
    });
    connect(m_doc, &Document::meshRemoved, this, [this](int index) {
        if (index >= 0 && index < int(m_meshVisibility.size()))
            m_meshVisibility.erase(m_meshVisibility.begin() + index);
        else
            ensureVisibilitySize();
        if (!m_doc->isRestoringUndoRedo())
            m_reframeCameraRequested = true;
        syncPerMeshRenderModesWithDocument();
        syncOverlaySettingsToCurrentMesh();
        refreshColorSourceAvailability();
        m_textureSrbs.clear();
        syncUvCacheWithDocument();
        m_uvFitRequested = true;
        m_qualityHistogram.valid = false;
        updateBoundingBoxCornersOverlay();
        updateQualityHistogramOverlay();
        layoutOverlayButtons();
        update();
    });
    connect(m_doc, &Document::currentMeshChanged, this, [this](int) {
        syncPerMeshRenderModesWithDocument();
        syncOverlaySettingsToCurrentMesh();
        refreshColorSourceAvailability();
        m_uvFitRequested = true;
        updateQualityHistogramOverlay();
        update();
    });
    connect(m_doc, &Document::currentLayerChanged, this, [this](Document::CurrentLayerKind, int) {
        update();
    });
    connect(m_doc, &Document::meshDataChanged, this, [this](int) {
        syncPerMeshRenderModesWithDocument();
        syncOverlaySettingsToCurrentMesh();
        refreshColorSourceAvailability();
        m_textureSrbs.clear();
        syncUvCacheWithDocument();
        m_qualityHistogram.valid = false;
        updateBoundingBoxCornersOverlay();
        updateQualityHistogramOverlay();
        update();
    });
    connect(m_doc, &Document::rasterAdded, this, [this](int) {
        syncRasterCacheWithDocument();
        update();
    });
    connect(m_doc, &Document::rasterRemoved, this, [this](int) {
        syncRasterCacheWithDocument();
        update();
    });
    connect(m_doc, &Document::rasterVisibilityChanged, this, [this](int, bool) {
        update();
    });
    connect(m_doc, &Document::currentRasterChanged, this, [this](int index) {
        // Exit RasterImage mode if the active raster was removed and there is
        // no valid replacement. Selecting a mesh does NOT exit this mode.
        if (m_viewMode == ViewMode::RasterImage && index < 0) {
            m_viewMode = ViewMode::Scene3D;
            if (m_overlayPanel)
                m_overlayPanel->setViewerModeUv(false);
            updateBoundingBoxCornersOverlay();
        }
        update();
    });
    connect(m_doc, &Document::rasterDataChanged, this, [this](int) {
        syncRasterCacheWithDocument();
        update();
    });

    if (m_currentViewIndicator)
        m_currentViewIndicator->setGeometry(rect().adjusted(1, 1, -1, -1));
}

void RenderWidget::ensureVisibilitySize()
{
    const int targetSize = m_doc ? m_doc->meshCount() : 0;
    if (targetSize <= 0) {
        m_meshVisibility.clear();
        return;
    }
    if (int(m_meshVisibility.size()) < targetSize)
        m_meshVisibility.resize(size_t(targetSize), true);
    else if (int(m_meshVisibility.size()) > targetSize)
        m_meshVisibility.resize(size_t(targetSize));
}

void RenderWidget::setRenderSettings(const RenderSettings &settings)
{
    const RenderSettings prev = m_renderSettings;
    m_renderSettings = settings;
    if (prev.qualityHistogramBins != m_renderSettings.qualityHistogramBins
        || prev.qualityHistogramSource != m_renderSettings.qualityHistogramSource
        || prev.qualityHistogramFixedRange != m_renderSettings.qualityHistogramFixedRange
        || prev.qualityHistogramCenterOnZero != m_renderSettings.qualityHistogramCenterOnZero
        || prev.qualityHistogramPercentileCrop != m_renderSettings.qualityHistogramPercentileCrop
        || prev.qualityHistogramMin != m_renderSettings.qualityHistogramMin
        || prev.qualityHistogramMax != m_renderSettings.qualityHistogramMax
        || prev.qualityHistogramColorMapId != m_renderSettings.qualityHistogramColorMapId
        || prev.qualityHistogramInvertColorMap != m_renderSettings.qualityHistogramInvertColorMap) {
        m_qualityHistogram.valid = false;
    }
    syncOverlaySettingsToCurrentMesh();
    if (m_overlayPanel)
        m_overlayPanel->setGlobalSettings(m_renderSettings);
    updateBoundingBoxCornersOverlay();
    updateQualityHistogramOverlay();
    layoutOverlayButtons();
    update();
}

bool RenderWidget::meshVisible(int index) const
{
    if (index < 0 || index >= int(m_meshVisibility.size()))
        return true;
    return m_meshVisibility[size_t(index)];
}

void RenderWidget::setMeshVisible(int index, bool visible)
{
    ensureVisibilitySize();
    if (index < 0 || index >= int(m_meshVisibility.size()))
        return;
    const size_t idx = size_t(index);
    if (m_meshVisibility[idx] == visible)
        return;
    m_meshVisibility[idx] = visible;
    updateBoundingBoxCornersOverlay();
    update();
}

void RenderWidget::setMeshVisibilityState(const std::vector<bool> &visibility)
{
    ensureVisibilitySize();
    const size_t n = m_meshVisibility.size();
    bool changed = false;
    for (size_t i = 0; i < n; ++i) {
        const bool v = (i < visibility.size()) ? visibility[i] : true;
        if (m_meshVisibility[i] != v) {
            m_meshVisibility[i] = v;
            changed = true;
        }
    }
    if (!changed)
        return;
    updateBoundingBoxCornersOverlay();
    update();
}

void RenderWidget::copyPerMeshRenderModesFrom(const RenderWidget *other)
{
    if (!other || other == this)
        return;
    m_meshRenderModes = other->m_meshRenderModes;
    syncPerMeshRenderModesWithDocument();
    syncOverlaySettingsToCurrentMesh();
    update();
}

ViewState RenderWidget::captureViewState() const
{
    ViewState vs;
    vs.trackball      = m_trackball.state();
    vs.renderSettings = m_renderSettings;
    vs.meshRenderModes = m_meshRenderModes;
    return vs;
}

void RenderWidget::applySynchronizedTrackballState(const ViewTrackball::State &state)
{
    if (m_viewMode != ViewMode::Scene3D)
        return;
    cancelCenterAnimation();
    m_trackball.setState(state);
    m_reframeCameraRequested = false;
    m_resetTrackballRequested = false;
    m_lastBroadcastTrackballState = state;
    m_lastBroadcastTrackballStateValid = true;
    updateBoundingBoxCornersOverlay();
    update();
}

void RenderWidget::restoreViewState(const ViewState &vs, bool restoreCamera)
{
    if (restoreCamera) {
        m_trackball.setState(vs.trackball);
        m_lastBroadcastTrackballState = vs.trackball;
        m_lastBroadcastTrackballStateValid = true;
        // Prevent the pending reframe from overriding the restored camera.
        m_reframeCameraRequested = false;
        m_resetTrackballRequested = false;
        cancelCenterAnimation();
    }

    m_meshRenderModes = vs.meshRenderModes;
    setRenderSettings(vs.renderSettings);   // also updates overlay panel + calls update()
    syncOverlaySettingsToCurrentMesh();
    update();
}

void RenderWidget::resetCameraToScene()
{
    if (m_viewMode == ViewMode::ParametrizationUV) {
        m_uvFitRequested = true;
        update();
        return;
    }
    cancelCenterAnimation();
    m_reframeCameraRequested = true;
    m_resetTrackballRequested = true;
    update();
}

QString RenderWidget::cameraStateJson() const
{
    const ViewTrackball::State state = m_trackball.state();
    const ViewTrackball::State defaultState;
    const QJsonObject trackball = trackballStateToJsonObject(state, &defaultState);

    QJsonObject root;
    root.insert(QStringLiteral("kind"), QStringLiteral("QMeshLab.CameraState"));
    root.insert(QStringLiteral("version"), 1);
    if (!trackball.isEmpty())
        root.insert(QStringLiteral("trackball"), trackball);

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool RenderWidget::applyCameraStateJson(const QString &jsonText, QString *errorMessage)
{
    auto fail = [&](const QString &msg) {
        if (errorMessage)
            *errorMessage = msg;
        return false;
    };

    const QString trimmed = jsonText.trimmed();
    if (trimmed.isEmpty())
        return fail(tr("Clipboard is empty."));

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(tr("Invalid JSON: %1").arg(parseError.errorString()));
    }
    if (!doc.isObject())
        return fail(tr("Invalid camera JSON: root must be an object."));

    const QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("kind"))) {
        const QString kind = root.value(QStringLiteral("kind")).toString();
        if (!kind.isEmpty()
            && kind != QStringLiteral("QMeshLab.CameraState")
            && kind != QStringLiteral("QMeshLab.CameraTrackballState")) {
            return fail(tr("Unsupported camera JSON kind: %1").arg(kind));
        }
    }

    QJsonObject trackballObj = root;
    if (root.contains(QStringLiteral("trackball"))) {
        const QJsonValue trackballValue = root.value(QStringLiteral("trackball"));
        if (!trackballValue.isObject()) {
            return fail(tr("Invalid camera JSON: 'trackball' must be an object."));
        }
        trackballObj = trackballValue.toObject();
    }

    ViewTrackball::State state;
    QString parseErrorMsg;
    if (!parseTrackballStateObject(trackballObj, state, &parseErrorMsg))
        return fail(parseErrorMsg);

    m_trackball.setState(state);
    m_lastBroadcastTrackballState = state;
    m_lastBroadcastTrackballStateValid = true;
    cancelCenterAnimation();
    m_reframeCameraRequested = false;
    m_resetTrackballRequested = false;

    if (errorMessage)
        errorMessage->clear();
    update();
    return true;
}

QString RenderWidget::renderStateJson() const
{
    const ViewTrackball::State defaultTrackball;
    const RenderSettings defaultRenderSettings;
    const PerMeshRenderSettings defaultPerMeshSettings;

    QJsonObject root;
    root.insert(QStringLiteral("kind"), QStringLiteral("QMeshLab.RenderState"));
    root.insert(QStringLiteral("version"), 1);
    if (m_viewMode != ViewMode::Scene3D)
        root.insert(QStringLiteral("view_mode"), viewModeToJson(m_viewMode));

    const float rasterOpacity = std::clamp(m_rasterOpacity, 0.0f, 1.0f);
    if (!fuzzyFloatEqual(rasterOpacity, 1.0f))
        root.insert(QStringLiteral("raster_opacity"), rasterOpacity);

    const QJsonObject trackball = trackballStateToJsonObject(m_trackball.state(), &defaultTrackball);
    if (!trackball.isEmpty())
        root.insert(QStringLiteral("trackball"), trackball);

    const QJsonObject renderSettings = renderSettingsToJsonObject(m_renderSettings, &defaultRenderSettings);
    if (!renderSettings.isEmpty())
        root.insert(QStringLiteral("render_settings"), renderSettings);

    QJsonArray meshVisibility;
    bool hasNonDefaultVisibility = false;
    for (bool v : m_meshVisibility)
        meshVisibility.push_back(v);
    for (bool v : m_meshVisibility) {
        if (!v) {
            hasNonDefaultVisibility = true;
            break;
        }
    }
    if (hasNonDefaultVisibility)
        root.insert(QStringLiteral("mesh_visibility"), meshVisibility);

    std::vector<std::uint64_t> meshIds;
    meshIds.reserve(m_meshRenderModes.size());
    for (const auto &kv : m_meshRenderModes)
        meshIds.push_back(kv.first);
    std::sort(meshIds.begin(), meshIds.end());

    QJsonArray meshRenderModes;
    for (std::uint64_t meshId : meshIds) {
        const auto it = m_meshRenderModes.find(meshId);
        if (it == m_meshRenderModes.end())
            continue;
        const QJsonObject sparseSettings = perMeshSettingsToJsonObject(it->second, &defaultPerMeshSettings);
        if (sparseSettings.isEmpty())
            continue;
        QJsonObject mode;
        mode.insert(QStringLiteral("mesh_id"), QString::number(qulonglong(meshId)));
        mode.insert(QStringLiteral("settings"), sparseSettings);
        meshRenderModes.push_back(mode);
    }
    if (!meshRenderModes.isEmpty())
        root.insert(QStringLiteral("mesh_render_modes"), meshRenderModes);

    const int vpW = qMax(0, size().width());
    const int vpH = qMax(0, size().height());
    if (vpW > 0 || vpH > 0) {
        root.insert(
            QStringLiteral("viewport_px"),
            QJsonArray{vpW, vpH});
    }

    if (m_doc) {
        if (m_doc->currentMeshIndex() >= 0)
            root.insert(QStringLiteral("current_mesh_index"), m_doc->currentMeshIndex());
        if (m_doc->currentRasterIndex() >= 0)
            root.insert(QStringLiteral("current_raster_index"), m_doc->currentRasterIndex());
        if (m_doc->currentLayerKind() != Document::CurrentLayerKind::None) {
            root.insert(
                QStringLiteral("current_layer_kind"),
                layerKindToJson(m_doc->currentLayerKind()));
        }
    }

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool RenderWidget::applyRenderStateJson(const QString &jsonText, QString *errorMessage)
{
    auto fail = [&](const QString &msg) {
        if (errorMessage)
            *errorMessage = msg;
        return false;
    };

    const QString trimmed = jsonText.trimmed();
    if (trimmed.isEmpty())
        return fail(tr("Render-state JSON is empty."));

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(tr("Invalid JSON: %1").arg(parseError.errorString()));
    if (!doc.isObject())
        return fail(tr("Invalid render-state JSON: root must be an object."));

    const QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("kind"))) {
        const QString kind = root.value(QStringLiteral("kind")).toString();
        if (!kind.isEmpty() && kind != QStringLiteral("QMeshLab.RenderState"))
            return fail(tr("Unsupported render-state JSON kind: %1").arg(kind));
    }

    ViewTrackball::State parsedTrackball;
    if (root.contains(QStringLiteral("trackball"))) {
        const QJsonValue value = root.value(QStringLiteral("trackball"));
        if (!value.isObject())
            return fail(tr("Invalid render-state JSON: 'trackball' must be an object."));
        QString parseMsg;
        if (!parseTrackballStateObject(value.toObject(), parsedTrackball, &parseMsg))
            return fail(parseMsg);
    }

    RenderSettings parsedRenderSettings;
    if (root.contains(QStringLiteral("render_settings"))) {
        const QJsonValue value = root.value(QStringLiteral("render_settings"));
        if (!value.isObject())
            return fail(tr("Invalid render-state JSON: 'render_settings' must be an object."));
        QString parseMsg;
        if (!parseRenderSettingsObject(value.toObject(), parsedRenderSettings, &parseMsg))
            return fail(parseMsg);
    }

    std::vector<bool> parsedVisibility;
    if (m_doc)
        parsedVisibility.assign(size_t(std::max(0, m_doc->meshCount())), true);
    else
        parsedVisibility = m_meshVisibility;
    if (root.contains(QStringLiteral("mesh_visibility"))) {
        const QJsonValue value = root.value(QStringLiteral("mesh_visibility"));
        if (!value.isArray())
            return fail(tr("Invalid render-state JSON: 'mesh_visibility' must be an array."));
        parsedVisibility.clear();
        const QJsonArray arr = value.toArray();
        parsedVisibility.reserve(size_t(arr.size()));
        for (const QJsonValue &v : arr) {
            if (!v.isBool())
                return fail(tr("Invalid render-state JSON: 'mesh_visibility' must contain bool values."));
            parsedVisibility.push_back(v.toBool());
        }
    }

    std::unordered_map<std::uint64_t, PerMeshRenderSettings> parsedMeshModes;
    if (m_doc) {
        for (int i = 0; i < m_doc->meshCount(); ++i)
            parsedMeshModes[m_doc->mesh(i).meshId] = PerMeshRenderSettings{};
    }
    if (root.contains(QStringLiteral("mesh_render_modes"))) {
        const QJsonValue value = root.value(QStringLiteral("mesh_render_modes"));
        if (!value.isArray())
            return fail(tr("Invalid render-state JSON: 'mesh_render_modes' must be an array."));
        parsedMeshModes.clear();
        const QJsonArray arr = value.toArray();
        for (const QJsonValue &entryVal : arr) {
            if (!entryVal.isObject())
                return fail(tr("Invalid render-state JSON: each mesh_render_modes entry must be an object."));
            const QJsonObject entry = entryVal.toObject();
            if (!entry.contains(QStringLiteral("mesh_id")) || !entry.value(QStringLiteral("mesh_id")).isString()) {
                return fail(tr("Invalid render-state JSON: mesh_render_modes entries require a string 'mesh_id'."));
            }
            bool ok = false;
            const std::uint64_t meshId = entry.value(QStringLiteral("mesh_id")).toString().toULongLong(&ok);
            if (!ok || meshId == 0)
                return fail(tr("Invalid render-state JSON: mesh_render_modes.mesh_id is invalid."));
            const QJsonValue settingsValue = entry.value(QStringLiteral("settings"));
            if (!settingsValue.isObject())
                return fail(tr("Invalid render-state JSON: mesh_render_modes.settings must be an object."));
            PerMeshRenderSettings settings;
            QString parseMsg;
            if (!parsePerMeshSettingsObject(settingsValue.toObject(), settings, &parseMsg))
                return fail(parseMsg);
            parsedMeshModes[meshId] = settings;
        }
    }

    float parsedRasterOpacity = m_rasterOpacity;
    if (root.contains(QStringLiteral("raster_opacity"))) {
        if (!parseFloatValue(root.value(QStringLiteral("raster_opacity")), parsedRasterOpacity))
            return fail(tr("Invalid render-state JSON: 'raster_opacity' must be a number."));
        parsedRasterOpacity = std::clamp(parsedRasterOpacity, 0.0f, 1.0f);
    }

    ViewMode parsedViewMode = m_viewMode;
    if (root.contains(QStringLiteral("view_mode"))) {
        if (!parseViewMode(root.value(QStringLiteral("view_mode")), parsedViewMode))
            return fail(tr("Invalid render-state JSON: unsupported 'view_mode'."));
    }

    int parsedCurrentMeshIndex = -1;
    int parsedCurrentRasterIndex = -1;
    Document::CurrentLayerKind parsedCurrentLayerKind = Document::CurrentLayerKind::None;
    if (m_doc && root.contains(QStringLiteral("current_mesh_index")))
        parsedCurrentMeshIndex = root.value(QStringLiteral("current_mesh_index")).toInt(-1);
    if (m_doc && root.contains(QStringLiteral("current_raster_index")))
        parsedCurrentRasterIndex = root.value(QStringLiteral("current_raster_index")).toInt(-1);
    if (m_doc && root.contains(QStringLiteral("current_layer_kind"))) {
        if (!parseLayerKind(root.value(QStringLiteral("current_layer_kind")), parsedCurrentLayerKind)) {
            return fail(tr("Invalid render-state JSON: unsupported 'current_layer_kind'."));
        }
    }

    if (m_doc) {
        m_doc->setCurrentMeshIndex(parsedCurrentMeshIndex);
        m_doc->setCurrentRasterIndex(parsedCurrentRasterIndex);
        if (parsedCurrentLayerKind == Document::CurrentLayerKind::Mesh)
            m_doc->setCurrentMeshIndex(m_doc->currentMeshIndex());
        else if (parsedCurrentLayerKind == Document::CurrentLayerKind::Raster)
            m_doc->setCurrentRasterIndex(m_doc->currentRasterIndex());
    }

    m_trackball.setState(parsedTrackball);
    m_lastBroadcastTrackballState = parsedTrackball;
    m_lastBroadcastTrackballStateValid = true;
    cancelCenterAnimation();
    m_reframeCameraRequested = false;
    m_resetTrackballRequested = false;

    m_meshRenderModes = std::move(parsedMeshModes);
    setMeshVisibilityState(parsedVisibility);
    m_rasterOpacity = parsedRasterOpacity;
    setRenderSettings(parsedRenderSettings);
    syncOverlaySettingsToCurrentMesh();

    QString modeError;
    if (!setViewMode(parsedViewMode, &modeError))
        return fail(modeError);

    if (errorMessage)
        errorMessage->clear();
    update();
    return true;
}

CameraShot RenderWidget::cameraShotForViewport(const QSize &pixelSize) const
{
    if (pixelSize.width() <= 0 || pixelSize.height() <= 0)
        return {};

    CameraShot::VcgShot shot;
    shot.Intrinsics.cameraType = vcg::Camera<float>::PERSPECTIVE;
    shot.Intrinsics.ViewportPx = vcg::Point2i(pixelSize.width(), pixelSize.height());
    shot.Intrinsics.CenterPx =
        vcg::Point2f(float(pixelSize.width()) * 0.5f, float(pixelSize.height()) * 0.5f);
    shot.Intrinsics.DistorCenterPx = shot.Intrinsics.CenterPx;
    shot.Intrinsics.PixelSizeMm = vcg::Point2f(1.0f, 1.0f);

    const float fovYRad = qDegreesToRadians(m_trackball.fovYDegrees());
    const float tanHalfFov = std::tan(0.5f * fovYRad);
    if (!(tanHalfFov > 0.0f) || !std::isfinite(tanHalfFov))
        return {};
    shot.Intrinsics.FocalMm = float(pixelSize.height()) / (2.0f * tanHalfFov);

    const QVector3D eye = m_trackball.cameraEyePosition();
    const QVector3D viewDir = m_trackball.cameraViewDirection().normalized();
    const QVector3D up =
        m_trackball.state().rotation.conjugated().rotatedVector(QVector3D(0.0f, 1.0f, 0.0f)).normalized();
    if (viewDir.isNull() || up.isNull())
        return {};

    const QVector3D at = eye + viewDir;
    shot.LookAt(
        eye.x(), eye.y(), eye.z(),
        at.x(), at.y(), at.z(),
        up.x(), up.y(), up.z());
    return CameraShot::fromVcgShot(shot);
}

bool RenderWidget::setViewMode(ViewMode mode, QString *errorMessage)
{
    if (mode == m_viewMode) {
        if (errorMessage)
            errorMessage->clear();
        return true;
    }

    if (mode == ViewMode::ParametrizationUV) {
        const int meshIndex = m_doc ? m_doc->currentMeshIndex() : -1;
        if (!meshHasParametrization(meshIndex)) {
            if (errorMessage)
                *errorMessage = tr("Current mesh has no UV parametrization.");
            return false;
        }
        m_depthPickPending = false;
        m_uvFitRequested = true;
    }
    if (mode == ViewMode::RasterImage) {
        const int rasterIndex = m_doc ? m_doc->currentRasterIndex() : -1;
        if (rasterIndex < 0 || rasterIndex >= m_doc->rasterCount()) {
            if (errorMessage)
                *errorMessage = tr("No current raster is available.");
            return false;
        }
        Document::RasterEntry &rasterEntry = m_doc->raster(rasterIndex);
        Document::RasterPlane *plane = rasterEntry.currentPlane();
        if (!plane) {
            if (errorMessage)
                *errorMessage = tr("Current raster has no image plane.");
            return false;
        }
        Document::ensureRasterPlaneImage(*plane);
        if (plane->image.isNull()) {
            if (errorMessage)
                *errorMessage = tr("Current raster has no image plane.");
            return false;
        }
        m_depthPickPending = false;
        resetRasterView();
        m_rasterOpacity = 0.75f;
    }

    m_viewMode = mode;
    if (m_overlayPanel)
        m_overlayPanel->setViewerModeUv(m_viewMode == ViewMode::ParametrizationUV);
    if (m_viewMode == ViewMode::Scene3D)
        updateBoundingBoxCornersOverlay();
    if (errorMessage)
        errorMessage->clear();
    update();
    return true;
}

void RenderWidget::setCurrentViewHighlighted(bool highlighted)
{
    if (m_currentViewHighlighted == highlighted)
        return;
    m_currentViewHighlighted = highlighted;

    if (m_currentViewIndicator) {
        m_currentViewIndicator->setVisible(highlighted);
        if (highlighted)
            m_currentViewIndicator->raise();
    }
}

void RenderWidget::startCenterAnimation(const QVector3D &targetCenter)
{
    const QVector3D currentCenter = m_trackball.center();
    if ((targetCenter - currentCenter).lengthSquared() < 1e-12f) {
        m_trackball.setCenter(targetCenter);
        m_centerAnimActive = false;
        return;
    }

    m_centerAnimStart = currentCenter;
    m_centerAnimTarget = targetCenter;
    m_centerAnimTimer.restart();
    m_centerAnimActive = true;
    update();
}

void RenderWidget::cancelCenterAnimation()
{
    m_centerAnimActive = false;
}

void RenderWidget::advanceCenterAnimation()
{
    if (!m_centerAnimActive)
        return;
    if (!m_centerAnimTimer.isValid())
        m_centerAnimTimer.start();

    const float t = std::clamp(
        float(m_centerAnimTimer.elapsed()) / float(qMax(1, m_centerAnimDurationMs)),
        0.0f,
        1.0f);
    const float eased = (t < 0.5f)
        ? (4.0f * t * t * t)
        : (1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f);
    const QVector3D c = m_centerAnimStart + (m_centerAnimTarget - m_centerAnimStart) * eased;
    m_trackball.setCenter(c);

    if (t >= 1.0f) {
        m_trackball.setCenter(m_centerAnimTarget);
        m_centerAnimActive = false;
    } else {
        update();
    }
}

void RenderWidget::createOverlayButtons()
{
    m_overlayPanel = new RenderOverlayPanel(this);
    m_overlayPanel->setViewerModeUv(m_viewMode == ViewMode::ParametrizationUV);
    m_overlayPanel->setGlobalSettings(m_renderSettings);
    auto makeCornerLabel = [this](const QColor &textColor) {
        auto *label = new QLabel(this);
        label->setVisible(false);
        label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        label->setTextInteractionFlags(Qt::NoTextInteraction);
        label->setWordWrap(false);
        label->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  color: rgba(%1,%2,%3,245);"
            "  background: rgba(20,20,20,170);"
            "  border: 1px solid rgba(90,90,90,180);"
            "  border-radius: 4px;"
            "  padding: 2px 4px;"
            "}")
                                 .arg(textColor.red())
                                 .arg(textColor.green())
                                 .arg(textColor.blue()));
        return label;
    };
    m_bboxMinCornerOverlayLabel = makeCornerLabel(QColor(140, 220, 255));
    m_bboxMaxCornerOverlayLabel = makeCornerLabel(QColor(255, 210, 140));
    m_bboxDimXOverlayLabel = makeCornerLabel(QColor(255, 150, 150));
    m_bboxDimYOverlayLabel = makeCornerLabel(QColor(150, 255, 170));
    m_bboxDimZOverlayLabel = makeCornerLabel(QColor(150, 190, 255));
    m_qualityHistogramOverlayLabel = new QLabel(this);
    m_qualityHistogramOverlayLabel->setVisible(false);
    m_qualityHistogramOverlayLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_qualityHistogramOverlayLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  background: rgba(20,20,20,120);"
        "  border: 1px solid rgba(90,90,90,140);"
        "  border-radius: 4px;"
        "  padding: 2px;"
        "}"));
    m_helpOverlayLabel = new QLabel(this);
    m_helpOverlayLabel->setVisible(false);
    m_helpOverlayLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_helpOverlayLabel->setTextFormat(Qt::RichText);
    m_helpOverlayLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_helpOverlayLabel->setWordWrap(true);
    m_helpOverlayLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_helpOverlayLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  color: rgba(245,245,248,245);"
        "  background: rgba(18,18,22,178);"
        "  border: 1px solid rgba(96,96,108,185);"
        "  border-radius: 8px;"
        "  padding: 10px 12px;"
        "}"));
    m_helpOverlayLabel->setText(quickHelpOverlayHtml());
    m_interactionStatusOverlayLabel = new QLabel(this);
    m_interactionStatusOverlayLabel->setVisible(false);
    m_interactionStatusOverlayLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_interactionStatusOverlayLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    m_interactionStatusOverlayLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  color: rgba(246,246,250,248);"
        "  background: rgba(20,20,24,188);"
        "  border: 1px solid rgba(110,110,122,190);"
        "  border-radius: 6px;"
        "  padding: 5px 10px;"
        "}"));
    m_interactionStatusOverlayTimer = new QTimer(this);
    m_interactionStatusOverlayTimer->setSingleShot(true);
    connect(m_interactionStatusOverlayTimer, &QTimer::timeout, this, [this]() {
        if (m_interactionStatusOverlayLabel)
            m_interactionStatusOverlayLabel->hide();
    });
    for (int i = 0; i <= 10; ++i) {
        auto *xLabel = new QLabel(this);
        xLabel->setVisible(false);
        xLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        xLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        xLabel->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  color: rgba(240,240,245,230);"
            "  background: transparent;"
            "  border: none;"
            "  padding: 0px;"
            "}"));
        xLabel->setText(QString::number(i / 10.0f, 'f', 1));
        m_uvScaleXTickLabels[size_t(i)] = xLabel;

        auto *yLabel = new QLabel(this);
        yLabel->setVisible(false);
        yLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        yLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        yLabel->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  color: rgba(240,240,245,230);"
            "  background: transparent;"
            "  border: none;"
            "  padding: 0px;"
            "}"));
        yLabel->setText(QString::number(i / 10.0f, 'f', 1));
        m_uvScaleYTickLabels[size_t(i)] = yLabel;
    }

    connect(m_overlayPanel, &RenderOverlayPanel::globalSettingsChanged, this,
            [this](const RenderSettings &settings) {
        emit viewActivated(this);
        const RenderSettings prev = m_renderSettings;
        RenderSettings next = settings;
        bool adjustedFixedRange = false;
        if (!prev.qualityHistogramFixedRange && next.qualityHistogramFixedRange) {
            const RenderQualityRange range = automaticQualityRangeForCurrentMesh(m_doc, next);
            if (range.valid) {
                next.qualityHistogramMin = range.minV;
                next.qualityHistogramMax = range.maxV;
                adjustedFixedRange = (next != settings);
            }
        }
        m_renderSettings = next;
        if (adjustedFixedRange)
            m_overlayPanel->setGlobalSettings(m_renderSettings);

        updateBoundingBoxCornersOverlay();
        if (prev.qualityHistogramBins != m_renderSettings.qualityHistogramBins
            || prev.qualityHistogramSource != m_renderSettings.qualityHistogramSource
            || prev.qualityHistogramFixedRange != m_renderSettings.qualityHistogramFixedRange
            || prev.qualityHistogramCenterOnZero != m_renderSettings.qualityHistogramCenterOnZero
            || prev.qualityHistogramPercentileCrop != m_renderSettings.qualityHistogramPercentileCrop
            || prev.qualityHistogramMin != m_renderSettings.qualityHistogramMin
            || prev.qualityHistogramMax != m_renderSettings.qualityHistogramMax
            || prev.qualityHistogramColorMapId != m_renderSettings.qualityHistogramColorMapId
            || prev.qualityHistogramInvertColorMap != m_renderSettings.qualityHistogramInvertColorMap) {
            m_qualityHistogram.valid = false;
        }
        updateQualityHistogramOverlay();
        update();
        layoutOverlayButtons();
    });
    connect(
        m_overlayPanel,
        &RenderOverlayPanel::bakeQualityMappingToVertexColorRequested,
        this,
        &RenderWidget::bakeCurrentQualityMappingToVertexColor);

    connect(m_overlayPanel, &RenderOverlayPanel::meshSettingsChanged, this,
            [this](const PerMeshRenderSettings &meshSettings) {
        emit viewActivated(this);
        const int meshIndex = m_doc ? m_doc->currentMeshIndex() : -1;
        if (m_doc && meshIndex >= 0 && meshIndex < m_doc->meshCount()) {
            const std::uint64_t meshId = m_doc->mesh(meshIndex).meshId;
            m_meshRenderModes[meshId] = meshSettings;
        }
        updateBoundingBoxCornersOverlay();
        update();
        layoutOverlayButtons();
    });

    updateBoundingBoxCornersOverlay();
    updateQualityHistogramOverlay();
    layoutOverlayButtons();
}

void RenderWidget::layoutOverlayButtons()
{
    constexpr int kOverlayMargin = 8;
    const int maxOverlayWidth = qMax(120, width() - 2 * kOverlayMargin);
    int panelBottom = kOverlayMargin;

    if (m_overlayPanel) {
        m_overlayPanel->setMaximumWidth(maxOverlayWidth);
        m_overlayPanel->adjustSize();
        m_overlayPanel->move(kOverlayMargin, kOverlayMargin);
        m_overlayPanel->raise();
        panelBottom = m_overlayPanel->y() + m_overlayPanel->height();
    }

    if (m_qualityHistogramOverlayLabel) {
        if (m_qualityHistogramOverlayLabel->isVisible()) {
            m_qualityHistogramOverlayLabel->adjustSize();
            const int x = kOverlayMargin;
            const int y = qMax(kOverlayMargin, panelBottom + kOverlayMargin);
            m_qualityHistogramOverlayLabel->move(x, y);
            m_qualityHistogramOverlayLabel->raise();
        }
    }

    if (m_helpOverlayLabel && m_helpOverlayVisible) {
        const int helpWidth = std::clamp(width() - 2 * kOverlayMargin, 260, 460);
        m_helpOverlayLabel->setFixedWidth(helpWidth);
        m_helpOverlayLabel->adjustSize();
        const int x = (width() - m_helpOverlayLabel->width()) / 2;
        const int y = std::max(
            kOverlayMargin,
            (height() - m_helpOverlayLabel->height()) / 2);
        m_helpOverlayLabel->move(x, y);
        m_helpOverlayLabel->raise();
    }

    if (m_interactionStatusOverlayLabel && m_interactionStatusOverlayLabel->isVisible()) {
        m_interactionStatusOverlayLabel->adjustSize();
        const int x = (width() - m_interactionStatusOverlayLabel->width()) / 2;
        const int y = kOverlayMargin + 8;
        m_interactionStatusOverlayLabel->move(x, y);
        m_interactionStatusOverlayLabel->raise();
    }
}

void RenderWidget::setHelpOverlayVisible(bool visible)
{
    if (m_helpOverlayVisible == visible)
        return;
    m_helpOverlayVisible = visible;
    if (m_helpOverlayLabel) {
        m_helpOverlayLabel->setVisible(visible);
        if (visible)
            m_helpOverlayLabel->setText(quickHelpOverlayHtml());
    }
    layoutOverlayButtons();
    update();
}

void RenderWidget::emitCameraStateChangedIfNeeded()
{
    if (m_viewMode != ViewMode::Scene3D)
        return;
    const ViewTrackball::State current = m_trackball.state();
    if (m_lastBroadcastTrackballStateValid
        && fuzzyStateEqual(current, m_lastBroadcastTrackballState)) {
        return;
    }
    m_lastBroadcastTrackballState = current;
    m_lastBroadcastTrackballStateValid = true;
    emit cameraStateChanged(this);
}

void RenderWidget::showInteractionStatusOverlay(const QString &text)
{
    if (!m_interactionStatusOverlayLabel || text.isEmpty())
        return;
    m_interactionStatusOverlayLabel->setText(text);
    m_interactionStatusOverlayLabel->show();
    layoutOverlayButtons();
    if (m_interactionStatusOverlayTimer)
        m_interactionStatusOverlayTimer->start(1200);
}

bool RenderWidget::computeVisibleSceneBoundingBox(QVector3D &minCorner, QVector3D &maxCorner) const
{
    bool hasBox = false;
    QVector3D sceneMin;
    QVector3D sceneMax;
    for (int i = 0; i < m_doc->meshCount(); ++i) {
        const auto &meshEntry = m_doc->mesh(i);
        if (!meshVisible(i))
            continue;
        if (!renderModeForMesh(i).showBoundingBox)
            continue;
        if (meshEntry.mesh.bbox.IsNull())
            continue;

        const vcg::Box3f &box = meshEntry.mesh.bbox;
        const QVector3D corners[8] = {
            QVector3D(box.min[0], box.min[1], box.min[2]),
            QVector3D(box.max[0], box.min[1], box.min[2]),
            QVector3D(box.min[0], box.max[1], box.min[2]),
            QVector3D(box.max[0], box.max[1], box.min[2]),
            QVector3D(box.min[0], box.min[1], box.max[2]),
            QVector3D(box.max[0], box.min[1], box.max[2]),
            QVector3D(box.min[0], box.max[1], box.max[2]),
            QVector3D(box.max[0], box.max[1], box.max[2])
        };

        for (const QVector3D &corner : corners) {
            const QVector4D transformed = meshEntry.transform * QVector4D(corner, 1.0f);
            const QVector3D worldCorner = (std::abs(transformed.w()) > 1e-8f)
                ? transformed.toVector3DAffine()
                : transformed.toVector3D();
            if (!hasBox) {
                sceneMin = worldCorner;
                sceneMax = worldCorner;
                hasBox = true;
                continue;
            }
            sceneMin.setX(std::min(sceneMin.x(), worldCorner.x()));
            sceneMin.setY(std::min(sceneMin.y(), worldCorner.y()));
            sceneMin.setZ(std::min(sceneMin.z(), worldCorner.z()));
            sceneMax.setX(std::max(sceneMax.x(), worldCorner.x()));
            sceneMax.setY(std::max(sceneMax.y(), worldCorner.y()));
            sceneMax.setZ(std::max(sceneMax.z(), worldCorner.z()));
        }
    }

    if (!hasBox)
        return false;

    minCorner = sceneMin;
    maxCorner = sceneMax;
    return true;
}

void RenderWidget::updateBoundingBoxCornersOverlay()
{
    if (!m_bboxMinCornerOverlayLabel || !m_bboxMaxCornerOverlayLabel
        || !m_bboxDimXOverlayLabel || !m_bboxDimYOverlayLabel || !m_bboxDimZOverlayLabel)
        return;

    const bool showCorners = m_renderSettings.showBoundingBoxCorners;
    const bool showDimensions = m_renderSettings.showBoundingBoxDimensions;
    bool anyBBoxEnabled = false;
    for (int i = 0; i < m_doc->meshCount(); ++i) {
        if (!meshVisible(i))
            continue;
        if (renderModeForMesh(i).showBoundingBox) {
            anyBBoxEnabled = true;
            break;
        }
    }
    if (!anyBBoxEnabled || (!showCorners && !showDimensions)) {
        m_bboxOverlayCornersValid = false;
        m_bboxMinCornerOverlayLabel->hide();
        m_bboxMaxCornerOverlayLabel->hide();
        m_bboxDimXOverlayLabel->hide();
        m_bboxDimYOverlayLabel->hide();
        m_bboxDimZOverlayLabel->hide();
        return;
    }

    QVector3D minCorner;
    QVector3D maxCorner;
    if (!computeVisibleSceneBoundingBox(minCorner, maxCorner)) {
        m_bboxOverlayCornersValid = false;
        m_bboxMinCornerOverlayLabel->hide();
        m_bboxMaxCornerOverlayLabel->hide();
        m_bboxDimXOverlayLabel->hide();
        m_bboxDimYOverlayLabel->hide();
        m_bboxDimZOverlayLabel->hide();
        return;
    }

    m_bboxOverlayCornersValid = true;
    m_bboxOverlayMinCorner = minCorner;
    m_bboxOverlayMaxCorner = maxCorner;

    if (showCorners) {
        const QString minText = tr("min (%1, %2, %3)")
                                    .arg(minCorner.x(), 0, 'f', 6)
                                    .arg(minCorner.y(), 0, 'f', 6)
                                    .arg(minCorner.z(), 0, 'f', 6);
        const QString maxText = tr("max (%1, %2, %3)")
                                    .arg(maxCorner.x(), 0, 'f', 6)
                                    .arg(maxCorner.y(), 0, 'f', 6)
                                    .arg(maxCorner.z(), 0, 'f', 6);
        if (m_bboxMinCornerOverlayLabel->text() != minText)
            m_bboxMinCornerOverlayLabel->setText(minText);
        if (m_bboxMaxCornerOverlayLabel->text() != maxText)
            m_bboxMaxCornerOverlayLabel->setText(maxText);
        m_bboxMinCornerOverlayLabel->show();
        m_bboxMaxCornerOverlayLabel->show();
    } else {
        m_bboxMinCornerOverlayLabel->hide();
        m_bboxMaxCornerOverlayLabel->hide();
    }

    if (showDimensions) {
        const QVector3D size = maxCorner - minCorner;
        const QString xText = tr("X: %1").arg(size.x(), 0, 'f', 6);
        const QString yText = tr("Y: %1").arg(size.y(), 0, 'f', 6);
        const QString zText = tr("Z: %1").arg(size.z(), 0, 'f', 6);
        if (m_bboxDimXOverlayLabel->text() != xText)
            m_bboxDimXOverlayLabel->setText(xText);
        if (m_bboxDimYOverlayLabel->text() != yText)
            m_bboxDimYOverlayLabel->setText(yText);
        if (m_bboxDimZOverlayLabel->text() != zText)
            m_bboxDimZOverlayLabel->setText(zText);
        m_bboxDimXOverlayLabel->show();
        m_bboxDimYOverlayLabel->show();
        m_bboxDimZOverlayLabel->show();
    } else {
        m_bboxDimXOverlayLabel->hide();
        m_bboxDimYOverlayLabel->hide();
        m_bboxDimZOverlayLabel->hide();
    }
}

void RenderWidget::updateBoundingBoxCornersOverlayPlacement(
    const QMatrix4x4 &mvp,
    const QMatrix4x4 &view,
    const QSize &pixelSize)
{
    if (!m_bboxOverlayCornersValid
        || !m_bboxMinCornerOverlayLabel
        || !m_bboxMaxCornerOverlayLabel
        || !m_bboxDimXOverlayLabel
        || !m_bboxDimYOverlayLabel
        || !m_bboxDimZOverlayLabel)
        return;

    const auto projectToScreen = [this, &mvp, &pixelSize](const QVector3D &world, QPoint &screenPos) -> bool {
        const QVector4D clip = mvp * QVector4D(world, 1.0f);
        if (clip.w() <= 1e-6f)
            return false;
        const QVector3D ndc = clip.toVector3DAffine();
        const float px = (ndc.x() * 0.5f + 0.5f) * float(pixelSize.width());
        const float py = (1.0f - (ndc.y() * 0.5f + 0.5f)) * float(pixelSize.height());

        // QRhi renders in physical pixels, QWidget overlays are in logical pixels.
        const float dpr = qMax(1.0, devicePixelRatioF());
        const float x = px / dpr;
        const float y = py / dpr;
        screenPos = QPoint(int(std::round(x)), int(std::round(y)));
        return true;
    };

    auto placeLabel = [this, &projectToScreen](QLabel *label, const QVector3D &corner, const QPoint &offset) {
        QPoint screenPos;
        if (!projectToScreen(corner, screenPos)) {
            label->hide();
            return;
        }
        label->adjustSize();
        QPoint targetPos = screenPos + offset;
        const int maxX = qMax(0, width() - label->width());
        const int maxY = qMax(0, height() - label->height());
        targetPos.setX(std::clamp(targetPos.x(), 0, maxX));
        targetPos.setY(std::clamp(targetPos.y(), 0, maxY));
        label->move(targetPos);
        label->show();
        label->raise();
    };

    if (m_bboxMinCornerOverlayLabel->isVisible())
        placeLabel(m_bboxMinCornerOverlayLabel, m_bboxOverlayMinCorner, QPoint(8, 8));
    if (m_bboxMaxCornerOverlayLabel->isVisible())
        placeLabel(m_bboxMaxCornerOverlayLabel, m_bboxOverlayMaxCorner, QPoint(8, -22));

    if (!m_bboxDimXOverlayLabel->isVisible()
        && !m_bboxDimYOverlayLabel->isVisible()
        && !m_bboxDimZOverlayLabel->isVisible()) {
        return;
    }

    bool okInv = false;
    const QMatrix4x4 invView = view.inverted(&okInv);
    if (!okInv) {
        m_bboxDimXOverlayLabel->hide();
        m_bboxDimYOverlayLabel->hide();
        m_bboxDimZOverlayLabel->hide();
        return;
    }
    const QVector3D cameraPos = (invView * QVector4D(0.0f, 0.0f, 0.0f, 1.0f)).toVector3D();

    const QVector3D mn = m_bboxOverlayMinCorner;
    const QVector3D mx = m_bboxOverlayMaxCorner;

    auto closestAxisEdgeMidpoint = [&cameraPos, &mn, &mx](int axis) {
        QVector3D bestMid;
        float bestDist2 = std::numeric_limits<float>::max();
        auto test = [&](const QVector3D &a, const QVector3D &b) {
            const QVector3D mid = (a + b) * 0.5f;
            const float dist2 = (mid - cameraPos).lengthSquared();
            if (dist2 < bestDist2) {
                bestDist2 = dist2;
                bestMid = mid;
            }
        };

        if (axis == 0) { // X
            test(QVector3D(mn.x(), mn.y(), mn.z()), QVector3D(mx.x(), mn.y(), mn.z()));
            test(QVector3D(mn.x(), mx.y(), mn.z()), QVector3D(mx.x(), mx.y(), mn.z()));
            test(QVector3D(mn.x(), mn.y(), mx.z()), QVector3D(mx.x(), mn.y(), mx.z()));
            test(QVector3D(mn.x(), mx.y(), mx.z()), QVector3D(mx.x(), mx.y(), mx.z()));
        } else if (axis == 1) { // Y
            test(QVector3D(mn.x(), mn.y(), mn.z()), QVector3D(mn.x(), mx.y(), mn.z()));
            test(QVector3D(mx.x(), mn.y(), mn.z()), QVector3D(mx.x(), mx.y(), mn.z()));
            test(QVector3D(mn.x(), mn.y(), mx.z()), QVector3D(mn.x(), mx.y(), mx.z()));
            test(QVector3D(mx.x(), mn.y(), mx.z()), QVector3D(mx.x(), mx.y(), mx.z()));
        } else { // Z
            test(QVector3D(mn.x(), mn.y(), mn.z()), QVector3D(mn.x(), mn.y(), mx.z()));
            test(QVector3D(mx.x(), mn.y(), mn.z()), QVector3D(mx.x(), mn.y(), mx.z()));
            test(QVector3D(mn.x(), mx.y(), mn.z()), QVector3D(mn.x(), mx.y(), mx.z()));
            test(QVector3D(mx.x(), mx.y(), mn.z()), QVector3D(mx.x(), mx.y(), mx.z()));
        }
        return bestMid;
    };

    if (m_bboxDimXOverlayLabel->isVisible())
        placeLabel(m_bboxDimXOverlayLabel, closestAxisEdgeMidpoint(0), QPoint(8, -16));
    if (m_bboxDimYOverlayLabel->isVisible())
        placeLabel(m_bboxDimYOverlayLabel, closestAxisEdgeMidpoint(1), QPoint(8, 8));
    if (m_bboxDimZOverlayLabel->isVisible())
        placeLabel(m_bboxDimZOverlayLabel, closestAxisEdgeMidpoint(2), QPoint(-50, -4));
}

void RenderWidget::updateQualityHistogramOverlay()
{
    if (!m_qualityHistogramOverlayLabel)
        return;

    auto hideOverlay = [this]() {
        if (m_qualityHistogramOverlayLabel)
            m_qualityHistogramOverlayLabel->hide();
    };

    if (!m_renderSettings.showQualityHistogram || !m_doc) {
        hideOverlay();
        return;
    }

    const int meshIndex = m_doc->currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= m_doc->meshCount()) {
        hideOverlay();
        return;
    }

    const auto &entry = m_doc->mesh(meshIndex);
    const int mask = entry.ioMask;
    const bool hasVertexQuality = (mask & vcg::tri::io::Mask::IOM_VERTQUALITY) != 0;
    const bool hasFaceQuality = (mask & vcg::tri::io::Mask::IOM_FACEQUALITY) != 0;
    const QualityHistogramSource sourceSelection = m_renderSettings.qualityHistogramSource;
    bool useVertexQuality = false;
    bool useFaceQuality = false;
    switch (sourceSelection) {
    case QualityHistogramSource::Auto:
        useVertexQuality = hasVertexQuality;
        useFaceQuality = !useVertexQuality && hasFaceQuality;
        break;
    case QualityHistogramSource::VertexQuality:
        useVertexQuality = hasVertexQuality;
        break;
    case QualityHistogramSource::FaceQuality:
        useFaceQuality = hasFaceQuality;
        break;
    }
    const int bins = std::clamp(m_renderSettings.qualityHistogramBins, 4, 512);
    const bool fixedRange = m_renderSettings.qualityHistogramFixedRange;
    const bool centerOnZero = m_renderSettings.qualityHistogramCenterOnZero;
    const float percentileCrop =
        std::clamp(m_renderSettings.qualityHistogramPercentileCrop, 0.0f, 0.5f);
    float fixedMin = m_renderSettings.qualityHistogramMin;
    float fixedMax = m_renderSettings.qualityHistogramMax;
    if (fixedMin > fixedMax)
        std::swap(fixedMin, fixedMax);
    const ColorMapRegistry &colorRegistry = ColorMapRegistry::instance();
    QString colorMapId = m_renderSettings.qualityHistogramColorMapId.trimmed().toLower();
    if (colorMapId.isEmpty() || !colorRegistry.hasMap(colorMapId))
        colorMapId = colorRegistry.fallbackMapId();
    const bool invertColorMap = m_renderSettings.qualityHistogramInvertColorMap;
    const ColorMapDefinition *colorMapDef = colorRegistry.definition(colorMapId);
    constexpr int kOverlayMargin = 8;
    const int maxPanelWidth = qMax(120, width() - 2 * kOverlayMargin);
    int panelBottom = kOverlayMargin;
    int panelWidth = qMin(360, qMax(120, maxPanelWidth));
    if (m_overlayPanel) {
        m_overlayPanel->setMaximumWidth(maxPanelWidth);
        m_overlayPanel->adjustSize();
        panelBottom = kOverlayMargin + m_overlayPanel->height();
        panelWidth = m_overlayPanel->width();
    }
    const int w = std::clamp(panelWidth, 120, qMax(120, width() - 2 * kOverlayMargin));
    const int availableTop = panelBottom + kOverlayMargin;
    const int h = height() - availableTop - kOverlayMargin;
    if (h < 72) {
        hideOverlay();
        return;
    }

    auto buildMessagePixmap = [w, h](const QString &line) {
        QPixmap pm(w, h);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(pm.rect(), QColor(32, 32, 32, 150));
        p.setPen(QColor(90, 90, 90, 150));
        p.drawRoundedRect(pm.rect().adjusted(0, 0, -1, -1), 4, 4);
        p.setPen(QColor(230, 230, 230));
        p.drawText(
            QRect(10, 8, w - 20, h - 16),
            Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
            line);
        return pm;
    };

    if (!useVertexQuality && !useFaceQuality) {
        QString message = tr("No quality found in current mesh");
        QString tooltip = tr("Mesh has no vertex/face quality");
        if (sourceSelection == QualityHistogramSource::VertexQuality) {
            message = tr("No vertex quality found in current mesh");
            tooltip = tr("Selected source is Vertex Quality but mesh has no VQ");
        } else if (sourceSelection == QualityHistogramSource::FaceQuality) {
            message = tr("No face quality found in current mesh");
            tooltip = tr("Selected source is Face Quality but mesh has no FQ");
        }
        m_qualityHistogramOverlayLabel->setPixmap(buildMessagePixmap(message));
        m_qualityHistogramOverlayLabel->setToolTip(tooltip);
        m_qualityHistogramOverlayLabel->show();
        layoutOverlayButtons();
        return;
    }

    const bool cacheValid =
        m_qualityHistogram.valid
        && m_qualityHistogram.meshId == entry.meshId
        && m_qualityHistogram.geometryRevision == entry.geometryRevision
        && m_qualityHistogram.bins == bins
        && m_qualityHistogram.sourceSelection == sourceSelection
        && m_qualityHistogram.fixedRange == fixedRange
        && m_qualityHistogram.centerOnZero == centerOnZero
        && m_qualityHistogram.percentileCrop == percentileCrop
        && m_qualityHistogram.fixedMin == fixedMin
        && m_qualityHistogram.fixedMax == fixedMax
        && m_qualityHistogram.colorMapId == colorMapId
        && m_qualityHistogram.invertColorMap == invertColorMap
        && m_qualityHistogram.vertexBased == useVertexQuality;

    if (!cacheValid) {
        m_qualityHistogram.valid = false;
        m_qualityHistogram.meshId = entry.meshId;
        m_qualityHistogram.geometryRevision = entry.geometryRevision;
        m_qualityHistogram.bins = bins;
        m_qualityHistogram.sourceSelection = sourceSelection;
        m_qualityHistogram.fixedRange = fixedRange;
        m_qualityHistogram.centerOnZero = centerOnZero;
        m_qualityHistogram.percentileCrop = percentileCrop;
        m_qualityHistogram.fixedMin = fixedMin;
        m_qualityHistogram.fixedMax = fixedMax;
        m_qualityHistogram.colorMapId = colorMapId;
        m_qualityHistogram.invertColorMap = invertColorMap;
        m_qualityHistogram.vertexBased = useVertexQuality;
        m_qualityHistogram.counts.assign(size_t(bins), 0);
        m_qualityHistogram.sampleCount = 0;
        m_qualityHistogram.minQ = 0.0f;
        m_qualityHistogram.maxQ = 1.0f;

        const VCGMesh &mesh = entry.mesh;
        std::vector<float> values;
        values.reserve(useVertexQuality ? size_t(mesh.VN()) : size_t(mesh.FN()));

        if (useVertexQuality) {
            for (int vi = 0; vi < mesh.VN(); ++vi) {
                const auto &v = mesh.vert[vi];
                if (v.IsD())
                    continue;
                const float q = static_cast<float>(v.cQ());
                if (!std::isfinite(q))
                    continue;
                values.push_back(q);
            }
        } else {
            for (int fi = 0; fi < mesh.FN(); ++fi) {
                const auto &f = mesh.face[fi];
                if (f.IsD())
                    continue;
                const float q = static_cast<float>(f.cQ());
                if (!std::isfinite(q))
                    continue;
                values.push_back(q);
            }
        }

        if (!values.empty()) {
            m_qualityHistogram.sampleCount = int(values.size());
            RenderQualityRange histRange;
            if (fixedRange) {
                histRange = fixedRenderQualityRange(fixedMin, fixedMax);
            } else {
                histRange = sampledRenderQualityRange(values, centerOnZero, percentileCrop);
            }
            m_qualityHistogram.minQ = histRange.minV;
            m_qualityHistogram.maxQ = histRange.maxV;
            for (float q : values) {
                const float t = normalizedRenderQuality(q, histRange);
                const int idx = std::clamp(int(t * bins), 0, bins - 1);
                m_qualityHistogram.counts[size_t(idx)] += 1;
            }
            m_qualityHistogram.valid = histRange.valid;
        }
    }

    if (!m_qualityHistogram.valid) {
        m_qualityHistogramOverlayLabel->setPixmap(buildMessagePixmap(tr("Quality values are not finite")));
        m_qualityHistogramOverlayLabel->setToolTip(tr("Cannot build histogram for current mesh"));
        m_qualityHistogramOverlayLabel->show();
        layoutOverlayButtons();
        return;
    }

    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(pm.rect(), QColor(32, 32, 32, 150));
    p.setPen(QColor(90, 90, 90, 150));
    p.drawRoundedRect(pm.rect().adjusted(0, 0, -1, -1), 4, 4);

    QFont valueFont = p.font();
    valueFont.setPointSizeF(std::max(7.0, valueFont.pointSizeF() - 1.0));
    const QFontMetrics valueFm(valueFont);
    const float qMin = m_qualityHistogram.minQ;
    const float qMax = m_qualityHistogram.maxQ;
    const float qRange = qMax - qMin;

    int top = 8;
    int bottom = 10;
    if (h - top - bottom < 24) {
        top = 8;
        bottom = 8;
    }

    const int targetTickCount = std::clamp((h - top - bottom) / (valueFm.height() + 2) + 1, 3, 14);
    NiceTickValues tickValues = buildNiceTickValues(double(qMin), double(qMax), targetTickCount);
    if (tickValues.values.empty())
        tickValues.values = {double(qMin), double(qMax)};
    const int interiorDecimals = std::clamp(decimalsForStep(tickValues.step), 0, 4);
    auto tickLabelText = [interiorDecimals, &tickValues](double value, int index) -> QString {
        const bool edge = (index == 0) || (index == int(tickValues.values.size()) - 1);
        if (edge)
            return QString::number(value, 'g', 8);
        return QString::number(value, 'f', interiorDecimals);
    };

    int valueColumnWidth = 28;
    for (int i = 0; i < int(tickValues.values.size()); ++i)
        valueColumnWidth = std::max(valueColumnWidth, valueFm.horizontalAdvance(tickLabelText(tickValues.values[size_t(i)], i)) + 6);
    valueColumnWidth = std::clamp(valueColumnWidth, 28, 96);
    const int tickLen = 4;
    const int colorBarWidth = 10;
    const int labelLeft = 10;
    const int colorBarLeft = labelLeft + valueColumnWidth + 4;
    const int left = colorBarLeft + colorBarWidth + 4 + tickLen + 4;
    const int right = 10;
    const QRect plotRect(left, top, w - left - right, h - top - bottom);
    if (plotRect.height() < 16 || plotRect.width() < 40) {
        hideOverlay();
        return;
    }
    const QRect colorBarRect(colorBarLeft, plotRect.top(), colorBarWidth, plotRect.height());
    const int maxCount = *std::max_element(
        m_qualityHistogram.counts.begin(), m_qualityHistogram.counts.end());
    const bool hasRange = std::abs(qRange) > 1e-20f;

    for (int yy = colorBarRect.top(); yy <= colorBarRect.bottom(); ++yy) {
        const float t = (colorBarRect.height() > 1)
            ? float(colorBarRect.bottom() - yy) / float(colorBarRect.height() - 1)
            : 0.0f;
        const float tc = invertColorMap ? (1.0f - t) : t;
        p.setPen(colorRegistry.sampleQColor(colorMapDef, tc, 0.92f));
        p.drawLine(colorBarRect.left(), yy, colorBarRect.right(), yy);
    }
    p.setPen(QColor(175, 175, 175, 180));
    p.drawRect(colorBarRect);

    p.setPen(QColor(175, 175, 175, 180));
    p.drawRect(plotRect);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < bins; ++i) {
        // Horizontal bars: each bin occupies one row, low-quality bins at the bottom.
        const int y0 = plotRect.top()
            + int((double(bins - i - 1) * plotRect.height()) / double(bins));
        const int y1 = plotRect.top()
            + int((double(bins - i) * plotRect.height()) / double(bins));
        const int bh = std::max(1, y1 - y0 - 1);
        const int c = m_qualityHistogram.counts[size_t(i)];
        const int bw = (maxCount > 0)
            ? int((double(c) / double(maxCount)) * double(plotRect.width() - 2))
            : 0;
        if (bw <= 0)
            continue;
        const float qValue = hasRange
            ? (qMin + ((float(i) + 0.5f) / float(bins)) * qRange)
            : qMin;
        const float t = hasRange
            ? std::clamp((qValue - qMin) / qRange, 0.0f, 1.0f)
            : 0.5f;
        const float tc = invertColorMap ? (1.0f - t) : t;
        p.setBrush(colorRegistry.sampleQColor(colorMapDef, tc, 0.92f));
        p.drawRect(plotRect.left() + 1, y0 + 1, bw, bh);
    }

    p.setFont(valueFont);
    p.setPen(QColor(205, 205, 205, 210));
    const int totalTicks = int(tickValues.values.size());
    const int maxVisibleTicks =
        std::max(2, plotRect.height() / std::max(1, valueFm.height() + 2) + 1);
    int tickStride = 1;
    if (totalTicks > maxVisibleTicks) {
        tickStride = std::max(
            1,
            int(std::ceil(double(totalTicks - 1) / double(std::max(1, maxVisibleTicks - 1)))));
    }
    for (int i = 0; i < totalTicks; ++i) {
        const bool edge = (i == 0) || (i == totalTicks - 1);
        if (!edge && (i % tickStride) != 0)
            continue;
        double t = 0.0;
        if (hasRange) {
            t = (tickValues.values[size_t(i)] - double(qMin)) / double(qRange);
        } else if (tickValues.values.size() > 1) {
            t = double(i) / double(tickValues.values.size() - 1);
        }
        t = std::clamp(t, 0.0, 1.0);
        const int y = plotRect.bottom() - int(t * double(plotRect.height() - 1));
        p.drawLine(plotRect.left() - tickLen, y, plotRect.left() - 1, y);
        const QString qText = tickLabelText(tickValues.values[size_t(i)], i);
        p.drawText(
            QRect(labelLeft, y - valueFm.height() / 2, valueColumnWidth, valueFm.height()),
            Qt::AlignRight | Qt::AlignVCenter,
            qText);
    }

    const QString sourceName = m_qualityHistogram.vertexBased ? tr("VQ") : tr("FQ");
    m_qualityHistogramOverlayLabel->setPixmap(pm);
    m_qualityHistogramOverlayLabel->setToolTip(
        tr("%1 quality distribution\nsamples: %2\nrange: [%3, %4]")
            .arg(sourceName)
            .arg(m_qualityHistogram.sampleCount)
            .arg(m_qualityHistogram.minQ, 0, 'g', 8)
            .arg(m_qualityHistogram.maxQ, 0, 'g', 8));
    m_qualityHistogramOverlayLabel->show();
    layoutOverlayButtons();
}

void RenderWidget::bakeCurrentQualityMappingToVertexColor()
{
    if (!m_doc)
        return;

    const int meshIndex = m_doc->currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= m_doc->meshCount()) {
        m_doc->writeLog(
            tr("Cannot bake quality colors: no current mesh selected."),
            Document::LogSource::Error);
        return;
    }

    const auto &entry = m_doc->mesh(meshIndex);
    const bool hasVertexQuality =
        (entry.ioMask & vcg::tri::io::Mask::IOM_VERTQUALITY) != 0;
    if (!hasVertexQuality) {
        m_doc->writeLog(
            tr("Cannot bake quality colors: current mesh has no vertex quality."),
            Document::LogSource::Error);
        return;
    }
    if (m_renderSettings.qualityHistogramSource == QualityHistogramSource::FaceQuality) {
        m_doc->writeLog(
            tr("Cannot bake to vertex color while the quality source is Face Q. Switch the source to Auto or Vertex Q."),
            Document::LogSource::Error);
        return;
    }

    RenderQualityRange range;
    if (m_renderSettings.qualityHistogramFixedRange) {
        range = fixedRenderQualityRange(
            m_renderSettings.qualityHistogramMin,
            m_renderSettings.qualityHistogramMax);
    } else {
        range = automaticQualityRangeForCurrentMesh(m_doc, m_renderSettings);
    }
    if (!range.valid) {
        m_doc->writeLog(
            tr("Cannot bake quality colors: current quality range is not finite."),
            Document::LogSource::Error);
        return;
    }

    const ColorMapRegistry &registry = ColorMapRegistry::instance();
    QString mapId = m_renderSettings.qualityHistogramColorMapId.trimmed().toLower();
    if (mapId != QStringLiteral("constant") && (mapId.isEmpty() || !registry.hasMap(mapId)))
        mapId = registry.fallbackMapId();

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("minVal"), double(range.minV));
    params.insert(QStringLiteral("maxVal"), double(range.maxV));
    params.insert(QStringLiteral("perc"), 0.0);
    params.insert(QStringLiteral("zeroSym"), false);
    params.insert(QStringLiteral("colorMap"), qualityBakeFilterEnumColorMap(mapId));
    params.insert(QStringLiteral("invert"), m_renderSettings.qualityHistogramInvertColorMap);
    params.insert(QStringLiteral("colorMapId"), mapId);

    const QString filterKey =
        QStringLiteral("qmeshlab.filter.colorproc::compute_color_from_scalar_per_vertex");
    const QString label = tr("Bake Quality to Vertex Color");
    m_doc->beginFilterProgress(label);
    QElapsedTimer timer;
    timer.start();
    const MeshFilterRunResult result = m_doc->runFilter(filterKey, params);
    const double elapsedMs = double(timer.nsecsElapsed()) / 1e6;
    const QString elapsedText = QString::number(elapsedMs, 'f', 2);

    if (!result.success) {
        const QString errorText = result.errorMessage.trimmed().isEmpty()
            ? tr("Unknown filter error")
            : result.errorMessage.trimmed();
        const QString msg = tr("Filter failed: %1").arg(errorText);
        m_doc->finishFilterProgress(false, msg);
        m_doc->writeLog(msg, Document::LogSource::Error);
        m_doc->writeLog(
            tr("Filter '%1' runtime: %2 ms (failed)").arg(label, elapsedText),
            Document::LogSource::Error);
        return;
    }

    if (MeshRenderMode *mode = mutableRenderModeForMesh(meshIndex)) {
        mode->fillMaterial = FillMaterial::Plain;
        mode->fillPlain.colorSource = FillColorSource::PerVertex;
        mode->pointColorSource = PointColorSource::PerVertex;
        if (entry.mesh.FN() > 0)
            mode->showFill = true;
        else
            mode->showPoints = true;
        if (meshIndex == m_doc->currentMeshIndex()) {
            refreshColorSourceAvailability();
            syncOverlaySettingsToCurrentMesh();
        }
    }

    QString status = tr("Baked quality mapping to vertex colors");
    if (!result.infoMessages.isEmpty())
        status = result.infoMessages.back();
    m_doc->finishFilterProgress(true, status);
    m_doc->writeLog(
        tr("Filter '%1' runtime: %2 ms").arg(label, elapsedText),
        Document::LogSource::Application);
    update();
}

void RenderWidget::mousePressEvent(QMouseEvent *e)
{
    emit viewActivated(this);
    if (m_viewMode == ViewMode::ParametrizationUV) {
        if (e && (e->button() == Qt::LeftButton || e->button() == Qt::MiddleButton)) {
            m_uvPanning = true;
            m_uvLastMousePos = e->position().toPoint();
            e->accept();
            return;
        }
        QRhiWidget::mousePressEvent(e);
        return;
    }
    if (m_viewMode == ViewMode::RasterImage) {
        if (e && (e->button() == Qt::LeftButton || e->button() == Qt::MiddleButton)) {
            m_rasterPanning = true;
            m_rasterLastMousePos = e->position().toPoint();
            e->accept();
            return;
        }
        if (e)
            e->accept();
        return;
    }
    cancelCenterAnimation();
    // Ctrl+Shift+Left → rotate headlight
    if (e
        && e->button() == Qt::LeftButton
        && (e->modifiers() & Qt::ShiftModifier)
        && (e->modifiers() & Qt::ControlModifier)) {
        m_lightDragActive = true;
        m_lightDragLastPos = e->position();
        showInteractionStatusOverlay(tr("Rotating light — Ctrl+Shift+drag"));
        e->accept();
        return;
    }
    m_trackball.mousePress(e, size());
}

void RenderWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    emit viewActivated(this);
    if (m_viewMode == ViewMode::ParametrizationUV) {
        if (e && e->button() == Qt::LeftButton) {
            const QSize sz(qMax(1, width()), qMax(1, height()));
            const QPointF p = e->position();
            const auto screenToUv = [&](const QPointF &screenPos, float zoom, const QVector2D &pan) {
                const float aspect = float(sz.width()) / float(sz.height());
                const float xLim = (aspect >= 1.0f) ? aspect : 1.0f;
                const float yLim = (aspect >= 1.0f) ? 1.0f : (1.0f / qMax(1e-6f, aspect));
                const float ndcX = 2.0f * (float(screenPos.x()) / float(sz.width())) - 1.0f;
                const float ndcY = 1.0f - 2.0f * (float(screenPos.y()) / float(sz.height()));
                return QVector2D(
                    pan.x() + ndcX * xLim / qMax(1e-6f, zoom),
                    pan.y() + ndcY * yLim / qMax(1e-6f, zoom));
            };

            const QVector2D clickedUv = screenToUv(p, qMax(1e-6f, m_uvZoom), m_uvPan);
            m_uvPan = clickedUv;
            m_uvZoom = std::clamp(m_uvZoom * 1.35f, 0.05f, 5000.0f);
            m_uvFitRequested = false;
            update();
            e->accept();
            return;
        }
        QRhiWidget::mouseDoubleClickEvent(e);
        return;
    }
    if (m_viewMode == ViewMode::RasterImage) {
        if (e && e->button() == Qt::LeftButton) {
            const QSize sz(qMax(1, width()), qMax(1, height()));
            const QVector2D clickedRaster = rasterScreenToImage(e->position(), sz);
            m_rasterPan = clickedRaster;
            m_rasterZoom = std::clamp(
                m_rasterZoom * 1.35f,
                RenderWidgetInternal::kImageViewMinZoom,
                RenderWidgetInternal::kImageViewMaxZoom);
            update();
            e->accept();
            return;
        }
        if (e)
            e->accept();
        return;
    }
    if (!e || m_doc->meshCount() <= 0)
        return;
    if (e->button() != Qt::LeftButton)
        return;
    m_depthPickPos = e->position().toPoint();
    ++m_depthPickSequence;  /* bump sequence so stale async callbacks are rejected */
    m_depthPickPending = true;
    update();
    e->accept();
}

void RenderWidget::mouseReleaseEvent(QMouseEvent *e)
{
    emit viewActivated(this);
    if (m_viewMode == ViewMode::ParametrizationUV) {
        m_uvPanning = false;
        if (e)
            e->accept();
        return;
    }
    if (m_viewMode == ViewMode::RasterImage) {
        m_rasterPanning = false;
        if (e)
            e->accept();
        return;
    }
    m_trackball.mouseRelease(e);
    if (m_lightDragActive && e && e->buttons() == Qt::NoButton) {
        m_lightDragActive = false;
        update();
    }
}

void RenderWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (e && e->buttons() != Qt::NoButton)
        emit viewActivated(this);
    if (m_viewMode == ViewMode::ParametrizationUV) {
        if (!e || !m_uvPanning)
            return;
        const QPointF pos = e->position();
        const QSize sz(qMax(1, width()), qMax(1, height()));
        const auto screenToUv = [&](const QPointF &screenPos, float zoom, const QVector2D &pan) {
            const float aspect = float(sz.width()) / float(sz.height());
            const float xLim = (aspect >= 1.0f) ? aspect : 1.0f;
            const float yLim = (aspect >= 1.0f) ? 1.0f : (1.0f / qMax(1e-6f, aspect));
            const float ndcX = 2.0f * (float(screenPos.x()) / float(sz.width())) - 1.0f;
            const float ndcY = 1.0f - 2.0f * (float(screenPos.y()) / float(sz.height()));
            return QVector2D(
                pan.x() + ndcX * xLim / qMax(1e-6f, zoom),
                pan.y() + ndcY * yLim / qMax(1e-6f, zoom));
        };
        const QVector2D uvBefore = screenToUv(QPointF(m_uvLastMousePos), m_uvZoom, m_uvPan);
        const QVector2D uvAfter = screenToUv(pos, m_uvZoom, m_uvPan);
        m_uvPan += (uvBefore - uvAfter);
        m_uvLastMousePos = pos.toPoint();
        update();
        e->accept();
        return;
    }
    if (m_viewMode == ViewMode::RasterImage) {
        if (!e || !m_rasterPanning)
            return;
        const QPointF pos = e->position();
        const QSize sz(qMax(1, width()), qMax(1, height()));
        const QVector2D rasterBefore = rasterScreenToImage(QPointF(m_rasterLastMousePos), sz);
        const QVector2D rasterAfter = rasterScreenToImage(pos, sz);
        m_rasterPan += (rasterBefore - rasterAfter);
        m_rasterLastMousePos = pos.toPoint();
        update();
        if (e)
            e->accept();
        return;
    }
    if (m_lightDragActive) {
        if (e && (e->buttons() & Qt::LeftButton)) {
            const QPointF pos = e->position();
            const QPointF delta = pos - m_lightDragLastPos;
            m_lightDragLastPos = pos;
            if (!delta.isNull()) {
                // Map pixel delta to rotation: treat the widget as a trackball of radius min(w,h)/2
                const float r = float(qMin(qMax(1, width()), qMax(1, height()))) * 0.5f;
                const float dx = float(delta.x()) / r;
                const float dy = float(delta.y()) / r;
                // Rotation axis is perpendicular to delta, in view space
                QVector3D axis(-dy, dx, 0.0f);
                const float angle = axis.length() * 90.0f; // degrees
                if (angle > 1e-5f) {
                    axis.normalize();
                    const QQuaternion delta3d = QQuaternion::fromAxisAndAngle(axis, angle);
                    m_lightRotation = (delta3d * m_lightRotation).normalized();
                    update();
                }
            }
        }
        e->accept();
        return;
    }
    if (m_trackball.mouseMove(e, size()))
        update();
}

void RenderWidget::wheelEvent(QWheelEvent *e)
{
    emit viewActivated(this);
    if (m_viewMode == ViewMode::ParametrizationUV) {
        if (!e) {
            return;
        }
        const QPoint numDegrees = e->angleDelta();
        const float steps = float(numDegrees.y()) / 120.0f;
        if (std::abs(steps) < 1e-4f) {
            e->accept();
            return;
        }

        const QSize sz(qMax(1, width()), qMax(1, height()));
        if (sz.width() <= 0 || sz.height() <= 0) {
            e->accept();
            return;
        }

        const QPointF p = e->position();
        const float oldZoom = qMax(1e-6f, m_uvZoom);
        const float zoomFactor = std::pow(1.15f, steps);
        const float newZoom = std::clamp(oldZoom * zoomFactor, 0.05f, 5000.0f);

        const auto screenToUv = [&](const QPointF &screenPos, float zoom, const QVector2D &pan) {
            const float aspect = float(sz.width()) / float(sz.height());
            const float xLim = (aspect >= 1.0f) ? aspect : 1.0f;
            const float yLim = (aspect >= 1.0f) ? 1.0f : (1.0f / qMax(1e-6f, aspect));
            const float ndcX = 2.0f * (float(screenPos.x()) / float(sz.width())) - 1.0f;
            const float ndcY = 1.0f - 2.0f * (float(screenPos.y()) / float(sz.height()));
            return QVector2D(
                pan.x() + ndcX * xLim / qMax(1e-6f, zoom),
                pan.y() + ndcY * yLim / qMax(1e-6f, zoom));
        };

        const QVector2D uvBefore = screenToUv(p, oldZoom, m_uvPan);
        m_uvZoom = newZoom;
        const QVector2D uvAfter = screenToUv(p, m_uvZoom, m_uvPan);
        m_uvPan += (uvBefore - uvAfter);
        update();
        e->accept();
        return;
    }
    if (m_viewMode == ViewMode::RasterImage) {
        if (e) {
            const QPoint numDegrees = e->angleDelta();
            const float steps = float(numDegrees.y()) / 120.0f;
            if (std::abs(steps) >= 1e-4f) {
                if (e->modifiers() & Qt::ControlModifier) {
                    m_rasterOpacity = std::clamp(m_rasterOpacity + steps * 0.05f, 0.0f, 1.0f);
                    showInteractionStatusOverlay(
                        tr("Raster opacity: %1%").arg(std::lround(m_rasterOpacity * 100.0f)));
                    update();
                } else {
                    const QSize sz(qMax(1, width()), qMax(1, height()));
                    const QPointF p = e->position();
                    const QVector2D rasterBefore = rasterScreenToImage(p, sz);
                    const float oldZoom = qMax(1e-6f, m_rasterZoom);
                    const float zoomFactor = std::pow(1.15f, steps);
                    m_rasterZoom = std::clamp(
                        oldZoom * zoomFactor,
                        RenderWidgetInternal::kImageViewMinZoom,
                        RenderWidgetInternal::kImageViewMaxZoom);
                    const QVector2D rasterAfter = rasterScreenToImage(p, sz);
                    m_rasterPan += (rasterBefore - rasterAfter);
                    update();
                }
            }
            e->accept();
        }
        return;
    }
    cancelCenterAnimation();
    const Qt::KeyboardModifiers mods = e ? e->modifiers() : Qt::NoModifier;
    const bool nearClipMode = (mods & Qt::ControlModifier) && !(mods & Qt::ShiftModifier);
    const bool fovMode = (mods & Qt::ShiftModifier);
    if (m_trackball.wheel(e)) {
        if (nearClipMode) {
            showInteractionStatusOverlay(
                tr("Near clip: %1").arg(m_trackball.nearClipPlaneDistance(), 0, 'g', 5));
        } else if (fovMode) {
            showInteractionStatusOverlay(
                tr("FOV: %1 deg").arg(m_trackball.fovYDegrees(), 0, 'f', 1));
        }
        update();
    }
}

void RenderWidget::resizeEvent(QResizeEvent *e)
{
    QRhiWidget::resizeEvent(e);
    if (m_currentViewIndicator)
        m_currentViewIndicator->setGeometry(rect().adjusted(1, 1, -1, -1));
    updateQualityHistogramOverlay();
    layoutOverlayButtons();
}
