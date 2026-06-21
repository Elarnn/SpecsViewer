#pragma once

/* ── Server settings ──────────────────────────────────────────────────────── */
#define BENCH_SERVER_HOST  L"specsviewer.elarnn.workers.dev"
#define BENCH_API_KEY      "BENCH_API_KEY_PLACEHOLDER"

struct AppState;

void config_load(struct AppState *S);
void config_save(const struct AppState *S);
