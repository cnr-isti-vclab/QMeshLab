#pragma once

#include "interactivetool.h"

#include <QVector3D>

#include <vector>

class MeasureTool final : public InteractiveTool
{
public:
    QString id() const override;
    QString name() const override;
    QString statusHint() const override;
    QString badgeDetail() const override;
    QString iconPath() const override;
    QCursor cursor() const override;

    void activate(RenderWidget &view) override;
    void deactivate(bool commit) override;
    bool mousePress(QMouseEvent *e) override;
    bool keyPress(QKeyEvent *e) override;
    void onSurfacePicked(const SurfacePick &result) override;
    void cancelGesture() override;
    void paintOverlay(
        QPainter &painter,
        const QMatrix4x4 &worldToClip,
        const QSize &viewportSize) override;

private:
    struct Measurement {
        QVector3D a;
        QVector3D b;
        double length = 0.0;
    };

    void printMeasurements() const;
    void saveMeasurements() const;

    std::vector<Measurement> m_measurements;
    QVector3D m_firstPoint;
    bool m_hasFirstPoint = false;
};
