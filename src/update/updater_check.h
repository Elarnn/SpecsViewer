#pragma once
#include <windows.h>

#define UPDATE_IDLE        0
#define UPDATE_CHECKING    1
#define UPDATE_AVAILABLE   2
#define UPDATE_LATEST      3
#define UPDATE_ERROR       4
#define UPDATE_DL_UPDATER  5
#define UPDATE_DL_ERROR    6

typedef struct {
    volatile LONG state;
    char latest_ver[64];
    char dl_error[256];
    char release_body[8192];
    HANDLE thread;
} UpdateState;

void update_check_start(UpdateState *us);
void update_cleanup(UpdateState *us);
void update_launch_updater(UpdateState *us);
