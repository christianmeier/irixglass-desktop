#!/bin/bash
# One fm process: a second launch hands its folder to the running fm (a new window there) and
# exits; Ctrl+N / New Window opens in the same process too. Off-screen; run by `make test`.
fm=${1:-build/irixfm}; fails=0
check() { if [ "$1" = 0 ]; then printf 'PASS %s\n' "$2"; else printf 'FAIL %s\n' "$2"; fails=$((fails+1)); fi; }
log=$(mktemp); a=$(mktemp -d); b=$(mktemp -d)
IRIXFM_SINGLE=1 IRIXFM_ASSETS=$PWD QT_QPA_PLATFORM=offscreen $fm "$a" 2>"$log" & first=$!
sleep 2
start=$(date +%s%N)
IRIXFM_SINGLE=1 IRIXFM_ASSETS=$PWD QT_QPA_PLATFORM=offscreen timeout 10 $fm "$b" 2>/dev/null; rc=$?
ms=$(( ($(date +%s%N) - start) / 1000000 ))
check $rc "a second launch exits at once ($ms ms)"
sleep 1
grep -q "window opened on $b" "$log"; check $? "…and the running fm opens a window on its folder"
kill -0 $first 2>/dev/null; check $? "…which is still the one process"
[ "$(pgrep -fc "$fm $b")" = 0 ]; check $? "no second fm process is left running"
t0=$(date +%s%N); kill -TERM $first; wait $first 2>/dev/null
q=$(( ($(date +%s%N) - t0) / 1000000 )); [ $q -lt 6000 ]; check $? "SIGTERM: fm quits, with its second window ($q ms)"
# after it quit, a launch starts a fresh fm (no stale socket in the way)
IRIXFM_SINGLE=1 IRIXFM_ASSETS=$PWD QT_QPA_PLATFORM=offscreen $fm "$a" 2>"$log" & again=$!
sleep 2
kill -0 $again 2>/dev/null; check $? "after fm quits, the next launch starts a new fm"
kill -TERM $again; wait $again 2>/dev/null
rm -rf "$log" "$a" "$b"
exit $((fails > 0))
