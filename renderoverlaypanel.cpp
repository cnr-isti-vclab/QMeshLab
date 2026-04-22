#include "renderoverlaypanel.h"
#include "colormap.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <type_traits>

namespace {
const QColor kAccentColor(36, 132, 210);
const QColor kNeutralArrowColor(90, 90, 90, 175);
const QColor kActiveArrowColor(36, 132, 210, 235);
constexpr int kSettingsRowHeight = 24;
constexpr int kColorButtonSize = 18;
constexpr Qt::Alignment kSettingsLabelAlignment = Qt::AlignRight | Qt::AlignVCenter;
constexpr int kPbrSourceRole = Qt::UserRole + 100;
constexpr int kPbrTextureIndexRole = Qt::UserRole + 101;

QWidget *makeCenteredFieldContainer(QWidget *fieldWidget, QWidget *parentWidget)
{
    auto *container = new QWidget(parentWidget);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    const QSize fieldHint = fieldWidget->sizeHint();
    if (auto *checkBox = qobject_cast<QCheckBox *>(fieldWidget)) {
        Q_UNUSED(checkBox);
        // Keep checkbox indicator fully visible even in narrow field columns.
        fieldWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        fieldWidget->setMinimumWidth(fieldHint.width() + 2);
        fieldWidget->setMaximumWidth(fieldHint.width() + 2);
    }

    layout->addWidget(fieldWidget, 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addStretch(1);
    container->setMinimumWidth(fieldHint.width() + 2);
    container->setMinimumHeight(kSettingsRowHeight);
    return container;
}

void applyUniformFormRowHeights(QFormLayout *form)
{
    if (!form)
        return;

    for (int row = 0; row < form->rowCount(); ++row) {
        if (QLayoutItem *labelItem = form->itemAt(row, QFormLayout::LabelRole)) {
            if (QWidget *labelWidget = labelItem->widget()) {
                if (auto *label = qobject_cast<QLabel *>(labelWidget))
                    label->setAlignment(kSettingsLabelAlignment);
                QSizePolicy labelPolicy = labelWidget->sizePolicy();
                labelPolicy.setVerticalPolicy(QSizePolicy::Fixed);
                labelWidget->setSizePolicy(labelPolicy);
                labelWidget->setMinimumHeight(kSettingsRowHeight);
            }
        }

        if (QLayoutItem *fieldItem = form->itemAt(row, QFormLayout::FieldRole)) {
            if (QWidget *fieldWidget = fieldItem->widget()) {
                if (fieldWidget->minimumHeight() != fieldWidget->maximumHeight())
                    fieldWidget->setMinimumHeight(kSettingsRowHeight);
            }
        }
    }
}

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

    m_currentMeshButton = makeButton(QStringLiteral(":/img/global.png"), tr("Viewer Settings"));
    m_currentMeshButton->setCheckable(false);
    m_modeButton = makeButton(QStringLiteral(":/img/options.png"), tr("Rendering Settings"));
    m_modeButton->setCheckable(true);
    m_modeButton->setChecked(false);
    m_normalsDecoratorsButton = makeButton(QStringLiteral(":/img/normals.png"), tr("Normal Decorators"));
    m_boundaryDecoratorsButton = makeButton(QStringLiteral(":/img/boundary.png"), tr("Boundary Decorators"));
    m_bboxButton = makeButton(QStringLiteral(":/img/box.png"), tr("Bounding Box"));
    m_pointsButton = makeButton(QStringLiteral(":/img/points.png"), tr("Points"));
    m_edgesButton = makeButton(QStringLiteral(":/img/edge-mesh.png"), tr("Edges pass"));
    m_wireButton = makeButton(QStringLiteral(":/img/wire.png"), tr("Wireframe pass"));
    m_fillButton = makeButton(QStringLiteral(":/img/flat.png"), tr("Fill pass"));
    m_selectionButton =
        makeButton(QStringLiteral(":/img/selected.png"), tr("Selected elements overlay"));
    m_qualityHistogramButton =
        makeButton(QStringLiteral(":/img/histogram.png"), tr("Quality Histogram"));

    buttonLayout->addWidget(m_modeButton);
    buttonLayout->addWidget(m_currentMeshButton);
    buttonLayout->addWidget(m_bboxButton);
    buttonLayout->addWidget(m_pointsButton);
    buttonLayout->addWidget(m_edgesButton);
    buttonLayout->addWidget(m_wireButton);
    buttonLayout->addWidget(m_fillButton);
    buttonLayout->addWidget(m_selectionButton);
    buttonLayout->addWidget(m_normalsDecoratorsButton);
    buttonLayout->addWidget(m_boundaryDecoratorsButton);
    buttonLayout->addWidget(m_qualityHistogramButton);

    m_normalsDecoratorsButton->setChecked(false);
    m_boundaryDecoratorsButton->setChecked(false);
    m_bboxButton->setChecked(false);
    m_pointsButton->setChecked(false);
    m_edgesButton->setChecked(false);
    m_wireButton->setChecked(true);
    m_fillButton->setChecked(true);
    m_selectionButton->setChecked(false);
    m_qualityHistogramButton->setChecked(false);

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
    auto makeColorButton = [this](QWidget *parentWidget) {
        auto *btn = new QPushButton(parentWidget);
        btn->setText(QString());
        btn->setToolTip(tr("Click to choose a new color"));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedSize(kColorButtonSize, kColorButtonSize);
        return btn;
    };
    auto *modeArrowSpacer = new QWidget(arrowRow);
    modeArrowSpacer->setFixedSize(32, 12);
    arrowLayout->addWidget(modeArrowSpacer);

    m_currentMeshSettingsArrow = makeArrowButton(tr("Settings: Viewer"));
    arrowLayout->addWidget(m_currentMeshSettingsArrow);

    m_bboxSettingsArrow = makeArrowButton(tr("Settings: Bounding Box"));
    m_pointsSettingsArrow = makeArrowButton(tr("Settings: Points"));
    m_edgesSettingsArrow = makeArrowButton(tr("Settings: Edges"));
    m_wireSettingsArrow = makeArrowButton(tr("Settings: Wireframe"));
    m_fillSettingsArrow = makeArrowButton(tr("Settings: Fill"));
    m_selectionSettingsArrow = makeArrowButton(tr("Settings: Selection"));
    m_qualityHistogramSettingsArrow = makeArrowButton(tr("Settings: Quality Histogram"));
    arrowLayout->addWidget(m_bboxSettingsArrow);
    arrowLayout->addWidget(m_pointsSettingsArrow);
    arrowLayout->addWidget(m_edgesSettingsArrow);
    arrowLayout->addWidget(m_wireSettingsArrow);
    arrowLayout->addWidget(m_fillSettingsArrow);
    arrowLayout->addWidget(m_selectionSettingsArrow);
    m_normalsDecoratorsSettingsArrow = makeArrowButton(tr("Settings: Normal Decorators"));
    arrowLayout->addWidget(m_normalsDecoratorsSettingsArrow);
    m_boundaryDecoratorsSettingsArrow = makeArrowButton(tr("Settings: Boundary Decorators"));
    arrowLayout->addWidget(m_boundaryDecoratorsSettingsArrow);
    arrowLayout->addWidget(m_qualityHistogramSettingsArrow);

    m_settingsContainer = new QFrame(this);
    m_settingsContainer->setVisible(false);
    m_settingsContainer->setObjectName(QStringLiteral("settingsContainer"));
    m_settingsContainer->setStyleSheet(QStringLiteral(
        "#settingsContainer { background: rgba(250,250,250,225); border: 1px solid rgba(40,40,40,160); border-radius: 4px; }"
        "#settingsContainer QLabel { border: none; background: transparent; }"));

    auto *settingsContainerLayout = new QVBoxLayout(m_settingsContainer);
    settingsContainerLayout->setContentsMargins(6, 6, 6, 6);
    settingsContainerLayout->setSpacing(4);
    m_settingsStack = new QStackedWidget(m_settingsContainer);
    settingsContainerLayout->addWidget(m_settingsStack);

    auto *currentMeshPage = new QWidget(m_settingsStack);
    auto *currentMeshLayout = new QVBoxLayout(currentMeshPage);
    currentMeshLayout->setContentsMargins(0, 0, 0, 0);
    currentMeshLayout->setSpacing(2);
    m_viewerSettingsStack = new QStackedWidget(currentMeshPage);
    currentMeshLayout->addWidget(m_viewerSettingsStack);

    auto *viewer3dPage = new QWidget(m_viewerSettingsStack);
    auto *viewer3dLayout = new QVBoxLayout(viewer3dPage);
    viewer3dLayout->setContentsMargins(0, 0, 0, 0);
    viewer3dLayout->setSpacing(2);
    auto *currentMeshForm = new QFormLayout();
    currentMeshForm->setContentsMargins(0, 0, 0, 0);
    currentMeshForm->setHorizontalSpacing(6);
    currentMeshForm->setVerticalSpacing(2);
    currentMeshForm->setLabelAlignment(kSettingsLabelAlignment);
    m_currentMeshHighlightCheck = new QCheckBox(viewer3dPage);
    m_currentMeshHighlightCheck->setChecked(m_settings.highlightCurrentMesh);
    m_showTrackballGizmoCheck = new QCheckBox(viewer3dPage);
    m_showTrackballGizmoCheck->setChecked(m_settings.showTrackballGizmo);
    m_currentMeshOutlineColorButton = makeColorButton(viewer3dPage);
    m_sceneBackgroundTopColorButton = makeColorButton(viewer3dPage);
    m_sceneBackgroundBottomColorButton = makeColorButton(viewer3dPage);
    m_currentMeshOutlineWidthSpin = new QDoubleSpinBox(viewer3dPage);
    m_currentMeshDilateRadiusSpin = new QDoubleSpinBox(viewer3dPage);
    m_currentMeshErodeRadiusSpin = new QDoubleSpinBox(viewer3dPage);
    m_currentMeshDebugViewCombo = new QComboBox(viewer3dPage);
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
        tr("Full Mask"),
        static_cast<int>(CurrentMeshDebugView::FullMask));
    m_currentMeshDebugViewCombo->addItem(
        tr("Visible Mask"),
        static_cast<int>(CurrentMeshDebugView::VisibleMask));
    m_currentMeshDebugViewCombo->addItem(
        tr("Occluded Mask"),
        static_cast<int>(CurrentMeshDebugView::OccludedMask));
    m_currentMeshDebugViewCombo->addItem(
        tr("Dilated"),
        static_cast<int>(CurrentMeshDebugView::DilatedMask));
    m_currentMeshDebugViewCombo->addItem(
        tr("Eroded"),
        static_cast<int>(CurrentMeshDebugView::ErodedMask));
    currentMeshForm->addRow(
        tr("Highlight"),
        makeCenteredFieldContainer(m_currentMeshHighlightCheck, viewer3dPage));
    currentMeshForm->addRow(
        tr("Trackball gizmo"),
        makeCenteredFieldContainer(m_showTrackballGizmoCheck, viewer3dPage));
    currentMeshForm->addRow(
        tr("Outline color"),
        makeCenteredFieldContainer(m_currentMeshOutlineColorButton, viewer3dPage));
    currentMeshForm->addRow(tr("Outline width"), m_currentMeshOutlineWidthSpin);
    currentMeshForm->addRow(tr("Dilate"), m_currentMeshDilateRadiusSpin);
    currentMeshForm->addRow(tr("Erode"), m_currentMeshErodeRadiusSpin);
    currentMeshForm->addRow(tr("Debug view"), m_currentMeshDebugViewCombo);
    currentMeshForm->addRow(
        tr("Bg top"),
        makeCenteredFieldContainer(m_sceneBackgroundTopColorButton, viewer3dPage));
    currentMeshForm->addRow(
        tr("Bg bottom"),
        makeCenteredFieldContainer(m_sceneBackgroundBottomColorButton, viewer3dPage));
    applyUniformFormRowHeights(currentMeshForm);
    viewer3dLayout->addLayout(currentMeshForm);
    m_viewerSettingsStack->addWidget(viewer3dPage);

