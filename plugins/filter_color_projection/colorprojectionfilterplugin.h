#pragma once

#include "meshfilterplugin.h"

class MeshFilterPluginManager;

class ColorProjectionFilterPlugin final : public MeshFilterPlugin
{
public:
    QString pluginId() const override;
    QString name() const override;
    MeshFilterRunResult runFilter(
        const QString     &filterId,
        const FilterParams &params,
        Document          &doc) const override;
};

void registerColorProjectionFilterPlugin(MeshFilterPluginManager &pluginManager);
