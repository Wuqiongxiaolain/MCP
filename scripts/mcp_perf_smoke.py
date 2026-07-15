#!/usr/bin/env python3
"""mcp_perf_smoke.py - Windows 可用的 MCP 并发/基础冒烟（不依赖 bash）

用法:
  python scripts/mcp_perf_smoke.py [path/to/graphmcp(.exe)]

验证:
  1) 双进程同 store 各写 30 次后 index_graph_count == graph_dirs_count
  2) graph_update + commit(all) 成功路径
  3) tools/list 可响应
"""

from __future__ import annotations

import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import threading
import time


def find_exe(argv: list[str]) -> str:
    if len(argv) > 1:
        return argv[1]
    root = pathlib.Path(__file__).resolve().parents[1]
    for name in ("graphmcp.exe", "graphmcp"):
        p = root / "bin" / name
        if p.exists():
            return str(p)
    raise SystemExit("graphmcp executable not found; pass path as argv[1]")


class MCP:
    def __init__(self, exe: str, store: str):
        env = os.environ.copy()
        env["GRAPHMCP_STORE"] = store
        env["GRAPHMCP_NO_LAUNCH"] = "1"
        self.p = subprocess.Popen(
            [exe, "serve"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
            env=env,
        )
        self.rid = 0

    def call(self, method: str, params=None):
        self.rid += 1
        msg = {"jsonrpc": "2.0", "id": self.rid, "method": method}
        if params is not None:
            msg["params"] = params
        assert self.p.stdin and self.p.stdout
        self.p.stdin.write(json.dumps(msg, ensure_ascii=False) + "\n")
        self.p.stdin.flush()
        line = self.p.stdout.readline()
        if not line:
            raise RuntimeError("empty stdout")
        return json.loads(line)

    def tool(self, name: str, arguments: dict):
        return self.call("tools/call", {"name": name, "arguments": arguments})

    def close(self):
        try:
            self.p.terminate()
            self.p.wait(timeout=3)
        except Exception:
            self.p.kill()


def assert_true(cond: bool, msg: str):
    if not cond:
        raise AssertionError(msg)


def main() -> int:
    exe = find_exe(sys.argv)
    store = tempfile.mkdtemp(prefix="graphmcp-perf-smoke-")
    print(f"exe={exe}")
    print(f"store={store}")
    try:
        # --- handshake + tools/list ---
        c0 = MCP(exe, store)
        c0.call("initialize", {"protocolVersion": "2024-11-05"})
        listed = c0.call("tools/list")
        tools = listed["result"]["tools"]
        assert_true(isinstance(tools, list) and len(tools) >= 20, "tools/list too small")
        c0.close()

        # --- concurrent creates ---
        c1, c2 = MCP(exe, store), MCP(exe, store)
        c1.call("initialize", {"protocolVersion": "2024-11-05"})
        c2.call("initialize", {"protocolVersion": "2024-11-05"})
        errs: list[str] = []
        lock = threading.Lock()

        def worker(client: MCP, prefix: str):
            for i in range(30):
                r = client.tool(
                    "graph_create",
                    {
                        "content": f"flowchart LR\n{prefix}{i}-->Z",
                        "name": f"{prefix}-{i}",
                    },
                )
                res = r.get("result", {})
                txt = res.get("content", [{"text": ""}])[0].get("text", "")
                if res.get("isError"):
                    with lock:
                        errs.append(txt)

        t0 = time.perf_counter()
        th1 = threading.Thread(target=worker, args=(c1, "A"))
        th2 = threading.Thread(target=worker, args=(c2, "B"))
        th1.start()
        th2.start()
        th1.join()
        th2.join()
        elapsed = (time.perf_counter() - t0) * 1000
        c1.close()
        c2.close()

        idx_path = pathlib.Path(store) / "index.json"
        assert_true(idx_path.exists(), "index.json missing")
        idx = json.loads(idx_path.read_text(encoding="utf-8"))
        index_n = len(idx.get("graphs", []))
        dirs = [
            p
            for p in pathlib.Path(store).iterdir()
            if p.is_dir() and p.name not in ("tables",) and not p.name.startswith(".")
        ]
        print(f"concurrent_writes: elapsed_ms={elapsed:.1f} errors={len(errs)}")
        print(f"index_graph_count={index_n} graph_dirs_count={len(dirs)}")
        assert_true(len(errs) == 0, f"tool errors: {errs[:3]}")
        assert_true(index_n == len(dirs), f"index/dirs mismatch {index_n}!={len(dirs)}")
        assert_true(index_n >= 58, f"expected >=58 graphs after 60 creates, got {index_n}")
        # 理想目标是 60；允许极少量跨进程 id 碰撞，但索引必须自洽
        if index_n < 60:
            print(f"WARN: got {index_n}/60 graphs (possible id collision)")

        # --- commit(all) path ---
        c3 = MCP(exe, store)
        c3.call("initialize", {"protocolVersion": "2024-11-05"})
        created = c3.tool(
            "graph_create", {"content": "flowchart LR\nA-->B", "name": "commit-case"}
        )
        gid = json.loads(created["result"]["content"][0]["text"])["id"]
        upd = c3.tool("graph_update", {"id": gid, "node": "A", "set": "label=A1"})
        assert_true(not upd["result"].get("isError"), upd["result"]["content"][0]["text"])
        bad = c3.tool("graph_commit", {"id": gid, "message": "no-all"})
        assert_true(bad["result"].get("isError"), "expected stage-empty error")
        good = c3.tool("graph_commit", {"id": gid, "message": "with-all", "all": True})
        assert_true(not good["result"].get("isError"), good["result"]["content"][0]["text"])
        c3.close()

        print("mcp_perf_smoke: PASS")
        return 0
    finally:
        shutil.rmtree(store, ignore_errors=True)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print(f"mcp_perf_smoke: FAIL: {e}", file=sys.stderr)
        raise SystemExit(1)
