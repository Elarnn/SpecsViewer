#include "app.h"
#include "bench/bench.h"
#include "net/bench_sync.h"
#include "ui.h"
#include "ui/nk_common.h"

#include <windows.h>
#include <string.h>

static const char *bench_phase_text(enum BenchPhase phase) {
  switch (phase) {
  case BENCH_PHASE_WARMUP: return "Warming up...";
  case BENCH_PHASE_SINGLE: return "Running: single-thread test";
  case BENCH_PHASE_MULTI:  return "Running: multithread test";
  case BENCH_PHASE_DONE:   return "Completed";
  default:                 return "Idle";
  }
}

static double bench_phase_duration(enum BenchPhase phase) {
  if (phase == BENCH_PHASE_SINGLE) return BENCH_SINGLE_SEC;
  if (phase == BENCH_PHASE_MULTI)  return BENCH_MULTI_SEC;
  return BENCH_WARMUP_SEC;
}

static const char *bench_mode_text(enum BenchMode mode) {
  return (mode == BENCH_MODE_STRESS) ? "Stress mode" : "Timed mode";
}

static void push_disabled_btn_style(struct nk_context *ctx,
                                    struct nk_style_button *out) {
  *out = ctx->style.button;
  struct nk_color bg   = nk_rgba(80, 80, 80, 80);
  struct nk_color text = nk_rgba(130, 130, 130, 160);
  ctx->style.button.normal       = nk_style_item_color(bg);
  ctx->style.button.hover        = nk_style_item_color(bg);
  ctx->style.button.active       = nk_style_item_color(bg);
  ctx->style.button.text_normal  = text;
  ctx->style.button.text_hover   = text;
  ctx->style.button.text_active  = text;
}

/* default_ref_tp: throughput of the fallback scale when no reference selected */
static void bench_draw_compare_chart(struct nk_context *ctx, const char *title,
                                     const BenchTestResult *res, int has_ref,
                                     double default_ref_tp) {
  /* Row 1: title */
  nk_layout_row_dynamic(ctx, 22, 1);
  nk_label(ctx, title, NK_TEXT_LEFT);

  /* Row 2: bar — always filled; baseline marker only when reference selected */
  {
    double relative, display_max, cpu_ratio;
    if (has_ref) {
      relative    = res->relative;
      display_max = bench_calc_display_max(relative);
      cpu_ratio   = bench_calc_fill_ratio(relative, display_max);
    } else if (default_ref_tp > 0.0 && res->throughput > 0.0) {
      relative    = res->throughput / default_ref_tp;
      display_max = bench_calc_display_max(relative);
      cpu_ratio   = bench_calc_fill_ratio(relative, display_max);
    } else {
      relative    = 0.0;
      display_max = 1.2;
      cpu_ratio   = 0.0;
    }

    nk_layout_row_dynamic(ctx, 18, 1);
    struct nk_rect            bounds = nk_widget_bounds(ctx);
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    struct nk_color bg     = ctx->style.chart.background.data.color;
    struct nk_color border = ctx->style.chart.border_color;

    nk_fill_rect(canvas, bounds, 2.0f, bg);
    nk_stroke_rect(canvas, bounds, 2.0f, 1.0f, border);
    if (cpu_ratio > 0.0) {
      float fill_w = (float)(cpu_ratio * (double)bounds.w);
      nk_fill_rect(canvas, nk_rect(bounds.x, bounds.y, fill_w, bounds.h),
                   2.0f, nk_rgb(57, 158, 255));
    }
    if (has_ref) {
      double base_ratio = bench_calc_baseline_ratio(display_max);
      float bx = bounds.x + (float)(base_ratio * (double)bounds.w);
      nk_stroke_line(canvas, bx, bounds.y - 2.0f, bx, bounds.y + bounds.h + 2.0f,
                     3.0f, nk_rgb(255, 210, 40));
    }
  }

  /* Row 3: stats (2 cols: CPU% left, Workload right) */
  nk_layout_row_dynamic(ctx, 18, 2);
  if (has_ref && res->relative > 0.0)
    nk_labelf(ctx, NK_TEXT_LEFT, "CPU: %.0f%%", res->relative * 100.0);
  else
    nk_spacer(ctx);
  nk_labelf(ctx, NK_TEXT_RIGHT, "Workload: %.2f M units/s", res->throughput / 1000000.0);
}

