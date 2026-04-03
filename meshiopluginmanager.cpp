#include "meshiopluginmanager.h"
#include <QObject>
#include <QStringList>

void MeshIOPluginManager::registerPlugin(std::unique_ptr<MeshIOPlugin> plugin)
{
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
    for (const auto &plugin : m_plugins)
        filters << plugin->filterString();
    filters << QObject::tr("All Files (*)");
    return filters.join(QStringLiteral(";;"));
}
