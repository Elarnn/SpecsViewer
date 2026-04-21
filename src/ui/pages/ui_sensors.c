#include "app.h"
#include "stdio.h"
#include "string.h"
#include "ui.h"
#include "ui/nk_common.h"
#include <GLFW/glfw3.h>

#define RT_GRAPH_HISTORY_COUNT 120
#define RT_GRAPH_UPDATE_INTERVAL 0.5f
#define CPU_GROUP_HEIGHT 370
#define GPU_GROUP_HEIGHT 410
#define RAM_GROUP_HEIGHT 210

// --- forward declarations ---

struct RtGraph;

static void rt_graph_clamp_0_100(float *value);
static void rt_graph_update_slow(struct RtGraph *g, float value,
                                 double interval_sec);
static void ui_draw_rt_graph(struct nk_context *ctx, const struct RtGraph *g,
                             float min_value, float max_value,
                             const char *suffix,
                             struct nk_color (*color_fn)(float));
struct nk_color rt_graph_segment_color(float value);
struct nk_color rt_graph_color_temp(float value);
static void ui_space(struct nk_context *ctx) {
  nk_layout_row_dynamic(ctx, 10, 1);
  nk_spacer(ctx);
}

struct RtGraph {
  float values[RT_GRAPH_HISTORY_COUNT];
  int head;
  int initialized;
  double last_update;
};

