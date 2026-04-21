// ui_drivers_list.c
#include "../../core/backend.h"
#include "../../core/security/secure.h"
#include "app.h"
#include "ui/nk_common.h"
#include "ui/ui.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int open_state[DRV_MAX] = {0}; // expanded state for each driver index

static const char *safe_str(const char *s) { return (s && s[0]) ? s : "-"; }

static inline int driver_is_danger(uint32_t flags) {
  return flags & (DRV_RISK_LOLDRIVERS | DRV_RISK_MS_BLOCKLIST);
}

static inline int driver_is_warning(uint32_t flags) {
  return (flags & DRV_RISK_UNSIGNED) &&
         !(flags & (DRV_RISK_LOLDRIVERS | DRV_RISK_MS_BLOCKLIST));
}

static const char *driver_title(const DriverInfo *d) {
  if (d->device_desc[0])
    return d->device_desc;
  if (d->driver_file[0])
    return d->driver_file;
  if (d->service[0])
    return d->service;
  return "Unknown";
}

static void append_reason(char *dst, size_t dstsz, const char *reason) {
  if (!dst || dstsz == 0 || !reason || !reason[0])
    return;

  if (dst[0])
    strncat(dst, ", ", dstsz - strlen(dst) - 1);
  strncat(dst, reason, dstsz - strlen(dst) - 1);
}

static void build_driver_header(char *out, size_t outsz, const DriverInfo *d,
                                uint32_t flags) {
  const char *name = driver_title(d);

  if (!flags) {
    snprintf(out, outsz, "%s", name);
    return;
  }

  // icons
  char icons[16] = {0};
  char reasons[128] = {0};

  /* Reasons list (can be multiple) */
  if (flags & DRV_RISK_LOLDRIVERS)
    append_reason(reasons, sizeof(reasons), "LOLDrivers");

  if (flags & DRV_RISK_MS_BLOCKLIST)
    append_reason(reasons, sizeof(reasons), "Microsoft DB");

  if (flags & DRV_RISK_UNSIGNED)
    append_reason(reasons, sizeof(reasons), "Unsigned");

  /* Severity icon (single, priority-based) */
  if (driver_is_danger(flags)) {
    strncat(icons, "D", sizeof(icons) - strlen(icons) - 1); // danger marker
  } else if (driver_is_warning(flags)) {
    strncat(icons, "W", sizeof(icons) - strlen(icons) - 1); // warning marker
  }

  /* Format */
  if (reasons[0])
    snprintf(out, outsz, "%s %s - %s", icons, name, reasons);
  else
    snprintf(out, outsz, "%s", name);
}

static const Snapshot *g_sort_snap = NULL;

static int class_rank(const char *cls) {
  if (!cls || !cls[0])
    return 999;

  if (!_stricmp(cls, "System"))
    return 10;
  if (!_stricmp(cls, "Display"))
    return 20;
  if (!_stricmp(cls, "Net"))
    return 30;
  if (!_stricmp(cls, "MEDIA"))
    return 40;
  if (!_stricmp(cls, "Bluetooth"))
    return 50;
  if (!_stricmp(cls, "USB"))
    return 60;
  if (!_stricmp(cls, "HDC"))
    return 70;
  if (!_stricmp(cls, "SCSIAdapter"))
    return 80;
  if (!_stricmp(cls, "Kernel"))
    return 90;
  if (!_stricmp(cls, "FSDriver"))
    return 100;

  return 500;
}

static const char *norm_class(const DriverInfo *d) {
  return (d && d->class_name[0]) ? d->class_name : "Other";
}

static int cmp_driver_idx(const void *a, const void *b) {
  int ia = *(const int *)a;
  int ib = *(const int *)b;

  const DriverInfo *da = &g_sort_snap->data.drivers[ia];
  const DriverInfo *db = &g_sort_snap->data.drivers[ib];

  int ra = class_rank(norm_class(da));
  int rb = class_rank(norm_class(db));
  if (ra != rb)
    return (ra < rb) ? -1 : 1;

  return _stricmp(driver_title(da), driver_title(db));
}