    auto *viewerUvPage = new QWidget(m_viewerSettingsStack);
    auto *viewerUvLayout = new QVBoxLayout(viewerUvPage);
    viewerUvLayout->setContentsMargins(0, 0, 0, 0);
    viewerUvLayout->setSpacing(2);
    auto *viewerUvForm = new QFormLayout();
    viewerUvForm->setContentsMargins(0, 0, 0, 0);
    viewerUvForm->setHorizontalSpacing(6);
    viewerUvForm->setVerticalSpacing(2);
    viewerUvForm->setLabelAlignment(kSettingsLabelAlignment);
    m_uvShowReferenceFrameCheck = new QCheckBox(viewerUvPage);
    m_uvShowReferenceFrameCheck->setChecked(m_settings.uvShowReferenceFrame);
    m_uvShowFullTextureCheck = new QCheckBox(viewerUvPage);
    m_uvShowFullTextureCheck->setChecked(m_settings.uvShowFullTexture);
    viewerUvForm->addRow(
        tr("UV axis"),
        makeCenteredFieldContainer(m_uvShowReferenceFrameCheck, viewerUvPage));
    viewerUvForm->addRow(
        tr("Full texture"),
        makeCenteredFieldContainer(m_uvShowFullTextureCheck, viewerUvPage));
    applyUniformFormRowHeights(viewerUvForm);
    viewerUvLayout->addLayout(viewerUvForm);
    m_viewerSettingsStack->addWidget(viewerUvPage);

    m_settingsStack->addWidget(currentMeshPage);

