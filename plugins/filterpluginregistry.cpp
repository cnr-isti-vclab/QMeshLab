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
#if QMESH_PLUGIN_FILTER_CLEAN_ENABLED
#include "plugins/filter_clean/cleanfilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_MESHING_ENABLED
#include "plugins/filter_meshing/meshingfilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_SCREENED_POISSON_ENABLED
#include "plugins/filter_screened_poisson/screenedpoissonfilterplugin.h"
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
#if QMESH_PLUGIN_FILTER_CLEAN_ENABLED
    registerCleanFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_MESHING_ENABLED
    registerMeshingFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_SCREENED_POISSON_ENABLED
    registerScreenedPoissonFilterPlugin(pluginManager);
#endif
}
