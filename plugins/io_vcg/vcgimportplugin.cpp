#include "plugins/io_vcg/vcgimportplugin.h"

#include "meshioplugin.h"
#include "meshiopluginmanager.h"

#include <wrap/io_trimesh/import.h>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <memory>

namespace {
class VCGImportPlugin final : public MeshIOPlugin
{
public:
    QString name() const override
    {
        return QObject::tr("VCGLib Generic Importer");
    }

    bool canLoad(const QString &filename) const override
    {
        const QString ext = QFileInfo(filename).suffix().toLower();
        return ext == QLatin1String("ply")
            || ext == QLatin1String("obj")
            || ext == QLatin1String("stl")
            || ext == QLatin1String("off")
            || ext == QLatin1String("vmi");
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

    QString errorString(int errCode) const override
    {
        return QString::fromLatin1(vcg::tri::io::Importer<VCGMesh>::ErrorMsg(errCode));
    }
};
}

void registerVcgImportPlugin(MeshIOPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<VCGImportPlugin>());
}
