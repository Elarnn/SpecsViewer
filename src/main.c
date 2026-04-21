// main.c
#include "app.h"
#include "onstart/privilege.h"
#include "onstart/winring_check.h"

int main(void) {
  struct AppState S = {0};

  int elevated = app_is_elevated();
  if (elevated) {
    winring_ensure();   /* download DLL+SYS if missing, may restart */
    app_driver_load();
  }

  const char *title = elevated ? "SpecsViewer (full-mode)" : "SpecsViewer (user-mode)";
  if (!app_init(&S, 680, 560, title))
    return -1;

  if (!elevated)
    S.usermode_warn_open = 1;

  while (!glfwWindowShouldClose(S.window))
    app_frame(&S);

  app_shutdown(&S);
  if (elevated)
    app_driver_unload();
  return 0;
}
