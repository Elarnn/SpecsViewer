// Returns 1 if the current process runs with administrator privileges, 0 otherwise.
int app_is_elevated(void);

// Called at startup when running as administrator.
// Initializes Ring 0 drivers required for full-mode hardware access.
void app_driver_load(void);

// Called at shutdown to unload Ring 0 drivers and free resources.
// Must be called after the backend thread has stopped.
void app_driver_unload(void);