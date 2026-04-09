#include "renderwidget.h"
#include "document.h"
#include "renderoverlaypanel.h"
#include <wrap/io_trimesh/io_mask.h>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QVector3D>
#include <QVector4D>
#include <vector>
#include <limits>
#include <cmath>

static QShader loadShader(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("Failed to open shader: %s", qPrintable(path));
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

namespace {
constexpr int kUbufSize = 288;
constexpr int kUbufFloatCount = kUbufSize / sizeof(float);
constexpr int kUbufBBoxColorOffset = 176 / sizeof(float);
constexpr int kUbufPointColorOffset = 192 / sizeof(float);
constexpr int kUbufPointParamsOffset = 208 / sizeof(float);
constexpr int kUbufWireColorOffset = 224 / sizeof(float);
constexpr int kUbufWireParamsOffset = 240 / sizeof(float);
constexpr int kUbufFillColorOffset = 256 / sizeof(float);
constexpr int kUbufLightingParamsOffset = 272 / sizeof(float);
constexpr int kFillVertexStrideFloats = 13;
constexpr int kPointsVertexStrideFloats = 11;
constexpr int kMaskMorphUbufSize = 16;
constexpr int kMaskDebugUbufSize = 16;
constexpr int kOutlineUbufSize = 32;
constexpr int kDecoratorUbufSize = 80; // mat4 mvp + vec4 color
constexpr int kDecoratorSlotVertexNormals = 0;
constexpr int kDecoratorSlotFaceNormals = 1;
constexpr int kDecoratorSlotBoundaryEdges = 2;
constexpr int kDecoratorSlotTextureSeams = 3;
constexpr int kDecoratorSlotCount = 4;
constexpr int kTrackballGizmoUbufSize = 80; // mat4 mvp + vec4(center.xyz, radius)
constexpr int kTrackballGizmoSteps = 96;
constexpr int kWireframeDefaultFaceThreshold = 10000;
constexpr float kPi = 3.14159265358979323846f;

QVector3D toVec3(const VCGMesh::CoordType &p)
{
    return QVector3D(p[0], p[1], p[2]);
}

float decodePackedDepthRgb8(const uchar *px, bool bgraOrder)
{
    const float c0 = (bgraOrder ? px[2] : px[0]) / 255.0f;
    const float c1 = px[1] / 255.0f;
    const float c2 = (bgraOrder ? px[0] : px[2]) / 255.0f;
    return c0 + c1 / 255.0f + c2 / 65025.0f;
}

std::vector<float> buildTrackballGizmoVertices()
{
    std::vector<float> v;
    v.reserve(kTrackballGizmoSteps * 2 * 3 * 6);

    auto append = [&v](const QVector3D &p, const QVector3D &c) {
        v.push_back(p.x());
        v.push_back(p.y());
        v.push_back(p.z());
        v.push_back(c.x());
        v.push_back(c.y());
        v.push_back(c.z());
    };

    auto emitCircle = [&](int axis, const QVector3D &color) {
        // axis: 0=XY(z=0), 1=YZ(x=0), 2=XZ(y=0)
        for (int i = 0; i < kTrackballGizmoSteps; ++i) {
            const float t0 = float(i) * 2.0f * kPi / float(kTrackballGizmoSteps);
            const float t1 = float(i + 1) * 2.0f * kPi / float(kTrackballGizmoSteps);
            QVector3D p0, p1;
            if (axis == 0) {
                p0 = QVector3D(std::cos(t0), std::sin(t0), 0.0f);
                p1 = QVector3D(std::cos(t1), std::sin(t1), 0.0f);
            } else if (axis == 1) {
                p0 = QVector3D(0.0f, std::cos(t0), std::sin(t0));
                p1 = QVector3D(0.0f, std::cos(t1), std::sin(t1));
            } else {
                p0 = QVector3D(std::cos(t0), 0.0f, std::sin(t0));
                p1 = QVector3D(std::cos(t1), 0.0f, std::sin(t1));
            }
            append(p0, color);
            append(p1, color);
        }
    };

    emitCircle(0, QVector3D(0.40f, 0.40f, 0.85f)); // XY - blue-ish
    emitCircle(1, QVector3D(0.40f, 0.85f, 0.40f)); // YZ - green-ish
    emitCircle(2, QVector3D(0.85f, 0.40f, 0.40f)); // XZ - red-ish
    return v;
}

const std::vector<float> &trackballGizmoVertices()
{
    static const std::vector<float> kVerts = buildTrackballGizmoVertices();
    return kVerts;
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
}

RenderWidget::RenderWidget(Document *doc, QWidget *parent)
    : QRhiWidget(parent), m_doc(doc)
{
    createOverlayButtons();
    ensureVisibilitySize();
    refreshColorSourceAvailability();

    connect(m_doc, &Document::meshAdded, this, [this](int index) {
        if (index >= 0 && index <= int(m_meshVisibility.size()))
            m_meshVisibility.insert(m_meshVisibility.begin() + index, true);
        else
            ensureVisibilitySize();
        m_reframeCameraRequested = true;
        applySceneDefaultRenderModeIfNeeded();
        refreshColorSourceAvailability();
        m_textureSrbs.clear();
        updateBoundingBoxCornersOverlay();
        layoutOverlayButtons();
        update();
    });
    connect(m_doc, &Document::meshRemoved, this, [this](int index) {
        if (index >= 0 && index < int(m_meshVisibility.size()))
            m_meshVisibility.erase(m_meshVisibility.begin() + index);
        else
            ensureVisibilitySize();
        m_reframeCameraRequested = true;
        if (m_doc->meshCount() == 0)
            m_applySceneDefaultRenderMode = true;
        refreshColorSourceAvailability();
        m_textureSrbs.clear();
        updateBoundingBoxCornersOverlay();
        layoutOverlayButtons();
        update();
    });
    connect(m_doc, &Document::currentMeshChanged, this, [this](int) {
        update();
    });
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

void RenderWidget::setShadingMode(ShadingMode mode)
{
    if (mode == ShadingMode::Wireframe) {
        const bool fillChanged = !m_renderSettings.showFill;
        const bool wireChanged = !m_renderSettings.showWire;
        m_renderSettings.showWire = true;
        m_renderSettings.showFill = true;

        if (fillChanged)
            m_fillPipeline.reset();
        if (wireChanged)
            m_wirePipeline.reset();

        if (m_overlayPanel) {
            m_overlayPanel->setSettings(m_renderSettings);
        }
        update();
        return;
    }

    if (m_shadingMode == mode)
        return;

    m_shadingMode = mode;
    m_renderSettings.fillShading = (mode == ShadingMode::Flat) ? FillShading::Flat : FillShading::Smooth;
    if (m_overlayPanel)
        m_overlayPanel->setSettings(m_renderSettings);
    m_fillPipeline.reset();
    update();
}

void RenderWidget::setRenderSettings(const RenderSettings &settings)
{
    const RenderSettings prev = m_renderSettings;
    m_renderSettings = settings;
    m_shadingMode = (m_renderSettings.fillShading == FillShading::Flat)
        ? ShadingMode::Flat
        : ShadingMode::Smooth;

    if (prev.showFill != m_renderSettings.showFill
        || prev.fillShading != m_renderSettings.fillShading
        || prev.fillBackfaceCulling != m_renderSettings.fillBackfaceCulling) {
        m_fillPipeline.reset();
    }
    if (prev.showWire != m_renderSettings.showWire
        || prev.wireBackfaceCulling != m_renderSettings.wireBackfaceCulling) {
        m_wirePipeline.reset();
    }

    if (m_overlayPanel)
        m_overlayPanel->setSettings(m_renderSettings);
    updateBoundingBoxCornersOverlay();
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

void RenderWidget::resetCameraToScene()
{
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
    m_overlayPanel->setSettings(m_renderSettings);
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

    connect(m_overlayPanel, &RenderOverlayPanel::settingsChanged, this,
            [this](const RenderSettings &settings) {
        emit viewActivated(this);
        const RenderSettings prev = m_renderSettings;
        m_renderSettings = settings;

        m_shadingMode = (m_renderSettings.fillShading == FillShading::Flat)
            ? ShadingMode::Flat
            : ShadingMode::Smooth;

        if (prev.showFill != m_renderSettings.showFill
            || prev.fillShading != m_renderSettings.fillShading
            || prev.fillBackfaceCulling != m_renderSettings.fillBackfaceCulling) {
            m_fillPipeline.reset();
        }
        if (prev.showWire != m_renderSettings.showWire
            || prev.wireBackfaceCulling != m_renderSettings.wireBackfaceCulling) {
            m_wirePipeline.reset();
        }
        updateBoundingBoxCornersOverlay();
        update();
        layoutOverlayButtons();
    });

    updateBoundingBoxCornersOverlay();
    layoutOverlayButtons();
}

void RenderWidget::layoutOverlayButtons()
{
    constexpr int kOverlayMargin = 8;
    const int maxOverlayWidth = qMax(120, width() - 2 * kOverlayMargin);

    if (m_overlayPanel) {
        m_overlayPanel->setMaximumWidth(maxOverlayWidth);
        m_overlayPanel->adjustSize();
        m_overlayPanel->move(kOverlayMargin, kOverlayMargin);
        m_overlayPanel->raise();
    }
}

bool RenderWidget::computeVisibleSceneBoundingBox(QVector3D &minCorner, QVector3D &maxCorner) const
{
    bool hasBox = false;
    vcg::Box3f sceneBox;
    for (int i = 0; i < m_doc->meshCount(); ++i) {
        const auto &meshEntry = m_doc->mesh(i);
        if (!meshVisible(i))
            continue;
        if (meshEntry.mesh.bbox.IsNull())
            continue;
        if (!hasBox) {
            sceneBox = meshEntry.mesh.bbox;
            hasBox = true;
        } else {
            sceneBox.Add(meshEntry.mesh.bbox);
        }
    }

    if (!hasBox || sceneBox.IsNull())
        return false;

    minCorner = QVector3D(sceneBox.min[0], sceneBox.min[1], sceneBox.min[2]);
    maxCorner = QVector3D(sceneBox.max[0], sceneBox.max[1], sceneBox.max[2]);
    return true;
}

void RenderWidget::updateBoundingBoxCornersOverlay()
{
    if (!m_bboxMinCornerOverlayLabel || !m_bboxMaxCornerOverlayLabel
        || !m_bboxDimXOverlayLabel || !m_bboxDimYOverlayLabel || !m_bboxDimZOverlayLabel)
        return;

    const bool showCorners = m_renderSettings.showBoundingBoxCorners;
    const bool showDimensions = m_renderSettings.showBoundingBoxDimensions;
    if (!m_renderSettings.showBoundingBox || (!showCorners && !showDimensions)) {
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

void RenderWidget::applySceneDefaultRenderModeIfNeeded()
{
    if (!m_applySceneDefaultRenderMode)
        return;

    if (m_doc->meshCount() <= 0)
        return;

    m_applySceneDefaultRenderMode = false;

    bool hasFaces = false;
    int firstFaceMeshFaces = 0;
    bool hasVertexColors = false;
    bool hasFaceColors = false;
    bool hasVertexNormals = false;
    for (int i = 0; i < m_doc->meshCount(); ++i) {
        const int faceCount = m_doc->mesh(i).mesh.FN();
        if (faceCount > 0) {
            hasFaces = true;
            firstFaceMeshFaces = faceCount;
            break;
        }
        const int mask = m_doc->mesh(i).ioMask;
        hasVertexColors = hasVertexColors || ((mask & vcg::tri::io::Mask::IOM_VERTCOLOR) != 0);
        hasFaceColors = hasFaceColors || ((mask & vcg::tri::io::Mask::IOM_FACECOLOR) != 0);
        hasVertexNormals = hasVertexNormals || ((mask & vcg::tri::io::Mask::IOM_VERTNORMAL) != 0);
    }

    if (hasFaces)
    {
        const bool showWireByDefault =
            (firstFaceMeshFaces > 0) && (firstFaceMeshFaces < kWireframeDefaultFaceThreshold);
        if (m_renderSettings.showWire != showWireByDefault)
            m_wirePipeline.reset();
        m_renderSettings.showWire = showWireByDefault;
        if (!m_renderSettings.showWire && m_renderSettings.currentPass == RenderPass::Wireframe)
            m_renderSettings.currentPass = RenderPass::Fill;

        m_renderSettings.fillColorSource = FillColorSource::Constant;
        if (hasVertexColors)
            m_renderSettings.fillColorSource = FillColorSource::PerVertex;
        else if (hasFaceColors)
            m_renderSettings.fillColorSource = FillColorSource::PerFace;
        if (m_overlayPanel)
            m_overlayPanel->setSettings(m_renderSettings);
        return;
    }

    m_renderSettings.showPoints = true;
    m_renderSettings.showWire = false;
    m_renderSettings.showFill = false;
    m_renderSettings.currentPass = RenderPass::Points;
    m_renderSettings.pointColorSource = hasVertexColors
        ? PointColorSource::PerVertex
        : PointColorSource::Constant;
    m_renderSettings.pointLighting = hasVertexNormals;

    m_shadingMode = ShadingMode::Smooth;
    m_fillPipeline.reset();
    m_wirePipeline.reset();

    if (m_overlayPanel)
        m_overlayPanel->setSettings(m_renderSettings);
}

void RenderWidget::refreshColorSourceAvailability()
{
    bool hasVertexColors = false;
    bool hasFaceColors = false;
    bool hasVertexNormals = false;
    for (int i = 0; i < m_doc->meshCount(); ++i) {
        const int mask = m_doc->mesh(i).ioMask;
        hasVertexColors = hasVertexColors || ((mask & vcg::tri::io::Mask::IOM_VERTCOLOR) != 0);
        hasFaceColors = hasFaceColors || ((mask & vcg::tri::io::Mask::IOM_FACECOLOR) != 0);
        hasVertexNormals = hasVertexNormals || ((mask & vcg::tri::io::Mask::IOM_VERTNORMAL) != 0);
    }

    if (m_overlayPanel)
        m_overlayPanel->setPointColorSourceAvailability(hasVertexColors);
    if (m_overlayPanel)
        m_overlayPanel->setPointLightingAvailability(hasVertexNormals);
    if (m_overlayPanel)
        m_overlayPanel->setFillColorSourceAvailability(hasVertexColors, hasFaceColors);

    RenderSettings corrected = m_renderSettings;
    if (corrected.pointColorSource == PointColorSource::PerVertex && !hasVertexColors)
        corrected.pointColorSource = PointColorSource::Constant;
    if (corrected.pointLighting && !hasVertexNormals)
        corrected.pointLighting = false;
    if (corrected.fillColorSource == FillColorSource::PerVertex && !hasVertexColors)
        corrected.fillColorSource = FillColorSource::Constant;
    if (corrected.fillColorSource == FillColorSource::PerFace && !hasFaceColors)
        corrected.fillColorSource = FillColorSource::Constant;

    if (corrected != m_renderSettings) {
        m_renderSettings = corrected;
        if (m_overlayPanel)
            m_overlayPanel->setSettings(m_renderSettings);
    }
}

int RenderWidget::fillGpuVariantIndexForCurrentSettings() const
{
    switch (m_renderSettings.fillColorSource) {
    case FillColorSource::PerVertex: return static_cast<int>(Document::FillGpuVariant::PerVertex);
    case FillColorSource::PerFace: return static_cast<int>(Document::FillGpuVariant::PerFace);
    case FillColorSource::Constant:
    default:
        return static_cast<int>(Document::FillGpuVariant::Constant);
    }
}

int RenderWidget::pointGpuVariantIndexForCurrentSettings() const
{
    switch (m_renderSettings.pointColorSource) {
    case PointColorSource::PerVertex: return static_cast<int>(Document::PointGpuVariant::PerVertex);
    case PointColorSource::Constant:
    default:
        return static_cast<int>(Document::PointGpuVariant::Constant);
    }
}

QRhiShaderResourceBindings *RenderWidget::shaderResourcesForTexture(QRhiTexture *texture)
{
    if (!texture || !m_rhi || !m_ubuf || !m_textureSampler)
        return m_srb.get();

    auto it = m_textureSrbs.find(texture);
    if (it != m_textureSrbs.end())
        return it->second.get();

    auto textureSrb = std::unique_ptr<QRhiShaderResourceBindings>(m_rhi->newShaderResourceBindings());
    textureSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0,
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_ubuf.get()),
        QRhiShaderResourceBinding::sampledTexture(
            1,
            QRhiShaderResourceBinding::FragmentStage,
            texture,
            m_textureSampler.get())
    });
    if (!textureSrb->create())
        return m_srb.get();

    QRhiShaderResourceBindings *raw = textureSrb.get();
    m_textureSrbs.emplace(texture, std::move(textureSrb));
    return raw;
}

void RenderWidget::updateCameraFrameIfNeeded()
{
    if (!m_reframeCameraRequested)
        return;
    if (m_doc->meshCount() == 0)
        return;

    vcg::Box3f bbox;
    bool hasVisibleMesh = false;
    for (int i = 0; i < m_doc->meshCount(); ++i) {
        if (!meshVisible(i))
            continue;
        bbox.Add(m_doc->mesh(i).mesh.bbox);
        hasVisibleMesh = true;
    }
    if (!hasVisibleMesh)
        return;
    if (bbox.IsNull())
        return;

    auto c = bbox.Center();
    const QVector3D center(c[0], c[1], c[2]);
    const float radius = qMax(1e-4f, bbox.Diag() / 2.0f);
    if (m_resetTrackballRequested)
        m_trackball.resetToFrame(center, radius, radius * 3.0f);
    else
        m_trackball.setFrame(center, radius, radius * 3.0f);
    m_reframeCameraRequested = false;
    m_resetTrackballRequested = false;
    m_centerAnimActive = false;
}

void RenderWidget::ensureCurrentMeshMaskResources(const QSize &pixelSize)
{
    if (!m_rhi || pixelSize.isEmpty())
        return;

    if (m_currentMaskRt && m_currentMaskSize == pixelSize)
        return;

    m_currentMaskFillPipeline.reset();
    m_currentMaskPointsPipeline.reset();
    m_maskMorphMaskToBaseSrb.reset();
    m_maskMorphMaskToWorkSrb.reset();
    m_maskMorphWorkToMaskSrb.reset();
    m_maskMorphToBasePipeline.reset();
    m_maskMorphToWorkPipeline.reset();
    m_maskMorphWorkToMaskPipeline.reset();
    m_maskDebugBaseSrb.reset();
    m_maskDebugWorkSrb.reset();
    m_maskDebugMaskSrb.reset();
    m_maskDebugPipeline.reset();
    m_outlineSrb.reset();
    m_outlinePipeline.reset();
    m_currentMaskBaseRt.reset();
    m_currentMaskBaseRp.reset();
    m_currentMaskBaseTexture.reset();
    m_currentMaskRt.reset();
    m_currentMaskRp.reset();
    m_currentMaskDepth.reset();
    m_currentMaskTexture.reset();
    m_currentMaskWorkRt.reset();
    m_currentMaskWorkRp.reset();
    m_currentMaskWorkTexture.reset();

    m_currentMaskTexture.reset(
        m_rhi->newTexture(
            QRhiTexture::RGBA8,
            pixelSize,
            1,
            QRhiTexture::RenderTarget));
    if (!m_currentMaskTexture || !m_currentMaskTexture->create()) {
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    m_currentMaskDepth.reset(m_rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixelSize, 1));
    if (!m_currentMaskDepth || !m_currentMaskDepth->create()) {
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    QRhiTextureRenderTargetDescription rtDesc(QRhiColorAttachment(m_currentMaskTexture.get()));
    rtDesc.setDepthStencilBuffer(m_currentMaskDepth.get());
    m_currentMaskRt.reset(m_rhi->newTextureRenderTarget(rtDesc));
    if (!m_currentMaskRt) {
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    m_currentMaskRp.reset(m_currentMaskRt->newCompatibleRenderPassDescriptor());
    m_currentMaskRt->setRenderPassDescriptor(m_currentMaskRp.get());
    if (!m_currentMaskRt->create()) {
        m_currentMaskRp.reset();
        m_currentMaskRt.reset();
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    m_currentMaskBaseTexture.reset(
        m_rhi->newTexture(
            QRhiTexture::RGBA8,
            pixelSize,
            1,
            QRhiTexture::RenderTarget));
    if (!m_currentMaskBaseTexture || !m_currentMaskBaseTexture->create()) {
        m_currentMaskBaseTexture.reset();
        m_currentMaskRp.reset();
        m_currentMaskRt.reset();
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    QRhiTextureRenderTargetDescription baseRtDesc(QRhiColorAttachment(m_currentMaskBaseTexture.get()));
    m_currentMaskBaseRt.reset(m_rhi->newTextureRenderTarget(baseRtDesc));
    if (!m_currentMaskBaseRt) {
        m_currentMaskBaseTexture.reset();
        m_currentMaskRp.reset();
        m_currentMaskRt.reset();
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    m_currentMaskBaseRp.reset(m_currentMaskBaseRt->newCompatibleRenderPassDescriptor());
    m_currentMaskBaseRt->setRenderPassDescriptor(m_currentMaskBaseRp.get());
    if (!m_currentMaskBaseRt->create()) {
        m_currentMaskBaseRp.reset();
        m_currentMaskBaseRt.reset();
        m_currentMaskBaseTexture.reset();
        m_currentMaskRp.reset();
        m_currentMaskRt.reset();
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    m_currentMaskWorkTexture.reset(
        m_rhi->newTexture(
            QRhiTexture::RGBA8,
            pixelSize,
            1,
            QRhiTexture::RenderTarget));
    if (!m_currentMaskWorkTexture || !m_currentMaskWorkTexture->create()) {
        m_currentMaskWorkTexture.reset();
        m_currentMaskBaseRp.reset();
        m_currentMaskBaseRt.reset();
        m_currentMaskBaseTexture.reset();
        m_currentMaskRp.reset();
        m_currentMaskRt.reset();
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    QRhiTextureRenderTargetDescription workRtDesc(QRhiColorAttachment(m_currentMaskWorkTexture.get()));
    m_currentMaskWorkRt.reset(m_rhi->newTextureRenderTarget(workRtDesc));
    if (!m_currentMaskWorkRt) {
        m_currentMaskWorkTexture.reset();
        m_currentMaskBaseRp.reset();
        m_currentMaskBaseRt.reset();
        m_currentMaskBaseTexture.reset();
        m_currentMaskRp.reset();
        m_currentMaskRt.reset();
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    m_currentMaskWorkRp.reset(m_currentMaskWorkRt->newCompatibleRenderPassDescriptor());
    m_currentMaskWorkRt->setRenderPassDescriptor(m_currentMaskWorkRp.get());
    if (!m_currentMaskWorkRt->create()) {
        m_currentMaskWorkRp.reset();
        m_currentMaskWorkRt.reset();
        m_currentMaskWorkTexture.reset();
        m_currentMaskBaseRp.reset();
        m_currentMaskBaseRt.reset();
        m_currentMaskBaseTexture.reset();
        m_currentMaskRp.reset();
        m_currentMaskRt.reset();
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    m_currentMaskSize = pixelSize;
}

void RenderWidget::ensureDepthPickResources(const QSize &pixelSize)
{
    if (!m_rhi || pixelSize.isEmpty())
        return;

    if (m_depthPickRt && m_depthPickSize == pixelSize)
        return;

    m_depthPickFillPipeline.reset();
    m_depthPickPointsPipeline.reset();
    m_depthPickSrb.reset();
    m_depthPickRp.reset();
    m_depthPickRt.reset();
    m_depthPickDepth.reset();
    m_depthPickTexture.reset();

    m_depthPickTexture.reset(
        m_rhi->newTexture(
            QRhiTexture::RGBA8,
            pixelSize,
            1,
            QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    if (!m_depthPickTexture || !m_depthPickTexture->create()) {
        m_depthPickTexture.reset();
        m_depthPickSize = QSize();
        return;
    }

    m_depthPickDepth.reset(m_rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixelSize, 1));
    if (!m_depthPickDepth || !m_depthPickDepth->create()) {
        m_depthPickDepth.reset();
        m_depthPickTexture.reset();
        m_depthPickSize = QSize();
        return;
    }

    QRhiTextureRenderTargetDescription rtDesc(QRhiColorAttachment(m_depthPickTexture.get()));
    rtDesc.setDepthStencilBuffer(m_depthPickDepth.get());
    m_depthPickRt.reset(m_rhi->newTextureRenderTarget(rtDesc));
    if (!m_depthPickRt) {
        m_depthPickDepth.reset();
        m_depthPickTexture.reset();
        m_depthPickSize = QSize();
        return;
    }

    m_depthPickRp.reset(m_depthPickRt->newCompatibleRenderPassDescriptor());
    m_depthPickRt->setRenderPassDescriptor(m_depthPickRp.get());
    if (!m_depthPickRt->create()) {
        m_depthPickRp.reset();
        m_depthPickRt.reset();
        m_depthPickDepth.reset();
        m_depthPickTexture.reset();
        m_depthPickSize = QSize();
        return;
    }

    if (!m_depthPickSrb && m_ubuf) {
        m_depthPickSrb.reset(m_rhi->newShaderResourceBindings());
        m_depthPickSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::VertexStage,
                m_ubuf.get())
        });
        if (!m_depthPickSrb->create())
            m_depthPickSrb.reset();
    }

    if (m_depthPickSrb && !m_depthPickFillPipeline) {
        m_depthPickFillPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/depth_pick.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/depth_pick.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load depth-pick fill shaders");
            m_depthPickFillPipeline.reset();
        } else {
            m_depthPickFillPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_depthPickFillPipeline->setDepthTest(true);
            m_depthPickFillPipeline->setDepthWrite(true);
            m_depthPickFillPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            layout.setBindings({ { kFillVertexStrideFloats * sizeof(float) } });
            layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
            m_depthPickFillPipeline->setVertexInputLayout(layout);
            m_depthPickFillPipeline->setShaderResourceBindings(m_depthPickSrb.get());
            m_depthPickFillPipeline->setRenderPassDescriptor(m_depthPickRp.get());
            if (!m_depthPickFillPipeline->create()) {
                qWarning("Failed to create depth-pick fill pipeline");
                m_depthPickFillPipeline.reset();
            }
        }
    }

    if (m_depthPickSrb && !m_depthPickPointsPipeline) {
        m_depthPickPointsPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/depth_pick.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/depth_pick.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load depth-pick points shaders");
            m_depthPickPointsPipeline.reset();
        } else {
            m_depthPickPointsPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_depthPickPointsPipeline->setTopology(QRhiGraphicsPipeline::Points);
            m_depthPickPointsPipeline->setDepthTest(true);
            m_depthPickPointsPipeline->setDepthWrite(true);
            m_depthPickPointsPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            layout.setBindings({ { kPointsVertexStrideFloats * sizeof(float) } });
            layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
            m_depthPickPointsPipeline->setVertexInputLayout(layout);
            m_depthPickPointsPipeline->setShaderResourceBindings(m_depthPickSrb.get());
            m_depthPickPointsPipeline->setRenderPassDescriptor(m_depthPickRp.get());
            if (!m_depthPickPointsPipeline->create()) {
                qWarning("Failed to create depth-pick points pipeline");
                m_depthPickPointsPipeline.reset();
            }
        }
    }

    m_depthPickSize = pixelSize;
}

void RenderWidget::ensureRenderResources()
{
    if (m_rhi != rhi()) {
        if (m_rhi)
            m_doc->releaseRhiGpuResources(m_rhi);
        m_rhi = rhi();
        m_fillPipeline.reset();
        m_wirePipeline.reset();
        m_bboxPipeline.reset();
        m_pointsPipeline.reset();
        m_decoratorPipeline.reset();
        for (auto &srb : m_decoratorSrbs)
            srb.reset();
        for (auto &ubuf : m_decoratorUbufs)
            ubuf.reset();
        m_depthPickTexture.reset();
        m_depthPickDepth.reset();
        m_depthPickRt.reset();
        m_depthPickRp.reset();
        m_depthPickSize = QSize();
        m_depthPickSrb.reset();
        m_depthPickFillPipeline.reset();
        m_depthPickPointsPipeline.reset();
        m_depthPickPending = false;
        m_depthPickInFlight = false;
        m_depthPickReadbackResult.reset();
        m_currentMaskBaseTexture.reset();
        m_currentMaskBaseRt.reset();
        m_currentMaskBaseRp.reset();
        m_currentMaskTexture.reset();
        m_currentMaskDepth.reset();
        m_currentMaskRt.reset();
        m_currentMaskRp.reset();
        m_currentMaskWorkTexture.reset();
        m_currentMaskWorkRt.reset();
        m_currentMaskWorkRp.reset();
        m_currentMaskSize = QSize();
        m_currentMaskFillPipeline.reset();
        m_currentMaskPointsPipeline.reset();
        m_maskMorphCopyUbuf.reset();
        m_maskMorphDilateUbuf.reset();
        m_maskMorphErodeUbuf.reset();
        m_maskMorphSampler.reset();
        m_maskMorphMaskToBaseSrb.reset();
        m_maskMorphMaskToWorkSrb.reset();
        m_maskMorphWorkToMaskSrb.reset();
        m_maskMorphToBasePipeline.reset();
        m_maskMorphToWorkPipeline.reset();
        m_maskMorphWorkToMaskPipeline.reset();
        m_maskDebugUbuf.reset();
        m_maskDebugBaseSrb.reset();
        m_maskDebugWorkSrb.reset();
        m_maskDebugMaskSrb.reset();
        m_maskDebugPipeline.reset();
        m_outlineUbuf.reset();
        m_outlineSampler.reset();
        m_outlineSrb.reset();
        m_outlinePipeline.reset();
        m_trackballGizmoUbuf.reset();
        m_trackballGizmoVbuf.reset();
        m_trackballGizmoSrb.reset();
        m_trackballGizmoPipeline.reset();
        m_trackballGizmoVertexCount = 0;
        m_srb.reset();
        m_ubuf.reset();
        m_textureSampler.reset();
        m_fallbackTexture.reset();
        m_fallbackTextureUploadPending = false;
        m_textureSrbs.clear();
    }

    if (!m_rhi || !renderTarget())
        return;

    if (!m_ubuf) {
        // Uniform buffer: mvp + modelView + normalMat + bbox/point/wire/fill parameters
        m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUbufSize));
        m_ubuf->create();
    }

    if (!m_textureSampler) {
        m_textureSampler.reset(
            m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                              QRhiSampler::Repeat, QRhiSampler::Repeat));
        m_textureSampler->create();
    }

    if (!m_fallbackTexture) {
        m_fallbackTexture.reset(
            m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1));
        if (m_fallbackTexture && m_fallbackTexture->create()) {
            m_fallbackTextureUploadPending = true;
        } else {
            m_fallbackTexture.reset();
            m_fallbackTextureUploadPending = false;
        }
    }

    ensureCurrentMeshMaskResources(renderTarget()->pixelSize());

    if (!m_srb) {
        m_srb.reset(m_rhi->newShaderResourceBindings());
        m_srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                m_ubuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_fallbackTexture.get(),
                m_textureSampler.get())
        });
        m_srb->create();
    }

    for (int slot = 0; slot < kDecoratorSlotCount; ++slot) {
        if (!m_decoratorUbufs[slot]) {
            m_decoratorUbufs[slot].reset(
                m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kDecoratorUbufSize));
            m_decoratorUbufs[slot]->create();
        }
        if (!m_decoratorSrbs[slot] && m_decoratorUbufs[slot]) {
            m_decoratorSrbs[slot].reset(m_rhi->newShaderResourceBindings());
            m_decoratorSrbs[slot]->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(
                    0,
                    QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                    m_decoratorUbufs[slot].get())
            });
            m_decoratorSrbs[slot]->create();
        }
    }

    if (!m_outlineUbuf) {
        m_outlineUbuf.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kOutlineUbufSize));
        m_outlineUbuf->create();
    }
    if (!m_outlineSampler) {
        m_outlineSampler.reset(
            m_rhi->newSampler(
                QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        m_outlineSampler->create();
    }
    if (!m_maskMorphCopyUbuf) {
        m_maskMorphCopyUbuf.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kMaskMorphUbufSize));
        m_maskMorphCopyUbuf->create();
    }
    if (!m_maskMorphDilateUbuf) {
        m_maskMorphDilateUbuf.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kMaskMorphUbufSize));
        m_maskMorphDilateUbuf->create();
    }
    if (!m_maskMorphErodeUbuf) {
        m_maskMorphErodeUbuf.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kMaskMorphUbufSize));
        m_maskMorphErodeUbuf->create();
    }
    if (!m_maskMorphSampler) {
        m_maskMorphSampler.reset(
            m_rhi->newSampler(
                QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        m_maskMorphSampler->create();
    }
    if (!m_maskDebugUbuf) {
        m_maskDebugUbuf.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kMaskDebugUbufSize));
        m_maskDebugUbuf->create();
    }
    if (!m_maskMorphMaskToBaseSrb
        && m_maskMorphCopyUbuf
        && m_maskMorphSampler
        && m_currentMaskTexture
        && m_currentMaskBaseTexture) {
        m_maskMorphMaskToBaseSrb.reset(m_rhi->newShaderResourceBindings());
        m_maskMorphMaskToBaseSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::FragmentStage,
                m_maskMorphCopyUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskTexture.get(),
                m_maskMorphSampler.get())
        });
        m_maskMorphMaskToBaseSrb->create();
    }
    if (!m_maskMorphMaskToWorkSrb
        && m_maskMorphDilateUbuf
        && m_maskMorphSampler
        && m_currentMaskBaseTexture
        && m_currentMaskWorkTexture) {
        m_maskMorphMaskToWorkSrb.reset(m_rhi->newShaderResourceBindings());
        m_maskMorphMaskToWorkSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::FragmentStage,
                m_maskMorphDilateUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskBaseTexture.get(),
                m_maskMorphSampler.get())
        });
        m_maskMorphMaskToWorkSrb->create();
    }
    if (!m_maskMorphWorkToMaskSrb
        && m_maskMorphErodeUbuf
        && m_maskMorphSampler
        && m_currentMaskTexture
        && m_currentMaskWorkTexture) {
        m_maskMorphWorkToMaskSrb.reset(m_rhi->newShaderResourceBindings());
        m_maskMorphWorkToMaskSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::FragmentStage,
                m_maskMorphErodeUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskWorkTexture.get(),
                m_maskMorphSampler.get())
        });
        m_maskMorphWorkToMaskSrb->create();
    }
    if (!m_maskDebugBaseSrb
        && m_maskDebugUbuf
        && m_maskMorphSampler
        && m_currentMaskBaseTexture) {
        m_maskDebugBaseSrb.reset(m_rhi->newShaderResourceBindings());
        m_maskDebugBaseSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::FragmentStage,
                m_maskDebugUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskBaseTexture.get(),
                m_maskMorphSampler.get())
        });
        m_maskDebugBaseSrb->create();
    }
    if (!m_maskDebugWorkSrb
        && m_maskDebugUbuf
        && m_maskMorphSampler
        && m_currentMaskWorkTexture) {
        m_maskDebugWorkSrb.reset(m_rhi->newShaderResourceBindings());
        m_maskDebugWorkSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::FragmentStage,
                m_maskDebugUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskWorkTexture.get(),
                m_maskMorphSampler.get())
        });
        m_maskDebugWorkSrb->create();
    }
    if (!m_maskDebugMaskSrb
        && m_maskDebugUbuf
        && m_maskMorphSampler
        && m_currentMaskTexture) {
        m_maskDebugMaskSrb.reset(m_rhi->newShaderResourceBindings());
        m_maskDebugMaskSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::FragmentStage,
                m_maskDebugUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskTexture.get(),
                m_maskMorphSampler.get())
        });
        m_maskDebugMaskSrb->create();
    }
    if (!m_outlineSrb && m_outlineUbuf && m_outlineSampler && m_currentMaskTexture) {
        m_outlineSrb.reset(m_rhi->newShaderResourceBindings());
        m_outlineSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::FragmentStage,
                m_outlineUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskTexture.get(),
                m_outlineSampler.get())
        });
        m_outlineSrb->create();
    }

    if (!m_renderSettings.showFill) {
        m_fillPipeline.reset();
    } else if (!m_fillPipeline) {
        m_fillPipeline.reset(m_rhi->newGraphicsPipeline());

        QString vsPath;
        QString fsPath;
        switch (m_renderSettings.fillShading) {
        case FillShading::Smooth:
            vsPath = QStringLiteral(":/shaders/fill_smooth.vert.qsb");
            fsPath = QStringLiteral(":/shaders/fill_smooth.frag.qsb");
            break;
        case FillShading::Flat:
            vsPath = QStringLiteral(":/shaders/fill_flat.vert.qsb");
            fsPath = QStringLiteral(":/shaders/fill_flat.frag.qsb");
            break;
        }

        QShader vs = loadShader(vsPath);
        QShader fs = loadShader(fsPath);
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load shaders");
            m_fillPipeline.reset();
            return;
        }

        m_fillPipeline->setShaderStages({
            { QRhiShaderStage::Vertex, vs },
            { QRhiShaderStage::Fragment, fs }
        });

        m_fillPipeline->setDepthTest(true);
        m_fillPipeline->setDepthWrite(true);
        m_fillPipeline->setCullMode(
            m_renderSettings.fillBackfaceCulling
                ? QRhiGraphicsPipeline::Back
                : QRhiGraphicsPipeline::None);

        QRhiVertexInputLayout inputLayout;
        // Fill vertex layout: position(3f) + normal(3f) + meshColor(4f) + texInfo(uv + useTexture flag).
        inputLayout.setBindings({ { kFillVertexStrideFloats * sizeof(float) } });
        if (m_renderSettings.fillShading == FillShading::Flat) {
            inputLayout.setAttributes({
                { 0, 0, QRhiVertexInputAttribute::Float3, 0 },                 // position
                { 0, 1, QRhiVertexInputAttribute::Float4, 6 * sizeof(float) }, // mesh color + use flag
                { 0, 2, QRhiVertexInputAttribute::Float3, 10 * sizeof(float) } // uv + use texture flag
            });
        } else {
            inputLayout.setAttributes({
                { 0, 0, QRhiVertexInputAttribute::Float3, 0 },                 // position
                { 0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float) }, // normal
                { 0, 2, QRhiVertexInputAttribute::Float4, 6 * sizeof(float) }, // mesh color + use flag
                { 0, 3, QRhiVertexInputAttribute::Float3, 10 * sizeof(float) } // uv + use texture flag
            });
        }
        m_fillPipeline->setVertexInputLayout(inputLayout);
        m_fillPipeline->setShaderResourceBindings(m_srb.get());
        m_fillPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

        if (!m_fillPipeline->create()) {
            qWarning("Failed to create fill pipeline");
            m_fillPipeline.reset();
        }
    }

    if (!m_renderSettings.showWire) {
        m_wirePipeline.reset();
    } else if (!m_wirePipeline) {
        m_wirePipeline.reset(m_rhi->newGraphicsPipeline());

        QShader vs = loadShader(QStringLiteral(":/shaders/fill_wire.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/fill_wire_overlay.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load wireframe shaders");
            m_wirePipeline.reset();
            return;
        }

        m_wirePipeline->setShaderStages({
            { QRhiShaderStage::Vertex, vs },
            { QRhiShaderStage::Fragment, fs }
        });
        m_wirePipeline->setDepthTest(true);
        m_wirePipeline->setDepthWrite(false);
        m_wirePipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        m_wirePipeline->setCullMode(
            m_renderSettings.wireBackfaceCulling
                ? QRhiGraphicsPipeline::Back
                : QRhiGraphicsPipeline::None);

        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.opColor = QRhiGraphicsPipeline::Add;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.opAlpha = QRhiGraphicsPipeline::Add;
        m_wirePipeline->setTargetBlends({ blend });

        QRhiVertexInputLayout wireLayout;
        wireLayout.setBindings({ { 6 * sizeof(float) } });
        wireLayout.setAttributes({
            { 0, 0, QRhiVertexInputAttribute::Float3, 0 },             // position
            { 0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float) } // barycentric
        });
        m_wirePipeline->setVertexInputLayout(wireLayout);
        m_wirePipeline->setShaderResourceBindings(m_srb.get());
        m_wirePipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

        if (!m_wirePipeline->create()) {
            qWarning("Failed to create wireframe pipeline");
            m_wirePipeline.reset();
        }
    }

    if (!m_bboxPipeline) {
        m_bboxPipeline.reset(m_rhi->newGraphicsPipeline());

        QShader vs = loadShader(QStringLiteral(":/shaders/overlay_bbox.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/overlay_bbox.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load bbox shaders");
            m_bboxPipeline.reset();
            return;
        }

        m_bboxPipeline->setShaderStages({
            { QRhiShaderStage::Vertex, vs },
            { QRhiShaderStage::Fragment, fs }
        });
        m_bboxPipeline->setTopology(QRhiGraphicsPipeline::Lines);
        m_bboxPipeline->setDepthTest(true);
        m_bboxPipeline->setDepthWrite(false);
        m_bboxPipeline->setCullMode(QRhiGraphicsPipeline::None);

        QRhiVertexInputLayout bboxLayout;
        bboxLayout.setBindings({ { 3 * sizeof(float) } });
        bboxLayout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
        m_bboxPipeline->setVertexInputLayout(bboxLayout);
        m_bboxPipeline->setShaderResourceBindings(m_srb.get());
        m_bboxPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

        if (!m_bboxPipeline->create()) {
            qWarning("Failed to create bbox pipeline");
            m_bboxPipeline.reset();
        }
    }

    if (!m_pointsPipeline) {
        m_pointsPipeline.reset(m_rhi->newGraphicsPipeline());

        QShader vs = loadShader(QStringLiteral(":/shaders/overlay_points.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/overlay_points.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load points shaders");
            m_pointsPipeline.reset();
            return;
        }

        m_pointsPipeline->setShaderStages({
            { QRhiShaderStage::Vertex, vs },
            { QRhiShaderStage::Fragment, fs }
        });
        m_pointsPipeline->setTopology(QRhiGraphicsPipeline::Points);
        m_pointsPipeline->setDepthTest(true);
        m_pointsPipeline->setDepthWrite(true);
        m_pointsPipeline->setCullMode(QRhiGraphicsPipeline::None);

        QRhiVertexInputLayout ptsLayout;
        // Points vertex layout: position(3f) + meshColor(3f) + useMeshColorFlag(1f)
        // + normal(3f) + useNormalFlag(1f)
        ptsLayout.setBindings({ { kPointsVertexStrideFloats * sizeof(float) } });
        ptsLayout.setAttributes({
            { 0, 0, QRhiVertexInputAttribute::Float3, 0 },                 // position
            { 0, 1, QRhiVertexInputAttribute::Float4, 3 * sizeof(float) }, // mesh color + use flag
            { 0, 2, QRhiVertexInputAttribute::Float4, 7 * sizeof(float) }  // normal + use flag
        });
        m_pointsPipeline->setVertexInputLayout(ptsLayout);
        m_pointsPipeline->setShaderResourceBindings(m_srb.get());
        m_pointsPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

        if (!m_pointsPipeline->create()) {
            qWarning("Failed to create points pipeline");
            m_pointsPipeline.reset();
        }
    }

    if (!m_decoratorPipeline && m_decoratorSrbs[kDecoratorSlotVertexNormals]) {
        m_decoratorPipeline.reset(m_rhi->newGraphicsPipeline());

        QShader vs = loadShader(QStringLiteral(":/shaders/overlay_decorator.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/overlay_decorator.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load decorator shaders");
            m_decoratorPipeline.reset();
        } else {
            m_decoratorPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_decoratorPipeline->setTopology(QRhiGraphicsPipeline::Lines);
            m_decoratorPipeline->setDepthTest(true);
            m_decoratorPipeline->setDepthWrite(false);
            m_decoratorPipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
            m_decoratorPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            layout.setBindings({ { 3 * sizeof(float) } });
            layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
            m_decoratorPipeline->setVertexInputLayout(layout);
            m_decoratorPipeline->setShaderResourceBindings(
                m_decoratorSrbs[kDecoratorSlotVertexNormals].get());
            m_decoratorPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            if (!m_decoratorPipeline->create()) {
                qWarning("Failed to create decorator pipeline");
                m_decoratorPipeline.reset();
            }
        }
    }

    if (!m_currentMaskFillPipeline && m_currentMaskRp) {
        m_currentMaskFillPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/selection_mask.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/selection_mask.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load current-mask shaders");
            m_currentMaskFillPipeline.reset();
        } else {
            m_currentMaskFillPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_currentMaskFillPipeline->setDepthTest(true);
            m_currentMaskFillPipeline->setDepthWrite(true);
            // Outline mask generation should include both front and back faces.
            m_currentMaskFillPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            layout.setBindings({ { kFillVertexStrideFloats * sizeof(float) } });
            layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
            m_currentMaskFillPipeline->setVertexInputLayout(layout);
            m_currentMaskFillPipeline->setShaderResourceBindings(m_srb.get());
            m_currentMaskFillPipeline->setRenderPassDescriptor(m_currentMaskRp.get());
            if (!m_currentMaskFillPipeline->create()) {
                qWarning("Failed to create current-mask fill pipeline");
                m_currentMaskFillPipeline.reset();
            }
        }
    }

    if (!m_currentMaskPointsPipeline && m_currentMaskRp) {
        m_currentMaskPointsPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/selection_mask.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/selection_mask.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load current-mask point shaders");
            m_currentMaskPointsPipeline.reset();
        } else {
            m_currentMaskPointsPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_currentMaskPointsPipeline->setTopology(QRhiGraphicsPipeline::Points);
            // Point-cloud outline mask: draw all vertices as pure occupancy (no depth/shading).
            m_currentMaskPointsPipeline->setDepthTest(false);
            m_currentMaskPointsPipeline->setDepthWrite(false);
            m_currentMaskPointsPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            layout.setBindings({ { kPointsVertexStrideFloats * sizeof(float) } });
            layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
            m_currentMaskPointsPipeline->setVertexInputLayout(layout);
            m_currentMaskPointsPipeline->setShaderResourceBindings(m_srb.get());
            m_currentMaskPointsPipeline->setRenderPassDescriptor(m_currentMaskRp.get());
            if (!m_currentMaskPointsPipeline->create()) {
                qWarning("Failed to create current-mask points pipeline");
                m_currentMaskPointsPipeline.reset();
            }
        }
    }

    if (!m_maskMorphToBasePipeline && m_maskMorphMaskToBaseSrb && m_currentMaskBaseRp) {
        m_maskMorphToBasePipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/selection_outline.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/selection_mask_morph.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load mask-morph shaders (to base)");
            m_maskMorphToBasePipeline.reset();
        } else {
            m_maskMorphToBasePipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_maskMorphToBasePipeline->setDepthTest(false);
            m_maskMorphToBasePipeline->setDepthWrite(false);
            m_maskMorphToBasePipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            m_maskMorphToBasePipeline->setVertexInputLayout(layout);
            m_maskMorphToBasePipeline->setShaderResourceBindings(m_maskMorphMaskToBaseSrb.get());
            m_maskMorphToBasePipeline->setRenderPassDescriptor(m_currentMaskBaseRp.get());
            if (!m_maskMorphToBasePipeline->create()) {
                qWarning("Failed to create mask-morph pipeline (to base)");
                m_maskMorphToBasePipeline.reset();
            }
        }
    }

    if (!m_maskMorphToWorkPipeline && m_maskMorphMaskToWorkSrb && m_currentMaskWorkRp) {
        m_maskMorphToWorkPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/selection_outline.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/selection_mask_morph.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load mask-morph shaders (to work)");
            m_maskMorphToWorkPipeline.reset();
        } else {
            m_maskMorphToWorkPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_maskMorphToWorkPipeline->setDepthTest(false);
            m_maskMorphToWorkPipeline->setDepthWrite(false);
            m_maskMorphToWorkPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            m_maskMorphToWorkPipeline->setVertexInputLayout(layout);
            m_maskMorphToWorkPipeline->setShaderResourceBindings(m_maskMorphMaskToWorkSrb.get());
            m_maskMorphToWorkPipeline->setRenderPassDescriptor(m_currentMaskWorkRp.get());
            if (!m_maskMorphToWorkPipeline->create()) {
                qWarning("Failed to create mask-morph pipeline (to work)");
                m_maskMorphToWorkPipeline.reset();
            }
        }
    }

    if (!m_maskMorphWorkToMaskPipeline && m_maskMorphWorkToMaskSrb && m_currentMaskRp) {
        m_maskMorphWorkToMaskPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/selection_outline.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/selection_mask_morph.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load mask-morph shaders (to mask)");
            m_maskMorphWorkToMaskPipeline.reset();
        } else {
            m_maskMorphWorkToMaskPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_maskMorphWorkToMaskPipeline->setDepthTest(false);
            m_maskMorphWorkToMaskPipeline->setDepthWrite(false);
            m_maskMorphWorkToMaskPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            m_maskMorphWorkToMaskPipeline->setVertexInputLayout(layout);
            m_maskMorphWorkToMaskPipeline->setShaderResourceBindings(m_maskMorphWorkToMaskSrb.get());
            m_maskMorphWorkToMaskPipeline->setRenderPassDescriptor(m_currentMaskRp.get());
            if (!m_maskMorphWorkToMaskPipeline->create()) {
                qWarning("Failed to create mask-morph pipeline (to mask)");
                m_maskMorphWorkToMaskPipeline.reset();
            }
        }
    }

    if (!m_maskDebugPipeline && m_maskDebugBaseSrb) {
        m_maskDebugPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/selection_outline.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/selection_mask_debug.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load mask-debug shaders");
            m_maskDebugPipeline.reset();
        } else {
            m_maskDebugPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_maskDebugPipeline->setDepthTest(false);
            m_maskDebugPipeline->setDepthWrite(false);
            m_maskDebugPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            m_maskDebugPipeline->setVertexInputLayout(layout);
            m_maskDebugPipeline->setShaderResourceBindings(m_maskDebugBaseSrb.get());
            m_maskDebugPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            if (!m_maskDebugPipeline->create()) {
                qWarning("Failed to create mask-debug pipeline");
                m_maskDebugPipeline.reset();
            }
        }
    }

    if (!m_outlinePipeline && m_outlineSrb) {
        m_outlinePipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/selection_outline.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/selection_outline.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load outline shaders");
            m_outlinePipeline.reset();
        } else {
            m_outlinePipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_outlinePipeline->setDepthTest(false);
            m_outlinePipeline->setDepthWrite(false);
            m_outlinePipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiGraphicsPipeline::TargetBlend blend;
            blend.enable = true;
            blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
            blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            blend.opColor = QRhiGraphicsPipeline::Add;
            blend.srcAlpha = QRhiGraphicsPipeline::One;
            blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            blend.opAlpha = QRhiGraphicsPipeline::Add;
            m_outlinePipeline->setTargetBlends({ blend });
            QRhiVertexInputLayout layout;
            m_outlinePipeline->setVertexInputLayout(layout);
            m_outlinePipeline->setShaderResourceBindings(m_outlineSrb.get());
            m_outlinePipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            if (!m_outlinePipeline->create()) {
                qWarning("Failed to create outline pipeline");
                m_outlinePipeline.reset();
            }
        }
    }

    if (!m_trackballGizmoUbuf) {
        m_trackballGizmoUbuf.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kTrackballGizmoUbufSize));
        m_trackballGizmoUbuf->create();
    }

    if (!m_trackballGizmoVbuf) {
        const auto &verts = trackballGizmoVertices();
        m_trackballGizmoVbuf.reset(
            m_rhi->newBuffer(
                QRhiBuffer::Dynamic,
                QRhiBuffer::VertexBuffer,
                static_cast<quint32>(verts.size() * sizeof(float))));
        m_trackballGizmoVbuf->create();
        m_trackballGizmoVertexCount = int(verts.size() / 6);
    }

    if (!m_trackballGizmoSrb && m_trackballGizmoUbuf) {
        m_trackballGizmoSrb.reset(m_rhi->newShaderResourceBindings());
        m_trackballGizmoSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::VertexStage,
                m_trackballGizmoUbuf.get())
        });
        m_trackballGizmoSrb->create();
    }

    if (!m_trackballGizmoPipeline && m_trackballGizmoSrb) {
        m_trackballGizmoPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/overlay_trackball_gizmo.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/overlay_trackball_gizmo.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load trackball gizmo shaders");
            m_trackballGizmoPipeline.reset();
        } else {
            m_trackballGizmoPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_trackballGizmoPipeline->setTopology(QRhiGraphicsPipeline::Lines);
            m_trackballGizmoPipeline->setDepthTest(true);
            m_trackballGizmoPipeline->setDepthWrite(false);
            m_trackballGizmoPipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
            m_trackballGizmoPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiGraphicsPipeline::TargetBlend blend;
            blend.enable = true;
            blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
            blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            blend.opColor = QRhiGraphicsPipeline::Add;
            blend.srcAlpha = QRhiGraphicsPipeline::One;
            blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            blend.opAlpha = QRhiGraphicsPipeline::Add;
            m_trackballGizmoPipeline->setTargetBlends({ blend });
            QRhiVertexInputLayout layout;
            layout.setBindings({ { 6 * sizeof(float) } });
            layout.setAttributes({
                { 0, 0, QRhiVertexInputAttribute::Float3, 0 },
                { 0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float) }
            });
            m_trackballGizmoPipeline->setVertexInputLayout(layout);
            m_trackballGizmoPipeline->setShaderResourceBindings(m_trackballGizmoSrb.get());
            m_trackballGizmoPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            if (!m_trackballGizmoPipeline->create()) {
                qWarning("Failed to create trackball gizmo pipeline");
                m_trackballGizmoPipeline.reset();
            }
        }
    }
}

