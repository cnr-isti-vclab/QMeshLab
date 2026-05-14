#include "meshfilterpanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector3D>
#include <QIcon>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace {
class AbsPercEditor : public QWidget
{
public:
    explicit AbsPercEditor(
        double minValue,
        double maxValue,
        int decimals,
        QWidget *parent = nullptr)
        : QWidget(parent)
        , m_minValue(minValue)
        , m_maxValue(maxValue)
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        m_absSpin = new QDoubleSpinBox(this);
        m_absSpin->setRange(minValue, maxValue);
        m_absSpin->setDecimals(std::clamp(decimals, 0, 10));
        m_absSpin->setAlignment(Qt::AlignRight);
        m_absSpin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        const double span = maxValue - minValue;
        if (std::isfinite(span) && std::fabs(span) > 1e-12)
            m_absSpin->setSingleStep(std::max(span / 100.0, 1e-12));

        auto *absLabel = new QLabel(tr("abs"), this);
        absLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));

        m_percentSpin = new QDoubleSpinBox(this);
        m_percentSpin->setRange(-200.0, 200.0);
        m_percentSpin->setDecimals(3);
        m_percentSpin->setSingleStep(0.5);
        m_percentSpin->setAlignment(Qt::AlignRight);
        m_percentSpin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto *percentLabel = new QLabel(tr("%"), this);
        percentLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));

        layout->addWidget(m_absSpin, 1);
        layout->addWidget(absLabel);
        layout->addSpacing(6);
        layout->addWidget(m_percentSpin, 1);
        layout->addWidget(percentLabel);

        const QString rangeText = tr("Percentage is mapped over the range %1 .. %2.")
                                      .arg(QLocale().toString(m_minValue))
                                      .arg(QLocale().toString(m_maxValue));
        m_percentSpin->setToolTip(rangeText);
        percentLabel->setToolTip(rangeText);

        connect(m_absSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
            updatePercentFromAbsolute(value);
        });
        connect(m_percentSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
            updateAbsoluteFromPercent(value);
        });

        updatePercentFromAbsolute(m_absSpin->value());
    }

    void setAbsoluteValue(double value)
    {
        m_absSpin->setValue(value);
    }

    double absoluteValue() const
    {
        return m_absSpin->value();
    }

private:
    void updatePercentFromAbsolute(double value)
    {
        const double denom = m_maxValue - m_minValue;
        if (!std::isfinite(denom) || std::fabs(denom) <= 1e-12) {
            m_percentSpin->setEnabled(false);
            m_percentSpin->setValue(0.0);
            return;
        }

        const QSignalBlocker blocker(m_percentSpin);
        m_percentSpin->setEnabled(true);
        m_percentSpin->setValue((100.0 * (value - m_minValue)) / denom);
    }

    void updateAbsoluteFromPercent(double value)
    {
        const double denom = m_maxValue - m_minValue;
        if (!std::isfinite(denom) || std::fabs(denom) <= 1e-12)
            return;

        const QSignalBlocker blocker(m_absSpin);
        m_absSpin->setValue(m_minValue + (denom * value * 0.01));
    }

    double m_minValue = 0.0;
    double m_maxValue = 0.0;
    QDoubleSpinBox *m_absSpin = nullptr;
    QDoubleSpinBox *m_percentSpin = nullptr;
};

class FilePathEditor : public QWidget
{
public:
    enum class Mode {
        OpenFile,
        SaveFile
    };

    explicit FilePathEditor(
        Mode mode,
        const QString &dialogTitle,
        const QStringList &nameFilters,
        const QString &defaultSuffix,
        const Document *doc,
        QWidget *parent = nullptr)
        : QWidget(parent)
        , m_mode(mode)
        , m_dialogTitle(dialogTitle)
        , m_nameFilters(nameFilters)
        , m_defaultSuffix(defaultSuffix)
        , m_doc(doc)
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        m_lineEdit = new QLineEdit(this);
        m_lineEdit->setClearButtonEnabled(true);
        m_lineEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto *browseButton = new QToolButton(this);
        browseButton->setText(QStringLiteral("..."));
        browseButton->setToolTip(QObject::tr("Choose file"));

        layout->addWidget(m_lineEdit, 1);
        layout->addWidget(browseButton, 0);

        connect(browseButton, &QToolButton::clicked, this, [this]() {
            QString startPath = m_lineEdit->text().trimmed();
            if (startPath.isEmpty() && m_doc) {
                const int meshIndex = m_doc->currentMeshIndex();
                if (meshIndex >= 0 && meshIndex < m_doc->meshCount()) {
                    const QString sourcePath = m_doc->mesh(meshIndex).sourcePath;
                    if (!sourcePath.isEmpty())
                        startPath = QFileInfo(sourcePath).absolutePath();
                }
            }

            QString chosenPath;
            if (m_mode == Mode::SaveFile) {
                chosenPath = QFileDialog::getSaveFileName(
                    this,
                    m_dialogTitle.isEmpty() ? QObject::tr("Choose File") : m_dialogTitle,
                    startPath,
                    m_nameFilters.join(QStringLiteral(";;")));
                if (!chosenPath.isEmpty()
                    && QFileInfo(chosenPath).suffix().isEmpty()
                    && !m_defaultSuffix.trimmed().isEmpty()) {
                    chosenPath = QStringLiteral("%1.%2").arg(chosenPath, m_defaultSuffix);
                }
            } else {
                chosenPath = QFileDialog::getOpenFileName(
                    this,
                    m_dialogTitle.isEmpty() ? QObject::tr("Choose File") : m_dialogTitle,
                    startPath,
                    m_nameFilters.join(QStringLiteral(";;")));
            }
            if (!chosenPath.isEmpty())
                m_lineEdit->setText(chosenPath);
        });
    }

    void setValue(const QString &value)
    {
        m_lineEdit->setText(value);
    }

    QString value() const
    {
        return m_lineEdit->text();
    }

private:
    Mode m_mode = Mode::OpenFile;
    QString m_dialogTitle;
    QStringList m_nameFilters;
    QString m_defaultSuffix;
    const Document *m_doc = nullptr;
    QLineEdit *m_lineEdit = nullptr;
};

QString textureChoiceLabel(const Document::MeshEntry &entry, int slotIndex)
{
    const int oneBased = slotIndex + 1;
    const QString name = Document::meshTextureDisplayName(entry, slotIndex);
    return QObject::tr("%1: %2").arg(oneBased).arg(name);
}

class TextureRefEditor : public QWidget
{
public:
    explicit TextureRefEditor(
        Document *doc,
        bool allowAutomatic,
        QWidget *parent = nullptr)
        : QWidget(parent)
        , m_doc(doc)
        , m_allowAutomatic(allowAutomatic)
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        m_combo = new QComboBox(this);
        m_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        layout->addWidget(m_combo);
        repopulate(0);
    }

    void setSourceMeshIndex(int meshIndex)
    {
        if (m_sourceMeshIndex == meshIndex)
            return;
        const int preferred = value();
        m_sourceMeshIndex = meshIndex;
        repopulate(preferred);
    }

    void setValue(int value)
    {
        const int pos = m_combo->findData(value);
        if (pos >= 0) {
            m_combo->setCurrentIndex(pos);
            return;
        }
        repopulate(value);
    }

    int value() const
    {
        return m_combo->currentData().toInt();
    }