    auto *normalDecoratorsPage = new QWidget(m_settingsStack);
    auto *normalDecoratorsLayout = new QVBoxLayout(normalDecoratorsPage);
    normalDecoratorsLayout->setContentsMargins(0, 0, 0, 0);
    normalDecoratorsLayout->setSpacing(2);
    auto *normalDecoratorsForm = new QFormLayout();
    normalDecoratorsForm->setContentsMargins(0, 0, 0, 0);
    normalDecoratorsForm->setHorizontalSpacing(6);
    normalDecoratorsForm->setVerticalSpacing(2);
    normalDecoratorsForm->setLabelAlignment(kSettingsLabelAlignment);
    m_decoratorVertexNormalsCheck = new QCheckBox(normalDecoratorsPage);
    m_decoratorFaceNormalsCheck = new QCheckBox(normalDecoratorsPage);
    m_decoratorBoundaryEdgesCheck = new QCheckBox(normalDecoratorsPage);
    m_decoratorTextureSeamsCheck = new QCheckBox(normalDecoratorsPage);
    m_decoratorVertexNormalsCheck->setChecked(m_settings.decoratorVertexNormals);
    m_decoratorFaceNormalsCheck->setChecked(m_settings.decoratorFaceNormals);
    m_decoratorBoundaryEdgesCheck->setChecked(m_settings.decoratorBoundaryEdges);
    m_decoratorTextureSeamsCheck->setChecked(m_settings.decoratorTextureSeams);
    m_decoratorVertexNormalColorButton = makeColorButton(normalDecoratorsPage);
    m_decoratorFaceNormalColorButton = makeColorButton(normalDecoratorsPage);
    m_decoratorBoundaryEdgeColorButton = makeColorButton(normalDecoratorsPage);
    m_decoratorTextureSeamColorButton = makeColorButton(normalDecoratorsPage);
    normalDecoratorsForm->addRow(
        tr("Vertex normals"),
        makeCenteredFieldContainer(m_decoratorVertexNormalsCheck, normalDecoratorsPage));
    normalDecoratorsForm->addRow(
        tr("Vertex normal color"),
        makeCenteredFieldContainer(m_decoratorVertexNormalColorButton, normalDecoratorsPage));
    normalDecoratorsForm->addRow(
        tr("Face normals"),
        makeCenteredFieldContainer(m_decoratorFaceNormalsCheck, normalDecoratorsPage));
    normalDecoratorsForm->addRow(
        tr("Face normal color"),
        makeCenteredFieldContainer(m_decoratorFaceNormalColorButton, normalDecoratorsPage));
    applyUniformFormRowHeights(normalDecoratorsForm);
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
    boundaryDecoratorsForm->setLabelAlignment(kSettingsLabelAlignment);
    m_decoratorBoundaryWidthSpin = new QDoubleSpinBox(boundaryDecoratorsPage);
    m_decoratorBoundaryWidthSpin->setRange(0.5, 64.0);
    m_decoratorBoundaryWidthSpin->setSingleStep(0.5);
    m_decoratorBoundaryWidthSpin->setDecimals(1);
    m_decoratorBoundaryWidthSpin->setSuffix(tr(" px"));
    m_decoratorBoundaryWidthSpin->setValue(m_settings.decoratorBoundaryWidth);
    boundaryDecoratorsForm->addRow(
        tr("Boundary edges"),
        makeCenteredFieldContainer(m_decoratorBoundaryEdgesCheck, boundaryDecoratorsPage));
    boundaryDecoratorsForm->addRow(
        tr("Boundary edge color"),
        makeCenteredFieldContainer(m_decoratorBoundaryEdgeColorButton, boundaryDecoratorsPage));
    boundaryDecoratorsForm->addRow(
        tr("Texture seams"),
        makeCenteredFieldContainer(m_decoratorTextureSeamsCheck, boundaryDecoratorsPage));
    boundaryDecoratorsForm->addRow(
        tr("Texture seam color"),
        makeCenteredFieldContainer(m_decoratorTextureSeamColorButton, boundaryDecoratorsPage));
    boundaryDecoratorsForm->addRow(tr("Line width"), m_decoratorBoundaryWidthSpin);
    applyUniformFormRowHeights(boundaryDecoratorsForm);
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
    bboxForm->setLabelAlignment(kSettingsLabelAlignment);
    m_bboxColorButton = makeColorButton(bboxPage);
    m_bboxShowCornersCheck = new QCheckBox(bboxPage);
    m_bboxShowCornersCheck->setChecked(m_settings.showBoundingBoxCorners);
    m_bboxShowDimensionsCheck = new QCheckBox(bboxPage);
    m_bboxShowDimensionsCheck->setChecked(m_settings.showBoundingBoxDimensions);
    bboxForm->addRow(
        tr("Wire color"),
        makeCenteredFieldContainer(m_bboxColorButton, bboxPage));
    bboxForm->addRow(
        tr("Show min/max corners"),
        makeCenteredFieldContainer(m_bboxShowCornersCheck, bboxPage));
    bboxForm->addRow(
        tr("Show X/Y/Z size"),
        makeCenteredFieldContainer(m_bboxShowDimensionsCheck, bboxPage));
    applyUniformFormRowHeights(bboxForm);
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
    pointsForm->setLabelAlignment(kSettingsLabelAlignment);
    m_pointsColorButton = makeColorButton(pointsPage);
    m_pointColorSourceCombo = new QComboBox(pointsPage);
    m_pointColorSourceCombo->addItem(tr("Constant"), static_cast<int>(PointColorSource::Constant));
    m_pointColorSourceCombo->addItem(tr("Per-Vertex"), static_cast<int>(PointColorSource::PerVertex));
    m_pointColorSourceCombo->addItem(
        tr("Per-Vertex Quality"),
        static_cast<int>(PointColorSource::PerVertexQuality));
    m_pointSizeSpin = new QDoubleSpinBox(pointsPage);
    m_pointSizeSpin->setRange(1.0, 32.0);
    m_pointSizeSpin->setSingleStep(0.5);
    m_pointSizeSpin->setDecimals(1);
    m_pointSizeSpin->setSuffix(tr(" px"));
    m_pointSizeSpin->setValue(m_settings.pointSize);
    m_pointLightingCheck = new QCheckBox(pointsPage);
    m_pointLightingCheck->setChecked(m_settings.pointLighting);
    pointsForm->addRow(tr("Color source"), m_pointColorSourceCombo);
    pointsForm->addRow(
        tr("Point color"),
        makeCenteredFieldContainer(m_pointsColorButton, pointsPage));
    pointsForm->addRow(tr("Point size"), m_pointSizeSpin);
    pointsForm->addRow(
        tr("Lighting"),
        makeCenteredFieldContainer(m_pointLightingCheck, pointsPage));
    applyUniformFormRowHeights(pointsForm);
    pointsLayout->addLayout(pointsForm);
    m_settingsStack->addWidget(pointsPage);
    auto *edgesPage = new QWidget(m_settingsStack);
    auto *edgesLayout = new QVBoxLayout(edgesPage);
    edgesLayout->setContentsMargins(0, 0, 0, 0);
    edgesLayout->setSpacing(2);
    auto *edgesForm = new QFormLayout();
    edgesForm->setContentsMargins(0, 0, 0, 0);
    edgesForm->setHorizontalSpacing(6);
    edgesForm->setVerticalSpacing(2);
    edgesForm->setLabelAlignment(kSettingsLabelAlignment);
    m_edgeColorButton = makeColorButton(edgesPage);
    m_edgeSizeSpin = new QDoubleSpinBox(edgesPage);
    m_edgeSizeSpin->setRange(1.0, 64.0);
    m_edgeSizeSpin->setSingleStep(0.5);
    m_edgeSizeSpin->setDecimals(1);
    m_edgeSizeSpin->setSuffix(tr(" px"));
    m_edgeSizeSpin->setValue(m_settings.edgeSize);
    edgesForm->addRow(
        tr("Edge color"),
        makeCenteredFieldContainer(m_edgeColorButton, edgesPage));
    edgesForm->addRow(tr("Edge width"), m_edgeSizeSpin);
    applyUniformFormRowHeights(edgesForm);
    edgesLayout->addLayout(edgesForm);
    m_settingsStack->addWidget(edgesPage);
    auto *wirePage = new QWidget(m_settingsStack);
    auto *wireLayout = new QVBoxLayout(wirePage);
    wireLayout->setContentsMargins(0, 0, 0, 0);
    wireLayout->setSpacing(2);
    auto *wireForm = new QFormLayout();
    wireForm->setContentsMargins(0, 0, 0, 0);
    wireForm->setHorizontalSpacing(6);
    wireForm->setVerticalSpacing(2);
    wireForm->setLabelAlignment(kSettingsLabelAlignment);
    m_wireColorButton = makeColorButton(wirePage);
    m_wireSizeSpin = new QDoubleSpinBox(wirePage);
    m_wireSizeSpin->setRange(0.5, 8.0);
    m_wireSizeSpin->setSingleStep(0.1);
    m_wireSizeSpin->setDecimals(1);
    m_wireSizeSpin->setSuffix(tr(" px"));
    m_wireSizeSpin->setValue(m_settings.wireSize);
    m_wireBackfaceCullingCheck = new QCheckBox(wirePage);
    m_wireBackfaceCullingCheck->setChecked(m_settings.wireBackfaceCulling);
    m_wireLightingCheck = new QCheckBox(wirePage);
    m_wireLightingCheck->setChecked(m_settings.wireLighting);
    wireForm->addRow(
        tr("Wire color"),
        makeCenteredFieldContainer(m_wireColorButton, wirePage));
    wireForm->addRow(tr("Wire width"), m_wireSizeSpin);
    wireForm->addRow(
        tr("Backface culling"),
        makeCenteredFieldContainer(m_wireBackfaceCullingCheck, wirePage));
    wireForm->addRow(
        tr("Lighting"),
        makeCenteredFieldContainer(m_wireLightingCheck, wirePage));
    applyUniformFormRowHeights(wireForm);
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
    fillForm->setLabelAlignment(kSettingsLabelAlignment);
    m_fillMaterialCombo = new QComboBox(fillPage);
    m_fillMaterialCombo->addItem(tr("Plain"), static_cast<int>(FillMaterial::Plain));
    m_fillMaterialCombo->addItem(tr("PBR"), static_cast<int>(FillMaterial::Pbr));
    fillForm->addRow(tr("Material"), m_fillMaterialCombo);
    m_fillColorButton = makeColorButton(fillPage);
    m_fillColorSourceCombo = new QComboBox(fillPage);
    m_fillColorSourceCombo->addItem(tr("Constant"), static_cast<int>(FillColorSource::Constant));
    m_fillColorSourceCombo->addItem(tr("Per-Vertex"), static_cast<int>(FillColorSource::PerVertex));
    m_fillColorSourceCombo->addItem(tr("Per-Face"), static_cast<int>(FillColorSource::PerFace));
    m_fillColorSourceCombo->addItem(
        tr("Per-Vertex Quality"),
        static_cast<int>(FillColorSource::PerVertexQuality));
    m_fillColorSourceCombo->addItem(
        tr("Per-Face Quality"),
        static_cast<int>(FillColorSource::PerFaceQuality));
    m_fillColorSourceCombo->addItem(tr("Texture"), static_cast<int>(FillColorSource::Texture));
    m_fillShadingCombo = new QComboBox(fillPage);
    m_fillShadingCombo->addItem(tr("Smooth"), static_cast<int>(FillShading::Smooth));
    m_fillShadingCombo->addItem(tr("Flat"), static_cast<int>(FillShading::Flat));
    m_fillBackfaceCullingCheck = new QCheckBox(fillPage);
    m_fillBackfaceCullingCheck->setChecked(m_settings.fillBackfaceCulling);
    m_fillLightingCheck = new QCheckBox(fillPage);
    m_fillLightingCheck->setChecked(m_settings.fillLighting);
    m_fillNormalScaleSpin = new QDoubleSpinBox(fillPage);
    m_fillNormalScaleSpin->setRange(0.0, 8.0);
    m_fillNormalScaleSpin->setSingleStep(0.05);
    m_fillNormalScaleSpin->setDecimals(2);
    m_fillNormalScaleSpin->setValue(m_settings.fillNormalMapScale);
    m_fillOcclusionStrengthSpin = new QDoubleSpinBox(fillPage);
    m_fillOcclusionStrengthSpin->setRange(0.0, 1.0);
    m_fillOcclusionStrengthSpin->setSingleStep(0.05);
    m_fillOcclusionStrengthSpin->setDecimals(2);
    m_fillOcclusionStrengthSpin->setValue(m_settings.fillOcclusionStrength);
    m_fillRoughnessFactorSpin = new QDoubleSpinBox(fillPage);
    m_fillRoughnessFactorSpin->setRange(0.0, 2.0);
    m_fillRoughnessFactorSpin->setSingleStep(0.05);
    m_fillRoughnessFactorSpin->setDecimals(2);
    m_fillRoughnessFactorSpin->setValue(m_settings.fillRoughnessFactor);