void RenderWidget::prepareDirtyBuffers(QRhiCommandBuffer *cb)
{
    if (!m_rhi)
        return;

    updateCameraFrameIfNeeded();

    if (m_fallbackTextureUploadPending && m_fallbackTexture) {
        QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
        QImage white(1, 1, QImage::Format_RGBA8888);
        white.fill(Qt::white);
        QRhiTextureUploadEntry entry(0, 0, QRhiTextureSubresourceUploadDescription(white));
        u->uploadTexture(m_fallbackTexture.get(), QRhiTextureUploadDescription({ entry }));
        cb->resourceUpdate(u);
        m_fallbackTextureUploadPending = false;
    }

    const bool needFill =
        m_renderSettings.showFill || m_renderSettings.highlightCurrentMesh;
    const bool needWire = m_renderSettings.showWire;
    const bool needPoints =
        m_renderSettings.showPoints || m_renderSettings.highlightCurrentMesh;
    const bool needBBox = m_renderSettings.showBoundingBox;
    const bool needDecoratorNormals =
        m_renderSettings.decoratorVertexNormals
        || m_renderSettings.decoratorFaceNormals;
    const bool needDecoratorBoundaries =
        m_renderSettings.decoratorBoundaryEdges
        || m_renderSettings.decoratorTextureSeams;
    if (!needFill && !needWire && !needPoints && !needBBox
        && !needDecoratorNormals && !needDecoratorBoundaries)
        return;

    const auto pointVariant = static_cast<Document::PointGpuVariant>(
        pointGpuVariantIndexForCurrentSettings());
    const auto fillVariant = static_cast<Document::FillGpuVariant>(
        m_renderSettings.showFill
            ? fillGpuVariantIndexForCurrentSettings()
            : static_cast<int>(Document::FillGpuVariant::Constant));

    for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
        if (!meshVisible(mi))
            continue;
        m_doc->ensureMeshGpuResources(
            m_rhi,
            cb,
            mi,
            fillVariant,
            pointVariant,
            needFill,
            needWire,
            needPoints,
            needBBox,
            needDecoratorNormals,
            needDecoratorBoundaries);
    }
}

