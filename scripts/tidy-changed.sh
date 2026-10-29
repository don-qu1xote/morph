#!/bin/bash
# clang-tidy только по строкам, которые форк поменял относительно upstream.
# Старые предупреждения upstream не наши, а их шум прячет свои.
#   scripts/tidy-changed.sh <build-dir> [база]
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

build_dir=${1:?нужна папка сборки с compile_commands.json}
base=${2:-$(cat .upstream)}

if [ ! -f "$build_dir/compile_commands.json" ]; then
    echo "tidy: нет $build_dir/compile_commands.json, сначала make configure"
    exit 1
fi

tool=$(command -v clang-tidy-diff-18.py || command -v clang-tidy-diff.py || command -v clang-tidy-diff || true)
if [ -z "$tool" ]; then
    echo "tidy: нужен clang-tidy-diff (Linux: apt install clang-tidy, macOS: brew install llvm)"
    exit 1
fi

git diff -U0 "$base" -- 'src/*.cpp' 'tests/*.cpp' | "$tool" -p1 -path "$build_dir" -iregex '.*\.cpp' -quiet
