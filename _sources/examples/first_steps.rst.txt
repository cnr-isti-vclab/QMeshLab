.. _first-steps:

Getting started
===============

Open the Python console in QMeshLab (Window > Python Console).  The
``ms`` variable holds a :ref:`MeshSet <meshset-api>` object linked to the
current document.

.. code-block:: python

   # Load a mesh from file
   ms.load_new_mesh("example.obj")
   print(f"Loaded mesh with {ms.mesh_number()} faces")

   # Apply a filter
   ms.meshing_remove_duplicate_vertices(TargetFaceNum=5000)

   # Access GUI state (desktop only)
   camera = mlgui.camera_state_json()
   print(camera)
