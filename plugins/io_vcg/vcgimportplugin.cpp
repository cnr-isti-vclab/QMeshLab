#include "plugins/io_vcg/vcgimportplugin.h"

#include "meshioplugin.h"
#include "meshiopluginmanager.h"

#include <wrap/io_trimesh/export_obj.h>
#include <wrap/io_trimesh/export_off.h>
#include <wrap/io_trimesh/export_ply.h>
#include <wrap/io_trimesh/export_stl.h>
#include <wrap/io_trimesh/import.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <memory>

namespace {
constexpr int kErrSaveUnsupportedFormat = -1000;
constexpr int kErrSavePlyBase = -2000;
constexpr int kErrSaveObjBase = -3000;
constexpr int kErrSaveStlBase = -4000;
constexpr int kErrSaveOffBase = -5000;

QString normalizedExtension(const QString &filename)
{
    return QFileInfo(filename).suffix().trimmed().toLower();
}

bool isSupportedLoadExtension(const QString &ext)
{
    return ext == QLatin1String("ply")
        || ext == QLatin1String("obj")
        || ext == QLatin1String("stl")
        || ext == QLatin1String("off")
        || ext == QLatin1String("vmi");
}

bool isSupportedSaveExtension(const QString &ext)
{
    return ext == QLatin1String("ply")
        || ext == QLatin1String("obj")
        || ext == QLatin1String("stl")
        || ext == QLatin1String("off");
}

int encodeExporterError(int base, int exporterErr)
{
    if (exporterErr == 0)
        return 0;
    return base - exporterErr;
}

bool decodeExporterError(int errCode, int base, int *outExporterErr)
{
    if (errCode >= base || errCode <= base - 1000)
        return false;
    if (outExporterErr)
        *outExporterErr = base - errCode;
    return true;
}

int requiredGeometryMask(const VCGMesh &mesh, int capabilityMask)
{
    int mask = 0;
    if ((capabilityMask & vcg::tri::io::Mask::IOM_VERTCOORD) != 0 && mesh.VN() > 0)
        mask |= vcg::tri::io::Mask::IOM_VERTCOORD;
    if ((capabilityMask & vcg::tri::io::Mask::IOM_FACEINDEX) != 0 && mesh.FN() > 0)
        mask |= vcg::tri::io::Mask::IOM_FACEINDEX;
    if ((capabilityMask & vcg::tri::io::Mask::IOM_EDGEINDEX) != 0 && mesh.EN() > 0)
        mask |= vcg::tri::io::Mask::IOM_EDGEINDEX;
    return mask;
}

int effectiveSaveMask(const VCGMesh &mesh, int capabilityMask, int requestedMask)
{
    int mask = (requestedMask != 0) ? requestedMask : capabilityMask;
    mask &= capabilityMask;
    mask |= requiredGeometryMask(mesh, capabilityMask);

    // Prefer wedge attributes when both corner and per-vertex variants are selected.
    if ((mask & vcg::tri::io::Mask::IOM_WEDGTEXCOORD) != 0)
        mask &= ~vcg::tri::io::Mask::IOM_VERTTEXCOORD;
    if ((mask & vcg::tri::io::Mask::IOM_WEDGNORMAL) != 0)
        mask &= ~vcg::tri::io::Mask::IOM_VERTNORMAL;
    if ((mask & vcg::tri::io::Mask::IOM_WEDGCOLOR) != 0)
        mask &= ~vcg::tri::io::Mask::IOM_VERTCOLOR;
    return mask;
}

int saveMaskCapabilityForExtension(const QString &ext)
{
    if (ext == QLatin1String("ply"))
        return vcg::tri::io::ExporterPLY<VCGMesh>::GetExportMaskCapability();
    if (ext == QLatin1String("obj"))
        return vcg::tri::io::ExporterOBJ<VCGMesh>::GetExportMaskCapability();
    if (ext == QLatin1String("stl"))
        return vcg::tri::io::ExporterSTL<VCGMesh>::GetExportMaskCapability();
    if (ext == QLatin1String("off"))
        return vcg::tri::io::ExporterOFF<VCGMesh>::GetExportMaskCapability();
    return 0;
}

class VCGImportPlugin final : public MeshIOPlugin
{
public:
    QString pluginId() const override
    {
        return QStringLiteral("io_vcg");
    }

    QString name() const override
    {
        return QObject::tr("VCGLib Generic Import/Export");
    }

    QStringList supportedExtensions() const override
    {
        return {
            QStringLiteral("ply"),
            QStringLiteral("obj"),
            QStringLiteral("stl"),
            QStringLiteral("off"),
            QStringLiteral("vmi")
        };
    }

