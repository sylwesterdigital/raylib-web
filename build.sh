# build.sh
#!/usr/bin/env zsh
# Usage: ./build.sh examples/your-example
set -euo pipefail

EX_DIR="${1:?Pass the example directory (e.g., examples/foo)}"
SRC="$EX_DIR/main.c"
OUT_JS="$EX_DIR/index.js"

[[ -f "$SRC" ]] || { echo "Missing $SRC" >&2; exit 1; }

emcc "$SRC" \
  -o "$OUT_JS" \
  -I"$RAYLIB_INCLUDE" "$RAYLIB_WEB_LIB" \
  -DPLATFORM_WEB \
  -s WASM=1 \
  -s MODULARIZE=1 \
  -s EXPORT_ES6=1 \
  -s ENVIRONMENT=web \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s ASYNCIFY \
  -s USE_GLFW=3 \
  -O2
