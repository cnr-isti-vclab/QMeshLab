#pragma once

#include "meshfilterplugin.h"
#include "../../filterparam.h"

class MeshFilterPluginManager;

class EmbreeFilterPlugin final : public MeshFilterPlugin
{
public:
    QString pluginId() const override;
    QString name() const override;
    MeshFilterRunResult runFilter(
        const QString &filterId,
        const FilterParams &params,
        Document &doc) const override;
};

void registerEmbreeFilterPlugin(MeshFilterPluginManager &pluginManager);

