// rating.c — security rating from the hardware protection mechanisms
#include "secure.h"
#include <string.h>

static int clampi(int v, int lo, int hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

/* Weighted sum: ON counts fully, UNKNOWN counts as a half, OFF as nothing. */
static void hw_add(int v, int w, int *max_w, int *got_x2, int *unknowns) {
  *max_w += w;
  if (v == 1) {
    *got_x2 += (w * 2);
  } else if (v < 0) {
    *got_x2 += w; // UNKNOWN = 0.5
    (*unknowns)++;
  }
}

SecRating sec_rating_compute(const Snapshot *snap) {
  SecRating r;
  memset(&r, 0, sizeof(r));

  if (!snap) {
    r.score = 0;
    r.level = SEC_RATE_UNKNOWN;
    r.label = "UNKNOWN";
    return r;
  }

  int hw_max = 0;
  int hw_got_x2 = 0;
  int hw_unknowns = 0;

  hw_add(snap->data.sec.uefi_mode, 18, &hw_max, &hw_got_x2, &hw_unknowns);
  hw_add(snap->data.sec.secure_boot, 22, &hw_max, &hw_got_x2, &hw_unknowns);
  hw_add(snap->data.sec.tpm20, 14, &hw_max, &hw_got_x2, &hw_unknowns);
  hw_add(snap->data.sec.bitlocker, 8, &hw_max, &hw_got_x2, &hw_unknowns);
  hw_add(snap->data.sec.vbs, 14, &hw_max, &hw_got_x2, &hw_unknowns);
  hw_add(snap->data.sec.hvci, 18, &hw_max, &hw_got_x2, &hw_unknowns);
  hw_add(snap->data.sec.nx, 10, &hw_max, &hw_got_x2, &hw_unknowns);
  hw_add(snap->data.sec.smep, 12, &hw_max, &hw_got_x2, &hw_unknowns);
  hw_add(snap->data.sec.cet, 12, &hw_max, &hw_got_x2, &hw_unknowns);

  int score = 0;
  if (hw_max > 0)
    score = (hw_got_x2 * 100 + hw_max) / (hw_max * 2); // rounded percentage

  // small penalty for UNKNOWN values so incomplete data is never "perfect"
  score -= clampi(hw_unknowns, 0, 6);
  score = clampi(score, 0, 100);

  r.score = score;
  if (score >= 80) {
    r.level = SEC_RATE_GREEN;
    r.label = "PROTECTED";
  } else if (score >= 50) {
    r.level = SEC_RATE_YELLOW;
    r.label = "WARNING";
  } else {
    r.level = SEC_RATE_RED;
    r.label = "DANGEROUS";
  }
  return r;
}
