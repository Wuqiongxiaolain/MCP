#!/usr/bin/env bash
# ci_capture.sh - 运行命令，将 stdout/stderr 与退出码写入 docs/ci_results/
#
# 用法:
#   bash scripts/ci_capture.sh <name> <command> [args...]
#
# 写出:
#   docs/ci_results/<name>.log
#   docs/ci_results/<name>.exit
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

set +e
"$@" 2>&1 | tee "$LOG"
# tee 成功时 PIPESTATUS[0] 为被测命令退出码
ec=${PIPESTATUS[0]}
set -e

printf '%s\n' "$ec" > "$EXIT_FILE"
exit "$ec"
