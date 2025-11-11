How to ship your existing C raylib app as an iOS app (same touch/rotate/pinch logic; no web bits).

# 1) Get raylib and build an iOS XCFramework (device + simulator)

```bash
# From your repo root (or anywhere)
git clone https://github.com/raysan5/raylib.git
cd raylib

# Build static lib for device
xcodebuild \
  -project projects/IOS/raylib.xcodeproj \
  -scheme raylib \
  -configuration Release \
  -sdk iphoneos \
  BUILD_LIBRARY_FOR_DISTRIBUTION=YES \
  clean build \
  SYMROOT=$(pwd)/build_ios

# Build static lib for simulator
xcodebuild \
  -project projects/IOS/raylib.xcodeproj \
  -scheme raylib \
  -configuration Release \
  -sdk iphonesimulator \
  BUILD_LIBRARY_FOR_DISTRIBUTION=YES \
  clean build \
  SYMROOT=$(pwd)/build_sim

# Create an XCFramework (drop-in for Xcode projects)
xcodebuild -create-xcframework \
  -library build_ios/Release-iphoneos/libraylib.a -headers src \
  -library build_sim/Release-iphonesimulator/libraylib.a -headers src \
  -output raylib.xcframework
```

You’ll now have `raylib.xcframework` in `raylib/`.

# 2) Make an iOS Xcode app target that links raylib

1. Open Xcode → File → New → Project… → iOS App (Language: Objective-C or Swift; either is fine).
2. Drag `raylib/raylib.xcframework` into your app’s project (tick “Copy items if needed”; add to app target).
3. Add your `main.c` to the app target (use your desktop/mobile version that already **guards web-only code with `#ifdef PLATFORM_WEB`** so the Emscripten bits are excluded on iOS).
4. In **Build Settings** of the app target:

   * **Other Linker Flags**: add
     `-ObjC`
   * **Header Search Paths**: add the raylib headers path (e.g. `$(PROJECT_DIR)/raylib/src`)
5. In **General → Frameworks, Libraries, and Embedded Content**, add these system frameworks (raylib iOS expects them):

   * **UIKit.framework**
   * **Foundation.framework**
   * **CoreGraphics.framework**
   * **CoreMotion.framework**
   * **CoreVideo.framework**
   * **AudioToolbox.framework**
   * **AVFoundation.framework**
   * **GameController.framework**
   * **OpenGLES.framework**  *(OpenGL ES is deprecated on iOS but still works; raylib uses ES 2.0 on iOS)*
6. In **Signing & Capabilities**, set your Team and a unique Bundle Identifier.
7. Run on Simulator or a real device.

# 3) Your C code (what to keep/change)

* Keep your current `main.c` (touch/rotate/pinch logic) **as is**, with the `#ifdef PLATFORM_WEB` sections present.
  On iOS, `PLATFORM_WEB` is not defined, so all web/Emscripten canvas + resize code is excluded automatically.
* `InitWindow(...)` just uses the device screen; your `GetScreenWidth/Height()` logic already works the same.
* Touch: your use of `GetTouchPointCount()`, `GetTouchPointId()`, and `GetTouchPosition()` maps to iOS multi-touch. No extra glue needed.

# 4) Notes

* The produced `.app` will be much larger than your 35KB desktop binary; that’s expected on iOS.
* If you later want Metal instead of OpenGL ES, you’ll need a raylib build with the Metal backend (experimental in some forks); the steps above use the standard OpenGL ES path that works out of the box.

That’s it: build raylib XCFramework once, make an iOS app target, link the XCFramework + system frameworks, drop in your `main.c`, and run.
