#include <windows.h>

static void ansi_to_utf8(const char *src, char *dst, int dstsz)
{
    if (!dst || dstsz <= 0) return;
    dst[0] = 0;
    if (!src || !src[0]) return;

    wchar_t wbuf[1024];
    int wlen = MultiByteToWideChar(CP_ACP, 0, src, -1, wbuf, (int)(sizeof(wbuf)/sizeof(wbuf[0])));
    if (wlen <= 0) return;

    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, dst, dstsz, NULL, NULL);
}
