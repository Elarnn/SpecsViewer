#pragma once
#include "ui/nk_common.h"

typedef enum {
    THEME_DEFAULT_GREY = 0,
    THEME_DARK_MODERN  = 1,
    THEME_ABYSS        = 2,
    THEME_LIGHT        = 3,
} AppTheme;

/* Change this one line to switch the active theme */
#define ACTIVE_THEME THEME_DEFAULT_GREY

void             theme_apply_nk(struct nk_context *ctx, AppTheme t);
void             theme_get_clear_color(AppTheme t, float *r, float *g, float *b);
unsigned int     theme_get_caption_color(AppTheme t);
struct nk_color  theme_get_value_color(AppTheme t);
