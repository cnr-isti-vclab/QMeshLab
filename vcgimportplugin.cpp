#include "vcgimportplugin.h"
#include <wrap/io_trimesh/import.h>
#include <QFileInfo>
#include <QObject>

QString VCGImportPlugin::name() const
{
    return QObject::tr("VCGLib Generic Importer");
}

bool VCGImportPlugin::canLoad(const QString &filename) const
{
    const QString ext = QFileInfo(filename).suffix().toLower();
    return ext == QLatin1String("ply")
        || ext == QLatin1String("obj")
        || ext == QLatin1String("stl")
        || ext == QLatin1String("off");
}

int VCGImportPlugin::load(const QString &filename, VCGMesh &mesh, vcg::CallBackPos *cb) const
{
    return vcg::tri::io::Importer<VCGMesh>::Open(
        mesh, filename.toStdString().c_str(), cb);
}

QString VCGImportPlugin::filterString() const
{
    return QObject::tr("Mesh Files (*.ply *.obj *.stl *.off)");
}

QString VCGImportPlugin::errorString(int errCode) const
{
    return QString::fromLatin1(
        vcg::tri::io::Importer<VCGMesh>::ErrorMsg(errCode));
}
