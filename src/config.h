#pragma once

/* ── Server settings ──────────────────────────────────────────────────────── */
#define BENCH_SERVER_HOST  L"specsviewer.elarnn.workers.dev"
#define BENCH_API_KEY      "FlmjJIErK6WfzMRT3n2fmZNe2IqawPGKVa8jFKzRhr0="

struct AppState;

void config_load(struct AppState *S);
void config_save(const struct AppState *S);
