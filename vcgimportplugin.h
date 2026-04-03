#pragma once

#include "meshioplugin.h"

// Built-in plugin that uses vcglib's generic Importer<VCGMesh> to load
// PLY, OBJ, STL and OFF files.
class VCGImportPlugin : public MeshIOPlugin
{
public:
    QString name() const override;
    bool canLoad(const QString &filename) const override;
    int load(const QString &filename, VCGMesh &mesh, vcg::CallBackPos *cb) const override;
    QString filterString() const override;
    QString errorString(int errCode) const override;
};
