#pragma once

#include <QString>

class QTextBrowser;

namespace MathMarkdownRenderer {

void setMarkdown(QTextBrowser &browser, const QString &markdown);

} // namespace MathMarkdownRenderer
