#include "renderoverlaypanel.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <type_traits>

namespace {
const QColor kAccentColor(36, 132, 210);
const QColor kNeutralArrowColor(90, 90, 90, 175);
const QColor kActiveArrowColor(36, 132, 210, 235);

class PassArrowButton final : public QToolButton
{
public:
    explicit PassArrowButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setCheckable(true);
        setAutoRaise(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(32, 12);
        setToolTip(QObject::tr("Show settings for this pass"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        QColor arrowColor = isChecked() ? kActiveArrowColor : kNeutralArrowColor;
        if (underMouse() || isDown())
            arrowColor = kActiveArrowColor;
        p.setBrush(arrowColor);

        const int triW = 14;
        const int triH = 8;
        const int cx = width() / 2;
        const int top = (height() - triH) / 2;
        QPolygon poly;
        poly << QPoint(cx - triW / 2, top)
             << QPoint(cx + triW / 2, top)
             << QPoint(cx, top + triH);
        p.drawPolygon(poly);
    }
};
}

RenderOverlayPanel::RenderOverlayPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *panelLayout = new QVBoxLayout(this);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(2);

    auto *buttonRow = new QWidget(this);
    auto *buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(4);
    panelLayout->addWidget(buttonRow);

    const QString passButtonStyle = QStringLiteral(
        "QToolButton { background: rgba(250,250,250,210); border: 1px solid rgba(40,40,40,160); border-radius: 4px; }"
        "QToolButton:checked { background: rgba(%1,%2,%3,220); border-color: rgba(%1,%2,%3,240); }"
        "QToolButton:hover { background: rgba(220,230,245,220); }"
        "QToolButton[settingsTarget=\"true\"] { border: 2px solid rgba(%1,%2,%3,240); }")
            .arg(kAccentColor.red()).arg(kAccentColor.green()).arg(kAccentColor.blue());

    auto makeButton = [this, &passButtonStyle](const QString &iconPath, const QString &tooltip) {
        auto *btn = new QToolButton(this);
        btn->setIcon(QIcon(iconPath));
        btn->setToolTip(tooltip);
        btn->setCheckable(true);
        btn->setAutoRaise(false);
        btn->setIconSize(QSize(32, 32));
        btn->setFixedSize(32, 32);
        btn->setStyleSheet(passButtonStyle);
        return btn;
    };

    m_currentMeshButton = makeButton(QStringLiteral(":/img/global.png"), tr("Current Mesh Highlight"));
    m_currentMeshButton->setCheckable(false);
    m_modeButton = makeButton(QStringLiteral(":/img/options.png"), tr("Rendering Settings"));
    m_modeButton->setCheckable(true);
    m_modeButton->setChecked(false);
    m_normalsDecoratorsButton = makeButton(QStringLiteral(":/img/normals.png"), tr("Normal Decorators"));
    m_boundaryDecoratorsButton = makeButton(QStringLiteral(":/img/boundary.png"), tr("Boundary Decorators"));
    m_bboxButton = makeButton(QStringLiteral(":/img/box.png"), tr("Bounding Box"));
    m_pointsButton = makeButton(QStringLiteral(":/img/points.png"), tr("Points"));
    m_wireButton = makeButton(QStringLiteral(":/img/wire.png"), tr("Wireframe pass"));
    m_fillButton = makeButton(QStringLiteral(":/img/flat.png"), tr("Fill pass"));

    buttonLayout->addWidget(m_modeButton);
    buttonLayout->addWidget(m_currentMeshButton);
    buttonLayout->addWidget(m_bboxButton);
    buttonLayout->addWidget(m_pointsButton);
    buttonLayout->addWidget(m_wireButton);
    buttonLayout->addWidget(m_fillButton);
    buttonLayout->addWidget(m_normalsDecoratorsButton);
    buttonLayout->addWidget(m_boundaryDecoratorsButton);

    m_normalsDecoratorsButton->setChecked(false);
    m_boundaryDecoratorsButton->setChecked(false);
    m_bboxButton->setChecked(false);
    m_pointsButton->setChecked(false);
    m_wireButton->setChecked(true);
    m_fillButton->setChecked(true);

    auto *arrowRow = new QWidget(this);
    auto *arrowLayout = new QHBoxLayout(arrowRow);
    arrowLayout->setContentsMargins(0, 0, 0, 0);
    arrowLayout->setSpacing(4);
    panelLayout->addWidget(arrowRow);

    auto makeArrowButton = [this, arrowRow](const QString &tooltip) {
        auto *btn = new PassArrowButton(arrowRow);
        btn->setToolTip(tooltip);
        return btn;
    };
    auto *modeArrowSpacer = new QWidget(arrowRow);
    modeArrowSpacer->setFixedSize(32, 12);
    arrowLayout->addWidget(modeArrowSpacer);

    m_currentMeshSettingsArrow = makeArrowButton(tr("Settings: Current Mesh"));
    arrowLayout->addWidget(m_currentMeshSettingsArrow);

