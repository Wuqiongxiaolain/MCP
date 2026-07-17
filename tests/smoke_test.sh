#!/usr/bin/env bash
# smoke_test.sh - graphmcp CLI full smoke test suite
# Usage: bash tests/smoke_test.sh [path/to/graphmcp]
# Default binary: ./bin/graphmcp (Linux) or ./bin/graphmcp.exe (Windows)

# 不用 set -e：断言走 pass/fail 计数，避免前置 setup 失败时跳过报告生成
set -uo pipefail

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
SVG_FALLBACK=0

cleanup() {
    rm -rf "$STORE" smoke-*.svg smoke-*.drawio smoke-*.mmd smoke-colors.mmd smoke-*.json smoke-*.png smoke-*.pdf smoke-*.excalidraw smoke-fix-* smoke-broken.xml 2>/dev/null || true
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
        if echo "$out" | grep -Fq "$pattern"; then
            pass "$label"
        else
            fail "$label" "output missing '$pattern': $out"
        fi
    else
        fail "$label" "command failed: $BIN $* | $out"
    fi
}

# run_output_contains: 无论退出码，断言输出含固定字符串（用于 validate 等非零成功场景）
run_output_contains() {
    local label="$1" pattern="$2"; shift 2
    local out
    out=$("$BIN" "$@" 2>&1) || true
    if echo "$out" | grep -Fq "$pattern"; then
        pass "$label"
    else
        fail "$label" "output missing '$pattern': $out"
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

# run_output_file: 断言输出文件非空；本地无栅格化工具时允许 SVG 回退（CI 设 SMOKE_REQUIRE_RASTER=1 则强制 png/pdf）
run_output_file() {
    local label="$1" out="$2"; shift 2
    "$BIN" "$@" > /dev/null 2>&1 || true
    if [ -s "$out" ]; then
        pass "$label"
    elif [ "${SMOKE_REQUIRE_RASTER:-}" = "1" ]; then
        fail "$label" "required raster output missing at $out (SMOKE_REQUIRE_RASTER=1)"
    elif [ -s "${out%.png}.svg" ] || [ -s "${out%.pdf}.svg" ]; then
        pass "$label (svg fallback)"
        SVG_FALLBACK=$((SVG_FALLBACK + 1))
    else
        fail "$label" "no output at $out"
    fi
}

# strip_svg_style: 剥离 SVG 内嵌 style，便于几何结构比对
strip_svg_style() {
    python3 -c 'import re,sys; t=open(sys.argv[1],encoding="utf-8",newline="").read(); sys.stdout.write(re.sub(r"<style>.*?</style>", "", t, flags=re.S|re.I))' "$1"
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
run_stdout_contains "from-mermaid content" "created graph" create from-mermaid --content $'flowchart TD\nA[Test]-->B[Done]' --name smoke-inline --id smoke-6
run_stdout_contains "from-input auto-detect" "created graph" create from-input --file examples/example_input/flowchart.mmd --id smoke-7

# ─── color + new mermaid types ────────────────────────────────
echo "[color/mermaid]"
run_stdout_contains "from-mermaid colors" "created graph" create from-mermaid --file examples/example_input/flowchart_colors.mmd --name smoke-colors --id smoke-color-1
run_stdout_contains "from-mermaid sequence" "created graph" create from-mermaid --file examples/example_input/sequence.mmd --name smoke-seq --id smoke-seq-1
run_stdout_contains "from-mermaid class" "created graph" create from-mermaid --file examples/example_input/class.mmd --name smoke-class --id smoke-class-1
run_stdout_contains "from-mermaid state" "created graph" create from-mermaid --file examples/example_input/state.mmd --name smoke-state --id smoke-state-1
run_stdout_contains "from-mermaid pie" "created graph" create from-mermaid --file examples/example_input/pie.mmd --name smoke-pie --id smoke-pie-1
run_ok "colors to-mermaid" convert to-mermaid --file examples/example_input/flowchart_colors.mmd --output smoke-colors.mmd
# classDef/linkStyle 必须出现在 flowchart 声明之后
if grep -q "flowchart" smoke-colors.mmd && grep -q "classDef" smoke-colors.mmd && grep -q "linkStyle" smoke-colors.mmd; then
  FLOW_LINE=$(grep -n "flowchart" smoke-colors.mmd | head -1 | cut -d: -f1)
  CLASS_LINE=$(grep -n "classDef" smoke-colors.mmd | head -1 | cut -d: -f1)
  LINK_LINE=$(grep -n "linkStyle" smoke-colors.mmd | head -1 | cut -d: -f1)
  if [ "$FLOW_LINE" -lt "$CLASS_LINE" ] && [ "$FLOW_LINE" -lt "$LINK_LINE" ]; then
    pass "colors mermaid order (flowchart before classDef/linkStyle)"
  else
    fail "colors mermaid order" "flowchart=$FLOW_LINE classDef=$CLASS_LINE linkStyle=$LINK_LINE"
  fi
else
  fail "colors mermaid export" "missing flowchart/classDef/linkStyle in smoke-colors.mmd"
fi
run_stdout_contains "colors model fillColor" "fillColor" convert to-model --file examples/example_input/flowchart_colors.mmd --stdout
run_fails "colors bad (classDef before flowchart)" convert to-model --file examples/example_input/flowchart_colors_bad.mmd --input-format mermaid --stdout
run_fails "unknown mermaid type" convert to-model --file examples/example_input/mermaid_unknown_bad.mmd --input-format mermaid --stdout

# ─── store ────────────────────────────────────────────────────
echo "[store]"
run_stdout_contains "list"              "smoke-1" store list
run_stdout_contains "list --type"       "smoke-arch" store list --type architecture
run_stdout_contains "list --format json" '"graphs"' store list --format json
run_stdout_contains "show"              "smoke-flow" store show --id smoke-1
run_stdout_contains "load to stdout"    '"id"' store load --id smoke-1

# ─── cursor ───────────────────────────────────────────────────
echo "[cursor]"
CURSOR_OPEN=$("$BIN" cursor open smoke-1 2>&1) || true
CURSOR_ID=$(echo "$CURSOR_OPEN" | sed -n 's/.*"cursor"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -1)
if [ -n "$CURSOR_ID" ] && echo "$CURSOR_OPEN" | grep -q '"index"'; then
    pass "cursor open"
else
    fail "cursor open" "$CURSOR_OPEN"
fi
if [ -n "$CURSOR_ID" ]; then
    run_stdout_contains "cursor get"  '"index"' cursor get smoke-1 --cursor "$CURSOR_ID"
    run_stdout_contains "cursor next" '"index"' cursor next smoke-1 --cursor "$CURSOR_ID"
    run_stdout_contains "cursor prev" '"index"' cursor prev smoke-1 --cursor "$CURSOR_ID"
    run_stdout_contains "cursor close" '"closed"' cursor close smoke-1 --cursor "$CURSOR_ID"
fi

# ─── graph show ───────────────────────────────────────────────
echo "[graph show]"
run_stdout_contains "show graph"        "smoke-flow" graph show smoke-1
run_stdout_contains "show node"         "需求输入"   graph show smoke-1 --node A
run_stdout_contains "show edge"         "from"       graph show smoke-1 --edge e1
run_stdout_contains "show json model" '"nodes"'   store load --id smoke-1

# ─── graph update ─────────────────────────────────────────────
echo "[graph update]"
run_stdout_contains "update node label" "updated node" graph update --node A --set label="New Start" smoke-1
run_stdout_contains "update node shape" "updated node" graph update --node A --set shape=diamond smoke-1
run_stdout_contains "update edge style" "updated edge" graph update --edge e1 --set style=dashed smoke-1
run_stdout_contains "update selector"   "updated"      graph update --selector "shape=rect" --set shape=round smoke-1

# ─── graph insert ─────────────────────────────────────────────
echo "[graph insert]"
run_stdout_contains "insert node" "inserted node" graph insert --node --type rect --label "Extra Step" --position "400 200" smoke-1
run_stdout_contains "insert edge" "inserted edge" graph insert --edge --from A --to B smoke-1

# ─── graph delete ─────────────────────────────────────────────
echo "[graph delete]"
run_stdout_contains "delete edge" "deleted edge" graph delete --edge e3 smoke-1
run_stdout_contains "delete node" "deleted node" graph delete --node D smoke-1

# ─── version draft ────────────────────────────────────────────
echo "[version draft]"
run_stdout_contains "draft show" "pending operations" version draft show smoke-1
run_stdout_contains "draft reset" "discarded" version draft reset smoke-1

# ─── draft (独立命令族) ───────────────────────────────────────
echo "[draft]"
"$BIN" graph update --node A --set label="Draft Test" smoke-1 > /dev/null 2>&1 || true
run_stdout_contains "draft status" '"draft"' draft status smoke-1
run_stdout_contains "draft discard" "discarded" draft discard smoke-1

# ─── version stage + commit ───────────────────────────────────
echo "[version stage+commit]"
# Make a change first
"$BIN" graph update --node A --set label="Staged Label" smoke-1 > /dev/null 2>&1 || true
run_stdout_contains "stage all" "staged" version stage smoke-1
run_stdout_contains "stage show" "Staged" version stage show smoke-1
run_stdout_contains "commit" "committed smoke-1 v" version commit smoke-1 -m "smoke test commit"
run_stdout_contains "stage clear" "cleared" version stage clear smoke-1

# Make another change and commitAll
"$BIN" graph update --node B --set label="Skipped Stage" smoke-1 > /dev/null 2>&1 || true
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
run_stdout_contains "show --version 2" "smoke test commit" version show smoke-1 --version 2

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
run_ok "to-excalidraw file" convert to-excalidraw --file examples/example_input/flowchart.mmd --output smoke-convert.excalidraw
run_output_file "to-png file" smoke-convert.png convert to-png --file examples/example_input/flowchart.mmd --output smoke-convert.png
run_output_file "to-pdf file" smoke-convert.pdf convert to-pdf --file examples/example_input/flowchart.mmd --output smoke-convert.pdf

# ─── export ───────────────────────────────────────────────────
echo "[export]"
run_ok "to-svg"     export to-svg --id smoke-1 --output smoke-export.svg
run_ok "to-drawio"  export to-drawio --id smoke-1 --output smoke-export.drawio
run_ok "to-mermaid" export to-mermaid --id smoke-1 --output smoke-export.mmd
run_stdout_contains "to-url" "https://mermaid.live" export to-url --id smoke-1
run_ok "export to-excalidraw" export to-excalidraw --id smoke-1 --output smoke-export.excalidraw
run_output_file "export to-png" smoke-export.png export to-png --id smoke-1 --output smoke-export.png

# ─── edit ─────────────────────────────────────────────────────
echo "[edit]"
run_stdout_contains "with-browser" "opening:" edit with-browser --id smoke-1
run_ok "with-svg" edit with-svg --id smoke-1
run_stdout_contains "with-drawio" "opening:" edit with-drawio --id smoke-1
run_stdout_contains "with-excalidraw" "opening:" edit with-excalidraw --id smoke-1

# ─── layout ───────────────────────────────────────────────────
echo "[layout]"
run_stdout_contains "layout auto" "layout applied" layout auto --id smoke-1
run_stdout_contains "layout auto --save" "saved" layout auto --id smoke-1 --save
run_stdout_contains "layout layered" "layout applied" layout layered --id smoke-2
run_stdout_contains "layout tree-h" "layout applied" layout tree-h --id smoke-2
run_stdout_contains "layout tree-v" "layout applied" layout tree-v --id smoke-3
run_stdout_contains "layout grid" "layout applied" layout grid --id smoke-1

# ─── validate ─────────────────────────────────────────────────
echo "[validate]"
run_stdout_contains "validate graph" "valid: no issues found" validate graph --id smoke-1
run_stdout_contains "validate input" "valid: no issues found" validate input --file examples/example_input/er.mmd --input-format mermaid
run_stdout_contains "validate strict" "valid: no issues found" validate input --file examples/example_input/flowchart.mmd --strict
run_stdout_contains "validate architecture xml" "valid: no issues found" validate input --file examples/example_input/architecture.xml --input-format xml

# 悬空边触发结构校验错误（Mermaid 会自动补全未声明节点）
printf '<graph type="flowchart"><node id="a" label="A"/><edge from="a" to="missing"/></graph>' > smoke-broken.xml
run_output_contains "validate broken" "error" validate input --file smoke-broken.xml --input-format xml

# ─── store delete ─────────────────────────────────────────────
echo "[store delete]"
run_stdout_contains "delete --force" "deleted graph" store delete --id smoke-7 --force

# ─── edge cases ───────────────────────────────────────────────
echo "[edge-cases]"
run_fails "invalid graph id"    graph show nonexistent-graph
run_fails "invalid node ref"    graph update --node FAKE --set label=X smoke-1
run_fails "commit empty stage"  version commit smoke-1 -m "should fail"
# 制造未提交 draft 后 checkout 应失败
"$BIN" graph update --node A --set label="dirty checkout test" smoke-1 > /dev/null 2>&1 || true
run_fails "checkout dirty"      version checkout smoke-1 1
run_stdout_contains "help flag" "usage" --help

# ─── fixture-regression（与 CI 样例输出比对）────────────────────
echo "[fixture-regression]"
"$BIN" convert to-mermaid --file examples/example_input/workflow.drawio --output smoke-fix-workflow.mmd > /dev/null 2>&1 || true
"$BIN" convert to-mermaid --file examples/example_input/whiteboard_freedraw.excalidraw --output smoke-fix-whiteboard.mmd > /dev/null 2>&1 || true
"$BIN" convert to-mermaid --file examples/example_input/architecture.xml --output smoke-fix-architecture.mmd > /dev/null 2>&1 || true
"$BIN" convert to-svg --file examples/example_input/whiteboard_freedraw.excalidraw --output smoke-fix-whiteboard.svg > /dev/null 2>&1 || true
if diff -q --strip-trailing-cr smoke-fix-workflow.mmd examples/example_output/workflow.drawio_out/workflow.drawio.mmd > /dev/null 2>&1; then
    pass "fixture workflow mermaid"
else
    fail "fixture workflow mermaid" "diff mismatch"
fi
if diff -q --strip-trailing-cr smoke-fix-whiteboard.mmd examples/example_output/whiteboard_freedraw.excalidraw_out/whiteboard_freedraw.excalidraw.mmd > /dev/null 2>&1; then
    pass "fixture whiteboard mermaid"
else
    fail "fixture whiteboard mermaid" "diff mismatch"
fi
if diff -q --strip-trailing-cr smoke-fix-architecture.mmd examples/example_output/architecture.xml_out/architecture.xml.mmd > /dev/null 2>&1; then
    pass "fixture architecture mermaid"
else
    fail "fixture architecture mermaid" "diff mismatch"
fi
if diff -q --strip-trailing-cr <(strip_svg_style smoke-fix-whiteboard.svg) <(strip_svg_style examples/example_output/whiteboard_freedraw.excalidraw_out/whiteboard_freedraw.excalidraw.svg) > /dev/null 2>&1; then
    pass "fixture whiteboard svg geometry"
else
    fail "fixture whiteboard svg geometry" "diff mismatch"
fi
if grep -q "base64," smoke-fix-whiteboard.svg; then
    pass "fixture whiteboard svg base64"
else
    fail "fixture whiteboard svg base64" "missing embedded font/image"
fi
if grep -q "font-family:&#39;Excalifont&#39;" smoke-fix-whiteboard.svg; then
    fail "fixture whiteboard font escape" "font CSS quotes wrongly xml-escaped"
else
    pass "fixture whiteboard font escape"
fi
if grep -q "font-family:'Excalifont'" smoke-fix-whiteboard.svg; then
    pass "fixture whiteboard font family"
else
    fail "fixture whiteboard font family" "Excalifont not found"
fi

# ─── legacy-compat（旧版扁平命令回归）──────────────────────────
echo "[legacy-compat]"
LEGACY_ID="smoke-legacy"
"$BIN" create --input examples/example_input/flowchart.mmd --id "$LEGACY_ID" --name legacy-flow > /dev/null 2>&1 || true
"$BIN" create --input examples/example_input/orgchart.csv --id "$LEGACY_ID" --name legacy-org > /dev/null 2>&1 || true
run_stdout_contains "legacy list" "$LEGACY_ID" list
run_stdout_contains "legacy history v1" "v1" history --id "$LEGACY_ID"
run_stdout_contains "legacy history v2" "v2" history --id "$LEGACY_ID"
run_ok "legacy rollback" rollback --id "$LEGACY_ID" --version 1
run_stdout_contains "legacy status HEAD" "HEAD:     v3" version status "$LEGACY_ID"
run_stdout_contains "legacy open" "opening:" open --id "$LEGACY_ID"

# ─── serve (stdio JSON-RPC 集成) ────────────────────────────────
echo "[serve]"
SERVE_OUT=$(printf '%s\n%s\n%s\n' \
    '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
    '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
    '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"graph_create","arguments":{"content":"flowchart TD\nM[MCP]-->N[Test]","name":"ci-mcp"}}}' \
    | "$BIN" serve 2>&1 || true)
if echo "$SERVE_OUT" | grep -q "protocolVersion"; then
    pass "serve initialize"
else
    fail "serve initialize" "$SERVE_OUT"
fi
if echo "$SERVE_OUT" | grep -Fq "graph_create"; then
    pass "serve tools/list"
else
    fail "serve tools/list" "graph_create not in tools/list output"
fi
if echo "$SERVE_OUT" | grep -Eq '(\\"status\\"|"status")[[:space:]]*:[[:space:]]*(\\"created\\"|"created")'; then
    pass "serve graph_create"
else
    fail "serve graph_create" "created status not found in: $SERVE_OUT"
fi

# ─── summary ──────────────────────────────────────────────────
echo "=========================================="
printf "Results: \033[32m%d passed\033[0m, " "$PASSED"
if [ "$FAILED" -gt 0 ]; then printf "\033[31m%d failed\033[0m" "$FAILED"; else printf "0 failed"; fi
echo ""
echo "=========================================="

# 简要 Markdown 报告（与 docs/TEST_REPORT.md 同目录，供 CI artifact）
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
REPORT_DIR="$REPO_ROOT/docs"
mkdir -p "$REPORT_DIR"
{
    echo "# graphmcp CLI smoke report"
    echo ""
    echo "- binary: $BIN"
    echo "- store: $STORE"
    echo "- passed: $PASSED"
    echo "- failed: $FAILED"
    echo "- svg_fallback: $SVG_FALLBACK"
    echo "- smoke_require_raster: ${SMOKE_REQUIRE_RASTER:-0}"
    echo ""
    if [ "$FAILED" -eq 0 ]; then echo "**ALL PASSED**"; else echo "**HAS FAILURES**"; fi
} > "$REPORT_DIR/SMOKE_REPORT.md"

exit $FAILED
