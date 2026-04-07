#pragma once

#include "renderingsettings.h"
#include <QWidget>

class QPushButton;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QStackedWidget;
class QToolButton;

class RenderOverlayPanel : public QWidget
{
    Q_OBJECT
public:
    explicit RenderOverlayPanel(QWidget *parent = nullptr);

    const RenderSettings &settings() const { return m_settings; }
    void setSettings(const RenderSettings &settings);
    void setPointColorSourceAvailability(bool hasVertexColors);
    void setPointLightingAvailability(bool hasVertexNormals);
    void setFillColorSourceAvailability(bool hasVertexColors, bool hasFaceColors);

signals:
    void settingsChanged(const RenderSettings &settings);

private:
    void setCurrentRenderPass(RenderPass pass);
    void setSettingsVisible(bool visible);
    int renderPassPageIndex(RenderPass pass) const;
    void syncRenderPassUiState();
    void updateCurrentMeshOutlineColorButtonStyle();
    void updateDecoratorVertexNormalColorButtonStyle();
    void updateDecoratorFaceNormalColorButtonStyle();
    void updateDecoratorBoundaryEdgeColorButtonStyle();
    void updateDecoratorTextureSeamColorButtonStyle();
    void updateBBoxColorButtonStyle();
    void updatePointsColorButtonStyle();
    void updateWireColorButtonStyle();
    void updateFillColorButtonStyle();

    RenderSettings m_settings;

    QWidget *m_settingsContainer = nullptr;
    QStackedWidget *m_settingsStack = nullptr;
    QPushButton *m_currentMeshOutlineColorButton = nullptr;
    QPushButton *m_decoratorVertexNormalColorButton = nullptr;
    QPushButton *m_decoratorFaceNormalColorButton = nullptr;
    QPushButton *m_decoratorBoundaryEdgeColorButton = nullptr;
    QPushButton *m_decoratorTextureSeamColorButton = nullptr;
    QPushButton *m_bboxColorButton = nullptr;
    QPushButton *m_pointsColorButton = nullptr;
    QPushButton *m_wireColorButton = nullptr;
    QPushButton *m_fillColorButton = nullptr;
    QDoubleSpinBox *m_currentMeshOutlineWidthSpin = nullptr;
    QDoubleSpinBox *m_currentMeshDilateRadiusSpin = nullptr;
    QDoubleSpinBox *m_currentMeshErodeRadiusSpin = nullptr;
    QComboBox *m_currentMeshDebugViewCombo = nullptr;
    QCheckBox *m_currentMeshHighlightCheck = nullptr;
    QCheckBox *m_decoratorVertexNormalsCheck = nullptr;
    QCheckBox *m_decoratorFaceNormalsCheck = nullptr;
    QCheckBox *m_decoratorBoundaryEdgesCheck = nullptr;
    QCheckBox *m_decoratorTextureSeamsCheck = nullptr;
    QDoubleSpinBox *m_pointSizeSpin = nullptr;
    QDoubleSpinBox *m_wireSizeSpin = nullptr;
    QComboBox *m_pointColorSourceCombo = nullptr;
    QCheckBox *m_pointLightingCheck = nullptr;
    QCheckBox *m_wireLightingCheck = nullptr;
    QCheckBox *m_wireBackfaceCullingCheck = nullptr;
    QCheckBox *m_fillLightingCheck = nullptr;
    QCheckBox *m_fillBackfaceCullingCheck = nullptr;
    QComboBox *m_fillShadingCombo = nullptr;
    QComboBox *m_fillColorSourceCombo = nullptr;
    bool m_hasPointVertexColorSource = false;
    bool m_hasPointNormalSource = false;
    bool m_hasVertexColorSource = false;
    bool m_hasFaceColorSource = false;
    QToolButton *m_currentMeshButton = nullptr;
    QToolButton *m_modeButton = nullptr;
    QToolButton *m_normalsDecoratorsButton = nullptr;
    QToolButton *m_boundaryDecoratorsButton = nullptr;
    QToolButton *m_bboxButton = nullptr;
    QToolButton *m_pointsButton = nullptr;
    QToolButton *m_wireButton = nullptr;
    QToolButton *m_fillButton = nullptr;
    QToolButton *m_currentMeshSettingsArrow = nullptr;
    QToolButton *m_normalsDecoratorsSettingsArrow = nullptr;
    QToolButton *m_boundaryDecoratorsSettingsArrow = nullptr;
    QToolButton *m_bboxSettingsArrow = nullptr;
    QToolButton *m_pointsSettingsArrow = nullptr;
    QToolButton *m_wireSettingsArrow = nullptr;
    QToolButton *m_fillSettingsArrow = nullptr;
};
