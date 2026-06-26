#include "pythonconsole.h"
#include "PythonHost.h"
#include "scripteditor.h"

#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>

static const char *kOutputStyle =
    "QPlainTextEdit {"
    "  background-color: #1e1e1e;"
    "  color: #d4d4d4;"
    "  border: none;"
    "}";

static const char *kInputStyle =
    "QLineEdit {"
    "  background-color: #1e1e1e;"
    "  color: #d4d4d4;"
    "  border: 1px solid #3c3c3c;"
    "  selection-background-color: #264f78;"
    "}";

static const char *kPromptStyle =
    "QLabel { color: #569cd6; background: transparent; }";

// ---------------------------------------------------------------------------

PythonConsoleWidget::PythonConsoleWidget(QWidget *parent)
    : QWidget(parent)
{
    const QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    layout->addWidget(splitter, 1);

    // ============================================================
    // Left side — Script editor
    // ============================================================
    m_scriptEditor = new ScriptEditorWidget(splitter);
    splitter->addWidget(m_scriptEditor);

    // ============================================================
    // Right side — Interactive console / script output
    // ============================================================
    auto *consolePane = new QWidget(splitter);
    auto *consoleLayout = new QVBoxLayout(consolePane);
    consoleLayout->setContentsMargins(0, 0, 0, 0);
    consoleLayout->setSpacing(2);

    // ---- Output area ----------------------------------------------
    m_output = new QPlainTextEdit(consolePane);
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(10000);
    m_output->setFont(monoFont);
    m_output->setStyleSheet(QString::fromLatin1(kOutputStyle));
    m_output->setWordWrapMode(QTextOption::NoWrap);
    consoleLayout->addWidget(m_output, 1);

    // ---- Input row ------------------------------------------------
    auto *inputRow = new QHBoxLayout();
    inputRow->setContentsMargins(2, 0, 2, 2);
    inputRow->setSpacing(4);

    m_promptLabel = new QLabel(QStringLiteral(">>>"), consolePane);
    m_promptLabel->setFont(monoFont);
    m_promptLabel->setStyleSheet(QString::fromLatin1(kPromptStyle));
    m_promptLabel->setFixedWidth(m_promptLabel->fontMetrics().horizontalAdvance(QStringLiteral("... ")));
    inputRow->addWidget(m_promptLabel);

    m_input = new QLineEdit(consolePane);
    m_input->setFont(monoFont);
    m_input->setStyleSheet(QString::fromLatin1(kInputStyle));
    m_input->setPlaceholderText(tr("Python expression or statement…"));
    m_input->installEventFilter(this);
    inputRow->addWidget(m_input, 1);

    m_clearButton = new QPushButton(tr("Clear"), consolePane);
    m_clearButton->setFixedWidth(60);
    connect(m_clearButton, &QPushButton::clicked, this, &PythonConsoleWidget::clearOutput);
    inputRow->addWidget(m_clearButton);

    consoleLayout->addLayout(inputRow);
    splitter->addWidget(consolePane);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({ 600, 420 });

    // ---- Connect to PythonHost signals ----------------------------
    PythonHost &host = PythonHost::instance();
    connect(&host, &PythonHost::outputWritten, this, &PythonConsoleWidget::appendOutput);
    connect(&host, &PythonHost::errorWritten,  this, &PythonConsoleWidget::appendError);

    // ---- Enter key submits the line -------------------------------
    connect(m_input, &QLineEdit::returnPressed, this, &PythonConsoleWidget::executeLine);
}

// ---------------------------------------------------------------------------

void PythonConsoleWidget::setScriptText(const QString &text)
{
    if (!m_scriptEditor)
        return;
    m_scriptEditor->setCode(text);
    m_scriptEditor->focusEditor();
}

void PythonConsoleWidget::setInputText(const QString &text)
{
    if (m_input) {
        m_input->setText(text);
        m_input->setFocus();
    }
}

void PythonConsoleWidget::appendOutput(const QString &text)
{
    QTextCursor cursor = m_output->textCursor();
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat fmt;
    fmt.setForeground(QColor(QStringLiteral("#d4d4d4")));
    cursor.insertText(text, fmt);
    m_output->setTextCursor(cursor);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

void PythonConsoleWidget::appendError(const QString &text)
{
    QTextCursor cursor = m_output->textCursor();
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat fmt;
    fmt.setForeground(QColor(QStringLiteral("#f44747")));
    cursor.insertText(text, fmt);
    m_output->setTextCursor(cursor);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

void PythonConsoleWidget::executeLine()
{
    const QString line = m_input->text();
    m_input->clear();

    // Echo the input line to the output area in a distinct colour.
    {
        const QString prompt = m_needsMore
            ? QStringLiteral("... ")
            : QStringLiteral(">>> ");
        QTextCursor cursor = m_output->textCursor();
        cursor.movePosition(QTextCursor::End);
        QTextCharFormat fmt;
        fmt.setForeground(QColor(QStringLiteral("#569cd6")));
        cursor.insertText(prompt + line + QLatin1Char('\n'), fmt);
        m_output->setTextCursor(cursor);
        m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
    }

    // Add non-empty lines to history (most-recent first).
    if (!line.isEmpty()) {
        if (m_history.isEmpty() || m_history.constFirst() != line)
            m_history.prepend(line);
        if (m_history.size() > 200)
            m_history.removeLast();
    }
    m_historyIndex = -1;

    if (!PythonHost::instance().isInitialized())
        return;

    const bool complete = PythonHost::instance().runLine(line);
    m_needsMore = !complete;
    setPrompt(m_needsMore);
}

void PythonConsoleWidget::clearOutput()
{
    m_output->clear();
}

void PythonConsoleWidget::setPrompt(bool needsMore)
{
    m_promptLabel->setText(needsMore ? QStringLiteral("...") : QStringLiteral(">>>"));
}

void PythonConsoleWidget::historyUp()
{
    if (m_history.isEmpty())
        return;
    m_historyIndex = qMin(m_historyIndex + 1, m_history.size() - 1);
    m_input->setText(m_history.at(m_historyIndex));
    m_input->end(false);
}

void PythonConsoleWidget::historyDown()
{
    if (m_historyIndex < 0)
        return;
    --m_historyIndex;
    if (m_historyIndex < 0)
        m_input->clear();
    else {
        m_input->setText(m_history.at(m_historyIndex));
        m_input->end(false);
    }
}

bool PythonConsoleWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        const auto *ke = static_cast<QKeyEvent *>(event);
        switch (ke->key()) {
        case Qt::Key_Up:
            historyUp();
            return true;
        case Qt::Key_Down:
            historyDown();
            return true;
        case Qt::Key_Escape:
            if (m_needsMore) {
                PythonHost::instance().resetConsole();
                m_needsMore = false;
                setPrompt(false);
                m_input->clear();
                appendOutput(QStringLiteral("\n"));
                return true;
            }
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
