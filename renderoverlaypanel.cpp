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
    m_fillLightingCheck = new QCheckBox(tr("On"), fillPage);
    fillForm->addRow(tr("Color source"), m_fillColorSourceCombo);
    fillForm->addRow(tr("Fill color"), m_fillColorButton);
    fillForm->addRow(tr("Shading"), m_fillShadingCombo);
    fillForm->addRow(tr("Lighting"), m_fillLightingCheck);
    fillLayout->addLayout(fillForm);
    m_settingsStack->addWidget(fillPage);
    panelLayout->addWidget(m_settingsContainer);

    connect(m_modeButton, &QToolButton::toggled, this, [this](bool checked) {
        if (m_settings.settingsPanelVisible == checked)
            return;
        m_settings.settingsPanelVisible = checked;
        if (m_settingsContainer)
            m_settingsContainer->setVisible(checked);
        emit settingsChanged(m_settings);
    });
    connect(m_currentMeshHighlightCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_settings.highlightCurrentMesh == checked)
            return;
        m_settings.highlightCurrentMesh = checked;
        emit settingsChanged(m_settings);
    });
    connect(m_currentMeshOutlineColorButton, &QPushButton::clicked, this, [this]() {
        const QColor picked = QColorDialog::getColor(
            m_settings.currentMeshOutlineColor, this, tr("Current Mesh Outline Color"));
        if (!picked.isValid())
            return;
        m_settings.currentMeshOutlineColor = picked;
        updateCurrentMeshOutlineColorButtonStyle();
        emit settingsChanged(m_settings);
    });
    connect(m_currentMeshOutlineWidthSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const float newWidth = static_cast<float>(value);
        if (m_settings.currentMeshOutlineWidth == newWidth)
            return;
        m_settings.currentMeshOutlineWidth = newWidth;
        emit settingsChanged(m_settings);
    });
    connect(m_currentMeshDilateRadiusSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const float newRadius = static_cast<float>(value);
        if (m_settings.currentMeshDilateRadius == newRadius)
            return;
        m_settings.currentMeshDilateRadius = newRadius;
        emit settingsChanged(m_settings);
    });
    connect(m_currentMeshErodeRadiusSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const float newRadius = static_cast<float>(value);
        if (m_settings.currentMeshErodeRadius == newRadius)
            return;
        m_settings.currentMeshErodeRadius = newRadius;
        emit settingsChanged(m_settings);
    });
    connect(m_currentMeshDebugViewCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (!m_currentMeshDebugViewCombo)
            return;
        const QVariant data = m_currentMeshDebugViewCombo->itemData(idx);
        if (!data.isValid())
            return;
        const CurrentMeshDebugView view = static_cast<CurrentMeshDebugView>(data.toInt());
        if (m_settings.currentMeshDebugView == view)
            return;
        m_settings.currentMeshDebugView = view;
        emit settingsChanged(m_settings);
    });
    connect(m_bboxColorButton, &QPushButton::clicked, this, [this]() {
        const QColor picked = QColorDialog::getColor(m_settings.bboxWireColor, this, tr("Bounding Box Wire Color"));
        if (!picked.isValid())
            return;
        m_settings.bboxWireColor = picked;
        updateBBoxColorButtonStyle();
        emit settingsChanged(m_settings);
    });
    connect(m_pointsColorButton, &QPushButton::clicked, this, [this]() {
        const QColor picked = QColorDialog::getColor(m_settings.pointColor, this, tr("Point Color"));
        if (!picked.isValid())
            return;
        m_settings.pointColor = picked;
        updatePointsColorButtonStyle();
        emit settingsChanged(m_settings);
    });
    connect(m_pointSizeSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const float newSize = static_cast<float>(value);
        if (m_settings.pointSize == newSize)
            return;
        m_settings.pointSize = newSize;
        emit settingsChanged(m_settings);
    });
    connect(m_pointColorSourceCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (!m_pointColorSourceCombo)
            return;
        const QVariant data = m_pointColorSourceCombo->itemData(idx);
        if (!data.isValid())
            return;
        const PointColorSource source = static_cast<PointColorSource>(data.toInt());
        if (m_settings.pointColorSource == source)
            return;
        m_settings.pointColorSource = source;
        emit settingsChanged(m_settings);
    });
    connect(m_pointLightingCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_settings.pointLighting == checked)
            return;
        m_settings.pointLighting = checked;
        emit settingsChanged(m_settings);
    });
    connect(m_wireColorButton, &QPushButton::clicked, this, [this]() {
        const QColor picked = QColorDialog::getColor(m_settings.wireColor, this, tr("Wire Color"));
        if (!picked.isValid())
            return;
        m_settings.wireColor = picked;
        updateWireColorButtonStyle();
        emit settingsChanged(m_settings);
    });
    connect(m_wireSizeSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const float newSize = static_cast<float>(value);
        if (m_settings.wireSize == newSize)
            return;
        m_settings.wireSize = newSize;
        emit settingsChanged(m_settings);
    });
    connect(m_wireBackfaceCullingCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_settings.wireBackfaceCulling == checked)
            return;
        m_settings.wireBackfaceCulling = checked;
        emit settingsChanged(m_settings);
    });
    connect(m_wireLightingCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_settings.wireLighting == checked)
            return;
        m_settings.wireLighting = checked;
        emit settingsChanged(m_settings);
    });
    connect(m_fillColorButton, &QPushButton::clicked, this, [this]() {
        const QColor picked = QColorDialog::getColor(m_settings.fillColor, this, tr("Fill Color"));
        if (!picked.isValid())
            return;
        m_settings.fillColor = picked;
        updateFillColorButtonStyle();
        emit settingsChanged(m_settings);
    });
    connect(m_fillColorSourceCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (!m_fillColorSourceCombo)
            return;
        const QVariant data = m_fillColorSourceCombo->itemData(idx);
        if (!data.isValid())
            return;
        const FillColorSource source = static_cast<FillColorSource>(data.toInt());
        if (m_settings.fillColorSource == source)
            return;
        m_settings.fillColorSource = source;
        emit settingsChanged(m_settings);
    });
    connect(m_fillShadingCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (!m_fillShadingCombo)
            return;
        const QVariant data = m_fillShadingCombo->itemData(idx);
        if (!data.isValid())
            return;
        const FillShading shading = static_cast<FillShading>(data.toInt());
        if (m_settings.fillShading == shading)
            return;
        m_settings.fillShading = shading;
        emit settingsChanged(m_settings);
    });
    connect(m_fillLightingCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_settings.fillLighting == checked)
            return;
        m_settings.fillLighting = checked;
        emit settingsChanged(m_settings);
    });

    connect(m_currentMeshButton, &QToolButton::clicked, this, [this]() {
        setCurrentRenderPass(RenderPass::CurrentMesh);
        setSettingsVisible(true);
    });
    connect(m_bboxButton, &QToolButton::clicked, this, [this]() {
        setCurrentRenderPass(RenderPass::BoundingBox);
    });
    connect(m_pointsButton, &QToolButton::clicked, this, [this]() {
        setCurrentRenderPass(RenderPass::Points);
    });
    connect(m_wireButton, &QToolButton::clicked, this, [this]() {
        setCurrentRenderPass(RenderPass::Wireframe);
    });
    connect(m_fillButton, &QToolButton::clicked, this, [this]() {
        setCurrentRenderPass(RenderPass::Fill);
    });

    connect(m_currentMeshSettingsArrow, &QToolButton::clicked, this, [this]() {
        setCurrentRenderPass(RenderPass::CurrentMesh);
        setSettingsVisible(true);
    });
    connect(m_bboxSettingsArrow, &QToolButton::clicked, this, [this]() {
        setCurrentRenderPass(RenderPass::BoundingBox);
        setSettingsVisible(true);
    });
    connect(m_pointsSettingsArrow, &QToolButton::clicked, this, [this]() {
        setCurrentRenderPass(RenderPass::Points);
        setSettingsVisible(true);
    });
    connect(m_wireSettingsArrow, &QToolButton::clicked, this, [this]() {
        setCurrentRenderPass(RenderPass::Wireframe);
        setSettingsVisible(true);
    });
    connect(m_fillSettingsArrow, &QToolButton::clicked, this, [this]() {
        setCurrentRenderPass(RenderPass::Fill);
        setSettingsVisible(true);
    });

    connect(m_bboxButton, &QToolButton::toggled, this, [this](bool checked) {
        if (m_settings.showBoundingBox == checked)
            return;
        m_settings.showBoundingBox = checked;
        emit settingsChanged(m_settings);
    });
    connect(m_pointsButton, &QToolButton::toggled, this, [this](bool checked) {
        if (m_settings.showPoints == checked)
            return;
        m_settings.showPoints = checked;
        emit settingsChanged(m_settings);
    });
    connect(m_wireButton, &QToolButton::toggled, this, [this](bool checked) {
        if (m_settings.showWire == checked)
            return;
        m_settings.showWire = checked;
        emit settingsChanged(m_settings);
    });
    connect(m_fillButton, &QToolButton::toggled, this, [this](bool checked) {
        if (m_settings.showFill == checked)
            return;
        m_settings.showFill = checked;
        emit settingsChanged(m_settings);
    });

    updateCurrentMeshOutlineColorButtonStyle();
    updateBBoxColorButtonStyle();
    updatePointsColorButtonStyle();
    updateWireColorButtonStyle();
    updateFillColorButtonStyle();
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
    case RenderPass::BoundingBox: return 1;
    case RenderPass::Points: return 2;
    case RenderPass::Wireframe: return 3;
    case RenderPass::Fill: return 4;
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
    if (m_settings == settings)
        return;

    m_settings = settings;

    if (m_currentMeshHighlightCheck) {
        QSignalBlocker blocker(m_currentMeshHighlightCheck);
        m_currentMeshHighlightCheck->setChecked(m_settings.highlightCurrentMesh);
    }
    if (m_bboxButton) {
        QSignalBlocker blocker(m_bboxButton);
        m_bboxButton->setChecked(m_settings.showBoundingBox);
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

    updateCurrentMeshOutlineColorButtonStyle();
    updateBBoxColorButtonStyle();
    updatePointsColorButtonStyle();
    updateWireColorButtonStyle();
    updateFillColorButtonStyle();
    syncRenderPassUiState();
}

