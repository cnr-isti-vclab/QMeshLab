#pragma once

#include "meshfilterplugin.h"

class MeshFilterPluginManager;

// All libigl-backed filters. One optional dependency means one plugin, so this
// spans several categories (booleans, parametrization, and more to come) — the
// categories, not the plugin, do the user-facing organizing.
class IglFilterPlugin final : public MeshFilterPlugin
{
public:
    QString pluginId() const override;
    QString name() const override;

    MeshFilterRunResult runFilter(
        const QString &filterId,
        const FilterParams &params,
        Document &doc) const override;
};

void registerIglFilterPlugin(MeshFilterPluginManager &pluginManager);
