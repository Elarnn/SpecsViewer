#include "app.h"
#include "ui.h"
#include "ui/nk_common.h"
#include <stdio.h>

void ui_page_ram(struct nk_context *ctx, const struct AppState *S) {
  char buf[128];

  struct nk_rect content = nk_window_get_content_region(ctx);

  const float usage_h = 140.0f; // высота нижнего блока (фиксированная)
  const float gap_h = 6.0f;     // небольшой зазор между зонами

  float top_h = content.h - usage_h - gap_h;
  if (top_h < 0)
    top_h = 0;

  /* ВЕРХНЯЯ зона */
  nk_layout_row_dynamic(ctx, top_h, 1);
  if (nk_group_begin(ctx, "Top",
                     NK_WINDOW_NO_SCROLLBAR)) /* Top будет скроллиться */
  {
    /* RAM box */
    nk_layout_row_dynamic(ctx, 170, 1); /* <- ВАЖНО: высота RAM группы */
    if (nk_group_begin(ctx, "RAM", NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE)) {
      /* твой контент RAM */
      nk_layout_row_template_begin(ctx, 26);
      nk_layout_row_template_push_static(ctx, 140);
      nk_layout_row_template_push_dynamic(ctx);
      nk_layout_row_template_end(ctx);

      nk_label(ctx, "Memory used / total", NK_TEXT_RIGHT);
      snprintf(buf, sizeof(buf), "%d mb / %d mb", S->snap.ram_rt.used_mb,
               S->snap.data.ram.total_mb);
      nk_label(ctx, buf, NK_TEXT_CENTERED);

      nk_label(ctx, "Type / Frequency:", NK_TEXT_RIGHT);
      snprintf(buf, sizeof(buf), "%s / %d MHz", S->snap.data.ram.type,
               S->snap.data.ram.mhz);
      nk_label(ctx, buf, NK_TEXT_CENTERED);

      nk_layout_row_template_begin(ctx, 26);
      nk_layout_row_template_push_static(ctx, 140);
      nk_layout_row_template_push_static(ctx, 80);
      nk_layout_row_template_push_dynamic(ctx);
      nk_layout_row_template_end(ctx);

      nk_label(ctx, "RAM modules:", NK_TEXT_RIGHT);
      snprintf(buf, sizeof(buf), "%d", S->snap.data.ram.module_count);
      nk_label(ctx, buf, NK_TEXT_CENTERED);
      nk_spacer(ctx);

      nk_label(ctx, "Channel:", NK_TEXT_RIGHT);
      nk_label(ctx,
               S->snap.data.ram.channel == 1
                   ? "single"
                   : (S->snap.data.ram.channel == 2 ? "dual" : "no data"),
               NK_TEXT_CENTERED);
      nk_spacer(ctx);

      nk_group_end(ctx);
    }

    nk_layout_row_dynamic(ctx, 6, 1);
    nk_spacer(ctx);

    /* MotherBoard box */
    nk_layout_row_dynamic(ctx, 160,
                          1); /* <- ВАЖНО: высота MotherBoard группы */
    if (nk_group_begin(ctx, "MotherBoard",
                       NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE)) {
      /* контент MB */
      nk_layout_row_template_begin(ctx, 26);
      nk_layout_row_template_push_static(ctx, 140);
      nk_layout_row_template_push_dynamic(ctx);
      nk_layout_row_template_end(ctx);

      nk_label(ctx, "Vendor:", NK_TEXT_RIGHT);
      nk_label(ctx, S->snap.data.board.board_vendor, NK_TEXT_CENTERED);

      nk_label(ctx, "Product:", NK_TEXT_RIGHT);
      nk_label(ctx, S->snap.data.board.board_product, NK_TEXT_CENTERED);

      nk_label(ctx, "SMBIOS UUID:", NK_TEXT_RIGHT);
      nk_label(ctx, S->snap.data.board.smbios_uuid, NK_TEXT_CENTERED);

      nk_layout_row_dynamic(ctx, 26, 4);

      nk_label(ctx, "Bios vendor:", NK_TEXT_RIGHT);
      nk_label(ctx, S->snap.data.board.bios_vendor, NK_TEXT_CENTERED);

      nk_label(ctx, "Bios version:", NK_TEXT_RIGHT);
      nk_label(ctx, S->snap.data.board.bios_version, NK_TEXT_CENTERED);

      nk_group_end(ctx);
    }

    nk_group_end(ctx);
  }

  /* НИЖНЯЯ зона — фиксированная, всегда у низа окна */
  nk_layout_row_dynamic(ctx, usage_h, 1);
  if (nk_group_begin(ctx, "RAM usage", NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
    nk_layout_row_template_begin(ctx, 26);
    nk_layout_row_template_push_static(ctx, 140);
    nk_layout_row_template_push_static(ctx, 100);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    nk_label(ctx, "Usage:", NK_TEXT_RIGHT);
    if (S->snap.data.ram.total_mb > 0) {
      double pct =
          (double)S->snap.ram_rt.used_mb * 100.0 / (double)S->snap.data.ram.total_mb;
      snprintf(buf, sizeof(buf), "%.1f %%", pct);
    } else {
      snprintf(buf, sizeof(buf), "—");
    }
    nk_label(ctx, buf, NK_TEXT_CENTERED);
    nk_size used = (nk_size)S->snap.ram_rt.used_mb;
    nk_size total = (nk_size)(S->snap.data.ram.total_mb ? S->snap.data.ram.total_mb : 1);
    if (used > total)
      used = total;

    nk_progress(ctx, &used, total, nk_false);

    nk_group_end(ctx); /* конец RAM usage */
  }
}
