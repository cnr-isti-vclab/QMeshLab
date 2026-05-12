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
#if QMESH_PLUGIN_FILTER_CGAL_ENABLED
#include "plugins/filter_cgal/cgalfilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_SCREENED_POISSON_ENABLED
#include "plugins/filter_screened_poisson/screenedpoissonfilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_SAMPLING_ENABLED
#include "plugins/filter_sampling/samplingfilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_UNSHARP_ENABLED
#include "plugins/filter_unsharp/unsharpfilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_CREATE_ENABLED
#include "plugins/filter_create/createfilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_GEODESIC_ENABLED
#include "plugins/filter_geodesic/geodesicfilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_TEXTURE_ENABLED
#include "plugins/filter_texture/texturefilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_TEXTURE_DEFRAGMENTATION_ENABLED
#include "plugins/filter_texture_defragmentation/texturedefragfilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_MEASURE_ENABLED
#include "plugins/filter_measure/measurefilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_MLS_ENABLED
#include "plugins/filter_mls/mlsfilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_SAMPLE_ENABLED
#include "plugins/filter_sample/samplefilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_LAYER_ENABLED
#include "plugins/filter_layer/layerfilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_COLORPROC_ENABLED
#include "plugins/filter_colorproc/colorprocfilterplugin.h"
#endif
#if QMESH_PLUGIN_FILTER_XATLAS_ENABLED
#include "plugins/filter_xatlas/xatlasfilterplugin.h"
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
#if QMESH_PLUGIN_FILTER_CGAL_ENABLED
    registerCgalFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_SCREENED_POISSON_ENABLED
    registerScreenedPoissonFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_SAMPLING_ENABLED
    registerSamplingFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_UNSHARP_ENABLED
    registerUnsharpFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_CREATE_ENABLED
    registerCreateFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_GEODESIC_ENABLED
    registerGeodesicFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_TEXTURE_ENABLED
    registerTextureFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_TEXTURE_DEFRAGMENTATION_ENABLED
    registerTextureDefragFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_MEASURE_ENABLED
    registerMeasureFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_MLS_ENABLED
    registerMlsFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_SAMPLE_ENABLED
    registerSampleFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_LAYER_ENABLED
    registerLayerFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_COLORPROC_ENABLED
    registerColorProcFilterPlugin(pluginManager);
#endif
#if QMESH_PLUGIN_FILTER_XATLAS_ENABLED
    registerXAtlasFilterPlugin(pluginManager);
#endif
}