    m_bboxSettingsArrow = makeArrowButton(tr("Settings: Bounding Box"));
    m_pointsSettingsArrow = makeArrowButton(tr("Settings: Points"));
    m_wireSettingsArrow = makeArrowButton(tr("Settings: Wireframe"));
    m_fillSettingsArrow = makeArrowButton(tr("Settings: Fill"));
    arrowLayout->addWidget(m_bboxSettingsArrow);
    arrowLayout->addWidget(m_pointsSettingsArrow);
    arrowLayout->addWidget(m_wireSettingsArrow);
    arrowLayout->addWidget(m_fillSettingsArrow);
    m_normalsDecoratorsSettingsArrow = makeArrowButton(tr("Settings: Normal Decorators"));
    arrowLayout->addWidget(m_normalsDecoratorsSettingsArrow);
    m_boundaryDecoratorsSettingsArrow = makeArrowButton(tr("Settings: Boundary Decorators"));
    arrowLayout->addWidget(m_boundaryDecoratorsSettingsArrow);

    m_settingsContainer = new QFrame(this);
    m_settingsContainer->setVisible(false);
    m_settingsContainer->setStyleSheet(QStringLiteral(
        "QFrame { background: rgba(250,250,250,225); border: 1px solid rgba(40,40,40,160); border-radius: 4px; }"));

    auto *settingsContainerLayout = new QVBoxLayout(m_settingsContainer);
    settingsContainerLayout->setContentsMargins(6, 6, 6, 6);
    settingsContainerLayout->setSpacing(4);
    m_settingsStack = new QStackedWidget(m_settingsContainer);
    settingsContainerLayout->addWidget(m_settingsStack);

    auto *currentMeshPage = new QWidget(m_settingsStack);
    auto *currentMeshLayout = new QVBoxLayout(currentMeshPage);
    currentMeshLayout->setContentsMargins(0, 0, 0, 0);
    currentMeshLayout->setSpacing(2);
    auto *currentMeshForm = new QFormLayout();
    currentMeshForm->setContentsMargins(0, 0, 0, 0);
    currentMeshForm->setHorizontalSpacing(6);
    currentMeshForm->setVerticalSpacing(2);
    currentMeshForm->setLabelAlignment(Qt::AlignLeft);
    m_currentMeshHighlightCheck = new QCheckBox(tr("On"), currentMeshPage);
    m_currentMeshHighlightCheck->setChecked(m_settings.highlightCurrentMesh);
    m_currentMeshOutlineColorButton = new QPushButton(tr("Choose..."), currentMeshPage);
    m_currentMeshOutlineWidthSpin = new QDoubleSpinBox(currentMeshPage);
    m_currentMeshDilateRadiusSpin = new QDoubleSpinBox(currentMeshPage);
    m_currentMeshErodeRadiusSpin = new QDoubleSpinBox(currentMeshPage);
    m_currentMeshDebugViewCombo = new QComboBox(currentMeshPage);
    m_currentMeshOutlineWidthSpin->setRange(1.0, 8.0);
    m_currentMeshOutlineWidthSpin->setSingleStep(0.5);
    m_currentMeshOutlineWidthSpin->setDecimals(1);
    m_currentMeshOutlineWidthSpin->setSuffix(tr(" px"));
    m_currentMeshOutlineWidthSpin->setValue(m_settings.currentMeshOutlineWidth);
    m_currentMeshDilateRadiusSpin->setRange(0.0, 16.0);
    m_currentMeshDilateRadiusSpin->setSingleStep(0.5);
    m_currentMeshDilateRadiusSpin->setDecimals(1);
    m_currentMeshDilateRadiusSpin->setSuffix(tr(" px"));
    m_currentMeshDilateRadiusSpin->setValue(m_settings.currentMeshDilateRadius);
    m_currentMeshErodeRadiusSpin->setRange(0.0, 16.0);
    m_currentMeshErodeRadiusSpin->setSingleStep(0.5);
    m_currentMeshErodeRadiusSpin->setDecimals(1);
    m_currentMeshErodeRadiusSpin->setSuffix(tr(" px"));
    m_currentMeshErodeRadiusSpin->setValue(m_settings.currentMeshErodeRadius);
    m_currentMeshDebugViewCombo->addItem(
        tr("Outline"),
        static_cast<int>(CurrentMeshDebugView::Outline));
    m_currentMeshDebugViewCombo->addItem(
        tr("Base Mask"),
        static_cast<int>(CurrentMeshDebugView::BaseMask));
    m_currentMeshDebugViewCombo->addItem(
        tr("Dilated"),
        static_cast<int>(CurrentMeshDebugView::DilatedMask));
    m_currentMeshDebugViewCombo->addItem(
        tr("Eroded"),
        static_cast<int>(CurrentMeshDebugView::ErodedMask));
    currentMeshForm->addRow(tr("Highlight"), m_currentMeshHighlightCheck);
    currentMeshForm->addRow(tr("Outline color"), m_currentMeshOutlineColorButton);
    currentMeshForm->addRow(tr("Outline width"), m_currentMeshOutlineWidthSpin);
    currentMeshForm->addRow(tr("Dilate"), m_currentMeshDilateRadiusSpin);
    currentMeshForm->addRow(tr("Erode"), m_currentMeshErodeRadiusSpin);
    currentMeshForm->addRow(tr("Debug view"), m_currentMeshDebugViewCombo);
    currentMeshLayout->addLayout(currentMeshForm);
    m_settingsStack->addWidget(currentMeshPage);

