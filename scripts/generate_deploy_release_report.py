#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""generate_deploy_release_report.py - 组装 docs/DEPLOY_RELEASE_REPORT.md

扫描当前目录或 --assets-dir 下的 graphmcp-*.tar.gz / *.zip，计算 SHA256，
可选运行 --bin 健康检查。

用法:
  python scripts/generate_deploy_release_report.py --assets-dir release-assets
  python scripts/generate_deploy_release_report.py --assets-dir . --bin bin/graphmcp --tag v0.2.9-beta
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple

ROOT = Path(__file__).resolve().parents[1]
OUT_MD = ROOT / "docs" / "DEPLOY_RELEASE_REPORT.md"
OUT_JSON = ROOT / "docs" / "DEPLOY_RELEASE_REPORT.json"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read_version() -> str:
    p = ROOT / "VERSION"
    return p.read_text(encoding="utf-8").strip() if p.is_file() else ""


def git_short() -> str:
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, text=True
        ).strip()
    except Exception:
        return os.environ.get("GITHUB_SHA", "unknown")[:7]


def find_assets(assets_dir: Path) -> Dict[str, Path]:
    found: Dict[str, Path] = {}
    if not assets_dir.is_dir():
        return found
    for p in sorted(assets_dir.rglob("*")):
        if not p.is_file():
            continue
        name = p.name.lower()
        if "linux" in name and (name.endswith(".tar.gz") or name.endswith(".tgz")):
            found["linux"] = p
        elif "windows" in name and name.endswith(".zip"):
            found["windows"] = p
        elif "macos" in name or "darwin" in name:
            if name.endswith(".tar.gz") or name.endswith(".tgz") or name.endswith(".zip"):
                found["macos"] = p
        elif name.startswith("graphmcp-") and name.endswith(".tar.gz") and "linux" not in name:
            # Jenkins 单平台包
            found.setdefault("linux", p)
    return found


def health_check(bin_path: Optional[Path]) -> Tuple[str, str]:
    if not bin_path or not bin_path.is_file():
        return "SKIPPED", "未提供 --bin"
    try:
        r = subprocess.run(
            [str(bin_path), "--help"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=30,
        )
        ok = r.returncode == 0 and ("Usage" in (r.stdout + r.stderr) or "usage" in (r.stdout + r.stderr).lower() or len(r.stdout + r.stderr) > 20)
        return ("PASS" if ok else "FAIL"), f"exit={r.returncode}"
    except Exception as e:
        return "FAIL", str(e)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--assets-dir", default=".", help="制品目录")
    ap.add_argument("--bin", default="", help="用于健康检查的 graphmcp 路径")
    ap.add_argument("--tag", default="", help="版本 tag")
    ap.add_argument("--dry-run", default="", help="true/false")
    ap.add_argument("--trigger", default="", help="触发来源")
    ap.add_argument("--breaking", default="no", help="yes/no")
    ap.add_argument("--breaking-notes", default="无", help="破坏性变更说明")
    ap.add_argument("--gh-release", default="n/a")
    ap.add_argument("--release-url", default="n/a")
    ap.add_argument("--nginx-deploy", default="n/a")
    ap.add_argument("--health-test", default="n/a", help="构建机 make test 结果")
    args = ap.parse_args()

    tag = args.tag or os.environ.get("GITHUB_REF_NAME") or os.environ.get("GRAPHMCP_TAG_NAME") or read_version()
    dry = args.dry_run or os.environ.get("CD_DRY_RUN") or os.environ.get("INPUT_DRY_RUN") or "n/a"
    trigger = args.trigger or os.environ.get("GITHUB_EVENT_NAME") or "manual"
    assets = find_assets(Path(args.assets_dir))
    rows: List[Tuple[str, str, str]] = []
    for plat, key in (("Linux", "linux"), ("Windows", "windows"), ("macOS", "macos")):
        p = assets.get(key)
        if p:
            rows.append((plat, p.name, sha256_file(p)))
        else:
            rows.append((plat, "（缺失）", "-"))

    bin_path = Path(args.bin) if args.bin else None
    if not bin_path:
        for cand in (ROOT / "bin" / "graphmcp", ROOT / "bin" / "graphmcp.exe"):
            if cand.is_file():
                bin_path = cand
                break
    help_status, help_note = health_check(bin_path)

    table = "\n".join(f"| {a} | `{b}` | `{c}` |" for a, b, c in rows)
    md = f"""# graphmcp 发布 / 部署报告

> generated: {datetime.now().strftime("%Y-%m-%d %H:%M:%S")}

## 元信息

| 项 | 值 |
|----|----|
| 版本 / Tag | `{tag}` |
| VERSION 文件 | `{read_version()}` |
| commit | `{git_short()}` |
| 触发 | `{trigger}` |
| dry_run | `{dry}` |
| 生成时间 | {datetime.now().strftime("%Y-%m-%d %H:%M:%S")} |

## Breaking change（MCP / CLI）

| 是否有破坏性变更 | 说明 |
|:----------------:|------|
| `{args.breaking}` | {args.breaking_notes} |

契约真源：`toolList()`；OpenAPI 为生成物。详见 [CHANGELOG.md](../CHANGELOG.md)。

## 制品与哈希

| 平台 | 文件名 | SHA256 |
|------|--------|--------|
{table}

## 健康检查

| 检查 | 结果 | 说明 |
|------|:----:|------|
| `graphmcp --help` | **{help_status}** | {help_note} |
| 构建机测试 | `{args.health_test}` | |

## 部署目标

| 目标 | 状态 | 说明 |
|------|:----:|------|
| GitHub Release | `{args.gh_release}` | {args.release_url} |
| 本地 nginx 下载站 | `{args.nginx_deploy}` | 见 [RUNBOOK.md](RUNBOOK.md) |

## 回滚方式

1. 保留上一可用 tag 的 Release 资产。
2. nginx：清理 `/artifacts` 坏包后重跑 `deploy_release.yml`。
3. 勿对 `main` 强推；用补丁 tag 或 revert。

## 签核

对照 [ACCEPTANCE_DOD.md](ACCEPTANCE_DOD.md)。测试 GO/NO-GO 见 CI `docs/TEST_REPORT.md`。
"""
    OUT_MD.parent.mkdir(parents=True, exist_ok=True)
    OUT_MD.write_text(md, encoding="utf-8", newline="\n")
    payload = {
        "tag": tag,
        "version": read_version(),
        "commit": git_short(),
        "dry_run": dry,
        "assets": {k: {"name": v.name, "sha256": sha256_file(v)} for k, v in assets.items()},
        "health_help": help_status,
        "breaking": args.breaking,
    }
    OUT_JSON.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {OUT_MD.relative_to(ROOT)}")
    return 0 if help_status != "FAIL" else 1


if __name__ == "__main__":
    sys.exit(main())
