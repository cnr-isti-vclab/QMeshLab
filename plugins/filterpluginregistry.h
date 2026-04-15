#pragma once

class MeshFilterPluginManager;

// Register all built-in mesh processing/filter plugins compiled in the app.
void registerBuiltinMeshFilterPlugins(MeshFilterPluginManager &pluginManager);
