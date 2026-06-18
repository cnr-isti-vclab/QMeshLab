.. _building:

Building the documentation
==========================

Prerequisites
-------------

* QMeshLab compiled with ``QMESHLAB_PYTHON_CONSOLE=ON`` (default).
* Python packages: ``sphinx`` and ``furo``.

Step 1 — Generate the API RST files
------------------------------------

.. code-block:: bash

   cmake --build build --target QMeshLab -j8
   QT_QPA_PLATFORM=offscreen ./build/QMeshLab --generate-docs docs

This introspects the ``_qmeshlab`` module from within the running app,
reads all ``filters.json`` files, and writes ``.rst`` files into
``docs/api/``.

Step 2 — Build the HTML site
-----------------------------

.. code-block:: bash

   pip install sphinx furo
   sphinx-build docs docs_build
   # open docs_build/index.html

CI pipeline
-----------

The GitHub Action at ``.github/workflows/docs.yml`` automates both steps
on every push to ``main`` and deploys the result to GitHub Pages.