void RenderOverlayPanel::setPointColorSourceAvailability(bool hasVertexColors)
{
    m_hasPointVertexColorSource = hasVertexColors;

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
    m_hasPointNormalSource = hasVertexNormals;
    if (!m_pointLightingCheck)
        return;
    m_pointLightingCheck->setEnabled(hasVertexNormals);
}

void RenderOverlayPanel::setFillColorSourceAvailability(bool hasVertexColors, bool hasFaceColors)
{
    m_hasVertexColorSource = hasVertexColors;
    m_hasFaceColorSource = hasFaceColors;

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

void RenderOverlayPanel::updateCurrentMeshOutlineColorButtonStyle()
{
    if (!m_currentMeshOutlineColorButton)
        return;
    m_currentMeshOutlineColorButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid rgba(40,40,40,160); border-radius: 3px; padding: 4px 8px; }")
            .arg(m_settings.currentMeshOutlineColor.name()));
}

void RenderOverlayPanel::updateBBoxColorButtonStyle()
{
    if (!m_bboxColorButton)
        return;
    m_bboxColorButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid rgba(40,40,40,160); border-radius: 3px; padding: 4px 8px; }")
            .arg(m_settings.bboxWireColor.name()));
}

void RenderOverlayPanel::updatePointsColorButtonStyle()
{
    if (!m_pointsColorButton)
        return;
    m_pointsColorButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid rgba(40,40,40,160); border-radius: 3px; padding: 4px 8px; }")
            .arg(m_settings.pointColor.name()));
}

void RenderOverlayPanel::updateWireColorButtonStyle()
{
    if (!m_wireColorButton)
        return;
    m_wireColorButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid rgba(40,40,40,160); border-radius: 3px; padding: 4px 8px; }")
            .arg(m_settings.wireColor.name()));
}

void RenderOverlayPanel::updateFillColorButtonStyle()
{
    if (!m_fillColorButton)
        return;
    m_fillColorButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid rgba(40,40,40,160); border-radius: 3px; padding: 4px 8px; }")
            .arg(m_settings.fillColor.name()));
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
    setPassMarker(m_bboxButton, RenderPass::BoundingBox);
    setPassMarker(m_pointsButton, RenderPass::Points);
    setPassMarker(m_wireButton, RenderPass::Wireframe);
    setPassMarker(m_fillButton, RenderPass::Fill);

    setArrowChecked(m_currentMeshSettingsArrow, RenderPass::CurrentMesh);
    setArrowChecked(m_bboxSettingsArrow, RenderPass::BoundingBox);
    setArrowChecked(m_pointsSettingsArrow, RenderPass::Points);
    setArrowChecked(m_wireSettingsArrow, RenderPass::Wireframe);
    setArrowChecked(m_fillSettingsArrow, RenderPass::Fill);
}
