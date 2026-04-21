// mboard_info.c
#define WIN32_LEAN_AND_MEAN
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>


#pragma pack(push, 1)
typedef struct SMBIOSHeader {
  uint8_t type;
  uint8_t length;
  uint16_t handle;
} SMBIOSHeader;

typedef struct RawSMBIOSData {
  uint8_t Used20CallingMethod;
  uint8_t SMBIOSMajorVersion;
  uint8_t SMBIOSMinorVersion;
  uint8_t DmiRevision;
  uint32_t Length;
  uint8_t SMBIOSTableData[1];
} RawSMBIOSData;
#pragma pack(pop)

typedef struct MoboCache {
  char board_vendor[48];
  char board_product[48];
  char bios_vendor[48];
  char bios_version[32];
  char smbios_uuid[37];
  int ready;
} MoboCache;

static MoboCache g_mb;

static void str_copy(char *dst, size_t dstsz, const char *src) {
  size_t n = 0;
  if (!dst || dstsz == 0)
    return;
  dst[0] = 0;
  if (!src)
    return;

  while (n + 1 < dstsz && src[n]) {
    dst[n] = src[n];
    n++;
  }
  dst[n] = 0;
}

static int is_all_bytes(const uint8_t *p, size_t n, uint8_t v) {
  size_t i;
  for (i = 0; i < n; i++) {
    if (p[i] != v)
      return 0;
  }
  return 1;
}

static void format_uuid_smbios(const uint8_t u[16], char out[37]) {
  static const char *hex = "0123456789abcdef";
  uint8_t b[16];

  // SMBIOS UUID: первые 4-2-2 байта little-endian
  b[0] = u[3];
  b[1] = u[2];
  b[2] = u[1];
  b[3] = u[0];
  b[4] = u[5];
  b[5] = u[4];
  b[6] = u[7];
  b[7] = u[6];
  b[8] = u[8];
  b[9] = u[9];
  b[10] = u[10];
  b[11] = u[11];
  b[12] = u[12];
  b[13] = u[13];
  b[14] = u[14];
  b[15] = u[15];

  // xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
  // 36 chars + '\0'
  {
    int p = 0;
    int i;
    for (i = 0; i < 16; i++) {
      if (i == 4 || i == 6 || i == 8 || i == 10)
        out[p++] = '-';
      out[p++] = hex[(b[i] >> 4) & 0xF];
      out[p++] = hex[(b[i] >> 0) & 0xF];
    }
    out[p] = 0;
  }
}

static const uint8_t *find_struct_end(const uint8_t *start, const uint8_t *end,
                                      const SMBIOSHeader *hdr) {
  const uint8_t *q;
  if (!start || !hdr)
    return end;
  if (start + hdr->length > end)
    return end;
  q = start + hdr->length;

  // строковая зона заканчивается двойным нулём
  while (q + 1 < end) {
    if (q[0] == 0 && q[1] == 0)
      return q + 2;
    q++;
  }
  return end;
}

static void copy_smbios_string(char *dst, size_t dstsz,
                               const uint8_t *struct_start,
                               const uint8_t *struct_end,
                               const SMBIOSHeader *hdr, uint8_t idx) {
  const uint8_t *p;
  uint8_t cur = 1;

  if (!dst || dstsz == 0)
    return;
  if (!struct_start || !hdr)
    return;
  if (idx == 0)
    return;
  if (struct_start + hdr->length > struct_end)
    return;

  p = struct_start + hdr->length;
  while (p < struct_end && cur < idx) {
    const uint8_t *z = (const uint8_t *)memchr(p, 0, (size_t)(struct_end - p));
    if (!z)
      return;
    p = z + 1;
    if (p >= struct_end)
      return;
    if (*p == 0)
      return;
    cur++;
  }

  if (p >= struct_end)
    return;
  if (*p == 0)
    return;

  {
    const uint8_t *z = (const uint8_t *)memchr(p, 0, (size_t)(struct_end - p));
    size_t n;
    if (!z)
      return;
    n = (size_t)(z - p);
    if (n + 1 > dstsz)
      n = dstsz - 1;
    memcpy(dst, p, n);
    dst[n] = 0;
  }
}

