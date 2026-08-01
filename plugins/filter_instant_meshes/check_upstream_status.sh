#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
reference="${repo_root}/.reference/instant-meshes"

if [[ ! -d "${reference}/.git" ]]; then
    echo "Reference clone not found: ${reference}" >&2
    echo "Clone https://github.com/wjakob/instant-meshes there first." >&2
    exit 1
fi

echo "Reference commit: $(git -C "${reference}" rev-parse HEAD)"
echo "Vendored computation files:"

while IFS= read -r file; do
    source_file="${reference}/${file#upstream/}"
    if [[ ! -f "${source_file}" ]]; then
        echo "missing upstream: ${file}"
    elif ! diff -q "${source_file}" "${script_dir}/${file}" >/dev/null; then
        echo "modified: ${file}"
        diff -u "${source_file}" "${script_dir}/${file}" || true
    else
        echo "unchanged: ${file}"
    fi
done < <(cd "${script_dir}" && find upstream -type f | sort)
