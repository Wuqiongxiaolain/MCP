#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""generate_perf_report.py - 从 bench 结果组装 docs/PERF_REPORT.md

数据源（不重跑 bench）：
  bin/bench_result.json
  tests/bench_baseline.json
  docs/ci_results/bench.exit（可选）

用法:
  python scripts/generate_perf_report.py
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import bench_compare as bc  # noqa: E402

CI_RESULTS = ROOT / "docs" / "ci_results"
RESULT_JSON = ROOT / "bin" / "bench_result.json"
BASELINE_JSON = ROOT / "tests" / "bench_baseline.json"
OUT_MD = ROOT / "docs" / "PERF_REPORT.md"
OUT_JSON = ROOT / "docs" / "PERF_REPORT.json"


def git_cmd(*args: str) -> str:
    try:
        return subprocess.check_output(
            ["git", *args], cwd=ROOT, text=True, stderr=subprocess.DEVNULL
        ).strip()
    except Exception:
        return "unknown"


def read_version() -> str:
    p = ROOT / "VERSION"
    return p.read_text(encoding="utf-8").strip() if p.is_file() else ""


def read_bench_exit() -> Optional[int]:
    p = CI_RESULTS / "bench.exit"
    if not p.is_file():
        return None
    try:
        return int(p.read_text(encoding="utf-8").strip().splitlines()[0])
    except Exception:
        return None


def compare_rows(
    baseline: Dict[str, Any], current: Dict[str, Any]
) -> Tuple[str, List[str], List[str], List[str], List[Dict[str, Any]]]:
    """返回 overall, failures, warnings, improvements, table_rows。"""
    failures: List[str] = []
    warnings: List[str] = []
    improvements: List[str] = []
    rows: List[Dict[str, Any]] = []

    # 与 bench_compare.main 对齐的分段泄漏检查
    first_half = current.get("memory_RSS_repeat_save_1st_half")
    second_half = current.get("memory_RSS_repeat_save_2nd_half")
    if first_half and second_half:
        f_val, f_u = bc.to_canonical(
            float(first_half["value"]), first_half.get("unit", "MB")
        )
        s_val, s_u = bc.to_canonical(
            float(second_half["value"]), second_half.get("unit", "MB")
        )
        if f_u == "MB" and s_u == "MB" and f_val > 0.05 and s_val / f_val >= bc.memory_half_ratio():
            failures.append(
                f"memory_RSS_repeat_save halves: "
                f"1st={f_val:.2f}MB 2nd={s_val:.2f}MB "
                f"(2nd/1st>={bc.memory_half_ratio():.1f}x)"
            )

    abs_before = current.get("memory_RSS_abs_before")
    if abs_before is not None:
        abs_val, abs_u = bc.to_canonical(
            float(abs_before["value"]), abs_before.get("unit", "MB")
        )
        if abs_u == "MB" and abs_val <= 0:
            warnings.append(
                "memory_RSS_abs_before=0：RSS 采样可能失败（增量 0 不可信）"
            )

    for name, cur in sorted(current.items()):
        if name in bc.RETIRED_BENCHMARKS:
            continue
        cur_val, cur_u = bc.to_canonical(float(cur["value"]), cur.get("unit", ""))
        status = "OK"
        vs = "—"
        note = ""
        ratio: Optional[float] = None

        # 绝对 RSS：信息行，区分「增量 0」与「采样失败」
        if bc.is_memory_abs_info(name) and cur_u == "MB":
            vs = "绝对 RSS（信息项，不参与门禁）"
            if cur_val <= 0 and name.endswith("_before"):
                status = "WARN"
            rows.append(
                {
                    "name": name,
                    "value": cur.get("value"),
                    "unit": cur.get("unit", ""),
                    "p95": cur.get("p95"),
                    "baseline": "—",
                    "ratio": None,
                    "status": status,
                    "vs": vs,
                }
            )
            continue

        if bc.is_memory_delta(name) and cur_u == "MB":
            fail_mb = bc.memory_fail_mb()
            warn_mb = bc.memory_warn_mb()
            cur_show = f"{cur['value']:.2f}{cur.get('unit', 'MB')}"
            vs = f"cap WARN={warn_mb:.1f} FAIL={fail_mb:.1f} MB"
            if cur_val >= fail_mb:
                status = "FAIL"
                note = f"{cur_show} ≥ FAIL cap"
                failures.append(f"{name}: {note}")
            elif cur_val >= warn_mb:
                status = "WARN"
                note = f"{cur_show} ≥ WARN cap"
                warnings.append(f"{name}: {note}")
            rows.append(
                {
                    "name": name,
                    "value": cur.get("value"),
                    "unit": cur.get("unit", ""),
                    "p95": cur.get("p95"),
                    "baseline": "—",
                    "ratio": None,
                    "status": status,
                    "vs": vs,
                }
            )
            continue

        if name not in baseline:
            status = "NEW"
            note = "基线中无此项"
            rows.append(
                {
                    "name": name,
                    "value": cur.get("value"),
                    "unit": cur.get("unit", ""),
                    "p95": cur.get("p95"),
                    "baseline": "—",
                    "ratio": None,
                    "status": status,
                    "vs": note,
                }
            )
            continue

        bl = baseline[name]
        cur_val, cur_u, cur_raw, cur_tag = bc.pick_stat(cur, name)
        bl_val, bl_u, bl_raw, bl_tag = bc.pick_stat(bl, name)
        if cur_u != bl_u:
            status = "WARN"
            note = f"单位不一致 {bl.get('unit')} vs {cur.get('unit')}"
            warnings.append(f"{name}: {note}")
            ratio = None
        elif bl_val <= 0:
            ratio = None
            note = "基线≤0，跳过"
        else:
            ratio = cur_val / bl_val
            warn_thr, fail_thr = bc.thresholds_for(name)
            bl_show = bc.format_stat(bl, bl_raw, bl_tag)
            cur_show = bc.format_stat(cur, cur_raw, cur_tag)
            vs = f"{bl_show} → {cur_show} ({ratio:.2f}x)"
            if ratio >= fail_thr:
                status = "FAIL"
                failures.append(f"{name}: {vs}")
            elif ratio >= warn_thr:
                status = "WARN"
                warnings.append(f"{name}: {vs}")
            elif ratio <= bc.IMPROVE_THRESHOLD:
                status = "IMPR"
                improvements.append(f"{name}: {vs}")
            else:
                status = "OK"

        rows.append(
            {
                "name": name,
                "value": cur.get("value"),
                "unit": cur.get("unit", ""),
                "p95": cur.get("p95"),
                "baseline": bl.get("value") if name in baseline else None,
                "baseline_unit": bl.get("unit") if name in baseline else None,
                "ratio": ratio,
                "status": status,
                "vs": vs if vs != "—" else (note or "—"),
            }
        )

    if failures:
        overall = "FAIL"
    elif warnings:
        overall = "WARN"
    else:
        overall = "PASS"

    bench_exit = read_bench_exit()
    if bench_exit not in (None, 0) and overall == "PASS":
        overall = "FAIL"
        failures.append(f"docs/ci_results/bench.exit={bench_exit}（比对未通过）")

    return overall, failures, warnings, improvements, rows