private:
    void repopulate(int preferredValue)
    {
        const QSignalBlocker blocker(m_combo);
        m_combo->clear();

        bool hasTextures = false;
        if (m_allowAutomatic)
            m_combo->addItem(QObject::tr("Automatic (per-face assignment)"), 0);

        if (m_doc && m_sourceMeshIndex >= 0 && m_sourceMeshIndex < m_doc->meshCount()) {
            const Document::MeshEntry &entry = m_doc->mesh(m_sourceMeshIndex);
            const int textureCount = Document::meshTextureAssociationCount(entry);
            for (int i = 0; i < textureCount; ++i)
                m_combo->addItem(textureChoiceLabel(entry, i), i + 1);
            hasTextures = textureCount > 0;
        }

        if (!hasTextures && !m_allowAutomatic)
            m_combo->addItem(QObject::tr("No textures available"), -1);

        int targetValue = preferredValue;
        if (targetValue <= 0 && m_allowAutomatic)
            targetValue = 0;
        const int pos = m_combo->findData(targetValue);
        if (pos >= 0)
            m_combo->setCurrentIndex(pos);
        else if (m_allowAutomatic && m_combo->count() > 0)
            m_combo->setCurrentIndex(0);
        else if (m_combo->count() > 0)
            m_combo->setCurrentIndex(0);

        m_combo->setEnabled(m_combo->count() > 0);
    }

    Document *m_doc = nullptr;
    bool m_allowAutomatic = true;
    int m_sourceMeshIndex = -1;
    QComboBox *m_combo = nullptr;
};

class TextureOutputRefEditor : public QWidget
{
public:
    explicit TextureOutputRefEditor(
        Document *doc,
        const QString &dialogTitle,
        const QStringList &nameFilters,
        const QString &defaultSuffix,
        QWidget *parent = nullptr)
        : QWidget(parent)
        , m_doc(doc)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        m_combo = new QComboBox(this);
        m_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        layout->addWidget(m_combo);

        m_fileEditor = new FilePathEditor(
            FilePathEditor::Mode::SaveFile,
            dialogTitle,
            nameFilters,
            defaultSuffix,
            doc,
            this);
        layout->addWidget(m_fileEditor);

        connect(m_combo, &QComboBox::currentIndexChanged, this, [this]() { updateModeUi(); });
        repopulate({});
    }

    void setSourceMeshIndex(int meshIndex)
    {
        if (m_sourceMeshIndex == meshIndex)
            return;
        const QVariantMap preferred = value();
        m_sourceMeshIndex = meshIndex;
        repopulate(preferred);
    }

    void setValue(const QVariant &rawValue)
    {
        QVariantMap preferred = rawValue.toMap();
        if (preferred.isEmpty()) {
            const QString path = rawValue.toString().trimmed();
            if (!path.isEmpty()) {
                preferred.insert(QStringLiteral("mode"), QStringLiteral("new"));
                preferred.insert(QStringLiteral("path"), path);
            }
        }
        repopulate(preferred);
    }

    QVariantMap value() const
    {
        const int currentData = m_combo->currentData().toInt();
        if (currentData > 0) {
            return QVariantMap{
                { QStringLiteral("mode"), QStringLiteral("existing") },
                { QStringLiteral("slot"), currentData }
            };
        }
        return QVariantMap{
            { QStringLiteral("mode"), QStringLiteral("new") },
            { QStringLiteral("path"), m_fileEditor->value().trimmed() }
        };
    }

private:
    void repopulate(const QVariantMap &preferred)
    {
        const QSignalBlocker blocker(m_combo);
        m_combo->clear();

        if (m_doc && m_sourceMeshIndex >= 0 && m_sourceMeshIndex < m_doc->meshCount()) {
            const Document::MeshEntry &entry = m_doc->mesh(m_sourceMeshIndex);
            const int textureCount = Document::meshTextureAssociationCount(entry);
            for (int i = 0; i < textureCount; ++i)
                m_combo->addItem(QObject::tr("Overwrite %1").arg(textureChoiceLabel(entry, i)), i + 1);
        }

        m_combo->addItem(QObject::tr("Create New Texture File..."), 0);

        const QString mode = preferred.value(QStringLiteral("mode")).toString().trimmed().toLower();
        const int preferredSlot = preferred.value(QStringLiteral("slot")).toInt();
        const QString preferredPath = preferred.value(QStringLiteral("path")).toString().trimmed();
        if (!preferredPath.isEmpty())
            m_fileEditor->setValue(preferredPath);

        int targetValue = 0;
        if (mode == QStringLiteral("existing") && preferredSlot > 0)
            targetValue = preferredSlot;
        const int pos = m_combo->findData(targetValue);
        if (pos >= 0)
            m_combo->setCurrentIndex(pos);
        else if (m_combo->count() > 0)
            m_combo->setCurrentIndex(m_combo->count() - 1);

        m_combo->setEnabled(m_combo->count() > 0);
        updateModeUi();
    }

    void updateModeUi()
    {
        const bool creatingNew = (m_combo->currentData().toInt() <= 0);
        m_fileEditor->setVisible(creatingNew);
    }

    Document *m_doc = nullptr;
    int m_sourceMeshIndex = -1;
    QComboBox *m_combo = nullptr;
    FilePathEditor *m_fileEditor = nullptr;
};

QString groupDisplayName(const QString &group)
{
    const QString trimmed = group.trimmed();
    if (trimmed.isEmpty())
        return QObject::tr("Main");
    QString name = trimmed;
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    name.replace(QLatin1Char('.'), QLatin1Char(' '));
    if (!name.isEmpty())
        name[0] = name[0].toUpper();
    return name;
}

QString meshComboLabel(const Document &doc, int meshIndex)
{
    if (meshIndex < 0 || meshIndex >= doc.meshCount())
        return QObject::tr("Mesh %1").arg(meshIndex);

    const Document::MeshEntry &entry = doc.mesh(meshIndex);
    QString label = entry.name.trimmed();
    if (label.isEmpty())
        label = QObject::tr("Mesh %1").arg(meshIndex + 1);
    if (meshIndex == doc.currentMeshIndex())
        label += QObject::tr(" (current)");
    return label;
}

QStringList tokenizeSearchTerms(const QString &text)
{
    static const QRegularExpression kSpaceRe(QStringLiteral("\\s+"));
    QStringList terms = text.trimmed().toLower().split(kSpaceRe, Qt::SkipEmptyParts);
    terms.removeDuplicates();
    return terms;
}

