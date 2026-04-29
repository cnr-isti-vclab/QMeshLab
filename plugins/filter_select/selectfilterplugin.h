#pragma once

#include "meshfilterplugin.h"
#include "../../filterparam.h"

class MeshFilterPluginManager;

class SelectFilterPlugin final : public MeshFilterPlugin
{
public:
    QString pluginId() const override;
    QString name() const override;
    MeshFilterRunResult runFilter(
        const QString &filterId,
        const FilterParams &params,
        Document &doc) const override;
};

void registerSelectFilterPlugin(MeshFilterPluginManager &pluginManager);

