#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""generate_quality_gate_report.py - 组装 docs/QUALITY_GATE_REPORT.md

默认：运行 cppcheck（若可用），读取 docs/ci_results/sonar_status.env（若有）。
Sonar 未配置时状态为 SKIPPED（不假装 PASS）。

用法:
  python scripts/generate_quality_gate_report.py
  python scripts/generate_quality_gate_report.py --skip-cppcheck   # 仅组装已有结果
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CI_RESULTS = ROOT / "docs" / "ci_results"
OUT_MD = ROOT / "docs" / "QUALITY_GATE_REPORT.md"
OUT_JSON = ROOT / "docs" / "QUALITY_GATE_REPORT.json"
CPPCHECK_LOG = CI_RESULTS / "cppcheck.log"
CPPCHECK_EXIT = CI_RESULTS / "cppcheck.exit"
SONAR_ENV = CI_RESULTS / "sonar_status.env"


def read_version() -> str:
    p = ROOT / "VERSION"
    return p.read_text(encoding="utf-8").strip() if p.is_file() else ""


def git_cmd(*args: str) -> str:
    try:
        return subprocess.check_output(
            ["git", *args], cwd=ROOT, text=True, stderr=subprocess.DEVNULL
        ).strip()
    except Exception:
        return "unknown"


def load_sonar_status() -> dict:
    """从环境变量或 sonar_status.env 读取。"""
    data = {
        "status": os.environ.get("SONAR_STATUS", ""),
        "host": os.environ.get("SONAR_HOST_URL", ""),
        "qg": os.environ.get("SONAR_QG", ""),
        "dashboard": os.environ.get("SONAR_DASHBOARD_URL", ""),
        "skip_reason": os.environ.get("SONAR_SKIP_REASON", ""),
    }
    if SONAR_ENV.is_file():
        for line in SONAR_ENV.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            key = k.strip().lower()
            val = v.strip().strip('"')
            if key == "sonar_status":
                data["status"] = val
            elif key == "sonar_host_url":
                data["host"] = val
            elif key == "sonar_qg":
                data["qg"] = val
            elif key == "sonar_dashboard_url":
                data["dashboard"] = val
            elif key == "sonar_skip_reason":
                data["skip_reason"] = val
    if not data["status"]:
        data["status"] = "SKIPPED"
        if not data["skip_reason"]:
            data["skip_reason"] = (
                "未写入 sonar_status.env；CI 未启用 SONAR_ENABLED 或缺少 Token/URL"
            )
    return data


def run_cppcheck() -> tuple[int, str]:
    CI_RESULTS.mkdir(parents=True, exist_ok=True)
    exe = shutil.which("cppcheck")
    if not exe:
        msg = "cppcheck 未安装，跳过执行（请在 CI 安装 cppcheck）"
        CPPCHECK_LOG.write_text(msg + "\n", encoding="utf-8")
        CPPCHECK_EXIT.write_text("127\n", encoding="utf-8")
        return 127, msg

    cmd = [
        exe,
        "--enable=warning,style,performance,portability",
        "--inline-suppr",
        "-q",
        # 不使用 --error-exitcode：style/warning 会非零退出，与「仅 error 阻断」冲突
        "--template={file}:{line}: {severity}: {message}",
        str(ROOT / "src"),
    ]
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    out = (proc.stdout or "") + (proc.stderr or "")
    CPPCHECK_LOG.write_text(out if out.strip() else "(no findings)\n", encoding="utf-8")
    # 以解析到的 severity 为准写 exit 文件：有 error → 1，否则 0（工具自身崩溃仍保留原码）
    parsed_exit = 1 if re.search(r":\s*error:", out, re.I) else 0
    if proc.returncode not in (0, 1) and not out.strip():
        parsed_exit = proc.returncode
    CPPCHECK_EXIT.write_text(f"{parsed_exit}\n", encoding="utf-8")
    return parsed_exit, out


def parse_cppcheck(log: str, exit_code: int) -> dict:
    errors = len(re.findall(r":\s*error:", log, re.I))
    warnings = len(re.findall(r":\s*warning:", log, re.I))
    styles = len(re.findall(r":\s*style:", log, re.I))
    if exit_code == 127:
        status = "SKIPPED"
    elif errors > 0:
        status = "FAIL"
    elif warnings > 0:
        # warning 纳入考察：报告 WARN，不阻断 CI
        status = "WARN"
    else:
        # style / performance / portability 只记报告
        status = "PASS"
    summary = log.strip() if log.strip() else "(no findings)"
    if len(summary) > 4000:
        summary = summary[:4000] + "\n... (truncated)"
    return {
        "status": status,
        "exit": exit_code,
        "errors": errors,
        "warnings": warnings,
        "styles": styles,
        "summary": summary,
    }