void RenderWidget::executePendingDepthPick(
    QRhiCommandBuffer *cb,
    const QMatrix4x4 &mvp,
    const QSize &pixelSize,
    int pointVariantIndex)
{
    if (!m_depthPickPending || m_depthPickInFlight || !m_rhi || !cb || pixelSize.isEmpty())
        return;

    ensureDepthPickResources(pixelSize);
    if (!m_depthPickRt || !m_depthPickTexture || !m_depthPickSrb)
        return;
    if (!m_depthPickFillPipeline && !m_depthPickPointsPipeline)
        return;

    const float sx = float(pixelSize.width()) / float(qMax(1, width()));
    const float sy = float(pixelSize.height()) / float(qMax(1, height()));
    const int px = std::clamp(
        int(std::floor((float(m_depthPickPos.x()) + 0.5f) * sx)),
        0,
        pixelSize.width() - 1);
    const int pyScreen = std::clamp(
        int(std::floor((float(m_depthPickPos.y()) + 0.5f) * sy)),
        0,
        pixelSize.height() - 1);
    const int pyRead = m_rhi->isYUpInFramebuffer()
        ? (pixelSize.height() - 1 - pyScreen)
        : pyScreen;
    const bool yUpInNdc = m_rhi->isYUpInNDC();
    const bool clipDepthZeroToOne = m_rhi->isClipDepthZeroToOne();

    const auto fillVariant = Document::FillGpuVariant::Constant;
    const auto pointVariant = static_cast<Document::PointGpuVariant>(pointVariantIndex);

    for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
        if (!meshVisible(mi))
            continue;
        m_doc->ensureMeshGpuResources(
            m_rhi,
            cb,
            mi,
            fillVariant,
            pointVariant,
            true,   // fill
            false,  // wire
            true,   // points
            false,  // bbox
            false,  // decorator normals
            false); // decorator boundaries
    }

    cb->beginPass(m_depthPickRt.get(), Qt::transparent, { 1.0f, 0 }, nullptr);
    cb->setViewport({ 0, 0, float(pixelSize.width()), float(pixelSize.height()) });

    for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
        if (!meshVisible(mi))
            continue;

        if (m_depthPickFillPipeline) {
            const Document::FillPassGpuView fillView =
                m_doc->fillPassGpuView(m_rhi, mi, fillVariant);
            cb->setGraphicsPipeline(m_depthPickFillPipeline.get());
            cb->setShaderResources(m_depthPickSrb.get());
            for (int bi = 0; bi < fillView.batchCount; ++bi) {
                const auto &batch = fillView.batches[bi];
                if (!batch.vertexBuffer || (batch.indexCount == 0 && batch.vertexCount == 0))
                    continue;
                const QRhiCommandBuffer::VertexInput vbufBinding(batch.vertexBuffer, 0);
                if (batch.indexCount > 0 && batch.indexBuffer) {
                    cb->setVertexInput(
                        0, 1, &vbufBinding, batch.indexBuffer, 0, QRhiCommandBuffer::IndexUInt32);
                    cb->drawIndexed(batch.indexCount);
                } else {
                    cb->setVertexInput(0, 1, &vbufBinding);
                    cb->draw(batch.vertexCount);
                }
            }
        }

        if (m_depthPickPointsPipeline) {
            const Document::PointsPassGpuView pointsView =
                m_doc->pointsPassGpuView(m_rhi, mi, pointVariant);
            if (pointsView.valid && pointsView.vertexBuffer && pointsView.vertexCount > 0) {
                cb->setGraphicsPipeline(m_depthPickPointsPipeline.get());
                cb->setShaderResources(m_depthPickSrb.get());
                const QRhiCommandBuffer::VertexInput pv(pointsView.vertexBuffer, 0);
                cb->setVertexInput(0, 1, &pv);
                cb->draw(pointsView.vertexCount);
            }
        }
    }

    cb->endPass();

    QMatrix4x4 invMvp;
    bool invOk = false;
    invMvp = mvp.inverted(&invOk);
    if (!invOk)
        return;

    QPointer<RenderWidget> self(this);
    m_depthPickReadbackResult = std::make_unique<QRhiReadbackResult>();
    m_depthPickReadbackResult->completed =
        [self, invMvp, px, pyScreen, pixelSize, yUpInNdc, clipDepthZeroToOne]() {
        if (!self)
            return;
        const QByteArray data =
            self->m_depthPickReadbackResult ? self->m_depthPickReadbackResult->data : QByteArray();
        const QRhiTexture::Format format = self->m_depthPickReadbackResult
            ? self->m_depthPickReadbackResult->format
            : QRhiTexture::UnknownFormat;
        QMetaObject::invokeMethod(
            self,
            [self, data, format, invMvp, px, pyScreen, pixelSize, yUpInNdc, clipDepthZeroToOne]() {
            if (!self)
                return;
            self->m_depthPickInFlight = false;
            self->m_depthPickReadbackResult.reset();

            if (self->m_depthPickPending)
                self->update();

            if (data.size() < 4)
                return;

            const uchar *pxData = reinterpret_cast<const uchar *>(data.constData());
            const float alpha = pxData[3] / 255.0f;
            if (alpha < 0.5f)
                return;

            const bool bgraOrder = (format == QRhiTexture::BGRA8);
            const float depth01 = decodePackedDepthRgb8(pxData, bgraOrder);
            if (depth01 <= 0.0f || depth01 >= 1.0f)
                return;

            const float ndcX =
                (2.0f * (float(px) + 0.5f) / float(qMax(1, pixelSize.width()))) - 1.0f;
            const float y01 = (float(pyScreen) + 0.5f) / float(qMax(1, pixelSize.height()));
            const float ndcY = yUpInNdc ? (1.0f - 2.0f * y01) : (2.0f * y01 - 1.0f);
            const float ndcZ = clipDepthZeroToOne ? depth01 : (depth01 * 2.0f - 1.0f);

            QVector4D world = invMvp * QVector4D(ndcX, ndcY, ndcZ, 1.0f);
            if (std::abs(world.w()) < 1e-8f)
                return;
            world /= world.w();
            const QVector3D worldPos = world.toVector3D();
            self->startCenterAnimation(worldPos);
            emit self->trackballCenterPicked(worldPos);
        },
            Qt::QueuedConnection);
    };

    QRhiReadbackDescription rb(m_depthPickTexture.get());
    rb.setRect(QRect(px, pyRead, 1, 1));

    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    u->readBackTexture(rb, m_depthPickReadbackResult.get());
    cb->resourceUpdate(u);

    m_depthPickPending = false;
    m_depthPickInFlight = true;
}

