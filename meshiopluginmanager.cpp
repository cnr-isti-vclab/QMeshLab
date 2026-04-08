#include "meshiopluginmanager.h"
#include <QObject>
#include <QRegularExpression>
#include <QStringList>

namespace {
QStringList extractExtensionsFromFilter(const QString &filter)
{
    static const QRegularExpression kExtensionPattern(QStringLiteral(R"(\*\.([A-Za-z0-9_+\-]+))"));

    QStringList extensions;
    QRegularExpressionMatchIterator matches = kExtensionPattern.globalMatch(filter);
    while (matches.hasNext()) {
        const QString ext = matches.next().captured(1).toLower();
        if (!ext.isEmpty())
            extensions << ext;
    }
    return extensions;
}
}

void MeshIOPluginManager::registerPlugin(std::unique_ptr<MeshIOPlugin> plugin)
{
    if (!plugin)
        return;

    const QStringList pluginExtensions = extractExtensionsFromFilter(plugin->filterString());
    for (const QString &ext : pluginExtensions) {
        if (!m_openableExtensions.contains(ext))
            m_openableExtensions << ext;
    }

    m_plugins.push_back(std::move(plugin));
}

const MeshIOPlugin *MeshIOPluginManager::pluginFor(const QString &filename) const
{
    for (const auto &plugin : m_plugins) {
        if (plugin->canLoad(filename))
            return plugin.get();
    }
    return nullptr;
}

QString MeshIOPluginManager::openDialogFilter() const
{
    QStringList filters;

    if (!m_openableExtensions.isEmpty()) {
        QStringList wildcardExtensions;
        wildcardExtensions.reserve(m_openableExtensions.size());
        for (const QString &ext : m_openableExtensions)
            wildcardExtensions << QStringLiteral("*.%1").arg(ext);

        filters << QObject::tr("All Supported Mesh Files (%1)")
            .arg(wildcardExtensions.join(QLatin1Char(' ')));
    }

    for (const auto &plugin : m_plugins)
        filters << plugin->filterString();
    filters << QObject::tr("All Files (*)");
    return filters.join(QStringLiteral(";;"));
}

QStringList MeshIOPluginManager::loadedPluginSummaries() const
{
    QStringList summaries;
    for (const auto &plugin : m_plugins) {
        summaries << QObject::tr("%1 - %2").arg(plugin->name(), plugin->filterString());
    }
    return summaries;
}
