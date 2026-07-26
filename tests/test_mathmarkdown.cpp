#include <QtTest/QtTest>

#include "mathmarkdownrenderer.h"

#include <QTextBlock>
#include <QTextBrowser>
#include <QTextFragment>

namespace {

int imageCount(const QTextDocument &document)
{
    int count = 0;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it)
            count += it.fragment().charFormat().isImageFormat();
    return count;
}

} // namespace

class MathMarkdownTests : public QObject
{
    Q_OBJECT

private slots:
    void rendersInlineAndDisplayMath();
    void rendersFilterFormulaSubset();
    void ignoresCodeAndEscapedDollars();
    void preservesMalformedMath();
};

void MathMarkdownTests::rendersInlineAndDisplayMath()
{
    QTextBrowser browser;
    MathMarkdownRenderer::setMarkdown(
        browser,
        QStringLiteral("Inline $x_i^2$.\n\n$$\\frac{a}{b}$$"));
    QVERIFY2(imageCount(*browser.document()) == 2, qPrintable(browser.toHtml()));

    bool centeredFormula = false;
    for (QTextBlock block = browser.document()->begin(); block.isValid(); block = block.next())
        centeredFormula |= block.blockFormat().alignment() == Qt::AlignCenter;
    QVERIFY(centeredFormula);
}

void MathMarkdownTests::rendersFilterFormulaSubset()
{
    QTextBrowser browser;
    MathMarkdownRenderer::setMarkdown(
        browser,
        QStringLiteral(
            "$2A/L_{\\max}^2$ and $[0,\\sqrt{3}/2]$\n\n"
            "$$D_{\\mathrm{angle}}=\\frac{1}{3}\\sum_{i=1}^{3}"
            "\\frac{|\\theta_i^{UV}-\\theta_i^{3D}|}{\\theta_i^{3D}}.$$"));
    QVERIFY2(imageCount(*browser.document()) == 3, qPrintable(browser.toHtml()));
}

void MathMarkdownTests::ignoresCodeAndEscapedDollars()
{
    QTextBrowser browser;
    MathMarkdownRenderer::setMarkdown(
        browser,
        QStringLiteral("`$code$` and \\$money plus $x$"));
    QVERIFY2(imageCount(*browser.document()) == 1, qPrintable(browser.toHtml()));
    QVERIFY(browser.toPlainText().contains(QStringLiteral("$code$")));
    QVERIFY(browser.toPlainText().contains(QStringLiteral("$money")));
}

void MathMarkdownTests::preservesMalformedMath()
{
    QTextBrowser browser;
    MathMarkdownRenderer::setMarkdown(
        browser,
        QStringLiteral("An unfinished $formula remains visible."));
    QCOMPARE(imageCount(*browser.document()), 0);
    QVERIFY(browser.toPlainText().contains(QStringLiteral("$formula")));
}

QTEST_MAIN(MathMarkdownTests)
#include "test_mathmarkdown.moc"
