#include "rendersettingsjson.h"

namespace RenderSettingsJson {

bool fuzzyFloatEqual(float a, float b, float eps)
{
    return std::abs(a - b) <= eps;
}

bool parseFloatValue(const QJsonValue &value, float &outValue)
{
    if (!value.isDouble())
        return false;
    outValue = float(value.toDouble());
    return std::isfinite(outValue);
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

QJsonObject globalSettingsToJson(const GlobalRenderSettings &s, const GlobalRenderSettings *defaults)
{
    QJsonObject o;
    const GlobalRenderSettings def = defaults ? *defaults : GlobalRenderSettings{};
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
    putBool(QStringLiteral("show_view_cameras"), s.showViewCameras, def.showViewCameras);
    putBool(QStringLiteral("show_decorator_info"), s.showDecoratorInfo, def.showDecoratorInfo);
    putBool(
        QStringLiteral("fill_texture_nearest_sampling"),
        s.fillTextureNearestSampling,
        def.fillTextureNearestSampling);
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
    putInt(QStringLiteral("uv_texture_channel"), s.uvTextureChannel, def.uvTextureChannel);
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

bool parseGlobalSettings(const QJsonObject &obj, GlobalRenderSettings &out, QString *error)
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
        || !parseBoolField("show_view_cameras", out.showViewCameras)
        || !parseBoolField("show_decorator_info", out.showDecoratorInfo)
        || !parseBoolField("fill_texture_nearest_sampling", out.fillTextureNearestSampling)
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
        || !parseIntField("uv_texture_channel", out.uvTextureChannel)
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

QJsonObject perMeshSettingsToJson(
    const PerMeshRenderSettings &s,
    const PerMeshRenderSettings *defaults)
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
    putBool(QStringLiteral("decorator_normals"), s.decoratorNormals, def.decoratorNormals);
    putBool(
        QStringLiteral("decorator_vertex_normals"),
        s.decoratorVertexNormals,
        def.decoratorVertexNormals);
    putBool(
        QStringLiteral("decorator_face_normals"),
        s.decoratorFaceNormals,
        def.decoratorFaceNormals);
    putBool(QStringLiteral("decorator_boundary"), s.decoratorBoundary, def.decoratorBoundary);
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

bool parsePerMeshSettings(const QJsonObject &obj, PerMeshRenderSettings &out, QString *error)
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
        || !parseBoolField("decorator_normals", out.decoratorNormals)
        || !parseBoolField("decorator_vertex_normals", out.decoratorVertexNormals)
        || !parseBoolField("decorator_face_normals", out.decoratorFaceNormals)
        || !parseBoolField("decorator_boundary", out.decoratorBoundary)
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

    // State written before the decorators gained their master switches has no
    // "decorator_normals"/"decorator_boundary" key and encoded "pass on" by having a
    // sub-flag true. Those sub-flags used to default to false, so only the keys actually
    // present are evidence: reading the current defaults instead would switch the
    // decorator on for every old project that had it off.
    const auto trueIfPresent = [&](const char *key) {
        const QString k = QString::fromLatin1(key);
        return obj.contains(k) && obj.value(k).isBool() && obj.value(k).toBool();
    };
    if (!obj.contains(QStringLiteral("decorator_normals"))) {
        out.decoratorVertexNormals = trueIfPresent("decorator_vertex_normals");
        out.decoratorFaceNormals = trueIfPresent("decorator_face_normals");
        out.decoratorNormals = out.decoratorVertexNormals || out.decoratorFaceNormals
            || out.decoratorCurvatureDir;
    }
    if (!obj.contains(QStringLiteral("decorator_boundary"))) {
        out.decoratorBoundaryEdges = trueIfPresent("decorator_boundary_edges");
        out.decoratorTextureSeams = trueIfPresent("decorator_texture_seams");
        out.decoratorBoundary = out.decoratorBoundaryEdges || out.decoratorTextureSeams
            || out.decoratorNonManifoldEdges || out.decoratorNonManifoldVertices;
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

} // namespace RenderSettingsJson
