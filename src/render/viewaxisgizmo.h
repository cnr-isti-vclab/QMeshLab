#pragma once

#include <QQuaternion>
#include <QVector3D>
#include <QWidget>

#include <array>

// Blender-style view orientation gizmo: six axis handles drawn in a corner of
// the viewport, clicked to look down that axis. Deliberately a plain QWidget
// painted with QPainter rather than a pass in the RHI frame - it costs nothing
// on the GPU, but it is composited by the widget stack and so never shows up in
// offscreen captures (renderOffscreenToImage) or headless renders.
class ViewAxisGizmo : public QWidget
{
    Q_OBJECT
public:
    explicit ViewAxisGizmo(QWidget *parent = nullptr);

    // World-to-camera rotation, i.e. ViewTrackball::State::rotation.
    void setOrientation(const QQuaternion &rotation);

signals:
    // Unit world-space direction to move the camera along: the requested eye
    // position is center + axis * distance.
    void axisPicked(const QVector3D &axis);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    struct Handle {
        QPointF center;      // widget coordinates
        float depth = 0.0f;  // camera-space z; larger is nearer the viewer
        int id = 0;          // axis * 2 + (negative ? 1 : 0)
        int axis = 0;        // 0 = X, 1 = Y, 2 = Z
        bool negative = false;
    };

    std::array<Handle, 6> projectedHandles() const;
    // Returns a handle id, or -1 when the point misses every handle.
    int handleAt(const QPointF &pos) const;
    void setHovered(int id);

    QQuaternion m_rotation;
    int m_hovered = -1;
};
