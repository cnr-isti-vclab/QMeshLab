#pragma once

#include "meshfilterplugin.h"

class MeshFilterPluginManager;

class ColorProcFilterPlugin final : public MeshFilterPlugin
{
public:
    QString pluginId() const override;
    QString name() const override;
    MeshFilterRunResult runFilter(
        const QString &filterId,
        const FilterParams &params,
        Document &doc) const override;
};

void registerColorProcFilterPlugin(MeshFilterPluginManager &pluginManager);
