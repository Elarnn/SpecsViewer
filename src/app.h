// app.h
#pragma once
#include "core/backend.h"
#include "ui.h"
#include "bench/bench.h"
#include "ui/nk_common.h"
#include <stdint.h>
#include <windows.h>
#include "update/updater_check.h"


#define SCAN_IDLE 0
#define SCAN_RUNNING 1
#define SCAN_DONE 2
#define SCAN_ERROR 3

struct AppState {
  enum AppTab active;
  struct nk_glfw nkglfw;
  struct nk_context *ctx;
  GLFWwindow *window;

  Backend backend;
  Snapshot snap;

  BenchState bench;

  // ====== Результаты Security Scan (что рисует UI) ======
  Snapshot drv_snap;
  int drv_ready; // 1 когда ВСЕ результаты скана готовы
  int drv_win_open;
  int fp_win_open;
  int usermode_warn_open;
  char fp_text[65536];

  uint32_t drv_risk_flags[DRV_MAX];
  int risk_count_loldrivers;
  int risk_count_msblock;
  int risk_count_unsigned;

  // ====== 3-й поток: Security Scan worker ======
  HANDLE scan_thread;
  HANDLE scan_stop_event;      // на будущее для отмены
  volatile LONG scan_state;    // SCAN_*
  volatile LONG scan_progress; // 0..1000 (проценты*10)
  CRITICAL_SECTION scan_cs;    // защита drv_snap + risk_* + hw + score

  struct nk_font *font_main;
  struct nk_font *font_cyr;
  struct nk_font *font_subheader;
  struct nk_font *font_header;

  unsigned int tex_warn;
  unsigned int tex_ok;
  unsigned int tex_danger;
  struct nk_image icon_warn;
  struct nk_image icon_ok;
  struct nk_image icon_danger;
  int warn_ready;
  int ok_ready;
  int danger_ready;

  unsigned int tex_github;
  unsigned int tex_win;
  struct nk_image icon_github;
  struct nk_image icon_win;
  int github_ready;
  int win_ready;

  // ====== Updater ======
  UpdateState updater;
  int updater_asked;
};

int app_init(struct AppState *S, int w, int h, const char *title);
void app_frame(struct AppState *S);
void app_shutdown(struct AppState *S);