    m_fillMaterialStack = new QStackedWidget(fillPage);
    auto *fillPlainPage = new QWidget(m_fillMaterialStack);
    auto *fillPlainLayout = new QVBoxLayout(fillPlainPage);
    fillPlainLayout->setContentsMargins(0, 0, 0, 0);
    fillPlainLayout->setSpacing(2);
    auto *fillPlainForm = new QFormLayout();
    fillPlainForm->setContentsMargins(0, 0, 0, 0);
    fillPlainForm->setHorizontalSpacing(6);
    fillPlainForm->setVerticalSpacing(2);
    fillPlainForm->setLabelAlignment(kSettingsLabelAlignment);
    fillPlainForm->addRow(tr("Color source"), m_fillColorSourceCombo);
    fillPlainForm->addRow(
        tr("Fill color"),
        makeCenteredFieldContainer(m_fillColorButton, fillPage));
    fillPlainForm->addRow(tr("Shading"), m_fillShadingCombo);
    fillPlainForm->addRow(
        tr("Backface culling"),
        makeCenteredFieldContainer(m_fillBackfaceCullingCheck, fillPage));
    fillPlainForm->addRow(
        tr("Lighting"),
        makeCenteredFieldContainer(m_fillLightingCheck, fillPage));
    applyUniformFormRowHeights(fillPlainForm);
    fillPlainLayout->addLayout(fillPlainForm);
    m_fillMaterialStack->addWidget(fillPlainPage);

    auto *fillPbrPage = new QWidget(m_fillMaterialStack);
    auto *fillPbrLayout = new QVBoxLayout(fillPbrPage);
    fillPbrLayout->setContentsMargins(0, 0, 0, 0);
    fillPbrLayout->setSpacing(2);
    auto *fillPbrForm = new QFormLayout();
    fillPbrForm->setContentsMargins(0, 0, 0, 0);
    fillPbrForm->setHorizontalSpacing(6);
    fillPbrForm->setVerticalSpacing(2);
    fillPbrForm->setLabelAlignment(kSettingsLabelAlignment);
    m_fillPbrAlbedoCombo = new QComboBox(fillPage);
    m_fillPbrNormalCombo = new QComboBox(fillPage);
    m_fillPbrOcclusionCombo = new QComboBox(fillPage);
    m_fillPbrRoughnessCombo = new QComboBox(fillPage);
    rebuildFillPbrSourceCombos();
    m_fillPbrColorButton = makeColorButton(fillPage);
    fillPbrForm->addRow(tr("Albedo"), m_fillPbrAlbedoCombo);
    fillPbrForm->addRow(tr("Normal"), m_fillPbrNormalCombo);
    fillPbrForm->addRow(tr("AO Map"), m_fillPbrOcclusionCombo);
    fillPbrForm->addRow(tr("Roughness Map"), m_fillPbrRoughnessCombo);
    fillPbrForm->addRow(
        tr("Albedo color"),
        makeCenteredFieldContainer(m_fillPbrColorButton, fillPage));
    fillPbrForm->addRow(tr("Normal scale"), m_fillNormalScaleSpin);
    fillPbrForm->addRow(tr("AO strength"), m_fillOcclusionStrengthSpin);
    fillPbrForm->addRow(tr("Rough fac"), m_fillRoughnessFactorSpin);
    applyUniformFormRowHeights(fillPbrForm);
    fillPbrLayout->addLayout(fillPbrForm);
    m_fillMaterialStack->addWidget(fillPbrPage);

    if (m_fillMaterialStack)
        m_fillMaterialStack->setCurrentIndex(m_settings.fillMaterial == FillMaterial::Pbr ? 1 : 0);
    applyUniformFormRowHeights(fillForm);
    fillLayout->addLayout(fillForm);
    fillLayout->addWidget(m_fillMaterialStack);
    m_settingsStack->addWidget(fillPage);

    auto *selectionPage = new QWidget(m_settingsStack);
    auto *selectionLayout = new QVBoxLayout(selectionPage);
    selectionLayout->setContentsMargins(0, 0, 0, 0);
    selectionLayout->setSpacing(2);
    auto *selectionForm = new QFormLayout();
    selectionForm->setContentsMargins(0, 0, 0, 0);
    selectionForm->setHorizontalSpacing(6);
    selectionForm->setVerticalSpacing(2);
    selectionForm->setLabelAlignment(kSettingsLabelAlignment);
    m_selectionShowVerticesCheck = new QCheckBox(selectionPage);
    m_selectionShowVerticesCheck->setChecked(m_settings.showSelectionVertices);
    m_selectionShowFacesCheck = new QCheckBox(selectionPage);
    m_selectionShowFacesCheck->setChecked(m_settings.showSelectionFaces);
    selectionForm->addRow(
        tr("Vertices"),
        makeCenteredFieldContainer(m_selectionShowVerticesCheck, selectionPage));
    selectionForm->addRow(
        tr("Faces"),
        makeCenteredFieldContainer(m_selectionShowFacesCheck, selectionPage));
    applyUniformFormRowHeights(selectionForm);
    selectionLayout->addLayout(selectionForm);
    m_settingsStack->addWidget(selectionPage);

    auto *histogramPage = new QWidget(m_settingsStack);
    auto *histogramLayout = new QVBoxLayout(histogramPage);
    histogramLayout->setContentsMargins(0, 0, 0, 0);
    histogramLayout->setSpacing(2);
    auto *histogramForm = new QFormLayout();
    histogramForm->setContentsMargins(0, 0, 0, 0);
    histogramForm->setHorizontalSpacing(6);
    histogramForm->setVerticalSpacing(2);
    histogramForm->setLabelAlignment(kSettingsLabelAlignment);
    m_qualityHistogramSourceCombo = new QComboBox(histogramPage);
    m_qualityHistogramSourceCombo->addItem(
        tr("Auto"), static_cast<int>(QualityHistogramSource::Auto));
    m_qualityHistogramSourceCombo->addItem(
        tr("Vertex Q"), static_cast<int>(QualityHistogramSource::VertexQuality));
    m_qualityHistogramSourceCombo->addItem(
        tr("Face Q"), static_cast<int>(QualityHistogramSource::FaceQuality));
    histogramForm->addRow(tr("Source"), m_qualityHistogramSourceCombo);
    m_qualityHistogramColorMapCombo = new QComboBox(histogramPage);
    const ColorMapRegistry &colorMapRegistry = ColorMapRegistry::instance();
    const QStringList colorMapIds = colorMapRegistry.mapIds();
    for (const QString &id : colorMapIds)
        m_qualityHistogramColorMapCombo->addItem(colorMapRegistry.displayName(id), id);
    if (m_qualityHistogramColorMapCombo->count() == 0)
        m_qualityHistogramColorMapCombo->addItem(tr("Rainbow"), QStringLiteral("rainbow"));
    int initialColorMapIndex = m_qualityHistogramColorMapCombo->findData(
        m_settings.qualityHistogramColorMapId.trimmed().toLower());
    if (initialColorMapIndex < 0)
        initialColorMapIndex = m_qualityHistogramColorMapCombo->findData(
            colorMapRegistry.fallbackMapId());
    if (initialColorMapIndex < 0 && m_qualityHistogramColorMapCombo->count() > 0)
        initialColorMapIndex = 0;
    if (initialColorMapIndex >= 0) {
        m_qualityHistogramColorMapCombo->setCurrentIndex(initialColorMapIndex);
        m_settings.qualityHistogramColorMapId =
            m_qualityHistogramColorMapCombo->itemData(initialColorMapIndex).toString();
    }
    histogramForm->addRow(tr("Color map"), m_qualityHistogramColorMapCombo);
    m_qualityHistogramInvertCheck = new QCheckBox(histogramPage);
    m_qualityHistogramInvertCheck->setChecked(m_settings.qualityHistogramInvertColorMap);
    histogramForm->addRow(
        tr("Invert"),
        makeCenteredFieldContainer(m_qualityHistogramInvertCheck, histogramPage));
    m_qualityHistogramFixedRangeCheck = new QCheckBox(histogramPage);
    m_qualityHistogramFixedRangeCheck->setChecked(m_settings.qualityHistogramFixedRange);
    histogramForm->addRow(
        tr("Fixed range"),
        makeCenteredFieldContainer(m_qualityHistogramFixedRangeCheck, histogramPage));
    m_qualityHistogramMinSpin = new QDoubleSpinBox(histogramPage);
    m_qualityHistogramMinSpin->setRange(-1e12, 1e12);
    m_qualityHistogramMinSpin->setDecimals(6);
    m_qualityHistogramMinSpin->setSingleStep(0.1);
    m_qualityHistogramMinSpin->setValue(m_settings.qualityHistogramMin);
    histogramForm->addRow(tr("Min"), m_qualityHistogramMinSpin);
    m_qualityHistogramMaxSpin = new QDoubleSpinBox(histogramPage);
    m_qualityHistogramMaxSpin->setRange(-1e12, 1e12);
    m_qualityHistogramMaxSpin->setDecimals(6);
    m_qualityHistogramMaxSpin->setSingleStep(0.1);
    m_qualityHistogramMaxSpin->setValue(m_settings.qualityHistogramMax);
    histogramForm->addRow(tr("Max"), m_qualityHistogramMaxSpin);
    m_qualityHistogramMinSpin->setEnabled(m_settings.qualityHistogramFixedRange);
    m_qualityHistogramMaxSpin->setEnabled(m_settings.qualityHistogramFixedRange);
    m_qualityHistogramBinsSpin = new QDoubleSpinBox(histogramPage);
    m_qualityHistogramBinsSpin->setRange(4.0, 512.0);
    m_qualityHistogramBinsSpin->setSingleStep(1.0);
    m_qualityHistogramBinsSpin->setDecimals(0);
    m_qualityHistogramBinsSpin->setValue(m_settings.qualityHistogramBins);
    histogramForm->addRow(tr("Bins"), m_qualityHistogramBinsSpin);
    applyUniformFormRowHeights(histogramForm);
    histogramLayout->addLayout(histogramForm);
    m_settingsStack->addWidget(histogramPage);

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
    bindCheckBox(m_showTrackballGizmoCheck, &RenderSettings::showTrackballGizmo);
    bindColorButton(
        m_currentMeshOutlineColorButton,
        &RenderSettings::currentMeshOutlineColor,
        tr("Current Mesh Outline Color"));
    bindColorButton(
        m_sceneBackgroundTopColorButton,
        &RenderSettings::sceneBackgroundTopColor,
        tr("Scene Background Top Color"));
    bindColorButton(
        m_sceneBackgroundBottomColorButton,
        &RenderSettings::sceneBackgroundBottomColor,
        tr("Scene Background Bottom Color"));
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
    bindFloatSpin(m_decoratorBoundaryWidthSpin, &RenderSettings::decoratorBoundaryWidth);

