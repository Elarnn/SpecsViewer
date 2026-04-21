#include "../secure.h"

#include <string.h>

int unsigned_is_driver_unsigned(const DriverInfo *d) {
  if (!d)
    return 0;

  // explicitly unsigned
  if (!d->is_signed)
    return 1;

  // signed flag but no signer → suspicious
  if (d->is_signed && (!d->signer[0]))
    return 1;

  return 0;
}

void unsigned_build_flags(const Snapshot *snap, uint32_t *flags,
                          int *out_count) {
  if (!snap || !flags || !out_count)
    return;

  *out_count = 0;

  int n = snap->data.drivers_count;
  if (n > DRV_MAX)
    n = DRV_MAX;

  for (int i = 0; i < n; i++) {
    const DriverInfo *d = &snap->data.drivers[i];

    if (unsigned_is_driver_unsigned(d)) {
      flags[i] |= DRV_RISK_UNSIGNED;
      (*out_count)++;
    }
  }
}