void RenderWidget::renderCurrentMeshMask(QRhiCommandBuffer *cb, const QSize &pixelSize)
{
    if (!m_renderSettings.highlightCurrentMesh)
        return;

    m_currentMaskFromPoints = false;

    const int currentMeshIndex = m_doc->currentMeshIndex();
    if (currentMeshIndex < 0)
        return;
    if (!meshVisible(currentMeshIndex))
        return;

    ensureCurrentMeshMaskResources(pixelSize);
    if (!m_currentMaskRt)
        return;

    cb->beginPass(m_currentMaskRt.get(), Qt::transparent, { 1.0f, 0 }, nullptr);
    cb->setViewport({ 0, 0, float(pixelSize.width()), float(pixelSize.height()) });

    const auto pointVariant = static_cast<Document::PointGpuVariant>(
        pointGpuVariantIndexForCurrentSettings());
    const auto fillVariant = static_cast<Document::FillGpuVariant>(
        m_renderSettings.showFill
            ? fillGpuVariantIndexForCurrentSettings()
            : static_cast<int>(Document::FillGpuVariant::Constant));

    bool drewSurface = false;
    if (m_currentMaskFillPipeline) {
        const Document::FillPassGpuView fillView =
            m_doc->fillPassGpuView(m_rhi, currentMeshIndex, fillVariant);
        cb->setGraphicsPipeline(m_currentMaskFillPipeline.get());
        cb->setShaderResources();
        for (int bi = 0; bi < fillView.batchCount; ++bi) {
            const auto &batch = fillView.batches[bi];
            if (!batch.vertexBuffer)
                continue;
            if (batch.indexCount == 0 && batch.vertexCount == 0)
                continue;
            drewSurface = true;
            const QRhiCommandBuffer::VertexInput vbufBinding(batch.vertexBuffer, 0);
            if (batch.indexCount > 0 && batch.indexBuffer) {
                cb->setVertexInput(
                    0, 1, &vbufBinding, batch.indexBuffer, 0, QRhiCommandBuffer::IndexUInt32);
                cb->drawIndexed(batch.indexCount);
            } else {
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(batch.vertexCount);
            }
        }
    }

    if (!drewSurface && m_currentMaskPointsPipeline) {
        const Document::PointsPassGpuView pointsView =
            m_doc->pointsPassGpuView(m_rhi, currentMeshIndex, pointVariant);
        cb->setGraphicsPipeline(m_currentMaskPointsPipeline.get());
        cb->setShaderResources();
        if (pointsView.valid && pointsView.vertexBuffer && pointsView.vertexCount > 0) {
            m_currentMaskFromPoints = true;
            const QRhiCommandBuffer::VertexInput pv(pointsView.vertexBuffer, 0);
            cb->setVertexInput(0, 1, &pv);
            cb->draw(pointsView.vertexCount);
        }
    }

    cb->endPass();
}

