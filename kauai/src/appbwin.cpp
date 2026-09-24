/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Windows base application class.

***************************************************************************/
#include <iostream>
#include "frame.h"
#include "fcntl.h"
#include "stdio.h"
#include "io.h"

ASSERTNAME

WIG vwig;

#ifdef WIN
static const char ksz4DMMExternalToolScaled200Prop[] = "4DMMExternalToolScaled200";
static int32_t v4DMMExternalToolScaleNum = 2;
static int32_t v4DMMExternalToolScaleDen = 1;
static int32_t v4DMMCustomCursorScaleNum = 0;
static int32_t v4DMMCustomCursorScaleDen = 0;

/***************************************************************************
    Set the startup scale used by native 4DMM auxiliary/tool windows. The
    command-line parser calls this before Studio creates any of those windows.
***************************************************************************/
void Set4DMMExternalToolScale(int32_t num, int32_t den)
{
    if (num <= 0 || den <= 0)
        return;
    v4DMMExternalToolScaleNum = num;
    v4DMMExternalToolScaleDen = den;
}

/***************************************************************************
    Return the current native/external-tool scale so a separately launched
    helper can use the exact same -gui_scale setting as 4DMM.
***************************************************************************/
void Get4DMMExternalToolScale(int32_t *pnum, int32_t *pden)
{
    if (pnum != pvNil)
        *pnum = v4DMMExternalToolScaleNum;
    if (pden != pvNil)
        *pden = v4DMMExternalToolScaleDen;
}

/***************************************************************************
    Override only the authored 3DMM main-window cursor scale. A zero pair
    clears the override so cursors once again follow the main -resolution
    presentation scale. Native Win32 cursors never use this value.
***************************************************************************/
void Set4DMMCustomCursorScale(int32_t num, int32_t den)
{
    if (num <= 0 || den <= 0)
    {
        v4DMMCustomCursorScaleNum = 0;
        v4DMMCustomCursorScaleDen = 0;
        return;
    }
    v4DMMCustomCursorScaleNum = num;
    v4DMMCustomCursorScaleDen = den;
}

/***************************************************************************
    Return one authored geometry coordinate for a native 4DMM auxiliary tool.

    The desired 200%-equivalent tool layout has two separate rules:
      - control/window geometry is 2x the authored Win32 coordinates;
      - fonts remain the normal native GUI font size.

    v72 scaled both geometry and fonts, which made the contents visually 2x too
    large. v73 stopped both transforms, which fixed text size but left the
    control layout and thumbnails occupying only the upper-left half of the
    already-correct 2x tool shell. Keep geometry and typography independent.
***************************************************************************/
int32_t Lw4DMMExternalToolUi200(HWND /*hwnd*/, int32_t lw)
{
    return MulDiv(lw, v4DMMExternalToolScaleNum, v4DMMExternalToolScaleDen);
}

