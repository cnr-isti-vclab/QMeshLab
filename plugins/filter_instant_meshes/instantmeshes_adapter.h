#pragma once

#include "vcgmesh.h"

#include <QString>

struct InstantMeshesParameters
{
    float targetEdgeLength = 0.0f;
    float creaseAngleDegrees = -1.0f;
    bool alignBoundaries = false;
    bool extrinsic = true;
    bool pureQuads = true;
    bool deterministic = false;
    int smoothingIterations = 2;
    int threads = 0;
};

bool runInstantMeshes(
    const VCGMesh &input,
    VCGMesh &output,
    const InstantMeshesParameters &parameters,
    QString &error);