void RenderWidget::processCurrentMeshMask(QRhiCommandBuffer *cb, const QSize &pixelSize)
{
    if (!m_renderSettings.highlightCurrentMesh)
        return;
    if (!m_currentMaskRt || !m_currentMaskBaseRt || !m_currentMaskWorkRt)
        return;
    if (!m_maskMorphCopyUbuf
        || !m_maskMorphDilateUbuf
        || !m_maskMorphErodeUbuf
        || !m_maskMorphMaskToBaseSrb
        || !m_maskMorphMaskToWorkSrb
        || !m_maskMorphWorkToMaskSrb)
        return;
    if (!m_maskMorphToBasePipeline
        || !m_maskMorphToWorkPipeline
        || !m_maskMorphWorkToMaskPipeline)
        return;

    const float invW = 1.0f / float(qMax(1, pixelSize.width()));
    const float invH = 1.0f / float(qMax(1, pixelSize.height()));

    auto updateMorphParams = [&](QRhiBuffer *ubuf, float mode, float radiusPx) {
        // mode: 0 = dilate, 1 = erode
        const float params[4] = { invW, invH, radiusPx, mode };
        QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
        u->updateDynamicBuffer(ubuf, 0, kMaskMorphUbufSize, params);
        cb->resourceUpdate(u);
    };

    // Snapshot base point mask for debugging/inspection before any morphology.
    updateMorphParams(m_maskMorphCopyUbuf.get(), 0.0f, 0.0f);
    cb->beginPass(m_currentMaskBaseRt.get(), Qt::transparent, { 1.0f, 0 }, nullptr);
    cb->setGraphicsPipeline(m_maskMorphToBasePipeline.get());
    cb->setViewport({ 0, 0, float(pixelSize.width()), float(pixelSize.height()) });
    cb->setShaderResources(m_maskMorphMaskToBaseSrb.get());
    cb->draw(3);
    cb->endPass();

    if (!m_currentMaskFromPoints)
        return;

    // Point-cloud outline pipeline:
    // 1) dilate(base, dilateRadius) -> work
    // 2) erode(work, erodeRadius) -> mask
    // 3) edge filter on final mask texture
    // This keeps mask rasterization cheap (1px points) and pushes expansion to screen space.
    const float dilateRadiusPx = qMax(0.0f, m_renderSettings.currentMeshDilateRadius);
    const float erosionRadiusPx = qBound(
        0.0f,
        m_renderSettings.currentMeshErodeRadius,
        dilateRadiusPx);
    if (dilateRadiusPx <= 0.0f && erosionRadiusPx <= 0.0f)
        return;

    updateMorphParams(m_maskMorphDilateUbuf.get(), 0.0f, dilateRadiusPx);
    cb->beginPass(m_currentMaskWorkRt.get(), Qt::transparent, { 1.0f, 0 }, nullptr);
    cb->setGraphicsPipeline(m_maskMorphToWorkPipeline.get());
    cb->setViewport({ 0, 0, float(pixelSize.width()), float(pixelSize.height()) });
    cb->setShaderResources(m_maskMorphMaskToWorkSrb.get());
    cb->draw(3);
    cb->endPass();

    updateMorphParams(m_maskMorphErodeUbuf.get(), 1.0f, erosionRadiusPx);
    cb->beginPass(m_currentMaskRt.get(), Qt::transparent, { 1.0f, 0 }, nullptr);
    cb->setGraphicsPipeline(m_maskMorphWorkToMaskPipeline.get());
    cb->setViewport({ 0, 0, float(pixelSize.width()), float(pixelSize.height()) });
    cb->setShaderResources(m_maskMorphWorkToMaskSrb.get());
    cb->draw(3);
    cb->endPass();
}

