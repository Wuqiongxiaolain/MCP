#!/usr/bin/env python3
"""mcp_perf_smoke.py - 跨平台 MCP 性能与可靠性冒烟（不依赖 bash）

用法:
  python scripts/mcp_perf_smoke.py [path/to/graphmcp(.exe)]

验证:
  1) 双进程并发写时 index/目录一致，且读者看不到截断 JSON
  2) 并发 delete/create 后 index/目录集合一致
  3) graph_update + commit(all)、history meta 与旧数据回退
  4) 外部导出硬超时后服务可恢复
  5) 内联导出护栏、紧凑响应与大载荷延迟/RSS
  6) bench_compare 的 ms/us 单位归一化
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
import queue
import ctypes
import traceback
from ctypes import wintypes


def findExe(argv: list[str]) -> str:
    if len(argv) > 1:
        return argv[1]
    root = pathlib.Path(__file__).resolve().parents[1]
    for name in ("graphmcp.exe", "graphmcp"):
        p = root / "bin" / name
        if p.exists():
            return str(p)
    raise SystemExit("graphmcp executable not found; pass path as argv[1]")


class MCP:
    def __init__(self, exe: str, store: str, env_overrides: dict | None = None):
        env = os.environ.copy()
        env["GRAPHMCP_STORE"] = store
        env["GRAPHMCP_NO_LAUNCH"] = "1"
        if env_overrides:
            env.update({str(k): str(v) for k, v in env_overrides.items()})
        self.p = subprocess.Popen(
            [exe, "serve"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
            env=env,
        )
        self.rid = 0
        self.last_raw = ""

    def call(self, method: str, params=None, timeout_s: float = 15.0):
        self.rid += 1
        msg = {"jsonrpc": "2.0", "id": self.rid, "method": method}
        if params is not None:
            msg["params"] = params
        assert self.p.stdin and self.p.stdout
        self.p.stdin.write(json.dumps(msg, ensure_ascii=False) + "\n")
        self.p.stdin.flush()
        result_queue: queue.Queue[str] = queue.Queue(maxsize=1)

        def readLine():
            result_queue.put(self.p.stdout.readline())

        reader = threading.Thread(target=readLine, daemon=True)
        reader.start()
        try:
            line = result_queue.get(timeout=timeout_s)
        except queue.Empty as exc:
            raise TimeoutError(f"MCP call timed out: {method}") from exc
        if not line:
            raise RuntimeError("empty stdout")
        self.last_raw = line
        response = json.loads(line)
        if "error" in response:
            raise RuntimeError(f"JSON-RPC error: {response['error']}")
        return response

    def tool(self, name: str, arguments: dict, timeout_s: float = 15.0):
        return self.call(
            "tools/call",
            {"name": name, "arguments": arguments},
            timeout_s=timeout_s,
        )

    def close(self):
        try:
            self.p.terminate()
            self.p.wait(timeout=3)
        except Exception:
            self.p.kill()


def assertTrue(cond: bool, msg: str):
    if not cond:
        raise AssertionError(msg)


def budget(name: str, default: float) -> float:
    """读取可覆盖的性能预算，便于不同 CI 机器校准。"""
    return float(os.environ.get(name, default))


def toolText(response: dict) -> str:
    return response.get("result", {}).get("content", [{"text": ""}])[0].get(
        "text", ""
    )


def getRssBytes(pid: int) -> int:
    """读取进程 RSS；不支持的平台返回 0。"""
    if sys.platform == "win32":
        class ProcessMemoryCounters(ctypes.Structure):
            _fields_ = [
                ("cb", wintypes.DWORD),
                ("PageFaultCount", wintypes.DWORD),
                ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t),
            ]

        query_info = 0x0400
        handle = ctypes.windll.kernel32.OpenProcess(query_info, False, pid)
        if not handle:
            return 0
        counters = ProcessMemoryCounters()
        counters.cb = ctypes.sizeof(counters)
        ok = ctypes.windll.psapi.GetProcessMemoryInfo(
            handle, ctypes.byref(counters), counters.cb
        )
        ctypes.windll.kernel32.CloseHandle(handle)
        return int(counters.WorkingSetSize) if ok else 0
    statm = pathlib.Path(f"/proc/{pid}/statm")
    if statm.exists():
        fields = statm.read_text(encoding="ascii").split()
        return int(fields[1]) * os.sysconf("SC_PAGE_SIZE")
    return 0


def graphId(response: dict) -> str:
    result = response.get("result", {})
    assertTrue(not result.get("isError"), toolText(response))
    return json.loads(toolText(response))["id"]


def assertStoreConsistent(store: str):
    index_path = pathlib.Path(store) / "index.json"
    assertTrue(index_path.exists(), "index.json missing")
    index = json.loads(index_path.read_text(encoding="utf-8"))
    index_ids = {item["id"] for item in index.get("graphs", [])}
    directory_ids = {
        path.name
        for path in pathlib.Path(store).iterdir()
        if path.is_dir() and (path / "latest.json").exists()
    }
    assertTrue(
        index_ids == directory_ids,
        f"index/dirs mismatch missing={directory_ids-index_ids} "
        f"ghost={index_ids-directory_ids}",
    )
    return index_ids


def testBenchUnits(root: pathlib.Path):
    """验证 1ms 与 1000us 等价，2ms 相对 1000us 会触发失败。"""
    with tempfile.TemporaryDirectory(prefix="graphmcp-bench-unit-") as temp_dir:
        baseline = pathlib.Path(temp_dir) / "baseline.json"
        current = pathlib.Path(temp_dir) / "current.json"
        baseline.write_text(
            json.dumps(
                {"benchmarks": [{"name": "unit_case", "value": 1.0, "unit": "ms"}]}
            ),
            encoding="utf-8",
        )
        current.write_text(
            json.dumps(
                {
                    "benchmarks": [
                        {"name": "unit_case", "value": 1000.0, "unit": "us"}
                    ]
                }
            ),
            encoding="utf-8",
        )
        script = root / "scripts" / "bench_compare.py"
        equal_run = subprocess.run(
            [sys.executable, str(script), str(baseline), str(current)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            env={**os.environ, "PYTHONUTF8": "1"},
        )
        assertTrue(equal_run.returncode == 0, equal_run.stdout + equal_run.stderr)
        assertTrue("FAIL:" not in equal_run.stdout, "unit normalization false positive")
        current.write_text(
            json.dumps(
                {
                    "benchmarks": [
                        {"name": "unit_case", "value": 2000.0, "unit": "us"}
                    ]
                }
            ),
            encoding="utf-8",
        )
        regression_run = subprocess.run(
            [sys.executable, str(script), str(baseline), str(current)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            env={**os.environ, "PYTHONUTF8": "1"},
        )
        assertTrue(regression_run.returncode == 2, "2x regression was not detected")

        # 内存稳定性：绝对上限（不依赖相对基线百分比）
        mem_bl = pathlib.Path(temp_dir) / "mem_baseline.json"
        mem_cur = pathlib.Path(temp_dir) / "mem_current.json"
        mem_bl.write_text(
            json.dumps(
                {
                    "benchmarks": [
                        {
                            "name": "memory_RSS_repeat_save_same_id",
                            "value": 0.1,
                            "unit": "MB",
                        }
                    ]
                }
            ),
            encoding="utf-8",
        )
        mem_cur.write_text(
            json.dumps(
                {
                    "benchmarks": [
                        {
                            "name": "memory_RSS_repeat_save_same_id",
                            "value": 5.0,
                            "unit": "MB",
                        }
                    ]
                }
            ),
            encoding="utf-8",
        )
        mem_fail = subprocess.run(
            [sys.executable, str(script), str(mem_bl), str(mem_cur)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            env={
                **os.environ,
                "PYTHONUTF8": "1",
                "GRAPHMCP_MEMORY_FAIL_MB": "4",
                "GRAPHMCP_MEMORY_WARN_MB": "3",
            },
        )
        assertTrue(mem_fail.returncode == 2, mem_fail.stdout + mem_fail.stderr)
        assertTrue(
            "absolute FAIL cap" in mem_fail.stdout
            or "absolute FAIL cap" in mem_fail.stderr,
            "memory absolute cap was not applied",
        )

        # 后半段相对前半段持续增长
        half_cur = pathlib.Path(temp_dir) / "half_current.json"
        half_cur.write_text(
            json.dumps(
                {
                    "benchmarks": [
                        {
                            "name": "memory_RSS_repeat_save_1st_half",
                            "value": 0.2,
                            "unit": "MB",
                        },
                        {
                            "name": "memory_RSS_repeat_save_2nd_half",
                            "value": 1.0,
                            "unit": "MB",
                        },
                        {
                            "name": "memory_RSS_repeat_save_same_id",
                            "value": 1.2,
                            "unit": "MB",
                        },
                    ]
                }
            ),
            encoding="utf-8",
        )
        half_fail = subprocess.run(
            [sys.executable, str(script), str(mem_bl), str(half_cur)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            env={
                **os.environ,
                "PYTHONUTF8": "1",
                "GRAPHMCP_MEMORY_FAIL_MB": "4",
                "GRAPHMCP_MEMORY_HALF_RATIO": "2.5",
            },
        )
        assertTrue(half_fail.returncode == 2, half_fail.stdout + half_fail.stderr)
        assertTrue(
            "halves" in half_fail.stdout or "halves" in half_fail.stderr,
            "half-ratio leak check missing",
        )

def main() -> int:
    exe = findExe(sys.argv)
    root = pathlib.Path(__file__).resolve().parents[1]
    store = tempfile.mkdtemp(prefix="graphmcp-perf-smoke-")
    print(f"exe={exe}")
    print(f"store={store}")
    try:
        # --- handshake + tools/list ---
        c0 = MCP(exe, store)
        c0.call("initialize", {"protocolVersion": "2024-11-05"})
        listed = c0.call("tools/list")
        tools = listed["result"]["tools"]
        names = {tool["name"] for tool in tools}
        required = {
            "graph_create",
            "graph_export",
            "graph_history",
            "graph_delete",
            "graph_commit",
            "graph_apply",
            "table_create",
            "table_export",
        }
        assertTrue(len(tools) >= 47 and required <= names, "tools/list incomplete")
        apply_tool = next(t for t in tools if t["name"] == "graph_apply")
        assertTrue(
            "all=true" in apply_tool.get("description", "")
            or "commit" in apply_tool.get("description", ""),
            "graph_apply description missing commit guidance",
        )
        commit_tool = next(t for t in tools if t["name"] == "graph_commit")
        assertTrue(
            "all=true" in commit_tool.get("description", ""),
            "graph_commit must steer agents to all=true",
        )
        # 外层 JSON-RPC 与内层工具清单应保持单行紧凑。
        assertTrue("\n" not in c0.last_raw.rstrip("\n"), "tools/list response not compact")
        cold_start = time.perf_counter()
        c0.call("tools/list")
        warm_ms = (time.perf_counter() - cold_start) * 1000
        print(f"tools_list_warm_ms={warm_ms:.3f}")
        assertTrue(
            warm_ms < budget("GRAPHMCP_SMOKE_MAX_TOOLS_LIST_MS", 50),
            f"warm tools/list exceeded budget: {warm_ms:.3f}ms",
        )
        c0.close()

        # --- concurrent creates ---
        c1, c2 = MCP(exe, store), MCP(exe, store)
        c1.call("initialize", {"protocolVersion": "2024-11-05"})
        c2.call("initialize", {"protocolVersion": "2024-11-05"})
        errs: list[str] = []
        parse_errors: list[str] = []
        created_ids: list[str] = []
        lock = threading.Lock()
        stop_reader = threading.Event()

        def indexReader():
            index_path = pathlib.Path(store) / "index.json"
            while not stop_reader.is_set():
                if index_path.exists():
                    try:
                        json.loads(index_path.read_text(encoding="utf-8"))
                    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
                        parse_errors.append(str(exc))
                    except PermissionError:
                        # Windows MoveFileEx(REPLACE_EXISTING) 的短暂共享窗口；
                        # 这不是截断 JSON，下一轮重试即可。
                        pass
                time.sleep(0.002)

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
                txt = toolText(r)
                if res.get("isError"):
                    with lock:
                        errs.append(txt)
                else:
                    with lock:
                        created_ids.append(json.loads(txt)["id"])

        t0 = time.perf_counter()
        reader_thread = threading.Thread(target=indexReader)
        reader_thread.start()
        th1 = threading.Thread(target=worker, args=(c1, "A"))
        th2 = threading.Thread(target=worker, args=(c2, "B"))
        th1.start()
        th2.start()
        th1.join()
        th2.join()
        stop_reader.set()
        reader_thread.join()
        elapsed = (time.perf_counter() - t0) * 1000
        c1.close()
        c2.close()

        index_ids = assertStoreConsistent(store)
        index_n = len(index_ids)
        print(f"concurrent_writes: elapsed_ms={elapsed:.1f} errors={len(errs)}")
        print(f"index_graph_count={index_n} graph_dirs_count={index_n}")
        assertTrue(len(errs) == 0, f"tool errors: {errs[:3]}")
        assertTrue(not parse_errors, f"atomic index read failures: {parse_errors[:3]}")
        assertTrue(index_n == 60, f"expected 60 unique graphs, got {index_n}")
        assertTrue(
            elapsed < budget("GRAPHMCP_SMOKE_MAX_CONCURRENT_MS", 3000),
            f"concurrent create exceeded budget: {elapsed:.1f}ms",
        )
        # latest.json 应为版本指针，避免与 snapshot 整图双写
        sample_id = next(iter(index_ids))
        latest_obj = json.loads(
            (pathlib.Path(store) / sample_id / "latest.json").read_text(
                encoding="utf-8"
            )
        )
        assertTrue(
            "version" in latest_obj and "nodes" not in latest_obj,
            f"latest.json should be version pointer, got keys={list(latest_obj)}",
        )
        snap = json.loads(
            (
                pathlib.Path(store)
                / sample_id
                / "versions"
                / f"v{int(latest_obj['version'])}.json"
            ).read_text(encoding="utf-8")
        )
        assertTrue("model" in snap and "nodes" in snap["model"], "snapshot missing model")
        latest_bytes = (pathlib.Path(store) / sample_id / "latest.json").stat().st_size
        snap_bytes = (
            pathlib.Path(store)
            / sample_id
            / "versions"
            / f"v{int(latest_obj['version'])}.json"
        ).stat().st_size
        assertTrue(
            latest_bytes < snap_bytes / 2,
            f"latest pointer not smaller than snapshot: {latest_bytes} vs {snap_bytes}",
        )

        # --- commit(all) path ---
        c3 = MCP(exe, store)
        c3.call("initialize", {"protocolVersion": "2024-11-05"})
        created = c3.tool(
            "graph_create", {"content": "flowchart LR\nA-->B", "name": "commit-case"}
        )
        gid = graphId(created)
        upd = c3.tool("graph_update", {"id": gid, "node": "A", "set": "label=A1"})
        assertTrue(not upd["result"].get("isError"), toolText(upd))
        bad = c3.tool("graph_commit", {"id": gid, "message": "no-all"})
        assertTrue(bad["result"].get("isError"), "expected stage-empty error")
        assertTrue(
            "all=true" in toolText(bad),
            "empty-stage error should steer agents to all=true",
        )
        good = c3.tool("graph_commit", {"id": gid, "message": "with-all", "all": True})
        assertTrue(not good["result"].get("isError"), toolText(good))

        # --- graph_apply：多改 + 提交 + 紧凑 model 导出 ---
        apply_created = c3.tool(
            "graph_create",
            {"content": "flowchart LR\nP-->Q", "name": "apply-case"},
        )
        apply_id = graphId(apply_created)
        shown = c3.tool("graph_show", {"id": apply_id})
        show_data = json.loads(toolText(shown))
        node_a = show_data["nodeList"][0]["id"]
        apply_ops = json.dumps(
            [
                {"op": "update", "node": node_a, "set": "label=Renamed"},
                {
                    "op": "insert",
                    "element": "node",
                    "label": "Extra",
                    "type": "rect",
                },
            ]
        )
        applied = c3.tool(
            "graph_apply",
            {
                "id": apply_id,
                "ops": apply_ops,
                "message": "apply-batch",
                "export_to": "model",
            },
        )
        assertTrue(not applied["result"].get("isError"), toolText(applied))
        apply_data = json.loads(toolText(applied))
        assertTrue(apply_data.get("committed") is True, "graph_apply did not commit")
        assertTrue(apply_data.get("opsApplied") == 2, "graph_apply ops count wrong")
        export_body = apply_data.get("export_content", "")
        assertTrue(export_body, "graph_apply export_content missing")
        assertTrue(
            "\n" not in export_body.rstrip("\n"),
            "model export should be compact (no pretty newlines)",
        )
        assertTrue('"label":"Renamed"' in export_body.replace(" ", ""), toolText(applied))

        # graph_apply 中段失败应回报部分成功，而非吞掉已应用计数
        partial = c3.tool(
            "graph_apply",
            {
                "id": apply_id,
                "ops": json.dumps(
                    [
                        {"op": "update", "node": node_a, "set": "label=Ok"},
                        {"op": "update", "node": "__missing__", "set": "label=X"},
                    ]
                ),
                "commit": False,
            },
        )
        assertTrue(partial["result"].get("isError"), "partial apply should error")
        partial_data = json.loads(toolText(partial))
        assertTrue(partial_data.get("status") == "partial", toolText(partial))
        assertTrue(partial_data.get("opsApplied") == 1, toolText(partial))
        assertTrue(partial_data.get("failedOpIndex") == 1, toolText(partial))

        # --- 损坏 index 必须拒绝写，且 commit 失败后保留 draft/stage ---
        retry_update = c3.tool(
            "graph_update", {"id": gid, "node": "A", "set": "label=A2"}
        )
        assertTrue(not retry_update["result"].get("isError"), toolText(retry_update))
        index_path = pathlib.Path(store) / "index.json"
        valid_index = index_path.read_text(encoding="utf-8")
        index_path.write_text('{"graphs":[', encoding="utf-8")
        failed_commit = c3.tool(
            "graph_commit", {"id": gid, "message": "must-preserve", "all": True}
        )
        assertTrue(failed_commit["result"].get("isError"), "corrupt index write passed")
        assertTrue(
            "preserved for retry" in toolText(failed_commit),
            "commit failure did not report preserved draft/stage",
        )
        assertTrue(
            index_path.read_text(encoding="utf-8") == '{"graphs":[',
            "corrupt index was overwritten",
        )
        index_path.write_text(valid_index, encoding="utf-8")
        retried_commit = c3.tool(
            "graph_commit", {"id": gid, "message": "retry", "all": True}
        )
        assertTrue(
            not retried_commit["result"].get("isError"),
            "preserved draft/stage could not be retried",
        )

        # --- history meta 与旧数据回退 ---
        meta_path = pathlib.Path(store) / gid / "versions" / "v1.meta.json"
        assertTrue(not meta_path.exists(), "history meta should be generated lazily")
        history = c3.tool("graph_history", {"id": gid})
        history_data = json.loads(toolText(history))
        assertTrue(len(history_data) == 3, "history meta path returned wrong count")
        assertTrue(meta_path.exists(), "history query did not generate meta cache")
        meta_path.unlink()
        legacy_history = c3.tool("graph_history", {"id": gid})
        legacy_data = json.loads(toolText(legacy_history))
        assertTrue(len(legacy_data) == 3, "legacy snapshot fallback failed")

        # --- 大载荷与内联护栏 ---
        before_rss = getRssBytes(c3.p.pid)
        big_content = "flowchart LR\n" + "\n".join(
            f"N{i}-->N{i+1}" for i in range(8000)
        )
        large_start = time.perf_counter()
        large = c3.tool(
            "graph_create", {"content": big_content, "name": "large-payload"}
        )
        large_ms = (time.perf_counter() - large_start) * 1000
        assertTrue(not large["result"].get("isError"), toolText(large))
        after_rss = getRssBytes(c3.p.pid)
        rss_delta_mb = max(0, after_rss - before_rss) / (1024 * 1024)
        print(
            f"large_payload_bytes={len(big_content.encode('utf-8'))} "
            f"latency_ms={large_ms:.1f} rss_delta_mb={rss_delta_mb:.1f}"
        )
        assertTrue(
            # 默认 2500ms：8000 边大图在共享 CI runner 上常 >1s；冒烟只拦数量级退化
            large_ms < budget("GRAPHMCP_SMOKE_MAX_LARGE_MS", 2500),
            f"large payload latency exceeded budget: {large_ms:.1f}ms",
        )
        if before_rss and after_rss:
            assertTrue(
                rss_delta_mb < budget("GRAPHMCP_SMOKE_MAX_RSS_MB", 256),
                f"large payload RSS delta exceeded budget: {rss_delta_mb:.1f}MB",
            )

        # 重复转换用于发现请求级对象未释放造成的持续增长。
        leak_content = "flowchart LR\n" + "\n".join(
            f"L{i}-->L{i+1}" for i in range(1000)
        )
        leak_before = getRssBytes(c3.p.pid)
        for _ in range(20):
            converted = c3.tool(
                "graph_convert", {"content": leak_content, "to": "mermaid"}
            )
            assertTrue(
                not converted["result"].get("isError"), toolText(converted)
            )
        leak_after = getRssBytes(c3.p.pid)
        leak_delta_mb = max(0, leak_after - leak_before) / (1024 * 1024)
        print(f"repeat_convert_rss_delta_mb={leak_delta_mb:.1f}")
        if leak_before and leak_after:
            assertTrue(
                leak_delta_mb < budget("GRAPHMCP_SMOKE_MAX_LEAK_MB", 64),
                f"repeated conversion RSS growth exceeded budget: "
                f"{leak_delta_mb:.1f}MB",
            )
        c3.close()

        guard_client = MCP(
            exe, store, {"GRAPHMCP_INLINE_MAX_BYTES": "1024"}
        )
        guard_client.call("initialize", {"protocolVersion": "2024-11-05"})
        guard = guard_client.tool(
            "graph_convert",
            {
                "content": "flowchart LR\n"
                + "\n".join(f"G{i}-->G{i+1}" for i in range(400)),
                "to": "mermaid",
            },
        )
        assertTrue(guard["result"].get("isError"), "inline guard did not reject")
        assertTrue(
            "GRAPHMCP_INLINE_MAX_BYTES" in toolText(guard),
            "inline guard message is not actionable",
        )
        # table_export 同样受内联护栏约束
        table = guard_client.tool(
            "table_create",
            {
                "content": "c1,c2\n" + "\n".join(f"{i},x{i}" for i in range(200)),
                "format": "csv",
                "name": "guard-table",
            },
        )
        assertTrue(not table["result"].get("isError"), toolText(table))
        table_id = json.loads(toolText(table))["id"]
        table_guard = guard_client.tool(
            "table_export", {"id": table_id, "to": "model"}
        )
        assertTrue(
            table_guard["result"].get("isError"),
            "table_export inline guard did not reject",
        )
        assertTrue(
            "GRAPHMCP_INLINE_MAX_BYTES" in toolText(table_guard),
            "table_export guard message is not actionable",
        )
        guard_client.close()

        # --- 并发 delete/create 后集合一致 ---
        delete_client = MCP(exe, store)
        add_client = MCP(exe, store)
        delete_client.call("initialize", {"protocolVersion": "2024-11-05"})
        add_client.call("initialize", {"protocolVersion": "2024-11-05"})
        race_errors: list[str] = []

        def deleteWorker():
            try:
                for graph_id in created_ids[:20]:
                    response = delete_client.tool(
                        "graph_delete", {"id": graph_id, "force": True}
                    )
                    assertTrue(
                        not response["result"].get("isError"), toolText(response)
                    )
            except Exception as exc:
                race_errors.append(f"delete: {exc}")

        def addWorker():
            try:
                for i in range(20):
                    response = add_client.tool(
                        "graph_create",
                        {
                            "content": f"flowchart LR\nD{i}-->E{i}",
                            "name": f"delete-race-{i}",
                        },
                    )
                    assertTrue(
                        not response["result"].get("isError"), toolText(response)
                    )
            except Exception as exc:
                race_errors.append(f"add: {exc}")

        delete_thread = threading.Thread(target=deleteWorker)
        add_thread = threading.Thread(target=addWorker)
        delete_thread.start()
        add_thread.start()
        delete_thread.join()
        add_thread.join()
        delete_client.close()
        add_client.close()
        assertTrue(not race_errors, f"delete/create race errors: {race_errors}")
        assertStoreConsistent(store)

        # --- 外部转换硬超时与恢复 ---
        fake_bin = pathlib.Path(store) / "fake-bin"
        fake_bin.mkdir()
        if sys.platform == "win32":
            fake_tool = fake_bin / "inkscape.bat"
            fake_tool.write_text(
                "@echo off\r\nping -n 6 127.0.0.1 >nul\r\n", encoding="utf-8"
            )
        else:
            fake_tool = fake_bin / "inkscape"
            fake_tool.write_text("#!/bin/sh\nsleep 5\n", encoding="utf-8")
            fake_tool.chmod(0o755)
        timeout_client = MCP(
            exe,
            store,
            {
                "PATH": str(fake_bin)
                + os.pathsep
                + (os.environ.get("PATH") or ""),
                "GRAPHMCP_EXPORT_TIMEOUT_MS": "200",
            },
        )
        timeout_client.call("initialize", {"protocolVersion": "2024-11-05"})
        timeout_graph = timeout_client.tool(
            "graph_create",
            {"content": "flowchart LR\nT-->U", "name": "timeout-case"},
        )
        timeout_id = graphId(timeout_graph)
        timeout_start = time.perf_counter()
        timeout_result = timeout_client.tool(
            "graph_export",
            {
                "id": timeout_id,
                "to": "png",
                "path": str(pathlib.Path(store) / "timeout.png"),
            },
            timeout_s=5,
        )
        timeout_ms = (time.perf_counter() - timeout_start) * 1000
        assertTrue(
            timeout_result["result"].get("isError"), "timeout was not surfaced"
        )
        assertTrue("timed out" in toolText(timeout_result), toolText(timeout_result))
        assertTrue(timeout_ms < 5_000, "timeout exceeded smoke watchdog")
        recovery = timeout_client.call("ping", timeout_s=2)
        assertTrue(recovery.get("result") == {}, "service did not recover after timeout")
        timeout_client.close()

        testBenchUnits(root)

        print("mcp_perf_smoke: PASS")
        return 0
    finally:
        shutil.rmtree(store, ignore_errors=True)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print(f"mcp_perf_smoke: FAIL: {e}", file=sys.stderr)
        traceback.print_exc()
        raise SystemExit(1)
