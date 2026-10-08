#!/usr/bin/env bash
set -euo pipefail

# Make sure we're in the same directory as this script
cd "$( dirname "${BASH_SOURCE[0]}" )/.."

while read -r LINE; do
    clear
    scripts/fasttest.sh || echo "Failed"
done < <(
    inotifywait . -r -m -q -P --includei '.(h|c)$' -e close_write,move,create --format '%w%f'
)