static void ui_group_bar(struct nk_context *ctx, float h) {
  struct nk_command_buffer *c = nk_window_get_canvas(ctx);
  struct nk_rect r;

  nk_layout_row_dynamic(ctx, h, 1);
  if (nk_widget(&r, ctx)) {
    nk_fill_rect(c, r, 0.0f, nk_rgb(55, 55, 60));
    nk_stroke_line(c, r.x, r.y + r.h - 1.0f, r.x + r.w, r.y + r.h - 1.0f, 1.0f,
                   nk_rgb(240, 240, 240));
  }
}

static void ui_drivers_list_content(struct nk_context *ctx,
                                    struct AppState *A) {
  nk_layout_row_dynamic(ctx, 18, 1);

  if (!A->drv_ready || A->drv_snap.data.drivers_count <= 0) {
    nk_label(ctx, "No data. Press \"Scan drivers\".", NK_TEXT_LEFT);
    return;
  }

  int n = A->drv_snap.data.drivers_count;
  if (n > DRV_MAX)
    n = DRV_MAX;

  int idx[DRV_MAX];
  for (int i = 0; i < n; ++i)
    idx[i] = i;

  g_sort_snap = &A->drv_snap;
  qsort(idx, (size_t)n, sizeof(idx[0]), cmp_driver_idx);

  const char *last_class = NULL;

  for (int k = 0; k < n; ++k) {
    int i = idx[k];
    const DriverInfo *d = &A->drv_snap.data.drivers[i];
    const char *cls = norm_class(d);

    if (!last_class || _stricmp(last_class, cls) != 0) {
      ui_group_bar(ctx, 6.0f);

      nk_layout_row_dynamic(ctx, 20, 1);
      nk_label(ctx, cls, NK_TEXT_LEFT);

      nk_layout_row_dynamic(ctx, 6, 1);
      nk_spacing(ctx, 1);

      last_class = cls;
    }

    uint32_t flags = g_drv_risk_flags[i];

    char reasons[96] = {0};
    if (flags & DRV_RISK_LOLDRIVERS)
      append_reason(reasons, sizeof(reasons), "LOLDrivers");
    if (flags & DRV_RISK_MS_BLOCKLIST)
      append_reason(reasons, sizeof(reasons), "Microsoft DB");
    if (flags & DRV_RISK_UNSIGNED)
      append_reason(reasons, sizeof(reasons), "Unsigned driver");

    char line[256];
    if (reasons[0])
      snprintf(line, sizeof(line), "%s - %s", driver_title(d), reasons);
    else
      snprintf(line, sizeof(line), "%s", driver_title(d));

    /* Header row: arrow + icon + text */
    nk_layout_row_template_begin(ctx, 22);
    nk_layout_row_template_push_static(ctx, 18);
    nk_layout_row_template_push_static(ctx, 22);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    /* Arrow */
    if (nk_button_symbol(ctx, open_state[i] ? NK_SYMBOL_TRIANGLE_DOWN
                                            : NK_SYMBOL_TRIANGLE_RIGHT))
      open_state[i] = !open_state[i];

    /* Icon */
    if (driver_is_danger(flags) && A->danger_ready) {
      nk_image(ctx, A->icon_danger);
    } else if (driver_is_warning(flags) && A->warn_ready) {
      nk_image(ctx, A->icon_warn);
    } else {
      nk_label(ctx, " ", NK_TEXT_CENTERED);
    }

    /* Text (clickable) */
    nk_selectable_label(ctx, line, NK_TEXT_LEFT, &open_state[i]);

    /* Details */
    if (open_state[i]) {
      nk_layout_row_dynamic(ctx, 18, 2);

      nk_label(ctx, "DriverFile", NK_TEXT_LEFT);
      nk_label(ctx, safe_str(d->driver_file), NK_TEXT_LEFT);

      nk_label(ctx, "Service", NK_TEXT_LEFT);
      nk_label(ctx, safe_str(d->service), NK_TEXT_LEFT);

      nk_label(ctx, "Class", NK_TEXT_LEFT);
      nk_label(ctx, safe_str(d->class_name), NK_TEXT_LEFT);

      nk_label(ctx, "Provider", NK_TEXT_LEFT);
      nk_label(ctx, safe_str(d->provider), NK_TEXT_LEFT);

      nk_label(ctx, "Version", NK_TEXT_LEFT);
      nk_label(ctx, safe_str(d->version), NK_TEXT_LEFT);

      nk_label(ctx, "Date", NK_TEXT_LEFT);
      nk_label(ctx, safe_str(d->date), NK_TEXT_LEFT);

      nk_label(ctx, "INF", NK_TEXT_LEFT);
      nk_label(ctx, safe_str(d->inf), NK_TEXT_LEFT);

      nk_label(ctx, "ImagePath", NK_TEXT_LEFT);
      nk_label(ctx, safe_str(d->image_path), NK_TEXT_LEFT);

      nk_label(ctx, "StartType", NK_TEXT_LEFT);
      nk_label(ctx, safe_str(d->start_type), NK_TEXT_LEFT);

      nk_label(ctx, "ServiceType", NK_TEXT_LEFT);
      nk_label(ctx, safe_str(d->service_type), NK_TEXT_LEFT);

      nk_label(ctx, "LoadOrderGroup", NK_TEXT_LEFT);
      nk_label(ctx, safe_str(d->load_order_group), NK_TEXT_LEFT);

      nk_label(ctx, "SHA256", NK_TEXT_LEFT);
      nk_label(ctx, safe_str(d->sha256), NK_TEXT_LEFT);

      nk_label(ctx, "Signed", NK_TEXT_LEFT);
      nk_label(ctx, d->is_signed ? "Yes" : "No", NK_TEXT_LEFT);

      nk_label(ctx, "Signer", NK_TEXT_LEFT);
      nk_label(ctx, safe_str(d->signer), NK_TEXT_LEFT);

      nk_layout_row_dynamic(ctx, 6, 1);
      nk_spacing(ctx, 1);
    }
  }
}