void ui_page_sensors(struct nk_context *ctx, const struct AppState *S) {
  char buf[128];

  static struct RtGraph cpu_graph      = {0};
  static struct RtGraph cpu_temp_graph = {0};
  static struct RtGraph ram_graph      = {0};
  static struct RtGraph gpu_graph      = {0};
  static struct RtGraph gpu_temp_graph = {0};

  nk_layout_row_dynamic(ctx, 500, 1);
  if (nk_group_begin(ctx, "Wrapper", 0)) {

    // ------------------------------------------------------------------ CPU --
    nk_layout_row_dynamic(ctx, CPU_GROUP_HEIGHT, 1);
    if (nk_group_begin(ctx, "Processor",
                       NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE)) {

      float cpu_load = (float)S->snap.cpu_rt.load;
      rt_graph_clamp_0_100(&cpu_load);
      rt_graph_update_slow(&cpu_graph, cpu_load, RT_GRAPH_UPDATE_INTERVAL);

      ui_space(ctx);

      nk_layout_row_template_begin(ctx, 140);
      nk_layout_row_template_push_static(ctx, 100);
      nk_layout_row_template_push_static(ctx, 80);
      nk_layout_row_template_push_dynamic(ctx);
      nk_layout_row_template_end(ctx);

      nk_label(ctx, "CPU Load:", NK_TEXT_RIGHT);
      snprintf(buf, sizeof(buf), "%.1f %%", cpu_load);
      nk_label(ctx, buf, NK_TEXT_CENTERED);
      ui_draw_rt_graph(ctx, &cpu_graph, 0.0f, 100.0f, "%",
                       rt_graph_segment_color);

      ui_space(ctx);
      ui_space(ctx);

      // --- CPU temperature graph ---
      float cpu_temp = (float)S->snap.cpu_rt.cpu_temp;
      if (cpu_temp < 20.0f) cpu_temp = 20.0f;
      if (cpu_temp > 110.0f) cpu_temp = 110.0f;
      rt_graph_update_slow(&cpu_temp_graph, cpu_temp, RT_GRAPH_UPDATE_INTERVAL);

      nk_layout_row_template_begin(ctx, 140);
      nk_layout_row_template_push_static(ctx, 100);
      nk_layout_row_template_push_static(ctx, 80);
      nk_layout_row_template_push_dynamic(ctx);
      nk_layout_row_template_end(ctx);

      nk_label(ctx, "CPU Temp:", NK_TEXT_RIGHT);
      if (S->snap.cpu_rt.cpu_temp >= 0) {
        snprintf(buf, sizeof(buf), "%d °C", S->snap.cpu_rt.cpu_temp);
        nk_label(ctx, buf, NK_TEXT_CENTERED);
      } else {
        nk_label(ctx, "N/A", NK_TEXT_CENTERED);
      }
      ui_draw_rt_graph(ctx, &cpu_temp_graph, 20.0f, 110.0f, "",
                       rt_graph_color_temp);

      ui_space(ctx);

      nk_group_end(ctx);
    }

    // ------------------------------------------------------------------ GPU --
    nk_layout_row_dynamic(ctx, GPU_GROUP_HEIGHT, 1);
    if (nk_group_begin(ctx, "Graphics card",
                       NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE)) {

      ui_space(ctx);

      // --- Core load graph ---
      float gpu_load = (float)S->snap.gpu_rt.vram_load;
      rt_graph_clamp_0_100(&gpu_load);
      rt_graph_update_slow(&gpu_graph, gpu_load, RT_GRAPH_UPDATE_INTERVAL);

      nk_layout_row_template_begin(ctx, 140);
      nk_layout_row_template_push_static(ctx, 100);
      nk_layout_row_template_push_static(ctx, 80);
      nk_layout_row_template_push_dynamic(ctx);
      nk_layout_row_template_end(ctx);

      nk_label(ctx, "Core load:", NK_TEXT_RIGHT);
      snprintf(buf, sizeof(buf), "%.1f %%", gpu_load);
      nk_label(ctx, buf, NK_TEXT_CENTERED);
      ui_draw_rt_graph(ctx, &gpu_graph, 0.0f, 100.0f, "%",
                       rt_graph_segment_color);

      ui_space(ctx);
      ui_space(ctx);

      // --- Core temperature graph ---
      float gpu_temp = (float)S->snap.gpu_rt.clock_temp;
      if (gpu_temp < 20.0f)  gpu_temp = 20.0f;
      if (gpu_temp > 110.0f) gpu_temp = 110.0f;
      rt_graph_update_slow(&gpu_temp_graph, gpu_temp, RT_GRAPH_UPDATE_INTERVAL);

      nk_layout_row_template_begin(ctx, 140);
      nk_layout_row_template_push_static(ctx, 100);
      nk_layout_row_template_push_static(ctx, 80);
      nk_layout_row_template_push_dynamic(ctx);
      nk_layout_row_template_end(ctx);

      nk_label(ctx, "Core temp:", NK_TEXT_RIGHT);
      snprintf(buf, sizeof(buf), "%d °C", S->snap.gpu_rt.clock_temp);
      nk_label(ctx, buf, NK_TEXT_CENTERED);
      ui_draw_rt_graph(ctx, &gpu_temp_graph, 20.0f, 110.0f, "",
                       rt_graph_color_temp);

      ui_space(ctx);

      // --- Frequency row ---
      nk_layout_row_template_begin(ctx, 26);
      nk_layout_row_template_push_static(ctx, 140);
      nk_layout_row_template_push_dynamic(ctx);
      nk_layout_row_template_push_static(ctx, 140);
      nk_layout_row_template_push_dynamic(ctx);
      nk_layout_row_template_end(ctx);

      nk_label(ctx, "Core freq:", NK_TEXT_RIGHT);
      snprintf(buf, sizeof(buf), "%d MHz", S->snap.gpu_rt.curr_core_freq);
      nk_label(ctx, buf, NK_TEXT_CENTERED);

      nk_label(ctx, "Memory freq:", NK_TEXT_RIGHT);
      snprintf(buf, sizeof(buf), "%d MHz", S->snap.gpu_rt.curr_mem_freq);
      nk_label(ctx, buf, NK_TEXT_CENTERED);

      nk_group_end(ctx);
    }

    // ------------------------------------------------------------------ RAM --
    nk_layout_row_dynamic(ctx, RAM_GROUP_HEIGHT, 1);
    if (nk_group_begin(ctx, "RAM", NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE)) {
      float ram_pct = 0.0f;

      if (S->snap.data.ram.total_mb > 0) {
        ram_pct = (float)((double)S->snap.ram_rt.used_mb * 100.0 /
                          (double)S->snap.data.ram.total_mb);
      }

      rt_graph_clamp_0_100(&ram_pct);
      rt_graph_update_slow(&ram_graph, ram_pct, RT_GRAPH_UPDATE_INTERVAL);

      snprintf(buf, sizeof(buf), "%.1f %%", ram_pct);

      ui_space(ctx);

      nk_layout_row_template_begin(ctx, 140);
      nk_layout_row_template_push_static(ctx, 100);
      nk_layout_row_template_push_static(ctx, 80);
      nk_layout_row_template_push_dynamic(ctx);
      nk_layout_row_template_end(ctx);

      nk_label(ctx, "Ram usage:", NK_TEXT_RIGHT);
      nk_label(ctx, buf, NK_TEXT_CENTERED);
      ui_draw_rt_graph(ctx, &ram_graph, 0.0f, 100.0f, "%",
                       rt_graph_segment_color);

      ui_space(ctx);

      nk_group_end(ctx);
    }
  }
}

static void rt_graph_clamp_0_100(float *value) {
  if (!value)
    return;

  if (*value < 0.0f)
    *value = 0.0f;
  if (*value > 100.0f)
    *value = 100.0f;
}

static void rt_graph_init(struct RtGraph *g, float value) {
  if (!g)
    return;

  for (int i = 0; i < RT_GRAPH_HISTORY_COUNT; ++i)
    g->values[i] = value;

  g->head = 0;
  g->initialized = 1;
  g->last_update = 0.0;
}

static void rt_graph_push(struct RtGraph *g, float value) {
  if (!g)
    return;

  if (!g->initialized)
    rt_graph_init(g, value);

  g->values[g->head] = value;
  g->head = (g->head + 1) % RT_GRAPH_HISTORY_COUNT;
}

