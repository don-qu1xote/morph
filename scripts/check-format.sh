#!/bin/bash
# Проверяет формат только тех строк, что изменились относительно базы.
# База по умолчанию это коммит upstream из файла .upstream.
#   scripts/check-format.sh            всё, что форк поменял в upstream
#   scripts/check-format.sh --staged   то, что лежит в индексе (для pre-commit)
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

EXPECTED_VERSION="18.1.8"
actual=$(clang-format --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || true)
if [ "$actual" != "$EXPECTED_VERSION" ]; then
    echo "check-format: нужен clang-format $EXPECTED_VERSION, а стоит ${actual:-никакой}."
    echo "  Разные версии форматируют по-разному, и то, что пройдёт тут, упадёт в CI."
    echo "  macOS: brew install pipx && pipx install clang-format==$EXPECTED_VERSION"
    echo "  Linux: pip install --user clang-format==$EXPECTED_VERSION"
    exit 1
fi

if [ "${1:-}" = "--staged" ]; then
    out=$(git clang-format --staged --diff --extensions cpp,h,mm 2>&1 || true)
else
    out=$(git clang-format --diff --extensions cpp,h,mm "${1:-$(cat .upstream)}" 2>&1 || true)
fi

case "$out" in
    "" | *"no modified files to format"* | *"did not modify any files"*)
        exit 0
        ;;
esac

echo "$out"
echo
echo "check-format: строки выше не по .clang-format, поправить: make fmt"
exit 1