def write_report(
    overall: str,
    failures: List[str],
    warnings: List[str],
    improvements: List[str],
    rows: List[Dict[str, Any]],
    meta: Dict[str, Any],
    note: str,
) -> None:
    n = len(rows)
    fail_n = sum(1 for r in rows if r["status"] == "FAIL")
    warn_n = sum(1 for r in rows if r["status"] == "WARN")

    table_lines = [
        "| 指标 | value | unit | p95 | vs 基线 | 状态 |",
        "|------|-------|------|-----|---------|------|",
    ]
    for r in rows:
        val = r.get("value")
        val_s = f"{val:.4g}" if isinstance(val, (int, float)) else str(val)
        p95 = r.get("p95")
        p95_s = f"{p95:.4g}" if isinstance(p95, (int, float)) else "—"
        vs = str(r.get("vs") or "—").replace("|", "/")
        table_lines.append(
            f"| `{r['name']}` | {val_s} | {r.get('unit', '')} | {p95_s} | {vs} | **{r['status']}** |"
        )

    fail_block = "\n".join(f"- {x}" for x in failures) if failures else "（无）"
    warn_block = "\n".join(f"- {x}" for x in warnings) if warnings else "（无）"
    impr_block = "\n".join(f"- {x}" for x in improvements[:15]) if improvements else "（无）"
    if len(improvements) > 15:
        impr_block += f"\n- … 另有 {len(improvements) - 15} 项改善"

    md = f"""# graphmcp 性能报告

> generated: {meta['time']}  |  branch: `{meta['branch']}`  |  commit: `{meta['commit']}`

## 元信息

| 项 | 值 |
|----|----|
| VERSION | `{meta['version']}` |
| commit | `{meta['commit']}` |
| 生成时间 | {meta['time']} |
| 基线文件 | `tests/bench_baseline.json` |
| 结果文件 | `bin/bench_result.json` |
| CI bench.exit | `{meta.get('bench_exit', 'n/a')}` |

## 结论

| 项 | 值 |
|----|----|
| 总评 | **{overall}** |
| 指标数 | {n}（FAIL={fail_n}, WARN={warn_n}） |
| 说明 | {note} |

- **PASS**：相对基线未破 WARN/FAIL 阈值。
- **WARN**：有退化告警或内存 WARN 帽；不必然阻断（视 CI bench 步骤）。
- **FAIL**：破 FAIL 阈值或 `bench.exit≠0`（连续重试后仍失败则 CI 已阻断）。

### FAIL 项

{fail_block}

### WARN 项

{warn_block}

### 明显改善（节选）

{impr_block}

## 指标摘要

{chr(10).join(table_lines)}

## 何时允许刷新基线

1. 故意的性能优化或算法变更，且已在稳定环境复测。
2. 经人工确认后：GitHub Actions → **Update bench baseline**（`workflow_dispatch`，须勾选确认）。
3. **禁止**在日常 CI 中自动写回基线。

## 相关

- [验收清单 · 性能项](ACCEPTANCE_DOD.md)
- 模板：[templates/PERF_REPORT.md](templates/PERF_REPORT.md)
- 原始数据：`bin/bench_result.json`；比对逻辑：`scripts/bench_compare.py`
"""
    OUT_MD.parent.mkdir(parents=True, exist_ok=True)
    OUT_MD.write_text(md, encoding="utf-8", newline="\n")
    payload = {
        "meta": meta,
        "overall": overall,
        "failures": failures,
        "warnings": warnings,
        "improvements": improvements,
        "rows": rows,
    }
    OUT_JSON.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