void RenderWidget::drawCurrentMeshDebugView(QRhiCommandBuffer *cb, const QSize &pixelSize)
{
    if (!m_maskDebugPipeline || !m_maskDebugUbuf)
        return;

    QRhiShaderResourceBindings *srb = nullptr;
    switch (m_renderSettings.currentMeshDebugView) {
    case CurrentMeshDebugView::Outline:
        return;
    case CurrentMeshDebugView::BaseMask:
        srb = m_maskDebugBaseSrb.get();
        break;
    case CurrentMeshDebugView::DilatedMask:
        srb = m_currentMaskFromPoints ? m_maskDebugWorkSrb.get() : m_maskDebugBaseSrb.get();
        break;
    case CurrentMeshDebugView::ErodedMask:
        srb = m_maskDebugMaskSrb.get();
        break;
    }
    if (!srb)
        return;

    const float debugData[4] = {
        1.0f / float(qMax(1, pixelSize.width())),
        1.0f / float(qMax(1, pixelSize.height())),
        m_rhi->isYUpInFramebuffer() ? 1.0f : 0.0f,
        0.0f
    };
    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    u->updateDynamicBuffer(m_maskDebugUbuf.get(), 0, kMaskDebugUbufSize, debugData);
    cb->resourceUpdate(u);

    cb->setGraphicsPipeline(m_maskDebugPipeline.get());
    cb->setViewport({ 0, 0, float(pixelSize.width()), float(pixelSize.height()) });
    cb->setShaderResources(srb);
    cb->draw(3);
}

