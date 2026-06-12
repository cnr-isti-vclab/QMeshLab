#include "meshset_core.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

namespace nb = nanobind;

namespace {

std::vector<std::string> moduleFilterList()
{
    MeshSetCore meshSet;
    return meshSet.filterList();
}

void modulePrintFilterList()
{
    nb::object print = nb::module_::import_("builtins").attr("print");
    for (const std::string &name : moduleFilterList())
        print(name);
}

void loadDefaultPlugins()
{
    // QMeshLab registers built-in plugins when the backing Document is created.
}

} // namespace

NB_MODULE(_qmeshlab, m)
{
    m.doc() = "QMeshLab Python bindings — MeshSet wraps the QMeshLab Document.";

    m.def("load_default_plugins", &loadDefaultPlugins);
    m.def("filter_list", &moduleFilterList);
    m.def("print_filter_list", &modulePrintFilterList);

    nb::class_<FilterInfoRecord>(m, "FilterInfo")
        .def_ro("key",                  &FilterInfoRecord::key)
        .def_ro("id",                   &FilterInfoRecord::id)
        .def_ro("plugin_id",            &FilterInfoRecord::plugin_id)
        .def_ro("plugin_name",          &FilterInfoRecord::plugin_name)
        .def_ro("name",                 &FilterInfoRecord::name)
        .def_ro("python_name",          &FilterInfoRecord::python_name)
        .def_ro("applicable",           &FilterInfoRecord::applicable)
        .def_ro("applicability_error",  &FilterInfoRecord::applicability_error)
        .def("__repr__", [](const FilterInfoRecord &f) {
            return "FilterInfo(python_name='" + f.python_name
                 + "', key='" + f.key
                 + "', name='" + f.name + "')";
        });

    nb::class_<FilterRunRecord>(m, "FilterRunResult")
        .def_ro("success",           &FilterRunRecord::success)
        .def_ro("document_modified", &FilterRunRecord::document_modified)
        .def_ro("error_message",     &FilterRunRecord::error_message)
        .def_ro("info_messages",     &FilterRunRecord::info_messages)
        .def_ro("new_mesh_indices",  &FilterRunRecord::new_mesh_indices);

    nb::class_<MeshSetCore>(m, "MeshSet")
        .def(nb::init<>())
        .def("__len__",           &MeshSetCore::meshCount)
        .def("mesh_count",         &MeshSetCore::meshCount)
        .def("mesh_number",        &MeshSetCore::meshCount)
        .def("number_meshes",      &MeshSetCore::meshCount)
        .def("current_mesh",       &MeshSetCore::currentMeshIndex)
        .def("current_mesh_id",    &MeshSetCore::currentMeshId)
        .def("set_current_mesh",   &MeshSetCore::setCurrentMesh,   nb::arg("index"))
        .def("mesh_id_exists",     &MeshSetCore::meshIdExists,     nb::arg("id"))
        .def("set_current_mesh_visibility",
             &MeshSetCore::setCurrentMeshVisibility, nb::arg("visibility"))
        .def("set_mesh_visibility",
             &MeshSetCore::setMeshVisibility, nb::arg("id"), nb::arg("visibility"))
        .def("is_current_mesh_visible", &MeshSetCore::isCurrentMeshVisible)
        .def("is_mesh_visible",         &MeshSetCore::isMeshVisible, nb::arg("id"))
        .def("load_new_mesh",      &MeshSetCore::loadNewMesh,       nb::arg("path"))
        .def("save_current_mesh",  &MeshSetCore::saveCurrentMesh,   nb::arg("path"))
        .def("raster_count",       &MeshSetCore::rasterCount)
        .def("raster_number",      &MeshSetCore::rasterCount)
        .def("number_rasters",     &MeshSetCore::rasterCount)
        .def("current_raster",     &MeshSetCore::currentRasterIndex)
        .def("set_current_raster", &MeshSetCore::setCurrentRaster,  nb::arg("index"))
        .def("load_raster_image",  &MeshSetCore::loadRasterImage,   nb::arg("path"))
        .def("load_new_raster",    &MeshSetCore::loadRasterImage,   nb::arg("file_name"))
        .def("clear",              &MeshSetCore::clear)
        .def("load_project",       &MeshSetCore::loadProject,       nb::arg("file_name"))
        .def("save_project",       &MeshSetCore::saveProject,       nb::arg("file_name"))
        .def("filter_list",        &MeshSetCore::filterList)
        .def("list_filters",       &MeshSetCore::listFilters)
        .def("apply_filter",       &MeshSetCore::applyFilter,
             nb::arg("filter"), nb::arg("params") = nb::dict());
}
