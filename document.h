#pragma once

#include "vcgmesh.h"
#include <QObject>
#include <QString>
#include <vector>
#include <memory>

class Document : public QObject
{
    Q_OBJECT
public:
    struct MeshEntry {
        QString name;
        bool visible = true;
        VCGMesh mesh;
    };

    explicit Document(QObject *parent = nullptr);

    int loadMesh(const QString &filename);
    void removeMesh(int index);

    int meshCount() const { return static_cast<int>(m_meshes.size()); }
    MeshEntry &mesh(int i) { return *m_meshes[i]; }
    const MeshEntry &mesh(int i) const { return *m_meshes[i]; }

signals:
    void meshAdded(int index);
    void meshRemoved(int index);

private:
    std::vector<std::unique_ptr<MeshEntry>> m_meshes;
};
