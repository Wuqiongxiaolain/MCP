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
        src/version_types.hpp src/cursor_types.hpp src/version_manager.hpp \

.PHONY: all test test-all test-version test-cursor smoke mcp-smoke clean export-testout docs-api

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

smoke: $(BIN)/graphmcp$(EXE)
	bash tests/smoke_test.sh $(BIN)/graphmcp$(EXE)

mcp-smoke: $(BIN)/graphmcp$(EXE)
	bash tests/mcp_smoke.sh $(BIN)/graphmcp$(EXE)

export-testout: $(BIN)/graphmcp$(EXE)
	bash scripts/export-example-testout.sh $(BIN)/graphmcp$(EXE)

# docs-api: 从运行中的 toolList() 生成 OpenAPI YAML（代码即文档）
docs-api: $(BIN)/graphmcp$(EXE)
	$(BIN)/graphmcp$(EXE) dump-tools --format openapi -o docs/api_reference/openapi.yaml

clean:
	-rm -rf $(BIN) test-store-tmp test-vm-store test-reset-store smoke-test-store-*