void ui_page_bench(struct nk_context *ctx, const struct AppState *S) {
  struct AppState *M = (struct AppState *)S;
  BenchSubmitStatus submit_st = (BenchSubmitStatus)M->bench_sync.submit_status;
  BenchUiState st;
  memset(&st, 0, sizeof(st));

  bench_poll(&M->bench);
  bench_read_ui(&M->bench, &st);

  if (!st.running && InterlockedCompareExchange(&M->bench_sync.refs_ready, 0, 1) == 1)
    bench_refs_set(M->bench_sync.server_refs, M->bench_sync.server_refs_count);

  bench_refs_load();

  int can_submit = !st.running && !st.cancelled &&
                   st.mode  == BENCH_MODE_TIMED &&
                   st.phase == BENCH_PHASE_DONE &&
                   st.single.throughput > 0.0 &&
                   st.multi.throughput  > 0.0;

  int has_ref_now = (bench_refs_selected() >= 0);
  /* chart always: title(22)+bar(18)+stats(18)=58px; 2 status rows always reserved */
  float g_h = 318.0f;

  nk_layout_row_dynamic(ctx, g_h, 1);
  if (nk_group_begin(ctx, "CPU benchmark",
                     NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {

    /* ── Status rows (always 2 rows reserved to prevent layout shift) ── */
    nk_layout_row_dynamic(ctx, 20, 1);
    if (st.running)
      nk_labelf(ctx, NK_TEXT_LEFT, " %s (%s)",
                bench_phase_text(st.phase), bench_mode_text(st.mode));
    else if (st.cancelled)
      nk_label(ctx, " Benchmark interrupted", NK_TEXT_LEFT);
    else
      nk_spacer(ctx);

    nk_layout_row_dynamic(ctx, 20, 1);
    if (st.running) {
      if (st.mode == BENCH_MODE_STRESS) {
        nk_labelf(ctx, NK_TEXT_LEFT,
                  " Stress running: %.1f sec (until Stop)", st.elapsed_sec);
      } else {
        double phase_dur = bench_phase_duration(st.phase);
        double p = (phase_dur > 0.0) ? (st.elapsed_sec / phase_dur) : 0.0;
        if (p < 0.0) p = 0.0;
        if (p > 1.0) p = 1.0;
        nk_labelf(ctx, NK_TEXT_LEFT,
                  " Progress: %.0f%%  (%.1f / %.0f sec)",
                  p * 100.0, st.elapsed_sec, phase_dur);
      }
    } else {
      nk_spacer(ctx);
    }

    nk_layout_row_dynamic(ctx, 4, 1);
    nk_spacer(ctx);

    BenchReference def_ref;
    bench_get_default_ref(&def_ref);

    bench_draw_compare_chart(ctx, "Multithread (all cores)", &st.multi,  has_ref_now, def_ref.multi_throughput);

    nk_layout_row_dynamic(ctx, 8, 1);
    nk_spacer(ctx);

    bench_draw_compare_chart(ctx, "Single-thread",           &st.single, has_ref_now, def_ref.single_throughput);

    nk_layout_row_dynamic(ctx, 8, 1);
    nk_spacer(ctx);

    /* ── Button row: [Start/Stop] | [Stress] | [Submit] ────────────────── */
    struct nk_style_button saved_style;

    /* template: dyn | 155 | 6 | 115 | 6 | 155 | dyn */
    nk_layout_row_template_begin(ctx, 28);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 155);
    nk_layout_row_template_push_static(ctx,   6);
    nk_layout_row_template_push_static(ctx, 115);
    nk_layout_row_template_push_static(ctx,   6);
    nk_layout_row_template_push_static(ctx, 155);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    nk_spacing(ctx, 1);

    /* Btn 1: Start benchmark / Stop */
    if (st.running) {
      if (nk_button_label(ctx, "Stop"))
        bench_stop(&M->bench);
    } else {
      if (nk_button_label(ctx, "Start benchmark")) {
        InterlockedExchange(&M->bench_sync.submit_status, (LONG)BENCH_SUBMIT_IDLE);
        bench_start_timed(&M->bench);
      }
    }

    nk_spacing(ctx, 1);

    /* Btn 2: Stress CPU (disabled while running) */
    if (st.running) {
      push_disabled_btn_style(ctx, &saved_style);
      nk_button_label(ctx, "Stress CPU");
      ctx->style.button = saved_style;
    } else {
      if (nk_button_label(ctx, "Stress CPU")) {
        InterlockedExchange(&M->bench_sync.submit_status, (LONG)BENCH_SUBMIT_IDLE);
        bench_start_stress(&M->bench);
      }
    }

    nk_spacing(ctx, 1);

    /* Btn 3: Submit to server */
    if (submit_st == BENCH_SUBMIT_PENDING) {
      nk_label(ctx, "Submitting...", NK_TEXT_CENTERED);
    } else if (submit_st == BENCH_SUBMIT_OK) {
      nk_label(ctx, "Submitted!", NK_TEXT_CENTERED);
    } else if (can_submit) {
      const char *lbl = (submit_st == BENCH_SUBMIT_ERROR)
                        ? "Retry submit" : "Submit to server";
      if (nk_button_label(ctx, lbl)) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        bench_sync_submit(&M->bench_sync,
          S->snap.data.cpu.model,
          st.single.throughput, st.multi.throughput,
          (int)si.dwNumberOfProcessors);
      }
    } else {
      push_disabled_btn_style(ctx, &saved_style);
      nk_button_label(ctx, "Submit to server");
      ctx->style.button = saved_style;
    }

    nk_spacing(ctx, 1);

    nk_layout_row_dynamic(ctx, 4, 1);
    nk_spacer(ctx);

    nk_group_end(ctx);
  }

  /* ── Reference selector ─────────────────────────────────────────────────── */
  nk_layout_row_dynamic(ctx, 480, 1);

  if (nk_group_begin(ctx, "CPU ref", NK_WINDOW_BORDER)) {
    nk_layout_row_template_begin(ctx, 26);
    nk_layout_row_template_push_static(ctx, 140);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    nk_label(ctx, "Your processor:", NK_TEXT_LEFT);
    nk_label(ctx, S->snap.data.cpu.model[0] ? S->snap.data.cpu.model : "-",
             NK_TEXT_CENTERED);

    nk_label(ctx, "Reference CPU:", NK_TEXT_LEFT);

    int ref_count = bench_refs_count();
    int selected  = bench_refs_selected();
    {
      const char *items[BENCH_MAX_REFS + 1];
      items[0] = "-- None --";
      int shown = ref_count > BENCH_MAX_REFS ? BENCH_MAX_REFS : ref_count;
      for (int i = 0; i < shown; ++i) {
        const BenchReference *r = bench_ref_get(i);
        items[i + 1] = r ? r->cpu_name : "";
      }
      int combo_sel = (selected < 0) ? 0 : selected + 1;
      int picked = nk_combo(ctx, items, shown + 1, combo_sel, 22, nk_vec2(460, 240));
      if (!st.running) {
        int new_sel = (picked == 0) ? -1 : picked - 1;
        if (new_sel != selected)
          bench_refs_select(new_sel);
      }
    }

    const BenchReference *curr_ref = (selected >= 0) ? bench_ref_get(selected) : NULL;
    if (curr_ref) {
      nk_layout_row_dynamic(ctx, 18, 1);
      nk_labelf(ctx, NK_TEXT_LEFT,
                "Reference values: single %.2f M units/s, multi %.2f M units/s",
                curr_ref->single_throughput / 1000000.0,
                curr_ref->multi_throughput  / 1000000.0);
    }

    nk_group_end(ctx);
  }
}
