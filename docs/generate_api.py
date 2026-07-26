"""
QMeshLab Python API documentation generator.

Run from within the QMeshLab desktop app (--generate-docs flag).
The _qmeshlab module is already imported when this script executes.
"""

import os
import json
import inspect
import importlib


# ---------------------------------------------------------------------------
# Introspection helpers
# ---------------------------------------------------------------------------

def _collect_class_members(cls, excluded_names=None):
    """Return list of (name, signature_string, docstring, is_method) for *cls*."""
    excluded = set(excluded_names or [])
    members = []
    for name in dir(cls):
        if name.startswith('_') and name not in ('__init__', '__len__', '__repr__'):
            continue
        if name in excluded:
            continue
        obj = getattr(cls, name, None)
        if obj is None:
            continue
        doc = (obj.__doc__ or '').strip()
        if callable(obj):
            try:
                sig = str(inspect.signature(obj))
            except (ValueError, TypeError):
                sig = '(...)'
            members.append((name, sig, doc, True))
        else:
            members.append((name, '', doc, False))
    members.sort(key=lambda m: m[0])
    return members


def _rst_escape(text):
    """Escape special RST characters so they render as literal text."""
    for ch in '*`|\\':
        text = text.replace(ch, '\\' + ch)
    return text


def _write_methods_rst(fh, class_name, members, extra_intro=''):
    """Write a 'Methods' section and table-of-contents for *members*."""
    if extra_intro:
        fh.write(extra_intro + '\n\n')

    fh.write('.. py:currentmodule:: _qmeshlab\n\n')

    methods = [m for m in members if m[3]]
    attrs = [m for m in members if not m[3]]

    if attrs:
        fh.write('Attributes\n')
        fh.write('----------\n\n')
        for name, sig, doc, _ in attrs:
            fh.write(f'.. py:attribute:: {class_name}.{name}\n')
            fh.write('   :module: _qmeshlab\n\n')
            if doc:
                fh.write(f'   {doc}\n\n')
            else:
                fh.write('   (undocumented)\n\n')

    if methods:
        fh.write('Methods\n')
        fh.write('-------\n\n')
        for name, sig, doc, _ in methods:
            fname = f'{class_name}.{name}'
            fh.write(f'.. py:method:: {fname}{sig}\n')
            fh.write('   :module: _qmeshlab\n\n')
            if doc:
                # nanobind docstrings often have multiple lines
                for line in doc.split('\n'):
                    fh.write(f'   {_rst_escape(line)}\n')
                fh.write('\n')
            else:
                fh.write('   (undocumented)\n\n')


# ---------------------------------------------------------------------------
# Filter descriptor reader
# ---------------------------------------------------------------------------

def _read_all_filters(source_dir):
    """Walk *source_dir* (the repository root) for filters.json files.

    Returns list of dicts with keys:
      plugin_id, name, python_name, description, long_description,
      parameters (list of dicts with name, type, default, description)
    """
    filters = []
    plugins_root = os.path.join(source_dir, 'plugins')
    if not os.path.isdir(plugins_root):
        return filters

    for root, dirs, files in os.walk(plugins_root):
        for fn in files:
            if fn != 'filters.json':
                continue
            path = os.path.join(root, fn)
            try:
                with open(path, encoding='utf-8') as fh:
                    data = json.load(fh)
            except (json.JSONDecodeError, OSError):
                continue

            if isinstance(data, dict):
                entries = data.get('filters', data.get('filterDescriptors', []))
                if not isinstance(entries, list):
                    continue
                descriptor_plugin_id = data.get('pluginId', '')
            elif isinstance(data, list):
                entries = data
                descriptor_plugin_id = ''
            else:
                continue
            for entry in entries:
                pname = entry.get('pythonName', entry.get('id', '')).strip()
                if not pname:
                    continue
                params = []
                for p in entry.get('parameters', []):
                    params.append({
                        'name': p.get('id', p.get('name', '?')),
                        'type': p.get('type', 'string'),
                        'default': p.get('default', None),
                        'description': (p.get('help', p.get('description', '')) or '').strip(),
                    })
                filters.append({
                    'plugin_id': entry.get('pluginId',
                                           entry.get('plugin_id', descriptor_plugin_id)),
                    'name': entry.get('name', pname),
                    'python_name': pname,
                    'description': (entry.get('shortDescription', '') or '').strip(),
                    'long_description':
                        (entry.get('longDescriptionMarkdown', '') or '').strip(),
                    'menu': entry.get('menuPath', ''),
                    'parameters': params,
                })
    filters.sort(key=lambda f: (f['menu'], f['python_name']))
    return filters


# ---------------------------------------------------------------------------
# Reference generators
# ---------------------------------------------------------------------------

