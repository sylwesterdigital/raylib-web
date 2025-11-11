# build.sh
#!/usr/bin/env zsh
# Usage: ./build.sh examples/<name>
set -euo pipefail

EX_DIR="${1:?Pass the example directory (e.g., examples/square)}"
SRC="$EX_DIR/main.c"
OUT_JS="$EX_DIR/index.js"

# Defaults (can be overridden by env)
export RAYLIB_INCLUDE="${RAYLIB_INCLUDE:-$PWD/raylib/src}"
export RAYLIB_WEB_LIB="${RAYLIB_WEB_LIB:-$PWD/third_party/raylib_web/raylib/libraylib.a}"

echo "[build $(date '+%H:%M:%S')] $SRC → $OUT_JS"
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
echo "[done  $(date '+%H:%M:%S')] wrote $OUT_JS and ${OUT_JS%.js}.wasm"
