#include "app.h"
#include "ui.h"
#include "ui/nk_common.h"
#include <stdio.h>


void ui_page_gpu(struct nk_context *ctx, const struct AppState *S) {

  if (S->snap.data.gpu.is_nvidia != 1) {
    nk_layout_row_dynamic(ctx, 24, 1);
    nk_label(ctx, "GPU is not supported", NK_TEXT_ALIGN_CENTERED);
    return;
  }
  char buf[128];

  struct nk_rect content = nk_window_get_content_region(ctx);

  const float usage_h = 160.0f; // высота нижнего блока
  const float gap_h = 6.0f;     // небольшой зазор
  float top_h = content.h - usage_h - gap_h;
  if (top_h < 0)
    top_h = 0;

  /* Верхняя часть */
  nk_layout_row_dynamic(ctx, top_h, 1);
  if (nk_group_begin(ctx, "Graphics card",
                     NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE)) {

    nk_layout_row_template_begin(ctx, 26);        // высота строки
    nk_layout_row_template_push_static(ctx, 140); // первая колонка: 140 px
    nk_layout_row_template_push_dynamic(ctx); // вторая колонка: всё оставшееся
    nk_layout_row_template_end(ctx);

    nk_label(ctx, "GPU name:", NK_TEXT_RIGHT);
    nk_label(ctx, S->snap.data.gpu.name, NK_TEXT_CENTERED);

    nk_label(ctx, "Device ID:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%04X %04X - %04X %04X",
             S->snap.data.gpu.ven,                     // Vendor ID
             S->snap.data.gpu.dev & 0xFFFF,            // Device ID
             (S->snap.data.gpu.subsys >> 16) & 0xFFFF, // Subsystem Vendor
             S->snap.data.gpu.subsys & 0xFFFF);
    nk_label(ctx, buf, NK_TEXT_CENTERED);

    nk_label(ctx, "Core clock:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "Base %d MHz / Boost %d MHz",
             S->snap.data.gpu.clock_base_mhz, S->snap.data.gpu.clock_boost_mhz);
    nk_label(ctx, buf, NK_TEXT_CENTERED);

    nk_label(ctx, "Memory used / total:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%u MB / %u MB", S->snap.gpu_rt.vram_used,
             S->snap.data.gpu.vram_total);
    nk_label(ctx, buf, NK_TEXT_CENTERED);

    nk_layout_row_template_begin(ctx, 26); // высота строки
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 80);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 80);
    nk_layout_row_template_push_static(ctx, 160);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    nk_label(ctx, "Memory type:", NK_TEXT_RIGHT);
    nk_label(ctx, S->snap.data.gpu.vram_type, NK_TEXT_LEFT);

    nk_label(ctx, "Memory freq:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%u MHz", S->snap.data.gpu.mem_mhz);
    nk_label(ctx, buf, NK_TEXT_LEFT);

    nk_label(ctx, "PCIe link width:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%uX", S->snap.data.gpu.pci_lanes);
    nk_label(ctx, buf, NK_TEXT_LEFT);

    nk_spacing(ctx, 1);

    nk_group_end(ctx);
  }

  nk_layout_row_dynamic(ctx, 160, 1);

  if (nk_group_begin(ctx, "GPU sensors", NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
    nk_layout_row_template_begin(ctx, 26);
    nk_layout_row_template_push_static(ctx, 140);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 140);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    nk_label(ctx, "Core temp:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%d °C", S->snap.gpu_rt.clock_temp);
    nk_label(ctx, buf, NK_TEXT_CENTERED);

    nk_label(ctx, "Core freq:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%d MHz", S->snap.gpu_rt.curr_core_freq);
    nk_label(ctx, buf, NK_TEXT_CENTERED);

    nk_label(ctx, "Memory freq:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%d MHz", S->snap.gpu_rt.curr_mem_freq);
    nk_label(ctx, buf, NK_TEXT_CENTERED);

    if (S->snap.gpu_rt.mem_temp != 0) {
      nk_label(ctx, "Memory temp:", NK_TEXT_RIGHT);
      snprintf(buf, sizeof(buf), "%d °C", S->snap.gpu_rt.mem_temp);
      nk_label(ctx, buf, NK_TEXT_CENTERED);
    } else {
      nk_spacer(ctx);
      nk_spacer(ctx);
    }

    nk_layout_row_template_begin(ctx, 26);
    nk_layout_row_template_push_static(ctx, 140);
    nk_layout_row_template_push_static(ctx, 175);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    nk_label(ctx, "Core load:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%.1f %%", S->snap.gpu_rt.vram_load);
    nk_label(ctx, buf, NK_TEXT_CENTERED);

    nk_size used_percent = S->snap.gpu_rt.vram_load;
    nk_progress(ctx, &used_percent, 100, nk_false);
  }
}