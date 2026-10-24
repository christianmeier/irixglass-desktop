#!/bin/bash
# fm's test suite for CI (`make check-ci`, which builds first; GitHub runs it in .github/workflows/ci.yml).
# The same tests and environment as `make test` (off-screen, throwaway XDG dirs for each test), plus:
#   - each test's stdout/stderr kept in $IRIXFM_CI_LOGS/<name>.{out,err}; stderr is shown when it fails
#   - a test fails when it exits non-zero, times out ($IRIXFM_TEST_TIMEOUT s, default 600) or prints
#     a line starting with FAIL
#   - known-flaky tests ($IRIXFM_FLAKY_TESTS, space-separated names, default network_test: its KIO
#     timing checks fail now and then) get one retry and the retry decides; the first try's FAIL lines
#     are printed as "flaky: FAIL …" so a grep for ^FAIL only sees real failures
#   - on GitHub: a log group per test, warnings/errors as annotations, a table in the job summary
# Exit status: 0 when every test passed (possibly on its retry), 1 otherwise.
set -u
cd "$(dirname "$0")/.." || exit 1

flaky=" ${IRIXFM_FLAKY_TESTS-network_test} "
logs=${IRIXFM_CI_LOGS:-$(mktemp -d)}
mkdir -p "$logs"
gh=${GITHUB_ACTIONS:-}
fm=build/irixfm
[ -x "$fm" ] || { echo "FAIL $fm missing: run make tests first"; exit 1; }

# run <name> <command…>: 0 if it exits 0 and prints no FAIL line
run() {
    local name=$1 tmp rc
    shift
    tmp=$(mktemp -d)
    XDG_CONFIG_HOME=$tmp XDG_CACHE_HOME=$tmp/cache QT_QPA_PLATFORM=offscreen \
        timeout "${IRIXFM_TEST_TIMEOUT:-600}" "$@" >"$logs/$name.out" 2>"$logs/$name.err"
    rc=$?
    rm -rf "$tmp"
    [ $rc = 124 ] && echo "FAIL $name timed out" >>"$logs/$name.out"
    grep -q '^FAIL' "$logs/$name.out" && rc=1
    return $rc
}

suite=()
for t in $(make -s --no-print-directory test-list); do suite+=("$(basename "$t")|$t"); done
suite+=("single_instance_test|tests/single_instance_test.sh $fm" "dbus_test|tests/dbus_test.sh $fm")

failed=() retried=() rows=()
pass=0 fail=0
for entry in "${suite[@]}"; do
    name=${entry%%|*}
    read -ra cmd <<<"${entry#*|}"
    if [ -n "$gh" ]; then echo "::group::$name"; else echo "== $name"; fi
    start=$SECONDS
    run "$name" "${cmd[@]}"; rc=$?
    if [ $rc != 0 ] && [[ "$flaky" == *" $name "* ]]; then
        sed 's/^FAIL/flaky: FAIL/' "$logs/$name.out"
        mv "$logs/$name.out" "$logs/$name.try1.out"; mv "$logs/$name.err" "$logs/$name.try1.err"
        echo "-- $name failed, known flaky: one retry"
        run "$name" "${cmd[@]}"; rc=$?
        retried+=("$name")
        [ $rc = 0 ] && [ -n "$gh" ] && echo "::warning title=flaky test::$name failed once and passed on its retry"
    fi
    cat "$logs/$name.out"
    secs=$((SECONDS - start))
    p=$(grep -c '^PASS' "$logs/$name.out"); f=$(grep -c '^FAIL' "$logs/$name.out")
    pass=$((pass + p)); fail=$((fail + f))
    if [ $rc != 0 ]; then
        failed+=("$name")
        echo "-- $name: stderr (last 60 lines)"; tail -n 60 "$logs/$name.err"
    fi
    [ -n "$gh" ] && echo "::endgroup::"
    status=$([ $rc = 0 ] && echo pass || echo "**FAIL**")
    [[ " ${retried[*]} " == *" $name "* ]] && status="$status (retried)"
    rows+=("| $name | $p | $f | ${secs}s | $status |")
    printf -- '-- %s: %d PASS, %d FAIL, %ds\n' "$name" "$p" "$f" "$secs"
    if [ $rc != 0 ] && [ -n "$gh" ]; then
        grep '^FAIL' "$logs/$name.out" | head -n 20 | while read -r line; do echo "::error title=$name::$line"; done
        [ "$f" = 0 ] && echo "::error title=$name::exited non-zero without a FAIL line (crash?), see its stderr"
    fi
done

echo "== total: $pass PASS, $fail FAIL${retried:+, retried: ${retried[*]}}${failed:+, failed: ${failed[*]}}"
echo "== logs in $logs"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
        echo "### fm tests: $pass PASS, $fail FAIL"
        echo
        echo "| test | PASS | FAIL | time | result |"
        echo "|---|---:|---:|---:|---|"
        printf '%s\n' "${rows[@]}"
    } >>"$GITHUB_STEP_SUMMARY"
fi
[ ${#failed[@]} = 0 ]
