#include "plugins/io_vcg/vcgimportplugin.h"

#include "meshioplugin.h"
#include "meshiopluginmanager.h"

#include <wrap/io_trimesh/import.h>
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
        vcg::tri::io::Importer<VCGMesh>::LoadMask(filename.toStdString().c_str(), loadMask);
        const int err = vcg::tri::io::Importer<VCGMesh>::Open(
            mesh, filename.toStdString().c_str(), loadMask, cb);
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
