#pragma once

#include <QDialog>

class ParameterFormBuilder;
class QFormLayout;

// Editor for the application preferences declared in resources/preferences.json.
//
// Deliberately thin: ParameterFormBuilder turns the declared descriptors into
// editors, so this class only decides that edits apply immediately (no OK/Cancel)
// and offers a reset. A new preference needs no change here.
class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

private:
    ParameterFormBuilder *m_form = nullptr;
    QFormLayout *m_formLayout = nullptr;
};
