// secure.h
#pragma once
#include "../backend.h"
#include <stddef.h>
#include <stdint.h>


// Reason flags (extend later)
#define DRV_RISK_LOLDRIVERS (1u << 0)
#define DRV_RISK_MS_BLOCKLIST (1u << 1)
#define DRV_RISK_UNSIGNED (1u << 2)

typedef struct Slot {
  uint8_t used;
  char key[65];
} Slot;

typedef struct LolDriversDb {
  Slot *slots;
  size_t cap;
  size_t len;
} LolDriversDb;

#ifndef MSBL_INJECT_MAX
#define MSBL_INJECT_MAX 32
#endif

typedef struct MsBlocklistDb {
  char (*sha256)[65];
  size_t sha256_count;
  size_t sha256_cap;

  // optional: keep page hashes too (not used for now)
  char (*sha256_page)[65];
  size_t sha256_page_count;
  size_t sha256_page_cap;

  // ---- selftest injection (does not modify real DB) ----
  char injected_sha256[MSBL_INJECT_MAX][65];
  int injected_count;

  int loaded;
} MsBlocklistDb;

// Download + parse + build SHA256 set. Returns 1 on success.
int loldrivers_db_init(LolDriversDb *db);
// Free all memory.
void loldrivers_db_free(LolDriversDb *db);
// Single check by sha256 (64 hex). Returns 1 if found in LOLDrivers.
int loldrivers_is_vulnerable_sha256(const LolDriversDb *db, const char *sha256);
// Iterate Snapshot drivers and print matches. Returns number of hits.
int loldrivers_check_snapshot_and_print(const LolDriversDb *db,
                                        const Snapshot *snap);
int loldrivers_db_add_sha256(LolDriversDb *db, const char *sha256);

// Init LOLDrivers DB once. Returns 1 if ready.
int ui_loldrivers_init_once(void);

// Self-test injection (debug only). Safe to call even if DB isn't ready yet.
void ui_loldrivers_selftest_inject(const Snapshot *snap, int want);

// True if SHA256 is in LOLDrivers DB (or injected in selftest).
int ui_loldrivers_is_hit_sha256(const char *sha256);

// Optional: free DB if you ever want to release memory on shutdown.
void ui_loldrivers_shutdown(void);

int drivers_collect_win(Snapshot *s);

// Per-driver flags for current snapshot (index == snap->drivers[i])
extern uint32_t g_drv_risk_flags[DRV_MAX];

// Filter mask for "Vulnerable drivers" counters (reasons selection)
extern uint32_t g_drv_risk_filter;

// Counters per reason (extend later)
extern int g_risk_count_loldrivers;

// Count drivers that match ANY selected reasons in mask
int driver_risk_count_filtered(const Snapshot *snap, uint32_t mask);

// Load hashes from SiPolicy_Enforced.xml (or similar) file
int ms_blocklist_init_from_xml(MsBlocklistDb *db, const char *xml_path);

// Free memory
void ms_blocklist_free(MsBlocklistDb *db);

// Lookup
int ms_blocklist_is_denied_sha256(const MsBlocklistDb *db, const char *sha256);

// Check snapshot and print hits
int ms_blocklist_check_snapshot_and_print(const MsBlocklistDb *db,
                                          const Snapshot *snap);

// self-test helpers
void ms_blocklist_selftest_clear(MsBlocklistDb *db);
int ms_blocklist_selftest_inject_from_snapshot(MsBlocklistDb *db,
                                               const Snapshot *snap, int want);

// true if driver is unsigned
int unsigned_is_driver_unsigned(const DriverInfo *d);

// build flags + count (called from secure page)
void unsigned_build_flags(const Snapshot *snap, uint32_t *flags,
                          int *out_count);

typedef struct HwSecInfo {
  int secure_boot; // 1/0/-1
  int uefi_mode;   // 1/0/-1
  int tpm20;       // 1/0/-1  (именно TPM 2.0)
  int bitlocker;   // 1/0/-1  (защита включена на системном диске)
  int vbs;
  int hvci;
  int ftpm; // AMD firmware TPM
  int ptt;  // Intel Platform Trust Technology
  int nx;
  int smep;
  int cet;
} HwSecInfo;

int hwsec_query_win(HwSecInfo *out);

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

SecRating sec_rating_compute(const Snapshot *snap, int count_loldrivers,
                             int count_msblock, int count_unsigned,
                             int ms_db_ready);

/* =================================
         HARDWARE FINGERPRINT
   ================================= */
// Save current hardware fingerprint JSON to file selected by user.
// Returns: 1 saved, 0 canceled, -1 error. Human-readable status -> out.
int hardware_fingerprint_build(const Snapshot *snap, char *out, size_t out_sz);

// Compare current snapshot hardware fingerprint with JSON file selected by user.
// Returns: 1 match, 0 canceled, -1 error/mismatch. Human-readable status -> out.
int hardware_fingerprint_compare(const Snapshot *snap, char *out, size_t out_sz);
