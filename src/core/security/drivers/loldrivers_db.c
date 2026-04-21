// loldrivers_db.c

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "../secure.h"

#ifndef LOLDRIVERS_URL
#define LOLDRIVERS_URL L"https://www.loldrivers.io/api/drivers.json"
#endif

/* ------------------------------ WinHTTP download
 * ------------------------------ */

static int download_url_utf8(const wchar_t *url, char **out_buf,
                             size_t *out_len) {
  *out_buf = NULL;
  *out_len = 0;

  URL_COMPONENTS uc;
  memset(&uc, 0, sizeof(uc));
  uc.dwStructSize = sizeof(uc);

  wchar_t host[256];
  wchar_t path[2048];
  uc.lpszHostName = host;
  uc.dwHostNameLength = (DWORD)(sizeof(host) / sizeof(host[0]));
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = (DWORD)(sizeof(path) / sizeof(path[0]));

  if (!WinHttpCrackUrl(url, 0, 0, &uc))
    return 0;

  HINTERNET hSession =
      WinHttpOpen(L"SpecsViewer/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession)
    return 0;

  HINTERNET hConnect = WinHttpConnect(hSession, uc.lpszHostName, uc.nPort, 0);
  if (!hConnect) {
    WinHttpCloseHandle(hSession);
    return 0;
  }

  DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", uc.lpszUrlPath,
                                          NULL, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!hRequest) {
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return 0;
  }

  int ok = 0;

  if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(hRequest, NULL)) {
    size_t cap = 0, len = 0;
    char *buf = NULL;

    for (;;) {
      DWORD avail = 0;
      if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0)
        break;

      if (len + (size_t)avail + 1 > cap) {
        size_t new_cap = (cap == 0) ? ((size_t)avail + 1) : cap * 2;
        while (new_cap < len + (size_t)avail + 1)
          new_cap *= 2;

        char *nb = (char *)realloc(buf, new_cap);
        if (!nb) {
          free(buf);
          buf = NULL;
          cap = 0;
          break;
        }
        buf = nb;
        cap = new_cap;
      }

      DWORD read = 0;
      if (!WinHttpReadData(hRequest, buf + len, avail, &read) || read == 0) {
        free(buf);
        buf = NULL;
        cap = 0;
        break;
      }
      len += (size_t)read;
    }

    if (buf) {
      buf[len] = '\0';
      *out_buf = buf;
      *out_len = len;
      ok = 1;
    }
  }

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  return ok;
}

/* ------------------------------ Hash set (SHA256)
 * ------------------------------ */

static uint64_t fnv1a64(const char *s) {
  uint64_t h = 1469598103934665603ull;
  for (; *s; s++) {
    h ^= (uint8_t)(*s);
    h *= 1099511628211ull;
  }
  return h;
}

static int is_hex_len(const char *s, int n) {
  if (!s)
    return 0;
  for (int i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if (!c || !isxdigit(c))
      return 0;
  }
  return s[n] == '\0';
}

static void tolower_hex64(char out[65], const char *in) {
  for (int i = 0; i < 64; i++) {
    unsigned char c = (unsigned char)in[i];
    out[i] = (char)tolower(c);
  }
  out[64] = '\0';
}

static size_t next_pow2(size_t x) {
  size_t p = 1;
  while (p < x)
    p <<= 1;
  return p;
}

static int db_rehash(LolDriversDb *db, size_t new_cap) {
  new_cap = next_pow2(new_cap);
  Slot *ns = (Slot *)calloc(new_cap, sizeof(Slot));
  if (!ns)
    return 0;

  Slot *old = db->slots;
  size_t old_cap = db->cap;

  db->slots = ns;
  db->cap = new_cap;
  db->len = 0;

  for (size_t i = 0; i < old_cap; i++) {
    if (!old[i].used)
      continue;

    const char *key = old[i].key;
    uint64_t h = fnv1a64(key);
    size_t mask = db->cap - 1;
    size_t idx = (size_t)(h & mask);

    for (;;) {
      if (!db->slots[idx].used) {
        db->slots[idx].used = 1;
        memcpy(db->slots[idx].key, key, 65);
        db->len++;
        break;
      }
      idx = (idx + 1) & mask;
    }
  }

  free(old);
  return 1;
}

