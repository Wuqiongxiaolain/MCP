#!/usr/bin/env python3
"""bench_compare.py - 性能回归检测脚本（CI 用）

用法：python3 scripts/bench_compare.py <baseline.json> <current.json>

比较当前基准测试结果与基线：
- 延迟类：超出基线 30% → 警告；50% → 失败（敏感指标更宽松）
- 磁盘/IO 敏感指标：优先用 p50 比对（抑制共享 runner 写盘长尾对 mean 的污染）
- 内存增量（memory_RSS_repeat_save_*）：用绝对 MB 上限，不用古董相对基线百分比
- 绝对 RSS（memory_RSS_abs_*）：仅做采样健康检查，不参与相对基线门禁
- 在 GitHub Actions 中输出 workflow 命令以生成注解和摘要

比较前将 ms/us/s/MB 统一换算，避免单位混用导致假退化。

环境变量（可选）：
  GRAPHMCP_MEMORY_FAIL_MB  内存稳定性 FAIL 绝对上限（默认 4）
  GRAPHMCP_MEMORY_WARN_MB  内存稳定性 WARN 绝对上限（默认 3）
  GRAPHMCP_MEMORY_HALF_RATIO  后半段相对前半段的泄漏倍率上限（默认 2.5）
  GRAPHMCP_BENCH_RELAXED=1  或检测到 JENKINS_URL：对磁盘敏感指标再放宽
  GRAPHMCP_BENCH_IO_FAIL_RATIO  放宽模式下 IO 敏感 FAIL 倍率（默认 3.0，即 +200%）
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

# 磁盘 / create 热路径：Jenkins Docker 上相对 GHA 基线常有 1.5～3× 稳定偏差
# 比对优先用 p50，避免偶发长尾抬高 mean 导致假 FAIL
IO_SENSITIVE = {
    "store_save",
    "store_load",
    "mcp_graph_create",
    "mcp_table_create",
    "tableStore_save",
    "tableStore_load",
    "tableStore_list",
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


def bench_relaxed() -> bool:
    """Jenkins 或显式放宽：磁盘敏感指标用更高 FAIL 倍率。"""
    if os.environ.get("GRAPHMCP_BENCH_RELAXED", "").strip() in ("1", "true", "TRUE"):
        return True
    if os.environ.get("JENKINS_URL"):
        return True
    return False


def io_fail_ratio() -> float:
    return float(os.environ.get("GRAPHMCP_BENCH_IO_FAIL_RATIO", "3.0"))


def is_memory_delta(name: str) -> bool:
    """重复 save 的 RSS 增量门禁（MB）。"""
    return name.startswith("memory_RSS_repeat_save_")


def is_memory_abs_info(name: str) -> bool:
    """绝对 RSS 信息项：只做采样健康检查。"""
    return name.startswith("memory_RSS_abs_")


def is_memory_stability(name: str) -> bool:
    """兼容旧调用名：增量类内存稳定性指标。"""
    return is_memory_delta(name)


def is_ci_sensitive(name: str) -> bool:
    return any(s in name for s in CI_SENSITIVE)


def is_io_sensitive(name: str) -> bool:
    return any(s in name for s in IO_SENSITIVE)


def thresholds_for(name: str) -> tuple:
    """返回 (warn_thr, fail_thr)。"""
    if bench_relaxed() and is_io_sensitive(name):
        fail_thr = io_fail_ratio()
        warn_thr = min(2.0, fail_thr * 0.75)
        return warn_thr, fail_thr
    if is_ci_sensitive(name):
        return 1.50, 1.80
    return WARN_THRESHOLD, FAIL_THRESHOLD


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


def pick_stat(entry: dict, name: str) -> tuple:
    """选取比对统计量。

    IO 敏感项优先 p50（与 unit 同一量纲），否则回退 mean(value)。
    返回 (canonical_value, canonical_unit, raw_value, stat_tag)。
    """
    unit = entry.get("unit", "")
    if is_io_sensitive(name) and entry.get("p50") is not None:
        raw = float(entry["p50"])
        canon, cu = to_canonical(raw, unit)
        return canon, cu, raw, "p50"
    raw = float(entry["value"])
    canon, cu = to_canonical(raw, unit)
    return canon, cu, raw, "mean"


def format_stat(entry: dict, raw: float, tag: str) -> str:
    unit = entry.get("unit", "")
    suffix = f" ({tag})" if tag != "mean" else ""
    return f"{raw:.2f}{unit}{suffix}"


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

    # 绝对 RSS=0：采样失败或平台未实现；增量=0 仍可能合法
    abs_before = current.get("memory_RSS_abs_before")
    if abs_before is not None:
        abs_val, abs_u = to_canonical(
            float(abs_before["value"]), abs_before.get("unit", "MB")
        )
        if abs_u == "MB" and abs_val <= 0:
            warnings.append(
                "memory_RSS_abs_before=0：RSS 采样可能失败（增量 0 不可信）"
            )

    for name, cur in sorted(current.items()):
        if name in RETIRED_BENCHMARKS:
            continue

        # 绝对 RSS：信息项，不参与相对基线 / 增量帽
        if is_memory_abs_info(name):
            continue

        cur_val, cur_u = to_canonical(float(cur["value"]), cur.get("unit", ""))

        # 内存增量稳定性：绝对上限
        if is_memory_delta(name) and cur_u == "MB":
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
            continue

        if name not in baseline:
            missing.append(name)
            continue

        bl = baseline[name]
        cur_val, cur_u, cur_raw, cur_tag = pick_stat(cur, name)
        bl_val, bl_u, bl_raw, bl_tag = pick_stat(bl, name)

        if cur_u != bl_u:
            warnings.append(
                f"{name}: 单位不一致 {bl.get('unit')} vs {cur.get('unit')}，跳过比较"
            )
            continue

        if bl_val <= 0:
            continue

        ratio = cur_val / bl_val

        warn_thr, fail_thr = thresholds_for(name)

        bl_show = format_stat(bl, bl_raw, bl_tag)
        cur_show = format_stat(cur, cur_raw, cur_tag)

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

    if bench_relaxed():
        print(
            f"INFO: bench relaxed mode on "
            f"(IO FAIL ratio={io_fail_ratio():.2f}; "
            f"JENKINS_URL/GRAPHMCP_BENCH_RELAXED)"
        )

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
                if is_memory_abs_info(name):
                    f.write(
                        f"| {name} | info | {cur['value']:.2f}{cur.get('unit', '')} | "
                        f"abs RSS |\n"
                    )
                    continue
                if is_memory_delta(name):
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
                cur_val, cur_u, cur_raw, cur_tag = pick_stat(cur, name)
                bl_val, bl_u, bl_raw, bl_tag = pick_stat(bl, name)
                if bl_val <= 0 or cur_u != bl_u:
                    f.write(
                        f"| {name} | {format_stat(bl, bl_raw, bl_tag)} | "
                        f"{format_stat(cur, cur_raw, cur_tag)} | n/a |\n"
                    )
                    continue
                ratio = (cur_val - bl_val) / bl_val * 100
                f.write(
                    f"| {name} | {format_stat(bl, bl_raw, bl_tag)} | "
                    f"{format_stat(cur, cur_raw, cur_tag)} | {ratio:+.0f}% |\n"
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
