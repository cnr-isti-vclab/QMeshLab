#pragma once

#include <QWidget>
#include <QStringList>

class QPlainTextEdit;
class QLineEdit;
class QPushButton;
class QLabel;
class ScriptEditorWidget;

// An interactive Python console widget.
//
// Displays the script editor and console side by side, so script output is
// immediately visible in the read-only console output area. The input line
// supports a >>> / ... prompt and Up / Down history navigation.
class PythonConsoleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PythonConsoleWidget(QWidget *parent = nullptr);
    void setScriptText(const QString &text);

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

    QPlainTextEdit *m_output      = nullptr;
    QLineEdit      *m_input       = nullptr;
    QLabel         *m_promptLabel = nullptr;
    QPushButton    *m_clearButton = nullptr;
    ScriptEditorWidget *m_scriptEditor = nullptr;

    QStringList m_history;
    int  m_historyIndex = -1;
    bool m_needsMore    = false;  // true while inside a multi-line statement
};
