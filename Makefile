# graphmcp - graph design & drawing MCP tool
# Works with GNU make (Linux/CI) and mingw32-make (Windows).
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
BIN      := bin

ifeq ($(OS),Windows_NT)
EXE := .exe
MKDIR = if not exist $(BIN) mkdir $(BIN)
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
        src/exporters.hpp src/storage.hpp src/cursor.hpp src/mcp.hpp

.PHONY: all test clean

all: $(BIN)/graphmcp$(EXE) $(BIN)/graphmcp_tests$(EXE)

$(BIN)/graphmcp$(EXE): src/main.cpp $(HDRS)
	-$(MKDIR)
	$(CXX) $(CXXFLAGS) $(STATIC) -o $@ src/main.cpp

$(BIN)/graphmcp_tests$(EXE): tests/test_main.cpp $(HDRS)
	-$(MKDIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_main.cpp

test: $(BIN)/graphmcp_tests$(EXE)
	$(BIN)/graphmcp_tests$(EXE)

clean:
	-rm -rf $(BIN) test-store-tmp