    bindColorButton(m_bboxColorButton, &RenderSettings::bboxWireColor, tr("Bounding Box Wire Color"));
    bindCheckBox(m_bboxShowCornersCheck, &RenderSettings::showBoundingBoxCorners);
    bindCheckBox(m_bboxShowDimensionsCheck, &RenderSettings::showBoundingBoxDimensions);

    bindEnumCombo(m_pointColorSourceCombo, &RenderSettings::pointColorSource);
    bindColorButton(m_pointsColorButton, &RenderSettings::pointColor, tr("Point Color"));
    bindFloatSpin(m_pointSizeSpin, &RenderSettings::pointSize);
    bindCheckBox(m_pointLightingCheck, &RenderSettings::pointLighting);

    bindColorButton(m_edgeColorButton, &RenderSettings::edgeColor, tr("Edge Color"));
    bindFloatSpin(m_edgeSizeSpin, &RenderSettings::edgeSize);

    bindColorButton(m_wireColorButton, &RenderSettings::wireColor, tr("Wire Color"));
    bindFloatSpin(m_wireSizeSpin, &RenderSettings::wireSize);
    bindCheckBox(m_wireBackfaceCullingCheck, &RenderSettings::wireBackfaceCulling);
    bindCheckBox(m_wireLightingCheck, &RenderSettings::wireLighting);

    bindEnumCombo(m_fillMaterialCombo, &RenderSettings::fillMaterial);
    auto bindPbrSourceCombo =
        [this](QComboBox *combo,
               FillPbrTextureSource RenderSettings::*sourceMember,
               int RenderSettings::*indexMember) {
            connect(
                combo,
                qOverload<int>(&QComboBox::currentIndexChanged),
                this,
                [this, combo, sourceMember, indexMember](int idx) {
                    if (!combo || idx < 0 || idx >= combo->count())
                        return;
                    const QVariant sourceData = combo->itemData(idx, kPbrSourceRole);
                    const QVariant textureData = combo->itemData(idx, kPbrTextureIndexRole);
                    if (!sourceData.isValid())
                        return;

                    bool changed = false;
                    const FillPbrTextureSource source =
                        static_cast<FillPbrTextureSource>(sourceData.toInt());
                    const int textureIndex = textureData.isValid() ? textureData.toInt() : -1;
                    if (m_settings.*sourceMember != source) {
                        m_settings.*sourceMember = source;
                        changed = true;
                    }
                    if (m_settings.*indexMember != textureIndex) {
                        m_settings.*indexMember = textureIndex;
                        changed = true;
                    }
                    if (!changed)
                        return;
                    syncFillPbrUiState();
                    emit settingsChanged(m_settings);
                });
        };
    bindPbrSourceCombo(
        m_fillPbrAlbedoCombo,
        &RenderSettings::fillPbrAlbedoSource,
        &RenderSettings::fillPbrAlbedoTextureIndex);
    bindPbrSourceCombo(
        m_fillPbrNormalCombo,
        &RenderSettings::fillPbrNormalSource,
        &RenderSettings::fillPbrNormalTextureIndex);
    bindPbrSourceCombo(
        m_fillPbrOcclusionCombo,
        &RenderSettings::fillPbrOcclusionSource,
        &RenderSettings::fillPbrOcclusionTextureIndex);
    bindPbrSourceCombo(
        m_fillPbrRoughnessCombo,
        &RenderSettings::fillPbrRoughnessSource,
        &RenderSettings::fillPbrRoughnessTextureIndex);
    bindEnumCombo(m_fillColorSourceCombo, &RenderSettings::fillColorSource);
    bindColorButton(m_fillColorButton, &RenderSettings::fillColor, tr("Fill Color"));
    bindColorButton(m_fillPbrColorButton, &RenderSettings::fillColor, tr("Fill Color"));
    bindEnumCombo(m_fillShadingCombo, &RenderSettings::fillShading);
    bindCheckBox(m_fillBackfaceCullingCheck, &RenderSettings::fillBackfaceCulling);
    bindCheckBox(m_fillLightingCheck, &RenderSettings::fillLighting);
    bindFloatSpin(m_fillNormalScaleSpin, &RenderSettings::fillNormalMapScale);
    bindFloatSpin(m_fillOcclusionStrengthSpin, &RenderSettings::fillOcclusionStrength);
    bindFloatSpin(m_fillRoughnessFactorSpin, &RenderSettings::fillRoughnessFactor);
    connect(
        m_fillMaterialCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [this](int) { syncFillPbrUiState(); });
    connect(
        m_fillPbrAlbedoCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [this](int) { syncFillPbrUiState(); });
    connect(
        m_fillPbrNormalCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [this](int) { syncFillPbrUiState(); });
    connect(
        m_fillPbrOcclusionCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [this](int) { syncFillPbrUiState(); });
    connect(
        m_fillPbrRoughnessCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [this](int) { syncFillPbrUiState(); });
    connect(
        m_fillColorSourceCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [this](int) { syncFillPbrUiState(); });
    bindCheckBox(m_selectionShowVerticesCheck, &RenderSettings::showSelectionVertices);
    bindCheckBox(m_selectionShowFacesCheck, &RenderSettings::showSelectionFaces);
    bindCheckBox(m_uvShowReferenceFrameCheck, &RenderSettings::uvShowReferenceFrame);
    bindCheckBox(m_uvShowFullTextureCheck, &RenderSettings::uvShowFullTexture);
    bindEnumCombo(m_qualityHistogramSourceCombo, &RenderSettings::qualityHistogramSource);
    connect(
        m_qualityHistogramColorMapCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [this](int idx) {
            const QString colorMapId =
                m_qualityHistogramColorMapCombo->itemData(idx).toString().trimmed().toLower();
            if (colorMapId.isEmpty())
                return;
            if (m_settings.qualityHistogramColorMapId == colorMapId)
                return;
            m_settings.qualityHistogramColorMapId = colorMapId;
            emit settingsChanged(m_settings);
        });
    connect(
        m_qualityHistogramInvertCheck,
        &QCheckBox::toggled,
        this,
        [this](bool checked) {
            if (m_settings.qualityHistogramInvertColorMap == checked)
                return;
            m_settings.qualityHistogramInvertColorMap = checked;
            emit settingsChanged(m_settings);
        });
    connect(
        m_qualityHistogramFixedRangeCheck,
        &QCheckBox::toggled,
        this,
        [this](bool checked) {
            if (m_settings.qualityHistogramFixedRange == checked)
                return;
            m_settings.qualityHistogramFixedRange = checked;
            if (m_qualityHistogramMinSpin)
                m_qualityHistogramMinSpin->setEnabled(checked);
            if (m_qualityHistogramMaxSpin)
                m_qualityHistogramMaxSpin->setEnabled(checked);
            emit settingsChanged(m_settings);
        });
    connect(
        m_qualityHistogramMinSpin,
        &QDoubleSpinBox::valueChanged,
        this,
        [this](double value) {
            const float v = static_cast<float>(value);
            if (m_settings.qualityHistogramMin == v)
                return;
            m_settings.qualityHistogramMin = v;
            emit settingsChanged(m_settings);
        });
    connect(
        m_qualityHistogramMaxSpin,
        &QDoubleSpinBox::valueChanged,
        this,
        [this](double value) {
            const float v = static_cast<float>(value);
            if (m_settings.qualityHistogramMax == v)
                return;
            m_settings.qualityHistogramMax = v;
            emit settingsChanged(m_settings);
        });
    connect(
        m_qualityHistogramBinsSpin,
        &QDoubleSpinBox::valueChanged,
        this,
        [this](double value) {
            const int bins = std::clamp(int(std::lround(value)), 4, 512);
            if (m_settings.qualityHistogramBins == bins)
                return;
            m_settings.qualityHistogramBins = bins;
            emit settingsChanged(m_settings);
        });

