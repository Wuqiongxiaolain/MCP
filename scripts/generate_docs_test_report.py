#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""generate_docs_test_report.py - 汇总单元/冒烟/性能等测试，写入 docs/TEST_REPORT.*

用法:
  # CI 推荐：只组装，不重跑（读 docs/ci_results/ 与既有制品）
  python scripts/generate_docs_test_report.py --from-ci

  # 本地调试：完整重跑各套件（非默认路径）
  python scripts/generate_docs_test_report.py --rerun
  python scripts/generate_docs_test_report.py --rerun --skip-export
  python scripts/generate_docs_test_report.py --rerun --bin bin/graphmcp.exe

依赖 (--from-ci): docs/ci_results/<suite>.{log,exit}、docs/SMOKE_REPORT.md、
      bin/bench_result.json、examples/example_testout/TEST_REPORT.json 等。
依赖 (--rerun): 已编译二进制 + bash；可选 graphmcp 生成汇总图 SVG。
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field, asdict
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple


ROOT = Path(__file__).resolve().parents[1]


@dataclass
class SuiteResult:
    name: str
    status: str  # PASS | FAIL | SKIP | WARN
    passed: int = 0
    failed: int = 0
    skipped: int = 0
    duration_s: float = 0.0
    note: str = ""
    details: List[Dict[str, Any]] = field(default_factory=list)
    log_excerpt: str = ""


def which_bash() -> Optional[str]:
    # 优先 Git Bash（Windows 上 System32/WindowsApps 的 bash 常为 WSL 桩，exit 127）
    for p in (
        r"C:\Program Files\Git\bin\bash.exe",
        r"C:\Program Files\Git\usr\bin\bash.exe",
        r"C:\Program Files (x86)\Git\bin\bash.exe",
    ):
        if Path(p).is_file():
            return p
    for cand in ("bash", "bash.exe"):
        found = shutil.which(cand)
        if not found:
            continue
        low = found.lower().replace("/", "\\")
        if "windowsapps" in low or low.endswith("system32\\bash.exe"):
            continue
        return found
    return None


def find_bin(name_stem: str, explicit: Optional[str] = None) -> Optional[Path]:
    if explicit:
        p = Path(explicit)
        if p.is_file():
            return p
    for ext in (".exe", ""):
        p = ROOT / "bin" / f"{name_stem}{ext}"
        if p.is_file():
            return p
    return None


def decode_subprocess_bytes(data: bytes) -> str:
    """Windows 子进程常混用 UTF-8 / GBK；优先无损解码，避免写入 U+FFFD 乱码。"""
    if not data:
        return ""
    if data.startswith(b"\xef\xbb\xbf"):
        data = data[3:]
    for enc in ("utf-8-sig", "utf-8", "gbk", "cp936"):
        try:
            return data.decode(enc)
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", errors="replace")


def sanitize_report_text(text: str) -> str:
    """清理日志摘录中的替换符与箭头显示问题。"""
    if not text:
        return ""
    if "\ufffd" in text:
        try:
            repaired = text.encode("latin1", errors="ignore").decode("gbk", errors="ignore")
            if repaired.count("\ufffd") < text.count("\ufffd"):
                text = repaired
        except Exception:
            pass
    text = text.replace("\ufffd", "")
    for bad, good in (
        ("\u2192", "->"),
        ("\u2190", "<-"),
        ("\u21d2", "=>"),
    ):
        text = text.replace(bad, good)
    text = re.sub(r"\x1b\[[0-9;]*m", "", text)
    return text


def run_cmd(
    argv: List[str],
    *,
    cwd: Optional[Path] = None,
    env: Optional[Dict[str, str]] = None,
    timeout: Optional[int] = None,
) -> Tuple[int, str, float]:
    t0 = datetime.now()
    merged = os.environ.copy()
    merged.setdefault("PYTHONIOENCODING", "utf-8")
    merged.setdefault("PYTHONUTF8", "1")
    merged.setdefault("LC_ALL", "C.UTF-8")
    merged.setdefault("LANG", "C.UTF-8")
    if env:
        merged.update(env)
    try:
        proc = subprocess.run(
            argv,
            cwd=str(cwd or ROOT),
            env=merged,
            capture_output=True,
            timeout=timeout,
        )
        out = decode_subprocess_bytes(proc.stdout or b"")
        err = decode_subprocess_bytes(proc.stderr or b"")
        if err:
            out = out + ("\n" if out else "") + err
        out = sanitize_report_text(out)
        dt = (datetime.now() - t0).total_seconds()
        return proc.returncode, out, dt
    except subprocess.TimeoutExpired as e:
        dt = (datetime.now() - t0).total_seconds()
        out = decode_subprocess_bytes(e.stdout or b"")
        err = decode_subprocess_bytes(e.stderr or b"")
        msg = out + (("\n" + err) if err else "") + f"\n[timeout after {timeout}s]"
        return 124, sanitize_report_text(msg), dt
    except FileNotFoundError as e:
        dt = (datetime.now() - t0).total_seconds()
        return 127, str(e), dt


