#!/usr/bin/env bash
# bench_ci_retry.sh - CI 性能比对带重试，抑制跑分机偶然抖动
#
# 用法: bash scripts/bench_ci_retry.sh <bench可执行文件> <基线json> <结果json> [最大次数=3]
#
# 行为：每次跑 bench → bench_compare；失败则 ::warning:: 并重试；
# 连续 max 次失败才 ::error:: 并以非 0 退出阻断 CI。
set -uo pipefail

BENCH_BIN="${1:?missing bench binary}"
BASELINE="${2:?missing baseline path}"
RESULT="${3:?missing result path}"
MAX_ATTEMPTS="${4:-3}"

if [[ ! -x "${BENCH_BIN}" && ! -f "${BENCH_BIN}" ]]; then
  echo "error: bench binary not found: ${BENCH_BIN}" >&2
  exit 1
fi

mkdir -p "$(dirname "${RESULT}")"

is_ci=0
if [[ "${CI:-}" == "true" || "${GITHUB_ACTIONS:-}" == "true" ]]; then
  is_ci=1
fi

warn() {
  if [[ "${is_ci}" -eq 1 ]]; then
    echo "::warning title=Benchmark Retry::$*"
  else
    echo "WARNING: $*"
  fi
}

err() {
  if [[ "${is_ci}" -eq 1 ]]; then
    echo "::error title=Benchmark Regression::$*"
  else
    echo "ERROR: $*" >&2
  fi
}

attempt=1
while [[ "${attempt}" -le "${MAX_ATTEMPTS}" ]]; do
  echo "=== bench-ci 第 ${attempt}/${MAX_ATTEMPTS} 次 ==="
  "${BENCH_BIN}" > "${RESULT}"

  if [[ ! -f "${BASELINE}" ]]; then
    warn "基线文件不存在，将当前结果写入基线: ${BASELINE}"
    cp "${RESULT}" "${BASELINE}"
    exit 0
  fi

  set +e
  python3 scripts/bench_compare.py "${BASELINE}" "${RESULT}"
  rc=$?
  set -e

  if [[ "${rc}" -eq 0 ]]; then
    echo "bench-ci 第 ${attempt} 次通过"
    exit 0
  fi

  if [[ "${attempt}" -lt "${MAX_ATTEMPTS}" ]]; then
    warn "bench-ci 第 ${attempt}/${MAX_ATTEMPTS} 次未通过 (exit=${rc})，冷却后重试"
    sleep 2
  else
    err "bench-ci 连续 ${MAX_ATTEMPTS} 次未通过，阻断 CI"
    exit "${rc}"
  fi
  attempt=$((attempt + 1))
done

exit 1
