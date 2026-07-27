#include "meshfilterplugin.h"
#include "filterdescriptorloader.h"

namespace {

QString bibValue(QString value)
{
    QString escaped;
    escaped.reserve(value.size());
    for (const QChar character : value) {
        if (character == u'\\')
            escaped += QStringLiteral("{\\textbackslash}");
        else if (QStringLiteral("{}&%#_").contains(character))
            escaped += u'\\' + character;
        else
            escaped += character;
    }
    return escaped;
}

QString authorText(const MeshFilterReferenceAuthor &author)
{
    if (author.given.isEmpty())
        return author.family;
    if (author.family.isEmpty())
        return author.given;
    return author.given + u' ' + author.family;
}

} // namespace

QString MeshFilterReference::label() const
{
    QString result;
    if (!authors.empty())
        result = authors.front().family + (authors.size() > 1 ? QStringLiteral(" et al.") : QString());
    if (year > 0)
        result += (result.isEmpty() ? QString() : QStringLiteral(", ")) + QString::number(year);
    return result.isEmpty() ? title : result;
}

QString MeshFilterReference::doiUrl() const
{
    if (doi.isEmpty())
        return {};
    return QStringLiteral("https://doi.org/%1").arg(doi);
}

QString MeshFilterReference::webUrl() const
{
    if (url.isEmpty())
        return {};
    QString normalizedUrl = url;
    QString normalizedDoi = doiUrl();
    normalizedUrl.remove(u'/');
    normalizedDoi.remove(u'/');
    if (!normalizedDoi.isEmpty()
        && normalizedUrl.compare(normalizedDoi, Qt::CaseInsensitive) == 0)
        return {};
    return url;
}

QString MeshFilterReference::markdownCitation() const
{
    QStringList names;
    for (const MeshFilterReferenceAuthor &author : authors)
        names << authorText(author);
    QString result = names.join(QStringLiteral(", "));
    if (!result.isEmpty())
        result += QStringLiteral(". ");
    result += QStringLiteral("**%1**").arg(title);
    if (!containerTitle.isEmpty())
        result += QStringLiteral(". *%1*").arg(containerTitle);
    if (year > 0)
        result += QStringLiteral(" (%1)").arg(year);
    result += u'.';
    if (!doi.isEmpty())
        result += QStringLiteral(" [DOI](%1)").arg(doiUrl());
    if (!webUrl().isEmpty())
        result += QStringLiteral(" [Web](%1)").arg(webUrl());
    return result;
}

QString MeshFilterReference::bibTeX() const
{
    QString entryType = QStringLiteral("misc");
    if (type == QStringLiteral("article-journal"))
        entryType = QStringLiteral("article");
    else if (type == QStringLiteral("paper-conference"))
        entryType = QStringLiteral("inproceedings");
    else if (type == QStringLiteral("book"))
        entryType = QStringLiteral("book");
    else if (type == QStringLiteral("chapter"))
        entryType = QStringLiteral("incollection");

    QStringList fields;
    auto add = [&fields](const QString &name, const QString &value) {
        if (!value.isEmpty())
            fields << QStringLiteral("  %1 = {%2}").arg(name, bibValue(value));
    };
    add(QStringLiteral("title"), title);
    QStringList bibAuthors;
    for (const MeshFilterReferenceAuthor &author : authors) {
        bibAuthors << (author.given.isEmpty()
                           ? author.family
                           : QStringLiteral("%1, %2").arg(author.family, author.given));
    }
    add(QStringLiteral("author"), bibAuthors.join(QStringLiteral(" and ")));
    add(type == QStringLiteral("article-journal") ? QStringLiteral("journal")
                                                  : QStringLiteral("booktitle"),
        containerTitle);
    if (year > 0)
        add(QStringLiteral("year"), QString::number(year));
    add(QStringLiteral("volume"), volume);
    add(QStringLiteral("number"), issue);
    QString bibPages = page;
    bibPages.replace(u'-', QStringLiteral("--"));
    add(QStringLiteral("pages"), bibPages);
    add(QStringLiteral("publisher"), publisher);
    add(QStringLiteral("doi"), doi);
    add(QStringLiteral("url"), url);
    return QStringLiteral("@%1{%2,\n%3\n}")
        .arg(entryType, id, fields.join(QStringLiteral(",\n")));
}

std::vector<MeshFilterDescriptor> MeshFilterPlugin::filters(const Document &doc) const
{
    const QString resourcePath =
        QStringLiteral(":/filters/%1/filters.json").arg(pluginId());
    QString error;
    auto descriptors = FilterDescriptorLoader::load(resourcePath, error);
    if (!error.isEmpty()) {
        // Plugin did not ship filters.json or it is malformed — return empty.
        return {};
    }
    FilterDescriptorLoader::resolveSymbolicBounds(descriptors, doc);
    return descriptors;
}
