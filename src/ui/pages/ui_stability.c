#include "app.h"
#include "bench/stability.h"
#include "config.h"
#include "ui.h"
#include "ui/nk_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* "CPU stability test" group on the Bench page: settings, live status,
   per-thread error map and the event log. The group has no scrollbar of its
   own (its height follows the content), so the mouse wheel always scrolls
   the page. */

static const char  *k_dur_names[] = {
  "5 min", "15 min", "30 min", "1 hour", "3 hours", "Custom", "Unlimited"
};
static const double k_dur_sec[] = { 300.0, 900.0, 1800.0, 3600.0, 10800.0, -1.0, 0.0 };
#define DUR_COUNT  ((int)(sizeof(k_dur_sec) / sizeof(k_dur_sec[0])))
#define DUR_CUSTOM 5

#define COL_OK   nk_rgb(80, 200, 120)
#define COL_BAD  nk_rgb(235, 80, 80)
#define COL_WARN nk_rgb(255, 170, 40)

#define ROW_H    18.0f
#define LOG_H    15.0f

static StabUiState g_ui;     /* large (per-CPU arrays), keep it off the stack */

/* sensor extremes during the current run */
static int    g_temp_max = -1;
static double g_vid_min = 0.0, g_vid_max = 0.0;
static int    g_was_running = 0;

static void fmt_hms(char *out, size_t n, double sec) {
  unsigned t = sec > 0.0 ? (unsigned)sec : 0u;
  snprintf(out, n, "%02u:%02u:%02u", t / 3600u, (t / 60u) % 60u, t % 60u);
}

static double selected_duration_sec(const struct AppState *S) {
  int d = S->stab_dur_sel;
  if (d < 0 || d >= DUR_COUNT) d = 1;
  if (d == DUR_CUSTOM) return (double)(S->stab_custom_min > 0 ? S->stab_custom_min : 1) * 60.0;
  return k_dur_sec[d];
}

static void status_line(struct nk_context *ctx, const StabUiState *u) {
  char el[16];
  fmt_hms(el, sizeof(el), u->elapsed_sec);

  switch (u->status) {
  case STAB_STATUS_PREPARING:
    nk_label(ctx, "Status: preparing reference results...", NK_TEXT_LEFT);
    break;
  case STAB_STATUS_RUNNING:
    if (u->total_errors > 0)
      nk_label_colored(ctx, "Status: running, ERRORS DETECTED", NK_TEXT_LEFT, COL_BAD);
    else
      nk_labelf(ctx, NK_TEXT_LEFT, "Status: running %s", stab_test_name(u->test));
    break;
  case STAB_STATUS_PASSED:
    if (u->whea_errors > 0)
      nk_labelf_colored(ctx, NK_TEXT_LEFT, COL_WARN,
                        "PASSED %s, but Windows logged WHEA hardware errors", el);
    else
      nk_labelf_colored(ctx, NK_TEXT_LEFT, COL_OK, "PASSED: no errors in %s", el);
    break;
  case STAB_STATUS_STOPPED:
    nk_labelf_colored(ctx, NK_TEXT_LEFT, u->whea_errors > 0 ? COL_WARN : COL_OK,
                      "Stopped after %s: no calculation errors", el);
    break;
  case STAB_STATUS_FAILED:
    nk_labelf_colored(ctx, NK_TEXT_LEFT, COL_BAD,
                      "FAILED: %d calculation error(s) after %s", u->total_errors, el);
    break;
  case STAB_STATUS_ERROR:
    nk_label_colored(ctx, "Could not run the test, see the log", NK_TEXT_LEFT, COL_BAD);
    break;
  default:
    nk_label(ctx, "Status: idle", NK_TEXT_LEFT);
    break;
  }
}

static void section_label(struct nk_context *ctx, const char *text) {
  nk_layout_row_dynamic(ctx, 6, 1);
  nk_spacer(ctx);
  nk_layout_row_dynamic(ctx, ROW_H, 1);
  nk_label(ctx, text, NK_TEXT_LEFT);
}

