# Desktop-only build **next to** your web files so they don’t get overwritten.

# 1) Create `CMakeLists.txt` in:

`/Users/smielniczuk/Documents/works/raylib-web/examples/squareballpinchpoli/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.15)
project(squareballpinchpoli C)

set(CMAKE_C_STANDARD 99)

# Homebrew raylib: brew install raylib
find_package(PkgConfig REQUIRED)
pkg_check_modules(RAYLIB REQUIRED raylib)

add_executable(squareballpinchpoli main.c)
target_include_directories(squareballpinchpoli PRIVATE ${RAYLIB_INCLUDE_DIRS})
target_link_directories(squareballpinchpoli PRIVATE ${RAYLIB_LIBRARY_DIRS})
target_link_libraries(squareballpinchpoli ${RAYLIB_LIBRARIES})

if(APPLE)
  target_link_libraries(squareballpinchpoli "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()
```

# 2) Build + run (CMake)

```bash
cd /Users/smielniczuk/Documents/works/raylib-web/examples/squareballpinchpoli
mkdir build_desktop && cd build_desktop
cmake ..
cmake --build . --config Release
./squareballpinchpoli
```

**Output:** a desktop executable named `squareballpinchpoli` inside `build_desktop/`. Run it with `./squareballpinchpoli`.

---

# One-liner alternative (no CMake)

```bash
cd /Users/smielniczuk/Documents/works/raylib-web/examples/squareballpinchpoli
clang -std=c99 -O2 main.c \
  $(pkg-config --cflags --libs raylib) \
  -framework Cocoa -framework IOKit -framework CoreVideo \
  -o squareballpinchpoli
./squareballpinchpoli
```

> Requires `brew install raylib pkg-config`.
