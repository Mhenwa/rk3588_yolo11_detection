#!/usr/bin/env bash
set -euo pipefail

repo_root="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
status=0

is_code_file() {
    case "$1" in
        *.h|*.hh|*.hpp|*.c|*.cc|*.cpp|*.cxx) return 0 ;;
        *) return 1 ;;
    esac
}

while IFS= read -r -d '' file; do
    rel="${file#"$repo_root"/}"
    from_mod="$(echo "$rel" | sed -E 's#^src/modules/([^/]+)/.*#\1#')"

    while IFS=: read -r line_no line_text; do
        [[ -z "$line_no" ]] && continue
        to_mod="$(echo "$line_text" | sed -E 's#.*"modules/([^/]+)/.*#\1#')"
        if [[ "$to_mod" != "$from_mod" ]]; then
            echo "[module-boundary] $rel:$line_no cross-module include is forbidden: $line_text" >&2
            status=1
        fi
    done < <(grep -nE '^[[:space:]]*#include[[:space:]]*"modules/' "$file" || true)

    while IFS=: read -r line_no line_text; do
        [[ -z "$line_no" ]] && continue
        echo "[module-boundary] $rel:$line_no include app/* from modules is forbidden: $line_text" >&2
        status=1
    done < <(grep -nE '^[[:space:]]*#include[[:space:]]*"app/' "$file" || true)
done < <(find "$repo_root/src/modules" -type f -print0)

while IFS= read -r -d '' file; do
    if ! is_code_file "$file"; then
        continue
    fi
    rel="${file#"$repo_root"/}"
    while IFS=: read -r line_no line_text; do
        [[ -z "$line_no" ]] && continue
        echo "[module-boundary] $rel:$line_no include modules/* from core/types is forbidden: $line_text" >&2
        status=1
    done < <(grep -nE '^[[:space:]]*#include[[:space:]]*"modules/' "$file" || true)
done < <(find "$repo_root/src/core/types" -type f -print0)

exit "$status"
