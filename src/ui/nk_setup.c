#include "app.h"
#include "nk_common.h"
#include "themes/themes.h"

#define MAX_VERTEX_BUFFER  (512 * 1024)
#define MAX_ELEMENT_BUFFER (128 * 1024)

void nk_setup_init(struct AppState *S) {
    S->ctx = nk_glfw3_init(&S->nkglfw, S->window, NK_GLFW3_INSTALL_CALLBACKS);

    struct nk_font_atlas *atlas = NULL;
    nk_glfw3_font_stash_begin(&S->nkglfw, &atlas);

    S->font_main = nk_font_atlas_add_default(atlas, 17.0f, NULL);

    static const nk_rune ranges_cyr[] = {
        0x0020, 0x00FF, /* Latin + basic symbols */
        0x0400, 0x052F, /* Cyrillic              */
        0
    };
    struct nk_font_config cfg = nk_font_config(0);
    cfg.range = ranges_cyr;

    S->font_cyr       = nk_font_atlas_add_from_file(atlas, "resources\\fonts\\segoeui.ttf", 22.0f, &cfg);
    S->font_subheader = nk_font_atlas_add_from_file(atlas, "resources\\fonts\\segoeui.ttf", 25.0f, &cfg);
    S->font_header    = nk_font_atlas_add_from_file(atlas, "resources\\fonts\\segoeui.ttf", 30.0f, &cfg);

    nk_glfw3_font_stash_end(&S->nkglfw);

    if (S->font_main) nk_style_set_font(S->ctx, &S->font_main->handle);

    if (!S->font_cyr)       S->font_cyr       = S->font_main;
    if (!S->font_subheader) S->font_subheader  = S->font_cyr;
    if (!S->font_header)    S->font_header     = S->font_subheader;

    S->active_theme     = (int)ACTIVE_THEME;
    S->theme_panel_open = 0;
    S->theme_panel_anim = 0.0f;
    theme_apply_nk(S->ctx, (AppTheme)S->active_theme);
}

void nk_setup_render(struct AppState *S) {
    int fbw, fbh;
    glfwGetFramebufferSize(S->window, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);

    float cr, cg, cb;
    theme_get_clear_color((AppTheme)S->active_theme, &cr, &cg, &cb);
    glClearColor(cr, cg, cb, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    nk_glfw3_render(&S->nkglfw, NK_ANTI_ALIASING_ON,
                    MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
    glfwSwapBuffers(S->window);
}

void nk_setup_shutdown(struct AppState *S) {
    nk_glfw3_shutdown(&S->nkglfw);
}