def main() -> int:
    meta = {
        "time": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "branch": git_cmd("branch", "--show-current") or "unknown",
        "commit": git_cmd("rev-parse", "--short", "HEAD") or "unknown",
        "version": read_version(),
        "bench_exit": read_bench_exit(),
    }

    if not RESULT_JSON.is_file():
        note = "缺少 bin/bench_result.json（bench 未跑或未产出）"
        write_report("SKIP", [], [], [], [], meta, note)
        # 仍写出占位报告，便于 Artifact；不阻断组装步骤
        print(f"Wrote {OUT_MD.relative_to(ROOT)} (SKIP)")
        return 0

    current = bc.load_results(str(RESULT_JSON))
    if not BASELINE_JSON.is_file():
        note = "缺少基线 tests/bench_baseline.json；仅列出当前结果"
        rows = [
            {
                "name": b["name"],
                "value": b.get("value"),
                "unit": b.get("unit", ""),
                "p95": b.get("p95"),
                "baseline": None,
                "ratio": None,
                "status": "OK",
                "vs": "无基线",
            }
            for b in sorted(
                json.loads(RESULT_JSON.read_text(encoding="utf-8")).get("benchmarks", []),
                key=lambda x: x["name"],
            )
        ]
        write_report("WARN", [], ["无基线文件"], [], rows, meta, note)
        print(f"Wrote {OUT_MD.relative_to(ROOT)} overall=WARN")
        return 0

    baseline = bc.load_results(str(BASELINE_JSON))
    overall, failures, warnings, improvements, rows = compare_rows(baseline, current)
    note = (
        f"相对基线比对完成；IO 敏感项用 p50；阈值见 bench_compare"
        f"（relaxed={bc.bench_relaxed()}）"
    )
    write_report(overall, failures, warnings, improvements, rows, meta, note)
    print(f"Wrote {OUT_MD.relative_to(ROOT)}")
    print(
        f"overall={overall} metrics={len(rows)} "
        f"fail={len(failures)} warn={len(warnings)}"
    )
    # 组装步骤不阻断 CI（bench 步骤本身已负责门禁）
    return 0


if __name__ == "__main__":
    sys.exit(main())