// Editor widget for Point3f parameters.
// Editor widget for Point3f parameters.
// Shows a preset combo-box that auto-fills three X/Y/Z spinboxes when a named
// source is selected.  Manually editing a spinbox switches the combo to "Custom".
class Point3fEditor : public QWidget
{
public:
    explicit Point3fEditor(
        const QString &role, // "point" or "direction"
        Document *doc,
        std::function<MeshFilterPanel::ViewContext()> viewContextProvider,
        QWidget *parent = nullptr)
        : QWidget(parent)
        , m_role(role)
        , m_doc(doc)
        , m_viewContextProvider(std::move(viewContextProvider))
    {
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(2);

        // --- Preset combo row ---
        m_combo = new QComboBox(this);
        m_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        populateCombo();

        auto *comboRow = new QHBoxLayout();
        comboRow->setContentsMargins(0, 0, 0, 0);
        comboRow->setSpacing(2);
        comboRow->addWidget(m_combo, 1);

        // Refresh button — re-fetches the dynamic preset (e.g. current view direction)
        m_refreshBtn = new QToolButton(this);
        m_refreshBtn->setText(QStringLiteral("\u21ba")); // ↺
        m_refreshBtn->setToolTip(QObject::tr("Re-fetch value from selected source"));
        m_refreshBtn->setVisible(false);
        comboRow->addWidget(m_refreshBtn, 0);
        outer->addLayout(comboRow);

        // --- Spinbox row ---
        auto *spinRow = new QHBoxLayout();
        spinRow->setContentsMargins(0, 0, 0, 0);
        spinRow->setSpacing(2);

        const QString labels[3] = { QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z") };
        for (int i = 0; i < 3; ++i) {
            auto *lbl = new QLabel(labels[i], this);
            lbl->setStyleSheet(QStringLiteral("color: palette(mid); padding: 0 1px;"));
            spinRow->addWidget(lbl, 0);
            m_spin[i] = new QDoubleSpinBox(this);
            m_spin[i]->setRange(-1e9, 1e9);
            m_spin[i]->setDecimals(4);
            m_spin[i]->setSingleStep(0.1);
            m_spin[i]->setAlignment(Qt::AlignRight);
            // Fixed compact width so three spinboxes fit side-by-side
            m_spin[i]->setFixedWidth(70);
            spinRow->addWidget(m_spin[i], 0);
        }
        spinRow->addStretch(1);
        outer->addLayout(spinRow);

        // Editing a spinbox switches combo to "Custom"
        for (int i = 0; i < 3; ++i) {
            connect(m_spin[i], qOverload<double>(&QDoubleSpinBox::valueChanged),
                    this, &Point3fEditor::onSpinEdited);
        }
        // Selecting a combo preset fills spinboxes
        connect(m_combo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &Point3fEditor::onPresetSelected);
        // Refresh button re-applies the current dynamic preset
        connect(m_refreshBtn, &QToolButton::clicked,
                this, [this]() { onPresetSelected(m_combo->currentIndex()); });
    }

    void setValue(const QVector3D &v)
    {
        // Set spinboxes quietly, then select "Custom" so the combo reflects the explicit value
        setSpinsQuiet(v);
        selectCustom();
    }

    QVector3D value() const
    {
        return QVector3D(float(m_spin[0]->value()),
                         float(m_spin[1]->value()),
                         float(m_spin[2]->value()));
    }

private:
    // Preset entry: displayed name + optional fixed value (nullopt = dynamic/custom)
    struct Preset {
        QString label;
        QVector3D vec;    // only used when !dynamic
        bool dynamic = false; // requires context provider
    };

    static constexpr int kCustomIndex = 0; // "Custom" is always first

    void setSpinsReadOnly(bool ro)
    {
        for (int i = 0; i < 3; ++i) {
            m_spin[i]->setReadOnly(ro);
            m_spin[i]->setButtonSymbols(ro ? QAbstractSpinBox::NoButtons
                                           : QAbstractSpinBox::UpDownArrows);
            m_spin[i]->setStyleSheet(ro ? QStringLiteral("color: palette(mid);") : QString());
        }
    }

    void updatePresetUi(int idx)
    {
        const bool isCustom = (idx == kCustomIndex);
        const bool isDynamic = !isCustom && idx < int(m_presets.size()) && m_presets[idx].dynamic;
        setSpinsReadOnly(!isCustom);
        m_refreshBtn->setVisible(isDynamic);
    }

    void populateCombo()
    {
        m_presets.clear();
        m_combo->blockSignals(true);

        // Always first: Custom (user-edited)
        m_presets.push_back({ QObject::tr("Custom"), {}, false });
        m_combo->addItem(m_presets.back().label);

        if (m_role == QStringLiteral("direction")) {
            m_presets.push_back({ QObject::tr("+X Axis"),  QVector3D( 1, 0, 0), false });
            m_presets.push_back({ QObject::tr("-X Axis"),  QVector3D(-1, 0, 0), false });
            m_presets.push_back({ QObject::tr("+Y Axis"),  QVector3D( 0, 1, 0), false });
            m_presets.push_back({ QObject::tr("-Y Axis"),  QVector3D( 0,-1, 0), false });
            m_presets.push_back({ QObject::tr("+Z Axis"),  QVector3D( 0, 0, 1), false });
            m_presets.push_back({ QObject::tr("-Z Axis"),  QVector3D( 0, 0,-1), false });
            m_presets.push_back({ QObject::tr("View Direction"),     {}, true });
        } else {
            // "point" role
            m_presets.push_back({ QObject::tr("Origin"),         QVector3D(0, 0, 0), false });
            m_presets.push_back({ QObject::tr("Mesh BBox Center"),   {}, true });
            m_presets.push_back({ QObject::tr("Camera Eye Position"),{}, true });
            m_presets.push_back({ QObject::tr("Trackball Center"),   {}, true });
        }

        for (int i = 1; i < int(m_presets.size()); ++i)
            m_combo->addItem(m_presets[i].label);

        m_combo->blockSignals(false);
    }

    void setSpinsQuiet(const QVector3D &v)
    {
        for (int i = 0; i < 3; ++i) {
            const QSignalBlocker b(m_spin[i]);
            m_spin[i]->setValue(double(v[i]));
        }
    }

    void selectCustom()
    {
        const QSignalBlocker b(m_combo);
        m_combo->setCurrentIndex(kCustomIndex);
        updatePresetUi(kCustomIndex);
    }

    void onSpinEdited()
    {
        // User changed a spinbox manually — switch to Custom (spinboxes stay editable)
        const QSignalBlocker b(m_combo);
        if (m_combo->currentIndex() != kCustomIndex) {
            m_combo->setCurrentIndex(kCustomIndex);
            updatePresetUi(kCustomIndex);
        }
    }

    void onPresetSelected(int idx)
    {
        if (idx < 0 || idx >= int(m_presets.size()))
            return;

        updatePresetUi(idx);

        if (idx == kCustomIndex)
            return; // spinboxes already editable, nothing to fill

        const Preset &p = m_presets[idx];
        if (!p.dynamic) {
            setSpinsQuiet(p.vec);
            return;
        }

        // Dynamic — requires context
        const bool hasMesh = m_doc
            && m_doc->currentMeshIndex() >= 0
            && m_doc->currentMeshIndex() < m_doc->meshCount();

        if (m_role == QStringLiteral("direction")) {
            if (p.label == QObject::tr("View Direction") && m_viewContextProvider) {
                setSpinsQuiet(m_viewContextProvider().viewDirection);
                return;
            }
        } else {
            if (p.label == QObject::tr("Mesh BBox Center") && hasMesh) {
                const vcg::Point3f c = m_doc->mesh(m_doc->currentMeshIndex()).mesh.bbox.Center();
                setSpinsQuiet(QVector3D(c[0], c[1], c[2]));
                return;
            }
            if (p.label == QObject::tr("Camera Eye Position") && m_viewContextProvider) {
                setSpinsQuiet(m_viewContextProvider().eyePosition);
                return;
            }
            if (p.label == QObject::tr("Trackball Center") && m_viewContextProvider) {
                setSpinsQuiet(m_viewContextProvider().trackballCenter);
                return;
            }
        }

        // Provider unavailable — fall back to Custom
        selectCustom();
    }

    QString m_role;
    Document *m_doc = nullptr;
    std::function<MeshFilterPanel::ViewContext()> m_viewContextProvider;
    std::vector<Preset> m_presets;
    QComboBox *m_combo = nullptr;
    QToolButton *m_refreshBtn = nullptr;
    QDoubleSpinBox *m_spin[3] = { nullptr, nullptr, nullptr };
};

} // namespace

MeshFilterPanel::MeshFilterPanel(Document *doc, QWidget *parent)
    : QWidget(parent)
    , m_doc(doc)
{
    buildUi();
    reloadFilters();
}

void MeshFilterPanel::setViewContextProvider(std::function<ViewContext()> fn)
{
    m_viewContextProvider = std::move(fn);
}

void MeshFilterPanel::setTrackballCenterProvider(std::function<QVector3D()> fn)
{
    // Legacy wrapper: build a ViewContext provider from the old trackball-center-only provider
    m_viewContextProvider = [fn = std::move(fn)]() -> ViewContext {
        const QVector3D c = fn ? fn() : QVector3D(0, 0, 0);
        return ViewContext{ c, c, QVector3D(0, 0, -1) };
    };
}