void ui_stability_group(struct nk_context *ctx, struct AppState *S) {
  StabUiState *u = &g_ui;
  stab_read_ui(&S->stab, u);

  const int bench_busy = (int)S->bench.running;
  const int avx2 = stab_avx2_supported();
  const int custom = (S->stab_dur_sel == DUR_CUSTOM);

  /* sensor extremes for the current run */
  if (u->running && !g_was_running) { g_temp_max = -1; g_vid_min = g_vid_max = 0.0; }
  g_was_running = u->running;
  if (u->status == STAB_STATUS_RUNNING) {
    int t = S->snap.cpu_rt.cpu_temp;
    double v = S->snap.cpu_rt.voltage;
    if (t > g_temp_max) g_temp_max = t;
    if (v > 0.0) {
      if (g_vid_min <= 0.0 || v < g_vid_min) g_vid_min = v;
      if (v > g_vid_max) g_vid_max = v;
    }
  }

  /* group height follows the content: no inner scrollbar */
  const float sy = ctx->style.window.spacing.y;
  const int thread_rows = (u->ncpu + 3) / 4;
  /* the log is the last thing on the page, so it may grow without shifting
     anything: reserve only the lines it has (at least the hint line) */
  const int log_rows = u->log_n > 0 ? u->log_n : 1;
  const struct nk_user_font *font = ctx->style.font;
  float h = (font ? font->height : 14.0f) + 4.0f * 4.0f        /* title bar      */
          + 2.0f * ctx->style.window.group_padding.y
          + (24 + sy) + (22 + sy) + (ROW_H + sy) + (28 + sy)    /* settings       */
          + (6 + sy) + (20 + sy) + 3.0f * (ROW_H + sy)          /* status         */
          + 2.0f * ((6 + sy) + (ROW_H + sy))                    /* section titles */
          + (float)thread_rows * (ROW_H + sy)                   /* thread map     */
          + (float)log_rows * (LOG_H + sy)                      /* log            */
          + 4.0f;

  nk_layout_row_dynamic(ctx, h, 1);
  if (!nk_group_begin(ctx, "CPU stability test",
                      NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR))
    return;

  /* ── Settings ─────────────────────────────────────────────────────────── */
  if (u->running) nk_widget_disable_begin(ctx);

  nk_layout_row_template_begin(ctx, 24);
  nk_layout_row_template_push_static(ctx, 52);
  nk_layout_row_template_push_static(ctx, 175);
  nk_layout_row_template_push_static(ctx, 14);
  nk_layout_row_template_push_static(ctx, 92);
  nk_layout_row_template_push_static(ctx, 130);
  if (custom) {
    nk_layout_row_template_push_static(ctx, 6);
    nk_layout_row_template_push_static(ctx, 64);
    nk_layout_row_template_push_static(ctx, 40);
  }
  nk_layout_row_template_push_dynamic(ctx);
  nk_layout_row_template_end(ctx);

  {
    const char *tests[STAB_TEST_COUNT];
    for (int i = 0; i < STAB_TEST_COUNT; i++) tests[i] = stab_test_name(i);
    if (!avx2) tests[STAB_TEST_AVX2] = "AVX2 / FMA (not supported)";
    nk_label(ctx, "Test:", NK_TEXT_LEFT);
    int sel = nk_combo(ctx, tests, STAB_TEST_COUNT, S->stab_test_sel, 22, nk_vec2(220, 160));
    if (!u->running && !(sel == STAB_TEST_AVX2 && !avx2)) S->stab_test_sel = sel;
  }
  nk_spacer(ctx);
  nk_label(ctx, "Duration:", NK_TEXT_LEFT);
  {
    int sel = nk_combo(ctx, k_dur_names, DUR_COUNT, S->stab_dur_sel, 22, nk_vec2(150, 210));
    if (!u->running) S->stab_dur_sel = sel;
  }
  if (custom) {
    nk_spacer(ctx);
    if (!S->stab_custom_buf[0])
      snprintf(S->stab_custom_buf, sizeof(S->stab_custom_buf), "%d", S->stab_custom_min);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, S->stab_custom_buf,
                                   (int)sizeof(S->stab_custom_buf), nk_filter_decimal);
    int m = atoi(S->stab_custom_buf);
    if (m > 0) S->stab_custom_min = (m > 99999) ? 99999 : m;
    nk_label(ctx, "min", NK_TEXT_LEFT);
  }
  nk_spacer(ctx);

  nk_layout_row_dynamic(ctx, 22, 1);
  nk_checkbox_label(ctx, "Stop on first error", &S->stab_stop_on_error);

  if (u->running) nk_widget_disable_end(ctx);

  nk_layout_row_dynamic(ctx, ROW_H, 1);
  nk_label(ctx, stab_test_description(S->stab_test_sel), NK_TEXT_LEFT);

  nk_layout_row_template_begin(ctx, 28);
  nk_layout_row_template_push_dynamic(ctx);
  nk_layout_row_template_push_static(ctx, 155);
  nk_layout_row_template_push_dynamic(ctx);
  nk_layout_row_template_end(ctx);
  nk_spacer(ctx);
  if (u->running) {
    if (nk_button_label(ctx, "Stop test")) stab_stop(&S->stab);
  } else if (bench_busy) {
    nk_widget_disable_begin(ctx);
    nk_button_label(ctx, "Benchmark running");
    nk_widget_disable_end(ctx);
  } else if (nk_button_label(ctx, "Start test")) {
    if (stab_start(&S->stab, S->stab_test_sel, selected_duration_sec(S), S->stab_stop_on_error))
      config_save(S);   /* remember the settings of the last run */
  }
  nk_spacer(ctx);

  /* ── Status ───────────────────────────────────────────────────────────── */
  nk_layout_row_dynamic(ctx, 6, 1);
  nk_spacer(ctx);
  nk_layout_row_dynamic(ctx, 20, 1);
  status_line(ctx, u);

  {
    char el[16], du[16];
    fmt_hms(el, sizeof(el), u->elapsed_sec);
    fmt_hms(du, sizeof(du), u->duration_sec);

    nk_layout_row_dynamic(ctx, ROW_H, 2);
    if (u->duration_sec > 0.0 && u->status != STAB_STATUS_IDLE)
      nk_labelf(ctx, NK_TEXT_LEFT, "Elapsed: %s / %s", el, du);
    else
      nk_labelf(ctx, NK_TEXT_LEFT, "Elapsed: %s", el);
    if (u->total_errors > 0)
      nk_labelf_colored(ctx, NK_TEXT_LEFT, COL_BAD, "Calculation errors: %d", u->total_errors);
    else
      nk_labelf(ctx, NK_TEXT_LEFT, "Calculation errors: 0   (%lld checks)", u->total_units);

    int t = S->snap.cpu_rt.cpu_temp;
    if (t > 0 && g_temp_max > 0)
      nk_labelf(ctx, NK_TEXT_LEFT, "CPU temp: %d C   (max %d C)", t, g_temp_max);
    else if (t > 0)
      nk_labelf(ctx, NK_TEXT_LEFT, "CPU temp: %d C", t);
    else
      nk_label(ctx, "CPU temp: n/a", NK_TEXT_LEFT);
    if (u->whea_errors < 0)
      nk_label(ctx, "WHEA errors: n/a", NK_TEXT_LEFT);
    else if (u->whea_errors > 0)
      nk_labelf_colored(ctx, NK_TEXT_LEFT, COL_WARN, "WHEA errors: %d", u->whea_errors);
    else
      nk_label(ctx, "WHEA errors: 0", NK_TEXT_LEFT);

    double v = S->snap.cpu_rt.voltage;
    if (v > 0.0 && g_vid_max > 0.0)
      nk_labelf(ctx, NK_TEXT_LEFT, "Core VID: %.3f V   (%.3f - %.3f)", v, g_vid_min, g_vid_max);
    else if (v > 0.0)
      nk_labelf(ctx, NK_TEXT_LEFT, "Core VID: %.3f V", v);
    else
      nk_label(ctx, "Core VID: n/a", NK_TEXT_LEFT);
    if (S->snap.cpu_rt.cur_mhz > 0)
      nk_labelf(ctx, NK_TEXT_LEFT, "Frequency: %d MHz   Load: %.0f%%",
                S->snap.cpu_rt.cur_mhz, S->snap.cpu_rt.load);
    else
      nk_labelf(ctx, NK_TEXT_LEFT, "Load: %.0f%%", S->snap.cpu_rt.load);
  }

  /* ── Per-thread map ───────────────────────────────────────────────────── */
  section_label(ctx, "Threads:");
  {
    int max_eff = 0;
    for (int i = 0; i < u->ncpu; i++)
      if (u->cpus[i].eff_class > max_eff) max_eff = u->cpus[i].eff_class;

    nk_layout_row_dynamic(ctx, ROW_H, 4);
    for (int i = 0; i < u->ncpu; i++) {
      const StabCpuView *c = &u->cpus[i];
      char core[16] = "";
      if (c->core >= 0) {
        /* C = core, P / E = performance / efficiency core on hybrid CPUs */
        char kind = u->hybrid ? (c->eff_class == max_eff ? 'P' : 'E') : 'C';
        snprintf(core, sizeof(core), " [%c%d]", kind, c->core);
      }
      if (c->errors > 0)
        nk_labelf_colored(ctx, NK_TEXT_LEFT, COL_BAD, "CPU %d%s: %d err", i, core, c->errors);
      else if (c->units > 0)
        nk_labelf_colored(ctx, NK_TEXT_LEFT, COL_OK, "CPU %d%s: OK", i, core);
      else
        nk_labelf(ctx, NK_TEXT_LEFT, "CPU %d%s: -", i, core);
    }
  }

  /* ── Log (last on the page; grows up to STAB_LOG_LINES rows) ─────────── */
  section_label(ctx, "Log:");
  nk_layout_row_dynamic(ctx, LOG_H, 1);
  if (u->log_n == 0)
    nk_label(ctx, "Errors and events of the test appear here.", NK_TEXT_LEFT);
  for (int i = 0; i < u->log_n; i++) {
    const char *line = u->log[i];
    if (strstr(line, "mismatch") || strstr(line, "differ") || strstr(line, "with "))
      nk_label_colored(ctx, line, NK_TEXT_LEFT, COL_BAD);
    else if (strstr(line, "WHEA"))
      nk_label_colored(ctx, line, NK_TEXT_LEFT, COL_WARN);
    else
      nk_label(ctx, line, NK_TEXT_LEFT);
  }

  nk_group_end(ctx);
}