    bindPassButton(m_currentMeshButton, RenderPass::CurrentMesh, true);
    bindPassButton(m_normalsDecoratorsButton, RenderPass::DecoratorNormals, false);
    bindPassButton(m_boundaryDecoratorsButton, RenderPass::DecoratorBoundary, false);
    bindPassButton(m_bboxButton, RenderPass::BoundingBox, false);
    bindPassButton(m_pointsButton, RenderPass::Points, false);
    bindPassButton(m_edgesButton, RenderPass::Edges, false);
    bindPassButton(m_wireButton, RenderPass::Wireframe, false);
    bindPassButton(m_fillButton, RenderPass::Fill, false);
    bindPassButton(m_selectionButton, RenderPass::Selection, false);
    bindPassButton(m_qualityHistogramButton, RenderPass::QualityHistogram, false);

    bindPassButton(m_currentMeshSettingsArrow, RenderPass::CurrentMesh, true);
    bindPassButton(m_normalsDecoratorsSettingsArrow, RenderPass::DecoratorNormals, true);
    bindPassButton(m_boundaryDecoratorsSettingsArrow, RenderPass::DecoratorBoundary, true);
    bindPassButton(m_bboxSettingsArrow, RenderPass::BoundingBox, true);
    bindPassButton(m_pointsSettingsArrow, RenderPass::Points, true);
    bindPassButton(m_edgesSettingsArrow, RenderPass::Edges, true);
    bindPassButton(m_wireSettingsArrow, RenderPass::Wireframe, true);
    bindPassButton(m_fillSettingsArrow, RenderPass::Fill, true);
    bindPassButton(m_selectionSettingsArrow, RenderPass::Selection, true);
    bindPassButton(m_qualityHistogramSettingsArrow, RenderPass::QualityHistogram, true);

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
    bindToolToggle(m_edgesButton, &RenderSettings::showEdges);
    bindToolToggle(m_wireButton, &RenderSettings::showWire);
    bindToolToggle(m_fillButton, &RenderSettings::showFill);
    bindToolToggle(m_selectionButton, &RenderSettings::showSelection);
    bindToolToggle(m_qualityHistogramButton, &RenderSettings::showQualityHistogram);

