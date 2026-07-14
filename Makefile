# graphmcp - graph design & drawing MCP tool
# Works with GNU make (Linux/CI) and mingw32-make (Windows).
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
BIN      := bin

ifeq ($(OS),Windows_NT)
EXE := .exe
MKDIR = mkdir -p $(BIN)
# Static-link the C/C++ runtime so the exe carries no MinGW DLL dependency;
# MCP clients may spawn the server with a stripped PATH where those DLLs
# (and the browser used for PNG/PDF) would otherwise be unreachable.
STATIC := -static -static-libgcc -static-libstdc++
else
EXE :=
MKDIR = mkdir -p $(BIN)
STATIC :=
endif

HDRS := src/json.hpp src/model.hpp src/parsers.hpp src/layout.hpp \
        src/exporters.hpp src/storage.hpp src/mcp.hpp \
        src/mcp_table_tools.hpp src/table_bridge.hpp src/table_model.hpp \
        src/table_storage.hpp src/table_xml.hpp \
        src/version_types.hpp src/cursor_types.hpp src/version_manager.hpp

.PHONY: all test test-all test-version test-cursor bench bench-ci bench-baseline smoke mcp-smoke table-smoke clean export-testout export-table-examples export-table-collab-examples docs-api

all: $(BIN)/graphmcp$(EXE) $(BIN)/graphmcp_tests$(EXE) \
     $(BIN)/graphmcp_version_tests$(EXE) $(BIN)/graphmcp_cursor_tests$(EXE)

$(BIN)/graphmcp$(EXE): src/main.cpp $(HDRS)
	-$(MKDIR)
	$(CXX) $(CXXFLAGS) $(STATIC) -o $@ src/main.cpp

$(BIN)/graphmcp_tests$(EXE): tests/test_main.cpp $(HDRS)
	-$(MKDIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_main.cpp

$(BIN)/graphmcp_version_tests$(EXE): tests/test_version.cpp $(HDRS)
	-$(MKDIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_version.cpp

$(BIN)/graphmcp_cursor_tests$(EXE): tests/test_cursor.cpp $(HDRS)
	-$(MKDIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_cursor.cpp

test: $(BIN)/graphmcp_tests$(EXE)
	$(BIN)/graphmcp_tests$(EXE)

test-version: $(BIN)/graphmcp_version_tests$(EXE)
	$(BIN)/graphmcp_version_tests$(EXE)

test-cursor: $(BIN)/graphmcp_cursor_tests$(EXE)
	$(BIN)/graphmcp_cursor_tests$(EXE)

test-all: test test-version test-cursor
	@echo "=========================================="
	@echo "all unit tests completed"

# ── 性能基准测试 ──
$(BIN)/graphmcp_bench$(EXE): tests/bench_main.cpp $(HDRS)
	-$(MKDIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/bench_main.cpp

bench: $(BIN)/graphmcp_bench$(EXE)
	$(BIN)/graphmcp_bench$(EXE)

# CI 用：运行 bench 并比对基线
BENCH_BASELINE := tests/bench_baseline.json
bench-ci: $(BIN)/graphmcp_bench$(EXE)
	$(BIN)/graphmcp_bench$(EXE) > $(BIN)/bench_result.json
	@if [ -f $(BENCH_BASELINE) ]; then \
		python3 scripts/bench_compare.py $(BENCH_BASELINE) $(BIN)/bench_result.json; \
	else \
		echo "::warning::基线文件不存在，将当前结果写入基线"; \
		cp $(BIN)/bench_result.json $(BENCH_BASELINE); \
	fi

# 更新基线（仅 main 分支使用）
bench-baseline: $(BIN)/graphmcp_bench$(EXE)
	$(BIN)/graphmcp_bench$(EXE) > $(BENCH_BASELINE)
	@echo "baseline updated: $(BENCH_BASELINE)"

smoke: $(BIN)/graphmcp$(EXE)
	bash tests/smoke_test.sh $(BIN)/graphmcp$(EXE)

mcp-smoke: $(BIN)/graphmcp$(EXE)
	bash tests/mcp_smoke.sh $(BIN)/graphmcp$(EXE)

table-smoke: $(BIN)/graphmcp$(EXE)
	bash tests/table_smoke.sh $(BIN)/graphmcp$(EXE)

export-testout: $(BIN)/graphmcp$(EXE)
	bash scripts/export-example-testout.sh $(BIN)/graphmcp$(EXE)

export-table-examples: $(BIN)/graphmcp$(EXE)
	bash scripts/export-table-examples.sh $(BIN)/graphmcp$(EXE)

# 图&表协同增强：rules / fix-enums / derive / slug / sample / propose
export-table-collab-examples: $(BIN)/graphmcp$(EXE)
	bash scripts/export-table-collab-examples.sh $(BIN)/graphmcp$(EXE)

# docs-api: 从运行中的 toolList() 生成 OpenAPI YAML（代码即文档）
docs-api: $(BIN)/graphmcp$(EXE)
	$(BIN)/graphmcp$(EXE) dump-tools --format openapi -o docs/api_reference/openapi.yaml

clean:
	-rm -rf $(BIN) test-store-tmp test-vm-store test-reset-store smoke-test-store-*
