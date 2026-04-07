#include "meshpluginregistry.h"

#include "meshiopluginmanager.h"

#if QMESH_PLUGIN_IO_VCG_ENABLED
#include "plugins/io_vcg/vcgimportplugin.h"
#endif

#if QMESH_PLUGIN_IO_E57_ENABLED
#include "plugins/io_e57/e57importplugin.h"
#endif

#if QMESH_PLUGIN_IO_GLTF_ENABLED
#include "plugins/io_gltf/gltfimportplugin.h"
#endif

void registerBuiltinMeshPlugins(MeshIOPluginManager &pluginManager)
{
#if QMESH_PLUGIN_IO_VCG_ENABLED
    registerVcgImportPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_IO_E57_ENABLED
    registerE57ImportPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_IO_GLTF_ENABLED
    registerGltfImportPlugin(pluginManager);
#endif
}
