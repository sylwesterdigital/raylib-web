// main.c — N independent squares (drag/rotate/pinch per-square) + gradient balls + tap sounds.
// Web/iOS: audio is unlocked via real DOM gesture callbacks (Emscripten HTML5 API).
// Tap pitch follows target square size: smaller square → higher pitch (inverse mapping).
#include "raylib.h"
#include <math.h>
#include <stdlib.h>

#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

// ---------------- Tunables ----------------
#define NUM_BALLS                2000
#define NUM_SQUARES              3

static const float BALL_RADIUS_MIN = 1.0f;
static const float BALL_RADIUS_MAX = 60.0f;
static const float SPEED_MIN       = 1.0f;
static const float SPEED_MAX       = 30.0f;

static const float SQUARE_SIZE_DEFAULT = 80.0f;  // px
static const float SQUARE_MIN_SIDE     = 10.0f;  // px
static const float SQUARE_MAX_SIDE     = 600.0f; // px
static const Color SQUARE_COLOR        = PURPLE;

static const float SPAWN_MARGIN        = 6.0f;
static const int   MAX_SUBSTEPS        = 2;
static const float SEP_BIAS            = 0.50f;
static const int   TRAP_FRAMES_TO_KILL = 3;
static const float TOUCH_DELTA_DEADZONE= 0.5f;   // px
// ------------------------------------------

// ---------- Tap sound config ----------
static const int   TAP_SR        = 48000; // Hz
static const float TAP_BASE_IN   = 660.0f;
static const float TAP_BASE_OUT  = 440.0f;
static const float TAP_MS        = 70.0f;
static const float TAP_GAIN      = 0.20f;

// Size→pitch mapping (inverse): side=SQUARE_MIN_SIDE → FREQ_MAX, side=SQUARE_MAX_SIDE → FREQ_MIN
static const float FREQ_MIN      = 320.0f;   // Hz when square is largest
static const float FREQ_MAX      = 1600.0f;  // Hz when square is smallest
// -------------------------------------

// Gradient stops for ball colors.
static const Color GRADIENT_STOPS[] = {
    (Color){255,255,255,255},
    (Color){30,230,230,255}
};
static const int   GRADIENT_COUNT   = (int)(sizeof(GRADIENT_STOPS)/sizeof(GRADIENT_STOPS[0]));

// ----- Types -----
typedef struct { float x, y, vx, vy, r; int trappedFrames; Color col; } Ball;

typedef struct {
    float x, y;     // center
    float half;     // half side
    float angle;    // degrees
} Square;

typedef struct {
    float *squareX;
    float *squareY;
    float  squareHalf;
    Ball  *balls;
    int    ballCount;
} AppState;

typedef struct { int id; Vector2 pos; } TrackedTouch; // -1 id when empty

// --------- Helpers ---------
static inline Vector2 V2(float x, float y){ Vector2 v=(Vector2){x,y}; return v; }
static inline Vector2 RotateCS(Vector2 v, float c, float s){ return (Vector2){ c*v.x - s*v.y, s*v.x + c*v.y }; }
static inline Vector2 InvRotateCS(Vector2 v, float c, float s){ return (Vector2){ c*v.x + s*v.y, -s*v.x + c*v.y }; }
static inline Vector2 Reflect(Vector2 v, Vector2 n){ float d=v.x*n.x + v.y*n.y; return (Vector2){ v.x-2.0f*d*n.x, v.y-2.0f*d*n.y }; }
static inline int TextureOk(Texture2D t){ return (t.id != 0) && (t.width > 0) && (t.height > 0); }

static inline Vector2 ClosestPointOnSquare(Vector2 p, float h){
    float cx = (p.x < -h) ? -h : (p.x >  h) ?  h : p.x;
    float cy = (p.y < -h) ? -h : (p.y >  h) ?  h : p.y;
    return (Vector2){cx, cy};
}

static inline Color LerpColor(Color a, Color b, float t){
    if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
    Color c;
    c.r = (unsigned char)(a.r + (b.r - a.r) * t);
    c.g = (unsigned char)(a.g + (b.g - a.g) * t);
    c.b = (unsigned char)(a.b + (b.b - a.b) * t);
    c.a = (unsigned char)(a.a + (b.a - a.a) * t);
    return c;
}
static inline Color GradientSample(const Color *stops, int count, float t){
    if (count <= 0) return WHITE;
    if (count == 1) return stops[0];
    if (t <= 0.0f) return stops[0];
    if (t >= 1.0f) return stops[count-1];
    float seg = t * (float)(count - 1);
    int   i   = (int)seg;
    float ft  = seg - (float)i;
    if (i >= count - 1) { i = count - 2; ft = 1.0f; }
    return LerpColor(stops[i], stops[i+1], ft);
}

