#!/usr/bin/env python3
"""bench_compare.py - 性能回归检测脚本（CI 用）

用法：python3 scripts/bench_compare.py <baseline.json> <current.json>

比较当前基准测试结果与基线：
- 延迟类：超出基线 30% → 警告；50% → 失败（敏感指标更宽松）
- 内存稳定性（memory_RSS_*）：用绝对 MB 上限，不用古董相对基线百分比
- 在 GitHub Actions 中输出 workflow 命令以生成注解和摘要

比较前将 ms/us/s/MB 统一换算，避免单位混用导致假退化。

环境变量（可选）：
  GRAPHMCP_MEMORY_FAIL_MB  内存稳定性 FAIL 绝对上限（默认 4）
  GRAPHMCP_MEMORY_WARN_MB  内存稳定性 WARN 绝对上限（默认 3）
  GRAPHMCP_MEMORY_HALF_RATIO  后半段相对前半段的泄漏倍率上限（默认 2.5）
"""

import json
import sys
import os

WARN_THRESHOLD = 1.30  # 30% 退化 → 警告
FAIL_THRESHOLD = 1.50  # 50% 退化 → 失败
IMPROVE_THRESHOLD = 0.70  # 30% 改善

# 这些指标对 CI runner / 磁盘敏感，用更宽松的阈值
CI_SENSITIVE = {
    "store_save",
    "store_load",
    "toSVG_n200",
    "mcp_tools_list",
    "mcp_graph_create",
    "tableStore_save",
    "tableStore_load",
    "tableStore_list",
    "mcp_table_create",
}

# 已退役的内存指标：基线里可残留，当前结果不再产出时忽略
RETIRED_BENCHMARKS = {
    "memory_RSS_5000iter",
}


def memory_fail_mb() -> float:
    return float(os.environ.get("GRAPHMCP_MEMORY_FAIL_MB", "4"))


def memory_warn_mb() -> float:
    return float(os.environ.get("GRAPHMCP_MEMORY_WARN_MB", "3"))


def memory_half_ratio() -> float:
    return float(os.environ.get("GRAPHMCP_MEMORY_HALF_RATIO", "2.5"))


def is_memory_stability(name: str) -> bool:
    return name.startswith("memory_RSS_")


def to_canonical(value: float, unit: str) -> tuple:
    """将值换算到规范量纲：时间→us，内存→MB。返回 (canonical_value, canonical_unit)。"""
    u = (unit or "").strip().lower()
    if u == "ms":
        return value * 1000.0, "us"
    if u == "s":
        return value * 1_000_000.0, "us"
    if u in ("us", "µs", "μs"):
        return value, "us"
    if u == "kb":
        return value / 1024.0, "MB"
    if u == "b":
        return value / (1024.0 * 1024.0), "MB"
    if u == "mb":
        return value, "MB"
    return value, unit


def load_results(path: str) -> dict:
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    return {b["name"]: b for b in data.get("benchmarks", [])}