    auto *normalDecoratorsPage = new QWidget(m_settingsStack);
    auto *normalDecoratorsLayout = new QVBoxLayout(normalDecoratorsPage);
    normalDecoratorsLayout->setContentsMargins(0, 0, 0, 0);
    normalDecoratorsLayout->setSpacing(2);
    auto *normalDecoratorsForm = new QFormLayout();
    normalDecoratorsForm->setContentsMargins(0, 0, 0, 0);
    normalDecoratorsForm->setHorizontalSpacing(6);
    normalDecoratorsForm->setVerticalSpacing(2);
    normalDecoratorsForm->setLabelAlignment(Qt::AlignLeft);
    m_decoratorVertexNormalsCheck = new QCheckBox(tr("On"), normalDecoratorsPage);
    m_decoratorFaceNormalsCheck = new QCheckBox(tr("On"), normalDecoratorsPage);
    m_decoratorBoundaryEdgesCheck = new QCheckBox(tr("On"), normalDecoratorsPage);
    m_decoratorTextureSeamsCheck = new QCheckBox(tr("On"), normalDecoratorsPage);
    m_decoratorVertexNormalsCheck->setChecked(m_settings.decoratorVertexNormals);
    m_decoratorFaceNormalsCheck->setChecked(m_settings.decoratorFaceNormals);
    m_decoratorBoundaryEdgesCheck->setChecked(m_settings.decoratorBoundaryEdges);
    m_decoratorTextureSeamsCheck->setChecked(m_settings.decoratorTextureSeams);
    m_decoratorVertexNormalColorButton = new QPushButton(tr("Choose..."), normalDecoratorsPage);
    m_decoratorFaceNormalColorButton = new QPushButton(tr("Choose..."), normalDecoratorsPage);
    m_decoratorBoundaryEdgeColorButton = new QPushButton(tr("Choose..."), normalDecoratorsPage);
    m_decoratorTextureSeamColorButton = new QPushButton(tr("Choose..."), normalDecoratorsPage);
    normalDecoratorsForm->addRow(tr("Vertex normals"), m_decoratorVertexNormalsCheck);
    normalDecoratorsForm->addRow(tr("Vertex normal color"), m_decoratorVertexNormalColorButton);
    normalDecoratorsForm->addRow(tr("Face normals"), m_decoratorFaceNormalsCheck);
    normalDecoratorsForm->addRow(tr("Face normal color"), m_decoratorFaceNormalColorButton);
    normalDecoratorsLayout->addLayout(normalDecoratorsForm);
    m_settingsStack->addWidget(normalDecoratorsPage);

    auto *boundaryDecoratorsPage = new QWidget(m_settingsStack);
    auto *boundaryDecoratorsLayout = new QVBoxLayout(boundaryDecoratorsPage);
    boundaryDecoratorsLayout->setContentsMargins(0, 0, 0, 0);
    boundaryDecoratorsLayout->setSpacing(2);
    auto *boundaryDecoratorsForm = new QFormLayout();
    boundaryDecoratorsForm->setContentsMargins(0, 0, 0, 0);
    boundaryDecoratorsForm->setHorizontalSpacing(6);
    boundaryDecoratorsForm->setVerticalSpacing(2);
    boundaryDecoratorsForm->setLabelAlignment(Qt::AlignLeft);
    boundaryDecoratorsForm->addRow(tr("Boundary edges"), m_decoratorBoundaryEdgesCheck);
    boundaryDecoratorsForm->addRow(tr("Boundary edge color"), m_decoratorBoundaryEdgeColorButton);
    boundaryDecoratorsForm->addRow(tr("Texture seams"), m_decoratorTextureSeamsCheck);
    boundaryDecoratorsForm->addRow(tr("Texture seam color"), m_decoratorTextureSeamColorButton);
    boundaryDecoratorsLayout->addLayout(boundaryDecoratorsForm);
    m_settingsStack->addWidget(boundaryDecoratorsPage);

    auto *bboxPage = new QWidget(m_settingsStack);
    auto *bboxLayout = new QVBoxLayout(bboxPage);
    bboxLayout->setContentsMargins(0, 0, 0, 0);
    bboxLayout->setSpacing(2);
    auto *bboxForm = new QFormLayout();
    bboxForm->setContentsMargins(0, 0, 0, 0);
    bboxForm->setHorizontalSpacing(6);
    bboxForm->setVerticalSpacing(2);
    bboxForm->setLabelAlignment(Qt::AlignLeft);
    m_bboxColorButton = new QPushButton(tr("Choose..."), bboxPage);
    bboxForm->addRow(tr("Wire color"), m_bboxColorButton);
    bboxLayout->addLayout(bboxForm);

