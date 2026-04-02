#include "document.h"
#include <wrap/io_trimesh/import.h>
#include <QFileInfo>

Document::Document(QObject *parent) : QObject(parent) {}

int Document::loadMesh(const QString &filename)
{
    auto entry = std::make_unique<MeshEntry>();
    int err = vcg::tri::io::Importer<VCGMesh>::Open(entry->mesh, filename.toStdString().c_str());
    if (err != 0)
        return err;
    vcg::tri::UpdateBounding<VCGMesh>::Box(entry->mesh);
    vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(entry->mesh);
    entry->name = QFileInfo(filename).fileName();
    int index = meshCount();
    m_meshes.push_back(std::move(entry));
    emit meshAdded(index);
    return 0;
}

void Document::removeMesh(int index)
{
    m_meshes.erase(m_meshes.begin() + index);
    emit meshRemoved(index);
}
