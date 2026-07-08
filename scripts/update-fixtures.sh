#!/usr/bin/env bash
# 从 example_input 重生成 example_output 中冒烟比对用的 fixture。
# 用法：在仓库根目录执行  bash scripts/update-fixtures.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BIN="${GRAPHMCP_BIN:-}"
if [[ -z "$BIN" ]]; then
  if [[ -x ./bin/graphmcp ]]; then
    BIN=./bin/graphmcp
  elif [[ -x ./build/graphmcp ]]; then
    BIN=./build/graphmcp
  elif [[ -x ./build/graphmcp.exe ]]; then
    BIN=./build/graphmcp.exe
  else
    echo "error: graphmcp 可执行文件未找到，请设置 GRAPHMCP_BIN 或先构建" >&2
    exit 1
  fi
fi

OUT=examples/example_output
IN=examples/example_input

"$BIN" convert --input "$IN/workflow.drawio" --to mermaid \
  -o "$OUT/workflow.drawio_out/workflow.drawio.mmd"
"$BIN" convert --input "$IN/architecture.xml" --to mermaid \
  -o "$OUT/architecture.xml_out/architecture.xml.mmd"
"$BIN" convert --input "$IN/whiteboard_freedraw.excalidraw" --to mermaid \
  -o "$OUT/whiteboard_freedraw.excalidraw_out/whiteboard_freedraw.excalidraw.mmd"
"$BIN" convert --input "$IN/whiteboard_freedraw.excalidraw" --to svg \
  -o "$OUT/whiteboard_freedraw.excalidraw_out/whiteboard_freedraw.excalidraw.svg"

echo "fixtures updated via $BIN"
