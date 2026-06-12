#include "abyss.h"

void theme_abyss_apply(struct nk_context *ctx) {
    struct nk_color t[NK_COLOR_COUNT];

    t[NK_COLOR_TEXT]                    = nk_rgba(176, 208, 255, 255); /* bright steel-blue */
    t[NK_COLOR_WINDOW]                  = nk_rgba(  0,  12,  24, 255); /* #000C18 */
    t[NK_COLOR_HEADER]                  = nk_rgba(  0,  20,  40, 255);
    t[NK_COLOR_BORDER]                  = nk_rgba( 42,  90, 148, 255);
    t[NK_COLOR_BUTTON]                  = nk_rgba( 10,  38,  72, 255);
    t[NK_COLOR_BUTTON_HOVER]            = nk_rgba( 22,  62, 115, 255);
    t[NK_COLOR_BUTTON_ACTIVE]           = nk_rgba( 36,  85, 148, 255);
    t[NK_COLOR_TOGGLE]                  = nk_rgba( 10,  38,  72, 255);
    t[NK_COLOR_TOGGLE_HOVER]            = nk_rgba( 22,  62, 115, 255);
    t[NK_COLOR_TOGGLE_CURSOR]           = nk_rgba(  0, 210, 230, 255);
    t[NK_COLOR_SELECT]                  = nk_rgba( 10,  38,  72, 255);
    t[NK_COLOR_SELECT_ACTIVE]           = nk_rgba(  0, 210, 230, 255);
    t[NK_COLOR_SLIDER]                  = nk_rgba( 10,  38,  72, 255);
    t[NK_COLOR_SLIDER_CURSOR]           = nk_rgba(  0, 210, 230, 255);
    t[NK_COLOR_SLIDER_CURSOR_HOVER]     = nk_rgba(  0, 185, 205, 255);
    t[NK_COLOR_SLIDER_CURSOR_ACTIVE]    = nk_rgba(  0, 158, 175, 255);
    t[NK_COLOR_PROPERTY]                = nk_rgba(  0,  20,  40, 255);
    t[NK_COLOR_EDIT]                    = nk_rgba( 10,  38,  72, 255);
    t[NK_COLOR_EDIT_CURSOR]             = nk_rgba(176, 208, 255, 255);
    t[NK_COLOR_COMBO]                   = nk_rgba( 10,  38,  72, 255);
    t[NK_COLOR_CHART]                   = nk_rgba(  0,  20,  40, 255);
    t[NK_COLOR_CHART_COLOR]             = nk_rgba(  0, 210, 230, 255);
    t[NK_COLOR_CHART_COLOR_HIGHLIGHT]   = nk_rgba(  0, 158, 175, 255);
    t[NK_COLOR_SCROLLBAR]               = nk_rgba(  0,  20,  40, 255);
    t[NK_COLOR_SCROLLBAR_CURSOR]        = nk_rgba( 42,  90, 148, 255);
    t[NK_COLOR_SCROLLBAR_CURSOR_HOVER]  = nk_rgba( 60, 118, 185, 255);
    t[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba( 80, 148, 215, 255);
    t[NK_COLOR_TAB_HEADER]              = nk_rgba(  0,  20,  40, 255);

    nk_style_from_table(ctx, t);
}
