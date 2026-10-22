#!/bin/bash
# org.freedesktop.FileManager1 ("Show in folder" in other apps): D-Bus activation, ShowItems,
# ShowFolders, ShowItemProperties, and not fighting over the name. Off-screen, on a private bus
# (dbus-run-session with its own config), never the user's session bus. Run by `make test`.
fm=$(realpath "${1:-build/irixfm}")
here=$(cd "$(dirname "$0")/.." && pwd)

if [ -z "$IRIXFM_DBUS_TEST_INNER" ]; then
    # shellcheck disable=SC2015  # intended: fail when either command is missing
    command -v dbus-run-session >/dev/null && command -v gdbus >/dev/null \
        || { echo "FAIL dbus-run-session and gdbus are needed"; exit 1; }
    work=$(mktemp -d)
    mkdir -p "$work/home-services" "$work/system-services"
    # Two service dirs in the order of <standard_session_servicedirs/>: ~/.local/share first, then
    # /usr/share, where a stand-in for Dolphin's file claims the same name.
    cat > "$work/bus.conf" <<EOF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:dir=$work</listen>
  <auth>EXTERNAL</auth>
  <servicedir>$work/home-services</servicedir>
  <servicedir>$work/system-services</servicedir>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
EOF
    # fm as `make install` sets it up, with its stderr kept for the checks
    printf '#!/bin/sh\nexec "%s" "$@" 2>>"%s/activated.log"\n' "$fm" "$work" > "$work/irixfm"
    printf '#!/bin/sh\ntouch "%s/dolphin-started"\n' "$work" > "$work/dolphin"
    chmod +x "$work/irixfm" "$work/dolphin"
    sed "s|^Exec=irixfm|Exec=$work/irixfm|" "$here/org.freedesktop.FileManager1.service" > "$work/home-services/org.freedesktop.FileManager1.service"
    printf '[D-BUS Service]\nName=org.freedesktop.FileManager1\nExec=%s/dolphin --daemon\n' "$work" \
        > "$work/system-services/org.kde.dolphin.FileManager1.service"
    # The bus daemon passes its environment to what it starts: fm off-screen, opted in to the bus,
    # Qt's messages (the QML "selected" lines) on stderr. The checks print to stdout; the bus's and
    # fm's chatter goes to $work/*.log (KEEP=1 keeps $work for a look).
    OUTER_BUS="${DBUS_SESSION_BUS_ADDRESS:-none}" IRIXFM_DBUS_TEST_INNER=1 IRIXFM_DBUS=1 IRIXFM_ASSETS="$here" \
        QT_FORCE_STDERR_LOGGING=1 QT_QPA_PLATFORM=offscreen \
        dbus-run-session --config-file="$work/bus.conf" -- "$0" "$fm" "$work" 2>"$work/bus.log"
    rc=$?
    if [ -n "$KEEP" ]; then echo "logs in $work"; else rm -rf "$work"; fi
    exit $rc
fi

work=$2; fails=0; pids=()
check() { if [ "$1" = 0 ]; then printf 'PASS %s\n' "$2"; else printf 'FAIL %s\n' "$2"; fails=$((fails+1)); fi; }
waitfor() { for _ in $(seq 75); do grep -qF -- "$1" "$2" 2>/dev/null && return 0; sleep 0.2; done; return 1; }
# shellcheck disable=SC2329  # called by the EXIT trap
cleanup() { for p in "${pids[@]}"; do kill -TERM "$p" 2>/dev/null; done; wait 2>/dev/null; }
trap cleanup EXIT
case "$DBUS_SESSION_BUS_ADDRESS" in
    *"$work"*) [ "$DBUS_SESSION_BUS_ADDRESS" != "$OUTER_BUS" ] ;;
    *) false ;;
esac
check $? "runs on its own private session bus, not the user's"
[ $fails = 0 ] || exit 1

fm1() { gdbus call --session -t 20 -d org.freedesktop.FileManager1 -o /org/freedesktop/FileManager1 -m "org.freedesktop.FileManager1.$1" "$2" "$3"; }
ownerpid() {
    local o; o=$(gdbus call --session -d org.freedesktop.DBus -o /org/freedesktop/DBus -m org.freedesktop.DBus.GetNameOwner org.freedesktop.FileManager1 2>/dev/null | sed -E "s/^\('([^']*)',\)$/\1/")
    [ -n "$o" ] && gdbus call --session -d org.freedesktop.DBus -o /org/freedesktop/DBus -m org.freedesktop.DBus.GetConnectionUnixProcessID "$o" | sed -E 's/^\(uint32 ([0-9]+),\)$/\1/'
}
a=$work/a; b=$work/b; c=$work/c; mkdir -p "$a/sub" "$b" "$c"
touch "$a/one.txt" "$a/two.txt" "$b/three.txt" "$a/.hidden"
log=$work/activated.log

