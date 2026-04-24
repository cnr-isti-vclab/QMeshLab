#include "renderwidget.h"
#include "colormap.h"
#include "document.h"
#include "renderoverlaypanel.h"
#include <wrap/io_trimesh/io_mask.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QVector3D>
#include <QVector4D>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

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

    QJsonObject trackball;
    trackball.insert(QStringLiteral("center"), vec3ToJsonArray(state.center));
    trackball.insert(QStringLiteral("rotation_xyzw"), quatToJsonArray(state.rotation));
    trackball.insert(QStringLiteral("distance"), state.distance);
    trackball.insert(QStringLiteral("radius"), state.radius);
    trackball.insert(QStringLiteral("fov_y_degrees"), state.fovYDeg);
    trackball.insert(QStringLiteral("gizmo_base_radius"), state.gizmoBaseRadius);
    trackball.insert(
        QStringLiteral("gizmo_reference_distance"),
        state.gizmoReferenceDistance);
    trackball.insert(
        QStringLiteral("gizmo_reference_fov_y_degrees"),
        state.gizmoReferenceFovYDeg);

    QJsonObject root;
    root.insert(QStringLiteral("kind"), QStringLiteral("QMeshLab.CameraTrackballState"));
    root.insert(QStringLiteral("version"), 1);
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
        if (!kind.isEmpty() && kind != QStringLiteral("QMeshLab.CameraTrackballState")) {
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

    ViewTrackball::State state = m_trackball.state();
    if (!parseVec3Value(trackballObj.value(QStringLiteral("center")), state.center))
        return fail(tr("Invalid camera JSON: 'center' must be [x, y, z]."));
    if (!parseQuatXyzwValue(trackballObj.value(QStringLiteral("rotation_xyzw")), state.rotation)) {
        return fail(tr("Invalid camera JSON: 'rotation_xyzw' must be [x, y, z, w]."));
    }

    if (!parseFloatValue(trackballObj.value(QStringLiteral("distance")), state.distance))
        return fail(tr("Invalid camera JSON: 'distance' must be a number."));
    if (!parseFloatValue(trackballObj.value(QStringLiteral("radius")), state.radius))
        return fail(tr("Invalid camera JSON: 'radius' must be a number."));
    if (!parseFloatValue(trackballObj.value(QStringLiteral("fov_y_degrees")), state.fovYDeg)) {
        return fail(tr("Invalid camera JSON: 'fov_y_degrees' must be a number."));
    }

    if (trackballObj.contains(QStringLiteral("gizmo_base_radius"))) {
        if (!parseFloatValue(
                trackballObj.value(QStringLiteral("gizmo_base_radius")),
                state.gizmoBaseRadius)) {
            return fail(tr("Invalid camera JSON: 'gizmo_base_radius' must be a number."));
        }
    } else {
        state.gizmoBaseRadius = qMax(1e-4f, state.radius * 1.02f);
    }

    if (trackballObj.contains(QStringLiteral("gizmo_reference_distance"))) {
        if (!parseFloatValue(
                trackballObj.value(QStringLiteral("gizmo_reference_distance")),
                state.gizmoReferenceDistance)) {
            return fail(
                tr("Invalid camera JSON: 'gizmo_reference_distance' must be a number."));
        }
    } else {
        state.gizmoReferenceDistance = state.distance;
    }

    if (trackballObj.contains(QStringLiteral("gizmo_reference_fov_y_degrees"))) {
        if (!parseFloatValue(
                trackballObj.value(QStringLiteral("gizmo_reference_fov_y_degrees")),
                state.gizmoReferenceFovYDeg)) {
            return fail(
                tr("Invalid camera JSON: 'gizmo_reference_fov_y_degrees' must be a number."));
        }
    } else {
        state.gizmoReferenceFovYDeg = state.fovYDeg;
    }

    m_trackball.setState(state);
    cancelCenterAnimation();
    m_reframeCameraRequested = false;
    m_resetTrackballRequested = false;

    if (errorMessage)
        errorMessage->clear();
    update();
    return true;
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
        m_renderSettings = settings;

        updateBoundingBoxCornersOverlay();
        if (prev.qualityHistogramBins != m_renderSettings.qualityHistogramBins
            || prev.qualityHistogramSource != m_renderSettings.qualityHistogramSource
            || prev.qualityHistogramFixedRange != m_renderSettings.qualityHistogramFixedRange
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
            const QVector4D transformed = meshEntry.renderTransform * QVector4D(corner, 1.0f);
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
        float minQ = std::numeric_limits<float>::max();
        float maxQ = -std::numeric_limits<float>::max();
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
                minQ = std::min(minQ, q);
                maxQ = std::max(maxQ, q);
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
                minQ = std::min(minQ, q);
                maxQ = std::max(maxQ, q);
            }
        }

        if (!values.empty()) {
            m_qualityHistogram.sampleCount = int(values.size());
            const float histMin = fixedRange ? fixedMin : minQ;
            const float histMax = fixedRange ? fixedMax : maxQ;
            m_qualityHistogram.minQ = histMin;
            m_qualityHistogram.maxQ = histMax;
            const float den = histMax - histMin;
            for (float q : values) {
                int idx = 0;
                if (std::abs(den) > 1e-12f) {
                    const float t = std::clamp((q - histMin) / den, 0.0f, 1.0f);
                    idx = std::min(bins - 1, int(t * bins));
                }
                m_qualityHistogram.counts[size_t(idx)] += 1;
            }
            m_qualityHistogram.valid = true;
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
    cancelCenterAnimation();
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
    if (!e || m_doc->meshCount() <= 0)
        return;
    if (e->button() != Qt::LeftButton)
        return;
    m_depthPickPos = e->position().toPoint();
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
    m_trackball.mouseRelease(e);
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
    cancelCenterAnimation();
    if (m_trackball.wheel(e))
        update();
}

void RenderWidget::resizeEvent(QResizeEvent *e)
{
    QRhiWidget::resizeEvent(e);
    if (m_currentViewIndicator)
        m_currentViewIndicator->setGeometry(rect().adjusted(1, 1, -1, -1));
    updateQualityHistogramOverlay();
    layoutOverlayButtons();
}
