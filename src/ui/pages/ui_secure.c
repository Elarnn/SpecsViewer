// ui_secure.c — Secure page: hardware security mechanisms, rating, fingerprint
#include "app.h"
#include "ui.h"
#include "ui/nk_common.h"

#include "../../core/backend.h"
#include "../../core/security/secure.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>

#ifndef SCAN_IDLE
#define SCAN_IDLE 0
#define SCAN_RUNNING 1
#define SCAN_DONE 2
#define SCAN_ERROR 3
#endif

/* -------------------- helpers -------------------- */

static void scan_cleanup_if_finished(struct AppState *A) {
  if (!A->scan_thread)
    return;

  DWORD w = WaitForSingleObject(A->scan_thread, 0);
  if (w == WAIT_OBJECT_0) {
    CloseHandle(A->scan_thread);
    A->scan_thread = NULL;
  }
}

// ---- helpers for 2x2 layout (two vertical columns, each has Name|Value) ----

static void kv_row_2col(struct nk_context *ctx, const char *name,
                        const char *val) {
  nk_layout_row_begin(ctx, NK_STATIC, 18, 2);
  nk_layout_row_push(ctx, 170); // name width
  nk_label(ctx, name, NK_TEXT_LEFT);
  nk_layout_row_push(ctx, 60); // value width
  nk_label(ctx, val, NK_TEXT_RIGHT);
  nk_layout_row_end(ctx);
}

static const char *state_str(int v) {
  if (v == 1)
    return "ON";
  if (v == 0)
    return "OFF";
  return "UNKNOWN";
}

static const char *tpm_type_str(int tpm20, int ftpm, int ptt) {
  if (tpm20 == 0)
    return "OFF";
  if (tpm20 < 0)
    return "UNKNOWN";

  if (ptt == 1)
    return "PTT";
  if (ftpm == 1)
    return "fTPM";

  if (ptt == 0 && ftpm == 0)
    return "dTPM";

  return "UNKNOWN";
}

static void progress_colored(struct nk_context *ctx, nk_size *value,
                             nk_size max, struct nk_color col) {
  struct nk_style_item old_norm = ctx->style.progress.cursor_normal;
  struct nk_style_item old_hover = ctx->style.progress.cursor_hover;
  struct nk_style_item old_act = ctx->style.progress.cursor_active;

  ctx->style.progress.cursor_normal = nk_style_item_color(col);
  ctx->style.progress.cursor_hover = nk_style_item_color(col);
  ctx->style.progress.cursor_active = nk_style_item_color(col);

  nk_progress(ctx, value, max, NK_FIXED);

  ctx->style.progress.cursor_normal = old_norm;
  ctx->style.progress.cursor_hover = old_hover;
  ctx->style.progress.cursor_active = old_act;
}

/* Live hardware snapshot + security results of the last scan (for the
   fingerprint, which covers both). */
static void build_fingerprint_snapshot(struct AppState *A, Snapshot *out) {
  *out = A->snap;
  EnterCriticalSection(&A->scan_cs);
  out->data.sec = A->sec_snap.data.sec;
  LeaveCriticalSection(&A->scan_cs);
}

/* -------------------- scan worker (3rd thread) -------------------- */

static DWORD WINAPI scan_thread_proc(LPVOID param) {
  struct AppState *A = (struct AppState *)param;

  InterlockedExchange(&A->scan_progress, 0);

  // local snapshot (do the work without holding locks)
  Snapshot tmp;
  memset(&tmp, 0, sizeof(tmp));

  if (WaitForSingleObject(A->scan_stop_event, 0) == WAIT_OBJECT_0) {
    InterlockedExchange(&A->scan_state, SCAN_IDLE);
    return 0;
  }

  // hardware security mechanisms (firmware, TPM, VBS/HVCI, CPU features)
  {
    HwSecInfo h;
    memset(&h, 0, sizeof(h));

    if (hwsec_query_win(&h)) {
      tmp.data.sec.uefi_mode = h.uefi_mode;
      tmp.data.sec.secure_boot = h.secure_boot;
      tmp.data.sec.tpm20 = h.tpm20;
      tmp.data.sec.bitlocker = h.bitlocker;
      tmp.data.sec.vbs = h.vbs;
      tmp.data.sec.hvci = h.hvci;
      tmp.data.sec.ftpm = h.ftpm;
      tmp.data.sec.ptt = h.ptt;
      tmp.data.sec.nx = h.nx;
      tmp.data.sec.smep = h.smep;
      tmp.data.sec.cet = h.cet;
    } else {
      tmp.data.sec.uefi_mode = -1;
      tmp.data.sec.secure_boot = -1;
      tmp.data.sec.tpm20 = -1;
      tmp.data.sec.bitlocker = -1;
      tmp.data.sec.vbs = -1;
      tmp.data.sec.hvci = -1;
      tmp.data.sec.ftpm = -1;
      tmp.data.sec.ptt = -1;
      tmp.data.sec.nx = -1;
      tmp.data.sec.smep = -1;
      tmp.data.sec.cet = -1;
    }
  }
  InterlockedExchange(&A->scan_progress, 900);

  if (WaitForSingleObject(A->scan_stop_event, 0) == WAIT_OBJECT_0) {
    InterlockedExchange(&A->scan_state, SCAN_IDLE);
    return 0;
  }

  // publish results under lock
  EnterCriticalSection(&A->scan_cs);
  A->sec_snap = tmp;
  A->sec_ready = 1;
  LeaveCriticalSection(&A->scan_cs);

  InterlockedExchange(&A->scan_progress, 1000);
  InterlockedExchange(&A->scan_state, SCAN_DONE);
  return 0;
}

