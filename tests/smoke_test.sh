#!/usr/bin/env bash
# smoke_test.sh - graphmcp CLI full smoke test suite
# Usage: bash tests/smoke_test.sh [path/to/graphmcp]
# Default binary: ./bin/graphmcp (Linux) or ./bin/graphmcp.exe (Windows)

set -euo pipefail

BIN="${1:-}"
if [ -z "$BIN" ]; then
    if [ -f "./bin/graphmcp" ]; then
        BIN="./bin/graphmcp"
    elif [ -f "./bin/graphmcp.exe" ]; then
        BIN="./bin/graphmcp.exe"
    elif [ -f "./out/build/x64-Debug/graphmcp.exe" ]; then
        BIN="./out/build/x64-Debug/graphmcp.exe"
    else
        echo "ERROR: cannot find graphmcp binary. Build first or pass path as argument."
        exit 1
    fi
fi

STORE="smoke-test-store-$$"
export GRAPHMCP_STORE="$STORE"
PASSED=0
FAILED=0

cleanup() {
    rm -rf "$STORE" smoke-*.svg smoke-*.drawio smoke-*.mmd smoke-*.json 2>/dev/null || true
}
trap cleanup EXIT

pass() { PASSED=$((PASSED + 1)); printf "  \033[32m✓\033[0m %s\n" "$1"; }
fail() { FAILED=$((FAILED + 1)); printf "  \033[31m✗\033[0m %s\n" "$1"; echo "    $2"; }

run_ok() {
    local label="$1"; shift
    if "$BIN" "$@" > /dev/null 2>&1; then
        pass "$label"
    else
        fail "$label" "$BIN $*"
    fi
}

run_stdout_contains() {
    local label="$1" pattern="$2"; shift 2
    local out
    if out=$("$BIN" "$@" 2>&1); then
        if echo "$out" | grep -q "$pattern"; then
            pass "$label"
        else
            fail "$label" "output missing '$pattern': $out"
        fi
    else
        fail "$label" "command failed: $BIN $*"
    fi
}

run_fails() {
    local label="$1"; shift
    if ! "$BIN" "$@" > /dev/null 2>&1; then
        pass "$label"
    else
        fail "$label" "expected failure but passed: $BIN $*"
    fi
}

echo "=========================================="
echo "graphmcp CLI Smoke Test Suite"
echo "binary: $BIN"
echo "=========================================="

# ─── create ───────────────────────────────────────────────────
echo "[create]"
run_stdout_contains "from-mermaid file"   "created graph" create from-mermaid --file examples/example_input/flowchart.mmd --name smoke-flow --id smoke-1
run_stdout_contains "from-markdown file"  "created graph" create from-markdown --file examples/example_input/outline.md --name smoke-mind --id smoke-2
run_stdout_contains "from-csv file"       "created graph" create from-csv --file examples/example_input/orgchart.csv --name smoke-org --id smoke-3
run_stdout_contains "from-xml file"       "created graph" create from-xml --file examples/example_input/architecture.xml --name smoke-arch --id smoke-4
run_stdout_contains "from-excalidraw file" "created graph" create from-excalidraw --file examples/example_input/whiteboard_freedraw.excalidraw --name smoke-wb --id smoke-5
run_stdout_contains "from-mermaid content" "created graph" create from-mermaid --content "flowchart TD\nA[Test]-->B[Done]" --name smoke-inline --id smoke-6
run_stdout_contains "from-input auto-detect" "created graph" create from-input --file examples/example_input/flowchart.mmd --id smoke-7

# ─── store ────────────────────────────────────────────────────
echo "[store]"
run_stdout_contains "list"              "smoke-1" store list
run_stdout_contains "list --type"       "smoke-arch" store list --type architecture
run_stdout_contains "list --format json" '"graphs"' store list --format json
run_stdout_contains "show"              "smoke-flow" store show --id smoke-1
run_stdout_contains "load to stdout"    '"id"' store load --id smoke-1

# ─── graph show ───────────────────────────────────────────────
echo "[graph show]"
run_stdout_contains "show graph"        "smoke-flow" graph show smoke-1
run_stdout_contains "show node"         "Start"      graph show smoke-1 --node A
run_stdout_contains "show edge"         "from"       graph show smoke-1 --edge e1
run_stdout_contains "show --format json" '"nodes"'   graph show smoke-1 --format json

# ─── graph update ─────────────────────────────────────────────
echo "[graph update]"
run_stdout_contains "update node label" "updated node" graph update --node A --set label="New Start" smoke-1
run_stdout_contains "update node shape" "updated node" graph update --node A --set shape=diamond smoke-1
run_stdout_contains "update edge style" "updated edge" graph update --edge e1 --set style=dashed smoke-1
run_stdout_contains "update selector"   "updated"      graph update --selector "shape=rect" --set shape=round smoke-1

# ─── graph insert ─────────────────────────────────────────────
echo "[graph insert]"
run_stdout_contains "insert node" "inserted node" graph insert --node --type rect --label "Extra Step" --position 400 200 smoke-1
run_stdout_contains "insert edge" "inserted edge" graph insert --edge --from A --to B smoke-1

# ─── graph delete ─────────────────────────────────────────────
echo "[graph delete]"
run_stdout_contains "delete edge" "deleted edge" graph delete --edge e3 smoke-1

# ─── version draft ────────────────────────────────────────────
echo "[version draft]"
run_stdout_contains "draft show" "pending operations" version draft show smoke-1
run_stdout_contains "draft reset" "discarded" version draft reset smoke-1

