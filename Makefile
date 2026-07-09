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

.PHONY: all test test-all test-version test-cursor smoke clean

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

clean:
	-rm -rf $(BIN) test-store-tmp test-vm-store test-reset-store smoke-test-store-*
