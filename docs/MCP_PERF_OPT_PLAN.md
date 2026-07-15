# MCP 性能优化计划

> 依据：2026-07-14 审查 Canvas 与隔离压测（`bin/audit_dynamic.json`）  
> 范围：性能热点 + P0 并发一致性。不含 pipeline 工具 / 完整 outputSchema 重构。  
> 状态：**已落地（2026-07-15）**

## 验收基线（审查实测）

| 场景 | 实测结果 | 优化目标 | 落地结果 |
|------|----------|----------|----------|
| 同 store 并发写 | index=1 / dirs=60 / 工具错=0 | index==dirs | `mcp_perf_smoke`：60/60，errors=0 |
| 大单行 `graph_create` (~110KB) | ~200 ms | 追踪不恶化并争取下降 | 紧凑序列化 + 原子写已启用 |
| heavy call 后慢导出 | 可能永久占坑 | 硬超时后返回 | `GRAPHMCP_EXPORT_TIMEOUT_MS`（默认 60s） |
| bench 单位混用 | 假退化 | 规范化比较 | `bench_compare.py` 已统一到 us/MB |

## 成功标准

- 双进程同 store 各写 30 次后 `index_graph_count == graph_dirs_count`，工具错误为 0
- 慢导出硬超时后返回 `isError`，服务端可继续处理后续请求
- MCP 响应体默认紧凑 dump（非 pretty）；`store_save` 共享一次 model 序列化
- 并发/超时回归可通过 `python scripts/mcp_perf_smoke.py`（Windows 可用）

## 阶段与代码落点

1. **P0 存储**：[`src/storage.hpp`](storage.hpp) 原子写 + `ge::StoreLock` + index 合并写；删除走 `removeGraphFromIndex`
2. **P0 超时**：`runQuiet` / `launchBrowser` 硬超时；`ExportResult.timedOut`；SVG 降级提示
3. **P1 JSON**：MCP/表工具 `dump()`；`tools/list` 进程缓存；`GRAPHMCP_INLINE_MAX_BYTES`（默认 1MB）
4. **P1 I/O**：`materializeDraftWithDraft`；`versions/vN.meta.json`
5. **基准**：`scripts/bench_compare.py` 单位归一化；`scripts/mcp_perf_smoke.py`；`make perf-smoke`

## 运行

```sh
mingw32-make all
mingw32-make test-all
python scripts/mcp_perf_smoke.py bin/graphmcp.exe
# 或: mingw32-make perf-smoke
```

## 环境变量

| 变量 | 默认 | 含义 |
|------|------|------|
| `GRAPHMCP_EXPORT_TIMEOUT_MS` | 60000 | 外部转换硬超时 |
| `GRAPHMCP_INLINE_MAX_BYTES` | 1048576 | 内联导出护栏 |

## 边界

- 不做多线程/异步 `serve`
- 不换第三方 JSON 库
- 不新增 pipeline 工具（另案）
