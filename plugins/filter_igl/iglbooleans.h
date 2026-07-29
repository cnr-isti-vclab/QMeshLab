#pragma once

#include "meshfilterplugin.h"

// Boolean operations backed by libigl. Implementation unit of filter_igl — one
// plugin per dependency (see docs/design/filter_organization.md, decision 1), so
// these are free functions rather than a plugin of their own.
bool isIglBooleanFilter(const QString &filterId);
MeshFilterRunResult runIglBooleanFilter(
    const QString &filterId,
    const FilterParams &params,
    Document &doc);