void MeshFilterPanel::buildUi()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(6);

    auto *searchLayout = new QHBoxLayout();
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(4);
    m_searchButton = new QToolButton(this);
    m_searchButton->setAutoRaise(true);
    const QIcon searchIcon = QIcon::fromTheme(QIcon::ThemeIcon::SystemSearch);
    if (!searchIcon.isNull())
        m_searchButton->setIcon(searchIcon);
    else
        m_searchButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    m_searchButton->setToolTip(tr("Search filters"));
    searchLayout->addWidget(m_searchButton, 0);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search filters..."));
    m_searchEdit->installEventFilter(this);
    searchLayout->addWidget(m_searchEdit, 1);
    rootLayout->addLayout(searchLayout);

    m_stack = new QStackedWidget(this);
    rootLayout->addWidget(m_stack, 1);

    m_resultsPage = new QWidget(m_stack);
    auto *resultsLayout = new QVBoxLayout(m_resultsPage);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    resultsLayout->setSpacing(4);
    m_resultsList = new QListWidget(m_resultsPage);
    m_resultsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultsList->installEventFilter(this);
    resultsLayout->addWidget(m_resultsList, 1);
    m_stack->addWidget(m_resultsPage);

    m_parametersPage = new QWidget(m_stack);
    auto *paramsPageLayout = new QVBoxLayout(m_parametersPage);
    paramsPageLayout->setContentsMargins(0, 0, 0, 0);
    paramsPageLayout->setSpacing(6);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);

    m_filterTitleLabel = new QLabel(m_parametersPage);
    QFont titleFont = m_filterTitleLabel->font();
    titleFont.setBold(true);
    m_filterTitleLabel->setFont(titleFont);
    m_filterTitleLabel->setWordWrap(true);
    headerLayout->addWidget(m_filterTitleLabel, 1);

    m_longDescriptionToggle = new QToolButton(m_parametersPage);
    m_longDescriptionToggle->setCheckable(true);
    m_longDescriptionToggle->setChecked(false);
    m_longDescriptionToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_longDescriptionToggle->setText(QStringLiteral("?"));
    m_longDescriptionToggle->setToolTip(tr("Show details"));
    m_longDescriptionToggle->hide();
#ifdef QMESHLAB_PYTHON_CONSOLE
    m_copyToConsoleButton = new QToolButton(m_parametersPage);
    m_copyToConsoleButton->setText(QStringLiteral(">_"));
    m_copyToConsoleButton->setToolTip(tr("Copy Python call to console"));
    m_copyToConsoleButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_copyToConsoleButton->setAutoRaise(true);
    m_copyToConsoleButton->hide();
    connect(m_copyToConsoleButton, &QToolButton::clicked, this, [this]() {
        if (m_currentFilterKey.isEmpty())
            return;
        const Document::FilterInfo *info = filterByKey(m_currentFilterKey);
        if (!info)
            return;
        const QString pyName = info->descriptor.effectivePythonName();
        const MeshFilterParameterValues vals = collectCurrentParameterValues();
        QStringList args;
        for (const ParameterBinding &binding : m_parameterBindings) {
            const auto it = vals.constFind(binding.descriptor.id);
            if (it == vals.constEnd())
                continue;
            const QVariant &v = it.value();
            QString lit;
            switch (binding.descriptor.type) {
            case MeshFilterParameterType::Bool:
                lit = v.toBool() ? QStringLiteral("True") : QStringLiteral("False");
                break;
            case MeshFilterParameterType::Int:
            case MeshFilterParameterType::Mesh:
                lit = QString::number(v.toInt());
                break;
            case MeshFilterParameterType::Double:
            case MeshFilterParameterType::AbsPerc:
                lit = QString::number(v.toDouble(), 'g', 10);
                break;
            case MeshFilterParameterType::Color: {
                const QColor c = colorFromVariant(v, QColor(Qt::white));
                lit = QStringLiteral("[%1, %2, %3, %4]")
                    .arg(c.redF(), 0, 'g', 4)
                    .arg(c.greenF(), 0, 'g', 4)
                    .arg(c.blueF(), 0, 'g', 4)
                    .arg(c.alphaF(), 0, 'g', 4);
                break;
            }
            case MeshFilterParameterType::Point3f: {
                if (v.userType() == QMetaType::QVector3D) {
                    const QVector3D p = v.value<QVector3D>();
                    lit = QStringLiteral("[%1, %2, %3]")
                        .arg(p.x(), 0, 'g', 6)
                        .arg(p.y(), 0, 'g', 6)
                        .arg(p.z(), 0, 'g', 6);
                } else {
                    lit = QStringLiteral("[0, 0, 0]");
                }
                break;
            }
            default: {
                QString s = v.toString();
                s.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
                s.replace(QStringLiteral("\""), QStringLiteral("\\\""));
                lit = QStringLiteral("\"%1\"").arg(s);
                break;
            }
            }
            args.append(QStringLiteral("%1=%2").arg(binding.descriptor.id, lit));
        }
        const QString code = QStringLiteral("ms.%1(%2)").arg(pyName, args.join(QStringLiteral(", ")));
        emit copyToConsoleRequested(code);
    });
#endif
    m_applyButton = new QPushButton(tr("Apply"), m_parametersPage);
    headerLayout->addWidget(m_longDescriptionToggle, 0, Qt::AlignTop);
#ifdef QMESHLAB_PYTHON_CONSOLE
    headerLayout->addWidget(m_copyToConsoleButton, 0, Qt::AlignTop);
#endif
    headerLayout->addWidget(m_applyButton, 0, Qt::AlignTop);
    paramsPageLayout->addLayout(headerLayout);

    m_filterDescriptionLabel = new QLabel(m_parametersPage);
    m_filterDescriptionLabel->setWordWrap(true);
    m_filterDescriptionLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    paramsPageLayout->addWidget(m_filterDescriptionLabel);

    m_filterModifiesLabel = new QLabel(m_parametersPage);
    m_filterModifiesLabel->setVisible(false);
    {
        QFont f = m_filterModifiesLabel->font();
        f.setFamily(QStringLiteral("Courier New, Courier, monospace"));
        m_filterModifiesLabel->setFont(f);
    }
    paramsPageLayout->addWidget(m_filterModifiesLabel);

    m_longDescriptionView = new QTextBrowser(m_parametersPage);
    m_longDescriptionView->setOpenExternalLinks(true);
    m_longDescriptionView->setVisible(false);
    m_longDescriptionView->setMaximumHeight(180);
    paramsPageLayout->addWidget(m_longDescriptionView);

    m_showAdvancedCheck = new QCheckBox(tr("Show advanced parameters"), m_parametersPage);
    m_showAdvancedCheck->setChecked(false);
    paramsPageLayout->addWidget(m_showAdvancedCheck);

    m_parametersScroll = new QScrollArea(m_parametersPage);
    m_parametersScroll->setWidgetResizable(true);
    m_parametersWidget = new QWidget(m_parametersScroll);
    m_parametersLayout = new QFormLayout(m_parametersWidget);
    m_parametersLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_parametersLayout->setContentsMargins(0, 0, 0, 0);
    m_noParametersLabel = new QLabel(tr("This filter has no parameters."), m_parametersWidget);
    m_noParametersLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    m_parametersLayout->addRow(m_noParametersLabel);
    m_parametersScroll->setWidget(m_parametersWidget);
    paramsPageLayout->addWidget(m_parametersScroll, 1);

    m_stack->addWidget(m_parametersPage);
    m_stack->setCurrentWidget(m_resultsPage);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &MeshFilterPanel::onSearchTextChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &MeshFilterPanel::onSearchReturnPressed);
    connect(m_resultsList, &QListWidget::itemClicked, this, &MeshFilterPanel::onResultItemClicked);
    connect(m_resultsList, &QListWidget::itemActivated, this, &MeshFilterPanel::onResultItemActivated);
    connect(m_searchButton, &QToolButton::clicked, this, [this]() { showSearchResultsFromUi(true); });
    connect(m_applyButton, &QPushButton::clicked, this, &MeshFilterPanel::onApplyClicked);
    connect(m_showAdvancedCheck, &QCheckBox::toggled, this, &MeshFilterPanel::onShowAdvancedToggled);
    connect(m_longDescriptionToggle, &QToolButton::toggled, this, [this](bool checked) {
        if (m_longDescriptionView)
            m_longDescriptionView->setVisible(checked);
    });
}

