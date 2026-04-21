// ms_blocklist_db.c
#define WIN32_LEAN_AND_MEAN
#include "../secure.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>


static int is_hex_char(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

static void to_lower_hex(char *dst, const char *src, size_t n) {
  for (size_t i = 0; i < n; i++) {
    char c = src[i];
    dst[i] = (char)tolower((unsigned char)c);
  }
  dst[n] = '\0';
}

static int is_sha256_str(const char *s) {
  if (!s)
    return 0;
  if (strlen(s) != 64)
    return 0;
  for (int i = 0; i < 64; i++)
    if (!is_hex_char(s[i]))
      return 0;
  return 1;
}

static int contains_ci(const char *hay, const char *needle) {
  if (!hay || !needle || !needle[0])
    return 0;
  size_t hn = strlen(hay);
  size_t nn = strlen(needle);
  if (nn > hn)
    return 0;

  for (size_t i = 0; i + nn <= hn; i++) {
    size_t k = 0;
    for (; k < nn; k++) {
      char a = (char)tolower((unsigned char)hay[i + k]);
      char b = (char)tolower((unsigned char)needle[k]);
      if (a != b)
        break;
    }
    if (k == nn)
      return 1;
  }
  return 0;
}

static int ensure_cap_sha256(MsBlocklistDb *db, int is_page) {
  if (!db)
    return 0;

  if (!is_page) {
    if (db->sha256_count + 1 <= db->sha256_cap)
      return 1;
    size_t nc = (db->sha256_cap == 0) ? 4096 : (db->sha256_cap * 2);
    void *p = realloc(db->sha256, nc * 65);
    if (!p)
      return 0;
    db->sha256 = (char (*)[65])p;
    db->sha256_cap = nc;
    return 1;
  } else {
    if (db->sha256_page_count + 1 <= db->sha256_page_cap)
      return 1;
    size_t nc = (db->sha256_page_cap == 0) ? 1024 : (db->sha256_page_cap * 2);
    void *p = realloc(db->sha256_page, nc * 65);
    if (!p)
      return 0;
    db->sha256_page = (char (*)[65])p;
    db->sha256_page_cap = nc;
    return 1;
  }
}

static int cmp_str65(const void *a, const void *b) {
  const char *sa = (const char *)a;
  const char *sb = (const char *)b;
  return strcmp(sa, sb);
}

static size_t uniq_sorted_65(char (*arr)[65], size_t n) {
  if (!arr || n == 0)
    return 0;
  size_t w = 1;
  for (size_t i = 1; i < n; i++) {
    if (strcmp(arr[i], arr[w - 1]) != 0) {
      if (w != i)
        memcpy(arr[w], arr[i], 65);
      w++;
    }
  }
  return w;
}

static char *read_file_all(const char *path, size_t *out_sz) {
  if (out_sz)
    *out_sz = 0;
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;

  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (sz <= 0) {
    fclose(f);
    return NULL;
  }

  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }

  size_t rd = fread(buf, 1, (size_t)sz, f);
  fclose(f);

  buf[rd] = '\0';
  if (out_sz)
    *out_sz = rd;
  return buf;
}

int ms_blocklist_init_from_xml(MsBlocklistDb *db, const char *xml_path) {
  if (!db || !xml_path)
    return 0;
  memset(db, 0, sizeof(*db));

  size_t sz = 0;
  char *xml = read_file_all(xml_path, &sz);
  if (!xml) {
    printf("[MSBlocklist] cannot read: %s\n", xml_path);
    return 0;
  }

  size_t deny_tags = 0, got_hash = 0, got_sha256 = 0, got_page = 0;

  const char *p = xml;
  while ((p = strstr(p, "<Deny")) != NULL) {
    deny_tags++;

    // find end of tag (handles multiline)
    const char *end = strstr(p, ">");
    if (!end)
      break;

    size_t len = (size_t)(end - p + 1);
    if (len > 4096) {
      p = end + 1;
      continue;
    } // sanity

    char tag[4096];
    memcpy(tag, p, len);
    tag[len] = '\0';

    // find Hash="..."
    const char *h = strstr(tag, "Hash=\"");
    if (!h) {
      p = end + 1;
      continue;
    }
    h += 6; // after Hash="

    const char *q = strchr(h, '"');
    if (!q) {
      p = end + 1;
      continue;
    }

    size_t hlen = (size_t)(q - h);
    got_hash++;

    // We only care about SHA256 now
    if (hlen == 64) {
      int is_page = 0;
      // Detect page hash by FriendlyName containing "Page"
      if (contains_ci(tag, "Hash Page") || contains_ci(tag, "Page Sha256") ||
          contains_ci(tag, "Page")) {
        is_page = 1;
      }

      char norm[65];
      to_lower_hex(norm, h, 64);

      if (!ensure_cap_sha256(db, is_page)) {
        free(xml);
        ms_blocklist_free(db);
        return 0;
      }

      if (!is_page) {
        memcpy(db->sha256[db->sha256_count++], norm, 65);
        got_sha256++;
      } else {
        memcpy(db->sha256_page[db->sha256_page_count++], norm, 65);
        got_page++;
      }
    }

    p = end + 1;
  }

  free(xml);

  if (db->sha256_count) {
    qsort(db->sha256, db->sha256_count, 65, cmp_str65);
    db->sha256_count = uniq_sorted_65(db->sha256, db->sha256_count);
  }
  if (db->sha256_page_count) {
    qsort(db->sha256_page, db->sha256_page_count, 65, cmp_str65);
    db->sha256_page_count =
        uniq_sorted_65(db->sha256_page, db->sha256_page_count);
  }

  db->loaded = 1;

  printf("[MSBlocklist] parsed deny tags: %zu\n", deny_tags);
  printf("[MSBlocklist] hashes found: %zu\n", got_hash);
  printf("[MSBlocklist] SHA256 file entries: %zu\n", db->sha256_count);
  printf("[MSBlocklist] SHA256 page entries: %zu (stored but not used)\n",
         db->sha256_page_count);

  return 1;
}