    m_settingsStack->addWidget(bboxPage);
    auto *pointsPage = new QWidget(m_settingsStack);
    auto *pointsLayout = new QVBoxLayout(pointsPage);
    pointsLayout->setContentsMargins(0, 0, 0, 0);
    pointsLayout->setSpacing(2);
    auto *pointsForm = new QFormLayout();
    pointsForm->setContentsMargins(0, 0, 0, 0);
    pointsForm->setHorizontalSpacing(6);
    pointsForm->setVerticalSpacing(2);
    pointsForm->setLabelAlignment(Qt::AlignLeft);
    m_pointsColorButton = new QPushButton(tr("Choose..."), pointsPage);
    m_pointColorSourceCombo = new QComboBox(pointsPage);
    m_pointColorSourceCombo->addItem(tr("Constant"), static_cast<int>(PointColorSource::Constant));
    m_pointColorSourceCombo->addItem(tr("Per-Vertex"), static_cast<int>(PointColorSource::PerVertex));
    m_pointSizeSpin = new QDoubleSpinBox(pointsPage);
    m_pointSizeSpin->setRange(1.0, 32.0);
    m_pointSizeSpin->setSingleStep(0.5);
    m_pointSizeSpin->setDecimals(1);
    m_pointSizeSpin->setSuffix(tr(" px"));
    m_pointSizeSpin->setValue(m_settings.pointSize);
    m_pointLightingCheck = new QCheckBox(tr("On"), pointsPage);
    m_pointLightingCheck->setChecked(m_settings.pointLighting);
    pointsForm->addRow(tr("Color source"), m_pointColorSourceCombo);
    pointsForm->addRow(tr("Point color"), m_pointsColorButton);
    pointsForm->addRow(tr("Point size"), m_pointSizeSpin);
    pointsForm->addRow(tr("Lighting"), m_pointLightingCheck);
    pointsLayout->addLayout(pointsForm);
    m_settingsStack->addWidget(pointsPage);
    auto *wirePage = new QWidget(m_settingsStack);
    auto *wireLayout = new QVBoxLayout(wirePage);
    wireLayout->setContentsMargins(0, 0, 0, 0);
    wireLayout->setSpacing(2);
    auto *wireForm = new QFormLayout();
    wireForm->setContentsMargins(0, 0, 0, 0);
    wireForm->setHorizontalSpacing(6);
    wireForm->setVerticalSpacing(2);
    wireForm->setLabelAlignment(Qt::AlignLeft);
    m_wireColorButton = new QPushButton(tr("Choose..."), wirePage);
    m_wireSizeSpin = new QDoubleSpinBox(wirePage);
    m_wireSizeSpin->setRange(0.5, 8.0);
    m_wireSizeSpin->setSingleStep(0.1);
    m_wireSizeSpin->setDecimals(1);
    m_wireSizeSpin->setSuffix(tr(" px"));
    m_wireSizeSpin->setValue(m_settings.wireSize);
    m_wireBackfaceCullingCheck = new QCheckBox(tr("On"), wirePage);
    m_wireBackfaceCullingCheck->setChecked(m_settings.wireBackfaceCulling);
    m_wireLightingCheck = new QCheckBox(tr("On"), wirePage);
    m_wireLightingCheck->setChecked(m_settings.wireLighting);
    wireForm->addRow(tr("Wire color"), m_wireColorButton);
    wireForm->addRow(tr("Wire size"), m_wireSizeSpin);
    wireForm->addRow(tr("Backface culling"), m_wireBackfaceCullingCheck);
    wireForm->addRow(tr("Lighting"), m_wireLightingCheck);
    wireLayout->addLayout(wireForm);
    m_settingsStack->addWidget(wirePage);
    auto *fillPage = new QWidget(m_settingsStack);
    auto *fillLayout = new QVBoxLayout(fillPage);
    fillLayout->setContentsMargins(0, 0, 0, 0);
    fillLayout->setSpacing(2);
    auto *fillForm = new QFormLayout();
    fillForm->setContentsMargins(0, 0, 0, 0);
    fillForm->setHorizontalSpacing(6);
    fillForm->setVerticalSpacing(2);
    fillForm->setLabelAlignment(Qt::AlignLeft);
    m_fillColorButton = new QPushButton(tr("Choose..."), fillPage);
    m_fillColorSourceCombo = new QComboBox(fillPage);
    m_fillColorSourceCombo->addItem(tr("Constant"), static_cast<int>(FillColorSource::Constant));
    m_fillColorSourceCombo->addItem(tr("Per-Vertex"), static_cast<int>(FillColorSource::PerVertex));
    m_fillColorSourceCombo->addItem(tr("Per-Face"), static_cast<int>(FillColorSource::PerFace));
    m_fillShadingCombo = new QComboBox(fillPage);
    m_fillShadingCombo->addItem(tr("Smooth"), static_cast<int>(FillShading::Smooth));
    m_fillShadingCombo->addItem(tr("Flat"), static_cast<int>(FillShading::Flat));
    m_fillBackfaceCullingCheck = new QCheckBox(tr("On"), fillPage);
    m_fillBackfaceCullingCheck->setChecked(m_settings.fillBackfaceCulling);
    m_fillLightingCheck = new QCheckBox(tr("On"), fillPage);
    m_fillLightingCheck->setChecked(m_settings.fillLighting);
    fillForm->addRow(tr("Color source"), m_fillColorSourceCombo);
    fillForm->addRow(tr("Fill color"), m_fillColorButton);
    fillForm->addRow(tr("Shading"), m_fillShadingCombo);
    fillForm->addRow(tr("Backface culling"), m_fillBackfaceCullingCheck);
    fillForm->addRow(tr("Lighting"), m_fillLightingCheck);
    fillLayout->addLayout(fillForm);
    m_settingsStack->addWidget(fillPage);
    panelLayout->addWidget(m_settingsContainer);

    auto setField = [this](auto member, const auto &value, bool syncUi = false) {
        if (m_settings.*member == value)
            return false;
        m_settings.*member = value;
        if (syncUi)
            setSettings(m_settings);
        emit settingsChanged(m_settings);
        return true;
    };

    auto bindCheckBox = [this, setField](
                            QCheckBox *checkBox,
                            bool RenderSettings::*member,
                            bool syncUi = false) {
        connect(checkBox, &QCheckBox::toggled, this, [this, setField, member, syncUi](bool checked) {
            setField(member, checked, syncUi);
        });
    };

