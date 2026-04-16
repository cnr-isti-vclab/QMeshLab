#pragma once

#include "meshfilterplugin.h"

class MeshFilterPluginManager;

class CleanFilterPlugin final : public MeshFilterPlugin
{
public:
    QString pluginId() const override;
    QString name() const override;
    std::vector<MeshFilterDescriptor> filters(const Document &doc) const override;
    MeshFilterRunResult runFilter(
        const QString &filterId,
        const MeshFilterParameterValues &parameters,
        Document &doc) const override;
};

void registerCleanFilterPlugin(MeshFilterPluginManager &pluginManager);

