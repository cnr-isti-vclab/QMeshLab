#include "mainwindow.h"
#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
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
