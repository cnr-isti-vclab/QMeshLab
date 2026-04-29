#pragma once

#include "renderingsettings.h"
#include "viewtrackball.h"
#include <cstdint>
#include <unordered_map>

// Snapshot of all per-render-widget view state (camera, global render settings,
// per-mesh render modes). Stored inside each UndoState so that undo/redo restores
// not only document mesh data but also the view the user was looking at.
struct ViewState {
    ViewTrackball::State                                   trackball;
    RenderSettings                                         renderSettings;
    std::unordered_map<std::uint64_t, PerMeshRenderSettings> meshRenderModes;
};