static void scan_start_async(struct AppState *A) {
  // if already running -> ignore
  LONG st = A->scan_state;
  if (st == SCAN_RUNNING)
    return;

  // close finished thread handle if any
  scan_cleanup_if_finished(A);

  // reset state
  ResetEvent(A->scan_stop_event);
  InterlockedExchange(&A->scan_state, SCAN_RUNNING);
  InterlockedExchange(&A->scan_progress, 0);

  // clear previous results so the UI is clean while scanning
  EnterCriticalSection(&A->scan_cs);
  memset(&A->sec_snap, 0, sizeof(A->sec_snap));
  A->sec_ready = 0;
  LeaveCriticalSection(&A->scan_cs);

  // create worker thread
  DWORD tid = 0;
  A->scan_thread = CreateThread(NULL, 0, scan_thread_proc, A, 0, &tid);
  if (!A->scan_thread) {
    InterlockedExchange(&A->scan_state, SCAN_ERROR);
    return;
  }

  SetThreadPriority(A->scan_thread, THREAD_PRIORITY_BELOW_NORMAL);
}

/* -------------------- UI drawing -------------------- */

void ui_page_secure(struct nk_context *ctx, const struct AppState *S) {
  struct AppState *A = (struct AppState *)S;

  // cleanup finished scan thread handle (non-blocking)
  scan_cleanup_if_finished(A);

  LONG st = A->scan_state;

  if (!A->sec_ready && st != SCAN_RUNNING) {
    struct nk_rect content = nk_window_get_content_region(ctx);
    float button_w = 360.0f;
    const float button_h = 80.0f;
    if (button_w > content.w)
      button_w = content.w;

    float side_w = (content.w - button_w) * 0.5f;
    float top_h = (content.h - button_h) * 0.5f;

    if (side_w < 0.0f)
      side_w = 0.0f;
    if (top_h < 0.0f)
      top_h = 0.0f;

    nk_layout_space_begin(ctx, NK_STATIC, content.h, 1);
    nk_layout_space_push(ctx, nk_rect(side_w, top_h, button_w, button_h));
    if (nk_button_label(ctx, "Scan hardware security"))
      scan_start_async(A);
    nk_layout_space_end(ctx);
    return;
  }

  nk_layout_row_dynamic(ctx, 26, 1);

  // Top area
  if (st == SCAN_RUNNING) {
    LONG p = A->scan_progress;
    if (p < 0)
      p = 0;
    if (p > 1000)
      p = 1000;

    char buf[64];
    snprintf(buf, sizeof(buf), "Scanning... %ld%%", p / 10);
    nk_label(ctx, buf, NK_TEXT_LEFT);

    nk_layout_row_dynamic(ctx, 18, 1);
    nk_size prog = (nk_size)p;
    nk_progress(ctx, &prog, 1000, NK_FIXED);
  } else {
    if (nk_button_label(ctx, "Rescan hardware security"))
      scan_start_async(A);
  }

  // Group: Hardware secure (2 vertical columns, fixed height)
  nk_layout_row_dynamic(ctx, 180, 1);
  if (nk_group_begin(ctx, "Hardware secure",
                     NK_WINDOW_BORDER | NK_WINDOW_TITLE |
                         NK_WINDOW_NO_SCROLLBAR)) {
    if (st == SCAN_RUNNING) {
      nk_layout_row_dynamic(ctx, 18, 1);
      nk_label(ctx, "Scanning hardware security...", NK_TEXT_LEFT);
    } else if (!A->sec_ready) {
      nk_layout_row_dynamic(ctx, 18, 1);
      nk_label(ctx, "Run scan to get hardware security status.", NK_TEXT_LEFT);
    } else {
      SecurityInfo sec;

      EnterCriticalSection(&A->scan_cs);
      sec = A->sec_snap.data.sec;
      LeaveCriticalSection(&A->scan_cs);

      nk_layout_row_begin(ctx, NK_DYNAMIC, 140, 2);

      nk_layout_row_push(ctx, 0.5f);
      if (nk_group_begin(ctx, "hw_left", NK_WINDOW_NO_SCROLLBAR)) {
        kv_row_2col(ctx, "UEFI mode", state_str(sec.uefi_mode));
        kv_row_2col(ctx, "Secure Boot", state_str(sec.secure_boot));
        kv_row_2col(ctx, "NX (DEP)", state_str(sec.nx));
        kv_row_2col(ctx, "VBS", state_str(sec.vbs));
        kv_row_2col(ctx, "HVCI", state_str(sec.hvci));
        nk_group_end(ctx);
      }

      nk_layout_row_push(ctx, 0.5f);
      if (nk_group_begin(ctx, "hw_right", NK_WINDOW_NO_SCROLLBAR)) {
        kv_row_2col(ctx, "TPM 2.0", state_str(sec.tpm20));
        kv_row_2col(ctx, "TPM type", tpm_type_str(sec.tpm20, sec.ftpm, sec.ptt));
        kv_row_2col(ctx, "CET", state_str(sec.cet));
        kv_row_2col(ctx, "BitLocker", state_str(sec.bitlocker));
        kv_row_2col(ctx, "SMEP (CPU)", state_str(sec.smep));
        nk_group_end(ctx);
      }

      nk_layout_row_end(ctx);
    }

    nk_group_end(ctx);
  }

  // Group: Rating
  nk_layout_row_dynamic(ctx, 60, 1);
  if (nk_group_begin(ctx, "Rating", NK_WINDOW_NO_SCROLLBAR)) {

    nk_layout_row_dynamic(ctx, 18, 1);

    if (st == SCAN_RUNNING) {
      nk_label(ctx, "Rating will be available after scan.", NK_TEXT_LEFT);
    } else if (!A->sec_ready) {
      nk_label(ctx, "Run a scan to calculate the rating and enable hardware fingerprint creation and comparison.", NK_TEXT_LEFT);
    } else {
      Snapshot snap;

      EnterCriticalSection(&A->scan_cs);
      snap = A->sec_snap;
      LeaveCriticalSection(&A->scan_cs);

      SecRating rr = sec_rating_compute(&snap);

      struct nk_color bar = nk_rgb(50, 200, 80);
      if (rr.level == SEC_RATE_YELLOW)
        bar = nk_rgb(220, 180, 30);
      else if (rr.level == SEC_RATE_RED)
        bar = nk_rgb(220, 50, 50);

      nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 3);

      nk_layout_row_push(ctx, 0.2f);
      nk_label(ctx, rr.label, NK_TEXT_LEFT);

      nk_layout_row_push(ctx, 0.10f);
      {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", rr.score);
        nk_label(ctx, buf, NK_TEXT_CENTERED);
      }

      nk_layout_row_push(ctx, 0.7f);
      {
        int s = rr.score;
        if (s < 0)
          s = 0;
        if (s > 100)
          s = 100;
        nk_size p = (nk_size)s * 10; // 0..1000
        progress_colored(ctx, &p, 1000, bar);
      }

      nk_layout_row_end(ctx);
    }

    nk_group_end(ctx);
  }

  // Bottom buttons (under all groups): show only after scan done
  if (A->sec_ready && st == SCAN_DONE) {
    nk_layout_row_begin(ctx, NK_DYNAMIC, 26, 2);

    nk_layout_row_push(ctx, 0.5f);
    if (nk_button_label(ctx, "Generate fingerprint")) {
      Snapshot fp_snap;
      build_fingerprint_snapshot(A, &fp_snap);
      int fp_rc = hardware_fingerprint_build(&fp_snap, A->fp_text, sizeof(A->fp_text));
      (void)fp_rc;
      A->fp_win_open = 1;
    }

    nk_layout_row_push(ctx, 0.5f);
    if (nk_button_label(ctx, "Compare fingerprints")) {
      Snapshot fp_snap;
      build_fingerprint_snapshot(A, &fp_snap);
      int cmp_rc = hardware_fingerprint_compare(&fp_snap, A->fp_text, sizeof(A->fp_text));
      (void)cmp_rc;
      A->fp_win_open = 1;
    }

    nk_layout_row_end(ctx);
  }
}
