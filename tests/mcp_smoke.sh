#!/usr/bin/env bash
# mcp_smoke.sh - graphmcp MCP stdio 黄金路径冒烟
# 用法: bash tests/mcp_smoke.sh [path/to/graphmcp]

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

if ! command -v jq >/dev/null 2>&1; then
    echo "error: jq is required for tests/mcp_smoke.sh" >&2
    exit 2
fi

OUT_DIR="examples/example_testout"
LOG_FILE="mcp-smoke.log"
STORE="mcp-smoke-store-$$"
TMP_REQ=".mcp-smoke.req.json"
TMP_RESP=".mcp-smoke.resp.json"
export GRAPHMCP_STORE="$STORE"

PASSED=0
FAILED=0
REPORT_ROWS=()
GID=""
CURSOR_ID=""

cleanup() {
    rm -rf "$STORE" "$TMP_REQ" "$TMP_RESP" 2>/dev/null || true
}
trap cleanup EXIT

: > "$LOG_FILE"
mkdir -p "$OUT_DIR"

pass() {
    PASSED=$((PASSED + 1))
    REPORT_ROWS+=("$1|PASS|$2")
    printf "  \033[32m✓\033[0m %s\n" "$1"
}

fail() {
    FAILED=$((FAILED + 1))
    REPORT_ROWS+=("$1|FAIL|$2")
    printf "  \033[31m✗\033[0m %s\n" "$1"
    echo "    $2"
}

rpc_call() {
    local req="$1"
    printf '%s\n' "$req" > "$TMP_REQ"
    {
        echo ">>> $req"
        "$BIN" serve < "$TMP_REQ" > "$TMP_RESP" 2>> "$LOG_FILE"
        cat "$TMP_RESP"
        echo
    } >> "$LOG_FILE"
}

extract_text() {
    jq -r '.result.content[0].text // empty' "$TMP_RESP"
}

check_eq() {
    local step="$1" actual="$2" expect="$3" note="$4"
    if [ "$actual" = "$expect" ]; then
        pass "$step" "$note"
    else
        fail "$step" "expected '$expect', got '$actual'"
    fi
}

echo "=========================================="
echo "graphmcp MCP Smoke Test Suite"
echo "binary: $BIN"
echo "=========================================="

rpc_call '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}'
PROTO="$(jq -r '.result.protocolVersion // empty' "$TMP_RESP")"
check_eq "initialize" "$PROTO" "2024-11-05" "protocolVersion=$PROTO"

rpc_call '{"jsonrpc":"2.0","method":"notifications/initialized"}'
if [ ! -s "$TMP_RESP" ]; then
    pass "notifications/initialized" "no response"
else
    fail "notifications/initialized" "notification should not produce response"
fi

rpc_call '{"jsonrpc":"2.0","id":3,"method":"tools/list"}'
TOOLS_LEN="$(jq -r '.result.tools | length' "$TMP_RESP")"
check_eq "tools/list" "$TOOLS_LEN" "24" "tools.length=$TOOLS_LEN"

rpc_call '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"graph_create","arguments":{"content":"flowchart TD\nA[Start]-->B[Done]","name":"mcp-smoke"}}}'
CREATE_TEXT="$(extract_text)"
CREATE_STATUS="$(printf '%s' "$CREATE_TEXT" | jq -r '.status // empty')"
GID="$(printf '%s' "$CREATE_TEXT" | jq -r '.id // empty')"
if [ "$CREATE_STATUS" = "created" ] && [ -n "$GID" ]; then
    pass "graph_create" "status=created id=$GID"
else
    fail "graph_create" "$CREATE_TEXT"
fi

rpc_call "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"graph_show\",\"arguments\":{\"id\":\"$GID\"}}}"
SHOW_TEXT="$(extract_text)"
if printf '%s' "$SHOW_TEXT" | jq -e '.nodeList != null' >/dev/null 2>&1; then
    pass "graph_show" "nodeList present"
else
    fail "graph_show" "$SHOW_TEXT"
fi

rpc_call "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/call\",\"params\":{\"name\":\"graph_update\",\"arguments\":{\"id\":\"$GID\",\"node\":\"A\",\"set\":\"label=MCP Smoke Updated\"}}}"
UPDATE_TEXT="$(extract_text)"
rpc_call "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{\"name\":\"graph_commit\",\"arguments\":{\"id\":\"$GID\",\"message\":\"mcp smoke commit\",\"all\":true}}}"
COMMIT_TEXT="$(extract_text)"
NEW_VER="$(printf '%s' "$COMMIT_TEXT" | jq -r '.version // 0')"
if printf '%s' "$UPDATE_TEXT" | jq -e '.status == "updated"' >/dev/null 2>&1 && [ "${NEW_VER:-0}" -ge 2 ]; then
    pass "graph_update+commit" "newVersion=$NEW_VER"
else
    fail "graph_update+commit" "update=$UPDATE_TEXT commit=$COMMIT_TEXT"
fi

rpc_call "{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"tools/call\",\"params\":{\"name\":\"graph_diff\",\"arguments\":{\"id\":\"$GID\",\"v1\":1,\"v2\":2,\"format\":\"json\"}}}"
DIFF_TEXT="$(extract_text)"
DIFF_COUNT="$(printf '%s' "$DIFF_TEXT" | jq -r '.operations | length // 0')"
if [ "${DIFF_COUNT:-0}" -ge 1 ]; then
    pass "graph_diff" "operations=$DIFF_COUNT"