/***************************************************************************
    Give one native 4DMM auxiliary window a fixed 200%-equivalent physical
    layout, independent from the main -resolution presentation scale.

    Scale the top-level client rectangle and child-control geometry to 2x, but
    deliberately leave every child control's font unchanged. This reproduces
    the target layout without the v72 double-sized text/check/radio problem.
***************************************************************************/
void Scale4DMMExternalToolWindow200(HWND hwnd)
{
    if (hwnd == hNil || !IsWindow(hwnd) || GetPropA(hwnd, ksz4DMMExternalToolScaled200Prop) != pvNil)
        return;

    HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

    struct CHILDGEOM
    {
        HWND hwnd;
        RECT rc;
    };
    CHILDGEOM rgchild[128];
    int32_t cchild = 0;
    for (HWND hwndChild = GetWindow(hwnd, GW_CHILD);
         hwndChild != hNil && cchild < (int32_t)(SIZEOF(rgchild) / SIZEOF(rgchild[0]));
         hwndChild = GetWindow(hwndChild, GW_HWNDNEXT))
    {
        RECT rc;
        if (!GetWindowRect(hwndChild, &rc))
            continue;
        MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&rc, 2);
        rgchild[cchild].hwnd = hwndChild;
        rgchild[cchild].rc = rc;
        ++cchild;
    }

    RECT rcClient;
    RECT rcWindow;
    if (!GetClientRect(hwnd, &rcClient) || !GetWindowRect(hwnd, &rcWindow))
        return;

    const int32_t dxpClient = MulDiv(rcClient.right - rcClient.left,
                                     v4DMMExternalToolScaleNum,
                                     v4DMMExternalToolScaleDen);
    const int32_t dypClient = MulDiv(rcClient.bottom - rcClient.top,
                                     v4DMMExternalToolScaleNum,
                                     v4DMMExternalToolScaleDen);
    RECT rcOuter = {0, 0, dxpClient, dypClient};
    const DWORD dwStyle = (DWORD)GetWindowLongPtrA(hwnd, GWL_STYLE);
    const DWORD dwExStyle = (DWORD)GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
    AdjustWindowRectEx(&rcOuter, dwStyle, GetMenu(hwnd) != hNil, dwExStyle);

    const int32_t dxpOuter = rcOuter.right - rcOuter.left;
    const int32_t dypOuter = rcOuter.bottom - rcOuter.top;
    int32_t xpWindow = rcWindow.left;
    int32_t ypWindow = rcWindow.top;

    MONITORINFO mi;
    ClearPb(&mi, SIZEOF(mi));
    mi.cbSize = SIZEOF(mi);
    if (hmon != hNil && GetMonitorInfo(hmon, &mi))
    {
        const int32_t dxpWork = mi.rcWork.right - mi.rcWork.left;
        const int32_t dypWork = mi.rcWork.bottom - mi.rcWork.top;
        if (dxpOuter >= dxpWork)
            xpWindow = mi.rcWork.left;
        else
            xpWindow = LwMax((int32_t)mi.rcWork.left,
                             LwMin(xpWindow, (int32_t)mi.rcWork.right - dxpOuter));
        if (dypOuter >= dypWork)
            ypWindow = mi.rcWork.top;
        else
            ypWindow = LwMax((int32_t)mi.rcWork.top,
                             LwMin(ypWindow, (int32_t)mi.rcWork.bottom - dypOuter));
    }

    SetWindowPos(hwnd, hNil, xpWindow, ypWindow, dxpOuter, dypOuter,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    for (int32_t i = 0; i < cchild; ++i)
    {
        const RECT &rc = rgchild[i].rc;
        const int32_t xp = MulDiv(rc.left, v4DMMExternalToolScaleNum, v4DMMExternalToolScaleDen);
        const int32_t yp = MulDiv(rc.top, v4DMMExternalToolScaleNum, v4DMMExternalToolScaleDen);
        const int32_t dxp = MulDiv(rc.right - rc.left,
                                   v4DMMExternalToolScaleNum,
                                   v4DMMExternalToolScaleDen);
        const int32_t dyp = MulDiv(rc.bottom - rc.top,
                                   v4DMMExternalToolScaleNum,
                                   v4DMMExternalToolScaleDen);
        SetWindowPos(rgchild[i].hwnd, hNil, xp, yp, dxp, dyp,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    SetPropA(hwnd, ksz4DMMExternalToolScaled200Prop, (HANDLE)1);
    InvalidateRect(hwnd, pvNil, fTrue);
}

static bool F4DMMUiScaleInfo(HWND *phwndScale, int32_t *pscaleNum, int32_t *pscaleDen)
{
    if (vwig.hwndApp == hNil || !IsWindow(vwig.hwndApp))
        return fFalse;

    // The customized Open/Save portfolio is a real Win32 common dialog owned
    // by the original Kauai HWND. While it is active, the source window must
    // behave exactly like ordinary 640x480 3DMM: no coordinate remapping, no
    // forced HWND_BOTTOM policy, and no activation redirect back to the scaled
    // presentation. utest.cpp sets this property for the complete portfolio
    // lifetime.
    if (GetPropA(vwig.hwndApp, "4DMMUiScaleSuspended") != pvNil)
        return fFalse;

    HWND hwndScale = (HWND)GetPropA(vwig.hwndApp, "4DMMUiScaleWindow");
    const int32_t scaleNum =
        (int32_t)(INT_PTR)GetPropA(vwig.hwndApp, "4DMMUiScaleNumerator");
    const int32_t scaleDen =
        (int32_t)(INT_PTR)GetPropA(vwig.hwndApp, "4DMMUiScaleDenominator");
    if (hwndScale == hNil || !IsWindow(hwndScale) || scaleNum <= scaleDen || scaleDen <= 0)
        return fFalse;

    if (phwndScale != pvNil)
        *phwndScale = hwndScale;
    if (pscaleNum != pvNil)
        *pscaleNum = scaleNum;
    if (pscaleDen != pvNil)
        *pscaleDen = scaleDen;
    return fTrue;
}

static HCURSOR Hcur4DMMScaledCustomCursor(HCURSOR hcurSource)
{
    if (hcurSource == hNil)
        return hNil;

    int32_t scaleNum;
    int32_t scaleDen;
    if (v4DMMCustomCursorScaleNum > 0 && v4DMMCustomCursorScaleDen > 0)
    {
        scaleNum = v4DMMCustomCursorScaleNum;
        scaleDen = v4DMMCustomCursorScaleDen;
    }
    else if (!F4DMMUiScaleInfo(pvNil, &scaleNum, &scaleDen))
    {
        return hcurSource;
    }

    struct SCALED_CURSOR_CACHE
    {
        HCURSOR hcurSource;
        HCURSOR hcurScaled;
        int32_t scaleNum;
        int32_t scaleDen;
    };
    static SCALED_CURSOR_CACHE rgcache[64] = {};
    static int32_t ccache = 0;

    for (int32_t i = 0; i < ccache; ++i)
    {
        if (rgcache[i].hcurSource == hcurSource && rgcache[i].scaleNum == scaleNum &&
            rgcache[i].scaleDen == scaleDen)
        {
            return rgcache[i].hcurScaled;
        }
    }

    ICONINFO iiSource;
    ClearPb(&iiSource, SIZEOF(iiSource));
    if (!GetIconInfo(hcurSource, &iiSource))
        return hcurSource;

    BITMAP bm;
    ClearPb(&bm, SIZEOF(bm));
    HBITMAP hbmSize = iiSource.hbmColor != hNil ? iiSource.hbmColor : iiSource.hbmMask;
    if (hbmSize == hNil || GetObjectA(hbmSize, SIZEOF(bm), &bm) == 0)
    {
        if (iiSource.hbmColor != hNil)
            DeleteObject(iiSource.hbmColor);
        if (iiSource.hbmMask != hNil)
            DeleteObject(iiSource.hbmMask);
        return hcurSource;
    }

    int32_t dxpSource = bm.bmWidth < 0 ? -bm.bmWidth : bm.bmWidth;
    int32_t dypSource = bm.bmHeight < 0 ? -bm.bmHeight : bm.bmHeight;
    if (iiSource.hbmColor == hNil)
        dypSource /= 2; // Monochrome cursor masks stack AND/XOR images vertically.

    const DWORD xpHotSource = iiSource.xHotspot;
    const DWORD ypHotSource = iiSource.yHotspot;
    if (iiSource.hbmColor != hNil)
        DeleteObject(iiSource.hbmColor);
    if (iiSource.hbmMask != hNil)
        DeleteObject(iiSource.hbmMask);

    if (dxpSource <= 0 || dypSource <= 0)
        return hcurSource;

    int32_t dxpScaled = MulDiv(dxpSource, scaleNum, scaleDen);
    int32_t dypScaled = MulDiv(dypSource, scaleNum, scaleDen);
    if (dxpScaled < 1)
        dxpScaled = 1;
    if (dypScaled < 1)
        dypScaled = 1;

    HCURSOR hcurResize = (HCURSOR)CopyImage(hcurSource, IMAGE_CURSOR, dxpScaled, dypScaled, 0);
    if (hcurResize == hNil)
        return hcurSource;

    // CopyImage scales the artwork, but rebuild the cursor with an explicitly
    // scaled hotspot so the authored click point follows the visible cursor at
    // fractional presentation scales too.
    HCURSOR hcurScaled = hcurResize;
    ICONINFO iiScaled;
    ClearPb(&iiScaled, SIZEOF(iiScaled));
    if (GetIconInfo(hcurResize, &iiScaled))
    {
        iiScaled.fIcon = fFalse;
        int32_t xpHot = MulDiv((int32_t)xpHotSource, scaleNum, scaleDen);
        int32_t ypHot = MulDiv((int32_t)ypHotSource, scaleNum, scaleDen);
        if (xpHot < 0)
            xpHot = 0;
        if (ypHot < 0)
            ypHot = 0;
        if (xpHot >= dxpScaled)
            xpHot = dxpScaled - 1;
        if (ypHot >= dypScaled)
            ypHot = dypScaled - 1;
        iiScaled.xHotspot = (DWORD)xpHot;
        iiScaled.yHotspot = (DWORD)ypHot;

        HCURSOR hcurHot = (HCURSOR)CreateIconIndirect(&iiScaled);
        if (iiScaled.hbmColor != hNil)
            DeleteObject(iiScaled.hbmColor);
        if (iiScaled.hbmMask != hNil)
            DeleteObject(iiScaled.hbmMask);
        if (hcurHot != hNil)
        {
            DestroyCursor(hcurResize);
            hcurScaled = hcurHot;
        }
    }

    if (ccache < (int32_t)(SIZEOF(rgcache) / SIZEOF(rgcache[0])))
    {
        rgcache[ccache].hcurSource = hcurSource;
        rgcache[ccache].hcurScaled = hcurScaled;
        rgcache[ccache].scaleNum = scaleNum;
        rgcache[ccache].scaleDen = scaleDen;
        ++ccache;
    }

    return hcurScaled;
}

static bool F4DMMMapGlobalPointFromScaledUi(POINT *ppt)
{
    AssertVarMem(ppt);

    HWND hwndScale;
    int32_t scaleNum;
    int32_t scaleDen;
    if (!F4DMMUiScaleInfo(&hwndScale, &scaleNum, &scaleDen))
        return fFalse;

    POINT ptScale = *ppt;
    if (!ScreenToClient(hwndScale, &ptScale))
        return fFalse;

    // TrackMouse consumes screen-space mouse samples, including samples just
    // outside the visible presentation while a hidden drag cursor is moving
    // quickly.  Those points still belong to the same scaled coordinate
    // system and must be transformed even when they are outside the client
    // rectangle.  Rejecting them here leaked raw physical desktop coordinates
    // into the logical 640x480 Kauai drag path.  A fast hand/reposition move
    // could therefore turn one ordinary edge sample into a delta thousands of
    // logical pixels wide before AdjustCursor had a chance to recenter it.
    //
    // The original unscaled TrackMouse path never rejects off-client screen
    // coordinates; it simply maps them through the GOB.  Preserve that exact
    // behavior by extending the presentation->source affine mapping beyond the
    // presentation bounds instead of treating an edge crossing as failure.
    POINT ptSource;
    ptSource.x = (ptScale.x * scaleDen) / scaleNum;
    ptSource.y = (ptScale.y * scaleDen) / scaleNum;
    if (!ClientToScreen(vwig.hwndApp, &ptSource))
        return fFalse;
    *ppt = ptSource;
    return fTrue;
}

static bool F4DMMMapSourceCursorToScaledUi(int32_t *pxpScreen, int32_t *pypScreen)
{
    AssertVarMem(pxpScreen);
    AssertVarMem(pypScreen);

    HWND hwndScale;
    int32_t scaleNum;
    int32_t scaleDen;
    if (!F4DMMUiScaleInfo(&hwndScale, &scaleNum, &scaleDen))
        return fFalse;

    POINT ptSource;
    ptSource.x = *pxpScreen;
    ptSource.y = *pypScreen;
    if (!ScreenToClient(vwig.hwndApp, &ptSource))
        return fFalse;
    RECT rcSource;
    if (!GetClientRect(vwig.hwndApp, &rcSource) || ptSource.x < rcSource.left || ptSource.y < rcSource.top ||
        ptSource.x >= rcSource.right || ptSource.y >= rcSource.bottom)
    {
        return fFalse;
    }

    POINT ptScale;
    // Map to the center of the source pixel's scaled footprint. This retains
    // the old integer-scale behavior and stays stable at fractional scales.
    ptScale.x = ((2 * ptSource.x + 1) * scaleNum) / (2 * scaleDen);
    ptScale.y = ((2 * ptSource.y + 1) * scaleNum) / (2 * scaleDen);
    if (!ClientToScreen(hwndScale, &ptScale))
        return fFalse;
    *pxpScreen = ptScale.x;
    *pypScreen = ptScale.y;
    return fTrue;
}
#endif

/* 3DMMEx:
 * Create debug console window and wire up std streams
 */
void APPB::CreateConsole()
{
    if (!AllocConsole())
    {
        return;
    }

    FILE *fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONIN$", "r", stdin);
    std::cout.clear();
    std::clog.clear();
    std::cerr.clear();
    std::cin.clear();
}

/** 3DMMv1.0: *************************************************************************
    Shutdown immediately.
***************************************************************************/
void APPB::Abort(void)
{
    _ShutDownViewer();
    FatalAppExit(0, PszLit("Fatal Error Termination"));
}

/** 3DMMv1.0: *************************************************************************
    Do OS specific initialization.
***************************************************************************/
bool APPB::_FInitOS(void)
{
    AssertThis(0);
    STN stnApp;
    PCSZ pszAppWndCls = PszLit("APP");

    // 3DMMv1.0: get the app name
    GetStnAppName(&stnApp);

    // 3DMMv1.0: register the window classes
    if (vwig.hinstPrev == hNil)
    {
        WNDCLASS wcs;

        wcs.style = CS_BYTEALIGNCLIENT | CS_OWNDC;
        wcs.lpfnWndProc = _LuWndProc;
        wcs.cbClsExtra = 0;
        wcs.cbWndExtra = 0;
        wcs.hInstance = vwig.hinst;
        wcs.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wcs.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcs.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcs.lpszMenuName = 0;
        wcs.lpszClassName = pszAppWndCls;
        if (!RegisterClass(&wcs))
            return fFalse;

        wcs.lpfnWndProc = _LuMdiWndProc;
        wcs.lpszClassName = PszLit("MDI");
        if (!RegisterClass(&wcs))
            return fFalse;
    }

    if ((vwig.hwndApp = CreateWindow(pszAppWndCls, stnApp.Psz(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                                     CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hNil, hNil, vwig.hinst, pvNil)) ==
        hNil)
    {
        return fFalse;
    }
    if (hNil == (vwig.hdcApp = GetDC(vwig.hwndApp)))
        return fFalse;

    // 3DMMv1.0: set a timer, so we can idle regularly.
    if (SetTimer(vwig.hwndApp, 0, 1, pvNil) == 0)
        return fFalse;

    vwig.haccel = LoadAccelerators(vwig.hinst, MIR(acidMain));
    ShowWindow(vwig.hwndApp, vwig.wShow);
    return fTrue;
}

/** 3DMMEx: *************************************************************************
    Initialize the sound manager.  Default is to return true whether or not
    we could create the sound manager.
***************************************************************************/
bool APPB::_FInitSound(int32_t wav)
{
    AssertBaseThis(0);
    PSNDV psndv;

    if (pvNil != vpsndm)
        return fTrue;

    // 3DMMEx: create the Sound manager
    if (pvNil == (vpsndm = SNDM::PsndmNew()))
        return fTrue;

#if defined(HAS_AUDIOMAN)
    if (pvNil != (psndv = SDAM::PsdamNew(wav)))
    {
        vpsndm->FAddDevice(kctgWave, psndv);
        ReleasePpo(&psndv);
    }
#endif // 3DMMEx: HAS_AUDIOMAN

    // 3DMMEx: create the midi playback device - use the stream one
    if (pvNil != (psndv = MDPS::PmdpsNew()))
    {
        vpsndm->FAddDevice(kctgMidi, psndv);
        ReleasePpo(&psndv);
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Get the next event from the OS event queue. Return true iff it's a
    real event (not just an idle type event).
***************************************************************************/
bool APPB::_FGetNextEvt(PEVT pevt)
{
    AssertThis(0);
    AssertVarMem(pevt);

    GetMessage(pevt, hNil, 0, 0);
    switch (pevt->message)
    {
    case WM_TIMER:
        return fFalse;

    case WM_MOUSEMOVE:
        // 3DMMv1.0: dispatch these so real Windows controls can receive them,
        // 3DMMv1.0: but return false so we can do our idle stuff - including
        // 3DMMv1.0: our own mouse moved stuff.
        _DispatchEvt(pevt);
        return fFalse;
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    The given GOB is tracking the mouse. See if there are any relevant
    mouse events in the system event queue. Fill in *ppt with the location
    of the mouse relative to pgob. Also ensure that GrfcustCur() will
    return the correct mouse state.
***************************************************************************/
void APPB::TrackMouse(PGOB pgob, PT *ppt)
{
    AssertThis(0);
    AssertPo(pgob, 0);
    AssertVarMem(ppt);

    EVT evt;
    POINT pts;

    for (;;)
    {
#ifdef WIN
        // v49: native BRender frames invalidate the 4x presentation window,
        // but WM_PAINT is a low-priority message and can starve indefinitely
        // while this legacy TrackMouse loop continually consumes mouse input.
        // Flush only an already-pending presentation paint here. UpdateWindow
        // is a no-op when no frame/UI invalidation is pending, and the actual
        // paint path only blits the completed renderer cache.
        HWND hwndScalePaint;
        if (F4DMMUiScaleInfo(&hwndScalePaint, pvNil, pvNil))
            UpdateWindow(hwndScalePaint);
#endif
        if (!PeekMessage(&evt, hNil, 0, 0, PM_REMOVE | PM_NOYIELD))
        {
            GetCursorPos(&pts);
            break;
        }

        if (FIn(evt.message, WM_MOUSEFIRST, WM_MOUSELAST + 1))
        {
            pts = evt.pt;
            break;
        }

        // 3DMMv1.0: toss key events
        if (!FIn(evt.message, WM_KEYFIRST, WM_KEYLAST + 1))
        {
            TranslateMessage(&evt);
            DispatchMessage(&evt);
        }
    }

#ifdef WIN
    // Preserve the original queued-event tracking semantics. Only if a scaled
    // presentation sample cannot be mapped (for example, a stale edge packet
    // immediately after a hidden-cursor recenter) retry once from the live
    // cursor. This prevents physical presentation coordinates from leaking
    // into the logical 640x480 drag path without changing normal drag deltas.
    if (!F4DMMMapGlobalPointFromScaledUi(&pts))
    {
        POINT ptsLive;
        if (GetCursorPos(&ptsLive) && F4DMMMapGlobalPointFromScaledUi(&ptsLive))
            pts = ptsLive;
    }
#endif
    ppt->xp = pts.x;
    ppt->yp = pts.y;
    pgob->MapPt(ppt, cooGlobal, cooLocal);
}

/** 3DMMv1.0: *************************************************************************
    Dispatch an OS level event to someone that knows what to do with it.
***************************************************************************/
void APPB::_DispatchEvt(PEVT pevt)
{
    AssertThis(0);
    AssertVarMem(pevt);

    CMD cmd;

#ifdef WIN
    // The 4x presentation window is a real top-level HWND, but the Kauai
    // application continues to own keyboard semantics on its original
    // 640x480 source HWND. Redirect only keyboard events from the scaled
    // presentation (or its native GL child) before the normal accelerator/
    // cidKey pipeline sees them. This keeps every existing shortcut intact.
    HWND hwndScaleInput;
    if (FIn(pevt->message, WM_KEYFIRST, WM_KEYLAST + 1) &&
        F4DMMUiScaleInfo(&hwndScaleInput, pvNil, pvNil) &&
        (pevt->hwnd == hwndScaleInput || IsChild(hwndScaleInput, pevt->hwnd)))
    {
        pevt->hwnd = vwig.hwndApp;
    }
#endif

    // Actor Studio spans two native top-level HWNDs plus ordinary list/button
    // children. Route its editing keys synchronously before Kauai's global
    // accelerator table can reinterpret Ctrl+Q as Quit, Ctrl+S as Save, etc.
    // This replaces the failed RegisterHotKey/GetAsyncKeyState polling paths
    // without changing a single main-Studio accelerator outside Actor Studio.
    if (pevt->message == WM_KEYDOWN || pevt->message == WM_SYSKEYDOWN)
    {
        HWND hwndRoot = GetAncestor(pevt->hwnd, GA_ROOT);
        if (hwndRoot != hNil)
        {
            achar szClass[64];
            szClass[0] = 0;
            GetClassName(hwndRoot, szClass, (int)(sizeof(szClass) / sizeof(szClass[0])));
            const bool fActorStudio =
                0 == lstrcmpi(szClass, PszLit("4DMMActorStudioWindow")) ||
                0 == lstrcmpi(szClass, PszLit("4DMMActorStudioViewportWindow"));
            if (fActorStudio)
            {
                const bool fCtrl = GetKeyState(VK_CONTROL) < 0;
                const bool fShift = GetKeyState(VK_SHIFT) < 0;
                const bool fAlt = GetKeyState(VK_MENU) < 0;
                const WPARAM vk = pevt->wParam;
                const bool fPlainTool = !fCtrl && !fAlt &&
                    (vk == 'Q' || vk == 'W' || vk == 'E' || vk == 'R' || vk == 'T' ||
                     vk == '1' || vk == '2' || vk == VK_DELETE || vk == VK_ESCAPE || vk == VK_SPACE);
                const bool fCtrlTool = fCtrl && !fAlt &&
                    (vk == 'Q' || vk == 'W' || vk == 'E' || vk == 'R' ||
                     vk == 'A' || vk == 'S' || vk == 'D' || vk == 'T' ||
                     vk == '1' || vk == '2' || vk == 'C' || vk == 'V' || vk == 'X' || vk == 'G' ||
                     vk == VK_DELETE);
                const bool fFrameNav = fCtrl && !fAlt && (vk == VK_UP || vk == VK_DOWN);
                const bool fPartNav = fAlt && !fCtrl && (vk == VK_UP || vk == VK_DOWN);
                const bool fLastListNav = !fCtrl && !fAlt && (vk == VK_UP || vk == VK_DOWN);
                if (fPlainTool || fCtrlTool || fFrameNav || fPartNav || fLastListNav)
                {
                    LPARAM grf = (fCtrl ? 0x01 : 0) | (fShift ? 0x02 : 0) | (fAlt ? 0x04 : 0);
                    if (SendMessage(hwndRoot, WM_APP + 0x052, vk, grf) != 0)
                        return;
                }
            }
        }
    }

    // Native browser/camera-editor hotkeys must work while keyboard focus is
    // inside one of their child list/edit/button HWNDs. Route the chord to the
    // owning tool window before Kauai or a native edit control consumes it.
    if (pevt->message == WM_KEYDOWN && GetAsyncKeyState(VK_CONTROL) < 0)
    {
        HWND hwndRoot = GetAncestor(pevt->hwnd, GA_ROOT);
        if (hwndRoot != hNil && hwndRoot != vwig.hwndApp)
        {
            achar szClass[64];
            szClass[0] = 0;
            GetClassName(hwndRoot, szClass, (int)(sizeof(szClass) / sizeof(szClass[0])));

            if ((pevt->wParam == 'F') &&
                0 == lstrcmpi(szClass, PszLit("3DMMExExternalContentBrowser")))
            {
                if (SendMessage(hwndRoot, WM_APP + 0x4C, 0, 0) != 0)
                    return;
            }
            else if ((pevt->wParam == 'D') && GetAsyncKeyState(VK_MENU) >= 0 &&
                     (0 == lstrcmpi(szClass, PszLit("3DMMExDepthMotionTweenEditor")) ||
                      0 == lstrcmpi(szClass, PszLit("3DMMExManualCameraFrameEditor"))))
            {
                SendMessage(hwndRoot, WM_APP + 0x4F, 0, 0);
                return;
            }
        }
    }

    // APP::TModal marks only the classic OK-only Kauai error balloons with
    // this property. Let Escape activate their normal default OK action rather
    // than introducing a second modal-dismissal implementation.
    if (pevt->message == WM_KEYDOWN && pevt->wParam == VK_ESCAPE &&
        vwig.hwndApp != hNil && GetPropA(vwig.hwndApp, "4DMMExEscapeDismissOkModal") != pvNil)
    {
        pevt->wParam = VK_RETURN;
    }

    // Native 4DMM editors/browsers are top-level WS_EX_TOOLWINDOW windows.
    // Escape should close whichever one currently owns keyboard focus, even
    // when that focus is inside a child button/list/edit control.  Intercept
    // the key before Kauai translates it into cidKey.  The separate -v
    // viewport is explicitly excluded and remains immune to Escape.
    if (pevt->message == WM_KEYDOWN && pevt->wParam == VK_ESCAPE)
    {
        HWND hwndRoot = GetAncestor(pevt->hwnd, GA_ROOT);
        if (hwndRoot != hNil && hwndRoot != vwig.hwndApp)
        {
            DWORD dwProcess = 0;
            GetWindowThreadProcessId(hwndRoot, &dwProcess);
            if (dwProcess == GetCurrentProcessId() &&
                (GetWindowLongPtr(hwndRoot, GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
            {
                achar szClass[64];
                szClass[0] = 0;
                GetClassName(hwndRoot, szClass, (int)(sizeof(szClass) / sizeof(szClass[0])));
                if (0 != lstrcmpi(szClass, PszLit("3DMMExViewportWnd")))
                {
                    // Light Lab deliberately uses Escape as Save/Apply. The
                    // title-bar close button remains the non-destructive Cancel
                    // path. Forward a private message rather than the generic
                    // WS_EX_TOOLWINDOW WM_CLOSE behavior below.
                    if (0 == lstrcmpi(szClass, PszLit("4DMMLightLabEditor")) ||
                        0 == lstrcmpi(szClass, PszLit("4DMMObjectPropertiesEditor")))
                    {
                        SendMessage(hwndRoot, WM_APP + 0x4E, 0, 0);
                        return;
                    }

                    // Object browsers consume the first Escape when they have
                    // a selected object: deselect there, then let a second
                    // Escape close the browser. Keep Kauai independent from
                    // Studio by using a small private window message contract.
                    if (0 == lstrcmpi(szClass, PszLit("3DMMExExternalContentBrowser")) &&
                        SendMessage(hwndRoot, WM_APP + 0x4D, 0, 0) != 0)
                    {
                        return;
                    }
                    PostMessage(hwndRoot, WM_CLOSE, 0, 0);
                    return;
                }
            }
        }
    }

    // Most Kauai windows intentionally translate keyboard input into cidKey
    // commands.  Native edit controls need the ordinary Win32 message path or
    // they can receive focus and mouse selection but never receive typed text.
    // movie.cpp marks the Depth Motion Tween editor with this property.
    if (FIn(pevt->message, WM_KEYFIRST, WM_KEYLAST + 1) &&
        GetPropA(pevt->hwnd, "3DMMExNativeTextInput") != pvNil)
    {
        TranslateMessage(pevt);
        DispatchMessage(pevt);
        return;
    }

    if (kwndNil != vwig.hwndClient && TranslateMDISysAccel(vwig.hwndClient, pevt) ||
        hNil != vwig.haccel && TranslateAccelerator(vwig.hwndApp, vwig.haccel, pevt))
    {
        return;
    }

    switch (pevt->message)
    {
    case WM_KEYDOWN:
    case WM_CHAR:
        if (_FTranslateKeyEvt(pevt, (PCMD_KEY)&cmd) && pvNil != vpcex)
            vpcex->EnqueueCmd(&cmd);
        ResetToolTip();
        break;
    case WM_SYSKEYDOWN:
        // 3DMMv1.0: fall thru
    case WM_SYSCHAR:
        if (_FTranslateKeyEvt(pevt, (PCMD_KEY)&cmd) && pvNil != vpcex)
        {
            vpcex->EnqueueCmd(&cmd);
        }
        // 3DMMEx: Fall through to ensure system key down events are still dispatched
        // 3DMMEx: Otherwise we will break system hotkeys like Alt-Space.
    case WM_KEYUP:
    case WM_DEADCHAR:
    case WM_SYSKEYUP:
    case WM_SYSDEADCHAR:
        ResetToolTip();
        // 3DMMv1.0: fall thru
    default:
        TranslateMessage(pevt);
        DispatchMessage(pevt);
        break;
    }
}

/** 3DMMv1.0: *************************************************************************
    Translate an OS level key down event to a CMD. This returns false if
    the key maps to a menu item.
***************************************************************************/
bool APPB::_FTranslateKeyEvt(PEVT pevt, PCMD_KEY pcmd)
{
    AssertThis(0);
    AssertVarMem(pevt);
    AssertVarMem(pcmd);

    EVT evt;
    UINT wmChar = 0;

    ClearPb(pcmd, SIZEOF(*pcmd));
    pcmd->cid = cidKey;

    if (pevt->message == WM_KEYDOWN)
    {
        wmChar = WM_CHAR;
    }
    else if (pevt->message == WM_SYSKEYDOWN)
    {
        wmChar = WM_SYSCHAR;
    }

    if (wmChar != 0)
    {
        TranslateMessage(pevt);
        if (PeekMessage(&evt, pevt->hwnd, 0, 0, PM_NOREMOVE) &&
            (wmChar == evt.message && PeekMessage(&evt, pevt->hwnd, wmChar, wmChar, PM_REMOVE)))
        {
            Assert(evt.message == wmChar, 0);
            pcmd->ch = evt.wParam;
        }
        else
            pcmd->ch = chNil;
        pcmd->vk = pevt->wParam;
    }
    else
    {
        pcmd->vk = vkNil;
        pcmd->ch = pevt->wParam;
    }
    pcmd->grfcust = GrfcustCur();
    pcmd->cact = SwLow(pevt->lParam);

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Look at the next system event and if it's a key, fill in the *pcmd with
    the relevant info.
***************************************************************************/
bool APPB::FGetNextKeyFromOsQueue(PCMD_KEY pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    EVT evt;

    for (;;)
    {
        if (!PeekMessage(&evt, hNil, 0, 0, PM_NOREMOVE) || !FIn(evt.message, WM_KEYFIRST, WM_KEYLAST + 1) ||
            !PeekMessage(&evt, evt.hwnd, evt.message, evt.message, PM_REMOVE))
        {
            break;
        }

        if (kwndNil != vwig.hwndClient && TranslateMDISysAccel(vwig.hwndClient, &evt) ||
            hNil != vwig.haccel && TranslateAccelerator(vwig.hwndApp, vwig.haccel, &evt))
        {
            break;
        }

        switch (evt.message)
        {
        case WM_CHAR:
        case WM_KEYDOWN:
            if (!_FTranslateKeyEvt(&evt, pcmd))
                goto LFail;
            return fTrue;

        default:
            TranslateMessage(&evt);
            DispatchMessage(&evt);
            break;
        }
    }

LFail:
    TrashVar(pcmd);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Flush user generated events from the system event queue.
***************************************************************************/
void APPB::FlushUserEvents(uint32_t grfevt)
{
    AssertThis(0);
    EVT evt;

    while ((grfevt & fevtMouse) && PeekMessage(&evt, hNil, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE) ||
           (grfevt & fevtKey) && PeekMessage(&evt, hNil, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
    {
    }
}

/** 3DMMv1.0: *************************************************************************
    Get our app window out of the clipboard viewer chain.
***************************************************************************/
void APPB::_ShutDownViewer(void)
{
    if (vwig.hwndApp != kwndNil)
        ChangeClipboardChain(vwig.hwndApp, vwig.hwndNextViewer);
    vwig.hwndNextViewer = kwndNil;
}

/** 3DMMv1.0: *************************************************************************
    Main window procedure (a static method).
***************************************************************************/
LRESULT CALLBACK APPB::_LuWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lw)
{
    AssertNilOrPo(vpappb, 0);
    int32_t lwRet;

    if (pvNil != vpappb && vpappb->_FFrameWndProc(hwnd, wm, wParam, lw, &lwRet))
    {
        return lwRet;
    }

    return DefFrameProc(hwnd, vwig.hwndClient, wm, wParam, lw);
}

/** 3DMMv1.0: *************************************************************************
    Handle Windows messages for the main app window. Return true iff the
    default window proc should _NOT_ be called.
***************************************************************************/
bool APPB::_FFrameWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lw, int32_t *plwRet)
{
    AssertThis(0);
    AssertVarMem(plwRet);

    PGOB pgob;
    RC rc;
    PT pt;
    int32_t xp, yp;
    int32_t lwT;
    int32_t lwStyle;

    *plwRet = 0;
    switch (wm)
    {
    default:
        return _FCommonWndProc(hwnd, wm, wParam, lw, plwRet);

    case WM_CREATE:
        Assert(vwig.hwndApp == kwndNil, 0);
        vwig.hwndNextViewer = SetClipboardViewer(hwnd);
        vwig.hwndApp = hwnd;
        return fTrue;

    case WM_CHANGECBCHAIN:
        if ((HWND)wParam == vwig.hwndNextViewer)
            vwig.hwndNextViewer = (HWND)lw;
        else if (kwndNil != vwig.hwndNextViewer)
            SendMessage(vwig.hwndNextViewer, wm, wParam, lw);
        return fTrue;

    case WM_DRAWCLIPBOARD:
        if (kwndNil != vwig.hwndNextViewer)
            SendMessage(vwig.hwndNextViewer, wm, wParam, lw);
        if (vwig.hwndApp != kwndNil && GetClipboardOwner() != vwig.hwndApp)
            vpclip->Import();
        return fTrue;

    case WM_DESTROY:
        _ShutDownViewer();
        vwig.hwndApp = kwndNil;
        PostQuitMessage(0);
        return fTrue;

    case WM_SIZE:
        // 3DMMv1.0: make sure the style bits are set correctly
        lwT = lwStyle = GetWindowLong(vwig.hwndApp, GWL_STYLE);
        if (_fFullScreen && wParam == SIZE_MAXIMIZED)
        {
            // 3DMMv1.0: in full screen mode, set popup and nuke the system menu stuff
            lwStyle |= WS_POPUP;
            lwStyle &= ~(WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
        }
        else
        {
            // 3DMMv1.0: in non-full screen mode, clear popup and set the system menu stuff
            lwStyle &= ~WS_POPUP;
            lwStyle |= (WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
        }
        if (lwT != lwStyle)
            SetWindowLong(vwig.hwndApp, GWL_STYLE, lwStyle);

        return _FCommonWndProc(hwnd, wm, wParam, lw, plwRet);

    case WM_PALETTECHANGED:
        if ((HWND)wParam == hwnd)
            return fTrue;
        // 3DMMv1.0: fall thru
    case WM_QUERYNEWPALETTE:
        *plwRet = GPT::CclrSetPalette(hwnd, fTrue) > 0;
        return fTrue;

    case WM_GETMINMAXINFO:
        BLOCK
        {
            int32_t dypFrame, dypScreen, dypExtra;
            MINMAXINFO *pmmi;

            pmmi = (MINMAXINFO *)lw;

            *plwRet = DefFrameProc(hwnd, vwig.hwndClient, wm, wParam, reinterpret_cast<LPARAM>(pmmi));
            dypFrame = GetSystemMetrics(SM_CYFRAME);
            dypScreen = GetSystemMetrics(SM_CYSCREEN);
            dypExtra = 0;

            FGetProp(kpridFullScreen, &lwT);
            if (lwT)
                dypExtra = GetSystemMetrics(SM_CYCAPTION);
            pmmi->ptMaxPosition.y = -dypFrame - dypExtra;
            pmmi->ptMaxSize.y = pmmi->ptMaxTrackSize.y = dypScreen + 2 * dypFrame + dypExtra;
            *plwRet = lwT;
            _FCommonWndProc(hwnd, wm, wParam, (LPARAM)pmmi, plwRet);
        }
        return fTrue;

    case WM_CLOSE:
        if (pvNil != vpcex)
            vpcex->EnqueueCid(cidQuit);
        return fTrue;

    case WM_QUERYENDSESSION:
        if (!_fQuit)
            Quit(fFalse);
        *plwRet = _fQuit;
        return fTrue;

    case WM_COMMAND:
        if (GET_WM_COMMAND_HWND(wParm, lw) != hNil)
            break;

        lwT = GET_WM_COMMAND_ID(wParam, lw);
        if (!FIn(lwT, wcidMinApp, wcidLimApp))
            break;

        if (pvNil != vpmubCur)
            vpmubCur->EnqueueWcid(lwT);
        else if (pvNil != vpcex)
            vpcex->EnqueueCid(lwT);
        return fTrue;

    case WM_INITMENU:
        if (vpmubCur != pvNil)
        {
            vpmubCur->Clean();
            return fTrue;
        }
        break;

    // 3DMMv1.0: these are for automated testing support...
    case WM_GOB_STATE:
        if (pvNil != (pgob = GOB::PgobFromHidScr(lw)))
            *plwRet = pgob->LwState();
        return fTrue;

    case WM_GOB_LOCATION:
        *plwRet = -1;
        if (pvNil == (pgob = GOB::PgobFromHidScr(lw)))
            return fTrue;

        pgob->GetRcVis(&rc, cooLocal);
        if (rc.FEmpty())
            return fTrue;
        pt.xp = pt.yp = 0;
        pgob->MapPt(&pt, cooLocal, cooGlobal);
        for (lwT = 0; lwT < 256; lwT++)
        {
            for (yp = rc.ypTop + (lwT & 0x0F); yp < rc.ypBottom; yp += 16)
            {
                for (xp = rc.xpLeft + (lwT >> 4); xp < rc.xpRight; xp += 16)
                {
                    if (pgob->FPtIn(xp, yp) && pgob == GOB::PgobFromPtGlobal(xp + pt.xp, yp + pt.yp))
                    {
                        pt.xp += xp;
                        pt.yp += yp;
                        GOB::PgobScreen()->MapPt(&pt, cooGlobal, cooLocal);
                        *plwRet = LwHighLow((int16_t)pt.xp, (int16_t)pt.yp);
                        return fTrue;
                    }
                }
            }
        }
        return fTrue;

    case WM_GLOBAL_STATE:
        *plwRet = GrfcustCur();
        return fTrue;

    case WM_CURRENT_CURSOR:
        if (pvNil != _pcurs)
            *plwRet = _pcurs->Cno();
        else
            *plwRet = cnoNil;
        return fTrue;

    case WM_GET_PROP:
        if (!FGetProp(lw, plwRet))
            *plwRet = wParam;
        return fTrue;

    case WM_SCALE_TIME:
        *plwRet = vpusac->LuScale();
        vpusac->Scale(lw);
        return fTrue;

    case WM_GOB_FROM_PT:
        pt.xp = wParam;
        pt.yp = lw;
        GOB::PgobScreen()->MapPt(&pt, cooLocal, cooGlobal);
        if (pvNil != (pgob = GOB::PgobFromPtGlobal(pt.xp, pt.yp)))
            *plwRet = pgob->Hid();
        return fTrue;

    case WM_FIRST_CHILD:
        if (pvNil != (pgob = GOB::PgobFromHidScr(lw)) && pvNil != (pgob = pgob->PgobFirstChild()))
        {
            *plwRet = pgob->Hid();
        }
        return fTrue;

    case WM_NEXT_SIB:
        if (pvNil != (pgob = GOB::PgobFromHidScr(lw)) && pvNil != (pgob = pgob->PgobNextSib()))
        {
            *plwRet = pgob->Hid();
        }
        return fTrue;

    case WM_PARENT:
        if (pvNil != (pgob = GOB::PgobFromHidScr(lw)) && pvNil != (pgob = pgob->PgobPar()))
        {
            *plwRet = pgob->Hid();
        }
        return fTrue;

    case WM_GOB_TYPE:
        if (pvNil != (pgob = GOB::PgobFromHidScr(lw)))
            *plwRet = pgob->Cls();
        return fTrue;

    case WM_IS_GOB:
        if (pvNil != (pgob = GOB::PgobFromHidScr(lw)))
            *plwRet = pgob->FIs(wParam);
        return fTrue;
    }

    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    MDI window proc (a static method).
***************************************************************************/
LRESULT CALLBACK APPB::_LuMdiWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lw)
{
    AssertNilOrPo(vpappb, 0);
    int32_t lwRet;

    if (pvNil != vpappb && vpappb->_FMdiWndProc(hwnd, wm, wParam, lw, &lwRet))
    {
        return lwRet;
    }

    return DefMDIChildProc(hwnd, wm, wParam, lw);
}

/** 3DMMv1.0: *************************************************************************
    Handle MDI window messages. Returns true iff the default window proc
    should _NOT_ be called.
***************************************************************************/
bool APPB::_FMdiWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lw, int32_t *plwRet)
{
    AssertThis(0);
    AssertVarMem(plwRet);

    PGOB pgob;
    int32_t lwT;

    *plwRet = 0;
    switch (wm)
    {
    default:
        return _FCommonWndProc(hwnd, wm, wParam, lw, plwRet);

    case WM_GETMINMAXINFO:
        *plwRet = DefMDIChildProc(hwnd, wm, wParam, lw);
        _FCommonWndProc(hwnd, wm, wParam, lw, &lwT);
        return fTrue;

    case WM_CLOSE:
        if ((pgob = GOB::PgobFromHwnd(hwnd)) != pvNil)
            vpcex->EnqueueCid(cidCloseWnd, pgob);
        return fTrue;

    case WM_MDIACTIVATE:
        GOB::ActivateHwnd(hwnd, GET_WM_MDIACTIVATE_FACTIVATE(hwnd, wParam, lw));
        break;
    }

    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Common stuff between the two window procs. Returns true if the default
    window proc should _NOT_ be called.
***************************************************************************/
bool APPB::_FCommonWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lw, int32_t *plwRet)
{
    AssertThis(0);
    AssertVarMem(plwRet);

    PGOB pgob;
    PT pt;
    PSCB pscb;
    RC rc;
    HDC hdc;
    PAINTSTRUCT ps;
    HRGN hrgn;

    *plwRet = 0;
    switch (wm)
    {
#ifdef WIN
    case WM_WINDOWPOSCHANGING:
        // -resolution 4x keeps the original 640x480 Kauai HWND alive as the
        // DWM source surface. v51 keeps that source in normal desktop
        // composition immediately underneath the scaled presentation rather
        // than exiling it off-desktop/at HWND_BOTTOM. Modal/loading transitions
        // can still try to promote it, so preserve the presentation-above-source
        // ordering while 4x mode is active.
        if (hwnd == vwig.hwndApp)
        {
            HWND hwndScale;
            if (F4DMMUiScaleInfo(&hwndScale, pvNil, pvNil) && hwndScale != hNil && IsWindow(hwndScale))
            {
                WINDOWPOS *pwp = (WINDOWPOS *)lw;
                if (pwp != pvNil && !(pwp->flags & SWP_NOZORDER))
                    pwp->hwndInsertAfter = hwndScale;
            }
        }
        break;

    case WM_ACTIVATE:
        if (hwnd == vwig.hwndApp && LOWORD(wParam) != WA_INACTIVE)
        {
            HWND hwndScale;
            if (F4DMMUiScaleInfo(&hwndScale, pvNil, pvNil) && hwndScale != hNil && IsWindow(hwndScale))
                SetWindowPos(hwndScale, HWND_TOP, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        break;
#endif

    case WM_PAINT:
        if (IsIconic(hwnd))
            break;

        // 3DMMv1.0: make sure the palette is selected and realized....
        // 3DMMv1.0: theoretically, we shouldn't have to do this, but because
        // 3DMMv1.0: of past and present Win bugs, we do it to be safe.
        GPT::CclrSetPalette(hwnd, fFalse);

        // 3DMMv1.0: invalidate stuff that we have marked internally (may as well
        // 3DMMv1.0: draw everything that needs drawn).
        InvalMarked(hwnd);

        // 3DMMv1.0: NOTE: BeginPaint has a bug where it returns in ps.rcPaint the
        // 3DMMv1.0: bounds of the update region intersected with the current clip region.
        // 3DMMv1.0: This causes us to not draw everything we need to. To fix this we
        // 3DMMv1.0: save, open up, and restore the clipping region around the BeginPaint
        // 3DMMv1.0: call.
        hdc = GetDC(hwnd);
        if (hNil == hdc)
            goto LFailPaint;

        if (FCreateRgn(&hrgn, pvNil) && 1 != GetClipRgn(hdc, hrgn))
            FreePhrgn(&hrgn);
        SelectClipRgn(hdc, hNil);

        if (!BeginPaint(hwnd, &ps))
        {
            ReleaseDC(hwnd, hdc);
        LFailPaint:
            Warn("Painting failed");
            break;
        }

        // 3DMMv1.0: Since we use CS_OWNDC, these DCs should be the same...
        Assert(hdc == ps.hdc, 0);

        rc = RC(ps.rcPaint);
        UpdateHwnd(hwnd, &rc);
        EndPaint(hwnd, &ps);

        // 3DMMv1.0: don't call the default window proc - or it will clear anything
        // 3DMMv1.0: that got invalidated while we were drawing (which can happen
        // 3DMMv1.0: in a multi-threaded pre-emptive environment).
        return fTrue;

    case WM_SYSCOMMAND:
        if (wParam == SC_SCREENSAVE && !FAllowScreenSaver())
            return fTrue;
        break;

    case WM_GETMINMAXINFO:
        if (pvNil != (pgob = GOB::PgobFromHwnd(hwnd)))
        {
            MINMAXINFO *pmmi = (MINMAXINFO far *)lw;

            pgob->GetMinMax(&rc);
            pmmi->ptMinTrackSize.x = LwMax(pmmi->ptMinTrackSize.x, rc.xpLeft);
            pmmi->ptMinTrackSize.y = LwMax(pmmi->ptMinTrackSize.y, rc.ypTop);
            pmmi->ptMaxTrackSize.x = LwMin(pmmi->ptMaxTrackSize.x, rc.xpRight);
            pmmi->ptMaxTrackSize.y = LwMin(pmmi->ptMaxTrackSize.y, rc.ypBottom);
        }
        return fTrue;

    case WM_SIZE:
        if (pvNil != (pgob = GOB::PgobFromHwnd(hwnd)))
            pgob->SetRcFromHwnd();
        break;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:

        // 3DMMEx: Avoid duplicate mouse messages if a GOB is tracking the mouse
        if (vpcex->PgobTracking() != pvNil)
        {
            break;
        }

        ResetToolTip();
        if (pvNil != (pgob = GOB::PgobFromHwnd(hwnd)) && pvNil != (pgob = pgob->PgobFromPt(SwLow(lw), SwHigh(lw), &pt)))
        {
            int32_t ts;

            // 3DMMv1.0: compute the multiplicity of the click - don't use Windows'
            // 3DMMv1.0: guess, since it can be wrong for our GOBs. It's even wrong
            // 3DMMv1.0: at the HWND level! (Try double-clicking the maximize button).
            ts = GetMessageTime();
            if (_pgobMouse == pgob && FIn(ts - _tsMouse, 0, GetDoubleClickTime()))
            {
                _cactMouse++;
            }
            else
                _cactMouse = 1;
            _tsMouse = ts;
            if (_pgobMouse != pgob && pvNil != _pgobMouse)
            {
                AssertPo(_pgobMouse, 0);
                vpcex->EnqueueCid(cidRollOff, _pgobMouse);
            }
            _pgobMouse = pgob;
            _xpMouse = klwMax;
            pgob->MouseDown(pt.xp, pt.yp, _cactMouse, GrfcustCur());
        }
        else
            _pgobMouse = pvNil;
        break;

    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
        ResetToolTip();
        break;

    case WM_SETCURSOR:
        if (LOWORD(lw) != HTCLIENT)
            return fFalse;
        RefreshCurs();
        return fTrue;

    case WM_HSCROLL:
    case WM_VSCROLL:
        pscb = (PSCB)CTL::PctlFromHctl(GET_WM_HSCROLL_HWND(wParam, lw));
        if (pvNil != pscb && pscb->FIs(kclsSCB))
        {
            pscb->TrackScroll(GET_WM_HSCROLL_CODE(wParam, lw), GET_WM_HSCROLL_POS(wParam, lw));
        }
        break;

    case WM_ACTIVATEAPP:
        _Activate(FPure(wParam));
        break;
    }

    return fFalse;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Debug initialization.
***************************************************************************/
bool APPB::_FInitDebug(void)
{
    AssertThis(0);
    return fTrue;
}

// 3DMMv1.0: passes the strings to the assert dialog proc
STN *_rgpstn[3];

/** 3DMMv1.0: *************************************************************************
    Dialog proc for assert.
***************************************************************************/
INT_PTR CALLBACK _FDlgAssert(HWND hdlg, UINT msg, WPARAM w, LPARAM lw)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        SetDlgItemText(hdlg, 3, _rgpstn[0]->Psz());
        SetDlgItemText(hdlg, 4, _rgpstn[1]->Psz());
        SetDlgItemText(hdlg, 5, _rgpstn[2]->Psz());
        return fTrue;

    case WM_COMMAND:
        switch (GET_WM_COMMAND_ID(w, lw))
        {
        default:
            break;

        case 0:
        case 1:
        case 2:
            EndDialog(hdlg, GET_WM_COMMAND_ID(w, lw));
            return fTrue;
        }
        break;
    }
    return fFalse;
}

MUTX _mutxAssert;

/** 3DMMv1.0: *************************************************************************
    The assert proc. Returning true breaks into the debugger.
***************************************************************************/
bool APPB::FAssertProcApp(PSZS pszsFile, int32_t lwLine, PSZS pszsMsg, void *pv, int32_t cb)
{
    const int32_t kclwChain = 10;
    STN stn0, stn1, stn2;
    int tmc;
    PCSZ psz;
    int32_t cact;
    int32_t *plw;
    int32_t ilw;
    int32_t rglw[kclwChain];

    _mutxAssert.Enter();

    if (_fInAssert)
    {
        _mutxAssert.Leave();
        return fFalse;
    }

    _fInAssert = fTrue;

    _rgpstn[0] = &stn0;
    _rgpstn[1] = &stn1;
    _rgpstn[2] = &stn2;

    // 3DMMv1.0: build the main assert message with file name and line number
    if (pszsMsg == pvNil || *pszsMsg == 0)
        psz = PszLit("Assert (%s line %d)");
    else
    {
        psz = PszLit("Assert (%s line %d): %s");
        stn2.SetSzs(pszsMsg);
    }
    if (pvNil != pszsFile)
        stn1.SetSzs(pszsFile);
    else
        stn1 = PszLit("Some Header file");
    stn0.FFormatSz(psz, &stn1, lwLine, &stn2);

#if defined(WIN) && defined(IN_80386)
    // 3DMMv1.0: call stack - follow the EBP chain....
    __asm { mov plw,ebp }
    for (ilw = 0; ilw < kclwChain; ilw++)
    {
        if (pvNil == plw || IsBadReadPtr(plw, 2 * size(int32_t)) || *plw <= (int32_t)plw)
        {
            rglw[ilw] = 0;
            plw = pvNil;
        }
        else
        {
            rglw[ilw] = plw[1];
            plw = (int32_t *)*plw;
        }
    }

    for (cact = 0; cact < 2; cact++)
    {
        // 3DMMv1.0: format data
        if (pv != pvNil && cb > 0)
        {
            uint8_t *pb = (uint8_t *)pv;
            int32_t cbT = cb;
            int32_t ilw;
            int32_t lw;
            STN stnT;

            stn2.SetNil();
            for (ilw = 0; ilw < 20 && cb >= 4; cb -= 4, pb += 4, ++ilw)
            {
                CopyPb(pb, &lw, 4);
                stnT.FFormatSz(PszLit("%08x "), lw);
                stn2.FAppendStn(&stnT);
            }
            if (ilw < 20 && cb > 0)
            {
                lw = 0;
                CopyPb(pb, &lw, cb);
                if (cb <= 2)
                {
                    stnT.FFormatSz(PszLit("%04x"), lw);
                }
                else
                {
                    stnT.FFormatSz(PszLit("%08x"), lw);
                }
                stn2.FAppendStn(&stnT);
            }
        }
        else
            stn2.SetNil();

        if (cact == 0)
        {
            pv = rglw;
            cb = size(rglw);
            stn1 = stn2;
        }
    }
#endif // 3DMMEx: WIN && IN_80386

    OutputDebugString(stn0.Psz());
    OutputDebugString(PszLit("\n"));

    if (stn1.Cch() > 0)
    {
        OutputDebugString(stn1.Psz());
        OutputDebugString(PszLit("\n"));
    }
    if (stn2.Cch() > 0)
    {
        OutputDebugString(stn2.Psz());
        OutputDebugString(PszLit("\n"));
    }

    if (std::this_thread::get_id() != vwig.tidMain)
    {
        // 3DMMv1.0: can't use a dialog - it may cause grid - lock
        int32_t sid;
        uint32_t grfmb;

        stn0.FAppendSz(PszLit("\n"));
        stn0.FAppendStn(&stn1);
        stn0.FAppendSz(PszLit("\n"));
        stn0.FAppendStn(&stn2);

        grfmb = MB_SYSTEMMODAL | MB_YESNO | MB_ICONHAND;
        sid = MessageBox(hNil, stn0.Psz(), PszLit("Thread Assert! (Y = Ignore, N = Debugger)"), grfmb);

        switch (sid)
        {
        default:
            tmc = 0;
            break;
        case IDNO:
            tmc = 1;
            break;
        }
    }
    else
    {
        // 3DMMv1.0: run the dialog
        tmc = DialogBox(vwig.hinst, PszLit("AssertDlg"), vwig.hwndApp, &_FDlgAssert);
    }

    _fInAssert = fFalse;
    _mutxAssert.Leave();

    switch (tmc)
    {
    case 0:
        // 3DMMv1.0: ignore
        return fFalse;

    case 1:
        // 3DMMv1.0: break into debugger
        return fTrue;

    case 2:
        // 3DMMv1.0: abort
        Abort(); // 3DMMv1.0: shouldn't return
        Debugger();
        break;
    }

    return fFalse;
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Put an alert up. Return which button was hit. Returns tYes for yes
    or ok; tNo for no; tMaybe for cancel.
***************************************************************************/
tribool APPB::TGiveAlertSz(const PCSZ psz, int32_t bk, int32_t cok)
{
    AssertThis(0);
    AssertSz(psz);

    int32_t sid;
    uint32_t grfmb;
    HWND hwnd;

    grfmb = MB_APPLMODAL;
    switch (bk)
    {
    default:
        BugVar("bad bk value", &bk);
        // 3DMMv1.0: fall through
    case bkOk:
        grfmb |= MB_OK;
        break;
    case bkOkCancel:
        grfmb |= MB_OKCANCEL;
        break;
    case bkYesNo:
        grfmb |= MB_YESNO;
        break;
    case bkYesNoCancel:
        grfmb |= MB_YESNOCANCEL;
        break;
    }

    switch (cok)
    {
    default:
        BugVar("bad cok value", &cok);
        // 3DMMv1.0: fall through
    case cokNil:
        break;
    case cokInformation:
        grfmb |= MB_ICONINFORMATION;
        break;
    case cokQuestion:
        grfmb |= MB_ICONQUESTION;
        break;
    case cokExclamation:
        grfmb |= MB_ICONEXCLAMATION;
        break;
    case cokStop:
        grfmb |= MB_ICONSTOP;
        break;
    }

    hwnd = GetActiveWindow();
    if (hNil == hwnd)
        hwnd = vwig.hwndApp;
    sid = MessageBox(hwnd, psz, PszLit(""), grfmb);

    switch (sid)
    {
    default:
    case IDYES:
    case IDOK:
        return tYes;
    case IDCANCEL:
        return tMaybe;
    case IDNO:
        return tNo;
    }
}

/** 3DMMEx: *************************************************************************
    Get the current cursor/modifier state.  If fAsync is set, the key state
    returned is the actual current values at the hardware level, ie, not
    synchronized with the command stream.
***************************************************************************/
uint32_t APPB::GrfcustCur(bool fAsync)
{
    AssertThis(0);

#ifdef WIN

    auto pfnT = fAsync ? GetAsyncKeyState : GetKeyState;
    _grfcust &= ~kgrfcustUser;
    if (pfnT(VK_CONTROL) < 0)
        _grfcust |= fcustCmd;
    if (pfnT(VK_SHIFT) < 0)
        _grfcust |= fcustShift;
    if (pfnT(VK_MENU) < 0)
        _grfcust |= fcustOption;
    if (pfnT(VK_LBUTTON) < 0)
        _grfcust |= fcustMouse;
#endif // 3DMMEx: WIN
#ifdef MAC
    Assert(!fAsync, "Unimplemented code"); // 3DMMEx: REVIEW shonk: Mac: implement
#endif                                     // 3DMMEx: MAC

    return _grfcust;
}

/** 3DMMEx: *************************************************************************
    Hide the cursor
***************************************************************************/
void APPB::HideCurs(void)
{
    AssertThis(0);

    MacWin(HideCursor(), ShowCursor(fFalse));
}

/** 3DMMEx: *************************************************************************
    Show the cursor
***************************************************************************/
void APPB::ShowCurs(void)
{
    AssertThis(0);

    MacWin(ShowCursor(), ShowCursor(fTrue));
}

/** 3DMMEx: *************************************************************************
    Warp the cursor to (xpScreen, ypScreen)
***************************************************************************/
void APPB::PositionCurs(int32_t xpScreen, int32_t ypScreen)
{
    AssertThis(0);

#ifdef WIN
    // Existing callers pass screen coordinates derived from the logical
    // 640x480 Kauai window. In scaled presentation mode warp to the
    // corresponding physical pixel inside the presentation window instead.
    const bool fScaledUi = F4DMMUiScaleInfo(pvNil, pvNil, pvNil);
    F4DMMMapSourceCursorToScaledUi(&xpScreen, &ypScreen);
#endif

    // 3DMMEx: REVIEW shonk: implement on Mac
    MacWin(RawRtn(), SetCursorPos(xpScreen, ypScreen));

#ifdef WIN
    if (_fFlushCursor)
    {
        // Preserve the legacy flush behavior exactly when it was already
        // requested by the caller.
        MSG msg;
        while (PeekMessage(&msg, hNil, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE | PM_NOYIELD))
        {
            // 3DMMEx: do nothing
        }
    }
    else if (fScaledUi)
    {
        // The scaled presentation introduces one extra failure mode the old
        // 640x480 path did not have: a queued WM_MOUSEMOVE can still describe
        // the pre-warp physical edge after SetCursorPos recenters the hidden
        // cursor. Drop only those stale motion packets. Do not consume button,
        // wheel, or modifier messages and do not alter ordinary tracking.
        MSG msg;
        while (PeekMessage(&msg, hNil, WM_MOUSEMOVE, WM_MOUSEMOVE, PM_REMOVE | PM_NOYIELD))
        {
            // 3DMMEx: do nothing
        }
    }
#endif // 3DMMEx: WIN
}

/** 3DMMEx: *************************************************************************
    Make sure the current cursor is being used by the system.
***************************************************************************/
void APPB::RefreshCurs(void)
{
    AssertThis(0);

    PCURS *ppcurs = _cactLongOp > 0 ? &_pcursWait : &_pcurs;

    if (pvNil != *ppcurs)
    {
        (*ppcurs)->Set();
#ifdef WIN
        // Only the authored 3DMM cursor is scaled. Native Win32 arrows,
        // wait cursors, Actor Studio cursors, and other external-tool cursors
        // never pass through this branch.
        if (_cactLongOp <= 0 && _pcurs != pvNil)
        {
            HCURSOR hcur = GetCursor();
            HCURSOR hcurScaled = Hcur4DMMScaledCustomCursor(hcur);
            if (hcurScaled != hNil && hcurScaled != hcur)
                SetCursor(hcurScaled);
        }
#endif // 3DMMEx: WIN
    }
    else
    {
#ifdef WIN
        SetCursor(LoadCursor(hNil, _cactLongOp > 0 ? IDC_WAIT : IDC_ARROW));
#endif // 3DMMEx: WIN
#ifdef MAC
        HCURS hcurs;

        if (_cactLongOp > 0 && hNil != (hcurs = GetCursor(watchCursor)))
            SetCursor(*hcurs);
        else
            SetCursor(&qd.arrow);
#endif // 3DMMEx: MAC
    }
}

/** 3DMMEx: *************************************************************************
    Open a window onto the clipboard, if it exists.
***************************************************************************/
bool APPB::FCmdShowClipboard(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    vpclip->Show();
    return fTrue;
}

/** 3DMMEx: *************************************************************************
    Enable app level commands
***************************************************************************/
bool APPB::FEnableAppCmd(PCMD pcmd, uint32_t *pgrfeds)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    AssertVarMem(pgrfeds);

    *pgrfeds = fedsEnable;
    switch (pcmd->cid)
    {
    case cidShowClipboard:
        if (vpclip->FDocIsClip(pvNil))
            goto LDisable;
        break;

    case cidChooseWnd:
        if ((HWND)(*(uintptr_t *)pcmd->rglw) == GOB::HwndMdiActive())
            *pgrfeds |= fedsCheck;
        else
            *pgrfeds |= fedsUncheck;
        break;
    default:
        BugVar("unhandled cid in FEnableAppCmd", &pcmd->cid);
    LDisable:
        *pgrfeds = fedsDisable;
        break;
    }

    return fTrue;
}

/** 3DMMEx: *************************************************************************
    Respond to a cidChooseWnd command.
***************************************************************************/
bool APPB::FCmdChooseWnd(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    GOB::MakeHwndActive((HWND)(*(uintptr_t *)pcmd->rglw));
    return fTrue;
}

/** 3DMMEx: *************************************************************************
    Return fTrue if the main app window is maximized.
***************************************************************************/
bool APPB::FIsMaximized()
{
    return FPure(IsZoomed(vwig.hwndApp));
}

/** 3DMMEx: *************************************************************************
    Maximize the window if fMaximized is true.
***************************************************************************/
bool APPB::FSetMaximized(bool fMaximized)
{
    AssertThis(0);

    if (FIsMaximized() == fMaximized)
    {
        return fMaximized;
    }
    else
    {
        return FPure(ShowWindow(vwig.hwndApp, fMaximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL));
    }
}

/** 3DMMEx: *************************************************************************
    Translate a key code from the current platform to a Win32 virtual key
***************************************************************************/
int32_t APPB::Win32VkFromVk(int32_t vk)
{
    // 3DMMEx: No translation required
    return vk;
}

/** 3DMMEx: *************************************************************************
    Set the application window's icon
***************************************************************************/
bool APPB::FSetWindowIcon(const uint8_t *prgb, int32_t cb)
{
    AssertThis(0);
    AssertPvCb(prgb, cb);

    // 3DMMEx: Not required on Windows: the icon is loaded from the resource section
    return fTrue;
}
