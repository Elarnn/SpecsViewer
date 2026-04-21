#include "ui.h"
#include "ui/nk_common.h"

void ui_topbar(struct nk_context *ctx, enum AppTab *active) {
  static const char *names[] = {
      "CPU",     "RAM/Mboard", "GPU",  "Secure",
      "Sensors", "Bench",      "About"}; // массив подписей кнопок

  nk_layout_row_begin(ctx, NK_STATIC, 24,
                      7); // начать строку: фиксированные слоты; высота=24 px; 7
                          // слотов (под 7 кнопок)
  for (int i = 0; i < 7; ++i) // цикл по кнопкам
  {
    if (names[i] == "RAM/Mboard")
      nk_layout_row_push(ctx, 100); // ширина следующего слота 92 px
    else
      nk_layout_row_push(ctx, 90);
    struct nk_style_button st =
        ctx->style.button; // локальная копия стиля кнопки
    if ((int)*active == i) {
      st.border_color = nk_rgb(30, 144, 255);
      st.border = 2.0f;
    } // если кнопка активна — выделить рамкой и цветом
    if (nk_button_label_styled(ctx, &st, names[i]))
      *active = (enum AppTab)i; // кнопка: ctx — контекст; st — стиль; names[i]
                                // — текст; при клике записать активную вкладку
  }
  nk_layout_row_end(ctx); // завершить строку

  nk_style_push_vec2(ctx, &ctx->style.window.spacing,
                     nk_vec2(ctx->style.window.spacing.x,
                             0)); // временно обнулить вертикальный spacing окна
                                  // (оставив горизонтальный)
  nk_layout_row_dynamic(ctx, 1, 1); // строка высотой 1 px, одна колонка
  nk_rule_horizontal(ctx, nk_rgb(160, 160, 160),
                     1);  // нарисовать горизонтальную линию: цвет; толщина=1
  nk_style_pop_vec2(ctx); // вернуть исходный spacing окна
}
