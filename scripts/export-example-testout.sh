#!/usr/bin/env bash
# export-example-testout.sh - 将 example_input 全量导出到 example_testout 并生成 TEST_REPORT.md
# 用法: bash scripts/export-example-testout.sh [path/to/graphmcp]

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

OUT_ROOT="examples/example_testout"
rm -rf "$OUT_ROOT"
mkdir -p "$OUT_ROOT"

BRANCH="$(git branch --show-current 2>/dev/null || echo unknown)"
COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
TS="$(date '+%Y-%m-%d %H:%M:%S')"

PASSED=0
FAILED=0
SKIPPED=0
ROWS=()

record_row() {
    local input="$1" format="$2" status="$3" size="$4" note="$5"
    ROWS+=("$input|$format|$status|$size|$note")
    case "$status" in
        PASS) PASSED=$((PASSED + 1)) ;;
        FAIL) FAILED=$((FAILED + 1)) ;;
        SKIP) SKIPPED=$((SKIPPED + 1)) ;;
    esac
}

FORMATS=(drawio mermaid excalidraw svg png pdf url)
EXTS=(".drawio" ".mmd" ".excalidraw" ".svg" ".png" ".pdf" ".url.txt")

shopt -s nullglob
for inp in examples/example_input/*; do
    [ -f "$inp" ] || continue
    name="$(basename "$inp")"
    dir="$OUT_ROOT/${name}_out"
    mkdir -p "$dir"

    if [ "$name" = "er.mmd" ]; then
        if vout="$("$BIN" validate input --file "$inp" --input-format mermaid 2>&1)"; then
            record_row "$name" validate PASS "-" "$(echo "$vout" | tr '\n' ' ')"
        else
            record_row "$name" validate FAIL "-" "$(echo "$vout" | tr '\n' ' ')"
        fi
        for f in "${FORMATS[@]}"; do
            record_row "$name" "$f" SKIP "-" "validate-only"
        done
        continue
    fi

    # 通用表 CSV / 表 XML：不做盲目 graph convert（见 scripts/export-table-examples.sh）
    case "$name" in
        enemy_sample.csv|enemy_sample.xml|enemy_sample.table-xml.xml|skill_relations.csv|name_slug_demo.csv)
            for f in "${FORMATS[@]}"; do
                record_row "$name" "$f" SKIP "-" "generic-table; use export-table-examples.sh → example_output"
            done
            continue
            ;;
    esac

    # 硬无效坏样例：只做 validate（期望失败），不计入 convert FAIL
    # 注意：flowchart_syntax_bad / sequence_bad 等为“软坏样例”——当前校验器可接受并正常 convert，走常规导出路径
    case "$name" in
        enemy_sample_bad.csv|flowchart_colors_bad.mmd|mermaid_unknown_bad.mmd)
            fmt_guess=auto
            case "$name" in
                *.mmd) fmt_guess=mermaid ;;
                *.csv) fmt_guess=csv ;;
                *.xml) fmt_guess=xml ;;
            esac
            if vout="$("$BIN" validate input --file "$inp" --input-format "$fmt_guess" 2>&1)"; then
                # 硬无效样例却通过校验 -> 记 FAIL（语义回归）
                record_row "$name" validate FAIL "-" "expected invalid but validate passed: $(echo "$vout" | tr '\n' ' ')"
            else
                record_row "$name" validate PASS "-" "expected-invalid: $(echo "$vout" | tr '\n' ' ' | cut -c1-80)"
            fi
            for f in "${FORMATS[@]}"; do
                record_row "$name" "$f" SKIP "-" "intentional-bad-sample"
            done
            continue
            ;;
    esac

    for i in "${!FORMATS[@]}"; do
        fmt="${FORMATS[$i]}"
        ext="${EXTS[$i]}"
        out_file="$dir/${name}${ext}"
        ok=0
        note=""

        if [ "$fmt" = "url" ]; then
            if content="$("$BIN" convert "to-$fmt" --file "$inp" 2>&1)"; then
                if [ -n "$(echo "$content" | tr -d '[:space:]')" ]; then
                    printf '%s' "$(echo "$content" | tr -d '\r')" > "$out_file"
                    ok=1
                fi
            fi
        else
            if msg="$("$BIN" convert "to-$fmt" --file "$inp" --output "$out_file" 2>&1)"; then
                if [ -s "$out_file" ]; then
                    ok=1
                fi
            else
                msg="${msg:-}"
            fi
            if [ "$ok" -eq 0 ] && [ -s "${out_file}.svg" ]; then
                ok=1
                note="svg fallback"
            elif [ "$ok" -eq 0 ]; then
                note="$(echo "$msg" | tr '\n' ' ')"
                [ ${#note} -gt 80 ] && note="${note:0:80}"
            fi
        fi

        if [ "$ok" -eq 1 ]; then
            sz=0
            [ -f "$out_file" ] && sz=$(wc -c < "$out_file" | tr -d ' ')
            record_row "$name" "$fmt" PASS "$sz" "$note"
        else
            record_row "$name" "$fmt" FAIL 0 "$note"
        fi
    done
done

# create + export 短冒烟
export GRAPHMCP_STORE="$OUT_ROOT/_store"
SMOKE_ID="testout-smoke"
S1=0 S2=0 S3=0
if "$BIN" create from-mermaid --file examples/example_input/flowchart.mmd --id "$SMOKE_ID" --name testout-flow >/dev/null 2>&1; then S1=1; fi
if "$BIN" create from-csv --file examples/example_input/orgchart.csv --id "$SMOKE_ID" --name testout-org >/dev/null 2>&1; then S2=1; fi
SMOKE_SVG="$OUT_ROOT/_smoke-export.svg"
if "$BIN" export to-svg --id "$SMOKE_ID" --output "$SMOKE_SVG" >/dev/null 2>&1 && [ -s "$SMOKE_SVG" ]; then S3=1; fi
SMOKE_FAIL=$((3 - S1 - S2 - S3))

REPORT="$OUT_ROOT/TEST_REPORT.md"
REPORT_JSON="$OUT_ROOT/TEST_REPORT.json"
MCP_NOTE="not run in export-example-testout.sh; use tests/mcp_smoke.sh"
{
    echo "# example_testout export report"
    echo ""
    echo "- branch: $BRANCH"
    echo "- commit: $COMMIT"
    echo "- time: $TS"
    echo "- binary: $BIN"
    echo ""
    echo "## convert export"
    echo ""
    echo "| input | format | status | size (bytes) | note |"
    echo "|-------|--------|--------|--------------|------|"
    for row in "${ROWS[@]}"; do
        IFS='|' read -r r_in r_fmt r_st r_sz r_note <<< "$row"
        echo "| $r_in | $r_fmt | $r_st | $r_sz | $r_note |"
    done
    echo ""
    echo "## create + export smoke"
    echo ""
    if [ "$S1" -eq 1 ]; then echo "- create flowchart.mmd: PASS"; else echo "- create flowchart.mmd: FAIL"; fi
    if [ "$S2" -eq 1 ]; then echo "- create orgchart.csv (same id): PASS"; else echo "- create orgchart.csv (same id): FAIL"; fi
    if [ "$S3" -eq 1 ]; then echo "- export to svg: PASS ($SMOKE_SVG)"; else echo "- export to svg: FAIL ($SMOKE_SVG)"; fi
    echo ""
    echo "## MCP protocol smoke"
    echo ""
    echo "| step | tool/method | status | note |"
    echo "|------|-------------|--------|------|"
    echo "| - | mcp_smoke.sh | SKIP | $MCP_NOTE |"
    echo ""
    echo "## summary"
    echo ""
    echo "- PASS: $PASSED"
    echo "- FAIL: $FAILED"
    echo "- SKIP: $SKIPPED"
    echo "- smoke failures: $SMOKE_FAIL"
    echo ""
    if [ "$FAILED" -eq 0 ] && [ "$SMOKE_FAIL" -eq 0 ]; then
        echo "**ALL PASSED**"
    else
        echo "**HAS FAILURES**"
    fi
} > "$REPORT"

python3 - "$REPORT_JSON" "$PASSED" "$FAILED" "$SKIPPED" "$SMOKE_FAIL" <<'PY'
import json, sys
report_json, passed, failed, skipped, smoke_fail = sys.argv[1:]
data = {
    "convert_export": {
        "passed": int(passed),
        "failed": int(failed),
        "skipped": int(skipped),
        "smoke_failures": int(smoke_fail),
    },
    "mcp": {
        "passed": 0,
        "failed": 0,
        "skipped": 1,
        "log": "",
        "steps": [],
        "note": "not run in export-example-testout.sh; use tests/mcp_smoke.sh",
    },
}
with open(report_json, "w", encoding="utf-8", newline="\n") as f:
    json.dump(data, f, ensure_ascii=False, indent=2)
    f.write("\n")
PY

cat "$REPORT"
echo ""
echo "Output: $OUT_ROOT"

if [ "$FAILED" -eq 0 ] && [ "$SMOKE_FAIL" -eq 0 ]; then
    exit 0
fi
exit 1
