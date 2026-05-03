#pragma once

#include "vcgmesh.h"
#include <QString>
#include <QStringList>
#include <vector>

struct MeshIOMaterialTextureRef
{
    QString fileName;
    QString filePath;

    bool isValid() const
    {
        return !fileName.trimmed().isEmpty() || !filePath.trimmed().isEmpty();
    }
};

struct MeshIOMaterialSlot
{
    QString name;
    MeshIOMaterialTextureRef baseColorTexture;
    MeshIOMaterialTextureRef normalTexture;
    MeshIOMaterialTextureRef occlusionTexture;
    MeshIOMaterialTextureRef roughnessTexture;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    float roughnessFactor = 1.0f;
};

struct MeshIOMaterialSet
{
    std::vector<MeshIOMaterialSlot> entries;

    bool empty() const { return entries.empty(); }
    void clear() { entries.clear(); }
};

struct MeshIOSaveOptions
{
    // vcg::tri::io::Mask bits to export.
    int mask = 0;
    // Preferred binary/text mode when format supports it.
    bool binary = true;
    // Optional hint used by formats supporting embedded textures (e.g. glTF/glb).
    bool embedTextures = false;
    // Optional hint used by formats supporting external texture references (e.g. PLY TextureFile comments).
    bool copyAssociatedTextures = false;
    // Optional hint for formats supporting Draco geometry compression (e.g. glTF/glb).
    bool dracoCompression = false;
    // Compression level in [0, 10], where 10 prefers smaller output over speed.
    int dracoCompressionLevel = 7;
};

// Abstract interface for a mesh I/O plugin.
// Implement this to add support for new file formats.
class MeshIOPlugin
{
public:
    virtual ~MeshIOPlugin() = default;

    // Stable plugin identifier used for preferences/settings (non-localized).
    virtual QString pluginId() const = 0;

    // Human-readable plugin name.
    virtual QString name() const = 0;

    // Supported file extensions (lowercase, without leading dot), e.g. {"ply", "obj"}.
    virtual QStringList supportedExtensions() const = 0;

    // Returns true if this plugin can handle the given filename (by extension).
    virtual bool canLoad(const QString &filename) const = 0;

    // Loads the mesh from filename into mesh.
    // The optional callback cb follows vcg::CallBackPos convention for progress reporting.
    // If outLoadMask is non-null, the plugin can store vcg::tri::io::Mask bits describing
    // which attributes were found in the input file.
    // Returns 0 on success, a non-zero error code otherwise.
    virtual int load(const QString &filename, VCGMesh &mesh, vcg::CallBackPos *cb, int *outLoadMask) const = 0;

    // Optional extended load path for material channel metadata (PBR-related texture channels).
    // Default behavior keeps backward compatibility: clear metadata and fallback to load().
    virtual int load(
        const QString &filename,
        VCGMesh &mesh,
        vcg::CallBackPos *cb,
        int *outLoadMask,
        MeshIOMaterialSet *outMaterialSet) const
    {
        if (outMaterialSet)
            outMaterialSet->clear();
        return load(filename, mesh, cb, outLoadMask);
    }

    // Qt file dialog filter string for this plugin's formats, e.g. "Mesh Files (*.ply *.obj)".
    virtual QString filterString() const = 0;

    // Human-readable error message for a given error code returned by load().
    virtual QString errorString(int errCode) const = 0;

    // Returns true if this plugin can save the given filename (typically by extension).
    virtual bool canSave(const QString &filename) const
    {
        (void) filename;
        return false;
    }

    // Saves the mesh to filename using the requested save options.
    // Returns 0 on success, a non-zero error code otherwise.
    virtual int save(
        const QString &filename,
        VCGMesh &mesh,
        const MeshIOSaveOptions &options,
        vcg::CallBackPos *cb) const
    {
        (void) filename;
        (void) mesh;
        (void) options;
        (void) cb;
        return -1;
    }

    // Qt file dialog filter string for save operation, e.g. "PLY (*.ply);;OBJ (*.obj)".
    // Empty by default if plugin does not support export.
    virtual QString saveFilterString() const
    {
        return QString();
    }

    // Exportable attribute capability mask for filename's format (vcg::tri::io::Mask bits).
    // Returns 0 when unknown/unsupported.
    virtual int saveMaskCapability(const QString &filename) const
    {
        (void) filename;
        return 0;
    }
};
