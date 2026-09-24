/* 3DMMEx:
 * Copyright (c) 1993 Argonaut Software Ltd. All rights reserved.
 *
 * $Id: stdmem.c 1.3 1994/11/07 01:39:20 sam Exp $
 * $Locker:  $
 *
 * Default memory handler that uses malloc()/free() from C library
 */
#include <Windows.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

// Modern BRender's compiler.h defines DEBUG to 0 in non-debug builds.
// Kauai/3DMM uses #ifdef DEBUG, so letting that macro escape BRender makes
// release translation units compile debug-only method bodies whose class
// declarations were omitted earlier. Preserve the application's DEBUG state
// across the BRender public header.
#if defined(DEBUG)
#define THREEDMM_DEBUG_WAS_DEFINED_BEFORE_BRENDER 1
#endif
#include "brender.h"
#if defined(BRENDER_MODERN_14) && !defined(THREEDMM_DEBUG_WAS_DEFINED_BEFORE_BRENDER) && defined(DEBUG)
#undef DEBUG
#endif
#ifdef THREEDMM_DEBUG_WAS_DEFINED_BEFORE_BRENDER
#undef THREEDMM_DEBUG_WAS_DEFINED_BEFORE_BRENDER
#endif

#ifdef DEBUG
static long _cb = 0; // 3DMMEx: Total memory allocated by BRender
#endif

/* 3DMMEx:
 * Glue functions for malloc()/free()
 */
static void *BR_CALLBACK BrStdlibAllocate(br_size_t size, br_uint_8 type)
{
    void *m;
    long cbAlloc = size;

#ifdef DEBUG
    cbAlloc += sizeof(long);
#endif // 3DMMEx: DEBUG

    m = (void *)GlobalAlloc(GMEM_FIXED, cbAlloc);
    if (m == NULL)
        return NULL;

#ifdef DEBUG
    *(long *)m = size;
    _cb += size;
    m = (char *)m + sizeof(long);
#endif

    return m;
}

static void BR_CALLBACK BrStdlibFree(void *mem)
{
#ifdef DEBUG
    void *pmemReal = (char *)mem - sizeof(long);
    long size = *(long *)pmemReal;
    _cb -= size;
    mem = pmemReal;
#endif // 3DMMEx: DEBUG
    GlobalFree((HGLOBAL)mem);
}

static br_size_t BR_CALLBACK BrStdlibInquire(br_uint_8 type)
{
    return 0;
}

/* 3DMMEx:
 * Allocator structure
 */
br_allocator BrStdlibAllocator = {
    "malloc",
    BrStdlibAllocate,
    BrStdlibFree,
    BrStdlibInquire,
};

/* 3DMMEx:
 * Override global variable s.t. this is the default allocator
 */
br_allocator *_BrDefaultAllocator = &BrStdlibAllocator;
