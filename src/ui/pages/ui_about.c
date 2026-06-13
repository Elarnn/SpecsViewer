// clang-format off
#include "ui/nk_common.h"
#include "ui.h"
#include "app.h"
#include "app_version.h"
#include "update/updater_check.h"
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
// clang-format on

#define OPEN_URL(url)                                                          \
  ShellExecuteA(NULL, "open", (url), NULL, NULL, SW_SHOWNORMAL)

/* Strip **bold**, *italic* markers and replace out-of-range UTF-8 sequences */
static void strip_inline(const char *src, char *dst, int dst_sz) {
    int i = 0;
    const char *p = src;
    while (*p && i < dst_sz - 1) {
        /* ** bold marker */
        if (p[0] == '*' && p[1] == '*') { p += 2; continue; }
        /* em dash U+2014 (0xE2 0x80 0x94) → - */
        if ((unsigned char)p[0] == 0xE2 &&
            (unsigned char)p[1] == 0x80 &&
            (unsigned char)p[2] == 0x94) { dst[i++] = '-'; p += 3; continue; }
        /* en dash U+2013 (0xE2 0x80 0x93) → - */
        if ((unsigned char)p[0] == 0xE2 &&
            (unsigned char)p[1] == 0x80 &&
            (unsigned char)p[2] == 0x93) { dst[i++] = '-'; p += 3; continue; }
        dst[i++] = *p++;
    }
    dst[i] = '\0';
}

static void render_markdown(struct nk_context *ctx, struct AppState *S,
                            const char *text) {
    char line[512];
    const char *p = text;

    while (*p) {
        const char *end = strchr(p, '\n');
        int len = end ? (int)(end - p) : (int)strlen(p);
        if (len > (int)sizeof(line) - 1) len = (int)sizeof(line) - 1;
        memcpy(line, p, len);
        if (len > 0 && line[len - 1] == '\r') len--;
        line[len] = '\0';
        p = end ? end + 1 : p + strlen(p);

        if (len == 0) {
            nk_layout_row_dynamic(ctx, 8, 1);
            nk_spacing(ctx, 1);
        } else if (strncmp(line, "### ", 4) == 0) {
            nk_layout_row_dynamic(ctx, 26, 1);
            nk_style_push_font(ctx, &S->font_subheader->handle);
            nk_label(ctx, line + 4, NK_TEXT_LEFT);
            nk_style_pop_font(ctx);
        } else if (strncmp(line, "## ", 3) == 0) {
            nk_layout_row_dynamic(ctx, 30, 1);
            nk_style_push_font(ctx, &S->font_subheader->handle);
            nk_label(ctx, line + 3, NK_TEXT_LEFT);
            nk_style_pop_font(ctx);
        } else if (strncmp(line, "# ", 2) == 0) {
            nk_layout_row_dynamic(ctx, 34, 1);
            nk_style_push_font(ctx, &S->font_header->handle);
            nk_label(ctx, line + 2, NK_TEXT_LEFT);
            nk_style_pop_font(ctx);
        } else if (strncmp(line, "- ", 2) == 0 || strncmp(line, "* ", 2) == 0) {
            char cleaned[512]; strip_inline(line + 2, cleaned, sizeof(cleaned));
            char bullet[520];
            snprintf(bullet, sizeof(bullet), "  \xC2\xB7 %s", cleaned);
            nk_layout_row_dynamic(ctx, 24, 1);
            nk_style_push_font(ctx, &S->font_cyr->handle);
            nk_label(ctx, bullet, NK_TEXT_LEFT);
            nk_style_pop_font(ctx);
        } else {
            char cleaned[512]; strip_inline(line, cleaned, sizeof(cleaned));
            nk_layout_row_dynamic(ctx, 24, 1);
            nk_style_push_font(ctx, &S->font_cyr->handle);
            nk_label(ctx, cleaned, NK_TEXT_LEFT);
            nk_style_pop_font(ctx);
        }
    }
}

static void push_github_btn_style(struct nk_context *ctx,
                                  struct nk_style_button *out) {
  *out = ctx->style.button;
  /* detect light background by window luminance */
  struct nk_color win = ctx->style.window.background;
  int is_light = ((int)win.r + (int)win.g + (int)win.b) > 380;
  ctx->style.button.normal = nk_style_item_color(nk_rgba(0, 0, 0, 0));
  ctx->style.button.hover  = nk_style_item_color(
      is_light ? nk_rgba(0, 0, 0, 45) : nk_rgba(255, 255, 255, 40));
  ctx->style.button.active = nk_style_item_color(
      is_light ? nk_rgba(0, 0, 0, 75) : nk_rgba(255, 255, 255, 70));
  ctx->style.button.border_color = nk_rgba(0, 0, 0, 0);
  ctx->style.button.border = 0;
  ctx->style.button.padding = nk_vec2(0, 0);
  ctx->style.button.image_padding = nk_vec2(0, 0);
  ctx->style.button.touch_padding = nk_vec2(0, 0);
  ctx->style.button.rounding = 6;
}