    auto bindToolToggle = [this, setField](
                              QToolButton *button,
                              bool RenderSettings::*member,
                              bool syncUi = false) {
        connect(button, &QToolButton::toggled, this, [this, setField, member, syncUi](bool checked) {
            setField(member, checked, syncUi);
        });
    };

    auto bindFloatSpin = [this, setField](QDoubleSpinBox *spin, float RenderSettings::*member) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, setField, member](double value) {
            setField(member, static_cast<float>(value));
        });
    };

    auto bindEnumCombo = [this, setField](QComboBox *combo, auto member) {
        using EnumType = std::decay_t<decltype(m_settings.*member)>;
        connect(
            combo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this, setField, combo, member](int idx) {
                const QVariant data = combo->itemData(idx);
                if (!data.isValid())
                    return;
                setField(member, static_cast<EnumType>(data.toInt()));
            });
    };

    auto bindColorButton = [this, setField](
                               QPushButton *button,
                               QColor RenderSettings::*member,
                               const QString &dialogTitle) {
        connect(button, &QPushButton::clicked, this, [this, setField, button, member, dialogTitle]() {
            const QColor picked = QColorDialog::getColor(m_settings.*member, this, dialogTitle);
            if (!picked.isValid())
                return;
            if (setField(member, picked))
                updateColorButtonStyle(button, picked);
        });
    };

    auto bindPassButton = [this](QToolButton *button, RenderPass pass, bool showSettings) {
        connect(button, &QToolButton::clicked, this, [this, pass, showSettings]() {
            setCurrentRenderPass(pass);
            if (showSettings)
                setSettingsVisible(true);
        });
    };

    connect(m_modeButton, &QToolButton::toggled, this, [this](bool checked) {
        if (m_settings.settingsPanelVisible == checked)
            return;
        m_settings.settingsPanelVisible = checked;
        if (m_settingsContainer)
            m_settingsContainer->setVisible(checked);
        adjustSize();
        emit settingsChanged(m_settings);
    });

    bindCheckBox(m_currentMeshHighlightCheck, &RenderSettings::highlightCurrentMesh);
    bindColorButton(
        m_currentMeshOutlineColorButton,
        &RenderSettings::currentMeshOutlineColor,
        tr("Current Mesh Outline Color"));
    bindFloatSpin(m_currentMeshOutlineWidthSpin, &RenderSettings::currentMeshOutlineWidth);
    bindFloatSpin(m_currentMeshDilateRadiusSpin, &RenderSettings::currentMeshDilateRadius);
    bindFloatSpin(m_currentMeshErodeRadiusSpin, &RenderSettings::currentMeshErodeRadius);
    bindEnumCombo(m_currentMeshDebugViewCombo, &RenderSettings::currentMeshDebugView);

    bindCheckBox(m_decoratorVertexNormalsCheck, &RenderSettings::decoratorVertexNormals, true);
    bindCheckBox(m_decoratorFaceNormalsCheck, &RenderSettings::decoratorFaceNormals, true);
    bindCheckBox(m_decoratorBoundaryEdgesCheck, &RenderSettings::decoratorBoundaryEdges, true);
    bindCheckBox(m_decoratorTextureSeamsCheck, &RenderSettings::decoratorTextureSeams, true);
    bindColorButton(
        m_decoratorVertexNormalColorButton,
        &RenderSettings::decoratorVertexNormalColor,
        tr("Decorator Vertex Normal Color"));
    bindColorButton(
        m_decoratorFaceNormalColorButton,
        &RenderSettings::decoratorFaceNormalColor,
        tr("Decorator Face Normal Color"));
    bindColorButton(
        m_decoratorBoundaryEdgeColorButton,
        &RenderSettings::decoratorBoundaryEdgeColor,
        tr("Decorator Boundary Edge Color"));
    bindColorButton(
        m_decoratorTextureSeamColorButton,
        &RenderSettings::decoratorTextureSeamColor,
        tr("Decorator Texture Seam Color"));

    bindColorButton(m_bboxColorButton, &RenderSettings::bboxWireColor, tr("Bounding Box Wire Color"));

    bindEnumCombo(m_pointColorSourceCombo, &RenderSettings::pointColorSource);
    bindColorButton(m_pointsColorButton, &RenderSettings::pointColor, tr("Point Color"));
    bindFloatSpin(m_pointSizeSpin, &RenderSettings::pointSize);
    bindCheckBox(m_pointLightingCheck, &RenderSettings::pointLighting);

    bindColorButton(m_wireColorButton, &RenderSettings::wireColor, tr("Wire Color"));
    bindFloatSpin(m_wireSizeSpin, &RenderSettings::wireSize);
    bindCheckBox(m_wireBackfaceCullingCheck, &RenderSettings::wireBackfaceCulling);
    bindCheckBox(m_wireLightingCheck, &RenderSettings::wireLighting);

    bindEnumCombo(m_fillColorSourceCombo, &RenderSettings::fillColorSource);
    bindColorButton(m_fillColorButton, &RenderSettings::fillColor, tr("Fill Color"));
    bindEnumCombo(m_fillShadingCombo, &RenderSettings::fillShading);
    bindCheckBox(m_fillBackfaceCullingCheck, &RenderSettings::fillBackfaceCulling);
    bindCheckBox(m_fillLightingCheck, &RenderSettings::fillLighting);

    bindPassButton(m_currentMeshButton, RenderPass::CurrentMesh, true);
    bindPassButton(m_normalsDecoratorsButton, RenderPass::DecoratorNormals, false);
    bindPassButton(m_boundaryDecoratorsButton, RenderPass::DecoratorBoundary, false);
    bindPassButton(m_bboxButton, RenderPass::BoundingBox, false);
    bindPassButton(m_pointsButton, RenderPass::Points, false);
    bindPassButton(m_wireButton, RenderPass::Wireframe, false);
    bindPassButton(m_fillButton, RenderPass::Fill, false);

    bindPassButton(m_currentMeshSettingsArrow, RenderPass::CurrentMesh, true);
    bindPassButton(m_normalsDecoratorsSettingsArrow, RenderPass::DecoratorNormals, true);
    bindPassButton(m_boundaryDecoratorsSettingsArrow, RenderPass::DecoratorBoundary, true);
    bindPassButton(m_bboxSettingsArrow, RenderPass::BoundingBox, true);
    bindPassButton(m_pointsSettingsArrow, RenderPass::Points, true);
    bindPassButton(m_wireSettingsArrow, RenderPass::Wireframe, true);
    bindPassButton(m_fillSettingsArrow, RenderPass::Fill, true);

    bindToolToggle(m_bboxButton, &RenderSettings::showBoundingBox);
    connect(m_normalsDecoratorsButton, &QToolButton::toggled, this, [this](bool checked) {
        const bool changed =
            (m_settings.decoratorVertexNormals != checked)
            || (m_settings.decoratorFaceNormals != checked);
        if (!changed)
            return;
        m_settings.decoratorVertexNormals = checked;
        m_settings.decoratorFaceNormals = checked;
        setSettings(m_settings);
        emit settingsChanged(m_settings);
    });
    connect(m_boundaryDecoratorsButton, &QToolButton::toggled, this, [this](bool checked) {
        const bool changed =
            (m_settings.decoratorBoundaryEdges != checked)
            || (m_settings.decoratorTextureSeams != checked);
        if (!changed)
            return;
        m_settings.decoratorBoundaryEdges = checked;
        m_settings.decoratorTextureSeams = checked;
        setSettings(m_settings);
        emit settingsChanged(m_settings);
    });
    bindToolToggle(m_pointsButton, &RenderSettings::showPoints);
    bindToolToggle(m_wireButton, &RenderSettings::showWire);
    bindToolToggle(m_fillButton, &RenderSettings::showFill);

    updateColorButtonStyle(m_currentMeshOutlineColorButton, m_settings.currentMeshOutlineColor);
    updateColorButtonStyle(m_decoratorVertexNormalColorButton, m_settings.decoratorVertexNormalColor);
    updateColorButtonStyle(m_decoratorFaceNormalColorButton, m_settings.decoratorFaceNormalColor);
    updateColorButtonStyle(m_decoratorBoundaryEdgeColorButton, m_settings.decoratorBoundaryEdgeColor);
    updateColorButtonStyle(m_decoratorTextureSeamColorButton, m_settings.decoratorTextureSeamColor);
    updateColorButtonStyle(m_bboxColorButton, m_settings.bboxWireColor);
    updateColorButtonStyle(m_pointsColorButton, m_settings.pointColor);
    updateColorButtonStyle(m_wireColorButton, m_settings.wireColor);
    updateColorButtonStyle(m_fillColorButton, m_settings.fillColor);
    setPointColorSourceAvailability(false);
    setPointLightingAvailability(false);
    setFillColorSourceAvailability(false, false);
    if (m_settingsStack)
        m_settingsStack->setCurrentIndex(renderPassPageIndex(m_settings.currentPass));
    if (m_settingsContainer)
        m_settingsContainer->setVisible(m_settings.settingsPanelVisible);
    syncRenderPassUiState();
}

