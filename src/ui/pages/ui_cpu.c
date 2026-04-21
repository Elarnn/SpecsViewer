#include "app.h"
#include "stdio.h"
#include "ui.h"
#include "ui/nk_common.h"


void ui_page_cpu(struct nk_context *ctx, const struct AppState *S) {
  char buf[128];

  struct nk_rect content = nk_window_get_content_region(ctx);

  const float usage_h = 110.0f;  // высота нижнего блока
  const float gap_h = 6.0f;     // небольшой зазор
  float top_h = content.h - usage_h - gap_h;
  if (top_h < 0)
    top_h = 0;

  /* Верхняя часть */
  nk_layout_row_dynamic(ctx, top_h, 1);

  if (nk_group_begin(ctx, "Processor",
                     NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE)) {
    nk_layout_row_template_begin(ctx, 26);        // высота строки
    nk_layout_row_template_push_static(ctx, 140); // первая колонка: 140 px
    nk_layout_row_template_push_dynamic(ctx); // вторая колонка: всё оставшееся
    nk_layout_row_template_end(ctx);

    nk_label(ctx, "CPU Model:", NK_TEXT_RIGHT);
    nk_label(ctx, S->snap.data.cpu.model[0] ? S->snap.data.cpu.model : "-",
             NK_TEXT_CENTERED);

    nk_layout_row_template_begin(ctx, 26);
    nk_layout_row_template_push_static(ctx, 140);
    nk_layout_row_template_push_static(ctx, 70);
    nk_layout_row_template_push_static(ctx, 140);
    nk_layout_row_template_push_static(ctx, 70);
    nk_layout_row_template_end(ctx);

    nk_label(ctx, "Cores:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%d", S->snap.data.cpu.cores);
    nk_label(ctx, buf, NK_TEXT_CENTERED);

    nk_label(ctx, "Threads:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%d", S->snap.data.cpu.threads);
    nk_label(ctx, buf, NK_TEXT_CENTERED);

    nk_layout_row_template_begin(ctx, 26);
    nk_layout_row_template_push_static(ctx, 140);
    nk_layout_row_template_push_static(ctx, 100);
    nk_layout_row_template_push_dynamic(ctx); // пока пусто
    nk_layout_row_template_end(ctx);

    nk_label(ctx, "Base frequency:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%d MHz", S->snap.data.cpu.base_mhz);
    nk_label(ctx, buf, NK_TEXT_CENTERED);

    nk_spacer(ctx);

    nk_layout_row_template_begin(ctx, 26);
    nk_layout_row_template_push_static(ctx, 140); // family column
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 140); // model column
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 140); // stepping column
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    nk_label(ctx, "Family:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%d", S->snap.data.cpu.family);
    nk_label(ctx, buf, NK_TEXT_CENTERED);

    nk_label(ctx, "Model:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%d", S->snap.data.cpu.model_id);
    nk_label(ctx, buf, NK_TEXT_CENTERED);

    nk_label(ctx, "Stepping:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%d", S->snap.data.cpu.stepping);
    nk_label(ctx, buf, NK_TEXT_CENTERED);

    nk_layout_row_template_begin(ctx, 50);
    nk_layout_row_template_push_static(ctx, 140);
    nk_layout_row_template_push_static(ctx, 12); // GAP
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    nk_label(ctx, "Instructions:", NK_TEXT_ALIGN_RIGHT | NK_TEXT_ALIGN_TOP);
    nk_spacer(ctx);
    nk_label_wrap(ctx,
                  S->snap.data.cpu.instructions[0] ? S->snap.data.cpu.instructions : "-");

    nk_layout_row_dynamic(ctx, 160, 2);
    nk_spacer(ctx);
    if (nk_group_begin(ctx, "Cache",
                       NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER |
                           NK_WINDOW_TITLE)) {
      nk_layout_row_dynamic(ctx, 26, 3);

      /* L1 Data */
      nk_label(ctx, "L1 Data", NK_TEXT_CENTERED);
      snprintf(buf, sizeof(buf), "%d x %d KBytes", S->snap.data.cpu.cache_l1d_x,
               S->snap.data.cpu.cache_l1d_kb);
      nk_label(ctx, buf, NK_TEXT_CENTERED);
      snprintf(buf, sizeof(buf), "%d-way", S->snap.data.cpu.cache_l1d_way);
      nk_label(ctx, buf, NK_TEXT_CENTERED);

      /* L1 Inst */
      nk_label(ctx, "L1 Inst", NK_TEXT_CENTERED);
      snprintf(buf, sizeof(buf), "%d x %d KBytes", S->snap.data.cpu.cache_l1i_x,
               S->snap.data.cpu.cache_l1i_kb);
      nk_label(ctx, buf, NK_TEXT_CENTERED);
      snprintf(buf, sizeof(buf), "%d-way", S->snap.data.cpu.cache_l1i_way);
      nk_label(ctx, buf, NK_TEXT_CENTERED);

      /* Level 2 */
      nk_label(ctx, "Level 2", NK_TEXT_CENTERED);
      if (S->snap.data.cpu.cache_l2_kb >= 1024)
        snprintf(buf, sizeof(buf), "%d x %.2f MBytes", S->snap.data.cpu.cache_l2_x,
                 S->snap.data.cpu.cache_l2_kb / 1024.0);
      else
        snprintf(buf, sizeof(buf), "%d x %d KBytes", S->snap.data.cpu.cache_l2_x,
                 S->snap.data.cpu.cache_l2_kb);
      nk_label(ctx, buf, NK_TEXT_CENTERED);
      snprintf(buf, sizeof(buf), "%d-way", S->snap.data.cpu.cache_l2_way);
      nk_label(ctx, buf, NK_TEXT_CENTERED);

      /* Level 3 */
      nk_label(ctx, "Level 3", NK_TEXT_CENTERED);
      if (S->snap.data.cpu.cache_l3_kb >= 1024)
        snprintf(buf, sizeof(buf), "%d x %.2f MBytes", S->snap.data.cpu.cache_l3_x,
                 S->snap.data.cpu.cache_l3_kb / 1024.0);
      else
        snprintf(buf, sizeof(buf), "%d x %d KBytes", S->snap.data.cpu.cache_l3_x,
                 S->snap.data.cpu.cache_l3_kb);
      nk_label(ctx, buf, NK_TEXT_CENTERED);
      snprintf(buf, sizeof(buf), "%d-way", S->snap.data.cpu.cache_l3_way);
      nk_label(ctx, buf, NK_TEXT_CENTERED);

      nk_group_end(ctx);
    }

    nk_group_end(ctx);
  }

  nk_layout_row_dynamic(ctx, usage_h, 1);
  if (nk_group_begin(ctx, "CPU sensors",
                     NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
    nk_layout_row_template_begin(ctx, 26);
    nk_layout_row_template_push_static(ctx, 140);
    nk_layout_row_template_push_static(ctx, 100);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    nk_label(ctx, "CPU Load:", NK_TEXT_RIGHT);

    snprintf(buf, sizeof(buf), "%.1f %%", S->snap.cpu_rt.load);
    nk_label(ctx, buf, NK_TEXT_CENTERED);

    nk_size load = 0;
    if (S->snap.cpu_rt.load > 0.0)
      load = (nk_size)(S->snap.cpu_rt.load + 0.5);
    if (load > 100)
      load = 100;
    nk_progress(ctx, &load, 100, nk_false);

    nk_label(ctx, "CPU Temp:", NK_TEXT_RIGHT);

    if (S->snap.cpu_rt.cpu_temp >= 0)
      snprintf(buf, sizeof(buf), "%d °C", S->snap.cpu_rt.cpu_temp);
    else
      snprintf(buf, sizeof(buf), "N/A");

    nk_label(ctx, buf, NK_TEXT_CENTERED);

    nk_spacer(ctx); /* third column — empty */

    nk_group_end(ctx);
  }
}
