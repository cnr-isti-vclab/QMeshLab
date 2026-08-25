#include "iglfilterplugin.h"

#include "iglbooleans.h"
#include "iglparametrization.h"
#include "iglquantities.h"
#include "meshfilterpluginmanager.h"

#include <memory>

QString IglFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.igl");
}

QString IglFilterPlugin::name() const
{
    return QObject::tr("libigl Filters");
}

MeshFilterRunResult IglFilterPlugin::runFilter(
    const QString &filterId,
    const FilterParams &params,
    Document &doc) const
{
    if (isIglBooleanFilter(filterId))
        return runIglBooleanFilter(filterId, params, doc);
    if (isIglParametrizationFilter(filterId))
        return runIglParametrizationFilter(filterId, params, doc);
    if (isIglQuantityFilter(filterId))
        return runIglQuantityFilter(filterId, params, doc);

    MeshFilterRunResult result;
    result.success = false;
    result.documentModified = false;
    result.errorMessage = QObject::tr("Unknown filter id: %1").arg(filterId);
    return result;
}

void registerIglFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<IglFilterPlugin>());
}
