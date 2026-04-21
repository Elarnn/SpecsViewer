#pragma once

#if defined(__GNUC__)  /* MinGW GCC */

/* базовые SAL */
#ifndef __success
#define __success(x)
#endif
#ifndef __checkReturn
#define __checkReturn
#endif
#ifndef __analysis_assume
#define __analysis_assume(x)
#endif

#ifndef __in
#define __in
#endif
#ifndef __out
#define __out
#endif
#ifndef __inout
#define __inout
#endif
#ifndef __in_opt
#define __in_opt
#endif
#ifndef __out_opt
#define __out_opt
#endif
#ifndef __inout_opt
#define __inout_opt
#endif

/* “ecount”/буферные аннотации — ВАЖНО: должны быть макросами, а не типами */
#ifndef __in_ecount
#define __in_ecount(x)
#endif
#ifndef __out_ecount
#define __out_ecount(x)
#endif
#ifndef __inout_ecount
#define __inout_ecount(x)
#endif

#ifndef __in_ecount_opt
#define __in_ecount_opt(x)
#endif
#ifndef __out_ecount_opt
#define __out_ecount_opt(x)
#endif
#ifndef __inout_ecount_opt
#define __inout_ecount_opt(x)
#endif

#ifndef __out_ecount_full
#define __out_ecount_full(x)
#endif
#ifndef __out_ecount_full_opt
#define __out_ecount_full_opt(x)
#endif

#ifndef __inout_ecount_full
#define __inout_ecount_full(x)
#endif
#ifndef __inout_ecount_full_opt
#define __inout_ecount_full_opt(x)
#endif

#ifndef __inout_ecount_part
#define __inout_ecount_part(x,y)
#endif
#ifndef __inout_ecount_part_opt
#define __inout_ecount_part_opt(x,y)
#endif

#ifndef __out_ecount_part
#define __out_ecount_part(x,y)
#endif
#ifndef __out_ecount_part_opt
#define __out_ecount_part_opt(x,y)
#endif

/* драйверные SAL’ы, иногда встречаются */
#ifndef __drv_when
#define __drv_when(x,y)
#endif
#ifndef __drv_aliasesMem
#define __drv_aliasesMem
#endif
#ifndef __drv_allocatesMem
#define __drv_allocatesMem(x)
#endif
#ifndef __drv_freesMem
#define __drv_freesMem(x)
#endif

#endif /* __GNUC__ */

/* Подключаем NVAPI только после заглушек */
#include "nvapi.h"
