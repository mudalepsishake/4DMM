/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Mac standard header file - equivalent of windows.h.

***************************************************************************/

#ifndef SYMC
/* 3DMMv1.0: Wings includes Mac headers explicitly (this isn't all of them): */
// 3DMMv1.0: NOTE: avoid including Traps.h because it defines a bunch of stuff
// 3DMMv1.0: that we use as methods!
#define __TRAPS__

#include <types.h>
#include <errors.h>
#include <osutils.h>  // 3DMMv1.0: types.h
#include <memory.h>   // 3DMMv1.0: types.h
#include <diskinit.h> // 3DMMv1.0: types.h
#include <fonts.h>    // 3DMMv1.0: types.h
#include <quickdra.h> // 3DMMv1.0: types.h, qdtext.h
#include <qdoffscr.h> // 3DMMv1.0: quickdra.h
#include <textedit.h> // 3DMMv1.0: quickdra.h
#include <controls.h> // 3DMMv1.0: quickdra.h
#include <menus.h>    // 3DMMv1.0: quickdra.h
#include <events.h>   // 3DMMv1.0: types.h quickdra.h osutils.h
#include <desk.h>     // 3DMMv1.0: types.h quickdra.h events.h
#include <windows.h>  // 3DMMv1.0: quickdra.h events.h controls.h
#include <palettes.h> // 3DMMv1.0: quickdra.h windows.h
#include <dialogs.h>  // 3DMMv1.0: windows.h textedit.h
#include <files.h>    // 3DMMv1.0: types.h osutils.h segload.h
#include <resource.h> // 3DMMv1.0: types.h files.h
#include <resource.h> // 3DMMv1.0: types.h files.h
#include <folders.h>  // 3DMMv1.0: types.h files.h
#include <finder.h>   //
#include <standard.h> // 3DMMv1.0: types.h dialogs.h files.h
#include <script.h>   // 3DMMv1.0: types.h quickdra.h intlreso.h
#include <textutil.h> // 3DMMv1.0: types.h script.h osutils.h
#include <lowmem.h>
#else //! 3DMMv1.0: SYMC
#include <script.h>
#include <qdoffscreen.h>
#include <palettes.h>
#include <finder.h>
#define __cdecl
#define __pascal pascal
#define GetDialogItem GetDItem
#define GetDialogItemText GetIText
#define SetDialogItemText SetIText
#define UppercaseText(prgch, cch, scr) UpperText(prgch, cch)
#define LowercaseText(prgch, cch, scr) LowerText(prgch, cch)
#include <SysEqu.h>
inline int32_t LMGetHeapEnd(void)
{
    return *(int32_t *)ApplLimit;
}
inline int32_t LMGetCurrentA5(void)
{
    return *(int32_t *)CurrentA5;
}
#endif //! 3DMMv1.0: SYMC

typedef GrafPort PRT;
typedef GrafPort *PPRT;
typedef CGrafPort *PCPRT;
typedef GDHandle HGD;
typedef WindowRecord SWND;
typedef SWND *HWND;
typedef RgnHandle HRGN;
typedef GWorldPtr PGWR;
typedef PixMapHandle HPIX;
typedef BitMap *PBMP;
typedef PicHandle HPIC;
typedef PaletteHandle HPAL;
typedef CTabHandle HCLT;
