#pragma once

/* Shows a native Win32 "update available" dialog.
   Returns 1 if user clicked "Update now", 0 otherwise.
   *dont_ask_out is set to 1 if "Don't ask again" was checked. */
int update_dialog_show(const char *version, int *dont_ask_out);