void RenderWidget::drawCurrentMeshOutline(QRhiCommandBuffer *cb, const QSize &pixelSize)
{
    if (!m_renderSettings.highlightCurrentMesh)
        return;

    if (m_renderSettings.currentMeshDebugView != CurrentMeshDebugView::Outline) {
        drawCurrentMeshDebugView(cb, pixelSize);
        return;
    }

    if (!m_outlinePipeline || !m_outlineSrb || !m_outlineUbuf)
        return;
    if (!m_currentMaskTexture)
        return;

    // For point-cloud highlighting, outline width is controlled by dilate/erode radius difference.
    // Keep edge extraction local and stable.
    const float widthPx = m_currentMaskFromPoints
        ? 1.0f
        : qMax(1.0f, m_renderSettings.currentMeshOutlineWidth);
    float outlineData[8] = {};
    outlineData[0] = m_renderSettings.currentMeshOutlineColor.redF();
    outlineData[1] = m_renderSettings.currentMeshOutlineColor.greenF();
    outlineData[2] = m_renderSettings.currentMeshOutlineColor.blueF();
    outlineData[3] = m_renderSettings.currentMeshOutlineColor.alphaF();
    outlineData[4] = widthPx;
    outlineData[5] = 1.0f / float(qMax(1, pixelSize.width()));
    outlineData[6] = 1.0f / float(qMax(1, pixelSize.height()));
    // Offscreen texture sampling needs a vertical flip on Y-up framebuffers.
    outlineData[7] = m_rhi->isYUpInFramebuffer() ? 1.0f : 0.0f;

    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    u->updateDynamicBuffer(m_outlineUbuf.get(), 0, kOutlineUbufSize, outlineData);
    cb->resourceUpdate(u);

    cb->setGraphicsPipeline(m_outlinePipeline.get());
    cb->setViewport({ 0, 0, float(pixelSize.width()), float(pixelSize.height()) });
    cb->setShaderResources(m_outlineSrb.get());
    cb->draw(3);
}

void RenderWidget::initialize(QRhiCommandBuffer *cb)
{
    ensureRenderResources();
    if (!m_rhi) {
        qWarning("QRhi not available");
        return;
    }

    prepareDirtyBuffers(cb);
}

