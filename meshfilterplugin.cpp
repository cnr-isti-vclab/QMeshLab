#include "meshfilterplugin.h"
#include "filterdescriptorloader.h"

std::vector<MeshFilterDescriptor> MeshFilterPlugin::filters(const Document &doc) const
{
    const QString resourcePath =
        QStringLiteral(":/filters/%1/filters.json").arg(pluginId());
    QString error;
    auto descriptors = FilterDescriptorLoader::load(resourcePath, error);
    if (!error.isEmpty()) {
        // Plugin did not ship filters.json or it is malformed — return empty.
        return {};
    }
    FilterDescriptorLoader::resolveSymbolicBounds(descriptors, doc);
    return descriptors;
}
