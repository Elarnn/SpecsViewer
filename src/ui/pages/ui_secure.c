// ui_secure.c
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

static MsBlocklistDb g_ms_db;
static int g_ms_ready = 0;

/* These are used by ui_drivers_list.c too */
uint32_t g_drv_risk_flags[DRV_MAX] = {0};
uint32_t g_drv_risk_filter = DRV_RISK_LOLDRIVERS | DRV_RISK_MS_BLOCKLIST;

int g_risk_count_loldrivers = 0;
int g_risk_count_msblock = 0;
int g_risk_count_unsigned = 0;

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

int driver_risk_count_filtered(const Snapshot *snap, uint32_t mask) {
  if (!snap || mask == 0)
    return 0;

  int n = snap->data.drivers_count;
  if (n > DRV_MAX)
    n = DRV_MAX;

  int cnt = 0;
  for (int i = 0; i < n; i++)
    if (g_drv_risk_flags[i] & mask)
      cnt++;

  return cnt;
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

static void risk_build_flags(const Snapshot *snap) {
  memset(g_drv_risk_flags, 0, sizeof(g_drv_risk_flags));

  g_risk_count_loldrivers = 0;
  g_risk_count_msblock = 0;
  g_risk_count_unsigned = 0;

  if (!snap)
    return;

  int n = snap->data.drivers_count;
  if (n > DRV_MAX)
    n = DRV_MAX;

  for (int i = 0; i < n; i++) {
    const DriverInfo *d = &snap->data.drivers[i];
    if (!d->sha256[0])
      continue;

    if (ui_loldrivers_is_hit_sha256(d->sha256)) {
      g_drv_risk_flags[i] |= DRV_RISK_LOLDRIVERS;
      g_risk_count_loldrivers++;
    }

    if (g_ms_ready && ms_blocklist_is_denied_sha256(&g_ms_db, d->sha256)) {
      g_drv_risk_flags[i] |= DRV_RISK_MS_BLOCKLIST;
      g_risk_count_msblock++;
    }
  }

  // unsigned drivers
  unsigned_build_flags(snap, g_drv_risk_flags, &g_risk_count_unsigned);
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

/* -------------------- scan worker (3rd thread) -------------------- */

static DWORD WINAPI scan_thread_proc(LPVOID param) {
  struct AppState *A = (struct AppState *)param;

  InterlockedExchange(&A->scan_progress, 0);

  // local snapshot (do heavy work without locks)
  Snapshot tmp;
  memset(&tmp, 0, sizeof(tmp));

  if (WaitForSingleObject(A->scan_stop_event, 0) == WAIT_OBJECT_0) {
    InterlockedExchange(&A->scan_state, SCAN_IDLE);
    return 0;
  }

  // stage 1: collect drivers
  drivers_collect_win(&tmp);
  InterlockedExchange(&A->scan_progress, 550);

  if (WaitForSingleObject(A->scan_stop_event, 0) == WAIT_OBJECT_0) {
    InterlockedExchange(&A->scan_state, SCAN_IDLE);
    return 0;
  }

  // stage 2: hardware secure checks (writes into Snapshot tmp fields)
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
  InterlockedExchange(&A->scan_progress, 720);

  if (WaitForSingleObject(A->scan_stop_event, 0) == WAIT_OBJECT_0) {
    InterlockedExchange(&A->scan_state, SCAN_IDLE);
    return 0;
  }

  // stage 3: init DBs (once)
  if (!g_ms_ready) {
    g_ms_ready = ms_blocklist_init_from_xml(
        &g_ms_db, "resources\\db\\SiPolicy_Enforced.xml");
    if (!g_ms_ready)
      printf("[MSBlocklist] DB init failed\n");
  }

  ui_loldrivers_init_once();
  InterlockedExchange(&A->scan_progress, 900);

  if (WaitForSingleObject(A->scan_stop_event, 0) == WAIT_OBJECT_0) {
    InterlockedExchange(&A->scan_state, SCAN_IDLE);
    return 0;
  }

  // stage 4: publish results + build flags under lock
  EnterCriticalSection(&A->scan_cs);

  A->drv_snap = tmp; // <-- один Snapshot содержит и drivers, и hw secure
  A->drv_ready = 1;

  risk_build_flags(&A->drv_snap);

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

  // clear previous results (optional, but UI becomes clean while scanning)
  EnterCriticalSection(&A->scan_cs);
  memset(&A->drv_snap, 0, sizeof(A->drv_snap));
  A->drv_ready = 0;

  memset(g_drv_risk_flags, 0, sizeof(g_drv_risk_flags));
  g_risk_count_loldrivers = 0;
  g_risk_count_msblock = 0;
  g_risk_count_unsigned = 0;
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

typedef enum RiskSeverity {
  RISK_OK = 0,
  RISK_WARNING = 1,
  RISK_DANGER = 2
} RiskSeverity;

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

static void draw_reason_row_ex(struct nk_context *ctx, const struct AppState *A,
                               const char *label, int count,
                               RiskSeverity sev_if_hit) {
  nk_layout_row_begin(ctx, NK_STATIC, 18, 3);

  /* icon */
  nk_layout_row_push(ctx, 20);

  RiskSeverity sev = (count > 0) ? sev_if_hit : RISK_OK;

  if (sev == RISK_OK) {
    if (A->ok_ready)
      nk_image(ctx, A->icon_ok);
    else {
      struct nk_color old = ctx->style.text.color;
      ctx->style.text.color = nk_rgb(50, 200, 80);
      nk_label(ctx, "✓", NK_TEXT_CENTERED);
      ctx->style.text.color = old;
    }
  } else if (sev == RISK_DANGER) {
    if (A->danger_ready)
      nk_image(ctx, A->icon_danger);
    else {
      struct nk_color old = ctx->style.text.color;
      ctx->style.text.color = nk_rgb(220, 50, 50);
      nk_label(ctx, "!", NK_TEXT_CENTERED);
      ctx->style.text.color = old;
    }
  } else /* RISK_WARNING */
  {
    if (A->warn_ready)
      nk_image(ctx, A->icon_warn);
    else {
      struct nk_color old = ctx->style.text.color;
      ctx->style.text.color = nk_rgb(220, 180, 30);
      nk_label(ctx, "!", NK_TEXT_CENTERED);
      ctx->style.text.color = old;
    }
  }

  /* label */
  nk_layout_row_push(ctx, 140);
  nk_label(ctx, label, NK_TEXT_LEFT);

  /* count */
  nk_layout_row_push(ctx, 40);
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", count);
    nk_label(ctx, buf, NK_TEXT_RIGHT);
  }

  nk_layout_row_end(ctx);
}

void ui_page_secure(struct nk_context *ctx, const struct AppState *S) {
  struct AppState *A = (struct AppState *)S;

  // cleanup finished scan thread handle (non-blocking)
  scan_cleanup_if_finished(A);

  LONG st = A->scan_state;

  if (!A->drv_ready && st != SCAN_RUNNING) {
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
    if (nk_button_label(ctx, "Scan system vulnerability"))
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
    // after done: вместо Open drivers list -> Rescan drivers
    if (nk_button_label(ctx, "Rescan drivers"))
      scan_start_async(A);
  }

  // Group: Vulnerable drivers
  nk_layout_row_dynamic(ctx, 160, 1);
  if (nk_group_begin(ctx, "Vulnerable drivers",
                     NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
    nk_layout_row_dynamic(ctx, 18, 1);

    if (st == SCAN_RUNNING) {
      nk_label(ctx, "Scanning drivers... please wait.", NK_TEXT_LEFT);
    } else if (!A->drv_ready) {
      nk_label(ctx, "Press scan button to check vulnerability.", NK_TEXT_LEFT);
    } else {
      // copy values under lock (short), then draw without holding lock
      int filtered = 0;
      int c_lold = 0, c_ms = 0, c_uns = 0;

      EnterCriticalSection(&A->scan_cs);
      filtered = driver_risk_count_filtered(&A->drv_snap, g_drv_risk_filter);
      c_lold = g_risk_count_loldrivers;
      c_ms = g_risk_count_msblock;
      c_uns = g_risk_count_unsigned;
      LeaveCriticalSection(&A->scan_cs);

      char hdr[96];
      snprintf(hdr, sizeof(hdr), "Dangerous drivers: %d", filtered);
      nk_label(ctx, hdr, NK_TEXT_LEFT);

      nk_layout_row_dynamic(ctx, 6, 1);
      nk_spacing(ctx, 1);

      draw_reason_row_ex(ctx, A, "LOLDrivers -", c_lold, RISK_DANGER);
      draw_reason_row_ex(ctx, A, "Microsoft DB -", c_ms, RISK_DANGER);
      draw_reason_row_ex(ctx, A, "Unsigned -", c_uns, RISK_WARNING);
    }

    nk_group_end(ctx);
  }

  // Group: Hardware secure (2 vertical columns, fixed height)
  nk_layout_row_dynamic(ctx, 180, 1);
  if (nk_group_begin(ctx, "Hardware secure",
                     NK_WINDOW_BORDER | NK_WINDOW_TITLE |
                         NK_WINDOW_NO_SCROLLBAR)) {
    if (st == SCAN_RUNNING) {
      nk_layout_row_dynamic(ctx, 18, 1);
      nk_label(ctx, "Scanning hardware security...", NK_TEXT_LEFT);
    } else if (!A->drv_ready) {
      nk_layout_row_dynamic(ctx, 18, 1);
      nk_label(ctx, "Run scan to get hardware security status.", NK_TEXT_LEFT);
    } else {
      int uefi = -1, sb = -1, tpm = -1, bl = -1, vbs = -1, hvci = -1;
      int ftpm = -1, ptt = -1, nx = -1, smep = -1, cet = -1;

      EnterCriticalSection(&A->scan_cs);
      uefi = A->drv_snap.data.sec.uefi_mode;
      sb = A->drv_snap.data.sec.secure_boot;
      tpm = A->drv_snap.data.sec.tpm20;
      bl = A->drv_snap.data.sec.bitlocker;
      vbs = A->drv_snap.data.sec.vbs;
      hvci = A->drv_snap.data.sec.hvci;
      ftpm = A->drv_snap.data.sec.ftpm;
      ptt = A->drv_snap.data.sec.ptt;
      nx = A->drv_snap.data.sec.nx;
      smep = A->drv_snap.data.sec.smep;
      cet = A->drv_snap.data.sec.cet;
      LeaveCriticalSection(&A->scan_cs);

      nk_layout_row_begin(ctx, NK_DYNAMIC, 140, 2);

      nk_layout_row_push(ctx, 0.5f);
      if (nk_group_begin(ctx, "hw_left", NK_WINDOW_NO_SCROLLBAR)) {
        kv_row_2col(ctx, "UEFI mode", state_str(uefi));
        kv_row_2col(ctx, "Secure Boot", state_str(sb));
        kv_row_2col(ctx, "NX (DEP)", state_str(nx));
        kv_row_2col(ctx, "VBS", state_str(vbs));
        kv_row_2col(ctx, "HVCI", state_str(hvci));
        nk_group_end(ctx);
      }

      nk_layout_row_push(ctx, 0.5f);
      if (nk_group_begin(ctx, "hw_right", NK_WINDOW_NO_SCROLLBAR)) {
        kv_row_2col(ctx, "TPM 2.0", state_str(tpm));
        kv_row_2col(ctx, "TPM type", tpm_type_str(tpm, ftpm, ptt));
        kv_row_2col(ctx, "CET", state_str(cet));
        kv_row_2col(ctx, "BitLocker", state_str(bl));
        kv_row_2col(ctx, "SMEP (CPU)", state_str(smep));
        nk_group_end(ctx);
      }

      nk_layout_row_end(ctx);
    }

    nk_group_end(ctx);
  }

  // Group: Rating
  nk_layout_row_dynamic(ctx, 60, 1);
  if (nk_group_begin(ctx, "Rating",
                      NK_WINDOW_NO_SCROLLBAR)) {

    nk_layout_row_dynamic(ctx, 18, 1);

    if (st == SCAN_RUNNING) {
      nk_label(ctx, "Rating will be available after scan.", NK_TEXT_LEFT);
    } else if (!A->drv_ready) {
      nk_label(ctx, "Run a scan to calculate the rating and enable hardware fingerprint creation and comparison.", NK_TEXT_LEFT);
    } else {
      Snapshot snap;
      int c_lold = 0, c_ms = 0, c_uns = 0;
      int ms_ready = 0;

      EnterCriticalSection(&A->scan_cs);
      snap = A->drv_snap;
      c_lold = g_risk_count_loldrivers;
      c_ms = g_risk_count_msblock;
      c_uns = g_risk_count_unsigned;
      ms_ready = g_ms_ready;
      LeaveCriticalSection(&A->scan_cs);

      SecRating rr = sec_rating_compute(&snap, c_lold, c_ms, c_uns, ms_ready);

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
  if (A->drv_ready && st == SCAN_DONE) {
    nk_layout_row_begin(ctx, NK_DYNAMIC, 26, 3);

    nk_layout_row_push(ctx, 0.33f);
    if (nk_button_label(ctx, "Open drivers list")) {
      A->drv_win_open = 1;
    }

    nk_layout_row_push(ctx, 0.34f);
    if (nk_button_label(ctx, "Generate fingerprint")) {
      Snapshot fp_snap;
      memset(&fp_snap, 0, sizeof(fp_snap));

      fp_snap = A->snap;

      EnterCriticalSection(&A->scan_cs);
      fp_snap.data.drivers_count = A->drv_snap.data.drivers_count;
      memcpy(fp_snap.data.drivers, A->drv_snap.data.drivers, sizeof(A->drv_snap.data.drivers));
      fp_snap.data.sec.uefi_mode = A->drv_snap.data.sec.uefi_mode;
      fp_snap.data.sec.secure_boot = A->drv_snap.data.sec.secure_boot;
      fp_snap.data.sec.tpm20 = A->drv_snap.data.sec.tpm20;
      fp_snap.data.sec.bitlocker = A->drv_snap.data.sec.bitlocker;
      fp_snap.data.sec.vbs = A->drv_snap.data.sec.vbs;
      fp_snap.data.sec.hvci = A->drv_snap.data.sec.hvci;
      fp_snap.data.sec.cred_guard = A->drv_snap.data.sec.cred_guard;
      fp_snap.data.sec.ftpm = A->drv_snap.data.sec.ftpm;
      fp_snap.data.sec.ptt = A->drv_snap.data.sec.ptt;
      fp_snap.data.sec.nx = A->drv_snap.data.sec.nx;
      fp_snap.data.sec.smep = A->drv_snap.data.sec.smep;
      fp_snap.data.sec.cet = A->drv_snap.data.sec.cet;
      LeaveCriticalSection(&A->scan_cs);

      int fp_rc = hardware_fingerprint_build(&fp_snap, A->fp_text, sizeof(A->fp_text));
      (void)fp_rc;
      A->fp_win_open = 1;
    }

    nk_layout_row_push(ctx, 0.33f);
    if (nk_button_label(ctx, "Compare fingerprints")) {
      Snapshot fp_snap;
      memset(&fp_snap, 0, sizeof(fp_snap));

      fp_snap = A->snap;

      EnterCriticalSection(&A->scan_cs);
      fp_snap.data.drivers_count = A->drv_snap.data.drivers_count;
      memcpy(fp_snap.data.drivers, A->drv_snap.data.drivers, sizeof(A->drv_snap.data.drivers));
      fp_snap.data.sec.uefi_mode = A->drv_snap.data.sec.uefi_mode;
      fp_snap.data.sec.secure_boot = A->drv_snap.data.sec.secure_boot;
      fp_snap.data.sec.tpm20 = A->drv_snap.data.sec.tpm20;
      fp_snap.data.sec.bitlocker = A->drv_snap.data.sec.bitlocker;
      fp_snap.data.sec.vbs = A->drv_snap.data.sec.vbs;
      fp_snap.data.sec.hvci = A->drv_snap.data.sec.hvci;
      fp_snap.data.sec.cred_guard = A->drv_snap.data.sec.cred_guard;
      fp_snap.data.sec.ftpm = A->drv_snap.data.sec.ftpm;
      fp_snap.data.sec.ptt = A->drv_snap.data.sec.ptt;
      fp_snap.data.sec.nx = A->drv_snap.data.sec.nx;
      fp_snap.data.sec.smep = A->drv_snap.data.sec.smep;
      fp_snap.data.sec.cet = A->drv_snap.data.sec.cet;
      LeaveCriticalSection(&A->scan_cs);

      int cmp_rc = hardware_fingerprint_compare(&fp_snap, A->fp_text, sizeof(A->fp_text));
      (void)cmp_rc;
      A->fp_win_open = 1;
    }

    nk_layout_row_end(ctx);
  }
}