void MeshFilterPanel::reloadFilters()
{
    cacheCurrentFilterParameters();
    const QString previousKey = m_currentFilterKey;
    if (m_doc) {
        m_filters = m_doc->filterInfos();
        std::sort(
            m_filters.begin(),
            m_filters.end(),
            [](const Document::FilterInfo &a, const Document::FilterInfo &b) {
                const int menuCmp =
                    a.descriptor.menuPath.compare(b.descriptor.menuPath, Qt::CaseInsensitive);
                if (menuCmp != 0)
                    return menuCmp < 0;
                return a.descriptor.name.compare(b.descriptor.name, Qt::CaseInsensitive) < 0;
            });
    } else {
        m_filters.clear();
    }

    rebuildResultsList();
    if (!previousKey.isEmpty()) {
        for (int i = 0; i < static_cast<int>(m_filters.size()); ++i) {
            if (m_filters[static_cast<size_t>(i)].key == previousKey) {
                openFilterAtIndex(i);
                return;
            }
        }
    }

    m_currentFilterKey.clear();
    m_stack->setCurrentWidget(m_resultsPage);
}

void MeshFilterPanel::showSearchResults()
{
    m_stack->setCurrentWidget(m_resultsPage);
}

void MeshFilterPanel::focusSearch()
{
    if (!m_searchEdit)
        return;
    m_searchEdit->setFocus(Qt::OtherFocusReason);
    m_searchEdit->selectAll();
}

void MeshFilterPanel::selectFilterByKey(const QString &filterKey, bool openParameters)
{
    if (filterKey.trimmed().isEmpty())
        return;

    for (int i = 0; i < static_cast<int>(m_filters.size()); ++i) {
        if (m_filters[static_cast<size_t>(i)].key != filterKey)
            continue;

        for (int row = 0; row < m_resultsList->count(); ++row) {
            QListWidgetItem *item = m_resultsList->item(row);
            if (!item)
                continue;
            if (item->data(Qt::UserRole).toInt() == i) {
                m_resultsList->setCurrentRow(row);
                break;
            }
        }
        if (openParameters)
            openFilterAtIndex(i);
        return;
    }
}

void MeshFilterPanel::onSearchTextChanged(const QString &)
{
    showSearchResultsFromUi(false);
    rebuildResultsList();
}

void MeshFilterPanel::onSearchReturnPressed()
{
    if (m_stack->currentWidget() == m_parametersPage) {
        onApplyClicked();
        return;
    }
    openSelectedResult(true);
}

void MeshFilterPanel::onResultItemClicked(QListWidgetItem *item)
{
    if (!item)
        return;
    const int filterIndex = item->data(Qt::UserRole).toInt();
    openFilterAtIndex(filterIndex);
}

void MeshFilterPanel::onResultItemActivated(QListWidgetItem *)
{
    openSelectedResult(true);
}

void MeshFilterPanel::onApplyClicked()
{
    if (m_currentFilterKey.trimmed().isEmpty())
        return;
    const Document::FilterInfo *info = filterByKey(m_currentFilterKey);
    if (!info)
        return;

    const MeshFilterParameterValues parameters = collectCurrentParameterValues();
    QString applicabilityError;
    if (!m_doc->validateFilterInvocation(m_currentFilterKey, parameters, applicabilityError))
        return;
    m_filterParameterCache.insert(m_currentFilterKey, parameters);
    emit runRequested(m_currentFilterKey, parameters, info->descriptor.name);
}

void MeshFilterPanel::onShowAdvancedToggled(bool checked)
{
    for (auto &binding : m_parameterBindings) {
        if (!binding.advanced)
            continue;
        if (binding.formLabel)
            binding.formLabel->setVisible(checked);
        if (binding.rowField)
            binding.rowField->setVisible(checked);
    }
}

void MeshFilterPanel::rebuildResultsList()
{
    m_resultsList->clear();
    m_visibleFilterIndices.clear();
    const QStringList terms = tokenizeSearchTerms(m_searchEdit->text());
    std::vector<int> titleFirst;
    std::vector<int> otherMatches;

    for (int i = 0; i < static_cast<int>(m_filters.size()); ++i) {
        const Document::FilterInfo &info = m_filters[static_cast<size_t>(i)];
        if (!matchesSearch(info, terms))
            continue;

        if (titleMatchesAllTerms(info, terms))
            titleFirst.push_back(i);
        else
            otherMatches.push_back(i);
    }

    auto appendResultItem = [this](int filterIndex) {
        const Document::FilterInfo &info = m_filters[static_cast<size_t>(filterIndex)];
        m_visibleFilterIndices.push_back(filterIndex);
        const QString text = info.descriptor.menuPath.trimmed().isEmpty()
            ? info.descriptor.name
            : QStringLiteral("%1 / %2").arg(info.descriptor.menuPath, info.descriptor.name);
        auto *item = new QListWidgetItem(text, m_resultsList);
        item->setData(Qt::UserRole, filterIndex);
        QString tip = info.descriptor.shortDescription.trimmed();
        if (!info.applicable && !info.applicabilityError.trimmed().isEmpty()) {
            if (!tip.isEmpty())
                tip += QStringLiteral("\n");
            tip += tr("Unavailable: %1").arg(info.applicabilityError);
            item->setForeground(palette().brush(QPalette::Mid));
        }
        if (!tip.isEmpty())
            item->setToolTip(tip);
    };

    for (int filterIndex : titleFirst)
        appendResultItem(filterIndex);
    for (int filterIndex : otherMatches)
        appendResultItem(filterIndex);

    if (m_resultsList->count() > 0 && m_resultsList->currentRow() < 0)
        m_resultsList->setCurrentRow(0);
}

void MeshFilterPanel::openSelectedResult(bool focusApplyButton)
{
    if (!m_resultsList)
        return;

    QListWidgetItem *item = m_resultsList->currentItem();
    if (!item && m_resultsList->count() > 0) {
        item = m_resultsList->item(0);
        m_resultsList->setCurrentItem(item);
    }
    if (!item)
        return;

    onResultItemClicked(item);
    if (focusApplyButton && m_applyButton && m_stack->currentWidget() == m_parametersPage)
        m_applyButton->setFocus(Qt::OtherFocusReason);
}

void MeshFilterPanel::openFilterAtIndex(int filterIndex)
{
    if (filterIndex < 0 || filterIndex >= static_cast<int>(m_filters.size()))
        return;

    cacheCurrentFilterParameters();

    const Document::FilterInfo &info = m_filters[static_cast<size_t>(filterIndex)];
    m_currentFilterKey = info.key;
    m_filterTitleLabel->setText(info.descriptor.name);
    m_filterDescriptionLabel->setText(info.descriptor.shortDescription);
    const QStringList &mods = info.descriptor.outputModifies;
    if (!mods.isEmpty()) {
        m_filterModifiesLabel->setText(tr("Modifies: ") + mods.join(QStringLiteral(" ")));
        m_filterModifiesLabel->setVisible(true);
    } else {
        m_filterModifiesLabel->setVisible(false);
    }
    const QString longDescription = info.descriptor.longDescriptionMarkdown.trimmed();
    const bool hasLongDescription = !longDescription.isEmpty();
    m_longDescriptionToggle->setVisible(hasLongDescription);
#ifdef QMESHLAB_PYTHON_CONSOLE
    if (m_copyToConsoleButton)
        m_copyToConsoleButton->setVisible(true);
#endif
    if (hasLongDescription) {
        m_longDescriptionView->setMarkdown(longDescription);
    } else {
        m_longDescriptionView->clear();
    }
    if (m_longDescriptionToggle->isChecked())
        m_longDescriptionToggle->setChecked(false);
    else
        m_longDescriptionView->setVisible(false);
    if (info.applicable) {
        m_filterDescriptionLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
        m_applyButton->setToolTip(QString());
        m_currentFilterUnavailableReason.clear();
    } else {
        const QString reason = info.applicabilityError.trimmed().isEmpty()
            ? tr("This filter is not available in the current context.")
            : info.applicabilityError.trimmed();
        m_filterDescriptionLabel->setStyleSheet(QStringLiteral("color: #a13a3a;"));
        m_applyButton->setToolTip(tr("Unavailable: %1").arg(reason));
        m_currentFilterUnavailableReason = reason;
    }
    buildParameterEditors(info);
    const auto cacheIt = m_filterParameterCache.constFind(info.key);
    if (cacheIt != m_filterParameterCache.constEnd())
        applyParameterValuesToEditors(cacheIt.value());
    refreshCurrentFilterApplicability();
    m_stack->setCurrentWidget(m_parametersPage);
}

