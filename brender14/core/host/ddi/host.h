/* BRender:
 * Copyright (c) 1992,1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: host.h 2.9 1997/08/13 10:42:18 JOHNG Exp $
 * $Locker: $
 */
#ifndef _HOST_H_
#define _HOST_H_

#ifndef _BRENDER_H_
#include "brender.h"
#endif

/* BRender:
 * Structure used to describe host information
 */
typedef struct host_info {
    br_uint_32 size;
    char       identifier[40];
    char       vendor_string[13];
    br_uint_32 capabilities;
    br_token   processor_family;
    br_token   processor_type;
} host_info;

#define HOST_CAPS_REAL_MEMORY          0x00000001 /* BRender: Can allocate/read/write real-mode mem.	*/
#define HOST_CAPS_REAL_INT_CALL        0x00000002 /* BRender: Can invoke real mode interrupts			*/
#define HOST_CAPS_REAL_INT_HOOK        0x00000004 /* BRender: Can hook real mode interrupts			*/
#define HOST_CAPS_PROTECTED_INT_CALL   0x00000008 /* BRender: Can invoke protected mode interrupts		*/
#define HOST_CAPS_PROTECTED_INT_HOOK   0x00000010 /* BRender: Can hook prot. mode interrupts			*/
#define HOST_CAPS_ALLOC_SELECTORS      0x00000020 /* BRender: Can allocate new selectors				*/
#define HOST_CAPS_PHYSICAL_MAP         0x00000040 /* BRender: Can map physical memory -> linear		*/
#define HOST_CAPS_EXCEPTION_HOOK       0x00000080 /* BRender: Can hook exceptions						*/
#define HOST_CAPS_BASE_SELECTORS_WRITE 0x00000100 /* BRender: Can modify base/limit of cs,ds,es,ss selectors	*/
#define HOST_CAPS_PORTS                0x00000200 /* BRender: Can use IO ports							*/
#define HOST_CAPS_MMX                  0x00000400 /* BRender: Has MMX extensions						*/
#define HOST_CAPS_FPU                  0x00000800 /* BRender: Has hardware FPU							*/
#define HOST_CAPS_CMOV                 0x00001000 /* BRender: Has CMOV extensions */
#define HOST_CAPS_SSE                  0x00002000 /* BRenderModern: Has SSE */
#define HOST_CAPS_SSE2                 0x00004000 /* BRenderModern: Has SSE2 */
#define HOST_CAPS_SSE3                 0x00008000 /* BRenderModern: Has SSE3 */
#define HOST_CAPS_SSSE3                0x00010000 /* BRenderModern: Has SSSE3 */
#define HOST_CAPS_SSE4_1               0x00020000 /* BRenderModern: Has SSE4.1 */
#define HOST_CAPS_SSE4_2               0x00040000 /* BRenderModern: Has SSE4.2 */
#define HOST_CAPS_POPCNT               0x00080000 /* BRenderModern: Has POPCNT instruction  */
#define HOST_CAPS_AVX                  0x00080000 /* BRenderModern: Has AVX */

#ifndef _HOST_P_H_
#include "host_p.h"
#endif

#endif
