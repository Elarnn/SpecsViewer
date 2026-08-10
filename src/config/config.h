#pragma once

/* ── Server settings ──────────────────────────────────────────────────────── */
#define BENCH_SERVER_HOST  L"specsviewer.elarnn.workers.dev"

struct AppState;

void config_load(struct AppState *S);
void config_save(const struct AppState *S);
