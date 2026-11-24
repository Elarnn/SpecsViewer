#include "app.h"
#include "bench/bench.h"
#include "net/bench_sync.h"
#include "ui.h"
#include "ui/nk_common.h"

#include <windows.h>
#include <stdio.h>
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

  /* Row 3: stats (2 cols: CPU% vs reference left, score right) */
  nk_layout_row_dynamic(ctx, 18, 2);
  if (has_ref && res->relative > 0.0)
    nk_labelf(ctx, NK_TEXT_LEFT, "CPU: %.0f%% of reference", res->relative * 100.0);
  else
    nk_spacer(ctx);
  if (res->throughput > 0.0)
    nk_labelf(ctx, NK_TEXT_RIGHT, "Score: %.0f pts", res->throughput);
  else
    nk_label(ctx, "Score: -", NK_TEXT_RIGHT);

  /* Row 4: per sub-test points (1000 = calibration CPU, single thread) */
  nk_layout_row_dynamic(ctx, 16, 1);
  if (res->sub_done > 0) {
    char line[192];
    int  pos = 0;
    for (int i = 0; i < res->sub_done && i < BENCH_SUBTESTS; i++) {
      int n = snprintf(line + pos, sizeof(line) - (size_t)pos, "%s%s %.0f",
                       i ? "   " : "", bench_subtest_name(i), res->sub_pts[i]);
      if (n < 0 || pos + n >= (int)sizeof(line)) break;
      pos += n;
    }
    nk_label(ctx, line, NK_TEXT_CENTERED);
  } else {
    nk_spacer(ctx);
  }
}

/* "Your processor", the reference selector and the reference score. The score
   row is always reserved so the charts below never shift. */
static void bench_draw_reference_rows(struct nk_context *ctx, const struct AppState *S,
                                      int running) {
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
    if (!running) {
      int new_sel = (picked == 0) ? -1 : picked - 1;
      if (new_sel != selected)
        bench_refs_select(new_sel);
    }
  }

  const BenchReference *curr_ref = (selected >= 0) ? bench_ref_get(selected) : NULL;
  nk_layout_row_dynamic(ctx, 18, 1);
  if (curr_ref)
    nk_labelf(ctx, NK_TEXT_LEFT,
              "Reference score: single %.0f pts, multi %.0f pts",
              curr_ref->single_throughput, curr_ref->multi_throughput);
  else
    nk_spacer(ctx);
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

  /* a result with failed checks was computed wrongly: never submit it */
  int can_submit = !st.running && !st.cancelled &&
                   st.phase == BENCH_PHASE_DONE &&
                   st.errors == 0 &&
                   st.single.throughput > 0.0 &&
                   st.multi.throughput  > 0.0;

  int has_ref_now = (bench_refs_selected() >= 0);
  /* reference rows (processor, reference, its score) + 2 status rows +
     2 charts (title 22 + bar 18 + stats 18 + breakdown 16) + buttons */
  float g_h = 442.0f;

  /* The page scrolls: benchmark, then the stability test. */
  {
    struct nk_rect region = nk_window_get_content_region(ctx);
    float page_h = region.h - 2.0f * ctx->style.window.group_padding.y;
    nk_layout_row_dynamic(ctx, page_h > 100.0f ? page_h : 100.0f, 1);
  }
  if (!nk_group_begin(ctx, "BenchPage", 0))
    return;

  nk_layout_row_dynamic(ctx, g_h, 1);
  if (nk_group_begin(ctx, "CPU benchmark",
                     NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {

    /* ── Processor and reference (the charts compare against it) ────────── */
    bench_draw_reference_rows(ctx, S, st.running);

    nk_layout_row_dynamic(ctx, 6, 1);
    nk_spacer(ctx);

    /* ── Status rows (always 2 rows reserved to prevent layout shift) ── */
    nk_layout_row_dynamic(ctx, 20, 1);
    if (st.errors > 0 && (st.running || st.phase == BENCH_PHASE_DONE))
      nk_labelf_colored(ctx, NK_TEXT_LEFT, nk_rgb(235, 80, 80),
                        " %d calculation error(s): the result is invalid and cannot be submitted",
                        st.errors);
    else if (st.running)
      nk_labelf(ctx, NK_TEXT_LEFT, " %s", bench_phase_text(st.phase));
    else if (st.cancelled)
      nk_label(ctx, " Benchmark interrupted", NK_TEXT_LEFT);
    else if (st.phase == BENCH_PHASE_DONE)
      nk_label_colored(ctx, " Completed, all results verified", NK_TEXT_LEFT, nk_rgb(80, 200, 120));
    else
      nk_spacer(ctx);

    nk_layout_row_dynamic(ctx, 20, 1);
    if (st.running) {
      const char *sub = (st.subtest >= 0) ? bench_subtest_name(st.subtest) : "";
      double phase_dur = bench_phase_duration(st.phase);
      double p = (phase_dur > 0.0) ? (st.elapsed_sec / phase_dur) : 0.0;
      if (p < 0.0) p = 0.0;
      if (p > 1.0) p = 1.0;
      if (st.subtest >= 0)
        nk_labelf(ctx, NK_TEXT_LEFT,
                  " Progress: %.0f%%  (%.1f / %.0f sec)   %d/%d %s",
                  p * 100.0, st.elapsed_sec, phase_dur,
                  st.subtest + 1, BENCH_SUBTESTS, sub);
      else
        nk_labelf(ctx, NK_TEXT_LEFT,
                  " Progress: %.0f%%  (%.1f / %.0f sec)",
                  p * 100.0, st.elapsed_sec, phase_dur);
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

    /* ── Button row: [Start/Stop] | [Submit] ────────────────────────────── */
    struct nk_style_button saved_style;

    /* template: dyn | 155 | 6 | 155 | dyn */
    nk_layout_row_template_begin(ctx, 28);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 155);
    nk_layout_row_template_push_static(ctx,   6);
    nk_layout_row_template_push_static(ctx, 155);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    nk_spacing(ctx, 1);

    /* Btn 1: Start benchmark / Stop */
    const int stab_busy = (int)M->stab.running;   /* stability test owns the CPU */
    if (st.running) {
      if (nk_button_label(ctx, "Stop"))
        bench_stop(&M->bench);
    } else if (stab_busy) {
      push_disabled_btn_style(ctx, &saved_style);
      nk_button_label(ctx, "Stability busy");
      ctx->style.button = saved_style;
    } else {
      if (nk_button_label(ctx, "Start benchmark")) {
        InterlockedExchange(&M->bench_sync.submit_status, (LONG)BENCH_SUBMIT_IDLE);
        bench_start_timed(&M->bench);
      }
    }

    nk_spacing(ctx, 1);

    /* Btn 2: Submit to server */
    if (submit_st == BENCH_SUBMIT_PENDING) {
      nk_label(ctx, "Submitting...", NK_TEXT_CENTERED);
    } else if (submit_st == BENCH_SUBMIT_OK) {
      nk_label(ctx, "Submitted!", NK_TEXT_CENTERED);
    } else if (can_submit) {
      const char *lbl = (submit_st == BENCH_SUBMIT_ERROR)
                        ? "Retry submit" : "Submit to server";
      if (nk_button_label(ctx, lbl)) {
        bench_sync_submit(&M->bench_sync,
          S->snap.data.cpu.model,
          &st.single, &st.multi,
          bench_logical_cpu_count());
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

  /* ── Stability test ─────────────────────────────────────────────────────── */
  ui_stability_group(ctx, M);

  nk_group_end(ctx);   /* BenchPage */
}