def parse_unit_counts(text: str) -> Tuple[int, int]:
    """解析 'tests: N passed, M failed' / 'version tests: ...' / 'cursor tests: ...'"""
    passed = failed = 0
    for m in re.finditer(
        r"(?:tests|version tests|cursor tests):\s*(\d+)\s+passed,\s*(\d+)\s+failed",
        text,
        re.I,
    ):
        passed += int(m.group(1))
        failed += int(m.group(2))
    return passed, failed


def parse_smoke_counts(text: str) -> Tuple[int, int]:
    # Results: N passed, M failed  或  - passed: N
    m = re.search(r"Results:.*?(\d+)\s+passed.*?(\d+)\s+failed", text, re.S | re.I)
    if m:
        return int(m.group(1)), int(m.group(2))
    m2 = re.search(r"- passed:\s*(\d+)", text)
    m3 = re.search(r"- failed:\s*(\d+)", text)
    if m2 and m3:
        return int(m2.group(1)), int(m3.group(1))
    # MCP smoke: PASSED= / FAILED=
    m4 = re.search(r"PASSED[=:\s]+(\d+)", text)
    m5 = re.search(r"FAILED[=:\s]+(\d+)", text)
    if m4 and m5:
        return int(m4.group(1)), int(m5.group(1))
    return 0, 0


