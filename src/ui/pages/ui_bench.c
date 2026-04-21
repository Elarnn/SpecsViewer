#include "app.h"
#include "bench/bench.h"
#include "ui.h"
#include "ui/nk_common.h"

#include <string.h>

static const char *bench_phase_text(enum BenchPhase phase) {
  switch (phase) {
  case BENCH_PHASE_MULTI:
    return "Running: multithread test";
  case BENCH_PHASE_SINGLE:
    return "Running: single-thread test";
  case BENCH_PHASE_DONE:
    return "Completed";
  default:
    return "Idle";
  }
}

static const char *bench_mode_text(enum BenchMode mode) {
  return (mode == BENCH_MODE_STRESS) ? "Stress mode" : "Timed mode";
}

static void bench_draw_compare_chart(struct nk_context *ctx, const char *title,
                                     const BenchTestResult *res) {
  struct nk_rect bounds;
  struct nk_command_buffer *canvas;

  double display_max = bench_calc_display_max(res->relative);
  double baseline_ratio = bench_calc_baseline_ratio(display_max);
  double cpu_ratio = bench_calc_fill_ratio(res->relative, display_max);

  nk_layout_row_dynamic(ctx, 22, 1);
  nk_label(ctx, title, NK_TEXT_LEFT);

  nk_layout_row_dynamic(ctx, 16, 1);
  bounds = nk_widget_bounds(ctx);
  canvas = nk_window_get_canvas(ctx);

  struct nk_color bg = nk_rgb(42, 42, 44);
  struct nk_color border = nk_rgb(82, 82, 86);
  struct nk_color cpu_col = nk_rgb(57, 158, 255);
  struct nk_color baseline_col = nk_rgb(255, 175, 45);

  nk_fill_rect(canvas, bounds, 2.0f, bg);
  nk_stroke_rect(canvas, bounds, 2.0f, 1.0f, border);

  float fill_w = (float)(cpu_ratio * (double)bounds.w);
  struct nk_rect fill = nk_rect(bounds.x, bounds.y, fill_w, bounds.h);
  nk_fill_rect(canvas, fill, 2.0f, cpu_col);

  float baseline_x = bounds.x + (float)(baseline_ratio * (double)bounds.w);
  nk_stroke_line(canvas, baseline_x, bounds.y, baseline_x, bounds.y + bounds.h,
                 1.0f, baseline_col);

  nk_layout_row_dynamic(ctx, 18, 3);
  nk_labelf(ctx, NK_TEXT_LEFT, "CPU: %.2fx", res->relative);
  nk_labelf(ctx, NK_TEXT_CENTERED, "Baseline: 1.00x");
  nk_labelf(ctx, NK_TEXT_RIGHT, "Scale max: %.2fx", display_max);

  nk_layout_row_dynamic(ctx, 18, 2);
  nk_labelf(ctx, NK_TEXT_LEFT, "Score: %.0f", res->score);
  nk_labelf(ctx, NK_TEXT_RIGHT, "Throughput: %.2f M ops/s",
            res->throughput / 1000000.0);
}

