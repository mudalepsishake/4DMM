/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    utest.cpp: Socrates main app class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    The APP class handles initialization of the product, and global
    actions such as resolution-switching, switching between the building
    and the studio, and quitting.

    The KWA (App KidWorld) is the parent of the gob tree in the product.
    It also is used to display a splash screen, and to find AVIs on the CD.

***************************************************************************/
#include "studio.h"
#include "socres.h"

#ifdef KAUAI_WIN32
#include "mminstal.h"
#include <stdio.h>
#include <dbghelp.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdlib.h>
#include <string.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#endif // 3DMMEx: KAUAI_WIN32

#ifdef KAUAI_SDL
#include <cstdio>
#endif // 3DMMEx: KAUAI_SDL

extern "C" int vgActorLightQuickModelUpdateEnabled;
#if defined(KAUAI_WIN32)
extern void Set4DMMExternalToolScale(int32_t num, int32_t den);
extern void Get4DMMExternalToolScale(int32_t *pnum, int32_t *pden);
extern void Set4DMMCustomCursorScale(int32_t num, int32_t den);
#endif

#if defined(KAUAI_WIN32) && defined(BRENDER_MODERN_14)
#define MODERN_BR_LOG(...) BrModernLog(__VA_ARGS__)
#else
#define MODERN_BR_LOG(...) ((void)0)
#endif

ASSERTNAME

// 3DMMv1.0: If the following value is defined, 3DMM displays the dlidDesktopResizing
// 3DMMv1.0: and dlidDesktopResized dialogs before and immediately after res-switching.
// 3DMMv1.0: The current thought is that these dialogs are unnecessary since we only
// 3DMMv1.0: res-switch to 640x480, which should always be safe.  We still display a
// 3DMMv1.0: dialog if the res-switch failed (dlidDesktopWontResize).
// 3DMMv1.0: #define RES_SWITCH_DIALOGS

// 3DMMv1.0: If the following value is defined, 3DMM does a run-time performance test
// 3DMMv1.0: of the graphics, fixed-point math, and copying speed at startup and sets
// 3DMMv1.0: _fSlowCPU to fTrue if it thinks 3DMM is running on a slow computer.
// 3DMMv1.0: The current feeling is that we don't have the resources to tweak the
// 3DMMv1.0: thresholds and verify that this gives us the result we want on all
// 3DMMv1.0: computers.
// 3DMMv1.0: #define PERF_TEST

// 3DMMv1.0: Duration to display homelogo
const uint32_t kdtsHomeLogo = 4 * kdtsSecond;

// 3DMMv1.0: Duration to display splash screen
const uint32_t kdtsSplashScreen = 4 * kdtsSecond;

// 3DMMv1.0: Duration before res-switch dialog cancels itself
const uint32_t kdtsMaxResSwitchDlg = 15 * kdtsSecond;

const uint32_t kcbCacheTagm = 2048 * 1024;

static PCSZ kpszAppWndCls = PszLit("3DMOVIE");
const PCSZ kpszOpenFile = PszLit("3DMMOpen.tmp");

const int32_t klwOpenDoc = 0x12123434; // 3DMMv1.0: arbitrary wParam for WM_USER

#ifdef KAUAI_WIN32
// Blank editor startup inherits one stale 1995 file-family error *before*
// normal Studio initialization is reliably far enough along to arm a filter.
// v156's first multi-log line was already error_dialog erc=400, with none of
// the later startup-arm markers preceding it.  Start this one-shot armed at
// process initialization and let command-line parsing disarm it immediately
// for an explicit document.  The portfolio FNI is deliberately not consulted:
// it can contain a stale/default 1995 value before Studio starts.
static bool vf4DMMSuppressNextBlankStartupFileError = fTrue;
static bool vf4DMMBlankStartupPhase = fTrue;
static bool vf4DMMCommandLineParsed = fFalse;
// Authoritative command-line document state.
static bool vf4DMMExplicitStartupDocument = fFalse;
// A direct invalid-VMM MessageBox is the complete user-facing error for that
// validation failure.  Clear any inherited Kauai file-family error generated
// by the failed open so it cannot immediately stack the old generic dialog on
// top of the explicit message.
static bool vf4DMMInvalidVmmMessageActive = fFalse;
static int32_t vc4DMMInvalidVmmIdlePasses = 0;
static bool vfViewportResolution1080p = fFalse;
static bool vfViewportResolution4x = fFalse;

// Object Groups is a native tool window that can remain open while the
// original Studio is torn down for File/Open and a replacement Studio/movie
// is constructed.  Notify it from those lifecycle boundaries instead of
// depending on WM_TIMER polling to discover the movie swap.
void Queue4DMMObjectGroupsMovieRefresh(void);

// The native movie layer is painted by the final 4x presentation HWND from a
// completed BRender frame cache. v50 begins the real compositor transition:
// the movie remains continuously present while Kauai workspace UI is layered
// over it as a separate DWM source rectangle. There is no viewport hide/show
// or colour-detection fallback in this path.
static const bool kf4DMMNativeMoviePaintedByPresentation = fTrue;

static bool F4DMMModernEmbeddedViewportHost(void)
{
#if defined(BRENDER_MODERN_14)
    // Standard modern mode already reads every BWLD GPU result back into its
    // Kauai framebuffer, and scaled mode paints the native movie cache in the
    // visible presentation HWND. In those modes the OpenGL HWND is only a WGL
    // context host: keeping it visible creates the extra Z-order surface that
    // can cover tooltips/easels/dropdowns. Native 1080p still intentionally
    // owns an external frontbuffer until its UI integration is implemented.
    return !vfViewportResolution1080p;
#else
    return fFalse;
#endif
}

// Scaled presentation mode is deliberately implemented as a presentation layer
// around the original 640x480 Kauai window instead of resizing the Kauai/GOB
// root. That preserves every original UI coordinate/clickbox while Windows/DWM
// presents the client at an independent scale. The Modern BRender movie
// viewport renders natively at the matching workspace size.
//
// v69 keeps the rational numerator / denominator transform introduced by v68
// and generalizes the command-line parser so any practical decimal scale from
// 1.25x through 8x can use the same compositor/input geometry. Keep the legacy
// 4x boolean as the low-risk "scaled presentation active" flag for now.
static const int32_t k4DMMUiScaleNumeratorDefault = 2;
static const int32_t k4DMMUiScaleDenominatorDefault = 1;
static int32_t vlw4DMMUiScaleNumeratorRequested = k4DMMUiScaleNumeratorDefault;
static int32_t vlw4DMMUiScaleDenominatorRequested = k4DMMUiScaleDenominatorDefault;

/***************************************************************************
    Parse a scaled-presentation command-line value such as 2x, 2.5x, or
    2.25x into an exact reduced rational. Keep this decimal parser deliberately
    small and deterministic: up to three fractional digits, no floating-point
    state, and a practical 1.25x..8x range for the 640x480 Studio client.
***************************************************************************/
static int32_t Lw4DMMGreatestCommonDivisor(int32_t lwA, int32_t lwB)
{
    while (lwB != 0)
    {
        const int32_t lwT = lwA % lwB;
        lwA = lwB;
        lwB = lwT;
    }
    return lwA > 0 ? lwA : 1;
}

static bool FParse4DMMUiScaleOption(PCSZ psz, int32_t *pnum, int32_t *pden)
{
    if (psz == pvNil || pnum == pvNil || pden == pvNil)
        return fFalse;

    int32_t lwWhole = 0;
    int32_t lwFraction = 0;
    int32_t lwFractionDen = 1;
    int32_t cFractionDigits = 0;
    bool fSawDigit = fFalse;
    bool fSawDecimal = fFalse;
    PCSZ pch = psz;

    while (*pch != chNil && *pch != ChLit('x') && *pch != ChLit('X'))
    {
        if (*pch == ChLit('.'))
        {
            if (fSawDecimal)
                return fFalse;
            fSawDecimal = fTrue;
            ++pch;
            continue;
        }

        if (*pch < ChLit('0') || *pch > ChLit('9'))
            return fFalse;

        const int32_t lwDigit = *pch - ChLit('0');
        fSawDigit = fTrue;
        if (!fSawDecimal)
        {
            // The final accepted scale is capped at 8x. This early bound also
            // prevents a malformed giant decimal from overflowing lwWhole.
            if (lwWhole > 8)
                return fFalse;
            lwWhole = lwWhole * 10 + lwDigit;
            if (lwWhole > 8)
                return fFalse;
        }
        else
        {
            if (cFractionDigits >= 3)
                return fFalse;
            lwFraction = lwFraction * 10 + lwDigit;
            lwFractionDen *= 10;
            ++cFractionDigits;
        }
        ++pch;
    }

    if (!fSawDigit || (fSawDecimal && cFractionDigits == 0) ||
        (*pch != ChLit('x') && *pch != ChLit('X')) || pch[1] != chNil)
    {
        return fFalse;
    }

    int32_t num = lwWhole * lwFractionDen + lwFraction;
    int32_t den = lwFractionDen;

    // Keep this path strictly upscaled. 1.25x is the smallest useful scaled
    // Studio presentation and 8x is a generous practical upper bound.
    if (num * 4 < den * 5 || num > den * 8)
        return fFalse;

    const int32_t lwGcd = Lw4DMMGreatestCommonDivisor(num, den);
    num /= lwGcd;
    den /= lwGcd;

    *pnum = num;
    *pden = den;
    return fTrue;
}

/***************************************************************************
    Parse -cursor_size. Keep the same Nx spelling as -resolution, but allow
    1x so a user can deliberately keep the authored cursor at native size
    while running a larger presentation. Up to three fractional digits and
    a practical 1x..8x range are supported.
***************************************************************************/
static bool FParse4DMMCursorScaleOption(PCSZ psz, int32_t *pnum, int32_t *pden)
{
    if (psz == pvNil || pnum == pvNil || pden == pvNil)
        return fFalse;

    int32_t lwWhole = 0;
    int32_t lwFraction = 0;
    int32_t lwFractionDen = 1;
    int32_t cFractionDigits = 0;
    bool fSawDigit = fFalse;
    bool fSawDecimal = fFalse;
    PCSZ pch = psz;

    while (*pch != chNil && *pch != ChLit('x') && *pch != ChLit('X'))
    {
        if (*pch == ChLit('.'))
        {
            if (fSawDecimal)
                return fFalse;
            fSawDecimal = fTrue;
            ++pch;
            continue;
        }
        if (*pch < ChLit('0') || *pch > ChLit('9'))
            return fFalse;

        const int32_t lwDigit = *pch - ChLit('0');
        fSawDigit = fTrue;
        if (!fSawDecimal)
        {
            if (lwWhole > 8)
                return fFalse;
            lwWhole = lwWhole * 10 + lwDigit;
            if (lwWhole > 8)
                return fFalse;
        }
        else
        {
            if (cFractionDigits >= 3)
                return fFalse;
            lwFraction = lwFraction * 10 + lwDigit;
            lwFractionDen *= 10;
            ++cFractionDigits;
        }
        ++pch;
    }

    if (!fSawDigit || (fSawDecimal && cFractionDigits == 0) ||
        (*pch != ChLit('x') && *pch != ChLit('X')) || pch[1] != chNil)
    {
        return fFalse;
    }

    int32_t num = lwWhole * lwFractionDen + lwFraction;
    int32_t den = lwFractionDen;
    if (num < den || num > den * 8)
        return fFalse;

    const int32_t lwGcd = Lw4DMMGreatestCommonDivisor(num, den);
    *pnum = num / lwGcd;
    *pden = den / lwGcd;
    return fTrue;
}

/***************************************************************************
    Parse -gui_scale. Auxiliary native windows only need one fractional digit,
    so keep the accepted spelling intentionally small: N or N.d. The result is
    reduced to the numerator/denominator pair consumed by appbwin.cpp.
***************************************************************************/
static bool FParse4DMMExternalToolScaleOption(PCSZ psz, int32_t *pnum, int32_t *pden)
{
    if (psz == pvNil || pnum == pvNil || pden == pvNil)
        return fFalse;

    int32_t whole = 0;
    int32_t fraction = 0;
    bool fSawDigit = fFalse;
    bool fSawDecimal = fFalse;
    bool fSawFraction = fFalse;
    for (PCSZ pch = psz; *pch != chNil; ++pch)
    {
        if (*pch == ChLit('.'))
        {
            if (fSawDecimal || !fSawDigit)
                return fFalse;
            fSawDecimal = fTrue;
            continue;
        }
        if (*pch < ChLit('0') || *pch > ChLit('9'))
            return fFalse;
        const int32_t digit = *pch - ChLit('0');
        fSawDigit = fTrue;
        if (!fSawDecimal)
        {
            if (whole > 9)
                return fFalse;
            whole = whole * 10 + digit;
            if (whole > 9)
                return fFalse;
        }
        else
        {
            if (fSawFraction)
                return fFalse;
            fraction = digit;
            fSawFraction = fTrue;
        }
    }

    if (!fSawDigit || (fSawDecimal && !fSawFraction))
        return fFalse;

    int32_t num = whole * 10 + (fSawFraction ? fraction : 0);
    int32_t den = 10;
    if (!fSawFraction)
    {
        num = whole;
        den = 1;
    }
    // Keep obviously destructive values out while still allowing everything
    // useful for normal desktop scaling, including 1.0 and 1.5.
    if (num <= 0 || num * 10 < den || num * 10 > den * 99)
        return fFalse;

    const int32_t gcd = Lw4DMMGreatestCommonDivisor(num, den);
    *pnum = num / gcd;
    *pden = den / gcd;
    return fTrue;
}
static const char ksz4DMMUiScaleWndClass[] = "4DMMUiScaleWindow";
static const char ksz4DMMUiScaleWindowProp[] = "4DMMUiScaleWindow";
static const char ksz4DMMUiScaleNumeratorProp[] = "4DMMUiScaleNumerator";
static const char ksz4DMMUiScaleDenominatorProp[] = "4DMMUiScaleDenominator";
static const char ksz4DMMUiScaleSuspendedProp[] = "4DMMUiScaleSuspended";
static HWND vhwnd4DMMUiScale = hNil;
static HANDLE vhthumb4DMMUiScale = hNil;
static HANDLE vrghthumb4DMMUiScaleRegion[4] = {hNil, hNil, hNil, hNil};
// The permanent four-region UI layer is correct around the movie workspace,
// but a 3D Word APE begins above that workspace.  While an APE shell owns the
// full 640x480 source client, its own four DWM regions reproduce all 2D chrome
// outside the preview hole, so the permanent layer must be hidden or it will
// cover the top of the native preview with the source UI.
static bool vf4DMMUiScaleBaseRegionsSuppressedByApe = fFalse;
static HANDLE vhthumb4DMMUiScaleViewport = hNil;
static HANDLE vhthumb4DMMUiScaleWorkspaceOverlay = hNil;
// APE easels need the inverse of the ordinary workspace overlay: keep the
// Kauai window chrome/buttons, but punch out only its 3D preview rectangle so
// the presentation HWND can paint the native BRender preview underneath.
// Dedicated handles keep that base easel layer separate from tooltip/menu
// overlays, which are still allowed to cross the native preview.
static const int32_t kc4DMMUiScaleApeOverlayRegion = 4;
static HANDLE vrghthumb4DMMUiScaleApeOverlayRegion[kc4DMMUiScaleApeOverlayRegion] =
    {hNil, hNil, hNil, hNil};
static bool vf4DMMUiScaleApeOverlayVisible = fFalse;
static RECT vrc4DMMUiScaleApeOverlayOwnerSource = {0, 0, 0, 0};
static RECT vrc4DMMUiScaleApeHoleSource = {0, 0, 0, 0};
// One popup menu can be active at a time. Give it a dedicated DWM layer that
// is registered after the APE/easel layers so Font/Shape/Material menus are
// guaranteed to appear above the native Modern preview.
static HANDLE vhthumb4DMMUiScaleApePopup = hNil;
static PGOB vpgob4DMMUiScaleApePopup = pvNil;
static RECT vrc4DMMUiScaleApePopupSource = {0, 0, 0, 0};
// v59 keeps the proven primary workspace-overlay path and adds a small set of
// independent live DWM rectangles for disjoint Kauai UI surfaces. A File menu
// and a tooltip/dialog child can therefore coexist above the native movie
// instead of each new paint replacing the previous overlay rectangle.
static const int32_t kc4DMMUiScaleWorkspaceOverlayExtra = 7;
static HANDLE vrghthumb4DMMUiScaleWorkspaceOverlayExtra[kc4DMMUiScaleWorkspaceOverlayExtra] =
    {hNil, hNil, hNil, hNil, hNil, hNil, hNil};
static bool vrgf4DMMUiScaleWorkspaceOverlayExtraVisible[kc4DMMUiScaleWorkspaceOverlayExtra] =
    {fFalse, fFalse, fFalse, fFalse, fFalse, fFalse, fFalse};
static RECT vrgrc4DMMUiScaleWorkspaceOverlayExtraSource[kc4DMMUiScaleWorkspaceOverlayExtra] = {};
// Only one Kauai tooltip can be live at a time. Track the DWM slot carrying
// it so moving from one side-list tooltip to another retires the previous
// tooltip surface instead of leaving its now-black source rectangle behind.
// Slot 0 is the primary overlay; 1..N are the extra overlay slots.
static int32_t vi4DMMUiScaleToolTipOverlaySlot = -1;
static bool vf4DMMUiScaleViewportHole = fFalse;
static bool vf4DMMUiScaleWorkspaceOverlayVisible = fFalse;
static RECT vrc4DMMUiScaleWorkspaceOverlaySource = {0, 0, 0, 0};
static HWND vhwnd4DMMUiScaleViewport = hNil;
static bool vf4DMMUiScaleSourceParked = fFalse;
static bool vf4DMMUiScaleSourceAltTabStyleChanged = fFalse;
static LONG_PTR vl4DMMUiScaleSourceExStyleRestore = 0;
static bool vf4DMMUiScaleNativeDialogSuspended = fFalse;
static RECT vrc4DMMUiScaleSourceRestore = {0, 0, 0, 0};
static const UINT_PTR kid4DMMUiScaleOverlayTimer = 0x4D4D;
// Source-side Kauai draw-completion notification. v58 sends this after every
// real source update path: WM_PAINT/UpdateHwnd, immediate kginDraw, and the
// marked-region _FastUpdate path. The presentation therefore follows Kauai's
// actual renderer rather than polling or guessing from framebuffer colours.
// Keep the numeric value in sync with kauai/src/appb.cpp.
static const UINT kwm4DMMUiScaleSourcePaint = WM_APP + 0x04D;
static HMODULE vhmod4DMMDwmApi = hNil;

typedef HRESULT(WINAPI *PFN4DMMDwmRegisterThumbnail)(HWND hwndDestination, HWND hwndSource, HANDLE *phThumbnail);
typedef HRESULT(WINAPI *PFN4DMMDwmUnregisterThumbnail)(HANDLE hThumbnail);

typedef struct _DWMTHUMBNAILPROPERTIES4DMM
{
    DWORD dwFlags;
    RECT rcDestination;
    RECT rcSource;
    BYTE opacity;
    BOOL fVisible;
    BOOL fSourceClientAreaOnly;
} DWMTHUMBNAILPROPERTIES4DMM;

typedef HRESULT(WINAPI *PFN4DMMDwmUpdateThumbnailProperties)(HANDLE hThumbnail,
                                                               const DWMTHUMBNAILPROPERTIES4DMM *pProperties);

static PFN4DMMDwmRegisterThumbnail vpfn4DMMDwmRegisterThumbnail = pvNil;
static PFN4DMMDwmUnregisterThumbnail vpfn4DMMDwmUnregisterThumbnail = pvNil;
static PFN4DMMDwmUpdateThumbnailProperties vpfn4DMMDwmUpdateThumbnailProperties = pvNil;

const DWORD kdw4DMMDwmTnpRectDestination = 0x00000001;
const DWORD kdw4DMMDwmTnpRectSource = 0x00000002;
const DWORD kdw4DMMDwmTnpOpacity = 0x00000004;
const DWORD kdw4DMMDwmTnpVisible = 0x00000008;
const DWORD kdw4DMMDwmTnpSourceClientAreaOnly = 0x00000010;

static void Get4DMMUiScaleRatio(int32_t *pnum, int32_t *pden)
{
    int32_t num = vlw4DMMUiScaleNumeratorRequested;
    int32_t den = vlw4DMMUiScaleDenominatorRequested;

    if (vwig.hwndApp != hNil && IsWindow(vwig.hwndApp))
    {
        const int32_t numProp =
            (int32_t)(INT_PTR)GetPropA(vwig.hwndApp, ksz4DMMUiScaleNumeratorProp);
        const int32_t denProp =
            (int32_t)(INT_PTR)GetPropA(vwig.hwndApp, ksz4DMMUiScaleDenominatorProp);
        if (numProp > denProp && denProp > 0)
        {
            num = numProp;
            den = denProp;
        }
    }

    if (num <= den || den <= 0)
    {
        num = k4DMMUiScaleNumeratorDefault;
        den = k4DMMUiScaleDenominatorDefault;
    }

    if (pnum != pvNil)
        *pnum = num;
    if (pden != pvNil)
        *pden = den;
}

static int32_t Lw4DMMScaleSourceToPresentation(int32_t lwSource)
{
    int32_t num;
    int32_t den;
    Get4DMMUiScaleRatio(&num, &den);
    return MulDiv(lwSource, num, den);
}

static int32_t Lw4DMMScalePresentationToSource(int32_t lwPresentation)
{
    int32_t num;
    int32_t den;
    Get4DMMUiScaleRatio(&num, &den);
    if (lwPresentation <= 0)
        return 0;
    return (lwPresentation * den) / num;
}

static bool F4DMMUiScaleActive(void)
{
    return vfViewportResolution4x && vhwnd4DMMUiScale != hNil && IsWindow(vhwnd4DMMUiScale);
}

/***************************************************************************
    Return the visible 4DMM application root for native auxiliary windows.

    The scaled presentation must remain independent from the legacy Kauai
    source HWND itself, because making those two windows owner/owned regressed
    the portfolio and modal-loading paths.  Native 4DMM tools are different:
    when scaled mode is live they should be owned by the visible presentation
    so Windows treats the console, settings and other tool windows as one
    Alt+Tab/minimize/restore family.
***************************************************************************/
static HWND Hwnd4DMMNativeToolOwner(void)
{
    if (F4DMMUiScaleActive() && !vf4DMMUiScaleNativeDialogSuspended)
        return vhwnd4DMMUiScale;
    if (vwig.hwndApp != hNil && IsWindow(vwig.hwndApp))
        return vwig.hwndApp;
    return hNil;
}

/***************************************************************************
    Keep the original 640x480 Kauai HWND alive and visible to DWM, but park it
    at the bottom of the Z order underneath the independent 4x presentation.
    DwmRegisterThumbnail needs a live source surface, so hiding/minimizing the
    source is deliberately avoided. The presentation is intentionally NOT an
    owned window in v25: Kauai modal/load loops can disable/promote an owner's
    other windows, which is what made the giant loading UI stop responding and
    exposed the little 640x480 source window in v22/v23.
***************************************************************************/
static bool F4DMMParkUiScaleSourceWindow(void)
{
    HWND hwndSource = vwig.hwndApp;
    if (!F4DMMUiScaleActive() || hwndSource == hNil || !IsWindow(hwndSource))
        return fFalse;

    RECT rcScale;
    RECT rcSource;
    if (!GetWindowRect(vhwnd4DMMUiScale, &rcScale) || !GetWindowRect(hwndSource, &rcSource))
        return fFalse;

    if (!vf4DMMUiScaleSourceParked)
        vrc4DMMUiScaleSourceRestore = rcSource;

    const int32_t dxpSource = rcSource.right - rcSource.left;
    const int32_t dypSource = rcSource.bottom - rcSource.top;
    // v51: keep the original Kauai window in normal desktop composition instead
    // of parking it at (-32000,-32000). The four DWM UI-region thumbnails in
    // v50 were the first path that depended on sub-rectangles of the live Kauai
    // surface, and the test produced a white shell around the still-alive movie
    // workspace. The source window itself was functioning (input/sound still
    // worked), but an off-desktop top-level HWND is not a reliable live DWM
    // source for this use.
    //
    // Place the 640x480 source directly underneath the presentation window.
    // It remains fully hidden from the user by Z order, but Windows/DWM now
    // keeps it as an ordinary composed window with a live backing surface. This
    // does not restore any colour probe, viewport hide/show, cover latch, or
    // PrintWindow path. The intended order remains native movie first, Kauai UI
    // on top; this change only makes the UI source itself a proper compositor
    // participant.
    const int32_t xpSource = rcScale.left;
    const int32_t ypSource = rcScale.top;

    if (vf4DMMUiScaleSourceParked && rcSource.left == xpSource && rcSource.top == ypSource)
    {
        // Keep the source immediately below the visible presentation. The
        // source therefore remains in normal DWM composition without ever
        // becoming the user's interactive surface.
        SetWindowPos(hwndSource, vhwnd4DMMUiScale, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return fTrue;
    }

    if (!SetWindowPos(hwndSource, vhwnd4DMMUiScale, xpSource, ypSource, 0, 0,
                      SWP_NOSIZE | SWP_NOACTIVATE))
    {
        MODERN_BR_LOG("4x UI source park FAIL source=%p presentation=%p gle=%lu",
                      hwndSource, vhwnd4DMMUiScale, (unsigned long)GetLastError());
        return fFalse;
    }

    vf4DMMUiScaleSourceParked = fTrue;
    MODERN_BR_LOG("4x UI source parked behind presentation source=%p pos=(%ld,%ld) size=%ldx%ld presentation=%p rect=(%ld,%ld)-(%ld,%ld)",
                  hwndSource, (long)xpSource, (long)ypSource, (long)dxpSource, (long)dypSource,
                  vhwnd4DMMUiScale, (long)rcScale.left, (long)rcScale.top,
                  (long)rcScale.right, (long)rcScale.bottom);
    return fTrue;
}

static bool F4DMMMapScaledPointToSource(HWND hwndFrom, LPARAM lParam, bool fScreenCoordinates,
                                        POINT *pptSourceClient, POINT *pptSourceScreen)
{
    if (!F4DMMUiScaleActive() || vwig.hwndApp == hNil || !IsWindow(vwig.hwndApp))
        return fFalse;

    POINT ptPhysical;
    ptPhysical.x = (short)LOWORD(lParam);
    ptPhysical.y = (short)HIWORD(lParam);
    if (!fScreenCoordinates)
    {
        if (!ClientToScreen(hwndFrom, &ptPhysical))
            return fFalse;
    }
    if (!ScreenToClient(vhwnd4DMMUiScale, &ptPhysical))
        return fFalse;

    RECT rcScale;
    if (!GetClientRect(vhwnd4DMMUiScale, &rcScale) || ptPhysical.x < rcScale.left || ptPhysical.y < rcScale.top ||
        ptPhysical.x >= rcScale.right || ptPhysical.y >= rcScale.bottom)
    {
        return fFalse;
    }

    POINT ptSource;
    ptSource.x = Lw4DMMScalePresentationToSource(ptPhysical.x);
    ptSource.y = Lw4DMMScalePresentationToSource(ptPhysical.y);
    if (pptSourceClient != pvNil)
        *pptSourceClient = ptSource;

    if (pptSourceScreen != pvNil)
    {
        POINT ptScreen = ptSource;
        if (!ClientToScreen(vwig.hwndApp, &ptScreen))
            return fFalse;
        *pptSourceScreen = ptScreen;
    }
    return fTrue;
}

static LRESULT Lresult4DMMForwardScaledMouse(HWND hwndFrom, UINT wm, WPARAM wParam, LPARAM lParam)
{
    POINT ptSourceClient;
    POINT ptSourceScreen;
    const bool fScreenCoordinates = wm == WM_MOUSEWHEEL;
    if (!F4DMMMapScaledPointToSource(hwndFrom, lParam, fScreenCoordinates,
                                     &ptSourceClient, &ptSourceScreen))
    {
        return 0;
    }

    LPARAM lParamSource;
    if (fScreenCoordinates)
        lParamSource = MAKELPARAM((short)ptSourceScreen.x, (short)ptSourceScreen.y);
    else
        lParamSource = MAKELPARAM((short)ptSourceClient.x, (short)ptSourceClient.y);

    // Let ordinary mouse movement pass through Kauai's normal OS event queue
    // instead of calling the source WndProc synchronously. Roll-on/roll-off and
    // tracking feedback in the 1995 UI depend on that queue/idle path, which is
    // why v14-v19 clicks worked while hover bubbles did not repaint. Button
    // transitions stay synchronous so existing click/drag sequencing is kept.
    if (wm == WM_MOUSEMOVE)
    {
        // Free Cam and Manual Camera capture this visible presentation HWND and
        // sample their own relative mouse position once per editor idle tick.
        // Forwarding every captured WM_MOUSEMOVE into the hidden 640x480 Kauai
        // source is therefore pure duplicate traffic.  Worse, a gaming mouse
        // can keep that source queue continuously non-empty, starving the
        // low-priority WM_PAINT which presents the completed native BRender
        // frame.  Ordinary editor hover still follows the historical forwarded
        // path; only the camera-capture case is swallowed here.
        if (GetCapture() == hwndFrom)
            return 0;

        if (!PostMessage(vwig.hwndApp, wm, wParam, lParamSource))
            MODERN_BR_LOG("4x UI mouse-move forward FAIL source=%p gle=%lu",
                          vwig.hwndApp, (unsigned long)GetLastError());
        return 0;
    }

    return SendMessage(vwig.hwndApp, wm, wParam, lParamSource);
}

static bool FGet4DMM4xViewportRect(RECT *prcViewport);
static void Sync4DMMUiScaleWorkspaceOverlay(void);
static void Sync4DMMUiScaleWorkspaceOverlayFromSourcePaint(RC *prcPaintSource);
static void Clear4DMMUiScaleWorkspaceOverlayThumbnail(void);
static void Set4DMMUiScaleBaseRegionsSuppressedByApe(bool fSuppress);
static void Clear4DMMUiScaleApeOverlayRegions(bool fRestoreBaseRegions = fTrue);
static void Clear4DMMUiScaleWorkspaceOverlaysIntersectingApe(const RECT *prcApe);
static bool F4DMMRectEqual(const RECT *prcA, const RECT *prcB);
static bool FGet4DMMUiScaleApeOwnerSourceRect(RECT *prcOwner);
static void Park4DMMUiScaleViewportSource(void);

static LRESULT CALLBACK Lresult4DMMUiScaleWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    switch (wm)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        bool fNativeApeActive = fFalse;
#if defined(BRENDER_MODERN_14)
        int32_t xpApeActive = 0, ypApeActive = 0, dxpApeActive = 0, dypApeActive = 0;
        fNativeApeActive = FBrModernPreviewGetActiveSourceRect(
            &xpApeActive, &ypApeActive, &dxpApeActive, &dypApeActive);
#endif

        // Paint the native BRender movie layer continuously. Kauai UI that
        // crosses the workspace is a separate DWM overlay rectangle above this
        // client paint, matching the original 3DMM ordering: 3D first, 2D UI
        // second. The movie is never hidden merely because UI is present.
        if (kf4DMMNativeMoviePaintedByPresentation && !vf4DMMUiScaleNativeDialogSuspended)
        {
            RECT rcViewport;
            if (FGet4DMM4xViewportRect(&rcViewport))
            {
                bool fPainted = fFalse;
#if defined(BRENDER_MODERN_14)
                // While a 3D Word/Costume/Action easel owns the workspace, the
                // main movie is intentionally dormant. Classic 3DMM dimmed the
                // movie behind these easels; black isolation is simpler and,
                // critically, prevents a tooltip/source repaint from exposing
                // the unrelated movie frame through an APE compositor gap.
                if (!fNativeApeActive)
                    fPainted = FBrModernViewportPaintLatestFrame((void *)hdc,
                                                                  rcViewport.left, rcViewport.top,
                                                                  rcViewport.right - rcViewport.left,
                                                                  rcViewport.bottom - rcViewport.top);
#endif
                if (!fPainted)
                {
                    HBRUSH hbrBlack = (HBRUSH)GetStockObject(BLACK_BRUSH);
                    FillRect(hdc, &rcViewport, hbrBlack);
                }
            }
        }

#if defined(BRENDER_MODERN_14)
        // APE previews are another native 3D base layer, not a child HWND.
        // Paint the most recent high-resolution 3D Word/Costume/Action frame
        // at its source-derived presentation rectangle. The DWM easel layer
        // is split around this rectangle below, so Kauai chrome stays above
        // it while the legacy low-resolution APE pixels never cover it.
        if (!vf4DMMUiScaleNativeDialogSuspended && fNativeApeActive)
        {
            RECT rcPreviewDst;
            rcPreviewDst.left = Lw4DMMScaleSourceToPresentation(xpApeActive);
            rcPreviewDst.top = Lw4DMMScaleSourceToPresentation(ypApeActive);
            rcPreviewDst.right = Lw4DMMScaleSourceToPresentation(xpApeActive + dxpApeActive);
            rcPreviewDst.bottom = Lw4DMMScaleSourceToPresentation(ypApeActive + dypApeActive);

            // Preserve the last complete native APE frame until its replacement
            // is ready. Clearing first created the remaining one-frame black
            // Costume flash between a material click and the new readback.
            if (!FBrModernPreviewPaintLatestFrame((void *)hdc,
                    rcPreviewDst.left, rcPreviewDst.top,
                    rcPreviewDst.right - rcPreviewDst.left,
                    rcPreviewDst.bottom - rcPreviewDst.top))
            {
                HBRUSH hbrBlack = (HBRUSH)GetStockObject(BLACK_BRUSH);
                FillRect(hdc, &rcPreviewDst, hbrBlack);
            }
        }
#endif

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ACTIVATEAPP:
    {
        // v76 proved the scaled-mode legacy deactivate/modal bypass is sufficient
        // to keep the cached BRender movie visible while 4DMM is inactive.  The
        // remaining focus-return stall was self-inflicted here: UpdateWindow()
        // synchronously repainted the native movie before APP::_Activate() could
        // finish, so the first click back into 4DMM was queued behind that paint.
        // Keep the deactivation-side repaint that preserves the final visible
        // frame, but on reactivation retain that already-valid cached surface and
        // return immediately to normal input processing.
        if (kf4DMMNativeMoviePaintedByPresentation && !vf4DMMUiScaleNativeDialogSuspended)
        {
            RECT rcViewport;
            if (FGet4DMM4xViewportRect(&rcViewport))
            {
                if (wParam == FALSE)
                {
                    MODERN_BR_LOG("scaled UI activation inactive repaint viewport=(%ld,%ld)-(%ld,%ld)",
                                  (long)rcViewport.left, (long)rcViewport.top,
                                  (long)rcViewport.right, (long)rcViewport.bottom);
                    InvalidateRect(hwnd, &rcViewport, FALSE);
                    UpdateWindow(hwnd);
                }
                else
                {
                    MODERN_BR_LOG("scaled UI activation foreground fast-return cached viewport retained");
                }
            }
        }
        break;
    }

    case WM_CLOSE:
        if (vwig.hwndApp != hNil && IsWindow(vwig.hwndApp))
            PostMessage(vwig.hwndApp, WM_CLOSE, 0, 0);
        return 0;

    case WM_TIMER:
        if (wParam == kid4DMMUiScaleOverlayTimer)
        {
            // Modal/load transitions can activate the hidden 640x480 source
            // HWND. Keep the 4x presentation above that source without
            // stealing focus from a genuine separate tool/dialog window.
            if (vf4DMMUiScaleNativeDialogSuspended)
                return 0;
            if (vwig.hwndApp != hNil && IsWindow(vwig.hwndApp) &&
                GetForegroundWindow() == vwig.hwndApp)
            {
                F4DMMParkUiScaleSourceWindow();
                SetWindowPos(vhwnd4DMMUiScale, HWND_TOP, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            Park4DMMUiScaleViewportSource();

#if defined(BRENDER_MODERN_14)
            // Preview activation/deactivation is a BRender event, not
            // necessarily a Kauai source paint. Rebuild the easel-hole layer
            // only when that state changes; stable frames still avoid GOB
            // polling on this timer.
            int32_t xpApe = 0, ypApe = 0, dxpApe = 0, dypApe = 0;
            const bool fApeFrame = FBrModernPreviewGetActiveSourceRect(
                &xpApe, &ypApe, &dxpApe, &dypApe);
            RECT rcApe = {xpApe, ypApe, xpApe + dxpApe, ypApe + dypApe};
            RECT rcApeOwner = {0, 0, 0, 0};
            const bool fHaveApeOwner = fApeFrame && FGet4DMMUiScaleApeOwnerSourceRect(&rcApeOwner);
            const bool fApeChanged = fApeFrame &&
                (!vf4DMMUiScaleApeOverlayVisible ||
                 !F4DMMRectEqual(&rcApe, &vrc4DMMUiScaleApeHoleSource) ||
                 (fHaveApeOwner &&
                  !F4DMMRectEqual(&rcApeOwner, &vrc4DMMUiScaleApeOverlayOwnerSource)));
            if (fApeChanged || (!fApeFrame && vf4DMMUiScaleApeOverlayVisible))
            {
                // If an APE is merely moving/rebuilding, keep the permanent
                // base regions suppressed until its replacement shell is ready.
                // Restore them only when the APE actually deactivates.
                Clear4DMMUiScaleApeOverlayRegions(fApeFrame ? fFalse : fTrue);
                Sync4DMMUiScaleWorkspaceOverlay();
            }
#endif

            // UI-layer synchronization otherwise remains driven by Kauai's
            // completed source drawing paths. Flush already-invalidated native
            // movie/APE paints during legacy TrackMouse loops.
            if (!vf4DMMUiScaleNativeDialogSuspended)
                UpdateWindow(hwnd);
            return 0;
        }
        break;

    case kwm4DMMUiScaleSourcePaint:
        // v58: Kauai supplies the exact completed source-draw bounds, including
        // updates that never become WM_PAINT. Use those source-client
        // coordinates as the compositor's authoritative candidate region,
        // then ask Kauai's own hit-test tree what currently
        // owns the freshly-painted pixels. This replaces the failed v54/v55
        // whole-tree rectangle hunt without returning to colour detection,
        // viewport hiding, PrintWindow, or input-time guesses.
        if (!vf4DMMUiScaleNativeDialogSuspended)
        {
            RC rcPaintSource;
            rcPaintSource.Set((short)LOWORD(wParam), (short)HIWORD(wParam),
                              (short)LOWORD(lParam), (short)HIWORD(lParam));
            Sync4DMMUiScaleWorkspaceOverlayFromSourcePaint(&rcPaintSource);
            // The source paint changed only Kauai's 2D overlay. Repainting the
            // native movie synchronously here races DWM's just-updated overlay
            // and is what lets a tooltip make the movie layer flash over an
            // easel. Leave the cached 3D base untouched; DWM can compose the
            // completed 2D source above it on its normal pass.
        }
        return 0;

    case WM_MOVE:
        Park4DMMUiScaleViewportSource();
        // The source HWND must remain underneath the presentation so it never
        // reappears as a second interactive 640x480 copy when the user moves
        // the 4x window.
        F4DMMParkUiScaleSourceWindow();
        break;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE && vwig.hwndApp != hNil && IsWindow(vwig.hwndApp))
        {
            // v25 deliberately removed the source->presentation ownership
            // relationship, so synchronize minimize explicitly.
            ShowWindow(vwig.hwndApp, SW_MINIMIZE);
            ShowWindow(hwnd, SW_MINIMIZE);
            return 0;
        }
        if ((wParam & 0xFFF0) == SC_RESTORE && vwig.hwndApp != hNil && IsWindow(vwig.hwndApp))
        {
            ShowWindow(vwig.hwndApp, SW_SHOWNOACTIVATE);
            F4DMMParkUiScaleSourceWindow();
            // Let DefWindowProc restore the presentation itself.
        }
        break;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    {
        // Forward input through the original Kauai path. The compositor does
        // not predict whether that input creates/destroys UI. Kauai draws
        // whatever actually changed, and the completed source-draw notification
        // updates the 2D compositor layer afterward.
        return Lresult4DMMForwardScaledMouse(hwnd, wm, wParam, lParam);
    }

    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    {
        // Mouse movement remains asynchronous exactly as before. No input
        // message directly synchronizes UI layers; Kauai's actual completed
        // source draw is the single source of truth for hover/transition updates.
        return Lresult4DMMForwardScaledMouse(hwnd, wm, wParam, lParam);
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && vwig.hwndApp != hNil && IsWindow(vwig.hwndApp))
        {
            HWND hwndApp = vwig.hwndApp;
            SendMessage(hwndApp, WM_SETCURSOR, reinterpret_cast<WPARAM>(hwndApp),
                        MAKELPARAM(HTCLIENT, HIWORD(lParam)));
            return fTrue;
        }
        break;

    case WM_DESTROY:
        if (hwnd == vhwnd4DMMUiScale)
            vhwnd4DMMUiScale = hNil;
        break;
    }

    return DefWindowProc(hwnd, wm, wParam, lParam);
}

static LRESULT CALLBACK Lresult4DMMViewportWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    // The 4x renderer now lives in a real top-level popup used only as the
    // OpenGL/DWM source. The presentation window owns all user input. Never
    // erase the GL source on incidental Win32 paints: keep the last complete
    // BRender frame visible until the next complete frame is swapped in.
    if (wm == WM_ERASEBKGND)
        return 1;
    if (wm == WM_PAINT)
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
#if defined(BRENDER_MODERN_14)
        if (FBrModernViewportReady())
            BrModernViewportPresent();
#endif
        return 0;
    }

    // Defensive fallback only. In 4x mode this source HWND is parked below the
    // presentation and should never be the hit-test target, but retaining the
    // mapping keeps input sane if Windows transiently exposes it.
    if (vfViewportResolution4x && F4DMMUiScaleActive())
    {
        switch (wm)
        {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
            return Lresult4DMMForwardScaledMouse(hwnd, wm, wParam, lParam);
        }
    }
    return DefWindowProc(hwnd, wm, wParam, lParam);
}

static bool F4DMMLoadDwmThumbnailApi(void)
{
    if (vpfn4DMMDwmRegisterThumbnail != pvNil && vpfn4DMMDwmUnregisterThumbnail != pvNil &&
        vpfn4DMMDwmUpdateThumbnailProperties != pvNil)
    {
        return fTrue;
    }

    if (vhmod4DMMDwmApi == hNil)
        vhmod4DMMDwmApi = LoadLibraryA("dwmapi.dll");
    if (vhmod4DMMDwmApi == hNil)
        return fFalse;

    vpfn4DMMDwmRegisterThumbnail = (PFN4DMMDwmRegisterThumbnail)GetProcAddress(vhmod4DMMDwmApi, "DwmRegisterThumbnail");
    vpfn4DMMDwmUnregisterThumbnail = (PFN4DMMDwmUnregisterThumbnail)GetProcAddress(vhmod4DMMDwmApi, "DwmUnregisterThumbnail");
    vpfn4DMMDwmUpdateThumbnailProperties =
        (PFN4DMMDwmUpdateThumbnailProperties)GetProcAddress(vhmod4DMMDwmApi, "DwmUpdateThumbnailProperties");
    return vpfn4DMMDwmRegisterThumbnail != pvNil && vpfn4DMMDwmUnregisterThumbnail != pvNil &&
           vpfn4DMMDwmUpdateThumbnailProperties != pvNil;
}

static bool F4DMMUpdateUiScaleThumbnail(void)
{
    if (!F4DMMUiScaleActive() || vhthumb4DMMUiScale == hNil || vpfn4DMMDwmUpdateThumbnailProperties == pvNil)
        return fFalse;

    RECT rcClient;
    if (!GetClientRect(vhwnd4DMMUiScale, &rcClient))
        return fFalse;

    DWMTHUMBNAILPROPERTIES4DMM props;
    ClearPb(&props, SIZEOF(props));
    props.dwFlags = kdw4DMMDwmTnpRectDestination | kdw4DMMDwmTnpOpacity | kdw4DMMDwmTnpVisible |
                    kdw4DMMDwmTnpSourceClientAreaOnly;
    props.rcDestination = rcClient;
    props.opacity = 255;
    props.fVisible = TRUE;
    props.fSourceClientAreaOnly = TRUE;
    return SUCCEEDED(vpfn4DMMDwmUpdateThumbnailProperties(vhthumb4DMMUiScale, &props));
}

static bool FEnsure4DMMUiScaleWindow(void)
{
    if (!vfViewportResolution4x)
        return fTrue;
    if (F4DMMUiScaleActive())
        return fTrue;
    if (vwig.hwndApp == hNil || !IsWindow(vwig.hwndApp) || !F4DMMLoadDwmThumbnailApi())
        return fFalse;

    WNDCLASSA wc;
    ClearPb(&wc, SIZEOF(wc));
    wc.style = CS_BYTEALIGNCLIENT;
    wc.lpfnWndProc = Lresult4DMMUiScaleWndProc;
    wc.hInstance = vwig.hinst;
    wc.hIcon = LoadIcon(vwig.hinst, MAKEINTRESOURCE(IDI_APP));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = ksz4DMMUiScaleWndClass;
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        MODERN_BR_LOG("4x UI RegisterClass failed gle=%lu", (unsigned long)GetLastError());
        return fFalse;
    }

    const DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                          WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    int32_t scaleNum;
    int32_t scaleDen;
    Get4DMMUiScaleRatio(&scaleNum, &scaleDen);
    const int32_t dxpUiClient = Lw4DMMScaleSourceToPresentation(640);
    const int32_t dypUiClient = Lw4DMMScaleSourceToPresentation(480);
    RECT rcWindow = {0, 0, dxpUiClient, dypUiClient};
    AdjustWindowRect(&rcWindow, dwStyle, fFalse);
    const int32_t dxpWindow = rcWindow.right - rcWindow.left;
    const int32_t dypWindow = rcWindow.bottom - rcWindow.top;

    RECT rcWork;
    if (!SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0))
    {
        rcWork.left = 0;
        rcWork.top = 0;
        rcWork.right = GetSystemMetrics(SM_CXSCREEN);
        rcWork.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    const int32_t xpWindow = rcWork.left + LwMax(((rcWork.right - rcWork.left) - dxpWindow) / 2, 0);
    const int32_t ypWindow = rcWork.top + LwMax(((rcWork.bottom - rcWork.top) - dypWindow) / 2, 0);

    // Do not make the 4x presentation an owned window of the 640x480 Kauai
    // source. Classic load/modal loops manipulate owner activation/enabled
    // state, which made the giant presentation non-interactive while the tiny
    // source remained clickable. Keep them independent and synchronize them
    // explicitly instead.
    vhwnd4DMMUiScale = CreateWindowExA(0, ksz4DMMUiScaleWndClass, "4DMM", dwStyle,
                                        xpWindow, ypWindow, dxpWindow, dypWindow,
                                        hNil, hNil, vwig.hinst, pvNil);
    if (vhwnd4DMMUiScale == hNil)
    {
        MODERN_BR_LOG("4x UI CreateWindowEx failed gle=%lu", (unsigned long)GetLastError());
        return fFalse;
    }

    SetPropA(vwig.hwndApp, ksz4DMMUiScaleWindowProp, (HANDLE)vhwnd4DMMUiScale);
    SetPropA(vwig.hwndApp, ksz4DMMUiScaleNumeratorProp, (HANDLE)(INT_PTR)scaleNum);
    SetPropA(vwig.hwndApp, ksz4DMMUiScaleDenominatorProp, (HANDLE)(INT_PTR)scaleDen);

    HRESULT hr = vpfn4DMMDwmRegisterThumbnail(vhwnd4DMMUiScale, vwig.hwndApp, &vhthumb4DMMUiScale);
    if (FAILED(hr) || vhthumb4DMMUiScale == hNil)
    {
        MODERN_BR_LOG("4x UI DwmRegisterThumbnail failed hr=0x%08lX", (unsigned long)hr);
        RemovePropA(vwig.hwndApp, ksz4DMMUiScaleSuspendedProp);
        RemovePropA(vwig.hwndApp, ksz4DMMUiScaleWindowProp);
        RemovePropA(vwig.hwndApp, ksz4DMMUiScaleNumeratorProp);
        RemovePropA(vwig.hwndApp, ksz4DMMUiScaleDenominatorProp);
        DestroyWindow(vhwnd4DMMUiScale);
        vhwnd4DMMUiScale = hNil;
        vhthumb4DMMUiScale = hNil;
        return fFalse;
    }
    if (!F4DMMUpdateUiScaleThumbnail())
    {
        MODERN_BR_LOG("4x UI DwmUpdateThumbnailProperties failed");
        vpfn4DMMDwmUnregisterThumbnail(vhthumb4DMMUiScale);
        vhthumb4DMMUiScale = hNil;
        RemovePropA(vwig.hwndApp, ksz4DMMUiScaleSuspendedProp);
        RemovePropA(vwig.hwndApp, ksz4DMMUiScaleWindowProp);
        RemovePropA(vwig.hwndApp, ksz4DMMUiScaleNumeratorProp);
        RemovePropA(vwig.hwndApp, ksz4DMMUiScaleDenominatorProp);
        DestroyWindow(vhwnd4DMMUiScale);
        vhwnd4DMMUiScale = hNil;
        return fFalse;
    }

    // The original 640x480 Kauai HWND must remain alive as DWM's source, but
    // it is no longer a user-facing application window in scaled mode. Remove
    // only its Alt-Tab/taskbar participation; do not introduce an owner chain
    // or change its activation semantics. The style is restored when scaled
    // presentation closes.
    if (!vf4DMMUiScaleSourceAltTabStyleChanged)
    {
        vl4DMMUiScaleSourceExStyleRestore = GetWindowLongPtr(vwig.hwndApp, GWL_EXSTYLE);
        const LONG_PTR exStyle = (vl4DMMUiScaleSourceExStyleRestore | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW;
        SetWindowLongPtr(vwig.hwndApp, GWL_EXSTYLE, exStyle);
        SetWindowPos(vwig.hwndApp, hNil, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        vf4DMMUiScaleSourceAltTabStyleChanged = fTrue;
        MODERN_BR_LOG("scaled UI source removed from Alt-Tab exstyle old=0x%p new=0x%p",
                      (void *)vl4DMMUiScaleSourceExStyleRestore, (void *)exStyle);
    }

    ShowWindow(vhwnd4DMMUiScale, SW_SHOW);
    UpdateWindow(vhwnd4DMMUiScale);
    F4DMMParkUiScaleSourceWindow();
    SetTimer(vhwnd4DMMUiScale, kid4DMMUiScaleOverlayTimer, 33, pvNil);
    RECT rcActualClient;
    GetClientRect(vhwnd4DMMUiScale, &rcActualClient);
    MODERN_BR_LOG("scaled UI compositor v80: playback-shell suppression on v78 activation baseline");
    MODERN_BR_LOG("scaled UI presentation ready source=%p presentation=%p requested_client=%dx%d actual_client=%ldx%ld scale=%ld/%ld",
                  vwig.hwndApp, vhwnd4DMMUiScale, (int)dxpUiClient, (int)dypUiClient,
                  (long)(rcActualClient.right - rcActualClient.left),
                  (long)(rcActualClient.bottom - rcActualClient.top),
                  (long)scaleNum, (long)scaleDen);
    return fTrue;
}

static bool FGet4DMM4xViewportRect(RECT *prcViewport)
{
    if (!vfViewportResolution4x || prcViewport == pvNil || !F4DMMUiScaleActive() || vapp.Pkwa() == pvNil)
        return fFalse;

    PGOB pgobWorkspace = vapp.Pkwa()->PgobFromHid(kidWorkspace);
    if (pgobWorkspace == pvNil)
        return fFalse;

    RC rcWorkspace;
    pgobWorkspace->GetRc(&rcWorkspace, cooHwnd);
    prcViewport->left = Lw4DMMScaleSourceToPresentation(rcWorkspace.xpLeft);
    prcViewport->top = Lw4DMMScaleSourceToPresentation(rcWorkspace.ypTop);
    // Scale both source boundaries through the same rational transform so a
    // fractional presentation cannot create a one-pixel seam at the viewport.
    prcViewport->right =
        Lw4DMMScaleSourceToPresentation(rcWorkspace.xpLeft + kdxpWorkspace);
    prcViewport->bottom =
        Lw4DMMScaleSourceToPresentation(rcWorkspace.ypTop + kdypWorkspace);
    return fTrue;
}

static void Clear4DMMUiScaleViewportThumbnail(void)
{
    if (vhthumb4DMMUiScaleViewport != hNil && vpfn4DMMDwmUnregisterThumbnail != pvNil)
        vpfn4DMMDwmUnregisterThumbnail(vhthumb4DMMUiScaleViewport);
    vhthumb4DMMUiScaleViewport = hNil;
}

static bool F4DMMUpdateUiScaleViewportThumbnail(bool fVisible)
{
    if (kf4DMMNativeMoviePaintedByPresentation)
    {
        // Keep the function contract for the existing viewport-source setup.
        // The native movie itself is painted by the visible presentation; the
        // source GL HWND stays alive independently for rendering.
        Clear4DMMUiScaleViewportThumbnail();
        if (F4DMMUiScaleActive())
        {
            RECT rcViewport;
            if (FGet4DMM4xViewportRect(&rcViewport))
                InvalidateRect(vhwnd4DMMUiScale, &rcViewport, fFalse);
        }
        return fTrue;
    }

    if (!F4DMMUiScaleActive() || vhwnd4DMMUiScaleViewport == hNil ||
        !IsWindow(vhwnd4DMMUiScaleViewport) || vpfn4DMMDwmRegisterThumbnail == pvNil ||
        vpfn4DMMDwmUpdateThumbnailProperties == pvNil)
    {
        return fFalse;
    }

    if (vhthumb4DMMUiScaleViewport == hNil)
    {
        HRESULT hrRegister = vpfn4DMMDwmRegisterThumbnail(vhwnd4DMMUiScale,
                                                           vhwnd4DMMUiScaleViewport,
                                                           &vhthumb4DMMUiScaleViewport);
        if (FAILED(hrRegister) || vhthumb4DMMUiScaleViewport == hNil)
        {
            MODERN_BR_LOG("4x UI viewport thumbnail register FAIL source=%p dest=%p hr=0x%08lX",
                          vhwnd4DMMUiScaleViewport, vhwnd4DMMUiScale,
                          (unsigned long)hrRegister);
            return fFalse;
        }
    }

    RECT rcViewport;
    RECT rcSource;
    if (!FGet4DMM4xViewportRect(&rcViewport) ||
        !GetClientRect(vhwnd4DMMUiScaleViewport, &rcSource))
    {
        return fFalse;
    }

    DWMTHUMBNAILPROPERTIES4DMM props;
    ClearPb(&props, SIZEOF(props));
    props.dwFlags = kdw4DMMDwmTnpRectDestination | kdw4DMMDwmTnpRectSource |
                    kdw4DMMDwmTnpOpacity | kdw4DMMDwmTnpVisible |
                    kdw4DMMDwmTnpSourceClientAreaOnly;
    props.rcDestination = rcViewport;
    props.rcSource = rcSource;
    props.opacity = 255;
    props.fVisible = fVisible ? TRUE : FALSE;
    props.fSourceClientAreaOnly = TRUE;
    HRESULT hr = vpfn4DMMDwmUpdateThumbnailProperties(vhthumb4DMMUiScaleViewport, &props);
    if (FAILED(hr))
    {
        MODERN_BR_LOG("4x UI viewport thumbnail update FAIL visible=%d hr=0x%08lX",
                      (int)fVisible, (unsigned long)hr);
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Keep the native 2176x1224 OpenGL window as a separate top-level source
    directly underneath the 4x presentation. DWM duplicates that completed GL
    surface into the workspace. Because the GL HWND is no longer a child of the
    visible 4x window, it cannot cover Kauai menus/dialogs or steal hover/mouse
    messages from the scaled UI.
***************************************************************************/
static void Park4DMMUiScaleViewportSource(void)
{
    if (F4DMMModernEmbeddedViewportHost())
        return;
    if (!F4DMMUiScaleActive() || vhwnd4DMMUiScaleViewport == hNil ||
        !IsWindow(vhwnd4DMMUiScaleViewport))
    {
        return;
    }

    RECT rcViewport;
    if (!FGet4DMM4xViewportRect(&rcViewport))
        return;
    POINT pt = {rcViewport.left, rcViewport.top};
    if (!ClientToScreen(vhwnd4DMMUiScale, &pt))
        return;

    const int32_t dxp = rcViewport.right - rcViewport.left;
    const int32_t dyp = rcViewport.bottom - rcViewport.top;
    SetWindowPos(vhwnd4DMMUiScaleViewport, vhwnd4DMMUiScale,
                 pt.x, pt.y, dxp, dyp,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void Clear4DMMUiScaleRegionThumbnails(void)
{
    if (vpfn4DMMDwmUnregisterThumbnail != pvNil)
    {
        for (int32_t iregion = 0; iregion < 4; ++iregion)
        {
            if (vrghthumb4DMMUiScaleRegion[iregion] != hNil)
                vpfn4DMMDwmUnregisterThumbnail(vrghthumb4DMMUiScaleRegion[iregion]);
            vrghthumb4DMMUiScaleRegion[iregion] = hNil;
        }
    }
    vf4DMMUiScaleViewportHole = fFalse;
    vf4DMMUiScaleBaseRegionsSuppressedByApe = fFalse;
}

static bool F4DMMRegisterUiScaleRegionThumbnail(int32_t iregion, const RECT *prcSource, const RECT *prcDest)
{
    if (iregion < 0 || iregion >= 4 || prcSource == pvNil || prcDest == pvNil ||
        prcSource->right <= prcSource->left || prcSource->bottom <= prcSource->top ||
        prcDest->right <= prcDest->left || prcDest->bottom <= prcDest->top)
    {
        return fTrue;
    }

    HRESULT hr = vpfn4DMMDwmRegisterThumbnail(vhwnd4DMMUiScale, vwig.hwndApp,
                                               &vrghthumb4DMMUiScaleRegion[iregion]);
    if (FAILED(hr) || vrghthumb4DMMUiScaleRegion[iregion] == hNil)
    {
        MODERN_BR_LOG("4x UI region thumbnail register FAIL region=%ld hr=0x%08lX",
                      (long)iregion, (unsigned long)hr);
        return fFalse;
    }

    DWMTHUMBNAILPROPERTIES4DMM props;
    ClearPb(&props, SIZEOF(props));
    props.dwFlags = kdw4DMMDwmTnpRectDestination | kdw4DMMDwmTnpRectSource |
                    kdw4DMMDwmTnpOpacity | kdw4DMMDwmTnpVisible |
                    kdw4DMMDwmTnpSourceClientAreaOnly;
    props.rcSource = *prcSource;
    props.rcDestination = *prcDest;
    props.opacity = 255;
    props.fVisible = TRUE;
    props.fSourceClientAreaOnly = TRUE;
    hr = vpfn4DMMDwmUpdateThumbnailProperties(vrghthumb4DMMUiScaleRegion[iregion], &props);
    if (FAILED(hr))
    {
        MODERN_BR_LOG("4x UI region thumbnail update FAIL region=%ld hr=0x%08lX",
                      (long)iregion, (unsigned long)hr);
        return fFalse;
    }
    return fTrue;
}

static void Set4DMMUiScaleBaseRegionsSuppressedByApe(bool fSuppress)
{
    if (vpfn4DMMDwmUpdateThumbnailProperties == pvNil)
    {
        if (!fSuppress)
            vf4DMMUiScaleBaseRegionsSuppressedByApe = fFalse;
        return;
    }

    DWMTHUMBNAILPROPERTIES4DMM props;
    ClearPb(&props, SIZEOF(props));
    props.dwFlags = kdw4DMMDwmTnpVisible;
    props.fVisible = fSuppress ? FALSE : TRUE;

    int32_t cUpdated = 0;
    int32_t cFailed = 0;
    for (int32_t iregion = 0; iregion < 4; ++iregion)
    {
        if (vrghthumb4DMMUiScaleRegion[iregion] == hNil)
            continue;
        ++cUpdated;
        const HRESULT hr = vpfn4DMMDwmUpdateThumbnailProperties(
            vrghthumb4DMMUiScaleRegion[iregion], &props);
        if (FAILED(hr))
        {
            ++cFailed;
            MODERN_BR_LOG("4x APE base-region visibility FAIL region=%ld suppress=%d hr=0x%08lX",
                          (long)iregion, (int)fSuppress, (unsigned long)hr);
        }
    }

    const bool fOld = vf4DMMUiScaleBaseRegionsSuppressedByApe;
    if (cFailed == 0 && (cUpdated > 0 || !fSuppress))
        vf4DMMUiScaleBaseRegionsSuppressedByApe = fSuppress;

    if (fOld != vf4DMMUiScaleBaseRegionsSuppressedByApe || cFailed > 0)
    {
        PSTDIO pstdioApeBase = vapp.Pstdio();
        MVIE::MultiLog(pstdioApeBase != pvNil ? pstdioApeBase->Pmvie() : pvNil,
                       "ape_base_regions suppress=%d updated=%ld failed=%ld active=%d",
                       (int)fSuppress, (long)cUpdated, (long)cFailed,
                       (int)vf4DMMUiScaleBaseRegionsSuppressedByApe);
    }
}

static void Clear4DMMUiScaleApeOverlayRegions(bool fRestoreBaseRegions)
{
    if (vpfn4DMMDwmUnregisterThumbnail != pvNil)
    {
        for (int32_t iregion = 0; iregion < kc4DMMUiScaleApeOverlayRegion; ++iregion)
        {
            if (vrghthumb4DMMUiScaleApeOverlayRegion[iregion] != hNil)
                vpfn4DMMDwmUnregisterThumbnail(vrghthumb4DMMUiScaleApeOverlayRegion[iregion]);
            vrghthumb4DMMUiScaleApeOverlayRegion[iregion] = hNil;
        }
    }
    vf4DMMUiScaleApeOverlayVisible = fFalse;
    ClearPb(&vrc4DMMUiScaleApeOverlayOwnerSource, SIZEOF(vrc4DMMUiScaleApeOverlayOwnerSource));
    ClearPb(&vrc4DMMUiScaleApeHoleSource, SIZEOF(vrc4DMMUiScaleApeHoleSource));
    if (fRestoreBaseRegions)
        Set4DMMUiScaleBaseRegionsSuppressedByApe(fFalse);
}

static bool F4DMMRectContainsRect(const RECT *prcOuter, const RECT *prcInner)
{
    return prcOuter != pvNil && prcInner != pvNil &&
           prcInner->right > prcInner->left && prcInner->bottom > prcInner->top &&
           prcOuter->left <= prcInner->left && prcOuter->top <= prcInner->top &&
           prcOuter->right >= prcInner->right && prcOuter->bottom >= prcInner->bottom;
}

static bool F4DMMRectIntersectsRect(const RECT *prcA, const RECT *prcB)
{
    return prcA != pvNil && prcB != pvNil &&
           prcA->left < prcB->right && prcA->right > prcB->left &&
           prcA->top < prcB->bottom && prcA->bottom > prcB->top;
}

static bool F4DMMRegisterUiScaleApeOverlayRegion(int32_t iregion,
                                                  const RECT *prcSource)
{
    if (iregion < 0 || iregion >= kc4DMMUiScaleApeOverlayRegion || prcSource == pvNil ||
        prcSource->right <= prcSource->left || prcSource->bottom <= prcSource->top)
        return fTrue;

    RECT rcDest;
    rcDest.left = Lw4DMMScaleSourceToPresentation(prcSource->left);
    rcDest.top = Lw4DMMScaleSourceToPresentation(prcSource->top);
    rcDest.right = Lw4DMMScaleSourceToPresentation(prcSource->right);
    rcDest.bottom = Lw4DMMScaleSourceToPresentation(prcSource->bottom);
    if (rcDest.right <= rcDest.left || rcDest.bottom <= rcDest.top)
        return fTrue;

    HRESULT hr = vpfn4DMMDwmRegisterThumbnail(vhwnd4DMMUiScale, vwig.hwndApp,
        &vrghthumb4DMMUiScaleApeOverlayRegion[iregion]);
    if (FAILED(hr) || vrghthumb4DMMUiScaleApeOverlayRegion[iregion] == hNil)
    {
        MODERN_BR_LOG("4x APE easel region register FAIL region=%ld hr=0x%08lX",
                      (long)iregion, (unsigned long)hr);
        return fFalse;
    }

    DWMTHUMBNAILPROPERTIES4DMM props;
    ClearPb(&props, SIZEOF(props));
    props.dwFlags = kdw4DMMDwmTnpRectDestination | kdw4DMMDwmTnpRectSource |
                    kdw4DMMDwmTnpOpacity | kdw4DMMDwmTnpVisible |
                    kdw4DMMDwmTnpSourceClientAreaOnly;
    props.rcSource = *prcSource;
    props.rcDestination = rcDest;
    props.opacity = 255;
    props.fVisible = TRUE;
    props.fSourceClientAreaOnly = TRUE;
    hr = vpfn4DMMDwmUpdateThumbnailProperties(
        vrghthumb4DMMUiScaleApeOverlayRegion[iregion], &props);
    if (FAILED(hr))
    {
        MODERN_BR_LOG("4x APE easel region update FAIL region=%ld hr=0x%08lX",
                      (long)iregion, (unsigned long)hr);
        return fFalse;
    }
    return fTrue;
}

static bool F4DMMUpdateUiScaleApeOverlay(const RECT *prcOwnerSource,
                                          const RECT *prcPreviewSource)
{
    if (!F4DMMUiScaleActive() || prcOwnerSource == pvNil || prcPreviewSource == pvNil ||
        !F4DMMRectContainsRect(prcOwnerSource, prcPreviewSource) ||
        vpfn4DMMDwmRegisterThumbnail == pvNil || vpfn4DMMDwmUpdateThumbnailProperties == pvNil ||
        vpfn4DMMDwmUnregisterThumbnail == pvNil)
        return fFalse;

    if (vf4DMMUiScaleApeOverlayVisible &&
        F4DMMRectEqual(prcOwnerSource, &vrc4DMMUiScaleApeOverlayOwnerSource) &&
        F4DMMRectEqual(prcPreviewSource, &vrc4DMMUiScaleApeHoleSource))
    {
        RECT rcSourceClientSame;
        const bool fOwnerCoversClientSame = GetClientRect(vwig.hwndApp, &rcSourceClientSame) &&
            F4DMMRectContainsRect(prcOwnerSource, &rcSourceClientSame);
        Set4DMMUiScaleBaseRegionsSuppressedByApe(fOwnerCoversClientSame);
        return fTrue;
    }

    // Establishing or moving an APE hole retires generic thumbnails that were
    // already covering it. Do this only when the ownership geometry changes.
    // Repeating the purge on every source paint erased the popup/font menus
    // that are legitimately supposed to appear above the native preview.
    Clear4DMMUiScaleWorkspaceOverlaysIntersectingApe(prcPreviewSource);
    Clear4DMMUiScaleApeOverlayRegions(fFalse);

    // Four disjoint live source rectangles reproduce the easel chrome while
    // excluding only the APE viewport. DWM therefore never contributes the
    // legacy 640x480 3D pixels at that location; the presentation's native
    // BRender cache owns the hole. Transient menu/tooltip slots remain separate
    // and can still intentionally cross the hole above the 3D preview.
    RECT rgsrc[kc4DMMUiScaleApeOverlayRegion] = {
        {prcOwnerSource->left, prcOwnerSource->top,
         prcOwnerSource->right, prcPreviewSource->top},
        {prcOwnerSource->left, prcPreviewSource->bottom,
         prcOwnerSource->right, prcOwnerSource->bottom},
        {prcOwnerSource->left, prcPreviewSource->top,
         prcPreviewSource->left, prcPreviewSource->bottom},
        {prcPreviewSource->right, prcPreviewSource->top,
         prcOwnerSource->right, prcPreviewSource->bottom},
    };

    for (int32_t iregion = 0; iregion < kc4DMMUiScaleApeOverlayRegion; ++iregion)
    {
        if (!F4DMMRegisterUiScaleApeOverlayRegion(iregion, &rgsrc[iregion]))
        {
            Clear4DMMUiScaleApeOverlayRegions(fTrue);
            return fFalse;
        }
    }

    vrc4DMMUiScaleApeOverlayOwnerSource = *prcOwnerSource;
    vrc4DMMUiScaleApeHoleSource = *prcPreviewSource;
    vf4DMMUiScaleApeOverlayVisible = fTrue;

    RECT rcSourceClient;
    const bool fOwnerCoversClient = GetClientRect(vwig.hwndApp, &rcSourceClient) &&
        F4DMMRectContainsRect(prcOwnerSource, &rcSourceClient);
    Set4DMMUiScaleBaseRegionsSuppressedByApe(fOwnerCoversClient);

    MODERN_BR_LOG("4x APE easel compositor ready owner=(%ld,%ld)-(%ld,%ld) hole=(%ld,%ld)-(%ld,%ld) base_suppressed=%d",
                  (long)prcOwnerSource->left, (long)prcOwnerSource->top,
                  (long)prcOwnerSource->right, (long)prcOwnerSource->bottom,
                  (long)prcPreviewSource->left, (long)prcPreviewSource->top,
                  (long)prcPreviewSource->right, (long)prcPreviewSource->bottom,
                  (int)vf4DMMUiScaleBaseRegionsSuppressedByApe);
    PSTDIO pstdioApe = vapp.Pstdio();
    MVIE::MultiLog(pstdioApe != pvNil ? pstdioApe->Pmvie() : pvNil,
                   "ape_compositor ready owner=(%ld,%ld)-(%ld,%ld) preview=(%ld,%ld)-(%ld,%ld) base_suppressed=%d",
                   (long)prcOwnerSource->left, (long)prcOwnerSource->top,
                   (long)prcOwnerSource->right, (long)prcOwnerSource->bottom,
                   (long)prcPreviewSource->left, (long)prcPreviewSource->top,
                   (long)prcPreviewSource->right, (long)prcPreviewSource->bottom,
                   (int)vf4DMMUiScaleBaseRegionsSuppressedByApe);
    return fTrue;
}

/***************************************************************************
    DWM thumbnails are compositor surfaces. v14 placed one full-window DWM
    thumbnail over the 2560x1920 presentation and then created the native GL
    viewport as a child HWND underneath it. The renderer was healthy, but the
    full DWM thumbnail covered that child with the source editor's intentionally
    black legacy viewport. Split the UI thumbnail into four rectangles around
    the movie workspace so the real 2176x1224 GL child owns the hole.
***************************************************************************/
static bool F4DMMMakeUiScaleViewportHole(const RECT *prcViewportDest)
{
    if (!F4DMMUiScaleActive() || prcViewportDest == pvNil || vf4DMMUiScaleViewportHole)
        return vf4DMMUiScaleViewportHole;
    if (vpfn4DMMDwmRegisterThumbnail == pvNil || vpfn4DMMDwmUpdateThumbnailProperties == pvNil ||
        vpfn4DMMDwmUnregisterThumbnail == pvNil)
    {
        return fFalse;
    }

    RECT rcSourceClient;
    if (!GetClientRect(vwig.hwndApp, &rcSourceClient))
        return fFalse;

    RECT rcViewportSource;
    rcViewportSource.left = Lw4DMMScalePresentationToSource(prcViewportDest->left);
    rcViewportSource.top = Lw4DMMScalePresentationToSource(prcViewportDest->top);
    rcViewportSource.right = rcViewportSource.left + kdxpWorkspace;
    rcViewportSource.bottom = rcViewportSource.top + kdypWorkspace;

    if (rcViewportSource.left < rcSourceClient.left || rcViewportSource.top < rcSourceClient.top ||
        rcViewportSource.right > rcSourceClient.right || rcViewportSource.bottom > rcSourceClient.bottom)
    {
        MODERN_BR_LOG("4x UI viewport-hole bounds invalid source=(%ld,%ld)-(%ld,%ld) client=(%ld,%ld)-(%ld,%ld)",
                      (long)rcViewportSource.left, (long)rcViewportSource.top,
                      (long)rcViewportSource.right, (long)rcViewportSource.bottom,
                      (long)rcSourceClient.left, (long)rcSourceClient.top,
                      (long)rcSourceClient.right, (long)rcSourceClient.bottom);
        return fFalse;
    }

    if (vhthumb4DMMUiScale != hNil)
    {
        vpfn4DMMDwmUnregisterThumbnail(vhthumb4DMMUiScale);
        vhthumb4DMMUiScale = hNil;
    }
    Clear4DMMUiScaleRegionThumbnails();

    RECT rgsrc[4] = {
        {rcSourceClient.left, rcSourceClient.top, rcSourceClient.right, rcViewportSource.top},
        {rcSourceClient.left, rcViewportSource.bottom, rcSourceClient.right, rcSourceClient.bottom},
        {rcSourceClient.left, rcViewportSource.top, rcViewportSource.left, rcViewportSource.bottom},
        {rcViewportSource.right, rcViewportSource.top, rcSourceClient.right, rcViewportSource.bottom},
    };
    RECT rgdst[4];
    for (int32_t iregion = 0; iregion < 4; ++iregion)
    {
        rgdst[iregion].left = Lw4DMMScaleSourceToPresentation(rgsrc[iregion].left);
        rgdst[iregion].top = Lw4DMMScaleSourceToPresentation(rgsrc[iregion].top);
        rgdst[iregion].right = Lw4DMMScaleSourceToPresentation(rgsrc[iregion].right);
        rgdst[iregion].bottom = Lw4DMMScaleSourceToPresentation(rgsrc[iregion].bottom);
        if (!F4DMMRegisterUiScaleRegionThumbnail(iregion, &rgsrc[iregion], &rgdst[iregion]))
        {
            Clear4DMMUiScaleRegionThumbnails();
            // Restore the old full-window presentation rather than leave a
            // partially blank UI if DWM rejects one of the region thumbnails.
            HRESULT hr = vpfn4DMMDwmRegisterThumbnail(vhwnd4DMMUiScale, vwig.hwndApp, &vhthumb4DMMUiScale);
            if (SUCCEEDED(hr) && vhthumb4DMMUiScale != hNil)
                F4DMMUpdateUiScaleThumbnail();
            return fFalse;
        }
    }

    vf4DMMUiScaleViewportHole = fTrue;
    if (vf4DMMUiScaleApeOverlayVisible)
    {
        RECT rcSourceClientNow;
        if (GetClientRect(vwig.hwndApp, &rcSourceClientNow) &&
            F4DMMRectContainsRect(&vrc4DMMUiScaleApeOverlayOwnerSource, &rcSourceClientNow))
        {
            Set4DMMUiScaleBaseRegionsSuppressedByApe(fTrue);
        }
    }
    MODERN_BR_LOG("4x UI compositor viewport hole ready source=(%ld,%ld)-(%ld,%ld) dest=(%ld,%ld)-(%ld,%ld)",
                  (long)rcViewportSource.left, (long)rcViewportSource.top,
                  (long)rcViewportSource.right, (long)rcViewportSource.bottom,
                  (long)prcViewportDest->left, (long)prcViewportDest->top,
                  (long)prcViewportDest->right, (long)prcViewportDest->bottom);
    return fTrue;
}

static bool F4DMMGobIsAncestorOrDescendant(PGOB pgobA, PGOB pgobB)
{
    if (pgobA == pvNil || pgobB == pvNil)
        return fFalse;

    for (PGOB pgob = pgobA; pgob != pvNil; pgob = pgob->PgobPar())
    {
        if (pgob == pgobB)
            return fTrue;
    }
    for (PGOB pgob = pgobB; pgob != pvNil; pgob = pgob->PgobPar())
    {
        if (pgob == pgobA)
            return fTrue;
    }
    return fFalse;
}

/***************************************************************************
    Return true only when this unrelated GOB branch owns a currently hittable
    point inside the visible workspace intersection. GetRcVis is important:
    old Kauai dialogs can leave helper GOB objects alive after the visible UI
    has gone away, so stale GetRc rectangles must not create phantom overlay
    layers after the actual UI disappears.
***************************************************************************/
static bool F4DMMGobOwnsVisibleWorkspacePoint(PGOB pgob, RC *prcIntersect)
{
    if (pgob == pvNil || prcIntersect == pvNil || prcIntersect->FEmpty() ||
        vwig.hwndApp == hNil || !IsWindow(vwig.hwndApp))
    {
        return fFalse;
    }

    // v55: discovery still walks Kauai's complete screen GOB tree, but the
    // ownership test must use the same coordinate contract as Kauai's real
    // source HWND mouse path. appbwin.cpp handles WM_MOUSEMOVE by taking the
    // 640x480 client coordinates and calling PgobFromHwnd(hwnd)->PgobFromPt()
    // directly. v54 instead converted those coordinates to Win32 desktop
    // pixels and passed them to PgobScreen()->PgobFromPt(), mixing two
    // unrelated coordinate spaces and rejecting the popup/picker GOBs even
    // though they were clickable underneath the presentation.
    PGOB pgobSourceRoot = GOB::PgobFromHwnd(vwig.hwndApp);
    if (pgobSourceRoot == pvNil)
        return fFalse;

    const int32_t xpLeft = prcIntersect->xpLeft;
    const int32_t xpRight = LwMax(prcIntersect->xpRight - 1, xpLeft);
    const int32_t ypTop = prcIntersect->ypTop;
    const int32_t ypBottom = LwMax(prcIntersect->ypBottom - 1, ypTop);
    const int32_t rgxp[3] = {xpLeft, (xpLeft + xpRight) / 2, xpRight};
    const int32_t rgyp[3] = {ypTop, (ypTop + ypBottom) / 2, ypBottom};

    for (int32_t iy = 0; iy < 3; ++iy)
    {
        for (int32_t ix = 0; ix < 3; ++ix)
        {
            PT ptLocal;
            PGOB pgobHit = pgobSourceRoot->PgobFromPt(rgxp[ix], rgyp[iy], &ptLocal);
            if (pgobHit != pvNil && F4DMMGobIsAncestorOrDescendant(pgobHit, pgob))
                return fTrue;
        }
    }
    return fFalse;
}

/***************************************************************************
    Find a *visible* Kauai/GOB overlay that crosses the movie workspace.

    File menus, Add Scene/Actor/Prop browsers, 3D Word tools, and classic
    confirmation panels are already painted correctly by Kauai into the live
    640x480 source HWND. v50 uses their actual visible GOB rectangle as a DWM
    layer above the always-live native movie instead of hiding the movie or
    guessing from source pixel colours.
***************************************************************************/
static PGOB Pgob4DMMUiScaleWorkspaceOverlay(void)
{
    if (!F4DMMUiScaleActive() || vapp.Pkwa() == pvNil)
        return pvNil;

    PGOB pgobWorkspace = vapp.Pkwa()->PgobFromHid(kidWorkspace);
    if (pgobWorkspace == pvNil)
        return pvNil;

    RC rcWorkspace;
    pgobWorkspace->GetRcVis(&rcWorkspace, cooHwnd);
    if (rcWorkspace.FEmpty())
        return pvNil;

    // v54: enumerate the complete Kauai screen GOB tree. File menus and
    // picker/easel UI may be siblings of Pkwa rather than descendants of it;
    // limiting traversal to Pkwa makes those live, painted GOBs impossible for
    // the compositor to discover even though their commands remain clickable.
    PGOB pgobTraversalRoot = GOB::PgobScreen();
    if (pgobTraversalRoot == pvNil)
        return pvNil;

    GTE gte;
    PGOB pgob;
    uint32_t grfgte;
    int32_t cGobVisited = 0;
    int32_t cGobWorkspaceIntersect = 0;
    PGOB pgobFirstIntersect = pvNil;
    RC rcFirstIntersect;
    rcFirstIntersect.Set(0, 0, 0, 0);
    gte.Init(pgobTraversalRoot, fgteNil);
    while (gte.FNextGob(&pgob, &grfgte, fgteNil))
    {
        if (!(grfgte & fgtePre) || pgob == pvNil)
            continue;
        ++cGobVisited;
        if (pgob == pgobTraversalRoot || pgob == vapp.Pkwa() || pgob == pgobWorkspace)
            continue;

        // The MVU is the permanent movie-view GOB itself, not an overlay. v27
        // accidentally classified it as a workspace overlay because it
        // naturally owns visible points across the entire viewport. Ignore the
        // MVU and anything in its own subtree; real menus/browsers/dialogs
        // live outside the movie-view subtree and are still detected below.
        bool fMovieViewGob = pgob->FIs(kclsMVU);
        for (PGOB pgobWalk = pgob->PgobPar(); !fMovieViewGob && pgobWalk != pvNil; pgobWalk = pgobWalk->PgobPar())
        {
            if (pgobWalk->FIs(kclsMVU))
                fMovieViewGob = fTrue;
        }
        if (fMovieViewGob)
            continue;

        // Ancestors of the workspace are ordinary permanent UI containers.
        // Descendants are *not* excluded: classic File menus, Add Scene/Prop/
        // Actor browsers and several confirmation panels are created inside
        // the workspace branch itself. v20 skipped those descendants, which
        // is why they vanished underneath the native viewport.
        bool fWorkspaceAncestor = fFalse;
        for (PGOB pgobWalk = pgobWorkspace->PgobPar(); pgobWalk != pvNil; pgobWalk = pgobWalk->PgobPar())
        {
            if (pgobWalk == pgob)
            {
                fWorkspaceAncestor = fTrue;
                break;
            }
        }
        if (fWorkspaceAncestor)
            continue;

        RC rcGob;
        pgob->GetRcVis(&rcGob, cooHwnd);
        if (rcGob.FEmpty())
            continue;

        RC rcIntersect = rcGob;
        if (!rcIntersect.FIntersect(&rcWorkspace) || rcIntersect.FEmpty())
            continue;

        ++cGobWorkspaceIntersect;
        if (pgobFirstIntersect == pvNil)
        {
            pgobFirstIntersect = pgob;
            rcFirstIntersect = rcIntersect;
        }

        if (F4DMMGobOwnsVisibleWorkspacePoint(pgob, &rcIntersect))
            return pgob;
    }

    // Log only when the screen-tree shape changes. This gives the next test a
    // useful distinction between "no overlay GOB exists in the traversal" and
    // "overlay-shaped GOBs exist but ownership validation still rejects them"
    // without turning every Kauai paint into another diagnostic leaf blower.
    static int32_t cGobVisitedLast = -1;
    static int32_t cGobWorkspaceIntersectLast = -1;
    if (cGobVisited != cGobVisitedLast || cGobWorkspaceIntersect != cGobWorkspaceIntersectLast)
    {
        if (pgobFirstIntersect != pvNil)
        {
            MODERN_BR_LOG("4x UI workspace overlay MISS screen_gobs=%ld intersecting=%ld first=%p hid=%ld cls=0x%08lX rc=(%ld,%ld)-(%ld,%ld)",
                          (long)cGobVisited, (long)cGobWorkspaceIntersect,
                          pgobFirstIntersect, (long)pgobFirstIntersect->Hid(),
                          (unsigned long)pgobFirstIntersect->Cls(),
                          (long)rcFirstIntersect.xpLeft, (long)rcFirstIntersect.ypTop,
                          (long)rcFirstIntersect.xpRight, (long)rcFirstIntersect.ypBottom);
        }
        else
        {
            MODERN_BR_LOG("4x UI workspace overlay MISS screen_gobs=%ld intersecting=0",
                          (long)cGobVisited);
        }
        cGobVisitedLast = cGobVisited;
        cGobWorkspaceIntersectLast = cGobWorkspaceIntersect;
    }
    return pvNil;
}

static bool F4DMMRectEqual(const RECT *prcA, const RECT *prcB)
{
    return prcA != pvNil && prcB != pvNil &&
           prcA->left == prcB->left && prcA->top == prcB->top &&
           prcA->right == prcB->right && prcA->bottom == prcB->bottom;
}

static bool F4DMMRectIntersectsRc(const RECT *prcA, const RC *prcB)
{
    return prcA != pvNil && prcB != pvNil &&
           prcA->left < prcB->xpRight && prcA->right > prcB->xpLeft &&
           prcA->top < prcB->ypBottom && prcA->bottom > prcB->ypTop;
}

static void Clear4DMMUiScaleWorkspaceOverlaySlot(HANDLE *phthumb, bool *pfVisible, RECT *prcSource)
{
    if (phthumb == pvNil || pfVisible == pvNil || prcSource == pvNil)
        return;

    if (vi4DMMUiScaleToolTipOverlaySlot == 0 && phthumb == &vhthumb4DMMUiScaleWorkspaceOverlay)
        vi4DMMUiScaleToolTipOverlaySlot = -1;
    else if (vi4DMMUiScaleToolTipOverlaySlot > 0)
    {
        const int32_t iExtra = vi4DMMUiScaleToolTipOverlaySlot - 1;
        if (iExtra >= 0 && iExtra < kc4DMMUiScaleWorkspaceOverlayExtra &&
            phthumb == &vrghthumb4DMMUiScaleWorkspaceOverlayExtra[iExtra])
        {
            vi4DMMUiScaleToolTipOverlaySlot = -1;
        }
    }

    if (*phthumb != hNil && vpfn4DMMDwmUnregisterThumbnail != pvNil)
        vpfn4DMMDwmUnregisterThumbnail(*phthumb);
    *phthumb = hNil;
    *pfVisible = fFalse;
    ClearPb(prcSource, SIZEOF(*prcSource));
}

static void Clear4DMMUiScaleWorkspaceOverlayThumbnail(void)
{
    Clear4DMMUiScaleWorkspaceOverlaySlot(&vhthumb4DMMUiScaleWorkspaceOverlay,
                                         &vf4DMMUiScaleWorkspaceOverlayVisible,
                                         &vrc4DMMUiScaleWorkspaceOverlaySource);
    for (int32_t i = 0; i < kc4DMMUiScaleWorkspaceOverlayExtra; ++i)
    {
        Clear4DMMUiScaleWorkspaceOverlaySlot(&vrghthumb4DMMUiScaleWorkspaceOverlayExtra[i],
                                             &vrgf4DMMUiScaleWorkspaceOverlayExtraVisible[i],
                                             &vrgrc4DMMUiScaleWorkspaceOverlayExtraSource[i]);
    }
}

// When an APE first claims or moves a native preview hole, retire generic
// thumbnails that were already covering that rectangle. After ownership is
// established, intentionally transient Kauai UI (tooltips and MP/MPFNT popup
// menus) may create a fresh overlay above the native 3D preview.
static void Clear4DMMUiScaleWorkspaceOverlaysIntersectingApe(const RECT *prcApe)
{
    if (prcApe == pvNil)
        return;

    // This is a transition cleanup, not a permanent z-order prohibition.
    // The caller invokes it when the APE owner/hole geometry changes; popup
    // menus and tooltips created afterward are allowed above the native frame.
    if (vf4DMMUiScaleWorkspaceOverlayVisible &&
        F4DMMRectIntersectsRect(prcApe, &vrc4DMMUiScaleWorkspaceOverlaySource))
    {
        Clear4DMMUiScaleWorkspaceOverlaySlot(&vhthumb4DMMUiScaleWorkspaceOverlay,
                                             &vf4DMMUiScaleWorkspaceOverlayVisible,
                                             &vrc4DMMUiScaleWorkspaceOverlaySource);
    }
    for (int32_t i = 0; i < kc4DMMUiScaleWorkspaceOverlayExtra; ++i)
    {
        if (!vrgf4DMMUiScaleWorkspaceOverlayExtraVisible[i] ||
            !F4DMMRectIntersectsRect(prcApe, &vrgrc4DMMUiScaleWorkspaceOverlayExtraSource[i]))
            continue;
        Clear4DMMUiScaleWorkspaceOverlaySlot(&vrghthumb4DMMUiScaleWorkspaceOverlayExtra[i],
                                             &vrgf4DMMUiScaleWorkspaceOverlayExtraVisible[i],
                                             &vrgrc4DMMUiScaleWorkspaceOverlayExtraSource[i]);
    }
}

/***************************************************************************
    Return true when pgob belongs to the permanent movie-view branch. The
    native BRender frame replaces only this branch in 4x presentation mode;
    every unrelated Kauai GOB remains eligible to paint above it.
***************************************************************************/
static bool F4DMMGobInMovieViewBranch(PGOB pgob)
{
    for (PGOB pgobWalk = pgob; pgobWalk != pvNil; pgobWalk = pgobWalk->PgobPar())
    {
        if (pgobWalk->FIs(kclsMVU))
            return fTrue;
    }
    return fFalse;
}

static bool F4DMMGobInApeTransientUiBranch(PGOB pgob)
{
    for (PGOB pgobWalk = pgob; pgobWalk != pvNil; pgobWalk = pgobWalk->PgobPar())
    {
        // MP is the normal shape/material popup; MPFNT owns the font popup.
        // These are real 2D Kauai surfaces and, like tooltips, must composite
        // above the native APE even when their rectangle crosses the 3D hole.
        if (pgobWalk->FIs(kclsMP) || pgobWalk->FIs(kclsMPFNT))
            return fTrue;
    }
    return fFalse;
}

static bool FGet4DMMUiScaleApePreviewSourceRect(RECT *prcPreview)
{
#if defined(BRENDER_MODERN_14)
    if (prcPreview == pvNil)
        return fFalse;
    int32_t xp = 0;
    int32_t yp = 0;
    int32_t dxp = 0;
    int32_t dyp = 0;
    if (!FBrModernPreviewGetActiveSourceRect(&xp, &yp, &dxp, &dyp) || dxp <= 0 || dyp <= 0)
        return fFalse;
    prcPreview->left = xp;
    prcPreview->top = yp;
    prcPreview->right = xp + dxp;
    prcPreview->bottom = yp + dyp;
    return fTrue;
#else
    (void)prcPreview;
    return fFalse;
#endif
}

/***************************************************************************
    Starting with a GOB already known to belong to the active easel, walk up
    until the first visible non-movie ancestor encloses the complete native APE
    preview. This identifies the Kauai easel shell without relying on specific
    1995 class IDs for 3D Word, Costume Changer or Action.
***************************************************************************/
static bool FGet4DMMUiScaleApeOwnerSourceRect(RECT *prcOwner)
{
#if defined(BRENDER_MODERN_14)
    if (prcOwner == pvNil)
        return fFalse;
    int32_t xp = 0, yp = 0, dxp = 0, dyp = 0;
    if (!FBrModernPreviewGetOwnerSourceRect(&xp, &yp, &dxp, &dyp) || dxp <= 0 || dyp <= 0)
        return fFalse;
    prcOwner->left = xp;
    prcOwner->top = yp;
    prcOwner->right = xp + dxp;
    prcOwner->bottom = yp + dyp;
    return fTrue;
#else
    (void)prcOwner;
    return fFalse;
#endif
}

static void Clear4DMMUiScaleApePopupOverlay(void)
{
    if (vhthumb4DMMUiScaleApePopup != hNil && vpfn4DMMDwmUnregisterThumbnail != pvNil)
        vpfn4DMMDwmUnregisterThumbnail(vhthumb4DMMUiScaleApePopup);
    vhthumb4DMMUiScaleApePopup = hNil;
    vpgob4DMMUiScaleApePopup = pvNil;
    ClearPb(&vrc4DMMUiScaleApePopupSource, SIZEOF(vrc4DMMUiScaleApePopupSource));
}

void Show4DMMUiScaleApePopupOverlay(PGOB pgobPopup)
{
    if (pgobPopup == pvNil || !F4DMMUiScaleActive() || vhwnd4DMMUiScale == hNil ||
        !IsWindow(vhwnd4DMMUiScale) || vwig.hwndApp == hNil || !IsWindow(vwig.hwndApp) ||
        vpfn4DMMDwmRegisterThumbnail == pvNil ||
        vpfn4DMMDwmUpdateThumbnailProperties == pvNil ||
        vpfn4DMMDwmUnregisterThumbnail == pvNil)
        return;

    RECT rcApePreview;
    if (!FGet4DMMUiScaleApePreviewSourceRect(&rcApePreview))
        return;

    RC rcPopup;
    pgobPopup->GetRcVis(&rcPopup, cooHwnd);
    if (rcPopup.FEmpty())
        return;
    RECT rcClient;
    if (!GetClientRect(vwig.hwndApp, &rcClient))
        return;
    RECT rcSource = {LwMax(rcPopup.xpLeft, rcClient.left),
                     LwMax(rcPopup.ypTop, rcClient.top),
                     LwMin(rcPopup.xpRight, rcClient.right),
                     LwMin(rcPopup.ypBottom, rcClient.bottom)};
    if (rcSource.right <= rcSource.left || rcSource.bottom <= rcSource.top)
        return;

    if (!F4DMMRectIntersectsRect(&rcSource, &rcApePreview))
        return;

    Clear4DMMUiScaleApePopupOverlay();
    HRESULT hr = vpfn4DMMDwmRegisterThumbnail(vhwnd4DMMUiScale, vwig.hwndApp,
                                               &vhthumb4DMMUiScaleApePopup);
    if (FAILED(hr) || vhthumb4DMMUiScaleApePopup == hNil)
    {
        PSTDIO pstdioFail = vapp.Pstdio();
        MVIE::MultiLog(pstdioFail != pvNil ? pstdioFail->Pmvie() : pvNil,
            "ape_popup_overlay register_fail cls=0x%08lX hid=%ld hr=0x%08lX source=(%ld,%ld)-(%ld,%ld)",
            (unsigned long)pgobPopup->Cls(), (long)pgobPopup->Hid(),
            (unsigned long)hr, (long)rcSource.left, (long)rcSource.top,
            (long)rcSource.right, (long)rcSource.bottom);
        vhthumb4DMMUiScaleApePopup = hNil;
        return;
    }

    RECT rcDest = {Lw4DMMScaleSourceToPresentation(rcSource.left),
                   Lw4DMMScaleSourceToPresentation(rcSource.top),
                   Lw4DMMScaleSourceToPresentation(rcSource.right),
                   Lw4DMMScaleSourceToPresentation(rcSource.bottom)};
    DWMTHUMBNAILPROPERTIES4DMM props;
    ClearPb(&props, SIZEOF(props));
    props.dwFlags = kdw4DMMDwmTnpRectDestination | kdw4DMMDwmTnpRectSource |
                    kdw4DMMDwmTnpOpacity | kdw4DMMDwmTnpVisible |
                    kdw4DMMDwmTnpSourceClientAreaOnly;
    props.rcSource = rcSource;
    props.rcDestination = rcDest;
    props.opacity = 255;
    props.fVisible = TRUE;
    props.fSourceClientAreaOnly = TRUE;
    hr = vpfn4DMMDwmUpdateThumbnailProperties(vhthumb4DMMUiScaleApePopup, &props);
    if (FAILED(hr))
    {
        PSTDIO pstdioFail = vapp.Pstdio();
        MVIE::MultiLog(pstdioFail != pvNil ? pstdioFail->Pmvie() : pvNil,
            "ape_popup_overlay update_fail cls=0x%08lX hid=%ld hr=0x%08lX source=(%ld,%ld)-(%ld,%ld)",
            (unsigned long)pgobPopup->Cls(), (long)pgobPopup->Hid(),
            (unsigned long)hr, (long)rcSource.left, (long)rcSource.top,
            (long)rcSource.right, (long)rcSource.bottom);
        Clear4DMMUiScaleApePopupOverlay();
        return;
    }

    vpgob4DMMUiScaleApePopup = pgobPopup;
    vrc4DMMUiScaleApePopupSource = rcSource;
    PSTDIO pstdio = vapp.Pstdio();
    MVIE::MultiLog(pstdio != pvNil ? pstdio->Pmvie() : pvNil,
        "ape_popup_overlay show cls=0x%08lX hid=%ld source=(%ld,%ld)-(%ld,%ld) dest=(%ld,%ld)-(%ld,%ld)",
        (unsigned long)pgobPopup->Cls(), (long)pgobPopup->Hid(),
        (long)rcSource.left, (long)rcSource.top, (long)rcSource.right, (long)rcSource.bottom,
        (long)rcDest.left, (long)rcDest.top, (long)rcDest.right, (long)rcDest.bottom);
}

void Hide4DMMUiScaleApePopupOverlay(PGOB pgobPopup)
{
    if (pgobPopup == pvNil || pgobPopup != vpgob4DMMUiScaleApePopup)
        return;
    PSTDIO pstdio = vapp.Pstdio();
    MVIE::MultiLog(pstdio != pvNil ? pstdio->Pmvie() : pvNil,
        "ape_popup_overlay hide cls=0x%08lX hid=%ld source=(%ld,%ld)-(%ld,%ld)",
        (unsigned long)pgobPopup->Cls(), (long)pgobPopup->Hid(),
        (long)vrc4DMMUiScaleApePopupSource.left, (long)vrc4DMMUiScaleApePopupSource.top,
        (long)vrc4DMMUiScaleApePopupSource.right, (long)vrc4DMMUiScaleApePopupSource.bottom);
    Clear4DMMUiScaleApePopupOverlay();
}

static PGOB Pgob4DMMUiScaleApeOwnerFromSeed(PGOB pgobSeed, const RECT *prcPreview,
                                             RC *prcOwner)
{
    if (pgobSeed == pvNil || prcPreview == pvNil || prcOwner == pvNil || vapp.Pkwa() == pvNil)
        return pvNil;
    PGOB pgobWorkspace = vapp.Pkwa()->PgobFromHid(kidWorkspace);
    if (pgobWorkspace == pvNil)
        return pvNil;
    RC rcWorkspace;
    pgobWorkspace->GetRcVis(&rcWorkspace, cooHwnd);
    if (rcWorkspace.FEmpty())
        return pvNil;

    for (PGOB pgobWalk = pgobSeed; pgobWalk != pvNil; pgobWalk = pgobWalk->PgobPar())
    {
        if (pgobWalk == pgobWorkspace || pgobWalk == vapp.Pkwa() ||
            F4DMMGobInMovieViewBranch(pgobWalk))
            continue;
        RC rcCandidate;
        pgobWalk->GetRcVis(&rcCandidate, cooHwnd);
        if (rcCandidate.FEmpty())
            continue;
        RC rcClip = rcCandidate;
        if (!rcClip.FIntersect(&rcWorkspace) || rcClip.FEmpty())
            continue;
        RECT rcCandidateWin = {rcClip.xpLeft, rcClip.ypTop, rcClip.xpRight, rcClip.ypBottom};
        if (!F4DMMRectContainsRect(&rcCandidateWin, prcPreview))
            continue;
        // The preview GOB itself is the hole, not the UI shell around it.
        if (F4DMMRectEqual(&rcCandidateWin, prcPreview))
            continue;
        *prcOwner = rcClip;
        return pgobWalk;
    }
    return pvNil;
}

/***************************************************************************
    Trim a modal GOK's fully transparent trailing hit-mask columns from its
    HWND-space rectangle.

    The scaled compositor copies a rectangular DWM thumbnail from Kauai's
    source window.  Masked GOK artwork can have transparent padding inside
    that rectangle; in the source window those pixels reveal the legacy black
    movie workspace.  Copying them verbatim over the native BRender movie
    produces a black bar even though the GOK itself never paints there.

    Use the GOK's own FPtIn() contract to find the rightmost column that is
    actually part of the rendered/hit mask.  This is intentionally limited to
    modal HBALs for now: tooltips deliberately opt out of hit testing, and the
    File/menu path is already proven correct.  If the GOK is rectangular or
    has no discoverable hit mask, leave the rectangle unchanged.
***************************************************************************/
static bool F4DMMTrimModalGobTransparentRightMargin(PGOB pgob, RC *prcHwnd)
{
    if (pgob == pvNil || prcHwnd == pvNil || prcHwnd->FEmpty() || !pgob->FIs(kclsGOK))
        return fFalse;

    RC rcLocal;
    pgob->GetRc(&rcLocal, cooLocal);
    if (rcLocal.FEmpty())
        return fFalse;

    // The same HBAL is repainted repeatedly while its buttons roll on/off.
    // Cache only the amount trimmed from its local right edge so hover paints
    // do not repeatedly walk the bitmap mask. Position changes remain safe
    // because the cached amount is re-applied to the current HWND rectangle.
    static PGOB spgobLast = pvNil;
    static int32_t sdxpLast = -1;
    static int32_t sdypLast = -1;
    static int32_t sdxpTrimLast = 0;
    static int32_t smethodLast = 0;

    const int32_t dxpLocal = rcLocal.Dxp();
    const int32_t dypLocal = rcLocal.Dyp();
    int32_t dxpTrim = 0;
    int32_t method = 0;

    if (spgobLast == pgob && sdxpLast == dxpLocal && sdypLast == dypLocal)
    {
        dxpTrim = sdxpTrimLast;
        method = smethodLast;
    }
    else
    {
        bool fFound = fFalse;
        int32_t xpRightOpaque = rcLocal.xpRight;
        for (int32_t xp = rcLocal.xpRight - 1; xp >= rcLocal.xpLeft && !fFound; --xp)
        {
            for (int32_t yp = rcLocal.ypTop; yp < rcLocal.ypBottom; ++yp)
            {
                if (pgob->FPtIn(xp, yp))
                {
                    xpRightOpaque = xp + 1;
                    fFound = fTrue;
                    break;
                }
            }
        }

        if (fFound && xpRightOpaque < rcLocal.xpRight)
        {
            dxpTrim = rcLocal.xpRight - xpRightOpaque;
            method = 1; // actual GOK hit/mask boundary
        }
        else
        {
            // Some rectangular GOK definitions intentionally report their
            // whole outer box as hittable even when the tiled artwork carries
            // an asymmetric trailing pad/shadow.  GORT's content rectangle is
            // the authoritative description of those border widths.  If the
            // right border contains a small *extra* amount beyond the matching
            // left border, remove only that asymmetry.  Keep the symmetric
            // frame itself intact.
            RC rcContent;
            ((PGOK)pgob)->GetRcContent(&rcContent);
            const int32_t dxpBorderLeft = rcContent.xpLeft - rcLocal.xpLeft;
            const int32_t dxpBorderRight = rcLocal.xpRight - rcContent.xpRight;
            const int32_t dxpExtraRight = dxpBorderRight - dxpBorderLeft;
            if (dxpBorderLeft >= 0 && dxpBorderRight >= 0 && dxpExtraRight > 0 && dxpExtraRight <= 16)
            {
                dxpTrim = dxpExtraRight;
                method = 2; // asymmetric GORP content border
            }
        }

        spgobLast = pgob;
        sdxpLast = dxpLocal;
        sdypLast = dypLocal;
        sdxpTrimLast = dxpTrim;
        smethodLast = method;
    }

    if (dxpTrim <= 0 || dxpTrim >= prcHwnd->Dxp())
        return fFalse;

    const int32_t xpRightOld = prcHwnd->xpRight;
    prcHwnd->xpRight -= dxpTrim;
    if (prcHwnd->xpRight <= prcHwnd->xpLeft)
    {
        prcHwnd->xpRight = xpRightOld;
        return fFalse;
    }

    MODERN_BR_LOG("scaled UI modal HBAL transparent-right trim gob=%p hid=%ld method=%ld trim=%ld source_right=%ld tight_right=%ld",
                  pgob, (long)pgob->Hid(), (long)method, (long)dxpTrim,
                  (long)xpRightOld, (long)prcHwnd->xpRight);
    return fTrue;
}

/***************************************************************************
    If Kauai is inside a modal help-balloon loop, return the actual HBAL
    rectangle rather than the full-size modal WOKS container.

    WOKS::FModalTopic deliberately creates a modal WOKS spanning the complete
    workspace, then places the visible HBAL dialog inside it.  Treating that
    WOKS as a compositor overlay reproduces Kauai's black legacy movie
    workspace around the dialog and unnecessarily covers the live BRender
    movie.  The HBAL is the real 2D UI surface that belongs above the movie.
***************************************************************************/
static PGOB Pgob4DMMUiScaleModalBalloonOwner(RC *prcPaintWorkspace, RC *prcOwner)
{
    if (prcPaintWorkspace == pvNil || prcOwner == pvNil || prcPaintWorkspace->FEmpty() || vpcex == pvNil)
        return pvNil;

    PGOB pgobModal = vpcex->PgobModal();
    if (pgobModal == pvNil)
        return pvNil;

    GTE gte;
    PGOB pgob;
    uint32_t grfgte;
    gte.Init(pgobModal, fgteNil);
    while (gte.FNextGob(&pgob, &grfgte, fgteNil))
    {
        if (!(grfgte & fgtePre) || pgob == pvNil || pgob == pgobModal || !pgob->FIs(kclsHBAL))
            continue;

        RC rcBalloon;
        pgob->GetRcVis(&rcBalloon, cooHwnd);
        if (rcBalloon.FEmpty())
            continue;

        RC rcIntersect = rcBalloon;
        if (!rcIntersect.FIntersect(prcPaintWorkspace) || rcIntersect.FEmpty())
            continue;

        RC rcBalloonComposited = rcBalloon;
        F4DMMTrimModalGobTransparentRightMargin(pgob, &rcBalloonComposited);
        if (rcBalloonComposited.FEmpty())
            rcBalloonComposited = rcBalloon;

        *prcOwner = rcBalloonComposited;
        return pgob;
    }

    return pvNil;
}

/***************************************************************************
    Return the live non-modal tooltip balloon when it owns part of a freshly
    painted workspace region.

    Kauai deliberately makes khidToolTip and all of its children invisible to
    normal PgobFromPt hit testing (GOB::FPtIn / FPtInBounds). That is correct
    for input, but it also means the generic source-paint owner resolver cannot
    discover the tooltip rectangle when the balloon crosses the movie view.
    Outside the movie view the ordinary scaled UI thumbnails still show it,
    which is exactly why v65 displayed only the non-overlapping portion of a
    tooltip and the native BRender layer cut the rest away.

    Resolve khidToolTip directly from the screen GOB tree and composite its
    complete visible HBAL rectangle whenever the completed Kauai paint touches
    the tooltip inside the workspace. This preserves Kauai's intentional
    mouse-invisibility while restoring the original draw order: movie first,
    tooltip second.
***************************************************************************/
static PGOB Pgob4DMMUiScaleToolTipOwner(RC *prcPaintWorkspace, RC *prcOwner)
{
    if (prcPaintWorkspace == pvNil || prcOwner == pvNil || prcPaintWorkspace->FEmpty())
        return pvNil;

    PGOB pgobToolTip = GOB::PgobFromHidScr(khidToolTip);
    if (pgobToolTip == pvNil || !pgobToolTip->FIs(kclsHBAL))
        return pvNil;

    RC rcToolTip;
    pgobToolTip->GetRcVis(&rcToolTip, cooHwnd);
    if (rcToolTip.FEmpty())
        return pvNil;

    RC rcIntersect = rcToolTip;
    if (!rcIntersect.FIntersect(prcPaintWorkspace) || rcIntersect.FEmpty())
        return pvNil;

    *prcOwner = rcToolTip;
    return pgobToolTip;
}

/***************************************************************************
    Resolve the topmost Kauai UI branch owning a freshly-painted workspace
    region. This follows the same source-HWND PgobFromPt contract as Kauai's
    real mouse dispatch, but starts from the completed source-draw bounds rather
    than trying to infer popup rectangles by enumerating the screen tree.
***************************************************************************/
static PGOB Pgob4DMMUiScaleSourcePaintOwner(RC *prcPaintWorkspace, RC *prcOwner)
{
    if (prcPaintWorkspace == pvNil || prcOwner == pvNil || prcPaintWorkspace->FEmpty() ||
        vapp.Pkwa() == pvNil || vwig.hwndApp == hNil || !IsWindow(vwig.hwndApp))
    {
        return pvNil;
    }

    PGOB pgobWorkspace = vapp.Pkwa()->PgobFromHid(kidWorkspace);
    PGOB pgobSourceRoot = GOB::PgobFromHwnd(vwig.hwndApp);
    if (pgobWorkspace == pvNil || pgobSourceRoot == pvNil)
        return pvNil;

    const int32_t dxp = prcPaintWorkspace->Dxp();
    const int32_t dyp = prcPaintWorkspace->Dyp();
    if (dxp <= 0 || dyp <= 0)
        return pvNil;

    // Sample inset quarter/centre points. Avoid the exact workspace edge:
    // the permanent 1-pixel boundary GOB seen in v54/v55 diagnostics lives at
    // (591,405)-(592,406) and is not UI that should cover the movie.
    const int32_t rgxp[3] = {
        prcPaintWorkspace->xpLeft + dxp / 4,
        prcPaintWorkspace->xpLeft + dxp / 2,
        prcPaintWorkspace->xpLeft + (dxp * 3) / 4};
    const int32_t rgyp[3] = {
        prcPaintWorkspace->ypTop + dyp / 4,
        prcPaintWorkspace->ypTop + dyp / 2,
        prcPaintWorkspace->ypTop + (dyp * 3) / 4};

    for (int32_t iy = 0; iy < 3; ++iy)
    {
        for (int32_t ix = 0; ix < 3; ++ix)
        {
            PT ptLocal;
            PGOB pgobHit = pgobSourceRoot->PgobFromPt(rgxp[ix], rgyp[iy], &ptLocal);
            if (pgobHit == pvNil || pgobHit == pgobSourceRoot || pgobHit == vapp.Pkwa() ||
                pgobHit == pgobWorkspace || F4DMMGobInMovieViewBranch(pgobHit))
            {
                continue;
            }

            // A button/thumbnail inside a popup is often the actual hit GOB.
            // Walk upward to the largest unrelated visible branch before the
            // permanent workspace/root containers so one DWM rectangle carries
            // the complete File menu / picker, not merely the clicked child.
            PGOB pgobOwner = pgobHit;
            RC rcOwner;
            pgobOwner->GetRcVis(&rcOwner, cooHwnd);
            RC rcOwnerIntersect = rcOwner;
            if (!rcOwnerIntersect.FIntersect(prcPaintWorkspace) || rcOwnerIntersect.FEmpty())
                rcOwnerIntersect = *prcPaintWorkspace;

            for (PGOB pgobPar = pgobHit->PgobPar(); pgobPar != pvNil; pgobPar = pgobPar->PgobPar())
            {
                if (pgobPar == pgobSourceRoot || pgobPar == vapp.Pkwa() || pgobPar == pgobWorkspace ||
                    F4DMMGobInMovieViewBranch(pgobPar))
                {
                    break;
                }

                RC rcPar;
                pgobPar->GetRcVis(&rcPar, cooHwnd);
                RC rcParIntersect = rcPar;
                if (!rcParIntersect.FIntersect(prcPaintWorkspace) || rcParIntersect.FEmpty())
                    continue;

                pgobOwner = pgobPar;
                rcOwnerIntersect = rcParIntersect;
            }

            *prcOwner = rcOwnerIntersect;
            return pgobOwner;
        }
    }

    return pvNil;
}

/***************************************************************************
    Synchronize one UI overlay from a completed Kauai source paint.

    This is deliberately event-driven and semantic: Kauai tells us exactly
    what source region it just rendered, and its own hit-test identifies whether a
    2D UI branch owns that freshly-painted region. No pixel colours are read,
    the native movie is never hidden, and no timer guesses whether UI exists.
***************************************************************************/
static bool F4DMMUpdateWorkspaceOverlaySlot(HANDLE *phthumb, bool *pfVisible, RECT *prcCurrent,
                                            const RECT *prcSource, PGOB pgobOwner, RC *prcPaintWorkspace,
                                            int32_t iSlot)
{
    if (phthumb == pvNil || pfVisible == pvNil || prcCurrent == pvNil || prcSource == pvNil ||
        pgobOwner == pvNil || prcPaintWorkspace == pvNil)
    {
        return fFalse;
    }

    if (*phthumb == hNil)
    {
        HRESULT hrRegister = vpfn4DMMDwmRegisterThumbnail(vhwnd4DMMUiScale, vwig.hwndApp, phthumb);
        if (FAILED(hrRegister) || *phthumb == hNil)
        {
            MODERN_BR_LOG("4x UI source-paint overlay register FAIL slot=%ld hr=0x%08lX",
                          (long)iSlot, (unsigned long)hrRegister);
            *phthumb = hNil;
            return fFalse;
        }
    }

    RECT rcDest;
    rcDest.left = Lw4DMMScaleSourceToPresentation(prcSource->left);
    rcDest.top = Lw4DMMScaleSourceToPresentation(prcSource->top);
    rcDest.right = Lw4DMMScaleSourceToPresentation(prcSource->right);
    rcDest.bottom = Lw4DMMScaleSourceToPresentation(prcSource->bottom);

    DWMTHUMBNAILPROPERTIES4DMM props;
    ClearPb(&props, SIZEOF(props));
    props.dwFlags = kdw4DMMDwmTnpRectDestination | kdw4DMMDwmTnpRectSource |
                    kdw4DMMDwmTnpOpacity | kdw4DMMDwmTnpVisible |
                    kdw4DMMDwmTnpSourceClientAreaOnly;
    props.rcSource = *prcSource;
    props.rcDestination = rcDest;
    props.opacity = 255;
    props.fVisible = TRUE;
    props.fSourceClientAreaOnly = TRUE;
    HRESULT hr = vpfn4DMMDwmUpdateThumbnailProperties(*phthumb, &props);
    if (FAILED(hr))
    {
        MODERN_BR_LOG("4x UI source-paint overlay update FAIL slot=%ld hr=0x%08lX",
                      (long)iSlot, (unsigned long)hr);
        Clear4DMMUiScaleWorkspaceOverlaySlot(phthumb, pfVisible, prcCurrent);
        return fFalse;
    }

    if (!*pfVisible || !F4DMMRectEqual(prcSource, prcCurrent))
    {
        MODERN_BR_LOG("4x UI source-paint overlay SHOW slot=%ld owner=%p hid=%ld cls=0x%08lX paint=(%ld,%ld)-(%ld,%ld) source=(%ld,%ld)-(%ld,%ld) dest=(%ld,%ld)-(%ld,%ld)",
                      (long)iSlot, pgobOwner, (long)pgobOwner->Hid(), (unsigned long)pgobOwner->Cls(),
                      (long)prcPaintWorkspace->xpLeft, (long)prcPaintWorkspace->ypTop,
                      (long)prcPaintWorkspace->xpRight, (long)prcPaintWorkspace->ypBottom,
                      (long)prcSource->left, (long)prcSource->top,
                      (long)prcSource->right, (long)prcSource->bottom,
                      (long)rcDest.left, (long)rcDest.top, (long)rcDest.right, (long)rcDest.bottom);
    }

    *prcCurrent = *prcSource;
    *pfVisible = fTrue;
    return fTrue;
}

static void Sync4DMMUiScaleWorkspaceOverlayFromSourcePaint(RC *prcPaintSource)
{
    if (!F4DMMUiScaleActive() || vf4DMMUiScaleNativeDialogSuspended ||
        prcPaintSource == pvNil || prcPaintSource->FEmpty() || vapp.Pkwa() == pvNil ||
        vwig.hwndApp == hNil || !IsWindow(vwig.hwndApp) ||
        vpfn4DMMDwmRegisterThumbnail == pvNil || vpfn4DMMDwmUpdateThumbnailProperties == pvNil)
    {
        return;
    }

    PGOB pgobWorkspace = vapp.Pkwa()->PgobFromHid(kidWorkspace);
    if (pgobWorkspace == pvNil)
        return;

    RC rcWorkspace;
    pgobWorkspace->GetRcVis(&rcWorkspace, cooHwnd);
    RC rcPaintWorkspace = *prcPaintSource;
    if (rcWorkspace.FEmpty() || !rcPaintWorkspace.FIntersect(&rcWorkspace) || rcPaintWorkspace.FEmpty())
        return;

    RC rcOwner;
    rcOwner.Set(0, 0, 0, 0);

    // v63: modal help topics use a full-workspace WOKS only as a command/
    // suspension boundary.  Prefer the real HBAL visual rectangle so the
    // continuously-live BRender movie remains visible around Exit and other
    // small modal panels.  Non-modal paints and modal types without an HBAL
    // keep the established v58-v62 owner path unchanged.
    PGOB pgobOwner = Pgob4DMMUiScaleModalBalloonOwner(&rcPaintWorkspace, &rcOwner);
    const bool fOwnerModalBalloon = pgobOwner != pvNil;
    bool fOwnerToolTip = fFalse;
    if (pgobOwner == pvNil)
    {
        pgobOwner = Pgob4DMMUiScaleToolTipOwner(&rcPaintWorkspace, &rcOwner);
        fOwnerToolTip = pgobOwner != pvNil;
    }

    // v72: tooltip HBALs reuse one explicitly-tracked compositor slot. Kauai
    // moves the single khidToolTip GOB from item to item; the old multi-surface
    // logic treated each disjoint position as an independent permanent overlay.
    // Once Kauai repainted the old tooltip area with the legacy black movie
    // workspace, those stale DWM thumbnails became the black squares seen over
    // the live BRender viewport. Retire the previous tooltip slot as soon as
    // the tooltip moves, before resolving any unrelated owner for this paint.
    if (vi4DMMUiScaleToolTipOverlaySlot >= 0)
    {
        HANDLE *phthumbPrevious = pvNil;
        bool *pfVisiblePrevious = pvNil;
        RECT *prcPrevious = pvNil;
        if (vi4DMMUiScaleToolTipOverlaySlot == 0)
        {
            phthumbPrevious = &vhthumb4DMMUiScaleWorkspaceOverlay;
            pfVisiblePrevious = &vf4DMMUiScaleWorkspaceOverlayVisible;
            prcPrevious = &vrc4DMMUiScaleWorkspaceOverlaySource;
        }
        else
        {
            const int32_t iExtra = vi4DMMUiScaleToolTipOverlaySlot - 1;
            if (iExtra >= 0 && iExtra < kc4DMMUiScaleWorkspaceOverlayExtra)
            {
                phthumbPrevious = &vrghthumb4DMMUiScaleWorkspaceOverlayExtra[iExtra];
                pfVisiblePrevious = &vrgf4DMMUiScaleWorkspaceOverlayExtraVisible[iExtra];
                prcPrevious = &vrgrc4DMMUiScaleWorkspaceOverlayExtraSource[iExtra];
            }
        }

        bool fRetirePrevious = fFalse;
        if (phthumbPrevious != pvNil && pfVisiblePrevious != pvNil && prcPrevious != pvNil &&
            *pfVisiblePrevious)
        {
            if (fOwnerToolTip)
            {
                RECT rcToolTipNow = {rcOwner.xpLeft, rcOwner.ypTop, rcOwner.xpRight, rcOwner.ypBottom};
                fRetirePrevious = !F4DMMRectEqual(prcPrevious, &rcToolTipNow);
            }
            else
            {
                // The old tooltip has just been erased. Retire its DWM surface
                // even when the newly-exposed source pixels belong to another
                // non-movie GOB, so the black legacy workspace cannot remain
                // cached above the live native viewport.
                fRetirePrevious = F4DMMRectIntersectsRc(prcPrevious, &rcPaintWorkspace);
            }
        }

        if (fRetirePrevious)
        {
            MODERN_BR_LOG("4x UI tooltip overlay RETIRE slot=%ld reason=%s source=(%ld,%ld)-(%ld,%ld)",
                          (long)vi4DMMUiScaleToolTipOverlaySlot,
                          fOwnerToolTip ? "move" : "erase",
                          (long)prcPrevious->left, (long)prcPrevious->top,
                          (long)prcPrevious->right, (long)prcPrevious->bottom);
            Clear4DMMUiScaleWorkspaceOverlaySlot(phthumbPrevious, pfVisiblePrevious, prcPrevious);
        }
    }

    if (pgobOwner == pvNil)
        pgobOwner = Pgob4DMMUiScaleSourcePaintOwner(&rcPaintWorkspace, &rcOwner);

    // v80: During ordinary movie playback, legacy 3DMM paints a full-workspace
    // GOK over the movie view while the real BRender frames continue rendering.
    // In the scaled compositor that helper GOK must remain a command/playback
    // surface, not become a DWM UI overlay: its source pixels are the intentionally
    // black legacy movie buffer in native-resolution mode, so promoting the GOK
    // hides every valid BRender playback frame until Stop clears the overlay.
    //
    // Suppress only a GOK that covers the complete workspace while the movie is
    // actually playing. File menus, tooltips, modal HBALs, Add Actor and all
    // non-playback overlays keep the established v58-v78 ownership path.
    if (pgobOwner != pvNil && pgobOwner->FIs(kclsGOK))
    {
        PSTDIO pstdioPlayback = vapp.Pstdio();
        PMVIE pmviePlayback = pstdioPlayback != pvNil ? pstdioPlayback->Pmvie() : pvNil;
        if (pmviePlayback != pvNil && pmviePlayback->FPlaying() &&
            rcOwner.xpLeft <= rcWorkspace.xpLeft && rcOwner.ypTop <= rcWorkspace.ypTop &&
            rcOwner.xpRight >= rcWorkspace.xpRight && rcOwner.ypBottom >= rcWorkspace.ypBottom)
        {
            static PGOB spgobPlaybackShellLast = pvNil;
            if (spgobPlaybackShellLast != pgobOwner)
            {
                MODERN_BR_LOG("scaled UI v80 playback shell suppressed owner=%p hid=%ld cls=0x%08lX owner=(%ld,%ld)-(%ld,%ld) workspace=(%ld,%ld)-(%ld,%ld)",
                              pgobOwner, (long)pgobOwner->Hid(), (unsigned long)pgobOwner->Cls(),
                              (long)rcOwner.xpLeft, (long)rcOwner.ypTop,
                              (long)rcOwner.xpRight, (long)rcOwner.ypBottom,
                              (long)rcWorkspace.xpLeft, (long)rcWorkspace.ypTop,
                              (long)rcWorkspace.xpRight, (long)rcWorkspace.ypBottom);
                spgobPlaybackShellLast = pgobOwner;
            }
            pgobOwner = pvNil;
        }
    }

    const bool fOwnerApeTransient = fOwnerToolTip ||
                                    F4DMMGobInApeTransientUiBranch(pgobOwner);

#if defined(BRENDER_MODERN_14)
    RECT rcApePreview;
    const bool fApeActive = FGet4DMMUiScaleApePreviewSourceRect(&rcApePreview);
    if (!fApeActive)
    {
        if (vf4DMMUiScaleApeOverlayVisible)
            Clear4DMMUiScaleApeOverlayRegions();
    }
    else
    {
        // APE itself now publishes the complete easel ancestor rectangle. This
        // avoids asking the generic hit-test/paint traversal to rediscover the
        // owner, which failed specifically for the 3D Word easel and left all
        // of its buttons behind the native movie layer.
        RECT rcApeOwnerDirect;
        bool fApeOverlayReady = fFalse;
        if (FGet4DMMUiScaleApeOwnerSourceRect(&rcApeOwnerDirect) &&
            F4DMMRectContainsRect(&rcApeOwnerDirect, &rcApePreview))
        {
            fApeOverlayReady = F4DMMUpdateUiScaleApeOverlay(&rcApeOwnerDirect, &rcApePreview);
            if (fApeOverlayReady && !fOwnerApeTransient)
                return;
        }

        if (!fApeOverlayReady && !fOwnerApeTransient && pgobOwner != pvNil)
        {
            RC rcApeOwner;
            rcApeOwner.Set(0, 0, 0, 0);
            PGOB pgobApeOwner = Pgob4DMMUiScaleApeOwnerFromSeed(
                pgobOwner, &rcApePreview, &rcApeOwner);
            if (pgobApeOwner != pvNil)
            {
                RECT rcApeOwnerWin = {rcApeOwner.xpLeft, rcApeOwner.ypTop,
                                      rcApeOwner.xpRight, rcApeOwner.ypBottom};
                if (F4DMMUpdateUiScaleApeOverlay(&rcApeOwnerWin, &rcApePreview))
                    return;
            }
        }
    }
#endif

    if (pgobOwner == pvNil)
    {
        int32_t cCleared = 0;
        if (vf4DMMUiScaleWorkspaceOverlayVisible &&
            F4DMMRectIntersectsRc(&vrc4DMMUiScaleWorkspaceOverlaySource, &rcPaintWorkspace))
        {
            Clear4DMMUiScaleWorkspaceOverlaySlot(&vhthumb4DMMUiScaleWorkspaceOverlay,
                                                 &vf4DMMUiScaleWorkspaceOverlayVisible,
                                                 &vrc4DMMUiScaleWorkspaceOverlaySource);
            ++cCleared;
        }
        for (int32_t i = 0; i < kc4DMMUiScaleWorkspaceOverlayExtra; ++i)
        {
            if (vrgf4DMMUiScaleWorkspaceOverlayExtraVisible[i] &&
                F4DMMRectIntersectsRc(&vrgrc4DMMUiScaleWorkspaceOverlayExtraSource[i], &rcPaintWorkspace))
            {
                Clear4DMMUiScaleWorkspaceOverlaySlot(&vrghthumb4DMMUiScaleWorkspaceOverlayExtra[i],
                                                     &vrgf4DMMUiScaleWorkspaceOverlayExtraVisible[i],
                                                     &vrgrc4DMMUiScaleWorkspaceOverlayExtraSource[i]);
                ++cCleared;
            }
        }

        static int32_t xpMissLeftLast = -1;
        static int32_t ypMissTopLast = -1;
        static int32_t xpMissRightLast = -1;
        static int32_t ypMissBottomLast = -1;
        if (xpMissLeftLast != rcPaintWorkspace.xpLeft || ypMissTopLast != rcPaintWorkspace.ypTop ||
            xpMissRightLast != rcPaintWorkspace.xpRight || ypMissBottomLast != rcPaintWorkspace.ypBottom ||
            cCleared > 0)
        {
            PGOB pgobSourceRoot = GOB::PgobFromHwnd(vwig.hwndApp);
            PGOB pgobHit = pvNil;
            PT ptLocal;
            if (pgobSourceRoot != pvNil)
                pgobHit = pgobSourceRoot->PgobFromPt((rcPaintWorkspace.xpLeft + rcPaintWorkspace.xpRight) / 2,
                                                     (rcPaintWorkspace.ypTop + rcPaintWorkspace.ypBottom) / 2,
                                                     &ptLocal);
            MODERN_BR_LOG("4x UI source-paint overlay MISS paint=(%ld,%ld)-(%ld,%ld) centre_hit=%p hid=%ld cls=0x%08lX movie_branch=%d cleared=%ld",
                          (long)rcPaintWorkspace.xpLeft, (long)rcPaintWorkspace.ypTop,
                          (long)rcPaintWorkspace.xpRight, (long)rcPaintWorkspace.ypBottom,
                          pgobHit, pgobHit != pvNil ? (long)pgobHit->Hid() : -1L,
                          pgobHit != pvNil ? (unsigned long)pgobHit->Cls() : 0UL,
                          pgobHit != pvNil ? (int)F4DMMGobInMovieViewBranch(pgobHit) : 0,
                          (long)cCleared);
            xpMissLeftLast = rcPaintWorkspace.xpLeft;
            ypMissTopLast = rcPaintWorkspace.ypTop;
            xpMissRightLast = rcPaintWorkspace.xpRight;
            ypMissBottomLast = rcPaintWorkspace.ypBottom;
        }
        return;
    }

    HANDLE *phthumbTarget = &vhthumb4DMMUiScaleWorkspaceOverlay;
    bool *pfVisibleTarget = &vf4DMMUiScaleWorkspaceOverlayVisible;
    RECT *prcCurrentTarget = &vrc4DMMUiScaleWorkspaceOverlaySource;
    int32_t iSlotTarget = 0;
    bool fTargetFound = fFalse;

    if (vf4DMMUiScaleWorkspaceOverlayVisible &&
        F4DMMRectIntersectsRc(&vrc4DMMUiScaleWorkspaceOverlaySource, &rcOwner))
    {
        fTargetFound = fTrue;
    }
    else
    {
        for (int32_t i = 0; i < kc4DMMUiScaleWorkspaceOverlayExtra; ++i)
        {
            if (vrgf4DMMUiScaleWorkspaceOverlayExtraVisible[i] &&
                F4DMMRectIntersectsRc(&vrgrc4DMMUiScaleWorkspaceOverlayExtraSource[i], &rcOwner))
            {
                phthumbTarget = &vrghthumb4DMMUiScaleWorkspaceOverlayExtra[i];
                pfVisibleTarget = &vrgf4DMMUiScaleWorkspaceOverlayExtraVisible[i];
                prcCurrentTarget = &vrgrc4DMMUiScaleWorkspaceOverlayExtraSource[i];
                iSlotTarget = i + 1;
                fTargetFound = fTrue;
                break;
            }
        }
    }

    if (!fTargetFound)
    {
        if (!vf4DMMUiScaleWorkspaceOverlayVisible)
        {
            fTargetFound = fTrue;
        }
        else
        {
            for (int32_t i = 0; i < kc4DMMUiScaleWorkspaceOverlayExtra; ++i)
            {
                if (!vrgf4DMMUiScaleWorkspaceOverlayExtraVisible[i])
                {
                    phthumbTarget = &vrghthumb4DMMUiScaleWorkspaceOverlayExtra[i];
                    pfVisibleTarget = &vrgf4DMMUiScaleWorkspaceOverlayExtraVisible[i];
                    prcCurrentTarget = &vrgrc4DMMUiScaleWorkspaceOverlayExtraSource[i];
                    iSlotTarget = i + 1;
                    fTargetFound = fTrue;
                    break;
                }
            }
        }
    }

    if (!fTargetFound)
    {
        MODERN_BR_LOG("4x UI source-paint overlay DROP no free disjoint slot owner=%p hid=%ld paint=(%ld,%ld)-(%ld,%ld)",
                      pgobOwner, (long)pgobOwner->Hid(),
                      (long)rcPaintWorkspace.xpLeft, (long)rcPaintWorkspace.ypTop,
                      (long)rcPaintWorkspace.xpRight, (long)rcPaintWorkspace.ypBottom);
        return;
    }

    if (*pfVisibleTarget)
    {
        RC rcCurrent;
        rcCurrent.Set(prcCurrentTarget->left, prcCurrentTarget->top,
                      prcCurrentTarget->right, prcCurrentTarget->bottom);
        RC rcOverlap = rcCurrent;
        if (rcOverlap.FIntersect(&rcOwner) && !rcOverlap.FEmpty())
        {
            rcOwner.xpLeft = LwMin(rcOwner.xpLeft, rcCurrent.xpLeft);
            rcOwner.ypTop = LwMin(rcOwner.ypTop, rcCurrent.ypTop);
            rcOwner.xpRight = LwMax(rcOwner.xpRight, rcCurrent.xpRight);
            rcOwner.ypBottom = LwMax(rcOwner.ypBottom, rcCurrent.ypBottom);
        }
    }

    RECT rcClient;
    if (!GetClientRect(vwig.hwndApp, &rcClient))
        return;

    RECT rcSource;
    rcSource.left = LwMax(rcOwner.xpLeft, rcClient.left);
    rcSource.top = LwMax(rcOwner.ypTop, rcClient.top);
    rcSource.right = LwMin(rcOwner.xpRight, rcClient.right);
    rcSource.bottom = LwMin(rcOwner.ypBottom, rcClient.bottom);
    if (rcSource.right <= rcSource.left || rcSource.bottom <= rcSource.top)
        return;

#if defined(BRENDER_MODERN_14)
    // Apart from real transient balloons, a generic rectangular source
    // thumbnail is never allowed to cover the active native APE. If easel
    // ownership was not recognized above, dropping this one rectangle is
    // safer than reintroducing the legacy low-resolution 3D pixels.
    RECT rcApeGuard;
    if (FGet4DMMUiScaleApePreviewSourceRect(&rcApeGuard) &&
        F4DMMRectIntersectsRect(&rcApeGuard, &rcSource) && !fOwnerApeTransient)
    {
        MODERN_BR_LOG("4x APE generic overlay DROP source=(%ld,%ld)-(%ld,%ld) hole=(%ld,%ld)-(%ld,%ld)",
                      (long)rcSource.left, (long)rcSource.top,
                      (long)rcSource.right, (long)rcSource.bottom,
                      (long)rcApeGuard.left, (long)rcApeGuard.top,
                      (long)rcApeGuard.right, (long)rcApeGuard.bottom);
        return;
    }
    if (fOwnerApeTransient && FGet4DMMUiScaleApePreviewSourceRect(&rcApeGuard) &&
        F4DMMRectIntersectsRect(&rcApeGuard, &rcSource))
    {
        PSTDIO pstdioTransient = vapp.Pstdio();
        MVIE::MultiLog(pstdioTransient != pvNil ? pstdioTransient->Pmvie() : pvNil,
            "ape_transient_overlay owner_cls=0x%08lX hid=%ld tooltip=%d source=(%ld,%ld)-(%ld,%ld)",
            pgobOwner != pvNil ? (unsigned long)pgobOwner->Cls() : 0UL,
            pgobOwner != pvNil ? (long)pgobOwner->Hid() : -1L, (int)fOwnerToolTip,
            (long)rcSource.left, (long)rcSource.top,
            (long)rcSource.right, (long)rcSource.bottom);
    }
#endif

    if (F4DMMUpdateWorkspaceOverlaySlot(phthumbTarget, pfVisibleTarget, prcCurrentTarget,
                                         &rcSource, pgobOwner, &rcPaintWorkspace, iSlotTarget) &&
        fOwnerToolTip)
    {
        vi4DMMUiScaleToolTipOverlaySlot = iSlotTarget;
    }
}

/***************************************************************************
    Compose Kauai's 2D workspace UI over the native BRender movie layer.

    The original 3DMM ordering is simple: render the 3D view, then let menus,
    browsers, easels and dialogs paint on top. The retired 4x workaround did
    the opposite architecturally: it detected UI, removed the movie layer and
    exposed a full DWM copy of the 640x480 source. That caused black flashes,
    stale cover state and drag latency.

    v50 keeps the native movie continuously alive. A real visible Kauai GOB
    crossing the workspace contributes only its intersecting source rectangle
    as an additional DWM thumbnail above the presentation's native-frame paint.
    No source-pixel colour heuristic, PrintWindow polling, cover hold timer or
    viewport hide/show state participates in this path.
***************************************************************************/
static void Sync4DMMUiScaleWorkspaceOverlay(void)
{
    if (!F4DMMUiScaleActive() || vf4DMMUiScaleNativeDialogSuspended ||
        vwig.hwndApp == hNil || !IsWindow(vwig.hwndApp) ||
        vpfn4DMMDwmRegisterThumbnail == pvNil || vpfn4DMMDwmUpdateThumbnailProperties == pvNil)
    {
        return;
    }

    PGOB pgobOverlay = Pgob4DMMUiScaleWorkspaceOverlay();
    RC rcSourceOverlay;
    rcSourceOverlay.Set(0, 0, 0, 0);
    if (pgobOverlay != pvNil)
    {
        PGOB pgobWorkspace = vapp.Pkwa() != pvNil ? vapp.Pkwa()->PgobFromHid(kidWorkspace) : pvNil;
        if (pgobWorkspace == pvNil)
            pgobOverlay = pvNil;
        else
        {
            RC rcWorkspace;
            pgobWorkspace->GetRcVis(&rcWorkspace, cooHwnd);
            pgobOverlay->GetRcVis(&rcSourceOverlay, cooHwnd);
            if (rcWorkspace.FEmpty() || rcSourceOverlay.FEmpty() ||
                !rcSourceOverlay.FIntersect(&rcWorkspace) || rcSourceOverlay.FEmpty())
            {
                pgobOverlay = pvNil;
            }
        }
    }

#if defined(BRENDER_MODERN_14)
    RECT rcApePreview;
    if (!FGet4DMMUiScaleApePreviewSourceRect(&rcApePreview))
    {
        if (vf4DMMUiScaleApeOverlayVisible)
            Clear4DMMUiScaleApeOverlayRegions();
    }
    else
    {
        // Prefer the authoritative owner rectangle published by APE itself.
        // This makes 3D Word/Costume/Action use one compositor contract even
        // when Kauai's generic visible-GOB traversal cannot identify the easel.
        RECT rcApeOwnerDirect;
        if (FGet4DMMUiScaleApeOwnerSourceRect(&rcApeOwnerDirect) &&
            F4DMMRectContainsRect(&rcApeOwnerDirect, &rcApePreview) &&
            F4DMMUpdateUiScaleApeOverlay(&rcApeOwnerDirect, &rcApePreview))
            return;

        if (pgobOverlay != pvNil)
        {
            RC rcApeOwner;
            rcApeOwner.Set(0, 0, 0, 0);
            PGOB pgobApeOwner = Pgob4DMMUiScaleApeOwnerFromSeed(
                pgobOverlay, &rcApePreview, &rcApeOwner);
            if (pgobApeOwner != pvNil)
            {
                RECT rcApeOwnerWin = {rcApeOwner.xpLeft, rcApeOwner.ypTop,
                                      rcApeOwner.xpRight, rcApeOwner.ypBottom};
                if (F4DMMUpdateUiScaleApeOverlay(&rcApeOwnerWin, &rcApePreview))
                    return;
            }
        }
    }
#endif

    if (pgobOverlay == pvNil)
    {
        if (vhthumb4DMMUiScaleWorkspaceOverlay != hNil && vf4DMMUiScaleWorkspaceOverlayVisible)
        {
            DWMTHUMBNAILPROPERTIES4DMM props;
            ClearPb(&props, SIZEOF(props));
            props.dwFlags = kdw4DMMDwmTnpVisible;
            props.fVisible = FALSE;
            if (SUCCEEDED(vpfn4DMMDwmUpdateThumbnailProperties(vhthumb4DMMUiScaleWorkspaceOverlay, &props)))
                MODERN_BR_LOG("4x UI workspace overlay HIDE; native movie remains live");
            else
                Clear4DMMUiScaleWorkspaceOverlayThumbnail();
        }
        vf4DMMUiScaleWorkspaceOverlayVisible = fFalse;
        return;
    }

    RECT rcSource;
    rcSource.left = rcSourceOverlay.xpLeft;
    rcSource.top = rcSourceOverlay.ypTop;
    rcSource.right = rcSourceOverlay.xpRight;
    rcSource.bottom = rcSourceOverlay.ypBottom;

    RECT rcClient;
    if (!GetClientRect(vwig.hwndApp, &rcClient))
        return;
    rcSource.left = LwMax(rcSource.left, rcClient.left);
    rcSource.top = LwMax(rcSource.top, rcClient.top);
    rcSource.right = LwMin(rcSource.right, rcClient.right);
    rcSource.bottom = LwMin(rcSource.bottom, rcClient.bottom);
    if (rcSource.right <= rcSource.left || rcSource.bottom <= rcSource.top)
        return;

    RECT rcDest;
    rcDest.left = Lw4DMMScaleSourceToPresentation(rcSource.left);
    rcDest.top = Lw4DMMScaleSourceToPresentation(rcSource.top);
    rcDest.right = Lw4DMMScaleSourceToPresentation(rcSource.right);
    rcDest.bottom = Lw4DMMScaleSourceToPresentation(rcSource.bottom);

    // A DWM thumbnail is a live compositor surface. Once its source/dest
    // rectangle is correct, there is nothing to repaint or recapture here;
    // Kauai's source HWND updates flow through DWM automatically.
    if (vhthumb4DMMUiScaleWorkspaceOverlay != hNil && vf4DMMUiScaleWorkspaceOverlayVisible &&
        F4DMMRectEqual(&rcSource, &vrc4DMMUiScaleWorkspaceOverlaySource))
    {
        return;
    }

    if (vhthumb4DMMUiScaleWorkspaceOverlay == hNil)
    {
        HRESULT hrRegister = vpfn4DMMDwmRegisterThumbnail(vhwnd4DMMUiScale, vwig.hwndApp,
                                                           &vhthumb4DMMUiScaleWorkspaceOverlay);
        if (FAILED(hrRegister) || vhthumb4DMMUiScaleWorkspaceOverlay == hNil)
        {
            MODERN_BR_LOG("4x UI workspace overlay register FAIL hr=0x%08lX",
                          (unsigned long)hrRegister);
            vhthumb4DMMUiScaleWorkspaceOverlay = hNil;
            return;
        }
    }

    DWMTHUMBNAILPROPERTIES4DMM props;
    ClearPb(&props, SIZEOF(props));
    props.dwFlags = kdw4DMMDwmTnpRectDestination | kdw4DMMDwmTnpRectSource |
                    kdw4DMMDwmTnpOpacity | kdw4DMMDwmTnpVisible |
                    kdw4DMMDwmTnpSourceClientAreaOnly;
    props.rcSource = rcSource;
    props.rcDestination = rcDest;
    props.opacity = 255;
    props.fVisible = TRUE;
    props.fSourceClientAreaOnly = TRUE;
    HRESULT hr = vpfn4DMMDwmUpdateThumbnailProperties(vhthumb4DMMUiScaleWorkspaceOverlay, &props);
    if (FAILED(hr))
    {
        MODERN_BR_LOG("4x UI workspace overlay update FAIL hr=0x%08lX", (unsigned long)hr);
        Clear4DMMUiScaleWorkspaceOverlayThumbnail();
        return;
    }

    if (!vf4DMMUiScaleWorkspaceOverlayVisible ||
        !F4DMMRectEqual(&rcSource, &vrc4DMMUiScaleWorkspaceOverlaySource))
    {
        MODERN_BR_LOG("4x UI workspace overlay SHOW gob=%p hid=%ld cls=0x%08lX source=(%ld,%ld)-(%ld,%ld) dest=(%ld,%ld)-(%ld,%ld); native movie stays live",
                      pgobOverlay, (long)pgobOverlay->Hid(), (unsigned long)pgobOverlay->Cls(),
                      (long)rcSource.left, (long)rcSource.top, (long)rcSource.right, (long)rcSource.bottom,
                      (long)rcDest.left, (long)rcDest.top, (long)rcDest.right, (long)rcDest.bottom);
    }
    vrc4DMMUiScaleWorkspaceOverlaySource = rcSource;
    vf4DMMUiScaleWorkspaceOverlayVisible = fTrue;
}

void Suspend4DMM4xPresentationForNativeDialog(void)
{
    if (!F4DMMUiScaleActive() || vf4DMMUiScaleNativeDialogSuspended)
        return;

    vf4DMMUiScaleNativeDialogSuspended = fTrue;
    HWND hwndSource = vwig.hwndApp;
    if (hwndSource != hNil && IsWindow(hwndSource))
    {
        // This property is intentionally read by Kauai's appbwin.cpp. It
        // disables every 4x source-window coordinate/Z-order interception
        // before we attempt to promote the original HWND. Without it,
        // WM_WINDOWPOSCHANGING immediately rewrites HWND_TOP to HWND_BOTTOM
        // and WM_ACTIVATE raises the hidden 4x presentation again.
        SetPropA(hwndSource, ksz4DMMUiScaleSuspendedProp, (HANDLE)1);
    }

    MODERN_BR_LOG("4x UI native-dialog SUSPEND v52 source=%p presentation=%p viewport=%p restore=(%ld,%ld)-(%ld,%ld)",
                  hwndSource, vhwnd4DMMUiScale, vhwnd4DMMUiScaleViewport,
                  (long)vrc4DMMUiScaleSourceRestore.left, (long)vrc4DMMUiScaleSourceRestore.top,
                  (long)vrc4DMMUiScaleSourceRestore.right, (long)vrc4DMMUiScaleSourceRestore.bottom);

    // The portfolio is a customized Win32 GetOpenFileName/GetSaveFileName
    // dialog. Give it the exact normal 3DMM ownership graph: hide both 4x
    // presentation surfaces and put the original 640x480 Kauai HWND back at
    // its saved desktop location as the only interactive application window.
    Clear4DMMUiScaleWorkspaceOverlayThumbnail();
    Clear4DMMUiScaleApeOverlayRegions();
    if (vhwnd4DMMUiScaleViewport != hNil && IsWindow(vhwnd4DMMUiScaleViewport))
        ShowWindow(vhwnd4DMMUiScaleViewport, SW_HIDE);
    if (vhwnd4DMMUiScale != hNil && IsWindow(vhwnd4DMMUiScale))
        ShowWindow(vhwnd4DMMUiScale, SW_HIDE);

    if (hwndSource != hNil && IsWindow(hwndSource))
    {
        SetWindowPos(hwndSource, HWND_TOP,
                     vrc4DMMUiScaleSourceRestore.left, vrc4DMMUiScaleSourceRestore.top,
                     0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
        ShowWindow(hwndSource, SW_RESTORE);
        EnableWindow(hwndSource, TRUE);
        SetForegroundWindow(hwndSource);
        SetActiveWindow(hwndSource);
        SetFocus(hwndSource);
        UpdateWindow(hwndSource);
        vf4DMMUiScaleSourceParked = fFalse;
    }
}

void Resume4DMM4xPresentationAfterNativeDialog(void)
{
    if (!vfViewportResolution4x || !vf4DMMUiScaleNativeDialogSuspended)
        return;

    HWND hwndSource = vwig.hwndApp;
    if (!F4DMMUiScaleActive())
    {
        if (hwndSource != hNil && IsWindow(hwndSource))
            RemovePropA(hwndSource, ksz4DMMUiScaleSuspendedProp);
        vf4DMMUiScaleNativeDialogSuspended = fFalse;
        return;
    }

    // Restore the normal layered 4x compositor immediately. The native movie
    // remains the presentation's base layer; any Kauai UI that subsequently
    // crosses the workspace is supplied by the GOB/DWM overlay layer. There
    // is deliberately no post-dialog full-window cover/probe period.
    Clear4DMMUiScaleWorkspaceOverlayThumbnail();
    Clear4DMMUiScaleApeOverlayRegions();

    ShowWindow(vhwnd4DMMUiScale, SW_SHOW);
    UpdateWindow(vhwnd4DMMUiScale);
    F4DMMParkUiScaleSourceWindow();
    if (vhwnd4DMMUiScaleViewport != hNil && IsWindow(vhwnd4DMMUiScaleViewport))
    {
        if (F4DMMModernEmbeddedViewportHost())
            ShowWindow(vhwnd4DMMUiScaleViewport, SW_HIDE);
        else
        {
            ShowWindow(vhwnd4DMMUiScaleViewport, SW_SHOWNOACTIVATE);
            Park4DMMUiScaleViewportSource();
        }
    }
    SetWindowPos(vhwnd4DMMUiScale, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE);

    if (hwndSource != hNil && IsWindow(hwndSource))
        RemovePropA(hwndSource, ksz4DMMUiScaleSuspendedProp);
    vf4DMMUiScaleNativeDialogSuspended = fFalse;

    Sync4DMMUiScaleWorkspaceOverlay();
    RECT rcViewport;
    if (FGet4DMM4xViewportRect(&rcViewport))
        InvalidateRect(vhwnd4DMMUiScale, &rcViewport, fFalse);
    UpdateWindow(vhwnd4DMMUiScale);
    SetForegroundWindow(vhwnd4DMMUiScale);
    SetFocus(vhwnd4DMMUiScale);
    MODERN_BR_LOG("4x UI native-dialog RESUME v59 multi-surface compositor source=%p presentation=%p viewport=%p",
                  hwndSource, vhwnd4DMMUiScale, vhwnd4DMMUiScaleViewport);
}

static void Close4DMMUiScaleWindow(void)
{
    if (vhwnd4DMMUiScale != hNil && IsWindow(vhwnd4DMMUiScale))
        KillTimer(vhwnd4DMMUiScale, kid4DMMUiScaleOverlayTimer);

    HWND hwndSource = vwig.hwndApp;
    if (vf4DMMUiScaleSourceParked && hwndSource != hNil && IsWindow(hwndSource))
    {
        SetWindowPos(hwndSource, hNil, vrc4DMMUiScaleSourceRestore.left, vrc4DMMUiScaleSourceRestore.top,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        MODERN_BR_LOG("4x UI source restored pos=(%ld,%ld)",
                      (long)vrc4DMMUiScaleSourceRestore.left, (long)vrc4DMMUiScaleSourceRestore.top);
    }
    vf4DMMUiScaleSourceParked = fFalse;
    vf4DMMUiScaleNativeDialogSuspended = fFalse;
    Clear4DMMUiScaleWorkspaceOverlayThumbnail();
    Clear4DMMUiScaleApePopupOverlay();
    Clear4DMMUiScaleApeOverlayRegions();
    Clear4DMMUiScaleViewportThumbnail();
    Clear4DMMUiScaleRegionThumbnails();
    if (vhthumb4DMMUiScale != hNil && vpfn4DMMDwmUnregisterThumbnail != pvNil)
        vpfn4DMMDwmUnregisterThumbnail(vhthumb4DMMUiScale);
    vhthumb4DMMUiScale = hNil;

    if (vwig.hwndApp != hNil && IsWindow(vwig.hwndApp))
    {
        if (vf4DMMUiScaleSourceAltTabStyleChanged)
        {
            SetWindowLongPtr(vwig.hwndApp, GWL_EXSTYLE, vl4DMMUiScaleSourceExStyleRestore);
            SetWindowPos(vwig.hwndApp, hNil, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            vf4DMMUiScaleSourceAltTabStyleChanged = fFalse;
        }
        RemovePropA(vwig.hwndApp, ksz4DMMUiScaleSuspendedProp);
        RemovePropA(vwig.hwndApp, ksz4DMMUiScaleWindowProp);
        RemovePropA(vwig.hwndApp, ksz4DMMUiScaleNumeratorProp);
        RemovePropA(vwig.hwndApp, ksz4DMMUiScaleDenominatorProp);
    }

    HWND hwndScale = vhwnd4DMMUiScale;
    vhwnd4DMMUiScale = hNil;
    vhwnd4DMMUiScaleViewport = hNil;
    if (hwndScale != hNil && IsWindow(hwndScale))
        DestroyWindow(hwndScale);

    vpfn4DMMDwmRegisterThumbnail = pvNil;
    vpfn4DMMDwmUnregisterThumbnail = pvNil;
    vpfn4DMMDwmUpdateThumbnailProperties = pvNil;
    if (vhmod4DMMDwmApi != hNil)
    {
        FreeLibrary(vhmod4DMMDwmApi);
        vhmod4DMMDwmApi = hNil;
    }
}
#endif

#ifdef KAUAI_WIN32
static void Close4DMMSettingsForShutdown(void);

static void _WriteStudioMiniDump(EXCEPTION_POINTERS *pep)
{
#if defined(BRENDER_MODERN_14)
    if (pep == pvNil)
        return;

    char szExe[MAX_PATH];
    DWORD cch = GetModuleFileNameA(NULL, szExe, SIZEOF(szExe));
    if (cch == 0 || cch >= SIZEOF(szExe))
        return;

    char *pchSlash = strrchr(szExe, '\\');
    char *pchSlash2 = strrchr(szExe, '/');
    if (pchSlash2 != pvNil && (pchSlash == pvNil || pchSlash2 > pchSlash))
        pchSlash = pchSlash2;
    char *pchName = (pchSlash == pvNil) ? szExe : pchSlash + 1;
    strcpy_s(pchName, SIZEOF(szExe) - (pchName - szExe), "3dmm_crash.dmp");

    HANDLE hfile = CreateFileA(szExe, GENERIC_WRITE, FILE_SHARE_READ, pvNil, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, hNil);
    if (hfile == INVALID_HANDLE_VALUE)
        return;

    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = pep;
    mei.ClientPointers = FALSE;
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hfile,
                      MiniDumpWithDataSegs, &mei, pvNil, pvNil);
    CloseHandle(hfile);
    BrModernLog("CRASH minidump written path=%s", szExe);
#else
    (void)pep;
#endif
}

/***************************************************************************
    Write the *first* Windows exception site before APP::Run starts cleanup.
    The normal Unexpected Exit dialog is followed by debug assertions from
    partially torn-down Studio objects, which hides the address that actually
    caused the crash.  Keep this logger dependency-free and next to 3DMOVIE.EXE
    so a crash report survives even if Kauai object state is already damaged.
***************************************************************************/
static void _LogStudioUnhandledException(EXCEPTION_POINTERS *pep)
{
    if (pep == pvNil || pep->ExceptionRecord == pvNil)
        return;

    char szExe[MAX_PATH];
    char szLog[MAX_PATH];
    DWORD cch = GetModuleFileNameA(NULL, szExe, SIZEOF(szExe));
    if (cch == 0 || cch >= SIZEOF(szExe))
        return;

    CopyPb(szExe, szLog, cch + 1);
    char *pchSlash = strrchr(szLog, '\\');
    char *pchSlash2 = strrchr(szLog, '/');
    if (pchSlash2 != pvNil && (pchSlash == pvNil || pchSlash2 > pchSlash))
        pchSlash = pchSlash2;
    char *pchName = (pchSlash == pvNil) ? szLog : pchSlash + 1;
    strcpy_s(pchName, SIZEOF(szLog) - (pchName - szLog), "3dmm_crash.log");

    FILE *pfile = pvNil;
    if (0 != fopen_s(&pfile, szLog, "w") || pfile == pvNil)
        return;

    EXCEPTION_RECORD *per = pep->ExceptionRecord;
    uintptr_t luBase = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t luAddress = (uintptr_t)per->ExceptionAddress;
    uintptr_t cbImage = 0;
    if (luBase != 0)
    {
        IMAGE_DOS_HEADER *pdos = (IMAGE_DOS_HEADER *)luBase;
        if (pdos->e_magic == IMAGE_DOS_SIGNATURE)
        {
            IMAGE_NT_HEADERS *pnt = (IMAGE_NT_HEADERS *)(luBase + pdos->e_lfanew);
            if (pnt->Signature == IMAGE_NT_SIGNATURE)
                cbImage = pnt->OptionalHeader.SizeOfImage;
        }
    }

    fprintf(pfile, "3DMMEx crash site\n");
    fprintf(pfile, "exception_code=0x%08lX\n", (unsigned long)per->ExceptionCode);
    fprintf(pfile, "exception_address=%p\n", per->ExceptionAddress);
    fprintf(pfile, "exe_base=%p\n", (void *)luBase);
    if (cbImage != 0 && luAddress >= luBase && luAddress < luBase + cbImage)
        fprintf(pfile, "exe_rva=0x%llX\n", (unsigned long long)(luAddress - luBase));
    else
        fprintf(pfile, "exe_rva=outside_main_exe\n");
    fprintf(pfile, "exception_flags=0x%08lX\n", (unsigned long)per->ExceptionFlags);
    fprintf(pfile, "parameter_count=%lu\n", (unsigned long)per->NumberParameters);
    for (DWORD i = 0; i < per->NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; i++)
        fprintf(pfile, "parameter[%lu]=0x%llX\n", (unsigned long)i,
                (unsigned long long)per->ExceptionInformation[i]);

    if (pep->ContextRecord != pvNil)
    {
#if defined(_M_IX86)
        CONTEXT *pcx = pep->ContextRecord;
        fprintf(pfile, "EIP=0x%08lX ESP=0x%08lX EBP=0x%08lX\n",
                (unsigned long)pcx->Eip, (unsigned long)pcx->Esp, (unsigned long)pcx->Ebp);
        fprintf(pfile, "EAX=0x%08lX EBX=0x%08lX ECX=0x%08lX EDX=0x%08lX ESI=0x%08lX EDI=0x%08lX\n",
                (unsigned long)pcx->Eax, (unsigned long)pcx->Ebx, (unsigned long)pcx->Ecx,
                (unsigned long)pcx->Edx, (unsigned long)pcx->Esi, (unsigned long)pcx->Edi);
#elif defined(_M_X64)
        CONTEXT *pcx = pep->ContextRecord;
        fprintf(pfile, "RIP=0x%llX RSP=0x%llX RBP=0x%llX\n",
                (unsigned long long)pcx->Rip, (unsigned long long)pcx->Rsp, (unsigned long long)pcx->Rbp);
#endif
    }

    fflush(pfile);
    fclose(pfile);
}

static LONG _StudioExceptionFilter(EXCEPTION_POINTERS *pep)
{
#if defined(BRENDER_MODERN_14)
    BrModernLogException(pep, "APP::Run unhandled SEH");
#endif
    if (pep != pvNil && pep->ExceptionRecord != pvNil)
    {
        MVIE::LightEditorLog(pvNil, "CRASH code=0x%08lX address=%p",
                             (unsigned long)pep->ExceptionRecord->ExceptionCode,
                             pep->ExceptionRecord->ExceptionAddress);
    }
    MVIE::DiagCrash(pep);
    _LogStudioUnhandledException(pep);
    _WriteStudioMiniDump(pep);
    return UnhandledExceptionFilter(pep);
}

namespace
{
const int32_t ksidV3dmm = 6;

uint16_t WFromVmmBytes(const std::vector<uint8_t> &rgb, size_t ib)
{
    return (uint16_t)(rgb[ib] | ((uint16_t)rgb[ib + 1] << 8));
}

uint32_t LwFromVmmBytes(const std::vector<uint8_t> &rgb, size_t ib)
{
    return (uint32_t)rgb[ib] | ((uint32_t)rgb[ib + 1] << 8) | ((uint32_t)rgb[ib + 2] << 16) |
           ((uint32_t)rgb[ib + 3] << 24);
}

size_t IbFindVmm(const std::vector<uint8_t> &rgb, const uint8_t *prgbFind, size_t cbFind, size_t ibStart,
               size_t ibLim)
{
    if (cbFind == 0 || ibLim > rgb.size() || ibStart > ibLim || cbFind > ibLim - ibStart)
        return std::string::npos;

    for (size_t ib = ibStart; ib + cbFind <= ibLim; ib++)
    {
        if (0 == memcmp(&rgb[ib], prgbFind, cbFind))
            return ib;
    }
    return std::string::npos;
}

size_t IbFindVmmChunkyPayload(const std::vector<uint8_t> &rgb, size_t ibStart)
{
    static const uint8_t krgbChunky[] = {'C', 'H', 'N', '2'};
    const size_t kcbChunkyPrefix = 128;
    size_t ib = ibStart;

    while ((ib = IbFindVmm(rgb, krgbChunky, SIZEOF(krgbChunky), ib, rgb.size())) != std::string::npos)
    {
        if (ib + kcbChunkyPrefix <= rgb.size())
        {
            // CFP layout from Kauai chunk.cpp.  V3DMM Windows packages carry
            // little-endian CHN2 files, so validate the relative file/index/map
            // offsets before accepting a CHN2 byte sequence as the movie.
            uint16_t bo = WFromVmmBytes(rgb, ib + 12);
            uint32_t cbFile = LwFromVmmBytes(rgb, ib + 16);
            uint32_t ibIndex = LwFromVmmBytes(rgb, ib + 20);
            uint32_t cbIndex = LwFromVmmBytes(rgb, ib + 24);
            uint32_t ibMap = LwFromVmmBytes(rgb, ib + 28);
            uint32_t cbMap = LwFromVmmBytes(rgb, ib + 32);
            uint64_t cbRemain = (uint64_t)rgb.size() - ib;

            if (0 == memcmp(&rgb[ib + 4], " COS", 4) && bo == kboCur && cbFile == cbRemain &&
                cbFile >= kcbChunkyPrefix && ibIndex >= kcbChunkyPrefix && ibIndex < cbFile && cbIndex > 0 &&
                (uint64_t)ibIndex + cbIndex == ibMap && (uint64_t)ibMap + cbMap == cbFile)
            {
                return ib;
            }
        }
        ib++;
    }
    return std::string::npos;
}

size_t IbFindVmmZipEnd(const std::vector<uint8_t> &rgb, size_t ibZip, size_t ibLim)
{
    static const uint8_t krgbEocd[] = {'P', 'K', 5, 6};
    static const uint8_t krgbCentral[] = {'P', 'K', 1, 2};
    size_t ibEocd = ibZip;

    while ((ibEocd = IbFindVmm(rgb, krgbEocd, SIZEOF(krgbEocd), ibEocd, ibLim)) != std::string::npos)
    {
        if (ibEocd + 22 <= ibLim)
        {
            uint16_t iDisk = WFromVmmBytes(rgb, ibEocd + 4);
            uint16_t iDiskCentral = WFromVmmBytes(rgb, ibEocd + 6);
            uint16_t cEntryDisk = WFromVmmBytes(rgb, ibEocd + 8);
            uint16_t cEntry = WFromVmmBytes(rgb, ibEocd + 10);
            uint32_t cbCentral = LwFromVmmBytes(rgb, ibEocd + 12);
            uint32_t ibCentralRel = LwFromVmmBytes(rgb, ibEocd + 16);
            uint16_t cbComment = WFromVmmBytes(rgb, ibEocd + 20);
            size_t ibCentral = ibZip + ibCentralRel;
            size_t ibEnd = ibEocd + 22 + cbComment;

            if (iDisk == 0 && iDiskCentral == 0 && cEntryDisk == cEntry && cEntry > 0 && ibEnd <= ibLim &&
                ibCentral + cbCentral == ibEocd && ibCentral + SIZEOF(krgbCentral) <= ibLim &&
                0 == memcmp(&rgb[ibCentral], krgbCentral, SIZEOF(krgbCentral)))
            {
                return ibEnd;
            }
        }
        ibEocd++;
    }
    return std::string::npos;
}

bool FWriteVmmBytes(const std::filesystem::path &path, const std::vector<uint8_t> &rgb, size_t ibMin, size_t ibLim)
{
    if (ibMin > ibLim || ibLim > rgb.size())
        return fFalse;

    std::ofstream stm(path, std::ios::binary | std::ios::trunc);
    if (!stm)
        return fFalse;
    stm.write((const char *)&rgb[ibMin], (std::streamsize)(ibLim - ibMin));
    return stm.good();
}

std::string StrVmmPowerShellLiteral(const std::filesystem::path &path)
{
    std::string str = path.string();
    size_t ich = 0;
    while ((ich = str.find('\'', ich)) != std::string::npos)
    {
        str.insert(ich, 1, '\'');
        ich += 2;
    }
    return str;
}

bool FExpandVmmArchives(const std::filesystem::path &pathPackages, const std::filesystem::path &pathCache)
{
    std::filesystem::path pathScript = pathCache / "expand_vmm.ps1";
    std::ofstream stm(pathScript, std::ios::binary | std::ios::trunc);
    if (!stm)
        return fFalse;

    stm << "$ErrorActionPreference = 'Stop'\r\n"
        << "Get-ChildItem -LiteralPath '" << StrVmmPowerShellLiteral(pathPackages) << "' -Filter '*.zip' | "
        << "ForEach-Object { Expand-Archive -LiteralPath $_.FullName -DestinationPath '"
        << StrVmmPowerShellLiteral(pathCache) << "' -Force }\r\n";
    stm.close();
    if (!stm.good())
        return fFalse;

    std::string strCmd = "powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" +
                         pathScript.string() + "\"";
    std::vector<char> rgchCmd(strCmd.begin(), strCmd.end());
    rgchCmd.push_back(0);

    STARTUPINFOA sui = {};
    PROCESS_INFORMATION pi = {};
    sui.cb = SIZEOF(sui);
    sui.dwFlags = STARTF_USESHOWWINDOW;
    sui.wShowWindow = SW_HIDE;

    bool fRet = fFalse;
    if (CreateProcessA(pvNil, rgchCmd.data(), pvNil, pvNil, fFalse, CREATE_NO_WINDOW, pvNil, pvNil, &sui, &pi))
    {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD dwExit = 1;
        if (GetExitCodeProcess(pi.hProcess, &dwExit) && dwExit == 0)
            fRet = fTrue;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    std::error_code ec;
    std::filesystem::remove(pathScript, ec);
    return fRet;
}
} // namespace

// Package a normal CHN2 movie plus its current 4DMM sidecar into the V3DMM
// wrapper understood by FResolveVmmMovie. Existing VMM package metadata/VXPs
// are preserved byte-for-byte; only the trailing movie payload and our
// optional 4DCT metadata block are replaced.
bool F4DMMPackageVmm(PCSZ pszMovie, PCSZ pszSidecar, PCSZ pszDest)
{
    if (pszMovie == pvNil || pszDest == pvNil)
        return fFalse;

    std::ifstream stmMovie(std::filesystem::path(pszMovie), std::ios::binary);
    if (!stmMovie)
        return fFalse;
    std::vector<uint8_t> rgbMovie((std::istreambuf_iterator<char>(stmMovie)), std::istreambuf_iterator<char>());
    if (rgbMovie.size() < 128 || 0 != memcmp(rgbMovie.data(), "CHN2", 4))
        return fFalse;

    std::vector<uint8_t> rgbPrefix;
    std::filesystem::path pathDest(pszDest);
    MVIE::MultiLog(pvNil, "vmm_package begin movie=%s sidecar=%s dest=%s", pszMovie,
                   pszSidecar != pvNil ? pszSidecar : "", pszDest);
    {
        // Keep the destination input stream inside this scope. v122 held it
        // open through MoveFileExA(), which makes ordinary Save fail on
        // Windows while Save As to a new path succeeds.
        MVIE::MultiLog(pvNil, "vmm_package prefix_open begin dest=%s", pszDest);
        std::ifstream stmOld(pathDest, std::ios::binary);
        if (stmOld)
        {
            std::vector<uint8_t> rgbOld((std::istreambuf_iterator<char>(stmOld)), std::istreambuf_iterator<char>());
            if (stmOld.bad())
            {
                MVIE::MultiLog(pvNil, "vmm_package fail stage=prefix_read dest=%s", pszDest);
                return fFalse;
            }
            if (rgbOld.size() >= 16 && 0 == memcmp(rgbOld.data(), "v3dmm", 5))
            {
                const size_t ibMovieOld = IbFindVmmChunkyPayload(rgbOld, 5);
                if (ibMovieOld != std::string::npos)
                    rgbPrefix.assign(rgbOld.begin(), rgbOld.begin() + ibMovieOld);
            }
            MVIE::MultiLog(pvNil, "vmm_package prefix_read ok old_size=%lu prefix_size=%lu",
                           (unsigned long)rgbOld.size(), (unsigned long)rgbPrefix.size());
        }
    }
    MVIE::MultiLog(pvNil, "vmm_package prefix_closed dest=%s", pszDest);

    if (rgbPrefix.empty())
    {
        static const uint8_t krgbMinimalPrefix[] = {
            'v','3','d','m','m', 1, 1,
            0,0,0,0,0,0,0,
            4,0, '3','d','m','m', 0,0,0,0
        };
        rgbPrefix.assign(krgbMinimalPrefix, krgbMinimalPrefix + SIZEOF(krgbMinimalPrefix));
    }

    // Strip an older 4DCT block only when it exactly occupies the end of the
    // wrapper prefix. This avoids mistaking arbitrary bytes inside embedded
    // VXP ZIPs for our extension marker.
    static const uint8_t krgb4Dct[] = {'4','D','C','T'};
    for (size_t ib = rgbPrefix.size(); ib-- > 0; )
    {
        if (ib + 8 > rgbPrefix.size() || 0 != memcmp(&rgbPrefix[ib], krgb4Dct, SIZEOF(krgb4Dct)))
            continue;
        const uint32_t cb = (uint32_t)rgbPrefix[ib + 4] |
                            ((uint32_t)rgbPrefix[ib + 5] << 8) |
                            ((uint32_t)rgbPrefix[ib + 6] << 16) |
                            ((uint32_t)rgbPrefix[ib + 7] << 24);
        if ((uint64_t)ib + 8 + cb == rgbPrefix.size())
            rgbPrefix.resize(ib);
        break;
    }

    std::vector<uint8_t> rgbSidecar;
    if (pszSidecar != pvNil && *pszSidecar != 0)
    {
        std::ifstream stmSide(std::filesystem::path(pszSidecar), std::ios::binary);
        if (stmSide)
            rgbSidecar.assign((std::istreambuf_iterator<char>(stmSide)), std::istreambuf_iterator<char>());
    }

    std::filesystem::path pathTemp = pathDest;
    pathTemp += ".4dmm_tmp";
    MVIE::MultiLog(pvNil, "vmm_package temp_open begin temp=%s", pathTemp.string().c_str());
    std::ofstream stmOut(pathTemp, std::ios::binary | std::ios::trunc);
    if (!stmOut)
    {
        MVIE::MultiLog(pvNil, "vmm_package fail stage=temp_open temp=%s", pathTemp.string().c_str());
        return fFalse;
    }
    stmOut.write((const char *)rgbPrefix.data(), (std::streamsize)rgbPrefix.size());
    if (!rgbSidecar.empty())
    {
        const uint32_t cb = (uint32_t)rgbSidecar.size();
        stmOut.write("4DCT", 4);
        const uint8_t rgbCb[4] = {
            (uint8_t)(cb & 0xff), (uint8_t)((cb >> 8) & 0xff),
            (uint8_t)((cb >> 16) & 0xff), (uint8_t)((cb >> 24) & 0xff)};
        stmOut.write((const char *)rgbCb, 4);
        stmOut.write((const char *)rgbSidecar.data(), (std::streamsize)rgbSidecar.size());
    }
    stmOut.write((const char *)rgbMovie.data(), (std::streamsize)rgbMovie.size());
    stmOut.close();
    if (!stmOut.good())
    {
        MVIE::MultiLog(pvNil, "vmm_package fail stage=temp_write temp=%s", pathTemp.string().c_str());
        std::error_code ec;
        std::filesystem::remove(pathTemp, ec);
        return fFalse;
    }
    MVIE::MultiLog(pvNil, "vmm_package temp_closed ok temp=%s prefix=%lu sidecar=%lu movie=%lu",
                   pathTemp.string().c_str(), (unsigned long)rgbPrefix.size(),
                   (unsigned long)rgbSidecar.size(), (unsigned long)rgbMovie.size());

    MVIE::MultiLog(pvNil, "vmm_package replace begin temp=%s dest=%s",
                   pathTemp.string().c_str(), pathDest.string().c_str());
    SetLastError(ERROR_SUCCESS);
    if (!MoveFileExA(pathTemp.string().c_str(), pathDest.string().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const DWORD dwError = GetLastError();
        MVIE::MultiLog(pvNil, "vmm_package fail stage=replace winerr=%lu temp=%s dest=%s",
                       (unsigned long)dwError, pathTemp.string().c_str(), pathDest.string().c_str());
        std::error_code ec;
        std::filesystem::remove(pathTemp, ec);
        return fFalse;
    }
    MVIE::MultiLog(pvNil, "vmm_package replace ok dest=%s", pathDest.string().c_str());
    return fTrue;
}

/***************************************************************************
    Launch the standalone native OBJ -> VXP2 tool.  The converter is installed
    beside 3dmovie.exe under tools\obj2vxp2 so this path remains independent
    of the source/build working directory.

    Converter file logging is intentionally enabled only when 4DMM itself was
    launched with -multi_log.  In that case pass an explicit child log path in
    the same runtime logs directory captured by the BLAZE diagnostics exporter.
***************************************************************************/
static std::wstring _WstrQuote4DMMProcessArg(const std::wstring &arg)
{
    if (arg.find_first_of(L" \t\"") == std::wstring::npos)
        return arg;

    std::wstring out = L"\"";
    size_t cSlash = 0;
    for (wchar_t ch : arg)
    {
        if (ch == L'\\')
        {
            ++cSlash;
            continue;
        }
        if (ch == L'\"')
        {
            out.append(cSlash * 2 + 1, L'\\');
            out.push_back(L'\"');
            cSlash = 0;
            continue;
        }
        out.append(cSlash, L'\\');
        cSlash = 0;
        out.push_back(ch);
    }
    out.append(cSlash * 2, L'\\');
    out.push_back(L'\"');
    return out;
}

static bool _FLaunch4DMMObj2Vxp2(PMVIE pmvie)
{
    wchar_t wszModule[MAX_PATH];
    const DWORD cchModule = GetModuleFileNameW(pvNil, wszModule, (DWORD)(sizeof(wszModule) / sizeof(wszModule[0])));
    if (cchModule == 0 || cchModule >= (DWORD)(sizeof(wszModule) / sizeof(wszModule[0])))
    {
        const DWORD dwError = GetLastError();
        MVIE::MultiLog(pmvie, "obj2vxp2_launch fail stage=module_path winerr=%lu", (unsigned long)dwError);
        MessageBoxW(pvNil, L"Could not locate 3dmovie.exe to find the OBJ to VXP2 converter.",
                    L"4DMM OBJ to VXP2", MB_OK | MB_ICONERROR);
        return fFalse;
    }

    const std::filesystem::path pathDist = std::filesystem::path(wszModule).parent_path();
    const std::filesystem::path pathTool = pathDist / L"tools" / L"obj2vxp2" / L"obj2vxp2.exe";
    std::error_code ec;
    if (!std::filesystem::exists(pathTool, ec) || ec)
    {
        MVIE::MultiLog(pmvie, "obj2vxp2_launch fail stage=tool_missing path=%s ec=%ld",
                       pathTool.string().c_str(), (long)ec.value());
        std::wstring wstr = L"The native OBJ to VXP2 converter was not found:\n\n" + pathTool.wstring();
        MessageBoxW(pvNil, wstr.c_str(), L"4DMM OBJ to VXP2", MB_OK | MB_ICONERROR);
        return fFalse;
    }

    std::wstring wstrCommand = _WstrQuote4DMMProcessArg(pathTool.wstring());

    int32_t guiScaleNum = 1;
    int32_t guiScaleDen = 1;
    Get4DMMExternalToolScale(&guiScaleNum, &guiScaleDen);
    wchar_t wszGuiScale[32];
    swprintf_s(wszGuiScale, sizeof(wszGuiScale) / sizeof(wszGuiScale[0]), L"%.1f",
               (double)guiScaleNum / (double)guiScaleDen);
    wstrCommand += L" -gui_scale ";
    wstrCommand += wszGuiScale;

    const bool fMultiLog = MVIE::FMultiLogMode();
    if (fMultiLog)
    {
        const std::filesystem::path pathLog = pathDist / L"logs" / L"obj2vxp2.log";
        std::filesystem::create_directories(pathLog.parent_path(), ec);
        if (ec)
        {
            MVIE::MultiLog(pmvie, "obj2vxp2_launch log_dir warning ec=%ld dir=%s",
                           (long)ec.value(), pathLog.parent_path().string().c_str());
            ec.clear();
        }
        wstrCommand += L" --multi-log ";
        wstrCommand += _WstrQuote4DMMProcessArg(pathLog.wstring());
    }

    MVIE::MultiLog(pmvie, "obj2vxp2_launch begin tool=%s child_log=%d gui_scale=%ld/%ld",
                   pathTool.string().c_str(), (int)fMultiLog, (long)guiScaleNum, (long)guiScaleDen);

    std::vector<wchar_t> rgchCommand(wstrCommand.begin(), wstrCommand.end());
    rgchCommand.push_back(chNil);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ClearPb(&si, SIZEOF(si));
    ClearPb(&pi, SIZEOF(pi));
    si.cb = SIZEOF(si);

    SetLastError(ERROR_SUCCESS);
    const std::filesystem::path pathToolDir = pathTool.parent_path();
    const BOOL fCreated = CreateProcessW(pathTool.c_str(), rgchCommand.data(), pvNil, pvNil, FALSE, 0,
                                         pvNil, pathToolDir.c_str(), &si, &pi);
    if (!fCreated)
    {
        const DWORD dwError = GetLastError();
        MVIE::MultiLog(pmvie, "obj2vxp2_launch fail stage=create_process winerr=%lu tool=%s",
                       (unsigned long)dwError, pathTool.string().c_str());
        wchar_t wszError[256];
        swprintf_s(wszError, sizeof(wszError) / sizeof(wszError[0]), L"Could not launch OBJ to VXP2 converter.\n\nWindows error %lu.",
                   (unsigned long)dwError);
        MessageBoxW(pvNil, wszError, L"4DMM OBJ to VXP2", MB_OK | MB_ICONERROR);
        return fFalse;
    }

    MVIE::MultiLog(pmvie, "obj2vxp2_launch ok pid=%lu", (unsigned long)pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return fTrue;
}

/***************************************************************************
    Append a command-line startup diagnostic before any MVIE exists.
***************************************************************************/
static void _LogUndoHistoryStartup(PCSZ pszText)
{
    char szPath[MAX_PATH];
    DWORD cch = GetModuleFileNameA(GetModuleHandleA(pvNil), szPath, (DWORD)SIZEOF(szPath));
    if (cch == 0 || cch >= (DWORD)SIZEOF(szPath))
        return;

    char *pchSlash = strrchr(szPath, '\\');
    if (pchSlash == pvNil)
        pchSlash = strrchr(szPath, '/');
    if (pchSlash != pvNil)
        *(pchSlash + 1) = chNil;
    else
        szPath[0] = chNil;

    if (strcat_s(szPath, SIZEOF(szPath), "undo_history.log") != 0)
        return;

    FILE *pfile = pvNil;
    if (fopen_s(&pfile, szPath, "a") != 0 || pfile == pvNil)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf_s(pfile, "%04u-%02u-%02u %02u:%02u:%02u.%03u  %s\n", (unsigned)st.wYear,
              (unsigned)st.wMonth, (unsigned)st.wDay, (unsigned)st.wHour, (unsigned)st.wMinute,
              (unsigned)st.wSecond, (unsigned)st.wMilliseconds, pszText);
    fclose(pfile);
}
#endif // 3DMMEx: KAUAI_WIN32

#ifdef KAUAI_SDL
extern const uint8_t vrgbAppIcon48x48[];
extern const int32_t vcbAppIcon48x48;
#endif // 3DMMEx: KAUAI_SDL

BEGIN_CMD_MAP(APP, APPB)
ON_CID_GEN(cidInfo, &APP::FCmdInfo, pvNil)
ON_CID_GEN(cidLoadStudio, &APP::FCmdLoadStudio, pvNil)
ON_CID_GEN(cidLoadBuilding, &APP::FCmdLoadBuilding, pvNil)
ON_CID_GEN(cidTheaterOpen, &APP::FCmdTheaterOpen, pvNil)
ON_CID_GEN(cidTheaterClose, &APP::FCmdTheaterClose, pvNil)
ON_CID_GEN(cidPortfolioOpen, &APP::FCmdPortfolioOpen, pvNil)
ON_CID_GEN(cidPortfolioClear, &APP::FCmdPortfolioClear, pvNil)
ON_CID_GEN(cidDisableAccel, &APP::FCmdDisableAccel, pvNil)
ON_CID_GEN(cidEnableAccel, &APP::FCmdEnableAccel, pvNil)
ON_CID_GEN(cidInvokeSplot, &APP::FCmdInvokeSplot, pvNil)
ON_CID_GEN(cidExitStudio, &APP::FCmdExitStudio, pvNil)
ON_CID_GEN(cidDeactivate, &APP::FCmdDeactivate, pvNil)
ON_CID_GEN(cidToggleFullscreen, &APP::FCmdToggleFullscreen, pvNil)
ON_CID_GEN(cid4DMMSceneLights, &APP::FCmd4DMMSceneLights, pvNil)
ON_CID_GEN(cid4DMMHideLightObjects, &APP::FCmd4DMMHideLightObjects, pvNil)
ON_CID_GEN(cid4DMMSettings, &APP::FCmd4DMMSettings, pvNil)
ON_CID_GEN(cid4DMMObjectGroups, &APP::FCmd4DMMObjectGroups, pvNil)
ON_CID_GEN(cid4DMMActorStudio, &APP::FCmd4DMMActorStudio, pvNil)
ON_CID_GEN(cid4DMMObj2Vxp2, &APP::FCmd4DMMObj2Vxp2, pvNil)
END_CMD_MAP_NIL()

APP vapp;
PTAGM vptagm;

RTCLASS(APP)
RTCLASS(KWA)

/** 3DMMv1.0: *************************************************************************
    Entry point for a Kauai-based app.
***************************************************************************/
void FrameMain(void)
{
#if defined(KAUAI_WIN32) && defined(BRENDER_MODERN_14)
    BrModernLogBootstrapFromCommandLine();
    MODERN_BR_LOG("APP FrameMain enter hinstance=%p command_line=%s", vwig.hinst, GetCommandLineA());
#endif
    Debug(vcactAV = 2;) // 3DMMv1.0: Speeds up the debug build
        vapp.Run(fappOffscreen, fgobNil, kginMark);
    MODERN_BR_LOG("APP FrameMain returned from vapp.Run");
}

/** 3DMMEx: ****************************************************************************
    Run
        Overridden APPB::Run method, so that we can attempt to recover
        gracefully from a crash.

    Arguments:
        uint32_t grfapp -- app flags
        uint32_t grfgob -- GOB flags
        long ginDef  -- default GOB invalidation

************************************************************ PETED ***********/
void APP::Run(uint32_t grfapp, uint32_t grfgob, int32_t ginDef)
{
    /* 3DMMv1.0: Don't bother w/ AssertThis; we'd have to use AssertBaseThis, or
        possibly the parent's AssertValid, which gets done almost right
        away anyway */

    MODERN_BR_LOG("APP::Run enter grfapp=0x%08lX grfgob=0x%08lX ginDef=%ld",
                  (unsigned long)grfapp, (unsigned long)grfgob, (long)ginDef);
    MODERN_BR_LOG("APP::Run _CleanupTemp BEGIN");
    _CleanupTemp();
    MODERN_BR_LOG("APP::Run _CleanupTemp returned");

    // 3DMMEx: Get stereo sound preference from registry
    int32_t lwValue = fFalse;
    (void)FGetSetRegKey(kszStereoSound, &lwValue, SIZEOF(lwValue), fregNil);
    if (FPure(lwValue))
    {
        grfapp |= fappStereoSound;
    }

#ifdef WIN
    __try
    {
        MODERN_BR_LOG("APP::Run APP_PAR::Run BEGIN");
        APP_PAR::Run(grfapp, grfgob, ginDef);
        MODERN_BR_LOG("APP::Run APP_PAR::Run returned normally");
    }
    __except (_StudioExceptionFilter(GetExceptionInformation()))
#else
    try
    {
        APP_PAR::Run(grfapp, grfgob, ginDef);
    }
    catch (...)
#endif
    {
        MODERN_BR_LOG("APP::Run entered abnormal-exit handler");
        PDLG pdlg;

        pdlg = DLG::PdlgNew(dlidAbnormalExit, pvNil, pvNil);
        if (pdlg != pvNil)
        {
            pdlg->IditDo();
            ReleasePpo(&pdlg);
        }

        _fQuit = fTrue;
        MVU::RestoreKeyboardRepeat();
#ifdef WIN
        ClipCursor(NULL);
#endif // 3DMMv1.0: WIN
        _CleanUp();
    }
}

/** 3DMMv1.0: *************************************************************************
    Get the name for the app.  Note that the string comes from a resource
    in the exe rather than a chunky file since the app name is needed
    before any chunky files are opened.  The app name should not change
    even for different products, i.e., Socrates and Playdo will have
    different _stnProduct, but the same _stnAppName.
***************************************************************************/
void APP::GetStnAppName(PSTN pstn)
{
    AssertBaseThis(0);
    AssertPo(pstn, 0);

    if (_stnAppName.Cch() == 0)
    {
#ifdef WIN
        SZ sz;

        if (0 != LoadString(vwig.hinst, stidAppName, sz, kcchMaxSz))
            _stnAppName = sz;
        else
            Warn("Couldn't read app name");
#else
        _stnAppName = PszLit("Microsoft 3D Movie Maker");
#endif
    }
    *pstn = _stnAppName;
}

#ifdef DEBUG
struct DBINFO
{
    int32_t cactAV;
};
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Init the APP
***************************************************************************/
bool APP::_FInit(uint32_t grfapp, uint32_t grfgob, int32_t ginDef)
{
    AssertBaseThis(0);
    MODERN_BR_LOG("APP::_FInit ENTER grfapp=0x%08lX grfgob=0x%08lX ginDef=%ld",
                  (unsigned long)grfapp, (unsigned long)grfgob, (long)ginDef);

    uint32_t tsHomeLogo;
    uint32_t tsSplashScreen;
    FNI fniUserDoc;
    int32_t fFirstTimeUser;
    bool fBypassStartup;

    // 3DMMEx: Load startup preferences
    int32_t fStartupSound = kfStartupSoundDefault;
    (void)FGetSetRegKey(kszStartupSoundValue, &fStartupSound, SIZEOF(fStartupSound), fregNil);

    // 3DMMv1.0: Only allow one copy of 3DMM to run at a time:
    MODERN_BR_LOG("APP::_FInit check existing instance");
    if (_FAppAlreadyRunning())
    {
        _TryToActivateWindow();
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }
#ifdef DEBUG
    {
        DBINFO dbinfo;

        dbinfo.cactAV = vcactAV;
        if (FGetSetRegKey(PszLit("DebugSettings"), &dbinfo, SIZEOF(DBINFO), fregSetDefault | fregBinary))
        {
            vcactAV = dbinfo.cactAV;
        }
    }
#endif // 3DMMv1.0: DEBUG

    MODERN_BR_LOG("APP::_FInit _FEnsureOS BEGIN");
    if (!_FEnsureOS())
    {
        _FGenericError(PszLit("_FEnsureOS"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    MODERN_BR_LOG("APP::_FInit _FEnsureOS OK; _FEnsureAudio BEGIN");
    if (!_FEnsureAudio())
    {
        _FGenericError(PszLit("_FEnsureAudio"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    MODERN_BR_LOG("APP::_FInit _FEnsureAudio OK; _FEnsureVideo BEGIN");
    if (!_FEnsureVideo())
    {
        _FGenericError(PszLit("_FEnsureVideo"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    MODERN_BR_LOG("APP::_FInit _FEnsureVideo OK; _ParseCommandLine BEGIN");
    _ParseCommandLine();
    MODERN_BR_LOG("APP::_FInit _ParseCommandLine returned");
#if defined(KAUAI_WIN32)
    vf4DMMCommandLineParsed = fTrue;
    GetPortfolioDoc(&fniUserDoc);
    if (vf4DMMExplicitStartupDocument)
    {
        // From this point onward a file-family error may belong to the user's
        // requested document, so the pre-init blank-editor safety net is no
        // longer allowed to consume anything.
        vf4DMMBlankStartupPhase = fFalse;
        vf4DMMSuppressNextBlankStartupFileError = fFalse;
        MVIE::MultiLog(pvNil, "startup_error_filter disarmed explicit_startup_document");
    }
    else
    {
        vf4DMMBlankStartupPhase = fTrue;
        // Do not re-arm if the pre-parse erc=400 was already consumed.  That
        // one process-start error is the entire reason this guard exists.
        if (vf4DMMSuppressNextBlankStartupFileError)
            MVIE::MultiLog(pvNil, "startup_error_filter armed no_explicit_startup_document");

        // -e without -o/a positional movie means "open the editor", not
        // "open whatever stale FNI the old portfolio happened to retain".
        if (fniUserDoc.Ftg() != ftgNil)
        {
            MVIE::MultiLog(pvNil,
                "startup_document stale_portfolio_discarded ftg=%lu explicit=0",
                (unsigned long)fniUserDoc.Ftg());
            _fniPortfolioDoc.SetNil();
            fniUserDoc.SetNil();
        }
    }
#endif
    fBypassStartup = (_fEditorOnly || _fPlaybackOnly);

    // 3DMMv1.0: If _ParseCommandLine doesn't set _stnProduct, set to 3dMovieMaker
    MODERN_BR_LOG("APP::_FInit _FEnsureProductNames BEGIN");
    if (!_FEnsureProductNames())
    {
        _FGenericError(PszLit("_FEnsureProductNames"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    MODERN_BR_LOG("APP::_FInit _FEnsureProductNames OK; _FFindMsKidsDir BEGIN");
    if (!_FFindMsKidsDir())
    {
        _FGenericError(PszLit("_FFindMsKidsDir"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    // 3DMMv1.0: Init product names for tagman & _fniProductDir & potentially _stnProduct
    MODERN_BR_LOG("APP::_FInit _FFindMsKidsDir OK; _FInitProductNames BEGIN");
    if (!_FInitProductNames())
    {
        // 3DMMEx: _FInitProductNames prints its own errors
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    MODERN_BR_LOG("APP::_FInit _FInitProductNames OK; _FOpenResourceFile BEGIN");
    if (!_FOpenResourceFile())
    {
        _FGenericError(PszLit("_FOpenResourceFile"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    MODERN_BR_LOG("APP::_FInit _FOpenResourceFile OK; _FReadStringTables BEGIN");
    if (!_FReadStringTables())
    {
        _FGenericError(PszLit("_FReadStringTables"));
        _fDontReportInitFailure = fTrue;

        goto LFail;
    }

    MODERN_BR_LOG("APP::_FInit _FReadStringTables OK; _FEnsureDisplayResolution BEGIN");
    if (!_FEnsureDisplayResolution())
    { // 3DMMv1.0: may call _FInitOS
        _FGenericError(PszLit("_FEnsureDisplayResolution"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    MODERN_BR_LOG("APP::_FInit _FEnsureDisplayResolution OK; _FEnsureColorDepth BEGIN");
    if (!_FEnsureColorDepth())
    {
        _FGenericError(PszLit("_FEnsureColorDepth"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    MODERN_BR_LOG("APP::_FInit _FEnsureColorDepth OK; APP_PAR::_FInit BEGIN");
    if (!APP_PAR::_FInit(grfapp, grfgob, ginDef))
    { // 3DMMv1.0: calls _FInitOS
        _FGenericError(PszLit("APP_PAR::_FInit"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

#ifdef KAUAI_SDL
    AssertDo(FSetWindowIcon(vrgbAppIcon48x48, vcbAppIcon48x48), "Couldn't set application icon");
#endif // 3DMMEx: KAUAI_SDL

    MODERN_BR_LOG("APP::_FInit APP_PAR::_FInit OK; _FInitAcceleratorTable BEGIN main_hwnd=%p", vwig.hwndApp);
    if (!_FInitAcceleratorTable())
    {
        _FGenericError(PszLit("_FInitAcceleratorTable"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    /* 3DMMv1.0: Ensure default font.  Do it here just so we get the error reported
        early. */
    OnnDefVariable();

    MODERN_BR_LOG("APP::_FInit accelerator OK; _FInitKidworld BEGIN");
    if (!_FInitKidworld())
    {
        _FGenericError(PszLit("_FInitKidworld"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    MODERN_BR_LOG("APP::_FInit Kidworld OK; startup_bypass=%d", (int)fBypassStartup);
    if (!fBypassStartup)
    {
        MODERN_BR_LOG("APP::_FInit _FDisplayHomeLogo BEGIN");
        if (!_FDisplayHomeLogo())
        {
            _FGenericError(PszLit("_FDisplayHomeLogo"));
            _fDontReportInitFailure = fTrue;
            goto LFail;
        }
    }
    else
    {
        // Direct-editor and playback-runner modes still need the initial
        // 3DMM palette installed.  The skipped Home Logo path normally does
        // this before any studio/movie draw happens.  Without it, movie
        // backgrounds can paint with whatever palette garbage is currently
        // active until Windows re-realizes the palette on an Alt-Tab.
        MODERN_BR_LOG("APP::_FInit bypass startup; _FSetInitialPalette BEGIN");
        if (!_FSetInitialPalette())
        {
            _FGenericError(PszLit("_FSetInitialPalette"));
            _fDontReportInitFailure = fTrue;
            goto LFail;
        }
    }
    tsHomeLogo = TsCurrent();

    MODERN_BR_LOG("APP::_FInit initial display/palette OK; _FDetermineIfSlowCPU BEGIN");
    if (!_FDetermineIfSlowCPU())
    {
        _FGenericError(PszLit("_FDetermineIfSlowCPU"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    MODERN_BR_LOG("APP::_FInit CPU test OK; _FInitTdt BEGIN");
    if (!_FInitTdt())
    {
        _FGenericError(PszLit("_FInitTdt"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    MODERN_BR_LOG("APP::_FInit TDT OK; MTRL::FSetShadeTable BEGIN");
    if (!MTRL::FSetShadeTable(_pcfl, kctgTmap, 0))
    {
        _FGenericError(PszLit("FSetShadeTable"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    if (fStartupSound && !fBypassStartup)
    {
        while (TsCurrent() - tsHomeLogo < kdtsHomeLogo)
            ; // spin until home logo has been up long enougH
    }

    if (!fBypassStartup)
    {
        if (!_FShowSplashScreen())
        {
            _FGenericError(PszLit("_FShowSplashScreen"));
            _fDontReportInitFailure = fTrue;
            goto LFail;
        }
    }
    tsSplashScreen = TsCurrent();

    if (fStartupSound && !fBypassStartup)
    {
        if (!_FPlaySplashSound())
        {
            _FGenericError(PszLit("_FPlaySplashSound"));
            _fDontReportInitFailure = fTrue;
            goto LFail;
        }
    }

    MODERN_BR_LOG("APP::_FInit shade table OK; _FGetUserName BEGIN");
    if (!_FGetUserName())
    {
        _FGenericError(PszLit("_FGetUserName"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    MODERN_BR_LOG("APP::_FInit user name OK; _FGetUserDirectories BEGIN");
    if (!_FGetUserDirectories())
    {
        _FGenericError(PszLit("_FGetUserDirectories"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    MODERN_BR_LOG("APP::_FInit user dirs OK; _FReadUserData BEGIN");
    if (!_FReadUserData())
    {
        _FGenericError(PszLit("_FReadUserData"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    MODERN_BR_LOG("APP::_FInit user data OK; _FInitCrm BEGIN");
    if (!_FInitCrm())
    {
        _FGenericError(PszLit("_FInitCrm"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    if (fStartupSound && !fBypassStartup)
    {
        while (TsCurrent() - tsSplashScreen < kdtsSplashScreen)
            ; // 3DMMv1.0: spin until splash screen has been up long enough
    }
    Pkwa()->SetMbmp(pvNil); // 3DMMv1.0: bring down splash screen

    // 3DMMv1.0: If the user specified a doc on the command line, go straight
    // 3DMMv1.0: to the studio.  Otherwise, start the building.
    MODERN_BR_LOG("APP::_FInit CRM OK; choose startup destination");
    GetPortfolioDoc(&fniUserDoc);
#if defined(KAUAI_WIN32)
    // The command line, not the mutable portfolio FNI, owns this decision.
    if (vf4DMMBlankStartupPhase && !vf4DMMExplicitStartupDocument)
    {
        if (fniUserDoc.Ftg() != ftgNil)
        {
            MVIE::MultiLog(pvNil,
                "startup_document late_stale_portfolio_discarded ftg=%lu",
                (unsigned long)fniUserDoc.Ftg());
            _fniPortfolioDoc.SetNil();
            fniUserDoc.SetNil();
        }
        if (vf4DMMSuppressNextBlankStartupFileError)
            MVIE::MultiLog(pvNil, "startup_error_filter retained direct_editor_no_document");
    }
    else
    {
        vf4DMMBlankStartupPhase = fFalse;
        vf4DMMSuppressNextBlankStartupFileError = fFalse;
    }
#endif

    // Publish the command-line undo mode before any movie is constructed.
    // Without -u, movies retain the original one-level undo/redo behavior.
    if (!FSetProp(kpridUndoHistory, _fUndoHistory))
    {
        _FGenericError(PszLit("FSetProp: kpridUndoHistory"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    if (_fPlaybackOnly)
    {
        // Playback-only mode should use the real 3DMM Theatre path, not the
        // Studio/editor path with the shell hidden offscreen.  The Theatre
        // scripts create kgobMovie, which owns the playback-only TATR object
        // and the movie viewport.
        if (!FSetProp(kpridPlaybackOnly, fTrue))
        {
            _FGenericError(PszLit("FSetProp: kpridPlaybackOnly"));
            _fDontReportInitFailure = fTrue;
            goto LFail;
        }
        if (!FSetProp(kpridBuildingGob, kgobTheatre1))
        {
            _FGenericError(PszLit("FSetProp: kpridBuildingGob"));
            _fDontReportInitFailure = fTrue;
            goto LFail;
        }
        if (!FSetProp(kpridBuildingState, kst5))
        {
            _FGenericError(PszLit("FSetProp: kpridBuildingState"));
            _fDontReportInitFailure = fTrue;
            goto LFail;
        }
        if (!_FInitBuilding())
        {
            _FGenericError(PszLit("_FInitBuilding"));
            _fDontReportInitFailure = fTrue;
            goto LFail;
        }
    }
    else if (_fEditorOnly || fniUserDoc.Ftg() != ftgNil)
    {
        if (!FSetProp(kpridBuildingGob, kgobStudio1))
        {
            _FGenericError(PszLit("FSetProp: kpridBuildingGob"));
            _fDontReportInitFailure = fTrue;
            goto LFail;
        }
        MODERN_BR_LOG("APP::_FInit direct Studio path; _FInitStudio BEGIN");
        if (!_FInitStudio(&fniUserDoc, fFalse))
        {
            _FGenericError(PszLit("_FInitStudio"));
            _fDontReportInitFailure = fTrue;
            goto LFail;
        }
    }
    else
    {
        // 3DMMv1.0: Startup place depends on whether this is the user's first time in.
        if (!FGetProp(kpridFirstTimeUser, &fFirstTimeUser))
        {
            _FGenericError(PszLit("FGetProp: kpridFirstTimeUser"));
            _fDontReportInitFailure = fTrue;
            goto LFail;
        }

        if (!FSetProp(kpridBuildingGob, (fFirstTimeUser ? kgobCloset : kgobLogin)))
        {
            _FGenericError(PszLit("FSetProp: kpridBuildingGob"));
            _fDontReportInitFailure = fTrue;
            goto LFail;
        }
        if (!_FInitBuilding())
        {
            _FGenericError(PszLit("_FInitBuilding"));
            _fDontReportInitFailure = fTrue;
            goto LFail;
        }
    }

    MODERN_BR_LOG("APP::_FInit startup destination initialized; EnsureInteractive");
    EnsureInteractive();
#if defined(KAUAI_WIN32) && defined(BRENDER_MODERN_14)
    // glrend needs a real HWND/DC even when the user does not request -v.
    // Create the same viewport host hidden so the internal Kauai viewport can
    // receive GPU readback without making the external window mandatory.
    _FEnsureViewportWindow();
#endif
    MODERN_BR_LOG("APP::_FInit SUCCESS main_hwnd=%p viewport_requested=%d", vwig.hwndApp, (int)_fViewportWindow);

    return fTrue;
LFail:
    MODERN_BR_LOG("APP::_FInit LFAIL dont_report=%d switched_resolution=%d",
                  (int)_fDontReportInitFailure, (int)_fSwitchedResolution);
    _fQuit = fTrue;

    if (_fSwitchedResolution)
    {
        if (_FSwitch640480(fFalse)) // 3DMMv1.0: try to restore desktop
            _fSwitchedResolution = fFalse;
    }

    // 3DMMv1.0: _fDontReportInitFailure will be true if one of the above functions
    // 3DMMv1.0: has already posted an alert explaining why the app is shutting down.
    // 3DMMv1.0: If it's false, we put up a OOM or generic error dialog.
    if (!_fDontReportInitFailure)
    {
        PDLG pdlg;

        if (vpers->FIn(ercOomHq) || vpers->FIn(ercOomPv) || vpers->FIn(ercOomNew))
            pdlg = DLG::PdlgNew(dlidInitFailedOOM, pvNil, pvNil);
        else
            pdlg = DLG::PdlgNew(dlidInitFailed, pvNil, pvNil);
        if (pvNil != pdlg)
            pdlg->IditDo();
        ReleasePpo(&pdlg);
    }
    return fFalse;
}

/** 3DMMv1.0: ****************************************************************************
    _CleanupTemp
        Removes any temp files leftover from a previous execution.

************************************************************ PETED ***********/
void APP::_CleanupTemp(void)
{
    FNI fni;

    /* 3DMMv1.0: Attempt to cleanup any leftovers from a previous bad exit */
    vftgTemp = kftgSocTemp;
    if (fni.FGetTemp())
    {
        FTG ftgTmp = vftgTemp;
        FNE fne;

        if (fne.FInit(&fni, &ftgTmp, 1))
        {
            bool fFlushErs = fFalse;

            while (fne.FNextFni(&fni))
            {
                if (!fni.FDelete())
                    fFlushErs = fTrue;
            }

            if (fFlushErs)
                vpers->Flush(ercFniDelete);
        }
    }
}

/** 3DMMv1.0: *************************************************************************
    Determine if another copy of 3DMM is already running by trying to
    create a named semaphore.
***************************************************************************/
bool APP::_FAppAlreadyRunning(void)
{
    AssertBaseThis(0);

#ifdef WIN
    HANDLE hsem;
    STN stn;

    GetStnAppName(&stn);

    hsem = CreateSemaphore(NULL, 0, 1, stn.Psz());
    if (hsem != NULL && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hsem);
        return fTrue;
    }
#else  // 3DMMv1.0: !WIN
    // 3DMMEx: FUTURE: Check if app is already running
#endif // 3DMMv1.0: WIN

    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Try to find another instance of 3DMM and bring its window to front
    Also, if a document was on the command line, notify the other instance
***************************************************************************/
void APP::_TryToActivateWindow(void)
{
    AssertBaseThis(0);

#ifdef WIN
    HWND hwnd;
    STN stn;
    FNI fniUserDoc;

    GetStnAppName(&stn);
    hwnd = FindWindow(kpszAppWndCls, stn.Psz());
    if (NULL != hwnd)
    {
        SetForegroundWindow(hwnd);
        ShowWindow(hwnd, SW_RESTORE); // 3DMMv1.0: in case it was minimized
        _ParseCommandLine();
        GetPortfolioDoc(&fniUserDoc);
        if (fniUserDoc.Ftg() != ftgNil)
            _FSendOpenDocCmd(hwnd, &fniUserDoc); // 3DMMv1.0: ignore failure
    }
#else  // 3DMMv1.0: !WIN
    RawRtn();
#endif // 3DMMv1.0: WIN
}

/** 3DMMv1.0: *************************************************************************
    If we're not running on at least Win95 or NT 3.51, complain and return
    fFalse.
***************************************************************************/
bool APP::_FEnsureOS(void)
{
    AssertBaseThis(0);

    // 3DMMEx: We can no longer compile for Windows pre-95 or NT 3.51
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Notifies the user if there is no suitable wave-out and/or midi-out
    hardware.
***************************************************************************/
bool APP::_FEnsureAudio(void)
{
    AssertBaseThis(0);

#ifdef KAUAI_WIN32
    int32_t cwod; // 3DMMv1.0: count of wave-out devices
    int32_t cmod; // 3DMMv1.0: count of midi-out devices
    int32_t fShowMessage;
    PDLG pdlg;

    cwod = waveOutGetNumDevs();
    if (cwod <= 0)
    {
        fShowMessage = fTrue;
        if (!FGetSetRegKey(kszWaveOutMsgValue, &fShowMessage, SIZEOF(fShowMessage), fregSetDefault | fregMachine))
        {
            Warn("Registry query failed");
        }
        if (fShowMessage)
        {
            // 3DMMv1.0: Put up an alert: no waveout
            pdlg = DLG::PdlgNew(dlidNoWaveOut, pvNil, pvNil);
            if (pvNil == pdlg)
                return fFalse;
            pdlg->IditDo();
            fShowMessage = !pdlg->FGetCheck(1); // 3DMMv1.0: 1 is ID of checkbox
            ReleasePpo(&pdlg);
            if (!fShowMessage)
            {
                // 3DMMv1.0: ignore failure
                FGetSetRegKey(kszWaveOutMsgValue, &fShowMessage, SIZEOF(fShowMessage), fregSetKey | fregMachine);
            }
        }
    }
    if (HWD_SUCCESS == wHaveWaveDevice(WAVE_FORMAT_2M08)) // 3DMMv1.0: 22kHz, Mono, 8bit is our minimum
    {
        if (wHaveACM())
        {
            // 3DMMv1.0: audio compression manager (sound mapper) not installed
            wInstallComp(IC_ACM);
            _fDontReportInitFailure = fTrue;
            return fFalse;
        }

        if (wHaveACMCodec(WAVE_FORMAT_ADPCM))
        {
            // 3DMMv1.0: audio codecs not installed
            wInstallComp(IC_ACM_ADPCM);
            _fDontReportInitFailure = fTrue;
            return fFalse;
        }
    } // 3DMMv1.0: have wave device

    cmod = midiOutGetNumDevs();
    if (cmod <= 0)
    {
        fShowMessage = fTrue;
        if (!FGetSetRegKey(kszMidiOutMsgValue, &fShowMessage, SIZEOF(fShowMessage), fregSetDefault | fregMachine))
        {
            Warn("Registry query failed");
        }
        if (fShowMessage)
        {
            // 3DMMv1.0: Put up an alert: no midiout
            pdlg = DLG::PdlgNew(dlidNoMidiOut, pvNil, pvNil);
            if (pvNil == pdlg)
                return fFalse;
            pdlg->IditDo();
            fShowMessage = !pdlg->FGetCheck(1); // 3DMMv1.0: 1 is ID of checkbox
            ReleasePpo(&pdlg);
            if (!fShowMessage)
            {
                // 3DMMv1.0: ignore failure
                FGetSetRegKey(kszMidiOutMsgValue, &fShowMessage, SIZEOF(fShowMessage), fregSetKey | fregMachine);
            }
        }
    }
#else  // 3DMMEx: !KAUAI_WIN32
    // 3DMMEx: TODO: Check if we can play audio
#endif // 3DMMEx: KAUAI_WIN32
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Notifies the user if there is no suitable video playback devices
***************************************************************************/
bool APP::_FEnsureVideo(void)
{
#ifdef KAUAI_WIN32
    if (wHaveMCI(PszLit("AVIVIDEO")))
    {
        // 3DMMv1.0: MCI for video is not installed
        wInstallComp(IC_MCI_VFW);
        _fDontReportInitFailure = fTrue;
        return fFalse;
    }

    if (HIC_SUCCESS != wHaveICMCodec(MS_VIDEO1))
    {
        // 3DMMv1.0:  video 1 codec not installed
        wInstallComp(IC_ICM_VIDEO1);
        _fDontReportInitFailure = fTrue;
        return fFalse;
    }
#else
// 3DMMEx: TODO: Check if we can play video
#endif

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Returns fTrue if color depth is >= 8 bits per pixel.  User is alerted
    if depth is less than 8, and function fails.  User is warned if depth
    is greater than 8, unless he has clicked the "don't show this messsage
    again" in the dialog before.
***************************************************************************/
bool APP::_FEnsureColorDepth(void)
{
    AssertBaseThis(0);

#ifdef WIN
    HDC hdc;
    int32_t cbitPixel;
    PDLG pdlg;
    bool fShowMessage;
    bool fDontShowAgain = fFalse;

    hdc = GetDC(NULL);
    if (NULL == hdc)
        return fFalse;
    cbitPixel = GetDeviceCaps(hdc, BITSPIXEL);
    ReleaseDC(NULL, hdc);

    if (cbitPixel < 8)
    {
        // 3DMMv1.0: Put up an alert: Not enough colors
        pdlg = DLG::PdlgNew(dlidNotEnoughColors, pvNil, pvNil);
        if (pvNil == pdlg)
            return fFalse;
        pdlg->IditDo();
        ReleasePpo(&pdlg);
        _fDontReportInitFailure = fTrue;
        return fFalse;
    }
    /* 3DMMEx: FOONE: It's the future, we don't need to worry about running in true-color mode.
    if (cbitPixel > 8)
        {
        // warn user
        fShowMessage = fTrue;
        if (!FGetSetRegKey(kszGreaterThan8bppMsgValue, &fShowMessage,
                SIZEOF(fShowMessage), fregSetDefault))
            {
            Warn("Registry query failed");
            }
        if (fShowMessage)
            {
            pdlg = DLG::PdlgNew(dlidTooManyColors, pvNil, pvNil);
            if (pvNil != pdlg)
                {
                pdlg->IditDo();
                fDontShowAgain = pdlg->FGetCheck(1); // 1 is ID of checkbox
                }
            ReleasePpo(&pdlg);
            if (fDontShowAgain)
                {
                fShowMessage = fFalse;
                FGetSetRegKey(kszGreaterThan8bppMsgValue, &fShowMessage,
                    SIZEOF(fShowMessage), fregSetKey); // ignore failure
                }
            }
        }
    */
#else  // 3DMMv1.0: !WIN
    // 3DMMEx: Assume colour depth >= 8-bit
#endif // 3DMMv1.0: WIN
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Dialog proc for Resolution Switch dialog.  Return fTrue (bring down
    the dialog) if kdtsMaxResSwitchDlg has passed since the dialog was
    created.
***************************************************************************/
bool _FDlgResSwitch(PDLG pdlg, int32_t *pidit, void *pv)
{
    AssertPo(pdlg, 0);
    AssertVarMem(pidit);
    AssertPvCb(pv, SIZEOF(int32_t));

    int32_t tsResize = *(int32_t *)pv;
    int32_t tsCur;

    if (*pidit != ivNil)
    {
        return fTrue; // 3DMMv1.0: Cancel or OK pressed
    }
    else
    {
        tsCur = TsCurrent();
        if (tsCur - tsResize >= kdtsMaxResSwitchDlg)
            return fTrue;
    }

    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Ensure that the screen is at the user's preferred resolution for 3DMM.
    If user has no registry preference, we offer to switch, try to switch,
    and save user's	preference.  Registry failures are non-fatal, but
    failing to create the main window causes this function to fail.

    When this function returns, _fRunInWindow is set correctly and the
    main app window *might* be created.
***************************************************************************/
bool APP::_FEnsureDisplayResolution(void)
{
    AssertBaseThis(0);

    PDLG pdlg;
    int32_t idit;
    int32_t fSwitchRes;
    bool fNoValue;
    int32_t tsResize;

    if (_fForceWindow)
    {
        _fRunInWindow = fTrue;
        return fTrue;
    }

    if (_FDisplayIs640480())
    {
        // 3DMMv1.0: System is already 640x480, so ignore registry and run fullscreen
        _fRunInWindow = fFalse;
        return fTrue;
    }

    if (!_FDisplaySwitchSupported())
    {
        // 3DMMv1.0: System can't switch res, so ignore registry and run in a window
        _fRunInWindow = fTrue;
        return fTrue;
    }

    // 3DMMv1.0: See if there's a res switch preference in the registry
    if (!FGetSetRegKey(kszSwitchResolutionValue, &fSwitchRes, SIZEOF(fSwitchRes), fregNil, &fNoValue))
    {
        // 3DMMv1.0: Registry error...just run in a window
        _fRunInWindow = fTrue;
        return fTrue;
    }

    if (!fNoValue)
    {
        // 3DMMv1.0: User has a preference
        if (!fSwitchRes)
        {
            _fRunInWindow = fTrue;
            return fTrue;
        }
        else // 3DMMv1.0: try to switch res
        {
            _fRunInWindow = fFalse;
            if (!_FInitOS())
            {
                // 3DMMv1.0: we're screwed
                return fFalse;
            }
            if (!_FSwitch640480(fTrue))
            {
                _fRunInWindow = fTrue;
                _RebuildMainWindow();
                goto LSwitchFailed;
            }
            return fTrue;
        }
    }

    // 3DMMv1.0: User doesn't have a preference yet.  Do the interactive thing.
#ifdef RES_SWITCH_DIALOGS
    pdlg = DLG::PdlgNew(dlidDesktopResizing, pvNil, pvNil);
    if (pvNil == pdlg)
        return fFalse;
    idit = pdlg->IditDo();
    ReleasePpo(&pdlg);
#else              //! 3DMMv1.0: RES_SWITCH_DIALOGS
    idit = 2; // 3DMMv1.0: OK
#endif             //! 3DMMv1.0: RES_SWITCH_DIALOGS
    if (idit == 1) // 3DMMv1.0: cancel
    {
        _fRunInWindow = fTrue;
        // 3DMMv1.0: try to set pref to fFalse
        fSwitchRes = fFalse;
        goto LWriteReg;
    }
    _fRunInWindow = fFalse;
    if (!_FInitOS())
        return fFalse;
    if (!_FSwitch640480(fTrue))
    {
        _fRunInWindow = fTrue;
        _RebuildMainWindow();
        goto LSwitchFailed;
    }

    tsResize = TsCurrent();
#ifdef RES_SWITCH_DIALOGS
    pdlg = DLG::PdlgNew(dlidDesktopResized, _FDlgResSwitch, &tsResize);
    idit = ivNil; // 3DMMv1.0: if dialog fails to come up, treat like a cancel
    if (pvNil != pdlg)
        idit = pdlg->IditDo();
    ReleasePpo(&pdlg);
#else                               //! 3DMMv1.0: RES_SWITCH_DIALOGS
    idit = 2; // 3DMMv1.0: OK
#endif                              //! 3DMMv1.0: RES_SWITCH_DIALOGS
    if (idit == 1 || idit == ivNil) // 3DMMv1.0: cancel or timeout or dialog failure
    {
        _fSwitchedResolution = fFalse;
        _fRunInWindow = fTrue;
        _RebuildMainWindow();
        if (!_FSwitch640480(fFalse)) // 3DMMv1.0: restore desktop resolution
            return fFalse;
        goto LSwitchFailed;
    }
    // 3DMMv1.0: try to set pref to fTrue
    fSwitchRes = fTrue;
    goto LWriteReg;

LSwitchFailed:
    pdlg = DLG::PdlgNew(dlidDesktopWontResize, pvNil, pvNil);
    if (pvNil != pdlg)
        pdlg->IditDo();
    ReleasePpo(&pdlg);
    // 3DMMv1.0: try to set pref to fFalse
    fSwitchRes = fFalse;
LWriteReg:
    FGetSetRegKey(kszSwitchResolutionValue, &fSwitchRes, SIZEOF(fSwitchRes), fregSetKey);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Return whether display is currently 640x480
***************************************************************************/
bool APP::_FDisplayIs640480(void)
{
#ifdef WIN
    return (GetSystemMetrics(SM_CXSCREEN) == 640 && GetSystemMetrics(SM_CYSCREEN) == 480);
#else  // 3DMMv1.0: !WIN
    int w, h;

    if (vwig.hwndApp == pvNil)
        return fFalse;

    SDL_GetWindowSize(vwig.hwndApp, &w, &h);

    // 3DMMEx: This is actually correct, since on Windows the screen upscaling is done by switching
    // 3DMMEx: the video controller into 640x480 mode to fill the screen.
    return (w != kdxpLogical && h != kdypLogical);
#endif // 3DMMv1.0: WIN
}

/** 3DMMv1.0: *************************************************************************
    Do OS specific initialization.
***************************************************************************/
bool APP::_FInitOS(void)
{
    AssertBaseThis(0);

    if (_fMainWindowCreated) // 3DMMv1.0: If someone else called _FInitOS already,
        return fTrue;        // 3DMMv1.0: we can leave

#if defined(KAUAI_SDL)

    if (!(FPure(APP_PAR::_FInitOS())))
    {
        return fFalse;
    }

    _fMainWindowCreated = fTrue;

#elif defined(KAUAI_WIN32)
    int32_t dxpWindow;
    int32_t dypWindow;
    int32_t xpWindow;
    int32_t ypWindow;
    uint32_t dwStyle = 0;
    STN stnWindowTitle;

    if (!FGetStnApp(idsWindowTitle, &stnWindowTitle))
        return fFalse;

    // 4DMM owns the primary frame caption.  Keep the historical string
    // resource untouched for compatibility/help text, but present the new
    // project identity in the actual top-level window.
    stnWindowTitle.SetSz(PszLit("4DMM"));

    // 3DMMv1.0: register the window classes
    if (vwig.hinstPrev == hNil)
    {
        WNDCLASS wcs;

        wcs.style = CS_BYTEALIGNCLIENT | CS_OWNDC;
        wcs.lpfnWndProc = _LuWndProc;
        wcs.cbClsExtra = 0;
        wcs.cbWndExtra = 0;
        wcs.hInstance = vwig.hinst;
        wcs.hIcon = LoadIcon(vwig.hinst, MAKEINTRESOURCE(IDI_APP));
        wcs.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcs.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wcs.lpszMenuName = 0;
        wcs.lpszClassName = kpszAppWndCls;
        if (!RegisterClass(&wcs))
            return fFalse;

        wcs.lpfnWndProc = _LuMdiWndProc;
        wcs.lpszClassName = PszLit("MDI");
        if (!RegisterClass(&wcs))
            return fFalse;
    }

    _GetWindowProps(&xpWindow, &ypWindow, &dxpWindow, &dypWindow, &dwStyle);

    if ((vwig.hwndApp = CreateWindow(kpszAppWndCls, stnWindowTitle.Psz(), dwStyle, xpWindow, ypWindow, dxpWindow,
                                     dypWindow, hNil, hNil, vwig.hinst, pvNil)) == hNil)
    {
        return fFalse;
    }

    if (hNil == (vwig.hdcApp = GetDC(vwig.hwndApp)))
        return fFalse;

    // 3DMMv1.0: set a timer, so we can idle regularly.
    if (SetTimer(vwig.hwndApp, 0, 1, pvNil) == 0)
        return fFalse;

    ShowWindow(vwig.hwndApp, vwig.wShow);
    _fMainWindowCreated = fTrue;
    if (vfViewportResolution4x && !FEnsure4DMMUiScaleWindow())
        MODERN_BR_LOG("scaled UI presentation deferred during APP::_FInitOS");
#else
    RawRtn();
#endif

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Determine window size, position, and window style.  Note that you
    should pass in the current dwStyle if the window already exists.  If
    the window does not already exist, pass in 0.
***************************************************************************/
void APP::_GetWindowProps(int32_t *pxp, int32_t *pyp, int32_t *pdxp, int32_t *pdyp, uint32_t *pdwStyle)
{
    AssertBaseThis(0);
    AssertVarMem(pxp);
    AssertVarMem(pyp);
    AssertVarMem(pdxp);
    AssertVarMem(pdyp);
    AssertVarMem(pdwStyle);

#if defined(KAUAI_WIN32)

    if (!_fRunInWindow)
    {
        *pdwStyle |= (WS_POPUP | WS_CLIPCHILDREN);
        *pdwStyle &= ~(WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
        *pxp = 0;
        *pyp = 0;
        *pdxp = GetSystemMetrics(SM_CXSCREEN);
        *pdyp = GetSystemMetrics(SM_CYSCREEN);
    }
    else
    {
        RECT rcs;
        *pdwStyle |= (WS_OVERLAPPED | WS_CLIPCHILDREN | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
        *pdwStyle &= ~WS_POPUP;
        rcs.left = 0;
        rcs.top = 0;
        rcs.right = 640;
        rcs.bottom = 480;
        AdjustWindowRect(&rcs, *pdwStyle, fFalse);
        *pdxp = rcs.right - rcs.left;
        *pdyp = rcs.bottom - rcs.top;
        // 3DMMv1.0: Center window on screen
        // 3DMMv1.0: REVIEW *****: how do you adjust window appropriately for taskbar?
        *pxp = LwMax((GetSystemMetrics(SM_CXSCREEN) - *pdxp) / 2, 0);
        *pyp = LwMax((GetSystemMetrics(SM_CYSCREEN) - *pdyp) / 2, 0);
    }

#else
    *pxp = 0;
    *pyp = 0;
    *pdxp = 640;
    *pdyp = 480;
    *pdwStyle = 0;
#endif
}

/** 3DMMv1.0: *************************************************************************
    Change main window properties based on whether we're running in a
    window or fullscreen.
***************************************************************************/
void APP::_RebuildMainWindow(void)
{
    AssertBaseThis(0);
    Assert(_fMainWindowCreated, 0);

#if defined(KAUAI_WIN32)
    int32_t dxpWindow;
    int32_t dypWindow;
    int32_t xpWindow;
    int32_t ypWindow;
    uint32_t dwStyle;

    dwStyle = GetWindowLong(vwig.hwndApp, GWL_STYLE);
    _GetWindowProps(&xpWindow, &ypWindow, &dxpWindow, &dypWindow, &dwStyle);
    SetWindowLong(vwig.hwndApp, GWL_STYLE, dwStyle);
    SetWindowPos(vwig.hwndApp, HWND_TOP, xpWindow, ypWindow, dxpWindow, dypWindow, 0);
#endif // 3DMMEx: KAUAI_WIN32
}

/** 3DMMv1.0: *************************************************************************
    Open "3D Movie Maker.chk" or "3DMovie.chk"
***************************************************************************/
bool APP::_FOpenResourceFile(void)
{
    AssertBaseThis(0);
    AssertPo(&_fniProductDir, ffniDir);

    FNI fni;
    STN stn;

    fni = _fniProductDir;
    if (!fni.FSetLeaf(&_stnProductLong, kftgChunky) || tYes != fni.TExists())
    {
        fni = _fniProductDir;
        if (!fni.FSetLeaf(&_stnProductShort, kftgChunky) || tYes != fni.TExists())
        {
            fni.GetStnPath(&stn);
            _FCantFindFileDialog(&stn); // 3DMMv1.0: ignore failure
            return fFalse;
        }
    }
    _pcfl = CFL::PcflOpen(&fni, fcflNil);
    if (pvNil == _pcfl)
        return fFalse;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Report that 3DMM can't find a file named pstnFile
***************************************************************************/
bool APP::_FCantFindFileDialog(PSTN pstnFile)
{
    AssertBaseThis(0);
    AssertPo(pstnFile, 0);

    PDLG pdlg;

    pdlg = DLG::PdlgNew(dlidCantFindFile, pvNil, pvNil);
    if (pvNil == pdlg)
        return fFalse;

    if (!pdlg->FPutStn(1, pstnFile))
    {
        ReleasePpo(&pdlg);
        return fFalse;
    }
    pdlg->IditDo();
    ReleasePpo(&pdlg);
    _fDontReportInitFailure = fTrue;
    return fTrue;
}

/** 3DMMEx: *************************************************************************
    Report that 3DMM ran into a generic error
***************************************************************************/
bool APP::_FGenericError(FNI *path)
{
    STN stn;
    path->GetStnPath(&stn);
    return _FGenericError(&stn);
}

/** 3DMMEx: *************************************************************************
    Report that 3DMM ran into a generic error
***************************************************************************/
bool APP::_FGenericError(PCSZ message)
{
    STN stn;
    stn.SetSz(message);
    return _FGenericError(&stn);
}
/** 3DMMEx: *************************************************************************
    Report that 3DMM ran into a generic error
***************************************************************************/
bool APP::_FGenericError(PSTN message)
{
    AssertBaseThis(0);

    PDLG pdlg;

    pdlg = DLG::PdlgNew(dlidGenericErrorBox, pvNil, pvNil);
    if (pvNil == pdlg)
        return fFalse;

    if (!pdlg->FPutStn(1, message))
    {
        ReleasePpo(&pdlg);
        return fFalse;
    }
    pdlg->IditDo();
    ReleasePpo(&pdlg);
    _fDontReportInitFailure = fTrue;
    return fTrue;
}

/** 3DMMv1.0: ****************************************************************************
    Finds _fniUsersDir, _fniMelanieDir, and _fniUserDir.  _fniUserDir is
    set from the registry.  This function also determines if this is a first
    time user.
************************************************************ PETED ***********/
bool APP::_FGetUserDirectories(void)
{
    AssertBaseThis(0);
    AssertPo(&_fniMsKidsDir, ffniDir);
    Assert(_stnUser.Cch() > 0, "need valid stnUser!");

    SZ szDir;
    STN stn;
    STN stnT;
    STN stnUsers;
    bool fFirstTimeUser;

    // 3DMMv1.0: First, find the Users directory
    _fniUsersDir = _fniMsKidsDir;
    if (!FGetStnApp(idsUsersDir, &stnUsers))
        return fFalse;
    if (!_fniUsersDir.FDownDir(&stnUsers, ffniMoveToDir))
    {
        _fniUsersDir.GetStnPath(&stn);
        if (!stn.FAppendStn(&stnUsers))
            return fFalse;
        _FCantFindFileDialog(&stn); // 3DMMv1.0: ignore failure
        return fFalse;
    }
    AssertPo(&_fniUsersDir, ffniDir);

    // 3DMMv1.0: Find Melanie's dir
    _fniMelanieDir = _fniUsersDir;
    if (!FGetStnApp(idsMelanie, &stn))
        return fFalse;
    if (!_fniMelanieDir.FDownDir(&stn, ffniMoveToDir))
    {
        _fniMelanieDir.GetStnPath(&stnT);
        if (!stnT.FAppendStn(&stn))
            return fFalse;
        _FCantFindFileDialog(&stnT); // 3DMMv1.0: ignore failure
        return fFalse;
    }
    AssertPo(&_fniMelanieDir, ffniDir);

    // 3DMMEx: Get the user's home directory from settings
    szDir[0] = chNil;
    if (FGetSetRegKey(kszHomeDirValue, szDir, SIZEOF(szDir), fregString))
    {
        fFirstTimeUser = fFalse;
    }
    else
    {
        fFirstTimeUser = fTrue;
    }

    stn.SetSz(szDir);
    if (stn.Cch() == 0 || !_fniUserDir.FBuildFromPath(&stn, kftgDir) || tYes != _fniUserDir.TExists())
    {
        // 3DMMEx: Find the user's documents directory
        szDir[0] = chNil;
        AssertDo(FGetDocumentsDir(szDir, SIZEOF(szDir)), "Could not get documents directory");
        stn.SetSz(szDir);

        if (!_fniUserDir.FBuildFromPath(&stn, kftgDir) || tYes != _fniUserDir.TExists())
        {
            Bug("Documents directory is invalid");
            return fFalse;
        }

        // 3DMMv1.0: Try to write path to user dir to the registry
        _fniUserDir.GetStnPath(&stn);
        stn.GetSz(szDir);
        FGetSetRegKey(kszHomeDirValue, szDir, CchSz(szDir) + kcchExtraSz,
                      fregSetKey | fregString); // 3DMMv1.0: ignore failure
    }
#ifdef WIN
    if (SetCurrentDirectory(szDir) == FALSE)
        return fFalse;
#endif // 3DMMv1.0: WIN
    AssertPo(&_fniUserDir, ffniDir);

    if (!FSetProp(kpridFirstTimeUser, fFirstTimeUser))
        return fFalse;
    return fTrue;
}

/** 3DMMv1.0: ****************************************************************************
    _FGetUserName
        Attempts to get the current user name.  May return a default value
        if we get a "non-serious" error (as defined in the spec).

    Arguments:
        None

    Returns:  fTrue if _stnUser has something usable in it on return

************************************************************ PETED ***********/
bool APP::_FGetUserName(void)
{
    AssertBaseThis(0);

#ifdef WIN
    bool fRet = fTrue;
    SZ szT;
    DWORD cchUser = CvFromRgv(szT);

    switch (WNetGetUser(NULL, szT, &cchUser))
    {
    case ERROR_EXTENDED_ERROR: {
        DWORD dwError;
        SZ szProvider;
        STN stnMessage;

        if (WNetGetLastError(&dwError, szT, CvFromRgv(szT), szProvider, CvFromRgv(szProvider)) == NO_ERROR)
        {
            STN stnFormat;

            if (FGetStnApp(idsWNetError, &stnFormat))
            {
                stnMessage.FFormat(&stnFormat, szProvider, dwError, szT);
                TGiveAlertSz(stnMessage.Psz(), bkOk, cokExclamation);
            }
        }
        else
            Bug("Call to WNetGetLastError failed; this should never happen");
        _stnUser.SetNil();
        fRet = fFalse;
        break;
    }

    case NO_ERROR:
        _stnUser = szT;
        if (_stnUser.Cch() > 0)
            break; // 3DMMv1.0: else fall through...

    case ERROR_MORE_DATA:
    case ERROR_NO_NETWORK:
    case ERROR_NO_NET_OR_BAD_PATH:
    default:
        if (!FGetStnApp(idsDefaultUser, &_stnUser))
            fRet = fFalse;
        break;
    }

    Assert(!fRet || _stnUser.Cch() > 0, "Bug in _FGetUserName");
    return fRet;
#else  // 3DMMv1.0: WIN
    char username[kcchMaxSz];

    if (GetUserName(username, kcchMaxSz))
        _stnUser.SetSz(username);
    else
        _stnUser = PszLit("User"); // 3DMMEx: Set a default user name

    Assert(_stnUser.Cch() > 0, "Bug in _FGetUserName");
    return fTrue;
#endif // 3DMMv1.0: !WIN
}

/** 3DMMv1.0: ****************************
    The user-data structure
*******************************/
struct UDAT
{
    int32_t rglw[kcpridUserData];
};

/** 3DMMv1.0: *************************************************************************
    Reads the "user data" from the registry and SetProp's the data onto
    the app
***************************************************************************/
bool APP::_FReadUserData(void)
{
    AssertBaseThis(0);

    UDAT udat;
    int32_t iprid;
    int32_t fEnableFeature;

    ClearPb(&udat, SIZEOF(UDAT));

    if (!FGetSetRegKey(kszUserDataValue, &udat, SIZEOF(UDAT), fregSetDefault | fregBinary))
    {
        return fFalse;
    }
    for (iprid = 0; iprid < kcpridUserData; iprid++)
    {
        if (!FSetProp(kpridUserDataBase + iprid, udat.rglw[iprid]))
            return fFalse;
    }

    // 3DMMEx: Read feature flags
    fEnableFeature = kszHighQualitySoundImportDefault;
    (void)FGetSetRegKey(kszHighQualitySoundImport, &fEnableFeature, SIZEOF(fEnableFeature), fregNil);
    FSetProp(kpridHighQualitySoundImport, fEnableFeature);

    fEnableFeature = kszStereoSoundDefault;
    (void)FGetSetRegKey(kszStereoSound, &fEnableFeature, SIZEOF(fEnableFeature), fregNil);
    FSetProp(kpridStereoSoundPlayback, fEnableFeature);

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Writes the "user data" from the app props to the registry
***************************************************************************/
bool APP::_FWriteUserData(void)
{
    AssertBaseThis(0);

    UDAT udat;
    int32_t iprid;

    for (iprid = 0; iprid < kcpridUserData; iprid++)
    {
        if (!FGetProp(kpridUserDataBase + iprid, &udat.rglw[iprid]))
            return fFalse;
    }
    if (!FGetSetRegKey(kszUserDataValue, &udat, SIZEOF(UDAT), fregSetKey | fregBinary))
    {
        return fFalse;
    }

    return fTrue;
}

/** 3DMMEx: ****************************************************************************
    FGetSetRegKey
        Given a reg key (and option sub-key), attempts to either get or set
        the current value of the key.  If the reg key is created on a Get
        (fSetKey == fFalse), the data in pvData will be used to set the
        default value for the key.


    Arguments:
        PSZ pszValueName  --  The value name
        void *pvData      --  pointer to buffer to read or write key into or from
        long cbData       --  size of the buffer
        uint32_t grfreg   --  flags describing what we should do
        bool *pfNoValue  --  optional parameter, takes whether a real registry
                              error occurred or not

    Returns:  fTrue if all actions necessary could be performed

************************************************************ PETED ***********/
bool APP::FGetSetRegKey(PCSZ pszValueName, void *pvData, int32_t cbData, uint32_t grfreg, bool *pfNoValue)
{
    AssertBaseThis(0);
    AssertSz(pszValueName);
    AssertPvCb(pvData, cbData);
    AssertNilOrVarMem(pfNoValue);

    return ::FGetSetRegKey(pszValueName, pvData, cbData, grfreg, pfNoValue);
}

/** 3DMMv1.0: *************************************************************************
    Set the palette and bring up the Microsoft Home Logo
***************************************************************************/
bool APP::_FDisplayHomeLogo(void)
{
    AssertBaseThis(0);
    AssertPo(_pcfl, 0);

    BLCK blck;
    PGL pglclr;
    PMBMP pmbmp;
    int16_t bo;
    int16_t osk;

    if (!_pcfl->FFind(kctgColorTable, kcnoGlcrInit, &blck))
        return fFalse;
    pglclr = GL::PglRead(&blck, &bo, &osk);
    if (pvNil == pglclr)
        return fFalse;
    GPT::SetActiveColors(pglclr, fpalIdentity);
    ReleasePpo(&pglclr);

    if (!_pcfl->FFind(kctgMbmp, kcnoMbmpHomeLogo, &blck))
        return fFalse;
    pmbmp = MBMP::PmbmpRead(&blck);
    if (pvNil == pmbmp)
        return fFalse;
    _pkwa->SetMbmp(pmbmp);
    ReleasePpo(&pmbmp);
    UpdateMarked();
    return fTrue;
}

/***************************************************************************
    Set the initial 3DMM color table without displaying the Home Logo.
    This is used by direct-editor/playback-only startup modes.
***************************************************************************/
bool APP::_FSetInitialPalette(void)
{
    AssertBaseThis(0);
    AssertPo(_pcfl, 0);

    BLCK blck;
    PGL pglclr;
    int16_t bo;
    int16_t osk;

    if (!_pcfl->FFind(kctgColorTable, kcnoGlcrInit, &blck))
        return fFalse;
    pglclr = GL::PglRead(&blck, &bo, &osk);
    if (pvNil == pglclr)
        return fFalse;
    GPT::SetActiveColors(pglclr, fpalIdentity);
    ReleasePpo(&pglclr);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Initialize tag manager
***************************************************************************/
bool APP::_FInitProductNames(void)
{
    AssertBaseThis(0);

    PGST pgst;
    BLCK blck;

    vptagm = TAGM::PtagmNew(&_fniMsKidsDir, APP::FInsertCD, kcbCacheTagm);
    if (vptagm == pvNil)
    {
        _FGenericError(PszLit("_FInitProductNames: TAGM::PtagmNew"));
        return fFalse;
    }

    if (!_FReadTitlesFromReg(&pgst))
    {
        _FGenericError(PszLit("_FInitProductNames: _FReadTitlesFromReg"));
        goto LFail;
    }

    if (!vptagm->FMergeGstSource(pgst, kboCur, koskCur))
    {
        _FGenericError(PszLit("_FInitProductNames: FMergeGstSource"));
        goto LFail;
    }

    if (!_FFindProductDir(pgst))
    {
        _FGenericError(PszLit("_FInitProductNames: _FFindProductDir"));
        goto LFail;
    }

    if (!vptagm->FGetSid(&_stnProductLong, &_sidProduct))
    {
        _FGenericError(PszLit("_FInitProductNames: FGetSid"));
        goto LFail;
    }

    ReleasePpo(&pgst);
    return fTrue;

LFail:
    ReleasePpo(&pgst);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Read the sids and titles of installed 3DMovie products from the registry
***************************************************************************/
bool APP::_FReadTitlesFromReg(PGST *ppgst)
{
    AssertBaseThis(0);
    AssertVarMem(ppgst);

    SZ szSid;
    STN stnSid;

    SZ szTitle;
    STN stnTitle;
    PGST pgst;
    int32_t sid;

    // 3DMMEx: List of expansion pack titles to configure
    const struct ExpansionTitles_t
    {
        int32_t sid;
        PCSZ pszTitle;
    } vrgExpansionTitles[] = {
        // 3DMMEx: Source IDs were chosen by the developers of the expansion packs
        {3, PszLit("N3DMM EXPANSION/N3DMMExp")},        // 3DMMEx: Nickelodeon 3D Movie Maker expansion pack (unofficial)
        {4, PszLit("Doraemon Character Kit/DORAEMON")}, // 3DMMEx: Doraemon Character Kit
        {5, PszLit("Expansions/EXPANSIONS")},           // 3DMMEx: 3DMM Expansion Pack (unofficial)
    };

    if ((pgst = GST::PgstNew(SIZEOF(int32_t))) == pvNil)
        goto LFail;

#ifdef WIN
    HKEY hkey;
    DWORD dwDisposition;
    DWORD iValue;
    DWORD cchSid;
    DWORD cchTitle;

    cchSid = kcchMaxSz;
    cchTitle = kcchMaxSz;

    if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, kszProductsKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ, NULL, &hkey,
                       &dwDisposition) == ERROR_SUCCESS)
    {
        for (iValue = 0; RegEnumValue(hkey, iValue, szSid, &cchSid, NULL, NULL, (unsigned char *)szTitle, &cchTitle) !=
                         ERROR_NO_MORE_ITEMS;
             iValue++)
        {
            stnSid.SetSz(szSid);
            if (!stnSid.FGetLw(&sid))
            {
                Warn("Invalid registry name for Products key value");
                continue;
            }
            stnTitle.SetSz(&szTitle[0]);
            if (!pgst->FAddStn(&stnTitle, &sid))
                goto LFail;
            cchTitle = kcchMaxSz;
            cchSid = kcchMaxSz;
        }

        (void)RegCloseKey(hkey);
    }
    else
    {
        Warn("_FReadTitlesFromReg: Could not open products key");
    }

#endif // 3DMMv1.0: WIN

    // 3DMMEx: Add main title if not already set
    if (pgst->IvMac() == 0)
    {
        stnTitle.SetSz(PszLit("3D Movie Maker/3DMovie"));
        sid = 2;
        if (!pgst->FAddStn(&stnTitle, &sid))
        {
            Warn("Failed to add fallback Title!");
            goto LFail;
        }
    }

    // 3DMMEx: Add optional expansion pack titles
    for (int32_t ititle = 0; ititle < CvFromRgv(vrgExpansionTitles); ititle++)
    {
        stnTitle.SetSz(vrgExpansionTitles[ititle].pszTitle);
        sid = vrgExpansionTitles[ititle].sid;
        if (!pgst->FFindExtra(&sid))
        {
            if (!pgst->FAddStn(&stnTitle, &sid))
            {
                Warn("Failed to add expansion title");
                goto LFail;
            }
        }
    }

    *ppgst = pgst;
    return fTrue;

LFail:
    ReleasePpo(&pgst);
    *ppgst = pvNil;
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Initialize 3-D Text
***************************************************************************/
bool APP::_FInitTdt(void)
{
    AssertBaseThis(0);
    AssertPo(_pcfl, 0);

    PGST pgst;

    // 3DMMv1.0: set TDT action names
    pgst = _PgstRead(kcnoGstAction);
    if (pvNil == pgst)
        return fFalse;
    Assert(pgst->CbExtra() == SIZEOF(int32_t), "bad Action string table");
    if (!TDT::FSetActionNames(pgst))
    {
        ReleasePpo(&pgst);
        return fFalse;
    }
    ReleasePpo(&pgst);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Read and byte-swap a GST from _pcfl.  Assumes that extra data, if any,
    is a long.
***************************************************************************/
PGST APP::_PgstRead(CNO cno)
{
    AssertBaseThis(0);
    AssertPo(_pcfl, 0);

    PGST pgst;
    BLCK blck;
    int16_t bo;
    int16_t osk;
    int32_t istn;
    int32_t lwExtra;

    if (!_pcfl->FFind(kctgGst, cno, &blck))
        return pvNil;
    pgst = GST::PgstRead(&blck, &bo, &osk);
    if (pvNil == pgst)
        return pvNil;
    Assert(pgst->CbExtra() == 0 || pgst->CbExtra() == SIZEOF(int32_t), "unexpected extra size");
    if (kboCur != bo && 0 != pgst->CbExtra())
    {
        for (istn = 0; istn < pgst->IvMac(); istn++)
        {
            pgst->GetExtra(istn, &lwExtra);
            SwapBytesRglw(&lwExtra, 1);
            pgst->PutExtra(istn, &lwExtra);
        }
    }
    return pgst;
}

/** 3DMMv1.0: *************************************************************************
    Read various string tables from _pcfl
***************************************************************************/
bool APP::_FReadStringTables(void)
{
    AssertBaseThis(0);
    AssertPo(_pcfl, 0);

    // 3DMMv1.0: read studio filename list
    _pgstStudioFiles = _PgstRead(kcnoGstStudioFiles);
    if (pvNil == _pgstStudioFiles)
        return fFalse;

    // 3DMMv1.0: read building filename list
    _pgstBuildingFiles = _PgstRead(kcnoGstBuildingFiles);
    if (pvNil == _pgstBuildingFiles)
        return fFalse;

    // 3DMMv1.0: read shared filename list
    _pgstSharedFiles = _PgstRead(kcnoGstSharedFiles);
    if (pvNil == _pgstSharedFiles)
        return fFalse;

    // 3DMMv1.0: read misc app strings
    _pgstApp = _PgstRead(kcnoGstApp);
    if (pvNil == _pgstApp)
        return fFalse;

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Initialize KWA (app kidworld)
***************************************************************************/
bool APP::_FInitKidworld(void)
{
    AssertBaseThis(0);

    RC rcRel(0, 0, krelOne, krelOne);
    GCB gcb(CMH::HidUnique(), GOB::PgobScreen(), fgobNil, kginMark, pvNil, &rcRel);

    _pkwa = NewObj KWA(&gcb);
    if (pvNil == _pkwa)
        return fFalse;

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Show the app splash screen
***************************************************************************/
bool APP::_FShowSplashScreen(void)
{
    AssertBaseThis(0);

    BLCK blck;
    PMBMP pmbmp;

    if (!_pcfl->FFind(kctgMbmp, kcnoMbmpSplash, &blck))
        return fFalse;
    pmbmp = MBMP::PmbmpRead(&blck);
    if (pvNil == pmbmp)
        return fFalse;
    _pkwa->SetMbmp(pmbmp);
    ReleasePpo(&pmbmp);
    UpdateMarked();
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Play the app splash sound
***************************************************************************/
bool APP::_FPlaySplashSound(void)
{
    AssertBaseThis(0);

    PCRF pcrf;

    pcrf = CRF::PcrfNew(_pcfl, 0);
    if (pvNil == pcrf)
        return fFalse;
    vpsndm->SiiPlay(pcrf, kctgMidi, kcnoMidiSplash);
    ReleasePpo(&pcrf);

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Read the chunky files specified by _pgstSharedFiles, _pgstBuildingFiles
    and _pgstStudioFiles and create the global CRM; indices to the Building
    and Studio CRFs are stored in _pglicrfBuilding and _pglicrfStudio.
***************************************************************************/
bool APP::_FInitCrm(void)
{
    AssertBaseThis(0);
    AssertPo(_pkwa, 0);
    AssertPo(_pgstSharedFiles, 0);
    AssertPo(_pgstBuildingFiles, 0);
    AssertPo(_pgstStudioFiles, 0);

    PSCEG psceg = pvNil;
    PSCPT pscpt = pvNil;

    _pcrmAll = CRM::PcrmNew(_pgstSharedFiles->IvMac() + _pgstBuildingFiles->IvMac() + _pgstStudioFiles->IvMac());
    if (pvNil == _pcrmAll)
        goto LFail;

    _pglicrfBuilding = GL::PglNew(SIZEOF(int32_t), _pgstBuildingFiles->IvMac());
    if (pvNil == _pglicrfBuilding)
        goto LFail;

    _pglicrfStudio = GL::PglNew(SIZEOF(int32_t), _pgstStudioFiles->IvMac());
    if (pvNil == _pglicrfStudio)
        goto LFail;

    if (!_FAddToCrm(_pgstSharedFiles, _pcrmAll, pvNil))
        goto LFail;

    if (!_FAddToCrm(_pgstBuildingFiles, _pcrmAll, _pglicrfBuilding))
        goto LFail;

    if (!_FAddToCrm(_pgstStudioFiles, _pcrmAll, _pglicrfStudio))
        goto LFail;

    // 3DMMv1.0: Initialize the shared util gob.
    psceg = _pkwa->PscegNew(_pcrmAll, _pkwa);
    if (pvNil == psceg)
        goto LFail;

    pscpt = (PSCPT)_pcrmAll->PbacoFetch(kctgScript, kcnoInitShared, SCPT::FReadScript);
    if (pvNil == pscpt)
        goto LFail;

    if (!psceg->FRunScript(pscpt))
        goto LFail;

    ReleasePpo(&psceg);
    ReleasePpo(&pscpt);
    return fTrue;
LFail:
    ReleasePpo(&psceg);
    ReleasePpo(&pscpt);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Helper function for _FInitCrm.  Adds the list of chunky files specified
    in pgstFiles to the CRM pointed to by pcrm.  If pglFiles is not pvNil,
    it is filled in with the positions in the CRM of each of the loaded
    crfs.
***************************************************************************/
bool APP::_FAddToCrm(PGST pgstFiles, PCRM pcrm, PGL pglFiles)
{
    AssertBaseThis(0);
    AssertPo(&_fniProductDir, ffniDir);
    AssertPo(pgstFiles, 0);
    AssertPo(pcrm, 0);
    AssertNilOrPo(pglFiles, 0);

    bool fRet = fFalse;
    FNI fni;
    STN stn;
    int32_t istn;
    int32_t cbCache;
    PCFL pcfl = pvNil;
    int32_t icfl;

    for (istn = 0; istn < pgstFiles->IvMac(); istn++)
    {
        pgstFiles->GetStn(istn, &stn);
        pgstFiles->GetExtra(istn, &cbCache);
#ifdef DEBUG
        {
            bool fAskForCDSav = Pkwa()->FAskForCD();
            bool fFoundFile;

            // 3DMMv1.0: In debug, we look for "buildingd.chk", "sharedd.chk", etc. and
            // 3DMMv1.0: use them instead of the normal files if they exist.  If they
            // 3DMMv1.0: don't exist, just use the normal files.
            STN stnT = stn;
            stnT.FAppendCh(ChLit('d'));
            stnT.FAppendSz(PszLit(".chk")); // 3DMMv1.0: REVIEW *****
            Pkwa()->SetCDPrompt(fFalse);
            fFoundFile = Pkwa()->FFindFile(&stnT, &fni);
            Pkwa()->SetCDPrompt(fAskForCDSav);
            if (fFoundFile)
            {
                pcfl = CFL::PcflOpen(&fni, fcflNil);
            }
            else
            {
#endif                                         // 3DMMv1.0: DEBUG
                stn.FAppendSz(PszLit(".chk")); // 3DMMv1.0: REVIEW *****
                if (Pkwa()->FFindFile(&stn, &fni))
                    pcfl = CFL::PcflOpen(&fni, fcflNil);
#ifdef DEBUG
            }
        }
#endif // 3DMMv1.0: DEBUG
        if (pvNil == pcfl)
        {
            if (fni.Ftg() != ftgNil)
                fni.GetStnPath(&stn);
            if (!_fDontReportInitFailure)
                _FCantFindFileDialog(&stn); // 3DMMv1.0: ignore failure
            goto LFail;
        }
        if (!pcrm->FAddCfl(pcfl, cbCache, &icfl))
            goto LFail;
        ReleasePpo(&pcfl);
        if (pglFiles != pvNil && !pglFiles->FAdd(&icfl))
            goto LFail;
    }

    fRet = fTrue;
LFail:
    if (!fRet)
        ReleasePpo(&pcfl);
    return fRet;
}

/** 3DMMv1.0: *************************************************************************
    Initialize and start the building script
***************************************************************************/
bool APP::_FInitBuilding(void)
{
    AssertBaseThis(0);

    bool fRet = fFalse;
    int32_t i;
    int32_t cbCache;
    int32_t iv;
    PCRF pcrfT;
    PSCEG psceg = pvNil;
    PSCPT pscpt = pvNil;

    BeginLongOp();

    psceg = _pkwa->PscegNew(_pcrmAll, _pkwa);
    if (pvNil == psceg)
        goto LFail;

    pscpt = (PSCPT)_pcrmAll->PbacoFetch(kctgScript, kcnoStartApp, SCPT::FReadScript);
    if (pvNil == pscpt)
        goto LFail;

    if (!psceg->FRunScript(pscpt))
        goto LFail;

    // 3DMMv1.0: Up the cache limits of the building crfs in the global crm.
    // 3DMMv1.0: Assumption: since the files were added to the crm in the order they
    // 3DMMv1.0: appear in _pgstBuilding, I can get the cache amounts from there.
    for (i = 0; i < _pglicrfBuilding->IvMac(); i++)
    {
        _pgstBuildingFiles->GetExtra(i, &cbCache);
        _pglicrfBuilding->Get(i, &iv);
        pcrfT = _pcrmAll->PcrfGet(iv);
        Assert(pcrfT != pvNil, "Main CRM is corrupt.");
        pcrfT->SetCbMax(cbCache);
    }

    // 3DMMv1.0: Zero the cache for the studio crfs in the global crm.
    for (i = 0; i < _pglicrfStudio->IvMac(); i++)
    {
        _pglicrfStudio->Get(i, &iv);
        pcrfT = _pcrmAll->PcrfGet(iv);
        Assert(pcrfT != pvNil, "Main CRM is corrupt.");
        pcrfT->SetCbMax(0);
    }

    fRet = fTrue;
LFail:
    if (!fRet)
        EndLongOp();
    ReleasePpo(&psceg);
    ReleasePpo(&pscpt);
    return fRet;
}

/** 3DMMv1.0: *************************************************************************
    Initialize and start the studio script
***************************************************************************/
bool APP::_FInitStudio(PFNI pfniUserDoc, bool fFailIfDocOpenFailed)
{
    AssertBaseThis(0);

    int32_t i;
    int32_t cbCache;
    int32_t iv;
    PCRF pcrfT;
    bool fRet = fFalse;

    _pstdio = STDIO::PstdioNew(khidStudio, _pcrmAll, (pfniUserDoc->Ftg() == ftgNil ? pvNil : pfniUserDoc),
                               fFailIfDocOpenFailed);
    if (_pstdio == pvNil)
    {
        goto LFail;
    }

    // Apply -u again after Studio has finished constructing its movie.  This
    // is deliberately redundant with MVIE's constructor so a stale or early
    // movie object cannot silently fall back to the original one-level stack.
    if (_fUndoHistory && _pstdio->Pmvie() != pvNil)
    {
        _pstdio->Pmvie()->SetCundbMax(50);
        _pstdio->Pmvie()->EnableUndoHistoryWindow();
        _pstdio->Pmvie()->ShowUndoHistoryWindow();
#ifdef KAUAI_WIN32
        _LogUndoHistoryStartup("APP::_FInitStudio applied 50-level undo and requested history window");
#endif // 3DMMEx: KAUAI_WIN32
    }

    // 3DMMv1.0: Up the cache limits of the studio crfs in the global crm.
    for (i = 0; i < _pglicrfStudio->IvMac(); i++)
    {
        _pgstStudioFiles->GetExtra(i, &cbCache);
        _pglicrfStudio->Get(i, &iv);
        pcrfT = _pcrmAll->PcrfGet(iv);
        Assert(pcrfT != pvNil, "Main CRM is corrupt.");
        pcrfT->SetCbMax(cbCache);
    }

    // 3DMMv1.0: Zero the cache for the building crfs in the global crm.
    for (i = 0; i < _pglicrfBuilding->IvMac(); i++)
    {
        _pglicrfBuilding->Get(i, &iv);
        pcrfT = _pcrmAll->PcrfGet(iv);
        Assert(pcrfT != pvNil, "Main CRM is corrupt.");
        pcrfT->SetCbMax(0);
    }

    fRet = fTrue;
#if defined(KAUAI_WIN32)
    Queue4DMMObjectGroupsMovieRefresh();

    // The blank-startup file-error filter is armed earlier in APP::_FInit /
    // FCmdLoadStudio.  Do not re-arm it here after a successful Studio init:
    // that would make an unrelated later file error eligible for suppression.
#endif // 3DMMEx: KAUAI_WIN32

LFail:
    if (!fRet)
        PushErc(ercSocCantInitStudio);
    return fRet;
}

/** 3DMMEx: *************************************************************************
    Initialize keyboard accelerator table
***************************************************************************/
bool APP::_FInitAcceleratorTable(void)
{
    AssertBaseThis(0);

    if (vpcex == pvNil)
    {
        Bug("Cannot initialize accelerator table: nil vpcex");
        return fFalse;
    }

    // 3DMMEx: Create global accelerator table
    _patblGlobal = ATBL::PatblNew(HidUnique(), vpcex);
    AssertPo(_patblGlobal, 0);
    if (_patblGlobal == pvNil)
    {
        Bug("Could not allocate global accelerator table");
        return fFalse;
    }

    // 3DMMEx: Enable the accelerator table
    AssertDo(vpcex->FAddCmh(_patblGlobal, 0, kgrfcmmAll), "Could not enable accelerator table");

    // 3DMMEx: Register global keyboard accelerators
    AssertDo(_patblGlobal->FAddCmdKey(VK_FROM_ALPHA('I'), fcustCmd | fcustShift, cidInfo), "Could not add hotkey");
    AssertDo(_patblGlobal->FAddCmdKey(VK_FROM_ALPHA('Q'), fcustCmd, cidQuit), "Could not add hotkey");
    AssertDo(_patblGlobal->FAddCmdKey(kvkReturn, fcustOption, cidToggleFullscreen), "Could not add hotkey");
    AssertDo(_patblGlobal->FAddCmdKey(VK_FROM_ALPHA('L'), fcustCmd | fcustShift, cid4DMMSceneLights), "Could not add 4DMM lighting hotkey");
    AssertDo(_patblGlobal->FAddCmdKey(VK_FROM_ALPHA('L'), fcustCmd | fcustOption, cid4DMMHideLightObjects), "Could not add 4DMM light-object hotkey");
#if defined(KAUAI_WIN32)
    AssertDo(_patblGlobal->FAddCmdKey(VK_OEM_3, fcustCmd, cid4DMMSettings), "Could not add 4DMM settings hotkey");
    AssertDo(_patblGlobal->FAddCmdKey(VK_OEM_3, fcustCmd | fcustShift, cid4DMMSettings), "Could not add 4DMM settings hotkey");
    // Route Object Groups through Kauai's proven global accelerator path rather
    // than depending on which child/native window happens to own WM_KEYDOWN.
    AssertDo(_patblGlobal->FAddCmdKey(VK_FROM_ALPHA('G'), fcustCmd, cid4DMMObjectGroups), "Could not add 4DMM Object Groups hotkey");
    AssertDo(_patblGlobal->FAddCmdKey(VK_OEM_3, fcustOption, cid4DMMObjectGroups), "Could not add 4DMM Object Groups Alt+tilde hotkey");
    AssertDo(_patblGlobal->FAddCmdKey(VK_OEM_3, fcustOption | fcustShift, cid4DMMObjectGroups), "Could not add 4DMM Object Groups Alt+tilde hotkey");
    // Use the same proven Kauai global accelerator path as Ctrl+G.  The v198
    // frame-window-only Ctrl+A handler never saw keystrokes while a movie
    // child/native control owned focus.  Alt+A is an intentional second path.
    AssertDo(_patblGlobal->FAddCmdKey(VK_FROM_ALPHA('A'), fcustCmd, cid4DMMActorStudio), "Could not add 4DMM Actor Studio Ctrl+A hotkey");
    AssertDo(_patblGlobal->FAddCmdKey(VK_FROM_ALPHA('A'), fcustOption, cid4DMMActorStudio), "Could not add 4DMM Actor Studio Alt+A hotkey");
    AssertDo(_patblGlobal->FAddCmdKey(ChLit('7'), fcustCmd, cid4DMMObj2Vxp2), "Could not add 4DMM OBJ to VXP2 Ctrl+7 hotkey");
#endif

    // 3DMMEx: Create main accelerator table
    _patblMain = ATBL::PatblNew(HidUnique(), vpcex);
    AssertPo(_patblMain, 0);
    if (_patblMain == pvNil)
    {
        Bug("Could not allocate app accelerator table");
        return fFalse;
    }

    // 3DMMEx: Register main keyboard accelerators
    AssertDo(_patblMain->FAddCmdKey(VK_FROM_ALPHA('C'), fcustCmd, cidCopy), "Could not add hotkey");
    AssertDo(_patblMain->FAddCmdKey(VK_FROM_ALPHA('C'), fcustCmd | fcustShift, cidShiftCopy), "Could not add hotkey");
    AssertDo(_patblMain->FAddCmdKey(VK_FROM_ALPHA('C'), fcustOption, cidEaselPickColor), "Could not add 3D Word custom-color hotkey");
    AssertDo(_patblMain->FAddCmdKey(VK_FROM_ALPHA('L'), fcustCmd, cidLightLab), "Could not add hotkey");
    AssertDo(_patblMain->FAddCmdKey(VK_FROM_ALPHA('M'), fcustCmd, cidMap), "Could not add hotkey");
    AssertDo(_patblMain->FAddCmdKey(VK_FROM_ALPHA('N'), fcustCmd, cidNew), "Could not add hotkey");
    AssertDo(_patblMain->FAddCmdKey(VK_FROM_ALPHA('O'), fcustCmd, cidOpen), "Could not add hotkey");
    AssertDo(_patblMain->FAddCmdKey(VK_FROM_ALPHA('S'), fcustCmd, cidSave), "Could not add hotkey");
    AssertDo(_patblMain->FAddCmdKey(VK_FROM_ALPHA('V'), fcustCmd, cidPaste), "Could not add hotkey");
    AssertDo(_patblMain->FAddCmdKey(kvkF1, fcustNil, cidHelpBook), "Could not add hotkey");
    AssertDo(_patblMain->FAddCmdKey(kvkF9, fcustNil, cidToggleXY), "Could not add hotkey");
    AssertDo(_patblMain->FAddCmdKey(kvkF10, fcustNil, cidWriteBmps), "Could not add hotkey");
    AssertDo(_patblMain->FAddCmdKey(VK_FROM_ALPHA('X'), fcustCmd, cidCut), "Could not add hotkey");
    AssertDo(_patblMain->FAddCmdKey(VK_FROM_ALPHA('X'), fcustCmd | fcustShift, cidShiftCut), "Could not add hotkey");
    AssertDo(_patblMain->FAddCmdKey(VK_FROM_ALPHA('Y'), fcustCmd, cidRedo), "Could not add hotkey");
    AssertDo(_patblMain->FAddCmdKey(VK_FROM_ALPHA('Z'), fcustCmd, cidUndo), "Could not add hotkey");

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Sets _fSlowCPU if this is a slow CPU.  It tests the graphics,
    fixed-point math, and memory copying speed, and if any of them are
    slower than a threshold, _fSlowCPU is set.
***************************************************************************/
bool APP::_FDetermineIfSlowCPU(void)
{
    AssertBaseThis(0);

    int32_t fSlowCPU;

    // 3DMMv1.0: If user has a saved preference, read and use that
    if (FGetSetRegKey(kszBetterSpeedValue, &fSlowCPU, SIZEOF(fSlowCPU), fregNil))
    {
        _fSlowCPU = (bool)fSlowCPU;
        return fTrue;
    }

    _fSlowCPU = fFalse;

#ifndef PERF_TEST

    return fTrue;

#else // 3DMMv1.0: PERF_TEST

    PGPT pgptWnd = pvNil;
    PGPT pgptOff = pvNil;
    RC rc1;
    RC rc2;
    uint32_t ts;
    uint32_t dts1;
    uint32_t dts2;
    uint32_t dts3;
    int32_t i;
    char *pch1 = pvNil;
    char *pch2 = pvNil;
    BRS r1;
    BRS r2;
    BRS r3;

    // 3DMMv1.0: Test 1: Graphics.  Copy some graphics to an offscreen buffer,
    // 3DMMv1.0: then blit it back to the window 100 times.
    rc1.Set(0, 0, 300, 300);
    rc2.Set(0, 0, 300, 300);
    pgptWnd = GPT::PgptNewHwnd(vwig.hwndApp);
    if (pvNil == pgptWnd)
        goto LFail;
    pgptOff = GPT::PgptNewOffscreen(&rc1, 8); // 3DMMv1.0: BWLD RGB buffer is 8-bit
    if (pvNil == pgptOff)
    {
        goto LFail;
    }
    // 3DMMv1.0: BLOCK
    {
        GNV gnvWnd(pgptWnd);
        GNV gnvOff(pgptOff);
        gnvOff.CopyPixels(&gnvWnd, &rc1, &rc1);
        ts = TsCurrent();
        for (i = 0; i < 100; i++)
            gnvWnd.CopyPixels(&gnvOff, &rc1, &rc2);
        dts1 = TsCurrent() - ts;
    }
    ReleasePpo(&pgptWnd);
    ReleasePpo(&pgptOff);

    // 3DMMv1.0: Test 2: Math.  Do 200,000 fixed-point adds and multiplies
    r1 = BR_SCALAR(1.12);    // 3DMMv1.0: arbitrary number
    r2 = BR_SCALAR(3.14159); // 3DMMv1.0: arbitrary number
    r3 = rZero;
    ts = TsCurrent();
    for (i = 0; i < 200000; i++)
    {
        r2 = BrsMul(r1, r2);
        r3 = BrsAdd(r2, r3);
    }
    dts2 = TsCurrent() - ts;

    // 3DMMv1.0: Test 3: Copying.  Copy a 50,000 byte block 200 times.
    if (!FAllocPv((void **)&pch1, 50000, mprNormal, fmemClear))
        goto LFail;
    if (!FAllocPv((void **)&pch2, 50000, mprNormal, fmemClear))
        goto LFail;
    ts = TsCurrent();
    for (i = 0; i < 200; i++)
        CopyPb(pch1, pch2, 50000);
    dts3 = TsCurrent() - ts;
    FreePpv((void **)&pch1);
    FreePpv((void **)&pch2);

    if (dts1 > 700 || dts2 > 200 || dts3 > 500)
        _fSlowCPU = fTrue;

    return fTrue;
LFail:
    ReleasePpo(&pgptWnd);
    ReleasePpo(&pgptOff);
    FreePpv((void **)&pch1);
    FreePpv((void **)&pch2);
    return fFalse;

#endif // 3DMMv1.0: PERF_TEST
}

static bool F4DMMPathIs3mm(PSTN pstnPath)
{
    if (pstnPath == pvNil)
        return fFalse;
    const int32_t cch = pstnPath->Cch();
    const achar *psz = pstnPath->Psz();
    return cch >= 4 && psz[cch - 4] == ChLit('.') &&
           psz[cch - 3] == ChLit('3') &&
           (psz[cch - 2] == ChLit('m') || psz[cch - 2] == ChLit('M')) &&
           (psz[cch - 1] == ChLit('m') || psz[cch - 1] == ChLit('M'));
}

static bool F4DMMPathIsMovieDocument(PSTN pstnPath)
{
    return F4DMMPathIs3mm(pstnPath) || F4DMMPathIsVmm(pstnPath);
}

/***************************************************************************
    Reads command line.  Also sets _fniExe to path to this executable
    and _fniPortfolioDoc to file specified on command line (if any).
    Also initializes _stnProductLong and _stnProductShort.

    -e: editor-only mode: skip building/splash and open directly into the editor
    -f: fast mode
    -s: slow mode
    -p"longname": long productname
    -t"shortname": short productname
    -m: don't minimize window on deactivate
    -multi: enable experimental multi-object selection and Object Groups
    -multi_log, -multi_logs: Object Groups bind/move/rotation diagnostics written to logs\multi.log
    -browser: enable external actor/prop/3D-word content browsers
    -gui_scale <N|N.d>: scale native/external 4DMM tool windows independently of the main presentation. One decimal digit is accepted (for example 1, 1.5, 2.0).
    -cursor_size <Nx>: override only the authored main-window 3DMM cursor scale; otherwise it follows -resolution.
    -w, -window: force windowed mode (Modern 4DMM defaults to a 2x 1280x960 presentation)
    -v: enable the Modern BRender viewport host (embedded/hidden in standard and scaled modes; 1080p retains the external compatibility surface)
    -resolution <1080p|1080|1920x1080|SCALE>x: override the default 2x presentation / native high-resolution movie output.
        1080p/1080/1920x1080 use the separate 1920x1080 external viewport.
        Decimal SCALE values from 1.25x through 8x use the scaled full-app compositor
        and imply -v and -w. Up to three fractional digits are accepted. Examples:
        2x -> 1280x960 app / 1088x612 native movie viewport
        2.25x -> 1440x1080 app / 1224x689 native movie viewport
        2.5x -> 1600x1200 app / 1360x765 native movie viewport
        3x -> 1920x1440 app / 1632x918 native movie viewport
        4x -> 2560x1920 app / 2176x1224 native movie viewport
    -l: add one experimental white directional light to each active scene
    -sh: enable the experimental one-Light-Lab shadow-map renderer
    -shadow_log, -shadow_logs: write focused shadow diagnostics to logs\shadow.log
    -c: render BRender movie worlds into 24-bit RGB888 true-colour buffers
    -a: enable actor/prop texture shading in RGB888 true-colour mode
    -u: enable 50-level undo plus the external Undo History window
    -uvdump: export every loaded BRender model as OBJ/CSV plus a UV seam report
    -precache: examine and cache all movie resources before displaying a loaded movie
    -logs: verbose crash/debug breadcrumbs plus per-frame lighting/performance timings
    -logs_light_ed: lightweight editor/light/selection/undo event log written to logs\light_editor.log
    -modern_br_log, -modern_br_logs: dedicated BRender 1.4/OpenGL bring-up log written to logs\modern_br.log; also enables -logs diagnostics
    -perf: low-overhead per-frame performance timings written to logs\performance.csv
    -o <movie.3mm>: load the specified movie. Without -e this is the
        playback-only Theatre runner; with -e it opens the movie directly
        in Studio and leaves normal editor behavior intact.

***************************************************************************/
void APP::_ParseCommandLine(void)
{
    AssertBaseThis(0);

    STN stn;
    PCSZ pch = pvNil;
    PCSZ pchT = pvNil;
    FNI fniT;
    bool fResolutionOptionSpecified = fFalse;
#if defined(KAUAI_WIN32)
    int32_t iargExplicitDocument = ivNil;
    PCSZ pszExplicitDocumentSource = PszLit("none");
    STN stnExplicitDocument;
    vf4DMMExplicitStartupDocument = fFalse;
#endif

    AssertDo(_fniCurrentDir.FGetCwd(), "Bad current directory?");
    AssertDo(_fniExe.FGetExe(), "Bad module filename?");

    // MSVC argv includes the executable as argv[0], but some launch paths can
    // hand us only real arguments.  If argv[0] already looks like an option,
    // parse it instead of skipping it.  This matters for -o, because otherwise
    // -o gets skipped and the following movie path is treated like a normal
    // command-line movie, which opens fullscreen and still shows startup.
    int32_t ipszFirst = 1;
    if (_cpszArgv > 0 && _rgpszArgv != pvNil && _rgpszArgv[0] != pvNil &&
        (*_rgpszArgv[0] == ChLit('/') || *_rgpszArgv[0] == ChLit('-')))
    {
        ipszFirst = 0;
    }

    MODERN_BR_LOG("APP::_ParseCommandLine argc=%ld first_index=%ld", (long)_cpszArgv, (long)ipszFirst);
    for (int32_t iargLog = 0; iargLog < _cpszArgv; ++iargLog)
        MODERN_BR_LOG("APP::_ParseCommandLine argv[%ld]=%s", (long)iargLog,
                      _rgpszArgv != pvNil && _rgpszArgv[iargLog] != pvNil ? _rgpszArgv[iargLog] : "(null)");

    for (int32_t ipsz = ipszFirst; ipsz < _cpszArgv; ipsz++)
    {
        pch = _rgpszArgv[ipsz];
        if (pch == pvNil || *pch == chNil)
            continue;

        // 3DMMv1.0: Look for /options or -options
        if (*pch == ChLit('/') || *pch == ChLit('-'))
        {
            // -precache must be recognized before the legacy -p product-name
            // option consumes it.  Match the complete option case-insensitively.
            stn.SetSz(pch + 1);
            achar szPrecache[] = PszLit("precache");
            if (stn.FEqualUserSz(szPrecache))
            {
                MVIE::SetPrecacheMode(fTrue);
                continue;
            }
            achar szLightEditorLogs[] = PszLit("logs_light_ed");
            if (stn.FEqualUserSz(szLightEditorLogs))
            {
                MVIE::SetLightEditorLogMode(fTrue);
                continue;
            }
            achar szMultiLog[] = PszLit("multi_log");
            achar szMultiLogs[] = PszLit("multi_logs");
            if (stn.FEqualUserSz(szMultiLog) || stn.FEqualUserSz(szMultiLogs))
            {
                MVIE::SetMultiLogMode(fTrue);
                continue;
            }
            achar szLogs[] = PszLit("logs");
            if (stn.FEqualUserSz(szLogs))
            {
                MVIE::SetDiagnosticsMode(fTrue);
                // logperf17: -logs also emits the low-overhead per-frame CSV so
                // extended diagnostic exports always contain the lighting timing
                // evidence. Use -perf without -logs when measuring with minimum
                // verbose-breadcrumb overhead.
                MVIE::SetPerformanceMode(fTrue);
                continue;
            }
            achar szPerf[] = PszLit("perf");
            if (stn.FEqualUserSz(szPerf))
            {
                MVIE::SetPerformanceMode(fTrue);
                continue;
            }
            // Experimental 3D fixed-point precision mode. Match the complete
            // spelling before the legacy one-letter switch parser sees the
            // leading digit and rejects it.
            achar sz3DFix[] = PszLit("3dfix");
            if (stn.FEqualUserSz(sz3DFix))
            {
                BWLD::Set3DFixMode(fTrue);
                continue;
            }

            // Shadow rendering and shadow diagnostics are independent. Keep
            // these ahead of the legacy single-letter -s parser.  -sh changes
            // rendering only; -shadow_log/-shadow_logs create shadow.log and
            // enable the expensive per-caster trace used for focused debugging.
            achar szShadowMode[] = PszLit("sh");
            if (stn.FEqualUserSz(szShadowMode))
            {
                MVIE::SetShadowMode(fTrue);
                continue;
            }

            achar szShadowLog[] = PszLit("shadow_log");
            achar szShadowLogs[] = PszLit("shadow_logs");
            if (stn.FEqualUserSz(szShadowLog) || stn.FEqualUserSz(szShadowLogs))
            {
#if defined(BRENDER_MODERN_14)
                BrShadowLogSetEnabled(true);
                Br4DMMShadowTraceSetEnabled(BR_TRUE);
                BrShadowLog("SHADOW TRACE CLI -shadow_log%s recognized; per-caster trace enabled",
                            stn.FEqualUserSz(szShadowLogs) ? "s alias" : "");
#endif
                continue;
            }

            achar szModernBrLog[] = PszLit("modern_br_log");
            achar szModernBrLogs[] = PszLit("modern_br_logs");
            if (stn.FEqualUserSz(szModernBrLog) || stn.FEqualUserSz(szModernBrLogs))
            {
#if defined(BRENDER_MODERN_14)
                BrModernLogSetEnabled(true);
                BrModernLog("APP::_ParseCommandLine recognized -modern_br_log%s",
                            stn.FEqualUserSz(szModernBrLogs) ? "s alias" : "");
#endif
                MVIE::SetDiagnosticsMode(fTrue);
                continue;
            }

            achar szGuiScale[] = PszLit("gui_scale");
            if (stn.FEqualUserSz(szGuiScale))
            {
#if defined(KAUAI_WIN32)
                if (ipsz + 1 >= _cpszArgv || _rgpszArgv[ipsz + 1] == pvNil ||
                    *_rgpszArgv[ipsz + 1] == ChLit('-') || *_rgpszArgv[ipsz + 1] == ChLit('/'))
                {
                    Warn("-gui_scale requires a value such as 1, 1.5, or 2.0");
                    continue;
                }
                ++ipsz;
                int32_t guiNum = 0;
                int32_t guiDen = 0;
                if (FParse4DMMExternalToolScaleOption(_rgpszArgv[ipsz], &guiNum, &guiDen))
                {
                    Set4DMMExternalToolScale(guiNum, guiDen);
                    MODERN_BR_LOG("APP::_ParseCommandLine recognized -gui_scale %s -> external tool scale=%ld/%ld",
                                  _rgpszArgv[ipsz], (long)guiNum, (long)guiDen);
                }
                else
                {
                    Warn("Bad -gui_scale value; use a positive value with at most one decimal digit (for example 1, 1.5, or 2.0)");
                }
#endif
                continue;
            }

            achar szCursorSize[] = PszLit("cursor_size");
            if (stn.FEqualUserSz(szCursorSize))
            {
#if defined(KAUAI_WIN32)
                if (ipsz + 1 >= _cpszArgv || _rgpszArgv[ipsz + 1] == pvNil ||
                    *_rgpszArgv[ipsz + 1] == ChLit('-') || *_rgpszArgv[ipsz + 1] == ChLit('/'))
                {
                    Warn("-cursor_size requires a scale from 1x through 8x (for example 2x or 2.5x)");
                    continue;
                }

                ++ipsz;
                int32_t cursorNum = 0;
                int32_t cursorDen = 0;
                if (FParse4DMMCursorScaleOption(_rgpszArgv[ipsz], &cursorNum, &cursorDen))
                {
                    Set4DMMCustomCursorScale(cursorNum, cursorDen);
                    MODERN_BR_LOG("APP::_ParseCommandLine recognized -cursor_size %s -> authored cursor scale=%ld/%ld",
                                  _rgpszArgv[ipsz], (long)cursorNum, (long)cursorDen);
                }
                else
                {
                    Warn("Bad -cursor_size value; use a scale from 1x through 8x (for example 2x or 2.5x)");
                }
#endif
                continue;
            }

            achar szResolution[] = PszLit("resolution");
            if (stn.FEqualUserSz(szResolution))
            {
                fResolutionOptionSpecified = fTrue;
#if defined(BRENDER_MODERN_14)
                if (ipsz + 1 >= _cpszArgv || _rgpszArgv[ipsz + 1] == pvNil ||
                    *_rgpszArgv[ipsz + 1] == ChLit('-') || *_rgpszArgv[ipsz + 1] == ChLit('/'))
                {
                    Warn("-resolution requires 1080p, 1080, 1920x1080, or a scale from 1.25x through 8x");
                    continue;
                }

                ++ipsz;
                stn.SetSz(_rgpszArgv[ipsz]);
                achar sz1080p[] = PszLit("1080p");
                achar sz1080[] = PszLit("1080");
                achar sz1920x1080[] = PszLit("1920x1080");
                if (stn.FEqualUserSz(sz1080p) || stn.FEqualUserSz(sz1080) ||
                    stn.FEqualUserSz(sz1920x1080))
                {
                    vfViewportResolution1080p = fTrue;
                    vfViewportResolution4x = fFalse;
                    vlw4DMMUiScaleNumeratorRequested = k4DMMUiScaleNumeratorDefault;
                    vlw4DMMUiScaleDenominatorRequested = k4DMMUiScaleDenominatorDefault;
                    _fViewportWindow = fTrue;
                    MODERN_BR_LOG("APP::_ParseCommandLine recognized -resolution %s -> native 1920x1080 external-only",
                                  _rgpszArgv[ipsz]);
                }
                else
                {
                    int32_t scaleNum;
                    int32_t scaleDen;
                    if (FParse4DMMUiScaleOption(_rgpszArgv[ipsz], &scaleNum, &scaleDen))
                    {
                        vfViewportResolution1080p = fFalse;
                        vfViewportResolution4x = fTrue;
                        vlw4DMMUiScaleNumeratorRequested = scaleNum;
                        vlw4DMMUiScaleDenominatorRequested = scaleDen;
                        _fViewportWindow = fTrue;
                        _fForceWindow = fTrue;
                        _fRunInWindow = fTrue;
                        MODERN_BR_LOG("APP::_ParseCommandLine recognized -resolution %s -> %ldx%ld UI + native %ldx%ld presentation-owned movie layer scale=%ld/%ld",
                                      _rgpszArgv[ipsz],
                                      (long)MulDiv(640, vlw4DMMUiScaleNumeratorRequested,
                                                   vlw4DMMUiScaleDenominatorRequested),
                                      (long)MulDiv(480, vlw4DMMUiScaleNumeratorRequested,
                                                   vlw4DMMUiScaleDenominatorRequested),
                                      (long)MulDiv(kdxpWorkspace, vlw4DMMUiScaleNumeratorRequested,
                                                   vlw4DMMUiScaleDenominatorRequested),
                                      (long)MulDiv(kdypWorkspace, vlw4DMMUiScaleNumeratorRequested,
                                                   vlw4DMMUiScaleDenominatorRequested),
                                      (long)vlw4DMMUiScaleNumeratorRequested,
                                      (long)vlw4DMMUiScaleDenominatorRequested);
                    }
                    else
                    {
                        Warn("Bad -resolution option; use 1080p, 1080, 1920x1080, or a scale from 1.25x through 8x");
                    }
                }
#else
                Warn("-resolution requires the Modern BRender/OpenGL build");
#endif
                continue;
            }

            // Browser mode is opt-in so the original Studio input behavior is
            // untouched unless -browser is explicitly present.
            achar szBrowser[] = PszLit("browser");
            if (stn.FEqualUserSz(szBrowser))
            {
                _fExternalBrowsers = fTrue;
                continue;
            }
            // Recognize the complete -multi option before the legacy -m
            // switch, which otherwise treats every -m... argument as -m.
            achar szMulti[] = PszLit("multi");
            if (stn.FEqualUserSz(szMulti))
            {
                MVIE::SetMultiSelectMode(fTrue);
                continue;
            }

            switch (ChUpper(*(pch + 1)))
            {
            case ChLit('M'):
                _fDontMinimize = fTrue;
                break;
            case ChLit('E'):
                // Keep the editor request independent of -o while parsing so
                // option order does not matter.  The final mode selection
                // below resolves -o alone to Theatre playback, and -e -o to
                // direct Studio editing of the supplied movie.
                _fEditorOnly = fTrue;
                break;
            case ChLit('O'): {
                PCSZ pszMovie = pch + 2;

                // Record that -o was requested, but defer deciding whether
                // it means Theatre playback or direct-editor open until every
                // option has been parsed.  This makes -e -o and -o -e behave
                // identically.
                _fPlaybackOnly = fTrue;

                // Support both -oC:\Movie.3mm and -o "C:\Movie.3mm".
                if (*pszMovie == chNil)
                {
                    if (ipsz + 1 >= _cpszArgv)
                    {
                        Warn("-o requires a movie path");
                        break;
                    }
                    ipsz++;
                    pszMovie = _rgpszArgv[ipsz];
                }

                stn.SetSz(pszMovie);

                // 3DMMv1.0: remove ending quotes since FBuildFromPath can't deal
                if (stn.Cch() > 0 && stn.Psz()[stn.Cch() - 1] == ChLit('\"'))
                    stn.Delete(stn.Cch() - 1, 1);
                if (stn.Cch() > 0 && stn.Psz()[0] == ChLit('\"'))
                    stn.Delete(0, 1);

                if (fniT.FBuildFromPath(&stn, F4DMMPathIsVmm(&stn) ? kftgVmm : kftg3mm))
                {
                    SetPortfolioDoc(&fniT);
#if defined(KAUAI_WIN32)
                    vf4DMMExplicitStartupDocument = fTrue;
                    iargExplicitDocument = ipsz;
                    pszExplicitDocumentSource = PszLit("-o");
                    stnExplicitDocument = stn;
#endif
                }
            }
            break;
            case ChLit('W'):
                _fForceWindow = fTrue;
                _fRunInWindow = fTrue;
                break;
            case ChLit('V'):
                _fViewportWindow = fTrue;
                break;
            case ChLit('L'):
                if (*(pch + 2) == chNil)
                    MVIE::SetTestLightMode(fTrue);
                else
                    Warn("Bad -l option; use -l");
                break;
            case ChLit('C'):
                if (*(pch + 2) == chNil)
                    BWLD::SetTrueColorMode(fTrue);
                else
                    Warn("Bad -c option; use -c");
                break;
            case ChLit('A'):
                if (*(pch + 2) == chNil)
                {
                    BWLD::SetActorLightMode(fTrue);
                    vgActorLightQuickModelUpdateEnabled = 1;
                }
                else
                    Warn("Bad -a option; use -a");
                break;
            case ChLit('U'):
                // Keep the existing -uvdump diagnostic switch, but reserve
                // the exact short -u switch for extended undo/history.
                if (ChUpper(*(pch + 2)) == ChLit('V') && ChUpper(*(pch + 3)) == ChLit('D') &&
                    ChUpper(*(pch + 4)) == ChLit('U') && ChUpper(*(pch + 5)) == ChLit('M') &&
                    ChUpper(*(pch + 6)) == ChLit('P') && *(pch + 7) == chNil)
                {
                    MODL::SetUvDumpEnabled(fTrue);
                }
                else if (*(pch + 2) == chNil)
                {
                    _fUndoHistory = fTrue;
                }
                else
                {
                    Warn("Bad -u option; use -u or -uvdump");
                }
                break;
            case ChLit('S'):
                _fSlowCPU = fTrue;
                break;
            case ChLit('F'):
                _fSlowCPU = fFalse;
                break;
            case ChLit('P'): {
                pch += 2; // 3DMMv1.0: skip "-p"
                pchT = pch;
                _SkipToSpace(&pchT);
                // 3DMMv1.0: skip quotes
                if (*pch == ChLit('"'))
                    _stnProductLong.SetRgch(pch + 1, pchT - pch - 2);
                else
                    _stnProductLong.SetRgch(pch, pchT - pch);
            }
            break;
            case ChLit('T'): {
                pch += 2; // 3DMMv1.0: skip "-t"
                pchT = pch;
                _SkipToSpace(&pchT);
                // 3DMMv1.0: skip quotes
                if (*pch == ChLit('"'))
                    _stnProductShort.SetRgch(pch + 1, pchT - pch - 2);
                else
                    _stnProductShort.SetRgch(pch, pchT - pch);
            }
            break;
            default:
                Warn("Bad command-line switch");
                break;
            }
        }
        else // 3DMMv1.0: try to parse as fni string
        {
            stn.SetSz(pch);

            // 3DMMv1.0: remove ending quotes since FBuildFromPath can't deal
            if (stn.Cch() > 0 && stn.Psz()[stn.Cch() - 1] == ChLit('\"'))
                stn.Delete(stn.Cch() - 1, 1);
            if (stn.Cch() > 0 && stn.Psz()[0] == ChLit('\"'))
                stn.Delete(0, 1);

            // A positional token is a document only when it actually names a
            // supported movie. FBuildFromPath() accepts arbitrary path-like
            // strings, which made unrelated launcher/option values poison the
            // startup state and disarm the one real erc=400 startup guard.
            if (F4DMMPathIsMovieDocument(&stn) &&
                fniT.FBuildFromPath(&stn, F4DMMPathIsVmm(&stn) ? kftgVmm : kftg3mm))
            {
                SetPortfolioDoc(&fniT);
#if defined(KAUAI_WIN32)
                vf4DMMExplicitStartupDocument = fTrue;
                iargExplicitDocument = ipsz;
                pszExplicitDocumentSource = PszLit("positional");
                stnExplicitDocument = stn;
#endif
            }
#if defined(KAUAI_WIN32)
            else
            {
                // MultiLog may only have become enabled by a later argument;
                // a complete argv dump is emitted after the loop below.
                MODERN_BR_LOG("APP::_ParseCommandLine ignored positional non-movie argv[%ld]=%s",
                              (long)ipsz, stn.Psz());
            }
#endif
        }
    }

#if defined(KAUAI_WIN32)
    // Emit the complete command line after all switches have been processed so
    // -multi_log itself can enable this trace even when it appears near the end.
    MVIE::MultiLog(pvNil, "startup_argv argc=%ld first_index=%ld explicit=%d source=%s arg=%ld path=%s",
                   (long)_cpszArgv, (long)ipszFirst, (int)vf4DMMExplicitStartupDocument,
                   pszExplicitDocumentSource, (long)iargExplicitDocument,
                   stnExplicitDocument.Cch() > 0 ? stnExplicitDocument.Psz() : "-");
    for (int32_t iargLog = 0; iargLog < _cpszArgv; ++iargLog)
    {
        MVIE::MultiLog(pvNil, "startup_argv[%ld]=%s", (long)iargLog,
                       _rgpszArgv != pvNil && _rgpszArgv[iargLog] != pvNil ?
                           _rgpszArgv[iargLog] : "(null)");
    }
#endif

#if defined(BRENDER_MODERN_14)
    // v71: 2x is now the normal 4DMM presentation. An explicit -resolution
    // still wins (including the 1080p external-only path), but omitting the
    // option no longer drops modern 4DMM back to the original 640x480 client.
    if (!fResolutionOptionSpecified)
    {
        vfViewportResolution1080p = fFalse;
        vfViewportResolution4x = fTrue;
        vlw4DMMUiScaleNumeratorRequested = 2;
        vlw4DMMUiScaleDenominatorRequested = 1;
        _fViewportWindow = fTrue;
        _fForceWindow = fTrue;
        _fRunInWindow = fTrue;
        MODERN_BR_LOG("APP::_ParseCommandLine no -resolution -> default 2x 1280x960 UI + native 1088x612 movie viewport");
    }

    // glrend front/offscreen buffers are true-colour.  Make RGB888 mandatory
    // for the experimental Modern backend so GPU readback can continue to
    // feed the unchanged Kauai editor viewport even when -c was omitted.
    BWLD::SetTrueColorMode(fTrue);
    const int32_t dxpModernViewport = vfViewportResolution4x ? Lw4DMMScaleSourceToPresentation(kdxpWorkspace)
                                      : vfViewportResolution1080p ? 1920
                                      : kdxpWorkspace;
    const int32_t dypModernViewport = vfViewportResolution4x ? Lw4DMMScaleSourceToPresentation(kdypWorkspace)
                                      : vfViewportResolution1080p ? 1080
                                      : kdypWorkspace;
    BrModernViewportConfigure(dxpModernViewport, dypModernViewport,
                              vfViewportResolution1080p || vfViewportResolution4x);
#endif

    // Publish the launch mode directly before any movie can be constructed.
    // This is the authoritative -u handoff; the property table remains only
    // as a compatibility path for the existing initialization code.
    MVIE::SetExtendedUndoMode(_fUndoHistory);
    if (MVIE::FDiagnosticsMode())
    {
        MVIE::DiagLog("launch parsed: -c=%d -a=%d -l=%d -3dfix=%d -u=%d -multi=%d -browser=%d -e=%d -o(playback-only)=%d -v=%d -resolution1080p=%d -resolutionScaled=%d scale=%d/%d -w=%d -perf=%d",
                      (int)BWLD::FTrueColorMode(), (int)BWLD::FActorLightMode(), (int)MVIE::FTestLightMode(),
                      (int)BWLD::F3DFixMode(), (int)_fUndoHistory, (int)MVIE::FMultiSelectMode(),
                      (int)_fExternalBrowsers, (int)_fEditorOnly, (int)_fPlaybackOnly,
                      (int)_fViewportWindow, (int)vfViewportResolution1080p, (int)vfViewportResolution4x,
                      (int)(vfViewportResolution4x ? vlw4DMMUiScaleNumeratorRequested : 1),
                      (int)(vfViewportResolution4x ? vlw4DMMUiScaleDenominatorRequested : 1),
                      (int)_fRunInWindow, (int)MVIE::FPerformanceMode());
    }
#if defined(KAUAI_WIN32) && defined(BRENDER_MODERN_14)
    MODERN_BR_LOG("APP::_ParseCommandLine final c=%d a=%d l=%d e=%d o=%d v=%d resolution1080p=%d resolutionScaled=%d scale=%d/%d w=%d modern_log=%d",
                  (int)BWLD::FTrueColorMode(), (int)BWLD::FActorLightMode(), (int)MVIE::FTestLightMode(),
                  (int)_fEditorOnly, (int)_fPlaybackOnly, (int)_fViewportWindow,
                  (int)vfViewportResolution1080p, (int)vfViewportResolution4x,
                  (int)(vfViewportResolution4x ? vlw4DMMUiScaleNumeratorRequested : 1),
                  (int)(vfViewportResolution4x ? vlw4DMMUiScaleDenominatorRequested : 1),
                  (int)_fRunInWindow, (int)FBrModernLogEnabled());
#endif
#ifdef KAUAI_WIN32
    if (_fUndoHistory)
        _LogUndoHistoryStartup("APP::_ParseCommandLine recognized -u; extended undo mode enabled");
#endif // 3DMMEx: KAUAI_WIN32

    // -o has two intentionally different launch paths.  With -e, the movie
    // path is simply the document to open directly in Studio.  Without -e,
    // retain the dedicated Theatre/TATR playback-only runner.
    if (_fPlaybackOnly && _fEditorOnly)
    {
        _fPlaybackOnly = fFalse;
    }
    else if (_fPlaybackOnly)
    {
        _fForceWindow = fTrue;
        _fRunInWindow = fTrue;
        _fViewportWindow = fTrue;
    }
}

/** 3DMMv1.0: *************************************************************************
    Make sure that _stnProductLong and _stnProductShort are initialized to
    non-empty strings.  If _stnProductLong wasn't initialized from the
    command line, it and _stnProductShort are read from the app resource
    file.  If _stnProductLong is valid and _stnProductShort is not,
    _stnProductShort is just set to _stnProductLong.
***************************************************************************/
bool APP::_FEnsureProductNames(void)
{
    AssertBaseThis(0);

    if (_stnProductLong.Cch() == 0)
    {
#ifdef WIN
        SZ sz;

        if (0 == LoadString(vwig.hinst, stid3DMovieNameLong, sz, kcchMaxSz))
            return fFalse;
        _stnProductLong.SetSz(sz);

        if (0 == LoadString(vwig.hinst, stid3DMovieNameShort, sz, kcchMaxSz))
            return fFalse;
        _stnProductShort.SetSz(sz);
#else
        _stnProductLong = PszLit("3D Movie Maker");
        _stnProductShort = PszLit("3dmovie");
#endif
    }

    if (_stnProductShort.Cch() == 0)
        _stnProductShort = _stnProductLong;

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Find _fniMsKidsDir
***************************************************************************/
bool APP::_FFindMsKidsDir(void)
{
    AssertBaseThis(0);
    Assert(_stnProductLong.Cch() > 0 && _stnProductShort.Cch() > 0, "_stnProductLong and _stnProductShort must exist");

    FNI fni;
    SZ szMsKidsDir;
    STN stn;
    STN stnUsers;
    FNI fniResourcesDir;
    FNI fniInstallDir;
    bool fFound = fFalse;
    PFNI rgpfniDir[3];

    AssertDo(fniResourcesDir.FGetResourcesDir(), "Could not find resources directory");

    // 3DMMEx: Build list of paths to search for the Microsoft Kids directory
    ClearPb(rgpfniDir, SIZEOF(rgpfniDir));
    rgpfniDir[0] = &fniResourcesDir; // 3DMMEx: Application resources directory
    rgpfniDir[1] = pvNil;            // 3DMMEx: InstallDirectory from registry if present
    rgpfniDir[2] = &_fniCurrentDir;  // 3DMMEx: Current working directory

    // 3DMMEx: Read the install directory from the registry
    szMsKidsDir[0] = chNil;
    if (!FGetSetRegKey(kszInstallDirValue, szMsKidsDir, SIZEOF(SZ), fregMachine | fregString))
    {
        Warn("Missing InstallDirectory registry entry or registry error");
    }

    stn = szMsKidsDir;
    if (stn.Cch() != 0)
    {
        if (fniInstallDir.FBuildFromPath(&stn, kftgDir))
        {
            rgpfniDir[1] = &fniInstallDir;
        }
        else
        {
            Bug("Could not build path from InstallDirectory registry entry");
        }
    }

    // 3DMMEx: Search each path for the Microsoft Kids directory
    for (int32_t ipfni = 0; ipfni < CvFromRgv(rgpfniDir); ipfni++)
    {
        if (rgpfniDir[ipfni] == pvNil)
        {
            continue;
        }

        _fniMsKidsDir = *rgpfniDir[ipfni];
        if (_fniMsKidsDir.FSetLeaf(pvNil, kftgDir))
        {
            if (_FFindMsKidsDirAt(&_fniMsKidsDir))
            {
                fFound = fTrue;
                break;
            }
        }
    }

    if (!fFound)
    {
        Warn("Can't find Microsoft Kids or MSKIDS.");
        stn = PszLit("Microsoft Kids");
        _FCantFindFileDialog(&stn); // 3DMMv1.0: ignore failure
        return fFalse;
    }

    AssertPo(&_fniMsKidsDir, ffniDir);
    return fTrue;
}

/** 3DMMEx: *************************************************************************
    Finds Microsoft Kids directory at a given path. Modifies the path to
    descend into the directory. Returns true if successful.
***************************************************************************/
bool APP::_FFindMsKidsDirAt(FNI *path)
{
    STN stn;

    /* 3DMMv1.0: REVIEW ***** (peted): if you check for the MSKIDS dir first, then
        you don't have to reset the dir string before presenting the error
        to the user */
    stn = PszLit("Microsoft Kids"); // 3DMMv1.0: REVIEW *****
    if (!path->FDownDir(&stn, ffniMoveToDir))
    {
        stn = PszLit("MSKIDS"); // 3DMMv1.0: REVIEW *****
        if (!path->FDownDir(&stn, ffniMoveToDir))
        {
            return fFalse;
        }
    }

    AssertPo(path, ffniDir);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Find _fniProductDir
    At this point, _stnProduct* is either the command line parameter or
    3D Movie Maker (in the absence of a command line parameter)
    _FFindProductDir() locates the .chk files by checking:
        first, _stnProduct* directories, or
        second, the registry of installed products.
    This routine updates _stnProductLong and _stnProductShort on return.
***************************************************************************/
bool APP::_FFindProductDir(PGST pgst)
{
    AssertBaseThis(0);
    AssertVarMem(pgst);

    STN stnLong;
    STN stnShort;
    STN stn;
    FNI fni;
    int32_t istn;

    if (_FQueryProductExists(&_stnProductLong, &_stnProductShort, &_fniProductDir))
        return fTrue;

    for (istn = 0; istn < pgst->IstnMac(); istn++)
    {
        pgst->GetStn(istn, &stn);
        vptagm->SplitString(&stn, &stnLong, &stnShort);
        if (_FQueryProductExists(&stnLong, &stnShort, &fni))
        {
            _stnProductLong = stnLong;
            _stnProductShort = stnShort;
            _fniProductDir = fni;
            return fTrue;
        }
    }
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    See if the product exists.
    Method:  See if the directory and chunk file exist.
***************************************************************************/
bool APP::_FQueryProductExists(STN *pstnLong, STN *pstnShort, FNI *pfni)
{
    AssertBaseThis(0);
    AssertVarMem(pfni);
    AssertPo(pstnLong, 0);
    AssertPo(pstnShort, 0);

    FNI fni;
    STN stn;

    *pfni = _fniMsKidsDir;
    if (!pfni->FDownDir(pstnLong, ffniMoveToDir) && !pfni->FDownDir(pstnShort, ffniMoveToDir))
    {
        pfni->GetStnPath(&stn);
        if (!stn.FAppendStn(&_stnProductLong))
            goto LFail;
        _FCantFindFileDialog(&stn); // 3DMMv1.0: ignore failure
        goto LFail;
    }

    fni = *pfni;
    if (fni.FSetLeaf(pstnLong, kftgChunky) && (tYes == fni.TExists()))
        return fTrue;
    fni = *pfni;
    if (fni.FSetLeaf(pstnShort, kftgChunky) && (tYes == fni.TExists()))
        return fTrue;
LFail:
    TrashVar(pfni);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Advances *ppch until it points to either the next space character or
    the end of the string.  Exception: if the space character is surrounded
    by double quotes, this function skips by it.
***************************************************************************/
void APP::_SkipToSpace(PCSZ *ppch)
{
    AssertBaseThis(0);
    AssertVarMem(ppch);

    bool fQuote = fFalse;

    while (**ppch != chNil && (fQuote || **ppch != kchSpace))
    {
        if (**ppch == ChLit('"'))
            fQuote = !fQuote;
        (*ppch)++;
    }
}

/** 3DMMv1.0: *************************************************************************
    Advances *ppch to the next non-space character.
***************************************************************************/
void APP::_SkipSpace(PCSZ *ppch)
{
    AssertBaseThis(0);
    AssertVarMem(ppch);

    while (**ppch == kchSpace)
        (*ppch)++;
}

/** 3DMMv1.0: *************************************************************************
    Socrates window was activated or deactivated.
***************************************************************************/
void APP::_Activate(bool fActive)
{
    AssertBaseThis(0);

#ifdef WIN
    // APPB::_Activate() is not just a foreground-state assignment: it also
    // calls vpsndm->Activate(fActive).  The v77 diagnostics measured the
    // presentation's WM_ACTIVATEAPP at 12:38:49.623 and this function's
    // post-APP_PAR activation marker at 12:38:50.457, an ~0.83 s gap.  The
    // Kauai source shows that sound-device activate/deactivate call is the only
    // work in APPB::_Activate() besides setting _fForeground.
    //
    // A scaled/windowed 4DMM presentation should stay live when another app
    // owns the foreground, so do not tear down and wake the 1995 sound manager
    // on every focus transition.  Keep its devices active, update the inherited
    // foreground flag directly, and retain the already-proven cidDeactivate
    // bypass.  Stock activation behavior remains unchanged for unscaled/legacy
    // and native-dialog/portfolio paths.
    const bool fScaledPresentation =
        vwig.hwndApp != hNil && IsWindow(vwig.hwndApp) &&
        GetPropA(vwig.hwndApp, "4DMMUiScaleWindow") != pvNil &&
        GetPropA(vwig.hwndApp, "4DMMUiScaleSuspended") == pvNil;
    if (fScaledPresentation)
    {
        _fForeground = FPure(fActive);
        if (vpcex != pvNil)
            vpcex->FlushCid(cidDeactivate);
        _fDown = fFalse;
        _cactToggle = 0;
        MODERN_BR_LOG("scaled UI activation v78 immediate active=%d sound-manager retained cidDeactivate/modal suppressed",
                      (int)fActive);
        return;
    }
#endif

    APP_PAR::_Activate(fActive);

#ifdef WIN
    bool fIsIconic;

    fIsIconic = IsIconic(vwig.hwndApp);

    if (!fActive) // 3DMMv1.0: app was just deactivated
    {
        if (_FDisplayIs640480() && !_fDontMinimize && !fIsIconic)
        {
            // 3DMMv1.0: Note: using SW_MINIMIZE causes a bug where alt-tabbing
            // 3DMMv1.0: from this app to a fullscreen DOS window reactivates
            // 3DMMv1.0: this app.  So use SW_SHOWMINNOACTIVE instead.
            ShowWindow(vwig.hwndApp, SW_SHOWMINNOACTIVE); // 3DMMv1.0: minimize app
            _fMinimized = fTrue;

            // 3DMMv1.0: Note that we examine _fMinimized during the WM_DISPLAYCHANGE message
            // 3DMMv1.0: received as a result of the following res change call. Therefore the
            // 3DMMv1.0: minimize operation MUST precede the res switch.
            if (_fSwitchedResolution)
                _FSwitch640480(fFalse);

            // 3DMMv1.0: When the portfolio is displayed, the main app is automatically disabled.
            // 3DMMv1.0: This means all keyboard/mouse input directed at the main app window will
            // 3DMMv1.0: be ignored until the portfolio is finished with. If the app is minimized
            // 3DMMv1.0: here while the portfolio is displayed, then we will be left with a disabled
            // 3DMMv1.0: app window on the win95 task bar. As a result, the app will not appear
            // 3DMMv1.0: only the task window invoked by an Alt-tab, nor is it resized when
            // 3DMMv1.0: the user clicks on it in the taskbar, (even though win95 tries to
            // 3DMMv1.0: activate it). We could do the following...
            // 3DMMv1.0: (1) Do not auto-minimize the app window while the portfolio is displayed.
            // 3DMMv1.0:		This is what happens on NT.
            // 3DMMv1.0: (2) Drop the portfolio here, so the app window is enabled on the taskbar.
            // 3DMMv1.0: (3) Make sure the app window is enabled now, by doing this...
            EnableWindow(vwig.hwndApp, TRUE);

            // 3DMMv1.0: The concern with doing this, is that when the app is later restored,
            // 3DMMv1.0: it is then enabled when it shouldn't be, as the portfolio is still
            // 3DMMv1.0: up in front of it. As it happens, this doesn't matter because the
            // 3DMMv1.0: portfolio is full screen. This means that the user can't direct any
            // 3DMMv1.0: mouse input to the main app window, and the portfolio will eat up any
            // 3DMMv1.0: keyboard input.
        }
    }

#endif // 3DMMv1.0: WIN

    /* 3DMMv1.0: Don't do this stuff unless we've got the CEX set up */
    if (vpcex != pvNil)
    {

        if (!fActive)
        {
            if (!vpcex->FCidIn(cidDeactivate))
            {
                vpcex->EnqueueCid(cidDeactivate);
                _fDown = fTrue;
                _cactToggle = 0;
            }
        }
        else if (_pcex != pvNil)
        {
            //
            // 3DMMv1.0: End the modal wait
            //
            Assert(CactModal() > 0, "AAAAAAAAAhhhhh! - P.Floyd (Encore performance)");

            // 3DMMv1.0: If there is no cidEndModal currently waiting to be processed,
            // 3DMMv1.0: enqueue one now. This ensures we don't get multiple cidEndModal's
            // 3DMMv1.0: processed. Note that we don't want to set _pcex null here as it
            // 3DMMv1.0: is later examined in the wndproc before we ultimately return
            // 3DMMv1.0: from FModalTopic.
            if (!_pcex->FCidIn(cidEndModal))
                _pcex->EnqueueCid(cidEndModal);
        }
        else
        {
            vpcex->FlushCid(cidDeactivate);
        }
    }
}

/** 3DMMv1.0: *************************************************************************
    Deactivate the app
***************************************************************************/
bool APP::FCmdDeactivate(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    GCB gcb;
    PWOKS pwoksModal;
    GTE gte;
    PGOB pgob;
    uint32_t grfgte;
    int32_t lwRet;
    CMD_MOUSE cmd;
    PT pt;
    bool fDoQuit;

    if (_pcex != pvNil)
    {
        Assert(CactModal() > 0, "AAAAAAAAAhhhhh! - P.Floyd");
        return (fTrue);
    }

    pgob = vpcex->PgobTracking();
    if ((pgob != pvNil) && (_cactToggle < 500))
    {
        //
        // 3DMMv1.0: Toggle the mouse button
        //
        TrackMouse(pgob, &pt);

        ClearPb(&cmd, SIZEOF(CMD_MOUSE));

        cmd.pcmh = pgob;
        cmd.cid = cidTrackMouse;
        cmd.xp = pt.xp;
        cmd.yp = pt.yp;
        cmd.grfcust = GrfcustCur();
        if (_fDown)
        {
            cmd.grfcust |= fcustMouse;
        }
        else
        {
            cmd.grfcust &= ~fcustMouse;
        }
        vpcex->EnqueueCmd((PCMD)&cmd);
        vpcex->EnqueueCid(cidDeactivate);
        _fDown = !_fDown;
        _cactToggle++;
        return fTrue;
    }

    gte.Init(Pkwa(), fgteNil);
    while (gte.FNextGob(&pgob, &grfgte, fgteNil))
    {
        if (!(grfgte & fgtePre) || !pgob->FIs(kclsGOK))
            continue;

        ((PGOK)pgob)->Suspend();
    }

    if (FPushModal())
    {
        gcb.Set(CMH::HidUnique(), Pkwa(), fgobNil, kginMark);
        gcb._rcRel.Set(0, 0, krelOne, krelOne);

        _pcex = vpcex;

        if (pvNil != (pwoksModal = NewObj WOKS(&gcb, Pkwa()->Pstrg())))
        {
            vpcex->SetModalGob(pwoksModal);
            FModalLoop(&lwRet); // 3DMMv1.0: If we cannot enter modal mode, then we just won't suspend.
            vpcex->SetModalGob(pvNil);
        }

        ReleasePpo(&pwoksModal);

        _pcex = pvNil;

        // 3DMMv1.0: The user may have selected Close the app system menu while the
        // 3DMMv1.0: app was minimized on the taskbar. Depending on how Windows sent
        // 3DMMv1.0: the messages to us, (ie the processing order of activate and
        // 3DMMv1.0: close messages are not predictable it seems), we may or may not
        // 3DMMv1.0: have a cidQuit message queued for the app. If PopModal destroys
        // 3DMMv1.0: queued messages, then we would loose any Waiting cidQuit.
        // 3DMMv1.0: Therefore check if we have a queued cidQuit, and if so, requeue
        // 3DMMv1.0: it after the call to PopModal.
        fDoQuit = vpcex->FCidIn(cidQuit);

        PopModal();

        if (fDoQuit)
            vpcex->EnqueueCid(cidQuit);
    }

    gte.Init(Pkwa(), fgteNil);
    while (gte.FNextGob(&pgob, &grfgte, fgteNil))
    {
        if (!(grfgte & fgtePre) || !pgob->FIs(kclsGOK))
            continue;

        ((PGOK)pgob)->Resume();
    }

    return (fTrue);
}

/***************************************************************************
    Allocate the application's offscreen staging surface.  Stock Kauai
    hardcodes this cache to 8-bit indexed colour.  In -c mode that silently
    quantizes the already-RGB888 BRender movie image before it reaches the
    real window.  Keep legacy behavior untouched unless true-colour mode is
    active, then maintain the same cache as a 24-bit DIB section.
***************************************************************************/
PGPT APP::_PgptEnsure(RC *prc)
{
    AssertBaseThis(0);
    AssertVarMem(prc);

    if (!BWLD::FTrueColorMode())
        return APP_PAR::_PgptEnsure(prc);

    RC rc;
    const int32_t kcbitTrueColorStage = 24;

    // Command-line parsing can enable -c after Kauai has already allocated
    // its normal 8-bit staging cache.  Do not reuse a stale indexed surface.
    if (_pgptOff != pvNil && _pgptOff->CbitPixel() != kcbitTrueColorStage)
    {
        ReleasePpo(&_pgptOff);
        _dxpOff = _dypOff = 0;
    }

    if (prc->Dxp() > _dxpOff || prc->Dyp() > _dypOff)
    {
        ReleasePpo(&_pgptOff);
        rc.Set(0, 0, LwMax(prc->Dxp(), _dxpOff), LwMax(prc->Dyp(), _dypOff));
        _pgptOff = GPT::PgptNewOffscreen(&rc, kcbitTrueColorStage);
        if (_pgptOff != pvNil)
        {
            _dxpOff = rc.Dxp();
            _dypOff = rc.Dyp();
        }
        else
        {
            _dxpOff = _dypOff = 0;
        }
    }

    if (_pgptOff != pvNil)
    {
        PT pt = prc->PtTopLeft();
        _pgptOff->SetPtBase(&pt);
    }

    return _pgptOff;
}

/** 3DMMv1.0: *************************************************************************
    Copy pixels from an offscreen buffer (pgnvSrc, prcSrc) to the screen
    (pgnvDst, prcDst).  This is called to move bits from an offscreen
    buffer to the screen during a _FastUpdate cycle.  This gives us
    a chance to do a transition.
***************************************************************************/
void APP::_CopyPixels(PGNV pgnvSrc, RC *prcSrc, PGNV pgnvDst, RC *prcDst)
{
    AssertBaseThis(0);
    AssertPo(pgnvSrc, 0);
    AssertVarMem(prcSrc);
    AssertPo(pgnvDst, 0);
    AssertVarMem(prcDst);

    PMVIE pmvie = _Pmvie(); // 3DMMv1.0: Get the current movie, if any
    PGOB pgob;
    RC rcDst, rcSrc, rcWorkspace;

    if (pmvie == pvNil || pmvie->Trans() == transNil)
    {
        APP_PAR::_CopyPixels(pgnvSrc, prcSrc, pgnvDst, prcDst);
        return;
    }

    Assert(prcSrc->Dyp() == prcDst->Dyp() && prcSrc->Dxp() == prcDst->Dxp(), "rc's are scaled");

    // 3DMMv1.0: Need to do a transition, but if it's a slow transition (not a cut),
    // 3DMMv1.0: we want to do a regular copy on all the areas around the workspace,
    // 3DMMv1.0: then the slow transition on just the workspace.

    pgob = Pkwa()->PgobFromHid(kidWorkspace);

    if (pgob == pvNil || pmvie->Trans() == transCut)
    {
        pmvie->DoTrans(pgnvDst, pgnvSrc, prcDst, prcSrc);
        return;
    }

    // 3DMMv1.0: NOTE: This code assumes that the following will base rcWorkspace
    // 3DMMv1.0: in the same coordinate system as prcDst, which is always true
    // 3DMMv1.0: according to ShonK.
    pgob->GetRc(&rcWorkspace, cooHwnd);

    // 3DMMv1.0: Do the areas around the workspace without the transition
    if (prcDst->ypTop < rcWorkspace.ypTop)
    {
        rcSrc = *prcSrc;
        rcSrc.ypBottom = rcWorkspace.ypTop + (rcSrc.ypTop - prcDst->ypTop);
        rcDst = *prcDst;
        rcDst.ypBottom = rcWorkspace.ypTop;
        APP_PAR::_CopyPixels(pgnvSrc, &rcSrc, pgnvDst, &rcDst);
    }

    if (prcDst->ypBottom > rcWorkspace.ypBottom)
    {
        rcSrc = *prcSrc;
        rcSrc.ypTop = rcWorkspace.ypBottom + (rcSrc.ypTop - prcDst->ypTop);
        rcDst = *prcDst;
        rcDst.ypTop = rcWorkspace.ypBottom;
        APP_PAR::_CopyPixels(pgnvSrc, &rcSrc, pgnvDst, &rcDst);
    }
    if (prcDst->xpLeft < rcWorkspace.xpLeft)
    {
        rcSrc.ypTop = rcWorkspace.ypTop + (prcSrc->ypTop - prcDst->ypTop);
        rcSrc.ypBottom = rcWorkspace.ypBottom + (prcSrc->ypTop - prcDst->ypTop);
        rcSrc.xpLeft = prcSrc->xpLeft;
        rcSrc.xpRight = rcWorkspace.xpLeft + (prcSrc->xpLeft - prcDst->xpLeft);
        rcDst = *prcDst;
        rcDst.xpRight = rcWorkspace.xpLeft;
        rcDst.ypTop = rcWorkspace.ypTop;
        rcDst.ypBottom = rcWorkspace.ypBottom;
        APP_PAR::_CopyPixels(pgnvSrc, &rcSrc, pgnvDst, &rcDst);
    }

    if (prcDst->xpRight > rcWorkspace.xpRight)
    {
        rcSrc.ypTop = rcWorkspace.ypTop + (prcSrc->ypTop - prcDst->ypTop);
        rcSrc.ypBottom = rcWorkspace.ypBottom + (prcSrc->ypTop - prcDst->ypTop);
        rcSrc.xpLeft = rcWorkspace.xpRight + (prcSrc->xpLeft - prcDst->xpLeft);
        rcSrc.xpRight = prcSrc->xpRight;
        rcDst = *prcDst;
        rcDst.xpLeft = rcWorkspace.xpRight;
        rcDst.ypTop = rcWorkspace.ypTop;
        rcDst.ypBottom = rcWorkspace.ypBottom;
        APP_PAR::_CopyPixels(pgnvSrc, &rcSrc, pgnvDst, &rcDst);
    }

    //
    // 3DMMv1.0: Now do the workspace copy, with the transition
    //
    if (rcWorkspace.FIntersect(prcDst))
    {
        rcSrc = rcWorkspace;
        rcSrc.Offset(prcSrc->xpLeft - prcDst->xpLeft, prcSrc->ypTop - prcDst->ypTop);
        pmvie->DoTrans(pgnvDst, pgnvSrc, &rcWorkspace, &rcSrc);
    }
}

/** 3DMMv1.0: *************************************************************************
    Load the Studio
***************************************************************************/
bool APP::FCmdLoadStudio(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    FNI fniUserDoc;
    CHID chidProject;
    int32_t kidBuilding;
    PGOB pgob;
    PGOK pgokBackground;

    kidBuilding = pcmd->rglw[0];
    chidProject = pcmd->rglw[1];

    GetPortfolioDoc(&fniUserDoc); // 3DMMv1.0: might be ftgNil
#if defined(KAUAI_WIN32)
    // Never re-arm here. If the process-start erc=400 fired before command-line
    // parsing, DisplayErrors already consumed the one legitimate suppression.
    // Re-arming at Studio load would risk hiding the user's next real failure.
    if (fniUserDoc.Ftg() != ftgNil || vf4DMMExplicitStartupDocument)
    {
        vf4DMMBlankStartupPhase = fFalse;
        vf4DMMSuppressNextBlankStartupFileError = fFalse;
    }
#endif

    if (_FInitStudio(&fniUserDoc))
    {
#if defined(KAUAI_WIN32)
        // Do NOT disarm the blank-startup filter here. The offending 1995
        // condition is not necessarily an ERC already waiting on vpers: the
        // first APP::FCmdIdle pass scans every open CFL, observes ElError(),
        // and only then synthesizes ercFileGeneral. v152/v153-WIP both cleared
        // the one-shot before that scan could occur. Leave it armed for exactly
        // the first idle pass; APP::FCmdIdle owns the final consume/disarm.
        if (vf4DMMBlankStartupPhase && vf4DMMSuppressNextBlankStartupFileError)
            MVIE::MultiLog(_pstdio != pvNil ? _pstdio->Pmvie() : pvNil,
                           "startup_error_filter retained_through_studio_ready awaiting_first_idle_cfl_scan");
#endif
        // 3DMMv1.0: Nuke the building gob
        pgob = Pkwa()->PgobFromHid(kidBuilding);
        if (pvNil != pgob)
            pgob->Release();

        // 3DMMv1.0: Start a project, if requested
        if (chidProject != chidNil)
        {
            pgokBackground = (PGOK)Pkwa()->PgobFromHid(kidBackground);
            if ((pgokBackground != pvNil) && pgokBackground->FIs(kclsGOK))
            {
                AssertPo(pgokBackground, 0);
                // 3DMMv1.0: REVIEW *****: if this fails, what happens?
                pgokBackground->FRunScript((kstDefault << 16) | chidProject);
            }
        }
        if (_fPlaybackOnly)
            _fPlaybackAutoPlay = fTrue;
    }
    else
    {
        Assert(_pstdio == pvNil, "_FInitStudio didn't clean up");
        vpcex->EnqueueCid(cidLoadStudioFailed);
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Load the Building
***************************************************************************/
bool APP::FCmdLoadBuilding(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    if ((_pstdio != pvNil) && !_pstdio->FShutdown())
    {
        int32_t kgobReturn;

        // 3DMMv1.0: This attempt may have destroyed the contents of kpridBuildingGob;
        // 3DMMv1.0: Try to reset it from kpridBuildingGobT.
        if (FGetProp(kpridBuildingGobT, &kgobReturn))
            FSetProp(kpridBuildingGob, kgobReturn);
        if (FGetProp(kpridBuildingStateT, &kgobReturn))
            FSetProp(kpridBuildingState, kgobReturn);
        Warn("Shutting down Studio failed");
        return fTrue;
    }

    if (_FInitBuilding())
    {
        ReleasePpo(&_pstdio);
#if defined(KAUAI_WIN32)
        Queue4DMMObjectGroupsMovieRefresh();
#endif // 3DMMEx: KAUAI_WIN32
    }
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Exit the studio
***************************************************************************/
bool APP::FCmdExitStudio(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    tribool tRet;
    STN stnBackup;

    // 3DMMv1.0: Now query the user to find whether they want to go to the
    // 3DMMv1.0: building or to exit the app completely.

    if (!FGetStnApp(idsExitStudio, &stnBackup))
        return fTrue;

    tRet = TModal(vpapp->PcrmAll(), ktpcQueryExitStudio, &stnBackup, bkYesNoCancel);

    // 3DMMv1.0: Take more action if the user did not hit cancel.
    if (tRet != tMaybe)
    {
        // 3DMMv1.0: Save the current movie if necessary.
        if ((_pstdio != pvNil) && !_pstdio->FShutdown(tRet == tYes))
            return fTrue;

        // 3DMMv1.0: Either go to the building, or leave the app.
        if (tRet == tYes)
        {
            // 3DMMv1.0: Go to the building.
            if (_FInitBuilding())
            {
                ReleasePpo(&_pstdio);
#if defined(KAUAI_WIN32)
                Queue4DMMObjectGroupsMovieRefresh();
#endif // 3DMMEx: KAUAI_WIN32
            }
        }
        else if (tRet == tNo)
        {
            // 3DMMv1.0: User wants to quit. We have have any dirty doc already by now.
            // 3DMMv1.0: Note that APP::Quit() doesn't release _pstdio when we quit so
            // 3DMMv1.0: we don't need to do it here either.
            _fQuit = fTrue;
        }
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Load the Theater
***************************************************************************/
bool APP::FCmdTheaterOpen(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    int32_t kidParent;

    kidParent = pcmd->rglw[0];

    if (pvNil != _ptatr)
    {
        Bug("You forgot to close the last TATR!");
        AssertPo(_ptatr, 0);
        return fTrue;
    }
    _ptatr = TATR::PtatrNew(kidParent);
    // 3DMMv1.0: Let the script know whether the open succeeded or failed
    vpcex->EnqueueCid(cidTheaterOpenCompleted, pvNil, pvNil, (pvNil != _ptatr));

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Close the Theater
***************************************************************************/
bool APP::FCmdTheaterClose(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    AssertPo(_ptatr, 0);
    ReleasePpo(&_ptatr);

    return fTrue;
}

#if defined(KAUAI_WIN32)
static bool F4DMMShowInvalidVmmMessage(void)
{
    // This is a complete, explicit validation error. Do not route it through
    // the old generic Kauai error resource, which prepends "ended unexpectedly"
    // and uses the historical 3D Movie Maker caption.
    if (vpers != pvNil)
        vpers->Clear();
    vf4DMMInvalidVmmMessageActive = fTrue;
    vc4DMMInvalidVmmIdlePasses = 2;

    HWND hwndOwner = hNil;
    if (vhwnd4DMMUiScale != hNil && IsWindow(vhwnd4DMMUiScale))
        hwndOwner = vhwnd4DMMUiScale;
    else if (vwig.hwndApp != hNil && IsWindow(vwig.hwndApp))
        hwndOwner = vwig.hwndApp;

    char szProduct[128];
    strcpy_s(szProduct, SIZEOF(szProduct), "4DMM");
    if (hwndOwner != hNil)
    {
        char szWindow[128];
        if (GetWindowTextA(hwndOwner, szWindow, SIZEOF(szWindow)) > 0)
            strcpy_s(szProduct, SIZEOF(szProduct), szWindow);
    }

    char szTitle[192];
    sprintf_s(szTitle, SIZEOF(szTitle), "%s error", szProduct);
    static PCSZ kpszInvalidVmm =
        PszLit("This .vmm file is invalid and cannot be opened.");
    MessageBoxA(hwndOwner, kpszInvalidVmm, szTitle,
                MB_OK | MB_ICONERROR | MB_TASKMODAL | MB_SETFOREGROUND);

    // MessageBox pumps messages, so clear any file-family error that happened
    // to land on the Kauai stack while the direct modal was open. A one/two-idle
    // guard remains for an error synthesized by the failed command afterward.
    if (vpers != pvNil)
        vpers->Clear();
    return fTrue;
}

#endif

/***************************************************************************
    Resolve a v3DMM movie package into a normal 3MM and a private content
    source.  VMMs can contain a self-contained CHN2 movie directly after the
    wrapper or older ZIP-wrapped VXP packages followed by a CHN2 movie.
***************************************************************************/
bool APP::FResolveVmmMovie(PFNI pfniVmm, PFNI pfniMovie)
{
    AssertBaseThis(0);
    AssertPo(pfniVmm, ffniFile);
    AssertVarMem(pfniMovie);

    if (!F4DMMFniIsVmm(pfniVmm))
    {
        *pfniMovie = *pfniVmm;
        return fTrue;
    }

#ifdef KAUAI_WIN32
    STN stnVmm;
    STN stnMsKids;
    pfniVmm->GetStnPath(&stnVmm);
    _fniMsKidsDir.GetStnPath(&stnMsKids);

    std::filesystem::path pathVmm(stnVmm.Psz());
    std::filesystem::path pathMsKids(stnMsKids.Psz());
    std::error_code ec;

    MVIE::MultiLog(pvNil, "vmm_load v157 begin path=%s ftg=%lu mskids=%s", stnVmm.Psz(),
                   (unsigned long)pfniVmm->Ftg(), stnMsKids.Psz());

    // Validate the wrapper and locate a structurally plausible movie payload
    // BEFORE touching SID 6 or any cache directory. v156 mutated the current
    // TAGM source first, so a nonsense .vmm could damage the live session even
    // though validation failed a few instructions later.
    MVIE::MultiLog(pvNil, "vmm_load wrapper_open begin path=%s", pathVmm.string().c_str());
    std::ifstream stm(pathVmm, std::ios::binary);
    if (!stm)
    {
        MVIE::MultiLog(pvNil, "vmm_load fail stage=wrapper_open path=%s", pathVmm.string().c_str());
        return fFalse;
    }
    std::vector<uint8_t> rgb((std::istreambuf_iterator<char>(stm)), std::istreambuf_iterator<char>());
    if (stm.bad())
    {
        MVIE::MultiLog(pvNil, "vmm_load fail stage=wrapper_read path=%s", pathVmm.string().c_str());
        return fFalse;
    }
    MVIE::MultiLog(pvNil, "vmm_load wrapper_read ok size=%lu", (unsigned long)rgb.size());
    if (rgb.size() < 16 || 0 != memcmp(rgb.data(), "v3dmm", 5))
    {
        MVIE::MultiLog(pvNil, "vmm_load fail stage=wrapper size=%lu", (unsigned long)rgb.size());
        F4DMMShowInvalidVmmMessage();
        return fFalse;
    }
    MVIE::MultiLog(pvNil, "vmm_load wrapper_ok size=%lu", (unsigned long)rgb.size());

    // V3DMM movies exist in at least two package layouts. Some carry a
    // complete self-contained CHN2 movie immediately after the V3DMM wrapper;
    // others use the older FE EB + CHN2 marker with VXP ZIPs before it.
    size_t ibMovie = IbFindVmmChunkyPayload(rgb, 5);
    if (ibMovie == std::string::npos)
    {
        static const uint8_t krgbMovieLegacy[] = {0xfe, 0xeb, 'C', 'H', 'N', '2', ' ', 'C', 'O', 'S'};
        size_t ibMarker = std::string::npos;
        size_t ibFind = 0;
        for (;;)
        {
            size_t ibT = IbFindVmm(rgb, krgbMovieLegacy, SIZEOF(krgbMovieLegacy), ibFind, rgb.size());
            if (ibT == std::string::npos)
                break;
            ibMarker = ibT;
            ibFind = ibT + 1;
        }
        if (ibMarker == std::string::npos)
        {
            MVIE::MultiLog(pvNil, "vmm_load fail stage=find_movie_payload size=%lu", (unsigned long)rgb.size());
            F4DMMShowInvalidVmmMessage();
            return fFalse;
        }
        MVIE::MultiLog(pvNil, "vmm_load legacy_movie_marker offset=%lu", (unsigned long)ibMarker);
        ibMovie = ibMarker + 2;
    }
    MVIE::MultiLog(pvNil, "vmm_load movie_payload offset=%lu size=%lu",
                   (unsigned long)ibMovie, (unsigned long)(rgb.size() - ibMovie));

    // From here onward the VMM is structurally valid enough to justify
    // changing the transient V3DMM source/cache state.
    if (vptagm == pvNil || !vptagm->FRemoveStnSource(ksidV3dmm))
    {
        MVIE::MultiLog(pvNil, "vmm_load fail stage=remove_source sid=%ld", (long)ksidV3dmm);
        return fFalse;
    }
    MVIE::MultiLog(pvNil, "vmm_load source_removed sid=%ld", (long)ksidV3dmm);

    static LONG slwVmmLoadSerial = 0;
    const DWORD dwVmmPid = GetCurrentProcessId();
    const LONG lwVmmSerial = InterlockedIncrement(&slwVmmLoadSerial);
    char szCacheName[64];
    sprintf_s(szCacheName, SIZEOF(szCacheName), "VMMCache_%08lX_%04lX", (unsigned long)dwVmmPid,
              (unsigned long)(lwVmmSerial & 0xffff));
    std::filesystem::path pathCache = pathMsKids / szCacheName;
    std::filesystem::path pathPackages = pathCache / "packages";

    MVIE::MultiLog(pvNil, "vmm_load cache_create begin dir=%s", pathCache.string().c_str());
    if (!std::filesystem::create_directories(pathPackages, ec) && ec)
    {
        MVIE::MultiLog(pvNil, "vmm_load fail stage=cache_create ec=%ld dir=%s", (long)ec.value(),
                       pathCache.string().c_str());
        return fFalse;
    }
    MVIE::MultiLog(pvNil, "vmm_load cache_create ok dir=%s", pathCache.string().c_str());

    std::filesystem::path pathMovie = pathCache / pathVmm.stem();
    pathMovie.replace_extension(".3mm");
    if (!FWriteVmmBytes(pathMovie, rgb, ibMovie, rgb.size()))
    {
        MVIE::MultiLog(pvNil, "vmm_load fail stage=write_movie path=%s offset=%lu size=%lu",
                       pathMovie.string().c_str(), (unsigned long)ibMovie,
                       (unsigned long)(rgb.size() - ibMovie));
        return fFalse;
    }
    MVIE::MultiLog(pvNil, "vmm_load write_movie ok path=%s", pathMovie.string().c_str());

    // The real sibling Movie.3ct is authoritative for a VMM. The embedded
    // 4DCT remains a portability copy only. PmvieNew still loads the private
    // cached .3mm, so mirror the authoritative sibling into that cache path;
    // when the sibling is missing, reconstruct it from embedded 4DCT first.
    static const uint8_t krgb4Dct[] = {'4','D','C','T'};
    bool fFound4Dct = fFalse;
    uint32_t cb4Dct = 0;
    size_t ib4Dct = std::string::npos;
    for (size_t ib4 = ibMovie; ib4-- > 0; )
    {
        if (ib4 + 8 > ibMovie || 0 != memcmp(&rgb[ib4], krgb4Dct, SIZEOF(krgb4Dct)))
            continue;
        const uint32_t cb4 = LwFromVmmBytes(rgb, ib4 + 4);
        if ((uint64_t)ib4 + 8 + cb4 == ibMovie)
        {
            fFound4Dct = fTrue;
            cb4Dct = cb4;
            ib4Dct = ib4;
        }
        break;
    }

    std::filesystem::path pathSidecarReal = pathVmm;
    pathSidecarReal.replace_extension(".3ct");
    std::filesystem::path pathSidecarCache = pathMovie;
    pathSidecarCache.replace_extension(".3ct");
    const bool fSiblingExists = std::filesystem::exists(pathSidecarReal, ec) && !ec;
    ec.clear();
    if (fSiblingExists)
    {
        MVIE::MultiLog(pvNil, "vmm_load sidecar_authority sibling path=%s embedded=%d embedded_size=%lu",
                       pathSidecarReal.string().c_str(), (int)fFound4Dct, (unsigned long)cb4Dct);
        if (!std::filesystem::copy_file(pathSidecarReal, pathSidecarCache,
                                        std::filesystem::copy_options::overwrite_existing, ec))
        {
            MVIE::MultiLog(pvNil, "vmm_load fail stage=copy_sibling_3ct ec=%ld src=%s dst=%s",
                           (long)ec.value(), pathSidecarReal.string().c_str(),
                           pathSidecarCache.string().c_str());
            return fFalse;
        }
        MVIE::MultiLog(pvNil, "vmm_load sidecar_cache_from_sibling ok cache=%s",
                       pathSidecarCache.string().c_str());
    }
    else if (fFound4Dct)
    {
        const bool fWroteSibling = FWriteVmmBytes(pathSidecarReal, rgb, ib4Dct + 8, ibMovie);
        if (fWroteSibling)
        {
            MVIE::MultiLog(pvNil, "vmm_load sidecar_reconstructed path=%s size=%lu",
                           pathSidecarReal.string().c_str(), (unsigned long)cb4Dct);
            ec.clear();
            if (!std::filesystem::copy_file(pathSidecarReal, pathSidecarCache,
                                            std::filesystem::copy_options::overwrite_existing, ec))
            {
                MVIE::MultiLog(pvNil, "vmm_load fail stage=cache_reconstructed_3ct ec=%ld src=%s dst=%s",
                               (long)ec.value(), pathSidecarReal.string().c_str(),
                               pathSidecarCache.string().c_str());
                return fFalse;
            }
        }
        else
        {
            // Read-only media should still be openable. If we cannot establish
            // the permanent sibling, use the embedded copy in the private
            // cache for this session and log the architectural degradation.
            if (!FWriteVmmBytes(pathSidecarCache, rgb, ib4Dct + 8, ibMovie))
            {
                MVIE::MultiLog(pvNil, "vmm_load fail stage=write_embedded_3ct cache=%s size=%lu",
                               pathSidecarCache.string().c_str(), (unsigned long)cb4Dct);
                return fFalse;
            }
            MVIE::MultiLog(pvNil,
                "vmm_load sidecar_reconstruct_warning sibling_write_failed path=%s cache=%s size=%lu",
                pathSidecarReal.string().c_str(), pathSidecarCache.string().c_str(),
                (unsigned long)cb4Dct);
        }
    }
    else
    {
        MVIE::MultiLog(pvNil, "vmm_load sidecar_none sibling=%s embedded=0",
                       pathSidecarReal.string().c_str());
    }
    MVIE::MultiLog(pvNil, "vmm_load metadata_4dct found=%d size=%lu sibling=%d",
                   (int)fFound4Dct, (unsigned long)cb4Dct, (int)fSiblingExists);

    // Older VMM packages can contain normal ZIP-wrapped VXPs before the movie.
    // Self-contained CHN2 VMMs simply have none, so this loop becomes a no-op.
    static const uint8_t krgbZip[] = {'P', 'K', 3, 4};
    size_t ib = 0;
    int32_t cvxp = 0;
    while (ib < ibMovie)
    {
        size_t ibZip = IbFindVmm(rgb, krgbZip, SIZEOF(krgbZip), ib, ibMovie);
        if (ibZip == std::string::npos)
            break;
        size_t ibZipLim = IbFindVmmZipEnd(rgb, ibZip, ibMovie);
        if (ibZipLim == std::string::npos)
        {
            ib = ibZip + 1;
            continue;
        }

        char szZip[32];
        sprintf_s(szZip, SIZEOF(szZip), "vxp_%03d.zip", cvxp);
        const std::filesystem::path pathZip = pathPackages / szZip;
        if (!FWriteVmmBytes(pathZip, rgb, ibZip, ibZipLim))
        {
            MVIE::MultiLog(pvNil, "vmm_load fail stage=write_vxp index=%ld path=%s",
                           (long)cvxp, pathZip.string().c_str());
            return fFalse;
        }
        MVIE::MultiLog(pvNil, "vmm_load vxp_zip index=%ld offset=%lu size=%lu path=%s",
                       (long)cvxp, (unsigned long)ibZip, (unsigned long)(ibZipLim - ibZip),
                       pathZip.string().c_str());
        cvxp++;
        ib = ibZipLim;
    }

    if (cvxp > 0 && !FExpandVmmArchives(pathPackages, pathCache))
    {
        MVIE::MultiLog(pvNil, "vmm_load fail stage=expand_vxp count=%ld", (long)cvxp);
        return fFalse;
    }
    ec.clear();
    std::filesystem::remove_all(pathPackages, ec);
    if (ec)
        MVIE::MultiLog(pvNil, "vmm_load packages_cleanup warning ec=%ld dir=%s",
                       (long)ec.value(), pathPackages.string().c_str());

    // Only old VXP-bearing packages need SID 6 pinned to the extracted cache.
    // A new self-contained VMM must leave SID 6 absent here so PmvieNew can
    // merge the extracted movie's own GST normally.
    if (cvxp > 0)
    {
        // TAGM source strings are long-name/short-name pairs separated by '/'.
        // With no slash, SplitString deliberately uses the same directory name
        // for both, which is exactly what this private per-load HD source needs.
        STN stnSource(szCacheName);
        if (!vptagm->FSetStnSource(&stnSource, ksidV3dmm))
        {
            MVIE::MultiLog(pvNil, "vmm_load fail stage=set_vxp_source count=%ld", (long)cvxp);
            return fFalse;
        }
        MVIE::MultiLog(pvNil, "vmm_load vxp_source_set count=%ld sid=%ld source=%s",
                       (long)cvxp, (long)ksidV3dmm, szCacheName);
    }
    else
    {
        MVIE::MultiLog(pvNil, "vmm_load self_contained no_vxp sid=%ld left_unset", (long)ksidV3dmm);
    }

    STN stnMovie(pathMovie.string().c_str());
    if (!pfniMovie->FBuildFromPath(&stnMovie, kftg3mm))
    {
        MVIE::MultiLog(pvNil, "vmm_load fail stage=build_movie_fni path=%s", stnMovie.Psz());
        return fFalse;
    }
    MVIE::MultiLog(pvNil, "vmm_load resolved movie=%s vxp=%ld metadata=%d",
                   stnMovie.Psz(), (long)cvxp, (int)fFound4Dct);
    return fTrue;
#else  // 3DMMEx: KAUAI_WIN32
    Warn("VMM package loading is currently implemented only on Windows");
    return fFalse;
#endif // 3DMMEx: KAUAI_WIN32
}

/** 3DMMv1.0: *************************************************************************
    Clear the portfolio doc
***************************************************************************/
bool APP::FCmdPortfolioClear(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    // The building startup script normally clears any stale portfolio doc when
    // entering the building.  In -o playback-only mode that doc is the command
    // line movie we are about to load through the real Theatre/TATR path, so
    // do not let the startup clear erase it before TATR::FCmdLoad sees it.
    if (_fPlaybackOnly)
        return fTrue;

    _fniPortfolioDoc.SetNil();

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Display the customized open file common dlg.
***************************************************************************/
bool APP::FCmdPortfolioOpen(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    FNI fni;
    bool fOKed;
    int32_t idsTitle, idsFilterLabel, idsFilterExt;
    FNI fniUsersDir;
    PFNI pfni;
    uint32_t grfPrevType;
    CNO cnoWave = cnoNil;

    // 3DMMv1.0: Set up strings specific to this use of the portfolio.

    switch (pcmd->rglw[0])
    {
    case kpfPortOpenMovie:
        idsTitle = idsPortfOpenMovieTitle;
        idsFilterLabel = idsPortfMovieFilterLabel;
        idsFilterExt = idsPortfMovieFilterExt;

        grfPrevType = fpfPortPrevMovie;
        cnoWave = kwavPortOpenMovie;

        break;

    case kpfPortOpenSound:
        // 3DMMv1.0: Only display extensions appropriate to the type of sound being imported.
        idsTitle = idsPortfOpenSoundTitle;
        idsFilterLabel = idsPortfSoundFilterLabel;
        if (pcmd->rglw[1] == kidMidiGlass)
            idsFilterExt = idsPortfSoundMidiFilterExt;
        else
            idsFilterExt = idsPortfSoundWaveFilterExt;

        grfPrevType = fpfPortPrevMovie | fpfPortPrevSound;
        cnoWave = kwavPortOpenSound;

        break;

    case kpfPortOpenTexture:
        idsTitle = idsPortfOpenTextureTitle;
        idsFilterLabel = idsPortfTextureFilterLabel;
        idsFilterExt = idsPortfTextureFilterExt;

        grfPrevType = fpfPortPrevTexture;
        // 3DMMv1.0: Currently no audio for open texture, because we don't open texture yet.

        break;

    default:
        Bug("Unrecognized portfolio open type.");
        return fFalse;
    }

    // 3DMMv1.0: Prepare to set the initial directory for the portfolio if necessary.

    switch (pcmd->rglw[2])
    {
    case kpfPortDirUsers:

        // 3DMMv1.0: Initial directory will be the 'Users' directory.

        vapp.GetFniUsers(&fniUsersDir);
        pfni = &fniUsersDir;

        break;

    default:

        // 3DMMv1.0: Initial directory will be current directory.

        pfni = pvNil;

        break;
    }

    // 3DMMv1.0: Now display the open dlg. Script is informed of the outcome
    // 3DMMv1.0: of the portfolio from beneath FPortDisplayWithIds.
    fOKed = FPortDisplayWithIds(&fni, fTrue, idsFilterLabel, idsFilterExt, idsTitle, pvNil, pvNil, pfni, grfPrevType,
                                cnoWave);
    if (fOKed)
    {
        // 3DMMv1.0: User selected a file, so store fni for later use.
        SetPortfolioDoc(&fni);
    }

    return fTrue;
}

/** 3DMMv1.0: ****************************************************************************
    OnnDefVariable
        Retrieves the default onn for this app.  Gets the name from the app's
        string table.
************************************************************ PETED ***********/
int32_t APP::OnnDefVariable(void)
{
    AssertBaseThis(0);

    if (_onnDefVariable == onnNil)
    {
        STN stn;

        if (!FGetStnApp(idsDefaultFont, &stn) || !FGetOnn(&stn, &_onnDefVariable))
        {
            _onnDefVariable = APP_PAR::OnnDefVariable();
        }
    }
    return _onnDefVariable;
}

/** 3DMMv1.0: ****************************************************************************
    FGetOnn
        APP version of FGetOnn.  Mainly used so that we can easily report
        failure to the user if the font isn't around.  And no, I don't care
        that we'll call FGetOnn twice on failure...it's a failure case, and
        can stand to be slow.  :)  Maps the font on failure so that there's
        still a usable onn if the user doesn't want to mess with alternate
        solutions.

    Arguments:
        PSTN pstn   --  The font name
        long *ponn  --  takes the result

    Returns:  fTrue if the original font could be found.

************************************************************ PETED ***********/
bool APP::FGetOnn(PSTN pstn, int32_t *ponn)
{
    AssertBaseThis(0);

    if (!vntl.FGetOnn(pstn, ponn))
    {

#ifdef KAUAI_SDL
        fprintf(stderr, "Could not find font: %s\n", pstn->Psz());
#endif // 3DMMEx: KAUAI_SDL

        if (!_fFontError)
        {
            PushErc(ercSocNoDefaultFont);
            _fFontError = fTrue;
        }
        *ponn = vntl.OnnMapStn(pstn);
        return fFalse;
    }
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Return the size of memory
***************************************************************************/
void APP::MemStat(int32_t *pdwTotalPhys, int32_t *pdwAvailPhys)
{
#ifdef WIN
    MEMORYSTATUS ms;
    ms.dwLength = SIZEOF(MEMORYSTATUS);
    GlobalMemoryStatus(&ms);
    if (pvNil != pdwTotalPhys)
        *pdwTotalPhys = ms.dwTotalPhys;
    if (pvNil != pdwAvailPhys)
        *pdwAvailPhys = ms.dwAvailPhys;
#else  // 3DMMv1.0: !WIN
    // 3DMMEx: FIXME: Get memory usage
    if (pvNil != pdwTotalPhys)
        *pdwTotalPhys = 0;
    if (pvNil != pdwAvailPhys)
        *pdwAvailPhys = 0;
#endif // 3DMMv1.0: WIN
}

/** 3DMMv1.0: ****************************************************************************
    DypTextDef
        Retrieves the default dypFont for this app.  Gets the font size as
        a string from the app's string table, then converts it to the number
        value.
************************************************************ PETED ***********/
int32_t APP::DypTextDef(void)
{
    AssertBaseThis(0);

    if (_dypTextDef == 0)
    {
        STN stn;

        if (pvNil == _pgstApp || !FGetStnApp(idsDefaultDypFont, &stn) || !stn.FGetLw(&_dypTextDef) || _dypTextDef <= 0)
        {
            Warn("DypTextDef failed");
            _dypTextDef = APP_PAR::DypTextDef();
        }
    }
    return _dypTextDef;
}

/** 3DMMv1.0: *************************************************************************
    Ask the user if they want to save changes to the given doc.
***************************************************************************/
tribool APP::TQuerySaveDoc(PDOCB pdocb, bool fForce)
{
    AssertThis(0);
    AssertPo(pdocb, 0);

    STN stnName;
    int32_t tpc;
    STN stnBackup;
    int32_t bk;
    tribool tResult;

    pdocb->GetName(&stnName);
    tpc = fForce ? ktpcQuerySave : ktpcQuerySaveWithCancel;
    if (!FGetStnApp(idsSaveChangesBkp, &stnBackup))
        stnBackup.SetNil();
    bk = fForce ? bkYesNo : bkYesNoCancel;

    tResult = TModal(vpapp->PcrmAll(), tpc, &stnBackup, bk, kstidQuerySave, &stnName);
    vpcex->EnqueueCid(cidQuerySaveDocResult, pvNil, pvNil, tResult);

    return tResult;
}

/** 3DMMv1.0: *************************************************************************
    Quit routine.  May or may not initiate the quit sequence (depending
    on user input).
***************************************************************************/
void APP::Quit(bool fForce)
{
    AssertThis(0);

    bool tRet;
    STN stnBackup;

    // 3DMMv1.0: If we already know we have to quit, or a modal topic is already
    // 3DMMv1.0: being  displayed, then do not query the user to quit here.
    if (_fQuit || vpappb->CactModal() > (_pcex != pvNil ? 1 : 0) || FInPortfolio())
    {
        // 3DMMv1.0: Make sure the app is visible to the user. Otherwise if this
        // 3DMMv1.0: return is preventing a system shutdown and we're minimized
        // 3DMMv1.0: on the taskbar, then the user won't know why the shutdown failed.
        if (!_fQuit)
            EnsureInteractive();
        return;
    }

    if (fForce)
    {
        // 3DMMv1.0: Force quit, don't ask the user if they want to quit.  But
        // 3DMMv1.0: do ask if they want to save their documents.
        DOCB::FQueryCloseAll(fdocForceClose);
        _fQuit = fTrue;

        return;
    }

    // 3DMMv1.0: If we're minimized, user is closing app from the taskbar.  Quit
    // 3DMMv1.0: without confirmation (we'll still confirm movie save if user has
    // 3DMMv1.0: a dirty doc)
    if (_fMinimized)
    {
        tRet = tYes;
    }
    else
    {
        if (!FGetStnApp(idsConfirmExitBkp, &stnBackup))
            stnBackup.SetNil();
        tRet = TModal(vpapp->PcrmAll(), ktpcQueryQuit, &stnBackup, bkYesNo);
    }

    if (tRet == tYes)
    {
        // 3DMMv1.0: User wants to quit, so shut down studio if necessary
        if (_pstdio == pvNil || _pstdio->FShutdown(fFalse))
            _fQuit = fTrue;
    }
}

/** 3DMMv1.0: *************************************************************************
    Return a pointer to the current movie, if any.  The movie could be in
    the studio, theater, or splot machine.
***************************************************************************/
PMVIE APP::_Pmvie(void)
{
    AssertBaseThis(0);

    PMVIE pmvie = pvNil;
    PSPLOT psplot;

    if (_pstdio != pvNil && _pstdio->Pmvie() != pvNil)
        pmvie = _pstdio->Pmvie();
    else if (_ptatr != pvNil && _ptatr->Pmvie() != pvNil)
        pmvie = _ptatr->Pmvie();
    else if (Pkwa() != pvNil)
    {
        psplot = (PSPLOT)Pkwa()->PgobFromCls(kclsSPLOT);
        if (psplot != pvNil)
            pmvie = psplot->Pmvie();
    }
    return pmvie;
}

/***************************************************************************
    In -o playback-only mode, start the loaded movie by enqueueing the same
    Studio play command used by the Play button.  This intentionally waits
    until idle time, after the command-line movie has been loaded into Studio.
***************************************************************************/
void APP::_QueuePlaybackOnlyPlay(void)
{
    AssertThis(0);

    PMVIE pmvie;

    if (!_fPlaybackOnly || !_fPlaybackAutoPlay)
        return;

    if (_pstdio == pvNil)
        return;

    pmvie = _pstdio->Pmvie();
    if (pmvie == pvNil || pmvie->Pscen() == pvNil)
        return;

    if (pmvie->FPlaying())
    {
        _fPlaybackAutoPlay = fFalse;
        return;
    }

    _fPlaybackAutoPlay = fFalse;
    vpcex->EnqueueCid(cidPlay, _pstdio);
}


/***************************************************************************
    In -o playback-only mode, suppress the full editor shell after the movie
    has loaded and the autoplay command has been queued.  Keep the main
    window technically visible but parked far offscreen instead of SW_HIDE /
    minimize, because the legacy render/paint/timer path still drives the
    true viewport mirror.
***************************************************************************/
void APP::_HidePlaybackOnlyEditorWindow(void)
{
    AssertBaseThis(0);

#if defined(KAUAI_WIN32)
    PMVIE pmvie;
    RECT rcs;
    int32_t dxpWindow;
    int32_t dypWindow;
    int32_t lwExStyle;
    bool fPlaybackShellReady;

    if (!_fPlaybackOnly || _fPlaybackEditorHidden)
        return;

    if (!_fMainWindowCreated || vwig.hwndApp == hNil || !IsWindow(vwig.hwndApp))
        return;

    // The old v23 Studio path waited for _pstdio because that was the object
    // graph that owned playback.  The -o path now enters Theatre/TATR instead,
    // so a live _ptatr is enough to prove the playback shell exists.  Keep the
    // Studio fallback for the older path, but don't block Theatre mode behind a
    // nil _pstdio forever.
    fPlaybackShellReady = (_ptatr != pvNil);

    if (!fPlaybackShellReady && _pstdio != pvNil)
    {
        pmvie = _pstdio->Pmvie();
        if (pmvie != pvNil && pmvie->Pscen() != pvNil && !_fPlaybackAutoPlay)
            fPlaybackShellReady = fTrue;
    }

    if (!fPlaybackShellReady)
        return;

    if (_fViewportWindow)
        _FEnsureViewportWindow();

    dxpWindow = 640;
    dypWindow = 480;
    if (GetWindowRect(vwig.hwndApp, &rcs))
    {
        dxpWindow = rcs.right - rcs.left;
        dypWindow = rcs.bottom - rcs.top;
    }

    // Remove the playback shell from Alt-Tab/taskbar as much as Win32 allows
    // after creation, then park it offscreen.  Do not minimize or SW_HIDE it:
    // those paths can stall the legacy draw/timer path that feeds -v.
    lwExStyle = GetWindowLong(vwig.hwndApp, GWL_EXSTYLE);
    lwExStyle |= WS_EX_TOOLWINDOW;
    lwExStyle &= ~WS_EX_APPWINDOW;
    SetWindowLong(vwig.hwndApp, GWL_EXSTYLE, lwExStyle);

    SetWindowPos(vwig.hwndApp, HWND_BOTTOM, -32000, -32000, dxpWindow, dypWindow,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    ShowWindow(vwig.hwndApp, SW_SHOWNOACTIVATE);

    _fPlaybackEditorHidden = fTrue;
#endif // 3DMMEx: KAUAI_WIN32
}


/** 3DMMv1.0: **************************************
    Info dialog items (idits)
****************************************/
enum
{
    iditOkInfo,
    iditWindowModeInfo,
#ifdef DEBUG
    iditCactAV,
#endif // 3DMMv1.0: DEBUG
    iditProductNameInfo,
    iditSaveChanges,
    iditRenderModeInfo,
    iditStartupSound,
    iditStereoSound,
    iditHighQualitySoundImport,
    iditReduceMouseJitter,

    iditLimInfo
};

#ifdef WIN

/** 3DMMv1.0: *************************************************************************
    Useful function
***************************************************************************/
char *LoadGenResource(HINSTANCE hInst, LPCSTR lpResource, LPCSTR lpType)
{
    HRSRC hResource;
    HGLOBAL hGbl;

    hResource = FindResourceA(hInst, lpResource, lpType);

    if (hResource == NULL)
        return (NULL);

    hGbl = LoadResource(hInst, hResource);

    return (char *)LockResource(hGbl);
}

#endif

/** 3DMMv1.0: *************************************************************************
    Put up info dialog
***************************************************************************/
bool APP::FCmdInfo(PCMD pcmd)
{
    AssertThis(0);
    PMVIE pmvie = pvNil;
    PDLG pdlg;
    int32_t idit;
    bool fRunInWindowNew;
    STN stn;
    STN stnT, stnGitTag;
    SZS szsT = szGitTag;
    bool fSaveChanges;
    int32_t lwValue = 0;
    bool fValue = fFalse;

    stn = PszLit("3DMMEx");
#ifdef DEBUG
    stn.FAppendSz(PszLit(" (Debug)"));
#endif // 3DMMv1.0: DEBUG
    stnGitTag.SetSzs(szsT);
    stnT.FFormatSz(PszLit(" %d.%d.%d (%s)"), rmj, rmm, rup, &stnGitTag);
    stn.FAppendStn(&stnT);

#if defined(KAUAI_WIN32)

    pmvie = _Pmvie();

    pdlg = DLG::PdlgNew(dlidInfo, pvNil, pvNil);
    if (pvNil == pdlg)
        return fTrue;
    pdlg->PutRadio(iditRenderModeInfo, _fSlowCPU ? 1 : 0);

    pdlg->FPutStn(iditProductNameInfo, &stn);

#ifdef DEBUG
    pdlg->FPutLwInEdit(iditCactAV, vcactAV);
#endif // 3DMMv1.0: DEBUG
    pdlg->PutRadio(iditWindowModeInfo, _fRunInWindow ? 1 : 0);

    // 3DMMEx: Get startup sound option
    lwValue = kfStartupSoundDefault;
    (void)FGetSetRegKey(kszStartupSoundValue, &lwValue, SIZEOF(lwValue), fregNil);
    pdlg->PutCheck(iditStartupSound, FPure(lwValue));

    // 3DMMEx: Get sound options
    lwValue = 0;
    AssertDo(FGetProp(kpridStereoSoundPlayback, &lwValue), "can't get stereo sound property");
    pdlg->PutCheck(iditStereoSound, FPure(lwValue));

    lwValue = 0;
    AssertDo(FGetProp(kpridHighQualitySoundImport, &lwValue), "can't get sound import property");
    pdlg->PutCheck(iditHighQualitySoundImport, FPure(lwValue));

    lwValue = 0;
    AssertDo(FGetProp(kpridReduceMouseJitter, &lwValue), "can't get reduce mouse jitter property");
    pdlg->PutCheck(iditReduceMouseJitter, FPure(lwValue));

    // 3DMMEx: Show dialog
    idit = pdlg->IditDo();

    fSaveChanges = pdlg->FGetCheck(iditSaveChanges);
    if (FPure(_fSlowCPU) != FPure(pdlg->LwGetRadio(iditRenderModeInfo)))
    {
        PMVIE pmvie;

        _fSlowCPU = !_fSlowCPU;
        pmvie = _Pmvie();
        if (pvNil != pmvie)
        {
            pmvie->Pbwld()->FSetHalfMode(fFalse, _fSlowCPU);
            pmvie->InvalViews();
        }
    }
    if (fSaveChanges)
    {
        int32_t fSlowCPU = _fSlowCPU;
        FGetSetRegKey(kszBetterSpeedValue, &fSlowCPU, SIZEOF(fSlowCPU), fregSetKey);
    }

    fRunInWindowNew = pdlg->LwGetRadio(iditWindowModeInfo);
    AssertDo(_FSetRunInWindow(fRunInWindowNew), "Could not change window mode");

    if (fSaveChanges)
    {
        int32_t fSwitchRes = !_fRunInWindow;

        FGetSetRegKey(kszSwitchResolutionValue, &fSwitchRes, SIZEOF(fSwitchRes), fregSetKey);
    }

#ifdef DEBUG
    {
        bool fEmpty;
        int32_t lwT;

        if (pdlg->FGetLwFromEdit(iditCactAV, &lwT, &fEmpty) && !fEmpty)
        {
            if (lwT < 0)
            {
                Debugger();
            }
            else
            {
                vcactAV = lwT;
                if (fSaveChanges)
                {
                    DBINFO dbinfo;

                    dbinfo.cactAV = vcactAV;
                    AssertDo(FGetSetRegKey(PszLit("DebugSettings"), &dbinfo, SIZEOF(DBINFO), fregSetKey | fregBinary),
                             "Couldn't save current debug settings in registry");
                }
            }
        }
    }
#endif // 3DMMv1.0: DEBUG

    // 3DMMEx: Save startup sound preference
    if (fSaveChanges)
    {
        int32_t lwValue = pdlg->FGetCheck(iditStartupSound);
        AssertDo(FGetSetRegKey(kszStartupSoundValue, &lwValue, SIZEOF(lwValue), fregSetKey),
                 "can't save startup sound to registry");
    }

    // 3DMMEx: Set audio preferences
    fValue = pdlg->FGetCheck(iditStereoSound);
    AssertDo(FSetProp(kpridStereoSoundPlayback, fValue), "can't save stereo sound property");

    fValue = pdlg->FGetCheck(iditHighQualitySoundImport);
    AssertDo(FSetProp(kpridHighQualitySoundImport, fValue), "can't save sound import property");

    fValue = pdlg->FGetCheck(iditReduceMouseJitter);
    AssertDo(FSetProp(kpridReduceMouseJitter, fValue), "can't save reduce mouse jitter property");

    if (fSaveChanges)
    {
        AssertDo(FGetProp(kpridHighQualitySoundImport, &lwValue), "can't get sound import property");
        AssertDo(FGetSetRegKey(kszHighQualitySoundImport, &lwValue, SIZEOF(lwValue), fregSetKey),
                 "can't save sound import preference to registry");

        AssertDo(FGetProp(kpridStereoSoundPlayback, &lwValue), "can't get stereo sound property");
        AssertDo(FGetSetRegKey(kszStereoSound, &lwValue, SIZEOF(lwValue), fregSetKey),
                 "can't save stereo sound preference to registry");

        // 3DMMEx: TODO: Save "flush mouse" property to the registry
    }

    ReleasePpo(&pdlg);

    return fTrue;

#else  // 3DMMEx: !KAUAI_WIN32
    // 3DMMEx: FUTURE: Add complete SDL dialog support
    stn.FAppendSz(PszLit("\n\nSettings dialog not implemented yet.\nYou can change settings by editing 3dmovie.ini."));
    (void)TGiveAlertSz(stn.Psz(), bkOk, cokInformation);
    return fTrue;
#endif // 3DMMEx: KAUAI_WIN32
}

#ifdef WIN
#ifdef UNICODE
typedef LONG(WINAPI *PFNCHDS)(LPDEVMODEW lpDevMode, DWORD dwFlags);
const char kpszChds[] = "ChangeDisplaySettingsW";
#else
typedef LONG(WINAPI *PFNCHDS)(LPDEVMODEA lpDevMode, DWORD dwFlags);
const PCSZ kpszChds = PszLit("ChangeDisplaySettingsA");
#endif // 3DMMv1.0: !UNICODE

#ifdef BUG1920
#ifdef UNICODE
typedef BOOL(WINAPI *PFNENUM)(LPCWSTR lpszDeviceName, DWORD iModeNum, LPDEVMODEW lpDevMode);
const PSZ kpszEnum = PszLit("EnumDisplaySettingsW");
#else
typedef BOOL(WINAPI *PFNENUM)(LPCSTR lpszDeviceName, DWORD iModeNum, LPDEVMODEA lpDevMode);
const PSZ kpszEnum = PszLit("EnumDisplaySettingsA");
#endif // 3DMMv1.0: !UNICODE
#endif // 3DMMv1.0: BUG1920
#endif // 3DMMv1.0: WIN

#ifndef DM_BITSPERPEL
#define DM_BITSPERPEL 0x00040000L // 3DMMv1.0: from wingdi.h
#define DM_PELSWIDTH 0x00080000L
#define DM_PELSHEIGHT 0x00100000L
#endif //! 3DMMv1.0: DM_BITSPERPEL

#ifndef CDS_FULLSCREEN
#define CDS_FULLSCREEN 4
#endif //! 3DMMv1.0: CDS_FULLSCREEN

#ifndef DISP_CHANGE_SUCCESSFUL
#define DISP_CHANGE_SUCCESSFUL 0
#endif //! 3DMMv1.0: DISP_CHANGE_SUCCESSFUL

/** 3DMMv1.0: *************************************************************************
    Determine if display resolution switching is supported
***************************************************************************/
bool APP::_FDisplaySwitchSupported(void)
{
    AssertBaseThis(0);

#ifdef WIN
    // 3DMMEx: We can no longer compile for Windows platforms that do not support screen resolution changes.
    return fTrue;
#else
    // 3DMMEx: SDL does support res-switching via upscaling
    return fTrue;
#endif // 3DMMv1.0: WIN
}

/** 3DMMv1.0: *************************************************************************
    Switch to/from 640x480x8bit video mode.  It uses GetProcAddress so it
    can fail gracefully on systems that don't support
    ChangeDisplaySettings().
***************************************************************************/
bool APP::_FSwitch640480(bool fTo640480)
{
    AssertBaseThis(0);

#ifdef KAUAI_WIN32
#ifdef BUG1920
    bool fSetMode = fFalse, fSetBbp = fTrue;
    DWORD iModeNum;
    PFNENUM pfnEnum;
#endif // 3DMMv1.0: BUG1920
    HINSTANCE hLibrary;
    PFNCHDS pfnChds;
    DEVMODE devmode;
    int32_t lwResult;

    hLibrary = LoadLibrary(PszLit("USER32.DLL"));
    if (0 == hLibrary)
        goto LFail;

    pfnChds = (PFNCHDS)GetProcAddress(hLibrary, kpszChds);
    if (pvNil == pfnChds)
        goto LFail;

#ifdef BUG1920
    pfnEnum = (PFNENUM)GetProcAddress(hLibrary, kpszEnum);
    if (pvNil == pfnEnum)
        goto LFail;
#endif // 3DMMv1.0: BUG1920

    if (fTo640480)
    {
        // 3DMMv1.0: Try to switch to 640x480
#ifdef BUG1920
    LRetry:
        for (iModeNum = 0; pfnEnum(NULL, iModeNum, &devmode); iModeNum++)
        {
            if ((fSetBbp ? devmode.dmBitsPerPel != 8 : devmode.dmBitsPerPel < 8) || devmode.dmPelsWidth != 640 ||
                devmode.dmPelsHeight != 480)
            {
                continue;
            }
            lwResult = pfnChds(&devmode, CDS_FULLSCREEN);
            if (lwResult == DISP_CHANGE_SUCCESSFUL)
            {
                fSetMode = fTrue;
                break;
            }
        }
        if (!fSetMode && fSetBbp)
        {
            fSetBbp = fFalse;
            goto LRetry;
        }

        if (fSetMode && _FDisplayIs640480())
#else  // 3DMMv1.0: BUG1920
        devmode.dmSize = SIZEOF(DEVMODE);
        devmode.dmBitsPerPel = 8;
        devmode.dmPelsWidth = 640;
        devmode.dmPelsHeight = 480;
        devmode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
        lwResult = pfnChds(&devmode, CDS_FULLSCREEN);
        if (DISP_CHANGE_SUCCESSFUL != lwResult)
        {
            // 3DMMv1.0: try without setting the bpp
            devmode.dmFields &= ~DM_BITSPERPEL;
            lwResult = pfnChds(&devmode, CDS_FULLSCREEN);
        }

        if (DISP_CHANGE_SUCCESSFUL == lwResult && _FDisplayIs640480())
#endif // 3DMMv1.0: !BUG1920
        {
            _fSwitchedResolution = fTrue;
            SetWindowPos(vwig.hwndApp, HWND_TOP, 0, 0, 640, 480, 0);
        }
        else
        {
            goto LFail;
        }
    }
    else
    {
        // 3DMMv1.0: Try to restore user's previous resolution
        lwResult = pfnChds(NULL, CDS_FULLSCREEN);
        if (DISP_CHANGE_SUCCESSFUL != lwResult)
            goto LFail;
    }
    FreeLibrary(hLibrary);
    return fTrue;
LFail:
    if (0 != hLibrary)
        FreeLibrary(hLibrary);
    return fFalse;
#elif defined(KAUAI_SDL)
    PGOB pgobScreen = GOB::PgobScreen();
    int32_t fSwitchRes = !_fRunInWindow;

    if (fTo640480)
    {
        SDL_SetWindowFullscreen((SDL_Window *)vwig.hwndApp, SDL_WINDOW_FULLSCREEN_DESKTOP);

        if (pgobScreen != pvNil)
        {
            PGPT pgpt = pgobScreen->Pgpt();

            pgpt->RebuildTexture();
        }

        _fSwitchedResolution = fTrue;
    }
    else
    {
        SDL_SetWindowFullscreen((SDL_Window *)vwig.hwndApp, 0);

        if (pgobScreen != pvNil)
        {
            PGPT pgpt = pgobScreen->Pgpt();

            pgpt->RebuildTexture();
        }
    }

    FGetSetRegKey(kszSwitchResolutionValue, &fSwitchRes, SIZEOF(fSwitchRes), fregSetKey);

    return fTrue;
#endif // 3DMMEx: KAUAI_WIN32
}



#if defined(KAUAI_WIN32)

#define kpszViewportWndCls PszLit("3DMMExViewportWnd")

#pragma pack(2)
struct BMPHDR3DMMEX
{
    BITMAPFILEHEADER bmfh;
    BITMAPINFOHEADER bmih;
};
#pragma pack()

static bool _FMakeScreenshotDir(char *szScreens)
{
    char szExe[MAX_PATH];
    char szDir[MAX_PATH];

    if (!GetModuleFileNameA(NULL, szExe, SIZEOF(szExe)))
        return fFalse;

    CopyPb(szExe, szDir, SIZEOF(szExe));
    int32_t ichMac = 0;
    while (ichMac < MAX_PATH && szDir[ichMac] != '\0')
        ichMac++;
    for (int32_t ich = ichMac - 1; ich >= 0; ich--)
    {
        if (szDir[ich] == '\\' || szDir[ich] == '/')
        {
            szDir[ich] = '\0';
            break;
        }
    }

    wsprintfA(szScreens, "%s\\screenshots", szDir);
    if (!CreateDirectoryA(szScreens, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return fFalse;

    return fTrue;
}

static bool _FSaveClientBmp(HWND hwnd, RECT *prcCapture, PCSZ pszPrefix, SYSTEMTIME *pst)
{
    RECT rcClient;
    RECT rc;
    int32_t dxp;
    int32_t dyp;
    int32_t cbRow;
    int32_t cbImage;
    HDC hdcWindow = NULL;
    HDC hdcMem = NULL;
    HBITMAP hbmp = NULL;
    HGDIOBJ hbmpOld = NULL;
    bool fRet = fFalse;
    uint8_t *prgb = pvNil;
    HANDLE hfile = INVALID_HANDLE_VALUE;
    BITMAPINFOHEADER bmih;
    BMPHDR3DMMEX bmh;
    char szScreens[MAX_PATH];
    char szFile[MAX_PATH];
    DWORD cbWritten;

    if (hwnd == hNil || !IsWindow(hwnd))
        return fFalse;

    if (!GetClientRect(hwnd, &rcClient))
        return fFalse;

    if (prcCapture != pvNil)
        rc = *prcCapture;
    else
        rc = rcClient;

    if (rc.left < rcClient.left)
        rc.left = rcClient.left;
    if (rc.top < rcClient.top)
        rc.top = rcClient.top;
    if (rc.right > rcClient.right)
        rc.right = rcClient.right;
    if (rc.bottom > rcClient.bottom)
        rc.bottom = rcClient.bottom;

    dxp = rc.right - rc.left;
    dyp = rc.bottom - rc.top;
    if (dxp <= 0 || dyp <= 0)
        return fFalse;

    hdcWindow = GetDC(hwnd);
    if (hdcWindow == NULL)
        goto LFail;

    hdcMem = CreateCompatibleDC(hdcWindow);
    if (hdcMem == NULL)
        goto LFail;

    hbmp = CreateCompatibleBitmap(hdcWindow, dxp, dyp);
    if (hbmp == NULL)
        goto LFail;

    hbmpOld = SelectObject(hdcMem, hbmp);
    if (hbmpOld == NULL)
        goto LFail;

    if (!BitBlt(hdcMem, 0, 0, dxp, dyp, hdcWindow, rc.left, rc.top, SRCCOPY))
        goto LFail;

    cbRow = ((dxp * 3 + 3) & ~3);
    cbImage = cbRow * dyp;
    prgb = (uint8_t *)malloc(cbImage);
    if (prgb == pvNil)
        goto LFail;

    ClearPb(&bmih, SIZEOF(bmih));
    bmih.biSize = SIZEOF(bmih);
    bmih.biWidth = dxp;
    bmih.biHeight = -dyp; // top-down DIB: saved image matches the visible window
    bmih.biPlanes = 1;
    bmih.biBitCount = 24;
    bmih.biCompression = BI_RGB;
    bmih.biSizeImage = cbImage;

    if (!GetDIBits(hdcMem, hbmp, 0, dyp, prgb, (BITMAPINFO *)&bmih, DIB_RGB_COLORS))
        goto LFail;

    if (!_FMakeScreenshotDir(szScreens))
        goto LFail;

    wsprintfA(szFile, "%s\\%s_%04d%02d%02d_%02d%02d%02d_%03d.bmp", szScreens, pszPrefix,
              pst->wYear, pst->wMonth, pst->wDay, pst->wHour, pst->wMinute, pst->wSecond, pst->wMilliseconds);

    hfile = CreateFileA(szFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hfile == INVALID_HANDLE_VALUE)
        goto LFail;

    ClearPb(&bmh, SIZEOF(bmh));
    bmh.bmfh.bfType = KLCONST2('M', 'B');
    bmh.bmfh.bfOffBits = SIZEOF(BITMAPFILEHEADER) + SIZEOF(BITMAPINFOHEADER);
    bmh.bmfh.bfSize = bmh.bmfh.bfOffBits + cbImage;
    bmh.bmih = bmih;

    if (!WriteFile(hfile, &bmh.bmfh, SIZEOF(bmh.bmfh), &cbWritten, NULL) || cbWritten != SIZEOF(bmh.bmfh))
        goto LFail;
    if (!WriteFile(hfile, &bmh.bmih, SIZEOF(bmh.bmih), &cbWritten, NULL) || cbWritten != SIZEOF(bmh.bmih))
        goto LFail;
    if (!WriteFile(hfile, prgb, cbImage, &cbWritten, NULL) || cbWritten != (DWORD)cbImage)
        goto LFail;

    fRet = fTrue;

LFail:
    if (hfile != INVALID_HANDLE_VALUE)
        CloseHandle(hfile);
    if (prgb != pvNil)
        free(prgb);
    if (hbmpOld != NULL && hdcMem != NULL)
        SelectObject(hdcMem, hbmpOld);
    if (hbmp != NULL)
        DeleteObject(hbmp);
    if (hdcMem != NULL)
        DeleteDC(hdcMem);
    if (hdcWindow != NULL)
        ReleaseDC(hwnd, hdcWindow);

    return fRet;
}

#endif // 3DMMEx: KAUAI_WIN32

bool APP::_FSaveScreenshot(void)
{
    AssertBaseThis(0);

#if defined(KAUAI_WIN32)
    SYSTEMTIME st;
    bool fFull;
    bool fViewport;

    if (vwig.hwndApp == hNil || !IsWindow(vwig.hwndApp))
        return fFalse;

    GetLocalTime(&st);

    // PrtSc saves both: the complete editor presentation and the true cached
    // MVU viewport. In 4x mode save what the user actually sees, not the hidden
    // 640x480 source window feeding the DWM thumbnail.
    HWND hwndApp = vwig.hwndApp;
    HWND hwndFull = F4DMMUiScaleActive() ? vhwnd4DMMUiScale : hwndApp;
    fFull = _FSaveClientBmp(hwndFull, pvNil, PszLit("full"), &st);

    _PaintViewportWindow();
    fViewport = _FSaveClientBmp(_hwndViewport, pvNil, PszLit("viewport"), &st);

    return fFull && fViewport;
#else  //! 3DMMEx: KAUAI_WIN32
    return fFalse;
#endif //! 3DMMEx: KAUAI_WIN32
}

#if defined(KAUAI_WIN32)

bool APP::_FEnsureViewportCache(void)
{
    AssertBaseThis(0);

    if (_pgptViewport != pvNil)
        return fTrue;

    RC rc;
    rc.Set(0, 0, kdxpWorkspace, kdypWorkspace);
    _pgptViewport = GPT::PgptNewOffscreen(&rc, BWLD::FTrueColorMode() ? 24 : 8);
    return _pgptViewport != pvNil;
}

bool APP::_FEnsureViewportWindow(void)
{
    AssertBaseThis(0);

#if !defined(BRENDER_MODERN_14)
    if (!_fViewportWindow)
        return fTrue;
#endif

    if (_hwndViewport != hNil && IsWindow(_hwndViewport))
        return fTrue;

    const bool fEmbeddedModernHost = F4DMMModernEmbeddedViewportHost();

    if (vfViewportResolution4x && !FEnsure4DMMUiScaleWindow())
        return fFalse;

    RECT rc4xViewport;
    if (vfViewportResolution4x && !FGet4DMM4xViewportRect(&rc4xViewport))
        return fFalse;
    if (vfViewportResolution4x && !F4DMMMakeUiScaleViewportHole(&rc4xViewport))
    {
        MODERN_BR_LOG("APP::_FEnsureViewportWindow unable to create scaled UI region compositor");
        return fFalse;
    }

    // Only log this function when it has real work to do. It is polled from
    // the editor loop, so logging the healthy no-op case generated thousands
    // of lines per minute and dominated diagnostic-mode playback time.
    MODERN_BR_LOG("APP::_FEnsureViewportWindow create requested=%d hwnd=%p app_hwnd=%p",
                  (int)_fViewportWindow, _hwndViewport, vwig.hwndApp);

    // Keep window creation separate from viewport cache creation.  The -v
    // window should exist even before the first BRender movie frame is
    // available, and creating the HWND alone avoids early graphics-port
    // assertions during startup / new-movie states.
    WNDCLASS wcs;
    ClearPb(&wcs, SIZEOF(wcs));
    wcs.style = CS_BYTEALIGNCLIENT | CS_OWNDC;
    wcs.lpfnWndProc = Lresult4DMMViewportWndProc;
    wcs.cbClsExtra = 0;
    wcs.cbWndExtra = 0;
    wcs.hInstance = vwig.hinst;
    wcs.hIcon = LoadIcon(vwig.hinst, MAKEINTRESOURCE(IDI_APP));
    wcs.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcs.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcs.lpszMenuName = 0;
    wcs.lpszClassName = kpszViewportWndCls;
    if (!RegisterClass(&wcs) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        MODERN_BR_LOG("APP::_FEnsureViewportWindow RegisterClass FAIL gle=%lu", (unsigned long)GetLastError());
        return fFalse;
    }
    MODERN_BR_LOG("APP::_FEnsureViewportWindow class registered/already exists");

    // Ordinary/scaled Modern mode no longer creates a second top-level GL
    // application surface at all. The WGL context lives on a hidden child of
    // the original Kauai root; only native 1080p keeps the external top-level
    // compatibility window. This is an architectural ownership change, not a
    // Z-order bandage: presentation is now explicitly somebody else's job.
    uint32_t dwStyle = fEmbeddedModernHost
                           ? (WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS)
                           : vfViewportResolution4x
                                 ? (WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS)
                                 : (WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    const uint32_t dwExStyle = fEmbeddedModernHost
                                   ? 0
                                   : vfViewportResolution4x
                                         ? (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) : 0;
    const int32_t dxpViewportClient = vfViewportResolution4x ? Lw4DMMScaleSourceToPresentation(kdxpWorkspace)
                                         : vfViewportResolution1080p ? 1920
                                         : kdxpWorkspace;
    const int32_t dypViewportClient = vfViewportResolution4x ? Lw4DMMScaleSourceToPresentation(kdypWorkspace)
                                         : vfViewportResolution1080p ? 1080
                                         : kdypWorkspace;
    int32_t dxpWindow = dxpViewportClient;
    int32_t dypWindow = dypViewportClient;
    int32_t xpWindow = CW_USEDEFAULT;
    int32_t ypWindow = CW_USEDEFAULT;
    // The embedded host is a real child of Kauai's root rather than an owned
    // or independent top-level window, so it cannot appear in Alt-Tab/taskbar
    // ordering or jump in front of native Kauai easels/tooltips. The old 4x
    // popup source path remains only for non-embedded/native compatibility.
    HWND hwndViewportParent = fEmbeddedModernHost ? vwig.hwndApp : hNil;

    if (fEmbeddedModernHost)
    {
        if (hwndViewportParent == hNil || !IsWindow(hwndViewportParent))
            return fFalse;
        // Hidden child WGL host: exact client size, no non-client chrome. Its
        // pixels are consumed through BWLD readback / presentation caches.
        xpWindow = 0;
        ypWindow = 0;
    }
    else if (vfViewportResolution4x)
    {
        POINT ptViewport = {rc4xViewport.left, rc4xViewport.top};
        if (!ClientToScreen(vhwnd4DMMUiScale, &ptViewport))
            return fFalse;
        xpWindow = ptViewport.x;
        ypWindow = ptViewport.y;
    }
    else
    {
        RECT rcs;
        rcs.left = 0;
        rcs.top = 0;
        rcs.right = dxpViewportClient;
        rcs.bottom = dypViewportClient;
        AdjustWindowRect(&rcs, dwStyle, fFalse);
        dxpWindow = rcs.right - rcs.left;
        dypWindow = rcs.bottom - rcs.top;

        if (vwig.hwndApp != hNil && IsWindow(vwig.hwndApp))
        {
            RECT rcMain;
            GetWindowRect(vwig.hwndApp, &rcMain);
            xpWindow = rcMain.right + 8;
            ypWindow = rcMain.top;
            if (xpWindow + dxpWindow > GetSystemMetrics(SM_CXSCREEN))
                xpWindow = LwMax(rcMain.left - dxpWindow - 8, 0);
            if (ypWindow + dypWindow > GetSystemMetrics(SM_CYSCREEN))
                ypWindow = LwMax((GetSystemMetrics(SM_CYSCREEN) - dypWindow) / 2, 0);
        }
    }

    int32_t scaleNum = 1;
    int32_t scaleDen = 1;
    if (vfViewportResolution4x)
        Get4DMMUiScaleRatio(&scaleNum, &scaleDen);
    MODERN_BR_LOG("APP::_FEnsureViewportWindow CreateWindowEx pos=(%ld,%ld) size=%ldx%ld hinst=%p parent=%p scaled=%d scale=%ld/%ld",
                  (long)xpWindow, (long)ypWindow, (long)dxpWindow, (long)dypWindow, vwig.hinst,
                  hwndViewportParent, (int)vfViewportResolution4x,
                  (long)scaleNum, (long)scaleDen);
    PCSZ pszViewportTitle = fEmbeddedModernHost
                                     ? PszLit("4DMM Modern Render Host")
                                     : PszLit("4DMM Viewport (External)");
    _hwndViewport = CreateWindowEx(dwExStyle, kpszViewportWndCls, pszViewportTitle, dwStyle,
                                   xpWindow, ypWindow, dxpWindow, dypWindow, hwndViewportParent, hNil,
                                   vwig.hinst, pvNil);
    if (_hwndViewport == hNil)
    {
        MODERN_BR_LOG("APP::_FEnsureViewportWindow CreateWindowEx FAIL gle=%lu", (unsigned long)GetLastError());
        return fFalse;
    }
    if (vfViewportResolution4x)
    {
        vhwnd4DMMUiScaleViewport = _hwndViewport;
        if (fEmbeddedModernHost)
        {
            ShowWindow(_hwndViewport, SW_HIDE);
            MODERN_BR_LOG("APP::_FEnsureViewportWindow embedded GL host hidden; scaled presentation owns movie pixels");
        }
        else
        {
            ShowWindow(_hwndViewport, SW_SHOWNOACTIVATE);
            Park4DMMUiScaleViewportSource();
        }
        if (!F4DMMUpdateUiScaleViewportThumbnail(fTrue))
        {
            MODERN_BR_LOG("APP::_FEnsureViewportWindow unable to configure scaled native viewport source");
            DestroyWindow(_hwndViewport);
            _hwndViewport = hNil;
            vhwnd4DMMUiScaleViewport = hNil;
            return fFalse;
        }
    }

    RECT rcViewportActual;
    GetClientRect(_hwndViewport, &rcViewportActual);
    MODERN_BR_LOG("APP::_FEnsureViewportWindow created hwnd=%p client_target=%dx%d client_actual=%ldx%ld native_1080p=%d ui_scaled=%d scale=%ld/%ld",
                  _hwndViewport, (int)dxpViewportClient, (int)dypViewportClient,
                  (long)(rcViewportActual.right - rcViewportActual.left),
                  (long)(rcViewportActual.bottom - rcViewportActual.top),
                  (int)vfViewportResolution1080p, (int)vfViewportResolution4x,
                  (long)scaleNum, (long)scaleDen);
#if defined(BRENDER_MODERN_14)
    if (fEmbeddedModernHost)
    {
        ShowWindow(_hwndViewport, SW_HIDE);
        MODERN_BR_LOG("APP::_FEnsureViewportWindow modern embedded host active hwnd=%p wh=%ldx%ld",
                      _hwndViewport, (long)dxpViewportClient, (long)dypViewportClient);
    }
    else if (_fViewportWindow)
    {
        if (!vfViewportResolution4x)
            ShowWindow(_hwndViewport, SW_SHOWNOACTIVATE);
    }
    else
        ShowWindow(_hwndViewport, SW_HIDE);
#else
    ShowWindow(_hwndViewport, SW_SHOWNOACTIVATE);
#endif
#if defined(BRENDER_MODERN_14)
    // The GL bridge only records the HWND here. BRender itself may not have
    // started yet; renderer/context creation is deferred until BWLD::Render.
    MODERN_BR_LOG("APP::_FEnsureViewportWindow handing HWND to modern renderer");
    BrModernViewportSetWindow((void *)_hwndViewport);
    MODERN_BR_LOG("APP::_FEnsureViewportWindow modern SetWindow returned ready=%d",
                  (int)FBrModernViewportReady());
#endif
    MODERN_BR_LOG("APP::_FEnsureViewportWindow SUCCESS hwnd=%p", _hwndViewport);
    return fTrue;
}

void APP::_CloseViewportWindow(void)
{
    AssertBaseThis(0);
    MODERN_BR_LOG("APP::_CloseViewportWindow enter hwnd=%p valid=%d", _hwndViewport,
                  _hwndViewport != hNil ? (int)IsWindow(_hwndViewport) : 0);

#if defined(BRENDER_MODERN_14)
    // Destroy the OpenGL context/device while the HWND/DC are still valid.
    BrModernViewportSetWindow(pvNil);
#endif
    if (vfViewportResolution4x)
        Clear4DMMUiScaleViewportThumbnail();
    if (_hwndViewport != hNil && IsWindow(_hwndViewport))
        DestroyWindow(_hwndViewport);
    if (_hwndViewport == vhwnd4DMMUiScaleViewport)
        vhwnd4DMMUiScaleViewport = hNil;
    _hwndViewport = hNil;

    ReleasePpo(&_pgptViewport);
    if (vfViewportResolution4x)
        Close4DMMUiScaleWindow();
    MODERN_BR_LOG("APP::_CloseViewportWindow complete");
}

void APP::_PaintViewportWindow(void)
{
    AssertBaseThis(0);

    if (!_fViewportWindow)
        return;

    if (vfViewportResolution4x && !FEnsure4DMMUiScaleWindow())
        return;

    if (_hwndViewport == hNil || !IsWindow(_hwndViewport))
    {
        _hwndViewport = hNil;
        if (!_FEnsureViewportWindow())
            return;
    }

#if defined(BRENDER_MODERN_14)
    if (FBrModernViewportReady())
    {
        // A completed modern frame is presented exactly once by
        // FBrModernViewportRender(). The generic Kauai UpdateHwnd,
        // _FastUpdate and WM_TIMER paths can all reach this function for the
        // same frame; presenting again here produced redundant SwapBuffers
        // calls (commonly two extras per rendered frame in the v88 log).
        // Real window exposure is still handled by the viewport WM_PAINT
        // handler, which calls BrModernViewportPresent() explicitly.
        return;
    }
    if (vfViewportResolution1080p || vfViewportResolution4x)
        return;
#endif

    if (_pgptViewport == pvNil || IsIconic(_hwndViewport))
        return;

    PGPT pgptWnd = GPT::PgptNewHwnd(_hwndViewport);
    if (pgptWnd == pvNil)
        return;

    RC rc;
    rc.Set(0, 0, kdxpWorkspace, kdypWorkspace);
    GNV gnvWnd(pgptWnd);
    GNV gnvViewport(_pgptViewport);
    gnvWnd.CopyPixels(&gnvViewport, &rc, &rc);

    ReleasePpo(&pgptWnd);
}

void APP::MirrorViewportFromGnv(PGNV pgnvSrc, RC *prcSrc, RC *prcDst)
{
    AssertBaseThis(0);
    AssertPo(pgnvSrc, 0);
    AssertVarMem(prcSrc);
    AssertVarMem(prcDst);

    if (!_fViewportWindow)
        return;

    if (!_FEnsureViewportWindow())
        return;

#if defined(BRENDER_MODERN_14)
    // Once glrend owns -v, the GPU frontbuffer is authoritative. Do not
    // paint the readback copy over it with GDI. Native 1080p and 4x modes
    // never fall back to the 544x306 mirror because that would defeat the
    // native render target.
    if (FBrModernViewportReady() || vfViewportResolution1080p || vfViewportResolution4x)
        return;
#endif

    if (!_FEnsureViewportCache())
        return;

    RC rcSrc = *prcSrc;
    RC rcDst = *prcDst;
    RC rcClip;
    rcClip.Set(0, 0, kdxpWorkspace, kdypWorkspace);
    if (!rcDst.FIntersect(&rcClip))
        return;

    // Copy the true BWLD render buffer into the standalone viewport cache.
    // Source and destination rectangles may differ when half-resolution
    // rendering/stretching is active, so keep both rectangles instead of
    // assuming a same-origin crop from the editor window.
    GNV gnvViewport(_pgptViewport);
    gnvViewport.CopyPixels(pgnvSrc, &rcSrc, &rcDst);
    _PaintViewportWindow();
}

void AppMirrorViewportFromGnv(PGNV pgnvSrc, RC *prcSrc, RC *prcDst)
{
    vapp.MirrorViewportFromGnv(pgnvSrc, prcSrc, prcDst);
}

#endif // 3DMMEx: KAUAI_WIN32

bool APP::_FSetRunInWindow(bool fRunInWindowNew)
{
    AssertThis(0);

    if (FPure(_fRunInWindow) != FPure(fRunInWindowNew))
    {
        if (!fRunInWindowNew)
        {
            // 3DMMv1.0: user wants to be fullscreen
            if (_FDisplaySwitchSupported())
            {
                _fRunInWindow = fFalse;
                _RebuildMainWindow();
                if (!_FSwitch640480(fTrue))
                {
                    _fRunInWindow = fTrue;
                    _RebuildMainWindow();
                }
                else
                {
                    // Modern Windows commonly accepts 640x480 while keeping a
                    // true-colour desktop. Re-realize 3DMM's indexed UI palette
                    // after the display transition so Comic Sans/UI colours do
                    // not inherit stale palette mappings.
                    _FSetInitialPalette();
                    InvalidateRect(vwig.hwndApp, NULL, TRUE);
                }
            }
        }
        else
        {
            // 3DMMv1.0: user wants to run in a window.
            // 3DMMv1.0: Don't allow user to run in a window at 640x480 resolution.
            if (!_FDisplayIs640480() || _fSwitchedResolution)
            {
                _fRunInWindow = fTrue;
                _RebuildMainWindow();
                if (_FSwitch640480(fFalse))
                {
                    _fSwitchedResolution = fFalse;
                    _FSetInitialPalette();
                    InvalidateRect(vwig.hwndApp, NULL, TRUE);
                }
                else
                {
                    // 3DMMv1.0: back to fullscreen
                    _fRunInWindow = fFalse;
                    _RebuildMainWindow();
                }
            }
        }
    }

    return FPure(_fRunInWindow) == FPure(fRunInWindowNew);
}

/** 3DMMv1.0: *************************************************************************
    Clean up routine - app is shutting down
***************************************************************************/
void APP::_CleanUp(void)
{
#if defined(KAUAI_WIN32)
    // The original 640x480 Kauai source HWND normally lives directly behind
    // the scaled presentation. Hide it before any 4DMM-owned tool/presentation
    // teardown so shutdown cannot expose one final flash of the "baby" window.
    if (vwig.hwndApp != hNil && IsWindow(vwig.hwndApp))
        ShowWindow(vwig.hwndApp, SW_HIDE);
    Close4DMMSettingsForShutdown();
    _CloseViewportWindow();
#endif // 3DMMEx: KAUAI_WIN32
    _FWriteUserData();
    ReleasePpo(&_pstdio);
    ReleasePpo(&_ptatr);
    ReleasePpo(&_pcfl);
    ReleasePpo(&_pmvieHandoff);
    ReleasePpo(&_pcrmAll);
    ReleasePpo(&_pglicrfBuilding);
    ReleasePpo(&_pglicrfStudio);
    ReleasePpo(&_pgstBuildingFiles);
    ReleasePpo(&_pgstStudioFiles);
    ReleasePpo(&_pgstSharedFiles);
    ReleasePpo(&_pgstApp);
    ReleasePpo(&_pkwa);
    BWLD::CloseBRender();
    APP_PAR::_CleanUp();
    if (_fSwitchedResolution)
        _FSwitch640480(fFalse); // 3DMMv1.0: try to restore desktop
}

/** 3DMMv1.0: *************************************************************************
    Put up a modal help balloon
***************************************************************************/
tribool APP::TModal(PRCA prca, int32_t tpc, PSTN pstnBackup, int32_t bkBackup, int32_t stidSubst, PSTN pstnSubst)
{
    AssertThis(0);
    AssertNilOrPo(prca, 0);

    int32_t lwSelect;
    tribool tRet;
    STN stn;

    // 3DMMv1.0: If app is minimized, restore it so user can see the dialog
    EnsureInteractive();

    if (ivNil != stidSubst)
    {
        AssertPo(pstnSubst, 0);
        if (!Pkwa()->Pstrg()->FPut(stidSubst, pstnSubst))
            return tMaybe;
    }

    bool fModalTopic = fFalse;
    MODERN_BR_LOG("KAUAI TModal enter topic=%ld backup=%ld prca=%p subst_id=%ld",
                  (long)tpc, (long)bkBackup, prca, (long)stidSubst);
#if defined(KAUAI_WIN32)
    // The classic circular-OK Kauai balloons hide the ordinary Windows cursor
    // and can be awkward to dismiss when an error repeats. Mark only OK-only
    // modal topics so APPB can translate Escape into the same Return/OK key;
    // Yes/No/Cancel questions keep their historical Escape semantics.
    const bool fEscapeDismissOk = bkBackup == bkOk && vwig.hwndApp != hNil && IsWindow(vwig.hwndApp);
    if (fEscapeDismissOk)
        SetPropA(vwig.hwndApp, "4DMMExEscapeDismissOkModal", (HANDLE)1);
#endif
    if (pvNil != prca)
        fModalTopic = Pkwa()->FModalTopic(prca, tpc, &lwSelect);
#if defined(KAUAI_WIN32)
    if (fEscapeDismissOk)
        RemovePropA(vwig.hwndApp, "4DMMExEscapeDismissOkModal");
#endif

    if (!fModalTopic)
    {
        MODERN_BR_LOG("KAUAI TModal fallback topic=%ld backup=%ld", (long)tpc, (long)bkBackup);
        // 3DMMv1.0: Backup plan: use old TGiveAlertSz.
        if (pvNil != pstnSubst)
            stn.FFormat(pstnBackup, pstnSubst);
        else
            stn = *pstnBackup;
        tRet = TGiveAlertSz(stn.Psz(), bkBackup, cokExclamation);
    }
    else
    {
        // 3DMMv1.0: Help balloon appeared ok, so translate returned value into
        // 3DMMv1.0: something we can use here.  lwSelect is returned as the
        // 3DMMv1.0: (1 based) index of the btn clicked, out of Yes/No/Cancel.
        switch (lwSelect)
        {
        case 1:
            tRet = tYes;
            break;
        case 2:
            tRet = tNo;
            break;
        case 3:
            tRet = tMaybe;
            break;
        default:
            Bug("TModal() balloon returned unrecognized selection");
            tRet = tMaybe;
        }
    }

    if (ivNil != stidSubst)
        Pkwa()->Pstrg()->Delete(stidSubst);
    UpdateMarked(); // 3DMMv1.0: make sure alert is drawn over
    return tRet;
}

/** 3DMMv1.0: *************************************************************************
    Static function to prompt the user to insert the CD named pstnTitle
***************************************************************************/
bool APP::FInsertCD(PSTN pstnTitle)
{
    AssertPo(pstnTitle, 0);

    STN stnBackup;
    bool tRet;

    stnBackup = PszLit("I can't find the CD '%s'  Please insert it.  Should I look again?");
    tRet = vpapp->TModal(vpapp->PcrmAll(), ktpcQueryCD, &stnBackup, bkYesNo, kstidQueryCD, pstnTitle);

    /* 3DMMv1.0: Don't tell the user that they told us not to try again; they know
        that already */
    if (tRet == tNo)
        vapp._fDontReportInitFailure = fTrue;

    return (tYes == tRet);
}

/** 3DMMv1.0: ****************************************************************************
    DisplayErrors
        Displays any errors that happen to be on the error stack.  Call this
        when you don't want to wait until idle time to show errors.
************************************************************ PETED ***********/
void APP::DisplayErrors(void)
{
    AssertThis(0);

    int32_t erc;
    STN stnMessage;

    if (vpers->FPop(&erc))
    {
        STN stnErr;

#if defined(KAUAI_WIN32)
        PMVIE pmvieError = _pstdio != pvNil ? _pstdio->Pmvie() : pvNil;
        MVIE::MultiLog(pmvieError,
                       "error_dialog erc=%ld parsed=%d explicit=%d blank=%d armed=%d scene=%d",
                       (long)erc, (int)vf4DMMCommandLineParsed,
                       (int)vf4DMMExplicitStartupDocument, (int)vf4DMMBlankStartupPhase,
                       (int)vf4DMMSuppressNextBlankStartupFileError,
                       (int)(pmvieError != pvNil && pmvieError->Pscen() != pvNil));

        const bool fFileFamilyError =
            erc == 400 || erc == ercCflOpen || erc == ercFileOpen || erc == ercFileGeneral ||
            erc == ercFniGeneral || erc == ercFilePerm;

        // The legacy Kauai ercSocNoModlForChar modal is drawn inside the old
        // 640x480 source window.  Once a native Modern APE owns the 3D Word
        // preview, that modal can exist and capture all input while remaining
        // completely invisible behind the presentation compositor.  Promote
        // this one error to a real Win32 modal instead.  ENTER works normally
        // and there is no hidden dialog to make the editor look frozen.
        if (erc == ercSocNoModlForChar)
        {
            HWND hwndOwner = hNil;
            if (vhwnd4DMMUiScale != hNil && IsWindow(vhwnd4DMMUiScale))
                hwndOwner = vhwnd4DMMUiScale;
            else if (vwig.hwndApp != hNil && IsWindow(vwig.hwndApp))
                hwndOwner = vwig.hwndApp;

            vpers->Clear();
            MVIE::MultiLog(pmvieError,
                "font_missing_model direct_dialog owner=%p", hwndOwner);
            MessageBoxA(hwndOwner,
                "The selected 3D Word font does not contain one or more of the characters you typed.",
                "4DMM error", MB_OK | MB_ICONWARNING | MB_TASKMODAL | MB_SETFOREGROUND);
            return;
        }

        // Invalid VMM validation has its own explicit Windows message and must
        // not be followed by the generic 1995 file-error modal.  The resolver
        // clears vpers before showing the direct message, but a caller/CFL can
        // still synthesize one file-family error on the next idle pass.
        if (vf4DMMInvalidVmmMessageActive && fFileFamilyError)
        {
            vf4DMMInvalidVmmMessageActive = fFalse;
            vpers->Clear();
            MVIE::MultiLog(pmvieError, "vmm_invalid generic_error_suppressed erc=%ld", (long)erc);
            return;
        }

        // The startup erc=400 can occur before _ParseCommandLine has finished.
        // Therefore the filter starts armed statically and, until parsing is
        // complete, the only safe facts available are: there is no current
        // scene and this is the single process-start file-family error. After
        // parsing, an explicit command-line document immediately disarms it.
        // Never use the portfolio FNI here; v155 proved it can be stale/default.
        const bool fNoStartupScene = pmvieError == pvNil || pmvieError->Pscen() == pvNil;
        const bool fNoExplicitDocument = !vf4DMMCommandLineParsed || !vf4DMMExplicitStartupDocument;
        const bool fBlankStartupFilter = vf4DMMSuppressNextBlankStartupFileError &&
                                         vf4DMMBlankStartupPhase &&
                                         fNoExplicitDocument && fNoStartupScene;
        if (fBlankStartupFilter && fFileFamilyError)
        {
            vf4DMMSuppressNextBlankStartupFileError = fFalse;
            vf4DMMBlankStartupPhase = fFalse;
            vpers->Clear();
            MVIE::MultiLog(pmvieError,
                           "startup_error_suppressed erc=%ld parsed=%d explicit=%d scene=0",
                           (long)erc, (int)vf4DMMCommandLineParsed,
                           (int)vf4DMMExplicitStartupDocument);
            return;
        }
#endif
        vpers->Clear();

        //
        // 3DMMv1.0: Convert to help topic number
        //
        switch (erc)
        {
        case ercOomHq:
            erc = ktpcercOomHq;
            break;

        case ercOomPv:
            erc = ktpcercOomPv;
            break;

        case ercOomNew:
            erc = ktpcercOomNew;
            break;

        /* 3DMMv1.0: We don't have any real specific information to present the user in
            these cases, plus they generally shouldn't come up anyway (a more
            informational error should have been pushed farther up the chain
            of error reporters) */
        case ercFilePerm:
        case ercFileOpen:
        case ercFileCreate:
        case ercFileSwapNames:
        case ercFileRename:
        case ercFniGeneral:
        case ercFniDelete:
        case ercFniRename:
        case ercFniMismatch:
        case ercFniDirCreate:
        case ercFneGeneral:
        case ercCflCreate:
        case ercCflSaveCopy:
        case ercSndmCantInit:
        case ercSndmPartialInit:
        case ercGfxCantDraw:
        case ercGfxCantSetFont:
        case ercGfxNoFontList:
        case ercGfxCantSetPalette:
        case ercDlgCantGetArgs:
        case ercDlgCantFind:
        case ercRtxdTooMuchText:
        case ercRtxdReadFailed:
        case ercRtxdSaveFailed:
        case ercCantOpenVideo:
        case ercMbmpCantOpenBitmap:

        /* 3DMMv1.0: In theory, these are obsolete. */
        case ercSocTdtTooLong:
        case ercSocWaveInProblems:
        case ercSocCreatedUserDir:

            /* 3DMMv1.0: Display a generic error message, with the error code in it */
            stnErr.FFormatSz(PszLit("%d"), erc);
            if (!vapp.Pkwa()->Pstrg()->FPut(kstidGenericError, &stnErr))
                stnErr.SetNil();
            erc = ktpcercSocGenericError;
            break;

        case ercFileGeneral:
            erc = ktpcercFileGeneral;
            break;

        case ercCflOpen:
            erc = ktpcercCflOpen;
            break;

        case ercCflSave:
            erc = ktpcercCflSave;
            break;

        case ercCrfCantLoad:
            erc = ktpcercCrfCantLoad;
            break;

        case ercFniHidden:
            erc = ktpcercFniHidden;
            break;

        case ercOomGdi:
            erc = ktpcercOomGdi;
            break;

        case ercDlgOom:
            erc = ktpcercDlgOom;
            break;

        case ercCantSave:
            erc = ktpcercCantSave;
            break;

        case ercSocSaveFailure:
            erc = ktpcercSocSaveFailure;
            break;

        case ercSocSceneSwitch:
            erc = ktpcercSocSceneSwitch;
            break;

        case ercSocSceneChop:
            erc = ktpcercSocSceneChop;
            break;

        case ercSocBadFile:
            erc = ktpcercSocBadFile;
            break;

        case ercSocNoTboxSelected:
            erc = ktpcercSocNoTboxSelected;
            break;

        case ercSocNoActrSelected:
            erc = ktpcercSocNoActrSelected;
            break;

        case ercSocNotUndoable:
            erc = ktpcercSocNotUndoable;
            break;

        case ercSocNoScene:
            erc = ktpcercSocNoScene;
            break;

        case ercSocBadVersion:
            erc = ktpcercSocBadVersion;
            break;

        case ercSocNothingToPaste:
            erc = ktpcercSocNothingToPaste;
            break;

        case ercSocBadFrameSlider:
            erc = ktpcercSocBadFrameSlider;
            break;

        case ercSocGotoFrameFailure:
            erc = ktpcercSocGotoFrameFailure;
            break;

        case ercSocDeleteBackFailure:
            erc = ktpcercSocDeleteBackFailure;
            break;

        case ercSocActionNotApplicable:
            erc = ktpcercSocActionNotApplicable;
            break;

        case ercSocCannotPasteThatHere:
            erc = ktpcercSocCannotPasteThatHere;
            break;

        case ercSocNoModlForChar:
            erc = ktpcercSocNoModlForChar;
            break;

        case ercSocNameTooLong:
            erc = ktpcercSocNameTooLong;
            break;

        case ercSocTboxTooSmall:
            erc = ktpcercSocTboxTooSmall;
            break;

        case ercSocNoThumbnails:
            erc = ktpcercSocNoThumbnails;
            break;

        case ercSocBadTdf:
            erc = ktpcercSocBadTdf;
            break;

        case ercSocNoActrMidi:
            erc = ktpcercSocNoActrMidi;
            break;

        case ercSocNoImportRollCall:
            erc = ktpcercSocNoImportRollCall;
            break;

        case ercSocNoNukeRollCall:
            erc = ktpcercSocNoNukeRollCall;
            break;

        case ercSocSceneSortError:
            erc = ktpcercSocSceneSortError;
            break;

        case ercSocCantInitSceneSort:
            erc = ktpcercSocCantInitSceneSort;
            break;

        case ercSocCantInitSplot:
            erc = ktpcercSocCantInitSplot;
            break;

        case ercSocNoWaveIn:
            erc = ktpcercSocNoWaveIn;
            break;

        case ercSocPortfolioFailed:
            erc = ktpcercSocPortfolioFailed;
            break;

        case ercSocCantInitStudio:
            erc = ktpcercSocCantInitStudio;
            break;

        case ercSoc3DWordCreate:
            erc = ktpcercSoc3DWordCreate;
            break;

        case ercSoc3DWordChange:
            erc = ktpcercSoc3DWordChange;
            break;

        case ercSocWaveSaveFailure:
            erc = ktpcercSocWaveSaveFailure;
            break;

        case ercSocNoSoundName:
            erc = ktpcercSocNoSoundName;
            break;

        case ercSndamWaveDeviceBusy:
            erc = ktpcercSndamWaveDeviceBusy;
            break;

        case ercSocNoKidSndsInMovie:
            erc = ktpcercSocNoKidSndsInMovie;
            break;

        case ercSocMissingMelanieDoc:
            erc = ktpcercSocMissingMelanieDoc;
            break;

        case ercSocCantLoadMelanieDoc:
            erc = ktpcercSocCantLoadMelanieDoc;
            break;

        case ercSocBadSoundFile:
            erc = ktpcercSocBadSoundFile;
            break;

        case ercSocBadSceneSound:
            erc = ktpcercSocBadSceneSound;
            break;

        case ercSocNoDefaultFont:
            erc = ktpcercSocNoDefaultFont;
            break;

        case ercSocCantCacheTag:
            erc = ktpcercSocCantCacheTag;
            break;

        case ercSocInvalidFilename:
            erc = ktpcercSocInvalidFilename;
            break;

        case ercSndMidiDeviceBusy:
            erc = ktpcercSndMidiDeviceBusy;
            break;

        case ercSocCantCopyMsnd:
            erc = ktpcercSocCantCopyMsnd;
            break;

        default:
            return;
        }

        FGetStnApp(idsOOM, &stnMessage);
        TModal(PcrmAll(), erc, &stnMessage, bkOk);
        if (stnErr.Cch() != 0)
            vapp.Pkwa()->Pstrg()->Delete(kstidGenericError);
    }
}

/** 3DMMv1.0: *************************************************************************
    Idle routine.  Do APPB idle stuff, then report any runtime errors.
***************************************************************************/
bool APP::FCmdIdle(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    bool fFileError = fFalse;
    PCFL pcfl;
    APP_PAR::FCmdIdle(pcmd);

    _QueuePlaybackOnlyPlay();
    _HidePlaybackOnlyEditorWindow();

    /* 3DMMv1.0: Check all open chunky files for errors */
    for (pcfl = CFL::PcflFirst(); pcfl != pvNil; pcfl = (PCFL)pcfl->PbllNext())
    {
        const int32_t elFile = pcfl->ElError();
        if (elFile != elNil)
        {
#if defined(KAUAI_WIN32)
            MVIE::MultiLog(_pstdio != pvNil ? _pstdio->Pmvie() : pvNil,
                           "cfl_error pcfl=%p main_resource=%d el=%ld",
                           pcfl, (int)(pcfl == _pcfl), (long)elFile);
#endif
            fFileError = fTrue;
            pcfl->ResetEl();
        }
    }
    /* 3DMMv1.0: Ensure that we report *something* if there was a file error */
    if (fFileError && vpers->Cerc() == 0)
        PushErc(ercFileGeneral);

    DisplayErrors();

#if defined(KAUAI_WIN32)
    // A direct invalid-VMM modal should need at most the current/next idle
    // cycle to swallow any generic follow-up. Expire the safety net quickly so
    // a later, unrelated file error can never disappear behind it.
    if (vf4DMMInvalidVmmMessageActive && vc4DMMInvalidVmmIdlePasses > 0)
    {
        --vc4DMMInvalidVmmIdlePasses;
        if (vc4DMMInvalidVmmIdlePasses == 0)
        {
            vf4DMMInvalidVmmMessageActive = fFalse;
            MVIE::MultiLog(_pstdio != pvNil ? _pstdio->Pmvie() : pvNil,
                           "vmm_invalid generic_error_guard expired");
        }
    }

    // Keep the process-start guard alive until its one inherited error is
    // consumed or the editor gains a real scene. Never use the portfolio FNI
    // to disarm this path: it is exactly the stale state that made v155/v156
    // unreliable. Command-line parsing has its own explicit-document flag.
    if (vf4DMMBlankStartupPhase && vf4DMMSuppressNextBlankStartupFileError)
    {
        PMVIE pmvieIdle = _pstdio != pvNil ? _pstdio->Pmvie() : pvNil;
        if ((vf4DMMCommandLineParsed && vf4DMMExplicitStartupDocument) ||
            (pmvieIdle != pvNil && pmvieIdle->Pscen() != pvNil))
        {
            vf4DMMBlankStartupPhase = fFalse;
            vf4DMMSuppressNextBlankStartupFileError = fFalse;
            MVIE::MultiLog(pmvieIdle,
                           "startup_error_filter disarmed editor_left_blank_startup explicit=%d scene=%d",
                           (int)vf4DMMExplicitStartupDocument,
                           (int)(pmvieIdle != pvNil && pmvieIdle->Pscen() != pvNil));
        }
    }
#endif
    return fTrue;
}

#ifdef WIN
/** 3DMMv1.0: *************************************************************************
    Tell another instance of the app to open a document.
***************************************************************************/
bool APP::_FSendOpenDocCmd(HWND hwnd, PFNI pfniUserDoc)
{
    AssertBaseThis(0);
    Assert(pvNil != hwnd, "bad hwnd");
    AssertPo(pfniUserDoc, ffniFile);

    STN stnUserDoc;
    STN stn;
    FNI fniTemp;
    PFIL pfil = pvNil;
    BLCK blck;
    DWORD dwProcId;

    // 3DMMv1.0: Write filename to 3DMMOpen.tmp in the temp dir
    pfniUserDoc->GetStnPath(&stnUserDoc);
    if (!fniTemp.FGetTemp())
        goto LFail;
    stn = kpszOpenFile;
    if (!fniTemp.FSetLeaf(&stn, kftgTemp))
        goto LFail;
    if (tYes == fniTemp.TExists())
    {
        // 3DMMv1.0: replace any existing open-doc request
        if (!fniTemp.FDelete())
            goto LFail;
    }
    pfil = FIL::PfilCreate(&fniTemp);
    if (pvNil == pfil)
        goto LFail;
    if (!pfil->FSetFpMac(stnUserDoc.CbData()))
        goto LFail;
    blck.Set(pfil, 0, stnUserDoc.CbData());
    if (!stnUserDoc.FWrite(&blck, 0))
        goto LFail;
    blck.Free(); // 3DMMv1.0: so it doesn't reference pfil anymore
    ReleasePpo(&pfil);
#ifdef WIN
    dwProcId = GetWindowThreadProcessId(hwnd, NULL);
    PostThreadMessage(dwProcId, WM_USER, klwOpenDoc, 0);
#else
    RawRtn();
#endif //! 3DMMv1.0: WIN
    return fTrue;
LFail:
    blck.Free(); // 3DMMv1.0: so it doesn't reference pfil anymore
    ReleasePpo(&pfil);
    return fFalse;
}
#endif // 3DMMv1.0: WIN

#ifdef WIN
/** 3DMMv1.0: *************************************************************************
    Process a request (from another instance of the app) to open a document
***************************************************************************/
bool APP::_FProcessOpenDocCmd(void)
{
    AssertBaseThis(0);

    STN stnUserDoc;
    STN stn;
    FNI fniTemp;
    PFIL pfil = pvNil;
    FNI fniUserDoc;
    BLCK blck;

    // 3DMMv1.0: Find the temp file
    if (!fniTemp.FGetTemp())
        return fFalse;
    stn = kpszOpenFile;
    if (!fniTemp.FSetLeaf(&stn, kftgTemp))
        return fFalse;
    if (tYes != fniTemp.TExists())
    {
        Bug("Got a ProcessOpenDocCmd but there's no temp file!");
        return fFalse;
    }

    // 3DMMv1.0: See if we can accept open document commands now: If accelerators
    // 3DMMv1.0: are enabled, then ctrl-o is enabled, so open-document commands are
    // 3DMMv1.0: acceptable.
    if (_cactDisable > 0 || CactModal() > 0)
        goto LFail;

    // 3DMMv1.0: Read the document filename from temp file
    pfil = FIL::PfilOpen(&fniTemp);
    if (pvNil == pfil)
        goto LFail;
    blck.Set(pfil, 0, pfil->FpMac());
    if (!stnUserDoc.FRead(&blck, 0))
        goto LFail;
    blck.Free(); // 3DMMv1.0: so it doesn't reference pfil anymore
    ReleasePpo(&pfil);

    if (!fniUserDoc.FBuildFromPath(&stnUserDoc, F4DMMPathIsVmm(&stnUserDoc) ? kftgVmm : 0))
        goto LFail;
    if (pvNil == _pstdio)
    {
        SetPortfolioDoc(&fniUserDoc);
        vpcex->EnqueueCid(cidLoadStudioDoc); // 3DMMv1.0: this will load the portfolio doc
    }
    else
    {
        // 3DMMv1.0: ignore failure
        _pstdio->FLoadMovie(&fniUserDoc);
    }

    fniTemp.FDelete(); // 3DMMv1.0: ignore failure
    return fTrue;
LFail:
    blck.Free(); // 3DMMv1.0: so it doesn't reference pfil anymore
    ReleasePpo(&pfil);
    fniTemp.FDelete(); // 3DMMv1.0: ignore failure
    return fFalse;
}
#endif // 3DMMv1.0: WIN

#ifdef KAUAI_WIN32
/** 3DMMv1.0: *************************************************************************
    Override standard _FGetNextEvt to catch WM_USER event.  Otherwise
    the event will get thrown away, because the event's hwnd is nil.
***************************************************************************/
bool APP::_FGetNextEvt(PEVT pevt)
{
    AssertThis(0);
    AssertVarMem(pevt);

    if (!APP_PAR::_FGetNextEvt(pevt))
        return fFalse;

    if (pevt->message != WM_USER || pevt->wParam != klwOpenDoc)
        return fTrue;
#ifdef WIN
    _FProcessOpenDocCmd(); // 3DMMv1.0: ignore failure
#endif
    return fFalse; // 3DMMv1.0: we've handled the WM_USER event
}
#endif // 3DMMEx: KAUAI_WIN32

/** 3DMMv1.0: *************************************************************************
    Override default _FastUpdate to optionally skip offscreen buffer
***************************************************************************/
void APP::_FastUpdate(PGOB pgob, PREGN pregnClip, uint32_t grfapp, PGPT pgpt)
{
    AssertBaseThis(0);

    PMVIE pmvie;

    pmvie = _Pmvie();

    if (_fOnscreenDrawing && pvNil != pmvie && pmvie->FPlaying() &&
        pmvie->Pscen()->Nfrm() != pmvie->Pscen()->NfrmFirst())
    {
        APP_PAR::_FastUpdate(pgob, pregnClip, grfapp | fappOnscreen, pgpt);
    }
    else
    {
        APP_PAR::_FastUpdate(pgob, pregnClip, grfapp, pgpt);
    }
#if defined(KAUAI_WIN32)
    _PaintViewportWindow();
#endif // 3DMMEx: KAUAI_WIN32
}

#ifdef WIN
/** 3DMMv1.0: *************************************************************************
    Override default UpdateHwnd to optionally skip offscreen buffer
***************************************************************************/
void APP::UpdateHwnd(KWND hwnd, RC *prc, uint32_t grfapp)
{
    AssertBaseThis(0); // 3DMMv1.0: APP may not be completely valid

    PMVIE pmvie;

    pmvie = _Pmvie();

    if (_fOnscreenDrawing && pvNil != pmvie && pmvie->FPlaying() &&
        pmvie->Pscen()->Nfrm() != pmvie->Pscen()->NfrmFirst())
    {
        APP_PAR::UpdateHwnd(hwnd, prc, grfapp | fappOnscreen);
    }
    else
    {
        APP_PAR::UpdateHwnd(hwnd, prc, grfapp);
    }
#if defined(KAUAI_WIN32)
    _PaintViewportWindow();
#endif // 3DMMEx: KAUAI_WIN32
}
#endif // 3DMMv1.0: WIN

#ifdef KAUAI_WIN32

#ifndef WM_DISPLAYCHANGE
#define WM_DISPLAYCHANGE 0x007E // 3DMMv1.0: from winuser.h
#endif                          // 3DMMv1.0: !WM_DISPLAYCHANGE

// Reusable native confirmation used by movie-wide 4DMM metadata changes.
// MessageBox cannot carry the requested "Don't ask again" checkbox, so keep
// this deliberately small and dependency-free instead of introducing a
// comctl32 TaskDialog requirement into the 1995 application.
static const achar ksz4DMMConfirmWndClass[] = "4DMMConfirmWindow";
static const int32_t kid4DMMConfirmDontAsk = 0x53F1;
struct DMMCONFIRMSTATE
{
    const achar *pszText;
    HWND hwndCheck;
    bool fDone;
    bool fYes;
    bool fDontAsk;
};

static LRESULT CALLBACK Lresult4DMMConfirmWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    DMMCONFIRMSTATE *pst = (DMMCONFIRMSTATE *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (wm)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTA *pcs = (CREATESTRUCTA *)lParam;
        pst = (DMMCONFIRMSTATE *)pcs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pst);
        break;
    }
    case WM_CREATE:
        if (pst == pvNil)
            return -1;
        // This dialog is authored directly in its final native-pixel client
        // geometry.  Do not run it through the generic 200% post-create scaler:
        // that scaler doubled the child coordinates after Windows had already
        // established a smaller client shell, clipping the prompt and leaving
        // the Yes/No buttons below the visible client area.
        CreateWindowExA(0, "STATIC", pst->pszText,
                        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                        16, 12, 618, 76, hwnd, hNil, GetModuleHandleA(pvNil), pvNil);
        // Match the checkbox's proven pre-v91 final geometry exactly.  Before
        // v91 this control was authored at 12,52,165,22 and then passed through
        // Scale4DMMExternalToolWindow200(), producing a 24,104,330,44 native
        // control.  The 44-pixel control height gives USER32 enough vertical
        // room to center the native checkbox glyph and DEFAULT_GUI_FONT text;
        // shrinking the final control to 24/22 pixels caused the glyph and the
        // descender in "again" to be clipped even though the dialog itself was
        // otherwise fixed.
        pst->hwndCheck = CreateWindowExA(0, "BUTTON", "Don't ask again",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                        24, 104, 330, 44, hwnd, (HMENU)kid4DMMConfirmDontAsk,
                        GetModuleHandleA(pvNil), pvNil);
        CreateWindowExA(0, "BUTTON", "Yes", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        466, 142, 76, 26, hwnd, (HMENU)IDYES, GetModuleHandleA(pvNil), pvNil);
        CreateWindowExA(0, "BUTTON", "No", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        558, 142, 76, 26, hwnd, (HMENU)IDNO, GetModuleHandleA(pvNil), pvNil);
        for (HWND hwndChild = GetWindow(hwnd, GW_CHILD); hwndChild != hNil;
             hwndChild = GetWindow(hwndChild, GW_HWNDNEXT))
        {
            SendMessageA(hwndChild, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
        }
        return 0;
    case WM_COMMAND:
        if (pst != pvNil && (LOWORD(wParam) == IDYES || LOWORD(wParam) == IDNO))
        {
            pst->fYes = LOWORD(wParam) == IDYES;
            pst->fDontAsk = pst->fYes && pst->hwndCheck != hNil &&
                SendMessage(pst->hwndCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
            pst->fDone = fTrue;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (pst != pvNil)
        {
            pst->fYes = fFalse;
            pst->fDone = fTrue;
        }
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcA(hwnd, wm, wParam, lParam);
}

bool F4DMMConfirmWithDontAsk(const achar *pszText, bool *pfDontAsk)
{
    if (pfDontAsk != pvNil)
        *pfDontAsk = fFalse;
    if (pszText == pvNil)
        return fFalse;

    HINSTANCE hinst = GetModuleHandleA(pvNil);
    WNDCLASSA wc;
    ClearPb(&wc, SIZEOF(wc));
    if (!GetClassInfoA(hinst, ksz4DMMConfirmWndClass, &wc))
    {
        wc.lpfnWndProc = Lresult4DMMConfirmWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(pvNil, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = ksz4DMMConfirmWndClass;
        if (RegisterClassA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return fFalse;
    }

    DMMCONFIRMSTATE st;
    ClearPb(&st, SIZEOF(st));
    st.pszText = pszText;
    HWND hwndOwner = Hwnd4DMMNativeToolOwner();
    const DWORD dwConfirmStyle = WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    const DWORD dwConfirmExStyle = WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW;
    RECT rcConfirm = {0, 0, 650, 180};
    AdjustWindowRectEx(&rcConfirm, dwConfirmStyle, fFalse, dwConfirmExStyle);
    const int32_t dxpConfirm = rcConfirm.right - rcConfirm.left;
    const int32_t dypConfirm = rcConfirm.bottom - rcConfirm.top;
    int32_t xp = CW_USEDEFAULT;
    int32_t yp = CW_USEDEFAULT;
    if (hwndOwner != hNil && IsWindow(hwndOwner))
    {
        RECT rcOwner;
        if (GetWindowRect(hwndOwner, &rcOwner))
        {
            xp = rcOwner.left + LwMax(0L, ((rcOwner.right - rcOwner.left) - dxpConfirm) / 2);
            yp = rcOwner.top + LwMax(0L, ((rcOwner.bottom - rcOwner.top) - dypConfirm) / 2);
        }
        EnableWindow(hwndOwner, fFalse);
    }

    HWND hwnd = CreateWindowExA(dwConfirmExStyle,
        ksz4DMMConfirmWndClass, "4DMM", dwConfirmStyle,
        xp, yp, dxpConfirm, dypConfirm, hwndOwner, hNil, hinst, &st);
    if (hwnd == hNil)
    {
        if (hwndOwner != hNil && IsWindow(hwndOwner))
            EnableWindow(hwndOwner, fTrue);
        return fFalse;
    }

    SetForegroundWindow(hwnd);
    MSG msg;
    while (!st.fDone && GetMessage(&msg, pvNil, 0, 0) > 0)
    {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE &&
            (msg.hwnd == hwnd || IsChild(hwnd, msg.hwnd)))
        {
            SendMessage(hwnd, WM_CLOSE, 0, 0);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (pfDontAsk != pvNil)
        *pfDontAsk = FPure(st.fDontAsk);
    if (hwndOwner != hNil && IsWindow(hwndOwner))
    {
        EnableWindow(hwndOwner, fTrue);
        SetForegroundWindow(hwndOwner);
    }
    return FPure(st.fYes);
}

// 4DMM consolidated console. Keep this as native tool windows so the legacy
// Kauai editor layout does not have to absorb another easel.
static const achar ksz4DMMSettingsWndClass[] = "4DMMSettingsWindow";
static const int32_t kid4DMMSettingsLights = 0x5401;
static const int32_t kid4DMMSettingsHideLightObjects = 0x5402;
static const int32_t kid4DMMSettingsDefaultMusic = 0x5403;
static const int32_t kid4DMMSettings100xGrow = 0x5404;
static const int32_t kid4DMMSettingsActorBrowser = 0x5405;
static const int32_t kid4DMMSettingsPropBrowser = 0x5406;
static const int32_t kid4DMMSettingsCombineBrowsers = 0x5407;
static const int32_t kid4DMMSettingsDefaultLightingShaders = 0x5408;
static const int32_t kid4DMMSettingsSceneSettings = 0x5409;
static const int32_t kid4DMMSettingsLightLabCombineLegacy = 0x540A;
static const int32_t kid4DMMSettingsAdvanced = 0x540B;
static const int32_t kid4DMMSettingsActions = 0x540C;
static const int32_t kid4DMMSettingsObjectGroups = 0x540D;
static const int32_t kid4DMMSettings100xShrink = 0x540E;
static const int32_t kid4DMMSettingsActorStudio = 0x540F;
static const int32_t kid4DMMSettingsImportVxp = 0x5410;
static const int32_t kid4DMMSceneSettingsDefaultLightingShaders = 0x5411;
static const int32_t kid4DMMSceneSettingsLightLabCombineLegacy = 0x5412;
static const int32_t kid4DMMAdvancedFreeLookLastKnown = 0x5421;
static const int32_t kid4DMMAdvancedCameraSpeedEdit = 0x5422;
static const int32_t kid4DMMAdvancedCameraSpeedSlider = 0x5423;
static const int32_t kid4DMMAdvancedCameraSpeedLabel = 0x5424;
static const int32_t kid4DMMAdvancedCameraSensitivityLabel = 0x5425;
static const int32_t kid4DMMAdvancedCameraSensitivityEdit = 0x5426;
static const int32_t kid4DMMActionsCreateLight = 0x5431;
static const int32_t kid4DMMActionsEnterFreeCam = 0x5432;
static const int32_t kid4DMMObjectGroupsList = 0x5441;
static const int32_t kid4DMMObjectGroupsBind = 0x5442;
static const int32_t kid4DMMObjectGroupsUnbind = 0x5443;
static const int32_t kid4DMMObjectGroupsRename = 0x5444;
static const int32_t kid4DMMObjectGroupsCreateObject = 0x5445;
static const int32_t kid4DMMObjectGroupsActorStudio = 0x5446;
static const int32_t kid4DMMObjectGroupRenameEdit = 0x5447;
static const int32_t kid4DMMObjectGroupRenameSave = 0x5448;
static const int32_t kid4DMMObjectGroupRenameCancel = 0x5449;
static const int32_t kid4DMMObjectGroupsJoin = 0x544A;
static const int32_t kid4DMMObjectGroupsAbandon = 0x544B;
static const int32_t kid4DMMObjectGroupsFrameOnly = 0x544C;
static const int32_t kid4DMMObjectGroupsVisibleOnly = 0x544D;
static const int32_t kid4DMMObjectGroupsDuplicate = 0x544E;
static const int32_t kid4DMMActorStudioActions = 0x5450;
static const int32_t kid4DMMActorStudioFrames = 0x5451;
static const int32_t kid4DMMActorStudioParts = 0x5452;
static const int32_t kid4DMMActorStudioInfo = 0x5453;
static const int32_t kid4DMMActorStudioSave = 0x5454;
static const int32_t kid4DMMActorStudioSaveAs = 0x5455;
static const int32_t kid4DMMActorStudioEdit = 0x5456;
static const int32_t kid4DMMActorStudioPitch = 0x5457;
static const int32_t kid4DMMActorStudioYaw = 0x5458;
static const int32_t kid4DMMActorStudioRoll = 0x5459;
static const int32_t kid4DMMActorStudioPosition = 0x545A;
static const int32_t kid4DMMActorStudioResetRotation = 0x545D;
static const int32_t kid4DMMActorStudioVerticalMovement = 0x545E;
static const int32_t kid4DMMActorStudioObject = 0x5460;
static const int32_t kid4DMMActorStudioNewAction = 0x5461;
static const int32_t kid4DMMActorStudioPlayPause = 0x5462;
static const int32_t kid4DMMActorStudioStop = 0x5463;
static const int32_t kid4DMMActorStudioFirst = 0x5464;
static const int32_t kid4DMMActorStudioLast = 0x5465;
static const int32_t kid4DMMActorStudioNext = 0x5466;
static const int32_t kid4DMMActorStudioBack = 0x5467;
static const int32_t kid4DMMActorStudioNewFrame = 0x5468;
static const int32_t kid4DMMActorStudioDeleteFrame = 0x5469;
static const int32_t kid4DMMActorStudioCopyFrame = 0x546A;
static const int32_t kid4DMMActorStudioCutFrame = 0x546B;
static const int32_t kid4DMMActorStudioPasteFrame = 0x546C;
static const int32_t kid4DMMActorStudioPasteBefore = 0x546D;
static const int32_t kid4DMMActorStudioPasteAfter = 0x546E;
static const int32_t kid4DMMActorStudioInsertBefore = 0x546F;
static const int32_t kid4DMMActorStudioInsertAfter = 0x5490;
static const int32_t kid4DMMActorStudioDuplicatePart = 0x5491;
static const int32_t kid4DMMActorStudioKeepPositions = 0x5492;
static const int32_t kid4DMMActorStudioGrowShrink = 0x5493;
static const int32_t kid4DMMActorStudioStretchSquish = 0x5494;
static const int32_t kid4DMMActorStudioDeleteTool = 0x5495;
static const int32_t kid4DMMActorStudioUseObject = 0x5496;
static const int32_t kid4DMMActorStudioResetCamera = 0x5497;
static const int32_t kid4DMMActorStudioToggleRotationEdge = 0x5498;
static const int32_t kid4DMMActorStudioSortDefaultPartGroups = 0x5499;
static const int32_t kid4DMMActorStudioCutPart = 0x549A;
static const int32_t kid4DMMActorStudioFastPitch = 0x54B0;
static const int32_t kid4DMMActorStudioFastYaw = 0x54B1;
static const int32_t kid4DMMActorStudioFastRoll = 0x54B2;
static const int32_t kid4DMMActorStudioFastResetRotation = 0x54B3;
static const int32_t kid4DMMActorStudioFastGrowShrink = 0x54B4;
static const int32_t kid4DMMActorStudioFastStretchSquish = 0x54B5;
static const int32_t kid4DMMActorStudioFastResetScale = 0x54B6;
static const int32_t kid4DMMActorStudioFastReposition = 0x54B7;
static const int32_t kid4DMMActorStudioFastVertical = 0x54B8;
static const int32_t kid4DMMActorStudioFastDelete = 0x54B9;
static const int32_t kid4DMMActorStudioFastCutDelete = 0x54BA;
static const int32_t kid4DMMActorStudioFastEscape = 0x54BB;
static const int32_t kid4DMMActorStudioFastCopyPart = 0x54BC;
static const int32_t kid4DMMActorStudioFastPastePart = 0x54BD;
static const int32_t kid4DMMActorStudioFastPastePartFrozen = 0x54BE;
static const int32_t kid4DMMActorStudioFrameMenuCopy = 0x54A0;
static const int32_t kid4DMMActorStudioFrameMenuCut = 0x54A1;
static const int32_t kid4DMMActorStudioFrameMenuPaste = 0x54A2;
static const int32_t kid4DMMActorStudioFrameMenuPasteBefore = 0x54A3;
static const int32_t kid4DMMActorStudioFrameMenuPasteAfter = 0x54A4;
static const int32_t kid4DMMActorStudioFrameMenuInsertBefore = 0x54A5;
static const int32_t kid4DMMActorStudioFrameMenuInsertAfter = 0x54A6;
static const achar ksz4DMMSceneSettingsWndClass[] = "4DMMSceneSettingsWindow";
static const achar ksz4DMMAdvancedSettingsWndClass[] = "4DMMAdvancedSettingsWindow";
static const achar ksz4DMMActionsWndClass[] = "4DMMActionsWindow";
static const achar ksz4DMMObjectGroupsWndClass[] = "4DMMObjectGroupsWindow";
static const achar ksz4DMMObjectGroupRenameWndClass[] = "4DMMObjectGroupRenameWindow";
static const achar ksz4DMMActorStudioWndClass[] = "4DMMActorStudioWindow";
static const achar ksz4DMMActorStudioViewportWndClass[] = "4DMMActorStudioViewportWindow";
static const UINT kwm4DMMActorStudioKey = WM_APP + 0x052;
static HWND vhwnd4DMMSettings = hNil;
static HWND vhwnd4DMMSceneSettings = hNil;
static HWND vhwnd4DMMAdvancedSettings = hNil;
static HWND vhwnd4DMMActions = hNil;
static HWND vhwnd4DMMObjectGroups = hNil;
static HWND vhwnd4DMMObjectGroupRename = hNil;
static HWND vhwnd4DMMActorStudio = hNil;
static HWND vhwnd4DMMActorStudioViewport = hNil;
static int32_t vid4DMMActorStudioGroup = 0;
static int32_t varid4DMMActorStudioTarget = aridNil;
static PMVIE vpmvie4DMMActorStudio = pvNil;
static int32_t vid4DMMObjectGroupRename = 0;
static int32_t vid4DMMObjectGroupSelected = 0;
static bool vf4DMMObjectGroupWholeSelected = fFalse;
static bool vf4DMMObjectGroupsFrameOnly = fFalse;
static bool vf4DMMObjectGroupsVisibleOnly = fFalse;
static PMVIE vpmvie4DMMObjectGroupsLast = pvNil;
static int32_t viscen4DMMObjectGroupsLast = ivNil;
static int32_t vc4DMMObjectGroupsLast = -1;
static uint32_t vdw4DMMObjectGroupsLastSignature = 0;
static WNDPROC vpfn4DMMObjectGroupsListOld = pvNil;
static WNDPROC vpfn4DMMObjectGroupRenameEditOld = pvNil;
static WNDPROC vpfn4DMMActorStudioListOld = pvNil;
static int32_t vid4DMMActorStudioNavList = kid4DMMActorStudioFrames;
static bool vf4DMMActorStudioSortDefaultPartGroups = fFalse;
static bool vf4DMMActorStudioHasDefaultPartGroups = fFalse;
// A cut BODY node remains in the Parts hierarchy but has no model in the
// current cel. Clicking that row respawns it, then hands control to the normal
// Reposition drag until the user's next click commits placement.
static bool vf4DMMActorStudioSpawnPlacementPending = fFalse;
static bool vf4DMMActorStudioSpawnPlacement = fFalse;
static int32_t vipart4DMMActorStudioSpawnPending = ivNil;
// Row-click Spawn uses the same cursor contract as main-window actor placement:
// the cursor is hidden and confined to the ASV, edge motion is recentered so
// the part never hits an invisible desktop wall, and placement restores the
// cursor at the projected center of the part. ptVirtual is an unbounded logical
// cursor consumed by the existing absolute-from-drag-start transform code.
static bool vf4DMMActorStudioSpawnCursorHidden = fFalse;
static POINT vpt4DMMActorStudioSpawnCursorLast = {};
static POINT vpt4DMMActorStudioSpawnVirtual = {};
// Tool hotkeys are polled while Actor Studio owns the foreground. RegisterHotKey
// reported success in v149 but Kauai's mixed native/GOB message routing still
// swallowed the resulting WM_HOTKEY packets. Polling is focus-independent and
// edge-triggered, so child list/combo focus no longer matters.
static bool vf4DMMActorStudioFastHotkeysRegistered = fFalse;
static uint32_t vgrf4DMMActorStudioPolledKeys = 0;
static PACTR vpactr4DMMActorStudioDetached = pvNil;
static ACTORSTUDIOFRAMESNAPSHOT vasframe4DMMActorStudioClipboard = {};
struct ACTORSTUDIOPARTCLIPBOARD
{
    bool fValid;
    PMVIE pmvieSource;
    CNO cnoTmplSource;
    int32_t ipartSource;
    int32_t anidSource;
    int32_t celnSource;
    BMAT34 bmat34CopyPose;
};
static ACTORSTUDIOPARTCLIPBOARD vaspart4DMMActorStudioClipboard = {};
struct ACTORSTUDIOOBJECTENTRY
{
    int32_t sid;
    CTG ctg;
    CNO cno;
    CNO cnoOwnedTmpl;
    int32_t idCustom;
    bool fProp;
    bool fHandmade;
    achar szName[160];
};
static std::vector<ACTORSTUDIOOBJECTENTRY> vrg4DMMActorStudioObjects;

enum ACTORSTUDIOTREENODEKIND
{
    kastnPart = 1,
    kastnObject = 2,
    kastnGroup = 3,
    kastnDefaultPartGroup = 4,
    kastnNonGroupedParts = 5,
    // Synthetic whole-object handle used only when Actor Studio needs an
    // editable parent above multiple imported Object Groups/Objects, or above
    // a customized default Actor/Prop. It owns no geometry itself; selecting it
    // transforms the top-most BODY roots of the complete target.
    kastnActorPropRoot = 6
};
struct ACTORSTUDIOTREEROW
{
    int32_t kind;
    int32_t idObject;
    int32_t idImport;
    int32_t idGroup;
    int32_t aridSource;
    int32_t ipartFirst;
    int32_t cpart;
    achar szName[160];
};
static std::vector<ACTORSTUDIOTREEROW> vrg4DMMActorStudioPartRows;
static std::vector<uint64_t> vrglu4DMMActorStudioExpanded;

struct ACTORSTUDIODEFAULTPARTINFO
{
    bool fPart;
    bool fGroup;
    int32_t ipartParent;
    int32_t cver;
    int32_t cfac;
    int32_t cpartDescendant;
    int32_t nPart;
    int32_t nGroup;
};
static std::vector<ACTORSTUDIODEFAULTPARTINFO> vrg4DMMActorStudioDefaultPartInfo;
static std::vector<int32_t> vrgid4DMMObjectGroupExpanded;

struct ACTORSTUDIOROTATIONEDGE
{
    PMVIE pmvie;
    CNO cnoTmpl;
    int32_t ipart;
    uint8_t grfOpposite;
};
static std::vector<ACTORSTUDIOROTATIONEDGE> vrg4DMMActorStudioRotationEdges;

struct OGROW
{
    bool fGroup;
    int32_t idGroup;
    int32_t arid;
};
static std::vector<OGROW> vrg4DMMObjectGroupRows;

// A forced post-load refresh is deliberately asynchronous.  _FInitStudio has
// already installed the replacement STDIO/MVIE when this is posted, but the
// message is not handled until the current load command returns to the Windows
// pump.  That keeps the Object Groups list synchronized with the final active
// scene instead of an intermediate portfolio/load state.
static const UINT kwm4DMMObjectGroupsMovieRefresh = WM_APP + 0x4D0;
static const UINT kwm4DMMObjectGroupsUndoRefresh = WM_APP + 0x4D1;
static const UINT kwm4DMMObjectGroupsSelectionRefresh = WM_APP + 0x4D2;

void Queue4DMMObjectGroupsMovieRefresh(void)
{
    if (vhwnd4DMMObjectGroups != hNil && IsWindow(vhwnd4DMMObjectGroups))
        PostMessageA(vhwnd4DMMObjectGroups, kwm4DMMObjectGroupsMovieRefresh, 0, 0);
}

void Queue4DMMObjectGroupsSelectionRefresh(void)
{
    if (vhwnd4DMMObjectGroups != hNil && IsWindow(vhwnd4DMMObjectGroups))
        PostMessageA(vhwnd4DMMObjectGroups, kwm4DMMObjectGroupsSelectionRefresh, 0, 0);
}

static void Close4DMMSettingsWindow(HWND hwnd);
static void Update4DMMObjectGroupsControls(void);

static PMVIE Pmvie4DMMSettingsCurrent(void)
{
    PSTDIO pstdio = vpapp != pvNil ? vpapp->Pstdio() : pvNil;
    return pstdio != pvNil ? pstdio->Pmvie() : pvNil;
}

static void Set4DMMNativeControlFont(HWND hwnd, const int32_t *prgid, int32_t cid)
{
    for (int32_t i = 0; i < cid; i++)
    {
        HWND hwndControl = GetDlgItem(hwnd, prgid[i]);
        if (hwndControl != hNil)
            SendMessageA(hwndControl, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
    }
}

/***************************************************************************
    The three compact settings/action tools use the same fixed 200%-equivalent
    outer shell as the console and browsers, but their contents should remain
    visually compact.  v75 kept authored vertical metrics, which fixed the
    oversized controls, but the authored left offsets no longer centered the
    now-wide controls inside the 2x client area.

    Keep native fonts, use 200% horizontal sizing, and center the compact
    controls against the actual client rectangle.  Vertical placement is done
    per-window below so each small tool can center its own control group.
***************************************************************************/
static void Set4DMMCompactToolControlGeometry200(HWND hwnd, int32_t id,
                                                  int32_t yp, int32_t dxp,
                                                  int32_t dyp)
{
    HWND hwndControl = GetDlgItem(hwnd, id);
    if (hwndControl == hNil)
        return;

    RECT rcClient;
    if (!GetClientRect(hwnd, &rcClient))
        return;

    const int32_t dxpControl = Lw4DMMExternalToolUi200(hwnd, dxp);
    const int32_t dxpClient = rcClient.right - rcClient.left;
    const int32_t xp = LwMax(0L, (dxpClient - dxpControl) / 2);

    SetWindowPos(hwndControl, hNil, xp, yp, dxpControl, dyp,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

static int32_t Yp4DMMCenteredControlGroup(HWND hwnd, int32_t dypGroup)
{
    RECT rcClient;
    if (!GetClientRect(hwnd, &rcClient))
        return 0;
    return LwMax(0L, ((rcClient.bottom - rcClient.top) - dypGroup) / 2);
}

static bool F4DMMBlockingStudioEditorOpen(void)
{
    // Actor/prop pickers and the two editing easels all temporarily own the
    // selected object and its input semantics. Console actions must not punch
    // through those panes as a side effect of a native button click.
    PGOB pgobWorld = vapp.Pkwa();
    return (pgobWorld != pvNil &&
            (pgobWorld->PgobFromHid(kidActorGlass) != pvNil ||
             pgobWorld->PgobFromHid(kidPropGlass) != pvNil)) ||
           GOB::PgobFromHidScr(kidSpltGlass) != pvNil ||
           GOB::PgobFromHidScr(kidCostGlass) != pvNil;
}

static bool F4DMMCreateLightActionEnabled(PMVIE pmvie)
{
    return pmvie != pvNil && pmvie->Pscen() != pvNil &&
           pmvie->AridSelected() != aridNil && !F4DMMBlockingStudioEditorOpen();
}

static bool F4DMMFreeCamActionEnabled(PMVIE pmvie)
{
    if (pmvie == pvNil || pmvie->Pscen() == pvNil || pmvie->FPlaying() ||
        pmvie->FManualCameraMode() || pmvie->FFreeLookMode() ||
        F4DMMBlockingStudioEditorOpen())
    {
        return fFalse;
    }
    if (FindWindowA("3DMMExDepthMotionTweenEditor", pvNil) != hNil ||
        FindWindowA("3DMMExManualCameraFrameEditor", pvNil) != hNil)
    {
        return fFalse;
    }
    // Match the F shortcut's native editing context rather than allowing the
    // console button to enter Free Cam from another Studio cover.
    return GOB::PgobFromHidScr(kidActorsBackground) != pvNil;
}

static void Update4DMMSceneSettingsControls(void)
{
    if (vhwnd4DMMSceneSettings == hNil || !IsWindow(vhwnd4DMMSceneSettings))
        return;
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    const bool fHaveScene = pmvie != pvNil && pmvie->Pscen() != pvNil && pmvie->Iscen() >= 0;
    CheckDlgButton(vhwnd4DMMSceneSettings, kid4DMMSceneSettingsDefaultLightingShaders,
                   fHaveScene && pmvie->FSceneDefaultLightingShaders(pmvie->Iscen()) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(vhwnd4DMMSceneSettings, kid4DMMSceneSettingsLightLabCombineLegacy,
                   fHaveScene && pmvie->FSceneLightLabCombineLegacy(pmvie->Iscen()) ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(vhwnd4DMMSceneSettings, kid4DMMSceneSettingsDefaultLightingShaders), fHaveScene);
    EnableWindow(GetDlgItem(vhwnd4DMMSceneSettings, kid4DMMSceneSettingsLightLabCombineLegacy), fHaveScene);
}

static LRESULT CALLBACK Lresult4DMMSceneSettingsWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    switch (wm)
    {
    case WM_CREATE:
    {
        vhwnd4DMMSceneSettings = hwnd;
        const DWORD grfCheck = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX;
        const DWORD grfCheckMulti = grfCheck | BS_MULTILINE;
        HINSTANCE hinst = GetModuleHandleA(pvNil);
        CreateWindowExA(0, "BUTTON", "Use shaders with default (legacy) lighting", grfCheck,
                        12, 12, 305, 24, hwnd, (HMENU)kid4DMMSceneSettingsDefaultLightingShaders,
                        hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Combine default (legacy) lighting with new lighting system", grfCheckMulti,
                        12, 40, 305, 42, hwnd, (HMENU)kid4DMMSceneSettingsLightLabCombineLegacy,
                        hinst, pvNil);
        const int32_t rgid[] = {kid4DMMSceneSettingsDefaultLightingShaders,
                                kid4DMMSceneSettingsLightLabCombineLegacy};
        Set4DMMNativeControlFont(hwnd, rgid, SIZEOF(rgid) / SIZEOF(rgid[0]));
        SetTimer(hwnd, 1, 250, pvNil);
        Update4DMMSceneSettingsControls();
        return 0;
    }
    case WM_TIMER:
        Update4DMMSceneSettingsControls();
        return 0;
    case WM_COMMAND:
    {
        PMVIE pmvie = Pmvie4DMMSettingsCurrent();
        if (pmvie == pvNil || pmvie->Pscen() == pvNil)
            break;
        if (LOWORD(wParam) == kid4DMMSceneSettingsDefaultLightingShaders && HIWORD(wParam) == BN_CLICKED)
        {
            pmvie->FSetSceneDefaultLightingShaders(pmvie->Iscen(),
                IsDlgButtonChecked(hwnd, kid4DMMSceneSettingsDefaultLightingShaders) == BST_CHECKED, fTrue);
            Update4DMMSceneSettingsControls();
            return 0;
        }
        if (LOWORD(wParam) == kid4DMMSceneSettingsLightLabCombineLegacy && HIWORD(wParam) == BN_CLICKED)
        {
            pmvie->FSetSceneLightLabCombineLegacy(pmvie->Iscen(),
                IsDlgButtonChecked(hwnd, kid4DMMSceneSettingsLightLabCombineLegacy) == BST_CHECKED, fTrue);
            Update4DMMSceneSettingsControls();
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        vhwnd4DMMSceneSettings = hNil;
        return 0;
    }
    return DefWindowProcA(hwnd, wm, wParam, lParam);
}

static bool FOpen4DMMSceneSettings(void)
{
    if (vhwnd4DMMSceneSettings != hNil && IsWindow(vhwnd4DMMSceneSettings))
    {
        Update4DMMSceneSettingsControls();
        ShowWindow(vhwnd4DMMSceneSettings, SW_SHOWNORMAL);
        SetForegroundWindow(vhwnd4DMMSceneSettings);
        return fTrue;
    }

    HINSTANCE hinst = GetModuleHandleA(pvNil);
    WNDCLASSA wc;
    ClearPb(&wc, SIZEOF(wc));
    if (!GetClassInfoA(hinst, ksz4DMMSceneSettingsWndClass, &wc))
    {
        wc.lpfnWndProc = Lresult4DMMSceneSettingsWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(pvNil, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = ksz4DMMSceneSettingsWndClass;
        if (RegisterClassA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return fFalse;
    }

    vhwnd4DMMSceneSettings = CreateWindowExA(
        WS_EX_TOOLWINDOW, ksz4DMMSceneSettingsWndClass, "4DMM Scene Settings",
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 350, 135, Hwnd4DMMNativeToolOwner(), hNil, hinst, pvNil);
    if (vhwnd4DMMSceneSettings == hNil)
        return fFalse;
    Scale4DMMExternalToolWindow200(vhwnd4DMMSceneSettings);
    const int32_t dypSceneCheck = 30;
    const int32_t dypSceneGap = 8;
    const int32_t ypScene = Yp4DMMCenteredControlGroup(
        vhwnd4DMMSceneSettings, dypSceneCheck * 2 + dypSceneGap);
    Set4DMMCompactToolControlGeometry200(vhwnd4DMMSceneSettings,
        kid4DMMSceneSettingsDefaultLightingShaders, ypScene, 305, dypSceneCheck);
    Set4DMMCompactToolControlGeometry200(vhwnd4DMMSceneSettings,
        kid4DMMSceneSettingsLightLabCombineLegacy,
        ypScene + dypSceneCheck + dypSceneGap, 305, dypSceneCheck);
    ShowWindow(vhwnd4DMMSceneSettings, SW_SHOWNORMAL);
    SetForegroundWindow(vhwnd4DMMSceneSettings);
    UpdateWindow(vhwnd4DMMSceneSettings);
    return fTrue;
}

static void Update4DMMAdvancedSettingsControls(void)
{
    if (vhwnd4DMMAdvancedSettings == hNil || !IsWindow(vhwnd4DMMAdvancedSettings))
        return;
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    const bool fHaveMovie = pmvie != pvNil;
    CheckDlgButton(vhwnd4DMMAdvancedSettings, kid4DMMAdvancedFreeLookLastKnown,
                   fHaveMovie && pmvie->FFreeLookAlwaysLastKnown() ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(vhwnd4DMMAdvancedSettings, kid4DMMAdvancedFreeLookLastKnown), fHaveMovie);
    EnableWindow(GetDlgItem(vhwnd4DMMAdvancedSettings, kid4DMMAdvancedCameraSpeedEdit), fHaveMovie);
    EnableWindow(GetDlgItem(vhwnd4DMMAdvancedSettings, kid4DMMAdvancedCameraSpeedSlider), fHaveMovie);
    EnableWindow(GetDlgItem(vhwnd4DMMAdvancedSettings, kid4DMMAdvancedCameraSensitivityEdit), fHaveMovie);

    if (fHaveMovie)
    {
        HWND hwndEdit = GetDlgItem(vhwnd4DMMAdvancedSettings, kid4DMMAdvancedCameraSpeedEdit);
        if (hwndEdit != hNil && GetFocus() != hwndEdit)
        {
            achar rgch[32];
            sprintf_s(rgch, SIZEOF(rgch), "%.2f", (double)pmvie->CameraMoveSpeed());
            SetWindowTextA(hwndEdit, rgch);
        }
        HWND hwndSlider = GetDlgItem(vhwnd4DMMAdvancedSettings, kid4DMMAdvancedCameraSpeedSlider);
        if (hwndSlider != hNil)
        {
            float flSlider = pmvie->CameraMoveSpeed();
            if (flSlider < 0.2f) flSlider = 0.2f;
            if (flSlider > 2.0f) flSlider = 2.0f;
            SendMessageA(hwndSlider, TBM_SETPOS, fTrue, (LPARAM)(int32_t)(flSlider * 10.0f + 0.5f));
        }
        HWND hwndSensitivity = GetDlgItem(vhwnd4DMMAdvancedSettings, kid4DMMAdvancedCameraSensitivityEdit);
        if (hwndSensitivity != hNil && GetFocus() != hwndSensitivity)
        {
            achar rgch[32];
            sprintf_s(rgch, SIZEOF(rgch), "%.2f", (double)pmvie->CameraMouseSensitivity());
            SetWindowTextA(hwndSensitivity, rgch);
        }
    }
}

static bool FApply4DMMCameraSpeedText(HWND hwnd)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    HWND hwndEdit = GetDlgItem(hwnd, kid4DMMAdvancedCameraSpeedEdit);
    if (pmvie == pvNil || hwndEdit == hNil)
        return fFalse;

    achar rgch[64];
    GetWindowTextA(hwndEdit, rgch, SIZEOF(rgch));
    char *pchEnd = pvNil;
    double d = strtod(rgch, &pchEnd);
    while (pchEnd != pvNil && (*pchEnd == ' ' || *pchEnd == '\t'))
        ++pchEnd;
    if (pchEnd == rgch || pchEnd == pvNil || *pchEnd != 0 || d < 0.01 || d > 10.0)
    {
        achar rgchGood[32];
        sprintf_s(rgchGood, SIZEOF(rgchGood), "%.2f", (double)pmvie->CameraMoveSpeed());
        SetWindowTextA(hwndEdit, rgchGood);
        return fFalse;
    }
    pmvie->SetCameraMoveSpeed((float)d);
    Update4DMMAdvancedSettingsControls();
    return fTrue;
}

static bool FApply4DMMCameraSensitivityText(HWND hwnd)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    HWND hwndEdit = GetDlgItem(hwnd, kid4DMMAdvancedCameraSensitivityEdit);
    if (pmvie == pvNil || hwndEdit == hNil)
        return fFalse;

    achar rgch[64];
    GetWindowTextA(hwndEdit, rgch, SIZEOF(rgch));
    char *pchEnd = pvNil;
    double d = strtod(rgch, &pchEnd);
    while (pchEnd != pvNil && (*pchEnd == ' ' || *pchEnd == '\t'))
        ++pchEnd;
    if (pchEnd == rgch || pchEnd == pvNil || *pchEnd != 0 || d < 0.01 || d > 0.30)
    {
        achar rgchGood[32];
        sprintf_s(rgchGood, SIZEOF(rgchGood), "%.2f", (double)pmvie->CameraMouseSensitivity());
        SetWindowTextA(hwndEdit, rgchGood);
        return fFalse;
    }
    pmvie->SetCameraMouseSensitivity((float)d);
    Update4DMMAdvancedSettingsControls();
    return fTrue;
}

static LRESULT CALLBACK Lresult4DMMAdvancedSettingsWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    switch (wm)
    {
    case WM_CREATE:
    {
        vhwnd4DMMAdvancedSettings = hwnd;
        HINSTANCE hinst = GetModuleHandleA(pvNil);
        CreateWindowExA(0, "BUTTON",
            "Always initiate freecam from last known freecam location (do not allow 'Move cam with selected object' option to change the freecam's positioning)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE,
            12, 10, 350, 82, hwnd, (HMENU)kid4DMMAdvancedFreeLookLastKnown, hinst, pvNil);
        CreateWindowExA(0, "STATIC", "Manual / Free Cam movement speed (0.01 - 10.00):",
                        WS_CHILD | WS_VISIBLE, 12, 102, 350, 20, hwnd,
                        (HMENU)kid4DMMAdvancedCameraSpeedLabel, hinst, pvNil);
        HWND hwndEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "1.00",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                        12, 126, 88, 24, hwnd, (HMENU)kid4DMMAdvancedCameraSpeedEdit, hinst, pvNil);
        if (hwndEdit != hNil)
            SetPropA(hwndEdit, "3DMMExNativeTextInput", (HANDLE)1);

        // Register the standard trackbar class without adding a new static
        // comctl32 linker dependency to the 1995 project.
        HMODULE hmodComCtl = LoadLibraryA("comctl32.dll");
        if (hmodComCtl != hNil)
        {
            typedef void (WINAPI *PFNINITCOMMONCONTROLS)(void);
            PFNINITCOMMONCONTROLS pfnInit = (PFNINITCOMMONCONTROLS)GetProcAddress(hmodComCtl, "InitCommonControls");
            if (pfnInit != pvNil)
                pfnInit();
        }
        HWND hwndSlider = CreateWindowExA(0, TRACKBAR_CLASSA, "",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS,
                        112, 122, 250, 34, hwnd, (HMENU)kid4DMMAdvancedCameraSpeedSlider, hinst, pvNil);
        if (hwndSlider != hNil)
        {
            SendMessageA(hwndSlider, TBM_SETRANGE, fTrue, MAKELPARAM(2, 20));
            SendMessageA(hwndSlider, TBM_SETTICFREQ, 2, 0);
            SendMessageA(hwndSlider, TBM_SETPAGESIZE, 0, 2);
        }
        CreateWindowExA(0, "STATIC",
            "Manual / Free Cam mouse sensitivity (0.01 - 0.30) Default = 0.07",
            WS_CHILD | WS_VISIBLE, 12, 166, 350, 20, hwnd,
            (HMENU)kid4DMMAdvancedCameraSensitivityLabel, hinst, pvNil);
        HWND hwndSensitivity = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "0.07",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            12, 190, 88, 24, hwnd, (HMENU)kid4DMMAdvancedCameraSensitivityEdit, hinst, pvNil);
        if (hwndSensitivity != hNil)
            SetPropA(hwndSensitivity, "3DMMExNativeTextInput", (HANDLE)1);
        const int32_t rgid[] = {kid4DMMAdvancedFreeLookLastKnown, kid4DMMAdvancedCameraSpeedLabel,
                                kid4DMMAdvancedCameraSpeedEdit,
                                kid4DMMAdvancedCameraSensitivityLabel,
                                kid4DMMAdvancedCameraSensitivityEdit};
        Set4DMMNativeControlFont(hwnd, rgid, SIZEOF(rgid) / SIZEOF(rgid[0]));
        SetTimer(hwnd, 1, 250, pvNil);
        Update4DMMAdvancedSettingsControls();
        return 0;
    }
    case WM_TIMER:
        Update4DMMAdvancedSettingsControls();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == kid4DMMAdvancedFreeLookLastKnown && HIWORD(wParam) == BN_CLICKED)
        {
            PMVIE pmvie = Pmvie4DMMSettingsCurrent();
            if (pmvie != pvNil)
                pmvie->FSetFreeLookAlwaysLastKnown(
                    IsDlgButtonChecked(hwnd, kid4DMMAdvancedFreeLookLastKnown) == BST_CHECKED, fTrue);
            Update4DMMAdvancedSettingsControls();
            return 0;
        }
        if (LOWORD(wParam) == kid4DMMAdvancedCameraSpeedEdit && HIWORD(wParam) == EN_KILLFOCUS)
        {
            FApply4DMMCameraSpeedText(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == kid4DMMAdvancedCameraSensitivityEdit && HIWORD(wParam) == EN_KILLFOCUS)
        {
            FApply4DMMCameraSensitivityText(hwnd);
            return 0;
        }
        break;
    case WM_HSCROLL:
        if ((HWND)lParam == GetDlgItem(hwnd, kid4DMMAdvancedCameraSpeedSlider))
        {
            PMVIE pmvie = Pmvie4DMMSettingsCurrent();
            if (pmvie != pvNil)
            {
                int32_t ipos = (int32_t)SendMessageA((HWND)lParam, TBM_GETPOS, 0, 0);
                pmvie->SetCameraMoveSpeed((float)ipos / 10.0f);
                Update4DMMAdvancedSettingsControls();
            }
            return 0;
        }
        break;
    case WM_CLOSE:
        FApply4DMMCameraSpeedText(hwnd);
        FApply4DMMCameraSensitivityText(hwnd);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        vhwnd4DMMAdvancedSettings = hNil;
        return 0;
    }
    return DefWindowProcA(hwnd, wm, wParam, lParam);
}

static bool FOpen4DMMAdvancedSettings(void)
{
    if (vhwnd4DMMAdvancedSettings != hNil && IsWindow(vhwnd4DMMAdvancedSettings))
    {
        Update4DMMAdvancedSettingsControls();
        ShowWindow(vhwnd4DMMAdvancedSettings, SW_SHOWNORMAL);
        SetForegroundWindow(vhwnd4DMMAdvancedSettings);
        return fTrue;
    }
    HINSTANCE hinst = GetModuleHandleA(pvNil);
    WNDCLASSA wc;
    ClearPb(&wc, SIZEOF(wc));
    if (!GetClassInfoA(hinst, ksz4DMMAdvancedSettingsWndClass, &wc))
    {
        wc.lpfnWndProc = Lresult4DMMAdvancedSettingsWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(pvNil, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = ksz4DMMAdvancedSettingsWndClass;
        if (RegisterClassA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return fFalse;
    }
    vhwnd4DMMAdvancedSettings = CreateWindowExA(
        WS_EX_TOOLWINDOW, ksz4DMMAdvancedSettingsWndClass, "4DMM Advanced Settings",
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 280, Hwnd4DMMNativeToolOwner(), hNil, hinst, pvNil);
    if (vhwnd4DMMAdvancedSettings == hNil)
        return fFalse;
    Scale4DMMExternalToolWindow200(vhwnd4DMMAdvancedSettings);
    // The native multiline checkbox needs a little vertical breathing room at
    // the fixed 200%-equivalent shell size.  54 px clipped the first/last text
    // scanlines on the user's actual DEFAULT_GUI_FONT; keep it centered but give
    // the three-line label its full native button height.
    const int32_t dypAdvanced = 72;
    Set4DMMCompactToolControlGeometry200(vhwnd4DMMAdvancedSettings,
        kid4DMMAdvancedFreeLookLastKnown, 10, 350, dypAdvanced);
    Set4DMMCompactToolControlGeometry200(vhwnd4DMMAdvancedSettings,
        kid4DMMAdvancedCameraSpeedLabel, 92, 350, 20);
    Set4DMMCompactToolControlGeometry200(vhwnd4DMMAdvancedSettings,
        kid4DMMAdvancedCameraSpeedEdit, 116, 88, 24);
    Set4DMMCompactToolControlGeometry200(vhwnd4DMMAdvancedSettings,
        kid4DMMAdvancedCameraSpeedSlider, 150, 250, 34);
    Set4DMMCompactToolControlGeometry200(vhwnd4DMMAdvancedSettings,
        kid4DMMAdvancedCameraSensitivityLabel, 196, 350, 20);
    Set4DMMCompactToolControlGeometry200(vhwnd4DMMAdvancedSettings,
        kid4DMMAdvancedCameraSensitivityEdit, 220, 88, 24);
    ShowWindow(vhwnd4DMMAdvancedSettings, SW_SHOWNORMAL);
    SetForegroundWindow(vhwnd4DMMAdvancedSettings);
    UpdateWindow(vhwnd4DMMAdvancedSettings);
    return fTrue;
}

static void Update4DMMActionsControls(void)
{
    if (vhwnd4DMMActions == hNil || !IsWindow(vhwnd4DMMActions))
        return;
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    EnableWindow(GetDlgItem(vhwnd4DMMActions, kid4DMMActionsCreateLight), F4DMMCreateLightActionEnabled(pmvie));
    EnableWindow(GetDlgItem(vhwnd4DMMActions, kid4DMMActionsEnterFreeCam), F4DMMFreeCamActionEnabled(pmvie));
}

static LRESULT CALLBACK Lresult4DMMActionsWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    switch (wm)
    {
    case WM_CREATE:
    {
        vhwnd4DMMActions = hwnd;
        const DWORD grfButton = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
        HINSTANCE hinst = GetModuleHandleA(pvNil);
        CreateWindowExA(0, "BUTTON", "Create light object (CTRL+L)", grfButton,
                        12, 12, 245, 30, hwnd, (HMENU)kid4DMMActionsCreateLight, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Enter freecam (F)", grfButton,
                        12, 48, 245, 30, hwnd, (HMENU)kid4DMMActionsEnterFreeCam, hinst, pvNil);
        const int32_t rgid[] = {kid4DMMActionsCreateLight, kid4DMMActionsEnterFreeCam};
        Set4DMMNativeControlFont(hwnd, rgid, 2);
        SetTimer(hwnd, 1, 250, pvNil);
        Update4DMMActionsControls();
        return 0;
    }
    case WM_TIMER:
        Update4DMMActionsControls();
        return 0;
    case WM_COMMAND:
    {
        PMVIE pmvie = Pmvie4DMMSettingsCurrent();
        if (LOWORD(wParam) == kid4DMMActionsCreateLight && HIWORD(wParam) == BN_CLICKED)
        {
            if (F4DMMCreateLightActionEnabled(pmvie))
                pmvie->FOpenLightLabEditor();
            Update4DMMActionsControls();
            return 0;
        }
        if (LOWORD(wParam) == kid4DMMActionsEnterFreeCam && HIWORD(wParam) == BN_CLICKED)
        {
            if (F4DMMFreeCamActionEnabled(pmvie))
                pmvie->FStartFreeLook(fFalse);
            Update4DMMActionsControls();
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        vhwnd4DMMActions = hNil;
        return 0;
    }
    return DefWindowProcA(hwnd, wm, wParam, lParam);
}

static bool FOpen4DMMActions(void)
{
    if (vhwnd4DMMActions != hNil && IsWindow(vhwnd4DMMActions))
    {
        Update4DMMActionsControls();
        ShowWindow(vhwnd4DMMActions, SW_SHOWNORMAL);
        SetForegroundWindow(vhwnd4DMMActions);
        return fTrue;
    }
    HINSTANCE hinst = GetModuleHandleA(pvNil);
    WNDCLASSA wc;
    ClearPb(&wc, SIZEOF(wc));
    if (!GetClassInfoA(hinst, ksz4DMMActionsWndClass, &wc))
    {
        wc.lpfnWndProc = Lresult4DMMActionsWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(pvNil, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = ksz4DMMActionsWndClass;
        if (RegisterClassA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return fFalse;
    }
    vhwnd4DMMActions = CreateWindowExA(
        WS_EX_TOOLWINDOW, ksz4DMMActionsWndClass, "4DMM Actions",
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 285, 125, Hwnd4DMMNativeToolOwner(), hNil, hinst, pvNil);
    if (vhwnd4DMMActions == hNil)
        return fFalse;
    Scale4DMMExternalToolWindow200(vhwnd4DMMActions);
    const int32_t dypActionButton = 30;
    const int32_t dypActionGap = 8;
    const int32_t ypActions = Yp4DMMCenteredControlGroup(
        vhwnd4DMMActions, dypActionButton * 2 + dypActionGap);
    Set4DMMCompactToolControlGeometry200(vhwnd4DMMActions,
        kid4DMMActionsCreateLight, ypActions, 245, dypActionButton);
    Set4DMMCompactToolControlGeometry200(vhwnd4DMMActions,
        kid4DMMActionsEnterFreeCam,
        ypActions + dypActionButton + dypActionGap, 245, dypActionButton);
    ShowWindow(vhwnd4DMMActions, SW_SHOWNORMAL);
    SetForegroundWindow(vhwnd4DMMActions);
    UpdateWindow(vhwnd4DMMActions);
    return fTrue;
}


static bool F4DMMObjectGroupExpanded(int32_t idGroup)
{
    for (size_t i = 0; i < vrgid4DMMObjectGroupExpanded.size(); i++)
        if (vrgid4DMMObjectGroupExpanded[i] == idGroup)
            return fTrue;
    return fFalse;
}

static void Set4DMMObjectGroupExpanded(int32_t idGroup, bool fExpanded)
{
    for (size_t i = 0; i < vrgid4DMMObjectGroupExpanded.size(); i++)
    {
        if (vrgid4DMMObjectGroupExpanded[i] != idGroup)
            continue;
        if (!fExpanded)
            vrgid4DMMObjectGroupExpanded.erase(vrgid4DMMObjectGroupExpanded.begin() + i);
        return;
    }
    if (fExpanded)
        vrgid4DMMObjectGroupExpanded.push_back(idGroup);
}

static int32_t I4DMMObjectGroupFind(PMVIE pmvie, int32_t idGroup)
{
    if (pmvie == pvNil)
        return ivNil;
    for (int32_t i = 0; i < pmvie->CObjectGroups(); i++)
    {
        const OBJECTGROUP *pgroup = pmvie->PObjectGroup(i);
        if (pgroup != pvNil && pgroup->id == idGroup)
            return i;
    }
    return ivNil;
}

static uint32_t Dw4DMMObjectGroupsSignature(PMVIE pmvie, int32_t iscen)
{
    // Detect a movie reload even if the allocator reuses the same MVIE address
    // and the new movie happens to contain the same number of groups.
    uint32_t dw = 2166136261u;
    if (pmvie == pvNil || iscen < 0)
        return dw;
    dw = (dw ^ (vf4DMMObjectGroupsFrameOnly ? 0x11u : 0x22u)) * 16777619u;
    dw = (dw ^ (vf4DMMObjectGroupsVisibleOnly ? 0x33u : 0x44u)) * 16777619u;
    if (pmvie->Pscen() != pvNil)
        dw = (dw ^ (uint32_t)pmvie->Pscen()->Nfrm()) * 16777619u;
    for (int32_t iGroup = 0; iGroup < pmvie->CObjectGroups(); iGroup++)
    {
        const OBJECTGROUP *pgroup = pmvie->PObjectGroup(iGroup);
        if (pgroup == pvNil || pgroup->iscen != iscen)
            continue;
        dw = (dw ^ (uint32_t)pgroup->id) * 16777619u;
        dw = (dw ^ (pgroup->fLocked ? 1u : 0u)) * 16777619u;
        for (const achar *pch = pgroup->szName; *pch != 0; pch++)
            dw = (dw ^ (uint8_t)*pch) * 16777619u;
        const int32_t cMember = pmvie->CObjectGroupMembers(pgroup->id);
        dw = (dw ^ (uint32_t)cMember) * 16777619u;
        for (int32_t iMember = 0; iMember < cMember; iMember++)
        {
            const OBJECTGROUPMEMBER *pmember = pmvie->PObjectGroupMember(pgroup->id, iMember);
            if (pmember != pvNil)
            {
                dw = (dw ^ (uint32_t)pmember->arid) * 16777619u;
                if ((vf4DMMObjectGroupsFrameOnly || vf4DMMObjectGroupsVisibleOnly) &&
                    pmvie->Pscen() != pvNil)
                {
                    PACTR pactr = pmvie->Pscen()->PactrFromArid(pmember->arid);
                    const uint32_t bits = pactr == pvNil ? 0u :
                        (pactr->FOnStage() ? 1u : 0u) | (pactr->FIsInView() ? 2u : 0u);
                    dw = (dw ^ bits) * 16777619u;
                }
            }
        }
    }
    return dw;
}

static bool F4DMMObjectGroupMemberPassesFilters(PMVIE pmvie, int32_t arid)
{
    if (!vf4DMMObjectGroupsFrameOnly && !vf4DMMObjectGroupsVisibleOnly)
        return fTrue;
    if (pmvie == pvNil || pmvie->Pscen() == pvNil)
        return fFalse;
    PACTR pactr = pmvie->Pscen()->PactrFromArid(arid);
    if (pactr == pvNil)
        return fFalse;
    if (vf4DMMObjectGroupsFrameOnly && !pactr->FOnStage())
        return fFalse;
    if (vf4DMMObjectGroupsVisibleOnly && (!pactr->FOnStage() || !pactr->FIsInView()))
        return fFalse;
    return fTrue;
}

static bool F4DMMObjectGroupPassesFilters(PMVIE pmvie, int32_t idGroup)
{
    if (!vf4DMMObjectGroupsFrameOnly && !vf4DMMObjectGroupsVisibleOnly)
        return fTrue;
    const int32_t cMember = pmvie != pvNil ? pmvie->CObjectGroupMembers(idGroup) : 0;
    for (int32_t iMember = 0; iMember < cMember; ++iMember)
    {
        const OBJECTGROUPMEMBER *pmember = pmvie->PObjectGroupMember(idGroup, iMember);
        if (pmember != pvNil && F4DMMObjectGroupMemberPassesFilters(pmvie, pmember->arid))
            return fTrue;
    }
    return fFalse;
}

static bool FDuplicate4DMMObjectGroup(PMVIE pmvie, int32_t idGroup, int32_t *pidNew)
{
    if (pidNew != pvNil)
        *pidNew = 0;
    if (pmvie == pvNil || pmvie->Pscen() == pvNil || idGroup <= 0)
        return fFalse;
    const int32_t cMember = pmvie->CObjectGroupMembers(idGroup);
    if (cMember < 2)
        return fFalse;

    std::vector<int32_t> rgaridNew;
    rgaridNew.reserve((size_t)cMember);
    for (int32_t iMember = 0; iMember < cMember; ++iMember)
    {
        const OBJECTGROUPMEMBER *pmember = pmvie->PObjectGroupMember(idGroup, iMember);
        PACTR pactrSource = pmember != pvNil ? pmvie->Pscen()->PactrFromArid(pmember->arid) : pvNil;
        if (pactrSource == pvNil)
            goto LFail;
        PACLP paclp = ACLP::PaclpNew(pactrSource, fFalse, fTrue);
        if (paclp == pvNil || !paclp->FPaste(pmvie) || pmvie->Pscen()->PactrSelected() == pvNil)
        {
            ReleasePpo(&paclp);
            goto LFail;
        }
        rgaridNew.push_back(pmvie->Pscen()->PactrSelected()->Arid());
        ReleasePpo(&paclp);
    }

    pmvie->Pscen()->SelectActr(pvNil);
    for (size_t i = 0; i < rgaridNew.size(); ++i)
    {
        PACTR pactr = pmvie->Pscen()->PactrFromArid(rgaridNew[i]);
        if (pactr == pvNil)
            goto LFail;
        if (i == 0)
            pmvie->Pscen()->SelectActr(pactr);
        else
            pmvie->Pscen()->SelectActrAdd(pactr);
    }

    {
        int32_t idNew = 0;
        if (!pmvie->FBindSelectedObjectGroup(&idNew) || idNew <= 0)
            goto LFail;
        const int32_t iSource = I4DMMObjectGroupFind(pmvie, idGroup);
        const OBJECTGROUP *pgroupSource = iSource != ivNil ? pmvie->PObjectGroup(iSource) : pvNil;
        if (pgroupSource != pvNil)
        {
            achar szName[kcchObjectGroupName];
            sprintf_s(szName, SIZEOF(szName), "%s Copy", pgroupSource->szName);
            pmvie->FRenameObjectGroup(idNew, szName);
        }
        if (pidNew != pvNil)
            *pidNew = idNew;
        MVIE::MultiLog(pmvie, "group_duplicate source=%ld new=%ld members=%ld",
                       (long)idGroup, (long)idNew, (long)cMember);
        return fTrue;
    }

LFail:
    // Best-effort rollback for a partial paste. The clipboard path also copied
    // Light Lab metadata, so remove that attachment before removing each ACTR.
    for (size_t i = 0; i < rgaridNew.size(); ++i)
    {
        pmvie->FRemoveLightLabConfigCore(pmvie->Iscen(), rgaridNew[i]);
        if (pmvie->Pscen()->PactrFromArid(rgaridNew[i]) != pvNil)
            pmvie->Pscen()->RemActrCore(rgaridNew[i]);
    }
    pmvie->Pscen()->SelectActr(pvNil);
    MVIE::MultiLog(pmvie, "group_duplicate fail source=%ld pasted=%ld",
                   (long)idGroup, (long)rgaridNew.size());
    return fFalse;
}

static PGUND Pgund4DMMObjectGroupMembership(PMVIE pmvie, const achar *pszUndoName)
{
    if (pmvie == pvNil)
        return pvNil;
    PGUND pgund = GUND::PgundNew();
    if (pgund == pvNil || !pgund->FCaptureMembership(pmvie, pszUndoName))
    {
        ReleasePpo(&pgund);
        pmvie->ClearUndo();
        PushErc(ercSocNotUndoable);
        return pvNil;
    }
    return pgund;
}

static void Commit4DMMObjectGroupMembershipUndo(PMVIE pmvie, PGUND *ppgund)
{
    if (pmvie == pvNil || ppgund == pvNil || *ppgund == pvNil)
        return;
    if (!pmvie->FAddUndo(*ppgund))
    {
        PushErc(ercSocNotUndoable);
        pmvie->ClearUndo();
    }
    ReleasePpo(ppgund);
}

/***************************************************************************
    4DMM Actor Studio v111 inspection/preview foundation.

    Actor Studio is deliberately separate from Object Groups now. Object
    Groups chooses/organizes objects; Actor Studio inspects one ACTR/TMPL in an
    isolated BWLD, using the same BODY/TMPL/COST plumbing as the stock APE but
    without launching the legacy Action Browser. The preview BODY never writes
    back to the scene actor in this build.
***************************************************************************/
// Actor Studio follows the active 4DMM movie viewport resolution. Modern
// 4DMM defaults to 2x (1088x612); an explicit scaled -resolution uses that
// same rational, and the native 1080p mode uses 1920x1080.
static void Get4DMMActorStudioPreviewSize(int32_t *pdxp, int32_t *pdyp)
{
    int32_t dxp = LwMul(kdxpWorkspace, 2);
    int32_t dyp = LwMul(kdypWorkspace, 2);
#if defined(BRENDER_MODERN_14)
    if (vfViewportResolution4x)
    {
        dxp = Lw4DMMScaleSourceToPresentation(kdxpWorkspace);
        dyp = Lw4DMMScaleSourceToPresentation(kdypWorkspace);
    }
    else if (vfViewportResolution1080p)
    {
        dxp = 1920;
        dyp = 1080;
    }
#endif
    if (pdxp != pvNil)
        *pdxp = dxp;
    if (pdyp != pvNil)
        *pdyp = dyp;
}

enum ASTRANSFORMMODE
{
    kastReposition = 1,
    kastPitch = 2,
    kastYaw = 3,
    kastRoll = 4,
    kastGrowShrink = 5,
    kastStretchSquish = 6,
    kastDelete = 7,
    kastCut = 8
};

struct ACTORSTUDIOPREVIEW
{
    PBWLD pbwld;
    PBODY pbody;
    PTMPL ptmpl;
    BACT bactLight;
    BLIT blit;
    int32_t arid;
    int32_t anid;
    int32_t celn;
    int32_t ipartSelected;
    int32_t kindSelected;
    int32_t ipartSelectionFirst;
    int32_t cpartSelection;
    int32_t dxpPreview;
    int32_t dypPreview;
    BRS xrCenter;
    BRS yrCenter;
    BRS zrCenter;
    BRS zrCameraBase;
    float flYaw;
    float flPitch;
    float flZoom;
    float flPanY;
    bool fOrbitTracking;
    bool fPanTracking;
    bool fPartTracking;
    bool fTrackOwnGeometryOnly;
    bool fVerticalMovement;
    bool fPlaying;
    int32_t modePartTool;
    int32_t ipartTrack;
    int32_t cpartTrack;
    int32_t rgipartTrack[kc4DMMActorStudioFramePartMax];
    BMAT34 rgbmat34PartTrackStart[kc4DMMActorStudioFramePartMax];
    BVEC3 bvec3TrackPivot;

    // Reset Rotation for a virtual Object/OG/whole-Actor selection cannot
    // identity each BODY root independently: stock roots deliberately carry
    // different authored orientations. Remember the unrotated group root
    // matrices the first time a grouped Pitch/Yaw/Roll drag begins and use
    // that authored pose as the reset target.
    bool fGroupRotationBaselineValid;
    int32_t anidGroupRotationBaseline;
    int32_t celnGroupRotationBaseline;
    int32_t kindGroupRotationBaseline;
    int32_t ipartGroupRotationBaselineFirst;
    int32_t cpartGroupRotationBaselineSelection;
    int32_t cpartGroupRotationBaselineRoots;
    int32_t rgipartGroupRotationBaseline[kc4DMMActorStudioFramePartMax];
    BMAT34 rgbmat34GroupRotationBaseline[kc4DMMActorStudioFramePartMax];

    int32_t anidTrack;
    int32_t celnTrack;
    POINT ptTrackLast;
    POINT ptPartTrackStart;
    BMAT34 bmat34PartTrackStart;
};

static ACTORSTUDIOPREVIEW vastp4DMM = {};

static const achar *Psz4DMMActorStudioType(PACTR pactr)
{
    if (pactr == pvNil || pactr->Ptmpl() == pvNil)
        return "Object";
    if (pactr->Ptmpl()->FIsTdt())
        return "3D Word";
    if (pactr->Ptmpl()->FIsProp())
        return "Prop";
    return "Actor";
}

static bool FRefresh4DMMActorStudio(bool fRebuildPreview);
static void Update4DMMActorStudioButtons(void);
static bool F4DMMActorStudioCopySelectedPart(void);
static bool F4DMMActorStudioPastePart(bool fFreezeCopyPose);
static bool F4DMMActorStudioCutSelectedPart(void);
static bool FMaybePrepare4DMMActorStudioSpawnSelectedPart(void);
static bool FBegin4DMMActorStudioPendingSpawnPlacement(void);
static bool FCancel4DMMActorStudioSpawnPlacement(void);
static bool FGet4DMMActorStudioPartScreenCenter(int32_t ipart, POINT *ppt);
static bool FUpdate4DMMActorStudioSpawnCursor(int32_t xp, int32_t yp);
static void End4DMMActorStudioSpawnCursor(bool fWarpToPart);
static void Invalidate4DMMActorStudioGroupRotationBaseline(void);

static bool F4DMMActorStudioSameTagIdentity(const TAG *ptagA, const TAG *ptagB)
{
    return ptagA != pvNil && ptagB != pvNil &&
           ptagA->sid == ptagB->sid && ptagA->ctg == ptagB->ctg && ptagA->cno == ptagB->cno;
}

static void Release4DMMActorStudioDetachedTarget(void)
{
    ReleasePpo(&vpactr4DMMActorStudioDetached);
}

static PACTR Pactr4DMMActorStudioTarget(PMVIE pmvie)
{
    if (vpactr4DMMActorStudioDetached != pvNil)
        return vpactr4DMMActorStudioDetached;
    if (pmvie == pvNil || pmvie->Pscen() == pvNil || varid4DMMActorStudioTarget == aridNil)
        return pvNil;
    return pmvie->Pscen()->PactrFromArid(varid4DMMActorStudioTarget);
}

PACTR Pactr4DMMActorStudioUndoTarget(PMVIE pmvie, CNO cnoTmpl)
{
    if (pmvie == pvNil || pmvie != vpmvie4DMMActorStudio ||
        vhwnd4DMMActorStudio == hNil || !IsWindow(vhwnd4DMMActorStudio))
        return pvNil;
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pactr == pvNil)
        return pvNil;
    TAG tag;
    pactr->GetTagTmpl(&tag);
    if (tag.sid != ksidUseCrf || tag.ctg != kctgTmpl || tag.cno != cnoTmpl || tag.pcrf == pvNil)
        return pvNil;
    return pactr;
}

static uint8_t Grf4DMMActorStudioRotationEdgeMode(int32_t mode)
{
    if (mode == kastPitch)
        return 0x01;
    if (mode == kastYaw)
        return 0x02;
    if (mode == kastRoll)
        return 0x04;
    return 0;
}

static ACTORSTUDIOROTATIONEDGE *P4DMMActorStudioRotationEdge(PMVIE pmvie, CNO cnoTmpl,
                                                             int32_t ipart, bool fCreate)
{
    for (size_t i = 0; i < vrg4DMMActorStudioRotationEdges.size(); ++i)
    {
        ACTORSTUDIOROTATIONEDGE &edge = vrg4DMMActorStudioRotationEdges[i];
        if (edge.pmvie == pmvie && edge.cnoTmpl == cnoTmpl && edge.ipart == ipart)
            return &edge;
    }
    if (!fCreate)
        return pvNil;
    ACTORSTUDIOROTATIONEDGE edge;
    ClearPb(&edge, SIZEOF(edge));
    edge.pmvie = pmvie;
    edge.cnoTmpl = cnoTmpl;
    edge.ipart = ipart;
    vrg4DMMActorStudioRotationEdges.push_back(edge);
    return &vrg4DMMActorStudioRotationEdges.back();
}

static bool F4DMMActorStudioPartIsImportedTdt(int32_t ipart)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil || ipart < 0)
        return fFalse;

    TAG tag;
    pactr->GetTagTmpl(&tag);
    int32_t idObject = 0;
    for (int32_t i = 0; i < pmvie->C4DMMCustomObjects(); ++i)
    {
        const CUSTOMOBJECT *pobj = pmvie->P4DMMCustomObject(i);
        if (pobj != pvNil && pobj->cnoOwnedTmpl == tag.cno)
        {
            idObject = pobj->id;
            break;
        }
    }
    if (idObject <= 0)
        return fFalse;

    for (int32_t i = 0; i < pmvie->C4DMMCustomParts(); ++i)
    {
        const CUSTOMPART *pmeta = pmvie->P4DMMCustomPart(i);
        if (pmeta == pvNil || pmeta->idObject != idObject || pmeta->cpart <= 0 ||
            !FIn(ipart, pmeta->ipartFirst, pmeta->ipartFirst + pmeta->cpart))
            continue;
        if (pmeta->fSourceTdt)
            return fTrue;
        if (pmvie->Pscen() != pvNil && pmeta->iscen == pmvie->Iscen())
        {
            PACTR pactrSource = pmvie->Pscen()->PactrFromArid(pmeta->aridSource);
            if (pactrSource != pvNil && pactrSource->Ptmpl() != pvNil && pactrSource->Ptmpl()->FIsTdt())
                return fTrue;
        }
        return fFalse;
    }
    return fFalse;
}

static bool F4DMMActorStudioRotationEdgeOpposite(int32_t mode, int32_t ipart)
{
    const uint8_t grfMode = Grf4DMMActorStudioRotationEdgeMode(mode);
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (grfMode == 0 || pmvie == pvNil || pactr == pvNil || ipart < 0)
        return fFalse;
    TAG tag;
    pactr->GetTagTmpl(&tag);
    ACTORSTUDIOROTATIONEDGE *pedge = P4DMMActorStudioRotationEdge(pmvie, tag.cno, ipart, fFalse);
    return pedge != pvNil && (pedge->grfOpposite & grfMode) != 0;
}

static bool FGet4DMMActorStudioRotationEdgePivot(int32_t mode, int32_t ipart, BVEC3 *pbvec3Pivot);

static bool FToggle4DMMActorStudioRotationEdge(void)
{
    const uint8_t grfMode = Grf4DMMActorStudioRotationEdgeMode(vastp4DMM.modePartTool);
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (grfMode == 0 || vastp4DMM.modePartTool == kastYaw ||
        pmvie == pvNil || pactr == pvNil || vastp4DMM.pbody == pvNil ||
        vastp4DMM.kindSelected != kastnPart || vastp4DMM.cpartSelection != 1 ||
        !FIn(vastp4DMM.ipartSelected, 0, vastp4DMM.pbody->Cpart()) ||
        !F4DMMActorStudioPartIsImportedTdt(vastp4DMM.ipartSelected))
        return fFalse;
    TAG tag;
    pactr->GetTagTmpl(&tag);
    ACTORSTUDIOROTATIONEDGE *pedge = P4DMMActorStudioRotationEdge(
        pmvie, tag.cno, vastp4DMM.ipartSelected, fTrue);
    if (pedge == pvNil)
        return fFalse;
    pedge->grfOpposite ^= grfMode;
    BVEC3 bvec3Pivot;
    const bool fOpposite = (pedge->grfOpposite & grfMode) != 0;
    const bool fHavePivot = fOpposite && FGet4DMMActorStudioRotationEdgePivot(
        vastp4DMM.modePartTool, vastp4DMM.ipartSelected, &bvec3Pivot);
    MVIE::MultiLog(pmvie,
        "actor_studio_rotation_edge toggle tmpl=%ld part=%ld mode=%ld opposite=%d pivot_valid=%d pivot=(%.6g,%.6g,%.6g)",
        (long)tag.cno, (long)vastp4DMM.ipartSelected, (long)vastp4DMM.modePartTool,
        (int)fOpposite, (int)fHavePivot,
        fHavePivot ? (double)BrScalarToFloat(bvec3Pivot.v[0]) : 0.0,
        fHavePivot ? (double)BrScalarToFloat(bvec3Pivot.v[1]) : 0.0,
        fHavePivot ? (double)BrScalarToFloat(bvec3Pivot.v[2]) : 0.0);
    Update4DMMActorStudioButtons();
    return fTrue;
}

static void Adjust4DMMActorStudioRotationEdgesAfterDelete(PMVIE pmvie, CNO cnoTmpl,
                                                           int32_t ipartFirst, int32_t cpartDelete)
{
    const int32_t ipartLim = ipartFirst + cpartDelete;
    for (size_t i = 0; i < vrg4DMMActorStudioRotationEdges.size();)
    {
        ACTORSTUDIOROTATIONEDGE &edge = vrg4DMMActorStudioRotationEdges[i];
        if (edge.pmvie != pmvie || edge.cnoTmpl != cnoTmpl)
        {
            ++i;
            continue;
        }
        if (FIn(edge.ipart, ipartFirst, ipartLim))
        {
            vrg4DMMActorStudioRotationEdges.erase(vrg4DMMActorStudioRotationEdges.begin() + i);
            continue;
        }
        if (edge.ipart >= ipartLim)
            edge.ipart -= cpartDelete;
        ++i;
    }
}

static bool FGet4DMMActorStudioRotationEdgePivot(int32_t mode, int32_t ipart, BVEC3 *pbvec3Pivot)
{
    if (pbvec3Pivot == pvNil || vastp4DMM.pbody == pvNil ||
        !FIn(ipart, 0, vastp4DMM.pbody->Cpart()) ||
        !F4DMMActorStudioPartIsImportedTdt(ipart))
        return fFalse;

    const int32_t iaxisRotate = mode == kastPitch ? 0 : mode == kastRoll ? 2 : -1;
    const uint8_t grfMode = Grf4DMMActorStudioRotationEdgeMode(mode);
    if (iaxisRotate < 0 || grfMode == 0)
        return fFalse;

    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil)
        return fFalse;
    TAG tag;
    pactr->GetTagTmpl(&tag);
    ACTORSTUDIOROTATIONEDGE *pedge = P4DMMActorStudioRotationEdge(pmvie, tag.cno, ipart, fFalse);

    // No record, or the toggle is back in its default state, means exactly
    // the historical behavior: rotate around BODY local origin. This is
    // important because toggling twice must return to the original hinge.
    if (pedge == pvNil || (pedge->grfOpposite & grfMode) == 0)
        return fFalse;

    BRB brb;
    if (!vastp4DMM.pbody->FGetPartModelBounds(ipart, &brb))
        return fFalse;

    // Imported 3D Word glyphs already have a meaningful edge-biased local
    // origin. v145 preserved that rendered geometry while zeroing the legacy
    // serialized BMDL pivot. To obtain the *opposite* hinge, reflect the local
    // origin through the model-bounds center. For a row-vector transform the
    // rotation axis itself is unaffected, so only the two perpendicular
    // coordinates need to move. This preserves any two-axis edge/corner bias
    // the glyph already has instead of guessing one perpendicular axis.
    pbvec3Pivot->v[0] = rZero;
    pbvec3Pivot->v[1] = rZero;
    pbvec3Pivot->v[2] = rZero;
    for (int32_t iaxis = 0; iaxis < 3; ++iaxis)
    {
        if (iaxis == iaxisRotate)
            continue;
        pbvec3Pivot->v[iaxis] = BrsAdd(brb.min.v[iaxis], brb.max.v[iaxis]);
    }
    return fTrue;
}

static void Build4DMMActorStudioObjectCatalog(PMVIE pmvie)
{
    vrg4DMMActorStudioObjects.clear();
    if (pmvie == pvNil)
        return;

    // The user-created section is reserved for genuinely handmade Create-Part
    // objects. Stock assets that acquired a writable movie-local replacement
    // remain one logical stock Actor/Prop in the UI.
    for (int32_t iPass = 0; iPass < 2; ++iPass)
    {
        const bool fProp = iPass != 0;
        for (int32_t i = 0; i < pmvie->C4DMMCustomObjects(); ++i)
        {
            const CUSTOMOBJECT *pobj = pmvie->P4DMMCustomObject(i);
            if (pobj == pvNil || !pmvie->F4DMMCustomObjectIsHandmade(i) || pobj->fProp != fProp)
                continue;
            ACTORSTUDIOOBJECTENTRY item;
            ClearPb(&item, SIZEOF(item));
            item.sid = pobj->sidTemplate;
            item.ctg = pobj->ctgTemplate;
            item.cno = pobj->cnoTemplate;
            item.cnoOwnedTmpl = pobj->cnoOwnedTmpl;
            item.idCustom = pobj->id;
            item.fProp = pobj->fProp;
            item.fHandmade = fTrue;
            sprintf_s(item.szName, SIZEOF(item.szName), "%s: %s",
                      item.fProp ? "Custom Prop" : "Custom Actor", pobj->szName);
            vrg4DMMActorStudioObjects.push_back(item);
        }
    }

    // Stock Actors followed by stock Props. Each original stock identity is
    // listed exactly once even when the movie has promoted it to a hidden
    // writable replacement.
    for (int32_t iPass = 0; iPass < 2; ++iPass)
    {
        const bool fProp = iPass != 0;
        PCRM pcrm = CRM::PcrmNew(1);
        if (pcrm == pvNil)
            continue;
        CKI ckiRoot;
        ckiRoot.ctg = fProp ? kctgPrth : kctgTmth;
        ckiRoot.cno = cnoNil;
        PBCL pbcl = BCL::PbclNew(pcrm, &ckiRoot, ctgNil, pvNil, fTrue);
        if (pbcl != pvNil)
        {
            for (int32_t ithd = 0; ithd < pbcl->IthdMac(); ++ithd)
            {
                THD thd;
                ClearPb(&thd, SIZEOF(thd));
                pbcl->GetThd(ithd, &thd);
                PTMPL ptmpl = (PTMPL)vptagm->PbacoFetch(&thd.tag, TMPL::FReadTmpl);
                if (ptmpl == pvNil)
                    continue;
                const bool fMatch = !ptmpl->FIsTdt() && ptmpl->FIsProp() == fProp;
                if (fMatch)
                {
                    bool fDuplicate = fFalse;
                    for (size_t i = 0; i < vrg4DMMActorStudioObjects.size(); ++i)
                    {
                        const ACTORSTUDIOOBJECTENTRY &old = vrg4DMMActorStudioObjects[i];
                        if (!old.fHandmade && old.sid == thd.tag.sid &&
                            old.ctg == thd.tag.ctg && old.cno == thd.tag.cno)
                        {
                            fDuplicate = fTrue;
                            break;
                        }
                    }
                    if (!fDuplicate)
                    {
                        ACTORSTUDIOOBJECTENTRY item;
                        ClearPb(&item, SIZEOF(item));
                        item.sid = thd.tag.sid;
                        item.ctg = thd.tag.ctg;
                        item.cno = thd.tag.cno;
                        item.cnoOwnedTmpl = cnoNil;
                        item.idCustom = 0;
                        item.fProp = fProp;
                        item.fHandmade = fFalse;
                        TAG tagResolved;
                        if (pmvie->FResolve4DMMReplacementTemplateTag(&thd.tag, &tagResolved))
                            item.cnoOwnedTmpl = tagResolved.cno;
                        STN stn;
                        ptmpl->GetName(&stn);
                        sprintf_s(item.szName, SIZEOF(item.szName), "%s: %s",
                                  fProp ? "Prop" : "Actor", stn.Psz());
                        vrg4DMMActorStudioObjects.push_back(item);
                    }
                }
                ReleasePpo(&ptmpl);
            }
        }
        ReleasePpo(&pbcl);
        ReleasePpo(&pcrm);
    }
}

static bool F4DMMActorStudioEntryMatchesTag(PMVIE pmvie, const ACTORSTUDIOOBJECTENTRY &item,
                                            const TAG *ptag)
{
    if (ptag == pvNil)
        return fFalse;
    TAG tagSource;
    ClearPb(&tagSource, SIZEOF(tagSource));
    tagSource.sid = item.sid;
    tagSource.ctg = item.ctg;
    tagSource.cno = item.cno;
    if (F4DMMActorStudioSameTagIdentity(&tagSource, ptag))
        return fTrue;
    return item.cnoOwnedTmpl != cnoNil && ptag->sid == ksidUseCrf &&
           ptag->ctg == kctgTmpl && ptag->cno == item.cnoOwnedTmpl;
}

static PACTR Pactr4DMMActorStudioFindLiveObject(PMVIE pmvie, const ACTORSTUDIOOBJECTENTRY &item)
{
    if (pmvie == pvNil || pmvie->Pscen() == pvNil)
        return pvNil;
    PGL pglRoll = pmvie->Pscen()->PglRollCall();
    if (pglRoll == pvNil)
        return pvNil;
    for (int32_t iactr = 0; iactr < pglRoll->IvMac(); ++iactr)
    {
        PACTR pactr = pvNil;
        pglRoll->Get(iactr, &pactr);
        if (pactr == pvNil)
            continue;
        TAG tag;
        pactr->GetTagTmpl(&tag);
        if (F4DMMActorStudioEntryMatchesTag(pmvie, item, &tag))
            return pactr;
    }
    return pvNil;
}

static bool FSelect4DMMActorStudioObjectEntry(int32_t iObject)
{
    PMVIE pmvie = vpmvie4DMMActorStudio;
    if (pmvie == pvNil || !FIn(iObject, 0, (int32_t)vrg4DMMActorStudioObjects.size()))
        return fFalse;
    const ACTORSTUDIOOBJECTENTRY &item = vrg4DMMActorStudioObjects[iObject];

    MVIE::MultiLog(pmvie,
        "actor_studio_object_select begin index=%ld handmade=%d sid=%ld ctg=0x%08lX cno=%ld owned_tmpl=%ld name=%s",
        (long)iObject, (int)item.fHandmade, (long)item.sid,
        (unsigned long)item.ctg, (long)item.cno, (long)item.cnoOwnedTmpl, item.szName);

    PACTR pactrLive = Pactr4DMMActorStudioFindLiveObject(pmvie, item);
    if (pactrLive != pvNil)
    {
        MVIE::MultiLog(pmvie, "actor_studio_object_select live arid=%ld refresh_begin",
                       (long)pactrLive->Arid());
        Release4DMMActorStudioDetachedTarget();
        varid4DMMActorStudioTarget = pactrLive->Arid();
        ClearPb(&vasframe4DMMActorStudioClipboard, SIZEOF(vasframe4DMMActorStudioClipboard));
        const bool fRefreshed = FRefresh4DMMActorStudio(fTrue);
        MVIE::MultiLog(pmvie, "actor_studio_object_select live refresh_return ok=%d",
                       (int)fRefreshed);
        return fRefreshed;
    }

    if (item.fHandmade && item.cnoOwnedTmpl == cnoNil)
    {
        MessageBoxA(vhwnd4DMMActorStudio,
            "This handmade Actor/Prop definition does not have a synthesized BODY/TMPL yet.",
            "4DMM Actor Studio", MB_OK | MB_ICONINFORMATION);
        return fFalse;
    }

    TAG tag;
    bool fOpened = fFalse;
    ClearPb(&tag, SIZEOF(tag));
    if (item.cnoOwnedTmpl != cnoNil)
    {
        MVIE::MultiLog(pmvie, "actor_studio_object_select owned_tag_open begin tmpl=%ld",
                       (long)item.cnoOwnedTmpl);
        if (!pmvie->FOpen4DMMOwnedTemplateTag(item.cnoOwnedTmpl, &tag))
        {
            MVIE::MultiLog(pmvie, "actor_studio_object_select owned_tag_open fail tmpl=%ld",
                           (long)item.cnoOwnedTmpl);
            return fFalse;
        }
        fOpened = fTrue;
        MVIE::MultiLog(pmvie,
            "actor_studio_object_select owned_tag_open ok sid=%ld ctg=0x%08lX cno=%ld pcrf=%p",
            (long)tag.sid, (unsigned long)tag.ctg, (long)tag.cno, tag.pcrf);
    }
    else
    {
        // Stock SIDs are resolved by TAGM directly and do not carry a CRF.
        // FOpenTag is only meaningful for ksidUseCrf tags.
        tag.sid = item.sid;
        tag.ctg = item.ctg;
        tag.cno = item.cno;
    }

    MVIE::MultiLog(pmvie,
        "actor_studio_object_select detached_pactr_new begin sid=%ld ctg=0x%08lX cno=%ld",
        (long)tag.sid, (unsigned long)tag.ctg, (long)tag.cno);
    PACTR pactrNew = ACTR::PactrNew(&tag);
    MVIE::MultiLog(pmvie, "actor_studio_object_select detached_pactr_new return pactr=%p",
                   pactrNew);
    if (fOpened)
    {
        TAGM::CloseTag(&tag);
        MVIE::MultiLog(pmvie, "actor_studio_object_select owned_tag_closed");
    }
    if (pactrNew == pvNil)
        return fFalse;

    Release4DMMActorStudioDetachedTarget();
    vpactr4DMMActorStudioDetached = pactrNew;
    varid4DMMActorStudioTarget = aridNil;
    ClearPb(&vasframe4DMMActorStudioClipboard, SIZEOF(vasframe4DMMActorStudioClipboard));
    MVIE::MultiLog(pmvie, "actor_studio_object_select detached refresh_begin pactr=%p tmpl=%p",
                   pactrNew, pactrNew->Ptmpl());
    const bool fRefreshed = FRefresh4DMMActorStudio(fTrue);
    MVIE::MultiLog(pmvie, "actor_studio_object_select detached refresh_return ok=%d",
                   (int)fRefreshed);
    return fRefreshed;
}

static void Fill4DMMActorStudioObjects(void)
{
    if (vhwnd4DMMActorStudio == hNil || !IsWindow(vhwnd4DMMActorStudio))
        return;
    HWND hwndObject = GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioObject);
    if (hwndObject == hNil)
        return;
    Build4DMMActorStudioObjectCatalog(vpmvie4DMMActorStudio);
    SendMessageA(hwndObject, CB_RESETCONTENT, 0, 0);
    PACTR pactr = Pactr4DMMActorStudioTarget(vpmvie4DMMActorStudio);
    TAG tagCurrent;
    bool fHaveCurrent = fFalse;
    if (pactr != pvNil)
    {
        pactr->GetTagTmpl(&tagCurrent);
        fHaveCurrent = fTrue;
    }
    for (size_t i = 0; i < vrg4DMMActorStudioObjects.size(); ++i)
    {
        const LRESULT irow = SendMessageA(hwndObject, CB_ADDSTRING, 0,
                                          (LPARAM)vrg4DMMActorStudioObjects[i].szName);
        if (irow == CB_ERR || irow == CB_ERRSPACE)
            continue;
        SendMessageA(hwndObject, CB_SETITEMDATA, (WPARAM)irow, (LPARAM)i);
        if (fHaveCurrent && F4DMMActorStudioEntryMatchesTag(vpmvie4DMMActorStudio,
                                                            vrg4DMMActorStudioObjects[i],
                                                            &tagCurrent))
            SendMessageA(hwndObject, CB_SETCURSEL, (WPARAM)irow, 0);
    }
}

static bool F4DMMActorStudioPartDescendsFrom(int32_t ipart, int32_t ipartAncestor)
{
    if (vastp4DMM.pbody == pvNil || ipart == ipartAncestor ||
        !FIn(ipart, 0, vastp4DMM.pbody->Cpart()) ||
        !FIn(ipartAncestor, 0, vastp4DMM.pbody->Cpart()))
        return fFalse;
    int32_t ipartParent = vastp4DMM.pbody->IpartParent(ipart);
    for (int32_t cstep = 0; ipartParent != ivNil && cstep < vastp4DMM.pbody->Cpart(); ++cstep)
    {
        if (ipartParent == ipartAncestor)
            return fTrue;
        ipartParent = vastp4DMM.pbody->IpartParent(ipartParent);
    }
    return fFalse;
}

static void Build4DMMActorStudioDefaultPartInfo(void)
{
    vrg4DMMActorStudioDefaultPartInfo.clear();
    vf4DMMActorStudioHasDefaultPartGroups = fFalse;
    if (vastp4DMM.pbody == pvNil)
        return;

    const int32_t cpart = vastp4DMM.pbody->Cpart();
    vrg4DMMActorStudioDefaultPartInfo.resize(cpart);
    const bool fTdt = vastp4DMM.ptmpl != pvNil && vastp4DMM.ptmpl->FIsTdt();
    std::vector<uint8_t> rgfRenderable((size_t)cpart, 0);

    // A BODY node is allowed to be BOTH an editable Part and a default Part
    // Group. This distinction matters for stock assets such as the DORAEMON
    // pink door: the parent node owns the door geometry itself while its child
    // owns the knob. In flat view the door and knob must therefore be two
    // independently editable Parts; in grouped view the same parent transform
    // also appears as a Part Group containing the door Part plus the knob Part.
    // Model-less nodes are useful PGs only when they organize at least two
    // renderable descendants. A model-less zero/one-descendant node remains an
    // editor-useless anchor/redundant handle and is hidden completely.
    for (int32_t ipart = 0; ipart < cpart; ++ipart)
    {
        ACTORSTUDIODEFAULTPARTINFO &info = vrg4DMMActorStudioDefaultPartInfo[ipart];
        ClearPb(&info, SIZEOF(info));
        info.ipartParent = vastp4DMM.pbody->IpartParent(ipart);
        vastp4DMM.pbody->FGetPartModelGeometry(ipart, &info.cver, &info.cfac);
        rgfRenderable[(size_t)ipart] = (info.cver > 0 || info.cfac > 0) ? 1 : 0;
        info.fPart = rgfRenderable[(size_t)ipart] != 0;
        info.fGroup = fFalse;
    }

    if (!fTdt)
    {
        for (int32_t ipartGroup = 0; ipartGroup < cpart; ++ipartGroup)
        {
            ACTORSTUDIODEFAULTPARTINFO &group = vrg4DMMActorStudioDefaultPartInfo[ipartGroup];
            int32_t cdescRenderable = 0;
            for (int32_t ipart = 0; ipart < cpart; ++ipart)
            {
                if (ipart != ipartGroup && rgfRenderable[(size_t)ipart] &&
                    F4DMMActorStudioPartDescendsFrom(ipart, ipartGroup))
                    ++cdescRenderable;
            }
            group.cpartDescendant = cdescRenderable;
            group.fGroup = group.fPart ? cdescRenderable >= 1 : cdescRenderable >= 2;
        }
    }

    int32_t nPart = 0;
    int32_t nGroup = 0;
    int32_t cAnchor = 0;
    int32_t cPartAndGroup = 0;
    for (int32_t ipart = 0; ipart < cpart; ++ipart)
    {
        ACTORSTUDIODEFAULTPARTINFO &info = vrg4DMMActorStudioDefaultPartInfo[ipart];
        if (info.fPart)
            info.nPart = ++nPart;
        if (info.fGroup)
            info.nGroup = ++nGroup;
        if (!info.fPart && !info.fGroup)
            ++cAnchor;
        if (info.fPart && info.fGroup)
            ++cPartAndGroup;
    }

    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    MVIE::MultiLog(pmvie,
        "actor_studio_default_parts summary raw=%ld parts=%ld groups=%ld part_and_group=%ld anchors=%ld tdt=%d sort=%d classifier=dual_geometry_hierarchy",
        (long)cpart, (long)nPart, (long)nGroup, (long)cPartAndGroup,
        (long)cAnchor, (int)fTdt, (int)vf4DMMActorStudioSortDefaultPartGroups);
    for (int32_t ipart = 0; ipart < cpart; ++ipart)
    {
        const ACTORSTUDIODEFAULTPARTINFO &info = vrg4DMMActorStudioDefaultPartInfo[ipart];
        const char *pszClass = info.fPart && info.fGroup ? "part+part_group" :
                               info.fPart ? "part" :
                               info.fGroup ? "part_group" : "anchor";
        MVIE::MultiLog(pmvie,
            "actor_studio_default_part raw=%ld user=%ld parent=%ld verts=%ld faces=%ld renderable_descendants=%ld class=%s is_part=%d is_group=%d pt=%ld group=%ld",
            (long)ipart, (long)(ipart + 1), (long)info.ipartParent,
            (long)info.cver, (long)info.cfac, (long)info.cpartDescendant,
            pszClass, (int)info.fPart, (int)info.fGroup,
            (long)info.nPart, (long)info.nGroup);
    }
}

static bool F4DMMActorStudioDefaultPartIsPart(int32_t ipart)
{
    if (!FIn(ipart, 0, (int32_t)vrg4DMMActorStudioDefaultPartInfo.size()))
        return fTrue;
    return vrg4DMMActorStudioDefaultPartInfo[ipart].fPart;
}

static bool F4DMMActorStudioDefaultPartIsGroup(int32_t ipart)
{
    return FIn(ipart, 0, (int32_t)vrg4DMMActorStudioDefaultPartInfo.size()) &&
           vrg4DMMActorStudioDefaultPartInfo[ipart].fGroup;
}

static bool F4DMMActorStudioDefaultPartIsAnchor(int32_t ipart)
{
    return FIn(ipart, 0, (int32_t)vrg4DMMActorStudioDefaultPartInfo.size()) &&
           !vrg4DMMActorStudioDefaultPartInfo[ipart].fPart &&
           !vrg4DMMActorStudioDefaultPartInfo[ipart].fGroup;
}

static void Format4DMMActorStudioVisiblePartOrdinal(int32_t nPart, achar *psz, size_t cb)
{
    if (psz == pvNil || cb == 0)
        return;
    // Preserve Actor Studio's established visible-part naming contract: the
    // first 26 parts are A..Z, then numbering resumes at 27 and continues
    // numerically. Hidden anchors and PG aliases do not consume Part ordinals.
    if (FIn(nPart, 1, 27))
        sprintf_s(psz, cb, "Pt. %c", (char)('A' + nPart - 1));
    else
        sprintf_s(psz, cb, "Pt. %d", (int)nPart);
}

static void Format4DMMActorStudioPartName(int32_t ipart, achar *psz, size_t cb)
{
    if (psz == pvNil || cb == 0)
        return;
    if (FIn(ipart, 0, (int32_t)vrg4DMMActorStudioDefaultPartInfo.size()))
    {
        const ACTORSTUDIODEFAULTPARTINFO &info = vrg4DMMActorStudioDefaultPartInfo[ipart];
        if (info.fPart && info.nPart > 0)
        {
            Format4DMMActorStudioVisiblePartOrdinal(info.nPart, psz, cb);
            return;
        }
        if (info.fGroup && info.nGroup > 0)
        {
            sprintf_s(psz, cb, "Pt Group %d", (int)info.nGroup);
            return;
        }
        if (!info.fPart && !info.fGroup)
        {
            sprintf_s(psz, cb, "Hidden anchor");
            return;
        }
    }
    Format4DMMActorStudioVisiblePartOrdinal(ipart + 1, psz, cb);
}

static void Format4DMMActorStudioPartGroupName(int32_t ipart, achar *psz, size_t cb)
{
    if (psz == pvNil || cb == 0)
        return;
    if (FIn(ipart, 0, (int32_t)vrg4DMMActorStudioDefaultPartInfo.size()))
    {
        const ACTORSTUDIODEFAULTPARTINFO &info = vrg4DMMActorStudioDefaultPartInfo[ipart];
        if (info.fGroup && info.nGroup > 0)
        {
            sprintf_s(psz, cb, "Pt Group %d", (int)info.nGroup);
            return;
        }
    }
    sprintf_s(psz, cb, "Pt Group");
}

static void Reset4DMMActorStudioPreview(void)
{
    if (vf4DMMActorStudioSpawnPlacement || vf4DMMActorStudioSpawnCursorHidden)
        End4DMMActorStudioSpawnCursor(fFalse);
    vf4DMMActorStudioSpawnPlacementPending = fFalse;
    vf4DMMActorStudioSpawnPlacement = fFalse;
    vipart4DMMActorStudioSpawnPending = ivNil;
    if (vastp4DMM.blit.type == BR_LIGHT_DIRECT)
    {
        BrLightDisable(&vastp4DMM.bactLight);
        if (vastp4DMM.bactLight.prev != pvNil)
            BrActorRemove(&vastp4DMM.bactLight);
    }

    if (vastp4DMM.pbody != pvNil)
        vastp4DMM.pbody->ClearPartHilite();
    ReleasePpo(&vastp4DMM.pbody);
    ReleasePpo(&vastp4DMM.ptmpl);
    ReleasePpo(&vastp4DMM.pbwld);
    ClearPb(&vastp4DMM, SIZEOF(vastp4DMM));
    vastp4DMM.arid = aridNil;
    vastp4DMM.anid = ivNil;
    vastp4DMM.celn = ivNil;
    vastp4DMM.ipartSelected = ivNil;
    vastp4DMM.kindSelected = kastnPart;
    vastp4DMM.ipartSelectionFirst = ivNil;
    vastp4DMM.cpartSelection = 0;
    vastp4DMM.ipartTrack = ivNil;
    vastp4DMM.cpartTrack = 0;
    vastp4DMM.anidTrack = ivNil;
    vastp4DMM.celnTrack = ivNil;
    vastp4DMM.modePartTool = kastReposition;
    vastp4DMM.flZoom = 1.0f;
}

static void Compute4DMMActorStudioFraming(void)
{
    if (vastp4DMM.pbody == pvNil)
        return;

    MVIE::MultiLog(vpmvie4DMMActorStudio,
        "actor_studio_preview framing_bounds begin body=%p parts=%ld",
        vastp4DMM.pbody, (long)vastp4DMM.pbody->Cpart());
    BCB bcbBody;
    vastp4DMM.pbody->GetBcbBounds(&bcbBody, fTrue);
    MVIE::MultiLog(vpmvie4DMMActorStudio,
        "actor_studio_preview framing_bounds return min=(%g,%g,%g) max=(%g,%g,%g)",
        (double)BrScalarToFloat(bcbBody.xrMin), (double)BrScalarToFloat(bcbBody.yrMin),
        (double)BrScalarToFloat(bcbBody.zrMin), (double)BrScalarToFloat(bcbBody.xrMax),
        (double)BrScalarToFloat(bcbBody.yrMax), (double)BrScalarToFloat(bcbBody.zrMax));
    BRS dxrBody = bcbBody.xrMax - bcbBody.xrMin;
    BRS dyrBody = bcbBody.yrMax - bcbBody.yrMin;
    BRS dzrBody = bcbBody.zrMax - bcbBody.zrMin;
    if (dxrBody <= rZero)
        dxrBody = rOne;
    if (dyrBody <= rZero)
        dyrBody = rOne;
    if (dzrBody <= rZero)
        dzrBody = rOne;

    vastp4DMM.xrCenter = BrsHalf(bcbBody.xrMax + bcbBody.xrMin);
    vastp4DMM.yrCenter = BrsHalf(bcbBody.yrMax + bcbBody.yrMin);
    vastp4DMM.zrCenter = BrsHalf(bcbBody.zrMax + bcbBody.zrMin);

    const BRS rDydxBody = BrsDiv(dyrBody, dxrBody);
    const BRS dxrView = BrIntToScalar(LwMax(vastp4DMM.dxpPreview, 1));
    const BRS dyrView = BrIntToScalar(LwMax(vastp4DMM.dypPreview, 1));
    const BRS rDydxView = BrsDiv(dyrView, dxrView);
    const BRA aHalfFov = BrScalarToAngle(BrsHalf(BrAngleToScalar(BR_ANGLE_DEG(60.0))));
    const BRS rAtan = BrsDiv(BR_COS(aHalfFov), BR_SIN(aHalfFov));

    BRS zrCamera;
    if (rDydxBody > rDydxView)
        zrCamera = BrsMul(BrsHalf(dyrBody), rAtan);
    else
        zrCamera = BrsMul(BrsHalf(BrsMul(dxrBody, rDydxView)), rAtan);
    zrCamera += bcbBody.zrMax;

    vastp4DMM.zrCameraBase = zrCamera - vastp4DMM.zrCenter;
    if (vastp4DMM.zrCameraBase < BR_SCALAR(2.0))
        vastp4DMM.zrCameraBase = BR_SCALAR(2.0);
}

static void Apply4DMMActorStudioCamera(void)
{
    if (vastp4DMM.pbwld == pvNil || vastp4DMM.pbody == pvNil)
        return;

    BMAT34 bmat34Camera;
    BrMatrix34Identity(&bmat34Camera);
    BrMatrix34PostRotateX(&bmat34Camera,
        BrDegreeToAngle(BrFloatToScalar(vastp4DMM.flPitch)));
    BrMatrix34PostRotateY(&bmat34Camera,
        BrDegreeToAngle(BrFloatToScalar(vastp4DMM.flYaw)));

    float flZoom = vastp4DMM.flZoom;
    if (flZoom < 0.15f)
        flZoom = 0.15f;
    if (flZoom > 8.0f)
        flZoom = 8.0f;
    vastp4DMM.flZoom = flZoom;

    const BRS zrDistance = BrFloatToScalar(
        BrScalarToFloat(vastp4DMM.zrCameraBase) / flZoom);
    const BRS yrPan = BrFloatToScalar(vastp4DMM.flPanY);

    bmat34Camera.m[3][0] = vastp4DMM.xrCenter +
        BrsMul(bmat34Camera.m[2][0], zrDistance) +
        BrsMul(bmat34Camera.m[1][0], yrPan);
    bmat34Camera.m[3][1] = vastp4DMM.yrCenter +
        BrsMul(bmat34Camera.m[2][1], zrDistance) +
        BrsMul(bmat34Camera.m[1][1], yrPan);
    bmat34Camera.m[3][2] = vastp4DMM.zrCenter +
        BrsMul(bmat34Camera.m[2][2], zrDistance) +
        BrsMul(bmat34Camera.m[1][2], yrPan);

    const BRS zrHither = zrDistance < BR_SCALAR(10.0) ? BR_SCALAR(0.1) : rOne;
    vastp4DMM.pbwld->SetCamera(&bmat34Camera, zrHither, BR_SCALAR(1000.0), BR_ANGLE_DEG(60.0));
}

static void Paint4DMMActorStudioViewport(HWND hwnd)
{
    if (hwnd == hNil || vastp4DMM.pbwld == pvNil)
        return;
    PGPT pgptWnd = GPT::PgptNewHwnd(hwnd);
    if (pgptWnd == pvNil)
        return;
    GNV gnvWnd(pgptWnd);
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    RC rc;
    rc.Set(0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top);
    vastp4DMM.pbwld->Draw(&gnvWnd, &rc, 0, 0);
    ReleasePpo(&pgptWnd);
}

static void Render4DMMActorStudioPreview(void)
{
    if (vastp4DMM.pbwld == pvNil || vastp4DMM.pbody == pvNil)
        return;
    // Per-paint begin/return logging produced hundreds of identical lines and
    // never helped localize the Duplicate crash. Keep the structural preview
    // build/PbodyCreate markers, which are the useful boundary.
    Apply4DMMActorStudioCamera();
    vastp4DMM.pbwld->MarkDirty();
    vastp4DMM.pbwld->Render();
    if (vhwnd4DMMActorStudioViewport != hNil && IsWindow(vhwnd4DMMActorStudioViewport))
    {
        InvalidateRect(vhwnd4DMMActorStudioViewport, pvNil, fFalse);
        UpdateWindow(vhwnd4DMMActorStudioViewport);
    }
}

static void Clear4DMMActorStudioPartSelection(bool fRender);
static bool F4DMMActorStudioSelectionPresentAtCurrentFrame(void);

static void Apply4DMMActorStudioSelectionHilite(void)
{
    if (vastp4DMM.pbody == pvNil || vastp4DMM.cpartSelection <= 0 ||
        !FIn(vastp4DMM.ipartSelectionFirst, 0, vastp4DMM.pbody->Cpart()) ||
        vastp4DMM.ipartSelectionFirst + vastp4DMM.cpartSelection > vastp4DMM.pbody->Cpart())
        return;

    if (vastp4DMM.kindSelected == kastnDefaultPartGroup &&
        vastp4DMM.cpartSelection == 1)
        vastp4DMM.pbody->HilitePartTree(vastp4DMM.ipartSelectionFirst);
    else
        vastp4DMM.pbody->HilitePartRange(vastp4DMM.ipartSelectionFirst,
                                         vastp4DMM.cpartSelection);
}

static bool FSet4DMMActorStudioActionCel(int32_t anid, int32_t celn, bool fReframe)
{
    if (vastp4DMM.ptmpl == pvNil || vastp4DMM.pbody == pvNil)
        return fFalse;
    int32_t ccel = 0;
    if (anid < 0 || anid >= vastp4DMM.ptmpl->Cactn() ||
        !vastp4DMM.ptmpl->FGetCcelActn(anid, &ccel) || ccel <= 0)
        return fFalse;
    if (celn < 0)
        celn = 0;
    if (celn >= ccel)
        celn = ccel - 1;
    MVIE::MultiLog(vpmvie4DMMActorStudio,
        "actor_studio_preview set_action_cel begin tmpl=%p body=%p action=%ld cel=%ld parts=%ld",
        vastp4DMM.ptmpl, vastp4DMM.pbody, (long)anid, (long)celn,
        (long)vastp4DMM.pbody->Cpart());
    if (!vastp4DMM.ptmpl->FSetActnCel(vastp4DMM.pbody, anid, celn))
    {
        MVIE::MultiLog(vpmvie4DMMActorStudio,
            "actor_studio_preview set_action_cel fail action=%ld cel=%ld",
            (long)anid, (long)celn);
        return fFalse;
    }
    MVIE::MultiLog(vpmvie4DMMActorStudio,
        "actor_studio_preview set_action_cel return action=%ld cel=%ld",
        (long)anid, (long)celn);

    const bool fFrameChanged = vastp4DMM.anid != anid || vastp4DMM.celn != celn;
    vastp4DMM.anid = anid;
    vastp4DMM.celn = celn;
    if (fFrameChanged && vastp4DMM.ipartSelected != ivNil)
    {
        // Preserve a hierarchy selection across frame/action navigation while
        // that exact selected entity still has renderable geometry on the new
        // cel.  Only clear the stale selection when temporal Cut has made the
        // selected Part/PG/Object/OG/EVERYTHING genuinely absent.
        if (F4DMMActorStudioSelectionPresentAtCurrentFrame())
        {
            vastp4DMM.pbody->ClearPartHilite();
            if (vastp4DMM.ipartSelectionFirst >= 0 && vastp4DMM.cpartSelection > 0 &&
                vastp4DMM.ipartSelectionFirst + vastp4DMM.cpartSelection <=
                    vastp4DMM.pbody->Cpart())
                Apply4DMMActorStudioSelectionHilite();
            MVIE::MultiLog(vpmvie4DMMActorStudio,
                "actor_studio_selection frame_change_keep action=%ld cel=%ld kind=%ld first=%ld parts=%ld",
                (long)anid, (long)celn, (long)vastp4DMM.kindSelected,
                (long)vastp4DMM.ipartSelectionFirst, (long)vastp4DMM.cpartSelection);
        }
        else
        {
            MVIE::MultiLog(vpmvie4DMMActorStudio,
                "actor_studio_selection frame_change_clear_absent action=%ld cel=%ld kind=%ld first=%ld parts=%ld",
                (long)anid, (long)celn, (long)vastp4DMM.kindSelected,
                (long)vastp4DMM.ipartSelectionFirst, (long)vastp4DMM.cpartSelection);
            Clear4DMMActorStudioPartSelection(fFalse);
        }
    }
    if (fReframe)
        Compute4DMMActorStudioFraming();
    Render4DMMActorStudioPreview();
    MVIE::MultiLog(vpmvie4DMMActorStudio,
        "actor_studio_pose arid=%ld action=%ld cel=%ld parts=%ld",
        (long)vastp4DMM.arid, (long)anid, (long)celn,
        (long)vastp4DMM.pbody->Cpart());
    return fTrue;
}

static bool FBuild4DMMActorStudioPreview(PMVIE pmvie, PACTR pactr)
{
    Reset4DMMActorStudioPreview();
    if (pmvie == pvNil || pactr == pvNil || pactr->Ptmpl() == pvNil)
        return fFalse;

    TAG tagDiag;
    pactr->GetTagTmpl(&tagDiag);
    MVIE::MultiLog(pmvie,
        "actor_studio_preview build begin pactr=%p arid=%ld tmpl=%p sid=%ld ctg=0x%08lX cno=%ld actions=%ld",
        pactr, (long)pactr->Arid(), pactr->Ptmpl(), (long)tagDiag.sid,
        (unsigned long)tagDiag.ctg, (long)tagDiag.cno, (long)pactr->Ptmpl()->Cactn());

    COST cost;
    const bool fHaveSceneCost = pactr->Pbody() != pvNil && cost.FGet(pactr->Pbody());

    TAG tagTmpl;
    pactr->GetTagTmpl(&tagTmpl);
    if (tagTmpl.sid == ksidUseCrf)
    {
        const int32_t cactnBefore = pactr->Ptmpl()->Cactn();
        pactr->Ptmpl()->RefreshActionCount();
        MVIE::MultiLog(pmvie,
            "actor_studio_action_count_open tmpl=%ld before=%ld after=%ld",
            (long)tagTmpl.cno, (long)cactnBefore, (long)pactr->Ptmpl()->Cactn());
    }

    vastp4DMM.ptmpl = pactr->Ptmpl();
    vastp4DMM.ptmpl->AddRef();
    MVIE::MultiLog(pmvie, "actor_studio_preview pbody_create begin tmpl=%p actions=%ld",
                   vastp4DMM.ptmpl, (long)vastp4DMM.ptmpl->Cactn());
    vastp4DMM.pbody = vastp4DMM.ptmpl->PbodyCreate();
    MVIE::MultiLog(pmvie, "actor_studio_preview pbody_create return body=%p parts=%ld sets=%ld",
                   vastp4DMM.pbody,
                   vastp4DMM.pbody != pvNil ? (long)vastp4DMM.pbody->Cpart() : -1L,
                   vastp4DMM.pbody != pvNil ? (long)vastp4DMM.pbody->Cbset() : -1L);
    if (vastp4DMM.pbody == pvNil)
    {
        Reset4DMMActorStudioPreview();
        return fFalse;
    }
    if (fHaveSceneCost)
        cost.Set(vastp4DMM.pbody);

    Get4DMMActorStudioPreviewSize(&vastp4DMM.dxpPreview, &vastp4DMM.dypPreview);
    MVIE::MultiLog(pmvie, "actor_studio_preview bwld_create begin size=%ldx%ld",
                   (long)vastp4DMM.dxpPreview, (long)vastp4DMM.dypPreview);
    vastp4DMM.pbwld = BWLD::PbwldNew(vastp4DMM.dxpPreview, vastp4DMM.dypPreview);
    MVIE::MultiLog(pmvie, "actor_studio_preview bwld_create return bwld=%p", vastp4DMM.pbwld);
    if (vastp4DMM.pbwld == pvNil)
    {
        Reset4DMMActorStudioPreview();
        return fFalse;
    }

    ClearPb(&vastp4DMM.blit, SIZEOF(vastp4DMM.blit));
    ClearPb(&vastp4DMM.bactLight, SIZEOF(vastp4DMM.bactLight));
    vastp4DMM.blit.type = BR_LIGHT_DIRECT;
    vastp4DMM.blit.colour = BR_COLOUR_RGB(0xff, 0xff, 0xff);
    vastp4DMM.blit.attenuation_c = rOne;
    vastp4DMM.bactLight.type = BR_ACTOR_LIGHT;
    vastp4DMM.bactLight.type_data = &vastp4DMM.blit;
    vastp4DMM.bactLight.t.type = BR_TRANSFORM_MATRIX34;
    BrMatrix34Identity(&vastp4DMM.bactLight.t.t.mat);
    BrMatrix34PostRotateX(&vastp4DMM.bactLight.t.t.mat, BR_ANGLE_DEG(-40.0));
    BrMatrix34PostRotateY(&vastp4DMM.bactLight.t.t.mat, BR_ANGLE_DEG(-40.0));
    MVIE::MultiLog(pmvie, "actor_studio_preview light_add begin bwld=%p", vastp4DMM.pbwld);
    vastp4DMM.pbwld->AddActor(&vastp4DMM.bactLight);
    BrLightEnable(&vastp4DMM.bactLight);
    MVIE::MultiLog(pmvie, "actor_studio_preview light_add return");

    MVIE::MultiLog(pmvie, "actor_studio_preview body_attach begin body=%p bwld=%p",
                   vastp4DMM.pbody, vastp4DMM.pbwld);
    vastp4DMM.pbody->SetBwld(vastp4DMM.pbwld);
    MVIE::MultiLog(pmvie, "actor_studio_preview body_set_bwld return");
    vastp4DMM.pbody->Show();
    MVIE::MultiLog(pmvie, "actor_studio_preview body_show return");

    BRA xa, ya, za;
    BMAT34 bmat34Rest;
    MVIE::MultiLog(pmvie, "actor_studio_preview rest_orientation begin");
    vastp4DMM.ptmpl->GetRestOrien(&xa, &ya, &za);
    BrMatrix34Identity(&bmat34Rest);
    BrMatrix34PostRotateX(&bmat34Rest, xa);
    BrMatrix34PostRotateY(&bmat34Rest, ya);
    BrMatrix34PostRotateZ(&bmat34Rest, za);
    vastp4DMM.pbody->LocateOrient(rZero, rZero, rZero, &bmat34Rest);
    MVIE::MultiLog(pmvie, "actor_studio_preview rest_orientation return");

    vastp4DMM.arid = pactr->Arid();
    vastp4DMM.ipartSelected = ivNil;
    vastp4DMM.flYaw = 0.0f;
    vastp4DMM.flPitch = 0.0f;
    vastp4DMM.flZoom = 1.0f;
    vastp4DMM.flPanY = 0.0f;

    int32_t anid = pactr->Arid() == aridNil ? 0 : pactr->AnidCur();
    if (anid < 0 || anid >= vastp4DMM.ptmpl->Cactn())
        anid = vastp4DMM.ptmpl->Cactn() > 0 ? 0 : ivNil;
    int32_t celn = pactr->Arid() == aridNil ? 0 : pactr->CelnCur();
    if (anid != ivNil)
    {
        MVIE::MultiLog(pmvie,
            "actor_studio_preview initial_pose begin action=%ld cel=%ld",
            (long)anid, (long)celn);
        if (!FSet4DMMActorStudioActionCel(anid, celn, fTrue))
        {
            Reset4DMMActorStudioPreview();
            return fFalse;
        }
    }
    else
    {
        vastp4DMM.anid = ivNil;
        vastp4DMM.celn = ivNil;
        Compute4DMMActorStudioFraming();
        Render4DMMActorStudioPreview();
    }

    MVIE::MultiLog(pmvie,
        "actor_studio_open arid=%ld type=%s actions=%ld parts=%ld sets=%ld preview=%ldx%ld",
        (long)pactr->Arid(), Psz4DMMActorStudioType(pactr),
        (long)vastp4DMM.ptmpl->Cactn(), (long)vastp4DMM.pbody->Cpart(),
        (long)vastp4DMM.pbody->Cbset(),
        (long)vastp4DMM.dxpPreview, (long)vastp4DMM.dypPreview);
    return fTrue;
}

static void Update4DMMActorStudioInfo(PMVIE pmvie)
{
    if (vhwnd4DMMActorStudio == hNil || !IsWindow(vhwnd4DMMActorStudio))
        return;
    HWND hwndInfo = GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioInfo);
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pactr == pvNil || vastp4DMM.pbody == pvNil || vastp4DMM.ptmpl == pvNil)
    {
        SetWindowTextA(hwndInfo, "Actor Studio target is unavailable.");
        return;
    }

    STN stnObject;
    STN stnTemplate;
    STN stnAction;
    pactr->GetName(&stnObject);
    vastp4DMM.ptmpl->GetName(&stnTemplate);
    achar szAction[192];
    if (vastp4DMM.anid >= 0 && vastp4DMM.ptmpl->FGetActnName(vastp4DMM.anid, &stnAction))
        sprintf_s(szAction, SIZEOF(szAction), "%s", stnAction.Psz());
    else
        sprintf_s(szAction, SIZEOF(szAction), "<none>");

    achar szPart[64] = "None";
    achar szParent[64] = "-";
    int32_t ibset = ivNil;
    if (vastp4DMM.ipartSelected != ivNil)
    {
        if (vastp4DMM.kindSelected == kastnActorPropRoot)
        {
            sprintf_s(szPart, SIZEOF(szPart), "Whole Actor / Prop");
            sprintf_s(szParent, SIZEOF(szParent), "Root");
        }
        else
        {
            if (vastp4DMM.kindSelected == kastnDefaultPartGroup)
                Format4DMMActorStudioPartGroupName(vastp4DMM.ipartSelected, szPart, SIZEOF(szPart));
            else
                Format4DMMActorStudioPartName(vastp4DMM.ipartSelected, szPart, SIZEOF(szPart));
            const int32_t ipartParent = vastp4DMM.pbody->IpartParent(vastp4DMM.ipartSelected);
            if (ipartParent == ivNil)
                sprintf_s(szParent, SIZEOF(szParent), "Root");
            else
                Format4DMMActorStudioPartName(ipartParent, szParent, SIZEOF(szParent));
            ibset = vastp4DMM.pbody->IbsetOfPart(vastp4DMM.ipartSelected);
        }
    }

    int32_t cEditablePart = 0;
    int32_t cDefaultGroup = 0;
    int32_t cHiddenAnchor = 0;
    for (size_t i = 0; i < vrg4DMMActorStudioDefaultPartInfo.size(); ++i)
    {
        const ACTORSTUDIODEFAULTPARTINFO &info = vrg4DMMActorStudioDefaultPartInfo[i];
        if (info.fPart)
            ++cEditablePart;
        if (info.fGroup)
            ++cDefaultGroup;
        if (!info.fPart && !info.fGroup)
            ++cHiddenAnchor;
    }

    achar szInfo[1024];
    sprintf_s(szInfo, SIZEOF(szInfo),
        "Target: %s (%s)\r\n"
        "Template: %s   Object ID: %d\r\n"
        "Action: %s   Frame: %d\r\n"
        "Editable parts: %d   Default part groups: %d   Hidden anchors: %d\r\n"
        "Selected part: %s   Parent: %s   Body set: %d\r\n"
        "Viewport: tool + Left drag edits/picks | Alt+Left drag forces selected part | Ctrl+Left drag orbits | Wheel zoom | Middle drag pans",
        stnObject.Psz(), Psz4DMMActorStudioType(pactr), stnTemplate.Psz(),
        (int)pactr->Arid(), szAction, (int)(vastp4DMM.celn + 1),
        (int)cEditablePart, (int)cDefaultGroup, (int)cHiddenAnchor,
        szPart, szParent, (int)ibset);
    SetWindowTextA(hwndInfo, szInfo);
}

static int32_t I4DMMActorStudioTargetCustomObject(PMVIE pmvie)
{
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil)
        return ivNil;

    TAG tag;
    pactr->GetTagTmpl(&tag);
    for (int32_t i = 0; i < pmvie->C4DMMCustomObjects(); ++i)
    {
        const CUSTOMOBJECT *pobj = pmvie->P4DMMCustomObject(i);
        if (pobj == pvNil || pobj->cnoOwnedTmpl == cnoNil)
            continue;
        if (tag.sid == ksidUseCrf && tag.ctg == kctgTmpl && tag.cno == pobj->cnoOwnedTmpl)
            return i;
    }
    return ivNil;
}

static int32_t C4DMMActorStudioDistinctImports(PMVIE pmvie, int32_t idObject)
{
    if (pmvie == pvNil || idObject <= 0)
        return 0;
    std::vector<int32_t> rgidImport;
    for (int32_t i = 0; i < pmvie->C4DMMCustomParts(); ++i)
    {
        const CUSTOMPART *pmeta = pmvie->P4DMMCustomPart(i);
        if (pmeta == pvNil || pmeta->idObject != idObject || pmeta->idImport <= 0 ||
            pmeta->cpart <= 0)
            continue;
        if (std::find(rgidImport.begin(), rgidImport.end(), pmeta->idImport) == rgidImport.end())
            rgidImport.push_back(pmeta->idImport);
    }
    return (int32_t)rgidImport.size();
}

static void Get4DMMActorStudioWholeObjectName(PMVIE pmvie, const CUSTOMOBJECT *pobj,
                                               achar *pszName, size_t cbName)
{
    if (pszName == pvNil || cbName == 0)
        return;
    pszName[0] = 0;
    if (pobj != pvNil && pobj->szName[0] != 0)
    {
        sprintf_s(pszName, cbName, "%s", pobj->szName);
        return;
    }

    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pactr != pvNil)
    {
        STN stn;
        pactr->GetName(&stn);
        if (stn.Psz() != pvNil && stn.Psz()[0] != 0)
        {
            sprintf_s(pszName, cbName, "%s", stn.Psz());
            return;
        }
    }
    sprintf_s(pszName, cbName, "Actor / Prop");
}

static int32_t Id4DMMActorStudioHandmadeObject(PMVIE pmvie)
{
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil)
        return 0;
    TAG tag;
    pactr->GetTagTmpl(&tag);
    for (int32_t i = 0; i < pmvie->C4DMMCustomObjects(); ++i)
    {
        const CUSTOMOBJECT *pobj = pmvie->P4DMMCustomObject(i);
        if (pobj != pvNil && pobj->cnoOwnedTmpl != cnoNil &&
            tag.sid == ksidUseCrf && tag.ctg == kctgTmpl && tag.cno == pobj->cnoOwnedTmpl &&
            pmvie->F4DMMCustomObjectIsHandmade(i))
            return pobj->id;
    }
    return 0;
}

static const CUSTOMPART *P4DMMActorStudioSelectedImportedObjectMeta(PMVIE pmvie)
{
    if (pmvie == pvNil || vastp4DMM.kindSelected != kastnObject ||
        vastp4DMM.cpartSelection <= 0 || vastp4DMM.ipartSelectionFirst < 0)
        return pvNil;

    const int32_t idObject = Id4DMMActorStudioHandmadeObject(pmvie);
    if (idObject <= 0)
        return pvNil;

    for (int32_t i = 0; i < pmvie->C4DMMCustomParts(); ++i)
    {
        const CUSTOMPART *pmeta = pmvie->P4DMMCustomPart(i);
        if (pmeta == pvNil || pmeta->idObject != idObject)
            continue;
        if (pmeta->ipartFirst == vastp4DMM.ipartSelectionFirst &&
            pmeta->cpart == vastp4DMM.cpartSelection)
            return pmeta;
    }
    return pvNil;
}

static bool F4DMMActorStudioSelectedObjectIsImportedTdt(PMVIE pmvie)
{
    const CUSTOMPART *pmeta = P4DMMActorStudioSelectedImportedObjectMeta(pmvie);
    return pmeta != pvNil && pmeta->fSourceTdt;
}

static bool F4DMMActorStudioSelectedObjectIsImportedProp(PMVIE pmvie)
{
    const CUSTOMPART *pmeta = P4DMMActorStudioSelectedImportedObjectMeta(pmvie);
    if (pmeta == pvNil || pmeta->fSourceTdt || pmvie == pvNil || pmvie->Pscen() == pvNil ||
        pmeta->iscen != pmvie->Iscen())
        return fFalse;
    PACTR pactrSource = pmvie->Pscen()->PactrFromArid(pmeta->aridSource);
    return pactrSource != pvNil && pactrSource->Ptmpl() != pvNil &&
        pactrSource->Ptmpl()->FIsProp();
}

static bool F4DMMActorStudioSelectedObjectIsImportedActor(PMVIE pmvie)
{
    const CUSTOMPART *pmeta = P4DMMActorStudioSelectedImportedObjectMeta(pmvie);
    if (pmeta == pvNil || pmeta->fSourceTdt || pmvie == pvNil || pmvie->Pscen() == pvNil ||
        pmeta->iscen != pmvie->Iscen())
        return fFalse;
    PACTR pactrSource = pmvie->Pscen()->PactrFromArid(pmeta->aridSource);
    return pactrSource != pvNil && pactrSource->Ptmpl() != pvNil &&
        !pactrSource->Ptmpl()->FIsProp() && !pactrSource->Ptmpl()->FIsTdt();
}

static bool F4DMMActorStudioSelectedObjectCanTemporalCutSpawn(PMVIE pmvie)
{
    return F4DMMActorStudioSelectedObjectIsImportedTdt(pmvie) ||
        F4DMMActorStudioSelectedObjectIsImportedProp(pmvie) ||
        F4DMMActorStudioSelectedObjectIsImportedActor(pmvie);
}

static const ACTORSTUDIOTREEROW *P4DMMActorStudioSelectedTreeRow(void)
{
    for (int32_t i = 0; i < (int32_t)vrg4DMMActorStudioPartRows.size(); ++i)
    {
        const ACTORSTUDIOTREEROW &row = vrg4DMMActorStudioPartRows[i];
        if (row.kind == vastp4DMM.kindSelected &&
            row.ipartFirst == vastp4DMM.ipartSelectionFirst &&
            row.cpart == vastp4DMM.cpartSelection)
            return &row;
    }
    return pvNil;
}

static bool F4DMMActorStudioSelectedObjectGroupCanTemporalCutSpawn(PMVIE pmvie)
{
    if (pmvie == pvNil || vastp4DMM.kindSelected != kastnGroup)
        return fFalse;
    const ACTORSTUDIOTREEROW *prow = P4DMMActorStudioSelectedTreeRow();
    return prow != pvNil && prow->idObject > 0 && prow->idImport > 0 &&
        prow->idGroup > 0 && prow->cpart > 0;
}

static void Get4DMMActorStudioSelectedObjectGroupParts(
    PMVIE pmvie, std::vector<int32_t> *prgipart)
{
    if (prgipart == pvNil)
        return;
    prgipart->clear();
    if (pmvie == pvNil || vastp4DMM.pbody == pvNil)
        return;
    const ACTORSTUDIOTREEROW *prow = P4DMMActorStudioSelectedTreeRow();
    if (prow == pvNil || prow->kind != kastnGroup || prow->idObject <= 0 ||
        prow->idImport <= 0 || prow->idGroup <= 0)
        return;

    for (int32_t i = 0; i < pmvie->C4DMMCustomParts(); ++i)
    {
        const CUSTOMPART *pmeta = pmvie->P4DMMCustomPart(i);
        if (pmeta == pvNil || pmeta->idObject != prow->idObject ||
            pmeta->idImport != prow->idImport || pmeta->idGroup != prow->idGroup ||
            pmeta->cpart <= 0 || pmeta->ipartFirst < 0)
            continue;
        const int32_t ipartLim = LwMin(
            pmeta->ipartFirst + pmeta->cpart, vastp4DMM.pbody->Cpart());
        for (int32_t ipart = pmeta->ipartFirst; ipart < ipartLim; ++ipart)
            if (std::find(prgipart->begin(), prgipart->end(), ipart) == prgipart->end())
                prgipart->push_back(ipart);
    }
}

static bool F4DMMActorStudioSelectedWholeRootCanTemporalCutSpawn(PMVIE pmvie)
{
    if (pmvie == pvNil || vastp4DMM.pbody == pvNil ||
        vastp4DMM.kindSelected != kastnActorPropRoot)
        return fFalse;
    const ACTORSTUDIOTREEROW *prow = P4DMMActorStudioSelectedTreeRow();
    return prow != pvNil && prow->kind == kastnActorPropRoot &&
        prow->ipartFirst == 0 && prow->cpart == vastp4DMM.pbody->Cpart() &&
        prow->cpart > 0;
}

static void Get4DMMActorStudioSelectedWholeRootParts(
    PMVIE pmvie, std::vector<int32_t> *prgipart)
{
    if (prgipart == pvNil)
        return;
    prgipart->clear();
    if (!F4DMMActorStudioSelectedWholeRootCanTemporalCutSpawn(pmvie))
        return;

    // EVERYTHING is the complete editable BODY owned by the Actor Studio
    // target. This deliberately includes hierarchy-only BODY entries: the
    // temporal presence writer ignores chidNil while preserving every authored
    // model in a customized default Actor/Prop or a multi-Object-Group object.
    for (int32_t ipart = 0; ipart < vastp4DMM.pbody->Cpart(); ++ipart)
        prgipart->push_back(ipart);
}

static void Get4DMMActorStudioSelectedImportedObjectParts(
    PMVIE pmvie, std::vector<int32_t> *prgipart)
{
    if (prgipart == pvNil)
        return;
    prgipart->clear();
    const CUSTOMPART *pmeta = P4DMMActorStudioSelectedImportedObjectMeta(pmvie);
    if (pmeta == pvNil || vastp4DMM.pbody == pvNil || pmeta->cpart <= 0 ||
        pmeta->ipartFirst < 0 || pmeta->ipartFirst + pmeta->cpart > vastp4DMM.pbody->Cpart())
        return;
    for (int32_t ipart = pmeta->ipartFirst; ipart < pmeta->ipartFirst + pmeta->cpart; ++ipart)
        prgipart->push_back(ipart);
}

static void Get4DMMActorStudioDefaultPartGroupMembersInRange(
    int32_t ipartGroup, int32_t ipartFirst, int32_t cpart,
    std::vector<int32_t> *prgipart)
{
    if (prgipart == pvNil)
        return;
    prgipart->clear();
    if (vastp4DMM.pbody == pvNil || cpart <= 0 ||
        !FIn(ipartGroup, ipartFirst, ipartFirst + cpart) ||
        !F4DMMActorStudioDefaultPartIsGroup(ipartGroup))
        return;

    const int32_t ipartLim = LwMin(ipartFirst + cpart, vastp4DMM.pbody->Cpart());
    if (F4DMMActorStudioDefaultPartIsPart(ipartGroup))
        prgipart->push_back(ipartGroup);
    for (int32_t ipart = ipartFirst; ipart < ipartLim; ++ipart)
    {
        if (ipart == ipartGroup || !F4DMMActorStudioDefaultPartIsPart(ipart))
            continue;
        if (F4DMMActorStudioPartDescendsFrom(ipart, ipartGroup))
            prgipart->push_back(ipart);
    }
}

static bool F4DMMActorStudioSelectionPresentAtCurrentFrame(void)
{
    if (vastp4DMM.pbody == pvNil)
        return fFalse;

    // This organizer row has no editable geometry.  It continues to exist
    // across cels and cannot become a stale destructive selection.
    if (vastp4DMM.kindSelected == kastnNonGroupedParts &&
        vastp4DMM.cpartSelection == 0)
        return fTrue;

    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    std::vector<int32_t> rgipart;
    switch (vastp4DMM.kindSelected)
    {
    case kastnPart:
        if (FIn(vastp4DMM.ipartSelectionFirst, 0, vastp4DMM.pbody->Cpart()))
            rgipart.push_back(vastp4DMM.ipartSelectionFirst);
        break;

    case kastnDefaultPartGroup:
        // PG rows store their hierarchy anchor as a one-Part selection, so
        // test the group's actual descendant geometry rather than the anchor.
        Get4DMMActorStudioDefaultPartGroupMembersInRange(
            vastp4DMM.ipartSelectionFirst, 0, vastp4DMM.pbody->Cpart(), &rgipart);
        break;

    case kastnObject:
        Get4DMMActorStudioSelectedImportedObjectParts(pmvie, &rgipart);
        break;

    case kastnGroup:
        Get4DMMActorStudioSelectedObjectGroupParts(pmvie, &rgipart);
        break;

    case kastnActorPropRoot:
        Get4DMMActorStudioSelectedWholeRootParts(pmvie, &rgipart);
        break;

    default:
        if (vastp4DMM.ipartSelectionFirst >= 0 && vastp4DMM.cpartSelection > 0)
        {
            const int32_t ipartLim = LwMin(
                vastp4DMM.ipartSelectionFirst + vastp4DMM.cpartSelection,
                vastp4DMM.pbody->Cpart());
            for (int32_t ipart = vastp4DMM.ipartSelectionFirst;
                 ipart < ipartLim; ++ipart)
                rgipart.push_back(ipart);
        }
        break;
    }

    for (size_t i = 0; i < rgipart.size(); ++i)
    {
        const int32_t ipart = rgipart[i];
        if (FIn(ipart, 0, vastp4DMM.pbody->Cpart()) &&
            vastp4DMM.pbody->FPartHasModel(ipart))
            return fTrue;
    }
    return fFalse;
}

static void Get4DMMActorStudioDefaultPartGroupsInRange(
    int32_t ipartFirst, int32_t cpart, std::vector<int32_t> *prgipartGroup)
{
    if (prgipartGroup == pvNil)
        return;
    prgipartGroup->clear();
    if (vastp4DMM.pbody == pvNil || cpart <= 0 ||
        !FIn(ipartFirst, 0, vastp4DMM.pbody->Cpart()))
        return;

    const int32_t ipartLim = LwMin(ipartFirst + cpart, vastp4DMM.pbody->Cpart());
    std::vector<std::vector<int32_t> > rgMembersSeen;
    for (int32_t ipartGroup = ipartFirst; ipartGroup < ipartLim; ++ipartGroup)
    {
        if (!F4DMMActorStudioDefaultPartIsGroup(ipartGroup))
            continue;
        std::vector<int32_t> rgMembers;
        Get4DMMActorStudioDefaultPartGroupMembersInRange(
            ipartGroup, ipartFirst, cpart, &rgMembers);
        if (rgMembers.size() < 2)
            continue;
        if (std::find(rgMembersSeen.begin(), rgMembersSeen.end(), rgMembers) != rgMembersSeen.end())
            continue;
        rgMembersSeen.push_back(rgMembers);
        prgipartGroup->push_back(ipartGroup);
    }
}

static int32_t C4DMMActorStudioPhysicalPartsInRange(int32_t ipartFirst, int32_t cpart)
{
    if (vastp4DMM.pbody == pvNil || cpart <= 0)
        return 0;
    const int32_t ipartLim = LwMin(ipartFirst + cpart, vastp4DMM.pbody->Cpart());
    int32_t cPhysical = 0;
    const int32_t ipartStart = ipartFirst < 0 ? 0 : ipartFirst;
    for (int32_t ipart = ipartStart; ipart < ipartLim; ++ipart)
        if (F4DMMActorStudioDefaultPartIsPart(ipart))
            ++cPhysical;
    return cPhysical;
}

static bool F4DMMActorStudioDefaultPartGroupsUsefulInRange(
    int32_t ipartFirst, int32_t cpart, int32_t *pcgroup, int32_t *pcpart,
    int32_t *pcSoleCoverage)
{
    std::vector<int32_t> rgGroup;
    Get4DMMActorStudioDefaultPartGroupsInRange(ipartFirst, cpart, &rgGroup);
    const int32_t cPhysical = C4DMMActorStudioPhysicalPartsInRange(ipartFirst, cpart);
    int32_t cSoleCoverage = 0;
    if (rgGroup.size() == 1)
    {
        std::vector<int32_t> rgMembers;
        Get4DMMActorStudioDefaultPartGroupMembersInRange(
            rgGroup[0], ipartFirst, cpart, &rgMembers);
        cSoleCoverage = (int32_t)rgMembers.size();
    }
    if (pcgroup != pvNil)
        *pcgroup = (int32_t)rgGroup.size();
    if (pcpart != pvNil)
        *pcpart = cPhysical;
    if (pcSoleCoverage != pvNil)
        *pcSoleCoverage = cSoleCoverage;

    // A single PG is useful when it organizes only a subset of the object's
    // physical Parts. If it covers every Part, showing that one wrapper adds no
    // information and the checkbox stays disabled. Two or more distinct PGs
    // are always useful. Exact duplicate PG membership sets are suppressed.
    return rgGroup.size() >= 2 ||
           (rgGroup.size() == 1 && cSoleCoverage > 0 && cSoleCoverage < cPhysical);
}

static void Update4DMMActorStudioDefaultPartGroupAvailability(PMVIE pmvie,
                                                                int32_t idObject)
{
    vf4DMMActorStudioHasDefaultPartGroups = fFalse;
    if (vastp4DMM.pbody == pvNil)
        return;

    const bool fTdtTarget = vastp4DMM.ptmpl != pvNil && vastp4DMM.ptmpl->FIsTdt();
    int32_t cgroupBest = 0;
    int32_t crange = 0;
    bool fAnyUseful = fFalse;

    if (!fTdtTarget && idObject > 0 && pmvie != pvNil)
    {
        for (int32_t imeta = 0; imeta < pmvie->C4DMMCustomParts(); ++imeta)
        {
            const CUSTOMPART *pmeta = pmvie->P4DMMCustomPart(imeta);
            if (pmeta == pvNil || pmeta->idObject != idObject || pmeta->fSourceTdt ||
                pmeta->cpart <= 0 || pmeta->ipartFirst < 0 ||
                pmeta->ipartFirst + pmeta->cpart > vastp4DMM.pbody->Cpart())
                continue;

            ++crange;
            int32_t cgroupRange = 0;
            int32_t cpartRange = 0;
            int32_t cSoleCoverage = 0;
            const bool fUseful = F4DMMActorStudioDefaultPartGroupsUsefulInRange(
                pmeta->ipartFirst, pmeta->cpart, &cgroupRange, &cpartRange, &cSoleCoverage);
            if (cgroupRange > cgroupBest)
                cgroupBest = cgroupRange;
            if (fUseful)
                fAnyUseful = fTrue;
            MVIE::MultiLog(pmvie,
                "actor_studio_default_group_range object=%ld import=%ld arid=%ld first=%ld raw_parts=%ld physical_parts=%ld groups=%ld sole_coverage=%ld useful=%d source_tdt=%d",
                (long)idObject, (long)pmeta->idImport, (long)pmeta->aridSource,
                (long)pmeta->ipartFirst, (long)pmeta->cpart, (long)cpartRange,
                (long)cgroupRange, (long)cSoleCoverage, (int)fUseful,
                (int)pmeta->fSourceTdt);
        }
    }
    else if (!fTdtTarget && idObject <= 0)
    {
        crange = 1;
        int32_t cpartRange = 0;
        int32_t cSoleCoverage = 0;
        fAnyUseful = F4DMMActorStudioDefaultPartGroupsUsefulInRange(
            0, vastp4DMM.pbody->Cpart(), &cgroupBest, &cpartRange, &cSoleCoverage);
        MVIE::MultiLog(pmvie,
            "actor_studio_default_group_range object=stock first=0 raw_parts=%ld physical_parts=%ld groups=%ld sole_coverage=%ld useful=%d",
            (long)vastp4DMM.pbody->Cpart(), (long)cpartRange, (long)cgroupBest,
            (long)cSoleCoverage, (int)fAnyUseful);
    }

    // Older handmade metadata may not carry source ranges. Fall back to the
    // synthesized BODY only when no usable provenance exists at all.
    if (!fTdtTarget && idObject > 0 && crange == 0)
    {
        int32_t cpartRange = 0;
        int32_t cSoleCoverage = 0;
        fAnyUseful = F4DMMActorStudioDefaultPartGroupsUsefulInRange(
            0, vastp4DMM.pbody->Cpart(), &cgroupBest, &cpartRange, &cSoleCoverage);
        crange = 1;
        MVIE::MultiLog(pmvie,
            "actor_studio_default_group_range object=%ld fallback_whole_body=1 physical_parts=%ld groups=%ld sole_coverage=%ld useful=%d",
            (long)idObject, (long)cpartRange, (long)cgroupBest,
            (long)cSoleCoverage, (int)fAnyUseful);
    }

    vf4DMMActorStudioHasDefaultPartGroups = !fTdtTarget && fAnyUseful;
    if (!vf4DMMActorStudioHasDefaultPartGroups)
        vf4DMMActorStudioSortDefaultPartGroups = fFalse;

    MVIE::MultiLog(pmvie,
        "actor_studio_default_group_availability object=%ld handmade=%d ranges=%ld best_groups=%ld enabled=%d tdt=%d",
        (long)idObject, idObject > 0 ? 1 : 0, (long)crange, (long)cgroupBest,
        (int)vf4DMMActorStudioHasDefaultPartGroups, (int)fTdtTarget);
}

static uint64_t Lu4DMMActorStudioTreeKey(int32_t kind, int32_t idImport,
                                          int32_t idGroup, int32_t aridSource,
                                          int32_t ipartFirst)
{
    uint64_t h = 1469598103934665603ULL;
    const uint32_t rg[5] = {(uint32_t)kind, (uint32_t)idImport, (uint32_t)idGroup,
                            (uint32_t)aridSource, (uint32_t)ipartFirst};
    for (int32_t i = 0; i < 5; ++i)
    {
        h ^= rg[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static bool F4DMMActorStudioTreeExpanded(uint64_t luKey)
{
    return std::find(vrglu4DMMActorStudioExpanded.begin(),
                     vrglu4DMMActorStudioExpanded.end(), luKey) !=
           vrglu4DMMActorStudioExpanded.end();
}

static void Set4DMMActorStudioTreeExpanded(uint64_t luKey, bool fExpanded)
{
    std::vector<uint64_t>::iterator it = std::find(vrglu4DMMActorStudioExpanded.begin(),
                                                    vrglu4DMMActorStudioExpanded.end(), luKey);
    if (fExpanded)
    {
        if (it == vrglu4DMMActorStudioExpanded.end())
            vrglu4DMMActorStudioExpanded.push_back(luKey);
    }
    else if (it != vrglu4DMMActorStudioExpanded.end())
        vrglu4DMMActorStudioExpanded.erase(it);
}

static bool F4DMMActorStudioSelectedDefaultPartGroupExpanded(void)
{
    if (vastp4DMM.kindSelected != kastnDefaultPartGroup)
        return fFalse;
    const ACTORSTUDIOTREEROW *prow = P4DMMActorStudioSelectedTreeRow();
    if (prow == pvNil || prow->kind != kastnDefaultPartGroup)
        return fFalse;
    return F4DMMActorStudioTreeExpanded(Lu4DMMActorStudioTreeKey(
        prow->kind, prow->idImport, prow->idGroup, prow->aridSource, prow->ipartFirst));
}

static int32_t Add4DMMActorStudioTreeRow(HWND hwndParts, const ACTORSTUDIOTREEROW &row)
{
    const int32_t irowData = (int32_t)vrg4DMMActorStudioPartRows.size();
    vrg4DMMActorStudioPartRows.push_back(row);
    const LRESULT irow = SendMessageA(hwndParts, LB_ADDSTRING, 0, (LPARAM)row.szName);
    if (irow != LB_ERR && irow != LB_ERRSPACE)
        SendMessageA(hwndParts, LB_SETITEMDATA, (WPARAM)irow, (LPARAM)irowData);
    return irowData;
}

static void Add4DMMActorStudioPhysicalPartRow(HWND hwndParts, int32_t idObject,
                                                int32_t idImport, int32_t idGroup,
                                                int32_t aridSource, int32_t ipart,
                                                int32_t cchIndent)
{
    if (!F4DMMActorStudioDefaultPartIsPart(ipart))
        return;
    ACTORSTUDIOTREEROW row;
    ClearPb(&row, SIZEOF(row));
    row.kind = kastnPart;
    row.idObject = idObject;
    row.idImport = idImport;
    row.idGroup = idGroup;
    row.aridSource = aridSource;
    row.ipartFirst = ipart;
    row.cpart = 1;
    achar szPart[64];
    Format4DMMActorStudioPartName(ipart, szPart, SIZEOF(szPart));
    sprintf_s(row.szName, SIZEOF(row.szName), "%*s%s", (int)cchIndent, "", szPart);
    Add4DMMActorStudioTreeRow(hwndParts, row);
}

static void Add4DMMActorStudioPartTreeRows(HWND hwndParts, int32_t idObject,
                                            int32_t idImport, int32_t idGroup,
                                            int32_t aridSource, int32_t ipartFirst,
                                            int32_t cpart, int32_t cchIndent)
{
    if (vastp4DMM.pbody == pvNil || cpart <= 0)
        return;
    const int32_t ipartLim = LwMin(ipartFirst + cpart, vastp4DMM.pbody->Cpart());
    int32_t cDefaultGroupsInRange = 0;
    int32_t cPhysicalPartsInRange = 0;
    int32_t cSoleCoverage = 0;
    const bool fUsefulRange = F4DMMActorStudioDefaultPartGroupsUsefulInRange(
        ipartFirst, cpart, &cDefaultGroupsInRange, &cPhysicalPartsInRange,
        &cSoleCoverage);
    if (!vf4DMMActorStudioSortDefaultPartGroups ||
        !vf4DMMActorStudioHasDefaultPartGroups || !fUsefulRange)
    {
        // Flat view is deliberately geometry-complete. A BODY node that owns
        // geometry and also acts as a PG still appears here as its own physical
        // Part, so no visible geometry becomes unreachable when PG view is off.
        for (int32_t ipart = ipartFirst; ipart < ipartLim; ++ipart)
            Add4DMMActorStudioPhysicalPartRow(hwndParts, idObject, idImport, idGroup,
                                               aridSource, ipart, cchIndent);
        return;
    }

    std::vector<int32_t> rgGroup;
    Get4DMMActorStudioDefaultPartGroupsInRange(ipartFirst, cpart, &rgGroup);
    std::vector<uint8_t> rgfGrouped((size_t)cpart, 0);
    int32_t cgroupShown = 0;
    for (size_t igroup = 0; igroup < rgGroup.size(); ++igroup)
    {
        const int32_t ipartGroup = rgGroup[igroup];
        std::vector<int32_t> rgMembers;
        Get4DMMActorStudioDefaultPartGroupMembersInRange(
            ipartGroup, ipartFirst, cpart, &rgMembers);
        if (rgMembers.size() < 2)
            continue;

        ++cgroupShown;
        ACTORSTUDIOTREEROW rowGroup;
        ClearPb(&rowGroup, SIZEOF(rowGroup));
        rowGroup.kind = kastnDefaultPartGroup;
        rowGroup.idObject = idObject;
        rowGroup.idImport = idImport;
        rowGroup.idGroup = idGroup;
        rowGroup.aridSource = aridSource;
        rowGroup.ipartFirst = ipartGroup;
        rowGroup.cpart = 1;
        const uint64_t luGroup = Lu4DMMActorStudioTreeKey(kastnDefaultPartGroup,
            idImport, idGroup, aridSource, ipartGroup);
        const bool fGroupOpen = F4DMMActorStudioTreeExpanded(luGroup);
        achar szGroup[64];
        // PG numbering restarts inside every Actor/Prop range. It is separate
        // from Pt. A..Z/27+ numbering, matching the intended hierarchy.
        sprintf_s(szGroup, SIZEOF(szGroup), "Pt Group %d", (int)cgroupShown);
        sprintf_s(rowGroup.szName, SIZEOF(rowGroup.szName), "%*s%c %s",
                  (int)cchIndent, "", fGroupOpen ? '-' : '+', szGroup);
        Add4DMMActorStudioTreeRow(hwndParts, rowGroup);

        for (size_t imember = 0; imember < rgMembers.size(); ++imember)
        {
            const int32_t ipart = rgMembers[imember];
            if (FIn(ipart, ipartFirst, ipartLim))
                rgfGrouped[(size_t)(ipart - ipartFirst)] = 1;
            if (fGroupOpen)
                Add4DMMActorStudioPhysicalPartRow(hwndParts, idObject, idImport, idGroup,
                                                   aridSource, ipart, cchIndent + 4);
        }
    }

    int32_t cNonGrouped = 0;
    for (int32_t ipart = ipartFirst; ipart < ipartLim; ++ipart)
    {
        if (F4DMMActorStudioDefaultPartIsPart(ipart) &&
            !rgfGrouped[(size_t)(ipart - ipartFirst)])
            ++cNonGrouped;
    }

    if (cgroupShown == 0)
    {
        for (int32_t ipart = ipartFirst; ipart < ipartLim; ++ipart)
            Add4DMMActorStudioPhysicalPartRow(hwndParts, idObject, idImport, idGroup,
                                               aridSource, ipart, cchIndent);
        return;
    }

    if (cNonGrouped > 0)
    {
        ACTORSTUDIOTREEROW rowLoose;
        ClearPb(&rowLoose, SIZEOF(rowLoose));
        rowLoose.kind = kastnNonGroupedParts;
        rowLoose.idObject = idObject;
        rowLoose.idImport = idImport;
        rowLoose.idGroup = idGroup;
        rowLoose.aridSource = aridSource;
        rowLoose.ipartFirst = ipartFirst;
        rowLoose.cpart = 0; // Synthetic organizer; it has no BODY transform of its own.
        const uint64_t luLoose = Lu4DMMActorStudioTreeKey(kastnNonGroupedParts,
            idImport, idGroup, aridSource, ipartFirst);
        const bool fLooseOpen = F4DMMActorStudioTreeExpanded(luLoose);
        sprintf_s(rowLoose.szName, SIZEOF(rowLoose.szName), "%*s%c Non-grouped Parts",
                  (int)cchIndent, "", fLooseOpen ? '-' : '+');
        Add4DMMActorStudioTreeRow(hwndParts, rowLoose);
        if (fLooseOpen)
        {
            for (int32_t ipart = ipartFirst; ipart < ipartLim; ++ipart)
            {
                if (F4DMMActorStudioDefaultPartIsPart(ipart) &&
                    !rgfGrouped[(size_t)(ipart - ipartFirst)])
                    Add4DMMActorStudioPhysicalPartRow(hwndParts, idObject, idImport, idGroup,
                                                       aridSource, ipart, cchIndent + 4);
            }
        }
    }

    MVIE::MultiLog(Pmvie4DMMSettingsCurrent(),
        "actor_studio_default_group_tree first=%ld raw_parts=%ld physical_parts=%ld groups=%ld shown=%ld non_grouped=%ld sole_coverage=%ld",
        (long)ipartFirst, (long)cpart, (long)cPhysicalPartsInRange,
        (long)cDefaultGroupsInRange, (long)cgroupShown, (long)cNonGrouped,
        (long)cSoleCoverage);
}

static int32_t I4DMMActorStudioTreeRowForPart(int32_t ipart)
{
    int32_t iFallback = ivNil;
    for (int32_t i = 0; i < (int32_t)vrg4DMMActorStudioPartRows.size(); ++i)
    {
        const ACTORSTUDIOTREEROW &row = vrg4DMMActorStudioPartRows[i];
        if (!FIn(ipart, row.ipartFirst, row.ipartFirst + row.cpart))
            continue;
        if (row.kind == kastnPart)
            return i;
        if (iFallback == ivNil || row.kind == kastnObject)
            iFallback = i;
    }
    return iFallback;
}

static int32_t I4DMMActorStudioTreeRowForViewportPart(int32_t ipart)
{
    if (vastp4DMM.pbody == pvNil || !FIn(ipart, 0, vastp4DMM.pbody->Cpart()))
        return ivNil;

    // An expanded PG exposes its physical Part rows. If the exact Part is not
    // visible, walk upward to the nearest visible PG row containing the hit.
    for (int32_t i = 0; i < (int32_t)vrg4DMMActorStudioPartRows.size(); ++i)
    {
        const ACTORSTUDIOTREEROW &row = vrg4DMMActorStudioPartRows[i];
        if (row.kind == kastnPart && row.cpart == 1 && row.ipartFirst == ipart)
            return i;
    }
    if (vf4DMMActorStudioSortDefaultPartGroups && vf4DMMActorStudioHasDefaultPartGroups)
    {
        for (int32_t ipartGroup = ipart; ipartGroup != ivNil;
             ipartGroup = vastp4DMM.pbody->IpartParent(ipartGroup))
        {
            for (int32_t i = 0; i < (int32_t)vrg4DMMActorStudioPartRows.size(); ++i)
            {
                const ACTORSTUDIOTREEROW &row = vrg4DMMActorStudioPartRows[i];
                if (row.kind == kastnDefaultPartGroup && row.ipartFirst == ipartGroup)
                    return i;
            }
        }
    }
    return I4DMMActorStudioTreeRowForPart(ipart);
}

static void Prepare4DMMActorStudioGroupedViewForSelectedPart(void)
{
    if (!vf4DMMActorStudioSortDefaultPartGroups ||
        !vf4DMMActorStudioHasDefaultPartGroups || vastp4DMM.pbody == pvNil ||
        vastp4DMM.kindSelected != kastnPart ||
        !FIn(vastp4DMM.ipartSelected, 0, vastp4DMM.pbody->Cpart()))
        return;

    const int32_t ipart = vastp4DMM.ipartSelected;
    ACTORSTUDIOTREEROW rowPart;
    ClearPb(&rowPart, SIZEOF(rowPart));
    bool fHavePartRow = fFalse;

    // Keep the already-visible Actor/Object/OG path open across the rebuild.
    for (int32_t i = 0; i < (int32_t)vrg4DMMActorStudioPartRows.size(); ++i)
    {
        const ACTORSTUDIOTREEROW &row = vrg4DMMActorStudioPartRows[i];
        if (row.kind == kastnPart && row.cpart == 1 && row.ipartFirst == ipart)
        {
            rowPart = row;
            fHavePartRow = fTrue;
        }
        if (row.cpart > 0 && FIn(ipart, row.ipartFirst, row.ipartFirst + row.cpart) &&
            (row.kind == kastnActorPropRoot || row.kind == kastnGroup || row.kind == kastnObject))
            Set4DMMActorStudioTreeExpanded(Lu4DMMActorStudioTreeKey(
                row.kind, row.idImport, row.idGroup, row.aridSource, row.ipartFirst), fTrue);
    }

    // Open the nearest authored PG that contains the selected Part so the same
    // Part row still exists after grouped view is enabled.
    std::vector<int32_t> rgMembers;
    for (int32_t ipartGroup = ipart; ipartGroup != ivNil;
         ipartGroup = vastp4DMM.pbody->IpartParent(ipartGroup))
    {
        if (!F4DMMActorStudioDefaultPartIsGroup(ipartGroup))
            continue;
        Get4DMMActorStudioDefaultPartGroupMembersInRange(
            ipartGroup, 0, vastp4DMM.pbody->Cpart(), &rgMembers);
        if (rgMembers.size() < 2)
            continue;
        Set4DMMActorStudioTreeExpanded(Lu4DMMActorStudioTreeKey(
            kastnDefaultPartGroup, fHavePartRow ? rowPart.idImport : 0,
            fHavePartRow ? rowPart.idGroup : 0,
            fHavePartRow ? rowPart.aridSource : 0, ipartGroup), fTrue);
        MVIE::MultiLog(vpmvie4DMMActorStudio,
            "actor_studio_default_part_sort preserve_part=%ld pg=%ld import=%ld group=%ld source_arid=%ld",
            (long)ipart, (long)ipartGroup,
            fHavePartRow ? (long)rowPart.idImport : 0L,
            fHavePartRow ? (long)rowPart.idGroup : 0L,
            fHavePartRow ? (long)rowPart.aridSource : 0L);
        break;
    }
}

static int32_t I4DMMActorStudioTreeRowForRange(int32_t kind, int32_t ipartFirst, int32_t cpart)
{
    for (int32_t i = 0; i < (int32_t)vrg4DMMActorStudioPartRows.size(); ++i)
    {
        const ACTORSTUDIOTREEROW &row = vrg4DMMActorStudioPartRows[i];
        if (row.kind == kind && row.ipartFirst == ipartFirst && row.cpart == cpart)
            return i;
    }
    return ivNil;
}

static bool FSelect4DMMActorStudioTreeRow(int32_t irowData, bool fSelectList)
{
    if (!FIn(irowData, 0, (int32_t)vrg4DMMActorStudioPartRows.size()) ||
        vastp4DMM.pbody == pvNil)
        return fFalse;
    const ACTORSTUDIOTREEROW &row = vrg4DMMActorStudioPartRows[irowData];
    if (row.kind == kastnNonGroupedParts && row.cpart == 0)
    {
        // Synthetic organizer only. It has no BODY transform of its own and
        // must never leave an older editable part selected behind it.
        vastp4DMM.pbody->ClearPartHilite();
        vastp4DMM.ipartSelected = ivNil;
        vastp4DMM.kindSelected = kastnNonGroupedParts;
        vastp4DMM.ipartSelectionFirst = ivNil;
        vastp4DMM.cpartSelection = 0;
        if (fSelectList && vhwnd4DMMActorStudio != hNil)
        {
            HWND hwndParts = GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioParts);
            const int32_t crow = (int32_t)SendMessageA(hwndParts, LB_GETCOUNT, 0, 0);
            for (int32_t irow = 0; irow < crow; ++irow)
            {
                if ((int32_t)SendMessageA(hwndParts, LB_GETITEMDATA, (WPARAM)irow, 0) == irowData)
                {
                    SendMessageA(hwndParts, LB_SETCURSEL, (WPARAM)irow, 0);
                    break;
                }
            }
        }
        Render4DMMActorStudioPreview();
        Update4DMMActorStudioInfo(vpmvie4DMMActorStudio);
        Update4DMMActorStudioButtons();
        MVIE::MultiLog(vpmvie4DMMActorStudio,
            "actor_studio_tree_select organizer=non_grouped first=%ld",
            (long)row.ipartFirst);
        return fTrue;
    }
    if (row.cpart <= 0 || row.ipartFirst < 0 ||
        row.ipartFirst + row.cpart > vastp4DMM.pbody->Cpart())
        return fFalse;

    vastp4DMM.ipartSelected = row.ipartFirst;
    vastp4DMM.kindSelected = row.kind;
    vastp4DMM.ipartSelectionFirst = row.ipartFirst;
    vastp4DMM.cpartSelection = row.cpart;
    if (fSelectList && vhwnd4DMMActorStudio != hNil)
    {
        HWND hwndParts = GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioParts);
        const int32_t crow = (int32_t)SendMessageA(hwndParts, LB_GETCOUNT, 0, 0);
        for (int32_t irow = 0; irow < crow; ++irow)
        {
            if ((int32_t)SendMessageA(hwndParts, LB_GETITEMDATA, (WPARAM)irow, 0) == irowData)
            {
                SendMessageA(hwndParts, LB_SETCURSEL, (WPARAM)irow, 0);
                break;
            }
        }
    }
    Apply4DMMActorStudioSelectionHilite();
    Render4DMMActorStudioPreview();
    Update4DMMActorStudioInfo(vpmvie4DMMActorStudio);
    Update4DMMActorStudioButtons();
    MVIE::MultiLog(vpmvie4DMMActorStudio,
        "actor_studio_tree_select kind=%ld import=%ld group=%ld source_arid=%ld first=%ld parts=%ld",
        (long)row.kind, (long)row.idImport, (long)row.idGroup, (long)row.aridSource,
        (long)row.ipartFirst, (long)row.cpart);
    return fTrue;
}

static void Fill4DMMActorStudioParts(void)
{
    if (vhwnd4DMMActorStudio == hNil || vastp4DMM.pbody == pvNil)
        return;
    HWND hwndParts = GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioParts);
    const int32_t ipartKeep = vastp4DMM.ipartSelected;
    SendMessageA(hwndParts, LB_RESETCONTENT, 0, 0);
    vrg4DMMActorStudioPartRows.clear();
    Build4DMMActorStudioDefaultPartInfo();

    bool rgfCovered[kc4DMMActorStudioFramePartMax];
    ClearPb(rgfCovered, SIZEOF(rgfCovered));
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    const int32_t idObject = Id4DMMActorStudioHandmadeObject(pmvie);
    Update4DMMActorStudioDefaultPartGroupAvailability(pmvie, idObject);
    std::vector<int32_t> rgidImportSeen;

    const int32_t iCustomTarget = I4DMMActorStudioTargetCustomObject(pmvie);
    const CUSTOMOBJECT *pobjTarget =
        (pmvie != pvNil && FIn(iCustomTarget, 0, pmvie->C4DMMCustomObjects())) ?
        pmvie->P4DMMCustomObject(iCustomTarget) : pvNil;
    const bool fHandmadeWholeRoot = idObject > 0 &&
        C4DMMActorStudioDistinctImports(pmvie, idObject) > 1;
    const bool fCustomizedDefaultWholeRoot =
        pobjTarget != pvNil && pobjTarget->templateKind == kctkDefault &&
        vastp4DMM.pbody->Cpart() > 1;
    const bool fShowWholeRoot = fHandmadeWholeRoot || fCustomizedDefaultWholeRoot;
    bool fWholeRootChildrenVisible = fTrue;
    const int32_t cchWholeRootIndent = fShowWholeRoot ? 4 : 0;

    if (fShowWholeRoot)
    {
        ACTORSTUDIOTREEROW rowRoot;
        ClearPb(&rowRoot, SIZEOF(rowRoot));
        rowRoot.kind = kastnActorPropRoot;
        rowRoot.idObject = idObject > 0 ? idObject :
            (pobjTarget != pvNil ? pobjTarget->id : 0);
        rowRoot.idImport = rowRoot.idObject;
        rowRoot.idGroup = pobjTarget != pvNil ? pobjTarget->templateKind : 0;
        rowRoot.ipartFirst = 0;
        rowRoot.cpart = vastp4DMM.pbody->Cpart();

        const uint64_t luRoot = Lu4DMMActorStudioTreeKey(
            kastnActorPropRoot, rowRoot.idImport, rowRoot.idGroup, 0, 0);
        fWholeRootChildrenVisible = F4DMMActorStudioTreeExpanded(luRoot);
        achar szWholeObject[160];
        Get4DMMActorStudioWholeObjectName(pmvie, pobjTarget,
                                           szWholeObject, SIZEOF(szWholeObject));
        sprintf_s(rowRoot.szName, SIZEOF(rowRoot.szName), "%c %s",
                  fWholeRootChildrenVisible ? '-' : '+', szWholeObject);
        Add4DMMActorStudioTreeRow(hwndParts, rowRoot);

        MVIE::MultiLog(pmvie,
            "actor_studio_whole_root show=1 handmade=%d customized_default=%d object=%ld imports=%ld parts=%ld expanded=%d name=%s",
            (int)fHandmadeWholeRoot, (int)fCustomizedDefaultWholeRoot,
            (long)rowRoot.idObject, (long)C4DMMActorStudioDistinctImports(pmvie, idObject),
            (long)rowRoot.cpart, (int)fWholeRootChildrenVisible, szWholeObject);
    }

    if (fWholeRootChildrenVisible && pmvie != pvNil && idObject > 0)
    {
        for (int32_t imetaFirst = 0; imetaFirst < pmvie->C4DMMCustomParts(); ++imetaFirst)
        {
            const CUSTOMPART *pmetaFirst = pmvie->P4DMMCustomPart(imetaFirst);
            if (pmetaFirst == pvNil || pmetaFirst->idObject != idObject ||
                pmetaFirst->idImport <= 0 || pmetaFirst->cpart <= 0 ||
                pmetaFirst->ipartFirst < 0 ||
                pmetaFirst->ipartFirst + pmetaFirst->cpart > vastp4DMM.pbody->Cpart() ||
                std::find(rgidImportSeen.begin(), rgidImportSeen.end(), pmetaFirst->idImport) !=
                    rgidImportSeen.end())
                continue;
            rgidImportSeen.push_back(pmetaFirst->idImport);

            if (pmetaFirst->idGroup > 0)
            {
                int32_t ipartGroupFirst = pmetaFirst->ipartFirst;
                int32_t ipartGroupLast = pmetaFirst->ipartFirst + pmetaFirst->cpart;
                for (int32_t imeta = 0; imeta < pmvie->C4DMMCustomParts(); ++imeta)
                {
                    const CUSTOMPART *pmeta = pmvie->P4DMMCustomPart(imeta);
                    if (pmeta == pvNil || pmeta->idObject != idObject ||
                        pmeta->idImport != pmetaFirst->idImport || pmeta->idGroup != pmetaFirst->idGroup ||
                        pmeta->cpart <= 0)
                        continue;
                    ipartGroupFirst = LwMin(ipartGroupFirst, pmeta->ipartFirst);
                    ipartGroupLast = LwMax(ipartGroupLast, pmeta->ipartFirst + pmeta->cpart);
                    for (int32_t ip = pmeta->ipartFirst;
                         ip < pmeta->ipartFirst + pmeta->cpart && ip < kc4DMMActorStudioFramePartMax; ++ip)
                        if (ip >= 0)
                            rgfCovered[ip] = fTrue;
                }

                ACTORSTUDIOTREEROW rowGroup;
                ClearPb(&rowGroup, SIZEOF(rowGroup));
                rowGroup.kind = kastnGroup;
                rowGroup.idObject = idObject;
                rowGroup.idImport = pmetaFirst->idImport;
                rowGroup.idGroup = pmetaFirst->idGroup;
                rowGroup.ipartFirst = ipartGroupFirst;
                rowGroup.cpart = ipartGroupLast - ipartGroupFirst;
                const uint64_t luGroup = Lu4DMMActorStudioTreeKey(kastnGroup,
                    rowGroup.idImport, rowGroup.idGroup, 0, rowGroup.ipartFirst);
                const bool fGroupOpen = F4DMMActorStudioTreeExpanded(luGroup);
                sprintf_s(rowGroup.szName, SIZEOF(rowGroup.szName), "%*s%c  %s",
                    (int)cchWholeRootIndent, "", fGroupOpen ? '-' : '+',
                    pmetaFirst->szGroupName[0] != 0 ?
                    pmetaFirst->szGroupName : "Object Group");
                Add4DMMActorStudioTreeRow(hwndParts, rowGroup);

                if (fGroupOpen)
                {
                    for (int32_t imeta = 0; imeta < pmvie->C4DMMCustomParts(); ++imeta)
                    {
                        const CUSTOMPART *pmeta = pmvie->P4DMMCustomPart(imeta);
                        if (pmeta == pvNil || pmeta->idObject != idObject ||
                            pmeta->idImport != pmetaFirst->idImport ||
                            pmeta->idGroup != pmetaFirst->idGroup || pmeta->cpart <= 0)
                            continue;
                        ACTORSTUDIOTREEROW rowObject;
                        ClearPb(&rowObject, SIZEOF(rowObject));
                        rowObject.kind = kastnObject;
                        rowObject.idObject = idObject;
                        rowObject.idImport = pmeta->idImport;
                        rowObject.idGroup = pmeta->idGroup;
                        rowObject.aridSource = pmeta->aridSource;
                        rowObject.ipartFirst = pmeta->ipartFirst;
                        rowObject.cpart = pmeta->cpart;
                        const uint64_t luObject = Lu4DMMActorStudioTreeKey(kastnObject,
                            rowObject.idImport, rowObject.idGroup, rowObject.aridSource,
                            rowObject.ipartFirst);
                        const bool fObjectOpen = F4DMMActorStudioTreeExpanded(luObject);
                        sprintf_s(rowObject.szName, SIZEOF(rowObject.szName), "%*s%c %s",
                            (int)(cchWholeRootIndent + 4), "", fObjectOpen ? '-' : '+',
                            pmeta->szObjectName[0] != 0 ?
                            pmeta->szObjectName : "Object");
                        Add4DMMActorStudioTreeRow(hwndParts, rowObject);
                        if (fObjectOpen)
                            Add4DMMActorStudioPartTreeRows(hwndParts, idObject,
                                pmeta->idImport, pmeta->idGroup, pmeta->aridSource,
                                pmeta->ipartFirst, pmeta->cpart,
                                cchWholeRootIndent + 8);
                    }
                }
            }
            else
            {
                for (int32_t imeta = 0; imeta < pmvie->C4DMMCustomParts(); ++imeta)
                {
                    const CUSTOMPART *pmeta = pmvie->P4DMMCustomPart(imeta);
                    if (pmeta == pvNil || pmeta->idObject != idObject ||
                        pmeta->idImport != pmetaFirst->idImport || pmeta->idGroup != 0 ||
                        pmeta->cpart <= 0)
                        continue;
                    for (int32_t ip = pmeta->ipartFirst;
                         ip < pmeta->ipartFirst + pmeta->cpart && ip < kc4DMMActorStudioFramePartMax; ++ip)
                        if (ip >= 0)
                            rgfCovered[ip] = fTrue;
                    ACTORSTUDIOTREEROW rowObject;
                    ClearPb(&rowObject, SIZEOF(rowObject));
                    rowObject.kind = kastnObject;
                    rowObject.idObject = idObject;
                    rowObject.idImport = pmeta->idImport;
                    rowObject.aridSource = pmeta->aridSource;
                    rowObject.ipartFirst = pmeta->ipartFirst;
                    rowObject.cpart = pmeta->cpart;
                    const uint64_t luObject = Lu4DMMActorStudioTreeKey(kastnObject,
                        rowObject.idImport, 0, rowObject.aridSource, rowObject.ipartFirst);
                    const bool fObjectOpen = F4DMMActorStudioTreeExpanded(luObject);
                    sprintf_s(rowObject.szName, SIZEOF(rowObject.szName), "%*s%c %s",
                        (int)cchWholeRootIndent, "", fObjectOpen ? '-' : '+',
                        pmeta->szObjectName[0] != 0 ?
                        pmeta->szObjectName : "Object");
                    Add4DMMActorStudioTreeRow(hwndParts, rowObject);
                    if (fObjectOpen)
                        Add4DMMActorStudioPartTreeRows(hwndParts, idObject,
                            pmeta->idImport, 0, pmeta->aridSource,
                            pmeta->ipartFirst, pmeta->cpart,
                            cchWholeRootIndent + 4);
                }
            }
        }
    }

    // Stock actors have no custom-import metadata, so their complete native
    // BODY hierarchy is available for the optional default Part Group view.
    // Handmade objects keep their existing Object Group/Object hierarchy and
    // classify/filter the BODY ranges inside each expanded Object above.
    if (fWholeRootChildrenVisible)
    {
        if (idObject <= 0)
        {
            Add4DMMActorStudioPartTreeRows(hwndParts, idObject, 0, 0, 0, 0,
                                           vastp4DMM.pbody->Cpart(),
                                           cchWholeRootIndent);
        }
        else
        {
            // Freshly duplicated parts may not have provenance yet. Keep only
            // real renderable leaves here; structural default PG/anchor nodes
            // belong to the native hierarchy, never to this metadata fallback.
            for (int32_t ipart = 0; ipart < vastp4DMM.pbody->Cpart(); ++ipart)
            {
                if (ipart < kc4DMMActorStudioFramePartMax && rgfCovered[ipart])
                    continue;
                if (!F4DMMActorStudioDefaultPartIsPart(ipart))
                    continue;
                Add4DMMActorStudioPhysicalPartRow(hwndParts, idObject, 0, 0, 0,
                                                   ipart, cchWholeRootIndent);
            }
        }
    }

    int32_t irowSelect = FIn(ipartKeep, 0, vastp4DMM.pbody->Cpart()) ?
                         I4DMMActorStudioTreeRowForPart(ipartKeep) : ivNil;
    if (irowSelect == ivNil && !vrg4DMMActorStudioPartRows.empty())
        irowSelect = 0;
    if (irowSelect != ivNil)
        FSelect4DMMActorStudioTreeRow(irowSelect, fTrue);
    MVIE::MultiLog(pmvie,
        "actor_studio_tree_build object=%ld metadata=%ld rows=%ld parts=%ld selected_part=%ld",
        (long)idObject, pmvie != pvNil ? (long)pmvie->C4DMMCustomParts() : 0L,
        (long)vrg4DMMActorStudioPartRows.size(), (long)vastp4DMM.pbody->Cpart(),
        (long)vastp4DMM.ipartSelected);
}

static bool FToggle4DMMActorStudioTreeRow(int32_t irowData)
{
    if (!FIn(irowData, 0, (int32_t)vrg4DMMActorStudioPartRows.size()))
        return fFalse;
    const ACTORSTUDIOTREEROW row = vrg4DMMActorStudioPartRows[irowData];
    if (row.kind != kastnGroup && row.kind != kastnObject &&
        row.kind != kastnDefaultPartGroup && row.kind != kastnNonGroupedParts &&
        row.kind != kastnActorPropRoot)
        return fFalse;
    const uint64_t luKey = Lu4DMMActorStudioTreeKey(row.kind, row.idImport,
                                                    row.idGroup, row.aridSource,
                                                    row.ipartFirst);
    const bool fExpand = !F4DMMActorStudioTreeExpanded(luKey);
    Set4DMMActorStudioTreeExpanded(luKey, fExpand);
    MVIE::MultiLog(vpmvie4DMMActorStudio,
        "actor_studio_tree_toggle kind=%ld import=%ld group=%ld source_arid=%ld first=%ld parts=%ld expanded=%d",
        (long)row.kind, (long)row.idImport, (long)row.idGroup, (long)row.aridSource,
        (long)row.ipartFirst, (long)row.cpart, (int)fExpand);
    Fill4DMMActorStudioParts();
    const int32_t irowNew = I4DMMActorStudioTreeRowForRange(row.kind, row.ipartFirst, row.cpart);
    if (irowNew != ivNil && row.cpart > 0)
        FSelect4DMMActorStudioTreeRow(irowNew, fTrue);
    return fTrue;
}

static void Fill4DMMActorStudioFrames(void)
{
    if (vhwnd4DMMActorStudio == hNil || vastp4DMM.ptmpl == pvNil)
        return;
    HWND hwndFrames = GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioFrames);
    SendMessageA(hwndFrames, LB_RESETCONTENT, 0, 0);
    int32_t ccel = 0;
    if (vastp4DMM.anid < 0 || !vastp4DMM.ptmpl->FGetCcelActn(vastp4DMM.anid, &ccel))
        return;
    for (int32_t celn = 0; celn < ccel; ++celn)
    {
        achar szFrame[64];
        sprintf_s(szFrame, SIZEOF(szFrame), "Frame %d", (int)(celn + 1));
        const LRESULT irow = SendMessageA(hwndFrames, LB_ADDSTRING, 0, (LPARAM)szFrame);
        if (irow != LB_ERR && irow != LB_ERRSPACE)
        {
            SendMessageA(hwndFrames, LB_SETITEMDATA, (WPARAM)irow, (LPARAM)celn);
            if (celn == vastp4DMM.celn)
                SendMessageA(hwndFrames, LB_SETCURSEL, (WPARAM)irow, 0);
        }
    }
}

static void Fill4DMMActorStudioActions(void)
{
    if (vhwnd4DMMActorStudio == hNil || vastp4DMM.ptmpl == pvNil)
        return;
    HWND hwndActions = GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioActions);
    SendMessageA(hwndActions, CB_RESETCONTENT, 0, 0);
    STN stnAction;
    PACTR pactr = Pactr4DMMActorStudioTarget(vpmvie4DMMActorStudio);

    // User-created animations are deliberately surfaced first. The action ID
    // remains the real TMPL child CHID, so frame selection and preview code do
    // not need a parallel numbering system.
    for (int32_t iPass = 0; iPass < 2; ++iPass)
    {
        const bool fWantCustom = iPass == 0;
        for (int32_t anid = 0; anid < vastp4DMM.ptmpl->Cactn(); ++anid)
        {
            const bool fCustom = vpmvie4DMMActorStudio != pvNil && pactr != pvNil &&
                                 vpmvie4DMMActorStudio->FActorStudioActionIsCustom(pactr, anid);
            if (iPass == 0)
            {
                STN stnClassify;
                const bool fHaveClassifyName = vastp4DMM.ptmpl->FGetActnName(anid, &stnClassify);
                MVIE::MultiLog(vpmvie4DMMActorStudio,
                    "actor_studio_action_classify action=%ld custom=%d name=%s",
                    (long)anid, (int)fCustom,
                    fHaveClassifyName ? stnClassify.Psz() : "<unnamed>");
            }
            if (fCustom != fWantCustom)
                continue;
            achar szAction[192];
            if (vastp4DMM.ptmpl->FGetActnName(anid, &stnAction))
            {
                strncpy_s(szAction, SIZEOF(szAction), stnAction.Psz(), _TRUNCATE);
                // v113-v115 accidentally wrote the UI marker into several
                // inherited ACTN names. Keep the actual resource untouched,
                // but normalize the display so only genuinely custom actions
                // receive the Actor Studio marker.
                int32_t cchAction = (int32_t)strlen(szAction);
                while (cchAction >= 2 && szAction[cchAction - 2] == ChLit(' ') &&
                       szAction[cchAction - 1] == ChLit('*'))
                {
                    szAction[cchAction - 2] = 0;
                    cchAction -= 2;
                }
                if (fCustom)
                    strcat_s(szAction, SIZEOF(szAction), " *");
            }
            else
                sprintf_s(szAction, SIZEOF(szAction), "<unnamed action %d>%s", (int)anid, fCustom ? " *" : "");
            const LRESULT irow = SendMessageA(hwndActions, CB_ADDSTRING, 0, (LPARAM)szAction);
            if (irow != CB_ERR && irow != CB_ERRSPACE)
            {
                SendMessageA(hwndActions, CB_SETITEMDATA, (WPARAM)irow, (LPARAM)anid);
                if (anid == vastp4DMM.anid)
                    SendMessageA(hwndActions, CB_SETCURSEL, (WPARAM)irow, 0);
            }
        }
    }
}

static void Update4DMMActorStudioButtons(void)
{
    if (vhwnd4DMMActorStudio == hNil || !IsWindow(vhwnd4DMMActorStudio))
        return;
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    const bool fHaveAction = pactr != pvNil && pactr->Ptmpl() != pvNil &&
                             !pactr->Ptmpl()->FIsTdt() && vastp4DMM.anid >= 0;
    const bool fCustom = fHaveAction && pmvie != pvNil &&
                         pmvie->FActorStudioActionIsCustom(pactr, vastp4DMM.anid);
    int32_t ccel = 0;
    if (fHaveAction)
        vastp4DMM.ptmpl->FGetCcelActn(vastp4DMM.anid, &ccel);
    const bool fFrameBrowse = fHaveAction && !vastp4DMM.fPlaying;
    const bool fFrameEdit = fCustom && !vastp4DMM.fPlaying;
    const bool fCanManipulate = fFrameEdit && vastp4DMM.pbody != pvNil;
    const bool fHasPart = fCanManipulate &&
                          FIn(vastp4DMM.ipartSelected, 0, vastp4DMM.pbody->Cpart());
    const bool fCanPaste = fFrameEdit && vasframe4DMMActorStudioClipboard.fValid;
    const bool fDefaultPartGroupSelected = fHasPart &&
        vastp4DMM.kindSelected == kastnDefaultPartGroup;
    const bool fWholeActorPropSelected = fHasPart &&
        vastp4DMM.kindSelected == kastnActorPropRoot;
    const bool fCanDuplicate = fHasPart && pactr != pvNil &&
        !fDefaultPartGroupSelected && !fWholeActorPropSelected;
    const bool fCanToggleRotationEdge = fHasPart && vastp4DMM.kindSelected == kastnPart &&
        vastp4DMM.cpartSelection == 1 &&
        F4DMMActorStudioPartIsImportedTdt(vastp4DMM.ipartSelected) &&
        (vastp4DMM.modePartTool == kastPitch || vastp4DMM.modePartTool == kastRoll);
    const bool fCanDeleteTool = fCanManipulate && vastp4DMM.pbody != pvNil &&
        vastp4DMM.pbody->Cpart() > 1 && !fDefaultPartGroupSelected &&
        !fWholeActorPropSelected;
    // Cut is tool-first, like the main 3DMM Cut button: arm it first, then
    // click the BODY Part in the ASV. Ctrl+X remains the immediate selected-
    // part command. Therefore the button itself must not require a current
    // Part selection.
    const bool fCanCutTool = fCanManipulate && vastp4DMM.pbody != pvNil;
    const bool fTdtTarget = vastp4DMM.ptmpl != pvNil && vastp4DMM.ptmpl->FIsTdt();
    const bool fCanSortDefaultPartGroups = !vastp4DMM.fPlaying &&
        vf4DMMActorStudioHasDefaultPartGroups && !fTdtTarget;
    const bool fCanUseObject = pmvie != pvNil && pactr != pvNil &&
        Id4DMMActorStudioHandmadeObject(pmvie) > 0 && pmvie->Pscen() != pvNil;

    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioObject), !vastp4DMM.fPlaying);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioActions), !vastp4DMM.fPlaying);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioFrames), fFrameBrowse);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioParts), !vastp4DMM.fPlaying);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioNewAction), fHaveAction && !vastp4DMM.fPlaying);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioSaveAs), fHaveAction && !vastp4DMM.fPlaying);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioSave), fCustom && !vastp4DMM.fPlaying);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioPlayPause), fHaveAction && ccel > 0);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioStop), fHaveAction && ccel > 0);

    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioFirst), fFrameBrowse && ccel > 0);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioLast), fFrameBrowse && ccel > 0);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioNext), fFrameBrowse && ccel > 0);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioBack), fFrameBrowse && ccel > 0);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioNewFrame), fFrameEdit);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioDeleteFrame), fFrameEdit && ccel > 1);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioCopyFrame), fFrameBrowse);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioCutFrame), fFrameEdit && ccel > 1);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioPasteFrame), fCanPaste);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioPasteBefore), fCanPaste);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioPasteAfter), fCanPaste);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioInsertBefore), fFrameEdit);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioInsertAfter), fFrameEdit);

    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioPitch), fCanManipulate);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioYaw), fCanManipulate);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioRoll), fCanManipulate);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioPosition), fCanManipulate);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioVerticalMovement), fCanManipulate);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioGrowShrink), fCanManipulate);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioStretchSquish), fCanManipulate);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioToggleRotationEdge),
                 fCanToggleRotationEdge);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioResetRotation), fHasPart);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioDuplicatePart), fCanDuplicate);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioKeepPositions), fCanDuplicate);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioCutPart), fCanCutTool);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioDeleteTool), fCanDeleteTool);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioUseObject), fCanUseObject);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioResetCamera),
                 vastp4DMM.pbody != pvNil && vastp4DMM.pbwld != pvNil);
    EnableWindow(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioSortDefaultPartGroups),
                 fCanSortDefaultPartGroups);
    SendMessageA(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioSortDefaultPartGroups),
                 BM_SETCHECK, vf4DMMActorStudioSortDefaultPartGroups ?
                     BST_CHECKED : BST_UNCHECKED, 0);

    SetWindowTextA(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioPlayPause),
                   vastp4DMM.fPlaying ? "Pause" : "Play/Pause");
    SendMessageA(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioPosition), BM_SETSTATE,
                 vastp4DMM.modePartTool == kastReposition, 0);
    SendMessageA(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioPitch), BM_SETSTATE,
                 vastp4DMM.modePartTool == kastPitch, 0);
    SendMessageA(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioYaw), BM_SETSTATE,
                 vastp4DMM.modePartTool == kastYaw, 0);
    SendMessageA(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioRoll), BM_SETSTATE,
                 vastp4DMM.modePartTool == kastRoll, 0);
    SendMessageA(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioToggleRotationEdge), BM_SETSTATE,
                 fCanToggleRotationEdge &&
                 F4DMMActorStudioRotationEdgeOpposite(vastp4DMM.modePartTool,
                                                       vastp4DMM.ipartSelected), 0);
    SendMessageA(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioGrowShrink), BM_SETSTATE,
                 vastp4DMM.modePartTool == kastGrowShrink, 0);
    SendMessageA(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioStretchSquish), BM_SETSTATE,
                 vastp4DMM.modePartTool == kastStretchSquish, 0);
    SendMessageA(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioCutPart), BM_SETSTATE,
                 vastp4DMM.modePartTool == kastCut, 0);
    SendMessageA(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioDeleteTool), BM_SETSTATE,
                 vastp4DMM.modePartTool == kastDelete, 0);
    SetWindowTextA(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioUseObject),
                   pactr != pvNil && pactr->Ptmpl() != pvNil && pactr->Ptmpl()->FIsProp() ?
                       "Use Prop" : "Hire Actor");
    SendMessageA(GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioVerticalMovement), BM_SETCHECK,
                 vastp4DMM.fVerticalMovement ? BST_CHECKED : BST_UNCHECKED, 0);
}

static bool FRefresh4DMMActorStudio(bool fRebuildPreview)
{
    if (vhwnd4DMMActorStudio == hNil || !IsWindow(vhwnd4DMMActorStudio))
        return fFalse;
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pmvie != vpmvie4DMMActorStudio || pactr == pvNil)
    {
        Close4DMMSettingsWindow(vhwnd4DMMActorStudio);
        return fFalse;
    }

    STN stn;
    pactr->GetName(&stn);
    achar szTitle[256];
    sprintf_s(szTitle, SIZEOF(szTitle), "4DMM Actor Studio - %s", stn.Psz());
    SetWindowTextA(vhwnd4DMMActorStudio, szTitle);
    if (vhwnd4DMMActorStudioViewport != hNil && IsWindow(vhwnd4DMMActorStudioViewport))
    {
        achar szViewportTitle[256];
        sprintf_s(szViewportTitle, SIZEOF(szViewportTitle), "4DMM Actor Studio Viewport - %s", stn.Psz());
        SetWindowTextA(vhwnd4DMMActorStudioViewport, szViewportTitle);
    }

    if (fRebuildPreview || vastp4DMM.arid != pactr->Arid() || vastp4DMM.pbody == pvNil)
    {
        if (!FBuild4DMMActorStudioPreview(pmvie, pactr))
            return fFalse;
        Fill4DMMActorStudioObjects();
        Fill4DMMActorStudioActions();
        Fill4DMMActorStudioFrames();
        Fill4DMMActorStudioParts();
    }
    Update4DMMActorStudioInfo(pmvie);
    Update4DMMActorStudioButtons();
    return fTrue;
}

static void Select4DMMActorStudioPartRow(int32_t ipart, bool fViewportPick = fFalse)
{
    if (vhwnd4DMMActorStudio == hNil || !IsWindow(vhwnd4DMMActorStudio) ||
        vastp4DMM.pbody == pvNil || !FIn(ipart, 0, vastp4DMM.pbody->Cpart()))
        return;
    const int32_t irowData = fViewportPick ?
        I4DMMActorStudioTreeRowForViewportPart(ipart) :
        I4DMMActorStudioTreeRowForPart(ipart);
    if (irowData != ivNil)
        FSelect4DMMActorStudioTreeRow(irowData, fTrue);
}

void Refresh4DMMActorStudioAfterActionEditSelection(PMVIE pmvie, int32_t arid, int32_t anid,
                                                     int32_t celn, int32_t ipart,
                                                     int32_t selectionKind,
                                                     int32_t ipartSelectionFirst,
                                                     int32_t cpartSelection)
{
    if (pmvie == pvNil || pmvie != vpmvie4DMMActorStudio ||
        vhwnd4DMMActorStudio == hNil || !IsWindow(vhwnd4DMMActorStudio) ||
        arid != varid4DMMActorStudioTarget || vastp4DMM.pbody == pvNil)
        return;

    // Undo/Redo may replace arbitrary BODY root matrices. A pre-undo rotation
    // baseline would no longer describe the current pose reliably.
    Invalidate4DMMActorStudioGroupRotationBaseline();

    if (!FSet4DMMActorStudioActionCel(anid, celn, fFalse))
    {
        MVIE::MultiLog(pmvie,
            "actor_studio_pose_refresh fail arid=%ld action=%ld cel=%ld part=%ld selection_kind=%ld first=%ld parts=%ld",
            (long)arid, (long)anid, (long)celn, (long)ipart,
            (long)selectionKind, (long)ipartSelectionFirst, (long)cpartSelection);
        return;
    }

    Fill4DMMActorStudioFrames();
    bool fSelectionRestored = fFalse;
    if (selectionKind != ivNil && cpartSelection > 0 &&
        FIn(ipartSelectionFirst, 0, vastp4DMM.pbody->Cpart()) &&
        ipartSelectionFirst + cpartSelection <= vastp4DMM.pbody->Cpart())
    {
        const int32_t irowData = I4DMMActorStudioTreeRowForRange(
            selectionKind, ipartSelectionFirst, cpartSelection);
        if (irowData != ivNil)
            fSelectionRestored = FSelect4DMMActorStudioTreeRow(irowData, fTrue);
    }
    if (!fSelectionRestored && FIn(ipart, 0, vastp4DMM.pbody->Cpart()))
        Select4DMMActorStudioPartRow(ipart);
    Update4DMMActorStudioInfo(pmvie);
    Update4DMMActorStudioButtons();
    MVIE::MultiLog(pmvie,
        "actor_studio_pose_refresh ok arid=%ld action=%ld cel=%ld part=%ld selection_kind=%ld first=%ld parts=%ld restored=%d",
        (long)arid, (long)anid, (long)celn, (long)ipart,
        (long)selectionKind, (long)ipartSelectionFirst, (long)cpartSelection,
        (int)fSelectionRestored);
}

void Refresh4DMMActorStudioAfterActionEdit(PMVIE pmvie, int32_t arid, int32_t anid,
                                            int32_t celn, int32_t ipart)
{
    Refresh4DMMActorStudioAfterActionEditSelection(
        pmvie, arid, anid, celn, ipart, ivNil, ivNil, 0);
}

static bool FReset4DMMActorStudioPartRotation(void);
static bool FReset4DMMActorStudioPartScale(void);
static void Set4DMMActorStudioPartTool(int32_t mode);
static bool F4DMMActorStudioForeground(void);
static void Clear4DMMMainSceneSelectionForActorStudio(PCSZ pszReason);

static void Clear4DMMActorStudioPartSelection(bool fRender)
{
    if (vastp4DMM.pbody != pvNil)
        vastp4DMM.pbody->ClearPartHilite();
    vastp4DMM.ipartSelected = ivNil;
    vastp4DMM.kindSelected = kastnPart;
    vastp4DMM.ipartSelectionFirst = ivNil;
    vastp4DMM.cpartSelection = 0;
    vastp4DMM.fTrackOwnGeometryOnly = fFalse;
    if (vhwnd4DMMActorStudio != hNil && IsWindow(vhwnd4DMMActorStudio))
    {
        HWND hwndParts = GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioParts);
        if (hwndParts != hNil)
            SendMessageA(hwndParts, LB_SETCURSEL, (WPARAM)-1, 0);
    }
    if (fRender)
        Render4DMMActorStudioPreview();
    Update4DMMActorStudioInfo(vpmvie4DMMActorStudio);
    Update4DMMActorStudioButtons();
    MVIE::MultiLog(vpmvie4DMMActorStudio, "actor_studio_selection clear");
}

static void Clear4DMMMainSceneSelectionForActorStudio(PCSZ pszReason)
{
    PMVIE pmvie = vpmvie4DMMActorStudio != pvNil ?
        vpmvie4DMMActorStudio : Pmvie4DMMSettingsCurrent();
    if (pmvie == pvNil || pmvie->Pscen() == pvNil)
        return;

    const int32_t cSelected = pmvie->Pscen()->CactrSelected();
    if (cSelected <= 0)
        return;

    // Actor Studio owns editing input whenever either of its top-level windows
    // is active. Clear the main movie selection at that boundary so a command
    // that belongs to AS can never operate on a stale highlighted scene object.
    // SelectActr(nil) also clears the complete -multi selection list.
    pmvie->Pscen()->SelectActr(pvNil);
    pmvie->InvalViewsAndScb();
    MVIE::MultiLog(pmvie,
        "actor_studio_main_selection_clear reason=%s count=%ld",
        pszReason != pvNil ? pszReason : "unknown", (long)cSelected);
}

static bool FReset4DMMActorStudioCamera(void)
{
    if (vastp4DMM.pbody == pvNil || vastp4DMM.pbwld == pvNil)
        return fFalse;
    vastp4DMM.flYaw = 0.0f;
    vastp4DMM.flPitch = 0.0f;
    vastp4DMM.flZoom = 1.0f;
    vastp4DMM.flPanY = 0.0f;
    Compute4DMMActorStudioFraming();
    Render4DMMActorStudioPreview();
    MVIE::MultiLog(vpmvie4DMMActorStudio,
        "actor_studio_camera reset center=(%g,%g,%g) distance=%g",
        (double)BrScalarToFloat(vastp4DMM.xrCenter),
        (double)BrScalarToFloat(vastp4DMM.yrCenter),
        (double)BrScalarToFloat(vastp4DMM.zrCenter),
        (double)BrScalarToFloat(vastp4DMM.zrCameraBase));
    return fTrue;
}

static void Set4DMMActorStudioFastHotkeys(bool fEnable)
{
    // v152 routes native keydown events synchronously in APPB::_DispatchEvt,
    // before Kauai's global accelerator table. Keep this state false so no
    // timer/GetAsyncKeyState path can steal or double-fire Ctrl+C/V/S/Q.
    if (vf4DMMActorStudioFastHotkeysRegistered || fEnable)
        MVIE::MultiLog(vpmvie4DMMActorStudio,
            "actor_studio_fast_hotkeys direct_dispatch requested=%d", (int)fEnable);
    vf4DMMActorStudioFastHotkeysRegistered = fFalse;
    vgrf4DMMActorStudioPolledKeys = 0;
}

static void Update4DMMActorStudioFastHotkeysForForeground(void)
{
    HWND hwndFg = GetForegroundWindow();
    const bool fActorStudioForeground =
        hwndFg == vhwnd4DMMActorStudio || hwndFg == vhwnd4DMMActorStudioViewport;
    Set4DMMActorStudioFastHotkeys(fActorStudioForeground);
}

static bool FHandle4DMMActorStudioRegisteredHotKey(int32_t id)
{
    if (!F4DMMActorStudioForeground())
        return fFalse;
    MVIE::MultiLog(vpmvie4DMMActorStudio,
        "actor_studio_fast_hotkey received id=%ld", (long)id);
    // ESC is selection semantics, not an editing tool. Let it work even if an
    // animation is playing, and consume it here before any legacy close path.
    if (id == kid4DMMActorStudioFastEscape)
    {
        if (!FCancel4DMMActorStudioSpawnPlacement())
        {
            Clear4DMMActorStudioPartSelection(fTrue);
            MVIE::MultiLog(vpmvie4DMMActorStudio, "actor_studio_escape deselect");
        }
        return fTrue;
    }
    if (vastp4DMM.fPlaying)
        return fFalse;
    switch (id)
    {
    case kid4DMMActorStudioFastPitch: Set4DMMActorStudioPartTool(kastPitch); return fTrue;
    case kid4DMMActorStudioFastYaw: Set4DMMActorStudioPartTool(kastYaw); return fTrue;
    case kid4DMMActorStudioFastRoll: Set4DMMActorStudioPartTool(kastRoll); return fTrue;
    case kid4DMMActorStudioFastResetRotation: return FReset4DMMActorStudioPartRotation();
    case kid4DMMActorStudioFastGrowShrink: Set4DMMActorStudioPartTool(kastGrowShrink); return fTrue;
    case kid4DMMActorStudioFastStretchSquish: Set4DMMActorStudioPartTool(kastStretchSquish); return fTrue;
    case kid4DMMActorStudioFastResetScale: return FReset4DMMActorStudioPartScale();
    case kid4DMMActorStudioFastReposition: Set4DMMActorStudioPartTool(kastReposition); return fTrue;
    case kid4DMMActorStudioFastVertical:
        vastp4DMM.fVerticalMovement = !vastp4DMM.fVerticalMovement;
        Update4DMMActorStudioButtons();
        return fTrue;
    case kid4DMMActorStudioFastDelete:
        Set4DMMActorStudioPartTool(kastDelete);
        return fTrue;
    case kid4DMMActorStudioFastCutDelete:
        // Ctrl+X belongs to Actor Studio while AS owns focus even when the
        // current hierarchy level is not cuttable yet (for example OG or
        // EVERYTHING). Consume the chord here so Kauai cannot fall through to
        // the main movie's Cut accelerator and edit the scene behind AS.
        F4DMMActorStudioCutSelectedPart();
        return fTrue;
    case kid4DMMActorStudioFastCopyPart:
        F4DMMActorStudioCopySelectedPart();
        return fTrue;
    case kid4DMMActorStudioFastPastePart:
        F4DMMActorStudioPastePart(fFalse);
        return fTrue;
    case kid4DMMActorStudioFastPastePartFrozen:
        F4DMMActorStudioPastePart(fTrue);
        return fTrue;
    }
    return fFalse;
}

static bool FBegin4DMMActorStudioPartDrag(HWND hwnd, int32_t xp, int32_t yp,
                                              bool fForceSelected = fFalse);
static bool FUpdate4DMMActorStudioPartDrag(int32_t xp, int32_t yp);
static bool FEnd4DMMActorStudioPartDrag(int32_t xp, int32_t yp, bool fCommit);
static bool FPick4DMMActorStudioPart(int32_t xp, int32_t yp, int32_t *pipart);
static bool F4DMMActorStudioDeleteSelectedNode(void);
static bool FOpen4DMMObjectGroups(void);
static bool FHandle4DMMActorStudioToolKey(WPARAM vk);
static bool FNavigate4DMMActorStudioList(HWND hwndList, int32_t drow, PCSZ pszSource);
static bool FSet4DMMActorStudioPlaying(bool fPlaying, bool fReturnFirst);
static bool FToggle4DMMActorStudioPlayback(void);
static bool FDispatch4DMMActorStudioNativeKey(WPARAM vk, LPARAM lParam)
{
    const bool fCtrl = (lParam & 0x01) != 0;
    const bool fShift = (lParam & 0x02) != 0;
    const bool fAlt = (lParam & 0x04) != 0;
    if (vk == VK_UP || vk == VK_DOWN)
    {
        int32_t idList = vid4DMMActorStudioNavList;
        PCSZ pszSource = PszLit("plain-last-list-direct");
        if (fCtrl && !fAlt)
        {
            idList = kid4DMMActorStudioFrames;
            pszSource = PszLit("ctrl-frame-direct");
        }
        else if (fAlt && !fCtrl)
        {
            idList = kid4DMMActorStudioParts;
            pszSource = PszLit("alt-part-direct");
        }
        else if (fCtrl || fAlt)
        {
            return fFalse;
        }
        if (idList != kid4DMMActorStudioFrames && idList != kid4DMMActorStudioParts)
            idList = kid4DMMActorStudioFrames;
        HWND hwndList = GetDlgItem(vhwnd4DMMActorStudio, idList);
        FNavigate4DMMActorStudioList(hwndList, vk == VK_UP ? -1 : 1, pszSource);
        return fTrue;
    }
    if (vk == VK_ESCAPE)
    {
        if (FCancel4DMMActorStudioSpawnPlacement())
            return fTrue;
        Clear4DMMActorStudioPartSelection(fTrue);
        return fTrue;
    }
    if (vk == VK_SPACE)
        return FToggle4DMMActorStudioPlayback();
    if (fCtrl && vk == 'X')
    {
        FHandle4DMMActorStudioRegisteredHotKey(kid4DMMActorStudioFastCutDelete);
        return fTrue;
    }
    if (fCtrl && vk == 'C')
    {
        FHandle4DMMActorStudioRegisteredHotKey(kid4DMMActorStudioFastCopyPart);
        return fTrue;
    }
    if (fCtrl && vk == 'V')
    {
        FHandle4DMMActorStudioRegisteredHotKey(
            fShift ? kid4DMMActorStudioFastPastePartFrozen : kid4DMMActorStudioFastPastePart);
        return fTrue;
    }
    if (fCtrl && vk == 'Q')
    {
        MVIE::MultiLog(vpmvie4DMMActorStudio, "actor_studio_input isolated_ctrl_q");
        return fTrue;
    }
    return FHandle4DMMActorStudioToolKey(vk);
}

static LRESULT CALLBACK Lresult4DMMActorStudioViewportWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    switch (wm)
    {
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE)
        {
            Clear4DMMMainSceneSelectionForActorStudio("asv_activate");
            Set4DMMActorStudioFastHotkeys(fTrue);
        }
        else
            Update4DMMActorStudioFastHotkeysForForeground();
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        Paint4DMMActorStudioViewport(hwnd);
        return 0;
    }
    case kwm4DMMActorStudioKey:
        return FDispatch4DMMActorStudioNativeKey(wParam, lParam) ? 1 : 0;
    case WM_KEYDOWN:
        // Direct APPB routing handles tool keys before Kauai accelerators.
        // Otherwise a native WM_KEYDOWN plus the 33ms poll can double-toggle
        // stateful keys such as T/2 or Space and appear to do nothing.
        if (!vf4DMMActorStudioFastHotkeysRegistered)
        {
            if (wParam == VK_ESCAPE)
            {
                Clear4DMMActorStudioPartSelection(fTrue);
                return 0;
            }
            if (wParam == VK_SPACE)
            {
                FToggle4DMMActorStudioPlayback();
                return 0;
            }
            if (FHandle4DMMActorStudioToolKey(wParam))
                return 0;
        }
        break;
    case WM_LBUTTONDOWN:
    {
        SetFocus(hwnd);
        const int32_t xp = (short)LOWORD(lParam);
        const int32_t yp = (short)HIWORD(lParam);
        const bool fAltForceSelected = GetKeyState(VK_MENU) < 0;

        // A row-click Spawn already owns a Reposition drag and capture. The
        // next click is its placement confirmation, so do not start a second
        // drag on button-down; WM_LBUTTONUP below commits the existing one.
        if (vf4DMMActorStudioSpawnPlacement && vastp4DMM.fPartTracking)
            return 0;

        // Playback freezes BODY-part editing/selection, but camera orbit is
        // still useful while inspecting a looping action.
        if (vastp4DMM.fPlaying && GetKeyState(VK_CONTROL) >= 0)
            return 0;

        // Match every other Actor Studio tool: Ctrl+Left drag is always camera
        // orbit, even while Cut is armed. Alt remains Cut's explicit
        // "use currently selected Part" override, so Ctrl+Alt does not steal it.
        if (!fAltForceSelected && GetKeyState(VK_CONTROL) < 0 && vastp4DMM.pbody != pvNil)
        {
            vastp4DMM.fOrbitTracking = fTrue;
            vastp4DMM.ptTrackLast.x = xp;
            vastp4DMM.ptTrackLast.y = yp;
            SetCapture(hwnd);
            return 0;
        }

        if (vastp4DMM.modePartTool == kastCut && vastp4DMM.pbody != pvNil)
        {
            if (!fAltForceSelected)
            {
                int32_t ipart = ivNil;
                if (!FPick4DMMActorStudioPart(xp, yp, &ipart))
                {
                    MVIE::MultiLog(vpmvie4DMMActorStudio,
                        "actor_studio_part_cut tool_pick_miss xy=%ld,%ld",
                        (long)xp, (long)yp);
                    return 0;
                }
                const bool fKeepDefaultGroup =
                    vastp4DMM.kindSelected == kastnDefaultPartGroup &&
                    !F4DMMActorStudioSelectedDefaultPartGroupExpanded() &&
                    FIn(vastp4DMM.ipartSelected, 0, vastp4DMM.pbody->Cpart()) &&
                    (ipart == vastp4DMM.ipartSelected ||
                     F4DMMActorStudioPartDescendsFrom(ipart, vastp4DMM.ipartSelected));
                const bool fKeepImportedObject =
                    F4DMMActorStudioSelectedObjectCanTemporalCutSpawn(
                        Pmvie4DMMSettingsCurrent()) &&
                    FIn(ipart, vastp4DMM.ipartSelectionFirst,
                        vastp4DMM.ipartSelectionFirst + vastp4DMM.cpartSelection);
                bool fKeepObjectGroup = fFalse;
                if (F4DMMActorStudioSelectedObjectGroupCanTemporalCutSpawn(
                        Pmvie4DMMSettingsCurrent()))
                {
                    std::vector<int32_t> rgipartGroup;
                    Get4DMMActorStudioSelectedObjectGroupParts(
                        Pmvie4DMMSettingsCurrent(), &rgipartGroup);
                    fKeepObjectGroup = std::find(
                        rgipartGroup.begin(), rgipartGroup.end(), ipart) !=
                        rgipartGroup.end();
                }
                const bool fKeepWholeRoot =
                    F4DMMActorStudioSelectedWholeRootCanTemporalCutSpawn(
                        Pmvie4DMMSettingsCurrent()) &&
                    FIn(ipart, vastp4DMM.ipartSelectionFirst,
                        vastp4DMM.ipartSelectionFirst + vastp4DMM.cpartSelection);
                if (!fKeepDefaultGroup && !fKeepImportedObject &&
                    !fKeepObjectGroup && !fKeepWholeRoot)
                    Select4DMMActorStudioPartRow(ipart, fTrue);
            }
            else
            {
                MVIE::MultiLog(vpmvie4DMMActorStudio,
                    "actor_studio_part_cut tool_alt selected=%ld kind=%ld parts=%ld xy=%ld,%ld",
                    (long)vastp4DMM.ipartSelected, (long)vastp4DMM.kindSelected,
                    (long)vastp4DMM.cpartSelection, (long)xp, (long)yp);
            }

            if (!F4DMMActorStudioCutSelectedPart())
            {
                MVIE::MultiLog(vpmvie4DMMActorStudio,
                    "actor_studio_part_cut tool_rejected selected=%ld kind=%ld parts=%ld action=%ld cel=%ld alt=%d",
                    (long)vastp4DMM.ipartSelected, (long)vastp4DMM.kindSelected,
                    (long)vastp4DMM.cpartSelection, (long)vastp4DMM.anid,
                    (long)vastp4DMM.celn, (int)fAltForceSelected);
            }
            return 0;
        }

        if (vastp4DMM.modePartTool == kastDelete && vastp4DMM.pbody != pvNil)
        {
            if (!fAltForceSelected)
            {
                int32_t ipart = ivNil;
                if (!FPick4DMMActorStudioPart(xp, yp, &ipart))
                    return 0;
                const bool fWithinCurrent = vastp4DMM.cpartSelection > 1 &&
                    FIn(ipart, vastp4DMM.ipartSelectionFirst,
                        vastp4DMM.ipartSelectionFirst + vastp4DMM.cpartSelection);
                if (!fWithinCurrent)
                    Select4DMMActorStudioPartRow(ipart, fTrue);
            }
            F4DMMActorStudioDeleteSelectedNode();
            return 0;
        }

        // ALT is Actor Studio's explicit "do not repick" modifier. It takes
        // precedence over camera orbit so a deeply occluded yellow-box part can
        // always be manipulated with the currently selected tool by dragging
        // anywhere in the preview.
        if (fAltForceSelected)
        {
            if (FBegin4DMMActorStudioPartDrag(hwnd, xp, yp, fTrue))
                return 0;
            MVIE::MultiLog(vpmvie4DMMActorStudio,
                "actor_studio_part_drag force_selected_rejected selected=%ld mode=%ld xy=%ld,%ld",
                (long)vastp4DMM.ipartSelected, (long)vastp4DMM.modePartTool,
                (long)xp, (long)yp);
            return 0;
        }
        if (FBegin4DMMActorStudioPartDrag(hwnd, xp, yp, fFalse))
            return 0;
        break;
    }
    case WM_MBUTTONDOWN:
        if (vastp4DMM.pbody != pvNil)
        {
            vastp4DMM.fPanTracking = fTrue;
            vastp4DMM.ptTrackLast.x = (short)LOWORD(lParam);
            vastp4DMM.ptTrackLast.y = (short)HIWORD(lParam);
            SetCapture(hwnd);
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (vastp4DMM.fPartTracking)
        {
            const int32_t xp = (short)LOWORD(lParam);
            const int32_t yp = (short)HIWORD(lParam);
            if (vf4DMMActorStudioSpawnPlacement)
                FUpdate4DMMActorStudioSpawnCursor(xp, yp);
            else
                FUpdate4DMMActorStudioPartDrag(xp, yp);
            return 0;
        }
        if (vastp4DMM.fOrbitTracking || vastp4DMM.fPanTracking)
        {
            const int32_t xp = (short)LOWORD(lParam);
            const int32_t yp = (short)HIWORD(lParam);
            const int32_t dxp = xp - vastp4DMM.ptTrackLast.x;
            const int32_t dyp = yp - vastp4DMM.ptTrackLast.y;
            vastp4DMM.ptTrackLast.x = xp;
            vastp4DMM.ptTrackLast.y = yp;
            if (vastp4DMM.fOrbitTracking)
            {
                vastp4DMM.flYaw -= (float)dxp * 0.45f;
                vastp4DMM.flPitch += (float)dyp * 0.45f;
                if (vastp4DMM.flPitch < -85.0f)
                    vastp4DMM.flPitch = -85.0f;
                if (vastp4DMM.flPitch > 85.0f)
                    vastp4DMM.flPitch = 85.0f;
            }
            else
            {
                const float flScale = BrScalarToFloat(vastp4DMM.zrCameraBase) / 320.0f;
                vastp4DMM.flPanY -= (float)dyp * flScale;
            }
            Render4DMMActorStudioPreview();
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (vastp4DMM.fPartTracking)
        {
            const bool fSpawnPlacement = vf4DMMActorStudioSpawnPlacement;
            const int32_t xpEnd = fSpawnPlacement ?
                vpt4DMMActorStudioSpawnVirtual.x : (short)LOWORD(lParam);
            const int32_t ypEnd = fSpawnPlacement ?
                vpt4DMMActorStudioSpawnVirtual.y : (short)HIWORD(lParam);
            FEnd4DMMActorStudioPartDrag(xpEnd, ypEnd, fTrue);
            if (fSpawnPlacement)
            {
                vf4DMMActorStudioSpawnPlacement = fFalse;
                End4DMMActorStudioSpawnCursor(fTrue);
                MVIE::MultiLog(vpmvie4DMMActorStudio,
                    "actor_studio_part_spawn placement_commit action=%ld cel=%ld part=%ld virtual_xy=%ld,%ld",
                    (long)vastp4DMM.anid, (long)vastp4DMM.celn,
                    (long)vastp4DMM.ipartSelected,
                    (long)xpEnd, (long)ypEnd);
            }
            return 0;
        }
        if (vastp4DMM.fOrbitTracking)
        {
            vastp4DMM.fOrbitTracking = fFalse;
            ReleaseCapture();
            MVIE::MultiLog(vpmvie4DMMActorStudio,
                "actor_studio_camera orbit yaw=%.3f pitch=%.3f zoom=%.3f panY=%.3f",
                (double)vastp4DMM.flYaw, (double)vastp4DMM.flPitch,
                (double)vastp4DMM.flZoom, (double)vastp4DMM.flPanY);
            return 0;
        }
        if (vastp4DMM.pbody != pvNil && vastp4DMM.pbwld != pvNil)
        {
            PBACT pbact = pvNil;
            const int32_t xp = (short)LOWORD(lParam);
            const int32_t yp = (short)HIWORD(lParam);
            if (vastp4DMM.pbwld->FClickedActor(xp, yp, &pbact))
            {
                const int32_t ipart = vastp4DMM.pbody->IpartFromBact(pbact);
                if (ipart != ivNil)
                {
                    Select4DMMActorStudioPartRow(ipart, fTrue);
                    MVIE::MultiLog(vpmvie4DMMActorStudio,
                        "actor_studio_part_click arid=%ld part=%ld parent=%ld set=%ld",
                        (long)vastp4DMM.arid, (long)ipart,
                        (long)vastp4DMM.pbody->IpartParent(ipart),
                        (long)vastp4DMM.pbody->IbsetOfPart(ipart));
                }
            }
            return 0;
        }
        break;
    case WM_MBUTTONUP:
        if (vastp4DMM.fPanTracking)
        {
            vastp4DMM.fPanTracking = fFalse;
            ReleaseCapture();
            MVIE::MultiLog(vpmvie4DMMActorStudio,
                "actor_studio_camera pan yaw=%.3f pitch=%.3f zoom=%.3f panY=%.3f",
                (double)vastp4DMM.flYaw, (double)vastp4DMM.flPitch,
                (double)vastp4DMM.flZoom, (double)vastp4DMM.flPanY);
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        if (vastp4DMM.pbody != pvNil)
        {
            const int32_t dz = (short)HIWORD(wParam);
            if (dz > 0)
                vastp4DMM.flZoom *= 1.15f;
            else if (dz < 0)
                vastp4DMM.flZoom /= 1.15f;
            Render4DMMActorStudioPreview();
            MVIE::MultiLog(vpmvie4DMMActorStudio,
                "actor_studio_camera wheel yaw=%.3f pitch=%.3f zoom=%.3f panY=%.3f",
                (double)vastp4DMM.flYaw, (double)vastp4DMM.flPitch,
                (double)vastp4DMM.flZoom, (double)vastp4DMM.flPanY);
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (vastp4DMM.fPartTracking && vastp4DMM.pbody != pvNil)
        {
            for (int32_t i = 0; i < vastp4DMM.cpartTrack; ++i)
            {
                if (FIn(vastp4DMM.rgipartTrack[i], 0, vastp4DMM.pbody->Cpart()))
                    vastp4DMM.pbody->SetPartMatrix(vastp4DMM.rgipartTrack[i],
                                                   &vastp4DMM.rgbmat34PartTrackStart[i]);
            }
            vastp4DMM.fPartTracking = fFalse;
            vastp4DMM.fTrackOwnGeometryOnly = fFalse;
            vastp4DMM.ipartTrack = ivNil;
            vastp4DMM.cpartTrack = 0;
            vastp4DMM.anidTrack = ivNil;
            vastp4DMM.celnTrack = ivNil;
            Apply4DMMActorStudioSelectionHilite();
            Render4DMMActorStudioPreview();
        }
        vastp4DMM.fOrbitTracking = fFalse;
        vastp4DMM.fPanTracking = fFalse;
        if (vf4DMMActorStudioSpawnPlacement || vf4DMMActorStudioSpawnCursorHidden)
            End4DMMActorStudioSpawnCursor(fFalse);
        vf4DMMActorStudioSpawnPlacement = fFalse;
        vf4DMMActorStudioSpawnPlacementPending = fFalse;
        vipart4DMMActorStudioSpawnPending = ivNil;
        break;
    case WM_CLOSE:
        if (vhwnd4DMMActorStudio != hNil && IsWindow(vhwnd4DMMActorStudio))
            PostMessageA(vhwnd4DMMActorStudio, WM_CLOSE, 0, 0);
        else
            DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (vhwnd4DMMActorStudioViewport == hwnd)
        {
            if (vf4DMMActorStudioSpawnPlacement || vf4DMMActorStudioSpawnCursorHidden)
                End4DMMActorStudioSpawnCursor(fFalse);
            vhwnd4DMMActorStudioViewport = hNil;
        }
        return 0;
    }
    return DefWindowProcA(hwnd, wm, wParam, lParam);
}

static bool FNavigate4DMMActorStudioList(HWND hwndList, int32_t drow, PCSZ pszSource)
{
    if (hwndList == hNil || !IsWindow(hwndList) || (drow != -1 && drow != 1))
        return fFalse;
    const int32_t idList = GetDlgCtrlID(hwndList);
    if (idList != kid4DMMActorStudioFrames && idList != kid4DMMActorStudioParts)
        return fFalse;
    const LRESULT crow = SendMessageA(hwndList, LB_GETCOUNT, 0, 0);
    if (crow <= 0 || crow == LB_ERR)
        return fFalse;
    LRESULT irow = SendMessageA(hwndList, LB_GETCURSEL, 0, 0);
    const LRESULT irowOld = irow;
    if (irow == LB_ERR)
        irow = drow < 0 ? crow - 1 : 0;
    else
    {
        irow += drow;
        if (irow < 0 || irow >= crow)
            return fFalse;
    }
    if (SendMessageA(hwndList, LB_SETCURSEL, (WPARAM)irow, 0) == LB_ERR)
        return fFalse;

    vid4DMMActorStudioNavList = idList;
    HWND hwndPar = GetParent(hwndList);
    if (hwndPar != hNil)
        SendMessageA(hwndPar, WM_COMMAND, MAKEWPARAM(idList, LBN_SELCHANGE), (LPARAM)hwndList);
    MVIE::MultiLog(vpmvie4DMMActorStudio,
        "actor_studio_keynav list=%s source=%s old=%ld new=%ld",
        idList == kid4DMMActorStudioFrames ? "frames" : "parts",
        pszSource != pvNil ? pszSource : "unknown", (long)irowOld, (long)irow);
    return fTrue;
}

static LRESULT CALLBACK Lresult4DMMActorStudioListProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    if (vpfn4DMMActorStudioListOld == pvNil)
        return DefWindowProcA(hwnd, wm, wParam, lParam);
    const int32_t idList = GetDlgCtrlID(hwnd);
    if (wm == WM_GETDLGCODE)
        return CallWindowProcA(vpfn4DMMActorStudioListOld, hwnd, wm, wParam, lParam) |
               DLGC_WANTARROWS | DLGC_WANTCHARS;
    if (wm == WM_SETFOCUS)
        vid4DMMActorStudioNavList = idList;
    if (wm == WM_KEYDOWN)
    {
        if (wParam == VK_UP || wParam == VK_DOWN)
        {
            FNavigate4DMMActorStudioList(hwnd, wParam == VK_UP ? -1 : 1, "direct");
            return 0;
        }
        if (!vf4DMMActorStudioFastHotkeysRegistered)
        {
            if (wParam == VK_ESCAPE)
            {
                if (!FCancel4DMMActorStudioSpawnPlacement())
                    Clear4DMMActorStudioPartSelection(fTrue);
                return 0;
            }
            if (wParam == VK_SPACE)
            {
                FToggle4DMMActorStudioPlayback();
                return 0;
            }
            if (FHandle4DMMActorStudioToolKey(wParam))
                return 0;
        }
    }
    if (wm == WM_LBUTTONUP && idList == kid4DMMActorStudioParts)
    {
        // Let the native list box finish the click first. If this mouse-up is
        // over an absent literal Part, restore its model and only then capture
        // the mouse for Hire-style placement. Doing the hit-test here also
        // handles a hidden row that was already selected by Undo/Redo or key
        // navigation and therefore produced no new LBN_SELCHANGE notification.
        const LRESULT lr = CallWindowProcA(vpfn4DMMActorStudioListOld, hwnd, wm, wParam, lParam);
        const LRESULT lrHit = SendMessageA(hwnd, LB_ITEMFROMPOINT, 0, lParam);
        const int32_t irowHit = LOWORD(lrHit);
        const bool fOutside = HIWORD(lrHit) != 0;
        if (!fOutside && irowHit >= 0)
        {
            const int32_t irowData =
                (int32_t)SendMessageA(hwnd, LB_GETITEMDATA, (WPARAM)irowHit, 0);
            if (FIn(irowData, 0, (int32_t)vrg4DMMActorStudioPartRows.size()))
            {
                const ACTORSTUDIOTREEROW &rowHit = vrg4DMMActorStudioPartRows[irowData];
                if (rowHit.kind == kastnPart || rowHit.kind == kastnDefaultPartGroup ||
                    rowHit.kind == kastnObject || rowHit.kind == kastnGroup ||
                    rowHit.kind == kastnActorPropRoot)
                {
                    const int32_t ipart = rowHit.ipartFirst;
                    if (vastp4DMM.kindSelected != rowHit.kind ||
                        vastp4DMM.ipartSelected != ipart ||
                        vastp4DMM.cpartSelection != rowHit.cpart)
                    {
                        FSelect4DMMActorStudioTreeRow(irowData, fFalse);
                    }

                    if (rowHit.kind == kastnObject || rowHit.kind == kastnGroup ||
                        rowHit.kind == kastnActorPropRoot)
                    {
                        bool fCanTemporalSpawn = fFalse;
                        if (rowHit.kind == kastnGroup)
                            fCanTemporalSpawn =
                                F4DMMActorStudioSelectedObjectGroupCanTemporalCutSpawn(
                                    Pmvie4DMMSettingsCurrent());
                        else if (rowHit.kind == kastnActorPropRoot)
                            fCanTemporalSpawn =
                                F4DMMActorStudioSelectedWholeRootCanTemporalCutSpawn(
                                    Pmvie4DMMSettingsCurrent());
                        else
                            fCanTemporalSpawn =
                                F4DMMActorStudioSelectedObjectCanTemporalCutSpawn(
                                    Pmvie4DMMSettingsCurrent());
                        if (fCanTemporalSpawn)
                            FMaybePrepare4DMMActorStudioSpawnSelectedPart();
                    }
                    else
                    {
                        bool fNeedsSpawn = fFalse;
                        if (vastp4DMM.pbody != pvNil &&
                            FIn(ipart, 0, vastp4DMM.pbody->Cpart()))
                        {
                            if (rowHit.kind == kastnPart)
                            {
                                fNeedsSpawn = !vastp4DMM.pbody->FPartHasModel(ipart);
                            }
                            else
                            {
                                std::vector<int32_t> rgMembers;
                                Get4DMMActorStudioDefaultPartGroupMembersInRange(
                                    ipart, 0, vastp4DMM.pbody->Cpart(), &rgMembers);
                                for (size_t i = 0; i < rgMembers.size(); ++i)
                                {
                                    if (!vastp4DMM.pbody->FPartHasModel(rgMembers[i]))
                                    {
                                        fNeedsSpawn = fTrue;
                                        break;
                                    }
                                }
                            }
                        }
                        if (fNeedsSpawn)
                            FMaybePrepare4DMMActorStudioSpawnSelectedPart();
                    }
                }
            }
        }
        if (vf4DMMActorStudioSpawnPlacementPending)
            FBegin4DMMActorStudioPendingSpawnPlacement();
        return lr;
    }
    if (wm == WM_RBUTTONUP && idList == kid4DMMActorStudioFrames)
    {
        const LRESULT lrHit = SendMessageA(hwnd, LB_ITEMFROMPOINT, 0, lParam);
        const int32_t irowHit = LOWORD(lrHit);
        const bool fOutside = HIWORD(lrHit) != 0;
        if (!fOutside && irowHit >= 0)
        {
            SendMessageA(hwnd, LB_SETCURSEL, (WPARAM)irowHit, 0);
            HWND hwndPar = GetParent(hwnd);
            if (hwndPar != hNil)
                SendMessageA(hwndPar, WM_COMMAND,
                             MAKEWPARAM(kid4DMMActorStudioFrames, LBN_SELCHANGE), (LPARAM)hwnd);
        }
        HMENU hmenu = CreatePopupMenu();
        if (hmenu != hNil)
        {
            const bool fPaste = vasframe4DMMActorStudioClipboard.fValid;
            PACTR pactr = Pactr4DMMActorStudioTarget(vpmvie4DMMActorStudio);
            const bool fCustom = vpmvie4DMMActorStudio != pvNil && pactr != pvNil &&
                vpmvie4DMMActorStudio->FActorStudioActionIsCustom(pactr, vastp4DMM.anid);
            int32_t ccel = 0;
            const bool fCanCut = fCustom && vastp4DMM.ptmpl != pvNil &&
                vastp4DMM.ptmpl->FGetCcelActn(vastp4DMM.anid, &ccel) && ccel > 1;
            AppendMenuA(hmenu, MF_STRING, kid4DMMActorStudioFrameMenuCopy, "Copy");
            AppendMenuA(hmenu, MF_STRING | (fCanCut ? MF_ENABLED : MF_GRAYED),
                        kid4DMMActorStudioFrameMenuCut, "Cut");
            AppendMenuA(hmenu, MF_STRING | (fPaste && fCustom ? MF_ENABLED : MF_GRAYED),
                        kid4DMMActorStudioFrameMenuPaste, "Paste");
            AppendMenuA(hmenu, MF_STRING | (fPaste && fCustom ? MF_ENABLED : MF_GRAYED),
                        kid4DMMActorStudioFrameMenuPasteBefore, "Paste Before");
            AppendMenuA(hmenu, MF_STRING | (fPaste && fCustom ? MF_ENABLED : MF_GRAYED),
                        kid4DMMActorStudioFrameMenuPasteAfter, "Paste After");
            AppendMenuA(hmenu, MF_SEPARATOR, 0, pvNil);
            AppendMenuA(hmenu, MF_STRING | (fCustom ? MF_ENABLED : MF_GRAYED),
                        kid4DMMActorStudioFrameMenuInsertBefore, "Insert Frame Before");
            AppendMenuA(hmenu, MF_STRING | (fCustom ? MF_ENABLED : MF_GRAYED),
                        kid4DMMActorStudioFrameMenuInsertAfter, "Insert Frame After");
            POINT pt;
            GetCursorPos(&pt);
            const int32_t id = TrackPopupMenu(hmenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                               pt.x, pt.y, 0, GetParent(hwnd), pvNil);
            DestroyMenu(hmenu);
            if (id != 0)
                SendMessageA(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(id, 0), (LPARAM)hwnd);
        }
        return 0;
    }
    return CallWindowProcA(vpfn4DMMActorStudioListOld, hwnd, wm, wParam, lParam);
}

static const int32_t kid4DMMAnimNameEdit = 0x5470;
static const int32_t kid4DMMAnimNameSave = 0x5471;
static const int32_t kid4DMMAnimNameCancel = 0x5472;
static const achar ksz4DMMAnimNameWndClass[] = "4DMMAnimationNameWindow";
struct ANIMNAMESTATE
{
    bool fDone;
    bool fSave;
    achar szName[kcch4DMMCustomName];
};

static LRESULT CALLBACK Lresult4DMMAnimNameWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    ANIMNAMESTATE *pst = (ANIMNAMESTATE *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    if (wm == WM_CREATE)
    {
        CREATESTRUCTA *pcs = (CREATESTRUCTA *)lParam;
        pst = (ANIMNAMESTATE *)pcs->lpCreateParams;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)pst);
        HINSTANCE hinst = GetModuleHandleA(pvNil);
        CreateWindowExA(0, "STATIC", "Animation name:", WS_CHILD | WS_VISIBLE,
                        12, 12, 330, 20, hwnd, hNil, hinst, pvNil);
        HWND hwndEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", pst != pvNil ? pst->szName : "",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                        12, 34, 330, 25, hwnd, (HMENU)kid4DMMAnimNameEdit, hinst, pvNil);
        if (hwndEdit != hNil)
            SetPropA(hwndEdit, "3DMMExNativeTextInput", (HANDLE)1);
        CreateWindowExA(0, "BUTTON", "Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        180, 70, 78, 28, hwnd, (HMENU)kid4DMMAnimNameSave, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        264, 70, 78, 28, hwnd, (HMENU)kid4DMMAnimNameCancel, hinst, pvNil);
        const int32_t rgid[] = {kid4DMMAnimNameEdit, kid4DMMAnimNameSave, kid4DMMAnimNameCancel};
        Set4DMMNativeControlFont(hwnd, rgid, SIZEOF(rgid) / SIZEOF(rgid[0]));
        SetFocus(hwndEdit);
        SendMessageA(hwndEdit, EM_SETSEL, 0, -1);
        return 0;
    }
    if (wm == WM_COMMAND && pst != pvNil)
    {
        const int32_t id = LOWORD(wParam);
        if (id == kid4DMMAnimNameSave && HIWORD(wParam) == BN_CLICKED)
        {
            GetWindowTextA(GetDlgItem(hwnd, kid4DMMAnimNameEdit), pst->szName, SIZEOF(pst->szName));
            if (pst->szName[0] == 0)
                return 0;
            pst->fSave = fTrue;
            pst->fDone = fTrue;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == kid4DMMAnimNameCancel && HIWORD(wParam) == BN_CLICKED)
        {
            pst->fDone = fTrue;
            DestroyWindow(hwnd);
            return 0;
        }
    }
    if (wm == WM_CLOSE)
    {
        if (pst != pvNil)
            pst->fDone = fTrue;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcA(hwnd, wm, wParam, lParam);
}

static bool FPrompt4DMMAnimationName(PCSZ pszDefault, PCSZ pszTitle, achar *pszOut, size_t cbOut)
{
    if (pszOut == pvNil || cbOut == 0)
        return fFalse;
    ANIMNAMESTATE st;
    ClearPb(&st, SIZEOF(st));
    if (pszDefault != pvNil)
        strncpy_s(st.szName, SIZEOF(st.szName), pszDefault, _TRUNCATE);

    HINSTANCE hinst = GetModuleHandleA(pvNil);
    WNDCLASSA wc;
    ClearPb(&wc, SIZEOF(wc));
    if (!GetClassInfoA(hinst, ksz4DMMAnimNameWndClass, &wc))
    {
        wc.lpfnWndProc = Lresult4DMMAnimNameWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(pvNil, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = ksz4DMMAnimNameWndClass;
        if (RegisterClassA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return fFalse;
    }

    HWND hwndOwner = vhwnd4DMMActorStudio;
    if (hwndOwner != hNil && IsWindow(hwndOwner))
        EnableWindow(hwndOwner, fFalse);
    HWND hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        ksz4DMMAnimNameWndClass, pszTitle != pvNil ? pszTitle : "Save Animation As",
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 370, 185, hwndOwner, hNil, hinst, &st);
    if (hwnd == hNil)
    {
        if (hwndOwner != hNil && IsWindow(hwndOwner))
            EnableWindow(hwndOwner, fTrue);
        return fFalse;
    }
    SetForegroundWindow(hwnd);
    MSG msg;
    while (!st.fDone && GetMessage(&msg, pvNil, 0, 0) > 0)
    {
        if (msg.message == WM_KEYDOWN && (msg.hwnd == hwnd || IsChild(hwnd, msg.hwnd)))
        {
            if (msg.wParam == VK_ESCAPE)
            {
                SendMessageA(hwnd, WM_CLOSE, 0, 0);
                continue;
            }
            if (msg.wParam == VK_RETURN)
            {
                SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(kid4DMMAnimNameSave, BN_CLICKED),
                             (LPARAM)GetDlgItem(hwnd, kid4DMMAnimNameSave));
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (hwndOwner != hNil && IsWindow(hwndOwner))
    {
        EnableWindow(hwndOwner, fTrue);
        SetForegroundWindow(hwndOwner);
    }
    if (!st.fSave)
        return fFalse;
    strncpy_s(pszOut, cbOut, st.szName, _TRUNCATE);
    return fTrue;
}

static bool FPick4DMMActorStudioPart(int32_t xp, int32_t yp, int32_t *pipart)
{
    AssertVarMem(pipart);
    *pipart = ivNil;
    if (vastp4DMM.pbody == pvNil || vastp4DMM.pbwld == pvNil)
        return fFalse;

    PBACT pbact = pvNil;
    if (!vastp4DMM.pbwld->FClickedActor(xp, yp, &pbact))
        return fFalse;
    int32_t ipart = vastp4DMM.pbody->IpartFromBact(pbact);
    // The selected BODY-part yellow box is deliberately easiest to grab, just
    // like the main 3DMM actor selection box. It is not itself a BODY part, so
    // a hit that resolves to the hilite actor falls back to the selected part.
    if (ipart == ivNil && FIn(vastp4DMM.ipartSelected, 0, vastp4DMM.pbody->Cpart()))
        ipart = vastp4DMM.ipartSelected;
    if (!FIn(ipart, 0, vastp4DMM.pbody->Cpart()))
        return fFalse;
    *pipart = ipart;
    return fTrue;
}

static bool FGet4DMMActorStudioParentToWorld(int32_t ipart, BMAT34 *pbmat34)
{
    AssertVarMem(pbmat34);
    if (vastp4DMM.pbody == pvNil || !FIn(ipart, 0, vastp4DMM.pbody->Cpart()))
        return fFalse;

    BrMatrix34Identity(pbmat34);
    int32_t ipartParent = vastp4DMM.pbody->IpartParent(ipart);
    while (ipartParent != ivNil)
    {
        BMAT34 bmat34Parent;
        BMAT34 bmat34Combined;
        vastp4DMM.pbody->GetPartMatrix(ipartParent, &bmat34Parent);
        BrMatrix34Mul(&bmat34Combined, pbmat34, &bmat34Parent);
        *pbmat34 = bmat34Combined;
        ipartParent = vastp4DMM.pbody->IpartParent(ipartParent);
    }

    // Part matrices live below BODY's root actor. Actor Studio applies the
    // template's rest orientation to that root, so include it before turning
    // a camera/world drag vector into this part's parent-local coordinates.
    // BRender uses row-vector composition: parent-local -> root -> world.
    BMAT34 bmat34Root;
    BMAT34 bmat34ParentToWorld;
    vastp4DMM.pbody->GetMatrix(&bmat34Root);
    BrMatrix34Mul(&bmat34ParentToWorld, pbmat34, &bmat34Root);
    *pbmat34 = bmat34ParentToWorld;
    return fTrue;
}

static bool FGet4DMMActorStudioPartScreenCenter(int32_t ipart, POINT *ppt)
{
    if (ppt == pvNil || vastp4DMM.pbody == pvNil || vastp4DMM.pbwld == pvNil ||
        vhwnd4DMMActorStudioViewport == hNil ||
        !IsWindow(vhwnd4DMMActorStudioViewport) ||
        !FIn(ipart, 0, vastp4DMM.pbody->Cpart()))
        return fFalse;

    RECT rcClient;
    if (!GetClientRect(vhwnd4DMMActorStudioViewport, &rcClient))
        return fFalse;
    const int32_t dxp = rcClient.right - rcClient.left;
    const int32_t dyp = rcClient.bottom - rcClient.top;
    if (dxp <= 0 || dyp <= 0)
        return fFalse;

    // Use the center of the actual part model, not merely the BODY joint/origin,
    // so the cursor comes back in the visible middle of asymmetric hands,
    // shoes, heads, etc. Model bounds are local to the Part BACT.
    BVEC3 bvec3Local = {rZero, rZero, rZero};
    BRB brb;
    if (vastp4DMM.pbody->FGetPartModelBounds(ipart, &brb))
    {
        bvec3Local.v[0] = BrsHalf(BrsAdd(brb.min.v[0], brb.max.v[0]));
        bvec3Local.v[1] = BrsHalf(BrsAdd(brb.min.v[1], brb.max.v[1]));
        bvec3Local.v[2] = BrsHalf(BrsAdd(brb.min.v[2], brb.max.v[2]));
    }

    BMAT34 bmat34Part;
    BMAT34 bmat34ParentToWorld;
    BMAT34 bmat34PartToWorld;
    BMAT34 bmat34Camera;
    BMAT34 bmat34WorldToCamera;
    BVEC3 bvec3World;
    BVEC3 bvec3Camera;
    BRA aFov = BR_ANGLE_DEG(60.0);

    vastp4DMM.pbody->GetPartMatrix(ipart, &bmat34Part);
    if (!FGet4DMMActorStudioParentToWorld(ipart, &bmat34ParentToWorld))
        return fFalse;
    BrMatrix34Mul(&bmat34PartToWorld, &bmat34Part, &bmat34ParentToWorld);
    BrMatrix34ApplyP(&bvec3World, &bvec3Local, &bmat34PartToWorld);
    vastp4DMM.pbwld->GetCamera(&bmat34Camera, pvNil, pvNil, &aFov);
    if (BrMatrix34Inverse(&bmat34WorldToCamera, &bmat34Camera) == rZero)
        return fFalse;
    BrMatrix34ApplyP(&bvec3Camera, &bvec3World, &bmat34WorldToCamera);

    // BRender's perspective camera looks down -Z. Mirror its FOV projection
    // (Matrix4PerspectiveNew) directly so no render callback/private BACT is
    // required just to recover a screen point.
    const float z = BrScalarToFloat(bvec3Camera.v[2]);
    if (z >= -0.0001f)
        return fFalse;
    const BRA aHalfFov = (BRA)(aFov / 2);
    const BRS rSin = BR_SIN(aHalfFov);
    if (rSin == rZero)
        return fFalse;
    const float flScale = BrScalarToFloat(BR_DIV(BR_COS(aHalfFov), rSin));
    const float flAspect = (float)dxp / (float)dyp;
    const float flInvDepth = 1.0f / -z;
    const float xNdc = BrScalarToFloat(bvec3Camera.v[0]) * flInvDepth *
                       (flScale / flAspect);
    const float yNdc = BrScalarToFloat(bvec3Camera.v[1]) * flInvDepth * flScale;

    ppt->x = (LONG)(((xNdc + 1.0f) * 0.5f) * (float)dxp);
    ppt->y = (LONG)(((1.0f - yNdc) * 0.5f) * (float)dyp);
    // A cut Part can legitimately have been parked partly offscreen. Keep the
    // cursor inside the ASV so placement always begins in a controllable spot.
    ppt->x = LwMax(1L, LwMin((int32_t)ppt->x, dxp - 2));
    ppt->y = LwMax(1L, LwMin((int32_t)ppt->y, dyp - 2));
    return fTrue;
}

static bool FUpdate4DMMActorStudioSpawnCursor(int32_t xp, int32_t yp)
{
    if (!vf4DMMActorStudioSpawnPlacement || !vastp4DMM.fPartTracking ||
        vhwnd4DMMActorStudioViewport == hNil ||
        !IsWindow(vhwnd4DMMActorStudioViewport))
        return fFalse;

    // Accumulate an unbounded logical cursor before any physical recenter.
    // FCompute4DMMActorStudioDragMatrix still sees one ordinary absolute drag
    // from ptPartTrackStart, while the real hidden cursor can be warped forever.
    const int32_t dxpMove = xp - vpt4DMMActorStudioSpawnCursorLast.x;
    const int32_t dypMove = yp - vpt4DMMActorStudioSpawnCursorLast.y;
    vpt4DMMActorStudioSpawnVirtual.x += dxpMove;
    vpt4DMMActorStudioSpawnVirtual.y += dypMove;
    vpt4DMMActorStudioSpawnCursorLast.x = xp;
    vpt4DMMActorStudioSpawnCursorLast.y = yp;

    if (!FUpdate4DMMActorStudioPartDrag(
            vpt4DMMActorStudioSpawnVirtual.x, vpt4DMMActorStudioSpawnVirtual.y))
        return fFalse;

    RECT rcClient;
    if (!GetClientRect(vhwnd4DMMActorStudioViewport, &rcClient))
        return fTrue;
    RECT rcSafe = rcClient;
    const int32_t kdpSpawnWarpInset = 24;
    InflateRect(&rcSafe, -kdpSpawnWarpInset, -kdpSpawnWarpInset);
    POINT ptNow = {xp, yp};
    if (PtInRect(&rcSafe, ptNow))
        return fTrue;

    POINT ptCenter = {
        (rcClient.left + rcClient.right) / 2,
        (rcClient.top + rcClient.bottom) / 2};
    POINT ptScreen = ptCenter;
    if (ClientToScreen(vhwnd4DMMActorStudioViewport, &ptScreen))
    {
        vpt4DMMActorStudioSpawnCursorLast = ptCenter;
        vpappb->PositionCurs(ptScreen.x, ptScreen.y);
        MVIE::MultiLog(vpmvie4DMMActorStudio,
            "actor_studio_part_spawn cursor_warp edge=%ld,%ld virtual=%ld,%ld center=%ld,%ld",
            (long)xp, (long)yp,
            (long)vpt4DMMActorStudioSpawnVirtual.x,
            (long)vpt4DMMActorStudioSpawnVirtual.y,
            (long)ptCenter.x, (long)ptCenter.y);
    }
    return fTrue;
}

static void End4DMMActorStudioSpawnCursor(bool fWarpToPart)
{
    ClipCursor(pvNil);

    POINT ptClient;
    bool fHavePartPoint = fFalse;
    if (fWarpToPart && FIn(vastp4DMM.ipartSelected, 0,
                           vastp4DMM.pbody != pvNil ? vastp4DMM.pbody->Cpart() : 0))
        fHavePartPoint = FGet4DMMActorStudioPartScreenCenter(
            vastp4DMM.ipartSelected, &ptClient);

    if (fHavePartPoint && vhwnd4DMMActorStudioViewport != hNil &&
        IsWindow(vhwnd4DMMActorStudioViewport))
    {
        POINT ptScreen = ptClient;
        if (ClientToScreen(vhwnd4DMMActorStudioViewport, &ptScreen))
            vpappb->PositionCurs(ptScreen.x, ptScreen.y);
    }

    if (vf4DMMActorStudioSpawnCursorHidden)
    {
        vpappb->ShowCurs();
        vf4DMMActorStudioSpawnCursorHidden = fFalse;
    }
}

static bool FCompute4DMMActorStudioDragMatrix(int32_t xp, int32_t yp, BMAT34 *pbmat34)
{
    AssertVarMem(pbmat34);
    if (!vastp4DMM.fPartTracking || vastp4DMM.pbody == pvNil ||
        !FIn(vastp4DMM.ipartTrack, 0, vastp4DMM.pbody->Cpart()))
        return fFalse;

    *pbmat34 = vastp4DMM.bmat34PartTrackStart;
    const int32_t dxp = xp - vastp4DMM.ptPartTrackStart.x;
    const int32_t dyp = yp - vastp4DMM.ptPartTrackStart.y;

    if (vastp4DMM.modePartTool == kastReposition)
    {
        // Match the ordinary 3DMM hand tool's response: 0.02 world units per
        // mouse pixel, scaled by camera distance / 100 and the historical 1.1
        // factor. The XZ/XY choice mirrors 3DMM's axis toggle, but here it is
        // exposed explicitly as Up/Down Movement.
        float flZoom = vastp4DMM.flZoom;
        if (flZoom < 0.15f)
            flZoom = 0.15f;
        const float flDistance = BrScalarToFloat(vastp4DMM.zrCameraBase) / flZoom;
        float flDepthFactor = flDistance / 100.0f;
        if (flDepthFactor < 1.0f)
            flDepthFactor = 1.0f;
        const float flPerPixel = 0.02f * flDepthFactor * 1.1f;

        BVEC3 bvec3Camera;
        BVEC3 bvec3Root;
        BVEC3 bvec3Local;
        bvec3Camera.v[0] = BrFloatToScalar((float)dxp * flPerPixel);
        if (vastp4DMM.fVerticalMovement)
        {
            bvec3Camera.v[1] = BrFloatToScalar((float)-dyp * flPerPixel);
            bvec3Camera.v[2] = rZero;
        }
        else
        {
            bvec3Camera.v[1] = rZero;
            bvec3Camera.v[2] = BrFloatToScalar((float)dyp * flPerPixel);
        }

        BMAT34 bmat34Yaw;
        BrMatrix34RotateY(&bmat34Yaw,
            BrDegreeToAngle(BrFloatToScalar(vastp4DMM.flYaw)));
        BrMatrix34ApplyV(&bvec3Root, &bvec3Camera, &bmat34Yaw);

        BMAT34 bmat34ParentToWorld;
        BMAT34 bmat34WorldToParent;
        if (!FGet4DMMActorStudioParentToWorld(vastp4DMM.ipartTrack, &bmat34ParentToWorld) ||
            BrMatrix34Inverse(&bmat34WorldToParent, &bmat34ParentToWorld) == rZero)
            return fFalse;
        BrMatrix34ApplyV(&bvec3Local, &bvec3Root, &bmat34WorldToParent);
        pbmat34->m[3][0] += bvec3Local.v[0];
        pbmat34->m[3][1] += bvec3Local.v[1];
        pbmat34->m[3][2] += bvec3Local.v[2];
        return fTrue;
    }

    const int32_t dyrMouse = vastp4DMM.ptPartTrackStart.y - yp;
    if (vastp4DMM.modePartTool == kastGrowShrink ||
        vastp4DMM.modePartTool == kastStretchSquish)
    {
        // Mirror the main 3DMM Resize/Squash-Stretch gesture. BODY-part scale
        // lives in the same per-frame matrix as its rotation/translation, so
        // derive every mouse packet from the mouse-down matrix and preserve
        // the part origin explicitly.
        float flScale = 1.0f;
        if (vastp4DMM.modePartTool == kastGrowShrink)
            flScale += (float)(dxp + dyrMouse) * 0.001f;
        else
            flScale += (float)(-dxp - dyrMouse) * 0.001f;
        if (flScale < 0.01f)
            flScale = 0.01f;
        if (flScale > 100.0f)
            flScale = 100.0f;

        const BRS xr = pbmat34->m[3][0];
        const BRS yr = pbmat34->m[3][1];
        const BRS zr = pbmat34->m[3][2];
        const BRS rScale = BrFloatToScalar(flScale);
        if (vastp4DMM.modePartTool == kastGrowShrink)
        {
            BrMatrix34PostScale(pbmat34, rScale, rScale, rScale);
        }
        else
        {
            const BRS rScaleY = BrsDiv(rOne, rScale);
            BrMatrix34PostScale(pbmat34, rScale, rScaleY, rScale);
        }
        pbmat34->m[3][0] = xr;
        pbmat34->m[3][1] = yr;
        pbmat34->m[3][2] = zr;
        return fTrue;
    }

    // Use the same drag scale and signs as MVU::_MouseDrag for the three
    // original 3DMM rotate tools. Working from the mouse-down matrix on every
    // packet avoids cumulative fixed-point drift during a long gesture.
    const BRS brs = BrFloatToScalar((float)(dxp + dyrMouse) * -0.001f);
    BRA a = BrScalarToAngle(brs);
    if (vastp4DMM.modePartTool == kastYaw)
        a = -a;

    // A BODY part normally rotates around its local origin. For articulated
    // handmade objects that origin can lie at the wrong end of a limb, so
    // Actor Studio can hold one model-bound edge fixed as the hinge. The edge
    // choice is per part and per rotation axis; Toggle Rotation Edge simply
    // flips between the two opposite extrema.
    BVEC3 bvec3PivotLocal;
    BVEC3 bvec3PivotParent;
    const bool fEdgePivot = FGet4DMMActorStudioRotationEdgePivot(
        vastp4DMM.modePartTool, vastp4DMM.ipartTrack, &bvec3PivotLocal);
    const BRS xr = pbmat34->m[3][0];
    const BRS yr = pbmat34->m[3][1];
    const BRS zr = pbmat34->m[3][2];

    if (fEdgePivot)
    {
        // Final TDT hinge experiment: move the whole part transform around an
        // explicit pivot in *parent* space, using the same translate /
        // PostRotate / translate-back construction that already works for
        // Actor Studio Object/OG rotations. v149's local PreRotate plus
        // post-hoc translation compensation was mathematically tidy but did
        // not produce a visibly planted glyph edge in the actual BODY graph.
        BrMatrix34ApplyP(&bvec3PivotParent, &bvec3PivotLocal, pbmat34);
        pbmat34->m[3][0] -= bvec3PivotParent.v[0];
        pbmat34->m[3][1] -= bvec3PivotParent.v[1];
        pbmat34->m[3][2] -= bvec3PivotParent.v[2];
        if (vastp4DMM.modePartTool == kastPitch)
            BrMatrix34PostRotateX(pbmat34, a);
        else if (vastp4DMM.modePartTool == kastRoll)
            BrMatrix34PostRotateZ(pbmat34, a);
        else
            return fFalse;
        pbmat34->m[3][0] += bvec3PivotParent.v[0];
        pbmat34->m[3][1] += bvec3PivotParent.v[1];
        pbmat34->m[3][2] += bvec3PivotParent.v[2];
        return fTrue;
    }

    // Default/off behavior stays byte-for-byte equivalent in semantics to the
    // established Actor Studio rotation: local-axis rotate, fixed translation.
    if (vastp4DMM.modePartTool == kastPitch)
        BrMatrix34PreRotateX(pbmat34, a);
    else if (vastp4DMM.modePartTool == kastYaw)
        BrMatrix34PreRotateY(pbmat34, a);
    else if (vastp4DMM.modePartTool == kastRoll)
        BrMatrix34PreRotateZ(pbmat34, a);
    else
        return fFalse;
    pbmat34->m[3][0] = xr;
    pbmat34->m[3][1] = yr;
    pbmat34->m[3][2] = zr;
    return fTrue;
}

static bool FCompute4DMMActorStudioRangeDragMatrices(int32_t xp, int32_t yp,
                                                       BMAT34 *prgbmat34)
{
    if (prgbmat34 == pvNil || !vastp4DMM.fPartTracking || vastp4DMM.pbody == pvNil ||
        vastp4DMM.cpartTrack <= 0 || vastp4DMM.cpartTrack > kc4DMMActorStudioFramePartMax)
        return fFalse;

    if (vastp4DMM.fTrackOwnGeometryOnly)
    {
        // A model-bearing BODY parent is simultaneously a physical Part and a
        // PG handle. When its *Part* row is selected, edit only that node's own
        // geometry. Moving the parent transform normally drags every child, so
        // counter-transform each direct child: C' * P' = C * P. Descendants of
        // those children remain stable automatically. Selecting the PG row does
        // not enter this path and therefore keeps normal grouped movement.
        if (!FCompute4DMMActorStudioDragMatrix(xp, yp, &prgbmat34[0]))
            return fFalse;
        BMAT34 bmat34NewParentInv;
        if (BrMatrix34Inverse(&bmat34NewParentInv, &prgbmat34[0]) == rZero)
            return fFalse;
        const BMAT34 &bmat34OldParent = vastp4DMM.rgbmat34PartTrackStart[0];
        const int32_t ipartParent = vastp4DMM.rgipartTrack[0];
        for (int32_t i = 1; i < vastp4DMM.cpartTrack; ++i)
        {
            if (vastp4DMM.pbody->IpartParent(vastp4DMM.rgipartTrack[i]) != ipartParent)
                return fFalse;
            BMAT34 bmat34ChildThroughOldParent;
            BrMatrix34Mul(&bmat34ChildThroughOldParent,
                          &vastp4DMM.rgbmat34PartTrackStart[i], &bmat34OldParent);
            BrMatrix34Mul(&prgbmat34[i], &bmat34ChildThroughOldParent,
                          &bmat34NewParentInv);
        }
        return fTrue;
    }

    if (vastp4DMM.cpartTrack == 1)
        return FCompute4DMMActorStudioDragMatrix(xp, yp, &prgbmat34[0]);

    // Imported Object/OG nodes are synthesized as root-level BODY siblings.
    // Keeping grouped manipulation at that level means one common transform
    // can be applied to every constituent part without inventing a second
    // hidden hierarchy underneath BODY.
    for (int32_t i = 0; i < vastp4DMM.cpartTrack; ++i)
    {
        const int32_t ipart = vastp4DMM.rgipartTrack[i];
        if (!FIn(ipart, 0, vastp4DMM.pbody->Cpart()) || vastp4DMM.pbody->IpartParent(ipart) != ivNil)
            return fFalse;
        prgbmat34[i] = vastp4DMM.rgbmat34PartTrackStart[i];
    }

    const int32_t dxp = xp - vastp4DMM.ptPartTrackStart.x;
    const int32_t dyp = yp - vastp4DMM.ptPartTrackStart.y;
    if (vastp4DMM.modePartTool == kastReposition)
    {
        float flZoom = vastp4DMM.flZoom;
        if (flZoom < 0.15f)
            flZoom = 0.15f;
        const float flDistance = BrScalarToFloat(vastp4DMM.zrCameraBase) / flZoom;
        float flDepthFactor = flDistance / 100.0f;
        if (flDepthFactor < 1.0f)
            flDepthFactor = 1.0f;
        const float flPerPixel = 0.02f * flDepthFactor * 1.1f;

        BVEC3 bvec3Camera;
        BVEC3 bvec3World;
        BVEC3 bvec3Root;
        bvec3Camera.v[0] = BrFloatToScalar((float)dxp * flPerPixel);
        if (vastp4DMM.fVerticalMovement)
        {
            bvec3Camera.v[1] = BrFloatToScalar((float)-dyp * flPerPixel);
            bvec3Camera.v[2] = rZero;
        }
        else
        {
            bvec3Camera.v[1] = rZero;
            bvec3Camera.v[2] = BrFloatToScalar((float)dyp * flPerPixel);
        }
        BMAT34 bmat34Yaw;
        BrMatrix34RotateY(&bmat34Yaw,
            BrDegreeToAngle(BrFloatToScalar(vastp4DMM.flYaw)));
        BrMatrix34ApplyV(&bvec3World, &bvec3Camera, &bmat34Yaw);

        BMAT34 bmat34RootToWorld;
        BMAT34 bmat34WorldToRoot;
        vastp4DMM.pbody->GetMatrix(&bmat34RootToWorld);
        if (BrMatrix34Inverse(&bmat34WorldToRoot, &bmat34RootToWorld) == rZero)
            return fFalse;
        BrMatrix34ApplyV(&bvec3Root, &bvec3World, &bmat34WorldToRoot);
        for (int32_t i = 0; i < vastp4DMM.cpartTrack; ++i)
        {
            prgbmat34[i].m[3][0] += bvec3Root.v[0];
            prgbmat34[i].m[3][1] += bvec3Root.v[1];
            prgbmat34[i].m[3][2] += bvec3Root.v[2];
        }
        return fTrue;
    }

    const int32_t dyrMouse = vastp4DMM.ptPartTrackStart.y - yp;
    const BRS xrPivot = vastp4DMM.bvec3TrackPivot.v[0];
    const BRS yrPivot = vastp4DMM.bvec3TrackPivot.v[1];
    const BRS zrPivot = vastp4DMM.bvec3TrackPivot.v[2];

    if (vastp4DMM.modePartTool == kastGrowShrink ||
        vastp4DMM.modePartTool == kastStretchSquish)
    {
        float flScale = 1.0f;
        if (vastp4DMM.modePartTool == kastGrowShrink)
            flScale += (float)(dxp + dyrMouse) * 0.001f;
        else
            flScale += (float)(-dxp - dyrMouse) * 0.001f;
        if (flScale < 0.01f)
            flScale = 0.01f;
        if (flScale > 100.0f)
            flScale = 100.0f;
        const BRS rScale = BrFloatToScalar(flScale);
        const BRS rScaleY = BrsDiv(rOne, rScale);
        for (int32_t i = 0; i < vastp4DMM.cpartTrack; ++i)
        {
            prgbmat34[i].m[3][0] -= xrPivot;
            prgbmat34[i].m[3][1] -= yrPivot;
            prgbmat34[i].m[3][2] -= zrPivot;
            if (vastp4DMM.modePartTool == kastGrowShrink)
                BrMatrix34PostScale(&prgbmat34[i], rScale, rScale, rScale);
            else
                BrMatrix34PostScale(&prgbmat34[i], rScale, rScaleY, rScale);
            prgbmat34[i].m[3][0] += xrPivot;
            prgbmat34[i].m[3][1] += yrPivot;
            prgbmat34[i].m[3][2] += zrPivot;
        }
        return fTrue;
    }

    const BRS brs = BrFloatToScalar((float)(dxp + dyrMouse) * -0.001f);
    BRA a = BrScalarToAngle(brs);
    if (vastp4DMM.modePartTool == kastYaw)
        a = -a;
    if (vastp4DMM.modePartTool != kastPitch && vastp4DMM.modePartTool != kastYaw &&
        vastp4DMM.modePartTool != kastRoll)
        return fFalse;

    for (int32_t i = 0; i < vastp4DMM.cpartTrack; ++i)
    {
        prgbmat34[i].m[3][0] -= xrPivot;
        prgbmat34[i].m[3][1] -= yrPivot;
        prgbmat34[i].m[3][2] -= zrPivot;
        if (vastp4DMM.modePartTool == kastPitch)
            BrMatrix34PostRotateX(&prgbmat34[i], a);
        else if (vastp4DMM.modePartTool == kastYaw)
            BrMatrix34PostRotateY(&prgbmat34[i], a);
        else
            BrMatrix34PostRotateZ(&prgbmat34[i], a);
        prgbmat34[i].m[3][0] += xrPivot;
        prgbmat34[i].m[3][1] += yrPivot;
        prgbmat34[i].m[3][2] += zrPivot;
    }
    return fTrue;
}

static int32_t CCollect4DMMActorStudioRangeRoots(int32_t ipartFirst, int32_t cpart,
                                                   int32_t *prgipart, int32_t cpartMax)
{
    if (vastp4DMM.pbody == pvNil || prgipart == pvNil || cpartMax <= 0 || cpart <= 0)
        return 0;
    const int32_t ipartLim = ipartFirst + cpart;
    int32_t croot = 0;
    for (int32_t ipart = ipartFirst; ipart < ipartLim; ++ipart)
    {
        if (!FIn(ipart, 0, vastp4DMM.pbody->Cpart()))
            continue;
        const int32_t ipartParent = vastp4DMM.pbody->IpartParent(ipart);
        if (ipartParent != ivNil && FIn(ipartParent, ipartFirst, ipartLim))
            continue;
        if (croot >= cpartMax)
            return 0;
        prgipart[croot++] = ipart;
    }
    return croot;
}

static int32_t CCollect4DMMActorStudioOwnGeometryTrackParts(
    int32_t ipart, int32_t *prgipart, int32_t cpartMax)
{
    if (vastp4DMM.pbody == pvNil || prgipart == pvNil || cpartMax <= 0 ||
        !FIn(ipart, 0, vastp4DMM.pbody->Cpart()))
        return 0;
    int32_t ctrack = 0;
    prgipart[ctrack++] = ipart;
    for (int32_t ipartChild = 0; ipartChild < vastp4DMM.pbody->Cpart(); ++ipartChild)
    {
        if (vastp4DMM.pbody->IpartParent(ipartChild) != ipart)
            continue;
        if (ctrack >= cpartMax)
            return 0;
        prgipart[ctrack++] = ipartChild;
    }
    return ctrack;
}

static int32_t CBuild4DMMActorStudioOwnGeometryMatrices(
    int32_t ipart, const BMAT34 *pbmat34NewParent, int32_t *prgipart,
    BMAT34 *prgbmat34, int32_t cpartMax)
{
    if (vastp4DMM.pbody == pvNil || pbmat34NewParent == pvNil ||
        prgipart == pvNil || prgbmat34 == pvNil)
        return 0;
    const int32_t ctrack = CCollect4DMMActorStudioOwnGeometryTrackParts(
        ipart, prgipart, cpartMax);
    if (ctrack <= 0)
        return 0;

    BMAT34 bmat34OldParent;
    BMAT34 bmat34NewParentInv;
    vastp4DMM.pbody->GetPartMatrix(ipart, &bmat34OldParent);
    if (BrMatrix34Inverse(&bmat34NewParentInv, pbmat34NewParent) == rZero)
        return 0;
    prgbmat34[0] = *pbmat34NewParent;
    for (int32_t i = 1; i < ctrack; ++i)
    {
        BMAT34 bmat34OldChild;
        BMAT34 bmat34ChildThroughOldParent;
        vastp4DMM.pbody->GetPartMatrix(prgipart[i], &bmat34OldChild);
        BrMatrix34Mul(&bmat34ChildThroughOldParent, &bmat34OldChild, &bmat34OldParent);
        BrMatrix34Mul(&prgbmat34[i], &bmat34ChildThroughOldParent,
                      &bmat34NewParentInv);
    }
    return ctrack;
}

static bool F4DMMActorStudioGroupRotationBaselineMatches(
    int32_t anid, int32_t celn, int32_t selectionKind,
    int32_t ipartSelectionFirst, int32_t cpartSelection,
    const int32_t *prgipart, int32_t cpartRoots)
{
    if (!vastp4DMM.fGroupRotationBaselineValid || prgipart == pvNil || cpartRoots <= 0 ||
        vastp4DMM.anidGroupRotationBaseline != anid ||
        vastp4DMM.celnGroupRotationBaseline != celn ||
        vastp4DMM.kindGroupRotationBaseline != selectionKind ||
        vastp4DMM.ipartGroupRotationBaselineFirst != ipartSelectionFirst ||
        vastp4DMM.cpartGroupRotationBaselineSelection != cpartSelection ||
        vastp4DMM.cpartGroupRotationBaselineRoots != cpartRoots)
        return fFalse;
    for (int32_t i = 0; i < cpartRoots; ++i)
        if (vastp4DMM.rgipartGroupRotationBaseline[i] != prgipart[i])
            return fFalse;
    return fTrue;
}

static void Capture4DMMActorStudioGroupRotationBaseline(
    int32_t anid, int32_t celn, int32_t selectionKind,
    int32_t ipartSelectionFirst, int32_t cpartSelection,
    const int32_t *prgipart, const BMAT34 *prgbmat34, int32_t cpartRoots)
{
    if (prgipart == pvNil || prgbmat34 == pvNil || cpartRoots <= 0 ||
        cpartRoots > kc4DMMActorStudioFramePartMax)
        return;
    vastp4DMM.fGroupRotationBaselineValid = fTrue;
    vastp4DMM.anidGroupRotationBaseline = anid;
    vastp4DMM.celnGroupRotationBaseline = celn;
    vastp4DMM.kindGroupRotationBaseline = selectionKind;
    vastp4DMM.ipartGroupRotationBaselineFirst = ipartSelectionFirst;
    vastp4DMM.cpartGroupRotationBaselineSelection = cpartSelection;
    vastp4DMM.cpartGroupRotationBaselineRoots = cpartRoots;
    CopyPb(prgipart, vastp4DMM.rgipartGroupRotationBaseline,
           LwMul(cpartRoots, SIZEOF(int32_t)));
    CopyPb(prgbmat34, vastp4DMM.rgbmat34GroupRotationBaseline,
           LwMul(cpartRoots, SIZEOF(BMAT34)));
}

static void Invalidate4DMMActorStudioGroupRotationBaseline(void)
{
    vastp4DMM.fGroupRotationBaselineValid = fFalse;
    vastp4DMM.cpartGroupRotationBaselineRoots = 0;
}

static bool FBegin4DMMActorStudioPartDrag(HWND hwnd, int32_t xp, int32_t yp,
                                              bool fForceSelected)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil || vastp4DMM.pbody == pvNil ||
        vastp4DMM.anid < 0 || vastp4DMM.celn < 0 ||
        vastp4DMM.modePartTool == kastDelete ||
        !pmvie->FActorStudioActionIsCustom(pactr, vastp4DMM.anid))
        return fFalse;

    int32_t ipart = ivNil;
    if (fForceSelected)
    {
        if (!FIn(vastp4DMM.ipartSelected, 0, vastp4DMM.pbody->Cpart()))
            return fFalse;
        ipart = vastp4DMM.ipartSelected;
    }
    else if (!FPick4DMMActorStudioPart(xp, yp, &ipart))
        return fFalse;

    // Preserve an explicitly selected Object/OG while clicking one of its
    // members. A selected default PG is also sticky across clicks on any of its
    // descendant Parts so the viewport does not silently collapse PG editing
    // back to the leaf Part representation of the same BODY hierarchy.
    const bool fKeepRange = vastp4DMM.cpartSelection > 1 &&
        FIn(ipart, vastp4DMM.ipartSelectionFirst,
            vastp4DMM.ipartSelectionFirst + vastp4DMM.cpartSelection);
    const bool fKeepDefaultGroup = vastp4DMM.kindSelected == kastnDefaultPartGroup &&
        !F4DMMActorStudioSelectedDefaultPartGroupExpanded() &&
        FIn(vastp4DMM.ipartSelected, 0, vastp4DMM.pbody->Cpart()) &&
        (ipart == vastp4DMM.ipartSelected ||
         F4DMMActorStudioPartDescendsFrom(ipart, vastp4DMM.ipartSelected));
    if (!fForceSelected && !fKeepRange && !fKeepDefaultGroup)
        Select4DMMActorStudioPartRow(ipart, fTrue);

    int32_t ipartFirst = vastp4DMM.ipartSelectionFirst;
    int32_t cpartSelection = vastp4DMM.cpartSelection;
    if (cpartSelection <= 0 || !FIn(ipartFirst, 0, vastp4DMM.pbody->Cpart()) ||
        ipartFirst + cpartSelection > vastp4DMM.pbody->Cpart())
    {
        ipartFirst = ipart;
        cpartSelection = 1;
    }

    vastp4DMM.fTrackOwnGeometryOnly = fFalse;
    int32_t cpartTrack = 0;
    if (cpartSelection == 1 && vastp4DMM.kindSelected == kastnPart &&
        F4DMMActorStudioDefaultPartIsPart(ipartFirst) &&
        F4DMMActorStudioDefaultPartIsGroup(ipartFirst))
    {
        // The physical Part alias of a model-bearing PG edits only the model on
        // this BODY node. Its direct children are tracked solely so they can be
        // counter-transformed and remain visually stationary.
        cpartTrack = CCollect4DMMActorStudioOwnGeometryTrackParts(
            ipartFirst, vastp4DMM.rgipartTrack, kc4DMMActorStudioFramePartMax);
        vastp4DMM.fTrackOwnGeometryOnly = cpartTrack > 0;
    }
    else
    {
        // Object and OG rows are contiguous BODY ranges, but v172 correctly
        // restored the native child hierarchy inside each imported object.
        // Transform only the top-most nodes of the selected range. Their BODY
        // descendants follow naturally and are never double-transformed.
        cpartTrack = CCollect4DMMActorStudioRangeRoots(
            ipartFirst, cpartSelection, vastp4DMM.rgipartTrack,
            kc4DMMActorStudioFramePartMax);
    }
    if (cpartTrack <= 0 || cpartTrack > kc4DMMActorStudioFramePartMax)
        return fFalse;

    double xrPivot = 0.0;
    double yrPivot = 0.0;
    double zrPivot = 0.0;
    for (int32_t i = 0; i < cpartTrack; ++i)
    {
        const int32_t ipartT = vastp4DMM.rgipartTrack[i];
        vastp4DMM.pbody->GetPartMatrix(ipartT, &vastp4DMM.rgbmat34PartTrackStart[i]);
        // Own-geometry compensation children are not independent transform
        // roots and must not skew the edit pivot away from their parent Part.
        if (!vastp4DMM.fTrackOwnGeometryOnly || i == 0)
        {
            xrPivot += BrScalarToFloat(vastp4DMM.rgbmat34PartTrackStart[i].m[3][0]);
            yrPivot += BrScalarToFloat(vastp4DMM.rgbmat34PartTrackStart[i].m[3][1]);
            zrPivot += BrScalarToFloat(vastp4DMM.rgbmat34PartTrackStart[i].m[3][2]);
        }
    }
    const bool fRotationTool = vastp4DMM.modePartTool == kastPitch ||
                               vastp4DMM.modePartTool == kastYaw ||
                               vastp4DMM.modePartTool == kastRoll;
    const bool fGroupedRotation = fRotationTool && !vastp4DMM.fTrackOwnGeometryOnly &&
                                  (cpartSelection > 1 || vastp4DMM.kindSelected != kastnPart);
    if (fGroupedRotation)
    {
        if (!F4DMMActorStudioGroupRotationBaselineMatches(
                vastp4DMM.anid, vastp4DMM.celn, vastp4DMM.kindSelected,
                ipartFirst, cpartSelection, vastp4DMM.rgipartTrack, cpartTrack))
        {
            Capture4DMMActorStudioGroupRotationBaseline(
                vastp4DMM.anid, vastp4DMM.celn, vastp4DMM.kindSelected,
                ipartFirst, cpartSelection, vastp4DMM.rgipartTrack,
                vastp4DMM.rgbmat34PartTrackStart, cpartTrack);
            MVIE::MultiLog(pmvie,
                "actor_studio_group_rotation_baseline capture action=%ld cel=%ld kind=%ld first=%ld selected_parts=%ld roots=%ld",
                (long)vastp4DMM.anid, (long)vastp4DMM.celn,
                (long)vastp4DMM.kindSelected, (long)ipartFirst,
                (long)cpartSelection, (long)cpartTrack);
        }
    }
    else if (vastp4DMM.modePartTool == kastGrowShrink ||
             vastp4DMM.modePartTool == kastStretchSquish ||
             fRotationTool ||
             (vastp4DMM.modePartTool == kastReposition &&
              vastp4DMM.fGroupRotationBaselineValid &&
              !F4DMMActorStudioGroupRotationBaselineMatches(
                  vastp4DMM.anid, vastp4DMM.celn, vastp4DMM.kindSelected,
                  ipartFirst, cpartSelection, vastp4DMM.rgipartTrack, cpartTrack)))
    {
        // A scale edit, a different/single rotation, or repositioning a
        // different subset changes what the remembered group baseline means.
        // Repositioning the same whole group deliberately keeps the baseline
        // so Reset Rotation can preserve that later whole-group translation.
        Invalidate4DMMActorStudioGroupRotationBaseline();
    }

    const int32_t cpartPivot = vastp4DMM.fTrackOwnGeometryOnly ? 1 : cpartTrack;
    vastp4DMM.ipartTrack = vastp4DMM.rgipartTrack[0];
    vastp4DMM.cpartTrack = cpartTrack;
    vastp4DMM.bmat34PartTrackStart = vastp4DMM.rgbmat34PartTrackStart[0];
    vastp4DMM.bvec3TrackPivot.v[0] = BrFloatToScalar((float)(xrPivot / cpartPivot));
    vastp4DMM.bvec3TrackPivot.v[1] = BrFloatToScalar((float)(yrPivot / cpartPivot));
    vastp4DMM.bvec3TrackPivot.v[2] = BrFloatToScalar((float)(zrPivot / cpartPivot));
    vastp4DMM.anidTrack = vastp4DMM.anid;
    vastp4DMM.celnTrack = vastp4DMM.celn;
    vastp4DMM.ptPartTrackStart.x = xp;
    vastp4DMM.ptPartTrackStart.y = yp;
    vastp4DMM.fPartTracking = fTrue;
    SetCapture(hwnd);

    MVIE::MultiLog(pmvie,
        "actor_studio_part_drag begin mode=%ld action=%ld cel=%ld first=%ld selected_parts=%ld transform_parts=%ld kind=%ld own_geometry_only=%d xy=%ld,%ld vertical=%d force_selected=%d",
        (long)vastp4DMM.modePartTool, (long)vastp4DMM.anidTrack,
        (long)vastp4DMM.celnTrack, (long)ipartFirst, (long)cpartSelection,
        (long)cpartTrack, (long)vastp4DMM.kindSelected,
        (int)vastp4DMM.fTrackOwnGeometryOnly, (long)xp, (long)yp,
        (int)vastp4DMM.fVerticalMovement, (int)fForceSelected);
    return fTrue;
}

static bool FUpdate4DMMActorStudioPartDrag(int32_t xp, int32_t yp)
{
    BMAT34 rgbmat34[kc4DMMActorStudioFramePartMax];
    if (!FCompute4DMMActorStudioRangeDragMatrices(xp, yp, rgbmat34))
        return fFalse;
    for (int32_t i = 0; i < vastp4DMM.cpartTrack; ++i)
        vastp4DMM.pbody->SetPartMatrix(vastp4DMM.rgipartTrack[i], &rgbmat34[i]);
    Apply4DMMActorStudioSelectionHilite();
    Render4DMMActorStudioPreview();
    return fTrue;
}

static bool FEnd4DMMActorStudioPartDrag(int32_t xp, int32_t yp, bool fCommit)
{
    if (!vastp4DMM.fPartTracking)
        return fFalse;

    BMAT34 rgbmat34[kc4DMMActorStudioFramePartMax];
    const bool fHaveMatrices = FCompute4DMMActorStudioRangeDragMatrices(xp, yp, rgbmat34);
    const int32_t cpart = vastp4DMM.cpartTrack;
    const int32_t ipartFirst = vastp4DMM.ipartTrack;
    const int32_t anid = vastp4DMM.anidTrack;
    const int32_t celn = vastp4DMM.celnTrack;
    int32_t rgipart[kc4DMMActorStudioFramePartMax];
    BMAT34 rgbmat34Start[kc4DMMActorStudioFramePartMax];
    for (int32_t i = 0; i < cpart; ++i)
    {
        rgipart[i] = vastp4DMM.rgipartTrack[i];
        rgbmat34Start[i] = vastp4DMM.rgbmat34PartTrackStart[i];
    }
    vastp4DMM.fPartTracking = fFalse;
    vastp4DMM.fTrackOwnGeometryOnly = fFalse;
    vastp4DMM.ipartTrack = ivNil;
    vastp4DMM.cpartTrack = 0;
    vastp4DMM.anidTrack = ivNil;
    vastp4DMM.celnTrack = ivNil;
    if (GetCapture() == vhwnd4DMMActorStudioViewport)
        ReleaseCapture();

    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    bool fSaved = fFalse;
    if (fCommit && fHaveMatrices && pmvie != pvNil && pactr != pvNil && cpart > 0)
    {
        fSaved = cpart == 1 ?
            pmvie->FActorStudioSetPartMatrix(
                pactr, anid, celn, rgipart[0], &rgbmat34[0],
                vastp4DMM.kindSelected, vastp4DMM.ipartSelectionFirst,
                vastp4DMM.cpartSelection) :
            pmvie->FActorStudioSetPartMatrices(
                pactr, anid, celn, rgipart, rgbmat34, cpart,
                vastp4DMM.kindSelected, vastp4DMM.ipartSelectionFirst,
                vastp4DMM.cpartSelection);
    }

    if (vastp4DMM.pbody != pvNil)
    {
        for (int32_t i = 0; i < cpart; ++i)
            vastp4DMM.pbody->SetPartMatrix(rgipart[i], fSaved ? &rgbmat34[i] : &rgbmat34Start[i]);
        Apply4DMMActorStudioSelectionHilite();
    }
    Render4DMMActorStudioPreview();
    Update4DMMActorStudioButtons();

    MVIE::MultiLog(pmvie,
        "actor_studio_part_drag end mode=%ld action=%ld cel=%ld first=%ld parts=%ld xy=%ld,%ld commit=%d saved=%d",
        (long)vastp4DMM.modePartTool, (long)anid, (long)celn, (long)ipartFirst,
        (long)cpart, (long)xp, (long)yp, (int)fCommit, (int)fSaved);
    return fSaved;
}

static bool FReset4DMMActorStudioPartRotation(void)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil || vastp4DMM.pbody == pvNil ||
        vastp4DMM.cpartSelection <= 0 ||
        !FIn(vastp4DMM.ipartSelectionFirst, 0, vastp4DMM.pbody->Cpart()) ||
        vastp4DMM.ipartSelectionFirst + vastp4DMM.cpartSelection > vastp4DMM.pbody->Cpart() ||
        vastp4DMM.anid < 0 || vastp4DMM.celn < 0 ||
        !pmvie->FActorStudioActionIsCustom(pactr, vastp4DMM.anid))
        return fFalse;

    int32_t rgipart[kc4DMMActorStudioFramePartMax];
    BMAT34 rgbmat34[kc4DMMActorStudioFramePartMax];
    int32_t cpartTransform = 0;
    const bool fOwnGeometryOnly = vastp4DMM.cpartSelection == 1 &&
        vastp4DMM.kindSelected == kastnPart &&
        F4DMMActorStudioDefaultPartIsPart(vastp4DMM.ipartSelectionFirst) &&
        F4DMMActorStudioDefaultPartIsGroup(vastp4DMM.ipartSelectionFirst);

    if (fOwnGeometryOnly)
    {
        BMAT34 bmat34New;
        vastp4DMM.pbody->GetPartMatrix(vastp4DMM.ipartSelectionFirst, &bmat34New);
        const BRS xr = bmat34New.m[3][0];
        const BRS yr = bmat34New.m[3][1];
        const BRS zr = bmat34New.m[3][2];
        double rgScale[3];
        for (int32_t iaxis = 0; iaxis < 3; ++iaxis)
        {
            const double x = BrScalarToFloat(bmat34New.m[iaxis][0]);
            const double y = BrScalarToFloat(bmat34New.m[iaxis][1]);
            const double z = BrScalarToFloat(bmat34New.m[iaxis][2]);
            rgScale[iaxis] = std::sqrt(x * x + y * y + z * z);
            if (rgScale[iaxis] < 0.000001)
                rgScale[iaxis] = 1.0;
        }
        BrMatrix34Identity(&bmat34New);
        bmat34New.m[0][0] = BrFloatToScalar((float)rgScale[0]);
        bmat34New.m[1][1] = BrFloatToScalar((float)rgScale[1]);
        bmat34New.m[2][2] = BrFloatToScalar((float)rgScale[2]);
        bmat34New.m[3][0] = xr;
        bmat34New.m[3][1] = yr;
        bmat34New.m[3][2] = zr;
        cpartTransform = CBuild4DMMActorStudioOwnGeometryMatrices(
            vastp4DMM.ipartSelectionFirst, &bmat34New, rgipart, rgbmat34,
            kc4DMMActorStudioFramePartMax);
    }
    else
    {
        cpartTransform = CCollect4DMMActorStudioRangeRoots(
            vastp4DMM.ipartSelectionFirst, vastp4DMM.cpartSelection,
            rgipart, kc4DMMActorStudioFramePartMax);
        const bool fGroupedSelection = vastp4DMM.cpartSelection > 1 ||
                                       vastp4DMM.kindSelected != kastnPart;
        if (fGroupedSelection)
        {
            if (cpartTransform <= 0 ||
                !F4DMMActorStudioGroupRotationBaselineMatches(
                    vastp4DMM.anid, vastp4DMM.celn, vastp4DMM.kindSelected,
                    vastp4DMM.ipartSelectionFirst, vastp4DMM.cpartSelection,
                    rgipart, cpartTransform))
            {
                // The old code reset every root to identity independently,
                // which destroys authored root orientation and produces the
                // "falling Jenga tower" actor. Without an in-session baseline
                // from an actual grouped rotation, doing nothing is safer than
                // inventing an orientation for unrelated BODY roots.
                MVIE::MultiLog(pmvie,
                    "actor_studio_part_reset_rotation group_baseline_missing action=%ld cel=%ld kind=%ld first=%ld selected_parts=%ld roots=%ld",
                    (long)vastp4DMM.anid, (long)vastp4DMM.celn,
                    (long)vastp4DMM.kindSelected,
                    (long)vastp4DMM.ipartSelectionFirst,
                    (long)vastp4DMM.cpartSelection, (long)cpartTransform);
                return fFalse;
            }

            double xrBaselinePivot = 0.0, yrBaselinePivot = 0.0, zrBaselinePivot = 0.0;
            double xrCurrentPivot = 0.0, yrCurrentPivot = 0.0, zrCurrentPivot = 0.0;
            BMAT34 rgbmat34Current[kc4DMMActorStudioFramePartMax];
            for (int32_t i = 0; i < cpartTransform; ++i)
            {
                vastp4DMM.pbody->GetPartMatrix(rgipart[i], &rgbmat34Current[i]);
                const BMAT34 &bmat34Base = vastp4DMM.rgbmat34GroupRotationBaseline[i];
                xrBaselinePivot += BrScalarToFloat(bmat34Base.m[3][0]);
                yrBaselinePivot += BrScalarToFloat(bmat34Base.m[3][1]);
                zrBaselinePivot += BrScalarToFloat(bmat34Base.m[3][2]);
                xrCurrentPivot += BrScalarToFloat(rgbmat34Current[i].m[3][0]);
                yrCurrentPivot += BrScalarToFloat(rgbmat34Current[i].m[3][1]);
                zrCurrentPivot += BrScalarToFloat(rgbmat34Current[i].m[3][2]);
            }
            xrBaselinePivot /= cpartTransform;
            yrBaselinePivot /= cpartTransform;
            zrBaselinePivot /= cpartTransform;
            xrCurrentPivot /= cpartTransform;
            yrCurrentPivot /= cpartTransform;
            zrCurrentPivot /= cpartTransform;

            for (int32_t i = 0; i < cpartTransform; ++i)
            {
                const BMAT34 &bmat34Base = vastp4DMM.rgbmat34GroupRotationBaseline[i];
                rgbmat34[i] = bmat34Base;

                // Preserve the current scale magnitude on each authored axis
                // while restoring that axis's authored direction. Group scale
                // edits invalidate the baseline before this point, so this is
                // principally protection for pre-existing per-root scale.
                for (int32_t iaxis = 0; iaxis < 3; ++iaxis)
                {
                    double xb = BrScalarToFloat(bmat34Base.m[iaxis][0]);
                    double yb = BrScalarToFloat(bmat34Base.m[iaxis][1]);
                    double zb = BrScalarToFloat(bmat34Base.m[iaxis][2]);
                    const double lenBase = std::sqrt(xb * xb + yb * yb + zb * zb);
                    const double xc = BrScalarToFloat(rgbmat34Current[i].m[iaxis][0]);
                    const double yc = BrScalarToFloat(rgbmat34Current[i].m[iaxis][1]);
                    const double zc = BrScalarToFloat(rgbmat34Current[i].m[iaxis][2]);
                    const double lenCurrent = std::sqrt(xc * xc + yc * yc + zc * zc);
                    if (lenBase > 0.000001 && lenCurrent > 0.000001)
                    {
                        const double scale = lenCurrent / lenBase;
                        rgbmat34[i].m[iaxis][0] = BrFloatToScalar((float)(xb * scale));
                        rgbmat34[i].m[iaxis][1] = BrFloatToScalar((float)(yb * scale));
                        rgbmat34[i].m[iaxis][2] = BrFloatToScalar((float)(zb * scale));
                    }
                }

                // Preserve any whole-group reposition that happened after the
                // baseline was captured, but restore the authored relative
                // placement of the independent roots.
                rgbmat34[i].m[3][0] = BrFloatToScalar((float)(
                    BrScalarToFloat(bmat34Base.m[3][0]) + xrCurrentPivot - xrBaselinePivot));
                rgbmat34[i].m[3][1] = BrFloatToScalar((float)(
                    BrScalarToFloat(bmat34Base.m[3][1]) + yrCurrentPivot - yrBaselinePivot));
                rgbmat34[i].m[3][2] = BrFloatToScalar((float)(
                    BrScalarToFloat(bmat34Base.m[3][2]) + zrCurrentPivot - zrBaselinePivot));
            }
        }
        else
        {
            // A literal single Part has one meaningful local orientation, so
            // the historical identity-plus-current-scale behavior is valid.
            for (int32_t i = 0; i < cpartTransform; ++i)
            {
                vastp4DMM.pbody->GetPartMatrix(rgipart[i], &rgbmat34[i]);
                const BRS xr = rgbmat34[i].m[3][0];
                const BRS yr = rgbmat34[i].m[3][1];
                const BRS zr = rgbmat34[i].m[3][2];
                double rgScale[3];
                for (int32_t iaxis = 0; iaxis < 3; ++iaxis)
                {
                    const double x = BrScalarToFloat(rgbmat34[i].m[iaxis][0]);
                    const double y = BrScalarToFloat(rgbmat34[i].m[iaxis][1]);
                    const double z = BrScalarToFloat(rgbmat34[i].m[iaxis][2]);
                    rgScale[iaxis] = std::sqrt(x * x + y * y + z * z);
                    if (rgScale[iaxis] < 0.000001)
                        rgScale[iaxis] = 1.0;
                }
                BrMatrix34Identity(&rgbmat34[i]);
                rgbmat34[i].m[0][0] = BrFloatToScalar((float)rgScale[0]);
                rgbmat34[i].m[1][1] = BrFloatToScalar((float)rgScale[1]);
                rgbmat34[i].m[2][2] = BrFloatToScalar((float)rgScale[2]);
                rgbmat34[i].m[3][0] = xr;
                rgbmat34[i].m[3][1] = yr;
                rgbmat34[i].m[3][2] = zr;
            }
        }
    }
    if (cpartTransform <= 0)
        return fFalse;

    const bool fSaved = cpartTransform == 1 ?
        pmvie->FActorStudioSetPartMatrix(
            pactr, vastp4DMM.anid, vastp4DMM.celn, rgipart[0], &rgbmat34[0],
            vastp4DMM.kindSelected, vastp4DMM.ipartSelectionFirst,
            vastp4DMM.cpartSelection) :
        pmvie->FActorStudioSetPartMatrices(
            pactr, vastp4DMM.anid, vastp4DMM.celn, rgipart, rgbmat34, cpartTransform,
            vastp4DMM.kindSelected, vastp4DMM.ipartSelectionFirst,
            vastp4DMM.cpartSelection);
    if (!fSaved)
        return fFalse;
    for (int32_t i = 0; i < cpartTransform; ++i)
        vastp4DMM.pbody->SetPartMatrix(rgipart[i], &rgbmat34[i]);
    Apply4DMMActorStudioSelectionHilite();
    Render4DMMActorStudioPreview();
    MVIE::MultiLog(pmvie,
        "actor_studio_part_reset_rotation action=%ld cel=%ld first=%ld selected_parts=%ld transform_parts=%ld own_geometry_only=%d",
        (long)vastp4DMM.anid, (long)vastp4DMM.celn,
        (long)vastp4DMM.ipartSelectionFirst, (long)vastp4DMM.cpartSelection,
        (long)cpartTransform, (int)fOwnGeometryOnly);
    return fTrue;
}

static bool FReset4DMMActorStudioPartScale(void)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil || vastp4DMM.pbody == pvNil ||
        vastp4DMM.cpartSelection <= 0 ||
        !FIn(vastp4DMM.ipartSelectionFirst, 0, vastp4DMM.pbody->Cpart()) ||
        vastp4DMM.ipartSelectionFirst + vastp4DMM.cpartSelection > vastp4DMM.pbody->Cpart() ||
        vastp4DMM.anid < 0 || vastp4DMM.celn < 0 ||
        !pmvie->FActorStudioActionIsCustom(pactr, vastp4DMM.anid))
        return fFalse;

    int32_t rgipart[kc4DMMActorStudioFramePartMax];
    BMAT34 rgbmat34[kc4DMMActorStudioFramePartMax];
    int32_t cpartTransform = 0;
    const bool fOwnGeometryOnly = vastp4DMM.cpartSelection == 1 &&
        vastp4DMM.kindSelected == kastnPart &&
        F4DMMActorStudioDefaultPartIsPart(vastp4DMM.ipartSelectionFirst) &&
        F4DMMActorStudioDefaultPartIsGroup(vastp4DMM.ipartSelectionFirst);

    if (fOwnGeometryOnly)
    {
        BMAT34 bmat34New;
        vastp4DMM.pbody->GetPartMatrix(vastp4DMM.ipartSelectionFirst, &bmat34New);
        for (int32_t iaxis = 0; iaxis < 3; ++iaxis)
        {
            const double x = BrScalarToFloat(bmat34New.m[iaxis][0]);
            const double y = BrScalarToFloat(bmat34New.m[iaxis][1]);
            const double z = BrScalarToFloat(bmat34New.m[iaxis][2]);
            const double len = std::sqrt(x * x + y * y + z * z);
            if (len > 0.000001)
            {
                bmat34New.m[iaxis][0] = BrFloatToScalar((float)(x / len));
                bmat34New.m[iaxis][1] = BrFloatToScalar((float)(y / len));
                bmat34New.m[iaxis][2] = BrFloatToScalar((float)(z / len));
            }
        }
        cpartTransform = CBuild4DMMActorStudioOwnGeometryMatrices(
            vastp4DMM.ipartSelectionFirst, &bmat34New, rgipart, rgbmat34,
            kc4DMMActorStudioFramePartMax);
    }
    else
    {
        cpartTransform = CCollect4DMMActorStudioRangeRoots(
            vastp4DMM.ipartSelectionFirst, vastp4DMM.cpartSelection,
            rgipart, kc4DMMActorStudioFramePartMax);
        for (int32_t i = 0; i < cpartTransform; ++i)
        {
            vastp4DMM.pbody->GetPartMatrix(rgipart[i], &rgbmat34[i]);
            for (int32_t iaxis = 0; iaxis < 3; ++iaxis)
            {
                const double x = BrScalarToFloat(rgbmat34[i].m[iaxis][0]);
                const double y = BrScalarToFloat(rgbmat34[i].m[iaxis][1]);
                const double z = BrScalarToFloat(rgbmat34[i].m[iaxis][2]);
                const double len = std::sqrt(x * x + y * y + z * z);
                if (len > 0.000001)
                {
                    rgbmat34[i].m[iaxis][0] = BrFloatToScalar((float)(x / len));
                    rgbmat34[i].m[iaxis][1] = BrFloatToScalar((float)(y / len));
                    rgbmat34[i].m[iaxis][2] = BrFloatToScalar((float)(z / len));
                }
            }
        }
    }
    if (cpartTransform <= 0)
        return fFalse;

    const bool fSaved = cpartTransform == 1 ?
        pmvie->FActorStudioSetPartMatrix(
            pactr, vastp4DMM.anid, vastp4DMM.celn, rgipart[0], &rgbmat34[0],
            vastp4DMM.kindSelected, vastp4DMM.ipartSelectionFirst,
            vastp4DMM.cpartSelection) :
        pmvie->FActorStudioSetPartMatrices(
            pactr, vastp4DMM.anid, vastp4DMM.celn, rgipart, rgbmat34, cpartTransform,
            vastp4DMM.kindSelected, vastp4DMM.ipartSelectionFirst,
            vastp4DMM.cpartSelection);
    if (!fSaved)
        return fFalse;
    for (int32_t i = 0; i < cpartTransform; ++i)
        vastp4DMM.pbody->SetPartMatrix(rgipart[i], &rgbmat34[i]);
    Apply4DMMActorStudioSelectionHilite();
    Render4DMMActorStudioPreview();
    MVIE::MultiLog(pmvie,
        "actor_studio_part_reset_scale action=%ld cel=%ld first=%ld selected_parts=%ld transform_parts=%ld own_geometry_only=%d",
        (long)vastp4DMM.anid, (long)vastp4DMM.celn,
        (long)vastp4DMM.ipartSelectionFirst, (long)vastp4DMM.cpartSelection,
        (long)cpartTransform, (int)fOwnGeometryOnly);
    return fTrue;
}

static void Set4DMMActorStudioPartTool(int32_t mode)
{
    if (mode != kastReposition && mode != kastPitch && mode != kastYaw && mode != kastRoll &&
        mode != kastGrowShrink && mode != kastStretchSquish && mode != kastDelete &&
        mode != kastCut)
        return;
    vastp4DMM.modePartTool = mode;
    Update4DMMActorStudioButtons();
    MVIE::MultiLog(vpmvie4DMMActorStudio, "actor_studio_part_tool mode=%ld", (long)mode);
}

static bool F4DMMActorStudioForeground(void)
{
    HWND hwndFg = GetForegroundWindow();
    return hwndFg == vhwnd4DMMActorStudio || hwndFg == vhwnd4DMMActorStudioViewport ||
           (vhwnd4DMMActorStudio != hNil && hwndFg != hNil && IsChild(vhwnd4DMMActorStudio, hwndFg));
}

static bool FHandle4DMMActorStudioToolKey(WPARAM vk)
{
    if (!F4DMMActorStudioForeground() || vastp4DMM.fPlaying)
        return fFalse;
    const bool fCtrl = GetKeyState(VK_CONTROL) < 0;

    if (fCtrl && vk == 'Q')
    {
        MVIE::MultiLog(vpmvie4DMMActorStudio, "actor_studio_input isolated_ctrl_q_fallback");
        return fTrue;
    }

    if (fCtrl && vk == 'G' && GetKeyState(VK_MENU) >= 0 && GetKeyState(VK_SHIFT) >= 0)
    {
        FOpen4DMMObjectGroups();
        return fTrue;
    }

    // Delete remains the destructive tool-first interaction. Ctrl+X is the
    // temporal Cut command. Always consume it while Actor Studio owns focus,
    // including hierarchy levels whose Cut implementation is still pending.
    if (fCtrl && vk == 'X')
    {
        F4DMMActorStudioCutSelectedPart();
        return fTrue;
    }
    if (vk == VK_DELETE)
    {
        Set4DMMActorStudioPartTool(kastDelete);
        return fTrue;
    }

    // Original Actor Studio one-key layout: Q/W/E select the three rotations,
    // R selects Reposition, and T toggles Up/Down. Keep the later Ctrl+QWER /
    // Ctrl+ASD fast-edit row too, so both requested layouts coexist.
    if (!fCtrl)
    {
        switch (vk)
        {
        case 'Q': Set4DMMActorStudioPartTool(kastPitch); return fTrue;
        case 'W': Set4DMMActorStudioPartTool(kastYaw); return fTrue;
        case 'E': Set4DMMActorStudioPartTool(kastRoll); return fTrue;
        case 'R': Set4DMMActorStudioPartTool(kastReposition); return fTrue;
        case 'T':
            vastp4DMM.fVerticalMovement = !vastp4DMM.fVerticalMovement;
            Update4DMMActorStudioButtons();
            return fTrue;
        }
    }

    // Fast-edit layout mirrors the physical keyboard: rotations across QWER,
    // scale tools across ASD, and the simple movement controls on 1/2.
    if (fCtrl)
    {
        switch (vk)
        {
        case 'W': Set4DMMActorStudioPartTool(kastYaw); return fTrue;
        case 'E': Set4DMMActorStudioPartTool(kastRoll); return fTrue;
        case 'R': return FReset4DMMActorStudioPartRotation();
        case 'A': Set4DMMActorStudioPartTool(kastGrowShrink); return fTrue;
        case 'S': Set4DMMActorStudioPartTool(kastStretchSquish); return fTrue;
        case 'D': return FReset4DMMActorStudioPartScale();
        case 'T':
            vastp4DMM.fVerticalMovement = !vastp4DMM.fVerticalMovement;
            Update4DMMActorStudioButtons();
            return fTrue;
        case '1': Set4DMMActorStudioPartTool(kastReposition); return fTrue;
        case '2':
            vastp4DMM.fVerticalMovement = !vastp4DMM.fVerticalMovement;
            Update4DMMActorStudioButtons();
            MVIE::MultiLog(vpmvie4DMMActorStudio,
                "actor_studio_vertical_movement hotkey enabled=%d", (int)vastp4DMM.fVerticalMovement);
            return fTrue;
        }
    }
    // 1/2 are also accepted unmodified because they do not collide with text
    // entry in either Actor Studio window and make the two most-used movement
    // controls genuinely one-key operations.
    if (vk == '1')
    {
        Set4DMMActorStudioPartTool(kastReposition);
        return fTrue;
    }
    if (vk == '2')
    {
        vastp4DMM.fVerticalMovement = !vastp4DMM.fVerticalMovement;
        Update4DMMActorStudioButtons();
        MVIE::MultiLog(vpmvie4DMMActorStudio,
            "actor_studio_vertical_movement hotkey enabled=%d", (int)vastp4DMM.fVerticalMovement);
        return fTrue;
    }
    return fFalse;
}

static void Poll4DMMActorStudioFastHotkeys(void)
{
    if (!vf4DMMActorStudioFastHotkeysRegistered || !F4DMMActorStudioForeground())
    {
        vgrf4DMMActorStudioPolledKeys = 0;
        return;
    }

    struct POLLKEY
    {
        UINT vk;
        uint32_t grf;
    };
    static const POLLKEY rg[] = {
        {'Q', 1u << 0}, {'W', 1u << 1}, {'E', 1u << 2}, {'R', 1u << 3},
        {'T', 1u << 4}, {'A', 1u << 5}, {'S', 1u << 6}, {'D', 1u << 7},
        {'1', 1u << 8}, {'2', 1u << 9}, {'G', 1u << 10}, {'X', 1u << 11},
        {'C', 1u << 12}, {'V', 1u << 13}, {VK_DELETE, 1u << 14},
        {VK_ESCAPE, 1u << 15}, {VK_SPACE, 1u << 16}};

    uint32_t grfNow = 0;
    for (int32_t i = 0; i < (int32_t)(SIZEOF(rg) / SIZEOF(rg[0])); ++i)
        if ((GetAsyncKeyState((int)rg[i].vk) & 0x8000) != 0)
            grfNow |= rg[i].grf;
    const uint32_t grfPressed = grfNow & ~vgrf4DMMActorStudioPolledKeys;
    vgrf4DMMActorStudioPolledKeys = grfNow;
    if (grfPressed == 0)
        return;

    const bool fCtrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool fShift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    for (int32_t i = 0; i < (int32_t)(SIZEOF(rg) / SIZEOF(rg[0])); ++i)
    {
        if ((grfPressed & rg[i].grf) == 0)
            continue;
        const UINT vk = rg[i].vk;
        bool fHandled = fFalse;

        if (vk == VK_ESCAPE)
        {
            if (!FCancel4DMMActorStudioSpawnPlacement())
                Clear4DMMActorStudioPartSelection(fTrue);
            fHandled = fTrue;
        }
        else if (vk == VK_SPACE)
            fHandled = FToggle4DMMActorStudioPlayback();
        else if (fCtrl && vk == 'X')
            fHandled = FHandle4DMMActorStudioRegisteredHotKey(kid4DMMActorStudioFastCutDelete);
        else if (fCtrl && vk == 'C')
            fHandled = FHandle4DMMActorStudioRegisteredHotKey(kid4DMMActorStudioFastCopyPart);
        else if (fCtrl && vk == 'V')
            fHandled = FHandle4DMMActorStudioRegisteredHotKey(
                fShift ? kid4DMMActorStudioFastPastePartFrozen : kid4DMMActorStudioFastPastePart);
        else
            fHandled = FHandle4DMMActorStudioToolKey(vk);

        if (fHandled)
            MVIE::MultiLog(vpmvie4DMMActorStudio,
                "actor_studio_fast_hotkey poll vk=%lu ctrl=%d shift=%d",
                (unsigned long)vk, (int)fCtrl, (int)fShift);
    }
}

static bool FSet4DMMActorStudioPlaying(bool fPlaying, bool fReturnFirst)
{
    if (vhwnd4DMMActorStudio == hNil || !IsWindow(vhwnd4DMMActorStudio) || vastp4DMM.ptmpl == pvNil)
        return fFalse;
    int32_t ccel = 0;
    if (vastp4DMM.anid < 0 || !vastp4DMM.ptmpl->FGetCcelActn(vastp4DMM.anid, &ccel) || ccel <= 0)
        return fFalse;

    if (fPlaying)
    {
        if (!vastp4DMM.fPlaying)
        {
            vastp4DMM.fPlaying = fTrue;
            SetTimer(vhwnd4DMMActorStudio, 2, 143, pvNil); // 7 FPS
        }
    }
    else
    {
        KillTimer(vhwnd4DMMActorStudio, 2);
        vastp4DMM.fPlaying = fFalse;
        if (fReturnFirst)
        {
            FSet4DMMActorStudioActionCel(vastp4DMM.anid, 0, fFalse);
            Fill4DMMActorStudioFrames();
        }
    }
    Update4DMMActorStudioInfo(vpmvie4DMMActorStudio);
    Update4DMMActorStudioButtons();
    MVIE::MultiLog(vpmvie4DMMActorStudio,
        "actor_studio_playback playing=%d return_first=%d action=%ld cel=%ld fps=7",
        (int)vastp4DMM.fPlaying, (int)fReturnFirst, (long)vastp4DMM.anid, (long)vastp4DMM.celn);
    return fTrue;
}

static bool FToggle4DMMActorStudioPlayback(void)
{
    return FSet4DMMActorStudioPlaying(!vastp4DMM.fPlaying, fFalse);
}

static int32_t Anid4DMMActorStudioNewActionSource(void)
{
    if (vastp4DMM.ptmpl == pvNil)
        return ivNil;
    PACTR pactr = Pactr4DMMActorStudioTarget(vpmvie4DMMActorStudio);
    int32_t anidFirst = ivNil;
    for (int32_t anid = 0; anid < vastp4DMM.ptmpl->Cactn(); ++anid)
    {
        if (vpmvie4DMMActorStudio != pvNil && pactr != pvNil &&
            vpmvie4DMMActorStudio->FActorStudioActionIsCustom(pactr, anid))
            continue;
        if (anidFirst == ivNil)
            anidFirst = anid;
        STN stn;
        if (!vastp4DMM.ptmpl->FGetActnName(anid, &stn))
            continue;
        achar szName[192];
        strncpy_s(szName, SIZEOF(szName), stn.Psz(), _TRUNCATE);
        int32_t cch = (int32_t)strlen(szName);
        while (cch >= 2 && szName[cch - 2] == ChLit(' ') && szName[cch - 1] == ChLit('*'))
        {
            szName[cch - 2] = 0;
            cch -= 2;
        }
        if (0 == _stricmp(szName, "At Rest"))
            return anid;
    }
    return anidFirst;
}

static bool FCreate4DMMActorStudioNewAction(void)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    const int32_t anidSource = Anid4DMMActorStudioNewActionSource();
    if (pmvie == pvNil || pactr == pvNil || anidSource == ivNil)
        return fFalse;
    achar szName[kcch4DMMCustomName];
    if (!FPrompt4DMMAnimationName("New Action", "New Action", szName, SIZEOF(szName)))
        return fFalse;
    int32_t anidNew = ivNil;
    if (!pmvie->FActorStudioNewAction(pactr, anidSource, szName, &anidNew))
    {
        MessageBoxA(vhwnd4DMMActorStudio, "4DMM could not create the new animation.",
                    "New Action", MB_OK | MB_ICONERROR);
        return fFalse;
    }
    if (!FRefresh4DMMActorStudio(fTrue))
        return fFalse;
    FSet4DMMActorStudioActionCel(anidNew, 0, fFalse);
    Fill4DMMActorStudioActions();
    Fill4DMMActorStudioFrames();
    Update4DMMActorStudioInfo(pmvie);
    Update4DMMActorStudioButtons();
    return fTrue;
}

static bool F4DMMActorStudioCopyFrame(void)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil || vastp4DMM.anid < 0 || vastp4DMM.celn < 0)
        return fFalse;
    if (!pmvie->FActorStudioGetFrameSnapshot(pactr, vastp4DMM.anid, vastp4DMM.celn,
                                              &vasframe4DMMActorStudioClipboard))
        return fFalse;
    MVIE::MultiLog(pmvie, "actor_studio_frame_clipboard copy action=%ld cel=%ld parts=%ld",
                   (long)vastp4DMM.anid, (long)vastp4DMM.celn,
                   (long)vasframe4DMMActorStudioClipboard.cpart);
    Update4DMMActorStudioButtons();
    return fTrue;
}

static bool F4DMMActorStudioAfterFrameMutation(int32_t celnSelect)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil || vastp4DMM.ptmpl == pvNil)
        return fFalse;
    int32_t ccel = 0;
    if (!vastp4DMM.ptmpl->FGetCcelActn(vastp4DMM.anid, &ccel) || ccel <= 0)
        return fFalse;
    celnSelect = LwMax(0L, LwMin(celnSelect, ccel - 1));
    if (!FSet4DMMActorStudioActionCel(vastp4DMM.anid, celnSelect, fFalse))
        return fFalse;
    Fill4DMMActorStudioFrames();
    Update4DMMActorStudioInfo(pmvie);
    Update4DMMActorStudioButtons();
    return fTrue;
}

static bool F4DMMActorStudioWriteClipboard(int32_t asfw)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil || !vasframe4DMMActorStudioClipboard.fValid)
        return fFalse;
    const int32_t celnOld = vastp4DMM.celn;
    if (!pmvie->FActorStudioWriteFrameSnapshot(pactr, vastp4DMM.anid, celnOld,
                                                &vasframe4DMMActorStudioClipboard, asfw))
        return fFalse;
    const int32_t celnNew = asfw == kasfwInsertAfter ? celnOld + 1 : celnOld;
    return F4DMMActorStudioAfterFrameMutation(celnNew);
}

static bool F4DMMActorStudioInsertDuplicate(int32_t asfw)
{
    ACTORSTUDIOFRAMESNAPSHOT snapshot;
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil ||
        !pmvie->FActorStudioGetFrameSnapshot(pactr, vastp4DMM.anid, vastp4DMM.celn, &snapshot))
        return fFalse;
    const int32_t celnOld = vastp4DMM.celn;
    if (!pmvie->FActorStudioWriteFrameSnapshot(pactr, vastp4DMM.anid, celnOld, &snapshot, asfw))
        return fFalse;
    const int32_t celnNew = asfw == kasfwInsertAfter ? celnOld + 1 : celnOld;
    return F4DMMActorStudioAfterFrameMutation(celnNew);
}

static bool F4DMMActorStudioDeleteCurrentFrame(void)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil)
        return fFalse;
    const int32_t celnOld = vastp4DMM.celn;
    if (!pmvie->FActorStudioDeleteFrame(pactr, vastp4DMM.anid, celnOld))
        return fFalse;
    return F4DMMActorStudioAfterFrameMutation(celnOld);
}

static bool F4DMMActorStudioCutFrame(void)
{
    if (!F4DMMActorStudioCopyFrame())
        return fFalse;
    return F4DMMActorStudioDeleteCurrentFrame();
}

static bool F4DMMActorStudioRecreateDetachedAfterTopology(PMVIE pmvie, const TAG *ptagBefore,
                                                           PACTR *ppactr)
{
    if (pmvie == pvNil || ptagBefore == pvNil || ppactr == pvNil)
        return fFalse;
    if (vpactr4DMMActorStudioDetached == pvNil)
    {
        *ppactr = Pactr4DMMActorStudioTarget(pmvie);
        return *ppactr != pvNil;
    }

    const CNO cnoOwnedTmpl = ptagBefore->cno;
    Release4DMMActorStudioDetachedTarget();
    TAG tagReload;
    if (ptagBefore->sid != ksidUseCrf || ptagBefore->ctg != kctgTmpl ||
        !pmvie->FOpen4DMMOwnedTemplateTag(cnoOwnedTmpl, &tagReload))
        return fFalse;
    PACTR pactrNew = ACTR::PactrNew(&tagReload);
    TAGM::CloseTag(&tagReload);
    if (pactrNew == pvNil)
        return fFalse;
    vpactr4DMMActorStudioDetached = pactrNew;
    varid4DMMActorStudioTarget = aridNil;
    *ppactr = pactrNew;
    return fTrue;
}

void Refresh4DMMActorStudioAfterTopologyEdit(PMVIE pmvie, int32_t arid, CNO cnoTmpl,
                                              int32_t anid, int32_t celn, int32_t ipart)
{
    if (pmvie == pvNil || pmvie != vpmvie4DMMActorStudio ||
        vhwnd4DMMActorStudio == hNil || !IsWindow(vhwnd4DMMActorStudio))
        return;

    Invalidate4DMMActorStudioGroupRotationBaseline();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pactr == pvNil)
        return;
    TAG tagBefore;
    pactr->GetTagTmpl(&tagBefore);
    if (tagBefore.sid != ksidUseCrf || tagBefore.ctg != kctgTmpl || tagBefore.cno != cnoTmpl)
        return;

    if (arid == aridNil)
    {
        if (!F4DMMActorStudioRecreateDetachedAfterTopology(pmvie, &tagBefore, &pactr))
            return;
    }
    else
    {
        varid4DMMActorStudioTarget = arid;
        if (pmvie->Pscen() != pvNil)
            pactr = pmvie->Pscen()->PactrFromArid(arid);
        if (pactr == pvNil)
            return;
    }

    if (!FRefresh4DMMActorStudio(fTrue))
        return;
    int32_t ccel = 0;
    if (vastp4DMM.ptmpl != pvNil && vastp4DMM.ptmpl->FGetCcelActn(anid, &ccel) && ccel > 0)
        FSet4DMMActorStudioActionCel(anid, LwMin(LwMax(celn, 0L), ccel - 1), fFalse);
    Fill4DMMActorStudioObjects();
    Fill4DMMActorStudioActions();
    Fill4DMMActorStudioFrames();
    Fill4DMMActorStudioParts();
    Clear4DMMActorStudioPartSelection(fFalse);
    if (vastp4DMM.pbody != pvNil && FIn(ipart, 0, vastp4DMM.pbody->Cpart()))
        Select4DMMActorStudioPartRow(ipart);
    Update4DMMActorStudioInfo(pmvie);
    Update4DMMActorStudioButtons();
    Render4DMMActorStudioPreview();
    MVIE::MultiLog(pmvie,
        "actor_studio_topology_refresh arid=%ld tmpl=%ld action=%ld cel=%ld part=%ld parts=%ld",
        (long)arid, (long)cnoTmpl, (long)anid, (long)celn, (long)ipart,
        vastp4DMM.pbody != pvNil ? (long)vastp4DMM.pbody->Cpart() : -1L);
}

static bool F4DMMActorStudioDeleteSelectedNode(void)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil || vastp4DMM.pbody == pvNil ||
        vastp4DMM.kindSelected == kastnDefaultPartGroup ||
        vastp4DMM.kindSelected == kastnNonGroupedParts ||
        vastp4DMM.kindSelected == kastnActorPropRoot ||
        vastp4DMM.cpartSelection <= 0 || vastp4DMM.ipartSelectionFirst < 0 ||
        vastp4DMM.ipartSelectionFirst + vastp4DMM.cpartSelection > vastp4DMM.pbody->Cpart() ||
        vastp4DMM.cpartSelection >= vastp4DMM.pbody->Cpart())
        return fFalse;

    const int32_t anidKeep = vastp4DMM.anid;
    const int32_t celnKeep = vastp4DMM.celn;
    const int32_t ipartFirst = vastp4DMM.ipartSelectionFirst;
    const int32_t cpartDelete = vastp4DMM.cpartSelection;
    const bool fDetachedTarget = vpactr4DMMActorStudioDetached != pvNil;
    TAG tagBefore;
    pactr->GetTagTmpl(&tagBefore);

    MVIE::MultiLog(pmvie,
        "actor_studio_tree_delete ui_begin kind=%ld first=%ld parts=%ld total=%ld detached=%d",
        (long)vastp4DMM.kindSelected, (long)ipartFirst, (long)cpartDelete,
        (long)vastp4DMM.pbody->Cpart(), (int)fDetachedTarget);

    // As with the now-proven Duplicate path, remove the private preview BODY
    // before changing the writable TMPL's topology.
    Reset4DMMActorStudioPreview();
    if (!pmvie->FActorStudioDeletePartRange(pactr, ipartFirst, cpartDelete,
                                                  vastp4DMM.kindSelected,
                                                  anidKeep, celnKeep))
    {
        MVIE::MultiLog(pmvie,
            "actor_studio_tree_delete core_fail first=%ld parts=%ld", (long)ipartFirst, (long)cpartDelete);
        FRefresh4DMMActorStudio(fTrue);
        return fFalse;
    }

    Adjust4DMMActorStudioRotationEdgesAfterDelete(pmvie, tagBefore.cno,
                                                   ipartFirst, cpartDelete);
    if (fDetachedTarget && !F4DMMActorStudioRecreateDetachedAfterTopology(pmvie, &tagBefore, &pactr))
    {
        MVIE::MultiLog(pmvie, "actor_studio_tree_delete detached_recreate_fail tmpl=%ld", (long)tagBefore.cno);
        return fFalse;
    }
    if (!FRefresh4DMMActorStudio(fTrue))
        return fFalse;

    int32_t ccel = 0;
    if (vastp4DMM.ptmpl != pvNil && vastp4DMM.ptmpl->FGetCcelActn(anidKeep, &ccel) && ccel > 0)
        FSet4DMMActorStudioActionCel(anidKeep, LwMin(celnKeep, ccel - 1), fFalse);
    Fill4DMMActorStudioActions();
    Fill4DMMActorStudioFrames();
    Fill4DMMActorStudioParts();
    if (vaspart4DMMActorStudioClipboard.fValid &&
        vaspart4DMMActorStudioClipboard.cnoTmplSource == tagBefore.cno)
        ClearPb(&vaspart4DMMActorStudioClipboard, SIZEOF(vaspart4DMMActorStudioClipboard));
    // Delete is a persistent tool. Do not auto-select the nearest surviving
    // part after a topology edit: the next click should pick exactly what the
    // user wants to delete next.
    Clear4DMMActorStudioPartSelection(fFalse);
    Set4DMMActorStudioPartTool(kastDelete);
    Render4DMMActorStudioPreview();
    MVIE::MultiLog(pmvie,
        "actor_studio_tree_delete ui_success first=%ld removed=%ld remaining=%ld selected=-1 tool=delete",
        (long)ipartFirst, (long)cpartDelete,
        vastp4DMM.pbody != pvNil ? (long)vastp4DMM.pbody->Cpart() : -1L);
    return fTrue;
}

static bool F4DMMActorStudioUseCurrentObject(void)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil || pactr->Ptmpl() == pvNil || pmvie->Pscen() == pvNil)
        return fFalse;
    const int32_t idObject = Id4DMMActorStudioHandmadeObject(pmvie);
    if (idObject <= 0)
        return fFalse;

    const CUSTOMOBJECT *pobj = pvNil;
    for (int32_t i = 0; i < pmvie->C4DMMCustomObjects(); ++i)
    {
        const CUSTOMOBJECT *ptest = pmvie->P4DMMCustomObject(i);
        if (ptest != pvNil && ptest->id == idObject)
        {
            pobj = ptest;
            break;
        }
    }
    if (pobj == pvNil || pobj->cnoOwnedTmpl == cnoNil)
        return fFalse;

    TAG tag;
    if (!pmvie->FOpen4DMMOwnedTemplateTag(pobj->cnoOwnedTmpl, &tag))
        return fFalse;
    const bool fInserted = pmvie->FInsActr(&tag);
    TAGM::CloseTag(&tag);
    if (!fInserted)
        return fFalse;
    PMVU pmvu = pmvie->PmvuCur();
    if (pmvu != pvNil)
    {
        pmvu->StartPlaceActor(fTrue);
        if (!pmvie->F4DMMActorStudioExposeCurrentPlacementInRollCall())
            MVIE::MultiLog(pmvie,
                "actor_studio_use_object rollcall_expose_fail id=%ld tmpl=%ld",
                (long)idObject, (long)pobj->cnoOwnedTmpl);
    }
    MVIE::MultiLog(pmvie,
        "actor_studio_use_object id=%ld tmpl=%ld type=%s result=1",
        (long)idObject, (long)pobj->cnoOwnedTmpl, pobj->fProp ? "prop" : "actor");
    return fTrue;
}

static bool F4DMMActorStudioDuplicatePart(int32_t ipartSource, bool fKeepPositions,
                                            const BMAT34 *pbmat34Freeze)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil || vastp4DMM.pbody == pvNil ||
        !FIn(ipartSource, 0, vastp4DMM.pbody->Cpart()))
        return fFalse;
    const int32_t anidKeep = vastp4DMM.anid;
    const int32_t celnKeep = vastp4DMM.celn;
    const bool fDetachedTarget = vpactr4DMMActorStudioDetached != pvNil;
    TAG tagBefore;
    pactr->GetTagTmpl(&tagBefore);

    MVIE::ActorStudioDuplicateLog(pmvie,
        "UI_BEGIN detached=%d preview_body=%p preview_parts=%ld target=%p target_body=%p",
        (int)fDetachedTarget, vastp4DMM.pbody,
        vastp4DMM.pbody != pvNil ? (long)vastp4DMM.pbody->Cpart() : -1L,
        pactr, pactr->Pbody());

    // Remove the separate preview BRender tree before mutating TMPL topology.
    // It has no role in the resource edit and keeping it attached while the
    // shared TMPL changes only creates another live owner that can render a
    // half-transitioned shape. A failed core edit rebuilds the old preview.
    MVIE::ActorStudioDuplicateLog(pmvie, "UI_PREVIEW_RESET_BEFORE_CORE begin");
    Reset4DMMActorStudioPreview();
    MVIE::ActorStudioDuplicateLog(pmvie, "UI_PREVIEW_RESET_BEFORE_CORE done");

    int32_t ipartNew = ivNil;
    if (!pmvie->FActorStudioDuplicatePart(pactr, anidKeep, celnKeep, ipartSource,
                                           fKeepPositions, pbmat34Freeze, &ipartNew))
    {
        MVIE::ActorStudioDuplicateLog(pmvie, "UI_CORE_RETURN fail; rebuilding previous preview");
        FRefresh4DMMActorStudio(fTrue);
        return fFalse;
    }
    MVIE::ActorStudioDuplicateLog(pmvie, "UI_CORE_RETURN ok new_part=%ld", (long)ipartNew);

    // For a detached Actor Studio target, its own BODY still represents the
    // old topology by design. Release that stale graph and construct a fresh
    // ACTR from the already-refreshed movie-owned TMPL before preview rebuild.
    if (fDetachedTarget)
    {
        const CNO cnoOwnedTmpl = tagBefore.cno;
        MVIE::ActorStudioDuplicateLog(pmvie,
            "UI_DETACHED_RECREATE release_old target=%p tmpl=%ld", pactr, (long)cnoOwnedTmpl);
        Release4DMMActorStudioDetachedTarget();

        TAG tagReload;
        if (tagBefore.sid != ksidUseCrf || tagBefore.ctg != kctgTmpl ||
            !pmvie->FOpen4DMMOwnedTemplateTag(cnoOwnedTmpl, &tagReload))
        {
            MVIE::ActorStudioDuplicateLog(pmvie,
                "UI_DETACHED_RECREATE open_tag_fail tmpl=%ld", (long)cnoOwnedTmpl);
            return fFalse;
        }
        PACTR pactrNew = ACTR::PactrNew(&tagReload);
        TAGM::CloseTag(&tagReload);
        if (pactrNew == pvNil)
        {
            MVIE::ActorStudioDuplicateLog(pmvie,
                "UI_DETACHED_RECREATE pactr_new_fail tmpl=%ld", (long)cnoOwnedTmpl);
            return fFalse;
        }
        vpactr4DMMActorStudioDetached = pactrNew;
        varid4DMMActorStudioTarget = aridNil;
        pactr = pactrNew;
        MVIE::ActorStudioDuplicateLog(pmvie,
            "UI_DETACHED_RECREATE done target=%p body=%p parts=%ld",
            pactrNew, pactrNew->Pbody(),
            pactrNew->Pbody() != pvNil ? (long)pactrNew->Pbody()->Cpart() : -1L);
    }

    MVIE::ActorStudioDuplicateLog(pmvie, "UI_PREVIEW_REBUILD begin target=%p", pactr);
    if (!FRefresh4DMMActorStudio(fTrue))
    {
        MVIE::ActorStudioDuplicateLog(pmvie, "UI_PREVIEW_REBUILD fail");
        return fFalse;
    }
    MVIE::ActorStudioDuplicateLog(pmvie,
        "UI_PREVIEW_REBUILD done body=%p parts=%ld", vastp4DMM.pbody,
        vastp4DMM.pbody != pvNil ? (long)vastp4DMM.pbody->Cpart() : -1L);

    if (!FSet4DMMActorStudioActionCel(anidKeep, celnKeep, fFalse))
    {
        MVIE::ActorStudioDuplicateLog(pmvie,
            "UI_RESTORE_POSE fail action=%ld cel=%ld", (long)anidKeep, (long)celnKeep);
        return fFalse;
    }
    Fill4DMMActorStudioActions();
    Fill4DMMActorStudioFrames();
    Fill4DMMActorStudioParts();
    if (FIn(ipartNew, 0, vastp4DMM.pbody->Cpart()))
        Select4DMMActorStudioPartRow(ipartNew);
    Update4DMMActorStudioInfo(pmvie);
    Update4DMMActorStudioButtons();
    MVIE::ActorStudioDuplicateLog(pmvie,
        "UI_SUCCESS new_part=%ld preview_parts=%ld", (long)ipartNew,
        vastp4DMM.pbody != pvNil ? (long)vastp4DMM.pbody->Cpart() : -1L);
    return fTrue;
}

static bool F4DMMActorStudioDuplicateSelectedRange(bool fKeepPositions)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil || vastp4DMM.pbody == pvNil ||
        (vastp4DMM.kindSelected != kastnObject && vastp4DMM.kindSelected != kastnGroup) ||
        vastp4DMM.cpartSelection <= 0 || vastp4DMM.ipartSelectionFirst < 0 ||
        vastp4DMM.ipartSelectionFirst + vastp4DMM.cpartSelection > vastp4DMM.pbody->Cpart())
        return fFalse;

    const int32_t anidKeep = vastp4DMM.anid;
    const int32_t celnKeep = vastp4DMM.celn;
    const int32_t kindKeep = vastp4DMM.kindSelected;
    const int32_t ipartFirst = vastp4DMM.ipartSelectionFirst;
    const int32_t cpartSource = vastp4DMM.cpartSelection;
    const bool fDetachedTarget = vpactr4DMMActorStudioDetached != pvNil;
    TAG tagBefore;
    pactr->GetTagTmpl(&tagBefore);

    MVIE::ActorStudioDuplicateLog(pmvie,
        "UI_RANGE_BEGIN kind=%ld first=%ld parts=%ld detached=%d preview_parts=%ld",
        (long)kindKeep, (long)ipartFirst, (long)cpartSource, (int)fDetachedTarget,
        vastp4DMM.pbody != pvNil ? (long)vastp4DMM.pbody->Cpart() : -1L);

    Reset4DMMActorStudioPreview();
    int32_t ipartNewFirst = ivNil;
    if (!pmvie->FActorStudioDuplicatePartRange(
            pactr, anidKeep, celnKeep, ipartFirst, cpartSource, kindKeep,
            fKeepPositions, &ipartNewFirst))
    {
        MVIE::ActorStudioDuplicateLog(pmvie,
            "UI_RANGE_CORE_RETURN fail kind=%ld first=%ld parts=%ld",
            (long)kindKeep, (long)ipartFirst, (long)cpartSource);
        FRefresh4DMMActorStudio(fTrue);
        return fFalse;
    }

    if (fDetachedTarget)
    {
        const CNO cnoOwnedTmpl = tagBefore.cno;
        Release4DMMActorStudioDetachedTarget();
        TAG tagReload;
        if (tagBefore.sid != ksidUseCrf || tagBefore.ctg != kctgTmpl ||
            !pmvie->FOpen4DMMOwnedTemplateTag(cnoOwnedTmpl, &tagReload))
            return fFalse;
        PACTR pactrNew = ACTR::PactrNew(&tagReload);
        TAGM::CloseTag(&tagReload);
        if (pactrNew == pvNil)
            return fFalse;
        vpactr4DMMActorStudioDetached = pactrNew;
        varid4DMMActorStudioTarget = aridNil;
        pactr = pactrNew;
    }

    if (!FRefresh4DMMActorStudio(fTrue) ||
        !FSet4DMMActorStudioActionCel(anidKeep, celnKeep, fFalse))
        return fFalse;

    Fill4DMMActorStudioActions();
    Fill4DMMActorStudioFrames();
    Fill4DMMActorStudioParts();
    const int32_t irowNew = I4DMMActorStudioTreeRowForRange(
        kindKeep, ipartNewFirst, cpartSource);
    if (irowNew != ivNil)
        FSelect4DMMActorStudioTreeRow(irowNew, fTrue);
    else if (FIn(ipartNewFirst, 0, vastp4DMM.pbody->Cpart()))
        Select4DMMActorStudioPartRow(ipartNewFirst);
    Update4DMMActorStudioInfo(pmvie);
    Update4DMMActorStudioButtons();
    MVIE::ActorStudioDuplicateLog(pmvie,
        "UI_RANGE_SUCCESS kind=%ld first=%ld parts=%ld new_first=%ld row=%ld preview_parts=%ld",
        (long)kindKeep, (long)ipartFirst, (long)cpartSource, (long)ipartNewFirst,
        (long)irowNew, vastp4DMM.pbody != pvNil ? (long)vastp4DMM.pbody->Cpart() : -1L);
    return fTrue;
}

static bool F4DMMActorStudioDuplicateSelectedPart(void)
{
    if (vastp4DMM.pbody == pvNil ||
        vastp4DMM.kindSelected == kastnDefaultPartGroup ||
        vastp4DMM.kindSelected == kastnNonGroupedParts ||
        vastp4DMM.kindSelected == kastnActorPropRoot ||
        !FIn(vastp4DMM.ipartSelected, 0, vastp4DMM.pbody->Cpart()))
        return fFalse;
    const bool fKeepPositions = SendMessageA(
        GetDlgItem(vhwnd4DMMActorStudio, kid4DMMActorStudioKeepPositions),
        BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (vastp4DMM.kindSelected == kastnObject || vastp4DMM.kindSelected == kastnGroup)
        return F4DMMActorStudioDuplicateSelectedRange(fKeepPositions);
    return F4DMMActorStudioDuplicatePart(vastp4DMM.ipartSelected, fKeepPositions, pvNil);
}

static bool F4DMMActorStudioCopySelectedPart(void)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil || vastp4DMM.pbody == pvNil ||
        vastp4DMM.kindSelected != kastnPart || vastp4DMM.cpartSelection != 1 ||
        !FIn(vastp4DMM.ipartSelected, 0, vastp4DMM.pbody->Cpart()))
        return fFalse;

    ACTORSTUDIOFRAMESNAPSHOT snapshot;
    if (!pmvie->FActorStudioGetFrameSnapshot(pactr, vastp4DMM.anid, vastp4DMM.celn, &snapshot) ||
        !FIn(vastp4DMM.ipartSelected, 0, snapshot.cpart))
        return fFalse;

    TAG tagTmpl;
    pactr->GetTagTmpl(&tagTmpl);
    vaspart4DMMActorStudioClipboard.fValid = fTrue;
    vaspart4DMMActorStudioClipboard.pmvieSource = pmvie;
    vaspart4DMMActorStudioClipboard.cnoTmplSource = tagTmpl.cno;
    vaspart4DMMActorStudioClipboard.ipartSource = vastp4DMM.ipartSelected;
    vaspart4DMMActorStudioClipboard.anidSource = vastp4DMM.anid;
    vaspart4DMMActorStudioClipboard.celnSource = vastp4DMM.celn;
    vaspart4DMMActorStudioClipboard.bmat34CopyPose =
        snapshot.rgbmat34[vastp4DMM.ipartSelected];
    MVIE::MultiLog(pmvie,
        "actor_studio_part_clipboard copy tmpl=%ld part=%ld action=%ld cel=%ld",
        (long)tagTmpl.cno, (long)vastp4DMM.ipartSelected,
        (long)vastp4DMM.anid, (long)vastp4DMM.celn);
    return fTrue;
}

static bool F4DMMActorStudioCutSelectedPart(void)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    const bool fHaveBody = vastp4DMM.pbody != pvNil;
    const bool fPartIndex = fHaveBody &&
        FIn(vastp4DMM.ipartSelected, 0, vastp4DMM.pbody->Cpart());
    const bool fCustom = pmvie != pvNil && pactr != pvNil && vastp4DMM.anid >= 0 &&
        pmvie->FActorStudioActionIsCustom(pactr, vastp4DMM.anid);
    const bool fLiteralPart = vastp4DMM.kindSelected == kastnPart &&
        vastp4DMM.cpartSelection == 1;
    const bool fDefaultGroup = vastp4DMM.kindSelected == kastnDefaultPartGroup &&
        vastp4DMM.cpartSelection == 1;
    const bool fImportedTdtObject =
        F4DMMActorStudioSelectedObjectIsImportedTdt(pmvie);
    const bool fImportedPropObject =
        F4DMMActorStudioSelectedObjectIsImportedProp(pmvie);
    const bool fImportedActorObject =
        F4DMMActorStudioSelectedObjectIsImportedActor(pmvie);
    const bool fImportedObject =
        fImportedTdtObject || fImportedPropObject || fImportedActorObject;
    const bool fObjectGroup =
        F4DMMActorStudioSelectedObjectGroupCanTemporalCutSpawn(pmvie);
    const bool fWholeRoot =
        F4DMMActorStudioSelectedWholeRootCanTemporalCutSpawn(pmvie);
    if (pmvie == pvNil || pactr == pvNil || !fHaveBody || !fPartIndex ||
        (!fLiteralPart && !fDefaultGroup && !fImportedObject && !fObjectGroup && !fWholeRoot) ||
        vastp4DMM.anid < 0 || vastp4DMM.celn < 0 || !fCustom)
    {
        MVIE::MultiLog(pmvie,
            "actor_studio_part_cut reject_precondition pmvie=%p pactr=%p body=%p kind=%ld selected=%ld parts=%ld part_index=%d action=%ld cel=%ld custom=%d tdt_object=%d prop_object=%d actor_object=%d object_group=%d whole_root=%d",
            pmvie, pactr, vastp4DMM.pbody, (long)vastp4DMM.kindSelected,
            (long)vastp4DMM.ipartSelected, (long)vastp4DMM.cpartSelection,
            (int)fPartIndex, (long)vastp4DMM.anid, (long)vastp4DMM.celn,
            (int)fCustom, (int)fImportedTdtObject, (int)fImportedPropObject,
            (int)fImportedActorObject, (int)fObjectGroup, (int)fWholeRoot);
        return fFalse;
    }

    const int32_t ipart = vastp4DMM.ipartSelected;
    const int32_t anid = vastp4DMM.anid;
    const int32_t celn = vastp4DMM.celn;
    const int32_t kindSelection = vastp4DMM.kindSelected;
    const int32_t ipartSelectionFirst = vastp4DMM.ipartSelectionFirst;
    const int32_t cpartSelection = vastp4DMM.cpartSelection;
    std::vector<int32_t> rgipartPresence;
    if (fDefaultGroup)
    {
        Get4DMMActorStudioDefaultPartGroupMembersInRange(
            ipart, 0, vastp4DMM.pbody->Cpart(), &rgipartPresence);
    }
    else if (fImportedObject)
    {
        // Imported 3D Word, Prop, and Actor Object rows are one contiguous BODY range.
        // Include the complete range rather than only currently visible models
        // so Cut also suppresses authored geometry that appears later in the
        // same action. Structural/null-model entries are harmless no-ops in
        // the engine-side presence writer.
        Get4DMMActorStudioSelectedImportedObjectParts(pmvie, &rgipartPresence);
    }
    else if (fObjectGroup)
    {
        // One Actor Studio Object Group may contain several imported Objects.
        // Cut the exact union of their BODY ranges as one temporal edit rather
        // than treating the group's min/max display range as geometry.
        Get4DMMActorStudioSelectedObjectGroupParts(pmvie, &rgipartPresence);
    }
    else if (fWholeRoot)
    {
        // EVERYTHING is the complete BODY target. For customized stock
        // Actors/Props this includes the native BODY plus any duplicated
        // parts; for handmade multi-OG objects it includes every imported OG.
        Get4DMMActorStudioSelectedWholeRootParts(pmvie, &rgipartPresence);
    }
    else if (vastp4DMM.pbody->FPartHasModel(ipart))
    {
        rgipartPresence.push_back(ipart);
    }
    if (rgipartPresence.empty())
    {
        MVIE::MultiLog(pmvie,
            "actor_studio_part_cut reject_no_renderable_members kind=%ld anchor_part=%ld action=%ld cel=%ld tdt_object=%d prop_object=%d actor_object=%d object_group=%d whole_root=%d",
            (long)kindSelection, (long)ipart, (long)anid, (long)celn,
            (int)fImportedTdtObject, (int)fImportedPropObject,
            (int)fImportedActorObject, (int)fObjectGroup, (int)fWholeRoot);
        return fFalse;
    }

    // Match 3DMM's Cut convention for the literal Part path. Part Group and
    // whole-Object clipboards are intentionally deferred until Actor Studio's
    // Copy/Paste layer understands those selection types; temporal Cut itself
    // remains independent and undoable.
    bool fCopied = fFalse;
    if (fLiteralPart)
    {
        fCopied = F4DMMActorStudioCopySelectedPart();
        if (!fCopied)
            MVIE::MultiLog(pmvie,
                "actor_studio_part_cut clipboard_copy_failed action=%ld cel=%ld part=%ld",
                (long)anid, (long)celn, (long)ipart);
    }

    vastp4DMM.pbody->ClearPartHilite();

    const bool fCut = rgipartPresence.size() == 1 ?
        pmvie->FActorStudioCutPart(
            pactr, anid, celn, rgipartPresence[0], kindSelection,
            ipartSelectionFirst, cpartSelection) :
        pmvie->FActorStudioCutParts(
            pactr, anid, celn, &rgipartPresence[0], (int32_t)rgipartPresence.size(),
            kindSelection, ipartSelectionFirst, cpartSelection);
    if (!fCut)
    {
        if (fLiteralPart && vastp4DMM.pbody != pvNil &&
            FIn(ipart, 0, vastp4DMM.pbody->Cpart()) &&
            vastp4DMM.pbody->FPartHasModel(ipart))
            vastp4DMM.pbody->HilitePartRange(ipart, 1);
        else if ((fDefaultGroup || fImportedObject || fObjectGroup || fWholeRoot) &&
                 vastp4DMM.pbody != pvNil)
            Apply4DMMActorStudioSelectionHilite();
        Render4DMMActorStudioPreview();
        MVIE::MultiLog(pmvie,
            "actor_studio_part_cut engine_rejected action=%ld cel=%ld anchor_part=%ld presence_parts=%ld kind=%ld copied=%d tdt_object=%d prop_object=%d actor_object=%d object_group=%d whole_root=%d",
            (long)anid, (long)celn, (long)ipart, (long)rgipartPresence.size(),
            (long)kindSelection, (int)fCopied, (int)fImportedTdtObject,
            (int)fImportedPropObject, (int)fImportedActorObject, (int)fObjectGroup,
            (int)fWholeRoot);
        return fFalse;
    }

    if (!FSet4DMMActorStudioActionCel(anid, celn, fFalse))
    {
        Clear4DMMActorStudioPartSelection(fTrue);
        MVIE::MultiLog(pmvie,
            "actor_studio_part_cut preview_reload_failed action=%ld cel=%ld anchor_part=%ld presence_parts=%ld kind=%ld",
            (long)anid, (long)celn, (long)ipart, (long)rgipartPresence.size(),
            (long)kindSelection);
        return fFalse;
    }
    Clear4DMMActorStudioPartSelection(fTrue);
    Update4DMMActorStudioInfo(pmvie);
    Update4DMMActorStudioButtons();
    MVIE::MultiLog(pmvie,
        "actor_studio_part_cut ui_commit action=%ld cel=%ld anchor_part=%ld presence_parts=%ld kind=%ld copied=%d tdt_object=%d prop_object=%d actor_object=%d object_group=%d whole_root=%d tool=%ld",
        (long)anid, (long)celn, (long)ipart, (long)rgipartPresence.size(),
        (long)kindSelection, (int)fCopied, (int)fImportedTdtObject,
        (int)fImportedPropObject, (int)fImportedActorObject,
        (int)fObjectGroup, (int)fWholeRoot, (long)vastp4DMM.modePartTool);
    return fTrue;
}

static bool FMaybePrepare4DMMActorStudioSpawnSelectedPart(void)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    const bool fLiteralPart = vastp4DMM.kindSelected == kastnPart &&
        vastp4DMM.cpartSelection == 1;
    const bool fDefaultGroup = vastp4DMM.kindSelected == kastnDefaultPartGroup &&
        vastp4DMM.cpartSelection == 1;
    const bool fImportedTdtObject =
        F4DMMActorStudioSelectedObjectIsImportedTdt(pmvie);
    const bool fImportedPropObject =
        F4DMMActorStudioSelectedObjectIsImportedProp(pmvie);
    const bool fImportedActorObject =
        F4DMMActorStudioSelectedObjectIsImportedActor(pmvie);
    const bool fImportedObject =
        fImportedTdtObject || fImportedPropObject || fImportedActorObject;
    const bool fObjectGroup =
        F4DMMActorStudioSelectedObjectGroupCanTemporalCutSpawn(pmvie);
    const bool fWholeRoot =
        F4DMMActorStudioSelectedWholeRootCanTemporalCutSpawn(pmvie);
    if (pmvie == pvNil || pactr == pvNil || vastp4DMM.pbody == pvNil ||
        (!fLiteralPart && !fDefaultGroup && !fImportedObject && !fObjectGroup && !fWholeRoot) ||
        !FIn(vastp4DMM.ipartSelected, 0, vastp4DMM.pbody->Cpart()) ||
        vastp4DMM.anid < 0 || vastp4DMM.celn < 0 ||
        !pmvie->FActorStudioActionIsCustom(pactr, vastp4DMM.anid))
        return fFalse;

    const int32_t ipart = vastp4DMM.ipartSelected;
    const int32_t anid = vastp4DMM.anid;
    const int32_t celn = vastp4DMM.celn;
    std::vector<int32_t> rgipartPresence;
    if (fDefaultGroup)
    {
        Get4DMMActorStudioDefaultPartGroupMembersInRange(
            ipart, 0, vastp4DMM.pbody->Cpart(), &rgipartPresence);
    }
    else if (fImportedObject)
    {
        Get4DMMActorStudioSelectedImportedObjectParts(pmvie, &rgipartPresence);
    }
    else if (fObjectGroup)
    {
        Get4DMMActorStudioSelectedObjectGroupParts(pmvie, &rgipartPresence);
    }
    else if (fWholeRoot)
    {
        Get4DMMActorStudioSelectedWholeRootParts(pmvie, &rgipartPresence);
    }
    else
    {
        rgipartPresence.push_back(ipart);
    }
    if (rgipartPresence.empty())
        return fFalse;

    bool fAnyAbsent = fFalse;
    if (fImportedObject || fObjectGroup || fWholeRoot)
    {
        // Whole imported Object/Object Group ranges may contain hierarchy-only BODY entries
        // whose runtime model pointer is always null. Detect temporal absence
        // from the authored CPS hidden-model bit instead of mistaking those
        // structural nodes for cut geometry.
        ACTORSTUDIOFRAMESNAPSHOT snapshot;
        if (!pmvie->FActorStudioGetFrameSnapshot(pactr, anid, celn, &snapshot) ||
            !snapshot.fValid)
            return fFalse;
        for (size_t i = 0; i < rgipartPresence.size(); ++i)
        {
            const int32_t ipartT = rgipartPresence[i];
            if (!FIn(ipartT, 0, snapshot.cpart))
                return fFalse;
            const int16_t chidModl = snapshot.rgcps[ipartT].chidModl;
            if (chidModl != chidNil && F4DMMActorStudioCpsModelHidden(chidModl))
            {
                fAnyAbsent = fTrue;
                break;
            }
        }
    }
    else
    {
        for (size_t i = 0; i < rgipartPresence.size(); ++i)
        {
            if (!vastp4DMM.pbody->FPartHasModel(rgipartPresence[i]))
            {
                fAnyAbsent = fTrue;
                break;
            }
        }
    }
    if (!fAnyAbsent)
        return fFalse;

    const int32_t kindSelection = vastp4DMM.kindSelected;
    const int32_t ipartSelectionFirst = vastp4DMM.ipartSelectionFirst;
    const int32_t cpartSelection = vastp4DMM.cpartSelection;
    const bool fSpawn = rgipartPresence.size() == 1 ?
        pmvie->FActorStudioSpawnPart(
            pactr, anid, celn, rgipartPresence[0], kindSelection,
            ipartSelectionFirst, cpartSelection) :
        pmvie->FActorStudioSpawnParts(
            pactr, anid, celn, &rgipartPresence[0], (int32_t)rgipartPresence.size(),
            kindSelection, ipartSelectionFirst, cpartSelection);
    if (!fSpawn)
        return fFalse;
    if (!FSet4DMMActorStudioActionCel(anid, celn, fFalse))
    {
        MVIE::MultiLog(pmvie,
            "actor_studio_part_spawn preview_reload_failed action=%ld cel=%ld anchor_part=%ld presence_parts=%ld kind=%ld",
            (long)anid, (long)celn, (long)ipart, (long)rgipartPresence.size(),
            (long)kindSelection);
        return fFalse;
    }

    vastp4DMM.ipartSelected = ipart;
    vastp4DMM.kindSelected = kindSelection;
    vastp4DMM.ipartSelectionFirst = ipartSelectionFirst;
    vastp4DMM.cpartSelection = cpartSelection;
    Apply4DMMActorStudioSelectionHilite();
    vipart4DMMActorStudioSpawnPending = ipart;
    vf4DMMActorStudioSpawnPlacementPending = fTrue;
    MVIE::MultiLog(pmvie,
        "actor_studio_part_spawn prepared action=%ld cel=%ld anchor_part=%ld presence_parts=%ld kind=%ld tdt_object=%d prop_object=%d actor_object=%d object_group=%d whole_root=%d",
        (long)anid, (long)celn, (long)ipart, (long)rgipartPresence.size(),
        (long)kindSelection, (int)fImportedTdtObject, (int)fImportedPropObject,
        (int)fImportedActorObject, (int)fObjectGroup, (int)fWholeRoot);
    Update4DMMActorStudioInfo(pmvie);
    Update4DMMActorStudioButtons();
    Render4DMMActorStudioPreview();
    return fTrue;
}

static bool FBegin4DMMActorStudioPendingSpawnPlacement(void)
{
    if (!vf4DMMActorStudioSpawnPlacementPending ||
        vhwnd4DMMActorStudioViewport == hNil ||
        !IsWindow(vhwnd4DMMActorStudioViewport) || vastp4DMM.pbody == pvNil)
        return fFalse;

    const int32_t ipart = vipart4DMMActorStudioSpawnPending;
    vf4DMMActorStudioSpawnPlacementPending = fFalse;
    vipart4DMMActorStudioSpawnPending = ivNil;
    const bool fLiteralPart = vastp4DMM.kindSelected == kastnPart &&
        vastp4DMM.cpartSelection == 1;
    const bool fDefaultGroup = vastp4DMM.kindSelected == kastnDefaultPartGroup &&
        vastp4DMM.cpartSelection == 1;
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    const bool fImportedObject =
        F4DMMActorStudioSelectedObjectCanTemporalCutSpawn(pmvie);
    const bool fObjectGroup =
        F4DMMActorStudioSelectedObjectGroupCanTemporalCutSpawn(pmvie);
    const bool fWholeRoot =
        F4DMMActorStudioSelectedWholeRootCanTemporalCutSpawn(pmvie);
    if (!FIn(ipart, 0, vastp4DMM.pbody->Cpart()) ||
        vastp4DMM.ipartSelected != ipart ||
        (!fLiteralPart && !fDefaultGroup && !fImportedObject && !fObjectGroup && !fWholeRoot))
        return fFalse;
    if (fLiteralPart && !vastp4DMM.pbody->FPartHasModel(ipart))
        return fFalse;

    Set4DMMActorStudioPartTool(kastReposition);

    // Main-window Hire/Place hides the cursor and makes the object itself the
    // pointer. Start Actor Studio Spawn on the visible center of this Part,
    // not at the Parts-list mouse position that initiated the row click.
    POINT pt;
    if (!FGet4DMMActorStudioPartScreenCenter(ipart, &pt))
    {
        RECT rcClient;
        if (!GetClientRect(vhwnd4DMMActorStudioViewport, &rcClient))
            return fFalse;
        pt.x = (rcClient.left + rcClient.right) / 2;
        pt.y = (rcClient.top + rcClient.bottom) / 2;
        MVIE::MultiLog(vpmvie4DMMActorStudio,
            "actor_studio_part_spawn center_projection_fallback part=%ld xy=%ld,%ld",
            (long)ipart, (long)pt.x, (long)pt.y);
    }
    if (!FBegin4DMMActorStudioPartDrag(
            vhwnd4DMMActorStudioViewport, pt.x, pt.y, fTrue))
        return fFalse;

    vf4DMMActorStudioSpawnPlacement = fTrue;
    vpt4DMMActorStudioSpawnCursorLast = pt;
    vpt4DMMActorStudioSpawnVirtual = pt;

    RECT rcClip;
    if (GetClientRect(vhwnd4DMMActorStudioViewport, &rcClip))
    {
        POINT ptClip = {rcClip.left, rcClip.top};
        if (ClientToScreen(vhwnd4DMMActorStudioViewport, &ptClip))
        {
            OffsetRect(&rcClip, ptClip.x - rcClip.left, ptClip.y - rcClip.top);
            ClipCursor(&rcClip);
        }
    }

    POINT ptScreen = pt;
    if (ClientToScreen(vhwnd4DMMActorStudioViewport, &ptScreen))
        vpappb->PositionCurs(ptScreen.x, ptScreen.y);
    if (!vf4DMMActorStudioSpawnCursorHidden)
    {
        vpappb->HideCurs();
        vf4DMMActorStudioSpawnCursorHidden = fTrue;
    }

    MVIE::MultiLog(vpmvie4DMMActorStudio,
        "actor_studio_part_spawn placement_begin action=%ld cel=%ld anchor_part=%ld kind=%ld xy=%ld,%ld cursor_hidden=1",
        (long)vastp4DMM.anid, (long)vastp4DMM.celn, (long)ipart,
        (long)vastp4DMM.kindSelected, (long)pt.x, (long)pt.y);
    return fTrue;
}

static bool FCancel4DMMActorStudioSpawnPlacement(void)
{
    if (!vf4DMMActorStudioSpawnPlacement || !vastp4DMM.fPartTracking)
        return fFalse;

    FEnd4DMMActorStudioPartDrag(
        vpt4DMMActorStudioSpawnVirtual.x,
        vpt4DMMActorStudioSpawnVirtual.y, fFalse);
    vf4DMMActorStudioSpawnPlacement = fFalse;
    vf4DMMActorStudioSpawnPlacementPending = fFalse;
    vipart4DMMActorStudioSpawnPending = ivNil;
    End4DMMActorStudioSpawnCursor(fFalse);
    MVIE::MultiLog(vpmvie4DMMActorStudio,
        "actor_studio_part_spawn placement_cancel part=%ld",
        (long)vastp4DMM.ipartSelected);
    return fTrue;
}

static bool F4DMMActorStudioPastePart(bool fFreezeCopyPose)
{
    if (!vaspart4DMMActorStudioClipboard.fValid)
        return fFalse;
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
    if (pmvie == pvNil || pactr == pvNil || vastp4DMM.pbody == pvNil)
        return fFalse;

    TAG tagTmpl;
    pactr->GetTagTmpl(&tagTmpl);
    if (pmvie != vaspart4DMMActorStudioClipboard.pmvieSource ||
        tagTmpl.cno != vaspart4DMMActorStudioClipboard.cnoTmplSource ||
        !FIn(vaspart4DMMActorStudioClipboard.ipartSource, 0, vastp4DMM.pbody->Cpart()))
    {
        MVIE::MultiLog(pmvie,
            "actor_studio_part_clipboard paste_reject same_movie=%d current_tmpl=%ld source_tmpl=%ld source_part=%ld parts=%ld",
            (int)(pmvie == vaspart4DMMActorStudioClipboard.pmvieSource),
            (long)tagTmpl.cno, (long)vaspart4DMMActorStudioClipboard.cnoTmplSource,
            (long)vaspart4DMMActorStudioClipboard.ipartSource,
            (long)vastp4DMM.pbody->Cpart());
        return fFalse;
    }

    const BMAT34 *pbmat34Freeze = fFreezeCopyPose ?
        &vaspart4DMMActorStudioClipboard.bmat34CopyPose : pvNil;
    MVIE::MultiLog(pmvie,
        "actor_studio_part_clipboard paste tmpl=%ld source_part=%ld mode=%s copy_action=%ld copy_cel=%ld",
        (long)tagTmpl.cno, (long)vaspart4DMMActorStudioClipboard.ipartSource,
        fFreezeCopyPose ? "frozen_copy_pose" : "keep_positions",
        (long)vaspart4DMMActorStudioClipboard.anidSource,
        (long)vaspart4DMMActorStudioClipboard.celnSource);
    return F4DMMActorStudioDuplicatePart(vaspart4DMMActorStudioClipboard.ipartSource,
                                         !fFreezeCopyPose, pbmat34Freeze);
}

static bool F4DMMActorStudioGotoFrame(int32_t celn)
{
    if (vastp4DMM.ptmpl == pvNil || vastp4DMM.anid < 0)
        return fFalse;
    int32_t ccel = 0;
    if (!vastp4DMM.ptmpl->FGetCcelActn(vastp4DMM.anid, &ccel) || ccel <= 0)
        return fFalse;
    celn = LwMax(0L, LwMin(celn, ccel - 1));
    if (!FSet4DMMActorStudioActionCel(vastp4DMM.anid, celn, fFalse))
        return fFalse;
    Fill4DMMActorStudioFrames();
    Update4DMMActorStudioInfo(vpmvie4DMMActorStudio);
    Update4DMMActorStudioButtons();
    return fTrue;
}

static LRESULT CALLBACK Lresult4DMMActorStudioWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    switch (wm)
    {
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE)
        {
            Clear4DMMMainSceneSelectionForActorStudio("asw_activate");
            Set4DMMActorStudioFastHotkeys(fTrue);
        }
        else
            Update4DMMActorStudioFastHotkeysForForeground();
        break;
    case WM_CREATE:
    {
        vhwnd4DMMActorStudio = hwnd;
        vf4DMMActorStudioSortDefaultPartGroups = fFalse;
        vf4DMMActorStudioHasDefaultPartGroups = fFalse;
        HINSTANCE hinst = GetModuleHandleA(pvNil);
        CreateWindowExA(0, "STATIC", "Actor / Prop", WS_CHILD | WS_VISIBLE,
            12, 8, 180, 18, hwnd, hNil, hinst, pvNil);
        CreateWindowExA(0, "COMBOBOX", "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            12, 26, 676, 260, hwnd, (HMENU)kid4DMMActorStudioObject, hinst, pvNil);
        CreateWindowExA(0, "STATIC", "Action (Animation)", WS_CHILD | WS_VISIBLE,
            12, 60, 180, 18, hwnd, hNil, hinst, pvNil);
        CreateWindowExA(0, "COMBOBOX", "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            12, 78, 676, 260, hwnd, (HMENU)kid4DMMActorStudioActions, hinst, pvNil);

        CreateWindowExA(0, "STATIC", "Animation Frames", WS_CHILD | WS_VISIBLE,
            12, 112, 332, 18, hwnd, hNil, hinst, pvNil);
        HWND hwndFrames = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
                LBS_WANTKEYBOARDINPUT,
            12, 132, 332, 238, hwnd, (HMENU)kid4DMMActorStudioFrames, hinst, pvNil);

        CreateWindowExA(0, "STATIC", "Moveable BODY parts", WS_CHILD | WS_VISIBLE,
            356, 112, 332, 18, hwnd, hNil, hinst, pvNil);
        HWND hwndParts = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
                LBS_WANTKEYBOARDINPUT,
            356, 132, 332, 238, hwnd, (HMENU)kid4DMMActorStudioParts, hinst, pvNil);

        CreateWindowExA(0, "BUTTON", "New Action", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            12, 382, 160, 28, hwnd, (HMENU)kid4DMMActorStudioNewAction, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Save As", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            180, 382, 164, 28, hwnd, (HMENU)kid4DMMActorStudioSaveAs, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            12, 416, 332, 28, hwnd, (HMENU)kid4DMMActorStudioSave, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Play/Pause", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            12, 450, 160, 28, hwnd, (HMENU)kid4DMMActorStudioPlayPause, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Stop", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            180, 450, 164, 28, hwnd, (HMENU)kid4DMMActorStudioStop, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "<< First", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            12, 484, 160, 28, hwnd, (HMENU)kid4DMMActorStudioFirst, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", ">> Last", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            180, 484, 164, 28, hwnd, (HMENU)kid4DMMActorStudioLast, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "> Next", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            12, 518, 160, 28, hwnd, (HMENU)kid4DMMActorStudioNext, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "< Back", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            180, 518, 164, 28, hwnd, (HMENU)kid4DMMActorStudioBack, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "+ New", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            12, 552, 160, 28, hwnd, (HMENU)kid4DMMActorStudioNewFrame, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "- Delete", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            180, 552, 164, 28, hwnd, (HMENU)kid4DMMActorStudioDeleteFrame, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Copy", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            12, 586, 160, 28, hwnd, (HMENU)kid4DMMActorStudioCopyFrame, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Cut", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            180, 586, 164, 28, hwnd, (HMENU)kid4DMMActorStudioCutFrame, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Paste", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            12, 620, 160, 28, hwnd, (HMENU)kid4DMMActorStudioPasteFrame, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Paste Before", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            180, 620, 164, 28, hwnd, (HMENU)kid4DMMActorStudioPasteBefore, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Paste After", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            12, 654, 160, 28, hwnd, (HMENU)kid4DMMActorStudioPasteAfter, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Insert Before", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            180, 654, 164, 28, hwnd, (HMENU)kid4DMMActorStudioInsertBefore, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Insert After", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            12, 688, 332, 28, hwnd, (HMENU)kid4DMMActorStudioInsertAfter, hinst, pvNil);

        CreateWindowExA(0, "BUTTON", "Reposition", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            356, 382, 166, 28, hwnd, (HMENU)kid4DMMActorStudioPosition, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Up/Down",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            522, 382, 166, 28, hwnd, (HMENU)kid4DMMActorStudioVerticalMovement, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Pitch", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            356, 416, 108, 28, hwnd, (HMENU)kid4DMMActorStudioPitch, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Yaw", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            468, 416, 108, 28, hwnd, (HMENU)kid4DMMActorStudioYaw, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Roll", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            580, 416, 108, 28, hwnd, (HMENU)kid4DMMActorStudioRoll, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Toggle Rotation Edge", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            356, 450, 332, 28, hwnd, (HMENU)kid4DMMActorStudioToggleRotationEdge, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Reset Rotation", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            356, 484, 332, 28, hwnd, (HMENU)kid4DMMActorStudioResetRotation, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Grow / Shrink", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            356, 518, 164, 28, hwnd, (HMENU)kid4DMMActorStudioGrowShrink, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Stretch / Squish", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            524, 518, 164, 28, hwnd, (HMENU)kid4DMMActorStudioStretchSquish, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Duplicate", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            356, 552, 332, 28, hwnd, (HMENU)kid4DMMActorStudioDuplicatePart, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Keep positions",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            356, 586, 332, 28, hwnd, (HMENU)kid4DMMActorStudioKeepPositions, hinst, pvNil);
        SendMessageA(GetDlgItem(hwnd, kid4DMMActorStudioKeepPositions), BM_SETCHECK, BST_CHECKED, 0);
        CreateWindowExA(0, "BUTTON", "Cut", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            356, 620, 164, 28, hwnd, (HMENU)kid4DMMActorStudioCutPart, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Delete", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            524, 620, 164, 28, hwnd, (HMENU)kid4DMMActorStudioDeleteTool, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Use Prop", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            356, 654, 164, 28, hwnd, (HMENU)kid4DMMActorStudioUseObject, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Reset Camera", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            524, 654, 164, 28, hwnd, (HMENU)kid4DMMActorStudioResetCamera, hinst, pvNil);

        // ASW bottom-right layout contract: the read-only Info/status box is
        // always the final control at the bottom of the right column. Any new
        // buttons, checkboxes, or rows added to the bottom of Actor Studio go
        // ABOVE this status box, never below it.
        CreateWindowExA(0, "BUTTON", "Show default part group(s)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            356, 688, 332, 28, hwnd, (HMENU)kid4DMMActorStudioSortDefaultPartGroups, hinst, pvNil);
        SendMessageA(GetDlgItem(hwnd, kid4DMMActorStudioSortDefaultPartGroups),
                     BM_SETCHECK, BST_UNCHECKED, 0);
        CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            356, 722, 332, 84, hwnd, (HMENU)kid4DMMActorStudioInfo, hinst, pvNil);

        const int32_t rgid[] = {
            kid4DMMActorStudioObject, kid4DMMActorStudioActions,
            kid4DMMActorStudioFrames, kid4DMMActorStudioParts,
            kid4DMMActorStudioNewAction, kid4DMMActorStudioSaveAs, kid4DMMActorStudioSave,
            kid4DMMActorStudioPlayPause, kid4DMMActorStudioStop,
            kid4DMMActorStudioFirst, kid4DMMActorStudioLast, kid4DMMActorStudioNext,
            kid4DMMActorStudioBack, kid4DMMActorStudioNewFrame, kid4DMMActorStudioDeleteFrame,
            kid4DMMActorStudioCopyFrame, kid4DMMActorStudioCutFrame, kid4DMMActorStudioPasteFrame,
            kid4DMMActorStudioPasteBefore, kid4DMMActorStudioPasteAfter,
            kid4DMMActorStudioInsertBefore, kid4DMMActorStudioInsertAfter,
            kid4DMMActorStudioPitch, kid4DMMActorStudioYaw, kid4DMMActorStudioRoll,
            kid4DMMActorStudioToggleRotationEdge,
            kid4DMMActorStudioPosition, kid4DMMActorStudioVerticalMovement,
            kid4DMMActorStudioResetRotation, kid4DMMActorStudioGrowShrink,
            kid4DMMActorStudioStretchSquish, kid4DMMActorStudioDuplicatePart,
            kid4DMMActorStudioKeepPositions, kid4DMMActorStudioCutPart,
            kid4DMMActorStudioDeleteTool,
            kid4DMMActorStudioUseObject, kid4DMMActorStudioResetCamera,
            kid4DMMActorStudioInfo, kid4DMMActorStudioSortDefaultPartGroups};
        Set4DMMNativeControlFont(hwnd, rgid, SIZEOF(rgid) / SIZEOF(rgid[0]));
        if (hwndFrames != hNil)
        {
            WNDPROC pfnOld = (WNDPROC)SetWindowLongPtrA(hwndFrames, GWLP_WNDPROC,
                                                       (LONG_PTR)Lresult4DMMActorStudioListProc);
            if (vpfn4DMMActorStudioListOld == pvNil)
                vpfn4DMMActorStudioListOld = pfnOld;
        }
        if (hwndParts != hNil)
        {
            WNDPROC pfnOld = (WNDPROC)SetWindowLongPtrA(hwndParts, GWLP_WNDPROC,
                                                       (LONG_PTR)Lresult4DMMActorStudioListProc);
            if (vpfn4DMMActorStudioListOld == pvNil)
                vpfn4DMMActorStudioListOld = pfnOld;
        }

        // Ctrl+Up/Down (Frames) and Alt+Up/Down (Parts) are routed by
        // APPB::_DispatchEvt before Kauai accelerators, exactly like the now-
        // proven Actor Studio tool hotkeys. Do not reserve system-wide Win32
        // hotkeys here: they can steal those chords from other applications
        // merely because Actor Studio remains open in the background.
        MVIE::MultiLog(vpmvie4DMMActorStudio,
            "actor_studio_keynav direct_dispatch ctrl=frames alt=parts");
        vid4DMMActorStudioNavList = kid4DMMActorStudioFrames;
        SetTimer(hwnd, 1, 250, pvNil);
        return 0;
    }
    case WM_TIMER:
        if (wParam == 2)
        {
            int32_t ccel = 0;
            if (vastp4DMM.fPlaying && vastp4DMM.ptmpl != pvNil && vastp4DMM.anid >= 0 &&
                vastp4DMM.ptmpl->FGetCcelActn(vastp4DMM.anid, &ccel) && ccel > 0)
            {
                const int32_t celnNext = (vastp4DMM.celn + 1) % ccel;
                // Drive playback through the exact same frame-navigation path
                // as the working Next/First buttons. This avoids maintaining a
                // second partial frame-switch path inside WM_TIMER.
                F4DMMActorStudioGotoFrame(celnNext);
            }
            return 0;
        }
        FRefresh4DMMActorStudio(fFalse);
        return 0;
    case kwm4DMMActorStudioKey:
        return FDispatch4DMMActorStudioNativeKey(wParam, lParam) ? 1 : 0;
    case WM_KEYDOWN:
        // APPB routes Actor Studio keys here before global accelerators.
        // The legacy direct path remains as a fallback for unusual Win32
        // dispatch paths that bypass APPB.
        // stateful keys such as T/2 or Space and appear to do nothing.
        if (!vf4DMMActorStudioFastHotkeysRegistered)
        {
            if (wParam == VK_ESCAPE)
            {
                Clear4DMMActorStudioPartSelection(fTrue);
                return 0;
            }
            if (wParam == VK_SPACE)
            {
                FToggle4DMMActorStudioPlayback();
                return 0;
            }
            if (FHandle4DMMActorStudioToolKey(wParam))
                return 0;
        }
        break;
    case WM_VKEYTOITEM:
    {
        const int32_t vk = LOWORD(wParam);
        HWND hwndList = (HWND)lParam;
        if (vk == VK_UP || vk == VK_DOWN)
        {
            FNavigate4DMMActorStudioList(hwndList, vk == VK_UP ? -1 : 1, "vkeytoitem");
            return -2;
        }
        break;
    }
    case WM_HOTKEY:
        if (FHandle4DMMActorStudioRegisteredHotKey((int32_t)wParam))
            return 0;
        break;
    case WM_COMMAND:
    {
        const int32_t id = LOWORD(wParam);
        if (id == kid4DMMActorStudioObject && HIWORD(wParam) == CBN_SELCHANGE)
        {
            HWND hwndObject = (HWND)lParam;
            const int32_t irow = (int32_t)SendMessageA(hwndObject, CB_GETCURSEL, 0, 0);
            if (irow != CB_ERR)
            {
                const int32_t iObject = (int32_t)SendMessageA(hwndObject, CB_GETITEMDATA, (WPARAM)irow, 0);
                FSelect4DMMActorStudioObjectEntry(iObject);
            }
            return 0;
        }
        if (id == kid4DMMActorStudioSortDefaultPartGroups && HIWORD(wParam) == BN_CLICKED)
        {
            const bool fTdt = vastp4DMM.ptmpl != pvNil && vastp4DMM.ptmpl->FIsTdt();
            const bool fRequested = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED;
            vf4DMMActorStudioSortDefaultPartGroups =
                fRequested && vf4DMMActorStudioHasDefaultPartGroups && !fTdt;
            SendMessageA((HWND)lParam, BM_SETCHECK,
                         vf4DMMActorStudioSortDefaultPartGroups ? BST_CHECKED : BST_UNCHECKED, 0);
            MVIE::MultiLog(vpmvie4DMMActorStudio,
                "actor_studio_default_part_sort requested=%d enabled=%d available=%d tdt=%d",
                (int)fRequested, (int)vf4DMMActorStudioSortDefaultPartGroups,
                (int)vf4DMMActorStudioHasDefaultPartGroups, (int)fTdt);
            if (vf4DMMActorStudioSortDefaultPartGroups)
                Prepare4DMMActorStudioGroupedViewForSelectedPart();
            Fill4DMMActorStudioParts();
            Update4DMMActorStudioButtons();
            return 0;
        }
        if (id == kid4DMMActorStudioNewAction && HIWORD(wParam) == BN_CLICKED)
        {
            FCreate4DMMActorStudioNewAction();
            return 0;
        }
        if (id == kid4DMMActorStudioPlayPause && HIWORD(wParam) == BN_CLICKED)
        {
            FToggle4DMMActorStudioPlayback();
            return 0;
        }
        if (id == kid4DMMActorStudioStop && HIWORD(wParam) == BN_CLICKED)
        {
            FSet4DMMActorStudioPlaying(fFalse, fTrue);
            return 0;
        }
        if (id == kid4DMMActorStudioFirst && HIWORD(wParam) == BN_CLICKED)
        {
            F4DMMActorStudioGotoFrame(0);
            return 0;
        }
        if (id == kid4DMMActorStudioLast && HIWORD(wParam) == BN_CLICKED)
        {
            int32_t ccel = 0;
            if (vastp4DMM.ptmpl != pvNil && vastp4DMM.ptmpl->FGetCcelActn(vastp4DMM.anid, &ccel) && ccel > 0)
                F4DMMActorStudioGotoFrame(ccel - 1);
            return 0;
        }
        if (id == kid4DMMActorStudioNext && HIWORD(wParam) == BN_CLICKED)
        {
            F4DMMActorStudioGotoFrame(vastp4DMM.celn + 1);
            return 0;
        }
        if (id == kid4DMMActorStudioBack && HIWORD(wParam) == BN_CLICKED)
        {
            F4DMMActorStudioGotoFrame(vastp4DMM.celn - 1);
            return 0;
        }
        if ((id == kid4DMMActorStudioCopyFrame || id == kid4DMMActorStudioFrameMenuCopy) &&
            (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0))
        {
            F4DMMActorStudioCopyFrame();
            return 0;
        }
        if ((id == kid4DMMActorStudioCutFrame || id == kid4DMMActorStudioFrameMenuCut) &&
            (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0))
        {
            F4DMMActorStudioCutFrame();
            return 0;
        }
        if ((id == kid4DMMActorStudioPasteFrame || id == kid4DMMActorStudioFrameMenuPaste) &&
            (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0))
        {
            F4DMMActorStudioWriteClipboard(kasfwReplace);
            return 0;
        }
        if ((id == kid4DMMActorStudioPasteBefore || id == kid4DMMActorStudioFrameMenuPasteBefore) &&
            (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0))
        {
            F4DMMActorStudioWriteClipboard(kasfwInsertBefore);
            return 0;
        }
        if ((id == kid4DMMActorStudioPasteAfter || id == kid4DMMActorStudioFrameMenuPasteAfter) &&
            (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0))
        {
            F4DMMActorStudioWriteClipboard(kasfwInsertAfter);
            return 0;
        }
        if ((id == kid4DMMActorStudioInsertBefore || id == kid4DMMActorStudioFrameMenuInsertBefore) &&
            (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0))
        {
            F4DMMActorStudioInsertDuplicate(kasfwInsertBefore);
            return 0;
        }
        if ((id == kid4DMMActorStudioInsertAfter || id == kid4DMMActorStudioFrameMenuInsertAfter ||
             id == kid4DMMActorStudioNewFrame) &&
            (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0))
        {
            F4DMMActorStudioInsertDuplicate(kasfwInsertAfter);
            return 0;
        }
        if (id == kid4DMMActorStudioDeleteFrame && HIWORD(wParam) == BN_CLICKED)
        {
            F4DMMActorStudioDeleteCurrentFrame();
            return 0;
        }
        if (id == kid4DMMActorStudioDuplicatePart && HIWORD(wParam) == BN_CLICKED)
        {
            F4DMMActorStudioDuplicateSelectedPart();
            return 0;
        }
        if (id == kid4DMMActorStudioSaveAs && HIWORD(wParam) == BN_CLICKED)
        {
            PMVIE pmvie = Pmvie4DMMSettingsCurrent();
            PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
            if (pmvie == pvNil || pactr == pvNil || vastp4DMM.ptmpl == pvNil || vastp4DMM.anid < 0)
                return 0;

            STN stnOld;
            achar szDefault[kcch4DMMCustomName];
            if (vastp4DMM.ptmpl->FGetActnName(vastp4DMM.anid, &stnOld))
                sprintf_s(szDefault, SIZEOF(szDefault), "%s Custom", stnOld.Psz());
            else
                sprintf_s(szDefault, SIZEOF(szDefault), "Custom Animation");
            achar szName[kcch4DMMCustomName];
            if (!FPrompt4DMMAnimationName(szDefault, "Save Animation As", szName, SIZEOF(szName)))
                return 0;

            const int32_t celnKeep = vastp4DMM.celn;
            int32_t anidNew = ivNil;
            if (!pmvie->FActorStudioSaveActionAs(pactr, vastp4DMM.anid, szName, &anidNew))
            {
                MessageBoxA(hwnd, "4DMM could not create the custom animation.",
                            "Save Animation As", MB_OK | MB_ICONERROR);
                return 0;
            }
            if (!FRefresh4DMMActorStudio(fTrue))
                return 0;
            FSet4DMMActorStudioActionCel(anidNew, celnKeep, fFalse);
            Fill4DMMActorStudioObjects();
            Fill4DMMActorStudioActions();
            Fill4DMMActorStudioFrames();
            // Refreshing/promoting a stock action to its custom Save As copy
            // rebuilds the preview BODY and can leave the inherited camera
            // framing stale. Treat Save As completion exactly like the user's
            // Reset Camera button so the new custom action opens cleanly.
            FReset4DMMActorStudioCamera();
            Update4DMMActorStudioInfo(pmvie);
            Update4DMMActorStudioButtons();
            return 0;
        }
        if (id == kid4DMMActorStudioSave && HIWORD(wParam) == BN_CLICKED)
        {
            PMVIE pmvie = Pmvie4DMMSettingsCurrent();
            PACTR pactr = Pactr4DMMActorStudioTarget(pmvie);
            if (pmvie != pvNil && pactr != pvNil &&
                pmvie->FActorStudioSaveAction(pactr, vastp4DMM.anid))
            {
                Update4DMMActorStudioButtons();
                Update4DMMActorStudioInfo(pmvie);
            }
            return 0;
        }
        if (id == kid4DMMActorStudioCutPart && HIWORD(wParam) == BN_CLICKED)
        {
            // Match the main 3DMM Cut toolbar semantics: the button arms a
            // tool. The next ASV click chooses the BODY Part to cut. Alt+click
            // applies Cut to the already-selected literal Part without repick.
            Set4DMMActorStudioPartTool(kastCut);
            return 0;
        }
        if (id == kid4DMMActorStudioDeleteTool && HIWORD(wParam) == BN_CLICKED)
        {
            Set4DMMActorStudioPartTool(kastDelete);
            return 0;
        }
        if (id == kid4DMMActorStudioUseObject && HIWORD(wParam) == BN_CLICKED)
        {
            if (!F4DMMActorStudioUseCurrentObject())
                MessageBoxA(hwnd, "4DMM could not insert this handmade Actor/Prop into the current scene.",
                            "Actor Studio", MB_OK | MB_ICONERROR);
            return 0;
        }
        if (id == kid4DMMActorStudioResetCamera && HIWORD(wParam) == BN_CLICKED)
        {
            FReset4DMMActorStudioCamera();
            return 0;
        }
        if ((id == kid4DMMActorStudioPitch || id == kid4DMMActorStudioYaw ||
             id == kid4DMMActorStudioRoll || id == kid4DMMActorStudioPosition ||
             id == kid4DMMActorStudioGrowShrink || id == kid4DMMActorStudioStretchSquish) &&
            HIWORD(wParam) == BN_CLICKED)
        {
            const int32_t mode = id == kid4DMMActorStudioPitch ? kastPitch :
                                 id == kid4DMMActorStudioYaw ? kastYaw :
                                 id == kid4DMMActorStudioRoll ? kastRoll :
                                 id == kid4DMMActorStudioGrowShrink ? kastGrowShrink :
                                 id == kid4DMMActorStudioStretchSquish ? kastStretchSquish : kastReposition;
            Set4DMMActorStudioPartTool(mode);
            return 0;
        }
        if (id == kid4DMMActorStudioToggleRotationEdge && HIWORD(wParam) == BN_CLICKED)
        {
            FToggle4DMMActorStudioRotationEdge();
            return 0;
        }
        if (id == kid4DMMActorStudioVerticalMovement && HIWORD(wParam) == BN_CLICKED)
        {
            vastp4DMM.fVerticalMovement =
                SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED;
            Set4DMMActorStudioPartTool(kastReposition);
            MVIE::MultiLog(vpmvie4DMMActorStudio,
                "actor_studio_vertical_movement enabled=%d", (int)vastp4DMM.fVerticalMovement);
            Update4DMMActorStudioButtons();
            return 0;
        }
        if (id == kid4DMMActorStudioResetRotation && HIWORD(wParam) == BN_CLICKED)
        {
            FReset4DMMActorStudioPartRotation();
            return 0;
        }
        if (id == kid4DMMActorStudioActions && HIWORD(wParam) == CBN_SELCHANGE)
        {
            HWND hwndActions = (HWND)lParam;
            const int32_t irow = (int32_t)SendMessageA(hwndActions, CB_GETCURSEL, 0, 0);
            if (irow != CB_ERR)
            {
                const int32_t anid = (int32_t)SendMessageA(hwndActions, CB_GETITEMDATA, (WPARAM)irow, 0);
                if (FSet4DMMActorStudioActionCel(anid, 0, fTrue))
                {
                    Fill4DMMActorStudioFrames();
                    Update4DMMActorStudioInfo(vpmvie4DMMActorStudio);
                    Update4DMMActorStudioButtons();
                }
            }
            return 0;
        }
        if (id == kid4DMMActorStudioFrames && HIWORD(wParam) == LBN_SELCHANGE)
        {
            if (vastp4DMM.fPlaying)
                return 0;
            vid4DMMActorStudioNavList = kid4DMMActorStudioFrames;
            HWND hwndFrames = (HWND)lParam;
            const int32_t irow = (int32_t)SendMessageA(hwndFrames, LB_GETCURSEL, 0, 0);
            if (irow != LB_ERR)
            {
                const int32_t celn = (int32_t)SendMessageA(hwndFrames, LB_GETITEMDATA, (WPARAM)irow, 0);
                if (FSet4DMMActorStudioActionCel(vastp4DMM.anid, celn, fFalse))
                    Update4DMMActorStudioInfo(vpmvie4DMMActorStudio);
            }
            return 0;
        }
        if (id == kid4DMMActorStudioParts &&
            (HIWORD(wParam) == LBN_SELCHANGE || HIWORD(wParam) == LBN_DBLCLK))
        {
            if (vastp4DMM.fPlaying)
                return 0;
            vid4DMMActorStudioNavList = kid4DMMActorStudioParts;
            HWND hwndParts = (HWND)lParam;
            const int32_t irow = (int32_t)SendMessageA(hwndParts, LB_GETCURSEL, 0, 0);
            if (irow != LB_ERR)
            {
                const int32_t irowData =
                    (int32_t)SendMessageA(hwndParts, LB_GETITEMDATA, (WPARAM)irow, 0);
                if (HIWORD(wParam) == LBN_DBLCLK)
                    FToggle4DMMActorStudioTreeRow(irowData);
                else
                    FSelect4DMMActorStudioTreeRow(irowData, fFalse);
            }
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        Close4DMMSettingsWindow(hwnd);
        return 0;
    case WM_DESTROY:
    {
        PMVIE pmvieActorStudio = vpmvie4DMMActorStudio;
        HWND hwndOwner = Hwnd4DMMNativeToolOwner();
        HWND hwndCapture = GetCapture();
        MVIE::MultiLog(pmvieActorStudio,
            "actor_studio_close begin controls=%p viewport=%p capture=%p owner=%p foreground=%p tracking=%d/%d/%d playing=%d",
            hwnd, vhwnd4DMMActorStudioViewport, hwndCapture, hwndOwner, GetForegroundWindow(),
            (int)vastp4DMM.fPartTracking, (int)vastp4DMM.fOrbitTracking,
            (int)vastp4DMM.fPanTracking, (int)vastp4DMM.fPlaying);

        Set4DMMActorStudioFastHotkeys(fFalse);
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        KillTimer(hwnd, 3);

        vastp4DMM.fPlaying = fFalse;
        vastp4DMM.fPartTracking = fFalse;
        vastp4DMM.fOrbitTracking = fFalse;
        vastp4DMM.fPanTracking = fFalse;
        vastp4DMM.ipartTrack = ivNil;
        vastp4DMM.anidTrack = ivNil;
        vastp4DMM.celnTrack = ivNil;
        if (hwndCapture == hwnd || hwndCapture == vhwnd4DMMActorStudioViewport ||
            (hwndCapture != hNil && IsChild(hwnd, hwndCapture)))
            ReleaseCapture();

        if (vhwnd4DMMActorStudioViewport != hNil && IsWindow(vhwnd4DMMActorStudioViewport))
            DestroyWindow(vhwnd4DMMActorStudioViewport);
        vhwnd4DMMActorStudioViewport = hNil;
        Reset4DMMActorStudioPreview();
        Release4DMMActorStudioDetachedTarget();
        vrg4DMMActorStudioObjects.clear();
        ClearPb(&vasframe4DMMActorStudioClipboard, SIZEOF(vasframe4DMMActorStudioClipboard));
        vhwnd4DMMActorStudio = hNil;
        varid4DMMActorStudioTarget = aridNil;
        vid4DMMActorStudioGroup = 0;
        vpmvie4DMMActorStudio = pvNil;
        vpfn4DMMActorStudioListOld = pvNil;

        if (pmvieActorStudio != pvNil)
        {
            if (pmvieActorStudio->Pbwld() != pvNil)
                pmvieActorStudio->Pbwld()->MarkDirty();
            pmvieActorStudio->InvalViewsAndScb();
        }
        if (vwig.hwndApp != hNil && IsWindow(vwig.hwndApp))
        {
            InvalidateRect(vwig.hwndApp, pvNil, fFalse);
            UpdateWindow(vwig.hwndApp);
        }
        if (hwndOwner != hNil && IsWindow(hwndOwner))
            InvalidateRect(hwndOwner, pvNil, fFalse);

        MVIE::MultiLog(pmvieActorStudio,
            "actor_studio_close end capture=%p owner=%p owner_enabled=%d foreground=%p",
            GetCapture(), hwndOwner,
            hwndOwner != hNil && IsWindow(hwndOwner) ? (int)IsWindowEnabled(hwndOwner) : -1,
            GetForegroundWindow());
        return 0;
    }
    }
    return DefWindowProcA(hwnd, wm, wParam, lParam);
}

static int32_t Arid4DMMActorStudioFromGroup(PMVIE pmvie, int32_t idGroup)
{
    if (pmvie == pvNil || pmvie->Pscen() == pvNil || idGroup <= 0)
        return aridNil;

    if (pmvie->Pscen()->CactrSelected() == 1 && pmvie->Pscen()->PactrSelected() != pvNil)
    {
        const int32_t aridSelected = pmvie->Pscen()->PactrSelected()->Arid();
        int32_t idSelected = 0;
        if (pmvie->FObjectInObjectGroup(aridSelected, &idSelected) && idSelected == idGroup)
            return aridSelected;
    }

    int32_t aridFallback = aridNil;
    const int32_t cMember = pmvie->CObjectGroupMembers(idGroup);
    for (int32_t iMember = 0; iMember < cMember; ++iMember)
    {
        const OBJECTGROUPMEMBER *pmember = pmvie->PObjectGroupMember(idGroup, iMember);
        if (pmember == pvNil)
            continue;
        PACTR pactr = pmvie->Pscen()->PactrFromArid(pmember->arid);
        if (pactr == pvNil || pactr->Ptmpl() == pvNil)
            continue;
        if (aridFallback == aridNil)
            aridFallback = pactr->Arid();
        // Prefer a stock articulated actor when a whole mixed group is the
        // launch target. Props/3D Words remain inspectable if no actor exists.
        if (!pactr->Ptmpl()->FIsProp() && !pactr->Ptmpl()->FIsTdt())
            return pactr->Arid();
    }
    return aridFallback;
}

static bool F4DMMActorStudioEligibleActor(PACTR pactr)
{
    return pactr != pvNil && pactr->FOnStage() && pactr->Ptmpl() != pvNil &&
        !pactr->Ptmpl()->FIsProp() && !pactr->Ptmpl()->FIsTdt();
}

// SCEN::PactrFromPt() deliberately includes BODY's normal selection-box BACT
// in its closest-hit search. With several selected Object Group members that
// can make the previously selected actor's box intercept an Action-tool
// right-click. Validate the direct result against ACTR::FPtIn(), whose BODY
// filter explicitly ignores the hilite BACT, then fall back to testing every
// articulated actor independently. This is Actor-Studio-only and therefore
// does not alter established 3DMM selection semantics.
static PACTR Pactr4DMMActorStudioFromPoint(PMVIE pmvie, int32_t xp, int32_t yp, int32_t *pibset)
{
    if (pibset != pvNil)
        *pibset = ivNil;
    if (pmvie == pvNil || pmvie->Pscen() == pvNil)
        return pvNil;

    PSCEN pscen = pmvie->Pscen();
    int32_t ibsetDirect = ivNil;
    PACTR pactrDirect = pscen->PactrFromPt(xp, yp, &ibsetDirect);
    if (F4DMMActorStudioEligibleActor(pactrDirect))
    {
        int32_t ibsetExact = ivNil;
        if (pactrDirect->FPtIn(xp, yp, &ibsetExact))
        {
            if (pibset != pvNil)
                *pibset = ibsetExact;
            return pactrDirect;
        }
        MVIE::MultiLog(pmvie,
            "actor_studio_action_pick rejected_hilite_hit arid=%ld xy=%ld,%ld",
            (long)pactrDirect->Arid(), (long)xp, (long)yp);
    }

    PGL pglpactr = pscen->PglRollCall();
    PACTR pactrBest = pvNil;
    int32_t ibsetBest = ivNil;
    int64_t areaBest = INT64_MAX;
    if (pglpactr != pvNil)
    {
        for (int32_t iactr = 0; iactr < pglpactr->IvMac(); ++iactr)
        {
            PACTR pactr = pvNil;
            pglpactr->Get(iactr, &pactr);
            if (!F4DMMActorStudioEligibleActor(pactr))
                continue;

            int32_t ibset = ivNil;
            if (!pactr->FPtIn(xp, yp, &ibset))
                continue;

            RC rc;
            pactr->GetRcBounds(&rc);
            const int64_t dxp = (int64_t)rc.xpRight - (int64_t)rc.xpLeft;
            const int64_t dyp = (int64_t)rc.ypBottom - (int64_t)rc.ypTop;
            const int64_t area = dxp > 0 && dyp > 0 ? dxp * dyp : INT64_MAX - 1;
            if (pactrBest == pvNil || area < areaBest)
            {
                pactrBest = pactr;
                ibsetBest = ibset;
                areaBest = area;
            }
        }
    }

    if (pactrBest != pvNil)
    {
        if (pibset != pvNil)
            *pibset = ibsetBest;
        MVIE::MultiLog(pmvie,
            "actor_studio_action_pick exact_fallback arid=%ld xy=%ld,%ld",
            (long)pactrBest->Arid(), (long)xp, (long)yp);
        return pactrBest;
    }

    // Last resort for a grouped BODY whose BRender ray traversal has no hit:
    // use the actor's latest rendered bounds. Prefer the smallest footprint
    // when boxes overlap so a large surrounding actor cannot swallow a click.
    if (pglpactr != pvNil)
    {
        areaBest = INT64_MAX;
        for (int32_t iactr = 0; iactr < pglpactr->IvMac(); ++iactr)
        {
            PACTR pactr = pvNil;
            pglpactr->Get(iactr, &pactr);
            if (!F4DMMActorStudioEligibleActor(pactr) || !pactr->FIsInView())
                continue;
            RC rc;
            pactr->GetRcBounds(&rc);
            if (!rc.FPtIn(xp, yp))
                continue;
            const int64_t dxp = (int64_t)rc.xpRight - (int64_t)rc.xpLeft;
            const int64_t dyp = (int64_t)rc.ypBottom - (int64_t)rc.ypTop;
            const int64_t area = dxp > 0 && dyp > 0 ? dxp * dyp : INT64_MAX - 1;
            if (pactrBest == pvNil || area < areaBest)
            {
                pactrBest = pactr;
                areaBest = area;
            }
        }
    }
    if (pactrBest != pvNil)
        MVIE::MultiLog(pmvie,
            "actor_studio_action_pick bounds_fallback arid=%ld xy=%ld,%ld",
            (long)pactrBest->Arid(), (long)xp, (long)yp);
    return pactrBest;
}

static bool FOpen4DMMActorStudioActor(int32_t arid, int32_t idGroup)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    if (pmvie == pvNil)
        return fFalse;

    PACTR pactrInitial = pmvie->Pscen() != pvNil && arid != aridNil ?
        pmvie->Pscen()->PactrFromArid(arid) : pvNil;

    // Actor Studio is a movie-level editor, not an Object-Group-only tool.
    // A group/live actor merely supplies the initial target. With no current
    // group (for example immediately after creating a new scene), open the
    // studio anyway and select from its Actor/Prop catalog.
    Release4DMMActorStudioDetachedTarget();
    varid4DMMActorStudioTarget = pactrInitial != pvNil ? arid : aridNil;
    vid4DMMActorStudioGroup = idGroup;
    vpmvie4DMMActorStudio = pmvie;
    Clear4DMMMainSceneSelectionForActorStudio("open");

    HINSTANCE hinst = GetModuleHandleA(pvNil);
    WNDCLASSA wc;
    ClearPb(&wc, SIZEOF(wc));
    if (!GetClassInfoA(hinst, ksz4DMMActorStudioViewportWndClass, &wc))
    {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = Lresult4DMMActorStudioViewportWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(pvNil, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = ksz4DMMActorStudioViewportWndClass;
        if (RegisterClassA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return fFalse;
    }
    ClearPb(&wc, SIZEOF(wc));
    if (!GetClassInfoA(hinst, ksz4DMMActorStudioWndClass, &wc))
    {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = Lresult4DMMActorStudioWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(pvNil, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = ksz4DMMActorStudioWndClass;
        if (RegisterClassA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return fFalse;
    }

    if (vhwnd4DMMActorStudioViewport == hNil || !IsWindow(vhwnd4DMMActorStudioViewport))
    {
        const DWORD styleViewport = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        int32_t dxpPreview = 0;
        int32_t dypPreview = 0;
        Get4DMMActorStudioPreviewSize(&dxpPreview, &dypPreview);
        RECT rcView = {0, 0, dxpPreview, dypPreview};
        AdjustWindowRectEx(&rcView, styleViewport, fFalse, WS_EX_TOOLWINDOW);
        MVIE::MultiLog(pmvie,
            "actor_studio_viewport_config preview=%ldx%ld scaled=%d 1080p=%d scale=%ld/%ld",
            (long)dxpPreview, (long)dypPreview, (int)vfViewportResolution4x,
            (int)vfViewportResolution1080p,
            (long)(vfViewportResolution4x ? vlw4DMMUiScaleNumeratorRequested : 1),
            (long)(vfViewportResolution4x ? vlw4DMMUiScaleDenominatorRequested : 1));
        vhwnd4DMMActorStudioViewport = CreateWindowExA(WS_EX_TOOLWINDOW,
            ksz4DMMActorStudioViewportWndClass, "4DMM Actor Studio Viewport", styleViewport,
            CW_USEDEFAULT, CW_USEDEFAULT, rcView.right - rcView.left, rcView.bottom - rcView.top,
            Hwnd4DMMNativeToolOwner(), hNil, hinst, pvNil);
        if (vhwnd4DMMActorStudioViewport == hNil)
            return fFalse;
    }

    if (vhwnd4DMMActorStudio == hNil || !IsWindow(vhwnd4DMMActorStudio))
    {
        const DWORD styleControls = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        RECT rcControls = {0, 0, 700, 824};
        AdjustWindowRectEx(&rcControls, styleControls, fFalse, WS_EX_TOOLWINDOW);

        RECT rcViewportScreen;
        GetWindowRect(vhwnd4DMMActorStudioViewport, &rcViewportScreen);
        const int32_t xpControls = rcViewportScreen.right + 8;
        const int32_t ypControls = rcViewportScreen.top;
        vhwnd4DMMActorStudio = CreateWindowExA(WS_EX_TOOLWINDOW,
            ksz4DMMActorStudioWndClass, "4DMM Actor Studio", styleControls,
            xpControls, ypControls, rcControls.right - rcControls.left, rcControls.bottom - rcControls.top,
            Hwnd4DMMNativeToolOwner(), hNil, hinst, pvNil);
        if (vhwnd4DMMActorStudio == hNil)
        {
            DestroyWindow(vhwnd4DMMActorStudioViewport);
            vhwnd4DMMActorStudioViewport = hNil;
            return fFalse;
        }
    }

    bool fTargetReady = fFalse;
    if (pactrInitial != pvNil)
        fTargetReady = FRefresh4DMMActorStudio(fTrue);
    else
    {
        Build4DMMActorStudioObjectCatalog(pmvie);
        if (!vrg4DMMActorStudioObjects.empty())
            fTargetReady = FSelect4DMMActorStudioObjectEntry(0);
    }
    if (!fTargetReady)
    {
        if (vhwnd4DMMActorStudio != hNil && IsWindow(vhwnd4DMMActorStudio))
            DestroyWindow(vhwnd4DMMActorStudio);
        return fFalse;
    }

    ShowWindow(vhwnd4DMMActorStudioViewport, SW_SHOWNORMAL);
    ShowWindow(vhwnd4DMMActorStudio, SW_SHOWNORMAL);
    UpdateWindow(vhwnd4DMMActorStudioViewport);
    UpdateWindow(vhwnd4DMMActorStudio);
    SetForegroundWindow(vhwnd4DMMActorStudioViewport);
    return fTrue;
}

static bool FOpen4DMMActorStudio(int32_t idGroup)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    const int32_t arid = Arid4DMMActorStudioFromGroup(pmvie, idGroup);
    return FOpen4DMMActorStudioActor(arid, idGroup);
}

static bool FOpen4DMMActorStudioCurrent(void)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    if (pmvie == pvNil)
        return fFalse;

    int32_t arid = aridNil;
    int32_t idGroup = 0;
    if (pmvie->Pscen() != pvNil && pmvie->Pscen()->PactrSelected() != pvNil)
    {
        arid = pmvie->Pscen()->PactrSelected()->Arid();
        pmvie->FObjectInObjectGroup(arid, &idGroup);
    }
    return FOpen4DMMActorStudioActor(arid, idGroup);
}


static void Update4DMMObjectGroupsControls(void)
{
    if (vhwnd4DMMObjectGroups == hNil || !IsWindow(vhwnd4DMMObjectGroups))
        return;
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    const bool fBind = pmvie != pvNil && pmvie->FCanBindSelectedObjectGroup();
    int32_t aridSelected = aridNil;
    int32_t idSelectedObjectGroup = 0;
    int32_t cSelected = 0;
    if (pmvie != pvNil && pmvie->Pscen() != pvNil)
    {
        cSelected = pmvie->Pscen()->CactrSelected();

        // Remembering that a group-row click selected the whole group is what
        // keeps Abandon disabled for that UI state. If selection subsequently
        // changes through the viewport, detect it directly from the scene too
        // so scaled/forwarded mouse input cannot leave the flag stale.
        if (vf4DMMObjectGroupWholeSelected)
        {
            int32_t cGroupOnStage = 0;
            bool fSameWholeGroup = vid4DMMObjectGroupSelected > 0;
            const int32_t cMember = fSameWholeGroup ?
                pmvie->CObjectGroupMembers(vid4DMMObjectGroupSelected) : 0;
            for (int32_t iMember = 0; iMember < cMember; iMember++)
            {
                const OBJECTGROUPMEMBER *pmember =
                    pmvie->PObjectGroupMember(vid4DMMObjectGroupSelected, iMember);
                PACTR pactr = pmember != pvNil ? pmvie->Pscen()->PactrFromArid(pmember->arid) : pvNil;
                if (pactr != pvNil && pactr->FOnStage())
                    cGroupOnStage++;
            }
            if (cSelected != cGroupOnStage || cSelected == 0)
                fSameWholeGroup = fFalse;
            for (int32_t iSel = 0; fSameWholeGroup && iSel < cSelected; iSel++)
            {
                PACTR pactr = pmvie->Pscen()->PactrSelectedAt(iSel);
                int32_t idGroupT = 0;
                if (pactr == pvNil || !pmvie->FObjectInObjectGroup(pactr->Arid(), &idGroupT) ||
                    idGroupT != vid4DMMObjectGroupSelected)
                {
                    fSameWholeGroup = fFalse;
                }
            }
            if (!fSameWholeGroup)
                vf4DMMObjectGroupWholeSelected = fFalse;
        }

        if (cSelected == 1 && pmvie->Pscen()->PactrSelected() != pvNil)
        {
            aridSelected = pmvie->Pscen()->PactrSelected()->Arid();
            pmvie->FObjectInObjectGroup(aridSelected, &idSelectedObjectGroup);
            // A viewport-selected grouped member identifies its owning group
            // immediately. An ungrouped yellow object deliberately preserves
            // the group row already highlighted as the Join target.
            if (!vf4DMMObjectGroupWholeSelected && idSelectedObjectGroup > 0)
                vid4DMMObjectGroupSelected = idSelectedObjectGroup;
        }
    }

    const bool fGroup = pmvie != pvNil &&
        I4DMMObjectGroupFind(pmvie, vid4DMMObjectGroupSelected) != ivNil;
    const bool fJoin = fGroup && !vf4DMMObjectGroupWholeSelected && cSelected == 1 &&
        aridSelected != aridNil &&
        pmvie->FCanJoinObjectToGroup(vid4DMMObjectGroupSelected, aridSelected);
    const bool fAbandon = !vf4DMMObjectGroupWholeSelected && cSelected == 1 &&
        idSelectedObjectGroup > 0 &&
        pmvie->FCanAbandonObjectFromGroup(idSelectedObjectGroup, aridSelected);
    EnableWindow(GetDlgItem(vhwnd4DMMObjectGroups, kid4DMMObjectGroupsBind), fBind);
    EnableWindow(GetDlgItem(vhwnd4DMMObjectGroups, kid4DMMObjectGroupsUnbind), fGroup);
    EnableWindow(GetDlgItem(vhwnd4DMMObjectGroups, kid4DMMObjectGroupsRename), fGroup);
    EnableWindow(GetDlgItem(vhwnd4DMMObjectGroups, kid4DMMObjectGroupsJoin), fJoin);
    EnableWindow(GetDlgItem(vhwnd4DMMObjectGroups, kid4DMMObjectGroupsAbandon), fAbandon);
    EnableWindow(GetDlgItem(vhwnd4DMMObjectGroups, kid4DMMObjectGroupsDuplicate), fGroup);

    // Create Part records the selected Object Group as a part of a user-owned
    // Actor/Prop definition. Native BODY synthesis is deliberately a later
    // step; Actor Studio remains available for any valid Object Group.
    EnableWindow(GetDlgItem(vhwnd4DMMObjectGroups, kid4DMMObjectGroupsCreateObject), fGroup);
    EnableWindow(GetDlgItem(vhwnd4DMMObjectGroups, kid4DMMObjectGroupsActorStudio), pmvie != pvNil);
}

static void Refresh4DMMObjectGroupsList(bool fForce)
{
    if (vhwnd4DMMObjectGroups == hNil || !IsWindow(vhwnd4DMMObjectGroups))
        return;
    HWND hwndList = GetDlgItem(vhwnd4DMMObjectGroups, kid4DMMObjectGroupsList);
    if (hwndList == hNil)
        return;

    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    const int32_t iscen = pmvie != pvNil ? pmvie->Iscen() : ivNil;
    const int32_t cGroups = pmvie != pvNil ? pmvie->CObjectGroups() : 0;
    const uint32_t dwSignature = Dw4DMMObjectGroupsSignature(pmvie, iscen);
    if (!fForce && pmvie == vpmvie4DMMObjectGroupsLast &&
        iscen == viscen4DMMObjectGroupsLast && cGroups == vc4DMMObjectGroupsLast &&
        dwSignature == vdw4DMMObjectGroupsLastSignature)
    {
        Update4DMMObjectGroupsControls();
        return;
    }
    if (pmvie != vpmvie4DMMObjectGroupsLast)
    {
        vid4DMMObjectGroupSelected = 0;
        vf4DMMObjectGroupWholeSelected = fFalse;
    }
    vpmvie4DMMObjectGroupsLast = pmvie;
    viscen4DMMObjectGroupsLast = iscen;
    vc4DMMObjectGroupsLast = cGroups;
    vdw4DMMObjectGroupsLastSignature = dwSignature;

    SendMessageA(hwndList, LB_RESETCONTENT, 0, 0);
    vrg4DMMObjectGroupRows.clear();
    int32_t iSelectRow = ivNil;

    if (pmvie != pvNil && iscen >= 0)
    {
        for (int32_t iGroup = 0; iGroup < cGroups; iGroup++)
        {
            const OBJECTGROUP *pgroup = pmvie->PObjectGroup(iGroup);
            if (pgroup == pvNil || pgroup->iscen != iscen ||
                !F4DMMObjectGroupPassesFilters(pmvie, pgroup->id))
                continue;

            const bool fExpanded = F4DMMObjectGroupExpanded(pgroup->id);
            achar sz[160];
            sprintf_s(sz, SIZEOF(sz), "%c\t%s%s",
                      fExpanded ? '-' : '+', pgroup->fLocked ? "(Locked) " : "", pgroup->szName);
            LRESULT irow = SendMessageA(hwndList, LB_ADDSTRING, 0, (LPARAM)sz);
            if (irow != LB_ERR && irow != LB_ERRSPACE)
            {
                OGROW row = {fTrue, pgroup->id, aridNil};
                vrg4DMMObjectGroupRows.push_back(row);
                SendMessageA(hwndList, LB_SETITEMDATA, (WPARAM)irow,
                             (LPARAM)(vrg4DMMObjectGroupRows.size() - 1));
                if (pgroup->id == vid4DMMObjectGroupSelected)
                    iSelectRow = (int32_t)irow;
            }

            if (!fExpanded)
                continue;
            const int32_t cMember = pmvie->CObjectGroupMembers(pgroup->id);
            for (int32_t iMember = 0; iMember < cMember; iMember++)
            {
                const OBJECTGROUPMEMBER *pmember = pmvie->PObjectGroupMember(pgroup->id, iMember);
                if (pmember == pvNil || !F4DMMObjectGroupMemberPassesFilters(pmvie, pmember->arid))
                    continue;
                STN stn;
                PACTR pactr = pmvie->Pscen() != pvNil ? pmvie->Pscen()->PactrFromArid(pmember->arid) : pvNil;
                achar szMember[160];
                if (pactr != pvNil)
                {
                    pactr->GetName(&stn);
                    sprintf_s(szMember, SIZEOF(szMember), "    %s", stn.Psz());
                }
                else
                    sprintf_s(szMember, SIZEOF(szMember), "    <missing object %d>", (int)pmember->arid);
                irow = SendMessageA(hwndList, LB_ADDSTRING, 0, (LPARAM)szMember);
                if (irow != LB_ERR && irow != LB_ERRSPACE)
                {
                    OGROW row = {fFalse, pgroup->id, pmember->arid};
                    vrg4DMMObjectGroupRows.push_back(row);
                    SendMessageA(hwndList, LB_SETITEMDATA, (WPARAM)irow,
                                 (LPARAM)(vrg4DMMObjectGroupRows.size() - 1));
                }
            }
        }
    }

    if (iSelectRow != ivNil)
        SendMessageA(hwndList, LB_SETCURSEL, (WPARAM)iSelectRow, 0);
    else
        vid4DMMObjectGroupSelected = 0;
    Update4DMMObjectGroupsControls();
}

static bool F4DMMObjectGroupRow(HWND hwndList, int32_t irow, OGROW *prow)
{
    if (prow == pvNil || irow < 0)
        return fFalse;
    LRESULT idata = SendMessageA(hwndList, LB_GETITEMDATA, (WPARAM)irow, 0);
    if (idata == LB_ERR || idata < 0 || (size_t)idata >= vrg4DMMObjectGroupRows.size())
        return fFalse;
    *prow = vrg4DMMObjectGroupRows[(size_t)idata];
    return fTrue;
}

static LRESULT CALLBACK Lresult4DMMObjectGroupsListProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    if (wm == WM_LBUTTONDOWN)
    {
        POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
        LRESULT litem = SendMessageA(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
        const int32_t irow = LOWORD(litem);
        const bool fOutside = HIWORD(litem) != 0;
        OGROW row;
        if (!fOutside && pt.x < 28 && F4DMMObjectGroupRow(hwnd, irow, &row) && row.fGroup)
        {
            Set4DMMObjectGroupExpanded(row.idGroup, !F4DMMObjectGroupExpanded(row.idGroup));
            Refresh4DMMObjectGroupsList(fTrue);
            return 0;
        }
    }
    return CallWindowProcA(vpfn4DMMObjectGroupsListOld, hwnd, wm, wParam, lParam);
}

static void Save4DMMObjectGroupRename(void)
{
    if (vhwnd4DMMObjectGroupRename == hNil || !IsWindow(vhwnd4DMMObjectGroupRename))
        return;
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    achar szName[kcchObjectGroupName];
    GetWindowTextA(GetDlgItem(vhwnd4DMMObjectGroupRename, kid4DMMObjectGroupRenameEdit),
                   szName, SIZEOF(szName));
    if (pmvie != pvNil && pmvie->FRenameObjectGroup(vid4DMMObjectGroupRename, szName))
        Refresh4DMMObjectGroupsList(fTrue);
    DestroyWindow(vhwnd4DMMObjectGroupRename);
}

static LRESULT CALLBACK Lresult4DMMObjectGroupRenameEditProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    if (wm == WM_KEYDOWN)
    {
        if (wParam == VK_RETURN)
        {
            Save4DMMObjectGroupRename();
            return 0;
        }
        if (wParam == VK_ESCAPE)
        {
            if (vhwnd4DMMObjectGroupRename != hNil && IsWindow(vhwnd4DMMObjectGroupRename))
                DestroyWindow(vhwnd4DMMObjectGroupRename);
            return 0;
        }
    }
    return CallWindowProcA(vpfn4DMMObjectGroupRenameEditOld, hwnd, wm, wParam, lParam);
}

static LRESULT CALLBACK Lresult4DMMObjectGroupRenameWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    switch (wm)
    {
    case WM_CREATE:
    {
        vhwnd4DMMObjectGroupRename = hwnd;
        HINSTANCE hinst = GetModuleHandleA(pvNil);
        HWND hwndEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            12, 12, 336, 26, hwnd, (HMENU)kid4DMMObjectGroupRenameEdit,
            hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        12, 48, 160, 28, hwnd, (HMENU)kid4DMMObjectGroupRenameSave, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        188, 48, 160, 28, hwnd, (HMENU)kid4DMMObjectGroupRenameCancel, hinst, pvNil);
        const int32_t rgidRename[] = {kid4DMMObjectGroupRenameEdit, kid4DMMObjectGroupRenameSave,
                                     kid4DMMObjectGroupRenameCancel};
        Set4DMMNativeControlFont(hwnd, rgidRename, SIZEOF(rgidRename) / SIZEOF(rgidRename[0]));
        if (hwndEdit != hNil)
        {
            // Kauai normally consumes keyboard messages into cidKey. Native edit
            // controls opt out through this property so typing/backspace/delete
            // follow the ordinary Win32 EDIT path.
            SetPropA(hwndEdit, "3DMMExNativeTextInput", (HANDLE)1);
            vpfn4DMMObjectGroupRenameEditOld = (WNDPROC)SetWindowLongPtrA(hwndEdit, GWLP_WNDPROC,
                                                        (LONG_PTR)Lresult4DMMObjectGroupRenameEditProc);
            PMVIE pmvie = Pmvie4DMMSettingsCurrent();
            int32_t iGroup = I4DMMObjectGroupFind(pmvie, vid4DMMObjectGroupRename);
            const OBJECTGROUP *pgroup = iGroup != ivNil ? pmvie->PObjectGroup(iGroup) : pvNil;
            if (pgroup != pvNil)
                SetWindowTextA(hwndEdit, pgroup->szName);
            SetFocus(hwndEdit);
            SendMessageA(hwndEdit, EM_SETSEL, 0, -1);
        }
        return 0;
    }
    case WM_SIZE:
    {
        RECT rc;
        if (GetClientRect(hwnd, &rc))
        {
            const int32_t margin = 12;
            const int32_t gap = 8;
            const int32_t dypButton = 28;
            const int32_t ypButton = LwMax(46L, rc.bottom - margin - dypButton);
            const int32_t dxpAvail = LwMax(160L, rc.right - margin * 2);
            const int32_t dxpButton = LwMax(70L, (dxpAvail - gap) / 2);
            SetWindowPos(GetDlgItem(hwnd, kid4DMMObjectGroupRenameEdit), hNil,
                         margin, margin, dxpAvail, 26, SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(GetDlgItem(hwnd, kid4DMMObjectGroupRenameSave), hNil,
                         margin, ypButton, dxpButton, dypButton, SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(GetDlgItem(hwnd, kid4DMMObjectGroupRenameCancel), hNil,
                         margin + dxpButton + gap, ypButton, rc.right - margin - (margin + dxpButton + gap),
                         dypButton, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED)
        {
            if (LOWORD(wParam) == kid4DMMObjectGroupRenameSave)
            {
                Save4DMMObjectGroupRename();
                return 0;
            }
            if (LOWORD(wParam) == kid4DMMObjectGroupRenameCancel)
            {
                DestroyWindow(hwnd);
                return 0;
            }
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd); // X cancels by design.
        return 0;
    case WM_DESTROY:
    {
        HWND hwndOwner = GetWindow(hwnd, GW_OWNER);
        vhwnd4DMMObjectGroupRename = hNil;
        vid4DMMObjectGroupRename = 0;
        vpfn4DMMObjectGroupRenameEditOld = pvNil;
        if (hwndOwner != hNil && IsWindow(hwndOwner) && IsWindowVisible(hwndOwner))
        {
            if (F4DMMUiScaleActive())
                F4DMMParkUiScaleSourceWindow();
            SetForegroundWindow(hwndOwner);
            SetFocus(hwndOwner);
        }
        return 0;
    }
    }
    return DefWindowProcA(hwnd, wm, wParam, lParam);
}

static bool FOpen4DMMObjectGroupRename(int32_t idGroup)
{
    if (idGroup <= 0)
        return fFalse;
    if (vhwnd4DMMObjectGroupRename != hNil && IsWindow(vhwnd4DMMObjectGroupRename))
        DestroyWindow(vhwnd4DMMObjectGroupRename);
    vid4DMMObjectGroupRename = idGroup;

    HINSTANCE hinst = GetModuleHandleA(pvNil);
    WNDCLASSA wc;
    ClearPb(&wc, SIZEOF(wc));
    if (!GetClassInfoA(hinst, ksz4DMMObjectGroupRenameWndClass, &wc))
    {
        wc.lpfnWndProc = Lresult4DMMObjectGroupRenameWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(pvNil, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = ksz4DMMObjectGroupRenameWndClass;
        if (RegisterClassA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return fFalse;
    }

    RECT rc = {0, 0, 360, 88};
    const DWORD style = WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectEx(&rc, style, fFalse, WS_EX_TOOLWINDOW);
    vhwnd4DMMObjectGroupRename = CreateWindowExA(WS_EX_TOOLWINDOW,
        ksz4DMMObjectGroupRenameWndClass, "Rename Object Group", style,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        vhwnd4DMMObjectGroups != hNil ? vhwnd4DMMObjectGroups : Hwnd4DMMNativeToolOwner(),
        hNil, hinst, pvNil);
    if (vhwnd4DMMObjectGroupRename == hNil)
        return fFalse;
    ShowWindow(vhwnd4DMMObjectGroupRename, SW_SHOWNORMAL);
    SetForegroundWindow(vhwnd4DMMObjectGroupRename);
    return fTrue;
}

static const int32_t kid4DMMCreatePartName = 0x5480;
static const int32_t kid4DMMCreatePartActor = 0x5481;
static const int32_t kid4DMMCreatePartProp = 0x5482;
static const int32_t kid4DMMCreatePartNew = 0x5483;
static const int32_t kid4DMMCreatePartTemplate = 0x5484;
static const int32_t kid4DMMCreatePartTemplateList = 0x5485;
static const int32_t kid4DMMCreatePartOk = 0x5486;
static const int32_t kid4DMMCreatePartCancel = 0x5487;
static const int32_t kid4DMMCreatePartAddToObject = 0x5488;
static const achar ksz4DMMCreatePartWndClass[] = "4DMMCreatePartWindow";

struct CREATEPARTTEMPLATE
{
    int32_t templateKind;
    int32_t idTemplateCustom;
    int32_t sidTemplate;
    CTG ctgTemplate;
    CNO cnoTemplate;
    bool fProp;
    achar szName[128];
};

struct CREATEPARTSTATE
{
    bool fDone;
    bool fOk;
    int32_t idGroup;
    int32_t aridSource;
    std::vector<CREATEPARTTEMPLATE> templates;
};

static void Add4DMMCreatePartCustomTemplates(PMVIE pmvie, std::vector<CREATEPARTTEMPLATE> *ptemplates)
{
    if (pmvie == pvNil || ptemplates == pvNil)
        return;
    // Required ordering: user actors, user props.
    for (int32_t iPass = 0; iPass < 2; ++iPass)
    {
        const bool fProp = iPass != 0;
        for (int32_t i = 0; i < pmvie->C4DMMCustomObjects(); ++i)
        {
            const CUSTOMOBJECT *pobj = pmvie->P4DMMCustomObject(i);
            if (pobj == pvNil || !pmvie->F4DMMCustomObjectIsHandmade(i) || pobj->fProp != fProp)
                continue;
            CREATEPARTTEMPLATE item;
            ClearPb(&item, SIZEOF(item));
            item.templateKind = kctkCustomObject;
            item.idTemplateCustom = pobj->id;
            item.fProp = pobj->fProp;
            sprintf_s(item.szName, SIZEOF(item.szName), "%s: %s",
                      item.fProp ? "Custom Prop" : "Custom Actor", pobj->szName);
            ptemplates->push_back(item);
        }
    }
}

static void Add4DMMCreatePartDefaultTemplates(bool fProp, std::vector<CREATEPARTTEMPLATE> *ptemplates)
{
    if (ptemplates == pvNil || vptagm == pvNil)
        return;

    PCRM pcrm = CRM::PcrmNew(1);
    if (pcrm == pvNil)
        return;
    CKI ckiRoot;
    ckiRoot.ctg = fProp ? kctgPrth : kctgTmth;
    ckiRoot.cno = cnoNil;
    PBCL pbcl = BCL::PbclNew(pcrm, &ckiRoot, ctgNil, pvNil, fTrue);
    if (pbcl != pvNil)
    {
        for (int32_t ithd = 0; ithd < pbcl->IthdMac(); ++ithd)
        {
            THD thd;
            ClearPb(&thd, SIZEOF(thd));
            pbcl->GetThd(ithd, &thd);
            PTMPL ptmpl = (PTMPL)vptagm->PbacoFetch(&thd.tag, TMPL::FReadTmpl);
            if (ptmpl == pvNil)
                continue;
            const bool fMatch = !ptmpl->FIsTdt() && ptmpl->FIsProp() == fProp;
            if (fMatch)
            {
                bool fDuplicate = fFalse;
                for (size_t i = 0; i < ptemplates->size(); ++i)
                {
                    const CREATEPARTTEMPLATE &old = (*ptemplates)[i];
                    if (old.templateKind == kctkDefault && old.sidTemplate == thd.tag.sid &&
                        old.ctgTemplate == thd.tag.ctg && old.cnoTemplate == thd.tag.cno)
                    {
                        fDuplicate = fTrue;
                        break;
                    }
                }
                if (!fDuplicate)
                {
                    CREATEPARTTEMPLATE item;
                    ClearPb(&item, SIZEOF(item));
                    item.templateKind = kctkDefault;
                    item.sidTemplate = thd.tag.sid;
                    item.ctgTemplate = thd.tag.ctg;
                    item.cnoTemplate = thd.tag.cno;
                    item.fProp = fProp;
                    STN stn;
                    ptmpl->GetName(&stn);
                    sprintf_s(item.szName, SIZEOF(item.szName), "%s: %s",
                              fProp ? "Prop" : "Actor", stn.Psz());
                    ptemplates->push_back(item);
                }
            }
            ReleasePpo(&ptmpl);
        }
    }
    ReleasePpo(&pbcl);
    ReleasePpo(&pcrm);
}

static int32_t C4DMMCreatePartCustomTemplates(const CREATEPARTSTATE *pst)
{
    if (pst == pvNil)
        return 0;
    int32_t c = 0;
    for (size_t i = 0; i < pst->templates.size(); ++i)
        if (pst->templates[i].templateKind == kctkCustomObject)
            ++c;
    return c;
}

static void Fill4DMMCreatePartTemplateCombo(HWND hwnd, CREATEPARTSTATE *pst, bool fCustomOnly)
{
    if (hwnd == hNil || pst == pvNil)
        return;
    HWND hwndCombo = GetDlgItem(hwnd, kid4DMMCreatePartTemplateList);
    SendMessageA(hwndCombo, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < pst->templates.size(); ++i)
    {
        if (fCustomOnly && pst->templates[i].templateKind != kctkCustomObject)
            continue;
        const LRESULT irow = SendMessageA(hwndCombo, CB_ADDSTRING, 0,
                                          (LPARAM)pst->templates[i].szName);
        if (irow != CB_ERR && irow != CB_ERRSPACE)
            SendMessageA(hwndCombo, CB_SETITEMDATA, (WPARAM)irow, (LPARAM)i);
    }
    if (SendMessageA(hwndCombo, CB_GETCOUNT, 0, 0) > 0)
        SendMessageA(hwndCombo, CB_SETCURSEL, 0, 0);
}

static void Update4DMMCreatePartMode(HWND hwnd, CREATEPARTSTATE *pst)
{
    if (hwnd == hNil || pst == pvNil)
        return;
    const bool fTemplate = IsDlgButtonChecked(hwnd, kid4DMMCreatePartTemplate) == BST_CHECKED;
    const bool fAdd = IsDlgButtonChecked(hwnd, kid4DMMCreatePartAddToObject) == BST_CHECKED;
    const bool fTargetExisting = fTemplate || fAdd;
    EnableWindow(GetDlgItem(hwnd, kid4DMMCreatePartTemplateList), fTargetExisting);
    EnableWindow(GetDlgItem(hwnd, kid4DMMCreatePartName), !fTargetExisting);
    EnableWindow(GetDlgItem(hwnd, kid4DMMCreatePartActor), !fTargetExisting);
    EnableWindow(GetDlgItem(hwnd, kid4DMMCreatePartProp), !fTargetExisting);
    // Both existing-object modes target only movie-owned custom Actor/Props.
    // "Use existing object as template" intentionally preserves the old
    // Add-to-object model-CHID reuse behavior; normal Add-to-object no longer does.
    Fill4DMMCreatePartTemplateCombo(hwnd, pst, fTargetExisting);
}

static LRESULT CALLBACK Lresult4DMMCreatePartWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    CREATEPARTSTATE *pst = (CREATEPARTSTATE *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch (wm)
    {
    case WM_CREATE:
    {
        CREATESTRUCTA *pcs = (CREATESTRUCTA *)lParam;
        pst = (CREATEPARTSTATE *)pcs->lpCreateParams;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)pst);
        HINSTANCE hinst = GetModuleHandleA(pvNil);
        CreateWindowExA(0, "STATIC", "Object name:", WS_CHILD | WS_VISIBLE,
                        12, 14, 110, 20, hwnd, hNil, hinst, pvNil);
        HWND hwndName = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                        124, 10, 392, 25, hwnd, (HMENU)kid4DMMCreatePartName, hinst, pvNil);
        if (hwndName != hNil)
            SetPropA(hwndName, "3DMMExNativeTextInput", (HANDLE)1);

        CreateWindowExA(0, "STATIC", "Object type:", WS_CHILD | WS_VISIBLE,
                        12, 50, 110, 20, hwnd, hNil, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Actor", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON,
                        124, 46, 100, 24, hwnd, (HMENU)kid4DMMCreatePartActor, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Prop", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                        230, 46, 100, 24, hwnd, (HMENU)kid4DMMCreatePartProp, hinst, pvNil);

        CreateWindowExA(0, "BUTTON", "Create new object",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON,
                        12, 84, 180, 24, hwnd, (HMENU)kid4DMMCreatePartNew, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Add to obj.",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                        202, 84, 180, 24, hwnd, (HMENU)kid4DMMCreatePartAddToObject, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Use existing object as template",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                        12, 112, 350, 24, hwnd, (HMENU)kid4DMMCreatePartTemplate, hinst, pvNil);

        CreateWindowExA(0, "STATIC", "Use which?", WS_CHILD | WS_VISIBLE,
                        12, 150, 110, 20, hwnd, hNil, hinst, pvNil);
        CreateWindowExA(0, "COMBOBOX", "",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                        124, 146, 392, 260, hwnd, (HMENU)kid4DMMCreatePartTemplateList, hinst, pvNil);

        CreateWindowExA(0, "BUTTON", "OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        346, 192, 80, 30, hwnd, (HMENU)kid4DMMCreatePartOk, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        436, 192, 80, 30, hwnd, (HMENU)kid4DMMCreatePartCancel, hinst, pvNil);
        const int32_t rgid[] = {kid4DMMCreatePartName, kid4DMMCreatePartActor, kid4DMMCreatePartProp,
            kid4DMMCreatePartNew, kid4DMMCreatePartTemplate, kid4DMMCreatePartAddToObject,
            kid4DMMCreatePartTemplateList, kid4DMMCreatePartOk, kid4DMMCreatePartCancel};
        Set4DMMNativeControlFont(hwnd, rgid, SIZEOF(rgid) / SIZEOF(rgid[0]));
        CheckRadioButton(hwnd, kid4DMMCreatePartActor, kid4DMMCreatePartProp, kid4DMMCreatePartActor);
        CheckRadioButton(hwnd, kid4DMMCreatePartNew, kid4DMMCreatePartAddToObject, kid4DMMCreatePartNew);
        Update4DMMCreatePartMode(hwnd, pst);
        SetFocus(hwndName);
        return 0;
    }
    case WM_COMMAND:
    {
        const int32_t id = LOWORD(wParam);
        if ((id == kid4DMMCreatePartNew || id == kid4DMMCreatePartTemplate ||
             id == kid4DMMCreatePartAddToObject) && HIWORD(wParam) == BN_CLICKED)
        {
            if ((id == kid4DMMCreatePartAddToObject || id == kid4DMMCreatePartTemplate) &&
                C4DMMCreatePartCustomTemplates(pst) <= 0)
            {
                // Restore the first radio before showing the requested error,
                // so dismissing the message never leaves an impossible mode checked.
                CheckRadioButton(hwnd, kid4DMMCreatePartNew, kid4DMMCreatePartAddToObject,
                                 kid4DMMCreatePartNew);
                Update4DMMCreatePartMode(hwnd, pst);
                MessageBoxA(hwnd, "Create custom actor or prop first.",
                            "Create New Actor or Prop", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            Update4DMMCreatePartMode(hwnd, pst);
            return 0;
        }
        if (id == kid4DMMCreatePartCancel && HIWORD(wParam) == BN_CLICKED)
        {
            if (pst != pvNil)
                pst->fDone = fTrue;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == kid4DMMCreatePartOk && HIWORD(wParam) == BN_CLICKED && pst != pvNil)
        {
            PMVIE pmvie = Pmvie4DMMSettingsCurrent();
            if (pmvie == pvNil)
                return 0;

            const bool fAdd = IsDlgButtonChecked(hwnd, kid4DMMCreatePartAddToObject) == BST_CHECKED;
            const bool fTemplate = IsDlgButtonChecked(hwnd, kid4DMMCreatePartTemplate) == BST_CHECKED;
            if (fAdd || fTemplate)
            {
                HWND hwndCombo = GetDlgItem(hwnd, kid4DMMCreatePartTemplateList);
                const LRESULT irow = SendMessageA(hwndCombo, CB_GETCURSEL, 0, 0);
                const LRESULT iTemplate = irow != CB_ERR ?
                    SendMessageA(hwndCombo, CB_GETITEMDATA, (WPARAM)irow, 0) : CB_ERR;
                if (iTemplate == CB_ERR || iTemplate < 0 || (size_t)iTemplate >= pst->templates.size() ||
                    pst->templates[(size_t)iTemplate].templateKind != kctkCustomObject)
                {
                    MessageBoxA(hwnd, "Select a custom actor or prop.",
                                "Create New Actor or Prop", MB_OK | MB_ICONINFORMATION);
                    return 0;
                }
                const CREATEPARTTEMPLATE &item = pst->templates[(size_t)iTemplate];
                if (!pmvie->FAdd4DMMCustomPartToObject(item.idTemplateCustom, pst->idGroup,
                                                        pst->aridSource, fTemplate))
                {
                    MessageBoxA(hwnd, "4DMM could not add this part to the selected custom object.",
                                "Create New Actor or Prop", MB_OK | MB_ICONERROR);
                    return 0;
                }
            }
            else
            {
                achar szName[kcch4DMMCustomName];
                GetWindowTextA(GetDlgItem(hwnd, kid4DMMCreatePartName), szName, SIZEOF(szName));
                if (szName[0] == 0)
                {
                    MessageBoxA(hwnd, "Enter an object name.", "Create New Actor or Prop",
                                MB_OK | MB_ICONINFORMATION);
                    return 0;
                }

                const bool fProp = IsDlgButtonChecked(hwnd, kid4DMMCreatePartProp) == BST_CHECKED;
                const int32_t templateKind = kctkNone;
                const int32_t idTemplateCustom = 0;
                const int32_t sidTemplate = 0;
                const CTG ctgTemplate = ctgNil;
                const CNO cnoTemplate = cnoNil;

                const bool fCreated = pst->idGroup > 0 ?
                    pmvie->FCreate4DMMCustomObjectPart(szName, fProp, templateKind, idTemplateCustom,
                                                       sidTemplate, ctgTemplate, cnoTemplate, pst->idGroup) :
                    pmvie->FCreate4DMMCustomObjectPartFromActor(szName, fProp, templateKind, idTemplateCustom,
                                                                sidTemplate, ctgTemplate, cnoTemplate, pst->aridSource);
                if (!fCreated)
                {
                    MessageBoxA(hwnd,
                        "4DMM could not create this custom object definition. The name may already be in use.",
                        "Create New Actor or Prop", MB_OK | MB_ICONERROR);
                    return 0;
                }
            }
            pst->fOk = fTrue;
            pst->fDone = fTrue;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        if (pst != pvNil)
            pst->fDone = fTrue;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcA(hwnd, wm, wParam, lParam);
}

static bool FOpen4DMMCreatePartSource(int32_t idGroup, int32_t aridSource)
{
    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    const bool fGroupSource = idGroup > 0 && I4DMMObjectGroupFind(pmvie, idGroup) != ivNil;
    const bool fActorSource = aridSource != aridNil && pmvie != pvNil && pmvie->Pscen() != pvNil &&
                              pmvie->Pscen()->PactrFromArid(aridSource) != pvNil;
    if (pmvie == pvNil || (!fGroupSource && !fActorSource))
        return fFalse;

    CREATEPARTSTATE st;
    st.fDone = fFalse;
    st.fOk = fFalse;
    st.idGroup = fGroupSource ? idGroup : 0;
    st.aridSource = fActorSource ? aridSource : aridNil;
    Add4DMMCreatePartCustomTemplates(pmvie, &st.templates);
    // Required ordering after custom content: stock actors, stock props.
    Add4DMMCreatePartDefaultTemplates(fFalse, &st.templates);
    Add4DMMCreatePartDefaultTemplates(fTrue, &st.templates);

    HINSTANCE hinst = GetModuleHandleA(pvNil);
    WNDCLASSA wc;
    ClearPb(&wc, SIZEOF(wc));
    if (!GetClassInfoA(hinst, ksz4DMMCreatePartWndClass, &wc))
    {
        wc.lpfnWndProc = Lresult4DMMCreatePartWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(pvNil, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = ksz4DMMCreatePartWndClass;
        if (RegisterClassA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return fFalse;
    }

    HWND hwndOwner = fGroupSource && vhwnd4DMMObjectGroups != hNil && IsWindow(vhwnd4DMMObjectGroups) ?
                     vhwnd4DMMObjectGroups : Hwnd4DMMNativeToolOwner();

    // Create Part runs its own modal message loop. Its historical owner was the
    // OGW when launched from an Object Group, which left the separate main
    // 3DMM source/presentation windows enabled. A click on the movie could
    // therefore enter Kauai's drag path while the dialog still owned focus;
    // the matching mouse-up could be lost and leave the main cursor hidden.
    // Make this dialog application-modal with respect to the two main 3DMM
    // surfaces and discard any stale button/move packets before restoring them.
    HWND hwndMainSource = vwig.hwndApp;
    HWND hwndMainScale = hwndMainSource != hNil && IsWindow(hwndMainSource) ?
        (HWND)GetPropA(hwndMainSource, "4DMMUiScaleWindow") : hNil;
    const bool fOwnerEnabled = hwndOwner != hNil && IsWindow(hwndOwner) && IsWindowEnabled(hwndOwner);
    const bool fMainSourceEnabled = hwndMainSource != hNil && IsWindow(hwndMainSource) &&
                                    IsWindowEnabled(hwndMainSource);
    const bool fMainScaleEnabled = hwndMainScale != hNil && IsWindow(hwndMainScale) &&
                                   IsWindowEnabled(hwndMainScale);
    const bool fCameraInputWasActive = pmvie->FCameraInputActive();

    if (fOwnerEnabled)
        EnableWindow(hwndOwner, fFalse);
    if (fMainSourceEnabled && hwndMainSource != hwndOwner)
        EnableWindow(hwndMainSource, fFalse);
    if (fMainScaleEnabled && hwndMainScale != hwndOwner)
        EnableWindow(hwndMainScale, fFalse);

    MSG msgMouse;
    if (hwndMainSource != hNil && IsWindow(hwndMainSource))
        while (PeekMessageA(&msgMouse, hwndMainSource, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE | PM_NOYIELD))
            ;
    if (hwndMainScale != hNil && IsWindow(hwndMainScale))
        while (PeekMessageA(&msgMouse, hwndMainScale, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE | PM_NOYIELD))
            ;

    RECT rc = {0, 0, 540, 240};
    const DWORD style = WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectEx(&rc, style, fFalse, WS_EX_TOOLWINDOW | WS_EX_DLGMODALFRAME);
    HWND hwnd = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_DLGMODALFRAME,
        ksz4DMMCreatePartWndClass, "Create New Actor or Prop", style,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        hwndOwner, hNil, hinst, &st);
    if (hwnd == hNil)
    {
        if (fMainSourceEnabled && hwndMainSource != hwndOwner && IsWindow(hwndMainSource))
            EnableWindow(hwndMainSource, fTrue);
        if (fMainScaleEnabled && hwndMainScale != hwndOwner && IsWindow(hwndMainScale))
            EnableWindow(hwndMainScale, fTrue);
        if (fOwnerEnabled && hwndOwner != hNil && IsWindow(hwndOwner))
            EnableWindow(hwndOwner, fTrue);
        return fFalse;
    }
    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd);

    MSG msg;
    while (!st.fDone && GetMessage(&msg, pvNil, 0, 0) > 0)
    {
        if (msg.message == WM_KEYDOWN && (msg.hwnd == hwnd || IsChild(hwnd, msg.hwnd)))
        {
            if (msg.wParam == VK_ESCAPE)
            {
                SendMessageA(hwnd, WM_CLOSE, 0, 0);
                continue;
            }
            if (msg.wParam == VK_RETURN && GetFocus() != GetDlgItem(hwnd, kid4DMMCreatePartTemplateList))
            {
                SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(kid4DMMCreatePartOk, BN_CLICKED),
                             (LPARAM)GetDlgItem(hwnd, kid4DMMCreatePartOk));
                continue;
            }
        }
        if (!IsDialogMessageA(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    // Remove any input aimed at the main editor while Create Part was modal.
    // In particular, never let a pre-close mouse-down become a post-close
    // Hand/Reposition gesture with no corresponding mouse-up.
    if (hwndMainSource != hNil && IsWindow(hwndMainSource))
        while (PeekMessageA(&msgMouse, hwndMainSource, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE | PM_NOYIELD))
            ;
    if (hwndMainScale != hNil && IsWindow(hwndMainScale))
        while (PeekMessageA(&msgMouse, hwndMainScale, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE | PM_NOYIELD))
            ;

    HWND hwndCapture = GetCapture();
    if (hwndCapture == hwndMainSource || hwndCapture == hwndMainScale)
    {
        ReleaseCapture();
        ClipCursor(pvNil);
    }

    if (fMainSourceEnabled && hwndMainSource != hwndOwner && IsWindow(hwndMainSource))
        EnableWindow(hwndMainSource, fTrue);
    if (fMainScaleEnabled && hwndMainScale != hwndOwner && IsWindow(hwndMainScale))
        EnableWindow(hwndMainScale, fTrue);
    if (fOwnerEnabled && hwndOwner != hNil && IsWindow(hwndOwner))
        EnableWindow(hwndOwner, fTrue);

    // A normal Create Part session begins and ends with an ordinary visible
    // editor cursor. Normalize Win32's thread-local ShowCursor count in case a
    // stale main-window drag hid it before the modal guard took effect. Do not
    // interfere with an intentionally active Free/Manual Camera session.
    if (!fCameraInputWasActive)
    {
        int32_t cactShow = ShowCursor(fTrue);
        while (cactShow < 0)
            cactShow = ShowCursor(fTrue);
        while (cactShow > 0)
            cactShow = ShowCursor(fFalse);

        if (hwndMainSource != hNil && IsWindow(hwndMainSource))
            SendMessageA(hwndMainSource, WM_SETCURSOR, (WPARAM)hwndMainSource,
                         MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    }

    if (hwndOwner != hNil && IsWindow(hwndOwner))
        SetForegroundWindow(hwndOwner);
    return FPure(st.fOk);
}

static bool FOpen4DMMCreatePart(int32_t idGroup)
{
    return FOpen4DMMCreatePartSource(idGroup, aridNil);
}

bool FOpen4DMMCreatePartForActor(int32_t arid)
{
    return FOpen4DMMCreatePartSource(0, arid);
}

static LRESULT CALLBACK Lresult4DMMObjectGroupsWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    if (wm == kwm4DMMObjectGroupsSelectionRefresh)
    {
        // Scene selection can change while this tool window is unfocused.
        // Re-evaluate the live predicates immediately so Bind/Join/Abandon
        // never wait for activation or the polling timer to repaint state.
        Update4DMMObjectGroupsControls();
        return 0;
    }

    if (wm == kwm4DMMObjectGroupsUndoRefresh)
    {
        // Membership undo/redo has already swapped the authoritative MVIE
        // tables and rebuilt the runtime BRender parents. Force one list
        // rebuild immediately, but preserve this movie's expansion/selection
        // state. The forced rebuild redraws the rows from the restored MVIE
        // table and re-runs the existing live control predicates.
        Refresh4DMMObjectGroupsList(fTrue);
        return 0;
    }

    if (wm == kwm4DMMObjectGroupsMovieRefresh)
    {
        // Reset every cache field, including selected/expanded UI state that
        // belongs to the previous movie, then rebuild from the replacement
        // MVIE unconditionally.
        vid4DMMObjectGroupSelected = 0;
        vf4DMMObjectGroupWholeSelected = fFalse;
        vpmvie4DMMObjectGroupsLast = pvNil;
        viscen4DMMObjectGroupsLast = ivNil;
        vc4DMMObjectGroupsLast = -1;
        vdw4DMMObjectGroupsLastSignature = 0;
        vrgid4DMMObjectGroupExpanded.clear();
        Refresh4DMMObjectGroupsList(fTrue);
        return 0;
    }

    switch (wm)
    {
    case WM_CREATE:
    {
        vhwnd4DMMObjectGroups = hwnd;
        HINSTANCE hinst = GetModuleHandleA(pvNil);
        HWND hwndList = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_USETABSTOPS,
            12, 12, 516, 354, hwnd, (HMENU)kid4DMMObjectGroupsList, hinst, pvNil);
        HWND hwndFrameOnly = CreateWindowExA(0, "BUTTON", "Currently in this frame only",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                        12, 372, 516, 24, hwnd, (HMENU)kid4DMMObjectGroupsFrameOnly, hinst, pvNil);
        HWND hwndVisibleOnly = CreateWindowExA(0, "BUTTON", "Currently visible only",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                        12, 402, 516, 24, hwnd, (HMENU)kid4DMMObjectGroupsVisibleOnly, hinst, pvNil);
        if (hwndFrameOnly != hNil)
            SendMessageA(hwndFrameOnly, BM_SETCHECK, vf4DMMObjectGroupsFrameOnly ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hwndVisibleOnly != hNil)
            SendMessageA(hwndVisibleOnly, BM_SETCHECK, vf4DMMObjectGroupsVisibleOnly ? BST_CHECKED : BST_UNCHECKED, 0);
        CreateWindowExA(0, "BUTTON", "Join Object", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        12, 414, 255, 30, hwnd, (HMENU)kid4DMMObjectGroupsJoin, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Abandon Object", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        273, 414, 255, 30, hwnd, (HMENU)kid4DMMObjectGroupsAbandon, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Bind", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        12, 450, 94, 30, hwnd, (HMENU)kid4DMMObjectGroupsBind, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Unbind", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        112, 450, 94, 30, hwnd, (HMENU)kid4DMMObjectGroupsUnbind, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Rename", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        212, 450, 94, 30, hwnd, (HMENU)kid4DMMObjectGroupsRename, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Duplicate", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        312, 450, 216, 30, hwnd, (HMENU)kid4DMMObjectGroupsDuplicate, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Create Part", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        12, 486, 255, 30, hwnd, (HMENU)kid4DMMObjectGroupsCreateObject, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Actor Studio", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        273, 486, 255, 30, hwnd, (HMENU)kid4DMMObjectGroupsActorStudio, hinst, pvNil);
        const int32_t rgid[] = {kid4DMMObjectGroupsList, kid4DMMObjectGroupsBind,
            kid4DMMObjectGroupsUnbind, kid4DMMObjectGroupsRename, kid4DMMObjectGroupsDuplicate,
            kid4DMMObjectGroupsFrameOnly, kid4DMMObjectGroupsVisibleOnly,
            kid4DMMObjectGroupsJoin, kid4DMMObjectGroupsAbandon,
            kid4DMMObjectGroupsCreateObject, kid4DMMObjectGroupsActorStudio};
        Set4DMMNativeControlFont(hwnd, rgid, SIZEOF(rgid) / SIZEOF(rgid[0]));
        if (hwndList != hNil)
        {
            const int32_t dxTabStop = 12;
            SendMessageA(hwndList, LB_SETTABSTOPS, 1, (LPARAM)&dxTabStop);
            vpfn4DMMObjectGroupsListOld = (WNDPROC)SetWindowLongPtrA(hwndList, GWLP_WNDPROC,
                                                      (LONG_PTR)Lresult4DMMObjectGroupsListProc);
        }
        SetTimer(hwnd, 1, 50, pvNil);
        RECT rcClient;
        if (GetClientRect(hwnd, &rcClient))
            SendMessageA(hwnd, WM_SIZE, SIZE_RESTORED,
                         MAKELPARAM(rcClient.right - rcClient.left, rcClient.bottom - rcClient.top));
        Refresh4DMMObjectGroupsList(fTrue);
        return 0;
    }
    case WM_SIZE:
    {
        RECT rc;
        if (!GetClientRect(hwnd, &rc))
            return 0;
        const int32_t margin = 12;
        const int32_t gap = 6;
        const int32_t dypButton = 30;
        const int32_t dypCheck = 24;
        const int32_t dxpAvail = LwMax(250L, rc.right - margin * 2);
        const int32_t ypRow3 = LwMax(margin, rc.bottom - margin - dypButton);
        const int32_t ypRow2 = LwMax(margin, ypRow3 - gap - dypButton);
        const int32_t ypRow1 = LwMax(margin, ypRow2 - gap - dypButton);
        const int32_t ypVisibleFilter = LwMax(margin, ypRow1 - gap - dypCheck);
        const int32_t ypFrameFilter = LwMax(margin, ypVisibleFilter - gap - dypCheck);
        SetWindowPos(GetDlgItem(hwnd, kid4DMMObjectGroupsList), hNil,
                     margin, margin, dxpAvail, LwMax(60L, ypFrameFilter - margin - gap),
                     SWP_NOZORDER | SWP_NOACTIVATE);

        const int32_t dxpPair = LwMax(100L, (dxpAvail - gap) / 2);
        // Filters are deliberately full-width, left-justified rows. Keeping
        // them out of a two-column strip also leaves their labels readable at
        // narrow OGW widths.
        SetWindowPos(GetDlgItem(hwnd, kid4DMMObjectGroupsFrameOnly), hNil,
                     margin, ypFrameFilter, dxpAvail, dypCheck,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(GetDlgItem(hwnd, kid4DMMObjectGroupsVisibleOnly), hNil,
                     margin, ypVisibleFilter, dxpAvail, dypCheck,
                     SWP_NOZORDER | SWP_NOACTIVATE);

        SetWindowPos(GetDlgItem(hwnd, kid4DMMObjectGroupsJoin), hNil,
                     margin, ypRow1, dxpPair, dypButton, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(GetDlgItem(hwnd, kid4DMMObjectGroupsAbandon), hNil,
                     margin + dxpPair + gap, ypRow1,
                     rc.right - margin - (margin + dxpPair + gap), dypButton,
                     SWP_NOZORDER | SWP_NOACTIVATE);

        const int32_t dxpQuarter = LwMax(55L, (dxpAvail - gap * 3) / 4);
        SetWindowPos(GetDlgItem(hwnd, kid4DMMObjectGroupsBind), hNil,
                     margin, ypRow2, dxpQuarter, dypButton, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(GetDlgItem(hwnd, kid4DMMObjectGroupsUnbind), hNil,
                     margin + dxpQuarter + gap, ypRow2, dxpQuarter, dypButton, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(GetDlgItem(hwnd, kid4DMMObjectGroupsRename), hNil,
                     margin + (dxpQuarter + gap) * 2, ypRow2, dxpQuarter, dypButton,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(GetDlgItem(hwnd, kid4DMMObjectGroupsDuplicate), hNil,
                     margin + (dxpQuarter + gap) * 3, ypRow2,
                     rc.right - margin - (margin + (dxpQuarter + gap) * 3), dypButton,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(GetDlgItem(hwnd, kid4DMMObjectGroupsCreateObject), hNil,
                     margin, ypRow3, dxpPair, dypButton, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(GetDlgItem(hwnd, kid4DMMObjectGroupsActorStudio), hNil,
                     margin + dxpPair + gap, ypRow3,
                     rc.right - margin - (margin + dxpPair + gap), dypButton,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE)
        {
            Refresh4DMMObjectGroupsList(fFalse);
            Update4DMMObjectGroupsControls();
        }
        break;
    case WM_NCACTIVATE:
        if (wParam != 0)
        {
            Refresh4DMMObjectGroupsList(fFalse);
            Update4DMMObjectGroupsControls();
        }
        break;
    case WM_TIMER:
        Refresh4DMMObjectGroupsList(fFalse);
        return 0;
    case WM_COMMAND:
    {
        PMVIE pmvie = Pmvie4DMMSettingsCurrent();
        const int32_t id = LOWORD(wParam);
        if (HIWORD(wParam) == BN_CLICKED && id == kid4DMMObjectGroupsFrameOnly)
        {
            vf4DMMObjectGroupsFrameOnly =
                SendMessageA(GetDlgItem(hwnd, kid4DMMObjectGroupsFrameOnly), BM_GETCHECK, 0, 0) == BST_CHECKED;
            Refresh4DMMObjectGroupsList(fTrue);
            return 0;
        }
        if (HIWORD(wParam) == BN_CLICKED && id == kid4DMMObjectGroupsVisibleOnly)
        {
            vf4DMMObjectGroupsVisibleOnly =
                SendMessageA(GetDlgItem(hwnd, kid4DMMObjectGroupsVisibleOnly), BM_GETCHECK, 0, 0) == BST_CHECKED;
            Refresh4DMMObjectGroupsList(fTrue);
            return 0;
        }
        if (id == kid4DMMObjectGroupsList && HIWORD(wParam) == LBN_DBLCLK)
        {
            HWND hwndList = (HWND)lParam;
            const int32_t irow = (int32_t)SendMessageA(hwndList, LB_GETCURSEL, 0, 0);
            OGROW row;
            if (F4DMMObjectGroupRow(hwndList, irow, &row) && row.fGroup)
            {
                Set4DMMObjectGroupExpanded(row.idGroup, !F4DMMObjectGroupExpanded(row.idGroup));
                vid4DMMObjectGroupSelected = row.idGroup;
                vf4DMMObjectGroupWholeSelected = fTrue;
                if (pmvie != pvNil)
                    pmvie->FSelectObjectGroup(row.idGroup);
                Refresh4DMMObjectGroupsList(fTrue);
            }
            return 0;
        }
        if (id == kid4DMMObjectGroupsList && HIWORD(wParam) == LBN_SELCHANGE)
        {
            HWND hwndList = (HWND)lParam;
            int32_t irow = (int32_t)SendMessageA(hwndList, LB_GETCURSEL, 0, 0);
            OGROW row;
            if (F4DMMObjectGroupRow(hwndList, irow, &row))
            {
                if (row.fGroup)
                {
                    vid4DMMObjectGroupSelected = row.idGroup;
                    vf4DMMObjectGroupWholeSelected = fTrue;
                    if (pmvie != pvNil)
                        pmvie->FSelectObjectGroup(row.idGroup);
                }
                else
                {
                    vf4DMMObjectGroupWholeSelected = fFalse;
                    // A member row still identifies its owning group. Keep that
                    // group selected while selecting the individual object so
                    // Abandon Object is immediately available.
                    vid4DMMObjectGroupSelected = row.idGroup;
                    if (pmvie != pvNil && pmvie->Pscen() != pvNil)
                    {
                        PACTR pactr = pmvie->Pscen()->PactrFromArid(row.arid);
                        if (pactr != pvNil)
                            pmvie->Pscen()->SelectActr(pactr);
                    }
                }
            }
            Update4DMMObjectGroupsControls();
            return 0;
        }
        if (HIWORD(wParam) == BN_CLICKED && pmvie != pvNil)
        {
            if (id == kid4DMMObjectGroupsBind)
            {
                PGUND pgund = Pgund4DMMObjectGroupMembership(pmvie, PszLit("Bind Object Group"));
                if (pgund == pvNil)
                    return 0;
                int32_t idGroup = 0;
                if (pmvie->FBindSelectedObjectGroup(&idGroup))
                {
                    Commit4DMMObjectGroupMembershipUndo(pmvie, &pgund);
                    vid4DMMObjectGroupSelected = idGroup;
                    vf4DMMObjectGroupWholeSelected = fFalse;
                    Set4DMMObjectGroupExpanded(idGroup, fFalse);
                    vc4DMMObjectGroupsLast = -1;
                    Refresh4DMMObjectGroupsList(fTrue);
                }
                ReleasePpo(&pgund);
                return 0;
            }
            if (id == kid4DMMObjectGroupsUnbind && vid4DMMObjectGroupSelected > 0)
            {
                PGUND pgund = Pgund4DMMObjectGroupMembership(pmvie, PszLit("Unbind Object Group"));
                if (pgund == pvNil)
                    return 0;
                int32_t idGroup = vid4DMMObjectGroupSelected;
                if (pmvie->FUnbindObjectGroup(idGroup))
                {
                    Commit4DMMObjectGroupMembershipUndo(pmvie, &pgund);
                    Set4DMMObjectGroupExpanded(idGroup, fFalse);
                    vid4DMMObjectGroupSelected = 0;
                    vf4DMMObjectGroupWholeSelected = fFalse;
                    vc4DMMObjectGroupsLast = -1;
                    Refresh4DMMObjectGroupsList(fTrue);
                }
                ReleasePpo(&pgund);
                return 0;
            }
            if (id == kid4DMMObjectGroupsJoin && vid4DMMObjectGroupSelected > 0 &&
                pmvie->Pscen() != pvNil && pmvie->Pscen()->CactrSelected() == 1 &&
                pmvie->Pscen()->PactrSelected() != pvNil)
            {
                PGUND pgund = Pgund4DMMObjectGroupMembership(pmvie, PszLit("Join Object"));
                if (pgund == pvNil)
                    return 0;
                const int32_t arid = pmvie->Pscen()->PactrSelected()->Arid();
                if (pmvie->FJoinObjectToGroup(vid4DMMObjectGroupSelected, arid))
                {
                    Commit4DMMObjectGroupMembershipUndo(pmvie, &pgund);
                    vf4DMMObjectGroupWholeSelected = fFalse;
                    Set4DMMObjectGroupExpanded(vid4DMMObjectGroupSelected, fTrue);
                    vc4DMMObjectGroupsLast = -1;
                    Refresh4DMMObjectGroupsList(fTrue);
                }
                ReleasePpo(&pgund);
                return 0;
            }
            if (id == kid4DMMObjectGroupsAbandon && !vf4DMMObjectGroupWholeSelected &&
                pmvie->Pscen() != pvNil && pmvie->Pscen()->CactrSelected() == 1 &&
                pmvie->Pscen()->PactrSelected() != pvNil)
            {
                const int32_t arid = pmvie->Pscen()->PactrSelected()->Arid();
                int32_t idGroup = 0;
                if (pmvie->FObjectInObjectGroup(arid, &idGroup) && idGroup > 0)
                {
                    PGUND pgund = Pgund4DMMObjectGroupMembership(pmvie, PszLit("Abandon Object"));
                    if (pgund == pvNil)
                        return 0;
                    if (pmvie->FAbandonObjectFromGroup(idGroup, arid))
                    {
                        Commit4DMMObjectGroupMembershipUndo(pmvie, &pgund);
                        vf4DMMObjectGroupWholeSelected = fFalse;
                        if (I4DMMObjectGroupFind(pmvie, idGroup) == ivNil)
                        {
                            Set4DMMObjectGroupExpanded(idGroup, fFalse);
                            if (vid4DMMObjectGroupSelected == idGroup)
                                vid4DMMObjectGroupSelected = 0;
                        }
                        else
                            vid4DMMObjectGroupSelected = idGroup;
                        vc4DMMObjectGroupsLast = -1;
                        Refresh4DMMObjectGroupsList(fTrue);
                    }
                    ReleasePpo(&pgund);
                }
                return 0;
            }
            if (id == kid4DMMObjectGroupsDuplicate && vid4DMMObjectGroupSelected > 0)
            {
                const int32_t idSource = vid4DMMObjectGroupSelected;
                PGUND pgund = Pgund4DMMObjectGroupMembership(pmvie, PszLit("Duplicate Object Group"));
                if (pgund == pvNil)
                    return 0;
                int32_t idNew = 0;
                if (FDuplicate4DMMObjectGroup(pmvie, idSource, &idNew))
                {
                    Commit4DMMObjectGroupMembershipUndo(pmvie, &pgund);
                    vid4DMMObjectGroupSelected = idNew;
                    vf4DMMObjectGroupWholeSelected = fTrue;
                    Set4DMMObjectGroupExpanded(idNew, fFalse);
                    pmvie->FSelectObjectGroup(idNew);
                    vc4DMMObjectGroupsLast = -1;
                    Refresh4DMMObjectGroupsList(fTrue);
                }
                ReleasePpo(&pgund);
                return 0;
            }
            if (id == kid4DMMObjectGroupsRename && vid4DMMObjectGroupSelected > 0)
            {
                FOpen4DMMObjectGroupRename(vid4DMMObjectGroupSelected);
                return 0;
            }
            if (id == kid4DMMObjectGroupsCreateObject && vid4DMMObjectGroupSelected > 0)
            {
                FOpen4DMMCreatePart(vid4DMMObjectGroupSelected);
                Update4DMMObjectGroupsControls();
                return 0;
            }
            if (id == kid4DMMObjectGroupsActorStudio)
            {
                FOpen4DMMActorStudio(vid4DMMObjectGroupSelected);
                return 0;
            }
        }
        break;
    }
    case WM_CLOSE:
        Close4DMMSettingsWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (vhwnd4DMMObjectGroupRename != hNil && IsWindow(vhwnd4DMMObjectGroupRename))
            DestroyWindow(vhwnd4DMMObjectGroupRename);
        vhwnd4DMMObjectGroups = hNil;
        vid4DMMObjectGroupSelected = 0;
        vf4DMMObjectGroupWholeSelected = fFalse;
        vpmvie4DMMObjectGroupsLast = pvNil;
        viscen4DMMObjectGroupsLast = ivNil;
        vc4DMMObjectGroupsLast = -1;
        vdw4DMMObjectGroupsLastSignature = 0;
        vpfn4DMMObjectGroupsListOld = pvNil;
        vrg4DMMObjectGroupRows.clear();
        return 0;
    }
    return DefWindowProcA(hwnd, wm, wParam, lParam);
}

static bool FOpen4DMMObjectGroups(void)
{
    if (!MVIE::FMultiSelectMode())
        return fFalse;
    if (vhwnd4DMMObjectGroups != hNil && IsWindow(vhwnd4DMMObjectGroups))
    {
        Refresh4DMMObjectGroupsList(fTrue);
        ShowWindow(vhwnd4DMMObjectGroups, SW_SHOWNORMAL);
        SetForegroundWindow(vhwnd4DMMObjectGroups);
        return fTrue;
    }

    HINSTANCE hinst = GetModuleHandleA(pvNil);
    WNDCLASSA wc;
    ClearPb(&wc, SIZEOF(wc));
    if (!GetClassInfoA(hinst, ksz4DMMObjectGroupsWndClass, &wc))
    {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = Lresult4DMMObjectGroupsWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(pvNil, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = ksz4DMMObjectGroupsWndClass;
        if (RegisterClassA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return fFalse;
    }

    RECT rc = {0, 0, 540, 600};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
    AdjustWindowRectEx(&rc, style, fFalse, WS_EX_TOOLWINDOW);
    vhwnd4DMMObjectGroups = CreateWindowExA(WS_EX_TOOLWINDOW,
        ksz4DMMObjectGroupsWndClass, "4DMM Object Groups", style,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        Hwnd4DMMNativeToolOwner(), hNil, hinst, pvNil);
    if (vhwnd4DMMObjectGroups == hNil)
        return fFalse;
    ShowWindow(vhwnd4DMMObjectGroups, SW_SHOWNORMAL);
    SetForegroundWindow(vhwnd4DMMObjectGroups);
    UpdateWindow(vhwnd4DMMObjectGroups);
    return fTrue;
}

enum VXPIMPORTMODE
{
    kvimLegacyActor = 1,
    kvimLegacyProp = 2,
    kvimVxp2 = 3
};

static bool F4DMMChooseVxpImports(HWND hwndOwner, std::vector<std::filesystem::path> *prgpath,
                                  VXPIMPORTMODE *pvim)
{
    if (prgpath == pvNil || pvim == pvNil)
        return fFalse;
    prgpath->clear();
    *pvim = kvimLegacyActor;

    std::vector<char> rgchFile(65536, 0);
    static const char kszFilter[] =
        "Actor(s) - Legacy VXP (*.vxp)\0*.vxp\0"
        "Prop(s) - Legacy VXP (*.vxp)\0*.vxp\0"
        "4DMM VXP2 (*.vxp2)\0*.vxp2\0\0";
    OPENFILENAMEA ofn;
    ClearPb(&ofn, SIZEOF(ofn));
    ofn.lStructSize = SIZEOF(ofn);
    ofn.hwndOwner = hwndOwner;
    ofn.lpstrFilter = kszFilter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = rgchFile.data();
    ofn.nMaxFile = (DWORD)rgchFile.size();
    ofn.lpstrTitle = "Import VXP(s) to Movie";
    ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST |
                OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameA(&ofn))
    {
        const DWORD dwErr = CommDlgExtendedError();
        if (dwErr != 0)
            MVIE::MultiLog(pvNil, "vxp_import dialog FAIL commdlg=0x%08lX", (unsigned long)dwErr);
        return fFalse;
    }

    if (ofn.nFilterIndex == 2)
        *pvim = kvimLegacyProp;
    else if (ofn.nFilterIndex == 3)
        *pvim = kvimVxp2;
    else
        *pvim = kvimLegacyActor;

    const char *pszFirst = rgchFile.data();
    const char *pszNext = pszFirst + strlen(pszFirst) + 1;
    if (*pszNext == 0)
    {
        prgpath->push_back(std::filesystem::path(pszFirst));
    }
    else
    {
        const std::filesystem::path pathDir(pszFirst);
        for (const char *pszName = pszNext; *pszName != 0; pszName += strlen(pszName) + 1)
            prgpath->push_back(pathDir / pszName);
    }
    return !prgpath->empty();
}

static bool F4DMMValidateVxp2ChunkyMagic(const std::filesystem::path &path)
{
    std::ifstream ifs(path, std::ios::binary);
    char rgch[4] = {0};
    return ifs.read(rgch, 4) && memcmp(rgch, "CHN4", 4) == 0;
}

static bool F4DMMStageVxp2Content(const std::filesystem::path &path4cn,
                                  const std::filesystem::path &pathStage3cn)
{
    std::error_code ec;
    if (!F4DMMValidateVxp2ChunkyMagic(path4cn) ||
        !std::filesystem::copy_file(path4cn, pathStage3cn,
                                    std::filesystem::copy_options::overwrite_existing, ec))
        return fFalse;

    std::fstream fs(pathStage3cn, std::ios::binary | std::ios::in | std::ios::out);
    if (!fs)
        return fFalse;
    fs.seekp(0, std::ios::beg);
    fs.write("CHN2", 4);
    fs.flush();
    return FPure(fs.good());
}

static void Import4DMMVxpsToMovie(PMVIE pmvie, HWND hwndOwner)
{
    if (pmvie == pvNil)
        return;

    std::vector<std::filesystem::path> rgpathVxp;
    VXPIMPORTMODE vim = kvimLegacyActor;
    if (!F4DMMChooseVxpImports(hwndOwner, &rgpathVxp, &vim))
        return;
    const bool fVxp2 = FPure(vim == kvimVxp2);
    const bool fProp = FPure(vim == kvimLegacyProp);

    std::error_code ec;
    std::filesystem::path pathTempBase = std::filesystem::temp_directory_path(ec);
    if (ec)
    {
        MessageBoxA(hwndOwner, "4DMM could not create a temporary expansion import directory.",
                    "Import VXP(s) to Movie", MB_OK | MB_ICONERROR);
        return;
    }

    static LONG slwVxpImportSerial = 0;
    char szTempName[64];
    sprintf_s(szTempName, SIZEOF(szTempName), "4DMM_VXP_%08lX_%04lX",
              (unsigned long)GetCurrentProcessId(),
              (unsigned long)(InterlockedIncrement(&slwVxpImportSerial) & 0xffff));
    const std::filesystem::path pathRoot = pathTempBase / szTempName;
    if (!std::filesystem::create_directories(pathRoot, ec) && ec)
    {
        MessageBoxA(hwndOwner, "4DMM could not create a temporary expansion import directory.",
                    "Import VXP(s) to Movie", MB_OK | MB_ICONERROR);
        return;
    }

    MVIE::MultiLog(pmvie, "%s begin files=%ld mode=%s temp=%s",
                   fVxp2 ? "vxp2_import" : "vxp_import", (long)rgpathVxp.size(),
                   fVxp2 ? "vxp2" : (fProp ? "legacy_prop" : "legacy_actor"),
                   pathRoot.string().c_str());

    vpappb->BeginLongOp();
    int32_t cImported = 0;
    int32_t cArchiveFailed = 0;
    for (size_t ivxp = 0; ivxp < rgpathVxp.size(); ++ivxp)
    {
        char szItem[32];
        sprintf_s(szItem, SIZEOF(szItem), "item_%03lu", (unsigned long)ivxp);
        const std::filesystem::path pathItem = pathRoot / szItem;
        const std::filesystem::path pathPackages = pathItem / "packages";
        const std::filesystem::path pathContent = pathItem / "content";
        ec.clear();
        if ((!std::filesystem::create_directories(pathPackages, ec) && ec) ||
            (!std::filesystem::create_directories(pathContent, ec) && ec))
        {
            ++cArchiveFailed;
            MVIE::MultiLog(pmvie, "%s archive FAIL stage=create_dirs file=%s ec=%ld",
                           fVxp2 ? "vxp2_import" : "vxp_import",
                           rgpathVxp[ivxp].string().c_str(), (long)ec.value());
            continue;
        }

        const std::filesystem::path pathZip = pathPackages / "selected.zip";
        ec.clear();
        if (!std::filesystem::copy_file(rgpathVxp[ivxp], pathZip,
                                        std::filesystem::copy_options::overwrite_existing, ec))
        {
            ++cArchiveFailed;
            MVIE::MultiLog(pmvie, "%s archive FAIL stage=stage_zip file=%s ec=%ld",
                           fVxp2 ? "vxp2_import" : "vxp_import",
                           rgpathVxp[ivxp].string().c_str(), (long)ec.value());
            continue;
        }
        if (!FExpandVmmArchives(pathPackages, pathContent))
        {
            ++cArchiveFailed;
            MVIE::MultiLog(pmvie, "%s archive FAIL stage=expand file=%s",
                           fVxp2 ? "vxp2_import" : "vxp_import",
                           rgpathVxp[ivxp].string().c_str());
            continue;
        }

        if (fVxp2)
        {
            std::vector<std::filesystem::path> rg4cn;
            std::vector<std::filesystem::path> rg4th;
            ec.clear();
            std::filesystem::recursive_directory_iterator it(pathContent,
                std::filesystem::directory_options::skip_permission_denied, ec);
            const std::filesystem::recursive_directory_iterator itEnd;
            for (; !ec && it != itEnd; it.increment(ec))
            {
                if (!it->is_regular_file(ec) || ec)
                    continue;
                const std::string ext = it->path().extension().string();
                if (_stricmp(ext.c_str(), ".4cn") == 0)
                    rg4cn.push_back(it->path());
                else if (_stricmp(ext.c_str(), ".4th") == 0)
                    rg4th.push_back(it->path());
            }

            if (rg4cn.size() != 1 || rg4th.size() != 1 ||
                !F4DMMValidateVxp2ChunkyMagic(rg4th[0]))
            {
                ++cArchiveFailed;
                MVIE::MultiLog(pmvie, "vxp2_import archive FAIL stage=package_shape 4cn=%ld 4th=%ld file=%s",
                               (long)rg4cn.size(), (long)rg4th.size(), rgpathVxp[ivxp].string().c_str());
                continue;
            }

            const std::filesystem::path pathStage3cn = pathItem / "vxp2_content.3cn";
            if (!F4DMMStageVxp2Content(rg4cn[0], pathStage3cn))
            {
                ++cArchiveFailed;
                MVIE::MultiLog(pmvie, "vxp2_import archive FAIL stage=stage_4cn path=%s",
                               rg4cn[0].string().c_str());
                continue;
            }

            STN stnPath(pathStage3cn.string().c_str());
            FNI fniContent;
            if (!fniContent.FBuildFromPath(&stnPath, kftgContent))
            {
                ++cArchiveFailed;
                MVIE::MultiLog(pmvie, "vxp2_import archive FAIL stage=build_fni path=%s",
                               pathStage3cn.string().c_str());
                continue;
            }
            int32_t cImportedFile = 0;
            const std::string strBase = rg4cn[0].parent_path().string();
            const bool fImported = pmvie->FImport4DMMVxpContent(&fniContent, fFalse, &cImportedFile,
                                                                fTrue, strBase.c_str());
            cImported += cImportedFile;
            if (!fImported)
                ++cArchiveFailed;
            MVIE::MultiLog(pmvie, "vxp2_import 4cn result=%d count=%ld path=%s",
                           (int)fImported, (long)cImportedFile, rg4cn[0].string().c_str());
            continue;
        }

        bool fFoundChunky = fFalse;
        ec.clear();
        std::filesystem::recursive_directory_iterator it(pathContent,
            std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator itEnd;
        for (; !ec && it != itEnd; it.increment(ec))
        {
            if (!it->is_regular_file(ec) || ec)
                continue;
            const std::string strExt = it->path().extension().string();
            if (_stricmp(strExt.c_str(), ".3cn") != 0)
                continue;

            fFoundChunky = fTrue;
            STN stnPath(it->path().string().c_str());
            FNI fni3cn;
            if (!fni3cn.FBuildFromPath(&stnPath, kftgContent))
            {
                MVIE::MultiLog(pmvie, "vxp_import archive FAIL stage=build_fni path=%s",
                               it->path().string().c_str());
                continue;
            }
            int32_t cImportedFile = 0;
            const bool fImported = pmvie->FImport4DMMVxpContent(&fni3cn, fProp, &cImportedFile);
            cImported += cImportedFile;
            MVIE::MultiLog(pmvie, "vxp_import 3cn result=%d count=%ld path=%s",
                           (int)fImported, (long)cImportedFile, it->path().string().c_str());
        }
        if (!fFoundChunky)
        {
            ++cArchiveFailed;
            MVIE::MultiLog(pmvie, "vxp_import archive FAIL stage=no_3cn file=%s",
                           rgpathVxp[ivxp].string().c_str());
        }
    }
    vpappb->EndLongOp();

    ec.clear();
    std::filesystem::remove_all(pathRoot, ec);
    if (ec)
        MVIE::MultiLog(pmvie, "%s cleanup warning ec=%ld dir=%s",
                       fVxp2 ? "vxp2_import" : "vxp_import", (long)ec.value(), pathRoot.string().c_str());

    char szResult[256];
    if (cImported > 0)
    {
        if (fVxp2)
            sprintf_s(szResult, SIZEOF(szResult),
                      "Imported %ld VXP2 template%s.%s",
                      (long)cImported, cImported == 1 ? "" : "s",
                      cArchiveFailed > 0 ? " Some selected VXP2 files could not be imported." : "");
        else
            sprintf_s(szResult, SIZEOF(szResult),
                      "Imported %ld VXP template%s as %s.%s",
                      (long)cImported, cImported == 1 ? "" : "s",
                      fProp ? "Prop(s)" : "Actor(s)",
                      cArchiveFailed > 0 ? " Some selected VXP files could not be imported." : "");
        MessageBoxA(hwndOwner, szResult, "Import VXP(s) to Movie",
                    MB_OK | (cArchiveFailed > 0 ? MB_ICONWARNING : MB_ICONINFORMATION));
    }
    else
    {
        MessageBoxA(hwndOwner,
                    fVxp2 ? "No actor/prop templates were imported from the selected VXP2 file(s)." :
                            "No actor/prop templates were imported from the selected VXP file(s).",
                    "Import VXP(s) to Movie", MB_OK | MB_ICONERROR);
    }
    MVIE::MultiLog(pmvie, "%s end imported=%ld archive_failures=%ld mode=%s",
                   fVxp2 ? "vxp2_import" : "vxp_import", (long)cImported, (long)cArchiveFailed,
                   fVxp2 ? "vxp2" : (fProp ? "legacy_prop" : "legacy_actor"));
}

static void Layout4DMMSettingsBrowserControls(HWND hwnd, bool fScaled)
{
    if (hwnd == hNil || !IsWindow(hwnd))
        return;

    const bool fCombined = FExternalContentBrowsersCombined();
    const int32_t ypActorStudio = MVIE::FMultiSelectMode() ? 342 : 312;
    const int32_t ypBrowser = ypActorStudio + 30;
    const int32_t ypPropBrowser = ypBrowser + 30;
    const int32_t ypImportVxp = ypBrowser + (fCombined ? 30 : 60);
    HWND hwndActor = GetDlgItem(hwnd, kid4DMMSettingsActorBrowser);
    HWND hwndProp = GetDlgItem(hwnd, kid4DMMSettingsPropBrowser);
    HWND hwndImport = GetDlgItem(hwnd, kid4DMMSettingsImportVxp);
    if (hwndActor == hNil || hwndProp == hNil || hwndImport == hNil)
        return;

    SetWindowTextA(hwndActor, fCombined ? "Open Objects Browser" : "Open Hired Actors Browser");

    const int32_t xp = fScaled ? Lw4DMMExternalToolUi200(hwnd, 12) : 12;
    const int32_t dxp = fScaled ? Lw4DMMExternalToolUi200(hwnd, 205) : 205;
    const int32_t dyp = fScaled ? Lw4DMMExternalToolUi200(hwnd, 26) : 26;
    const int32_t ypActorPhysical = fScaled ? Lw4DMMExternalToolUi200(hwnd, ypBrowser) : ypBrowser;
    const int32_t ypPropPhysical = fScaled ? Lw4DMMExternalToolUi200(hwnd, ypPropBrowser) : ypPropBrowser;
    const int32_t ypImportPhysical = fScaled ? Lw4DMMExternalToolUi200(hwnd, ypImportVxp) : ypImportVxp;

    SetWindowPos(hwndActor, hNil, xp, ypActorPhysical, dxp, dyp, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(hwndProp, hNil, xp, ypPropPhysical, dxp, dyp, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(hwndImport, hNil, xp, ypImportPhysical, dxp, dyp, SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(hwndProp, fCombined ? SW_HIDE : SW_SHOWNOACTIVATE);

    // The browser mode changes the number of visible rows. Keep the console
    // shell fitted to the active layout instead of leaving a fixed-height
    // window with controls hidden below the client area. Resize by the client
    // delta so the existing non-client frame/title metrics remain untouched.
    const int32_t dypMargin = fScaled ? Lw4DMMExternalToolUi200(hwnd, 12) : 12;
    const int32_t dypClientWanted = ypImportPhysical + dyp + dypMargin;
    RECT rcClient;
    RECT rcWindow;
    if (GetClientRect(hwnd, &rcClient) && GetWindowRect(hwnd, &rcWindow))
    {
        const int32_t dypClientNow = rcClient.bottom - rcClient.top;
        const int32_t dypWindowNow = rcWindow.bottom - rcWindow.top;
        const int32_t dypWindowWanted = dypWindowNow + (dypClientWanted - dypClientNow);
        int32_t xpWindow = rcWindow.left;
        int32_t ypWindow = rcWindow.top;

        HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        ClearPb(&mi, SIZEOF(mi));
        mi.cbSize = SIZEOF(mi);
        if (hmon != hNil && GetMonitorInfoA(hmon, &mi))
        {
            const int32_t dypWork = mi.rcWork.bottom - mi.rcWork.top;
            if (dypWindowWanted >= dypWork)
                ypWindow = mi.rcWork.top;
            else
                ypWindow = LwMax((int32_t)mi.rcWork.top,
                                 LwMin(ypWindow, (int32_t)mi.rcWork.bottom - dypWindowWanted));
        }

        if (dypWindowWanted != dypWindowNow || ypWindow != rcWindow.top)
        {
            SetWindowPos(hwnd, hNil, xpWindow, ypWindow,
                         rcWindow.right - rcWindow.left, dypWindowWanted,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            PMVIE pmvie = Pmvie4DMMSettingsCurrent();
            if (pmvie != pvNil)
            {
                MVIE::MultiLog(pmvie,
                    "console_browser_resize combined=%d client_old=%ld client_new=%ld window_old=%ld window_new=%ld",
                    (int)fCombined, (long)dypClientNow, (long)dypClientWanted,
                    (long)dypWindowNow, (long)dypWindowWanted);
            }
        }
    }

    InvalidateRect(hwnd, pvNil, fTrue);
    UpdateWindow(hwnd);
}

static void Update4DMMSettingsControls(void)
{
    if (vhwnd4DMMSettings == hNil || !IsWindow(vhwnd4DMMSettings))
        return;

    PMVIE pmvie = Pmvie4DMMSettingsCurrent();
    const bool fHaveMovie = pmvie != pvNil;
    const bool fHaveScene = fHaveMovie && pmvie->Pscen() != pvNil && pmvie->Iscen() >= 0;

    CheckDlgButton(vhwnd4DMMSettings, kid4DMMSettingsLights,
                   fHaveScene && pmvie->FSceneLightsEnabled(pmvie->Iscen()) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(vhwnd4DMMSettings, kid4DMMSettingsHideLightObjects,
                   fHaveScene && pmvie->FSceneHideLightObjects(pmvie->Iscen()) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(vhwnd4DMMSettings, kid4DMMSettingsDefaultMusic,
                   fHaveMovie && pmvie->FDefaultMusicForNewScenes() ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(vhwnd4DMMSettings, kid4DMMSettings100xGrow,
                   fHaveMovie && pmvie->FExperimental100xGrow() ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(vhwnd4DMMSettings, kid4DMMSettings100xShrink,
                   fHaveMovie && pmvie->FExperimental100xShrink() ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(vhwnd4DMMSettings, kid4DMMSettingsCombineBrowsers,
                   FExternalContentBrowsersCombined() ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(vhwnd4DMMSettings, kid4DMMSettingsDefaultLightingShaders,
                   fHaveMovie && pmvie->FDefaultLightingShaders() ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(vhwnd4DMMSettings, kid4DMMSettingsLightLabCombineLegacy,
                   fHaveMovie && pmvie->FLightLabCombineLegacy() ? BST_CHECKED : BST_UNCHECKED);

    EnableWindow(GetDlgItem(vhwnd4DMMSettings, kid4DMMSettingsLights), fHaveScene);
    EnableWindow(GetDlgItem(vhwnd4DMMSettings, kid4DMMSettingsHideLightObjects), fHaveScene);
    EnableWindow(GetDlgItem(vhwnd4DMMSettings, kid4DMMSettingsDefaultMusic), fHaveMovie);
    EnableWindow(GetDlgItem(vhwnd4DMMSettings, kid4DMMSettings100xGrow), fHaveMovie);
    EnableWindow(GetDlgItem(vhwnd4DMMSettings, kid4DMMSettings100xShrink), fHaveMovie);
    EnableWindow(GetDlgItem(vhwnd4DMMSettings, kid4DMMSettingsCombineBrowsers), fHaveMovie);
    EnableWindow(GetDlgItem(vhwnd4DMMSettings, kid4DMMSettingsDefaultLightingShaders), fHaveMovie);
    EnableWindow(GetDlgItem(vhwnd4DMMSettings, kid4DMMSettingsLightLabCombineLegacy), fHaveMovie);
    EnableWindow(GetDlgItem(vhwnd4DMMSettings, kid4DMMSettingsSceneSettings), fHaveScene);
    EnableWindow(GetDlgItem(vhwnd4DMMSettings, kid4DMMSettingsAdvanced), fHaveMovie);
    EnableWindow(GetDlgItem(vhwnd4DMMSettings, kid4DMMSettingsActions), fHaveMovie);
    EnableWindow(GetDlgItem(vhwnd4DMMSettings, kid4DMMSettingsObjectGroups), fHaveMovie && MVIE::FMultiSelectMode());
    EnableWindow(GetDlgItem(vhwnd4DMMSettings, kid4DMMSettingsActorStudio), fHaveMovie);
    EnableWindow(GetDlgItem(vhwnd4DMMSettings, kid4DMMSettingsActorBrowser), fHaveMovie);
    EnableWindow(GetDlgItem(vhwnd4DMMSettings, kid4DMMSettingsPropBrowser), fHaveMovie);
    EnableWindow(GetDlgItem(vhwnd4DMMSettings, kid4DMMSettingsImportVxp), fHaveMovie);
}

static void Close4DMMSettingsWindow(HWND hwnd)
{
    HWND hwndOwner = GetWindow(hwnd, GW_OWNER);
    if (hwndOwner == hNil || !IsWindow(hwndOwner))
        hwndOwner = Hwnd4DMMNativeToolOwner();

    // Hide the tool first so USER32 never has to expose the source Kauai HWND
    // while the owned-window chain is being destroyed/activated. This is most
    // visible after the Rename child has just transferred activation back to
    // Object Groups. Keep the 640x480 source parked before and after teardown.
    if (F4DMMUiScaleActive())
    {
        ShowWindow(hwnd, SW_HIDE);
        F4DMMParkUiScaleSourceWindow();
        if (hwndOwner == vhwnd4DMMUiScale && IsWindow(hwndOwner))
            SetWindowPos(hwndOwner, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    DestroyWindow(hwnd);
    if (hwndOwner != hNil && IsWindow(hwndOwner))
    {
        if (IsIconic(hwndOwner))
            ShowWindow(hwndOwner, SW_RESTORE);
        if (F4DMMUiScaleActive())
            F4DMMParkUiScaleSourceWindow();
        SetForegroundWindow(hwndOwner);
    }
}

static LRESULT CALLBACK Lresult4DMMSettingsWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    switch (wm)
    {
    case WM_CREATE:
    {
        vhwnd4DMMSettings = hwnd;
        const DWORD grfCheck = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX;
        const DWORD grfCheckMulti = grfCheck | BS_MULTILINE;
        const DWORD grfButton = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
        HINSTANCE hinst = GetModuleHandleA(pvNil);
        CreateWindowExA(0, "BUTTON", "Enable lights for scene", grfCheck,
                        12, 12, 305, 20, hwnd, (HMENU)kid4DMMSettingsLights, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Make designated light objects invisible", grfCheck,
                        12, 36, 305, 20, hwnd, (HMENU)kid4DMMSettingsHideLightObjects, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Enable default music for new scenes", grfCheck,
                        12, 60, 305, 20, hwnd, (HMENU)kid4DMMSettingsDefaultMusic, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Enable 100x grow limit (experimental)", grfCheck,
                        12, 84, 305, 20, hwnd, (HMENU)kid4DMMSettings100xGrow, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Allow down to 100x smaller (experimental)", grfCheck,
                        12, 108, 305, 20, hwnd, (HMENU)kid4DMMSettings100xShrink, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Combine actors && props browsers", grfCheck,
                        12, 132, 305, 20, hwnd, (HMENU)kid4DMMSettingsCombineBrowsers, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Use shaders with default (legacy) lighting", grfCheck,
                        12, 156, 305, 20, hwnd, (HMENU)kid4DMMSettingsDefaultLightingShaders, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Combine default (legacy) lighting with new lighting system", grfCheckMulti,
                        12, 180, 305, 34, hwnd, (HMENU)kid4DMMSettingsLightLabCombineLegacy, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Scene settings", grfButton,
                        12, 222, 205, 26, hwnd, (HMENU)kid4DMMSettingsSceneSettings, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Advanced settings", grfButton,
                        12, 252, 205, 26, hwnd, (HMENU)kid4DMMSettingsAdvanced, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Actions", grfButton,
                        12, 282, 205, 26, hwnd, (HMENU)kid4DMMSettingsActions, hinst, pvNil);
        if (MVIE::FMultiSelectMode())
            CreateWindowExA(0, "BUTTON", "Object Groups", grfButton,
                            12, 312, 205, 26, hwnd, (HMENU)kid4DMMSettingsObjectGroups, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Actor Studio", grfButton,
                        12, MVIE::FMultiSelectMode() ? 342 : 312, 205, 26,
                        hwnd, (HMENU)kid4DMMSettingsActorStudio, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Open Hired Actors Browser", grfButton,
                        12, MVIE::FMultiSelectMode() ? 372 : 342, 205, 26,
                        hwnd, (HMENU)kid4DMMSettingsActorBrowser, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Open Used Props Browser", grfButton,
                        12, MVIE::FMultiSelectMode() ? 402 : 372, 205, 26,
                        hwnd, (HMENU)kid4DMMSettingsPropBrowser, hinst, pvNil);
        CreateWindowExA(0, "BUTTON", "Import VXP(s) to Movie", grfButton,
                        12, MVIE::FMultiSelectMode() ? 432 : 402, 205, 26,
                        hwnd, (HMENU)kid4DMMSettingsImportVxp, hinst, pvNil);
        Layout4DMMSettingsBrowserControls(hwnd, fFalse);
        const int32_t rgid[] = {
            kid4DMMSettingsLights, kid4DMMSettingsHideLightObjects,
            kid4DMMSettingsDefaultMusic, kid4DMMSettings100xGrow, kid4DMMSettings100xShrink,
            kid4DMMSettingsCombineBrowsers, kid4DMMSettingsDefaultLightingShaders,
            kid4DMMSettingsLightLabCombineLegacy, kid4DMMSettingsSceneSettings,
            kid4DMMSettingsAdvanced, kid4DMMSettingsActions, kid4DMMSettingsObjectGroups,
            kid4DMMSettingsActorStudio, kid4DMMSettingsActorBrowser, kid4DMMSettingsPropBrowser,
            kid4DMMSettingsImportVxp
        };
        Set4DMMNativeControlFont(hwnd, rgid, SIZEOF(rgid) / SIZEOF(rgid[0]));
        SetTimer(hwnd, 1, 250, pvNil);
        Update4DMMSettingsControls();
        return 0;
    }
    case WM_TIMER:
        Update4DMMSettingsControls();
        return 0;
    case WM_COMMAND:
    {
        PMVIE pmvie = Pmvie4DMMSettingsCurrent();
        if (pmvie == pvNil)
            break;
        switch (LOWORD(wParam))
        {
        case kid4DMMSettingsLights:
            if (HIWORD(wParam) == BN_CLICKED && pmvie->Pscen() != pvNil)
                pmvie->FSetSceneLightsEnabled(pmvie->Iscen(),
                    IsDlgButtonChecked(hwnd, kid4DMMSettingsLights) == BST_CHECKED, fTrue);
            break;
        case kid4DMMSettingsHideLightObjects:
            if (HIWORD(wParam) == BN_CLICKED && pmvie->Pscen() != pvNil)
                pmvie->FSetSceneHideLightObjects(pmvie->Iscen(),
                    IsDlgButtonChecked(hwnd, kid4DMMSettingsHideLightObjects) == BST_CHECKED, fTrue);
            break;
        case kid4DMMSettingsDefaultMusic:
            if (HIWORD(wParam) == BN_CLICKED)
                pmvie->SetDefaultMusicForNewScenes(
                    IsDlgButtonChecked(hwnd, kid4DMMSettingsDefaultMusic) == BST_CHECKED);
            break;
        case kid4DMMSettings100xGrow:
            if (HIWORD(wParam) == BN_CLICKED)
                pmvie->SetExperimental100xGrow(
                    IsDlgButtonChecked(hwnd, kid4DMMSettings100xGrow) == BST_CHECKED);
            break;
        case kid4DMMSettings100xShrink:
            if (HIWORD(wParam) == BN_CLICKED)
                pmvie->SetExperimental100xShrink(
                    IsDlgButtonChecked(hwnd, kid4DMMSettings100xShrink) == BST_CHECKED);
            break;
        case kid4DMMSettingsCombineBrowsers:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                const bool fCombined =
                    IsDlgButtonChecked(hwnd, kid4DMMSettingsCombineBrowsers) == BST_CHECKED;
                SetExternalContentBrowsersCombined(fCombined);
                Layout4DMMSettingsBrowserControls(hwnd, fTrue);
                MVIE::MultiLog(pmvie, "console_browser_layout combined=%d actor_button=%s prop_button_visible=%d",
                               (int)fCombined,
                               fCombined ? "objects" : "hired_actors",
                               (int)!fCombined);
            }
            break;
        case kid4DMMSettingsDefaultLightingShaders:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                const bool fOldGlobal = pmvie->FDefaultLightingShaders();
                const bool fUse = IsDlgButtonChecked(hwnd, kid4DMMSettingsDefaultLightingShaders) == BST_CHECKED;
                bool fDontAsk = fFalse;
                bool fProceed = fTrue;
                if (pmvie->FAnySceneDefaultLightingShadersDisagree(fOldGlobal) && !pmvie->FDontAskShaderPropagation())
                {
                    achar szPrompt[256];
                    sprintf_s(szPrompt, SIZEOF(szPrompt),
                        "This action will result in scene-specific settings for all scenes being changed to %s the shaders with default (legacy) lighting. Continue?",
                        fUse ? "use" : "not use");
                    fProceed = F4DMMConfirmWithDontAsk(szPrompt, &fDontAsk);
                }
                if (fProceed)
                    pmvie->FSetDefaultLightingShadersForMovie(fUse, fDontAsk, fTrue);
            }
            break;
        case kid4DMMSettingsLightLabCombineLegacy:
            if (HIWORD(wParam) == BN_CLICKED)
                pmvie->FSetLightLabCombineLegacyForMovie(
                    IsDlgButtonChecked(hwnd, kid4DMMSettingsLightLabCombineLegacy) == BST_CHECKED, fTrue);
            break;
        case kid4DMMSettingsSceneSettings:
            if (HIWORD(wParam) == BN_CLICKED)
                FOpen4DMMSceneSettings();
            break;
        case kid4DMMSettingsAdvanced:
            if (HIWORD(wParam) == BN_CLICKED)
                FOpen4DMMAdvancedSettings();
            break;
        case kid4DMMSettingsActions:
            if (HIWORD(wParam) == BN_CLICKED)
                FOpen4DMMActions();
            break;
        case kid4DMMSettingsObjectGroups:
            if (HIWORD(wParam) == BN_CLICKED)
                FOpen4DMMObjectGroups();
            break;
        case kid4DMMSettingsActorStudio:
            if (HIWORD(wParam) == BN_CLICKED)
                FOpen4DMMActorStudioCurrent();
            break;
        case kid4DMMSettingsActorBrowser:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                const bool fCombined = FExternalContentBrowsersCombined();
                MVIE::MultiLog(pmvie, "console_browser_button kind=%s",
                               fCombined ? "objects" : "hired_actors");
                FShowExternalContentBrowser(1);
            }
            break;
        case kid4DMMSettingsPropBrowser:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                MVIE::MultiLog(pmvie, "console_browser_button kind=used_props");
                FShowExternalContentBrowser(2);
            }
            break;
        case kid4DMMSettingsImportVxp:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                MVIE::MultiLog(pmvie, "console_import_vxp_button");
                Import4DMMVxpsToMovie(pmvie, hwnd);
            }
            break;
        }
        Update4DMMSettingsControls();
        return 0;
    }
    case WM_CLOSE:
        Close4DMMSettingsWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        vhwnd4DMMSettings = hNil;
        return 0;
    }
    return DefWindowProcA(hwnd, wm, wParam, lParam);
}

static void Close4DMMSettingsForShutdown(void)
{
    if (vhwnd4DMMActorStudio != hNil && IsWindow(vhwnd4DMMActorStudio))
        DestroyWindow(vhwnd4DMMActorStudio);
    vhwnd4DMMActorStudio = hNil;
    if (vhwnd4DMMActorStudioViewport != hNil && IsWindow(vhwnd4DMMActorStudioViewport))
        DestroyWindow(vhwnd4DMMActorStudioViewport);
    vhwnd4DMMActorStudioViewport = hNil;
    if (vhwnd4DMMObjectGroupRename != hNil && IsWindow(vhwnd4DMMObjectGroupRename))
        DestroyWindow(vhwnd4DMMObjectGroupRename);
    vhwnd4DMMObjectGroupRename = hNil;
    if (vhwnd4DMMObjectGroups != hNil && IsWindow(vhwnd4DMMObjectGroups))
        DestroyWindow(vhwnd4DMMObjectGroups);
    vhwnd4DMMObjectGroups = hNil;
    if (vhwnd4DMMActions != hNil && IsWindow(vhwnd4DMMActions))
        DestroyWindow(vhwnd4DMMActions);
    vhwnd4DMMActions = hNil;
    if (vhwnd4DMMAdvancedSettings != hNil && IsWindow(vhwnd4DMMAdvancedSettings))
        DestroyWindow(vhwnd4DMMAdvancedSettings);
    vhwnd4DMMAdvancedSettings = hNil;
    if (vhwnd4DMMSceneSettings != hNil && IsWindow(vhwnd4DMMSceneSettings))
        DestroyWindow(vhwnd4DMMSceneSettings);
    vhwnd4DMMSceneSettings = hNil;
    if (vhwnd4DMMSettings != hNil && IsWindow(vhwnd4DMMSettings))
        DestroyWindow(vhwnd4DMMSettings);
    vhwnd4DMMSettings = hNil;
}

static bool FOpen4DMMSettings(PMVIE /*pmvie*/)
{
    if (vhwnd4DMMSettings != hNil && IsWindow(vhwnd4DMMSettings))
    {
        Layout4DMMSettingsBrowserControls(vhwnd4DMMSettings, fTrue);
        Update4DMMSettingsControls();
        ShowWindow(vhwnd4DMMSettings, SW_SHOWNORMAL);
        SetForegroundWindow(vhwnd4DMMSettings);
        return fTrue;
    }

    HINSTANCE hinst = GetModuleHandleA(pvNil);
    WNDCLASSA wc;
    ClearPb(&wc, SIZEOF(wc));
    if (!GetClassInfoA(hinst, ksz4DMMSettingsWndClass, &wc))
    {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = Lresult4DMMSettingsWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(pvNil, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = ksz4DMMSettingsWndClass;
        if (RegisterClassA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return fFalse;
    }

    vhwnd4DMMSettings = CreateWindowExA(
        WS_EX_TOOLWINDOW, ksz4DMMSettingsWndClass, "4DMM console",
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 340, 512,
        Hwnd4DMMNativeToolOwner(), hNil, hinst, pvNil);
    if (vhwnd4DMMSettings == hNil)
        return fFalse;
    Scale4DMMExternalToolWindow200(vhwnd4DMMSettings);
    Layout4DMMSettingsBrowserControls(vhwnd4DMMSettings, fTrue);
    ShowWindow(vhwnd4DMMSettings, SW_SHOWNORMAL);
    SetForegroundWindow(vhwnd4DMMSettings);
    UpdateWindow(vhwnd4DMMSettings);
    return fTrue;
}

bool APP::FCmd4DMMSceneLights(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    PMVIE pmvie = (_pstdio != pvNil) ? _pstdio->Pmvie() : pvNil;
    if (pmvie != pvNil && pmvie->Pscen() != pvNil)
        pmvie->FSetSceneLightsEnabled(pmvie->Iscen(), !pmvie->FSceneLightsEnabled(pmvie->Iscen()), fTrue);
    Update4DMMSettingsControls();
    return fTrue;
}

bool APP::FCmd4DMMHideLightObjects(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    PMVIE pmvie = (_pstdio != pvNil) ? _pstdio->Pmvie() : pvNil;
    if (pmvie != pvNil && pmvie->Pscen() != pvNil)
        pmvie->FSetSceneHideLightObjects(pmvie->Iscen(), !pmvie->FSceneHideLightObjects(pmvie->Iscen()), fTrue);
    Update4DMMSettingsControls();
    return fTrue;
}

bool APP::FCmd4DMMSettings(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    PMVIE pmvie = (_pstdio != pvNil) ? _pstdio->Pmvie() : pvNil;
    FOpen4DMMSettings(pmvie);
    return fTrue;
}

bool APP::FCmd4DMMObjectGroups(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    PMVIE pmvie = (_pstdio != pvNil) ? _pstdio->Pmvie() : pvNil;
    MVIE::MultiLog(pmvie, "object_groups_hotkey command");
    FOpen4DMMObjectGroups();
    return fTrue;
}

bool APP::FCmd4DMMActorStudio(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    PMVIE pmvie = (_pstdio != pvNil) ? _pstdio->Pmvie() : pvNil;
    MVIE::MultiLog(pmvie, "actor_studio_hotkey command");
    FOpen4DMMActorStudioCurrent();
    return fTrue;
}

bool APP::FCmd4DMMObj2Vxp2(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
#if defined(KAUAI_WIN32)
    PMVIE pmvie = (_pstdio != pvNil) ? _pstdio->Pmvie() : pvNil;
    MVIE::MultiLog(pmvie, "obj2vxp2_hotkey command");
    _FLaunch4DMMObj2Vxp2(pmvie);
#endif
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Handle Windows messages for the main app window. Return true iff the
    default window proc should _NOT_ be called.
***************************************************************************/
bool APP::_FFrameWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lw, int32_t *plwRet)
{
    AssertBaseThis(0);
    AssertVarMem(plwRet);

    // The native OBJ -> VXP2 converter is a process-level 4DMM tool. Keep a
    // frame-window fallback in addition to the global accelerator so Ctrl+7
    // remains available regardless of which editor child owns command focus.
    if (wm == WM_KEYDOWN && wParam == ChLit('7') && GetKeyState(VK_CONTROL) < 0 &&
        GetKeyState(VK_MENU) >= 0 && GetKeyState(VK_SHIFT) >= 0 && (lw & (1L << 30)) == 0)
    {
        PMVIE pmvieObj2Vxp2 = (_pstdio != pvNil) ? _pstdio->Pmvie() : pvNil;
        MVIE::MultiLog(pmvieObj2Vxp2, "obj2vxp2_hotkey native_frame");
        _FLaunch4DMMObj2Vxp2(pmvieObj2Vxp2);
        *plwRet = 0;
        return fTrue;
    }

    // Actor Studio is a movie-level editor.  The global Kauai accelerator
    // table is authoritative (same route as Ctrl+G); keep this native frame
    // fallback for both requested chords as well.
    const bool fActorStudioCtrlA = wm == WM_KEYDOWN && wParam == ChLit('A') &&
        GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_MENU) >= 0;
    const bool fActorStudioAltA = wm == WM_SYSKEYDOWN && wParam == ChLit('A') &&
        GetKeyState(VK_MENU) < 0 && GetKeyState(VK_CONTROL) >= 0;
    if ((fActorStudioCtrlA || fActorStudioAltA) && GetKeyState(VK_SHIFT) >= 0 &&
        (lw & (1L << 30)) == 0)
    {
        FOpen4DMMActorStudioCurrent();
        *plwRet = 0;
        return fTrue;
    }

    // Object Groups is a global 4DMM organizational tool. Give it the same
    // direct native shortcut behavior as Light Lab so Ctrl+G works regardless
    // of which movie editing tool currently owns the Kauai command focus.
    if (wm == WM_KEYDOWN && wParam == ChLit('G') && GetKeyState(VK_CONTROL) < 0 &&
        GetKeyState(VK_MENU) >= 0 && GetKeyState(VK_SHIFT) >= 0 && (lw & (1L << 30)) == 0)
    {
        FOpen4DMMObjectGroups();
        *plwRet = 0;
        return fTrue;
    }

    // 4DMM Light Lab native Ctrl+L path.  Keep the accelerator-table entry,
    // but handle the exact chord here as well so native/window input additions
    // cannot make Light Lab silently disappear behind command routing.
    if (wm == WM_KEYDOWN && wParam == ChLit('L') && GetKeyState(VK_CONTROL) < 0 &&
        GetKeyState(VK_MENU) >= 0 && GetKeyState(VK_SHIFT) >= 0 && (lw & (1L << 30)) == 0)
    {
        PMVIE pmvieLightLab = (_pstdio != pvNil) ? _pstdio->Pmvie() : pvNil;
        PMVU pmvuLightLab = (pmvieLightLab != pvNil) ? pmvieLightLab->PmvuCur() : pvNil;
        if (pmvuLightLab != pvNil)
        {
            pmvuLightLab->FCmdLightLab(pvNil);
            *plwRet = 0;
            return fTrue;
        }
    }

    // 4DMM selection-box colour cycle.  This is intentionally scoped to the
    // experimental -c -a -l combination requested for the lighting build.
    // Ignore key-repeat so one physical Alt+Y press advances exactly one mode.
    if (wm == WM_SYSKEYDOWN && wParam == ChLit('Y') && GetKeyState(VK_MENU) < 0 &&
        (lw & (1L << 30)) == 0 && BWLD::FTrueColorMode() && BWLD::FActorLightMode() && MVIE::FTestLightMode())
    {
        const int32_t imodHilite = BODY::CycleHiliteMode();
        PMVIE pmvieHilite = (_pstdio != pvNil) ? _pstdio->Pmvie() : pvNil;
        if (pmvieHilite != pvNil)
        {
            if (pmvieHilite->Pbwld() != pvNil)
                pmvieHilite->Pbwld()->MarkDirty();
            pmvieHilite->InvalViews();
        }
        if (MVIE::FDiagnosticsMode())
            MVIE::DiagLog("selection box Alt+Y mode=%d (0=flat-yellow 1=lit-yellow 2=actorlight-current)",
                          (long)imodHilite);
        *plwRet = 0;
        return fTrue;
    }

    // Swallow the translated Alt+Y character as well so Windows does not beep
    // or hand it to an unrelated menu after the mode switch above.
    if (wm == WM_SYSCHAR && (wParam == ChLit('Y') || wParam == ChLit('y')) &&
        BWLD::FTrueColorMode() && BWLD::FActorLightMode() && MVIE::FTestLightMode())
    {
        *plwRet = 0;
        return fTrue;
    }

    // -multi subtraction uses Shift+right-click rather than Ctrl+left-click.
    // Right mouse never enters Kauai's left-button TrackMouse path, so removing
    // a member here cannot accidentally drag whichever actor currently owns the
    // primary selection pointer.  Source-window coordinates are converted to
    // workspace-local coordinates before using the same scene picker as normal
    // viewport selection.
    if (wm == WM_RBUTTONUP && MVIE::FMultiSelectMode() && GetKeyState(VK_SHIFT) < 0)
    {
        PMVIE pmvieMulti = (_pstdio != pvNil) ? _pstdio->Pmvie() : pvNil;
        if (pmvieMulti != pvNil && pmvieMulti->Pscen() != pvNil &&
            !pmvieMulti->FCameraInputActive() && Pkwa() != pvNil)
        {
            PGOB pgobWorkspace = Pkwa()->PgobFromHid(kidWorkspace);
            if (pgobWorkspace != pvNil)
            {
                RC rcWorkspace;
                pgobWorkspace->GetRc(&rcWorkspace, cooHwnd);
                const int32_t xpHwnd = (short)LOWORD(lw);
                const int32_t ypHwnd = (short)HIWORD(lw);
                if (FIn(xpHwnd, rcWorkspace.xpLeft, rcWorkspace.xpRight) &&
                    FIn(ypHwnd, rcWorkspace.ypTop, rcWorkspace.ypBottom))
                {
                    int32_t ibset = ivNil;
                    PACTR pactr = pmvieMulti->Pscen()->PactrFromPt(
                        xpHwnd - rcWorkspace.xpLeft, ypHwnd - rcWorkspace.ypTop, &ibset);
                    if (pactr != pvNil && pmvieMulti->Pscen()->FActrSelected(pactr->Arid()))
                    {
                        vf4DMMObjectGroupWholeSelected = fFalse;
                        pmvieMulti->Pscen()->SelectActrRemove(pactr);
                        if (pmvieMulti->Pbwld() != pvNil)
                            pmvieMulti->Pbwld()->MarkDirty();
                        pmvieMulti->MarkViews();
                    }
                    Refresh4DMMObjectGroupsList(fFalse);
                    Update4DMMObjectGroupsControls();
                    *plwRet = 0;
                    return fTrue;
                }
            }
        }
    }

    // Object Properties gesture: with the ordinary hand/Reposition tool
    // (toolCompose) active, right-click the actor/prop/3D Word under the
    // cursor. Alt+right-click deliberately bypasses hit-testing and opens the
    // currently selected object's properties from anywhere in the viewport.
    // Empty-space right-click without Alt falls through to the stock browser path.
    if (wm == WM_RBUTTONUP && GetKeyState(VK_SHIFT) >= 0)
    {
        PMVIE pmvieProps = (_pstdio != pvNil) ? _pstdio->Pmvie() : pvNil;
        PMVU pmvuProps = pmvieProps != pvNil ? pmvieProps->PmvuCur() : pvNil;
        if (pmvieProps != pvNil && pmvieProps->Pscen() != pvNil && pmvuProps != pvNil &&
            pmvuProps->Tool() == toolCompose && !pmvieProps->FCameraInputActive() && Pkwa() != pvNil)
        {
            PGOB pgobWorkspace = Pkwa()->PgobFromHid(kidWorkspace);
            if (pgobWorkspace != pvNil)
            {
                RC rcWorkspace;
                pgobWorkspace->GetRc(&rcWorkspace, cooHwnd);
                const int32_t xpHwnd = (short)LOWORD(lw);
                const int32_t ypHwnd = (short)HIWORD(lw);
                if (FIn(xpHwnd, rcWorkspace.xpLeft, rcWorkspace.xpRight) &&
                    FIn(ypHwnd, rcWorkspace.ypTop, rcWorkspace.ypBottom))
                {
                    PACTR pactr = pvNil;
                    if (GetKeyState(VK_MENU) < 0)
                    {
                        pactr = pmvieProps->Pscen()->PactrSelected();
                        if (pactr != pvNil && MVIE::FDiagnosticsMode())
                            MVIE::DiagLog("object_properties alt_right_selected arid=%ld", (long)pactr->Arid());
                    }
                    else
                    {
                        int32_t ibset = ivNil;
                        pactr = pmvieProps->Pscen()->PactrFromPt(
                            xpHwnd - rcWorkspace.xpLeft, ypHwnd - rcWorkspace.ypTop, &ibset);
                        if (pactr != pvNil)
                            pmvieProps->Pscen()->SelectActr(pactr);
                    }

                    if (pactr != pvNil)
                    {
                        if (pmvieProps->Pbwld() != pvNil)
                            pmvieProps->Pbwld()->MarkDirty();
                        pmvieProps->MarkViews();
                        pmvieProps->FOpenObjectPropertiesEditor(pactr->Arid());
                        *plwRet = 0;
                        return fTrue;
                    }
                }
            }
        }
    }

    // Actor Studio gesture: with the Action tool active, right-clicking an
    // actor in the movie viewport opens the isolated Actor Studio inspector.
    // Normal Action-tool left-click remains untouched and still opens 3DMM's
    // stock Actions browser.  This runs after Shift+right-click subtraction so
    // -multi keeps its established deselection gesture.
    if (wm == WM_RBUTTONUP && GetKeyState(VK_SHIFT) >= 0)
    {
        PMVIE pmvieStudio = (_pstdio != pvNil) ? _pstdio->Pmvie() : pvNil;
        PMVU pmvuStudio = pmvieStudio != pvNil ? pmvieStudio->PmvuCur() : pvNil;
        if (pmvieStudio != pvNil && pmvieStudio->Pscen() != pvNil && pmvuStudio != pvNil &&
            pmvuStudio->Tool() == toolAction && !pmvieStudio->FCameraInputActive() && Pkwa() != pvNil)
        {
            PGOB pgobWorkspace = Pkwa()->PgobFromHid(kidWorkspace);
            if (pgobWorkspace != pvNil)
            {
                RC rcWorkspace;
                pgobWorkspace->GetRc(&rcWorkspace, cooHwnd);
                const int32_t xpHwnd = (short)LOWORD(lw);
                const int32_t ypHwnd = (short)HIWORD(lw);
                if (FIn(xpHwnd, rcWorkspace.xpLeft, rcWorkspace.xpRight) &&
                    FIn(ypHwnd, rcWorkspace.ypTop, rcWorkspace.ypBottom))
                {
                    int32_t ibset = ivNil;
                    const int32_t xpWorkspace = xpHwnd - rcWorkspace.xpLeft;
                    const int32_t ypWorkspace = ypHwnd - rcWorkspace.ypTop;
                    PACTR pactr = Pactr4DMMActorStudioFromPoint(
                        pmvieStudio, xpWorkspace, ypWorkspace, &ibset);
                    if (pactr != pvNil)
                    {
                        pmvieStudio->Pscen()->SelectActr(pactr);
                        int32_t idGroup = 0;
                        pmvieStudio->FObjectInObjectGroup(pactr->Arid(), &idGroup);
                        MVIE::MultiLog(pmvieStudio,
                            "actor_studio_action_right_click arid=%ld group=%ld ibset=%ld",
                            (long)pactr->Arid(), (long)idGroup, (long)ibset);
                        FOpen4DMMActorStudioActor(pactr->Arid(), idGroup);
                        Refresh4DMMObjectGroupsList(fFalse);
                        Update4DMMObjectGroupsControls();
                        *plwRet = 0;
                        return fTrue;
                    }
                }
            }
        }
    }

    // Right-click is the native 3DMM gesture for reopening the hired actor,
    // hired prop and 3D-word material browsers, so keep it available without
    // requiring the optional -browser switch. The keyboard B shortcut remains
    // opt-in behind -browser.
    if (wm == WM_RBUTTONUP || (_fExternalBrowsers && wm == WM_KEYDOWN && wParam == ChLit('B')))
    {
        PMVIE pmvieBrowser = (_pstdio != pvNil) ? _pstdio->Pmvie() : pvNil;
        if (pmvieBrowser == pvNil || !pmvieBrowser->FCameraInputActive())
        {
            POINT ptBrowser;
            if (wm == WM_RBUTTONUP)
            {
                ptBrowser.x = (short)LOWORD(lw);
                ptBrowser.y = (short)HIWORD(lw);
            }
            else
            {
                if (!GetCursorPos(&ptBrowser) || !ScreenToClient(hwnd, &ptBrowser))
                {
                    ptBrowser.x = -1;
                    ptBrowser.y = -1;
                }
            }

            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            const int32_t dxpClient = rcClient.right - rcClient.left;
            const int32_t dypClient = rcClient.bottom - rcClient.top;
            const int32_t xpRaw = ptBrowser.x;
            const int32_t ypRaw = ptBrowser.y;
            const int32_t xp = dxpClient > 0 ? LwMulDiv(xpRaw, 640, dxpClient) : xpRaw;
            const int32_t yp = dypClient > 0 ? LwMulDiv(ypRaw, 480, dypClient) : ypRaw;

            int32_t exbrk = 0;
            if (xp >= 0 && xp <= 45 && yp >= 99 && yp <= 410)
                exbrk = 1; // actor browser
            else if (xp >= 593 && xp <= 639 && yp >= 99 && yp <= 410)
                exbrk = 2; // prop browser
            else if (wm == WM_RBUTTONUP && xp >= 197 && xp <= 246 && yp >= 322 && yp <= 369)
                exbrk = 3; // 3D-word color / texture browser

            if (MVIE::FDiagnosticsMode())
            {
                MVIE::DiagLog("browser input: source=%s raw=(%d,%d) logical=(%d,%d) client=%dx%d kind=%d cameraInput=%d",
                              wm == WM_RBUTTONUP ? "right-click" : "B", xpRaw, ypRaw, xp, yp,
                              dxpClient, dypClient, exbrk,
                              pmvieBrowser != pvNil ? (int)pmvieBrowser->FCameraInputActive() : 0);
            }

            if (exbrk != 0 && !(wm == WM_KEYDOWN && exbrk == 3))
            {
                const bool fShown = FShowExternalContentBrowser(exbrk);
                if (MVIE::FDiagnosticsMode())
                    MVIE::DiagLog("browser input: show kind=%d result=%d", exbrk, (int)fShown);
                *plwRet = 0;
                return fTrue;
            }
        }
        else if (MVIE::FDiagnosticsMode())
        {
            MVIE::DiagLog("browser input ignored: camera/manual input active");
        }
    }

    // Selection itself is established on left-button down. By left-button up
    // the scene's unlimited selection list is authoritative, so update Object
    // Groups synchronously instead of waiting for the tool window's own timer
    // (which can be starved by Kauai tracking/modal loops).
    if (wm == WM_LBUTTONUP && MVIE::FMultiSelectMode())
    {
        vf4DMMObjectGroupWholeSelected = fFalse;
        Refresh4DMMObjectGroupsList(fFalse);
        Update4DMMObjectGroupsControls();
    }

    switch (wm)
    {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wParam == VK_SNAPSHOT)
        {
            _FSaveScreenshot();
            *plwRet = 0;
            return fTrue;
        }
        break;

    case WM_TIMER:
#if defined(BRENDER_MODERN_14)
        _FEnsureViewportWindow();
#else
        if (_fViewportWindow)
            _FEnsureViewportWindow();
#endif
        _PaintViewportWindow();
        Update4DMMSettingsControls();
        // Native Object Groups WM_TIMER messages can be delayed while Kauai is
        // inside its own tracking/modal pumps. The main application timer is
        // already the reliable cross-mode heartbeat, so use it to notice movie
        // replacement/reload and live Bind eligibility as well.
        Refresh4DMMObjectGroupsList(fFalse);
        break;

    case WM_ERASEBKGND:
        // 3DMMv1.0: Tell windows that we handled the Erase so it doesn't do one.
        // 3DMMv1.0: In general we don't want to erase our background ahead of time.
        // 3DMMv1.0: This prevents AVIs from flashing.
        *plwRet = fTrue;
        return fTrue;

    case WM_SIZE: {
        bool fRet;
        int32_t lwStyle;

        fRet = APP_PAR::_FFrameWndProc(hwnd, wm, wParam, lw, plwRet);
        lwStyle = GetWindowLong(hwnd, GWL_STYLE);
        lwStyle &= ~WS_MAXIMIZEBOX;
        if (wParam == SIZE_MINIMIZED)
        {
            _fMinimized = fTrue;
            if (vpcex != pvNil)
                lwStyle |= WS_SYSMENU;
            else
                lwStyle &= ~WS_SYSMENU;
        }
        else if (!_fRunInWindow)
            lwStyle &= ~WS_SYSMENU;
        SetWindowLong(hwnd, GWL_STYLE, lwStyle);
        if (wParam == SIZE_RESTORED)
        {
            if (_fMainWindowCreated)
                _RebuildMainWindow();
            if (_fSwitchedResolution && _fMinimized)
            {
                if (!_FDisplayIs640480())
                    _FSwitch640480(fTrue);
            }
            ShowWindow(vwig.hwndApp, SW_RESTORE); // 3DMMv1.0: restore app window
            _fMinimized = fFalse;
        }
        return fRet;
    }
    case WM_DISPLAYCHANGE:
        // 3DMMv1.0: Note that we don't need to do any of this if we're closing down
        if (_fQuit)
            break;

        if (_fForceWindow)
        {
            _fRunInWindow = fTrue;
            if (_fMainWindowCreated && !_fMinimized)
                _RebuildMainWindow();
            return fTrue;
        }

        if (_FDisplayIs640480())
        {
            _fRunInWindow = fFalse;
            _RebuildMainWindow();
        }
        else
        {
            // 3DMMv1.0: We're not running at 640x480 resolution now. Current design is that
            // 3DMMv1.0: if we switch from 640x480 to higher while the app is minimized, the
            // 3DMMv1.0: app it to still be full screen when restored. Therefore we don't
            // 3DMMv1.0: need to change _fRunInWindow here, as that we remain the same as
            // 3DMMv1.0: before the res switch, (as will the Windows properties for the app).
            // 3DMMv1.0: All we need to is make a note that we're no longer running in the
            // 3DMMv1.0: current windows settings resolution if we're running in full screen.

            if (!_fRunInWindow)
            {
                _fSwitchedResolution = fTrue;

                // 3DMMv1.0: If we're not minimized then we must switch to 640x480 resolution.
                // 3DMMv1.0: Don't switch res unless we're the active app window

                if (!_fMinimized && GetForegroundWindow() == vwig.hwndApp)
                {
                    if (!_FSwitch640480(fTrue))
                        _fSwitchedResolution = fFalse;
                }
            }

            // 3DMMv1.0: Call rebuild now to make sure the app window gets positioned
            // 3DMMv1.0: at the centre of the screen. Note that none of the other
            // 3DMMv1.0: window attributes will change beneath _RebuildMainWindow.
            if (!_fMinimized)
                _RebuildMainWindow();
        }
        return fTrue;

    case WM_INITMENUPOPUP: {
        // 3DMMv1.0: Disable the Close menu item if we are displaying a modal topic.
        // 3DMMv1.0: The user can't exit until a modal topic is dismissed.

        bool fDisableClose = (vpappb->CactModal() > (_pcex != pvNil ? 1 : 0));

        EnableMenuItem((HMENU)wParam, SC_CLOSE,
                       MF_BYCOMMAND | (fDisableClose ? (MF_DISABLED | MF_GRAYED) : MF_ENABLED));

        break;
    }
    }

    if ((_pstdio == pvNil) || (_pstdio->Pmvie() == pvNil))
    {
        return APP_PAR::_FFrameWndProc(hwnd, wm, wParam, lw, plwRet);
    }

    switch (wm)
    {
    case WM_QUERY_EXISTS:
        *plwRet = _pstdio->Pmvie()->LwQueryExists(wParam, lw);
        return (fTrue);

    case WM_QUERY_LOCATION:
        *plwRet = _pstdio->Pmvie()->LwQueryLocation(wParam, lw);
        return (fTrue);

    case WM_SET_MOVIE_POS:
        *plwRet = _pstdio->Pmvie()->LwSetMoviePos(wParam, lw);
        return (fTrue);

    default:
        return APP_PAR::_FFrameWndProc(hwnd, wm, wParam, lw, plwRet);
    }
}
#endif // 3DMMv1.0: WIN

/** 3DMMv1.0: *************************************************************************
 *
 * Returns whether or not screen savers should be allowed.
 *
 * Parameters:
 *  None
 *
 * Returns:
 *  fTrue  - Screen savers should be allowed
 *  fFalse - Screen savers should be blocked
 *
 **************************************************************************/
bool APP::FAllowScreenSaver(void)
{
    AssertBaseThis(0);

    // 3DMMv1.0: Disable the screen saver if...
    // 3DMMv1.0: 1. We're going to autominimize if a screen saver starts. Otherwise
    // 3DMMv1.0:    the user would be confused when they get back to the machine.
    // 3DMMv1.0: 2. We've switched resolutions, (ie we're full screen in a > 640x480 mode).
    // 3DMMv1.0:    Otherwise the screen save only acts on a portion of the screen.

    return !_FDisplayIs640480() && !_fSwitchedResolution;
}

/** 3DMMv1.0: *************************************************************************
    Disable the application accelerators
***************************************************************************/
void APP::DisableAccel(void)
{
    AssertBaseThis(0); // 3DMMv1.0: Gets called from destructors

    if (_cactDisable == 0)
    {
        vpcex->BuryCmh(_patblMain);
    }

    _cactDisable++;
}

/** 3DMMv1.0: *************************************************************************
    Enable the application accelerators
***************************************************************************/
void APP::EnableAccel(void)
{
    AssertBaseThis(0); // 3DMMv1.0: Gets called from destructors
    Assert(_cactDisable > 0, "Enable called w/o a disable");

    _cactDisable--;

    if (_cactDisable == 0)
    {
        AssertDo(vpcex->FAddCmh(_patblMain, 0, kgrfcmmAll), "Could not enable accelerator table");
    }
}

/** 3DMMv1.0: *************************************************************************
 *
 * Handle disable accelerator command
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 **************************************************************************/
bool APP::FCmdDisableAccel(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    DisableAccel();
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Handle enable accelerator command
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 **************************************************************************/
bool APP::FCmdEnableAccel(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    EnableAccel();
    return fTrue;
}

/** 3DMMv1.0: ****************************************************************************
    FCmdInvokeSplot
        Invokes the splot machine.

    Arguments:
        PCMD pcmd
            rglw[0]  --  contains the GOB id of the parent of the Splot machine
            rglw[1]  --  contains the GOB id of the Splot machine itself

    Returns: fTrue always

************************************************************ PETED ***********/
bool APP::FCmdInvokeSplot(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PSPLOT psplot;

    psplot = SPLOT::PsplotNew(pcmd->rglw[0], pcmd->rglw[1], _pcrmAll);
    if (psplot == pvNil)
        PushErc(ercSocCantInitSplot);

    return fTrue;
}

/** 3DMMEx: *************************************************************************
 *
 * Handle toggle fullscreen command
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 **************************************************************************/
bool APP::FCmdToggleFullscreen(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    AssertDo(_FSetRunInWindow(FPure(!_fRunInWindow)), "Could not toggle fullscreen");
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Handoff a movie to the app so it can pass it on to the studio
***************************************************************************/
void APP::HandoffMovie(PMVIE pmvie)
{
    AssertThis(0);
    AssertPo(pmvie, 0);

    ReleasePpo(&_pmvieHandoff);
    _pmvieHandoff = pmvie;
    _pmvieHandoff->AddRef();
}

/** 3DMMv1.0: *************************************************************************
    Grab the APP movie
***************************************************************************/
PMVIE APP::PmvieRetrieve(void)
{
    AssertThis(0);

    PMVIE pmvie = _pmvieHandoff;

    _pmvieHandoff = pvNil; // 3DMMv1.0:  Caller now owns this pointer
    return pmvie;
}

#ifdef BUG1085
/** 3DMMv1.0: ****************************************************************************
    HideCurs
    ShowCurs
    PushCurs
    PopCurs

        Some simple cursor restoration functionality, for use when a modal
        topic comes up.  Assumes that you won't try to mess with the cursor
        state while you've got the old cursor state pushed, and that you won't
        try to push the cursor state while you've already got the old cursor
        state pushed.

************************************************************ PETED ***********/
void APP::HideCurs(void)
{
    AssertThis(0);

    Assert(_cactCursHide != ivNil, "Can't hide cursor in Push/PopCurs pair");
    _cactCursHide++;
    APP_PAR::HideCurs();
}

void APP::ShowCurs(void)
{
    AssertThis(0);

    Assert(_cactCursHide > 0, "Unbalanced ShowCurs call");
    _cactCursHide--;
    APP_PAR::ShowCurs();
}

void APP::PushCurs(void)
{
    AssertThis(0);

    Assert(_cactCursHide != ivNil, "Can't nest cursor restoration");
    _cactCursSav = _cactCursHide;
    while (_cactCursHide)
        ShowCurs();
    _cactCursHide = ivNil;
}

void APP::PopCurs(void)
{
    AssertThis(0);

    Assert(_cactCursHide == ivNil, "Unbalanced cursor restoration");
    _cactCursHide = 0;
    while (_cactCursHide < _cactCursSav)
        HideCurs();
}
#endif // 3DMMv1.0: BUG1085

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of the APP
***************************************************************************/
void APP::AssertValid(uint32_t grf)
{
    APP_PAR::AssertValid(0);
    AssertNilOrPo(_pstdio, 0);
    AssertNilOrPo(_ptatr, 0);
    AssertNilOrPo(_pmvieHandoff, 0);
    AssertPo(_pcfl, 0);
    AssertPo(_pgstStudioFiles, 0);
    AssertPo(_pgstBuildingFiles, 0);
    AssertPo(_pgstSharedFiles, 0);
    AssertPo(_pgstApp, 0);
    AssertPo(_pkwa, 0);
    AssertPo(_pgstApp, 0);
    AssertPo(_pcrmAll, 0);
    AssertPo(_pglicrfBuilding, 0);
    AssertPo(_pglicrfStudio, 0);
    AssertPo(_patblMain, 0);
    AssertPo(_patblGlobal, 0);
    AssertNilOrPo(_pcex, 0);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the APP
***************************************************************************/
void APP::MarkMem(void)
{
    AssertThis(0);
    APP_PAR::MarkMem();
    MarkMemObj(vptagm);
    MTRL::MarkShadeTable();
    TDT::MarkActionNames();
    MarkMemObj(_pstdio);
    MarkMemObj(_ptatr);
    MarkMemObj(_pmvieHandoff);
    MarkMemObj(_pcfl);
    MarkMemObj(_pgstStudioFiles);
    MarkMemObj(_pgstBuildingFiles);
    MarkMemObj(_pgstSharedFiles);
    MarkMemObj(_pgstApp);
    MarkMemObj(_pkwa);
    MarkMemObj(_pgstApp);
    MarkMemObj(_pcrmAll);
    MarkMemObj(_pglicrfBuilding);
    MarkMemObj(_pglicrfStudio);
    MarkMemObj(_pcex);
    MarkMemObj(_patblMain);
    MarkMemObj(_patblGlobal);
#if defined(KAUAI_WIN32)
    MarkMemObj(_pgptViewport);
    MarkExternalContentBrowserMem();
#endif // 3DMMEx: KAUAI_WIN32
}
#endif // 3DMMv1.0: DEBUG

//
//
//
// 3DMMv1.0:  KWA (KidWorld for App) stuff begins here
//
//
//

/** 3DMMv1.0: *************************************************************************
    KWA destructor
***************************************************************************/
KWA::~KWA(void)
{
    ReleasePpo(&_pmbmp);
}

/** 3DMMv1.0: *************************************************************************
    Set the KWA's MBMP (for splash screen)
***************************************************************************/
void KWA::SetMbmp(PMBMP pmbmp)
{
    AssertThis(0);
    AssertNilOrPo(pmbmp, 0);

    RC rc;

    if (pvNil != pmbmp)
        pmbmp->AddRef();
    ReleasePpo(&_pmbmp);
    _pmbmp = pmbmp;
    GetRcVis(&rc, cooLocal);
    vpappb->MarkRc(&rc, this);
}

/** 3DMMv1.0: *************************************************************************
    Draw the KWA's MBMP, if any (for splash screen)
***************************************************************************/
void KWA::Draw(PGNV pgnv, RC *prcClip)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    AssertVarMem(prcClip);

    if (pvNil != _pmbmp)
        pgnv->DrawMbmp(_pmbmp, 0, 0);
}

/** 3DMMv1.0: *************************************************************************
    Find a file given a string.
***************************************************************************/
bool KWA::FFindFile(PSTN pstnSrc, PFNI pfni)
{
    AssertThis(0);
    AssertPo(pstnSrc, 0);
    AssertPo(pfni, 0);

    return vptagm->FFindFile(vapp.SidProduct(), pstnSrc, pfni, FAskForCD());
}

/** 3DMMv1.0: *************************************************************************
    Do a modal help topic.
***************************************************************************/
bool KWA::FModalTopic(PRCA prca, CNO cnoTopic, int32_t *plwRet)
{
    AssertThis(0);
    AssertPo(prca, 0);
    AssertVarMem(plwRet);

    bool fRet;

    // 3DMMv1.0: Take any special action here if necessary, before the
    // 3DMMv1.0: modal help topic is displayed. (Eg, disable help keys).

    // 3DMMv1.0: Now take the default action.
#ifdef BUG1085
    vapp.PushCurs();
    fRet = KWA_PAR::FModalTopic(prca, cnoTopic, plwRet);
    vapp.PopCurs();
#else
    fRet = KWA_PAR::FModalTopic(prca, cnoTopic, plwRet);
#endif // 3DMMv1.0: !BUG1085

    // 3DMMv1.0: Let script know that the modal topic has been dismissed.
    // 3DMMv1.0: This is required for the projects.
    vpcex->EnqueueCid(cidModalTopicClosed, 0, 0, *plwRet);

    return fRet;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of the KWA
***************************************************************************/
void KWA::AssertValid(uint32_t grf)
{
    KWA_PAR::AssertValid(0);
    AssertNilOrPo(_pmbmp, 0);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the KWA
***************************************************************************/
void KWA::MarkMem(void)
{
    AssertThis(0);
    KWA_PAR::MarkMem();
    MarkMemObj(_pmbmp);
}
#endif // 3DMMv1.0: DEBUG
