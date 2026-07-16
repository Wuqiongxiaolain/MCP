# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test

```sh
# Windows (MinGW)
mingw32-make all              # → bin/graphmcp.exe + bin/graphmcp_tests.exe
mingw32-make test             # run all tests

# Linux / CI
make all && make test

# Direct single-command build (no make)
g++ -std=c++17 -O2 -Wall -Wextra -static -static-libgcc -static-libstdc++ -o bin/graphmcp.exe src/main.cpp
g++ -std=c++17 -O2 -Wall -Wextra -o bin/graphmcp_tests.exe tests/test_main.cpp

# Run the tool
bin/graphmcp serve             # start MCP server over stdio
bin/graphmcp create --input example.mmd --name "图名"
bin/graphmcp export --id <id> --to svg -o output.svg
```

The project has **zero third-party dependencies** — JSON, XML, and Base64 are all hand-written. A single `g++` invocation is enough to build.

Tests live in `tests/test_main.cpp` (16 test groups, ~168 assertions). They are a standalone executable — no test framework, just a `CHECK(cond)` macro that counts pass/fail. The test runner is `runAll()` at the bottom of the file.

## Architecture

**Core principle: all formats → unified graph model → all output formats.** N parsers × M exporters need only N+M adapters, not N×M converters.cle

```
input (Mermaid / Markdown / CSV / XML / Excalidraw)
        │
  parsers.hpp ──→ model.hpp (Graph) ──→ exporters.hpp
                       │
          ┌────────────┼────────────┐
      layout.hpp   validate    storage.hpp
      (auto-layout) (checks)   (versioned JSON store)
```

### Module map (every file is a header except `main.cpp` and `test_main.cpp`)

| Module            | Purpose                                                                  | Know this                                                                                                                                                                                                                                                      |
| ----------------- | ------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `model.hpp`     | `Graph` / `Node` / `Edge` structs + JSON round-trip                | `node.parent` serves triple duty: tree hierarchy (mindmap/orgchart), subgraph grouping (flowchart), container nesting (architecture). Excalidraw raw `elements[]` are kept losslessly alongside derived logical nodes.                                     |
| `parsers.hpp`   | 5 parsers +`detectFormat()` auto-detection                             | Mermaid parser is hand-written lexer (no regex). Each parser returns a`Graph`.                                                                                                                                                                               |
| `exporters.hpp` | 6 exporters: drawio, Mermaid, Excalidraw, SVG, PNG/PDF, mermaid.live URL | PNG/PDF delegates to external converters (inkscape→rsvg→magick→Chrome/Edge headless) with SVG fallback. Excalidraw export prefers original`elements[]` for lossless round-trip.                                                                           |
| `layout.hpp`    | Validation + auto-layout                                                 | Validation: duplicate IDs, dangling edges, hierarchy cycles, orphan nodes. Layout: Kahn topological layering (with cycle fallback), recursive tree layout for mindmap/orgchart, container bounding-box backfill.                                               |
| `storage.hpp`   | Versioned JSON-file store                                                | `graph-store/index.json` catalog + `{id}/latest.json` + immutable `{id}/versions/vN.json` snapshots. Rollback = old snapshot saved as new version (non-destructive).                                                                                     |
| `cursor.hpp`    | Draft/cursor editing                                                     | Database-cursor semantics over nodes/edges (open/get/next/prev/update/insert/delete/close). Changes land in`draft.json`; `graph_draft commit` freezes a version, `discard` drops the draft.                                                              |
| `mcp.hpp`       | MCP JSON-RPC 2.0 over stdio                                              | 10 tools registered:`graph_create`, `graph_convert`, `graph_export`, `graph_open`, `graph_validate`, `graph_list`, `graph_history`, `graph_rollback`, `graph_cursor`, `graph_draft`. Notifications (no `id` field) are not responded to. |
| `json.hpp`      | JSON parser/serializer                                                   | Recursive descent; objects use`vector<pair>` to preserve insertion order (deterministic output). Handles `\uXXXX` surrogate pairs → UTF-8.                                                                                                                |
| `main.cpp`      | CLI entry point                                                          | 11 subcommands. On Windows: uses`GetCommandLineW` + `WideCharToMultiByte(CP_UTF8)` to get correct UTF-8 argv (otherwise Chinese arguments become garbled).                                                                                                 |

### Single translation unit

All `.hpp` files are header-only. Both `main.cpp` and `test_main.cpp` `#include` the headers they need directly. There is no linking step beyond a single `g++` invocation — no library, no separate compilation. Module boundaries are enforced by C++ namespaces (`gj::` for JSON, `gm::` for graph model).

### Storage layout

```
graph-store/
  index.json              # array of {id, name, type, versionCount, createdAt, updatedAt}
  {graphId}/
    latest.json            # current version (mutable until committed)
    draft.json             # uncommitted cursor edits
    cursors/               # per-cursor state files
    versions/
      v1.json              # immutable snapshots
      v2.json
```

Environment variable `GRAPHMCP_STORE` overrides the store directory (default: `./graph-store`).

## Key implementation details

- **Windows `-static`**: The Makefile static-links libgcc/libstdc++ on Windows so the binary carries no MinGW DLL dependency — MCP clients may spawn the server with a stripped PATH.
- **File output**: `writeFile()` creates parent directories automatically (`ensureParentDirs`). This was added after discovering `ofstream` does not create intermediate directories.
- **PNG/PDF headless browser**: On Windows, the command is written to a temp `.bat` file to avoid cmd.exe quote-stripping; a unique `--user-data-dir` prevents attaching to an existing browser instance.
- **Graph types**: `flowchart`, `architecture`, `er`, `orgchart`, `mindmap`, `whiteboard` — the type string is stored in the model and preserved across round-trips.

## Commit practice

将大功能拆成独立可运行的小 commit，每个 commit 对应一个逻辑完整的改动，写完就提交，不要等全部完成再一次提交。

**原则**：
- 每个 commit 编译通过 + 测试全绿
- 一个 commit 只做一件事（改一个模块、加一个阶段）
- commit message 用英文前缀：`feat:` / `fix:` / `test:` / `bench:` / `merge:`

**示例**（如一次 layout 改善拆分为 5 个 commit）：
```
test: add layer balancing, waypoint, and serialization tests
feat(layout): persist dummy node positions as edge waypoints
feat(layout): add greedy adjacent-swap crossing minimization
feat(layout): add layer balancing with cascade push-down
feat(model): add waypoints field to Edge struct
```

**实施节奏**：
1. 基础设施先行（model 字段、序列化）
2. 核心逻辑递进（layout 阶段1 → 阶段2 → 阶段3）
3. 导出器适配（consumers of new model data）
4. 测试收尾（覆盖新增行为）