// ----- Square queries -----
static inline int PointInRotatedSquare(float px, float py, const Square *sq){
    const float PI_F = 3.14159265358979323846f;
    float a = sq->angle*(PI_F/180.0f), c=cosf(a), s=sinf(a);
    Vector2 pl = InvRotateCS((Vector2){ px - sq->x, py - sq->y }, c, s);
    return (fabsf(pl.x) <= sq->half && fabsf(pl.y) <= sq->half);
}
static inline int CenterInsideSquare(const Square *sq, float px, float py){
    return PointInRotatedSquare(px, py, sq);
}
static inline int CenterInsideAnySquare(const Square *sqs, int n, float px, float py){
    for (int i=0;i<n;++i) if (CenterInsideSquare(&sqs[i], px, py)) return 1;
    return 0;
}
static inline void PushOutsideSquareHull(const Square *sq, float r, float *bx, float *by){
    float dx = *bx - sq->x, dy = *by - sq->y;
    float len2 = dx*dx + dy*dy;
    float halfDiag = sq->half * 1.41421356237f;
    float minD = halfDiag + r + SPAWN_MARGIN;
    float minD2 = minD*minD;
    if (len2 < 1e-8f){ dx=1.0f; dy=0.0f; len2=1.0f; }
    if (len2 < minD2){
        float s = minD / sqrtf(len2);
        *bx = sq->x + dx*s; *by = sq->y + dy*s;
    }
}
static int TopSquareAt(float px, float py, const Square *sqs, int count){
    for (int i=count-1;i>=0;--i) if (PointInRotatedSquare(px, py, &sqs[i])) return i;
    return -1;
}

// ----- Ball helpers -----
static inline void AssignBallKinematicsAndColor(Ball *b){
    const float PI_F = 3.14159265358979323846f;
    float angle = (float)GetRandomValue(0, 359) * (PI_F/180.0f);
    float speed = (float)GetRandomValue((int)SPEED_MIN, (int)SPEED_MAX);
    float t01   = (float)GetRandomValue(0,1000)/1000.0f;
    b->r   = BALL_RADIUS_MIN + t01 * (BALL_RADIUS_MAX - BALL_RADIUS_MIN);
    b->col = GradientSample(GRADIENT_STOPS, GRADIENT_COUNT, t01);
    b->vx  = cosf(angle) * speed;
    b->vy  = sinf(angle) * speed;
    if (fabsf(b->vx) < 1e-3f && fabsf(b->vy) < 1e-3f){ b->vx = speed; b->vy = 0.0f; }
    b->trappedFrames = 0;
}
static void RespawnBallOutsideAllSquares(Ball *b, const Square *sqs, int n, float seedX, float seedY){
    AssignBallKinematicsAndColor(b);
    const int   sw = GetScreenWidth();
    const int   sh = GetScreenHeight();
    const float PI_F = 3.14159265358979323846f;

    for (int tries=0; tries<256; ++tries){
        float ang   = (float)GetRandomValue(0, 359) * (PI_F/180.0f);
        float maxHull = 0.0f;
        for (int i=0;i<n;++i){
            float dx = seedX - sqs[i].x, dy = seedY - sqs[i].y;
            float d  = sqrtf(dx*dx + dy*dy);
            float hull = d + sqs[i].half * 1.41421356237f + b->r + SPAWN_MARGIN;
            if (hull > maxHull) maxHull = hull;
        }
        float extra = (float)GetRandomValue(0, 200);
        float radial = (maxHull > 0.0f ? maxHull : 200.0f) + extra;

        float x = seedX + cosf(ang) * radial;
        float y = seedY + sinf(ang) * radial;

        if (x < b->r) x = b->r; if (x > sw - b->r) x = sw - b->r;
        if (y < b->r) y = b->r; if (y > sh - b->r) y = sh - b->r;

        for (int i=0;i<n;++i) PushOutsideSquareHull(&sqs[i], b->r, &x, &y);

        if (!CenterInsideAnySquare(sqs, n, x, y)){
            b->x = x; b->y = y;
            return;
        }
    }

    for (int tries=0; tries<2048; ++tries){
        float x = (float)GetRandomValue((int)BALL_RADIUS_MIN, (int)(sw - BALL_RADIUS_MIN));
        float y = (float)GetRandomValue((int)BALL_RADIUS_MIN, (int)(sh - BALL_RADIUS_MIN));
        if (!CenterInsideAnySquare(sqs, n, x, y)){
            b->x = x; b->y = y;
            return;
        }
    }
    b->x = sw*0.5f; b->y = BALL_RADIUS_MAX + SPAWN_MARGIN;
    for (int i=0;i<n;++i) PushOutsideSquareHull(&sqs[i], b->r, &b->x, &b->y);
}