bool MeshFilterPanel::matchesSearch(
    const Document::FilterInfo &filterInfo,
    const QStringList &terms) const
{
    if (terms.isEmpty())
        return true;

    const QString name = filterInfo.descriptor.name.toLower();
    const QString shortDesc = filterInfo.descriptor.shortDescription.toLower();
    const QString longDesc = filterInfo.descriptor.longDescriptionMarkdown.toLower();

    for (const QString &term : terms) {
        if (term.isEmpty())
            continue;
        const bool termMatched =
            name.contains(term)
            || shortDesc.contains(term)
            || longDesc.contains(term);
        if (!termMatched)
            return false;
    }
    return true;
}

bool MeshFilterPanel::titleMatchesAllTerms(
    const Document::FilterInfo &filterInfo,
    const QStringList &terms) const
{
    if (terms.isEmpty())
        return true;

    const QString name = filterInfo.descriptor.name.toLower();
    for (const QString &term : terms) {
        if (term.isEmpty())
            continue;
        if (!name.contains(term))
            return false;
    }
    return true;
}

void MeshFilterPanel::clearParameterEditors()
{
    m_parameterBindings.clear();
    while (m_parametersLayout->rowCount() > 0)
        m_parametersLayout->removeRow(0);
    m_noParametersLabel = new QLabel(tr("This filter has no parameters."), m_parametersWidget);
    m_noParametersLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    m_parametersLayout->addRow(m_noParametersLabel);
}

void MeshFilterPanel::buildParameterEditors(const Document::FilterInfo &filterInfo)
{
    clearParameterEditors();
    if (filterInfo.descriptor.parameters.empty()) {
        m_noParametersLabel->show();
        if (m_showAdvancedCheck) {
            m_showAdvancedCheck->setChecked(false);
            m_showAdvancedCheck->hide();
        }
        return;
    }
    if (m_noParametersLabel)
        m_noParametersLabel->hide();

    std::set<QString> uniqueGroups;
    for (const auto &param : filterInfo.descriptor.parameters) {
        if (param.id.trimmed().isEmpty())
            continue;
        uniqueGroups.insert(param.group);
    }
    const bool showGroupHeaders = uniqueGroups.size() > 1;

    QString currentGroup;
    bool hasAdvanced = false;
    for (const auto &param : filterInfo.descriptor.parameters) {
        if (param.id.trimmed().isEmpty())
            continue;
        if (showGroupHeaders && currentGroup != param.group) {
            currentGroup = param.group;
            auto *groupLabel = new QLabel(groupDisplayName(currentGroup), m_parametersWidget);
            QFont f = groupLabel->font();
            f.setBold(true);
            groupLabel->setFont(f);
            groupLabel->setStyleSheet(QStringLiteral("color: palette(mid); padding-top: 6px;"));
            m_parametersLayout->addRow(groupLabel);
        } else {
            currentGroup = param.group;
        }

        ParameterBinding binding;
        binding.descriptor = param;
        binding.advanced = param.isAdvancedGroup();
        hasAdvanced = hasAdvanced || binding.advanced;

        QWidget *editor = nullptr;
        switch (param.type) {
        case MeshFilterParameterType::Bool: {
            auto *w = new QCheckBox(m_parametersWidget);
            w->setChecked(param.defaultValue.toBool());
            editor = w;
            break;
        }
        case MeshFilterParameterType::Int: {
            auto *w = new QSpinBox(m_parametersWidget);
            const int minV = param.minValue.isValid() ? param.minValue.toInt() : std::numeric_limits<int>::lowest();
            const int maxV = param.maxValue.isValid() ? param.maxValue.toInt() : std::numeric_limits<int>::max();
            w->setRange(minV, maxV);
            w->setValue(param.defaultValue.isValid() ? param.defaultValue.toInt() : 0);
            editor = w;
            break;
        }
        case MeshFilterParameterType::Mesh: {
            auto *w = new QComboBox(m_parametersWidget);
            for (int mi = 0; mi < m_doc->meshCount(); ++mi)
                w->addItem(meshComboLabel(*m_doc, mi), mi);
            int defaultIndex = param.defaultValue.isValid() ? param.defaultValue.toInt() : m_doc->currentMeshIndex();
            if (defaultIndex < 0 || defaultIndex >= m_doc->meshCount())
                defaultIndex = m_doc->currentMeshIndex();
            if (defaultIndex >= 0) {
                const int pos = w->findData(defaultIndex);
                if (pos >= 0)
                    w->setCurrentIndex(pos);
            }
            connect(w, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
                refreshDependentParameterEditors();
            });
            editor = w;
            break;
        }
        case MeshFilterParameterType::Double: {
            auto *w = new QDoubleSpinBox(m_parametersWidget);
            const double minV = param.minValue.isValid() ? param.minValue.toDouble() : -1e12;
            const double maxV = param.maxValue.isValid() ? param.maxValue.toDouble() : 1e12;
            w->setRange(minV, maxV);
            w->setDecimals(std::clamp(param.decimals, 0, 10));
            w->setValue(param.defaultValue.isValid() ? param.defaultValue.toDouble() : 0.0);
            editor = w;
            break;
        }
        case MeshFilterParameterType::AbsPerc: {
            const double minV = param.minValue.isValid() ? param.minValue.toDouble() : 0.0;
            const double maxV = param.maxValue.isValid() ? param.maxValue.toDouble() : 1.0;
            auto *w = new AbsPercEditor(minV, maxV, param.decimals, m_parametersWidget);
            w->setAbsoluteValue(param.defaultValue.isValid() ? param.defaultValue.toDouble() : minV);
            editor = w;
            break;
        }
        case MeshFilterParameterType::String: {
            auto *w = new QLineEdit(m_parametersWidget);
            w->setText(param.defaultValue.toString());
            editor = w;
            break;
        }
        case MeshFilterParameterType::FileOpen: {
            auto *w = new FilePathEditor(
                FilePathEditor::Mode::OpenFile,
                param.fileDialogTitle,
                param.fileNameFilters,
                param.fileDefaultSuffix,
                m_doc,
                m_parametersWidget);
            w->setValue(param.defaultValue.toString());
            editor = w;
            break;
        }
        case MeshFilterParameterType::FileSave: {
            auto *w = new FilePathEditor(
                FilePathEditor::Mode::SaveFile,
                param.fileDialogTitle,
                param.fileNameFilters,
                param.fileDefaultSuffix,
                m_doc,
                m_parametersWidget);
            w->setValue(param.defaultValue.toString());
            editor = w;
            break;
        }
        case MeshFilterParameterType::TextureRef: {
            auto *w = new TextureRefEditor(m_doc, param.textureAllowAutomatic, m_parametersWidget);
            editor = w;
            break;
        }
        case MeshFilterParameterType::TextureOutputRef: {
            auto *w = new TextureOutputRefEditor(
                m_doc,
                param.fileDialogTitle,
                param.fileNameFilters,
                param.fileDefaultSuffix,
                m_parametersWidget);
            w->setValue(param.defaultValue);
            editor = w;
            break;
        }
        case MeshFilterParameterType::Enum: {
            auto *w = new QComboBox(m_parametersWidget);
            for (const auto &opt : param.enumOptions)
                w->addItem(opt.label, opt.id);
            const QString defaultId = param.defaultValue.toString();
            if (!defaultId.isEmpty()) {
                const int pos = w->findData(defaultId);
                if (pos >= 0)
                    w->setCurrentIndex(pos);
            }
            editor = w;
            break;
        }
        case MeshFilterParameterType::Color: {
            auto *w = new QPushButton(m_parametersWidget);
            const QColor c = colorFromVariant(param.defaultValue, QColor(Qt::white));
            w->setProperty("filterColor", c);
            updateColorButtonStyle(w, c);
            connect(w, &QPushButton::clicked, this, [this, w]() {
                const QColor current = colorFromVariant(w->property("filterColor"), QColor(Qt::white));
                const QColor chosen = QColorDialog::getColor(current, this, tr("Choose Color"));
                if (!chosen.isValid())
                    return;
                w->setProperty("filterColor", chosen);
                updateColorButtonStyle(w, chosen);
            });
            editor = w;
            break;
        }
        case MeshFilterParameterType::Point3f: {
            auto *w = new Point3fEditor(param.point3fRole, m_doc, m_viewContextProvider, m_parametersWidget);
            const QVector3D defVal = (param.defaultValue.userType() == QMetaType::QVector3D)
                ? param.defaultValue.value<QVector3D>()
                : QVector3D(0.0f, 0.0f, 0.0f);
            w->setValue(defVal);
            editor = w;
            break;
        }
        }

        if (!editor)
            continue;
        binding.editor = editor;

        auto connectApplicabilityRefresh = [&](QObject *obj) {
            if (!obj)
                return;
            connect(obj, SIGNAL(destroyed(QObject*)), this, SLOT(update()));
        };

        auto *labelWidget = new QLabel(param.label, m_parametersWidget);
        binding.formLabel = labelWidget;
        binding.rowField = editor;
        if (!param.helpMarkdown.trimmed().isEmpty()) {
            labelWidget->setToolTip(param.helpMarkdown);
            editor->setToolTip(param.helpMarkdown);
        }
        m_parametersLayout->addRow(labelWidget, editor);

        if (binding.advanced && !m_showAdvancedCheck->isChecked()) {
            labelWidget->hide();
            editor->hide();
        }

        m_parameterBindings.push_back(std::move(binding));

        if (auto *w = qobject_cast<QCheckBox *>(editor)) {
            connect(w, &QCheckBox::toggled, this, [this]() { refreshCurrentFilterApplicability(); });
        } else if (auto *w = qobject_cast<QSpinBox *>(editor)) {
            connect(w, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { refreshCurrentFilterApplicability(); });
        } else if (auto *w = qobject_cast<QDoubleSpinBox *>(editor)) {
            connect(w, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { refreshCurrentFilterApplicability(); });
        } else if (auto *w = qobject_cast<QComboBox *>(editor)) {
            connect(w, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { refreshCurrentFilterApplicability(); });
        } else if (auto *w = qobject_cast<QLineEdit *>(editor)) {
            connect(w, &QLineEdit::textChanged, this, [this](const QString &) { refreshCurrentFilterApplicability(); });
        } else if (auto *w = dynamic_cast<AbsPercEditor *>(editor)) {
            for (QDoubleSpinBox *spin : w->findChildren<QDoubleSpinBox *>()) {
                connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { refreshCurrentFilterApplicability(); });
            }
        } else if (auto *w = dynamic_cast<FilePathEditor *>(editor)) {
            for (QLineEdit *lineEdit : w->findChildren<QLineEdit *>()) {
                connect(lineEdit, &QLineEdit::textChanged, this, [this](const QString &) { refreshCurrentFilterApplicability(); });
            }
        } else if (auto *w = dynamic_cast<TextureRefEditor *>(editor)) {
            for (QComboBox *combo : w->findChildren<QComboBox *>()) {
                connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { refreshCurrentFilterApplicability(); });
            }
        } else if (auto *w = dynamic_cast<TextureOutputRefEditor *>(editor)) {
            for (QComboBox *combo : w->findChildren<QComboBox *>()) {
                connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { refreshCurrentFilterApplicability(); });
            }
            for (QLineEdit *lineEdit : w->findChildren<QLineEdit *>()) {
                connect(lineEdit, &QLineEdit::textChanged, this, [this](const QString &) { refreshCurrentFilterApplicability(); });
            }
        } else if (auto *w = dynamic_cast<Point3fEditor *>(editor)) {
            for (QDoubleSpinBox *spin : w->findChildren<QDoubleSpinBox *>()) {
                connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { refreshCurrentFilterApplicability(); });
            }
        }
    }

    refreshDependentParameterEditors();
    refreshCurrentFilterApplicability();

    if (m_showAdvancedCheck) {
        if (hasAdvanced) {
            m_showAdvancedCheck->show();
        } else {
            m_showAdvancedCheck->setChecked(false);
            m_showAdvancedCheck->hide();
        }
    }
}