# 1. Nothing owns the name: the call starts fm through its service file (not the Dolphin stand-in).
fm1 ShowItems "['file://$a/one.txt']" "" >/dev/null; check $? "ShowItems with no fm running: the call succeeds (D-Bus activation)"
waitfor "window opened on $a" "$log"; check $? "…fm was started and opened a window on the folder"
waitfor "selected $a/one.txt" "$log"; check $? "…with the item selected"
[ ! -e "$work/dolphin-started" ]; check $? "…the ~/.local/share service file wins over Dolphin's in /usr/share"
first=$(ownerpid); [ -n "$first" ] && pids+=("$first")
[ -n "$first" ] && grep -q irixfm "/proc/$first/cmdline" 2>/dev/null && grep -q -- --dbus-activated "/proc/$first/cmdline"
check $? "…and fm (--dbus-activated) owns org.freedesktop.FileManager1"
gdbus introspect --session -d org.freedesktop.FileManager1 -o /org/freedesktop/FileManager1 | grep -c -E "ShowFolders|ShowItems\(|ShowItemProperties" | grep -qx 3
check $? "introspection lists ShowFolders, ShowItems and ShowItemProperties"

# 2. The running fm takes the calls: a new window each, items from a second folder in a tab.
n=$(grep -c "window opened on" "$log")
fm1 ShowItems "['file://$a/two.txt', '$a/sub', 'file://$b/three.txt']" "" >/dev/null; check $? "ShowItems: several items, two folders, URIs and plain paths"
waitfor "selected $a/two.txt $a/sub" "$log" || waitfor "selected $a/sub $a/two.txt" "$log"; check $? "…both items in the first folder selected"
[ "$(grep -c "window opened on" "$log")" = $((n + 1)) ]; check $? "…in one new window (the second folder is a tab of it)"
fm1 ShowFolders "['file://$b', 'file://$c']" "" >/dev/null; check $? "ShowFolders"
waitfor "window opened on $b" "$log"; check $? "…opens a window on the first folder"
fm1 ShowItemProperties "['file://$a/one.txt', 'file://$b']" "" >/dev/null; check $? "ShowItemProperties"
waitfor "info window for $a/one.txt" "$log" && waitfor "info window for $b" "$log"; check $? "…opens a Get Info window for each item"
[ "$(ownerpid)" = "$first" ]; check $? "all of it in the one fm process"
! fm1 ShowItems "['sftp://example.org/x', 'file://$work/missing']" "" >/dev/null 2>&1; check $? "nothing local to show: the caller gets an error"
fm1 ShowItems "['file://$a/.hidden']" "" >/dev/null; sleep 1.5
! grep -q "selected $a/.hidden" "$log"; check $? "a hidden file isn't selected while hidden files are off"

# 3. Don't fight over the name: a second fm waits in line, an activated one gives up.
"$fm" "$c" 2>"$work/second.log" & second=$!; pids+=("$second")
waitfor "waits in line" "$work/second.log"; check $? "a second fm doesn't take the name, it waits in line (logged)"
[ "$(ownerpid)" = "$first" ]; check $? "…the first fm keeps it"
timeout 10 "$fm" --dbus-activated 2>/dev/null; check $? "fm started for the name while it's taken exits at once"
kill -TERM "$first"; wait "$first" 2>/dev/null
for _ in $(seq 25); do [ "$(ownerpid)" = "$second" ] && break; sleep 0.2; done
[ "$(ownerpid)" = "$second" ]; check $? "when the owner quits, the fm in line gets the name"
kill -TERM "$second"; wait "$second" 2>/dev/null

# 4. Off-screen runs (tests, snapshots) stay off the bus unless they opt in.
env -u IRIXFM_DBUS "$fm" "$c" 2>/dev/null & third=$!; pids+=("$third")
sleep 2
[ -z "$(ownerpid)" ]; check $? "an off-screen fm without IRIXFM_DBUS doesn't claim the name"
kill -TERM "$third"; wait "$third" 2>/dev/null
exit $((fails > 0))