// Resolve circle vs. specific rotating square.
static inline void ResolveCircleVsSquare(const Square *sq, float radius, float *bx, float *by, float *vx, float *vy){
    const float PI_F = 3.14159265358979323846f;
    float a = sq->angle*(PI_F/180.0f), c=cosf(a), s=sinf(a);

    Vector2 pW = (Vector2){ *bx - sq->x, *by - sq->y };
    Vector2 vW = (Vector2){ *vx, *vy };
    Vector2 pL = InvRotateCS(pW, c, s);
    Vector2 vL = InvRotateCS(vW, c, s);

    Vector2 qL = ClosestPointOnSquare(pL, sq->half);
    float dx = pL.x - qL.x, dy = pL.y - qL.y;
    float dist2 = dx*dx + dy*dy;

    if (dist2 <= radius*radius){
        float dist = (dist2 > 0.0f) ? sqrtf(dist2) : 0.0f;
        Vector2 nL = (dist > 1e-6f) ? (Vector2){ dx/dist, dy/dist }
                                    : (fabsf(vL.x) > fabsf(vL.y)) ? (Vector2){ (vL.x>0.0f)?1.0f:-1.0f, 0.0f }
                                                                   : (Vector2){ 0.0f, (vL.y>0.0f)?1.0f:-1.0f };

        float penetration = (radius - dist) + SEP_BIAS; if (penetration < 0.0f) penetration = 0.0f;
        Vector2 nW = RotateCS(nL, c, s);
        *bx += nW.x * penetration; *by += nW.y * penetration;

        Vector2 vRef = Reflect((Vector2){ *vx, *vy }, nW);
        *vx = vRef.x; *vy = vRef.y;

        *bx += (*vx) * (1.0f/8000.0f);
        *by += (*vy) * (1.0f/8000.0f);
    }
}

// ----- Touch utilities -----
static inline int FindTouchById(int id, Vector2 *outPos){
    int count = GetTouchPointCount();
    for (int i=0;i<count;++i){
        if (GetTouchPointId(i) == id){ *outPos = GetTouchPosition(i); return 1; }
    }
    return 0;
}
static void UpdateTrackedTouches(TrackedTouch *t0, TrackedTouch *t1){
    int count = GetTouchPointCount();
    TrackedTouch prev0 = *t0, prev1 = *t1;
    t0->id = -1; t1->id = -1;
    if (count <= 0) return;

    Vector2 pos;
    if (prev0.id != -1 && FindTouchById(prev0.id, &pos)){ t0->id = prev0.id; t0->pos = pos; }
    if (prev1.id != -1 && FindTouchById(prev1.id, &pos)){
        if (t0->id == -1){ t0->id = prev1.id; t0->pos = pos; }
        else             { t1->id = prev1.id; t1->pos = pos; }
    }
    for (int i=0;i<count && (t0->id == -1 || t1->id == -1); ++i){
        int id = GetTouchPointId(i);
        if (id == t0->id || id == t1->id) continue;
        Vector2 p = GetTouchPosition(i);
        if (t0->id == -1){ t0->id = id; t0->pos = p; }
        else             { t1->id = id; t1->pos = p; }
    }
}

#ifdef PLATFORM_WEB
static EM_BOOL OnResize(int eventType, const EmscriptenUiEvent *ui, void *userData){
    (void)eventType; (void)ui;
    AppState *s = (AppState*)userData;

    double cssW = 0.0, cssH = 0.0;
    emscripten_get_element_css_size("#canvas", &cssW, &cssH);
    const double dpr = emscripten_get_device_pixel_ratio();
    emscripten_set_canvas_element_size("#canvas", (int)(cssW*dpr), (int)(cssH*dpr));
    SetWindowSize((int)cssW, (int)cssH);

    const float sw = (float)GetScreenWidth();
    const float sh = (float)GetScreenHeight();

    for (int i=0;i<s->ballCount;++i){
        Ball *b = &s->balls[i];
        if (b->x < b->r) b->x = b->r;
        if (b->y < b->r) b->y = b->r;
        if (b->x > sw - b->r) b->x = sw - b->r;
        if (b->y > sh - b->r) b->y = sh - b->r;
        b->trappedFrames = 0;
    }
    return EM_TRUE;
}
#endif

