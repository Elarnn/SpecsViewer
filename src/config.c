#include "config.h"
#include "app.h"
#include "ui/themes/themes.h"
#include <stdio.h>
#include <windows.h>

static void build_paths(char *out_dir, char *out_file, size_t sz) {
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    char *sep = strrchr(exe, '\\');
    if (sep) *sep = '\0';
    snprintf(out_dir,  sz, "%s\\config",              exe);
    snprintf(out_file, sz, "%s\\config\\settings.cfg", exe);
}

void config_load(struct AppState *S) {
    char dir[MAX_PATH], path[MAX_PATH];
    build_paths(dir, path, MAX_PATH);

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[64];
    while (fgets(line, sizeof(line), f)) {
        int v;
        if (sscanf(line, "theme=%d",           &v) == 1 &&
            v >= 0 && v <= THEME_LIGHT) {
            S->active_theme = v;
            theme_apply_nk(S->ctx, (AppTheme)v);
        }
        if (sscanf(line, "dont_ask_update=%d", &v) == 1)
            S->update_dont_ask = v ? 1 : 0;
        if (sscanf(line, "dont_ask_usermode=%d", &v) == 1)
            S->usermode_warn_dont_ask = v ? 1 : 0;
    }
    fclose(f);
}

void config_save(const struct AppState *S) {
    char dir[MAX_PATH], path[MAX_PATH];
    build_paths(dir, path, MAX_PATH);

    CreateDirectoryA(dir, NULL);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "theme=%d\n",              S->active_theme);
    fprintf(f, "dont_ask_update=%d\n",   S->update_dont_ask);
    fprintf(f, "dont_ask_usermode=%d\n", S->usermode_warn_dont_ask);
    fclose(f);
}