void MeshFilterPanel::refreshDependentParameterEditors()
{
    for (const ParameterBinding &binding : m_parameterBindings) {
        if (binding.descriptor.type != MeshFilterParameterType::TextureRef
            && binding.descriptor.type != MeshFilterParameterType::TextureOutputRef)
            continue;

        auto applySourceMeshIndex = [&](auto *editor) {
            if (!editor)
                return;

            int sourceMeshIndex = m_doc ? m_doc->currentMeshIndex() : -1;
            if (!binding.descriptor.textureSourceMeshParameter.trimmed().isEmpty()) {
                if (const ParameterBinding *sourceBinding = bindingById(binding.descriptor.textureSourceMeshParameter)) {
                    if (auto *combo = qobject_cast<QComboBox *>(sourceBinding->editor))
                        sourceMeshIndex = combo->currentData().toInt();
                }
            }
            editor->setSourceMeshIndex(sourceMeshIndex);
        };

        if (binding.descriptor.type == MeshFilterParameterType::TextureRef)
            applySourceMeshIndex(dynamic_cast<TextureRefEditor *>(binding.editor));
        else
            applySourceMeshIndex(dynamic_cast<TextureOutputRefEditor *>(binding.editor));
    }
}

void MeshFilterPanel::refreshCurrentFilterApplicability()
{
    const Document::FilterInfo *info = filterByKey(m_currentFilterKey);
    if (!info || !m_applyButton || !m_filterDescriptionLabel)
        return;

    const MeshFilterParameterValues parameters = collectCurrentParameterValues();
    QString errorMessage;
    const bool applicable = m_doc->validateFilterInvocation(m_currentFilterKey, parameters, errorMessage);

    if (applicable) {
        m_filterDescriptionLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
        m_applyButton->setToolTip(QString());
    } else {
        const QString reason = errorMessage.trimmed().isEmpty()
            ? (!m_currentFilterUnavailableReason.trimmed().isEmpty()
                   ? m_currentFilterUnavailableReason
                   : tr("This filter is not available in the current context."))
            : errorMessage.trimmed();
        m_filterDescriptionLabel->setStyleSheet(QStringLiteral("color: #a13a3a;"));
        m_applyButton->setToolTip(tr("Unavailable: %1").arg(reason));
    }
    m_applyButton->setEnabled(applicable);
}

MeshFilterParameterValues MeshFilterPanel::collectCurrentParameterValues() const
{
    MeshFilterParameterValues values;
    for (const ParameterBinding &binding : m_parameterBindings) {
        const QVariant value = parameterValue(binding);
        if (value.isValid())
            values.insert(binding.descriptor.id, value);
    }
    return values;
}