    bool canLoad(const QString &filename) const override
    {
        return isSupportedLoadExtension(normalizedExtension(filename));
    }

    bool canSave(const QString &filename) const override
    {
        return isSupportedSaveExtension(normalizedExtension(filename));
    }

    int load(const QString &filename, VCGMesh &mesh, vcg::CallBackPos *cb, int *outLoadMask) const override
    {
        int loadMask = 0;
        const QFileInfo fi(filename);
        const QString ext = fi.suffix().toLower();
        const QString previousCwd = QDir::currentPath();
        bool switchedCwd = false;

        // VCGLib OBJ importer resolves mtllib/map_Kd paths against the process cwd.
        // Temporarily switch to the OBJ directory so relative material/texture paths resolve.
        if (ext == QLatin1String("obj")) {
            const QString objDir = fi.absolutePath();
            if (!objDir.isEmpty())
                switchedCwd = QDir::setCurrent(objDir);
        }

        vcg::tri::io::Importer<VCGMesh>::LoadMask(filename.toStdString().c_str(), loadMask);
        const int err = vcg::tri::io::Importer<VCGMesh>::Open(mesh, filename.toStdString().c_str(), loadMask, cb);

        if (switchedCwd)
            QDir::setCurrent(previousCwd);

        if (outLoadMask)
            *outLoadMask = loadMask;
        return err;
    }

    QString filterString() const override
    {
        return QObject::tr("Mesh Files (*.ply *.obj *.stl *.off *.vmi)");
    }

    int save(
        const QString &filename,
        VCGMesh &mesh,
        const MeshIOSaveOptions &options,
        vcg::CallBackPos *cb) const override
    {
        const QString ext = normalizedExtension(filename);
        if (!isSupportedSaveExtension(ext))
            return kErrSaveUnsupportedFormat;

        const int capabilityMask = saveMaskCapabilityForExtension(ext);
        const int saveMask = effectiveSaveMask(mesh, capabilityMask, options.mask);
        const QByteArray encodedPath = QFile::encodeName(filename);
        const char *path = encodedPath.constData();

        if (ext == QLatin1String("ply")) {
            const int err =
                vcg::tri::io::ExporterPLY<VCGMesh>::Save(mesh, path, saveMask, options.binary, cb);
            return encodeExporterError(kErrSavePlyBase, err);
        }
        if (ext == QLatin1String("obj")) {
            const int err = vcg::tri::io::ExporterOBJ<VCGMesh>::Save(mesh, path, saveMask, cb);
            return encodeExporterError(kErrSaveObjBase, err);
        }
        if (ext == QLatin1String("stl")) {
            const int err =
                vcg::tri::io::ExporterSTL<VCGMesh>::Save(mesh, path, options.binary, saveMask);
            return encodeExporterError(kErrSaveStlBase, err);
        }
        if (ext == QLatin1String("off")) {
            const int err = vcg::tri::io::ExporterOFF<VCGMesh>::Save(mesh, path, saveMask);
            return encodeExporterError(kErrSaveOffBase, err);
        }

        return kErrSaveUnsupportedFormat;
    }

    QString saveFilterString() const override
    {
        return QObject::tr("PLY (*.ply);;Wavefront OBJ (*.obj);;STL (*.stl);;OFF (*.off)");
    }

    int saveMaskCapability(const QString &filename) const override
    {
        return saveMaskCapabilityForExtension(normalizedExtension(filename));
    }

    QString errorString(int errCode) const override
    {
        if (errCode == kErrSaveUnsupportedFormat)
            return QObject::tr("Unsupported export format");

        int exporterErr = 0;
        if (decodeExporterError(errCode, kErrSavePlyBase, &exporterErr))
            return QString::fromLatin1(vcg::tri::io::ExporterPLY<VCGMesh>::ErrorMsg(exporterErr));
        if (decodeExporterError(errCode, kErrSaveObjBase, &exporterErr))
            return QString::fromLatin1(vcg::tri::io::ExporterOBJ<VCGMesh>::ErrorMsg(exporterErr));
        if (decodeExporterError(errCode, kErrSaveStlBase, &exporterErr))
            return QString::fromLatin1(vcg::tri::io::ExporterSTL<VCGMesh>::ErrorMsg(exporterErr));
        if (decodeExporterError(errCode, kErrSaveOffBase, &exporterErr))
            return QString::fromLatin1(vcg::tri::io::ExporterOFF<VCGMesh>::ErrorMsg(exporterErr));

        return QString::fromLatin1(vcg::tri::io::Importer<VCGMesh>::ErrorMsg(errCode));
    }
};
}

void registerVcgImportPlugin(MeshIOPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<VCGImportPlugin>());
}