void RenderWidget::render(QRhiCommandBuffer *cb)
{
    ensureRenderResources();
    if (!m_rhi || !m_ubuf)
        return;

    advanceCenterAnimation();

    const bool drawFillPass = m_renderSettings.showFill;
    const bool drawWirePass = m_renderSettings.showWire;
    const bool drawBBoxPass = m_renderSettings.showBoundingBox;
    const bool drawPointsPass = m_renderSettings.showPoints;
    const bool drawDecoratorPass =
        m_renderSettings.decoratorVertexNormals
        || m_renderSettings.decoratorFaceNormals
        || m_renderSettings.decoratorBoundaryEdges
        || m_renderSettings.decoratorTextureSeams;
    const bool drawTrackballGizmo = (m_doc->meshCount() > 0);
    const int currentMeshIndex = m_doc->currentMeshIndex();
    const bool drawCurrentMeshHighlight =
        m_renderSettings.highlightCurrentMesh
        && (currentMeshIndex >= 0)
        && meshVisible(currentMeshIndex);
    const bool anyDrawPass =
        drawFillPass || drawWirePass || drawBBoxPass || drawPointsPass || drawDecoratorPass
        || drawCurrentMeshHighlight || drawTrackballGizmo;
    const bool needMvpForFrame = anyDrawPass || m_depthPickPending;

    if (anyDrawPass)
        prepareDirtyBuffers(cb);

    m_frameTimer.start();

    const QSize sz = renderTarget()->pixelSize();

    QRhiResourceUpdateBatch *u = nullptr;
    QMatrix4x4 mvp;
    if (needMvpForFrame) {
        const float aspect = sz.width() / float(sz.height());
        const float sceneRadius = m_trackball.radius();

        QMatrix4x4 proj;
        proj.perspective(
            m_trackball.fovYDegrees(),
            aspect,
            0.01f * sceneRadius,
            100.0f * sceneRadius);

        const QMatrix4x4 view = m_trackball.viewMatrix();

        QMatrix4x4 modelView = view;

        mvp = proj * view;
        QMatrix3x3 normalMat = modelView.normalMatrix();

        // Pack uniform: mat4 mvp + mat4 modelView + mat3 as 3 vec4 (std140) + render colors/params.
        float ubufData[kUbufFloatCount] = {};
        memcpy(ubufData, mvp.constData(), 64);
        memcpy(ubufData + 16, modelView.constData(), 64);
        // std140: mat3 is stored as 3 columns of vec4
        const float *n = normalMat.constData();
        ubufData[32] = n[0]; ubufData[33] = n[1]; ubufData[34] = n[2]; ubufData[35] = 0;
        ubufData[36] = n[3]; ubufData[37] = n[4]; ubufData[38] = n[5]; ubufData[39] = 0;
        ubufData[40] = n[6]; ubufData[41] = n[7]; ubufData[42] = n[8]; ubufData[43] = 0;
        ubufData[kUbufBBoxColorOffset + 0] = m_renderSettings.bboxWireColor.redF();
        ubufData[kUbufBBoxColorOffset + 1] = m_renderSettings.bboxWireColor.greenF();
        ubufData[kUbufBBoxColorOffset + 2] = m_renderSettings.bboxWireColor.blueF();
        ubufData[kUbufBBoxColorOffset + 3] = m_renderSettings.bboxWireColor.alphaF();
        ubufData[kUbufPointColorOffset + 0] = m_renderSettings.pointColor.redF();
        ubufData[kUbufPointColorOffset + 1] = m_renderSettings.pointColor.greenF();
        ubufData[kUbufPointColorOffset + 2] = m_renderSettings.pointColor.blueF();
        ubufData[kUbufPointColorOffset + 3] = m_renderSettings.pointColor.alphaF();
        ubufData[kUbufPointParamsOffset + 0] = m_renderSettings.pointSize;
        ubufData[kUbufWireColorOffset + 0] = m_renderSettings.wireColor.redF();
        ubufData[kUbufWireColorOffset + 1] = m_renderSettings.wireColor.greenF();
        ubufData[kUbufWireColorOffset + 2] = m_renderSettings.wireColor.blueF();
        // Wire pass is intentionally translucent to compose independently over fill/effects.
        ubufData[kUbufWireColorOffset + 3] = m_renderSettings.wireColor.alphaF() * 0.7f;
        ubufData[kUbufWireParamsOffset + 0] = m_renderSettings.wireSize;
        ubufData[kUbufFillColorOffset + 0] = m_renderSettings.fillColor.redF();
        ubufData[kUbufFillColorOffset + 1] = m_renderSettings.fillColor.greenF();
        ubufData[kUbufFillColorOffset + 2] = m_renderSettings.fillColor.blueF();
        ubufData[kUbufFillColorOffset + 3] = m_renderSettings.fillColor.alphaF();
        // bbox lighting removed: slot 0 intentionally unused/reserved.
        ubufData[kUbufLightingParamsOffset + 0] = 0.0f;
        ubufData[kUbufLightingParamsOffset + 1] = m_renderSettings.pointLighting ? 1.0f : 0.0f;
        ubufData[kUbufLightingParamsOffset + 2] = m_renderSettings.wireLighting ? 1.0f : 0.0f;
        ubufData[kUbufLightingParamsOffset + 3] = m_renderSettings.fillLighting ? 1.0f : 0.0f;

        u = m_rhi->nextResourceUpdateBatch();
        u->updateDynamicBuffer(m_ubuf.get(), 0, kUbufSize, ubufData);

        if (drawTrackballGizmo && m_trackballGizmoUbuf && m_trackballGizmoVbuf) {
            const auto &verts = trackballGizmoVertices();
            u->updateDynamicBuffer(
                m_trackballGizmoVbuf.get(),
                0,
                int(verts.size() * sizeof(float)),
                verts.data());

            float gizmoData[kTrackballGizmoUbufSize / sizeof(float)] = {};
            memcpy(gizmoData, mvp.constData(), 64);
            const QVector3D center = m_trackball.center();
            gizmoData[16] = center.x();
            gizmoData[17] = center.y();
            gizmoData[18] = center.z();
            gizmoData[19] = m_trackball.gizmoWorldRadius();
            u->updateDynamicBuffer(
                m_trackballGizmoUbuf.get(),
                0,
                kTrackballGizmoUbufSize,
                gizmoData);
        }
    }

    const int pointVariantIndex = pointGpuVariantIndexForCurrentSettings();
    if (m_depthPickPending) {
        if (u) {
            cb->resourceUpdate(u);
            u = nullptr;
        }
        executePendingDepthPick(cb, mvp, sz, pointVariantIndex);
    }

    if (drawCurrentMeshHighlight) {
        if (u) {
            cb->resourceUpdate(u);
            u = nullptr;
        }
        renderCurrentMeshMask(cb, sz);
        processCurrentMeshMask(cb, sz);
    }

    const auto fillVariant = static_cast<Document::FillGpuVariant>(
        fillGpuVariantIndexForCurrentSettings());
    const auto pointVariant = static_cast<Document::PointGpuVariant>(
        pointVariantIndex);

    cb->beginPass(renderTarget(), QColor(40, 40, 40), { 1.0f, 0 }, u);

    if (drawFillPass && m_fillPipeline) {
        cb->setGraphicsPipeline(m_fillPipeline.get());
        cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });

        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;
            const Document::FillPassGpuView fillView =
                m_doc->fillPassGpuView(m_rhi, mi, fillVariant);
            if (!fillView.valid)
                continue;

            for (int bi = 0; bi < fillView.batchCount; ++bi) {
                const auto &batch = fillView.batches[bi];
                if (!batch.vertexBuffer || (batch.indexCount == 0 && batch.vertexCount == 0))
                    continue;

                cb->setShaderResources(shaderResourcesForTexture(batch.texture));
                const QRhiCommandBuffer::VertexInput vbufBinding(batch.vertexBuffer, 0);
                if (batch.indexCount > 0 && batch.indexBuffer) {
                    cb->setVertexInput(
                        0, 1, &vbufBinding, batch.indexBuffer, 0, QRhiCommandBuffer::IndexUInt32);
                    cb->drawIndexed(batch.indexCount);
                } else {
                    cb->setVertexInput(0, 1, &vbufBinding);
                    cb->draw(batch.vertexCount);
                }
            }
        }
    }

    if (drawWirePass && m_wirePipeline) {
        cb->setGraphicsPipeline(m_wirePipeline.get());
        cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
        cb->setShaderResources();

        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;
            const Document::WirePassGpuView wireView = m_doc->wirePassGpuView(m_rhi, mi);
            if (!wireView.valid || !wireView.vertexBuffer || wireView.vertexCount <= 0)
                continue;
            const QRhiCommandBuffer::VertexInput vbufBinding(wireView.vertexBuffer, 0);
            cb->setVertexInput(0, 1, &vbufBinding);
            cb->draw(wireView.vertexCount);
        }
    }

    if (drawBBoxPass && m_bboxPipeline) {
        cb->setGraphicsPipeline(m_bboxPipeline.get());
        cb->setShaderResources();
        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;
            const Document::BBoxPassGpuView bboxView = m_doc->bboxPassGpuView(m_rhi, mi);
            if (!bboxView.valid || !bboxView.vertexBuffer || bboxView.vertexCount <= 0)
                continue;
            const QRhiCommandBuffer::VertexInput bv(bboxView.vertexBuffer, 0);
            cb->setVertexInput(0, 1, &bv);
            cb->draw(bboxView.vertexCount);
        }
    }

    if (drawPointsPass && m_pointsPipeline) {
        cb->setGraphicsPipeline(m_pointsPipeline.get());
        cb->setShaderResources();
        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;
            const Document::PointsPassGpuView pointsView =
                m_doc->pointsPassGpuView(m_rhi, mi, pointVariant);
            if (!pointsView.valid || !pointsView.vertexBuffer || pointsView.vertexCount <= 0)
                continue;
            const QRhiCommandBuffer::VertexInput pv(pointsView.vertexBuffer, 0);
            cb->setVertexInput(0, 1, &pv);
            cb->draw(pointsView.vertexCount);
        }
    }

    if (drawDecoratorPass && m_decoratorPipeline) {
        auto setDecoratorColor = [&](int slot, const QColor &color) -> bool {
            if (slot < 0 || slot >= kDecoratorSlotCount)
                return false;
            QRhiBuffer *decoratorUbuf = m_decoratorUbufs[slot].get();
            QRhiShaderResourceBindings *decoratorSrb = m_decoratorSrbs[slot].get();
            if (!decoratorUbuf || !decoratorSrb)
                return false;
            float decoratorData[kDecoratorUbufSize / sizeof(float)] = {};
            memcpy(decoratorData, mvp.constData(), 64);
            decoratorData[16] = color.redF();
            decoratorData[17] = color.greenF();
            decoratorData[18] = color.blueF();
            decoratorData[19] = color.alphaF();
            QRhiResourceUpdateBatch *uDecor = m_rhi->nextResourceUpdateBatch();
            uDecor->updateDynamicBuffer(
                decoratorUbuf, 0, kDecoratorUbufSize, decoratorData);
            cb->resourceUpdate(uDecor);
            cb->setGraphicsPipeline(m_decoratorPipeline.get());
            cb->setShaderResources(decoratorSrb);
            cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
            return true;
        };

        auto drawDecoratorKind = [&](auto bufferGetter, auto countGetter) {
            for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
                if (!meshVisible(mi))
                    continue;
                const Document::DecoratorPassGpuView decorView =
                    m_doc->decoratorPassGpuView(m_rhi, mi);
                if (!decorView.valid)
                    continue;
                QRhiBuffer *vbuf = bufferGetter(decorView);
                const int vertexCount = countGetter(decorView);
                if (!vbuf || vertexCount <= 0)
                    continue;
                const QRhiCommandBuffer::VertexInput binding(vbuf, 0);
                cb->setVertexInput(0, 1, &binding);
                cb->draw(vertexCount);
            }
        };

        if (m_renderSettings.decoratorVertexNormals) {
            if (setDecoratorColor(
                    kDecoratorSlotVertexNormals,
                    m_renderSettings.decoratorVertexNormalColor)) {
                drawDecoratorKind(
                    [](const Document::DecoratorPassGpuView &view) {
                        return view.vertexNormalsBuffer;
                    },
                    [](const Document::DecoratorPassGpuView &view) {
                        return view.vertexNormalsVertexCount;
                    });
            }
        }
        if (m_renderSettings.decoratorFaceNormals) {
            if (setDecoratorColor(
                    kDecoratorSlotFaceNormals,
                    m_renderSettings.decoratorFaceNormalColor)) {
                drawDecoratorKind(
                    [](const Document::DecoratorPassGpuView &view) {
                        return view.faceNormalsBuffer;
                    },
                    [](const Document::DecoratorPassGpuView &view) {
                        return view.faceNormalsVertexCount;
                    });
            }
        }
        if (m_renderSettings.decoratorBoundaryEdges) {
            if (setDecoratorColor(
                    kDecoratorSlotBoundaryEdges,
                    m_renderSettings.decoratorBoundaryEdgeColor)) {
                drawDecoratorKind(
                    [](const Document::DecoratorPassGpuView &view) {
                        return view.boundaryEdgesBuffer;
                    },
                    [](const Document::DecoratorPassGpuView &view) {
                        return view.boundaryEdgesVertexCount;
                    });
            }
        }
        if (m_renderSettings.decoratorTextureSeams) {
            if (setDecoratorColor(
                    kDecoratorSlotTextureSeams,
                    m_renderSettings.decoratorTextureSeamColor)) {
                drawDecoratorKind(
                    [](const Document::DecoratorPassGpuView &view) {
                        return view.textureSeamsBuffer;
                    },
                    [](const Document::DecoratorPassGpuView &view) {
                        return view.textureSeamsVertexCount;
                    });
            }
        }
    }

    if (drawTrackballGizmo && m_trackballGizmoPipeline && m_trackballGizmoVbuf && m_trackballGizmoSrb) {
        cb->setGraphicsPipeline(m_trackballGizmoPipeline.get());
        cb->setShaderResources(m_trackballGizmoSrb.get());
        cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
        const QRhiCommandBuffer::VertexInput gv(m_trackballGizmoVbuf.get(), 0);
        cb->setVertexInput(0, 1, &gv);
        cb->draw(m_trackballGizmoVertexCount);
    }

    if (drawCurrentMeshHighlight)
        drawCurrentMeshOutline(cb, sz);

    if (needMvpForFrame) {
        const QMatrix4x4 view = m_trackball.viewMatrix();
        updateBoundingBoxCornersOverlayPlacement(mvp, view, sz);
    }

    cb->endPass();

    const float cpuMs = m_frameTimer.nsecsElapsed() / 1e6f;

    const bool gpuTimingSupported = m_rhi->isFeatureSupported(QRhi::Timestamps);
    float gpuMs = 0.0f;
    bool gpuSampleValid = false;
    if (gpuTimingSupported) {
        // API returns elapsed seconds for the last completed frame.
        const double gpuSeconds = cb->lastCompletedGpuTime();
        if (gpuSeconds > 0.0) {
            gpuMs = static_cast<float>(gpuSeconds * 1000.0);
            gpuSampleValid = true;
        }
    }

    emit frameRendered(cpuMs, gpuMs, gpuTimingSupported, gpuSampleValid);
}

void RenderWidget::mousePressEvent(QMouseEvent *e)
{
    emit viewActivated(this);
    cancelCenterAnimation();
    m_trackball.mousePress(e, size());
}

void RenderWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    emit viewActivated(this);
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
    m_trackball.mouseRelease(e);
}

void RenderWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (e && e->buttons() != Qt::NoButton)
        emit viewActivated(this);
    if (m_trackball.mouseMove(e, size()))
        update();
}

void RenderWidget::wheelEvent(QWheelEvent *e)
{
    emit viewActivated(this);
    cancelCenterAnimation();
    if (m_trackball.wheel(e))
        update();
}

void RenderWidget::resizeEvent(QResizeEvent *e)
{
    QRhiWidget::resizeEvent(e);
    layoutOverlayButtons();
}
