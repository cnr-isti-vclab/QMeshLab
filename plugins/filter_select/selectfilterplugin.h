#pragma once

#include "meshfilterplugin.h"

class MeshFilterPluginManager;

class SelectFilterPlugin final : public MeshFilterPlugin
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

void registerSelectFilterPlugin(MeshFilterPluginManager &pluginManager);

