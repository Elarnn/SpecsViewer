// privilege.c
#include "privilege.h"
#include "../core/winring/winring0.h"
#include <windows.h>

int app_is_elevated(void) {
  HANDLE tok = NULL;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok))
    return 0;

  TOKEN_ELEVATION te;
  DWORD cb = 0;
  int ok = GetTokenInformation(tok, TokenElevation, &te, sizeof(te), &cb);
  CloseHandle(tok);

  return ok && te.TokenIsElevated;
}

void app_driver_load(void) {
  cpu_wr0_init();
}

void app_driver_unload(void) {
  cpu_wr0_shutdown();
}