void ms_blocklist_free(MsBlocklistDb *db) {
  if (!db)
    return;
  free(db->sha256);
  free(db->sha256_page);
  memset(db, 0, sizeof(*db));
}

static int bsearch_65(const char (*arr)[65], size_t n, const char *key64lower) {
  size_t lo = 0, hi = n;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    int c = strcmp(arr[mid], key64lower);
    if (c == 0)
      return 1;
    if (c < 0)
      lo = mid + 1;
    else
      hi = mid;
  }
  return 0;
}

static int str_eq_ci64(const char *a, const char *b) {
  for (int i = 0; i < 64; i++) {
    char ca = (char)tolower((unsigned char)a[i]);
    char cb = (char)tolower((unsigned char)b[i]);
    if (ca != cb)
      return 0;
  }
  return 1;
}

void ms_blocklist_selftest_clear(MsBlocklistDb *db) {
  if (!db)
    return;
  db->injected_count = 0;
}

static uint32_t seed32_now(void) {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  uint32_t s = (uint32_t)(t.QuadPart ^ (uint64_t)GetTickCount());
  if (s == 0)
    s = 0xA53A9E17u;
  return s;
}

int ms_blocklist_selftest_inject_from_snapshot(MsBlocklistDb *db,
                                               const Snapshot *snap, int want) {
  if (!db || !db->loaded || !snap || want <= 0)
    return 0;

  // collect indices with valid sha256
  int idxs[DRV_MAX];
  int n = 0;

  for (int i = 0; i < snap->data.drivers_count && i < DRV_MAX; i++) {
    const char *h = snap->data.drivers[i].sha256;
    if (h && h[0] && strlen(h) == 64) {
      idxs[n++] = i;
    }
  }

  if (n == 0) {
    printf("[MSBlocklist SELFTEST] no driver sha256 found in snapshot\n");
    return 0;
  }

  if (want > n)
    want = n;

  uint32_t seed = seed32_now();
  int injected = 0;

  // partial Fisher–Yates shuffle to pick 'want' unique indices
  for (int k = 0; k < want; k++) {
    seed = seed * 1664525u + 1013904223u;
    int j = k + (int)(seed % (uint32_t)(n - k));

    int tmp = idxs[k];
    idxs[k] = idxs[j];
    idxs[j] = tmp;

    int di = idxs[k];
    const char *sha = snap->data.drivers[di].sha256;

    // normalize to lower
    char norm[65];
    to_lower_hex(norm, sha, 64);

    // skip duplicates inside injection list
    int dup = 0;
    for (int t = 0; t < db->injected_count; t++) {
      if (str_eq_ci64(db->injected_sha256[t], norm)) {
        dup = 1;
        break;
      }
    }
    if (dup)
      continue;

    if (db->injected_count >= MSBL_INJECT_MAX)
      break;

    memcpy(db->injected_sha256[db->injected_count++], norm, 65);
    injected++;

    printf(
        "[MSBlocklist SELFTEST] injected #%d: driver idx=%d sha256=%s (%s)\n",
        injected, di, sha,
        snap->data.drivers[di].driver_file[0] ? snap->data.drivers[di].driver_file
                                         : "(no file)");
  }

  return injected;
}

int ms_blocklist_is_denied_sha256(const MsBlocklistDb *db, const char *sha256) {
  if (!db || !db->loaded)
    return 0;
  if (!is_sha256_str(sha256))
    return 0;

  char norm[65];
  to_lower_hex(norm, sha256, 64);

  // 1) check injected list first (selftest)
  for (int i = 0; i < db->injected_count; i++) {
    if (strcmp(db->injected_sha256[i], norm) == 0)
      return 1;
  }

  // 2) real DB (sorted array)
  return bsearch_65((const char (*)[65])db->sha256, db->sha256_count, norm);
}

int ms_blocklist_check_snapshot_and_print(const MsBlocklistDb *db,
                                          const Snapshot *snap) {
  if (!db || !db->loaded || !snap)
    return 0;

  int hits = 0;

  for (int i = 0; i < snap->data.drivers_count && i < DRV_MAX; i++) {
    const DriverInfo *d = &snap->data.drivers[i];
    if (!d->sha256[0])
      continue;
    if (!is_sha256_str(d->sha256))
      continue;

    if (ms_blocklist_is_denied_sha256(db, d->sha256)) {
      hits++;

      printf("\n[MSBlocklist HIT] #%d\n", hits);
      printf("  Desc   : %s\n",
             d->device_desc[0] ? d->device_desc : "(no desc)");
      printf("  Service : %s\n", d->service[0] ? d->service : "(no service)");
      printf("  File    : %s\n",
             d->driver_file[0] ? d->driver_file : "(no file)");
      printf("  Path    : %s\n",
             d->image_path[0] ? d->image_path : "(no path)");
      printf("  SHA256  : %s\n", d->sha256);
      printf("  Signed  : %s\n", d->is_signed ? "yes" : "no");
      printf("  Signer  : %s\n", d->signer[0] ? d->signer : "(unknown)");
    }
  }

  printf("\n[MSBlocklist] total hits: %d\n", hits);
  return hits;
}
