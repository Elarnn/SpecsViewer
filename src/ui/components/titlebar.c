#include "app.h"
#include "ui.h"
#include "ui/nk_common.h"
#include "themes/themes.h"
#include "config.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>

/* --------------------------------------------------------- Win32 setup --- */

static WNDPROC g_orig_wndproc = NULL;

static LRESULT CALLBACK titlebar_wndproc(HWND hwnd, UINT msg,
                                         WPARAM wp, LPARAM lp) {
    if (msg == WM_NCHITTEST) {
        POINT pt = { (int)(short)LOWORD(lp), (int)(short)HIWORD(lp) };
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (pt.y >= 0 && pt.y < TITLEBAR_H &&
            pt.x >= TITLEBAR_LEFT_ZONE &&
            pt.x <  rc.right - TITLEBAR_BTN_W * 2 - 8)
            return HTCAPTION;
    }
    if (msg == WM_NCLBUTTONDBLCLK && wp == HTCAPTION)
        return 0;
    return CallWindowProcA(g_orig_wndproc, hwnd, msg, wp, lp);
}

void titlebar_win32_setup(GLFWwindow *win) {
    HWND hwnd = glfwGetWin32Window(win);

    HICON hBig   = (HICON)LoadImageA(GetModuleHandle(NULL), MAKEINTRESOURCE(1),
                                     IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    HICON hSmall = (HICON)LoadImageA(GetModuleHandle(NULL), MAKEINTRESOURCE(1),
                                     IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (hBig)   SendMessage(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hBig);
    if (hSmall) SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSmall);

    g_orig_wndproc = (WNDPROC)(LONG_PTR)
        SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)titlebar_wndproc);

    int corner = 3; /* DWMWCP_DONOTROUND */
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */,
                          &corner, sizeof(corner));
}

/* ------------------------------------------------------- Nuklear UI ----- */

static struct nk_style_button s_wnd_btn(struct nk_context *ctx) {
    struct nk_style_button b = ctx->style.button;
    b.border = 0;
    b.normal = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    /* hover/active inherited from theme — works correctly on all backgrounds */
    return b;
}

void ui_titlebar(struct nk_context *ctx, struct AppState *S) {
    /* Measure title text so it gets exactly the space it needs */
    const char *title_str = S->title[0] ? S->title : "SpecsViewer";
    float title_w = 160.0f;
    if (ctx->style.font)
        title_w = ctx->style.font->width(ctx->style.font->userdata,
                                         ctx->style.font->height,
                                         title_str, (int)nk_strlen(title_str))
                  + 14.0f; /* left+right padding */

    nk_layout_row_template_begin(ctx, TITLEBAR_H);
    nk_layout_row_template_push_static(ctx, title_w);           /* title */
    nk_layout_row_template_push_static(ctx, 8);                 /* separator */
    nk_layout_row_template_push_static(ctx, TITLEBAR_THEMES_W); /* "Themes" button */
    nk_layout_row_template_push_dynamic(ctx);                   /* drag spacer */
    nk_layout_row_template_push_static(ctx, TITLEBAR_BTN_W);    /* minimize */
    nk_layout_row_template_push_static(ctx, TITLEBAR_BTN_W);    /* close */
    nk_layout_row_template_end(ctx);

    nk_label(ctx, title_str, NK_TEXT_LEFT);

    /* vertical separator between title and Themes button */
    {
        struct nk_rect b;
        if (nk_widget(&b, ctx) != NK_WIDGET_INVALID) {
            float cx = (float)(int)(b.x + b.w * 0.5f);
            nk_fill_rect(nk_window_get_canvas(ctx),
                         nk_rect(cx, b.y + 5.0f, 1.0f, b.h - 10.0f),
                         0.0f, nk_rgb(75, 75, 75));
        }
    }

    /* Themes toggle button — highlighted when panel is open */
    struct nk_style_button theme_btn = s_wnd_btn(ctx);
    if (S->theme_panel_open) {
        theme_btn.normal = nk_style_item_color(nk_rgba(0, 122, 204, 180));
        theme_btn.hover  = nk_style_item_color(nk_rgba(0, 122, 204, 220));
    }
    if (nk_button_label_styled(ctx, &theme_btn, "Themes"))
        S->theme_panel_open = !S->theme_panel_open;

    /* fill the dynamic spacer slot — every template column needs a widget */
    nk_label(ctx, "", NK_TEXT_LEFT);

    struct nk_style_button btn = s_wnd_btn(ctx);
    if (nk_button_label_styled(ctx, &btn, "-"))
        glfwIconifyWindow(S->window);

    struct nk_style_button close_btn = s_wnd_btn(ctx);
    close_btn.hover  = nk_style_item_color(nk_rgba(196, 43, 28, 255));
    close_btn.active = nk_style_item_color(nk_rgba(220, 60, 40, 255));
    if (nk_button_label_styled(ctx, &close_btn, "X"))
        glfwSetWindowShouldClose(S->window, 1);
}

/* ------------------------------------------------------------------ panel -- */

#define THEME_PANEL_W 190.0f

static const UiShutterItem k_themes[] = {
    { "Dark Modern",  THEME_DARK_MODERN  },
    { "Default Grey", THEME_DEFAULT_GREY },
    { "Abyss",        THEME_ABYSS        },
    { "Light",        THEME_LIGHT        },
};

void ui_theme_panel(struct nk_context *ctx, struct AppState *S, int ww, int wh) {
    (void)ww; (void)wh;
    static double last_t = 0.0;

    /* anchor left edge of panel under the "Themes" button */
    const char *ts = S->title[0] ? S->title : "SpecsViewer";
    float tw = 160.0f;
    if (ctx->style.font)
        tw = ctx->style.font->width(ctx->style.font->userdata,
                                    ctx->style.font->height,
                                    ts, (int)nk_strlen(ts)) + 14.0f;
    float pdx      = ctx->style.window.padding.x;
    float spx      = ctx->style.window.spacing.x;
    float anchor_x = pdx + tw + spx + 8.0f + spx;

    int sel = ui_shutter(ctx, "ThemePanel", anchor_x, THEME_PANEL_W,
                         &S->theme_panel_open, &S->theme_panel_anim, &last_t,
                         "Themes", k_themes,
                         (int)(sizeof(k_themes) / sizeof(k_themes[0])),
                         S->active_theme);
    if (sel >= 0) {
        S->active_theme = sel;
        theme_apply_nk(ctx, (AppTheme)sel);
        config_save(S);
    }
}
