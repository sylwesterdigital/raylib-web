Yu’ll need Nintendo’s **official SDK + dev kit** (closed under NDA) or a **Switch porting partner**. raylib doesn’t ship public Switch support; the platform layer must be swapped for Nintendo APIs.

## What it takes

1. **Apply & get access**

   * Join **Nintendo Developer Portal** and get approved.
   * Order a **Nintendo Switch development kit**.
   * Download the official **SDK, toolchain, docs** (all under NDA).

2. **Choose the path**

   * **In-house port**: replace raylib’s platform backends (windowing, input, graphics, audio, filesystem, timing) with Nintendo’s APIs.
   * **Porting studio**: hire a certified partner; hand them your C codebase.
   * **Engine migration** (if desired): move to an engine with Switch exporters (then re-implement your gameplay). Not required, just an option.

3. **raylib → Switch: technical map (in-house)**

   * **Graphics**: raylib uses OpenGL; Switch uses Nintendo’s graphics stack (NVN). Implement a **rlgl backend** for NVN or render via the SDK’s GL/EGL layer if available in your program tier (details are NDA).
   * **Window / app loop**: replace `InitWindow/BeginDrawing/EndDrawing` with SDK app lifecycle & frame presentation.
   * **Input**: map Joy-Con/Pro Controller to your current mouse/touch/keys:

     * Sticks → translation of the square(s).
     * Gyro/shoulders or right stick → rotation.
     * Triggers or ± buttons → scale.
     * Support **handheld**, **single Joy-Con**, **dual Joy-Con**, **Pro**.
   * **Touch**: map the capacitive screen to your existing touch handlers (handheld only).
   * **Audio**: swap to `nn::audio` (or SDK audio service); convert assets to recommended formats.
   * **Filesystem**: replace `Load/Save` with the SDK’s content & save-data services.
   * **Timing/threads**: use the SDK’s time primitives; avoid busy waits.
   * **Shaders**: if you introduce any, compile via Nintendo’s shader pipeline; avoid desktop-GL only features.

4. **Performance targets**

   * Aim **60 FPS** at **720p handheld** and **1080p docked**.
   * Batch draw calls; use a single dynamic vertex buffer for balls (you can instance tiny quads or draw points + shader).
   * Keep allocations off the frame loop; no per-frame malloc/free.
   * Use **-O2/-O3**, LTO if supported; profile on hardware.

5. **Platform features**

   * **Suspend/Resume**: handle sleep, home button, controller disconnect.
   * **Safe areas** & **docking** resolution changes.
   * **Parental controls**, **user selection**, **language/region**.

6. **Compliance**

   * Prepare for **Lotcheck/TRC**: no crashes, no forbidden terminology, proper controller diagrams, error dialogs, save-data flows, screenshots behavior, etc.
   * **Age ratings** (IARC/PEGI/ESRB), **privacy**, **accessibility** basics.

7. **Store readiness**

   * eShop assets (capsules, video, descriptions), localized.
   * Cloud saves / achievements if you add them (follow guidelines).

8. **Build & QA**

   * Set up **CI** to produce devkit builds.
   * On-device performance + soak tests; input matrix across all controller types.

9. **If you prefer a partner**

   * Provide: source, asset list, licenses, third-party libs, expected FPS/resolutions, feature list.
   * They’ll handle SDK specifics and certification.

10. **Scope estimate checklist (you fill)**

* Number of effects/shaders?
* Save system?
* Localization?
* Online features? (If yes, extra SDK services + cert items.)
* Target FPS/resolutions?

## Practical next steps for you

* Apply on Nintendo’s portal and get a dev kit.
* Decide **in-house vs partner**.
* If in-house: start by stubbing a minimal **NVN/SDK render + input loop**, then port:

  1. rendering of one square,
  2. input mapping,
  3. balls batch rendering,
  4. timing & suspend/resume,
  5. audio,
  6. save-data (if needed).
* Keep the web/desktop build for iteration; guard Switch code with `#if defined(PLATFORM_SWITCH)` to share gameplay code.

> Note: exact API names, headers, and code samples from the SDK cannot be shared here due to NDA. Once you’re in the program, the official samples make the platform layer work straightforward—your gameplay loop and physics can remain essentially unchanged.
