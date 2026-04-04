#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("QMeshLab"));
    app.setOrganizationDomain(QStringLiteral("qmeshlab.org"));
    app.setApplicationName(QStringLiteral("QMeshLab"));

    MainWindow w;
    w.show();

    return app.exec();
}
