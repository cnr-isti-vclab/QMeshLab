#pragma once

#include "meshfilterplugin.h"

class MeshFilterPluginManager;

// Filters built on the TrueForm geometry library (external/trueform).
// See plugins/io_trueform/UPSTREAM.md for the licensing terms and the
// Qt/oneTBB `emit` collision that every TrueForm translation unit must guard.
class TrueFormFilterPlugin final : public MeshFilterPlugin
{
public:
    QString pluginId() const override;
    QString name() const override;
    MeshFilterRunResult runFilter(
        const QString &filterId,
        const FilterParams &params,
        Document &doc) const override;
};

void registerTrueFormFilterPlugin(MeshFilterPluginManager &pluginManager);
