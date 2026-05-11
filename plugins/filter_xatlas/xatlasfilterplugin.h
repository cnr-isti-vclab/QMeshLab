#pragma once

#include "meshfilterplugin.h"
#include "filterparam.h"

class MeshFilterPluginManager;

class XAtlasFilterPlugin final : public MeshFilterPlugin
{
public:
    QString pluginId() const override;
    QString name() const override;
    MeshFilterRunResult runFilter(
        const QString &filterId,
        const FilterParams &params,
        Document &doc) const override;
};

void registerXAtlasFilterPlugin(MeshFilterPluginManager &pluginManager);
