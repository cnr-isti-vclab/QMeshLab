#pragma once

#include "meshfilterplugin.h"

// UV parametrization (harmonic, LSCM) backed by libigl. Implementation unit of
// filter_igl; see iglbooleans.h.
bool isIglParametrizationFilter(const QString &filterId);
MeshFilterRunResult runIglParametrizationFilter(
    const QString &filterId,
    const FilterParams &params,
    Document &doc);
