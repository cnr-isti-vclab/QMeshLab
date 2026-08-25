#pragma once

#include "meshfilterplugin.h"

// Per-vertex curvature and distance quantities backed by libigl. Keeping them
// in one unit avoids repeating mesh conversion, attribute transfer and result
// visualization code for closely related matrix-to-attribute filters.
bool isIglQuantityFilter(const QString &filterId);
MeshFilterRunResult runIglQuantityFilter(
    const QString &filterId,
    const FilterParams &params,
    Document &doc);