static int db_ensure_capacity(LolDriversDb *db, size_t want_len) {
  if (db->cap == 0) {
    db->cap = 16384; // good starting point for LOLDrivers
    db->slots = (Slot *)calloc(db->cap, sizeof(Slot));
    if (!db->slots) {
      db->cap = 0;
      return 0;
    }
    db->len = 0;
  }

  // keep load factor <= ~0.70
  if (want_len * 10 >= db->cap * 7) {
    return db_rehash(db, db->cap * 2);
  }
  return 1;
}

static int db_insert(LolDriversDb *db, const char key65[65]) {
  if (!db_ensure_capacity(db, db->len + 1))
    return 0;

  uint64_t h = fnv1a64(key65);
  size_t mask = db->cap - 1;
  size_t idx = (size_t)(h & mask);

  for (;;) {
    if (!db->slots[idx].used) {
      db->slots[idx].used = 1;
      memcpy(db->slots[idx].key, key65, 65);
      db->len++;
      return 1;
    }
    if (memcmp(db->slots[idx].key, key65, 65) == 0) {
      return 1; // already exists
    }
    idx = (idx + 1) & mask;
  }
}

static int db_contains(const LolDriversDb *db, const char key65[65]) {
  if (!db || !db->slots || db->cap == 0)
    return 0;

  uint64_t h = fnv1a64(key65);
  size_t mask = db->cap - 1;
  size_t idx = (size_t)(h & mask);

  for (;;) {
    if (!db->slots[idx].used)
      return 0;
    if (memcmp(db->slots[idx].key, key65, 65) == 0)
      return 1;
    idx = (idx + 1) & mask;
  }
}

/* ------------------------------ "Parsing" SHA256 from JSON text
 * ------------------------------ */
// We don't need a full JSON parser for now. We only extract all occurrences of:
// "SHA256":"<64-hex>"

static int extract_next_sha256(const char *start, const char **next,
                               char out65[65]) {
  const char *p = strstr(start, "\"SHA256\"");
  if (!p)
    return 0;

  p = strchr(p, ':');
  if (!p)
    return 0;

  p = strchr(p, '"'); // first quote after ':'
  if (!p)
    return 0;
  p++; // begin of value

  const char *end = strchr(p, '"');
  if (!end)
    return 0;

  size_t len = (size_t)(end - p);
  if (len == 64) {
    char tmp[65];
    memcpy(tmp, p, 64);
    tmp[64] = '\0';

    // validate + normalize
    if (is_hex_len(tmp, 64)) {
      tolower_hex64(out65, tmp);
      *next = end + 1;
      return 1;
    }
  }

  *next = end + 1;
  return -1; // found SHA256 key, but value not valid
}

static int build_db_from_json(LolDriversDb *db, const char *json) {
  const char *p = json;
  char sha65[65];

  // ensure initial allocation
  if (!db_ensure_capacity(db, 1))
    return 0;

  for (;;) {
    const char *next = NULL;
    int r = extract_next_sha256(p, &next, sha65);
    if (r == 0)
      break;
    if (r == 1) {
      if (!db_insert(db, sha65))
        return 0;
    }
    p = next;
    if (!p)
      break;
  }

  return (db->len > 0);
}

/* ------------------------------ Public API ------------------------------ */

int loldrivers_db_init(LolDriversDb *db) {
  if (!db)
    return 0;
  memset(db, 0, sizeof(*db));

  char *json = NULL;
  size_t len = 0;

  if (!download_url_utf8(LOLDRIVERS_URL, &json, &len)) {
    printf("[LOLDrivers] download failed\n");
    return 0;
  }

  int ok = build_db_from_json(db, json);
  free(json);

  if (!ok) {
    printf("[LOLDrivers] parse/build failed\n");
    loldrivers_db_free(db);
    return 0;
  }

  printf("[LOLDrivers] loaded SHA256 entries: %zu\n", db->len);
  return 1;
}

