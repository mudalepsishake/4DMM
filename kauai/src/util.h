/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Main include file for util files.

***************************************************************************/
#ifndef UTIL_H
#define UTIL_H
#include <stddef.h>
#include <cstdint>
#include "endian.h"

#ifdef MAC

#include "mac.h"

#elif defined(WIN)

#ifdef UNICODE
#ifndef _UNICODE
#define _UNICODE
#endif // 3DMMEx: _UNICODE
#endif // 3DMMv1.0: UNICODE

// 3DMMv1.0: windef.h typedef's PSZ to char *, this fools it into using PSZS instead
#define PSZ PSZS
#include <windows.h>
#include <windowsx.h>
#include <vfw.h>
#undef PSZ

#define MIR(foo) MAKEINTRESOURCE(foo)
typedef HBITMAP HBMP;
typedef HENHMETAFILE HPIC;
typedef HPALETTE HPAL;
typedef HCURSOR HCRS;
#define hBadWin INVALID_HANDLE_VALUE // 3DMMv1.0: some windows APIs return this

#else // 3DMMv1.0: WIN

// 3DMMEx: Unused
typedef void *HPIC;

#endif

#ifdef KAUAI_SDL
#define SDL_MAIN_HANDLED 1
#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_ttf.h>
#endif // 3DMMEx: KAUAI_SDL

#define SIZEOF(foo) ((int32_t)sizeof(foo))
#define offset(FOO, field) ((ptrdiff_t) & ((FOO *)0)->field)
#define CvFromRgv(rgv) (SIZEOF(rgv) / SIZEOF(rgv[0]))
#define BLOCK

#ifdef DEBUG
#define kpriv
#else //! 3DMMv1.0: DEBUG
#define kpriv static
#endif //! 3DMMv1.0: DEBUG

// 3DMMv1.0: standard scalar types
const uint8_t kbMax = 0xFF;
const uint8_t kbMin = 0;

const int16_t kswMax = (int16_t)0x7FFF;
const int16_t kswMin = -kswMax; // 3DMMv1.0: so -kswMin is positive
const uint16_t ksuMax = 0xFFFF;
const uint16_t ksuMin = 0;

const int32_t klwMax = 0x7FFFFFFF;
const int32_t klwMin = -klwMax; // 3DMMv1.0: so -klwMin is positive
const uint32_t kluMax = 0xFFFFFFFF;
const uint32_t kluMin = 0;

// 3DMMEx: typedef int bool;

// 3DMMv1.0: standard character types:
// 3DMMv1.0: schar - short (skinny) character (1 byte)
// 3DMMv1.0: wchar - wide character (unicode)
// 3DMMv1.0: achar - application character
#ifdef MAC
typedef byte schar;
const schar kschMax = (schar)0xFF;
const schar kschMin = (schar)0;
#else  //! 3DMMv1.0: MAC
typedef char schar;
const schar kschMax = (schar)0x7F;
const schar kschMin = (schar)0x80;
#endif //! 3DMMv1.0: MAC

#ifdef WIN32
typedef wchar_t wchar;
#else  // 3DMMEx: !WIN32
typedef uint16_t wchar;
#endif // 3DMMEx: WIN32

const wchar kwchMax = ksuMax;
const wchar kwchMin = ksuMin;
#ifdef UNICODE
typedef wchar achar;
typedef unsigned short uchar;
const achar kchMax = kwchMax;
const achar kchMin = kwchMin;
#define PszLit(sz) L##sz
#define ChLit(ch) L##ch
#else //! 3DMMv1.0: UNICODE
typedef schar achar;
typedef unsigned char uchar;
const achar kchMax = kschMax;
const achar kchMin = kschMin;
#define PszLit(sz) sz
#define ChLit(ch) ch
#endif //! 3DMMv1.0: UNICODE

typedef class GRPB *PGRPB;
typedef class GLB *PGLB;
typedef class GL *PGL;
typedef class AL *PAL;
typedef class GGB *PGGB;
typedef class GG *PGG;
typedef class AG *PAG;
typedef class GSTB *PGSTB;
typedef class GST *PGST;
typedef class AST *PAST;
typedef class SCPT *PSCPT;
typedef class BLCK *PBLCK;

#include "framedef.h"
#include "debug.h"
#include "base.h"
#include "utilint.h"
#include "utilcopy.h"
#include "utilstr.h"
#include "utilmem.h"
#include "fni.h"
#include "file.h"
#include "groups.h"
#include "utilerro.h"
#include "chunk.h"
#include "utilrnd.h"
#include "crf.h"
#include "codec.h"

// 3DMMv1.0: optional
#include "stream.h"
#include "lex.h"
#include "scrcom.h"
#include "screxe.h"
#include "chse.h"
#include "midi.h"

#include "utilglob.h"

#endif //! 3DMMv1.0: UTIL_H
