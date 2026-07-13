#!/usr/bin/env bash
# export-table-examples.sh - 将通用表样例导出到 examples/example_output
# 用法: bash scripts/export-table-examples.sh [path/to/graphmcp]
# 与 export-example-testout.sh（写入 example_testout）区分：本脚本写入 example_output 并适合提交。

set -euo pipefail

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

STORE="$ROOT/.tmp-table-ex-$$"
mkdir -p "$STORE"
export GRAPHMCP_STORE="$STORE"
export GRAPHMCP_NO_LAUNCH=1
unset GRAPHMCP_TABLE_CREATE_LEGACY_UPSERT 2>/dev/null || true

cleanup() {
    rm -rf "$STORE" 2>/dev/null || true
}
trap cleanup EXIT

echo "=== export table examples ==="
echo "binary: $BIN"
echo "store:  $STORE"

# ── enemy_sample.csv：表原生交换格式 + check（无边列，不做图导出）──
ENEMY_CSV_IN="examples/example_input/enemy_sample.csv"
ENEMY_CSV_OUT="examples/example_output/enemy_sample.csv_out"
ENEMY_CSV_ID="ex-enemy-sample"
mkdir -p "$ENEMY_CSV_OUT"

"$BIN" table create --file "$ENEMY_CSV_IN" --id "$ENEMY_CSV_ID" --name enemies --format csv
"$BIN" table export --id "$ENEMY_CSV_ID" --to csv -o "$ENEMY_CSV_OUT/enemy_sample.csv.csv"
"$BIN" table export --id "$ENEMY_CSV_ID" --to model -o "$ENEMY_CSV_OUT/enemy_sample.csv.model.json"
"$BIN" table export --id "$ENEMY_CSV_ID" --to xml -o "$ENEMY_CSV_OUT/enemy_sample.csv.xml"
"$BIN" table check "$ENEMY_CSV_ID" --allowed '{"层级":["小怪","Boss"]}' \
    > "$ENEMY_CSV_OUT/enemy_sample.csv.check_report.csv"
echo "wrote $ENEMY_CSV_OUT (csv, model.json, xml, check_report.csv)"

# ── enemy_sample.xml：同上，验证表 XML 导入后的互通导出 ──
ENEMY_XML_IN="examples/example_input/enemy_sample.xml"
ENEMY_XML_OUT="examples/example_output/enemy_sample.xml_out"
ENEMY_XML_ID="ex-enemy-sample-xml"
mkdir -p "$ENEMY_XML_OUT"

"$BIN" table create --file "$ENEMY_XML_IN" --id "$ENEMY_XML_ID" --name enemies --format xml
"$BIN" table export --id "$ENEMY_XML_ID" --to csv -o "$ENEMY_XML_OUT/enemy_sample.xml.csv"
"$BIN" table export --id "$ENEMY_XML_ID" --to model -o "$ENEMY_XML_OUT/enemy_sample.xml.model.json"
"$BIN" table export --id "$ENEMY_XML_ID" --to xml -o "$ENEMY_XML_OUT/enemy_sample.xml.xml"
"$BIN" table check "$ENEMY_XML_ID" --allowed '{"层级":["小怪","Boss"]}' \
    > "$ENEMY_XML_OUT/enemy_sample.xml.check_report.csv"
echo "wrote $ENEMY_XML_OUT (csv, model.json, xml, check_report.csv)"

# ── skill_relations：表原生 + 可投影图格式 ──
SKILL_IN="examples/example_input/skill_relations.csv"
SKILL_OUT="examples/example_output/skill_relations.csv_out"
SKILL_TID="ex-skill-relations"
mkdir -p "$SKILL_OUT"

"$BIN" table create --file "$SKILL_IN" --id "$SKILL_TID" --name skills --format csv
"$BIN" table export --id "$SKILL_TID" --to csv -o "$SKILL_OUT/skill_relations.csv.csv"
"$BIN" table export --id "$SKILL_TID" --to model -o "$SKILL_OUT/skill_relations.csv.model.json"
"$BIN" table export --id "$SKILL_TID" --to xml -o "$SKILL_OUT/skill_relations.csv.xml"

gline=$("$BIN" table from-table --file "$SKILL_IN" --name skill-graph)
# 输出形如: graph <id> v1 nodes=N edges=M
GID=$(echo "$gline" | awk '{print $2}')
if [ -z "$GID" ]; then
    echo "error: failed to parse graph id from: $gline" >&2
    exit 1
fi
echo "graph id: $GID"

export_graph() {
    local fmt="$1" ext="$2" required="${3:-1}"
    local out="$SKILL_OUT/skill_relations.csv${ext}"
    if [ "$fmt" = "url" ]; then
        if content=$("$BIN" export "to-$fmt" --id "$GID" 2>/dev/null) && \
           [ -n "$(echo "$content" | tr -d '[:space:]')" ]; then
            printf '%s' "$(echo "$content" | tr -d '\r')" > "$out"
            echo "  PASS $fmt -> $out"
            return 0
        fi
    else
        if "$BIN" export "to-$fmt" --id "$GID" -o "$out" >/dev/null 2>&1 && [ -s "$out" ]; then
            echo "  PASS $fmt -> $out"
            return 0
        fi
    fi
    if [ "$required" = "1" ]; then
        echo "  FAIL $fmt (required)" >&2
        return 1
    fi
    echo "  SKIP $fmt (optional; png/pdf may need browser)"
    rm -f "$out" 2>/dev/null || true
    return 0
}

export_graph mermaid .mmd 1
export_graph drawio .drawio 1
export_graph excalidraw .excalidraw 1
export_graph svg .svg 1
export_graph url .url.txt 1
# png/pdf 尽力生成，失败不阻塞
export_graph png .png 0 || true
export_graph pdf .pdf 0 || true

echo "wrote $SKILL_OUT (table + graph exports)"
echo "done."
