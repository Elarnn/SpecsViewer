#pragma once
#include <nuklear.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void ansi_to_utf8(const char *src, char *dst, int dstsz);

#ifdef __cplusplus
}
#endif
