# -*- coding: utf-8 -*-
"""从 README.md BFS，检查 docs 下 md 是否可达，并报告断链。

用法: python scripts/check_docs_links.py
退出码: 0=无孤立 md 且无断链；1=有问题
"""
import re
import sys
from collections import deque
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parent.parent
link_re = re.compile(r"\[([^\]]*)\]\(([^)]+)\)|!\[([^\]]*)\]\(([^)]+)\)")


def collect_md_targets():
    md_files = {ROOT / "README.md", ROOT / "CHANGELOG.md"}
    md_files |= set((ROOT / "docs").rglob("*.md"))
    return {p.resolve() for p in md_files if p.is_file()}


def resolve_link(src: Path, href: str):
    href = href.strip()
    if href.startswith(("http://", "https://", "mailto:", "#")):
        return None
    href = href.split("#")[0].split("?")[0]
    if not href:
        return None
    href = unquote(href)
    return (src.parent / href).resolve()


def iter_hrefs(text: str):
    for m in link_re.finditer(text):
        href = m.group(2) or m.group(4)
        if href:
            yield href.strip()


def main() -> int:
    all_md = collect_md_targets()
    start = (ROOT / "README.md").resolve()
    seen = set()
    q = deque([start])
    while q:
        cur = q.popleft()
        if cur in seen or not cur.is_file():
            continue
        seen.add(cur)
        text = cur.read_text(encoding="utf-8")
        for href in iter_hrefs(text):
            tgt = resolve_link(cur, href)
            if tgt is None:
                continue
            if tgt.suffix.lower() in {".md", ".yaml", ".yml"} and tgt.exists():
                if tgt not in seen:
                    q.append(tgt)
            if tgt.is_dir():
                for name in ("README.md", "index.md"):
                    cand = tgt / name
                    if cand.is_file() and cand not in seen:
                        q.append(cand)

    reachable_md = {p for p in seen if p.suffix.lower() == ".md"}
    orphans = sorted(
        p.relative_to(ROOT).as_posix() for p in all_md if p not in reachable_md
    )

    print("=== REACHABLE MD from README ===")
    for p in sorted(x.relative_to(ROOT).as_posix() for x in reachable_md):
        print(" ", p)
    print("\n=== ORPHAN MD ===")
    if not orphans:
        print("  (none)")
    for p in orphans:
        print(" ", p)

    print("\n=== BROKEN LINKS (reachable md -> missing) ===")
    broken = []
    for cur in sorted(seen):
        if cur.suffix.lower() != ".md":
            continue
        # Cursor Plan 副本内的仓库相对链接（如 src/...）相对副本目录解析会误报，跳过断链检查
        try:
            rel_cur = cur.relative_to(ROOT).as_posix()
        except ValueError:
            continue
        if "/cursor-plans/" in rel_cur or rel_cur.endswith(".plan.md"):
            continue
        text = cur.read_text(encoding="utf-8")
        for href in iter_hrefs(text):
            if href.startswith(("http://", "https://", "mailto:", "#")):
                continue
            path_part = href.split("#")[0].split("?")[0]
            if not path_part:
                continue
            tgt = resolve_link(cur, href)
            if tgt is None:
                continue
            try:
                rel = tgt.relative_to(ROOT)
            except ValueError:
                continue
            if not tgt.exists():
                broken.append(
                    (
                        cur.relative_to(ROOT).as_posix(),
                        href,
                        rel.as_posix(),
                    )
                )
    if not broken:
        print("  (none)")
    for b in broken:
        print(f"  {b[0]} -> {b[1]}  (missing: {b[2]})")
    print(f"\norphan_count={len(orphans)} broken_count={len(broken)}")
    return 1 if orphans or broken else 0


if __name__ == "__main__":
    sys.exit(main())