// ---------- Procedural click SFX ----------
static Wave MakeTapWave(float freqHz, float ms, float gain, int sr){
    const float durSec = ms * (1.0f/1000.0f);
    int frames = (int)(durSec * (float)sr);
    if (frames < 1) frames = 1;

    float *buf = (float*)MemAlloc(sizeof(float) * frames);
    const float twopi = 6.28318530717958647692f;
    const float dphi  = twopi * freqHz / (float)sr;

    int attack = (int)(0.003f * sr); if (attack < 1) attack = 1; if (attack > frames) attack = frames;
    int decay  = frames - attack;    if (decay  < 1) decay  = 1;

    float phase = 0.0f;
    for (int i=0;i<frames;++i){
        float s = sinf(phase);
        float env = (i < attack) ? ((float)i / (float)attack)
                                 : expf(-6.0f * (float)(i-attack) / (float)decay);
        buf[i] = s * env * gain;
        phase += dphi;
        if (phase > twopi) phase -= twopi;
    }

    Wave w = (Wave){0};
    w.frameCount = (unsigned int)frames;
    w.sampleRate = sr;
    w.sampleSize = 32; // float32
    w.channels   = 1;
    w.data       = buf;
    return w;
}

// ---------- Gesture-safe audio (lazy init) + size→pitch ----------
static int   gAudioReady = 0;
static Sound gTapIn = {0}, gTapOut = {0};

static void EnsureAudioReady(void){
    if (gAudioReady) return;
    InitAudioDevice();
    SetMasterVolume(1.0f);

    Wave wIn  = MakeTapWave(TAP_BASE_IN,  TAP_MS, TAP_GAIN, TAP_SR);
    Wave wOut = MakeTapWave(TAP_BASE_OUT, TAP_MS, TAP_GAIN, TAP_SR);
    gTapIn  = LoadSoundFromWave(wIn);
    gTapOut = LoadSoundFromWave(wOut);
    UnloadWave(wIn);
    UnloadWave(wOut);

    gAudioReady = 1;
}

// Maps current square side (pixels) to target frequency (Hz). Smaller side → higher freq.
static inline float SizeToFreq(float sidePx){
    if (sidePx < SQUARE_MIN_SIDE) sidePx = SQUARE_MIN_SIDE;
    if (sidePx > SQUARE_MAX_SIDE) sidePx = SQUARE_MAX_SIDE;
    float t = (sidePx - SQUARE_MIN_SIDE) / (SQUARE_MAX_SIDE - SQUARE_MIN_SIDE);
    return FREQ_MAX + (FREQ_MIN - FREQ_MAX) * t;
}

// Plays tap with pitch scaled so base generated tone shifts to desired frequency.
static inline void PlayTapInForSide(float sidePx){
    if (!gAudioReady) EnsureAudioReady();
    if (!gAudioReady) return;
    float want = SizeToFreq(sidePx);
    float pitch = want / TAP_BASE_IN;
    if (pitch < 0.25f) pitch = 0.25f; if (pitch > 4.0f) pitch = 4.0f;
    SetSoundPitch(gTapIn, pitch);
    PlaySound(gTapIn);
}
static inline void PlayTapOutForSide(float sidePx){
    if (!gAudioReady) EnsureAudioReady();
    if (!gAudioReady) return;
    float want = SizeToFreq(sidePx);
    float pitch = want / TAP_BASE_OUT;
    if (pitch < 0.25f) pitch = 0.25f; if (pitch > 4.0f) pitch = 4.0f;
    SetSoundPitch(gTapOut, pitch);
    PlaySound(gTapOut);
}

#ifdef PLATFORM_WEB
// One-time DOM gesture hooks so iOS Safari allows WebAudio to start.
static EM_BOOL OnFirstMouse(int eventType, const EmscriptenMouseEvent *e, void *ud){
    (void)eventType; (void)e; (void)ud; EnsureAudioReady(); return EM_TRUE;
}
static EM_BOOL OnFirstTouch(int eventType, const EmscriptenTouchEvent *e, void *ud){
    (void)eventType; (void)e; (void)ud; EnsureAudioReady(); return EM_TRUE;
}
static EM_BOOL OnFirstKey(int eventType, const EmscriptenKeyboardEvent *e, void *ud){
    (void)eventType; (void)e; (void)ud; EnsureAudioReady(); return EM_TRUE;
}
static void InstallWebAudioUnlockers(void){
    emscripten_set_mousedown_callback("#canvas", NULL, EM_TRUE, OnFirstMouse);
    emscripten_set_touchstart_callback("#canvas", NULL, EM_TRUE, OnFirstTouch);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE, OnFirstKey);
}
#endif
// -----------------------------------------------

