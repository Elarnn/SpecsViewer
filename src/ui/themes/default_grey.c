#include "default_grey.h"

void theme_default_grey_apply(struct nk_context *ctx) {
    struct nk_color t[NK_COLOR_COUNT];

    t[NK_COLOR_TEXT]                    = nk_rgba(175, 175, 175, 255);
    t[NK_COLOR_WINDOW]                  = nk_rgba( 45,  45,  45, 255);
    t[NK_COLOR_HEADER]                  = nk_rgba( 40,  40,  40, 255);
    t[NK_COLOR_BORDER]                  = nk_rgba( 65,  65,  65, 255);
    t[NK_COLOR_BUTTON]                  = nk_rgba( 50,  50,  50, 255);
    t[NK_COLOR_BUTTON_HOVER]            = nk_rgba(150, 150, 150, 255);
    t[NK_COLOR_BUTTON_ACTIVE]           = nk_rgba(210, 210, 210, 255);
    t[NK_COLOR_TOGGLE]                  = nk_rgba(100, 100, 100, 255);
    t[NK_COLOR_TOGGLE_HOVER]            = nk_rgba(120, 120, 120, 255);
    t[NK_COLOR_TOGGLE_CURSOR]           = nk_rgba( 45,  45,  45, 255);
    t[NK_COLOR_SELECT]                  = nk_rgba( 45,  45,  45, 255);
    t[NK_COLOR_SELECT_ACTIVE]           = nk_rgba( 35,  35,  35, 255);
    t[NK_COLOR_SLIDER]                  = nk_rgba( 38,  38,  38, 255);
    t[NK_COLOR_SLIDER_CURSOR]           = nk_rgba(100, 100, 100, 255);
    t[NK_COLOR_SLIDER_CURSOR_HOVER]     = nk_rgba(120, 120, 120, 255);
    t[NK_COLOR_SLIDER_CURSOR_ACTIVE]    = nk_rgba(150, 150, 150, 255);
    t[NK_COLOR_PROPERTY]                = nk_rgba( 38,  38,  38, 255);
    t[NK_COLOR_EDIT]                    = nk_rgba( 38,  38,  38, 255);
    t[NK_COLOR_EDIT_CURSOR]             = nk_rgba(175, 175, 175, 255);
    t[NK_COLOR_COMBO]                   = nk_rgba( 45,  45,  45, 255);
    t[NK_COLOR_CHART]                   = nk_rgba( 38,  38,  38, 255);
    t[NK_COLOR_CHART_COLOR]             = nk_rgba(110, 110, 110, 255);
    t[NK_COLOR_CHART_COLOR_HIGHLIGHT]   = nk_rgba(200,  60,  60, 255);
    t[NK_COLOR_SCROLLBAR]               = nk_rgba( 40,  40,  40, 255);
    t[NK_COLOR_SCROLLBAR_CURSOR]        = nk_rgba(100, 100, 100, 255);
    t[NK_COLOR_SCROLLBAR_CURSOR_HOVER]  = nk_rgba(120, 120, 120, 255);
    t[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(150, 150, 150, 255);
    t[NK_COLOR_TAB_HEADER]              = nk_rgba( 40,  40,  40, 255);

    nk_style_from_table(ctx, t);

    ctx->style.button.text_normal = nk_rgb(188, 188, 188);
}
