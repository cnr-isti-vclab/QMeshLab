#pragma once

class MeshIOPluginManager;

// Register all built-in mesh I/O plugins compiled into the application.
void registerBuiltinMeshPlugins(MeshIOPluginManager &pluginManager);
