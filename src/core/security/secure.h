// secure.h — hardware security mechanisms, rating and hardware fingerprint
#pragma once
#include "../backend.h"
#include <stddef.h>
#include <stdint.h>

/* =================================
        HARDWARE SECURITY SCAN
   ================================= */
typedef struct HwSecInfo {
  int secure_boot; // 1/0/-1
  int uefi_mode;   // 1/0/-1
  int tpm20;       // 1/0/-1  (TPM 2.0 specifically)
  int bitlocker;   // 1/0/-1  (protection enabled on the system drive)
  int vbs;
  int hvci;
  int ftpm; // AMD firmware TPM
  int ptt;  // Intel Platform Trust Technology
  int nx;
  int smep;
  int cet;
} HwSecInfo;

int hwsec_query_win(HwSecInfo *out);

/* =================================
               RATING
   ================================= */
typedef enum SecRatingLevel {
  SEC_RATE_GREEN = 0,
  SEC_RATE_YELLOW = 1,
  SEC_RATE_RED = 2,
  SEC_RATE_UNKNOWN = 3
} SecRatingLevel;

typedef struct SecRating {
  int score;            // 0..100
  SecRatingLevel level; // for UI color
  const char *label;    // "PROTECTED"/"WARNING"/"DANGEROUS"/"UNKNOWN"
} SecRating;

/* Rating built from the hardware security mechanisms in snap->data.sec. */
SecRating sec_rating_compute(const Snapshot *snap);

/* =================================
         HARDWARE FINGERPRINT
   ================================= */
// Save current hardware fingerprint JSON to file selected by user.
// Returns: 1 saved, 0 canceled, -1 error. Human-readable status -> out.
int hardware_fingerprint_build(const Snapshot *snap, char *out, size_t out_sz);

// Compare current snapshot hardware fingerprint with JSON file selected by user.
// Returns: 1 match, 0 canceled, -1 error/mismatch. Human-readable status -> out.
int hardware_fingerprint_compare(const Snapshot *snap, char *out, size_t out_sz);
