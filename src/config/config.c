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
        if (sscanf(line, "cpu_sel=%d", &v) == 1 &&
            v >= 0 && v < MAX_CPU_DEVICES)
            S->pref_cpu_sel = v;
        if (sscanf(line, "gpu_sel=%d", &v) == 1 &&
            v >= 0 && v < MAX_GPU_DEVICES)
            S->pref_gpu_sel = v;
        if (sscanf(line, "stab_test=%d", &v) == 1 && v >= 0 && v < STAB_TEST_COUNT)
            S->stab_test_sel = v;
        if (sscanf(line, "stab_duration=%d", &v) == 1 && v >= 0 && v < 16)
            S->stab_dur_sel = v;
        if (sscanf(line, "stab_custom_min=%d", &v) == 1 && v >= 1 && v <= 99999)
            S->stab_custom_min = v;
        if (sscanf(line, "stab_stop_on_error=%d", &v) == 1)
            S->stab_stop_on_error = v ? 1 : 0;
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
    fprintf(f, "cpu_sel=%d\n",           S->pref_cpu_sel);
    fprintf(f, "gpu_sel=%d\n",           S->pref_gpu_sel);
    fprintf(f, "stab_test=%d\n",         S->stab_test_sel);
    fprintf(f, "stab_duration=%d\n",     S->stab_dur_sel);
    fprintf(f, "stab_custom_min=%d\n",   S->stab_custom_min);
    fprintf(f, "stab_stop_on_error=%d\n", S->stab_stop_on_error ? 1 : 0);
    fclose(f);
}
