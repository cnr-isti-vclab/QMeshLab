#pragma once

#include "filterparam.h"
#include "meshfilterplugin.h"

class MeshFilterPluginManager;

class QSlimFilterPlugin final : public MeshFilterPlugin
{
public:
    QString pluginId() const override;
    QString name() const override;
    MeshFilterRunResult runFilter(
        const QString &filterId,
        const FilterParams &params,
        Document &doc) const override;
};

void registerQSlimFilterPlugin(MeshFilterPluginManager &pluginManager);