void MeshFilterPanel::applyParameterValuesToEditors(const MeshFilterParameterValues &values)
{
    for (const ParameterBinding &binding : m_parameterBindings) {
        QWidget *editor = binding.editor;
        if (!editor)
            continue;

        const auto it = values.constFind(binding.descriptor.id);
        if (it == values.constEnd())
            continue;

        const QVariant value = it.value();
        switch (binding.descriptor.type) {
        case MeshFilterParameterType::Bool: {
            if (auto *w = qobject_cast<QCheckBox *>(editor))
                w->setChecked(value.toBool());
            break;
        }
        case MeshFilterParameterType::Int: {
            if (auto *w = qobject_cast<QSpinBox *>(editor))
                w->setValue(value.toInt());
            break;
        }
        case MeshFilterParameterType::Mesh: {
            if (auto *w = qobject_cast<QComboBox *>(editor)) {
                const int pos = w->findData(value.toInt());
                if (pos >= 0)
                    w->setCurrentIndex(pos);
            }
            break;
        }
        case MeshFilterParameterType::Double: {
            if (auto *w = qobject_cast<QDoubleSpinBox *>(editor))
                w->setValue(value.toDouble());
            break;
        }
        case MeshFilterParameterType::AbsPerc: {
            if (auto *w = dynamic_cast<AbsPercEditor *>(editor))
                w->setAbsoluteValue(value.toDouble());
            break;
        }
        case MeshFilterParameterType::String: {
            if (auto *w = qobject_cast<QLineEdit *>(editor))
                w->setText(value.toString());
            break;
        }
        case MeshFilterParameterType::FileOpen: {
            if (auto *w = dynamic_cast<FilePathEditor *>(editor))
                w->setValue(value.toString());
            break;
        }
        case MeshFilterParameterType::FileSave: {
            if (auto *w = dynamic_cast<FilePathEditor *>(editor))
                w->setValue(value.toString());
            break;
        }
        case MeshFilterParameterType::TextureRef: {
            if (auto *w = dynamic_cast<TextureRefEditor *>(editor))
                w->setValue(value.toInt());
            break;
        }
        case MeshFilterParameterType::TextureOutputRef: {
            if (auto *w = dynamic_cast<TextureOutputRefEditor *>(editor))
                w->setValue(value);
            break;
        }
        case MeshFilterParameterType::Enum: {
            if (auto *w = qobject_cast<QComboBox *>(editor)) {
                const QString enumId = value.toString();
                const int pos = w->findData(enumId);
                if (pos >= 0)
                    w->setCurrentIndex(pos);
            }
            break;
        }
        case MeshFilterParameterType::Color: {
            const QColor fallback = colorFromVariant(editor->property("filterColor"), QColor(Qt::white));
            const QColor c = colorFromVariant(value, fallback);
            editor->setProperty("filterColor", c);
            updateColorButtonStyle(editor, c);
            break;
        }
        case MeshFilterParameterType::Point3f: {
            if (auto *w = dynamic_cast<Point3fEditor *>(editor)) {
                const QVector3D v = (value.userType() == QMetaType::QVector3D)
                    ? value.value<QVector3D>()
                    : QVector3D(0.0f, 0.0f, 0.0f);
                w->setValue(v);
            }
            break;
        }
        }
    }
    refreshDependentParameterEditors();
}

void MeshFilterPanel::cacheCurrentFilterParameters()
{
    if (m_currentFilterKey.trimmed().isEmpty())
        return;
    m_filterParameterCache.insert(m_currentFilterKey, collectCurrentParameterValues());
}

QVariant MeshFilterPanel::parameterValue(const ParameterBinding &binding) const
{
    QWidget *editor = binding.editor;
    if (!editor)
        return {};
    switch (binding.descriptor.type) {
    case MeshFilterParameterType::Bool:
        return qobject_cast<QCheckBox *>(editor)->isChecked();
    case MeshFilterParameterType::Int:
        return qobject_cast<QSpinBox *>(editor)->value();
    case MeshFilterParameterType::Mesh:
        return qobject_cast<QComboBox *>(editor)->currentData().toInt();
    case MeshFilterParameterType::Double:
        return qobject_cast<QDoubleSpinBox *>(editor)->value();
    case MeshFilterParameterType::AbsPerc:
        return dynamic_cast<AbsPercEditor *>(editor)->absoluteValue();
    case MeshFilterParameterType::String:
        return qobject_cast<QLineEdit *>(editor)->text();
    case MeshFilterParameterType::FileOpen:
        return dynamic_cast<FilePathEditor *>(editor)->value();
    case MeshFilterParameterType::FileSave:
        return dynamic_cast<FilePathEditor *>(editor)->value();
    case MeshFilterParameterType::TextureRef:
        return dynamic_cast<TextureRefEditor *>(editor)->value();
    case MeshFilterParameterType::TextureOutputRef:
        return dynamic_cast<TextureOutputRefEditor *>(editor)->value();
    case MeshFilterParameterType::Enum:
        return qobject_cast<QComboBox *>(editor)->currentData().toString();
    case MeshFilterParameterType::Color:
        return colorFromVariant(editor->property("filterColor"), QColor(Qt::white));
    case MeshFilterParameterType::Point3f:
        return QVariant::fromValue(dynamic_cast<Point3fEditor *>(editor)->value());
    }
    return {};
}

QColor MeshFilterPanel::colorFromVariant(const QVariant &value, const QColor &fallback) const
{
    if (value.userType() == QMetaType::QColor) {
        const QColor c = value.value<QColor>();
        return c.isValid() ? c : fallback;
    }
    const QString s = value.toString().trimmed();
    if (s.isEmpty())
        return fallback;
    const QColor c(s);
    return c.isValid() ? c : fallback;
}

const MeshFilterPanel::ParameterBinding *MeshFilterPanel::bindingById(const QString &parameterId) const
{
    const QString trimmed = parameterId.trimmed();
    if (trimmed.isEmpty())
        return nullptr;
    for (const ParameterBinding &binding : m_parameterBindings) {
        if (binding.descriptor.id == trimmed)
            return &binding;
    }
    return nullptr;
}

void MeshFilterPanel::updateColorButtonStyle(QWidget *button, const QColor &color) const
{
    if (!button)
        return;
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; border: 1px solid palette(mid); min-height: 24px; }")
        .arg(color.name(QColor::HexRgb)));
}

const Document::FilterInfo *MeshFilterPanel::filterByKey(const QString &filterKey) const
{
    if (filterKey.trimmed().isEmpty())
        return nullptr;
    for (const auto &info : m_filters) {
        if (info.key == filterKey)
            return &info;
    }
    return nullptr;
}

void MeshFilterPanel::showSearchResultsFromUi(bool focusSearch)
{
    if (m_stack && m_stack->currentWidget() == m_parametersPage)
        cacheCurrentFilterParameters();
    showSearchResults();
    if (focusSearch && m_searchEdit)
        m_searchEdit->setFocus(Qt::OtherFocusReason);
}

bool MeshFilterPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_searchEdit && event) {
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::FocusIn) {
            showSearchResultsFromUi(false);
        }
    }
    if (watched == m_searchEdit && event && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Down && keyEvent->modifiers() == Qt::NoModifier) {
            showSearchResultsFromUi(false);
            if (m_resultsList && m_resultsList->count() > 0) {
                if (m_resultsList->currentRow() < 0)
                    m_resultsList->setCurrentRow(0);
                m_resultsList->setFocus(Qt::OtherFocusReason);
            }
            return true;
        }
    }
    if (watched == m_resultsList && event && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const int key = keyEvent->key();
        if ((key == Qt::Key_Return || key == Qt::Key_Enter)
            && keyEvent->modifiers() == Qt::NoModifier) {
            openSelectedResult(true);
            return true;
        }
        if (key == Qt::Key_Up && keyEvent->modifiers() == Qt::NoModifier
            && m_resultsList->currentRow() <= 0) {
            if (m_searchEdit)
                m_searchEdit->setFocus(Qt::OtherFocusReason);
            return true;
        }
        if (key == Qt::Key_Escape) {
            showSearchResultsFromUi(true);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