    updateColorButtonStyle(m_currentMeshOutlineColorButton, m_settings.currentMeshOutlineColor);
    updateColorButtonStyle(m_sceneBackgroundTopColorButton, m_settings.sceneBackgroundTopColor);
    updateColorButtonStyle(
        m_sceneBackgroundBottomColorButton,
        m_settings.sceneBackgroundBottomColor);
    updateColorButtonStyle(m_decoratorVertexNormalColorButton, m_settings.decoratorVertexNormalColor);
    updateColorButtonStyle(m_decoratorFaceNormalColorButton, m_settings.decoratorFaceNormalColor);
    updateColorButtonStyle(m_decoratorBoundaryEdgeColorButton, m_settings.decoratorBoundaryEdgeColor);
    updateColorButtonStyle(m_decoratorTextureSeamColorButton, m_settings.decoratorTextureSeamColor);
    updateColorButtonStyle(m_bboxColorButton, m_settings.bboxWireColor);
    updateColorButtonStyle(m_pointsColorButton, m_settings.pointColor);
    updateColorButtonStyle(m_edgeColorButton, m_settings.edgeColor);
    updateColorButtonStyle(m_wireColorButton, m_settings.wireColor);
    updateColorButtonStyle(m_fillColorButton, m_settings.fillColor);
    updateColorButtonStyle(m_fillPbrColorButton, m_settings.fillColor);
    setPointColorSourceAvailability(false, false);
    setPointLightingAvailability(false);
    setFillColorSourceAvailability(false, false, false, false, false);
    setFillPbrMapAvailability(false, false, false);
    if (m_settingsStack)
        m_settingsStack->setCurrentIndex(renderPassPageIndex(m_settings.currentPass));
    syncViewerSettingsModeUi();
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
    case RenderPass::Edges: return 5;
    case RenderPass::Wireframe: return 6;
    case RenderPass::Fill: return 7;
    case RenderPass::Selection: return 8;
    case RenderPass::QualityHistogram: return 9;
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

void RenderOverlayPanel::setViewerModeUv(bool uvMode)
{
    if (m_viewerModeUv == uvMode)
        return;
    m_viewerModeUv = uvMode;
    syncViewerSettingsModeUi();
}

void RenderOverlayPanel::syncViewerSettingsModeUi()
{
    if (!m_viewerSettingsStack)
        return;
    m_viewerSettingsStack->setCurrentIndex(m_viewerModeUv ? 1 : 0);
}

void RenderOverlayPanel::setSettings(const RenderSettings &settings)
{
    m_settings = settings;

    if (m_currentMeshHighlightCheck) {
        QSignalBlocker blocker(m_currentMeshHighlightCheck);
        m_currentMeshHighlightCheck->setChecked(m_settings.highlightCurrentMesh);
    }
    if (m_showTrackballGizmoCheck) {
        QSignalBlocker blocker(m_showTrackballGizmoCheck);
        m_showTrackballGizmoCheck->setChecked(m_settings.showTrackballGizmo);
    }
    if (m_uvShowReferenceFrameCheck) {
        QSignalBlocker blocker(m_uvShowReferenceFrameCheck);
        m_uvShowReferenceFrameCheck->setChecked(m_settings.uvShowReferenceFrame);
    }
    if (m_uvShowFullTextureCheck) {
        QSignalBlocker blocker(m_uvShowFullTextureCheck);
        m_uvShowFullTextureCheck->setChecked(m_settings.uvShowFullTexture);
    }
    if (m_bboxButton) {
        QSignalBlocker blocker(m_bboxButton);
        m_bboxButton->setChecked(m_settings.showBoundingBox);
    }
    if (m_bboxShowCornersCheck) {
        QSignalBlocker blocker(m_bboxShowCornersCheck);
        m_bboxShowCornersCheck->setChecked(m_settings.showBoundingBoxCorners);
    }
    if (m_bboxShowDimensionsCheck) {
        QSignalBlocker blocker(m_bboxShowDimensionsCheck);
        m_bboxShowDimensionsCheck->setChecked(m_settings.showBoundingBoxDimensions);
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
    if (m_edgesButton) {
        QSignalBlocker blocker(m_edgesButton);
        m_edgesButton->setChecked(m_settings.showEdges);
    }
    if (m_wireButton) {
        QSignalBlocker blocker(m_wireButton);
        m_wireButton->setChecked(m_settings.showWire);
    }
    if (m_fillButton) {
        QSignalBlocker blocker(m_fillButton);
        m_fillButton->setChecked(m_settings.showFill);
    }
    if (m_selectionButton) {
        QSignalBlocker blocker(m_selectionButton);
        m_selectionButton->setChecked(m_settings.showSelection);
    }
    if (m_selectionShowVerticesCheck) {
        QSignalBlocker blocker(m_selectionShowVerticesCheck);
        m_selectionShowVerticesCheck->setChecked(m_settings.showSelectionVertices);
    }
    if (m_selectionShowFacesCheck) {
        QSignalBlocker blocker(m_selectionShowFacesCheck);
        m_selectionShowFacesCheck->setChecked(m_settings.showSelectionFaces);
    }
    if (m_qualityHistogramButton) {
        QSignalBlocker blocker(m_qualityHistogramButton);
        m_qualityHistogramButton->setChecked(m_settings.showQualityHistogram);
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
    if (m_fillNormalScaleSpin) {
        QSignalBlocker blocker(m_fillNormalScaleSpin);
        m_fillNormalScaleSpin->setValue(m_settings.fillNormalMapScale);
    }
    if (m_fillOcclusionStrengthSpin) {
        QSignalBlocker blocker(m_fillOcclusionStrengthSpin);
        m_fillOcclusionStrengthSpin->setValue(m_settings.fillOcclusionStrength);
    }
    if (m_fillRoughnessFactorSpin) {
        QSignalBlocker blocker(m_fillRoughnessFactorSpin);
        m_fillRoughnessFactorSpin->setValue(m_settings.fillRoughnessFactor);
    }
    if (m_pointSizeSpin) {
        QSignalBlocker blocker(m_pointSizeSpin);
        m_pointSizeSpin->setValue(m_settings.pointSize);
    }
    if (m_wireSizeSpin) {
        QSignalBlocker blocker(m_wireSizeSpin);
        m_wireSizeSpin->setValue(m_settings.wireSize);
    }
    if (m_edgeSizeSpin) {
        QSignalBlocker blocker(m_edgeSizeSpin);
        m_edgeSizeSpin->setValue(m_settings.edgeSize);
    }
    if (m_decoratorBoundaryWidthSpin) {
        QSignalBlocker blocker(m_decoratorBoundaryWidthSpin);
        m_decoratorBoundaryWidthSpin->setValue(m_settings.decoratorBoundaryWidth);
    }
    if (m_qualityHistogramBinsSpin) {
        QSignalBlocker blocker(m_qualityHistogramBinsSpin);
        m_qualityHistogramBinsSpin->setValue(m_settings.qualityHistogramBins);
    }
    if (m_qualityHistogramFixedRangeCheck) {
        QSignalBlocker blocker(m_qualityHistogramFixedRangeCheck);
        m_qualityHistogramFixedRangeCheck->setChecked(m_settings.qualityHistogramFixedRange);
    }
    if (m_qualityHistogramMinSpin) {
        QSignalBlocker blocker(m_qualityHistogramMinSpin);
        m_qualityHistogramMinSpin->setValue(m_settings.qualityHistogramMin);
        m_qualityHistogramMinSpin->setEnabled(m_settings.qualityHistogramFixedRange);
    }
    if (m_qualityHistogramMaxSpin) {
        QSignalBlocker blocker(m_qualityHistogramMaxSpin);
        m_qualityHistogramMaxSpin->setValue(m_settings.qualityHistogramMax);
        m_qualityHistogramMaxSpin->setEnabled(m_settings.qualityHistogramFixedRange);
    }
    if (m_qualityHistogramSourceCombo) {
        QSignalBlocker blocker(m_qualityHistogramSourceCombo);
        const int value = static_cast<int>(m_settings.qualityHistogramSource);
        for (int i = 0; i < m_qualityHistogramSourceCombo->count(); ++i) {
            if (m_qualityHistogramSourceCombo->itemData(i).toInt() == value) {
                m_qualityHistogramSourceCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    if (m_qualityHistogramColorMapCombo) {
        QSignalBlocker blocker(m_qualityHistogramColorMapCombo);
        QString value = m_settings.qualityHistogramColorMapId.trimmed().toLower();
        int idx = m_qualityHistogramColorMapCombo->findData(value);
        if (idx < 0) {
            idx = m_qualityHistogramColorMapCombo->findData(
                ColorMapRegistry::instance().fallbackMapId());
        }
        if (idx < 0 && m_qualityHistogramColorMapCombo->count() > 0)
            idx = 0;
        if (idx >= 0) {
            m_qualityHistogramColorMapCombo->setCurrentIndex(idx);
            m_settings.qualityHistogramColorMapId =
                m_qualityHistogramColorMapCombo->itemData(idx).toString();
        }
    }
    if (m_qualityHistogramInvertCheck) {
        QSignalBlocker blocker(m_qualityHistogramInvertCheck);
        m_qualityHistogramInvertCheck->setChecked(m_settings.qualityHistogramInvertColorMap);
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
    if (m_fillMaterialCombo) {
        QSignalBlocker blocker(m_fillMaterialCombo);
        const int value = static_cast<int>(m_settings.fillMaterial);
        for (int i = 0; i < m_fillMaterialCombo->count(); ++i) {
            if (m_fillMaterialCombo->itemData(i).toInt() == value) {
                m_fillMaterialCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    syncFillPbrSourceCombo(
        m_fillPbrAlbedoCombo,
        m_settings.fillPbrAlbedoSource,
        m_settings.fillPbrAlbedoTextureIndex);
    syncFillPbrSourceCombo(
        m_fillPbrNormalCombo,
        m_settings.fillPbrNormalSource,
        m_settings.fillPbrNormalTextureIndex);
    syncFillPbrSourceCombo(
        m_fillPbrOcclusionCombo,
        m_settings.fillPbrOcclusionSource,
        m_settings.fillPbrOcclusionTextureIndex);
    syncFillPbrSourceCombo(
        m_fillPbrRoughnessCombo,
        m_settings.fillPbrRoughnessSource,
        m_settings.fillPbrRoughnessTextureIndex);
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
    syncViewerSettingsModeUi();
    syncFillPbrUiState();

    updateColorButtonStyle(m_currentMeshOutlineColorButton, m_settings.currentMeshOutlineColor);
    updateColorButtonStyle(m_sceneBackgroundTopColorButton, m_settings.sceneBackgroundTopColor);
    updateColorButtonStyle(
        m_sceneBackgroundBottomColorButton,
        m_settings.sceneBackgroundBottomColor);
    updateColorButtonStyle(m_decoratorVertexNormalColorButton, m_settings.decoratorVertexNormalColor);
    updateColorButtonStyle(m_decoratorFaceNormalColorButton, m_settings.decoratorFaceNormalColor);
    updateColorButtonStyle(m_decoratorBoundaryEdgeColorButton, m_settings.decoratorBoundaryEdgeColor);
    updateColorButtonStyle(m_decoratorTextureSeamColorButton, m_settings.decoratorTextureSeamColor);
    updateColorButtonStyle(m_bboxColorButton, m_settings.bboxWireColor);
    updateColorButtonStyle(m_pointsColorButton, m_settings.pointColor);
    updateColorButtonStyle(m_edgeColorButton, m_settings.edgeColor);
    updateColorButtonStyle(m_wireColorButton, m_settings.wireColor);
    updateColorButtonStyle(m_fillColorButton, m_settings.fillColor);
    updateColorButtonStyle(m_fillPbrColorButton, m_settings.fillColor);
    syncRenderPassUiState();
}

void RenderOverlayPanel::setPointColorSourceAvailability(
    bool hasVertexColors, bool hasVertexQuality)
{
    if (!m_pointColorSourceCombo)
        return;

    const int vertexIndex =
        m_pointColorSourceCombo->findData(static_cast<int>(PointColorSource::PerVertex));
    const int qualityIndex =
        m_pointColorSourceCombo->findData(static_cast<int>(PointColorSource::PerVertexQuality));
    if (vertexIndex >= 0) {
        m_pointColorSourceCombo->setItemData(
            vertexIndex,
            hasVertexColors ? QVariant() : QVariant(0),
            Qt::UserRole - 1);
    }
    if (qualityIndex >= 0) {
        m_pointColorSourceCombo->setItemData(
            qualityIndex,
            hasVertexQuality ? QVariant() : QVariant(0),
            Qt::UserRole - 1);
    }
}

void RenderOverlayPanel::setPointLightingAvailability(bool hasVertexNormals)
{
    if (!m_pointLightingCheck)
        return;
    m_pointLightingCheck->setEnabled(hasVertexNormals);
}

void RenderOverlayPanel::setFillColorSourceAvailability(
    bool hasVertexColors,
    bool hasFaceColors,
    bool hasVertexQuality,
    bool hasFaceQuality,
    bool hasTextures)
{
    if (!m_fillColorSourceCombo)
        return;
    m_fillHasTextures = hasTextures;

    const int vertexIndex =
        m_fillColorSourceCombo->findData(static_cast<int>(FillColorSource::PerVertex));
    const int faceIndex =
        m_fillColorSourceCombo->findData(static_cast<int>(FillColorSource::PerFace));
    const int vertexQualityIndex =
        m_fillColorSourceCombo->findData(static_cast<int>(FillColorSource::PerVertexQuality));
    const int faceQualityIndex =
        m_fillColorSourceCombo->findData(static_cast<int>(FillColorSource::PerFaceQuality));
    const int textureIndex =
        m_fillColorSourceCombo->findData(static_cast<int>(FillColorSource::Texture));

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
    if (vertexQualityIndex >= 0) {
        m_fillColorSourceCombo->setItemData(
            vertexQualityIndex,
            hasVertexQuality ? QVariant() : QVariant(0),
            Qt::UserRole - 1);
    }
    if (faceQualityIndex >= 0) {
        m_fillColorSourceCombo->setItemData(
            faceQualityIndex,
            hasFaceQuality ? QVariant() : QVariant(0),
            Qt::UserRole - 1);
    }
    if (textureIndex >= 0) {
        m_fillColorSourceCombo->setItemData(
            textureIndex,
            hasTextures ? QVariant() : QVariant(0),
            Qt::UserRole - 1);
    }
    rebuildFillPbrSourceCombos();
    if (m_uvShowFullTextureCheck)
        m_uvShowFullTextureCheck->setEnabled(hasTextures);
    syncFillPbrUiState();
}

void RenderOverlayPanel::setFillPbrMapAvailability(
    bool hasNormalMap,
    bool hasOcclusionMap,
    bool hasRoughnessMap)
{
    m_fillHasNormalMap = hasNormalMap;
    m_fillHasOcclusionMap = hasOcclusionMap;
    m_fillHasRoughnessMap = hasRoughnessMap;
    syncFillPbrUiState();
}

void RenderOverlayPanel::setFillPbrTextureNames(const QStringList &textureNames)
{
    if (m_fillTextureNames == textureNames)
        return;
    m_fillTextureNames = textureNames;
    rebuildFillPbrSourceCombos();
}

void RenderOverlayPanel::rebuildFillPbrSourceCombos()
{
    rebuildFillPbrSourceCombo(
        m_fillPbrAlbedoCombo,
        m_settings.fillPbrAlbedoSource,
        m_settings.fillPbrAlbedoTextureIndex);
    rebuildFillPbrSourceCombo(
        m_fillPbrNormalCombo,
        m_settings.fillPbrNormalSource,
        m_settings.fillPbrNormalTextureIndex);
    rebuildFillPbrSourceCombo(
        m_fillPbrOcclusionCombo,
        m_settings.fillPbrOcclusionSource,
        m_settings.fillPbrOcclusionTextureIndex);
    rebuildFillPbrSourceCombo(
        m_fillPbrRoughnessCombo,
        m_settings.fillPbrRoughnessSource,
        m_settings.fillPbrRoughnessTextureIndex);
}

void RenderOverlayPanel::rebuildFillPbrSourceCombo(
    QComboBox *combo,
    FillPbrTextureSource currentSource,
    int currentTextureIndex)
{
    if (!combo)
        return;

    QSignalBlocker blocker(combo);
    combo->clear();

    auto addItem = [this, combo](const QString &label, FillPbrTextureSource source, int textureIndex) {
        combo->addItem(label);
        const int idx = combo->count() - 1;
        combo->setItemData(idx, static_cast<int>(source), kPbrSourceRole);
        combo->setItemData(idx, textureIndex, kPbrTextureIndexRole);
        if (source == FillPbrTextureSource::Texture && !m_fillHasTextures)
            combo->setItemData(idx, QVariant(0), Qt::UserRole - 1);
    };

    addItem(tr("None"), FillPbrTextureSource::None, -1);
    addItem(tr("Constant"), FillPbrTextureSource::Constant, -1);
    for (int i = 0; i < m_fillTextureNames.size(); ++i)
        addItem(m_fillTextureNames.at(i), FillPbrTextureSource::Texture, i);

    syncFillPbrSourceCombo(combo, currentSource, currentTextureIndex);
}

void RenderOverlayPanel::syncFillPbrSourceCombo(
    QComboBox *combo,
    FillPbrTextureSource currentSource,
    int currentTextureIndex)
{
    if (!combo)
        return;

    QSignalBlocker blocker(combo);
    int selected = -1;
    const int sourceValue = static_cast<int>(currentSource);
    for (int i = 0; i < combo->count(); ++i) {
        const int itemSource = combo->itemData(i, kPbrSourceRole).toInt();
        const int itemTexIndex = combo->itemData(i, kPbrTextureIndexRole).toInt();
        if (itemSource == sourceValue && itemTexIndex == currentTextureIndex) {
            selected = i;
            break;
        }
    }
    if (selected < 0) {
        for (int i = 0; i < combo->count(); ++i) {
            const int itemSource = combo->itemData(i, kPbrSourceRole).toInt();
            const int itemTexIndex = combo->itemData(i, kPbrTextureIndexRole).toInt();
            if (itemSource == sourceValue && itemTexIndex == -1) {
                selected = i;
                break;
            }
        }
    }
    if (selected < 0)
        selected = 0;
    combo->setCurrentIndex(selected);
}

void RenderOverlayPanel::syncFillPbrUiState()
{
    const bool usePbr = (m_settings.fillMaterial == FillMaterial::Pbr);
    if (m_fillMaterialStack)
        m_fillMaterialStack->setCurrentIndex(usePbr ? 1 : 0);

    const bool constantAlbedo = (m_settings.fillPbrAlbedoSource == FillPbrTextureSource::Constant);
    if (m_fillPbrAlbedoCombo)
        m_fillPbrAlbedoCombo->setEnabled(usePbr);
    if (m_fillPbrNormalCombo)
        m_fillPbrNormalCombo->setEnabled(usePbr);
    if (m_fillPbrOcclusionCombo)
        m_fillPbrOcclusionCombo->setEnabled(usePbr);
    if (m_fillPbrRoughnessCombo)
        m_fillPbrRoughnessCombo->setEnabled(usePbr);
    if (m_fillPbrColorButton)
        m_fillPbrColorButton->setEnabled(usePbr && constantAlbedo);

    const bool normalEnabled =
        usePbr && m_settings.fillPbrNormalSource == FillPbrTextureSource::Texture;
    if (m_fillNormalScaleSpin)
        m_fillNormalScaleSpin->setEnabled(normalEnabled);

    const bool occlusionEnabled = usePbr && m_settings.fillPbrOcclusionSource != FillPbrTextureSource::None;
    if (m_fillOcclusionStrengthSpin)
        m_fillOcclusionStrengthSpin->setEnabled(occlusionEnabled);

    const bool roughnessEnabled = usePbr && m_settings.fillPbrRoughnessSource != FillPbrTextureSource::None;
    if (m_fillRoughnessFactorSpin)
        m_fillRoughnessFactorSpin->setEnabled(roughnessEnabled);
}

void RenderOverlayPanel::updateColorButtonStyle(QPushButton *button, const QColor &color)
{
    if (!button)
        return;
    button->setText(QString());
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid rgba(40,40,40,160); border-radius: 3px; padding: 0px; min-width: %2px; min-height: %2px; max-width: %2px; max-height: %2px; }"
        "QPushButton:hover { border-color: rgba(36,132,210,220); }")
            .arg(color.name())
            .arg(kColorButtonSize));
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
    setPassMarker(m_edgesButton, RenderPass::Edges);
    setPassMarker(m_wireButton, RenderPass::Wireframe);
    setPassMarker(m_fillButton, RenderPass::Fill);
    setPassMarker(m_selectionButton, RenderPass::Selection);
    setPassMarker(m_qualityHistogramButton, RenderPass::QualityHistogram);

    setArrowChecked(m_currentMeshSettingsArrow, RenderPass::CurrentMesh);
    setArrowChecked(m_normalsDecoratorsSettingsArrow, RenderPass::DecoratorNormals);
    setArrowChecked(m_boundaryDecoratorsSettingsArrow, RenderPass::DecoratorBoundary);
    setArrowChecked(m_bboxSettingsArrow, RenderPass::BoundingBox);
    setArrowChecked(m_pointsSettingsArrow, RenderPass::Points);
    setArrowChecked(m_edgesSettingsArrow, RenderPass::Edges);
    setArrowChecked(m_wireSettingsArrow, RenderPass::Wireframe);
    setArrowChecked(m_fillSettingsArrow, RenderPass::Fill);
    setArrowChecked(m_selectionSettingsArrow, RenderPass::Selection);
    setArrowChecked(m_qualityHistogramSettingsArrow, RenderPass::QualityHistogram);
}
