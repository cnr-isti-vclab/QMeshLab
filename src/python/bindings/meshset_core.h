#pragma once

#include <nanobind/nanobind.h>

#include <QString>
#include <string>
#include <vector>

class Document;

struct FilterInfoRecord
{
    std::string key;
    std::string id;
    std::string plugin_id;
    std::string plugin_name;
    std::string name;
    std::string python_name;   // snake_case Python identifier, e.g. "meshing_remove_duplicate_vertices"
    bool applicable = true;
    std::string applicability_error;
};

struct FilterRunRecord
{
    bool success = false;
    bool document_modified = false;
    std::string error_message;
    std::vector<std::string> info_messages;
    std::vector<int> new_mesh_indices;
};

// C++ core that wraps a Document and is exposed to Python as `MeshSet`.
//
// Two construction modes:
//  - MeshSetCore()          — standalone: creates and owns its own Document.
//                             Used by the pymeshlab2 standalone library.
//  - MeshSetCore(Document*) — embedded: borrows a live Document owned by
//                             MainWindow.  Does NOT delete the document on
//                             destruction.  Used by the in-app Python console.
class MeshSetCore
{
public:
    MeshSetCore();
    explicit MeshSetCore(Document *doc);
    ~MeshSetCore();

    int meshCount() const;
    int currentMeshIndex() const;
    void setCurrentMesh(int index);

    void loadNewMesh(const std::string &path);
    void saveCurrentMesh(const std::string &path);

    int rasterCount() const;
    int currentRasterIndex() const;
    void setCurrentRaster(int index);
    void loadRasterImage(const std::string &path);

    std::vector<FilterInfoRecord> listFilters() const;
    FilterRunRecord applyFilter(const std::string &filterNameOrKey,
                                const nanobind::dict &params) const;

private:
    QString resolveFilterKey(const QString &filterNameOrKey) const;

    Document *m_document = nullptr;
    bool m_ownsDocument = false;
};
