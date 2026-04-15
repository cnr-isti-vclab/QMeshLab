#include "filterpluginregistry.h"

#include "meshfilterpluginmanager.h"

#if QMESH_PLUGIN_FILTER_BASIC_ENABLED
#include "plugins/filter_basic/basicfilterplugin.h"
#endif

void registerBuiltinMeshFilterPlugins(MeshFilterPluginManager &pluginManager)
{
#if QMESH_PLUGIN_FILTER_BASIC_ENABLED
    registerBasicFilterPlugin(pluginManager);
#endif
}
