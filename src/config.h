#pragma once

/* ── Server settings ──────────────────────────────────────────────────────── */
#define BENCH_SERVER_HOST  L"specsviewer.elarnn.workers.dev"
#include "config.secret.h"

struct AppState;

void config_load(struct AppState *S);
void config_save(const struct AppState *S);
