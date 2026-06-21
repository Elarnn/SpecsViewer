#include "themes.h"
#include "dark_modern.h"
#include "default_grey.h"
#include "abyss.h"
#include "light.h"

void theme_apply_nk(struct nk_context *ctx, AppTheme t) {
    switch (t) {
        case THEME_DARK_MODERN:  theme_dark_modern_apply(ctx);  break;
        case THEME_DEFAULT_GREY: theme_default_grey_apply(ctx); break;
        case THEME_ABYSS:        theme_abyss_apply(ctx);        break;
        case THEME_LIGHT:        theme_light_apply(ctx);        break;
    }
}

void theme_get_clear_color(AppTheme t, float *r, float *g, float *b) {
    switch (t) {
        case THEME_DARK_MODERN:
            *r = THEME_DARK_MODERN_CLEAR_R;
            *g = THEME_DARK_MODERN_CLEAR_G;
            *b = THEME_DARK_MODERN_CLEAR_B;
            break;
        case THEME_DEFAULT_GREY:
            *r = THEME_DEFAULT_GREY_CLEAR_R;
            *g = THEME_DEFAULT_GREY_CLEAR_G;
            *b = THEME_DEFAULT_GREY_CLEAR_B;
            break;
        case THEME_ABYSS:
            *r = THEME_ABYSS_CLEAR_R;
            *g = THEME_ABYSS_CLEAR_G;
            *b = THEME_ABYSS_CLEAR_B;
            break;
        case THEME_LIGHT:
            *r = THEME_LIGHT_CLEAR_R;
            *g = THEME_LIGHT_CLEAR_G;
            *b = THEME_LIGHT_CLEAR_B;
            break;
    }
}

/* Returns the accent color for spec value text.
   alpha=0 means "use default text color" (nk_label_val falls back to nk_label). */
struct nk_color theme_get_value_color(AppTheme t) {
    switch (t) {
        case THEME_DARK_MODERN:  return nk_rgb(100, 181, 246);   /* dark modern blue accent */
        case THEME_DEFAULT_GREY: return nk_rgb(70, 130, 180);   /* default orange accent */
        case THEME_ABYSS:        return nk_rgb(0, 210, 230);   /* abyss cyan accent */
        case THEME_LIGHT:        return nk_rgb(0, 79, 132);   /* light blue accent */
        default:                 return nk_rgba(0, 0, 0, 0);   /* no override */
    }
}

unsigned int theme_get_caption_color(AppTheme t) {
    switch (t) {
        case THEME_DARK_MODERN:  return THEME_DARK_MODERN_CAPTION;
        case THEME_DEFAULT_GREY: return THEME_DEFAULT_GREY_CAPTION;
        case THEME_ABYSS:        return THEME_ABYSS_CAPTION;
        case THEME_LIGHT:        return THEME_LIGHT_CAPTION;
        default:                 return 0xFFFFFFFF;
    }
}