void loldrivers_db_free(LolDriversDb *db) {
  if (!db)
    return;
  free(db->slots);
  memset(db, 0, sizeof(*db));
}

int loldrivers_is_vulnerable_sha256(const LolDriversDb *db,
                                    const char *sha256) {
  if (!db || !sha256)
    return 0;
  if (strlen(sha256) != 64)
    return 0;

  char key65[65];
  for (int i = 0; i < 64; i++) {
    unsigned char c = (unsigned char)sha256[i];
    if (!isxdigit(c))
      return 0;
    key65[i] = (char)tolower(c);
  }
  key65[64] = '\0';

  return db_contains(db, key65);
}

int loldrivers_db_add_sha256(LolDriversDb *db, const char *sha256) {
  if (!db || !sha256)
    return 0;
  if (strlen(sha256) != 64)
    return 0;

  char key65[65];
  for (int i = 0; i < 64; i++) {
    unsigned char c = (unsigned char)sha256[i];
    if (!isxdigit(c))
      return 0;
    key65[i] = (char)tolower(c);
  }
  key65[64] = '\0';

  return db_insert(db, key65); // db_insert у тебя уже есть внутри модуля
}

#ifndef LOL_SELFTEST_INJECT
#define LOL_SELFTEST_INJECT 1
#endif

static LolDriversDb g_lol_db;
static int g_lol_ready = 0;

int ui_loldrivers_init_once(void) {
  if (g_lol_ready)
    return 1;

  g_lol_ready = loldrivers_db_init(&g_lol_db);
  if (!g_lol_ready)
    printf("[LOLDrivers] DB init failed\n");

  return g_lol_ready;
}

int ui_loldrivers_is_hit_sha256(const char *sha256) {
  if (!g_lol_ready || !sha256 || !sha256[0])
    return 0;
  return loldrivers_is_vulnerable_sha256(&g_lol_db, sha256);
}

static int collect_sha256_indices(const Snapshot *snap, int *out_idxs,
                                  int cap) {
  if (!snap || !out_idxs || cap <= 0)
    return 0;

  int n = 0;
  for (int i = 0; i < snap->data.drivers_count && i < DRV_MAX; i++) {
    const char *h = snap->data.drivers[i].sha256;
    if (h && h[0] && strlen(h) == 64) {
      out_idxs[n++] = i;
      if (n >= cap)
        break;
    }
  }
  return n;
}

void ui_loldrivers_selftest_inject(const Snapshot *snap, int want) {
#if LOL_SELFTEST_INJECT
  if (!snap || want <= 0)
    return;

  if (!ui_loldrivers_init_once())
    return;

  int idxs[DRV_MAX];
  int n = collect_sha256_indices(snap, idxs, DRV_MAX);

  if (n == 0) {
    printf("[LOLDrivers SELFTEST] no driver sha256 found in snapshot\n");
    return;
  }

  if (want > n)
    want = n;

  // Pick 'want' unique items (partial Fisher-Yates)
  uint32_t seed = (uint32_t)GetTickCount();

  for (int k = 0; k < want; k++) {
    seed = seed * 1664525u + 1013904223u;
    int j = k + (int)(seed % (uint32_t)(n - k));

    int tmp = idxs[k];
    idxs[k] = idxs[j];
    idxs[j] = tmp;

    int idx = idxs[k];
    const char *sha = snap->data.drivers[idx].sha256;

    if (loldrivers_db_add_sha256(&g_lol_db, sha)) {
      printf("[LOLDrivers SELFTEST] injected #%d: idx=%d sha256=%s (%s)\n",
             k + 1, idx, sha,
             snap->data.drivers[idx].driver_file[0] ? snap->data.drivers[idx].driver_file
                                               : "(no file)");
    }
  }
#else
  (void)snap;
  (void)want;
#endif
}

void ui_loldrivers_shutdown(void) {
  if (!g_lol_ready)
    return;
  loldrivers_db_free(&g_lol_db);
  g_lol_ready = 0;
}
