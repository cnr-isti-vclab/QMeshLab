// Python.h must be included before any Qt headers to avoid macro conflicts.
#ifdef QMESHLAB_PYTHON_CONSOLE
#define PY_SSIZE_T_CLEAN
#include <Python.h>
// Forward-declare the init function for the statically-linked _qmeshlab module.
// The symbol is provided by src/python/bindings/module.cpp via nanobind NB_STATIC.
extern "C" PyObject *PyInit__qmeshlab();
#endif

#include "mainwindow.h"
#include <QApplication>
#include <QIcon>
#include <QImageReader>

int main(int argc, char *argv[])
{
#ifdef QMESHLAB_PYTHON_CONSOLE
    // Register the built-in _qmeshlab extension before Py_Initialize so that
    // `import _qmeshlab` works inside the embedded interpreter.
    // This must happen before QApplication (and certainly before PythonHost::initialize).
    PyImport_AppendInittab("_qmeshlab", &PyInit__qmeshlab);
#endif

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
