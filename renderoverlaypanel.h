#pragma once

#include "renderingsettings.h"
#include <QWidget>

class QPushButton;
class QDoubleSpinBox;
class QComboBox;
class QStackedWidget;
class QToolButton;

class RenderOverlayPanel : public QWidget
{
    Q_OBJECT
public:
    explicit RenderOverlayPanel(QWidget *parent = nullptr);

    const RenderSettings &settings() const { return m_settings; }
    void setSettings(const RenderSettings &settings);

signals:
    void settingsChanged(const RenderSettings &settings);

private:
    void setCurrentRenderPass(RenderPass pass);
    void setSettingsVisible(bool visible);
    int renderPassPageIndex(RenderPass pass) const;
    void syncRenderPassUiState();
    void updateBBoxColorButtonStyle();
    void updatePointsColorButtonStyle();
    void updateWireColorButtonStyle();
    void updateFillColorButtonStyle();

    RenderSettings m_settings;

    QWidget *m_settingsContainer = nullptr;
    QStackedWidget *m_settingsStack = nullptr;
    QPushButton *m_bboxColorButton = nullptr;
    QPushButton *m_pointsColorButton = nullptr;
    QPushButton *m_wireColorButton = nullptr;
    QPushButton *m_fillColorButton = nullptr;
    QDoubleSpinBox *m_pointSizeSpin = nullptr;
    QDoubleSpinBox *m_wireSizeSpin = nullptr;
    QComboBox *m_fillShadingCombo = nullptr;
    QToolButton *m_modeButton = nullptr;
    QToolButton *m_bboxButton = nullptr;
    QToolButton *m_pointsButton = nullptr;
    QToolButton *m_wireButton = nullptr;
    QToolButton *m_fillButton = nullptr;
    QToolButton *m_bboxSettingsArrow = nullptr;
    QToolButton *m_pointsSettingsArrow = nullptr;
    QToolButton *m_wireSettingsArrow = nullptr;
    QToolButton *m_fillSettingsArrow = nullptr;
};
