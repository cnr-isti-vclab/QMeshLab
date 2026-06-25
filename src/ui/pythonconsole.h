#pragma once

#include <QWidget>
#include <QStringList>

class QPlainTextEdit;
class QLineEdit;
class QPushButton;
class QLabel;
class QTabWidget;
class ScriptEditorWidget;

// An interactive Python console widget.
//
// Displays an output area (read-only) and an input line with a >>> / ...
// prompt.  Connects to PythonHost signals so that all Python output is
// routed here.  History is navigated with Up / Down on the input line.
class PythonConsoleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PythonConsoleWidget(QWidget *parent = nullptr);

public slots:
    // Append normal Python output (white text).
    void appendOutput(const QString &text);
    // Append Python error/traceback text (red text).
    void appendError(const QString &text);
    // Set the input line text (e.g. to paste a generated call) and focus it.
    void setInputText(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void executeLine();
    void clearOutput();

private:
    void setPrompt(bool needsMore);
    void historyUp();
    void historyDown();

    QTabWidget     *m_tabs        = nullptr;
    QPlainTextEdit *m_output      = nullptr;
    QLineEdit      *m_input       = nullptr;
    QLabel         *m_promptLabel = nullptr;
    QPushButton    *m_clearButton = nullptr;
    ScriptEditorWidget *m_scriptEditor = nullptr;

    QStringList m_history;
    int  m_historyIndex = -1;
    bool m_needsMore    = false;  // true while inside a multi-line statement
};
