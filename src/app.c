// app.c
#include "app.h"
#include "ui/nk_common.h"
#include <windows.h>
#include <stdio.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#define MAX_VERTEX_BUFFER (512 * 1024)
#define MAX_ELEMENT_BUFFER (128 * 1024)

#define FAIL(MSG)                                                              \
  do {                                                                         \
    MessageBoxA(NULL, MSG, "SpecsViewer error", MB_OK | MB_ICONERROR);         \
    return 0;                                                                  \
  } while (0)

int app_init(struct AppState *S, int w, int h, const char *title) {
  /* ----------------------------- GLFW + Window -----------------------------
   */
  if (!glfwInit())
    FAIL("glfwInit failed");

  // OpenGL 2.1 context (simple + compatible with Nuklear GL2 backend)
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

  // Fixed-size UI window
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  S->window = glfwCreateWindow(w, h, title, NULL, NULL);
  if (!S->window) {
    glfwTerminate();
    FAIL("glfwCreateWindow failed (OpenGL not supported)");
  }

  glfwMakeContextCurrent(S->window);
  glfwSwapInterval(1); // VSync ON

  /* Set window icon (title bar + taskbar) from the embedded .rc resource */
  {
    HWND hwnd = glfwGetWin32Window(S->window);
    HICON hIconBig   = (HICON)LoadImageA(GetModuleHandle(NULL), MAKEINTRESOURCE(1),
                                         IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    HICON hIconSmall = (HICON)LoadImageA(GetModuleHandle(NULL), MAKEINTRESOURCE(1),
                                         IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (hIconBig)   SendMessage(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hIconBig);
    if (hIconSmall) SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
  }

  /* ------------------------------- GLAD Loader ------------------------------
   */
  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    FAIL("gladLoadGLLoader failed");

  /* ------------------------------- ICON Loader ------------------------------
   */
  S->warn_ready = 0;
  S->ok_ready = 0;
  S->danger_ready = 0;
  S->github_ready = 0;
  S->win_ready = 0;

  if (load_texture_rgba("resources\\icons\\warn.png", &S->tex_warn)) {
    S->icon_warn = nk_image_id((int)S->tex_warn);
    S->warn_ready = 1;
  }

  if (load_texture_rgba("resources\\icons\\ok.png", &S->tex_ok)) {
    S->icon_ok = nk_image_id((int)S->tex_ok);
    S->ok_ready = 1;
  }
  if (load_texture_rgba("resources\\icons\\danger.png", &S->tex_ok)) {
    S->icon_danger = nk_image_id((int)S->tex_danger);
    S->danger_ready = 1;
  }

  if (load_texture_rgba("resources\\icons\\github.png", &S->tex_github)) {
    S->icon_github = nk_image_id((int)S->tex_github);
    S->github_ready = 1;
  }

  if (load_texture_rgba("resources\\icons\\windows.png", &S->tex_win)) {
    S->icon_win = nk_image_id((int)S->tex_win);
    S->win_ready = 1;
  }

  /* ------------------------------ Nuklear Setup -----------------------------
   */
  S->ctx = nk_glfw3_init(&S->nkglfw, S->window, NK_GLFW3_INSTALL_CALLBACKS);

  /* --------------------------------- Fonts ---------------------------------
   */
  struct nk_font_atlas *atlas = NULL;
  nk_glfw3_font_stash_begin(&S->nkglfw, &atlas);

  // 1) Main font (global default)
  S->font_main = nk_font_atlas_add_default(atlas, 16.0f, NULL);

  // 2) Cyrillic-capable font (used selectively, e.g. drivers list)
  static const nk_rune ranges_cyr[] = {0x0020, 0x00FF, // Latin + basic symbols
                                       0x0400, 0x052F, // Cyrillic
                                       0};

  struct nk_font_config cfg = nk_font_config(0);
  cfg.range = ranges_cyr;

  S->font_cyr = nk_font_atlas_add_from_file(
      atlas,
      "resources\\fonts\\segoeui.ttf",
      22.0f, &cfg);

  S->font_subheader = nk_font_atlas_add_from_file(
      atlas,
      "resources\\fonts\\segoeui.ttf",
      25.0f, &cfg);

  S->font_header = nk_font_atlas_add_from_file(
      atlas,
      "resources\\fonts\\segoeui.ttf",
      30.0f, &cfg);

  nk_glfw3_font_stash_end(&S->nkglfw);

  // Apply global font (safe even if NULL)
  if (S->font_main)
    nk_style_set_font(S->ctx, &S->font_main->handle);

  // Fallback: keep app stable if Cyrillic font failed to load
  if (!S->font_cyr)
    S->font_cyr = S->font_main;
  if (!S->font_subheader)
    S->font_subheader = S->font_cyr;
  if (!S->font_header)
    S->font_header = S->font_subheader;

  /* ------------------------------ Global UI Style ---------------------------
   */
  // Button style (global override)
  S->ctx->style.button.hover = nk_style_item_color(nk_rgba(150, 150, 150, 255));
  S->ctx->style.button.active = nk_style_item_color(nk_rgba(210, 210, 210, 255));
  S->ctx->style.button.text_normal = nk_rgb(188, 188, 188);
  S->ctx->style.button.text_hover = nk_rgb(50, 50, 50);
  S->ctx->style.button.text_active = nk_rgb(50, 50, 50);

  /* ------------------------------- App Defaults -----------------------------
   */
  InitializeCriticalSection(&S->scan_cs);
  S->scan_thread = NULL;
  S->scan_stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
  S->scan_state = SCAN_IDLE;
  S->scan_progress = 0;
  S->drv_ready = 0;
  S->drv_win_open = 0;
  S->fp_win_open = 0;
  S->fp_text[0] = '\0';

  S->updater.state = UPDATE_IDLE;
  S->updater.thread = NULL;
  S->updater.latest_ver[0] = '\0';
  S->updater_asked = 0;

  bench_init(&S->bench);

  /* -------------------------------- Backend --------------------------------
   */
  if (!backend_start(&S->backend))
    FAIL("backend_start failed");

  update_check_start(&S->updater);

  return 1;
}

void app_frame(struct AppState *S) {
  /* Auto-prompt for update once, regardless of active tab */
  if (!S->updater_asked &&
      (int)S->updater.state == UPDATE_AVAILABLE) {
    S->updater_asked = 1;
    char msg[128];
    snprintf(msg, sizeof(msg), "Version %s is available. Update now?",
             S->updater.latest_ver);
    if (MessageBoxA(NULL, msg, "Update Available",
                    MB_YESNO | MB_ICONINFORMATION) == IDYES)
      update_launch_updater(&S->updater);
  }

  glfwPollEvents();
  nk_glfw3_new_frame(&S->nkglfw);

  /* получить актуальный снимок для UI (O(1) копия структуры) */
  backend_read(&S->backend, &S->snap);

  ui_frame(S);

  int fbw, fbh;
  glfwGetFramebufferSize(S->window, &fbw, &fbh);
  glViewport(0, 0, fbw, fbh);
  glClearColor(0.12f, 0.12f, 0.13f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT);

  nk_glfw3_render(&S->nkglfw, NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER,
                  MAX_ELEMENT_BUFFER);
  glfwSwapBuffers(S->window);
}

void app_shutdown(struct AppState *S) {
  if (S->tex_warn) {
    GLuint t = (GLuint)S->tex_warn;
    glDeleteTextures(1, &t);
    S->tex_warn = 0;
  }
  if (S->tex_ok) {
    GLuint t = (GLuint)S->tex_ok;
    glDeleteTextures(1, &t);
    S->tex_ok = 0;
  }
  if (S->tex_danger) {
    GLuint t = (GLuint)S->tex_danger;
    glDeleteTextures(1, &t);
    S->tex_danger = 0;
  }
  if (S->tex_github) {
    GLuint t = (GLuint)S->tex_github;
    glDeleteTextures(1, &t);
    S->tex_github = 0;
  }
  if (S->tex_win) {
    GLuint t = (GLuint)S->tex_win;
    glDeleteTextures(1, &t);
    S->tex_win = 0;
  }
  if (S->scan_stop_event)
    SetEvent(S->scan_stop_event);
  if (S->scan_thread) {
    WaitForSingleObject(S->scan_thread, INFINITE);
    CloseHandle(S->scan_thread);
    S->scan_thread = NULL;
  }
  if (S->scan_stop_event) {
    CloseHandle(S->scan_stop_event);
    S->scan_stop_event = NULL;
  }
  DeleteCriticalSection(&S->scan_cs);

  update_cleanup(&S->updater);

  bench_shutdown(&S->bench);

  backend_stop(&S->backend);
  nk_glfw3_shutdown(&S->nkglfw);
  if (S->window)
    glfwDestroyWindow(S->window);
  glfwTerminate();
}