static void rt_graph_update_slow(struct RtGraph *g, float value,
                                 double interval_sec) {
  double now;

  if (!g)
    return;

  now = glfwGetTime();

  if (!g->initialized) {
    rt_graph_init(g, value);
    g->last_update = now;
    return;
  }

  if ((now - g->last_update) >= interval_sec) {
    rt_graph_push(g, value);
    g->last_update = now;
  }
}

// Color scale for 0-100% load
struct nk_color rt_graph_segment_color(float value) {
  if (value >= 80.0f)
    return nk_rgb(220, 60, 60);
  if (value >= 50.0f)
    return nk_rgb(230, 180, 60);
  return nk_rgb(80, 200, 120);
}

// Color scale for 20-110 C temperature
struct nk_color rt_graph_color_temp(float value) {
  if (value >= 85.0f)
    return nk_rgb(220, 60, 60);   // red:    hot
  if (value >= 65.0f)
    return nk_rgb(230, 180, 60);  // yellow: warm
  return nk_rgb(80, 200, 120);    // green:  cool
}

static void rt_draw_text_right(struct nk_command_buffer *canvas,
                               const struct nk_user_font *font,
                               struct nk_rect r, const char *text,
                               struct nk_color fg) {
  int len;
  float tw;
  float x;
  struct nk_rect tr;

  if (!canvas || !font || !text)
    return;

  len = (int)strlen(text);
  tw = font->width(font->userdata, font->height, text, len);

  x = r.x + r.w - tw;
  if (x < r.x)
    x = r.x;

  tr = nk_rect(x, r.y, tw, r.h);
  nk_draw_text(canvas, tr, text, len, font, nk_rgba(0, 0, 0, 0), fg);
}

static void ui_draw_rt_graph(struct nk_context *ctx, const struct RtGraph *g,
                             float min_value, float max_value,
                             const char *suffix,
                             struct nk_color (*color_fn)(float)) {
  struct nk_rect bounds;
  struct nk_command_buffer *canvas;
  const struct nk_user_font *font;
  const float left_pad = 34.0f;
  const float top_pad = 4.0f;
  const float bottom_pad = 4.0f;
  const float right_pad = 2.0f;
  struct nk_rect plot;
  struct nk_color grid_color = nk_rgb(110, 110, 110);
  struct nk_color text_color = nk_rgb(170, 170, 170);

  if (!g)
    return;

  if (!nk_widget(&bounds, ctx))
    return;

  canvas = nk_window_get_canvas(ctx);
  font = ctx->style.font;

  if (!canvas || !font)
    return;

  plot =
      nk_rect(bounds.x + left_pad, bounds.y + top_pad,
              bounds.w - left_pad - right_pad, bounds.h - top_pad - bottom_pad);

  if (plot.w <= 2.0f || plot.h <= 2.0f)
    return;

  for (int i = 0; i <= 4; ++i) {
    float y = plot.y + (plot.h * (float)i) / 4.0f;
    int label_value = (int)(max_value - ((max_value - min_value) * i / 4.0f));
    char label[16];
    struct nk_rect tr;

    nk_stroke_line(canvas, plot.x, y, plot.x + plot.w, y, 1.0f, grid_color);

    snprintf(label, sizeof(label), "%d%s", label_value, suffix ? suffix : "");

    tr = nk_rect(bounds.x + 2.0f, y - font->height * 0.5f, left_pad - 6.0f,
                 font->height);

    rt_draw_text_right(canvas, font, tr, label, text_color);
  }

  for (int i = 1; i < RT_GRAPH_HISTORY_COUNT; ++i) {
    int idx0 = (g->head + i - 1) % RT_GRAPH_HISTORY_COUNT;
    int idx1 = (g->head + i) % RT_GRAPH_HISTORY_COUNT;
    float v0 = g->values[idx0];
    float v1 = g->values[idx1];
    float t0, t1;
    float x0, x1, y0, y1;
    float avg;
    struct nk_color seg_color;

    if (v0 < min_value) v0 = min_value;
    if (v0 > max_value) v0 = max_value;
    if (v1 < min_value) v1 = min_value;
    if (v1 > max_value) v1 = max_value;

    t0 = (v0 - min_value) / (max_value - min_value);
    t1 = (v1 - min_value) / (max_value - min_value);

    x0 = plot.x +
         ((float)(i - 1) / (float)(RT_GRAPH_HISTORY_COUNT - 1)) * plot.w;
    x1 = plot.x + ((float)i / (float)(RT_GRAPH_HISTORY_COUNT - 1)) * plot.w;

    y0 = plot.y + plot.h - t0 * plot.h;
    y1 = plot.y + plot.h - t1 * plot.h;

    avg = (v0 + v1) * 0.5f;
    seg_color = color_fn ? color_fn(avg) : nk_rgb(80, 200, 120);

    nk_stroke_line(canvas, x0, y0, x1, y1, 2.0f, seg_color);
  }
}