def main():
    if len(sys.argv) < 3:
        print(f"用法: {sys.argv[0]} <baseline.json> <current.json>", file=sys.stderr)
        sys.exit(0)

    baseline_path = sys.argv[1]
    current_path = sys.argv[2]

    if not os.path.exists(baseline_path):
        print(f"::warning::基线文件不存在: {baseline_path}")
        sys.exit(0)

    baseline = load_results(baseline_path)
    current = load_results(current_path)

    if not current:
        print("::error::当前基准测试结果为空")
        sys.exit(2)

    warnings = []
    failures = []
    improvements = []
    missing = []

    # 分段泄漏：后半明显大于前半 → 持续增长嫌疑
    first_half = current.get("memory_RSS_repeat_save_1st_half")
    second_half = current.get("memory_RSS_repeat_save_2nd_half")
    if first_half and second_half:
        f_val, f_u = to_canonical(
            float(first_half["value"]), first_half.get("unit", "MB")
        )
        s_val, s_u = to_canonical(
            float(second_half["value"]), second_half.get("unit", "MB")
        )
        if f_u == "MB" and s_u == "MB" and f_val > 0.05 and s_val / f_val >= memory_half_ratio():
            failures.append(
                f"memory_RSS_repeat_save halves: "
                f"1st={f_val:.2f}MB 2nd={s_val:.2f}MB "
                f"(2nd/1st>={memory_half_ratio():.1f}x suggests sustained growth)"
            )

    for name, cur in sorted(current.items()):
        if name in RETIRED_BENCHMARKS:
            continue

        cur_val, cur_u = to_canonical(float(cur["value"]), cur.get("unit", ""))

        # 内存稳定性：绝对上限，对齐「重复同一工作负载是否泄漏」
        if is_memory_stability(name) and cur_u == "MB":
            cur_show = f"{cur['value']:.2f}{cur.get('unit', 'MB')}"
            fail_mb = memory_fail_mb()
            warn_mb = memory_warn_mb()
            if cur_val >= fail_mb:
                failures.append(
                    f"{name}: {cur_show} exceeds absolute FAIL cap {fail_mb:.1f}MB"
                )
            elif cur_val >= warn_mb:
                warnings.append(
                    f"{name}: {cur_show} exceeds absolute WARN cap {warn_mb:.1f}MB"
                )
            # 不再用 memory_RSS_5000iter 一类旧基线做相对百分比
            continue

        if name not in baseline:
            missing.append(name)
            continue

        bl = baseline[name]
        bl_val, bl_u = to_canonical(float(bl["value"]), bl.get("unit", ""))

        if cur_u != bl_u:
            warnings.append(
                f"{name}: 单位不一致 {bl.get('unit')} vs {cur.get('unit')}，跳过比较"
            )
            continue

        if bl_val <= 0:
            continue

        ratio = cur_val / bl_val

        is_sensitive = any(s in name for s in CI_SENSITIVE)
        fail_thr = 1.80 if is_sensitive else FAIL_THRESHOLD
        warn_thr = 1.50 if is_sensitive else WARN_THRESHOLD

        bl_show = f"{bl['value']:.2f}{bl['unit']}"
        cur_show = f"{cur['value']:.2f}{cur['unit']}"

        if ratio >= fail_thr:
            failures.append(
                f"{name}: {bl_show} → {cur_show} (+{(ratio-1)*100:.0f}%)"
            )
        elif ratio >= warn_thr:
            warnings.append(
                f"{name}: {bl_show} → {cur_show} (+{(ratio-1)*100:.0f}%)"
            )
        elif ratio <= IMPROVE_THRESHOLD:
            improvements.append(
                f"{name}: {bl_show} → {cur_show} (-{(1-ratio)*100:.0f}%)"
            )

    is_ci = os.environ.get("CI") == "true" or os.environ.get("GITHUB_ACTIONS") == "true"

    for w in warnings:
        if is_ci:
            print(f"::warning title=Benchmark Regression::{w}")
        else:
            print(f"WARN: {w}")

    for f in failures:
        if is_ci:
            print(f"::error title=Benchmark Regression::{f}")
        else:
            print(f"FAIL: {f}")

    for imp in improvements:
        print(f"INFO: {imp}")

    for m in missing:
        print(f"INFO: 新指标（无基线）: {m}")

    if is_ci and os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as f:
            f.write("## 性能基准测试结果\n\n")
            f.write("| 测试 | 基线 | 当前 | 变化 |\n")
            f.write("|------|------|------|------|\n")
            for name, cur in sorted(current.items()):
                if is_memory_stability(name):
                    f.write(
                        f"| {name} | abs-cap | {cur['value']:.2f}{cur.get('unit', '')} | "
                        f"FAIL>{memory_fail_mb():.0f}MB |\n"
                    )
                    continue
                bl = baseline.get(name)
                if not bl:
                    f.write(
                        f"| {name} | — | {cur['value']:.2f}{cur['unit']} | new |\n"
                    )
                    continue
                cur_val, cur_u = to_canonical(float(cur["value"]), cur.get("unit", ""))
                bl_val, bl_u = to_canonical(float(bl["value"]), bl.get("unit", ""))
                if bl_val <= 0 or cur_u != bl_u:
                    f.write(
                        f"| {name} | {bl['value']:.2f}{bl['unit']} | "
                        f"{cur['value']:.2f}{cur['unit']} | n/a |\n"
                    )
                    continue
                ratio = (cur_val - bl_val) / bl_val * 100
                f.write(
                    f"| {name} | {bl['value']:.2f}{bl['unit']} | "
                    f"{cur['value']:.2f}{cur['unit']} | {ratio:+.0f}% |\n"
                )
            if missing:
                f.write(f"\n新增 {len(missing)} 个指标，无基线数据\n")
            if improvements:
                f.write(f"\n{len(improvements)} 个指标有明显改善\n")
            if warnings:
                f.write(f"\n{len(warnings)} 个指标超过警告阈值\n")
            if failures:
                f.write(f"\n{len(failures)} 个指标超过失败阈值\n")

    if failures:
        print(f"\n{len(failures)} 个指标严重退化", file=sys.stderr)
        sys.exit(2)
    elif warnings:
        print(f"\n{len(warnings)} 个指标轻度退化（不阻断）", file=sys.stderr)
        sys.exit(0)
    else:
        print(f"\n所有 {len(current)} 个指标在正常范围", file=sys.stderr)
        sys.exit(0)


if __name__ == "__main__":
    main()
