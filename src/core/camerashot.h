#pragma once

#include <vcg/math/shot.h>
#include <QMatrix4x4>
#include <QSize>
#include <QVector2D>
#include <QVector3D>
#include <array>

class CameraShot
{
public:
    using VcgShot = vcg::Shot<float>;

    enum class CameraType {
        Perspective,
        Orthographic,
        Isometric,
        Cavalieri
    };

    CameraShot();

    static CameraShot fromVcgShot(const VcgShot &shot);
    static CameraShot defaultPerspectiveForImageSize(
        const QSize &imageSize,
        float focalMm = 50.0f,
        float sensorWidthMm = 36.0f);

    VcgShot toVcgShot() const;

    bool isValid() const;

    CameraType cameraType() const;
    void setCameraType(CameraType type);

    QSize viewportPx() const;
    void setViewportPx(const QSize &size);

    QVector2D centerPx() const;
    void setCenterPx(const QVector2D &center);

    QVector2D pixelSizeMm() const;
    void setPixelSizeMm(const QVector2D &pixelSize);

    QVector2D distortionCenterPx() const;
    void setDistortionCenterPx(const QVector2D &center);

    std::array<float, 4> distortionK() const;
    void setDistortionK(const std::array<float, 4> &k);

    float focalMm() const;
    void setFocalMm(float focalMm);

    QVector3D viewPoint() const;
    void setViewPoint(const QVector3D &point);

    QVector3D referenceAxis(int axisIndex) const;
    QVector3D viewDirection() const;

    QVector2D project(const QVector3D &worldPoint) const;
    QVector3D unproject(const QVector2D &pixelPoint, float depth) const;
    float depth(const QVector3D &worldPoint) const;
    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projectionMatrix(float nearPlane, float farPlane) const;

    // Transform the shot's extrinsic parameters
    void applyRigidTransformation(const QMatrix4x4 &m);
    void rescalingWorld(float scale);
    void applySimilarity(const QMatrix4x4 &m);

private:
    VcgShot m_shot;
};