void ui_page_bench(struct nk_context *ctx, const struct AppState *S) {
  struct AppState *M = (struct AppState *)S;
  BenchUiState st;
  memset(&st, 0, sizeof(st));

  bench_poll(&M->bench);
  bench_read_ui(&M->bench, &st);
  bench_refs_load();

  /* Group height: content(~248) + overhead(~52); expand when running/cancelled */
  float g_h = 300.0f;
  if (st.running)   g_h += 48.0f; /* status row (20) + progress row (20) + 2×spacing */
  if (st.cancelled) g_h += 24.0f; /* cancelled row (20) + spacing */

  nk_layout_row_dynamic(ctx, g_h, 1);
  if (nk_group_begin(ctx, "CPU benchmark",
                     NK_WINDOW_TITLE | NK_WINDOW_BORDER |
                     NK_WINDOW_NO_SCROLLBAR)) {

    /* ── Buttons (centered, with explicit widths and gap) ── */
    if (st.running) {
      /* dynamic | Stop(140) | dynamic */
      nk_layout_row_template_begin(ctx, 28);
      nk_layout_row_template_push_dynamic(ctx);
      nk_layout_row_template_push_static(ctx, 140);
      nk_layout_row_template_push_dynamic(ctx);
      nk_layout_row_template_end(ctx);
      nk_spacing(ctx, 1);
      if (nk_button_label(ctx, "Stop"))
        bench_stop(&M->bench);
      nk_spacing(ctx, 1);

      nk_layout_row_dynamic(ctx, 20, 1);
      nk_labelf(ctx, NK_TEXT_LEFT, " %s (%s)",
                bench_phase_text(st.phase), bench_mode_text(st.mode));

      nk_layout_row_dynamic(ctx, 20, 1);
      if (st.mode == BENCH_MODE_STRESS) {
        nk_labelf(ctx, NK_TEXT_LEFT,
                  " Stress running: %.1f sec (until Stop)", st.elapsed_sec);
      } else {
        double p = st.elapsed_sec / BENCH_TEST_DURATION_SEC;
        if (p < 0.0) p = 0.0;
        if (p > 1.0) p = 1.0;
        nk_labelf(ctx, NK_TEXT_LEFT,
                  " Current test progress: %.0f%% (%.1f / %.0f sec)",
                  p * 100.0, st.elapsed_sec, BENCH_TEST_DURATION_SEC);
      }
    } else {
      /* dynamic | Start(180) | gap(10) | Stress(130) | dynamic */
      nk_layout_row_template_begin(ctx, 28);
      nk_layout_row_template_push_dynamic(ctx);
      nk_layout_row_template_push_static(ctx, 180);
      nk_layout_row_template_push_static(ctx, 10);
      nk_layout_row_template_push_static(ctx, 130);
      nk_layout_row_template_push_dynamic(ctx);
      nk_layout_row_template_end(ctx);
      nk_spacing(ctx, 1);
      if (nk_button_label(ctx, "Start benchmark"))
        bench_start_timed(&M->bench);
      nk_spacing(ctx, 1); /* gap between buttons */
      if (nk_button_label(ctx, "Stress CPU"))
        bench_start_stress(&M->bench);
      nk_spacing(ctx, 1);
    }

    if (st.cancelled) {
      nk_layout_row_dynamic(ctx, 20, 1);
      nk_label(ctx, " Benchmark interrupted", NK_TEXT_LEFT);
    }

    nk_layout_row_dynamic(ctx, 4, 1);
    nk_spacer(ctx);

    bench_draw_compare_chart(ctx, "Multithread (all cores)", &st.multi);

    nk_layout_row_dynamic(ctx, 8, 1);
    nk_spacer(ctx);

    bench_draw_compare_chart(ctx, "Single-thread", &st.single);

    nk_layout_row_dynamic(ctx, 10, 1);
    nk_spacer(ctx);

    nk_group_end(ctx);
  }

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
    int selected = bench_refs_selected();
    if (ref_count > 0) {
      const char *items[16] = {0};
      int shown_count = ref_count > 16 ? 16 : ref_count;

      for (int i = 0; i < shown_count; ++i) {
        const BenchReference *r = bench_ref_get(i);
        if (r)
          items[i] = r->cpu_name;
      }

      int picked =
          nk_combo(ctx, items, shown_count, selected, 22, nk_vec2(460, 240));
      if (!st.running && picked != selected)
        bench_refs_select(picked);
    }

    const BenchReference *curr_ref = bench_ref_get(bench_refs_selected());
    if (curr_ref) {
      nk_layout_row_dynamic(ctx, 18, 1);
      nk_labelf(ctx, NK_TEXT_LEFT,
                "Reference values: single %.2f M ops/s, multi %.2f M ops/s",
                curr_ref->single_throughput / 1000000.0,
                curr_ref->multi_throughput / 1000000.0);
    }

    nk_group_end(ctx);
  }
}
