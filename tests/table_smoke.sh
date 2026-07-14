#!/usr/bin/env bash
# table_smoke.sh - graphmcp table CLI 冒烟
# 用法: bash tests/table_smoke.sh [path/to/graphmcp]
# Windows 可用 Git Bash

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BIN="${1:-}"
if [ -z "$BIN" ]; then
    if [ -x "./bin/graphmcp" ]; then
        BIN="./bin/graphmcp"
    elif [ -x "./bin/graphmcp.exe" ]; then
        BIN="./bin/graphmcp.exe"
    else
        echo "error: binary not found, build first or pass path" >&2
        exit 1
    fi
fi

STORE="table-smoke-store-$$"
export GRAPHMCP_STORE="$STORE"
export GRAPHMCP_NO_LAUNCH=1
# 清除可能干扰 create 语义的 legacy 开关
unset GRAPHMCP_TABLE_CREATE_LEGACY_UPSERT 2>/dev/null || true

PASSED=0
FAILED=0
TID=""

cleanup() {
    rm -rf "$STORE" 2>/dev/null || true
    unset GRAPHMCP_TABLE_CREATE_LEGACY_UPSERT 2>/dev/null || true
}
trap cleanup EXIT

pass() { PASSED=$((PASSED + 1)); printf "  \033[32m✓\033[0m %s\n" "$1"; }
fail() { FAILED=$((FAILED + 1)); printf "  \033[31m✗\033[0m %s\n" "$1"; echo "    $2"; }

echo "=== table CLI smoke ==="
echo "binary: $BIN"
echo "store:  $STORE"
echo

# 1) table create enemy_sample
out=""
if out=$("$BIN" table create --file examples/example_input/enemy_sample.csv --name enemies --id smoke-enemies 2>&1); then
    TID="smoke-enemies"
    pass "table create enemy_sample"
else
    fail "table create enemy_sample" "$out"
fi

# 2) table show / export model
if [ -n "$TID" ]; then
    if out=$("$BIN" table show "$TID" 2>&1) && echo "$out" | grep -Fq "编号"; then
        pass "table show"
    else
        fail "table show" "$out"
    fi
    model_out="$STORE/enemies.model.json"
    if "$BIN" table export --id "$TID" --to model -o "$model_out" >/dev/null 2>&1 && [ -s "$model_out" ]; then
        pass "table export model"
    else
        fail "table export model" "missing $model_out"
    fi
fi

# 3) table from-table skill_relations
if out=$("$BIN" table from-table --file examples/example_input/skill_relations.csv --name skills 2>&1) && echo "$out" | grep -Eq 'graph .+ nodes='; then
    pass "table from-table skill_relations"
else
    fail "table from-table skill_relations" "$out"
fi

# 4) 同 id 再次 create 期望非 0；LEGACY_UPSERT=1 时期望 0
dup_out=""
if "$BIN" table create --file examples/example_input/enemy_sample.csv --id smoke-enemies --name enemies2 >/dev/null 2>&1; then
    fail "table create duplicate rejects" "expected non-zero exit"
else
    pass "table create duplicate rejects"
fi

export GRAPHMCP_TABLE_CREATE_LEGACY_UPSERT=1
if "$BIN" table create --file examples/example_input/enemy_sample.csv --id smoke-enemies --name enemies3 >/dev/null 2>&1; then
    pass "table create LEGACY_UPSERT"
else
    fail "table create LEGACY_UPSERT" "expected exit 0 with GRAPHMCP_TABLE_CREATE_LEGACY_UPSERT=1"
fi
unset GRAPHMCP_TABLE_CREATE_LEGACY_UPSERT

# 5) table check + --allowed；可选 --ignore-hint-row=false
if [ -n "$TID" ]; then
    if out=$("$BIN" table check "$TID" --allowed '{"层级":["小怪","Boss"]}' 2>&1); then
        pass "table check allowed"
    else
        fail "table check allowed" "$out"
    fi
    if "$BIN" table check "$TID" --allowed '{"层级":["小怪","Boss"]}' --ignore-hint-row=false >/dev/null 2>&1; then
        pass "table check --ignore-hint-row=false"
    else
        fail "table check --ignore-hint-row=false" "non-zero exit"
    fi
fi

echo
echo "passed=$PASSED failed=$FAILED"
if [ "$FAILED" -ne 0 ]; then
    exit 1
fi
exit 0
