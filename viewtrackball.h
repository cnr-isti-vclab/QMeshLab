#pragma once

#include <QMatrix4x4>
#include <QPointF>
#include <QQuaternion>
#include <QSize>
#include <QVector3D>
#include <QVector4D>

class QMouseEvent;
class QWheelEvent;

class ViewTrackball
{
public:
    ViewTrackball();

    void setFrame(const QVector3D &center, float radius, float distance);
    void setCenter(const QVector3D &center) { m_center = center; }
    QVector3D center() const { return m_center; }
    float radius() const { return m_radius; }
    float gizmoWorldRadius() const;

    QMatrix4x4 viewMatrix() const;

    void mousePress(const QMouseEvent *e, const QSize &viewportSize);
    void mouseRelease(const QMouseEvent *e);
    bool mouseMove(const QMouseEvent *e, const QSize &viewportSize);
    bool wheel(const QWheelEvent *e);

private:
    enum class NavigationMode {
        None,
        Rotate,
        Pan
    };

    QVector3D projectOnArcball(const QPointF &pos, const QSize &viewportSize) const;
    QVector3D cameraRight() const;
    QVector3D cameraUp() const;
    QMatrix4x4 projectionMatrix(float aspect) const;
    QMatrix4x4 viewMatrixForRotation(const QQuaternion &rotation) const;
    QVector3D viewPointForRotation(const QQuaternion &rotation) const;
    bool viewRayFromWindow(const QPointF &pos, const QSize &viewportSize, const QQuaternion &rotation, QVector3D &rayOrigin, QVector3D &rayDir) const;
    bool hitViewPlane(const QPointF &pos, const QSize &viewportSize, const QQuaternion &rotation, QVector3D &hit) const;
    bool hitHyper(const QPointF &pos, const QSize &viewportSize, const QQuaternion &rotation, QVector3D &hit) const;
    bool hitSphereLikeVcg(const QPointF &pos, const QSize &viewportSize, const QQuaternion &rotation, QVector3D &hit) const;

    NavigationMode m_navigationMode = NavigationMode::None;
    QPointF m_lastMousePos;
    QVector3D m_lastArcballVec = QVector3D(0.0f, 0.0f, 1.0f);
    QVector3D m_dragStartHit;
    bool m_dragStartHitValid = false;
    QQuaternion m_dragStartRotation;
    QVector3D m_dragLastAxis = QVector3D(0.0f, 1.0f, 0.0f);
    bool m_dragLastAxisValid = false;
    QQuaternion m_rotation;
    QVector3D m_center;
    float m_distance = 3.0f;
    float m_radius = 1.0f;
    float m_gizmoBaseRadius = 1.0f;
    float m_gizmoReferenceDistance = 3.0f;
};