int main(void){
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1024, 600, "raylib: N squares (drag/rotate/pinch) + gradient balls + tap sounds (size→pitch)");
    SetTargetFPS(90);

    // Enable detailed logging so FILEIO and explicit errors are visible in console.
    SetTraceLogLevel(LOG_DEBUG);

    // --- Texture asset (single canonical path; build preloads at /assets) ---
    static const char *TEX_PATH = "/assets/characters/coco.png";
    if (!FileExists(TEX_PATH)){
        TraceLog(LOG_ERROR, "Missing texture: %s (cwd: %s)", TEX_PATH, GetWorkingDirectory());
        CloseWindow();
        return 1;
    }
    Texture2D texCat = LoadTexture(TEX_PATH);
    if (!TextureOk(texCat)){
        TraceLog(LOG_ERROR, "LoadTexture failed: %s", TEX_PATH);
        CloseWindow();
        return 1;
    }
    SetTextureFilter(texCat, TEXTURE_FILTER_BILINEAR);

#ifdef PLATFORM_WEB
    InstallWebAudioUnlockers();
#endif

    const int swInit = GetScreenWidth();
    const int shInit = GetScreenHeight();

    // Squares init
    Square squares[NUM_SQUARES];
    for (int i=0;i<NUM_SQUARES;++i){
        squares[i].half  = SQUARE_SIZE_DEFAULT * 0.5f;
        squares[i].angle = 0.0f;
    }
    if (NUM_SQUARES >= 1){ squares[0].x = swInit * 0.35f; squares[0].y = shInit * 0.5f; }
    if (NUM_SQUARES >= 2){ squares[1].x = swInit * 0.65f; squares[1].y = shInit * 0.5f; }
    for (int i=2;i<NUM_SQUARES;++i){ squares[i].x = swInit*0.5f; squares[i].y = shInit*0.5f; }

    float pinchBaseDist[NUM_SQUARES];        for (int i=0;i<NUM_SQUARES;++i) pinchBaseDist[i]=0.0f;
    float pinchBaseSide[NUM_SQUARES];        for (int i=0;i<NUM_SQUARES;++i) pinchBaseSide[i]=squares[i].half*2.0f;
    float pinchBaseAngleDeg[NUM_SQUARES];    for (int i=0;i<NUM_SQUARES;++i) pinchBaseAngleDeg[i]=0.0f;
    float pinchStartVecDeg[NUM_SQUARES];     for (int i=0;i<NUM_SQUARES;++i) pinchStartVecDeg[i]=0.0f;

    // Input state
    TrackedTouch t0 = (TrackedTouch){ .id = -1, .pos = (Vector2){0} };
    TrackedTouch t1 = (TrackedTouch){ .id = -1, .pos = (Vector2){0} };
    int prevTouchCount = 0;

    int dragMouseSquare   = -1;
    int rotateMouseSquare = -1;
    int dragTouchSquare   = -1;
    int pinchSquare       = -1;
    int pinchActive       = 0;

    // Balls: spawn outside all squares
    Ball *balls = (Ball*)malloc(sizeof(Ball) * NUM_BALLS);
    if (!balls){
        if (gAudioReady){ UnloadSound(gTapIn); UnloadSound(gTapOut); CloseAudioDevice(); }
        UnloadTexture(texCat);
        CloseWindow();
        return 1;
    }
    {
        float seedX = swInit * 0.5f, seedY = shInit * 0.5f;
        for (int i=0;i<NUM_BALLS;++i) RespawnBallOutsideAllSquares(&balls[i], squares, NUM_SQUARES, seedX, seedY);
    }

