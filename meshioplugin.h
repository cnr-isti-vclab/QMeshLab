#pragma once

#include "vcgmesh.h"
#include <QString>

// Abstract interface for a mesh I/O plugin.
// Implement this to add support for new file formats.
class MeshIOPlugin
{
public:
    virtual ~MeshIOPlugin() = default;

    // Human-readable plugin name.
    virtual QString name() const = 0;

    // Returns true if this plugin can handle the given filename (by extension).
    virtual bool canLoad(const QString &filename) const = 0;

    // Loads the mesh from filename into mesh.
    // The optional callback cb follows vcg::CallBackPos convention for progress reporting.
    // Returns 0 on success, a non-zero error code otherwise.
    virtual int load(const QString &filename, VCGMesh &mesh, vcg::CallBackPos *cb) const = 0;

    // Qt file dialog filter string for this plugin's formats, e.g. "Mesh Files (*.ply *.obj)".
    virtual QString filterString() const = 0;

    // Human-readable error message for a given error code returned by load().
    virtual QString errorString(int errCode) const = 0;
};
