#include "dark_modern.h"

void theme_dark_modern_apply(struct nk_context *ctx) {
    struct nk_color t[NK_COLOR_COUNT];

    t[NK_COLOR_TEXT]                    = nk_rgba(204, 204, 204, 255);
    t[NK_COLOR_WINDOW]                  = nk_rgba( 30,  30,  30, 255);
    t[NK_COLOR_HEADER]                  = nk_rgba( 37,  37,  38, 255);
    t[NK_COLOR_BORDER]                  = nk_rgba( 69,  69,  69, 255);
    t[NK_COLOR_BUTTON]                  = nk_rgba( 45,  45,  45, 255);
    t[NK_COLOR_BUTTON_HOVER]            = nk_rgba( 60,  60,  60, 255);
    t[NK_COLOR_BUTTON_ACTIVE]           = nk_rgba( 69,  69,  69, 255);
    t[NK_COLOR_TOGGLE]                  = nk_rgba( 45,  45,  45, 255);
    t[NK_COLOR_TOGGLE_HOVER]            = nk_rgba( 60,  60,  60, 255);
    t[NK_COLOR_TOGGLE_CURSOR]           = nk_rgba(  0, 122, 204, 255);
    t[NK_COLOR_SELECT]                  = nk_rgba( 45,  45,  45, 255);
    t[NK_COLOR_SELECT_ACTIVE]           = nk_rgba(  0, 122, 204, 255);
    t[NK_COLOR_SLIDER]                  = nk_rgba( 45,  45,  45, 255);
    t[NK_COLOR_SLIDER_CURSOR]           = nk_rgba(  0, 122, 204, 255);
    t[NK_COLOR_SLIDER_CURSOR_HOVER]     = nk_rgba( 17, 119, 187, 255);
    t[NK_COLOR_SLIDER_CURSOR_ACTIVE]    = nk_rgba( 14,  99, 156, 255);
    t[NK_COLOR_PROPERTY]                = nk_rgba( 37,  37,  38, 255);
    t[NK_COLOR_EDIT]                    = nk_rgba( 60,  60,  60, 255);
    t[NK_COLOR_EDIT_CURSOR]             = nk_rgba(204, 204, 204, 255);
    t[NK_COLOR_COMBO]                   = nk_rgba( 45,  45,  45, 255);
    t[NK_COLOR_CHART]                   = nk_rgba( 37,  37,  38, 255);
    t[NK_COLOR_CHART_COLOR]             = nk_rgba(  0, 122, 204, 255);
    t[NK_COLOR_CHART_COLOR_HIGHLIGHT]   = nk_rgba( 14,  99, 156, 255);
    t[NK_COLOR_SCROLLBAR]               = nk_rgba( 37,  37,  38, 255);
    t[NK_COLOR_SCROLLBAR_CURSOR]        = nk_rgba( 69,  69,  69, 255);
    t[NK_COLOR_SCROLLBAR_CURSOR_HOVER]  = nk_rgba( 90,  90,  90, 255);
    t[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(106, 106, 106, 255);
    t[NK_COLOR_TAB_HEADER]              = nk_rgba( 37,  37,  38, 255);

    nk_style_from_table(ctx, t);
}