def _write_filter_markdown(fh, filter_list):
    """Write the filter reference as MyST Markdown."""
    fh.write('(filters)=\n\n')
    fh.write('# Filters\n\n')
    fh.write('All filters available in QMeshLab are listed below. Each filter can '
             'be called as a method on a `MeshSet` object, for example '
             '`ms.apply_filter("meshing_remove_duplicate_vertices", ...)`, or '
             'using the dynamically-bound snake_case name: '
             '`ms.meshing_remove_duplicate_vertices(...)`.\n\n')

    for index, f in enumerate(filter_list):
        if index:
            fh.write('---\n\n')
        anchor = f['python_name'].replace('_', '-')
        fh.write(f'(filter-{anchor})=\n\n')
        fh.write(f'## {f["name"]}\n\n')
        if f['menu']:
            fh.write(f'**Menu:** {f["menu"]}\n\n')
        if f['plugin_id']:
            fh.write(f'**Plugin:** {f["plugin_id"]}\n\n')
        if f['description']:
            fh.write(f'{f["description"]}\n\n')

        fh.write(f'```{{py:function}} ms.{f["python_name"]}(**params)\n')
        fh.write(':module: _qmeshlab\n\n')
        long_description = f['long_description']
        if long_description and long_description != f['description']:
            fh.write(f'{long_description}\n\n')
        if f['parameters']:
            fh.write('**Parameters:**\n\n')
            for p in f['parameters']:
                default_str = ''
                if p['default'] is not None:
                    default_str = f', default: `{p["default"]}`'
                fh.write(f'- **{p["name"]}** (*{p["type"]}*{default_str})')
                if p['description']:
                    fh.write(f' — {p["description"]}')
                fh.write('\n')
        else:
            fh.write('This filter has no parameters.\n')
        fh.write('```\n\n')


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def generate(output_dir):
    """Main entry point.  *output_dir* is the path where .rst files are written.

    This function is called from within the QMeshLab app after the
    `--generate-docs` flag has initialised the Python interpreter and
    imported the ``_qmeshlab`` module.
    """
    import _qmeshlab as qml

    # Locate the repository root (the output_dir is "docs" under it).
    source_dir = os.path.normpath(os.path.join(output_dir, '..'))

    # --- Read all filters.json ---
    all_filters = _read_all_filters(source_dir)
    filter_python_names = {f['python_name'] for f in all_filters}

    # --- Introspect classes ---
    meshset_members = _collect_class_members(
        qml.MeshSet,
        excluded_names=filter_python_names)
    mlgui_members = _collect_class_members(qml.MlGui)
    filter_info_members = _collect_class_members(qml.FilterInfo)
    filter_run_members = _collect_class_members(qml.FilterRunResult)

    # --- Write api/meshset.rst ---
    os.makedirs(os.path.join(output_dir, 'api'), exist_ok=True)

    with open(os.path.join(output_dir, 'api', 'meshset.rst'), 'w', encoding='utf-8') as fh:
        fh.write('.. _meshset-api:\n\n')
        fh.write('MeshSet\n')
        fh.write('=======\n\n')
        fh.write('This page documents the core ``MeshSet`` API.\n')
        fh.write('Filter-specific methods are documented in :ref:`filters` to avoid\n')
        fh.write('duplicating the full filter list here.\n\n')
        _write_methods_rst(fh, 'MeshSet', meshset_members)

    # --- Write api/mlgui.rst ---
    with open(os.path.join(output_dir, 'api', 'mlgui.rst'), 'w', encoding='utf-8') as fh:
        fh.write('.. _mlgui-api:\n\n')
        fh.write('MlGui\n')
        fh.write('=====\n\n')
        fh.write('The ``mlgui`` object is available only when running QMeshLab\n')
        fh.write('with a visible window (desktop app).  It is not available in\n')
        fh.write('headless pymeshlab.\n\n')
        _write_methods_rst(fh, 'MlGui', mlgui_members)

    # --- Write api/types.rst ---
    with open(os.path.join(output_dir, 'api', 'types.rst'), 'w', encoding='utf-8') as fh:
        fh.write('.. _types-api:\n\n')
        fh.write('Supporting Types\n')
        fh.write('================\n\n')
        _write_methods_rst(fh, 'FilterInfo', filter_info_members,
                          extra_intro='.. py:class:: FilterInfo\n\n   Describes one registered filter.\n')
        _write_methods_rst(fh, 'FilterRunResult', filter_run_members,
                          extra_intro='.. py:class:: FilterRunResult\n\n   Return value from ``apply_filter``.\n')

    # --- Write api/filters.md ---
    with open(os.path.join(output_dir, 'api', 'filters.md'), 'w', encoding='utf-8') as fh:
        _write_filter_markdown(fh, all_filters)

    old_filter_page = os.path.join(output_dir, 'api', 'filters.rst')
    if os.path.exists(old_filter_page):
        os.remove(old_filter_page)

    print(f"generate_api: wrote API files to {output_dir}/api/")
    print(f"              {len(meshset_members)} MeshSet methods, "
          f"{len(mlgui_members)} MlGui methods, "
          f"{len(all_filters)} filters")


if __name__ == '__main__':
    # Standalone test (runs outside the app, won't work because _qmeshlab
    # isn't loaded).  Use the --generate-docs app flag instead.
    print("This script runs inside the QMeshLab app via --generate-docs.")
    print("  ./QMeshLab --generate-docs <output_dir>")
