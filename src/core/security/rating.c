#include "secure.h" // unsigned_is_driver_unsigned, DRV_MAX
#include <ctype.h>
#include <string.h>


static int clampi(int v, int lo, int hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static int penalty_bucket(int count, int p1, int p2, int prest, int cap) {
  if (count <= 0)
    return 0;
  int p = 0;
  if (count >= 1)
    p += p1;
  if (count >= 2)
    p += p2;
  if (count > 2)
    p += prest * (count - 2);
  if (p > cap)
    p = cap;
  return p;
}

static void hw_add(int v, int w, int *max_w, int *got_x2, int *unknowns) {
  *max_w += w;
  if (v == 1) {
    *got_x2 += (w * 2);
  } else if (v < 0) {
    *got_x2 += w; // UNKNOWN = 0.5
    (*unknowns)++;
  }
}

static int contains_ci(const char *s, const char *needle) {
  if (!s || !needle || !needle[0])
    return 0;

  for (size_t i = 0; s[i]; i++) {
    size_t j = 0;
    while (needle[j] && s[i + j] &&
           (char)tolower((unsigned char)s[i + j]) ==
               (char)tolower((unsigned char)needle[j])) {
      j++;
    }
    if (!needle[j])
      return 1;
  }
  return 0;
}

static int is_ms_vendor_driver(const DriverInfo *d) {
  if (!d)
    return 0;
  // считаем MS, если provider или signer содержит "microsoft"
  if (contains_ci(d->provider, "microsoft"))
    return 1;
  if (contains_ci(d->signer, "microsoft"))
    return 1;
  return 0;
}

SecRating sec_rating_compute(const Snapshot *snap, int count_loldrivers,
                             int count_msblock, int count_unsigned,
                             int ms_db_ready) {
  (void)count_unsigned; // фронт передаёт как раньше, но мы считаем точнее сами

  SecRating r;
  memset(&r, 0, sizeof(r));

  if (!snap) {
    r.score = 0;
    r.level = SEC_RATE_UNKNOWN;
    r.label = "UNKNOWN";
    return r;
  }

  // ---- 0) Пересчитать unsigned по Snapshot: MS vs 3rd-party ----
  int unsigned_ms = 0;
  int unsigned_3p = 0;

  int n = snap->data.drivers_count;
  if (n > DRV_MAX)
    n = DRV_MAX;

  for (int i = 0; i < n; i++) {
    const DriverInfo *d = &snap->data.drivers[i];
    if (unsigned_is_driver_unsigned(d)) {
      if (is_ms_vendor_driver(d))
        unsigned_ms++;
      else
        unsigned_3p++;
    }
  }

  // ---- 1) Drivers risk -> driver_security (0..100) ----
  int p_lold = penalty_bucket(count_loldrivers, 22, 16, 10, 70);
  int p_ms = penalty_bucket(count_msblock, 18, 12, 8, 60);

  // unsigned: MS слабее, 3rd-party сильнее
  int p_uns_3p = penalty_bucket(unsigned_3p, 7, 5, 3, 40);
  int p_uns_ms = penalty_bucket(unsigned_ms, 3, 2, 1, 20);

  int penalty = p_lold + p_ms + p_uns_3p + p_uns_ms;

  // если MS DB не загрузилась — немного хуже, т.к. coverage ниже
  if (!ms_db_ready)
    penalty += 5;

  penalty = clampi(penalty, 0, 100);
  int drv_sec = 100 - penalty;

  // ---- 2) Hardware score (0..100) ----
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

  int hw_pct = 0;
  if (hw_max > 0)
    hw_pct = (hw_got_x2 * 100) / (hw_max * 2);

  // ---- 3) Combine ----
  int score = (drv_sec * 70 + hw_pct * 30 + 50) / 100;

  // небольшой штраф за UNKNOWN чтобы “не идеально” при неполных данных
  int unk_pen = hw_unknowns;
  if (!ms_db_ready)
    unk_pen += 1;
  if (unk_pen > 6)
    unk_pen = 6;

  score -= unk_pen;
  score = clampi(score, 0, 100);

  // Жёсткое правило: если есть dangerous hit (LOL/MS) — не позволяем зелёный
  if ((count_loldrivers + count_msblock) > 0) {
    if (score > 59)
      score = 59;
    r.score = score;
    r.level = SEC_RATE_RED;
    r.label = "DANGEROUS";
    return r;
  }

  r.score = score;

  if (score >= 85) {
    r.level = SEC_RATE_GREEN;
    r.label = "PROTECTED";
  } else if (score >= 60) {
    r.level = SEC_RATE_YELLOW;
    r.label = "WARNING";
  } else {
    r.level = SEC_RATE_RED;
    r.label = "DANGEROUS";
  }

  return r;
}
