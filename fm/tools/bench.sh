#!/bin/bash
# tools/bench.sh <repo> [runs] [split]: (make first) IRIXFM_BENCH for ~, /usr/bin, /usr/lib in icon and list view, warm QML cache.
# With "split", the split view's side is scrolled instead.
repo=${1:-$(cd "$(dirname "$0")/.." && pwd)}; runs=${2:-3}; mode=${3:-1}; cache=$(mktemp -d)
for d in "$HOME" /usr/bin /usr/lib; do for v in 0 1; do for _ in $(seq "$runs"); do
  c=$(mktemp -d); mkdir -p "$c/irixfm"; printf '[fm]\nviewMode=%s\nsplitViewMode=%s\n' "$v" "$v" > "$c/irixfm/irixfm.conf"
  QT_QPA_PLATFORM=offscreen IRIXFM_BENCH=$mode IRIXFM_ASSETS=$repo XDG_CONFIG_HOME=$c XDG_CACHE_HOME=$cache timeout 120 "$repo/build/irixfm" "$d" 2>&1 | grep '^bench' | sed "s|^bench:|$(basename "$d") view=$v|"
  rm -rf "$c"
done; done; done