def overall(cpp: dict, sonar: dict) -> str:
    if cpp["status"] == "FAIL" or sonar["status"] == "FAIL":
        return "FAIL"
    if cpp["status"] == "SKIPPED":
        return "FAIL"  # 必过项缺失工具视为未满足（CI 应安装）
    # warning 优先于「纯 PASS + Sonar 跳过」的 PARTIAL，便于首屏看见
    if cpp["status"] == "WARN" or sonar["status"] == "WARN":
        return "WARN"
    if cpp["status"] == "PASS" and sonar["status"] == "PASS":
        return "PASS"
    if cpp["status"] == "PASS" and sonar["status"] == "SKIPPED":
        return "PARTIAL"  # cppcheck 过，Sonar 跳过
    return "PARTIAL"


def write_report(cpp: dict, sonar: dict, meta: dict) -> None:
    ov = overall(cpp, sonar)
    run_url = meta.get("run_url") or os.environ.get("GITHUB_SERVER_URL", "")
    if run_url and os.environ.get("GITHUB_REPOSITORY") and os.environ.get("GITHUB_RUN_ID"):
        run_url = (
            f"{os.environ['GITHUB_SERVER_URL']}/{os.environ['GITHUB_REPOSITORY']}"
            f"/actions/runs/{os.environ['GITHUB_RUN_ID']}"
        )
    elif os.environ.get("BUILD_URL"):
        run_url = os.environ["BUILD_URL"]
    else:
        run_url = run_url or "n/a"

    md = f"""# graphmcp 质量门报告

> generated: {meta['time']}  |  branch: `{meta['branch']}`  |  commit: `{meta['commit']}`

## 元信息

| 项 | 值 |
|----|----|
| VERSION | `{meta['version']}` |
| branch | `{meta['branch']}` |
| commit | `{meta['commit']}` |
| 生成时间 | {meta['time']} |
| CI run | {run_url} |

## 总结论

| 门禁 | 状态 | 说明 |
|------|:----:|------|
| **总体** | **{ov}** | PASS / WARN / FAIL / PARTIAL |
| cppcheck（必过） | **{cpp['status']}** | error→FAIL；warning→WARN（不阻断）；style 只记报告 |
| SonarQube / SonarCloud（可选） | **{sonar['status']}** | PASS / FAIL / SKIPPED |

> 未配置 Sonar 时必须为 **SKIPPED**，不得记为 PASS。完整签核见 [ACCEPTANCE_DOD.md](ACCEPTANCE_DOD.md)。

## cppcheck

| 项 | 值 |
|----|----|
| 命令 | `cppcheck --enable=warning,style,performance,portability --inline-suppr -q src` |
| exit（门禁用，仅 error 记 1） | {cpp['exit']} |
| error 数 | {cpp['errors']} |
| warning 数 | {cpp['warnings']} |
| style 数 | {cpp.get('styles', 0)} |

### 关键问题摘要

```
{cpp['summary']}
```

## Sonar

| 项 | 值 |
|----|----|
| Host | {sonar.get('host') or 'n/a'} |
| Project Key | graphmcp |
| Quality Gate | {sonar.get('qg') or 'n/a'} |
| 仪表盘 | {sonar.get('dashboard') or 'n/a'} |

跳过原因（若 SKIPPED）：{sonar.get('skip_reason') or 'n/a'}

## 阻断规则

1. cppcheck **error**（FAIL）→ 阻断。
2. cppcheck **warning**（WARN）→ **不阻断**，须在报告中审阅。
3. Sonar 已启用且 **FAIL** → 阻断。
4. Sonar **SKIPPED** → 不阻断，DoD 须明示。
"""
    OUT_MD.write_text(md, encoding="utf-8", newline="\n")
    import json

    OUT_JSON.write_text(
        json.dumps(
            {"meta": meta, "overall": ov, "cppcheck": cpp, "sonar": sonar},
            ensure_ascii=False,
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-cppcheck", action="store_true", help="不重跑，只读 ci_results")
    args = ap.parse_args()

    meta = {
        "time": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "branch": git_cmd("branch", "--show-current") or "unknown",
        "commit": git_cmd("rev-parse", "--short", "HEAD") or "unknown",
        "version": read_version(),
    }

    if args.skip_cppcheck and CPPCHECK_EXIT.is_file():
        try:
            code = int(CPPCHECK_EXIT.read_text(encoding="utf-8").strip().splitlines()[0])
        except Exception:
            code = 1
        log = CPPCHECK_LOG.read_text(encoding="utf-8") if CPPCHECK_LOG.is_file() else ""
    else:
        code, log = run_cppcheck()

    cpp = parse_cppcheck(log, code)
    sonar = load_sonar_status()
    write_report(cpp, sonar, meta)
    print(f"Wrote {OUT_MD.relative_to(ROOT)}")
    print(f"overall={overall(cpp, sonar)} cppcheck={cpp['status']} sonar={sonar['status']}")
    if cpp["status"] == "WARN" and os.environ.get("GITHUB_ACTIONS"):
        print(
            f"::warning::cppcheck 发现 {cpp['warnings']} 条 warning（质量门 WARN，不阻断 CI）"
        )
    # 仅 FAIL / 工具缺失阻断；WARN 返回 0
    if cpp["status"] == "FAIL":
        return 1
    if sonar["status"] == "FAIL":
        return 1
    if cpp["status"] == "SKIPPED":
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
