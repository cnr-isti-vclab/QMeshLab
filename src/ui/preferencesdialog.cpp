#include "preferencesdialog.h"

#include "parameterformbuilder.h"
#include "preferences.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));

    auto *rootLayout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Changes apply immediately and are remembered between sessions."),
        this);
    intro->setStyleSheet(QStringLiteral("color: palette(mid);"));
    intro->setWordWrap(true);
    rootLayout->addWidget(intro);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *content = new QWidget(scroll);
    m_formLayout = new QFormLayout(content);
    m_formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    scroll->setWidget(content);
    rootLayout->addWidget(scroll, 1);

    Preferences &preferences = Preferences::instance();
    m_form = new ParameterFormBuilder(m_formLayout, content, this);
    // Preferences are app-wide, so the document-coupled editor types never appear
    // here; leaving the context empty makes the builder skip them if one is declared.
    m_form->setAdvancedVisible(true);
    m_form->build(preferences.descriptors(), preferences.values());

    connect(
        m_form,
        &ParameterFormBuilder::valueChanged,
        this,
        [this](const QString &parameterId) {
            Preferences::instance().setValue(parameterId, m_form->value(parameterId));
        });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *resetButton =
        buttons->addButton(tr("Restore Defaults"), QDialogButtonBox::ResetRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        Preferences &preferences = Preferences::instance();
        preferences.resetToDefaults();
        // Push the defaults back into the editors. The builder's own reset would do
        // the same, but going through the registry keeps it the single source of
        // truth and emits changed() for every consumer that is listening.
        m_form->setValues(preferences.values());
    });
    rootLayout->addWidget(buttons);

    resize(520, 460);
}
