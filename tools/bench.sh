#!/bin/bash
# tools/bench.sh <repo> [runs]: (make first) IRIXFM_BENCH for ~, /usr/bin, /usr/lib in icon and list view, warm QML cache.
repo=${1:-$HOME/CODE/irixfm}; runs=${2:-3}; cache=$(mktemp -d)
for d in "$HOME" /usr/bin /usr/lib; do for v in 0 1; do for i in $(seq $runs); do
  c=$(mktemp -d); mkdir -p $c/irixfm; printf '[fm]\nviewMode=%s\n' $v > $c/irixfm/irixfm.conf
  QT_QPA_PLATFORM=offscreen IRIXFM_BENCH=1 IRIXFM_ASSETS=$repo XDG_CONFIG_HOME=$c XDG_CACHE_HOME=$cache timeout 120 $repo/build/irixfm "$d" 2>&1 | grep '^bench' | sed "s|^bench:|$(basename $d) view=$v|"
  rm -rf $c
done; done; done