void ui_page_about(struct nk_context *ctx, struct AppState *S) {
  const float icon_sz  = 100.0f;
  const float text_h   = 26.0f;
  const float gap      = 8.0f;
  const float block_h  = text_h + gap + icon_sz;

  const float total_h  = nk_window_get_content_region(ctx).h;
  const float spacing  = ctx->style.window.spacing.y;

  const float g1_h = 88.0f;
  const float g3_h = 155.0f;
  const float g4_h = 50.0f;
  float g2_h = total_h - g1_h - g3_h - g4_h - 3.0f * spacing;
  if (g2_h < 30.0f) g2_h = 30.0f;

  /* ── Group 1: build info ─────────────────────────────────────────────────── */
  nk_layout_row_dynamic(ctx, g1_h, 1);
  if (nk_group_begin(ctx, "About",
                     NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {

    nk_layout_row_dynamic(ctx, 26, 1);
    nk_label(ctx, "Author: Oleksandr Fadyeyev", NK_TEXT_CENTERED);

    char version[64];
    snprintf(version, sizeof(version), "Version: %s", SPECSVIEWER_VERSION);
    nk_label(ctx, version, NK_TEXT_CENTERED);

    nk_group_end(ctx);
  }

  /* ── Group 2: changelog ──────────────────────────────────────────────────── */
  nk_layout_row_dynamic(ctx, g2_h, 1);
  if (nk_group_begin(ctx, "Changelog", NK_WINDOW_BORDER)) {
    int ustate = (int)S->updater.state;
    if (ustate == UPDATE_CHECKING || ustate == UPDATE_IDLE) {
      nk_layout_row_dynamic(ctx, 26, 1);
      nk_style_push_font(ctx, &S->font_header->handle);
      nk_label(ctx, "Loading changelog...", NK_TEXT_LEFT);
      nk_style_pop_font(ctx);
    } else if (ustate == UPDATE_ERROR) {
      nk_layout_row_dynamic(ctx, 26, 1);
      nk_style_push_font(ctx, &S->font_header->handle);
      nk_label(ctx, "Failed to load changelog.", NK_TEXT_LEFT);
      nk_style_pop_font(ctx);
    } else if (S->updater.release_body[0]) {
      render_markdown(ctx, S, S->updater.release_body);
    } else {
      nk_layout_row_dynamic(ctx, 26, 1);
      nk_style_push_font(ctx, &S->font_header->handle);
      nk_label(ctx, "No changelog available.", NK_TEXT_LEFT);
      nk_style_pop_font(ctx);
    }
    nk_group_end(ctx);
  }

  /* ── Group 3: links / icons (pinned to bottom) ───────────────────────────── */
  nk_layout_row_dynamic(ctx, g3_h, 1);
  if (nk_group_begin(ctx, "Links",
                     NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {

    nk_layout_row_dynamic(ctx, 1, 1);
    float W    = nk_widget_width(ctx);
    float half = W * 0.5f;
    nk_spacing(ctx, 1);

    nk_layout_space_begin(ctx, NK_STATIC, block_h, 4);

    nk_layout_space_push(ctx, nk_rect(0, 0, half, text_h));
    nk_label(ctx, "More infos and last updates at Github", NK_TEXT_CENTERED);

    float gh_x = (half - icon_sz) * 0.5f;
    nk_layout_space_push(ctx, nk_rect(gh_x, text_h + gap, icon_sz, icon_sz));
    if (S->github_ready) {
      struct nk_style_button saved;
      push_github_btn_style(ctx, &saved);
      if (nk_button_image(ctx, S->icon_github))
        OPEN_URL("https://github.com/Elarnn");
      ctx->style.button = saved;
    } else {
      nk_spacing(ctx, 1);
    }

    nk_layout_space_push(ctx, nk_rect(half, 0, half, text_h));
    nk_label(ctx, "Designed for Windows x64", NK_TEXT_CENTERED);

    float win_x = half + (half - icon_sz) * 0.5f;
    nk_layout_space_push(ctx, nk_rect(win_x, text_h + gap, icon_sz, icon_sz));
    if (S->win_ready)
      nk_image(ctx, S->icon_win);
    else
      nk_spacing(ctx, 1);

    nk_layout_space_end(ctx);
    nk_group_end(ctx);
  }

  /* ── Group 4: check for updates ─────────────────────────────────────────── */
  nk_layout_row_dynamic(ctx, g4_h, 1);
  if (nk_group_begin(ctx, "UpdateCheck",
                     NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {

    int ustate = (int)S->updater.state;
    nk_layout_row_dynamic(ctx, 30, 1);

    if (ustate == UPDATE_AVAILABLE) {
      char lbl[80];
      snprintf(lbl, sizeof(lbl), "Version %s available - Click to update",
               S->updater.latest_ver);
      if (nk_button_label(ctx, lbl)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Version %s is available. Update now?",
                 S->updater.latest_ver);
        if (MessageBoxA(NULL, msg, "Update Available",
                        MB_YESNO | MB_ICONINFORMATION) == IDYES)
          update_launch_updater(&S->updater);
      }
    } else if (ustate == UPDATE_DL_UPDATER) {
      nk_label(ctx, "Downloading updater...", NK_TEXT_CENTERED);
    } else if (ustate == UPDATE_DL_ERROR) {
      char lbl[320];
      snprintf(lbl, sizeof(lbl), "%.250s - Retry", S->updater.dl_error);
      if (nk_button_label(ctx, lbl)) {
        update_cleanup(&S->updater);
        update_launch_updater(&S->updater);
      }
    } else {
      if (nk_button_label(ctx, "Check for Updates")) {
        MessageBoxA(NULL,
                    "You are running the latest version of SpecsViewer.\n"
                    "No updates are available at this time.",
                    "Up to Date", MB_OK | MB_ICONINFORMATION);
      }
    }

    nk_group_end(ctx);
  }
}

