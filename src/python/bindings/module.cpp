#include "meshset_core.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

namespace nb = nanobind;

NB_MODULE(_qmeshlab, m)
{
    m.doc() = "QMeshLab Python bindings — MeshSet wraps the QMeshLab Document.";

    nb::class_<FilterInfoRecord>(m, "FilterInfo")
        .def_ro("key",                  &FilterInfoRecord::key)
        .def_ro("id",                   &FilterInfoRecord::id)
        .def_ro("plugin_id",            &FilterInfoRecord::plugin_id)
        .def_ro("plugin_name",          &FilterInfoRecord::plugin_name)
        .def_ro("name",                 &FilterInfoRecord::name)
        .def_ro("applicable",           &FilterInfoRecord::applicable)
        .def_ro("applicability_error",  &FilterInfoRecord::applicability_error);

    nb::class_<FilterRunRecord>(m, "FilterRunResult")
        .def_ro("success",           &FilterRunRecord::success)
        .def_ro("document_modified", &FilterRunRecord::document_modified)
        .def_ro("error_message",     &FilterRunRecord::error_message)
        .def_ro("info_messages",     &FilterRunRecord::info_messages)
        .def_ro("new_mesh_indices",  &FilterRunRecord::new_mesh_indices);

    nb::class_<MeshSetCore>(m, "MeshSet")
        .def(nb::init<>())
        .def("mesh_count",       &MeshSetCore::meshCount)
        .def("current_mesh",     &MeshSetCore::currentMeshIndex)
        .def("set_current_mesh", &MeshSetCore::setCurrentMesh, nb::arg("index"))
        .def("load_new_mesh",    &MeshSetCore::loadNewMesh,    nb::arg("path"))
        .def("save_current_mesh",&MeshSetCore::saveCurrentMesh,nb::arg("path"))
        .def("list_filters",     &MeshSetCore::listFilters)
        .def("apply_filter",     &MeshSetCore::applyFilter,
             nb::arg("filter"), nb::arg("params") = nb::dict());
}
