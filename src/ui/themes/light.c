#include "light.h"

void theme_light_apply(struct nk_context *ctx) {
    struct nk_color t[NK_COLOR_COUNT];

    t[NK_COLOR_TEXT]                    = nk_rgba( 30,  30,  30, 255);
    t[NK_COLOR_WINDOW]                  = nk_rgba(242, 242, 242, 255); /* #F2F2F2 */
    t[NK_COLOR_HEADER]                  = nk_rgba(225, 225, 225, 255);
    t[NK_COLOR_BORDER]                  = nk_rgba(180, 180, 180, 255);
    t[NK_COLOR_BUTTON]                  = nk_rgba(220, 220, 220, 255);
    t[NK_COLOR_BUTTON_HOVER]            = nk_rgba(200, 200, 200, 255);
    t[NK_COLOR_BUTTON_ACTIVE]           = nk_rgba(175, 175, 175, 255);
    t[NK_COLOR_TOGGLE]                  = nk_rgba(200, 200, 200, 255);
    t[NK_COLOR_TOGGLE_HOVER]            = nk_rgba(180, 180, 180, 255);
    t[NK_COLOR_TOGGLE_CURSOR]           = nk_rgba(  0, 120, 215, 255); /* Windows blue */
    t[NK_COLOR_SELECT]                  = nk_rgba(220, 220, 220, 255);
    t[NK_COLOR_SELECT_ACTIVE]           = nk_rgba(  0, 120, 215, 255);
    t[NK_COLOR_SLIDER]                  = nk_rgba(210, 210, 210, 255);
    t[NK_COLOR_SLIDER_CURSOR]           = nk_rgba(  0, 120, 215, 255);
    t[NK_COLOR_SLIDER_CURSOR_HOVER]     = nk_rgba(  0,  99, 177, 255);
    t[NK_COLOR_SLIDER_CURSOR_ACTIVE]    = nk_rgba(  0,  80, 145, 255);
    t[NK_COLOR_PROPERTY]                = nk_rgba(225, 225, 225, 255);
    t[NK_COLOR_EDIT]                    = nk_rgba(255, 255, 255, 255);
    t[NK_COLOR_EDIT_CURSOR]             = nk_rgba( 30,  30,  30, 255);
    t[NK_COLOR_COMBO]                   = nk_rgba(220, 220, 220, 255);
    t[NK_COLOR_CHART]                   = nk_rgba(235, 235, 235, 255);
    t[NK_COLOR_CHART_COLOR]             = nk_rgba(  0, 120, 215, 255);
    t[NK_COLOR_CHART_COLOR_HIGHLIGHT]   = nk_rgba(  0,  80, 145, 255);
    t[NK_COLOR_SCROLLBAR]               = nk_rgba(230, 230, 230, 255);
    t[NK_COLOR_SCROLLBAR_CURSOR]        = nk_rgba(170, 170, 170, 255);
    t[NK_COLOR_SCROLLBAR_CURSOR_HOVER]  = nk_rgba(140, 140, 140, 255);
    t[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(110, 110, 110, 255);
    t[NK_COLOR_TAB_HEADER]              = nk_rgba(225, 225, 225, 255);

    nk_style_from_table(ctx, t);

    /* button text must be dark on light background */
    ctx->style.button.text_normal = nk_rgb( 30,  30,  30);
    ctx->style.button.text_hover  = nk_rgb( 20,  20,  20);
    ctx->style.button.text_active = nk_rgb(  0,   0,   0);
}
