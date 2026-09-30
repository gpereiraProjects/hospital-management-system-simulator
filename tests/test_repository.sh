#!/usr/bin/env bash
set -euo pipefail

required_files=(
  AUTHORS.md
  CONTRIBUTING.md
  LICENSE
  README.md
  docs/ARCHITECTURE.md
  docs/COMMANDS.md
  docs/DEVELOPMENT.md
  docs/PROJECT_SCOPE.md
  docs/REQUIREMENTS_TRACEABILITY.md
)

for path in "${required_files[@]}"; do
  [[ -s $path ]] || {
    echo "missing required publication file: $path" >&2
    exit 1
  }
done

for link in \
  docs/ARCHITECTURE.md \
  docs/COMMANDS.md \
  docs/DEVELOPMENT.md \
  docs/PROJECT_SCOPE.md \
  docs/REQUIREMENTS_TRACEABILITY.md; do
  grep -Fq "($link)" README.md || {
    echo "README does not link to $link" >&2
    exit 1
  }
done

if git ls-files | grep -Eq '(^|/)(build|logs|results)/|(^|/)REPORT\.pdf$|\.out$|\.log$'; then
  echo 'generated or removed academic artifacts are tracked' >&2
  exit 1
fi

for path in src/*.c include/*.h; do
  if [[ -n $(tail -c 1 "$path") ]]; then
    echo "source file does not end with a newline: $path" >&2
    exit 1
  fi
done

git diff --check
echo 'repository publication hygiene: all checks passed'
