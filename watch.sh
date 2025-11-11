# watch.sh
#!/usr/bin/env zsh
# Usage: ./watch.sh examples/your-example
set -euo pipefail

EX_DIR="${1:?Pass the example directory (e.g., examples/foo)}"
[[ -d "$EX_DIR" ]] || { echo "No such directory: $EX_DIR" >&2; exit 1; }
[[ -f "$EX_DIR/main.c" ]] || { echo "Missing $EX_DIR/main.c" >&2; exit 1; }

# show which file triggered (-p), clear screen (-c), watch new files (-d)
find "$EX_DIR" -type f \( -name '*.c' -o -name '*.h' \) | entr -c -d -p ./build.sh "$EX_DIR"