int RenderOverlayPanel::renderPassPageIndex(RenderPass pass) const
{
    switch (pass) {
    case RenderPass::CurrentMesh: return 0;
    case RenderPass::DecoratorNormals: return 1;
    case RenderPass::DecoratorBoundary: return 2;
    case RenderPass::BoundingBox: return 3;
    case RenderPass::Points: return 4;
    case RenderPass::Wireframe: return 5;
    case RenderPass::Fill: return 6;
    }
    return 0;
}

void RenderOverlayPanel::setCurrentRenderPass(RenderPass pass)
{
    if (m_settings.currentPass == pass)
        return;
    m_settings.currentPass = pass;
    if (m_settingsStack)
        m_settingsStack->setCurrentIndex(renderPassPageIndex(pass));
    syncRenderPassUiState();
    emit settingsChanged(m_settings);
}

void RenderOverlayPanel::setSettingsVisible(bool visible)
{
    if (m_modeButton && m_modeButton->isChecked() != visible)
        m_modeButton->setChecked(visible);
}

void RenderOverlayPanel::setSettings(const RenderSettings &settings)
{
    m_settings = settings;

    if (m_currentMeshHighlightCheck) {
        QSignalBlocker blocker(m_currentMeshHighlightCheck);
        m_currentMeshHighlightCheck->setChecked(m_settings.highlightCurrentMesh);
    }
    if (m_bboxButton) {
        QSignalBlocker blocker(m_bboxButton);
        m_bboxButton->setChecked(m_settings.showBoundingBox);
    }
    if (m_normalsDecoratorsButton) {
        QSignalBlocker blocker(m_normalsDecoratorsButton);
        m_normalsDecoratorsButton->setChecked(
            m_settings.decoratorVertexNormals || m_settings.decoratorFaceNormals);
    }
    if (m_boundaryDecoratorsButton) {
        QSignalBlocker blocker(m_boundaryDecoratorsButton);
        m_boundaryDecoratorsButton->setChecked(
            m_settings.decoratorBoundaryEdges || m_settings.decoratorTextureSeams);
    }
    if (m_pointsButton) {
        QSignalBlocker blocker(m_pointsButton);
        m_pointsButton->setChecked(m_settings.showPoints);
    }
    if (m_wireButton) {
        QSignalBlocker blocker(m_wireButton);
        m_wireButton->setChecked(m_settings.showWire);
    }
    if (m_fillButton) {
        QSignalBlocker blocker(m_fillButton);
        m_fillButton->setChecked(m_settings.showFill);
    }
    if (m_modeButton) {
        QSignalBlocker blocker(m_modeButton);
        m_modeButton->setChecked(m_settings.settingsPanelVisible);
    }
    if (m_currentMeshOutlineWidthSpin) {
        QSignalBlocker blocker(m_currentMeshOutlineWidthSpin);
        m_currentMeshOutlineWidthSpin->setValue(m_settings.currentMeshOutlineWidth);
    }
    if (m_currentMeshDilateRadiusSpin) {
        QSignalBlocker blocker(m_currentMeshDilateRadiusSpin);
        m_currentMeshDilateRadiusSpin->setValue(m_settings.currentMeshDilateRadius);
    }
    if (m_currentMeshErodeRadiusSpin) {
        QSignalBlocker blocker(m_currentMeshErodeRadiusSpin);
        m_currentMeshErodeRadiusSpin->setValue(m_settings.currentMeshErodeRadius);
    }
    if (m_currentMeshDebugViewCombo) {
        QSignalBlocker blocker(m_currentMeshDebugViewCombo);
        const int value = static_cast<int>(m_settings.currentMeshDebugView);
        for (int i = 0; i < m_currentMeshDebugViewCombo->count(); ++i) {
            if (m_currentMeshDebugViewCombo->itemData(i).toInt() == value) {
                m_currentMeshDebugViewCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    if (m_decoratorVertexNormalsCheck) {
        QSignalBlocker blocker(m_decoratorVertexNormalsCheck);
        m_decoratorVertexNormalsCheck->setChecked(m_settings.decoratorVertexNormals);
    }
    if (m_decoratorFaceNormalsCheck) {
        QSignalBlocker blocker(m_decoratorFaceNormalsCheck);
        m_decoratorFaceNormalsCheck->setChecked(m_settings.decoratorFaceNormals);
    }
    if (m_decoratorBoundaryEdgesCheck) {
        QSignalBlocker blocker(m_decoratorBoundaryEdgesCheck);
        m_decoratorBoundaryEdgesCheck->setChecked(m_settings.decoratorBoundaryEdges);
    }
    if (m_decoratorTextureSeamsCheck) {
        QSignalBlocker blocker(m_decoratorTextureSeamsCheck);
        m_decoratorTextureSeamsCheck->setChecked(m_settings.decoratorTextureSeams);
    }
    if (m_pointLightingCheck) {
        QSignalBlocker blocker(m_pointLightingCheck);
        m_pointLightingCheck->setChecked(m_settings.pointLighting);
    }
    if (m_wireLightingCheck) {
        QSignalBlocker blocker(m_wireLightingCheck);
        m_wireLightingCheck->setChecked(m_settings.wireLighting);
    }
    if (m_wireBackfaceCullingCheck) {
        QSignalBlocker blocker(m_wireBackfaceCullingCheck);
        m_wireBackfaceCullingCheck->setChecked(m_settings.wireBackfaceCulling);
    }
    if (m_fillLightingCheck) {
        QSignalBlocker blocker(m_fillLightingCheck);
        m_fillLightingCheck->setChecked(m_settings.fillLighting);
    }
    if (m_fillBackfaceCullingCheck) {
        QSignalBlocker blocker(m_fillBackfaceCullingCheck);
        m_fillBackfaceCullingCheck->setChecked(m_settings.fillBackfaceCulling);
    }
    if (m_pointSizeSpin) {
        QSignalBlocker blocker(m_pointSizeSpin);
        m_pointSizeSpin->setValue(m_settings.pointSize);
    }
    if (m_wireSizeSpin) {
        QSignalBlocker blocker(m_wireSizeSpin);
        m_wireSizeSpin->setValue(m_settings.wireSize);
    }
    if (m_pointColorSourceCombo) {
        QSignalBlocker blocker(m_pointColorSourceCombo);
        const int value = static_cast<int>(m_settings.pointColorSource);
        for (int i = 0; i < m_pointColorSourceCombo->count(); ++i) {
            if (m_pointColorSourceCombo->itemData(i).toInt() == value) {
                m_pointColorSourceCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    if (m_fillShadingCombo) {
        QSignalBlocker blocker(m_fillShadingCombo);
        const int value = static_cast<int>(m_settings.fillShading);
        for (int i = 0; i < m_fillShadingCombo->count(); ++i) {
            if (m_fillShadingCombo->itemData(i).toInt() == value) {
                m_fillShadingCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    if (m_fillColorSourceCombo) {
        QSignalBlocker blocker(m_fillColorSourceCombo);
        const int value = static_cast<int>(m_settings.fillColorSource);
        for (int i = 0; i < m_fillColorSourceCombo->count(); ++i) {
            if (m_fillColorSourceCombo->itemData(i).toInt() == value) {
                m_fillColorSourceCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    if (m_settingsContainer)
        m_settingsContainer->setVisible(m_settings.settingsPanelVisible);
    if (m_settingsStack)
        m_settingsStack->setCurrentIndex(renderPassPageIndex(m_settings.currentPass));

    updateColorButtonStyle(m_currentMeshOutlineColorButton, m_settings.currentMeshOutlineColor);
    updateColorButtonStyle(m_decoratorVertexNormalColorButton, m_settings.decoratorVertexNormalColor);
    updateColorButtonStyle(m_decoratorFaceNormalColorButton, m_settings.decoratorFaceNormalColor);
    updateColorButtonStyle(m_decoratorBoundaryEdgeColorButton, m_settings.decoratorBoundaryEdgeColor);
    updateColorButtonStyle(m_decoratorTextureSeamColorButton, m_settings.decoratorTextureSeamColor);
    updateColorButtonStyle(m_bboxColorButton, m_settings.bboxWireColor);
    updateColorButtonStyle(m_pointsColorButton, m_settings.pointColor);
    updateColorButtonStyle(m_wireColorButton, m_settings.wireColor);
    updateColorButtonStyle(m_fillColorButton, m_settings.fillColor);
    syncRenderPassUiState();
}

void RenderOverlayPanel::setPointColorSourceAvailability(bool hasVertexColors)
{
    if (!m_pointColorSourceCombo)
        return;

    const int vertexIndex =
        m_pointColorSourceCombo->findData(static_cast<int>(PointColorSource::PerVertex));
    if (vertexIndex >= 0) {
        m_pointColorSourceCombo->setItemData(
            vertexIndex,
            hasVertexColors ? QVariant() : QVariant(0),
            Qt::UserRole - 1);
    }
}

void RenderOverlayPanel::setPointLightingAvailability(bool hasVertexNormals)
{
    if (!m_pointLightingCheck)
        return;
    m_pointLightingCheck->setEnabled(hasVertexNormals);
}

void RenderOverlayPanel::setFillColorSourceAvailability(bool hasVertexColors, bool hasFaceColors)
{
    if (!m_fillColorSourceCombo)
        return;

    const int vertexIndex =
        m_fillColorSourceCombo->findData(static_cast<int>(FillColorSource::PerVertex));
    const int faceIndex =
        m_fillColorSourceCombo->findData(static_cast<int>(FillColorSource::PerFace));

    if (vertexIndex >= 0) {
        m_fillColorSourceCombo->setItemData(
            vertexIndex,
            hasVertexColors ? QVariant() : QVariant(0),
            Qt::UserRole - 1);
    }
    if (faceIndex >= 0) {
        m_fillColorSourceCombo->setItemData(
            faceIndex,
            hasFaceColors ? QVariant() : QVariant(0),
            Qt::UserRole - 1);
    }
}

void RenderOverlayPanel::updateColorButtonStyle(QPushButton *button, const QColor &color)
{
    if (!button)
        return;
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid rgba(40,40,40,160); border-radius: 3px; padding: 4px 8px; }")
            .arg(color.name()));
}

void RenderOverlayPanel::syncRenderPassUiState()
{
    auto setPassMarker = [this](QToolButton *btn, RenderPass pass) {
        if (!btn)
            return;
        const bool isTarget = (m_settings.currentPass == pass);
        if (btn->property("settingsTarget").toBool() == isTarget)
            return;
        btn->setProperty("settingsTarget", isTarget);
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
        btn->update();
    };

    auto setArrowChecked = [this](QToolButton *btn, RenderPass pass) {
        if (!btn)
            return;
        const bool isTarget = (m_settings.currentPass == pass);
        QSignalBlocker blocker(btn);
        btn->setChecked(isTarget);
        btn->update();
    };

    setPassMarker(m_currentMeshButton, RenderPass::CurrentMesh);
    setPassMarker(m_normalsDecoratorsButton, RenderPass::DecoratorNormals);
    setPassMarker(m_boundaryDecoratorsButton, RenderPass::DecoratorBoundary);
    setPassMarker(m_bboxButton, RenderPass::BoundingBox);
    setPassMarker(m_pointsButton, RenderPass::Points);
    setPassMarker(m_wireButton, RenderPass::Wireframe);
    setPassMarker(m_fillButton, RenderPass::Fill);

    setArrowChecked(m_currentMeshSettingsArrow, RenderPass::CurrentMesh);
    setArrowChecked(m_normalsDecoratorsSettingsArrow, RenderPass::DecoratorNormals);
    setArrowChecked(m_boundaryDecoratorsSettingsArrow, RenderPass::DecoratorBoundary);
    setArrowChecked(m_bboxSettingsArrow, RenderPass::BoundingBox);
    setArrowChecked(m_pointsSettingsArrow, RenderPass::Points);
    setArrowChecked(m_wireSettingsArrow, RenderPass::Wireframe);
    setArrowChecked(m_fillSettingsArrow, RenderPass::Fill);
}
