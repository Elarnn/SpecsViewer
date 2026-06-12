#include "app.h"
#include "ui.h"
#include "ui/nk_common.h"
#include <GLFW/glfw3.h>

#define SHUTTER_SPEED 14.0f

int ui_shutter(struct nk_context   *ctx,
               const char          *panel_id,
               float                anchor_x,
               float                panel_w,
               int                 *open,
               float               *anim,
               double              *last_t,
               const char          *header,
               const UiShutterItem *items,
               int                  n_items,
               int                  active_id) {
    /* animation */
    double now = glfwGetTime();
    float  dt  = *last_t > 0.0 ? (float)(now - *last_t) : 0.0f;
    *last_t    = now;

    if (*open)
        *anim += dt * SHUTTER_SPEED;
    else
        *anim -= dt * SHUTTER_SPEED;

    if (*anim > 1.0f) *anim = 1.0f;
    if (*anim < 0.0f) *anim = 0.0f;

    if (*anim <= 0.0f)
        return -1;

    /* geometry */
    float spy      = ctx->style.window.spacing.y;
    float pdy      = ctx->style.window.padding.y;
    float target_h = 2.0f * pdy + 2.0f
                   + (22.0f + spy)
                   + (4.0f  + spy)
                   + (float)n_items * (30.0f + spy);

    float anim_h = target_h * (*anim);
    if (anim_h < 4.0f)
        return -1;

    float py = (float)TITLEBAR_H;

    /* close on outside click (ignore clicks inside the titlebar — handled by toggle button) */
    if (*open
        && nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT)
        && ctx->input.mouse.pos.y >= (float)TITLEBAR_H
        && !nk_input_is_mouse_hovering_rect(&ctx->input,
               nk_rect(anchor_x, py, panel_w, target_h)))
        *open = 0;

    /* render */
    int selected = -1;
    if (nk_begin(ctx, panel_id,
                 nk_rect(anchor_x, py, panel_w, anim_h),
                 NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {

        nk_layout_row_dynamic(ctx, 22, 1);
        nk_label(ctx, header, NK_TEXT_CENTERED);

        nk_layout_row_dynamic(ctx, 4, 1);
        nk_rule_horizontal(ctx, nk_rgb(69, 69, 69), nk_false);

        for (int i = 0; i < n_items; ++i) {
            nk_layout_row_dynamic(ctx, 30, 1);
            int is_active = (active_id == items[i].id);
            struct nk_style_button st = ctx->style.button;
            if (is_active) {
                st.border       = 2.0f;
                st.border_color = nk_rgb(0, 122, 204);
                st.normal       = nk_style_item_color(nk_rgba(0, 122, 204, 40));
            }
            if (nk_button_label_styled(ctx, &st, items[i].label) && !is_active) {
                selected  = items[i].id;
                *open     = 0;
            }
        }
    }
    nk_end(ctx);
    return selected;
}
