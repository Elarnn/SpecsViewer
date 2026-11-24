// ui_device_select.c
// Switching the active CPU / GPU when several are present.
//
// The device name on the CPU / GPU page is drawn as a clickable field with a
// small arrow (ui_device_name_dropdown). Clicking it unfolds a dropdown list
// right under the name (ui_window_device_select, rendered after the Root
// window so it is drawn on top of the page). Both are used only when the
// backend detected more than one device of that kind.

#include "app.h"
#include "config.h"
#include "ui.h"
#include "ui/nk_common.h"
#include <stdio.h>

#define DEVSEL_MAX_ITEMS \
  (MAX_CPU_DEVICES > MAX_GPU_DEVICES ? MAX_CPU_DEVICES : MAX_GPU_DEVICES)
#define DEVSEL_PANEL_MIN_W 300.0f

/* ── Clickable name + arrow ─────────────────────────────────────────────── */

void ui_device_name_dropdown(struct nk_context *ctx, struct AppState *S,
                             int kind, const char *name, struct nk_color vc) {
  const int   open = (S->devsel_open == kind);
  const char *text = (name && name[0]) ? name : "-";
  struct nk_color tc = vc.a ? vc : ctx->style.text.color;

  /* looks like a plain value label: no background in any state, the arrow
     is the only hint (down = list closed, up = list open) */
  struct nk_style_button st = ctx->style.button;
  st.normal         = nk_style_item_color(nk_rgba(0, 0, 0, 0));
  st.hover          = st.normal;
  st.active         = st.normal;
  st.border         = 0.0f;
  st.text_normal    = tc;
  st.text_hover     = tc;
  st.text_active    = tc;
  st.text_alignment = NK_TEXT_CENTERED;

  struct nk_rect b = nk_widget_bounds(ctx);
  if (nk_button_label_styled(ctx, &st, text)) {
    S->devsel_open   = open ? DEVSEL_NONE : kind;
    S->devsel_anchor = b;
  }
  if (S->devsel_open == kind)
    S->devsel_anchor = b; /* follow the layout (e.g. window resize) */

  /* arrow right after the centered text: down = closed, up = open */
  const struct nk_user_font *f = ctx->style.font;
  float tw = f->width(f->userdata, f->height, text, nk_strlen(text));
  float s  = f->height * 0.28f; /* half width of the triangle */
  float cx = b.x + b.w * 0.5f + tw * 0.5f + 10.0f + s;
  float cx_max = b.x + b.w - s - 6.0f;
  if (cx > cx_max)
    cx = cx_max;
  float cy = b.y + b.h * 0.5f;
  float hy = s * 0.6f;

  struct nk_command_buffer *out = nk_window_get_canvas(ctx);
  if (open)
    nk_fill_triangle(out, cx - s, cy + hy, cx + s, cy + hy, cx, cy - hy, tc);
  else
    nk_fill_triangle(out, cx - s, cy - hy, cx + s, cy - hy, cx, cy + hy, tc);
}

/* ── Dropdown list ──────────────────────────────────────────────────────── */

void ui_window_device_select(struct nk_context *ctx, struct AppState *S, int ww,
                             int wh) {
  (void)wh;
  static double last_t    = 0.0;
  static int    last_kind = DEVSEL_NONE;
  static char   labels[DEVSEL_MAX_ITEMS][192];
  UiShutterItem items[DEVSEL_MAX_ITEMS];

  /* the dropdown belongs to the page it was opened from */
  if ((S->devsel_open == DEVSEL_CPU && S->active != TAB_CPU) ||
      (S->devsel_open == DEVSEL_GPU && S->active != TAB_GPU))
    S->devsel_open = DEVSEL_NONE;

  /* while folding back (open == NONE, anim > 0) keep drawing the last list */
  if (S->devsel_open != DEVSEL_NONE)
    last_kind = S->devsel_open;
  if (last_kind == DEVSEL_NONE)
    return;

  const int         is_cpu = (last_kind == DEVSEL_CPU);
  const DeviceList *d      = &S->snap.data.devices;
  int n   = is_cpu ? d->cpu_count : d->gpu_count;
  int sel = is_cpu ? d->cpu_sel   : d->gpu_sel;
  if (n > DEVSEL_MAX_ITEMS)
    n = DEVSEL_MAX_ITEMS;

  for (int i = 0; i < n; i++) {
    const char *name = is_cpu ? d->cpus[i].model : d->gpus[i].name;
    const char *note = (!is_cpu && !d->gpus[i].is_nvidia) ? "   (basic info)" : "";
    snprintf(labels[i], sizeof(labels[i]), "#%d   %s%s", i + 1,
             name[0] ? name : "Unknown", note);
    items[i].label = labels[i];
    items[i].id    = i;
  }

  /* unfold right under the name field, at least DEVSEL_PANEL_MIN_W wide */
  struct nk_rect a = S->devsel_anchor;
  float px = a.x;
  float py = a.y + a.h + 2.0f;
  float pw = a.w;
  if (pw < DEVSEL_PANEL_MIN_W)
    pw = DEVSEL_PANEL_MIN_W;
  if (px + pw > (float)ww - 4.0f)
    px = (float)ww - 4.0f - pw;
  if (px < 4.0f) {
    px = 4.0f;
    pw = (float)ww - 8.0f;
  }

  int picked = ui_shutter(ctx, "DeviceSelect", px, py, pw, a,
                          &S->devsel_open, &S->devsel_anim, &last_t,
                          is_cpu ? "Select processor" : "Select graphics card",
                          items, n, sel);

  if (picked >= 0) {
    if (is_cpu) {
      backend_select_cpu(&S->backend, picked);
      S->pref_cpu_sel = picked;
    } else {
      backend_select_gpu(&S->backend, picked);
      S->pref_gpu_sel = picked;
    }
    config_save(S);
  }
}
