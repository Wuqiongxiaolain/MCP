#!/usr/bin/env python3
"""bench_compare.py - 性能回归检测脚本（CI 用）

用法：python3 scripts/bench_compare.py <baseline.json> <current.json>

比较当前基准测试结果与基线：
- 任何指标超出基线 30% → 警告（退出码 1）
- 任何指标超出基线 50% → 失败（退出码 2）
- 在 GitHub Actions 中输出 workflow 命令以生成注解和摘要

CI 阈值（比本地宽松，容忍 runner 波动）：
  WARN_THRESHOLD = 30%  — 发出警告但通过
  FAIL_THRESHOLD = 50%  — 阻断 CI

基线更新策略：
  - main 分支 push → 自动更新基线（通过 bench-baseline target）
  - PR 分支 → 与基线比较，超阈值则告警
"""

import json
import sys
import os

WARN_THRESHOLD = 1.30  # 30% 退化 → 警告
FAIL_THRESHOLD = 1.50  # 50% 退化 → 失败
# 改善超过此阈值也报告（用于发现优化效果）
IMPROVE_THRESHOLD = 0.70  # 30% 改善

# 这些指标对 CI runner 敏感，用更宽松的阈值
CI_SENSITIVE = {"store_save", "store_load", "toSVG_n200", "mcp_tools_list"}


def load_results(path: str) -> dict:
    with open(path) as f:
        data = json.load(f)
    return {b["name"]: b for b in data.get("benchmarks", [])}


def to_ns(val: float, unit: str) -> float:
    """归一化到纳秒，避免跨单位比较时误判"""
    if unit == "ns":
        return val
    if unit == "us":
        return val * 1_000
    if unit == "ms":
        return val * 1_000_000
    return val  # 未知单位保持原值


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

    for name, cur in sorted(current.items()):
        if name not in baseline:
            missing.append(name)
            continue

        bl = baseline[name]
        cur_val_ns = to_ns(cur["value"], cur.get("unit", ""))
        bl_val_ns  = to_ns(bl["value"], bl.get("unit", ""))

        if bl_val_ns <= 0:
            continue

        ratio = cur_val_ns / bl_val_ns

        # CI 敏感指标用更宽松阈值
        is_sensitive = any(s in name for s in CI_SENSITIVE)
        fail_thr = 1.80 if is_sensitive else FAIL_THRESHOLD
        warn_thr = 1.50 if is_sensitive else WARN_THRESHOLD

        if ratio >= fail_thr:
            failures.append(
                f"{name}: {bl_val:.2f}{bl['unit']} → "
                f"{cur_val:.2f}{cur['unit']} (+{(ratio-1)*100:.0f}%)"
            )
        elif ratio >= warn_thr:
            warnings.append(
                f"{name}: {bl_val:.2f}{bl['unit']} → "
                f"{cur_val:.2f}{cur['unit']} (+{(ratio-1)*100:.0f}%)"
            )
        elif ratio <= IMPROVE_THRESHOLD:
            improvements.append(
                f"{name}: {bl_val:.2f}{bl['unit']} → "
                f"{cur_val:.2f}{cur['unit']} (-{(1-ratio)*100:.0f}%)"
            )

    # ── 输出 GitHub Actions workflow 注解 ──
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

    # ── Markdown 摘要表格（GitHub Actions step summary） ──
    if is_ci and os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f:
            f.write("## 📊 性能基准测试结果\n\n")
            f.write("| 测试 | 基线 | 当前 | 变化 |\n")
            f.write("|------|------|------|------|\n")
            for name, cur in sorted(current.items()):
                bl = baseline.get(name)
                if not bl:
                    f.write(f"| {name} | — | {cur['value']:.2f}{cur['unit']} | 🆕 |\n")
                    continue
                cur_val_ns = to_ns(cur["value"], cur.get("unit", ""))
                bl_val_ns  = to_ns(bl["value"], bl.get("unit", ""))
                ratio = (cur_val_ns - bl_val_ns) / bl_val_ns * 100
                emoji = (
                    "🔴" if abs(ratio) >= 50
                    else "🟡" if abs(ratio) >= 30
                    else "🟢" if ratio < -20
                    else ""
                )
                f.write(
                    f"| {name} | {bl_val:.2f}{bl['unit']} | "
                    f"{cur_val:.2f}{cur['unit']} | {emoji} {ratio:+.0f}% |\n"
                )
            if missing:
                f.write(f"\n🆕 新增 {len(missing)} 个指标，无基线数据\n")
            if improvements:
                f.write(f"\n✅ {len(improvements)} 个指标有明显改善\n")
            if warnings:
                f.write(f"\n⚠️ {len(warnings)} 个指标超过警告阈值 (+30%)\n")
            if failures:
                f.write(f"\n❌ {len(failures)} 个指标超过失败阈值 (+50%)\n")

    # ── 退出码 ──
    if failures:
        print(f"\n❌ {len(failures)} 个指标严重退化", file=sys.stderr)
        sys.exit(2)
    elif warnings:
        print(f"\n⚠️ {len(warnings)} 个指标轻度退化（不阻断）", file=sys.stderr)
        sys.exit(0)  # 警告不阻断 CI
    else:
        print(f"\n✅ 所有 {len(current)} 个指标在正常范围", file=sys.stderr)
        sys.exit(0)


if __name__ == "__main__":
    main()
