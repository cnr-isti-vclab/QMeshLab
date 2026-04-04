#include "renderoverlaypanel.h"
#include <QColorDialog>
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

    m_modeButton = makeButton(QStringLiteral(":/img/options.png"), tr("Rendering Settings"));
    m_modeButton->setCheckable(true);
    m_modeButton->setChecked(false);
    m_bboxButton = makeButton(QStringLiteral(":/img/box.png"), tr("Bounding Box"));
    m_pointsButton = makeButton(QStringLiteral(":/img/points.png"), tr("Points"));
    m_wireButton = makeButton(QStringLiteral(":/img/wire.png"), tr("Wireframe pass"));
    m_fillButton = makeButton(QStringLiteral(":/img/flat.png"), tr("Fill pass"));

    buttonLayout->addWidget(m_modeButton);
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

    auto *modeArrowSpacer = new QWidget(arrowRow);
    modeArrowSpacer->setFixedSize(32, 12);
    arrowLayout->addWidget(modeArrowSpacer);

    auto makeArrowButton = [this, arrowRow](const QString &tooltip) {
        auto *btn = new PassArrowButton(arrowRow);
        btn->setToolTip(tooltip);
        return btn;
    };
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

    auto makePlaceholderPage = [this]() {
        auto *page = new QWidget(m_settingsStack);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addStretch(1);
        return page;
    };

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
    m_settingsStack->addWidget(makePlaceholderPage());
    m_settingsStack->addWidget(makePlaceholderPage());
    m_settingsStack->addWidget(makePlaceholderPage());
    panelLayout->addWidget(m_settingsContainer);

    connect(m_modeButton, &QToolButton::toggled, this, [this](bool checked) {
        if (m_settings.settingsPanelVisible == checked)
            return;
        m_settings.settingsPanelVisible = checked;
        if (m_settingsContainer)
            m_settingsContainer->setVisible(checked);
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

    updateBBoxColorButtonStyle();
    if (m_settingsStack)
        m_settingsStack->setCurrentIndex(renderPassPageIndex(m_settings.currentPass));
    if (m_settingsContainer)
        m_settingsContainer->setVisible(m_settings.settingsPanelVisible);
    syncRenderPassUiState();
}

int RenderOverlayPanel::renderPassPageIndex(RenderPass pass) const
{
    switch (pass) {
    case RenderPass::BoundingBox: return 0;
    case RenderPass::Points: return 1;
    case RenderPass::Wireframe: return 2;
    case RenderPass::Fill: return 3;
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
    if (m_settingsContainer)
        m_settingsContainer->setVisible(m_settings.settingsPanelVisible);
    if (m_settingsStack)
        m_settingsStack->setCurrentIndex(renderPassPageIndex(m_settings.currentPass));

    updateBBoxColorButtonStyle();
    syncRenderPassUiState();
}

void RenderOverlayPanel::updateBBoxColorButtonStyle()
{
    if (!m_bboxColorButton)
        return;
    m_bboxColorButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid rgba(40,40,40,160); border-radius: 3px; padding: 4px 8px; }")
            .arg(m_settings.bboxWireColor.name()));
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

    setPassMarker(m_bboxButton, RenderPass::BoundingBox);
    setPassMarker(m_pointsButton, RenderPass::Points);
    setPassMarker(m_wireButton, RenderPass::Wireframe);
    setPassMarker(m_fillButton, RenderPass::Fill);

    setArrowChecked(m_bboxSettingsArrow, RenderPass::BoundingBox);
    setArrowChecked(m_pointsSettingsArrow, RenderPass::Points);
    setArrowChecked(m_wireSettingsArrow, RenderPass::Wireframe);
    setArrowChecked(m_fillSettingsArrow, RenderPass::Fill);
}