static int parse_smbios_into_cache(MoboCache *out) {
  // Было: DWORD sig = 'RSMB';
  DWORD sig = 0x52534D42;

  uint8_t *buf = NULL;
  UINT got = 0;

  for (int attempt = 0; attempt < 3; attempt++) {
    SetLastError(0);
    UINT need = GetSystemFirmwareTable(sig, 0, NULL, 0);
    if (!need) {
      return 0;
    }

    buf = (uint8_t *)malloc(need);
    if (!buf)
      return 0;

    SetLastError(0);
    got = GetSystemFirmwareTable(sig, 0, buf, need);
    if (got)
      break;

    DWORD err = GetLastError();
    free(buf);
    buf = NULL;

    if (err == ERROR_INSUFFICIENT_BUFFER) {
      continue; // пробуем заново
    }
    return 0;
  }

  if (!got || !buf)
    return 0;
  if (got < sizeof(RawSMBIOSData)) {
    free(buf);
    return 0;
  }

  RawSMBIOSData *raw = (RawSMBIOSData *)buf;

  size_t avail = got - offsetof(RawSMBIOSData, SMBIOSTableData);
  if (raw->Length > avail)
    raw->Length = (uint32_t)avail;

  const uint8_t *p = raw->SMBIOSTableData;
  const uint8_t *end = raw->SMBIOSTableData + raw->Length;

  while (p + sizeof(SMBIOSHeader) <= end) {
    const SMBIOSHeader *hdr = (const SMBIOSHeader *)p;
    if (hdr->length < sizeof(SMBIOSHeader))
      break;
    if (p + hdr->length > end)
      break;

    const uint8_t *struct_end = find_struct_end(p, end, hdr);

    if (hdr->type == 0) {
      if (!out->bios_vendor[0] && hdr->length > 0x04)
        copy_smbios_string(out->bios_vendor, sizeof(out->bios_vendor), p,
                           struct_end, hdr, p[0x04]);
      if (!out->bios_version[0] && hdr->length > 0x05)
        copy_smbios_string(out->bios_version, sizeof(out->bios_version), p,
                           struct_end, hdr, p[0x05]);

    } else if (hdr->type == 2) {
      if (!out->board_vendor[0] && hdr->length > 0x04)
        copy_smbios_string(out->board_vendor, sizeof(out->board_vendor), p,
                           struct_end, hdr, p[0x04]);
      if (!out->board_product[0] && hdr->length > 0x05)
        copy_smbios_string(out->board_product, sizeof(out->board_product), p,
                           struct_end, hdr, p[0x05]);

    } else if (hdr->type == 1) {
      // FALLBACK: на многих системах “плата” норм заполняется именно тут
      if (!out->board_vendor[0] && hdr->length > 0x04)
        copy_smbios_string(out->board_vendor, sizeof(out->board_vendor), p,
                           struct_end, hdr, p[0x04]);
      if (!out->board_product[0] && hdr->length > 0x05)
        copy_smbios_string(out->board_product, sizeof(out->board_product), p,
                           struct_end, hdr, p[0x05]);

      if (!out->smbios_uuid[0] && hdr->length >= 0x18) {
        const uint8_t *u = p + 0x08;
        if (!is_all_bytes(u, 16, 0x00) && !is_all_bytes(u, 16, 0xFF)) {
          format_uuid_smbios(u, out->smbios_uuid);
        }
      }
    }

    p = struct_end;
    if (p > end)
      break;
  }

  free(buf);

  return 1;
}

static void ensure_mobo_cached(void) {
  if (g_mb.ready)
    return;

  memset(&g_mb, 0, sizeof(g_mb));

  if (parse_smbios_into_cache(&g_mb)) {
    g_mb.ready = 1;
  } else {
    // НЕ ставим ready = 1, чтобы можно было повторить позже
    str_copy(g_mb.board_vendor, sizeof g_mb.board_vendor, "Unknown");
    str_copy(g_mb.board_product, sizeof g_mb.board_product, "Unknown");
    str_copy(g_mb.bios_vendor, sizeof g_mb.bios_vendor, "Unknown");
    str_copy(g_mb.bios_version, sizeof g_mb.bios_version, "Unknown");
    str_copy(g_mb.smbios_uuid, sizeof g_mb.smbios_uuid, "Unknown");
  }
}

/* Геттеры (можно дергать как угодно; парсинг один раз, с кэшем) */
void get_board_vendor_s(char *out, size_t outsz) {
  ensure_mobo_cached();
  str_copy(out, outsz, g_mb.board_vendor);
}
void get_board_product_s(char *out, size_t outsz) {
  ensure_mobo_cached();
  str_copy(out, outsz, g_mb.board_product);
}
void get_bios_vendor_s(char *out, size_t outsz) {
  ensure_mobo_cached();
  str_copy(out, outsz, g_mb.bios_vendor);
}
void get_bios_version_s(char *out, size_t outsz) {
  ensure_mobo_cached();
  str_copy(out, outsz, g_mb.bios_version);
}
void get_smbios_uuid_s(char *out, size_t outsz) {
  ensure_mobo_cached();
  str_copy(out, outsz, g_mb.smbios_uuid);
}
