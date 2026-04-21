#pragma once

// Checks for WinRing0x64.dll and WinRing0x64.sys next to the exe.
// If missing and running elevated, prompts the user to download them.
// If the user confirms, downloads WinRing0x64.zip from GitHub releases,
// extracts it next to the exe, and restarts the application (never returns).
// Call this before app_driver_load() in main().
void winring_ensure(void);
