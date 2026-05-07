#include "mainwindow.h"
#include <QApplication>
#include <QIcon>
#include <QImageReader>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // Allow loading very large textures (e.g. high-res atlas images in glb/gltf).
    // Qt defaults to 256 MB decoded-image cap, which can reject valid assets.
    QImageReader::setAllocationLimit(0);
    app.setOrganizationName(QStringLiteral("QMeshLab"));
    app.setOrganizationDomain(QStringLiteral("qmeshlab.org"));
    app.setApplicationName(QStringLiteral("QMeshLab"));
    const QIcon appIcon(QStringLiteral(":/img/MeshLab_Icon_512x512.png"));
    if (!appIcon.isNull())
        app.setWindowIcon(appIcon);

    MainWindow w;
    if (!appIcon.isNull())
        w.setWindowIcon(appIcon);
    w.show();

    return app.exec();
}