#ifdef PLATFORM_WEB
    AppState state = { .squareX=&squares[0].x, .squareY=&squares[0].y, .squareHalf=squares[0].half, .balls=balls, .ballCount=NUM_BALLS };
    OnResize(0, NULL, &state);
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &state, EM_TRUE, OnResize);
#endif

    while (!WindowShouldClose()){
        const float dt   = GetFrameTime();
        const int   swWin = GetScreenWidth();
        const int   shWin = GetScreenHeight();

        // ---------- INPUT ----------
        int touchCount = GetTouchPointCount();

        if (touchCount == 0){
            Vector2 mpos = GetMousePosition();

            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)){
                dragMouseSquare = TopSquareAt(mpos.x, mpos.y, squares, NUM_SQUARES);
                if (dragMouseSquare != -1){
                    PlayTapInForSide(squares[dragMouseSquare].half * 2.0f);
                }else{
                    PlayTapInForSide(SQUARE_SIZE_DEFAULT);
                }
            }
            if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)){
                int idx = (dragMouseSquare != -1) ? dragMouseSquare : TopSquareAt(mpos.x, mpos.y, squares, NUM_SQUARES);
                if (idx < 0) idx = 0;
                PlayTapOutForSide(squares[idx].half * 2.0f);
                dragMouseSquare = -1;
            }

            if (dragMouseSquare != -1 && IsMouseButtonDown(MOUSE_LEFT_BUTTON)){
                Vector2 d = GetMouseDelta();
                if (fabsf(d.x) > TOUCH_DELTA_DEADZONE || fabsf(d.y) > TOUCH_DELTA_DEADZONE){
                    squares[dragMouseSquare].x += d.x;
                    squares[dragMouseSquare].y += d.y;
                }
            }

            if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)){
                rotateMouseSquare = TopSquareAt(mpos.x, mpos.y, squares, NUM_SQUARES);
                if (rotateMouseSquare != -1){
                    PlayTapInForSide(squares[rotateMouseSquare].half * 2.0f);
                }else{
                    PlayTapInForSide(SQUARE_SIZE_DEFAULT);
                }
            }
            if (IsMouseButtonReleased(MOUSE_RIGHT_BUTTON)){
                int idx = (rotateMouseSquare != -1) ? rotateMouseSquare : TopSquareAt(mpos.x, mpos.y, squares, NUM_SQUARES);
                if (idx < 0) idx = 0;
                PlayTapOutForSide(squares[idx].half * 2.0f);
                rotateMouseSquare = -1;
            }

            if (rotateMouseSquare != -1 && IsMouseButtonDown(MOUSE_RIGHT_BUTTON)){
                Vector2 d = GetMouseDelta();
                if (fabsf(d.x) > TOUCH_DELTA_DEADZONE) squares[rotateMouseSquare].angle += d.x * 0.35f;
            }

            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f){
                int idx = TopSquareAt(mpos.x, mpos.y, squares, NUM_SQUARES);
                if (idx < 0) idx = NUM_SQUARES-1;
                float side = squares[idx].half * 2.0f;
                side += wheel * 8.0f;
                if (side < SQUARE_MIN_SIDE) side = SQUARE_MIN_SIDE;
                if (side > SQUARE_MAX_SIDE) side = SQUARE_MAX_SIDE;
                squares[idx].half = side * 0.5f;
            }

            t0.id = -1; t1.id = -1; prevTouchCount = 0; pinchActive = 0; dragTouchSquare = -1; pinchSquare = -1;
        } else {
            TrackedTouch prev0 = t0, prev1 = t1;
            UpdateTrackedTouches(&t0, &t1);
            int effectiveCount = (t0.id != -1) + (t1.id != -1);

            if (prevTouchCount >= 2 && effectiveCount == 1){
                pinchActive = 0; pinchSquare = -1;
                if (t0.id != -1) t0.pos = t0.pos; else if (t1.id != -1) t1.pos = t1.pos;
            }
            if (prevTouchCount == 0 && effectiveCount >= 1){
                if (t0.id != -1) prev0 = t0;
                if (t1.id != -1) prev1 = t1;
            }

            if (effectiveCount == 1){
                const TrackedTouch *a = (t0.id != -1)? &t0 : &t1;
                if (prevTouchCount == 0){
                    dragTouchSquare = TopSquareAt(a->pos.x, a->pos.y, squares, NUM_SQUARES);
                    if (dragTouchSquare != -1) PlayTapInForSide(squares[dragTouchSquare].half * 2.0f);
                    else                       PlayTapInForSide(SQUARE_SIZE_DEFAULT);
                }
                if (dragTouchSquare != -1){
                    Vector2 base = (a->id == prev0.id) ? prev0.pos : prev1.pos;
                    Vector2 d = (Vector2){ a->pos.x - base.x, a->pos.y - base.y };
                    if (fabsf(d.x) > TOUCH_DELTA_DEADZONE || fabsf(d.y) > TOUCH_DELTA_DEADZONE){
                        squares[dragTouchSquare].x += d.x;
                        squares[dragTouchSquare].y += d.y;
                    }
                }
                pinchActive = 0; pinchSquare = -1;
            } else if (effectiveCount >= 2){
                if (prevTouchCount < 2 && pinchSquare == -1){
                    Vector2 c = (Vector2){ (t0.pos.x+t1.pos.x)*0.5f, (t0.pos.y+t1.pos.y)*0.5f };
                    int sIdx = TopSquareAt(c.x, c.y, squares, NUM_SQUARES);
                    if (sIdx < 0){
                        int a = TopSquareAt(t0.pos.x, t0.pos.y, squares, NUM_SQUARES);
                        int b = TopSquareAt(t1.pos.x, t1.pos.y, squares, NUM_SQUARES);
                        sIdx = (a>=0)? a : b;
                    }
                    pinchSquare = sIdx;
                    if (pinchSquare != -1) PlayTapInForSide(squares[pinchSquare].half * 2.0f);
                    else                    PlayTapInForSide(SQUARE_SIZE_DEFAULT);
                    pinchActive = 0;
                }

                if (pinchSquare != -1){
                    Square *sq = &squares[pinchSquare];

                    Vector2 curC = (Vector2){ (t0.pos.x + t1.pos.x)*0.5f, (t0.pos.y + t1.pos.y)*0.5f };
                    Vector2 prvC;
                    if      (t0.id == prev0.id && t1.id == prev1.id) prvC = (Vector2){ (prev0.pos.x+prev1.pos.x)*0.5f,(prev0.pos.y+prev1.pos.y)*0.5f };
                    else if (t0.id == prev1.id && t1.id == prev0.id) prvC = (Vector2){ (prev0.pos.x+prev1.pos.x)*0.5f,(prev0.pos.y+prev1.pos.y)*0.5f };
                    else                                              prvC = curC;
                    Vector2 cd = (Vector2){ curC.x - prvC.x, curC.y - prvC.y };
                    if (fabsf(cd.x) > TOUCH_DELTA_DEADZONE || fabsf(cd.y) > TOUCH_DELTA_DEADZONE){
                        sq->x += cd.x; sq->y += cd.y;
                    }

                    // Absolute 2-finger vector (current)
                    Vector2 vCurr = (Vector2){ t1.pos.x - t0.pos.x, t1.pos.y - t0.pos.y };
                    float currDist = sqrtf(vCurr.x*vCurr.x + vCurr.y*vCurr.y);
                    float currAngDeg = atan2f(vCurr.y, vCurr.x) * 57.2957795f; // radians→degrees

                    // Determine if the same pair is tracked; if not, (re)seed bases.
                    int havePrevPair = ((t0.id == prev0.id && t1.id == prev1.id) || (t0.id == prev1.id && t1.id == prev0.id));
                    if (!havePrevPair || prevTouchCount < 2){
                        pinchBaseDist[pinchSquare]     = (currDist > 0.0f) ? currDist : 1.0f;
                        pinchBaseSide[pinchSquare]     = sq->half * 2.0f;
                        pinchBaseAngleDeg[pinchSquare] = sq->angle;
                        pinchStartVecDeg[pinchSquare]  = currAngDeg;
                        pinchActive = 1;
                    } else if (pinchActive){
                        // Scale
                        if (currDist > 0.0f && pinchBaseDist[pinchSquare] > 0.0f){
                            float side = pinchBaseSide[pinchSquare] * (currDist / pinchBaseDist[pinchSquare]);
                            if (side < SQUARE_MIN_SIDE) side = SQUARE_MIN_SIDE;
                            if (side > SQUARE_MAX_SIDE) side = SQUARE_MAX_SIDE;
                            sq->half = side * 0.5f;
                        }
                        // Rotation by absolute delta from initial pinch vector
                        float delta = currAngDeg - pinchStartVecDeg[pinchSquare];
                        while (delta > 180.0f)  delta -= 360.0f;
                        while (delta < -180.0f) delta += 360.0f;
                        sq->angle = pinchBaseAngleDeg[pinchSquare] + delta;
                    }
                }
            }

            if (effectiveCount == 0 && prevTouchCount > 0){
                int idx = -1;
                if (dragTouchSquare != -1) idx = dragTouchSquare;
                else if (pinchSquare != -1) idx = pinchSquare;
                if (idx < 0) idx = 0;
                PlayTapOutForSide(squares[idx].half * 2.0f);
            }
            prevTouchCount = effectiveCount;
        }

        // Clamp squares inside window
        for (int i=0;i<NUM_SQUARES;++i){
            if (squares[i].x < squares[i].half) squares[i].x = squares[i].half;
            if (squares[i].y < squares[i].half) squares[i].y = squares[i].half;
            if (squares[i].x > swWin - squares[i].half) squares[i].x = swWin - squares[i].half;
            if (squares[i].y > shWin - squares[i].half) squares[i].y = shWin - squares[i].half;
        }

        // ---------- Simulation ----------
        for (int i=0;i<NUM_BALLS;++i){
            Ball *b = &balls[i];

            float spd = hypotf(b->vx, b->vy);
            int steps = (spd > 0.0f) ? 1 + (int)((spd * dt) / fmaxf(b->r*2.0f, 2.0f)) : 1;
            if (steps > MAX_SUBSTEPS) steps = MAX_SUBSTEPS; if (steps < 1) steps = 1;
            float sdt = dt / (float)steps;

            for (int s=0;s<steps;++s){
                b->x += b->vx * sdt;
                b->y += b->vy * sdt;

                if (b->x - b->r < 0.0f){ b->x = b->r;       b->vx = -b->vx; }
                if (b->x + b->r > swWin){ b->x = swWin-b->r; b->vx = -b->vx; }
                if (b->y - b->r < 0.0f){ b->y = b->r;       b->vy = -b->vy; }
                if (b->y + b->r > shWin){ b->y = shWin-b->r; b->vy = -b->vy; }

                for (int k=0;k<NUM_SQUARES;++k){
                    float halfDiag = squares[k].half * 1.41421356237f;
                    float dx = b->x - squares[k].x, dy = b->y - squares[k].y;
                    float maxR = halfDiag + b->r;
                    if (dx*dx + dy*dy <= maxR*maxR){
                        ResolveCircleVsSquare(&squares[k], b->r, &b->x, &b->y, &b->vx, &b->vy);
                    }
                }
            }

            int insideAny = 0;
            for (int k=0;k<NUM_SQUARES && !insideAny;++k){
                if (CenterInsideSquare(&squares[k], b->x, b->y)) insideAny = 1;
            }
            if (insideAny){
                RespawnBallOutsideAllSquares(b, squares, NUM_SQUARES, swWin*0.5f, shWin*0.5f);
            } else {
                b->trappedFrames = 0;
            }
        }

        // ---------- Draw ----------
        BeginDrawing();
            ClearBackground(WHITE);

            // Determine the active square (rendered on top) based on current interaction.
            int activeIdx = -1;
            if (pinchSquare       != -1) activeIdx = pinchSquare;
            else if (rotateMouseSquare != -1) activeIdx = rotateMouseSquare;
            else if (dragTouchSquare   != -1) activeIdx = dragTouchSquare;
            else if (dragMouseSquare   != -1) activeIdx = dragMouseSquare;

            // Draw non-active squares first (back-to-front), then the active one last (on top).
            for (int i=0;i<NUM_SQUARES;++i){
                if (i == activeIdx) continue;
                float sideNow = squares[i].half * 2.0f;
                float sx = (float)texCat.width, sy = (float)texCat.height;
                float scale = fmaxf(sideNow / sx, sideNow / sy);
                Rectangle src  = (Rectangle){ 0.0f, 0.0f, sx, sy };
                Rectangle dest = (Rectangle){ squares[i].x, squares[i].y, sx*scale, sy*scale };
                Vector2   origin = (Vector2){ dest.width*0.5f, dest.height*0.5f };
                DrawTexturePro(texCat, src, dest, origin, squares[i].angle, WHITE);
            }
            if (activeIdx != -1){
                float sideNow = squares[activeIdx].half * 2.0f;
                float sx = (float)texCat.width, sy = (float)texCat.height;
                float scale = fmaxf(sideNow / sx, sideNow / sy);
                Rectangle src  = (Rectangle){ 0.0f, 0.0f, sx, sy };
                Rectangle dest = (Rectangle){ squares[activeIdx].x, squares[activeIdx].y, sx*scale, sy*scale };
                Vector2   origin = (Vector2){ dest.width*0.5f, dest.height*0.5f };
                DrawTexturePro(texCat, src, dest, origin, squares[activeIdx].angle, WHITE);
            }

            for (int i=0;i<NUM_BALLS;++i){
                if (balls[i].r <= 1.5f) DrawPixelV(V2(balls[i].x, balls[i].y), balls[i].col);
                else                     DrawCircleV(V2(balls[i].x, balls[i].y), balls[i].r, balls[i].col);
            }
        EndDrawing();
    }

    if (gAudioReady){
        UnloadSound(gTapIn);
        UnloadSound(gTapOut);
        CloseAudioDevice();
    }
    UnloadTexture(texCat);
    free(balls);
    CloseWindow();
    return 0;
}
