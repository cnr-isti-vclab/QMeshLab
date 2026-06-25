#pragma once

#include <QWidget>

class QPlainTextEdit;
class QPushButton;

// A minimal Python script editor with syntax highlighting.
// Contains a multi-line editor, a Run button, and a Clear button.
// Output goes to the PythonHost (which routes to the console).
class ScriptEditorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScriptEditorWidget(QWidget *parent = nullptr);

private slots:
    void executeCode();
    void clearEditor();

private:
    QPlainTextEdit *m_editor = nullptr;
    QPushButton *m_runButton = nullptr;
};
