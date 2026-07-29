#pragma once

#include "vcgmesh.h"

#include <Eigen/Dense>
#include <QMatrix4x4>
#include <QString>
#include <vector>

namespace qmeshlab::libigl {

using VertexMatrix = Eigen::Matrix<double, Eigen::Dynamic, 3, Eigen::RowMajor>;
using FaceMatrix = Eigen::Matrix<int, Eigen::Dynamic, 3, Eigen::RowMajor>;

struct EigenMesh
{
    VertexMatrix vertices;
    FaceMatrix faces;
    std::vector<int> vertexToSourceIndex;
    std::vector<int> faceToSourceIndex;
    int skippedFaces = 0;
};

bool meshToEigen(
    const VCGMesh &mesh,
    EigenMesh &out,
    QString &error,
    const QMatrix4x4 *transform = nullptr);

bool eigenToMesh(
    const VertexMatrix &vertices,
    const FaceMatrix &faces,
    VCGMesh &out,
    QString &error);

} // namespace qmeshlab::libigl
