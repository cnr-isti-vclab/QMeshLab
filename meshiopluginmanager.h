#pragma once

#include "meshioplugin.h"
#include <memory>
#include <QStringList>
#include <vector>

// Registry of MeshIOPlugin instances.
// Plugins are checked in registration order; the first that canLoad() wins.
class MeshIOPluginManager
{
public:
    void registerPlugin(std::unique_ptr<MeshIOPlugin> plugin);

    // Returns the first registered plugin that can handle filename, or nullptr.
    const MeshIOPlugin *pluginFor(const QString &filename) const;

    // Builds the combined Qt file dialog filter from all registered plugins.
    QString openDialogFilter() const;

    // Returns one line per loaded plugin for diagnostics/UI.
    QStringList loadedPluginSummaries() const;

private:
    std::vector<std::unique_ptr<MeshIOPlugin>> m_plugins;
};
