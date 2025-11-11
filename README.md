Shortest path: **build for the web (HTML5) and open it on your phone’s browser.**
No Android phone required.

# Easiest “phone” build (HTML5)

1. Install Emscripten (emsdk).
2. Build raylib for the web.
3. Compile a tiny example and serve it; open on the phone via the local network.

### Minimal example (`main.c`)

```c
#include "raylib.h"

int main(void) {
    const int screenWidth = 800;
    const int screenHeight = 450;
    InitWindow(screenWidth, screenHeight, "raylib on phone (HTML5)");

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawText("Hello from raylib!", 190, 200, 20, DARKGRAY);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
```

### Build & run (typical flow)

```bash
# 1) Get emscripten
git clone https://github.com/emscripten-core/emsdk
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh

# 2) Get raylib
cd ..
git clone https://github.com/raysan5/raylib
cd raylib
make PLATFORM=PLATFORM_WEB  # builds raylib for HTML5

# 3) Build your app
# Put main.c next to raylib/src or set include/lib paths accordingly:
emcc ../main.c -o index.html \
  -I./src -L./src \
  -s USE_GLFW=3 -s FULL_ES2=1 \
  --preload-file resources@/resources \
  -lraylib -s ASYNCIFY

# 4) Serve and test on phone (same Wi-Fi)
emrun --no_browser --port 8080 .
# On the phone, open: http://<your-computer-LAN-IP>:8080/index.html
```

This gives a touch-friendly canvas in the browser, works on iOS and Android, and is perfect for quick demos. You can later wrap it as a PWA if desired.

# Native Android (optional)

* An Android phone is **not required** to get started; use the Android Emulator in Android Studio.
* For a native APK:

  1. Install Android Studio (+ NDK + CMake).
  2. Create a Native C++ project, add raylib (as a submodule or prebuilt .aar/.so), link in `CMakeLists.txt`, target GLES2, minSdk 21+.
  3. Run on the emulator; plug in a real device later for sensors/perf.

**Summary:** The fastest way to “produce something for the phone” with raylib is the HTML5 build and opening it on the phone’s browser. An Android phone is not needed; use either the browser route or the Android emulator.