else
    fail "graph_diff" "$DIFF_TEXT"
fi

rpc_call "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"tools/call\",\"params\":{\"name\":\"graph_export\",\"arguments\":{\"id\":\"$GID\",\"to\":\"svg\"}}}"
EXPORT_TEXT="$(extract_text)"
if printf '%s' "$EXPORT_TEXT" | grep -q '<svg\|written'; then
    pass "graph_export svg" "svg inline/path ok"
else
    fail "graph_export svg" "$EXPORT_TEXT"
fi

rpc_call "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"tools/call\",\"params\":{\"name\":\"graph_cursor_open\",\"arguments\":{\"id\":\"$GID\",\"target\":\"nodes\"}}}"
CURSOR_OPEN_TEXT="$(extract_text)"
CURSOR_ID="$(printf '%s' "$CURSOR_OPEN_TEXT" | jq -r '.cursor // empty')"
rpc_call "{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"tools/call\",\"params\":{\"name\":\"graph_cursor_get\",\"arguments\":{\"id\":\"$GID\",\"cursor\":\"$CURSOR_ID\"}}}"
CURSOR_GET_TEXT="$(extract_text)"
rpc_call "{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"tools/call\",\"params\":{\"name\":\"graph_cursor_move\",\"arguments\":{\"id\":\"$GID\",\"cursor\":\"$CURSOR_ID\",\"delta\":1}}}"
CURSOR_MOVE_TEXT="$(extract_text)"
rpc_call "{\"jsonrpc\":\"2.0\",\"id\":13,\"method\":\"tools/call\",\"params\":{\"name\":\"graph_cursor_close\",\"arguments\":{\"id\":\"$GID\",\"cursor\":\"$CURSOR_ID\"}}}"
CURSOR_CLOSE_TEXT="$(extract_text)"
if [ -n "$CURSOR_ID" ] &&
   printf '%s' "$CURSOR_GET_TEXT" | jq -e '.index != null' >/dev/null 2>&1 &&
   printf '%s' "$CURSOR_MOVE_TEXT" | jq -e '.index != null' >/dev/null 2>&1 &&
   printf '%s' "$CURSOR_CLOSE_TEXT" | jq -e '.closed == true' >/dev/null 2>&1; then
    pass "graph_cursor chain" "cursor=$CURSOR_ID"
else
    fail "graph_cursor chain" "open=$CURSOR_OPEN_TEXT get=$CURSOR_GET_TEXT move=$CURSOR_MOVE_TEXT close=$CURSOR_CLOSE_TEXT"
fi

rpc_call "{\"jsonrpc\":\"2.0\",\"id\":14,\"method\":\"tools/call\",\"params\":{\"name\":\"graph_delete\",\"arguments\":{\"id\":\"$GID\",\"force\":true}}}"
DELETE_TEXT="$(extract_text)"
if printf '%s' "$DELETE_TEXT" | jq -e '.status == "deleted"' >/dev/null 2>&1; then
    pass "graph_delete" "deleted $GID"
else
    fail "graph_delete" "$DELETE_TEXT"
fi

REPORT_MD="$OUT_DIR/TEST_REPORT.md"
REPORT_JSON="$OUT_DIR/TEST_REPORT.json"
export MCP_REPORT_ROWS
MCP_REPORT_ROWS=$(printf '%s\n' "${REPORT_ROWS[@]}")
python3 - "$REPORT_MD" "$REPORT_JSON" "$PASSED" "$FAILED" "$LOG_FILE" <<'PY'
import json, os, sys
report_md, report_json, passed, failed, log_file = sys.argv[1:]
passed = int(passed)
failed = int(failed)
rows = []
for row in os.environ.get("MCP_REPORT_ROWS", "").split("\n"):
    if row:
        step, status, note = row.split("|", 2)
        rows.append({"step": step, "status": status, "note": note})

section_lines = [
    "## MCP protocol smoke",
    "",
    "| step | tool/method | status | note |",
    "|------|-------------|--------|------|",
]
for r in rows:
    section_lines.append(f"| {r['step']} | {r['step']} | {r['status']} | {r['note']} |")
section_lines += [
    "",
    f"- PASS: {passed}",
    f"- FAIL: {failed}",
    f"- log: {log_file}",
]
section = "\n".join(section_lines)

if os.path.exists(report_md):
    with open(report_md, "r", encoding="utf-8") as f:
        md = f.read().rstrip() + "\n\n" + section + "\n"
else:
    md = "# MCP test report\n\n" + section + "\n"
with open(report_md, "w", encoding="utf-8", newline="\n") as f:
    f.write(md)

if os.path.exists(report_json):
    with open(report_json, "r", encoding="utf-8") as f:
        data = json.load(f)
else:
    data = {}
data["mcp"] = {
    "passed": passed,
    "failed": failed,
    "log": log_file,
    "steps": rows,
}
with open(report_json, "w", encoding="utf-8", newline="\n") as f:
    json.dump(data, f, ensure_ascii=False, indent=2)
    f.write("\n")
PY

echo "=========================================="
printf "Results: \033[32m%d passed\033[0m, " "$PASSED"
if [ "$FAILED" -gt 0 ]; then printf "\033[31m%d failed\033[0m" "$FAILED"; else printf "0 failed"; fi
echo ""
echo "=========================================="

if [ "$FAILED" -eq 0 ]; then
    exit 0
fi
exit 1