# ─── version stage + commit ───────────────────────────────────
echo "[version stage+commit]"
# Make a change first
"$BIN" graph update --node A --set label="Staged Label" smoke-1 > /dev/null 2>&1
run_stdout_contains "stage all" "staged" version stage smoke-1
run_stdout_contains "stage show" "Staged" version stage show smoke-1
run_stdout_contains "commit" "committed smoke-1 v" version commit smoke-1 -m "smoke test commit"
run_stdout_contains "stage clear" "cleared" version stage clear smoke-1

# Make another change and commitAll
"$BIN" graph update --node B --set label="Skipped Stage" smoke-1 > /dev/null 2>&1
run_stdout_contains "commit --all" "committed" version commit smoke-1 -m "direct commit" --all

# ─── version log ──────────────────────────────────────────────
echo "[version log]"
run_stdout_contains "log" "v1" version log smoke-1
run_stdout_contains "log --limit" "v" version log smoke-1 --limit 2
run_stdout_contains "log --format json" "version" version log smoke-1 --format json
run_stdout_contains "log --format oneline" "v" version log smoke-1 --format oneline

# ─── version show ─────────────────────────────────────────────
echo "[version show]"
run_stdout_contains "show latest" "nodes" version show smoke-1
run_stdout_contains "show --version 1" "smoke test commit" version show smoke-1 --version 1

# ─── version diff ─────────────────────────────────────────────
echo "[version diff]"
run_stdout_contains "diff" "Summary" version diff smoke-1 1 2
run_stdout_contains "diff --format json" '[' version diff smoke-1 1 2 --format json

# ─── version status ───────────────────────────────────────────
echo "[version status]"
run_stdout_contains "status" "HEAD" version status smoke-1

# ─── version checkout ─────────────────────────────────────────
echo "[version checkout]"
"$BIN" version draft reset smoke-1 > /dev/null 2>&1 || true
run_stdout_contains "checkout" "HEAD moved" version checkout smoke-1 1

# ─── convert ──────────────────────────────────────────────────
echo "[convert]"
run_ok "to-svg file"     convert to-svg --file examples/example_input/flowchart.mmd --output smoke-convert.svg
run_ok "to-drawio file"  convert to-drawio --file examples/example_input/flowchart.mmd --output smoke-convert.drawio
run_ok "to-mermaid file" convert to-mermaid --file examples/example_input/outline.md --output smoke-convert.mmd
run_stdout_contains "to-url stdout" "https://mermaid.live" convert to-url --file examples/example_input/flowchart.mmd --stdout
run_stdout_contains "to-model stdout" '"nodes"' convert to-model --file examples/example_input/flowchart.mmd --stdout

# ─── export ───────────────────────────────────────────────────
echo "[export]"
run_ok "to-svg"     export to-svg --id smoke-1 --output smoke-export.svg
run_ok "to-drawio"  export to-drawio --id smoke-1 --output smoke-export.drawio
run_ok "to-mermaid" export to-mermaid --id smoke-1 --output smoke-export.mmd
run_stdout_contains "to-url" "https://mermaid.live" export to-url --id smoke-1

# ─── edit ─────────────────────────────────────────────────────
echo "[edit]"
run_stdout_contains "with-browser" "opening:" edit with-browser --id smoke-1
run_ok "with-svg" edit with-svg --id smoke-1

# ─── layout ───────────────────────────────────────────────────
echo "[layout]"
run_stdout_contains "layout auto" "layout applied" layout auto --id smoke-1
run_stdout_contains "layout auto --save" "saved" layout auto --id smoke-1 --save

# ─── validate ─────────────────────────────────────────────────
echo "[validate]"
run_stdout_contains "validate graph" "valid: no issues found" validate graph --id smoke-1
run_stdout_contains "validate input" "valid: no issues found" validate input --file examples/example_input/er.mmd --input-format mermaid
run_stdout_contains "validate strict" "valid: no issues found" validate input --file examples/example_input/flowchart.mmd --strict

# Create a broken input to test validation failure
printf 'flowchart TD\nA-->B\nC-->missing\n' > smoke-broken.mmd
run_stdout_contains "validate broken" "missing" validate input --file smoke-broken.mmd

# ─── store delete ─────────────────────────────────────────────
echo "[store delete]"
run_stdout_contains "delete --force" "deleted graph" store delete --id smoke-7 --force

# ─── edge cases ───────────────────────────────────────────────
echo "[edge-cases]"
run_fails "invalid graph id"    graph show nonexistent-graph
run_fails "invalid node ref"    graph update --node FAKE --set label=X smoke-1
run_fails "commit empty stage"  version commit smoke-1 -m "should fail"
run_fails "checkout dirty"      version checkout smoke-1 1
run_stdout_contains "help flag" "usage" --help

# ─── serve (basic protocol check) ─────────────────────────────
echo "[serve]"
SERVE_OUT=$(printf '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}\n' | "$BIN" serve 2>&1 || true)
if echo "$SERVE_OUT" | grep -q "protocolVersion"; then
    pass "serve initialize"
else
    fail "serve initialize" "$SERVE_OUT"
fi

# ─── summary ──────────────────────────────────────────────────
echo "=========================================="
printf "Results: \033[32m%d passed\033[0m, " "$PASSED"
if [ "$FAILED" -gt 0 ]; then printf "\033[31m%d failed\033[0m" "$FAILED"; else printf "0 failed"; fi
echo ""
echo "=========================================="
exit $FAILED
