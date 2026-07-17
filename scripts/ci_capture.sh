#!/usr/bin/env bash
# ci_capture.sh - 运行命令，将 stdout/stderr 与退出码写入 docs/ci_results/
#
# 用法:
#   bash scripts/ci_capture.sh <name> <command> [args...]
#
# 写出:
#   docs/ci_results/<name>.log
#   docs/ci_results/<name>.exit
#   docs/ci_results/<name>.duration   # 秒（浮点），供测试报告回填耗时
#
# 退出码与被包装命令一致（供 CI 门禁使用）。

set -uo pipefail

NAME="${1:?usage: ci_capture.sh <name> <command> [args...]}"
shift
if [ "$#" -lt 1 ]; then
    echo "usage: ci_capture.sh <name> <command> [args...]" >&2
    exit 2
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$ROOT/docs/ci_results"
mkdir -p "$OUT_DIR"

LOG="$OUT_DIR/${NAME}.log"
EXIT_FILE="$OUT_DIR/${NAME}.exit"
DUR_FILE="$OUT_DIR/${NAME}.duration"

# 步骤：记录墙钟耗时，便于 --from-ci 组装报告时回填 duration_s
START_TS="$(date +%s.%N 2>/dev/null || date +%s)"

set +e
"$@" 2>&1 | tee "$LOG"
# tee 成功时 PIPESTATUS[0] 为被测命令退出码
ec=${PIPESTATUS[0]}
set -e

END_TS="$(date +%s.%N 2>/dev/null || date +%s)"
# 兼容仅有整秒 date 的环境
if command -v python3 >/dev/null 2>&1; then
    python3 - "$START_TS" "$END_TS" "$DUR_FILE" <<'PY'
import sys
start, end, path = sys.argv[1], sys.argv[2], sys.argv[3]
try:
    dur = max(0.0, float(end) - float(start))
except ValueError:
    dur = 0.0
with open(path, "w", encoding="utf-8", newline="\n") as f:
    f.write(f"{dur:.3f}\n")
PY
else
    printf '%s\n' "0" > "$DUR_FILE"
fi

printf '%s\n' "$ec" > "$EXIT_FILE"
exit "$ec"
