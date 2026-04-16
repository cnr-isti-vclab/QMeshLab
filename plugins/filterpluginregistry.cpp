#include "filterpluginregistry.h"

#include "meshfilterpluginmanager.h"

#if QMESH_PLUGIN_FILTER_BASIC_ENABLED
#include "plugins/filter_basic/basicfilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_FUNC_ENABLED
#include "plugins/filter_func/funcfilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_EMBREE_ENABLED
#include "plugins/filter_embree/embreefilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_SELECT_ENABLED
#include "plugins/filter_select/selectfilterplugin.h"
#endif

void registerBuiltinMeshFilterPlugins(MeshFilterPluginManager &pluginManager)
{
#if QMESH_PLUGIN_FILTER_BASIC_ENABLED
    registerBasicFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_FUNC_ENABLED
    registerFuncFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_EMBREE_ENABLED
    registerEmbreeFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_SELECT_ENABLED
    registerSelectFilterPlugin(pluginManager);
#endif
}