void ui_window_drivers(struct nk_context *ctx, struct AppState *A, int ww,
                       int wh) {
  if (!A->drv_win_open)
    return;

  float pad = 24.0f;
  struct nk_rect r =
      nk_rect(pad, pad, (float)ww - pad * 2.0f, (float)wh - pad * 2.0f);

  nk_flags flags = NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
                   NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR;

  if (!nk_begin(ctx, "Drivers", r, flags)) {
    nk_end(ctx);
    return;
  }
  nk_window_set_focus(ctx, "Drivers");

  struct nk_rect content = nk_window_get_content_region(ctx);

  nk_layout_row_template_begin(ctx, 26);
  nk_layout_row_template_push_dynamic(ctx);
  nk_layout_row_template_push_static(ctx, 30.0f);
  nk_layout_row_template_end(ctx);
  nk_label(ctx, "Drivers list", NK_TEXT_LEFT);

  if (nk_button_label(ctx, "X")) {
    A->drv_win_open = 0;
    nk_window_set_focus(ctx, "Root");
    nk_end(ctx);
    return;
  }


  nk_layout_row_dynamic(ctx, 18, 1);
  if (A->drv_ready) {
    char buf[80];
    snprintf(buf, sizeof(buf), "Total: %d", A->drv_snap.data.drivers_count);
    nk_label(ctx, buf, NK_TEXT_LEFT);
  } else {
    nk_label(ctx, "No data. Press \"Scan drivers\".", NK_TEXT_LEFT);
  }

  float list_h = content.h - 60.0f;
  if (list_h < 120.0f)
    list_h = 120.0f;

  int pushed = 0;
  if (A->font_cyr) {
    nk_style_push_font(ctx, &A->font_cyr->handle);
    pushed = 1;
  }

  nk_layout_row_dynamic(ctx, list_h, 1);
  if (nk_group_begin(ctx, "drivers_list_popup",
                     NK_WINDOW_BORDER | NK_WINDOW_SCROLL_AUTO_HIDE)) {
    ui_drivers_list_content(ctx, A);
    nk_group_end(ctx);
  }

  if (pushed)
    nk_style_pop_font(ctx);

  nk_end(ctx);
}
