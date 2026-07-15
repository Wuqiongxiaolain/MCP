#!/usr/bin/env bash
# export-table-collab-examples.sh - 图&表协同增强工具样例导出
# 用法: bash scripts/export-table-collab-examples.sh [path/to/graphmcp]
# 产物写入 examples/example_output/{enemy_spec.md_out,enemy_sample_bad.csv_out,table_collab_pipeline_out}

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

STORE="$ROOT/.tmp-table-collab-ex-$$"
mkdir -p "$STORE"
export GRAPHMCP_STORE="$STORE"
export GRAPHMCP_NO_LAUNCH=1

cleanup() {
    rm -rf "$STORE" 2>/dev/null || true
}
trap cleanup EXIT

echo "=== export table collab examples ==="
echo "binary: $BIN"
echo "store:  $STORE"

SPEC_IN="examples/example_input/enemy_spec.md"
SPEC_OUT="examples/example_output/enemy_spec.md_out"
BAD_IN="examples/example_input/enemy_sample_bad.csv"
BAD_OUT="examples/example_output/enemy_sample_bad.csv_out"
PIPE_OUT="examples/example_output/table_collab_pipeline_out"
mkdir -p "$SPEC_OUT" "$BAD_OUT" "$PIPE_OUT"

# ── 1) 导图 → 规则表 ──
gline=$("$BIN" create from-markdown --file "$SPEC_IN" --name "enemy-spec" --type mindmap)
# created graph '...' id=... vN (...)
GID=$(echo "$gline" | sed -n 's/.*id=\([^ ]*\).*/\1/p')
if [ -z "$GID" ]; then
    echo "error: failed to parse graph id from: $gline" >&2
    exit 1
fi
echo "mindmap graph id: $GID"

rline=$("$BIN" table rules-from-graph --graph-id "$GID" --name enemy-rules --id ex-enemy-rules)
RID=$(echo "$rline" | awk '/^table /{print $2; exit}')
if [ -z "$RID" ]; then
    RID="ex-enemy-rules"
fi
"$BIN" table export --id "$RID" --to csv -o "$SPEC_OUT/enemy_spec.rules.csv"
"$BIN" table export --id "$RID" --to csv -o "$PIPE_OUT/01_rules.csv"
"$BIN" export to-mermaid --id "$GID" -o "$SPEC_OUT/enemy_spec.mmd" >/dev/null
echo "wrote $SPEC_OUT (rules.csv, mmd)"

# ── 2) 非法枚举表 → check → fix-enums ──
"$BIN" table create --file "$BAD_IN" --id ex-enemy-bad --name enemies-bad --format csv --force
"$BIN" table check ex-enemy-bad --rules "$RID" \
    > "$BAD_OUT/enemy_sample_bad.check_report.csv" || true
"$BIN" table check ex-enemy-bad --rules "$RID" \
    > "$PIPE_OUT/02_check_before.csv" || true

"$BIN" table fix-enums ex-enemy-bad --rules-id "$RID" > "$BAD_OUT/enemy_sample_bad.fix_enums.log"
"$BIN" table export --id ex-enemy-bad --to csv -o "$BAD_OUT/enemy_sample_bad.fixed.csv"
"$BIN" table export --id ex-enemy-bad --to csv -o "$PIPE_OUT/03_fixed.csv"
"$BIN" table check ex-enemy-bad --rules "$RID" \
    > "$PIPE_OUT/04_check_after.csv" || true
echo "wrote $BAD_OUT (check_report, fixed, fix log)"

# ── 3) derive（合法 enemy_sample）+ slug 演示 ──
"$BIN" table create --file examples/example_input/enemy_sample.csv \
    --id ex-enemy-ok --name enemies --format csv --force
dline=$("$BIN" table derive --source-id ex-enemy-ok --mode animation_checklist)
DID=$(echo "$dline" | awk '/^table /{print $2; exit}')
"$BIN" table export --id "$DID" --to csv -o "$PIPE_OUT/05_anim_checklist.csv"
mkdir -p "examples/example_output/enemy_sample.csv_out"
"$BIN" table export --id "$DID" --to csv \
    -o "examples/example_output/enemy_sample.csv_out/enemy_sample.anim_checklist.csv"

"$BIN" table create --file examples/example_input/name_slug_demo.csv \
    --id ex-slug-demo --name slug-demo --format csv --force
"$BIN" table transform-column ex-slug-demo \
    --source-column 名称 --target-column key --transform slug
"$BIN" table export --id ex-slug-demo --to csv -o "$PIPE_OUT/06_with_slug.csv"

# 也在合法宽表上加中文名 slug 列（预期多为 col_N，演示非 ASCII 回退）
"$BIN" table transform-column ex-enemy-ok \
    --source-column 名称 --target-column key --transform slug
"$BIN" table export --id ex-enemy-ok --to csv \
    -o "examples/example_output/enemy_sample.csv_out/enemy_sample.with_slug.csv"

# ── 4) sample-rows + propose-rows（空表模板）──
"$BIN" table from-graph --graph-id "$GID" --mode skeleton --with-hint-row >/dev/null
# 重新建空骨架：从合法表只保留列头
printf '%s\n' "编号,名称,层级,生成动画？" > "$STORE/empty.csv"
"$BIN" table create --file "$STORE/empty.csv" --id ex-enemy-empty --name enemies-empty --force
"$BIN" table sample-rows ex-enemy-empty --count 1 --rules-id "$RID"
"$BIN" table export --id ex-enemy-empty --to csv -o "$PIPE_OUT/07_sample_rows.csv"

"$BIN" table propose-rows ex-enemy-empty \
    --rows '[{"编号":"99","名称":"样例Boss","层级":"Boss","生成动画？":"x"}]' \
    --rules-id "$RID"
"$BIN" table export --id ex-enemy-empty --to csv -o "$PIPE_OUT/08_after_propose.csv"

# pipeline 说明
cat > "$PIPE_OUT/README.md" <<'EOF'
# 图&表协同流水线样例

复现：`bash scripts/export-table-collab-examples.sh`

| 文件 | 对应工具 |
|------|----------|
| `01_rules.csv` | `table_rules_from_graph` / `table rules-from-graph` |
| `02_check_before.csv` | `table_check`（修复前） |
| `03_fixed.csv` | `table_fix_enums` 写回后的业务表 |
| `04_check_after.csv` | 修复后再 `table_check` |
| `05_anim_checklist.csv` | `table_derive` `animation_checklist` |
| `06_with_slug.csv` | `table_transform_column` `slug`（`name_slug_demo.csv`） |
| `07_sample_rows.csv` | `table_sample_rows` |
| `08_after_propose.csv` | `table_propose_rows`（追加一行） |

另导出到既有目录：

- `enemy_sample.csv_out/enemy_sample.anim_checklist.csv`
- `enemy_sample.csv_out/enemy_sample.with_slug.csv`（中文名多为 `col_N` 回退）

自然语言改表预览请用 MCP `table_update` 的 `dry_run`/`detail`（无单独产物文件）。
EOF

echo "wrote $PIPE_OUT"
echo "done."