def excerpt(text: str, max_lines: int = 40) -> str:
    text = sanitize_report_text(text)
    lines = text.strip().splitlines()
    if len(lines) <= max_lines:
        return text.strip()
    head = lines[: max_lines // 2]
    tail = lines[-(max_lines // 2) :]
    return "\n".join(head + ["...", *tail])


def run_unit_suite(stem: str, label: str) -> SuiteResult:
    bin_path = find_bin(stem)
    if not bin_path:
        return SuiteResult(label, "SKIP", note=f"binary not found: bin/{stem}")
    code, out, dt = run_cmd([str(bin_path)], timeout=600)
    p, f = parse_unit_counts(out)
    if p == 0 and f == 0 and code == 0:
        # 兜底：无摘要行时按退出码
        status = "PASS"
        p = 1
    elif f > 0 or code != 0:
        status = "FAIL"
        if f == 0 and code != 0:
            f = 1
    else:
        status = "PASS"
    return SuiteResult(
        label,
        status,
        passed=p,
        failed=f,
        duration_s=dt,
        note=f"exit={code}",
        log_excerpt=excerpt(out, 30),
    )


def run_bash_script(script_rel: str, bin_path: Path, label: str, timeout: int = 900) -> SuiteResult:
    bash = which_bash()
    if not bash:
        return SuiteResult(label, "SKIP", note="bash not found (need Git Bash)")
    script = ROOT / script_rel
    if not script.is_file():
        return SuiteResult(label, "SKIP", note=f"missing {script_rel}")
    # mcp_smoke 依赖 jq
    if "mcp_smoke" in script_rel.replace("\\", "/"):
        if not shutil.which("jq") and not Path(
            r"C:\Program Files\Git\usr\bin\jq.exe"
        ).is_file():
            return SuiteResult(
                label,
                "SKIP",
                note="jq not found (mcp_smoke.sh requires jq)",
            )
    code, out, dt = run_cmd(
        [bash, str(script), str(bin_path)],
        timeout=timeout,
        env={"GRAPHMCP_NO_LAUNCH": "1"},
    )
    if "jq is required" in out:
        return SuiteResult(
            label,
            "SKIP",
            duration_s=dt,
            note="jq not found (mcp_smoke.sh requires jq)",
            log_excerpt=excerpt(out, 20),
        )
    p, f = parse_smoke_counts(out)
    if p == 0 and f == 0:
        if code == 0:
            status, p = "PASS", 1
        else:
            status, f = "FAIL", 1
    elif f > 0 or code != 0:
        status = "FAIL"
    else:
        status = "PASS"
    return SuiteResult(
        label,
        status,
        passed=p,
        failed=f,
        duration_s=dt,
        note=f"exit={code}; script={script_rel}",
        log_excerpt=excerpt(out, 40),
    )


def run_perf_smoke(bin_path: Path) -> SuiteResult:
    py = sys.executable
    script = ROOT / "scripts" / "mcp_perf_smoke.py"
    if not script.is_file():
        return SuiteResult("MCP 性能冒烟", "SKIP", note="scripts/mcp_perf_smoke.py missing")
    code, out, dt = run_cmd([py, str(script), str(bin_path)], timeout=300)
    # 解析 ok/fail 行
    oks = len(re.findall(r"\bok\b|PASS|passed", out, re.I))
    fails = len(re.findall(r"\bFAIL\b|AssertionError|error:", out, re.I))
    if code != 0:
        return SuiteResult(
            "MCP 性能冒烟",
            "FAIL",
            passed=max(0, oks - fails),
            failed=max(1, fails),
            duration_s=dt,
            note=f"exit={code}",
            log_excerpt=excerpt(out, 40),
        )
    return SuiteResult(
        "MCP 性能冒烟",
        "PASS",
        passed=max(1, oks),
        failed=0,
        duration_s=dt,
        note=f"exit={code}",
        log_excerpt=excerpt(out, 30),
    )


def run_bench(compare: bool = True) -> SuiteResult:
    bench = find_bin("graphmcp_bench")
    if not bench:
        return SuiteResult("微基准性能测试", "SKIP", note="bin/graphmcp_bench not found")
    out_json = ROOT / "bin" / "bench_result.json"
    out_json.parent.mkdir(parents=True, exist_ok=True)
    code, out, dt = run_cmd([str(bench)], timeout=600)
    # stdout 应为 JSON
    raw = out
    # 有时 stderr 混入；尝试提取最外层 JSON
    try:
        start = raw.index("{")
        end = raw.rindex("}") + 1
        data = json.loads(raw[start:end])
        out_json.write_text(
            json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        n = len(data.get("benchmarks", []))
    except Exception as e:
        return SuiteResult(
            "微基准性能测试",
            "FAIL",
            failed=1,
            duration_s=dt,
            note=f"JSON parse failed: {e}; exit={code}",
            log_excerpt=excerpt(raw, 20),
        )

    details: List[Dict[str, Any]] = []
    status = "PASS" if code == 0 else "FAIL"
    note = f"metrics={n}; wrote {out_json.relative_to(ROOT)}"
    warn = fail = 0
    if compare:
        baseline = ROOT / "tests" / "bench_baseline.json"
        cmp_script = ROOT / "scripts" / "bench_compare.py"
        if baseline.is_file() and cmp_script.is_file():
            ccode, cout, _ = run_cmd(
                [sys.executable, str(cmp_script), str(baseline), str(out_json)],
                timeout=60,
            )
            # 粗略统计
            warn = len(re.findall(r"WARN|warning|退化", cout, re.I))
            fail = len(re.findall(r"FAIL|::error|失败", cout, re.I))
            if ccode != 0:
                # 本地/CI runner 差异常导致相对基线失败：报告记 WARN，不阻断「bench 已跑通」
                status = "WARN"
                fail = max(fail, 1)
            elif warn > 0:
                status = "WARN"
            details.append(
                {
                    "compare_exit": ccode,
                    "excerpt": excerpt(cout, 50),
                }
            )
            note += f"; compare exit={ccode} (相对 CI 基线；本地波动记 WARN)"
        else:
            note += "; baseline/compare skipped"

    # 摘录若干指标
    try:
        benches = data.get("benchmarks", [])[:8]
        for b in benches:
            details.append(
                {
                    "name": b.get("name"),
                    "value": b.get("value"),
                    "unit": b.get("unit"),
                    "p95": b.get("p95"),
                }
            )
    except Exception:
        pass

    return SuiteResult(
        "微基准性能测试",
        status,
        passed=n if status != "FAIL" else max(0, n - fail),
        failed=0 if status != "FAIL" else max(1, fail),
        skipped=warn + (fail if status == "WARN" else 0),
        duration_s=dt,
        note=note,
        details=details,
        log_excerpt="",
    )


def run_export_testout(bin_path: Path) -> SuiteResult:
    r = run_bash_script(
        "scripts/export-example-testout.sh",
        bin_path,
        "样例全量导出回归",
        timeout=1800,
    )
    # 合并 scripts 写出的 TEST_REPORT.json 计数
    report_json = ROOT / "examples" / "example_testout" / "TEST_REPORT.json"
    if report_json.is_file():
        try:
            data = json.loads(report_json.read_text(encoding="utf-8"))
            ce = data.get("convert_export", {})
            p = int(ce.get("passed", 0))
            f = int(ce.get("failed", 0))
            sk = int(ce.get("skipped", 0))
            smoke_fail = int(ce.get("smoke_failures", 0))
            if f > 0 or smoke_fail > 0:
                status = "FAIL"
            elif p > 0:
                status = "PASS"
            else:
                status = r.status
            r.passed = p
            r.failed = f + smoke_fail
            r.skipped = sk
            r.status = status
            r.note += (
                f"; convert PASS={p} FAIL={f} SKIP={sk}; "
                f"see examples/example_testout/TEST_REPORT.md"
            )
            r.details.append({"source": str(report_json.relative_to(ROOT)), **ce})
        except Exception as e:
            r.note += f"; parse TEST_REPORT.json failed: {e}"
    return r


def overall_status(suites: List[SuiteResult]) -> str:
    if any(s.status == "FAIL" for s in suites):
        return "FAIL"
    if any(s.status == "WARN" for s in suites):
        return "WARN"
    if all(s.status == "SKIP" for s in suites):
        return "SKIP"
    return "PASS"


CI_RESULTS = ROOT / "docs" / "ci_results"


def read_exit_code(name: str) -> Optional[int]:
    p = CI_RESULTS / f"{name}.exit"
    if not p.is_file():
        return None
    try:
        return int(p.read_text(encoding="utf-8").strip().splitlines()[0])
    except Exception:
        return None


def read_ci_log(name: str) -> str:
    p = CI_RESULTS / f"{name}.log"
    if not p.is_file():
        return ""
    return sanitize_report_text(p.read_text(encoding="utf-8", errors="replace"))


def suite_from_unit_log(name: str, label: str) -> SuiteResult:
    """从 ci_capture 产物组装单元测试套件结果。"""
    out = read_ci_log(name)
    code = read_exit_code(name)
    if code is None and not out:
        return SuiteResult(label, "SKIP", note=f"missing docs/ci_results/{name}.{{log,exit}}")
    if code is None:
        code = 1 if "failed" in out.lower() and re.search(r"\d+\s+failed", out) else 0
    p, f = parse_unit_counts(out)
    if p == 0 and f == 0 and code == 0:
        status, p = "PASS", 1
    elif f > 0 or code != 0:
        status = "FAIL"
        if f == 0:
            f = 1
    else:
        status = "PASS"
    return SuiteResult(
        label,
        status,
        passed=p,
        failed=f,
        note=f"from-ci; exit={code}",
        log_excerpt=excerpt(out, 30),
    )


def suite_from_smoke_artifacts(label: str, log_name: str, report_fallback: Optional[Path] = None) -> SuiteResult:
    """从日志 / SMOKE_REPORT 组装冒烟结果。"""
    out = read_ci_log(log_name)
    code = read_exit_code(log_name)
    if not out and report_fallback and report_fallback.is_file():
        out = sanitize_report_text(report_fallback.read_text(encoding="utf-8", errors="replace"))
    if code is None and not out:
        return SuiteResult(label, "SKIP", note=f"missing docs/ci_results/{log_name}.*")
    if "jq is required" in out or "jq not found" in out.lower():
        return SuiteResult(label, "SKIP", note="jq not found (mcp_smoke.sh requires jq)", log_excerpt=excerpt(out, 20))
    if code is None:
        code = 0 if "ALL PASSED" in out or re.search(r"\b0 failed\b", out) else 1
    p, f = parse_smoke_counts(out)
    if p == 0 and f == 0:
        if code == 0:
            status, p = "PASS", 1
        else:
            status, f = "FAIL", 1
    elif f > 0 or code != 0:
        status = "FAIL"
    else:
        status = "PASS"
    return SuiteResult(
        label,
        status,
        passed=p,
        failed=f,
        note=f"from-ci; exit={code}",
        log_excerpt=excerpt(out, 40),
    )


def suite_from_perf_log() -> SuiteResult:
    out = read_ci_log("perf-smoke")
    code = read_exit_code("perf-smoke")
    if code is None and not out:
        return SuiteResult("MCP 性能冒烟", "SKIP", note="missing docs/ci_results/perf-smoke.*")
    if code is None:
        code = 0 if "PASS" in out and "FAIL" not in out else 1
    oks = len(re.findall(r"\bok\b|PASS|passed", out, re.I))
    fails = len(re.findall(r"\bFAIL\b|AssertionError|error:", out, re.I))
    if code != 0:
        return SuiteResult(
            "MCP 性能冒烟",
            "FAIL",
            passed=max(0, oks - fails),
            failed=max(1, fails),
            note=f"from-ci; exit={code}",
            log_excerpt=excerpt(out, 40),
        )
    return SuiteResult(
        "MCP 性能冒烟",
        "PASS",
        passed=max(1, oks),
        failed=0,
        note=f"from-ci; exit={code}",
        log_excerpt=excerpt(out, 30),
    )


def suite_from_bench_artifacts() -> SuiteResult:
    """读 bin/bench_result.json；可选比对日志或现场跑 compare（不重跑 bench）。"""
    out_json = ROOT / "bin" / "bench_result.json"
    code = read_exit_code("bench")
    compare_log = read_ci_log("bench-compare")
    if not out_json.is_file():
        # 兼容：bench 日志里可能嵌了 JSON
        raw = read_ci_log("bench")
        if raw:
            try:
                start, end = raw.index("{"), raw.rindex("}") + 1
                data = json.loads(raw[start:end])
                out_json.parent.mkdir(parents=True, exist_ok=True)
                out_json.write_text(
                    json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
                )
            except Exception:
                return SuiteResult(
                    "微基准性能测试",
                    "FAIL" if (code or 1) != 0 else "SKIP",
                    failed=1 if code not in (0, None) else 0,
                    note="from-ci; bench_result.json missing",
                    log_excerpt=excerpt(raw, 20),
                )
        else:
            return SuiteResult("微基准性能测试", "SKIP", note="missing bin/bench_result.json")

    try:
        data = json.loads(out_json.read_text(encoding="utf-8"))
        n = len(data.get("benchmarks", []))
    except Exception as e:
        return SuiteResult(
            "微基准性能测试",
            "FAIL",
            failed=1,
            note=f"from-ci; JSON parse failed: {e}",
        )

    details: List[Dict[str, Any]] = []
    status = "PASS" if (code in (0, None)) else "FAIL"
    note = f"from-ci; metrics={n}; {out_json.relative_to(ROOT)}"
    warn = fail = 0

    cout = compare_log
    ccode: Optional[int] = read_exit_code("bench-compare")
    if not cout:
        baseline = ROOT / "tests" / "bench_baseline.json"
        cmp_script = ROOT / "scripts" / "bench_compare.py"
        if baseline.is_file() and cmp_script.is_file():
            ccode, cout, _ = run_cmd(
                [sys.executable, str(cmp_script), str(baseline), str(out_json)],
                timeout=60,
            )
    if cout:
        warn = len(re.findall(r"WARN|warning|退化", cout, re.I))
        fail = len(re.findall(r"FAIL|::error|失败", cout, re.I))
        if ccode is not None and ccode != 0:
            status = "WARN"
            fail = max(fail, 1)
        elif warn > 0:
            status = "WARN"
        details.append({"compare_exit": ccode, "excerpt": excerpt(cout, 50)})
        note += f"; compare exit={ccode} (相对 CI 基线；本地波动记 WARN)"

    try:
        for b in data.get("benchmarks", [])[:8]:
            details.append(
                {
                    "name": b.get("name"),
                    "value": b.get("value"),
                    "unit": b.get("unit"),
                    "p95": b.get("p95"),
                }
            )
    except Exception:
        pass

    return SuiteResult(
        "微基准性能测试",
        status,
        passed=n if status != "FAIL" else max(0, n - fail),
        failed=0 if status != "FAIL" else max(1, fail),
        skipped=warn + (fail if status == "WARN" else 0),
        note=note,
        details=details,
    )


def suite_from_export_artifacts() -> SuiteResult:
    """只读 examples/example_testout/TEST_REPORT.json，不重跑导出。"""
    report_json = ROOT / "examples" / "example_testout" / "TEST_REPORT.json"
    code = read_exit_code("export-testout")
    log = read_ci_log("export-testout")
    if not report_json.is_file():
        if code is None and not log:
            return SuiteResult("样例全量导出回归", "SKIP", note="missing export artifacts")
        status = "PASS" if code == 0 else ("FAIL" if code else "SKIP")
        return SuiteResult(
            "样例全量导出回归",
            status,
            passed=1 if status == "PASS" else 0,
            failed=1 if status == "FAIL" else 0,
            note=f"from-ci; exit={code}; no TEST_REPORT.json",
            log_excerpt=excerpt(log, 30),
        )
    try:
        data = json.loads(report_json.read_text(encoding="utf-8"))
        ce = data.get("convert_export", {})
        p = int(ce.get("passed", 0))
        f = int(ce.get("failed", 0))
        sk = int(ce.get("skipped", 0))
        smoke_fail = int(ce.get("smoke_failures", 0))
        if f > 0 or smoke_fail > 0 or (code is not None and code != 0):
            status = "FAIL"
        elif p > 0:
            status = "PASS"
        else:
            status = "SKIP"
        return SuiteResult(
            "样例全量导出回归",
            status,
            passed=p,
            failed=f + smoke_fail,
            skipped=sk,
            note=(
                f"from-ci; exit={code}; convert PASS={p} FAIL={f} SKIP={sk}; "
                "see examples/example_testout/TEST_REPORT.md"
            ),
            details=[{"source": str(report_json.relative_to(ROOT)), **ce}],
            log_excerpt=excerpt(log, 20) if log else "",
        )
    except Exception as e:
        return SuiteResult(
            "样例全量导出回归",
            "FAIL",
            failed=1,
            note=f"from-ci; parse TEST_REPORT.json failed: {e}",
        )


def assemble_from_ci() -> List[SuiteResult]:
    """消费 CI 已跑产物，组装套件列表（不重跑测试）。"""
    smoke_md = ROOT / "docs" / "SMOKE_REPORT.md"
    return [
        suite_from_unit_log("unit-main", "单元测试（主）"),
        suite_from_unit_log("unit-version", "单元测试（版本）"),
        suite_from_unit_log("unit-cursor", "单元测试（游标）"),
        suite_from_smoke_artifacts("CLI 冒烟", "smoke", smoke_md),
        suite_from_smoke_artifacts("MCP 协议冒烟", "mcp-smoke"),
        suite_from_smoke_artifacts("表协作冒烟", "table-smoke"),
        suite_from_perf_log(),
        suite_from_bench_artifacts(),
        suite_from_export_artifacts(),
    ]


def write_text_utf8(path: Path, text: str) -> None:
    """强制 UTF-8 无 BOM，避免被识别为二进制/乱码。"""
    path.write_text(text, encoding="utf-8", newline="\n")


def status_color(status: str) -> Tuple[str, str]:
    # fill, stroke
    return {
        "PASS": ("#e8f7ec", "#2d8a4e"),
        "FAIL": ("#fde8e8", "#c0392b"),
        "WARN": ("#fff7e0", "#c9a227"),
        "SKIP": ("#f0f2f5", "#7a8494"),
    }.get(status, ("#eef4ff", "#4a72b8"))


def generate_summary_svg(suites: List[SuiteResult], overall: str, gm: Path) -> Optional[Path]:
    """用手摆坐标的 model + graphmcp 导出汇总图。

    原理：store 放在 docs/ci_results/，避免污染已提交的 docs/diagrams/doc-figures。
    """
    store = ROOT / "docs" / "ci_results" / "diagram-store"
    store.mkdir(parents=True, exist_ok=True)
    svg_out = ROOT / "docs" / "images" / "test-report-summary.svg"
    svg_out.parent.mkdir(parents=True, exist_ok=True)

    # 节点：overall + 各 suite 一排
    nodes = []
    edges = []
    fill_o, stroke_o = status_color(overall)
    nodes.append(
        {
            "id": "overall",
            "label": f"测试汇总<br/>{overall}",
            "shape": "rect",
            "fillColor": fill_o,
            "strokeColor": stroke_o,
            "x": 320,
            "y": 20,
            "w": 160,
            "h": 56,
        }
    )
    # 两行排布
    cols = 3
    x0, y0, dx, dy = 40, 120, 220, 120
    for i, s in enumerate(suites):
        col, row = i % cols, i // cols
        fill, stroke = status_color(s.status)
        short = s.name
        if len(short) > 18:
            short = short[:16] + "…"
        nid = f"s{i}"
        nodes.append(
            {
                "id": nid,
                "label": f"{short}<br/>{s.status}",
                "shape": "rect",
                "fillColor": fill,
                "strokeColor": stroke,
                "x": x0 + col * dx,
                "y": y0 + row * dy,
                "w": 180,
                "h": 56,
            }
        )
        edges.append(
            {
                "id": f"e{i}",
                "from": "overall",
                "to": nid,
                "style": "solid",
                "arrow": "arrow",
            }
        )

    model = {
        "id": "test-report-summary",
        "name": "测试报告汇总",
        "type": "flowchart",
        "laidOut": True,
        "nodes": nodes,
        "edges": edges,
    }
    model_path = store / "test-report-summary.model.json"
    model_path.write_text(
        json.dumps(model, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    # 覆盖入库
    run_cmd(
        [
            str(gm),
            "store",
            "delete",
            "--id",
            "test-report-summary",
            "--store",
            str(store),
            "--force",
        ],
        timeout=30,
    )
    code, out, _ = run_cmd(
        [
            str(gm),
            "create",
            "from-model",
            "--file",
            str(model_path),
            "--id",
            "test-report-summary",
            "--name",
            "测试报告汇总",
            "--no-layout",
            "--store",
            str(store),
        ],
        timeout=60,
    )
    if code != 0:
        print(f"[warn] graph create failed: {out}", file=sys.stderr)
        return None
    code2, out2, _ = run_cmd(
        [
            str(gm),
            "export",
            "to-svg",
            "--id",
            "test-report-summary",
            "--store",
            str(store),
            "-o",
            str(svg_out),
        ],
        timeout=60,
    )
    if code2 != 0:
        print(f"[warn] svg export failed: {out2}", file=sys.stderr)
        return None
    # 同步 model
    run_cmd(
        [
            str(gm),
            "export",
            "to-model",
            "--id",
            "test-report-summary",
            "--store",
            str(store),
            "-o",
            str(model_path),
        ],
        timeout=30,
    )
    lock = store / ".store.lock"
    if lock.exists():
        try:
            lock.unlink()
        except OSError:
            pass
    return svg_out


def write_markdown(
    path: Path,
    meta: Dict[str, Any],
    suites: List[SuiteResult],
    overall: str,
    svg_rel: Optional[str],
) -> None:
    lines: List[str] = []
    lines += [
        "# graphmcp 测试报告",
        "",
        f"> generated: {meta['time']}  |  branch: `{meta['branch']}`  |  commit: `{meta['commit']}`"
        + (f"  |  mode: `{meta['mode']}`" if meta.get("mode") else ""),
        "",
        f"**总体结论：{overall}**",
        "",
    ]
    if svg_rel:
        lines += [
            f"汇总图（graphmcp 导出，图 id=`test-report-summary`）：",
            "",
            f"![测试汇总]({svg_rel})",
            "",
        ]
    lines += [
        "## 环境",
        "",
        f"| 项 | 值 |",
        f"|----|----|",
        f"| 主机 OS | {meta['os']} |",
        f"| Python | {meta['python']} |",
        f"| graphmcp | `{meta['bin']}` |",
        f"| VERSION | {meta.get('version', '')} |",
        f"| bash | {meta.get('bash', 'n/a')} |",
        "",
        "## 套件一览",
        "",
        "| 套件 | 状态 | PASS | FAIL | SKIP/WARN | 耗时 (s) | 说明 |",
        "|------|------|------|------|-----------|----------|------|",
    ]
    for s in suites:
        lines.append(
            f"| {s.name} | **{s.status}** | {s.passed} | {s.failed} | {s.skipped} | "
            f"{s.duration_s:.1f} | {sanitize_report_text(s.note)} |"
        )

    lines += ["", "## 分项详情", ""]
    for s in suites:
        lines += [
            f"### {s.name}",
            "",
            f"- 状态：**{s.status}**",
            f"- 说明：{sanitize_report_text(s.note)}",
            "",
        ]
        if s.details:
            # 表格或代码块
            if s.name.startswith("微基准") and any("name" in d and "value" in d for d in s.details):
                metric_rows = [d for d in s.details if "name" in d and "value" in d]
                compare_rows = [d for d in s.details if "compare_exit" in d]
                if metric_rows:
                    lines += [
                        "| 指标 | value | unit | p95 |",
                        "|------|-------|------|-----|",
                    ]
                    for d in metric_rows:
                        lines.append(
                            f"| {d.get('name')} | {d.get('value')} | {d.get('unit')} | {d.get('p95')} |"
                        )
                    lines.append("")
                for d in compare_rows:
                    lines += [
                        "基线比对摘录：",
                        "",
                        "```",
                        d.get("excerpt", ""),
                        "```",
                        "",
                    ]
            else:
                lines += ["```json", json.dumps(s.details, ensure_ascii=False, indent=2), "```", ""]
        if s.log_excerpt:
            lines += ["日志摘录：", "", "```", s.log_excerpt, "```", ""]

    lines += [
        "## 如何获取报告",
        "",
        "报告由 **CI 汇总导出**（默认不在本机重跑）：",
        "",
        "```sh",
        "# GitHub Actions：下载 Artifact「docs-test-report-<run>」",
        "gh run download <run-id> -n docs-test-report-<n>",
        "",
        "# Jenkins：Build → Artifacts → docs/TEST_REPORT.md",
        "```",
        "",
        "CI 内组装（只读 `docs/ci_results/` 等制品）：",
        "",
        "```sh",
        "python scripts/generate_docs_test_report.py --from-ci",
        "```",
        "",
        "本地完整重跑（调试用）：",
        "",
        "```sh",
        "python scripts/generate_docs_test_report.py --rerun",
        "python scripts/generate_docs_test_report.py --rerun --skip-export",
        "```",
        "",
        "## 相关产物",
        "",
        "- 本报告：`docs/TEST_REPORT.md` / `docs/TEST_REPORT.json`（gitignore，见 CI Artifact）",
        "- CLI 冒烟：`docs/SMOKE_REPORT.md`",
        "- 性能结果：`bin/bench_result.json`（相对 `tests/bench_baseline.json`）",
        "- 样例导出：`examples/example_testout/TEST_REPORT.md`",
        "- 捕获日志：`docs/ci_results/`",
        "",
    ]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def write_report_outputs(
    meta: Dict[str, Any],
    suites: List[SuiteResult],
    overall: str,
    *,
    skip_diagram: bool,
    gm: Optional[Path],
) -> int:
    svg_rel = None
    if not skip_diagram and gm:
        print("== summary diagram ==")
        svg = generate_summary_svg(suites, overall, gm)
        if svg:
            svg_rel = "images/test-report-summary.svg"

    md_path = ROOT / "docs" / "TEST_REPORT.md"
    json_path = ROOT / "docs" / "TEST_REPORT.json"
    write_markdown(md_path, meta, suites, overall, svg_rel)

    payload = {
        "meta": meta,
        "overall": overall,
        "suites": [asdict(s) for s in suites],
        "summary_svg": svg_rel,
    }
    json_path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    print("")
    print(f"Wrote {md_path.relative_to(ROOT)}")
    print(f"Wrote {json_path.relative_to(ROOT)}")
    if svg_rel:
        print(f"Wrote docs/{svg_rel}")
    print(f"Overall: {overall}")
    return 0 if overall != "FAIL" else 1


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate docs/TEST_REPORT.md")
    ap.add_argument(
        "--from-ci",
        action="store_true",
        help="assemble report from docs/ci_results/ and existing artifacts (no re-run)",
    )
    ap.add_argument(
        "--rerun",
        action="store_true",
        help="locally re-run all suites (debug only; CI should use --from-ci)",
    )
    ap.add_argument("--bin", help="path to graphmcp binary")
    ap.add_argument("--skip-export", action="store_true", help="(--rerun) skip example export")
    ap.add_argument("--skip-mcp-smoke", action="store_true", help="(--rerun) skip MCP smoke")
    ap.add_argument("--skip-diagram", action="store_true", help="skip graphmcp summary SVG")
    args = ap.parse_args()

    if not args.from_ci and not args.rerun:
        # 默认：有 ci_results 则组装，否则提示使用 --from-ci / --rerun
        if (CI_RESULTS / "unit-main.exit").is_file() or (CI_RESULTS / "smoke.exit").is_file():
            args.from_ci = True
        else:
            print(
                "error: 请指定 --from-ci（CI 组装）或 --rerun（本地重跑）。"
                "默认报告由 CI Artifact 提供，不再依赖本机汇总。",
                file=sys.stderr,
            )
            return 2

    os.chdir(ROOT)
    gm = find_bin("graphmcp", args.bin)
    version = ""
    vp = ROOT / "VERSION"
    if vp.is_file():
        version = vp.read_text(encoding="utf-8").strip()

    meta = {
        "time": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "branch": subprocess.getoutput("git branch --show-current") or "unknown",
        "commit": subprocess.getoutput("git rev-parse --short HEAD") or "unknown",
        "os": sys.platform,
        "python": sys.version.split()[0],
        "bin": str(gm) if gm else "missing",
        "version": version,
        "bash": which_bash() or "missing",
        "mode": "from-ci" if args.from_ci else "rerun",
    }

    if args.from_ci:
        print("== assemble from CI artifacts ==")
        suites = assemble_from_ci()
        overall = overall_status(suites)
        # 组装步骤不二次阻断 CI：始终写报告，退出 0
        write_report_outputs(meta, suites, overall, skip_diagram=args.skip_diagram, gm=gm)
        return 0

    # --rerun：完整重跑
    suites: List[SuiteResult] = []
    print("== unit: graphmcp_tests ==")
    suites.append(run_unit_suite("graphmcp_tests", "单元测试（主）"))
    print("== unit: version ==")
    suites.append(run_unit_suite("graphmcp_version_tests", "单元测试（版本）"))
    print("== unit: cursor ==")
    suites.append(run_unit_suite("graphmcp_cursor_tests", "单元测试（游标）"))

    if not gm:
        print("[warn] graphmcp binary missing; smoke/bench may be limited", file=sys.stderr)
        suites.append(SuiteResult("CLI 冒烟", "SKIP", note="graphmcp missing"))
        suites.append(SuiteResult("MCP 协议冒烟", "SKIP", note="graphmcp missing"))
        suites.append(SuiteResult("表协作冒烟", "SKIP", note="graphmcp missing"))
        suites.append(SuiteResult("MCP 性能冒烟", "SKIP", note="graphmcp missing"))
    else:
        print("== CLI smoke ==")
        suites.append(run_bash_script("tests/smoke_test.sh", gm, "CLI 冒烟", timeout=900))
        if not args.skip_mcp_smoke:
            print("== MCP smoke ==")
            suites.append(
                run_bash_script("tests/mcp_smoke.sh", gm, "MCP 协议冒烟", timeout=900)
            )
        else:
            suites.append(SuiteResult("MCP 协议冒烟", "SKIP", note="--skip-mcp-smoke"))
        print("== table smoke ==")
        suites.append(
            run_bash_script("tests/table_smoke.sh", gm, "表协作冒烟", timeout=600)
        )
        print("== MCP perf smoke ==")
        suites.append(run_perf_smoke(gm))

    print("== bench ==")
    suites.append(run_bench(compare=True))

    if args.skip_export:
        suites.append(SuiteResult("样例全量导出回归", "SKIP", note="--skip-export"))
    elif gm:
        print("== export-example-testout ==")
        suites.append(run_export_testout(gm))
    else:
        suites.append(SuiteResult("样例全量导出回归", "SKIP", note="graphmcp missing"))

    overall = overall_status(suites)
    return write_report_outputs(meta, suites, overall, skip_diagram=args.skip_diagram, gm=gm)


if __name__ == "__main__":
    sys.exit(main())
