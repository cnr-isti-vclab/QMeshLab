#pragma once

#include "filterparam.h"
#include "meshfilterplugin.h"

class MeshFilterPluginManager;

class MeshFixFilterPlugin final : public MeshFilterPlugin
{
public:
    QString pluginId() const override;
    QString name() const override;
    MeshFilterRunResult runFilter(
        const QString &filterId,
        const FilterParams &params,
        Document &doc) const override;
};

void registerMeshFixFilterPlugin(MeshFilterPluginManager &pluginManager);
