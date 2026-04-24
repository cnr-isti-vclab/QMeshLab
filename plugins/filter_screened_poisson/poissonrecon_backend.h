#ifndef QMESH_SCREENED_POISSON_POISSONRECON_BACKEND_H
#define QMESH_SCREENED_POISSON_POISSONRECON_BACKEND_H

#include "meshfilterplugin.h"

#include <QString>
#include <QStringList>

class Document;

namespace ScreenedPoisson
{

struct BackendStatus
{
    bool vendoredSourcesPresent = false;
    QString sourceRoot;
    QString summary;
    QStringList keyEntryPoints;
};

BackendStatus inspectBackend();
QString placeholderErrorMessage();
bool isEnabledByEnvironment();
MeshFilterRunResult runSingleMeshFilter(
    Document &doc,
    const std::vector<int> &meshIndices,
    bool mergeVisible,
    const MeshFilterParameterValues &parameters);

} // namespace ScreenedPoisson

#endif
