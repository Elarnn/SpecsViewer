#include "app.h"
#include "ui.h"
#include "ui/nk_common.h"
#include <stdio.h>


static const char *lookup_subvendor(unsigned int id) {
  switch (id) {
    case 0x1462: return "MSI";
    case 0x1043: return "ASUS";
    case 0x1458: return "Gigabyte";
    case 0x3842: return "EVGA";
    case 0x196E: return "PNY";
    case 0x1569: return "Palit";
    case 0x1B4C: return "Colorful";
    case 0x1ACC: return "Inno3D";
    case 0x19DA: return "Zotac";
    case 0x174B: return "Sapphire";
    case 0x1DA2: return "Sapphire";
    case 0x16B3: return "XFX";
    case 0x10DE: return "NVIDIA";
    case 0x1028: return "Dell";
    case 0x103C: return "HP";
    case 0x17AA: return "Lenovo";
    default:     return NULL;
  }
}

void ui_page_gpu(struct nk_context *ctx, const struct AppState *S) {

  if (S->snap.data.gpu.is_nvidia != 1) {
    nk_layout_row_dynamic(ctx, 24, 1);
    nk_label(ctx, "GPU is not supported", NK_TEXT_ALIGN_CENTERED);
    return;
  }
  char buf[128];
  struct nk_color vc = theme_get_value_color((AppTheme)S->active_theme);

  struct nk_rect content = nk_window_get_content_region(ctx);

  const float usage_h = 160.0f;
  const float gap_h = 6.0f;
  float top_h = content.h - usage_h - gap_h;
  if (top_h < 0)
    top_h = 0;

  nk_layout_row_dynamic(ctx, top_h, 1);
  if (nk_group_begin(ctx, "Graphics card",
                     NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE)) {

    nk_layout_row_template_begin(ctx, 26);
    nk_layout_row_template_push_static(ctx, 140);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    nk_label(ctx, "GPU name:", NK_TEXT_RIGHT);
    nk_label_val(ctx, S->snap.data.gpu.name, NK_TEXT_CENTERED, vc);

    nk_layout_row_template_begin(ctx, 26);
    nk_layout_row_template_push_static(ctx, 140);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 95);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    nk_label(ctx, "Device ID:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%04X %04X - %04X %04X",
             S->snap.data.gpu.ven,
             S->snap.data.gpu.dev & 0xFFFF,
             (S->snap.data.gpu.subsys >> 16) & 0xFFFF,
             S->snap.data.gpu.subsys & 0xFFFF);
    nk_label_val(ctx, buf, NK_TEXT_CENTERED, vc);
    nk_label(ctx, "Subvendor:", NK_TEXT_RIGHT);
    {
      unsigned int subven = (S->snap.data.gpu.subsys >> 16) & 0xFFFF;
      const char *svname = lookup_subvendor(subven);
      if (svname)
        snprintf(buf, sizeof(buf), "%s", svname);
      else
        snprintf(buf, sizeof(buf), "%04X", subven);
    }
    nk_label_val(ctx, buf, NK_TEXT_LEFT, vc);

    nk_layout_row_template_begin(ctx, 26);
    nk_layout_row_template_push_static(ctx, 140);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    nk_label(ctx, "Core clock:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "Base %d MHz / Boost %d MHz",
             S->snap.data.gpu.clock_base_mhz, S->snap.data.gpu.clock_boost_mhz);
    nk_label_val(ctx, buf, NK_TEXT_CENTERED, vc);

    nk_label(ctx, "Memory used / total:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%u MB / %u MB", S->snap.gpu_rt.vram_used,
             S->snap.data.gpu.vram_total);
    nk_label_val(ctx, buf, NK_TEXT_CENTERED, vc);

    nk_layout_row_template_begin(ctx, 26);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 70);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx, 95);
    nk_layout_row_template_push_static(ctx, 150);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    nk_label(ctx, "Memory type:", NK_TEXT_RIGHT);
    nk_label_val(ctx, S->snap.data.gpu.vram_type, NK_TEXT_LEFT, vc);
    nk_label(ctx, "Memory freq:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%u MHz", S->snap.data.gpu.mem_mhz);
    nk_label_val(ctx, buf, NK_TEXT_LEFT, vc);
    nk_label(ctx, "Memory bandwidth: ", NK_TEXT_RIGHT);
    if (S->snap.data.gpu.vram_bus_bits && S->snap.data.gpu.mem_mhz)
      snprintf(buf, sizeof(buf), "%.0f GB/s",
               2.0 * S->snap.data.gpu.mem_mhz * S->snap.data.gpu.vram_bus_bits / 8.0 / 1000.0);
    else
      snprintf(buf, sizeof(buf), "-");
    nk_label_val(ctx, buf, NK_TEXT_LEFT, vc);

    nk_label(ctx, "VRAM bus width:", NK_TEXT_RIGHT);
    if (S->snap.data.gpu.vram_bus_bits)
      snprintf(buf, sizeof(buf), "%u-bit", S->snap.data.gpu.vram_bus_bits);
    else
      snprintf(buf, sizeof(buf), "-");
    nk_label_val(ctx, buf, NK_TEXT_LEFT, vc);
    nk_label(ctx, "PCIe width:", NK_TEXT_RIGHT);
    snprintf(buf, sizeof(buf), "%uX", S->snap.data.gpu.pci_lanes);
    nk_label_val(ctx, buf, NK_TEXT_LEFT, vc);
    nk_spacing(ctx, 1);
    nk_spacing(ctx, 1);

    nk_label(ctx, "Driver version:", NK_TEXT_RIGHT);
    nk_label_val(ctx, S->snap.data.gpu.drv_version[0] ? S->snap.data.gpu.drv_version : "-", NK_TEXT_LEFT, vc);
    nk_label(ctx, "Driver date:", NK_TEXT_RIGHT);
    nk_label_val(ctx, S->snap.data.gpu.drv_date[0] ? S->snap.data.gpu.drv_date : "-", NK_TEXT_LEFT, vc);
    nk_spacing(ctx, 1);
    nk_spacing(ctx, 1);

    nk_label(ctx, "Architecture:", NK_TEXT_RIGHT);
    nk_label_val(ctx, S->snap.data.gpu.arch[0] ? S->snap.data.gpu.arch : "-", NK_TEXT_LEFT, vc);
    nk_label(ctx, "Die:", NK_TEXT_RIGHT);
    nk_label_val(ctx, S->snap.data.gpu.die[0] ? S->snap.data.gpu.die : "-", NK_TEXT_LEFT, vc);
    nk_label(ctx, "Process node:", NK_TEXT_RIGHT);
    nk_label_val(ctx, S->snap.data.gpu.process_node[0] ? S->snap.data.gpu.process_node : "-", NK_TEXT_LEFT, vc);

    {
      static const char *feat_names[5] = {
        "CUDA", "Vulkan", "DirectX 12", "Ray Tracing", "DLSS"
      };
      const int feats[5] = {
        S->snap.data.gpu.feat_cuda,
        S->snap.data.gpu.feat_vulkan,
        S->snap.data.gpu.feat_dx12,
        S->snap.data.gpu.feat_ray_tracing,
        S->snap.data.gpu.feat_dlss
      };

      int fi = 0;
      for (int row = 0; row < 2; row++) {
        nk_layout_row_template_begin(ctx, 22);
        nk_layout_row_template_push_static(ctx, 140);
        nk_layout_row_template_push_static(ctx, 22);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, 22);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, 22);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_end(ctx);

        nk_label(ctx, row == 0 ? "GPU Features:" : "", NK_TEXT_RIGHT);
        for (int col = 0; col < 3; col++, fi++) {
          if (fi < 5) {
            if (feats[fi] && S->ok_ready)
              nk_image(ctx, S->icon_ok);
            else
              nk_spacing(ctx, 1);
            nk_label(ctx, feat_names[fi], NK_TEXT_LEFT);
          } else {
            nk_spacing(ctx, 1);
            nk_spacing(ctx, 1);
          }
        }
      }
    }

    nk_spacing(ctx, 1);

    nk_group_end(ctx);
  }

  nk_layout_row_dynamic(ctx, usage_h, 1);

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
    nk_group_end(ctx);
  }
}
