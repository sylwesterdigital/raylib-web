// main.c — update posX/posY from window size via resize callback
#include "raylib.h"
#include <stdio.h>

#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

typedef struct {
    float *posX;
    float *posY;
} AppState;

static EM_BOOL OnResize(int eventType, const EmscriptenUiEvent *ui, void *userData) {
    (void)eventType;
    AppState *state = (AppState*)userData;

    // Sync canvas pixels to CSS size * DPR
    double cssW = 0.0, cssH = 0.0;
    emscripten_get_element_css_size("#canvas", &cssW, &cssH);
    const double dpr = emscripten_get_device_pixel_ratio();
    const int w = (int)(cssW * dpr), h = (int)(cssH * dpr);
    emscripten_set_canvas_element_size("#canvas", w, h);
    SetWindowSize((int)cssW, (int)cssH);

    // Update position based on window size (example: keep centered)
    *state->posX = (float)cssW * 0.5f;
    *state->posY = (float)cssH * 0.5f;

    printf("[resize] ui.inner=%dx%d css=%.0fx%.0f dpr=%.2f pos=(%.1f,%.1f)\n",
           ui ? ui->windowInnerWidth : -1, ui ? ui->windowInnerHeight : -1,
           cssW, cssH, dpr, *state->posX, *state->posY);
    fflush(stdout);
    return EM_TRUE;
}
#endif

int main(void) {
    const int size = 320;
    float posX = 200.0f, posY = 300.0f;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(800, 450, "raylib: resize updates posX/posY");
    SetTargetFPS(60);

#ifdef PLATFORM_WEB
    AppState state = { .posX = &posX, .posY = &posY };
    OnResize(0, NULL, &state);
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &state, EM_TRUE, OnResize);
#endif

    float angle = 0.0f;

    while (!WindowShouldClose()) {
        angle += 10.0f * GetFrameTime();

        BeginDrawing();
            ClearBackground(DARKGRAY);
            Rectangle rec = (Rectangle){ posX, posY, (float)size, (float)size };
            Vector2 origin = (Vector2){ size/2.0f, size/2.0f };
            DrawRectanglePro(rec, origin, angle, RED);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
