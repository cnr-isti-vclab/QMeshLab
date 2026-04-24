#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
REFERENCE_DIR="${REPO_ROOT}/.reference/PoissonRecon/Src"
VENDORED_DIR="${SCRIPT_DIR}/Src"

if [[ ! -d "${REFERENCE_DIR}" ]]; then
  echo "Reference clone not found: ${REFERENCE_DIR}" >&2
  exit 1
fi

echo "Reference clone: ${REFERENCE_DIR}"
echo "Vendored subtree: ${VENDORED_DIR}"
echo

if git -C "${REPO_ROOT}/.reference/PoissonRecon" rev-parse HEAD >/dev/null 2>&1; then
  echo "Reference commit: $(git -C "${REPO_ROOT}/.reference/PoissonRecon" rev-parse HEAD)"
  echo
fi

echo "Changed files:"
diff -rq "${REFERENCE_DIR}" "${VENDORED_DIR}" || true
echo

echo "Unified diffs for changed files:"
while IFS= read -r relative_path; do
  echo "===== ${relative_path} ====="
  diff -u "${REFERENCE_DIR}/${relative_path}" "${VENDORED_DIR}/${relative_path}" || true
  echo
done < <(
  diff -rq "${REFERENCE_DIR}" "${VENDORED_DIR}" \
    | awk '/^Files / { ref=$2; sub("^" "'"${REFERENCE_DIR}"'/", "", ref); print ref }'
)
