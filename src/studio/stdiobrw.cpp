/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

  stdiobrw.cpp

  Author: ******

  Date: April, 1995

    Review Status: Reviewed

    This file contains the code which invokes browsers
    and applies browser selections.

    Studio Independent Browsers:
    BASE --> CMH --> GOK	-->	BRWD  (Browser display class)
    BRWD --> BRWL  (Browser list class; chunky based)
    BRWD --> BRWT  (Browser text class)
    BRWD --> BRWL --> BRWN  (Browser named list class)

    Studio Dependent Browsers:
    BRWD --> BRWT --> BRWA  (Browser action class)
    BRWD --> BRWL --> BRWP	(Browser prop/actor class)
    BRWD --> BRWL --> BRWB	(Browser background class)
    BRWD --> BRWL --> BRWC	(Browser camera class)
    BRWD --> BRWL --> BRWN --> BRWM (Browser music class)
    BRWD --> BRWL --> BRWN --> BRWM --> BRWI (Browser import sound class)

NOTE:  In this implementation, browsers are considered to be studio related.
If for any reason one wanted to decouple them from the studio, then	it would
be easy for browser.cpp to enqueue cids for
    1) SetTagTool and
    2) ApplySelection (which would take a browser identifying argument).
The studio (or anyone else) could then apply all browser based selections.

The chunky based browsers come in two categories:
    1) Content spanning potentially multiple products (eg, bkgds, actors)
    2) Content which is a child of an existing selection.
Browsers of type 1) are cno based.  They sort on the basis of the cno of the
thumbnail chunk. The contents of the thumbnail chunk then point to the cno
of the CD content.
Browsers of type 2) are chid based. They sort on the basis of the chid of the
thumbnail chunk.  The contents of the thumbnail chunk then point to the chid
of the CD content.

***************************************************************************/

#include "soc.h"
#include "studio.h"
#if defined(KAUAI_WIN32)
#include <stdarg.h>
#include <stdio.h>
#include <vector>
#endif

ASSERTNAME


#if defined(KAUAI_WIN32)
extern bool FOpen4DMMCreatePartForActor(int32_t arid);
/***************************************************************************
    External content browsers for the modernized Studio UI.

    These deliberately consume the same thumbnail descriptor files used by
    the original in-app browsers.  The actor/material grids therefore render
    the original GOKD thumbnail resources rather than maintaining a second
    catalogue that can drift out of sync with 3DMM content.
***************************************************************************/
enum EXBRK
{
    kexbrActor = 1,
    kexbrProp = 2,
    kexbrMaterial = 3,
    kexbrObject = 4,
};

const int kexbrIdSceneOnly = 4101;
const int kexbrIdBrowserList = 4102;
const int kexbrIdMoveCam = 4103;
const int kexbrIdShowThumbnails = 4104;
const int kexbrIdPropModeProps = 4105;
const int kexbrIdPropModeWords = 4106;
const int kexbrIdPropModeBoth = 4107;
const int kexbrIdUsePropIcons = 4108;
const int kexbrIdCustomColor = 4109;
const int kexbrIdFilterActors = 4110;
const int kexbrIdFilterProps = 4111;
const int kexbrIdFilterWords = 4112;
const int kexbrIdFrameOnly = 4113;
const int kexbrIdVisibleOnly = 4114;
const int kexbrIdCreatePart = 4115;
const UINT kwm4DMMExternalBrowserFreeCam = WM_APP + 0x4C;
const UINT kwm4DMMExternalBrowserEscape = WM_APP + 0x4D;
const int kexbrTimerContext = 1;
const int32_t kdypExbrListRow = 22;
const int32_t kdypExbrPropThumbRow = 82;

static HWND _Hwnd4DMMExternalBrowserOwner(void)
{
    if (vwig.hwndApp != hNil && IsWindow(vwig.hwndApp))
    {
        HWND hwndScale = (HWND)GetPropA(vwig.hwndApp, "4DMMUiScaleWindow");
        if (GetPropA(vwig.hwndApp, "4DMMUiScaleSuspended") == pvNil &&
            hwndScale != hNil && IsWindow(hwndScale))
        {
            return hwndScale;
        }
        return vwig.hwndApp;
    }
    return hNil;
}
const int32_t kypExbrActorControlsBottom = 145;
const int32_t kypExbrPropControlsBottom = 216;
const int32_t kypExbrObjectControlsBottom = 216;

enum EXBRPROPMODE
{
    kexbrPropModeProps = 1,
    kexbrPropModeWords = 2,
    kexbrPropModeBoth = 3,
};

// These dimensions come from the 3D-word easel's chunky layout (pos3.chh),
// which is not part of this C++ translation unit. Keep local browser constants
// so the external previews match the original 78x44 frames without depending
// on chunky-only symbols.
const int32_t kdxpExbrMaterialFrame = 78;
const int32_t kdypExbrMaterialFrame = 44;
const int32_t kdxpExbrMaterialBorder = 7;
const int32_t kdypExbrMaterialBorder = 7;
const int32_t kypExbrMaterialGridTop = 40;

struct EXBRSTATE
{
    EXBRK exbrk;
    HWND hwnd;
    HWND hwndSceneOnly;
    HWND hwndFrameOnly;
    HWND hwndVisibleOnly;
    HWND hwndMoveCam;
    HWND hwndCreatePart;
    HWND hwndShowThumbnails;
    HWND hwndPropModeProps;
    HWND hwndPropModeWords;
    HWND hwndPropModeBoth;
    HWND hwndUsePropIcons;
    HWND hwndFilterActors;
    HWND hwndFilterProps;
    HWND hwndFilterWords;
    HWND hwndCustomColor;
    HWND hwndList;
    PBCL pbcl;
    PBCL pbclAux;
    PBCLS pbcls;
    PCRM pcrm;
    PGOB pgobThumbHost;
    std::vector<int32_t> rgithd;
    int32_t irowFirst;
    int32_t ithdSelected;
    bool fSceneOnly;
    bool fFrameOnly;
    bool fVisibleOnly;
    bool fMoveCam;
    bool fShowThumbnails;
    bool fUsePropIcons;
    bool fFilterActors;
    bool fFilterProps;
    bool fFilterWords;
    EXBRPROPMODE exbrPropMode;
    uint32_t luSourceSig;

    EXBRSTATE()
        : exbrk(kexbrActor), hwnd(hNil), hwndSceneOnly(hNil), hwndFrameOnly(hNil), hwndVisibleOnly(hNil), hwndMoveCam(hNil), hwndCreatePart(hNil), hwndShowThumbnails(hNil),
          hwndPropModeProps(hNil), hwndPropModeWords(hNil), hwndPropModeBoth(hNil), hwndUsePropIcons(hNil),
          hwndFilterActors(hNil), hwndFilterProps(hNil), hwndFilterWords(hNil), hwndCustomColor(hNil), hwndList(hNil),
          pbcl(pvNil), pbclAux(pvNil), pbcls(pvNil), pcrm(pvNil), pgobThumbHost(pvNil), irowFirst(0), ithdSelected(ivNil),
          fSceneOnly(fTrue), fFrameOnly(fFalse), fVisibleOnly(fFalse), fMoveCam(fTrue), fShowThumbnails(fTrue), fUsePropIcons(fTrue),
          fFilterActors(fTrue), fFilterProps(fTrue), fFilterWords(fTrue),
          exbrPropMode(kexbrPropModeWords), luSourceSig(0)
    {
    }
};

static const achar kszExternalBrowserClass[] = PszLit("3DMMExExternalContentBrowser");
static HWND vhwndActorBrowser = hNil;
static HWND vhwndPropBrowser = hNil;
static HWND vhwndMaterialBrowser = hNil;
static HWND vhwndObjectBrowser = hNil;
static HWND vhwndBrowserCameraFollowOwner = hNil;
static bool vfCombineActorsPropsBrowsers = fFalse;

struct EXBRPREFS
{
    bool fInit;
    bool fSceneOnly;
    bool fFrameOnly;
    bool fVisibleOnly;
    bool fMoveCam;
    bool fShowThumbnails;
    bool fUsePropIcons;
    bool fFilterActors;
    bool fFilterProps;
    bool fFilterWords;
    EXBRPROPMODE exbrPropMode;
};

static EXBRPREFS vrgExbrPrefs[5];
static bool vfExternalBrowserGlobalPrefsLoaded = fFalse;

static bool _FExternalBrowserIniPath(achar *pszPath, size_t cbPath)
{
    if (pszPath == pvNil || cbPath == 0)
        return fFalse;
    pszPath[0] = 0;
    DWORD cch = GetModuleFileNameA(pvNil, pszPath, (DWORD)cbPath);
    if (cch == 0 || cch >= cbPath)
        return fFalse;
    achar *pchSlash = strrchr(pszPath, '\\');
    achar *pchSlash2 = strrchr(pszPath, '/');
    if (pchSlash2 != pvNil && (pchSlash == pvNil || pchSlash2 > pchSlash))
        pchSlash = pchSlash2;
    if (pchSlash == pvNil)
        return fFalse;
    pchSlash[1] = 0;
    strcat_s(pszPath, cbPath, "4dmm_ui.ini");
    return fTrue;
}

static PCSZ _PszExternalBrowserPrefsSection(EXBRK exbrk)
{
    switch (exbrk)
    {
    case kexbrActor: return "ActorBrowser";
    case kexbrProp: return "PropBrowser";
    case kexbrObject: return "ObjectBrowser";
    default: return pvNil;
    }
}

static void _ReadExternalBrowserPrefsFromIni(EXBRK exbrk, EXBRPREFS *pprefs)
{
    PCSZ pszSection = _PszExternalBrowserPrefsSection(exbrk);
    achar szPath[MAX_PATH];
    if (pprefs == pvNil || pszSection == pvNil || !_FExternalBrowserIniPath(szPath, SIZEOF(szPath)))
        return;
#define READ_EXBR_BOOL(member, key) \
    pprefs->member = FPure(GetPrivateProfileIntA(pszSection, key, pprefs->member ? 1 : 0, szPath) != 0)
    READ_EXBR_BOOL(fSceneOnly, "SceneOnly");
    READ_EXBR_BOOL(fFrameOnly, "FrameOnly");
    READ_EXBR_BOOL(fVisibleOnly, "VisibleOnly");
    READ_EXBR_BOOL(fMoveCam, "MoveCamera");
    READ_EXBR_BOOL(fShowThumbnails, "ShowThumbnails");
    READ_EXBR_BOOL(fUsePropIcons, "UsePropIcons");
    READ_EXBR_BOOL(fFilterActors, "FilterActors");
    READ_EXBR_BOOL(fFilterProps, "FilterProps");
    READ_EXBR_BOOL(fFilterWords, "FilterWords");
#undef READ_EXBR_BOOL
    const int32_t mode = (int32_t)GetPrivateProfileIntA(pszSection, "PropMode", (int)pprefs->exbrPropMode, szPath);
    if (mode >= (int32_t)kexbrPropModeProps && mode <= (int32_t)kexbrPropModeBoth)
        pprefs->exbrPropMode = (EXBRPROPMODE)mode;
}

static void _WriteExternalBrowserPrefsToIni(EXBRK exbrk, const EXBRPREFS *pprefs)
{
    PCSZ pszSection = _PszExternalBrowserPrefsSection(exbrk);
    achar szPath[MAX_PATH];
    if (pprefs == pvNil || pszSection == pvNil || !_FExternalBrowserIniPath(szPath, SIZEOF(szPath)))
        return;
    achar sz[32];
#define WRITE_EXBR_BOOL(member, key) \
    WritePrivateProfileStringA(pszSection, key, pprefs->member ? "1" : "0", szPath)
    WRITE_EXBR_BOOL(fSceneOnly, "SceneOnly");
    WRITE_EXBR_BOOL(fFrameOnly, "FrameOnly");
    WRITE_EXBR_BOOL(fVisibleOnly, "VisibleOnly");
    WRITE_EXBR_BOOL(fMoveCam, "MoveCamera");
    WRITE_EXBR_BOOL(fShowThumbnails, "ShowThumbnails");
    WRITE_EXBR_BOOL(fUsePropIcons, "UsePropIcons");
    WRITE_EXBR_BOOL(fFilterActors, "FilterActors");
    WRITE_EXBR_BOOL(fFilterProps, "FilterProps");
    WRITE_EXBR_BOOL(fFilterWords, "FilterWords");
#undef WRITE_EXBR_BOOL
    sprintf_s(sz, SIZEOF(sz), "%ld", (long)pprefs->exbrPropMode);
    WritePrivateProfileStringA(pszSection, "PropMode", sz, szPath);
}

static void _EnsureExternalBrowserGlobalPrefsLoaded(void)
{
    if (vfExternalBrowserGlobalPrefsLoaded)
        return;
    vfExternalBrowserGlobalPrefsLoaded = fTrue;
    achar szPath[MAX_PATH];
    if (_FExternalBrowserIniPath(szPath, SIZEOF(szPath)))
        vfCombineActorsPropsBrowsers = FPure(
            GetPrivateProfileIntA("Global", "CombineActorPropBrowsers", 0, szPath) != 0);
}

static void _WriteExternalBrowserCombinedPref(void)
{
    achar szPath[MAX_PATH];
    if (_FExternalBrowserIniPath(szPath, SIZEOF(szPath)))
        WritePrivateProfileStringA("Global", "CombineActorPropBrowsers",
                                   vfCombineActorsPropsBrowsers ? "1" : "0", szPath);
}

static EXBRPREFS *_PprefsExternalBrowser(EXBRK exbrk)
{
    if (exbrk < kexbrActor || exbrk > kexbrObject || exbrk == kexbrMaterial)
        return pvNil;
    EXBRPREFS *pprefs = &vrgExbrPrefs[(int)exbrk];
    if (!pprefs->fInit)
    {
        ClearPb(pprefs, SIZEOF(*pprefs));
        pprefs->fInit = fTrue;
        pprefs->fSceneOnly = fTrue;
        pprefs->fFrameOnly = fFalse;
        pprefs->fVisibleOnly = fFalse;
        pprefs->fMoveCam = fTrue;
        pprefs->fShowThumbnails = FPure(exbrk == kexbrActor || exbrk == kexbrObject);
        pprefs->fUsePropIcons = fTrue;
        pprefs->fFilterActors = fTrue;
        pprefs->fFilterProps = fTrue;
        pprefs->fFilterWords = fTrue;
        pprefs->exbrPropMode = kexbrPropModeWords;
        _ReadExternalBrowserPrefsFromIni(exbrk, pprefs);
    }
    return pprefs;
}

static void _LoadExternalBrowserPrefs(EXBRSTATE *pst)
{
    EXBRPREFS *pprefs = pst == pvNil ? pvNil : _PprefsExternalBrowser(pst->exbrk);
    if (pprefs == pvNil)
        return;
    pst->fSceneOnly = pprefs->fSceneOnly;
    pst->fFrameOnly = pprefs->fFrameOnly;
    pst->fVisibleOnly = pprefs->fVisibleOnly;
    pst->fMoveCam = pprefs->fMoveCam;
    pst->fShowThumbnails = pprefs->fShowThumbnails;
    pst->fUsePropIcons = pprefs->fUsePropIcons;
    pst->fFilterActors = pprefs->fFilterActors;
    pst->fFilterProps = pprefs->fFilterProps;
    pst->fFilterWords = pprefs->fFilterWords;
    pst->exbrPropMode = pprefs->exbrPropMode;
}

static void _SaveExternalBrowserPrefs(EXBRSTATE *pst)
{
    EXBRPREFS *pprefs = pst == pvNil ? pvNil : _PprefsExternalBrowser(pst->exbrk);
    if (pprefs == pvNil)
        return;
    pprefs->fSceneOnly = pst->fSceneOnly;
    pprefs->fFrameOnly = pst->fFrameOnly;
    pprefs->fVisibleOnly = pst->fVisibleOnly;
    pprefs->fMoveCam = pst->fMoveCam;
    pprefs->fShowThumbnails = pst->fShowThumbnails;
    pprefs->fUsePropIcons = pst->fUsePropIcons;
    pprefs->fFilterActors = pst->fFilterActors;
    pprefs->fFilterProps = pst->fFilterProps;
    pprefs->fFilterWords = pst->fFilterWords;
    pprefs->exbrPropMode = pst->exbrPropMode;
    _WriteExternalBrowserPrefsToIni(pst->exbrk, pprefs);
}

static void _LogExternalBrowser(PCSZ pszFormat, ...)
{
    if (!MVIE::FDiagnosticsMode() || pszFormat == pvNil)
        return;

    achar sz[512];
    va_list ap;
    va_start(ap, pszFormat);
    vsnprintf_s(sz, SIZEOF(sz), _TRUNCATE, pszFormat, ap);
    va_end(ap);
    MVIE::DiagLog("browser: %s", sz);
}

static HWND *_PhwndForExternalBrowser(EXBRK exbrk)
{
    switch (exbrk)
    {
    case kexbrActor:
        return &vhwndActorBrowser;
    case kexbrProp:
        return &vhwndPropBrowser;
    case kexbrMaterial:
        return &vhwndMaterialBrowser;
    case kexbrObject:
        return &vhwndObjectBrowser;
    default:
        return pvNil;
    }
}

static bool _FExternalBrowserContextAlive(EXBRK exbrk)
{
    PSTDIO pstdio = vpapp->Pstdio();
    if (pstdio == pvNil || pstdio->Pmvie() == pvNil)
    {
        _LogExternalBrowser("context rejected kind=%d: no Studio/movie", (int)exbrk);
        return fFalse;
    }

    // Keep actor/prop browsers alive during Free Cam so the camera-follow
    // checkbox can be visibly cleared and the browser is still there when the
    // user exits Free Cam.  Manual Camera remains exclusive and continues to
    // suppress native browser interaction.
    if (pstdio->Pmvie()->FManualCameraMode())
    {
        _LogExternalBrowser("context rejected kind=%d: manual camera active", (int)exbrk);
        return fFalse;
    }

    if (exbrk == kexbrMaterial)
    {
        PGOB pgob = vapp.Pkwa()->PgobFromHid(kidSpltGlass);
        const bool fAlive = FPure(pgob != pvNil && pgob->FIs(kclsESLT));
        _LogExternalBrowser("material easel context: pgob=%p valid=%d", pgob, (int)fAlive);
        return fAlive;
    }

    const bool fAlive = FPure(pstdio->Pmvie()->Pscen() != pvNil);
    _LogExternalBrowser("scene context kind=%d valid=%d", (int)exbrk, (int)fAlive);
    return fAlive;
}

static bool _FExternalBrowserItemUsedInScene(EXBRSTATE *pst, int32_t iitem)
{
    AssertVarMem(pst);
    if (pst->exbrk == kexbrMaterial)
        return fTrue;

    PSTDIO pstdio = vpapp->Pstdio();
    if (pstdio == pvNil || pstdio->Pmvie() == pvNil || pstdio->Pmvie()->Pscen() == pvNil)
        return fFalse;

    int32_t arid;
    int32_t cactRef;
    STN stn;
    if (!pstdio->Pmvie()->FGetArid(iitem, &arid, &stn, &cactRef))
        return fFalse;

    // PactrFromArid searches the scene's lifetime actor list, so this means
    // "exists somewhere in the current scene", not just "visible this frame".
    return FPure(pstdio->Pmvie()->Pscen()->PactrFromArid(arid) != pvNil);
}

static bool _FExternalBrowserItemInCurrentFrame(EXBRSTATE *pst, int32_t iitem)
{
    AssertVarMem(pst);
    PSTDIO pstdio = vpapp->Pstdio();
    if (pstdio == pvNil || pstdio->Pmvie() == pvNil || pstdio->Pmvie()->Pscen() == pvNil)
        return fFalse;
    PMVIE pmvie = pstdio->Pmvie();
    int32_t arid, cactRef;
    STN stn;
    if (!pmvie->FGetArid(iitem, &arid, &stn, &cactRef))
        return fFalse;
    PACTR pactr = pmvie->Pscen()->PactrFromArid(arid);
    return FPure(pactr != pvNil && pactr->FOnStage());
}

static bool _FExternalBrowserItemCurrentlyVisible(EXBRSTATE *pst, int32_t iitem)
{
    AssertVarMem(pst);
    PSTDIO pstdio = vpapp->Pstdio();
    if (pstdio == pvNil || pstdio->Pmvie() == pvNil || pstdio->Pmvie()->Pscen() == pvNil)
        return fFalse;
    PMVIE pmvie = pstdio->Pmvie();
    int32_t arid, cactRef;
    STN stn;
    if (!pmvie->FGetArid(iitem, &arid, &stn, &cactRef))
        return fFalse;
    PACTR pactr = pmvie->Pscen()->PactrFromArid(arid);
    return FPure(pactr != pvNil && pactr->FOnStage() && pactr->FIsInView());
}

static uint32_t _LuExternalBrowserSourceSig(EXBRSTATE *pst)
{
    if (pst == pvNil || pst->exbrk == kexbrMaterial)
        return 0;

    PSTDIO pstdio = vpapp->Pstdio();
    if (pstdio == pvNil || pstdio->Pmvie() == pvNil)
        return 0;

    PMVIE pmvie = pstdio->Pmvie();
    uint32_t lu = 2166136261U;
    const bool fVisibleFilter = pst->fVisibleOnly;
    if ((pst->fFrameOnly || fVisibleFilter) && pmvie->Pscen() != pvNil)
    {
        const int32_t nfrm = pmvie->Pscen()->Nfrm();
        lu = (lu ^ (uint32_t)nfrm) * 16777619U;
        lu = (lu ^ (uint32_t)fVisibleFilter) * 16777619U;
    }
    int32_t arid;
    int32_t cactRef;
    STN stn;
    for (int32_t iarid = 0; pmvie->FGetArid(iarid, &arid, &stn, &cactRef); iarid++)
    {
        lu = (lu ^ (uint32_t)arid) * 16777619U;
        lu = (lu ^ (uint32_t)cactRef) * 16777619U;
        lu = (lu ^ (uint32_t)pmvie->FIsPropBrwsIarid(iarid)) * 16777619U;
        lu = (lu ^ (uint32_t)pmvie->FActorHasLight(pmvie->Iscen(), arid)) * 16777619U;
        lu = (lu ^ (uint32_t)pmvie->FObjectSelectable(arid)) * 16777619U;
    }

    PSCEN pscen = pmvie->Pscen();
    if (pscen != pvNil)
    {
        PGL pglpactr = pscen->PglRollCall();
        if (pglpactr != pvNil)
        {
            for (int32_t iactr = 0; iactr < pglpactr->IvMac(); iactr++)
            {
                PACTR pactr;
                pglpactr->Get(iactr, &pactr);
                if (pactr != pvNil)
                {
                    lu = (lu ^ (uint32_t)pactr->Arid()) * 16777619U;
                    if (pst->fFrameOnly || fVisibleFilter)
                        lu = (lu ^ (uint32_t)pactr->FOnStage()) * 16777619U;
                    if (fVisibleFilter)
                        lu = (lu ^ (uint32_t)pactr->FIsInView()) * 16777619U;
                }
            }
        }
    }
    return lu;
}

static int32_t _CcolExternalBrowser(EXBRSTATE *pst)
{
    return (pst->exbrk == kexbrProp || pst->exbrk == kexbrObject) ? 1 : 4;
}

static int32_t _CrowPageExternalBrowser(EXBRSTATE *pst)
{
    if (pst->exbrk == kexbrActor)
        return 4;
    if (pst->exbrk == kexbrMaterial)
        return 8;
    return 1;
}

static int32_t _YpExternalBrowserListTop(EXBRSTATE *pst)
{
    if (pst == pvNil)
        return kypExbrActorControlsBottom;
    if (pst->exbrk == kexbrProp)
        return kypExbrPropControlsBottom;
    if (pst->exbrk == kexbrObject)
        return kypExbrObjectControlsBottom;
    return kypExbrActorControlsBottom;
}

static void _UpdateExternalBrowserScroll(EXBRSTATE *pst)
{
    if (pst == pvNil || pst->hwnd == hNil || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject ||
        (pst->exbrk == kexbrActor && !pst->fShowThumbnails))
        return;

    const int32_t ccol = _CcolExternalBrowser(pst);
    const int32_t crowPage = _CrowPageExternalBrowser(pst);
    const int32_t crowTotal = (int32_t)((pst->rgithd.size() + ccol - 1) / ccol);
    const int32_t irowMax = LwMax(0, crowTotal - crowPage);
    pst->irowFirst = LwBound(pst->irowFirst, 0, irowMax);

    SCROLLINFO si;
    ClearPb(&si, SIZEOF(si));
    si.cbSize = SIZEOF(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = LwMax(0, crowTotal - 1);
    si.nPage = crowPage;
    si.nPos = pst->irowFirst;
    SetScrollInfo(pst->hwnd, SB_VERT, &si, fTrue);
}

static void _GetExternalBrowserName(EXBRSTATE *pst, int32_t iitem, PSTN pstn)
{
    AssertVarMem(pst);
    AssertPo(pstn, 0);
    pstn->SetNil();

    if (pst->exbrk == kexbrActor || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject)
    {
        PSTDIO pstdio = vpapp->Pstdio();
        if (pstdio == pvNil || pstdio->Pmvie() == pvNil)
            return;

        int32_t arid;
        int32_t cactRef;
        if (!pstdio->Pmvie()->FGetArid(iitem, &arid, pstn, &cactRef))
        {
            pstn->SetNil();
            return;
        }
        STN stnDisplay;
        if (pstdio->Pmvie()->FGetDisplayName(arid, &stnDisplay))
            *pstn = stnDisplay;
    }
}

static void _RebuildExternalBrowserItems(EXBRSTATE *pst)
{
    AssertVarMem(pst);
    pst->rgithd.clear();
    pst->irowFirst = 0;

    if (pst->exbrk == kexbrMaterial)
    {
        if (pst->pbcl == pvNil)
            return;
        const int32_t cthd = pst->pbcl->IthdMac();
        for (int32_t ithd = 0; ithd < cthd; ithd++)
            pst->rgithd.push_back(ithd);
    }
    else
    {
        PSTDIO pstdio = vpapp->Pstdio();
        if (pstdio == pvNil || pstdio->Pmvie() == pvNil)
            return;

        PMVIE pmvie = pstdio->Pmvie();
        STN stn;
        int32_t arid;
        int32_t cactRef;
        for (int32_t iarid = 0; pmvie->FGetArid(iarid, &arid, &stn, &cactRef); iarid++)
        {
            // This is the same roll-call test used by BRWR::_Cthum(), which
            // backs the original left/right side lists. fbrwsProp includes
            // ordinary props and 3D words.
            const bool fProp = pmvie->FIsPropBrwsIarid(iarid);
            const bool fTdt = FPure(fProp && pmvie->FIsIaridTdt(iarid));
            if (pst->exbrk == kexbrActor && fProp)
                continue;
            if (pst->exbrk == kexbrProp && !fProp)
                continue;
            if (pst->exbrk == kexbrProp)
            {
                if (pst->exbrPropMode == kexbrPropModeProps && fTdt)
                    continue;
                if (pst->exbrPropMode == kexbrPropModeWords && !fTdt)
                    continue;
            }
            if (pst->exbrk == kexbrObject)
            {
                if (!fProp && !pst->fFilterActors)
                    continue;
                if (fProp && !fTdt && !pst->fFilterProps)
                    continue;
                if (fTdt && !pst->fFilterWords)
                    continue;
            }
            if (!cactRef)
                continue;
            if (pst->fSceneOnly && !_FExternalBrowserItemUsedInScene(pst, iarid))
                continue;
            if (pst->fFrameOnly && !_FExternalBrowserItemInCurrentFrame(pst, iarid))
                continue;
            if (pst->fVisibleOnly && !_FExternalBrowserItemCurrentlyVisible(pst, iarid))
                continue;
            pst->rgithd.push_back(iarid);
        }
        pst->luSourceSig = _LuExternalBrowserSourceSig(pst);
        _LogExternalBrowser("roll-call rebuild kind=%d items=%d sceneOnly=%d frameOnly=%d visibleOnly=%d liveView=%d",
                            (int)pst->exbrk, (int)pst->rgithd.size(),
                            (int)pst->fSceneOnly, (int)pst->fFrameOnly, (int)pst->fVisibleOnly,
                            (int)pmvie->FLiveCameraDisplacedFromFrame());
    }

    if ((pst->exbrk == kexbrActor || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject) && pst->hwndList != hNil)
    {
        SendMessage(pst->hwndList, WM_SETREDRAW, fFalse, 0);
        SendMessage(pst->hwndList, LB_RESETCONTENT, 0, 0);
        int32_t dxpMax = 0;
        HDC hdc = GetDC(pst->hwndList);
        HFONT hfnt = (HFONT)SendMessage(pst->hwndList, WM_GETFONT, 0, 0);
        HFONT hfntOld = hfnt != hNil ? (HFONT)SelectObject(hdc, hfnt) : hNil;
        for (size_t i = 0; i < pst->rgithd.size(); i++)
        {
            STN stn;
            _GetExternalBrowserName(pst, pst->rgithd[i], &stn);
            LRESULT ilb = SendMessage(pst->hwndList, LB_ADDSTRING, 0, (LPARAM)stn.Psz());
            if (ilb != LB_ERR && ilb != LB_ERRSPACE)
            {
                const int32_t iarid = pst->rgithd[i];
                SendMessage(pst->hwndList, LB_SETITEMDATA, (WPARAM)ilb, (LPARAM)iarid);
                if (iarid == pst->ithdSelected)
                    SendMessage(pst->hwndList, LB_SETCURSEL, (WPARAM)ilb, 0);
                int32_t dypItem = Lw4DMMExternalToolUi200(pst->hwnd, kdypExbrListRow);
                PSTDIO pstdio = vpapp->Pstdio();
                if ((pst->exbrk == kexbrProp || pst->exbrk == kexbrObject) && pstdio != pvNil &&
                    pstdio->Pmvie() != pvNil && !pstdio->Pmvie()->FIsIaridTdt(iarid) &&
                    ((pst->exbrk == kexbrProp && pst->fUsePropIcons) ||
                     (pst->exbrk == kexbrObject && pst->fShowThumbnails)))
                {
                    dypItem = Lw4DMMExternalToolUi200(pst->hwnd, kdypExbrPropThumbRow);
                }
                SendMessage(pst->hwndList, LB_SETITEMHEIGHT, (WPARAM)ilb, (LPARAM)dypItem);
            }

            SIZE siz;
            if (GetTextExtentPoint32(hdc, stn.Psz(), stn.Cch(), &siz))
                dxpMax = LwMax(dxpMax, siz.cx + Lw4DMMExternalToolUi200(pst->hwnd,
                    ((pst->exbrk == kexbrProp && pst->fUsePropIcons) ||
                     (pst->exbrk == kexbrObject && pst->fShowThumbnails)) ? 92 : 12));
        }
        if (hfntOld != hNil)
            SelectObject(hdc, hfntOld);
        ReleaseDC(pst->hwndList, hdc);
        SendMessage(pst->hwndList, LB_SETHORIZONTALEXTENT, dxpMax, 0);
        SendMessage(pst->hwndList, WM_SETREDRAW, fTrue, 0);
        InvalidateRect(pst->hwndList, pvNil, fTrue);
    }

    if (pst->exbrk == kexbrActor && pst->hwndList != hNil)
    {
        ShowWindow(pst->hwndList, pst->fShowThumbnails ? SW_HIDE : SW_SHOW);
        ShowScrollBar(pst->hwnd, SB_VERT, pst->fShowThumbnails);
    }

    _UpdateExternalBrowserScroll(pst);
    if (pst->hwnd != hNil)
        InvalidateRect(pst->hwnd, pvNil, fTrue);
}

static PGOB _PgobExternalBrowserThumbHost(void)
{
    // PgokNew() requires its parent to belong to the same Kidspace world.
    // The v3 prototype attached this host directly to PgobScreen(), which is
    // outside vapp.Pkwa() and caused one kidworld.cpp assertion per preview.
    PGOB pgobWorld = vapp.Pkwa();
    if (pgobWorld == pvNil)
        return pvNil;

    GCB gcb;
    RC rcAbs, rcRel;
    rcAbs.Set(-30000, -30000, -29800, -29800);
    rcRel.Set(krelZero, krelZero, krelZero, krelZero);
    gcb.Set(CMH::HidUnique(), pgobWorld, fgobNil, kginDefault, &rcAbs, &rcRel);
    return NewObj GOB(&gcb);
}

static void _DrawExternalBrowserThumb(EXBRSTATE *pst, PGPT pgpt, int32_t iitem, RC *prcDst)
{
    AssertVarMem(pst);
    AssertPo(pgpt, 0);
    AssertVarMem(prcDst);
    if (pst->pgobThumbHost == pvNil || pst->pcrm == pvNil)
        return;

    CNO cnoGokd = cnoNil;
    int32_t sidLog = 0;
    if (pst->exbrk == kexbrActor || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject)
    {
        PSTDIO pstdio = vpapp->Pstdio();
        if (pstdio == pvNil || pstdio->Pmvie() == pvNil)
            return;

        int32_t arid;
        int32_t cactRef;
        STN stn;
        TAG tag;
        if (!pstdio->Pmvie()->FGetArid(iitem, &arid, &stn, &cactRef, &tag))
            return;
        if ((pst->exbrk == kexbrProp || pst->exbrk == kexbrObject) && pstdio->Pmvie()->FIsIaridTdt(iitem))
            return;
        cnoGokd = pstdio->CnoGokdFromCnoTmpl(tag.cno);
        sidLog = tag.sid;
        if (cnoGokd == cnoNil)
        {
            _LogExternalBrowser("roll-call preview has no GOKD iarid=%d arid=%d tmpl=%d sid=%d",
                                (int)iitem, (int)arid, (int)tag.cno, (int)tag.sid);
            return;
        }
    }
    else
    {
        if (pst->pbcl == pvNil)
            return;
        THD thd;
        pst->pbcl->GetThd(iitem, &thd);
        cnoGokd = thd.cno;
        sidLog = thd.tag.sid;
    }

    PGOK pgok = vapp.Pkwa()->PgokNew(pst->pgobThumbHost, CMH::HidUnique(), cnoGokd, pst->pcrm);
    if (pgok == pvNil)
    {
        _LogExternalBrowser("preview create failed kind=%d item=%d gokd=%d sid=%d",
                            (int)pst->exbrk, (int)iitem, (int)cnoGokd, (int)sidLog);
        return;
    }

    RC rcSrc;
    pgok->GetRc(&rcSrc, cooLocal);
    if (rcSrc.FEmpty())
    {
        _LogExternalBrowser("preview empty rect kind=%d item=%d gokd=%d",
                            (int)pst->exbrk, (int)iitem, (int)cnoGokd);
    }
    else
    {
        // Draw the chunky thumbnail at its authored/native size first.
        // Scaling DrawTree's destination rectangle re-layouts the GOK tree
        // itself, which is why v8 portraits were cropped and geometrically
        // distorted. Scale only the completed raster.
        RC rcNative(0, 0, rcSrc.Dxp(), rcSrc.Dyp());
        PGPT pgptNative = GPT::PgptNewOffscreen(&rcNative, 8);
        if (pgptNative != pvNil)
        {
            GNV gnvNative(pgptNative);
            gnvNative.FillRc(&rcNative, kacrWhite);
            pgok->DrawTree(pgptNative, &rcNative, pvNil, fgobNoVis);

            RC rcDst = *prcDst;
            int32_t dxp = rcDst.Dxp();
            int32_t dyp = LwMulDiv(dxp, rcNative.Dyp(), rcNative.Dxp());
            if (dyp > rcDst.Dyp())
            {
                dyp = rcDst.Dyp();
                dxp = LwMulDiv(dyp, rcNative.Dxp(), rcNative.Dyp());
            }
            const int32_t xp = rcDst.xpLeft + (rcDst.Dxp() - dxp) / 2;
            const int32_t yp = rcDst.ypTop + (rcDst.Dyp() - dyp) / 2;
            rcDst.Set(xp, yp, xp + dxp, yp + dyp);

            GNV gnvDst(pgpt);
            gnvDst.CopyPixels(&gnvNative, &rcNative, &rcDst);
            ReleasePpo(&pgptNative);
        }
    }
    ReleasePpo(&pgok);
}

static void _SyncExternalBrowserSelection(EXBRSTATE *pst)
{
    if (pst == pvNil || (pst->exbrk != kexbrActor && pst->exbrk != kexbrProp && pst->exbrk != kexbrObject))
        return;

    PSTDIO pstdio = vpapp->Pstdio();
    if (pstdio == pvNil || pstdio->Pmvie() == pvNil)
        return;

    PMVIE pmvie = pstdio->Pmvie();

    // SMCC::ActorSelected updates STDIO's selection before it asks the native
    // browser to synchronize.  Reading Pscen()->PactrSelected() here could
    // therefore observe the previous actor during viewport/roll-call clicks.
    // Use the Studio selection mirror that the callback has just updated.
    const int32_t aridSelected = pstdio->AridSelected();
    int32_t iaridSelected = ivNil;

    // Entering Free Cam explicitly cancels the movie's camera-follow state.
    // Mirror that transition in every open actor/prop browser so the checkbox
    // cannot remain visually checked and re-tether the camera on the next
    // browser interaction.
    if (pst->fMoveCam && pmvie->FFreeLookMode())
    {
        pst->fMoveCam = fFalse;
        if (pst->hwndMoveCam != hNil)
            SendMessage(pst->hwndMoveCam, BM_SETCHECK, BST_UNCHECKED, 0);
        if (vhwndBrowserCameraFollowOwner == pst->hwnd)
            vhwndBrowserCameraFollowOwner = hNil;
        _SaveExternalBrowserPrefs(pst);
        _LogExternalBrowser("camera follow unchecked for Free Cam kind=%d", (int)pst->exbrk);
    }

    // The external actor/prop browsers store roll-call indices (iarid), while
    // the movie's authoritative selection is an actor id (arid). Translate the
    // current 3DMM selection back into the browser's index space. This lets a
    // viewport click and the built-in hired lists drive the external windows
    // without creating a second selection source of truth.
    if (aridSelected != aridNil)
    {
        int32_t arid;
        int32_t cactRef;
        STN stn;
        for (int32_t iarid = 0; pmvie->FGetArid(iarid, &arid, &stn, &cactRef); iarid++)
        {
            if (arid != aridSelected)
                continue;

            const bool fProp = pmvie->FIsPropBrwsIarid(iarid);
            if ((pst->exbrk == kexbrActor && fProp) || (pst->exbrk == kexbrProp && !fProp))
                break;

            // Respect the browser's current Prop / 3D Words / Both and
            // scene-only filters. If the selected object is filtered out,
            // the external browser correctly shows no selected row.
            for (size_t i = 0; i < pst->rgithd.size(); i++)
            {
                if (pst->rgithd[i] == iarid)
                {
                    iaridSelected = iarid;
                    break;
                }
            }
            break;
        }
    }

    const bool fLogicalSelectionChanged = pst->ithdSelected != iaridSelected;
    pst->ithdSelected = iaridSelected;

    if (pst->hwndList != hNil && !(pst->exbrk == kexbrActor && pst->fShowThumbnails))
    {
        LRESULT ilbSelected = -1;
        const LRESULT clb = SendMessage(pst->hwndList, LB_GETCOUNT, 0, 0);
        for (LRESULT ilb = 0; ilb < clb; ilb++)
        {
            if ((int32_t)SendMessage(pst->hwndList, LB_GETITEMDATA, (WPARAM)ilb, 0) == iaridSelected)
            {
                ilbSelected = ilb;
                break;
            }
        }
        SendMessage(pst->hwndList, LB_SETCURSEL, (WPARAM)ilbSelected, 0);
        if (ilbSelected >= 0)
            SendMessage(pst->hwndList, LB_SETTOPINDEX, (WPARAM)ilbSelected, 0);
    }
    else if (pst->exbrk == kexbrActor && pst->fShowThumbnails && iaridSelected != ivNil)
    {
        int32_t iitemSelected = ivNil;
        for (size_t i = 0; i < pst->rgithd.size(); i++)
        {
            if (pst->rgithd[i] == iaridSelected)
            {
                iitemSelected = (int32_t)i;
                break;
            }
        }
        if (iitemSelected != ivNil)
        {
            const int32_t ccol = _CcolExternalBrowser(pst);
            const int32_t crowPage = _CrowPageExternalBrowser(pst);
            const int32_t irowSelected = iitemSelected / ccol;
            if (irowSelected < pst->irowFirst)
                pst->irowFirst = irowSelected;
            else if (irowSelected >= pst->irowFirst + crowPage)
                pst->irowFirst = irowSelected - crowPage + 1;
            _UpdateExternalBrowserScroll(pst);
        }
    }

    if (pst->hwnd != hNil && (fLogicalSelectionChanged || iaridSelected != ivNil))
        InvalidateRect(pst->hwnd, pvNil, fFalse);

    if (fLogicalSelectionChanged)
    {
        MVIE::LightEditorLog(pmvie,
                             "external_browser_sync kind=%ld arid=%ld iarid=%ld items=%ld",
                             (long)pst->exbrk, (long)aridSelected, (long)iaridSelected,
                             (long)pst->rgithd.size());
    }
}

void SyncExternalContentBrowserSelection(void)
{
    const HWND rghwnd[] = {vhwndActorBrowser, vhwndPropBrowser, vhwndObjectBrowser};
    for (int32_t i = 0; i < (int32_t)(SIZEOF(rghwnd) / SIZEOF(rghwnd[0])); i++)
    {
        HWND hwnd = rghwnd[i];
        if (hwnd == hNil || !IsWindow(hwnd))
            continue;
        EXBRSTATE *pst = (EXBRSTATE *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (pst == pvNil)
            continue;

        // A copy/new actor can change roll-call contents in the same command
        // that changes selection. Refresh the browser's item map first instead
        // of waiting for its 250 ms timer.
        if (pst->luSourceSig != _LuExternalBrowserSourceSig(pst))
            _RebuildExternalBrowserItems(pst);
        _SyncExternalBrowserSelection(pst);
    }
}

enum EXBRSELOP
{
    kexbrselReplace,
    kexbrselAdd,
    kexbrselRemove
};

static void _ApplyExternalBrowserSelection(EXBRSTATE *pst, int32_t iitem,
                                            EXBRSELOP exbrsel = kexbrselReplace)
{
    AssertVarMem(pst);

    if (pst->exbrk == kexbrMaterial)
    {
        if (pst->pbcl == pvNil)
            return;
        THD thd;
        pst->pbcl->GetThd(iitem, &thd);
        PGOB pgob = vapp.Pkwa()->PgobFromHid(kidSpltGlass);
        if (pgob == pvNil || !pgob->FIs(kclsESLT))
            return;
        pst->ithdSelected = iitem;
        vpcex->EnqueueCid(cidEaselSetColor, (PESLT)pgob, pvNil, thd.tag.cno, thd.tag.sid);
        InvalidateRect(pst->hwnd, pvNil, fFalse);
        return;
    }

    PSTDIO pstdio = vpapp->Pstdio();
    if (pstdio == pvNil || pstdio->Pmvie() == pvNil)
        return;

    PMVIE pmvie = pstdio->Pmvie();
    int32_t arid;
    int32_t cactRef;
    STN stn;
    if (!pmvie->FGetArid(iitem, &arid, &stn, &cactRef))
        return;

    PMVU pmvu = (PMVU)pmvie->PddgActive();
    if (pmvu == pvNil)
        return;

    if (!pmvie->FObjectSelectable(arid))
    {
        _LogExternalBrowser("selection ignored non-selectable kind=%d arid=%d",
                            (int)pst->exbrk, (int)arid);
        return;
    }

    PSCEN pscen = pmvie->Pscen();
    PACTR pactrBefore = pscen != pvNil ? pscen->PactrFromArid(arid) : pvNil;
    const bool fExistingOnStage = FPure(pactrBefore != pvNil && pactrBefore->FOnStage());

    if (exbrsel == kexbrselRemove)
    {
        if (pactrBefore != pvNil && pscen != pvNil && pscen->FActrSelected(arid))
        {
            pscen->SelectActrRemove(pactrBefore);
            if (pmvie->Pbwld() != pvNil)
                pmvie->Pbwld()->MarkDirty();
            pmvie->MarkViews();
            _LogExternalBrowser("shift-right remove kind=%d arid=%d remaining=%d",
                                (int)pst->exbrk, (int)arid, (int)pscen->CactrSelected());
        }
        return;
    }

    if (exbrsel == kexbrselAdd && fExistingOnStage && pscen != pvNil)
    {
        pscen->SelectActrAdd(pactrBefore);
        if (pmvie->Pbwld() != pvNil)
            pmvie->Pbwld()->MarkDirty();
        pmvie->MarkViews();
        pst->ithdSelected = iitem;
        _LogExternalBrowser("shift-left add kind=%d arid=%d selected=%d",
                            (int)pst->exbrk, (int)arid, (int)pscen->CactrSelected());
        InvalidateRect(pst->hwnd, pvNil, fFalse);
        return;
    }

    // With camera follow enabled, an object that already exists onstage must
    // never fall through FChooseArid() merely because it is outside the last
    // rendered camera bounds. FIsInView() is a camera-visibility test, not an
    // onstage test. Select the existing scene object directly, then move the
    // camera to it. Offstage/not-yet-in-scene roll-call items retain vanilla
    // 3DMM bring-back/placement behavior.
    bool fChosen = fFalse;
    if (pst->fMoveCam && fExistingOnStage)
    {
        pmvie->Pscen()->SelectActr(pactrBefore);
        pmvie->InvalViews();
        fChosen = fTrue;
    }
    else
    {
        fChosen = pmvie->FChooseArid(arid);
    }

    if (fChosen)
    {
        if (!pmvu->FActrMode())
        {
            pmvu->SetTool(toolCompose);
            pstdio->ChangeTool(toolCompose);
        }
        pst->ithdSelected = iitem;
        if (pst->fMoveCam)
        {
            pmvie->SetBrowserCameraFollow(arid, fExistingOnStage);
            vhwndBrowserCameraFollowOwner = fExistingOnStage ? pst->hwnd : hNil;
            if (fExistingOnStage)
            {
                pmvie->FUpdateBrowserCameraFollow();
                pmvie->MarkViews();
            }
            _LogExternalBrowser("camera follow selection kind=%d arid=%d onstage=%d",
                                (int)pst->exbrk, (int)arid, (int)fExistingOnStage);
        }
        InvalidateRect(pst->hwnd, pvNil, fFalse);
    }
}

static void _PaintExternalBrowser(EXBRSTATE *pst)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(pst->hwnd, &ps);
    RECT rcsClient;
    GetClientRect(pst->hwnd, &rcsClient);
    FillRect(hdc, &rcsClient, (HBRUSH)(COLOR_BTNFACE + 1));

    if (pst->exbrk == kexbrProp || pst->exbrk == kexbrObject ||
        (pst->exbrk == kexbrActor && !pst->fShowThumbnails))
    {
        EndPaint(pst->hwnd, &ps);
        return;
    }

    PGPT pgpt = GPT::PgptNew(hdc);
    const int32_t ccol = 4;
    const int32_t crow = _CrowPageExternalBrowser(pst);
    const int32_t ypGrid = Lw4DMMExternalToolUi200(
        pst->hwnd, pst->exbrk == kexbrActor ? kypExbrActorControlsBottom : kypExbrMaterialGridTop);
    const int32_t dxpCell = Lw4DMMExternalToolUi200(
        pst->hwnd, pst->exbrk == kexbrActor ? 100 : kdxpExbrMaterialFrame);
    const int32_t dypCell = Lw4DMMExternalToolUi200(
        pst->hwnd, pst->exbrk == kexbrActor ? 92 : kdypExbrMaterialFrame);
    const int32_t xpGrid = Lw4DMMExternalToolUi200(pst->hwnd, 6);

    HWND hwndFontSource = pst->exbrk == kexbrMaterial ? pst->hwndCustomColor : pst->hwndSceneOnly;
    HFONT hfnt = hwndFontSource != hNil ? (HFONT)SendMessage(hwndFontSource, WM_GETFONT, 0, 0) : hNil;
    if (hfnt == hNil)
        hfnt = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT hfntOld = (HFONT)SelectObject(hdc, hfnt);
    SetBkMode(hdc, TRANSPARENT);

    for (int32_t irow = 0; irow < crow; irow++)
    {
        for (int32_t icol = 0; icol < ccol; icol++)
        {
            const int32_t iitem = (pst->irowFirst + irow) * ccol + icol;
            if (iitem < 0 || iitem >= (int32_t)pst->rgithd.size())
                continue;

            const int32_t ithd = pst->rgithd[iitem];
            const int32_t xp = xpGrid + icol * dxpCell;
            const int32_t yp = ypGrid + irow * dypCell;
            const int32_t d2 = Lw4DMMExternalToolUi200(pst->hwnd, 2);
            RECT rcsCell = {xp, yp, xp + dxpCell - d2, yp + dypCell - d2};
            FillRect(hdc, &rcsCell, (HBRUSH)(COLOR_WINDOW + 1));
            FrameRect(hdc, &rcsCell, (HBRUSH)(COLOR_3DSHADOW + 1));

            RC rcThumb;
            if (pst->exbrk == kexbrActor)
                rcThumb.Set(xp + Lw4DMMExternalToolUi200(pst->hwnd, 12),
                            yp + Lw4DMMExternalToolUi200(pst->hwnd, 4),
                            xp + dxpCell - Lw4DMMExternalToolUi200(pst->hwnd, 14),
                            yp + Lw4DMMExternalToolUi200(pst->hwnd, 67));
            else
                rcThumb.Set(xp + Lw4DMMExternalToolUi200(pst->hwnd, kdxpExbrMaterialBorder),
                            yp + Lw4DMMExternalToolUi200(pst->hwnd, kdypExbrMaterialBorder),
                            xp + dxpCell - Lw4DMMExternalToolUi200(pst->hwnd, kdxpExbrMaterialBorder),
                            yp + dypCell - Lw4DMMExternalToolUi200(pst->hwnd, kdypExbrMaterialBorder));

            if (pgpt != pvNil)
                _DrawExternalBrowserThumb(pst, pgpt, ithd, &rcThumb);

            // Thumbnail GOK drawing may select its own font into this HDC.
            // Reassert the browser font before every text draw so a repaint or
            // resize cannot make later rows inherit a random content font.
            SelectObject(hdc, hfnt);

            if (pst->exbrk == kexbrActor)
            {
                STN stn;
                _GetExternalBrowserName(pst, ithd, &stn);
                RECT rcsText = {xp + Lw4DMMExternalToolUi200(pst->hwnd, 3),
                                yp + Lw4DMMExternalToolUi200(pst->hwnd, 69),
                                xp + dxpCell - Lw4DMMExternalToolUi200(pst->hwnd, 5),
                                yp + dypCell - Lw4DMMExternalToolUi200(pst->hwnd, 4)};
                DrawText(hdc, stn.Psz(), stn.Cch(), &rcsText,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }

            if (ithd == pst->ithdSelected)
            {
                HBRUSH hbr = CreateSolidBrush(RGB(0, 90, 210));
                FrameRect(hdc, &rcsCell, hbr);
                const int32_t d1 = Lw4DMMExternalToolUi200(pst->hwnd, 1);
                InflateRect(&rcsCell, -d1, -d1);
                FrameRect(hdc, &rcsCell, hbr);
                DeleteObject(hbr);
            }
        }
    }

    SelectObject(hdc, hfntOld);
    if (pgpt != pvNil)
    {
        GPT::Flush();
        ReleasePpo(&pgpt);
    }
    EndPaint(pst->hwnd, &ps);
}

static void _ScrollExternalBrowser(EXBRSTATE *pst, int32_t irowNew)
{
    if (pst == pvNil || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject ||
        (pst->exbrk == kexbrActor && !pst->fShowThumbnails))
        return;
    const int32_t ccol = _CcolExternalBrowser(pst);
    const int32_t crowPage = _CrowPageExternalBrowser(pst);
    const int32_t crowTotal = (int32_t)((pst->rgithd.size() + ccol - 1) / ccol);
    const int32_t irowMax = LwMax(0, crowTotal - crowPage);
    irowNew = LwBound(irowNew, 0, irowMax);
    if (irowNew == pst->irowFirst)
        return;
    pst->irowFirst = irowNew;
    _UpdateExternalBrowserScroll(pst);
    InvalidateRect(pst->hwnd, pvNil, fTrue);
}

static LRESULT CALLBACK _ExternalBrowserWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    EXBRSTATE *pst = (EXBRSTATE *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    // Keep 4DMM organizational/editor shortcuts global even while an APO
    // browser owns keyboard focus.  Forward them to the main frame rather than
    // duplicating Object Groups / Actor Studio open logic in this window.
    const bool fCtrlG = wm == WM_KEYDOWN && wParam == ChLit('G') &&
        GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_MENU) >= 0 &&
        GetKeyState(VK_SHIFT) >= 0;
    const bool fCtrlA = wm == WM_KEYDOWN && wParam == ChLit('A') &&
        GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_MENU) >= 0 &&
        GetKeyState(VK_SHIFT) >= 0;
    const bool fAltA = wm == WM_SYSKEYDOWN && wParam == ChLit('A') &&
        GetKeyState(VK_MENU) < 0 && GetKeyState(VK_CONTROL) >= 0 &&
        GetKeyState(VK_SHIFT) >= 0;
    if (fCtrlG || fCtrlA || fAltA)
    {
        HWND hwndOwner = _Hwnd4DMMExternalBrowserOwner();
        if (hwndOwner != hNil && IsWindow(hwndOwner))
        {
            SendMessageA(hwndOwner, wm, wParam, lParam);
            return 0;
        }
    }

    switch (wm)
    {
    case WM_NCCREATE: {
        CREATESTRUCT *pcs = (CREATESTRUCT *)lParam;
        pst = (EXBRSTATE *)pcs->lpCreateParams;
        if (pst == pvNil)
            return fFalse;
        pst->hwnd = hwnd;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pst);
        break;
    }

    case WM_CREATE:
        if (pst == pvNil)
            return -1;
        if (pst->exbrk == kexbrActor || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject)
        {
            // Keep Create Part in the browser's normal vertical control flow.
            // The previous side-column placement forced the whole APOB to stay
            // artificially wide or overlap controls while resizing.
            pst->hwndCreatePart = CreateWindowEx(0, PszLit("BUTTON"), PszLit("Create Part"),
                                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                                 8, 5, 394, 22, hwnd, (HMENU)kexbrIdCreatePart,
                                                 GetModuleHandle(pvNil), pvNil);
            pst->hwndSceneOnly = CreateWindowEx(0, PszLit("BUTTON"), PszLit("Currently in this scene only"),
                                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                                8, 32, 205, 22, hwnd, (HMENU)kexbrIdSceneOnly,
                                                GetModuleHandle(pvNil), pvNil);
            pst->hwndFrameOnly = CreateWindowEx(0, PszLit("BUTTON"), PszLit("Currently in this frame only"),
                                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                                8, 54, 205, 22, hwnd, (HMENU)kexbrIdFrameOnly,
                                                GetModuleHandle(pvNil), pvNil);
            pst->hwndVisibleOnly = CreateWindowEx(0, PszLit("BUTTON"), PszLit("Currently visible only"),
                                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                                   8, 76, 205, 22, hwnd, (HMENU)kexbrIdVisibleOnly,
                                                   GetModuleHandle(pvNil), pvNil);
            pst->hwndMoveCam = CreateWindowEx(0, PszLit("BUTTON"), PszLit("Move cam with selected object"),
                                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                              8, 98, 220, 22, hwnd, (HMENU)kexbrIdMoveCam,
                                              GetModuleHandle(pvNil), pvNil);

            if (pst->hwndSceneOnly != hNil)
                SendMessage(pst->hwndSceneOnly, BM_SETCHECK, pst->fSceneOnly ? BST_CHECKED : BST_UNCHECKED, 0);
            if (pst->hwndFrameOnly != hNil)
                SendMessage(pst->hwndFrameOnly, BM_SETCHECK, pst->fFrameOnly ? BST_CHECKED : BST_UNCHECKED, 0);
            if (pst->hwndVisibleOnly != hNil)
            {
                SendMessage(pst->hwndVisibleOnly, BM_SETCHECK, pst->fVisibleOnly ? BST_CHECKED : BST_UNCHECKED, 0);
                EnableWindow(pst->hwndVisibleOnly, fTrue);
            }
            if (pst->hwndMoveCam != hNil)
                SendMessage(pst->hwndMoveCam, BM_SETCHECK, pst->fMoveCam ? BST_CHECKED : BST_UNCHECKED, 0);

            if (pst->exbrk == kexbrActor)
            {
                pst->hwndShowThumbnails = CreateWindowEx(0, PszLit("BUTTON"), PszLit("Show thumbnails"),
                                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                                         8, 120, 180, 22, hwnd, (HMENU)kexbrIdShowThumbnails,
                                                         GetModuleHandle(pvNil), pvNil);
                if (pst->hwndShowThumbnails != hNil)
                {
                    SendMessage(pst->hwndShowThumbnails, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
                    SendMessage(pst->hwndShowThumbnails, BM_SETCHECK, pst->fShowThumbnails ? BST_CHECKED : BST_UNCHECKED, 0);
                }
            }
            else if (pst->exbrk == kexbrProp)
            {
                pst->hwndPropModeProps = CreateWindowEx(0, PszLit("BUTTON"), PszLit("Props"),
                                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON,
                                                        8, 122, 110, 20, hwnd, (HMENU)kexbrIdPropModeProps,
                                                        GetModuleHandle(pvNil), pvNil);
                pst->hwndPropModeWords = CreateWindowEx(0, PszLit("BUTTON"), PszLit("3D Words"),
                                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                                                        8, 143, 110, 20, hwnd, (HMENU)kexbrIdPropModeWords,
                                                        GetModuleHandle(pvNil), pvNil);
                pst->hwndPropModeBoth = CreateWindowEx(0, PszLit("BUTTON"), PszLit("Props && 3D Words"),
                                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                                                       8, 164, 150, 20, hwnd, (HMENU)kexbrIdPropModeBoth,
                                                       GetModuleHandle(pvNil), pvNil);
                pst->hwndUsePropIcons = CreateWindowEx(0, PszLit("BUTTON"), PszLit("Use Prop Icons"),
                                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTOCHECKBOX,
                                                       8, 187, 160, 22, hwnd, (HMENU)kexbrIdUsePropIcons,
                                                       GetModuleHandle(pvNil), pvNil);
                if (pst->hwndPropModeProps != hNil)
                    SendMessage(pst->hwndPropModeProps, BM_SETCHECK, pst->exbrPropMode == kexbrPropModeProps ? BST_CHECKED : BST_UNCHECKED, 0);
                if (pst->hwndPropModeWords != hNil)
                    SendMessage(pst->hwndPropModeWords, BM_SETCHECK, pst->exbrPropMode == kexbrPropModeWords ? BST_CHECKED : BST_UNCHECKED, 0);
                if (pst->hwndPropModeBoth != hNil)
                    SendMessage(pst->hwndPropModeBoth, BM_SETCHECK, pst->exbrPropMode == kexbrPropModeBoth ? BST_CHECKED : BST_UNCHECKED, 0);
                if (pst->hwndUsePropIcons != hNil)
                {
                    SendMessage(pst->hwndUsePropIcons, BM_SETCHECK, pst->fUsePropIcons ? BST_CHECKED : BST_UNCHECKED, 0);
                    EnableWindow(pst->hwndUsePropIcons, pst->exbrPropMode != kexbrPropModeWords);
                }
            }
            else
            {
                pst->hwndFilterActors = CreateWindowEx(0, PszLit("BUTTON"), PszLit("Actors"),
                                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                                       8, 122, 110, 20, hwnd, (HMENU)kexbrIdFilterActors,
                                                       GetModuleHandle(pvNil), pvNil);
                pst->hwndFilterProps = CreateWindowEx(0, PszLit("BUTTON"), PszLit("Props"),
                                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                                      8, 143, 110, 20, hwnd, (HMENU)kexbrIdFilterProps,
                                                      GetModuleHandle(pvNil), pvNil);
                pst->hwndFilterWords = CreateWindowEx(0, PszLit("BUTTON"), PszLit("3D Words"),
                                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                                      8, 164, 110, 20, hwnd, (HMENU)kexbrIdFilterWords,
                                                      GetModuleHandle(pvNil), pvNil);
                pst->hwndUsePropIcons = CreateWindowEx(0, PszLit("BUTTON"), PszLit("Show thumbnails"),
                                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                                       8, 187, 160, 22, hwnd, (HMENU)kexbrIdUsePropIcons,
                                                       GetModuleHandle(pvNil), pvNil);
                if (pst->hwndFilterActors != hNil)
                    SendMessage(pst->hwndFilterActors, BM_SETCHECK, pst->fFilterActors ? BST_CHECKED : BST_UNCHECKED, 0);
                if (pst->hwndFilterProps != hNil)
                    SendMessage(pst->hwndFilterProps, BM_SETCHECK, pst->fFilterProps ? BST_CHECKED : BST_UNCHECKED, 0);
                if (pst->hwndFilterWords != hNil)
                    SendMessage(pst->hwndFilterWords, BM_SETCHECK, pst->fFilterWords ? BST_CHECKED : BST_UNCHECKED, 0);
                if (pst->hwndUsePropIcons != hNil)
                    SendMessage(pst->hwndUsePropIcons, BM_SETCHECK, pst->fShowThumbnails ? BST_CHECKED : BST_UNCHECKED, 0);
            }

            const HWND rghwndControls[] = {pst->hwndSceneOnly, pst->hwndFrameOnly, pst->hwndVisibleOnly, pst->hwndMoveCam, pst->hwndCreatePart, pst->hwndPropModeProps,
                                           pst->hwndPropModeWords, pst->hwndPropModeBoth, pst->hwndUsePropIcons,
                                           pst->hwndFilterActors, pst->hwndFilterProps, pst->hwndFilterWords};
            for (int32_t ihwnd = 0; ihwnd < SIZEOF(rghwndControls) / SIZEOF(rghwndControls[0]); ihwnd++)
            {
                if (rghwndControls[ihwnd] != hNil)
                    SendMessage(rghwndControls[ihwnd], WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
            }
            if (pst->hwndSceneOnly != hNil)
                SendMessage(pst->hwndSceneOnly, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
            if (pst->hwndMoveCam != hNil)
                SendMessage(pst->hwndMoveCam, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);

            const int32_t ypList = _YpExternalBrowserListTop(pst);
            pst->hwndList = CreateWindowEx(WS_EX_CLIENTEDGE, PszLit("LISTBOX"), PszLit(""),
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
                                               LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS,
                                           7, ypList, 395, 350, hwnd, (HMENU)kexbrIdBrowserList,
                                           GetModuleHandle(pvNil), pvNil);
            if (pst->hwndList != hNil)
            {
                SendMessage(pst->hwndList, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
                if (pst->exbrk == kexbrActor && pst->fShowThumbnails)
                    ShowWindow(pst->hwndList, SW_HIDE);
            }
        }
        else if (pst->exbrk == kexbrMaterial)
        {
            pst->hwndCustomColor = CreateWindowEx(
                0, PszLit("BUTTON"), PszLit("Custom color (ALT+C)"),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                6, 6, 4 * kdxpExbrMaterialFrame - 2 + 25, 28, hwnd,
                (HMENU)kexbrIdCustomColor, GetModuleHandle(pvNil), pvNil);
            if (pst->hwndCustomColor != hNil)
                SendMessage(pst->hwndCustomColor, WM_SETFONT,
                            (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
        }
        SetTimer(hwnd, kexbrTimerContext, 250, pvNil);
        _RebuildExternalBrowserItems(pst);
        _SyncExternalBrowserSelection(pst);
        return 0;

    case WM_SIZE:
        if (pst != pvNil && (pst->exbrk == kexbrActor || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject) && pst->hwndList != hNil)
        {
            const int32_t dxp = LOWORD(lParam);
            const int32_t dyp = HIWORD(lParam);
            const int32_t xpInset = Lw4DMMExternalToolUi200(hwnd, 7);
            const int32_t ypList = Lw4DMMExternalToolUi200(hwnd, _YpExternalBrowserListTop(pst));
            if (pst->hwndCreatePart != hNil)
            {
                const int32_t xpButton = Lw4DMMExternalToolUi200(hwnd, 8);
                const int32_t ypButton = Lw4DMMExternalToolUi200(hwnd, 5);
                const int32_t dypButton = Lw4DMMExternalToolUi200(hwnd, 22);
                MoveWindow(pst->hwndCreatePart, xpButton, ypButton,
                           LwMax(Lw4DMMExternalToolUi200(hwnd, 80), dxp - 2 * xpButton),
                           dypButton, fTrue);
            }
            MoveWindow(pst->hwndList, xpInset, ypList,
                       LwMax(Lw4DMMExternalToolUi200(hwnd, 20), dxp - 2 * xpInset),
                       LwMax(Lw4DMMExternalToolUi200(hwnd, 20), dyp - ypList - xpInset), fTrue);
        }
        return 0;

    case WM_TIMER:
        if (wParam == kexbrTimerContext && pst != pvNil)
        {
            if (!_FExternalBrowserContextAlive(pst->exbrk))
            {
                DestroyWindow(hwnd);
                return 0;
            }
            if ((pst->exbrk == kexbrActor || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject) &&
                pst->hwndVisibleOnly != hNil)
            {
                EnableWindow(pst->hwndVisibleOnly, fTrue);
            }
            if ((pst->exbrk == kexbrActor || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject) &&
                pst->hwndCreatePart != hNil)
            {
                EnableWindow(pst->hwndCreatePart, pst->ithdSelected != ivNil);
            }
            if ((pst->exbrk == kexbrActor || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject) &&
                pst->luSourceSig != _LuExternalBrowserSourceSig(pst))
            {
                _RebuildExternalBrowserItems(pst);
            }
            _SyncExternalBrowserSelection(pst);
        }
        break;

    case WM_MEASUREITEM:
        if (pst != pvNil && (pst->exbrk == kexbrActor || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject))
        {
            MEASUREITEMSTRUCT *pmis = (MEASUREITEMSTRUCT *)lParam;
            if (pmis != pvNil && pmis->CtlID == kexbrIdBrowserList)
            {
                pmis->itemHeight = Lw4DMMExternalToolUi200(hwnd, kdypExbrListRow);
                return fTrue;
            }
        }
        break;

    case WM_DRAWITEM:
        if (pst != pvNil && (pst->exbrk == kexbrActor || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject))
        {
            DRAWITEMSTRUCT *pdis = (DRAWITEMSTRUCT *)lParam;
            if (pdis != pvNil && pdis->CtlID == kexbrIdBrowserList && pdis->itemID != (UINT)-1)
            {
                const int32_t iarid = (int32_t)pdis->itemData;
                const bool fSelected = FPure(pdis->itemState & ODS_SELECTED);
                HBRUSH hbrBack = (HBRUSH)(fSelected ? COLOR_HIGHLIGHT + 1 : COLOR_WINDOW + 1);
                FillRect(pdis->hDC, &pdis->rcItem, hbrBack);
                SetBkMode(pdis->hDC, TRANSPARENT);
                SetTextColor(pdis->hDC, GetSysColor(fSelected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));

                HFONT hfnt = (HFONT)SendMessage(pst->hwndList, WM_GETFONT, 0, 0);
                if (hfnt == hNil)
                    hfnt = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                HFONT hfntOld = (HFONT)SelectObject(pdis->hDC, hfnt);

                const int32_t d4 = Lw4DMMExternalToolUi200(hwnd, 4);
                const int32_t d6 = Lw4DMMExternalToolUi200(hwnd, 6);
                int32_t xpText = pdis->rcItem.left + d6;
                if ((pst->exbrk == kexbrProp && pst->fUsePropIcons) ||
                    (pst->exbrk == kexbrObject && pst->fShowThumbnails))
                {
                    PSTDIO pstdio = vpapp->Pstdio();
                    const bool fTdt = FPure(pstdio != pvNil && pstdio->Pmvie() != pvNil &&
                                             pstdio->Pmvie()->FIsIaridTdt(iarid));
                    if (!fTdt)
                    {
                        PGPT pgpt = GPT::PgptNew(pdis->hDC);
                        if (pgpt != pvNil)
                        {
                            RC rcThumb;
                            rcThumb.Set(pdis->rcItem.left + d6, pdis->rcItem.top + d4,
                                        pdis->rcItem.left + Lw4DMMExternalToolUi200(hwnd, 76),
                                        pdis->rcItem.bottom - d4);
                            _DrawExternalBrowserThumb(pst, pgpt, iarid, &rcThumb);
                            GPT::Flush();
                            ReleasePpo(&pgpt);
                        }
                        // Kidspace thumbnail rendering may leave a content font
                        // selected in the list HDC. Always restore our browser
                        // font before drawing this or any later row label.
                        SelectObject(pdis->hDC, hfnt);
                        xpText = pdis->rcItem.left + Lw4DMMExternalToolUi200(hwnd, 84);
                    }
                }

                STN stn;
                _GetExternalBrowserName(pst, iarid, &stn);
                RECT rcsText = pdis->rcItem;
                rcsText.left = xpText;
                rcsText.right -= d6;
                DrawText(pdis->hDC, stn.Psz(), stn.Cch(), &rcsText,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                if (pdis->itemState & ODS_FOCUS)
                    DrawFocusRect(pdis->hDC, &pdis->rcItem);
                if (hfntOld != hNil)
                    SelectObject(pdis->hDC, hfntOld);
                return fTrue;
            }
        }
        break;

    case kwm4DMMExternalBrowserFreeCam:
        if (pst != pvNil && (pst->exbrk == kexbrActor || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject))
        {
            PSTDIO pstdio = vpapp->Pstdio();
            PMVIE pmvie = pstdio != pvNil ? pstdio->Pmvie() : pvNil;
            if (pmvie != pvNil && pmvie->Pscen() != pvNil && !pmvie->FPlaying())
            {
                if (vhwndBrowserCameraFollowOwner == hwnd)
                {
                    vhwndBrowserCameraFollowOwner = hNil;
                    pmvie->SetBrowserCameraFollow(aridNil, fFalse);
                }
                HWND hwndEditor = _Hwnd4DMMExternalBrowserOwner();
                if (hwndEditor != hNil && IsWindow(hwndEditor))
                {
                    ShowWindow(hwndEditor, SW_SHOWNORMAL);
                    BringWindowToTop(hwndEditor);
                    SetForegroundWindow(hwndEditor);
                    SetFocus(hwndEditor);
                }
                if (pmvie->FStartFreeLook(fFalse))
                {
                    pmvie->Pmcc()->UpdateScrollbars();
                    return 1;
                }
            }
        }
        return 0;

    case kwm4DMMExternalBrowserEscape:
        if (pst != pvNil && (pst->exbrk == kexbrActor || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject) &&
            pst->ithdSelected != ivNil)
        {
            PSTDIO pstdio = vpapp->Pstdio();
            PMVIE pmvie = pstdio != pvNil ? pstdio->Pmvie() : pvNil;
            if (pmvie != pvNil && pmvie->Pscen() != pvNil)
            {
                pmvie->Pscen()->SelectActr(pvNil);
                if (vhwndBrowserCameraFollowOwner == hwnd)
                {
                    vhwndBrowserCameraFollowOwner = hNil;
                    pmvie->SetBrowserCameraFollow(aridNil, fFalse);
                }
                pmvie->InvalViewsAndScb();
            }
            pst->ithdSelected = ivNil;
            if (pst->hwndList != hNil)
                SendMessage(pst->hwndList, LB_SETCURSEL, (WPARAM)-1, 0);
            InvalidateRect(hwnd, pvNil, fFalse);
            return 1;
        }
        return 0;

    case WM_COMMAND:
        if (pst != pvNil && LOWORD(wParam) == kexbrIdSceneOnly && HIWORD(wParam) == BN_CLICKED)
        {
            pst->fSceneOnly =
                FPure(SendMessage(pst->hwndSceneOnly, BM_GETCHECK, 0, 0) == BST_CHECKED);
            _SaveExternalBrowserPrefs(pst);
            _RebuildExternalBrowserItems(pst);
            return 0;
        }
        if (pst != pvNil && LOWORD(wParam) == kexbrIdFrameOnly && HIWORD(wParam) == BN_CLICKED)
        {
            pst->fFrameOnly =
                FPure(SendMessage(pst->hwndFrameOnly, BM_GETCHECK, 0, 0) == BST_CHECKED);
            _SaveExternalBrowserPrefs(pst);
            _RebuildExternalBrowserItems(pst);
            return 0;
        }
        if (pst != pvNil && LOWORD(wParam) == kexbrIdVisibleOnly && HIWORD(wParam) == BN_CLICKED)
        {
            pst->fVisibleOnly =
                FPure(SendMessage(pst->hwndVisibleOnly, BM_GETCHECK, 0, 0) == BST_CHECKED);
            _SaveExternalBrowserPrefs(pst);
            _RebuildExternalBrowserItems(pst);
            return 0;
        }
        if (pst != pvNil && (pst->exbrk == kexbrActor || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject) &&
            LOWORD(wParam) == kexbrIdCreatePart && HIWORD(wParam) == BN_CLICKED)
        {
            PSTDIO pstdio = vpapp->Pstdio();
            PMVIE pmvie = pstdio != pvNil ? pstdio->Pmvie() : pvNil;
            int32_t arid = aridNil;
            int32_t cactRef = 0;
            STN stn;
            if (pmvie == pvNil || pst->ithdSelected == ivNil ||
                !pmvie->FGetArid(pst->ithdSelected, &arid, &stn, &cactRef) ||
                arid == aridNil || !FOpen4DMMCreatePartForActor(arid))
            {
                MessageBoxA(hwnd, "Select an actor, prop, or 3D Word that exists in the current movie first.",
                            "Create Part", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }
        if (pst != pvNil && (pst->exbrk == kexbrActor || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject) &&
            LOWORD(wParam) == kexbrIdMoveCam && HIWORD(wParam) == BN_CLICKED)
        {
            pst->fMoveCam = FPure(SendMessage(pst->hwndMoveCam, BM_GETCHECK, 0, 0) == BST_CHECKED);
            PSTDIO pstdio = vpapp->Pstdio();
            PMVIE pmvie = pstdio != pvNil ? pstdio->Pmvie() : pvNil;
            if (pmvie != pvNil)
            {
                if (pst->fMoveCam && pmvie->Pscen() != pvNil && pmvie->Pscen()->PactrSelected() != pvNil &&
                    pmvie->Pscen()->PactrSelected()->FOnStage())
                {
                    const int32_t arid = pmvie->Pscen()->PactrSelected()->Arid();
                    pmvie->SetBrowserCameraFollow(arid, fTrue);
                    vhwndBrowserCameraFollowOwner = hwnd;
                    pmvie->FUpdateBrowserCameraFollow();
                    pmvie->MarkViews();
                }
                else if (!pst->fMoveCam && vhwndBrowserCameraFollowOwner == hwnd)
                {
                    vhwndBrowserCameraFollowOwner = hNil;
                    pmvie->SetBrowserCameraFollow(aridNil, fFalse);
                }
            }
            _SaveExternalBrowserPrefs(pst);
            _LogExternalBrowser("camera follow checkbox kind=%d enabled=%d", (int)pst->exbrk, (int)pst->fMoveCam);
            return 0;
        }
        if (pst != pvNil && pst->exbrk == kexbrProp && HIWORD(wParam) == BN_CLICKED &&
            (LOWORD(wParam) == kexbrIdPropModeProps || LOWORD(wParam) == kexbrIdPropModeWords ||
             LOWORD(wParam) == kexbrIdPropModeBoth))
        {
            if (LOWORD(wParam) == kexbrIdPropModeProps)
                pst->exbrPropMode = kexbrPropModeProps;
            else if (LOWORD(wParam) == kexbrIdPropModeBoth)
                pst->exbrPropMode = kexbrPropModeBoth;
            else
                pst->exbrPropMode = kexbrPropModeWords;
            if (pst->hwndUsePropIcons != hNil)
                EnableWindow(pst->hwndUsePropIcons, pst->exbrPropMode != kexbrPropModeWords);
            _SaveExternalBrowserPrefs(pst);
            _LogExternalBrowser("prop mode=%d use-icons=%d", (int)pst->exbrPropMode, (int)pst->fUsePropIcons);
            _RebuildExternalBrowserItems(pst);
            return 0;
        }
        if (pst != pvNil && (pst->exbrk == kexbrProp || pst->exbrk == kexbrObject) &&
            LOWORD(wParam) == kexbrIdUsePropIcons && HIWORD(wParam) == BN_CLICKED)
        {
            const bool fChecked = FPure(SendMessage(pst->hwndUsePropIcons, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (pst->exbrk == kexbrObject)
                pst->fShowThumbnails = fChecked;
            else
                pst->fUsePropIcons = fChecked;
            _SaveExternalBrowserPrefs(pst);
            _LogExternalBrowser("list thumbnails kind=%d enabled=%d", (int)pst->exbrk, (int)fChecked);
            _RebuildExternalBrowserItems(pst);
            return 0;
        }
        if (pst != pvNil && pst->exbrk == kexbrActor &&
            LOWORD(wParam) == kexbrIdShowThumbnails && HIWORD(wParam) == BN_CLICKED)
        {
            pst->fShowThumbnails =
                FPure(SendMessage(pst->hwndShowThumbnails, BM_GETCHECK, 0, 0) == BST_CHECKED);
            _SaveExternalBrowserPrefs(pst);
            _LogExternalBrowser("show thumbnails kind=%d enabled=%d", (int)pst->exbrk, (int)pst->fShowThumbnails);
            _RebuildExternalBrowserItems(pst);
            return 0;
        }
        if (pst != pvNil && pst->exbrk == kexbrObject && HIWORD(wParam) == BN_CLICKED &&
            (LOWORD(wParam) == kexbrIdFilterActors || LOWORD(wParam) == kexbrIdFilterProps ||
             LOWORD(wParam) == kexbrIdFilterWords))
        {
            pst->fFilterActors = FPure(SendMessage(pst->hwndFilterActors, BM_GETCHECK, 0, 0) == BST_CHECKED);
            pst->fFilterProps = FPure(SendMessage(pst->hwndFilterProps, BM_GETCHECK, 0, 0) == BST_CHECKED);
            pst->fFilterWords = FPure(SendMessage(pst->hwndFilterWords, BM_GETCHECK, 0, 0) == BST_CHECKED);
            _SaveExternalBrowserPrefs(pst);
            _LogExternalBrowser("object filters actors=%d props=%d words=%d",
                                (int)pst->fFilterActors, (int)pst->fFilterProps, (int)pst->fFilterWords);
            _RebuildExternalBrowserItems(pst);
            _SyncExternalBrowserSelection(pst);
            return 0;
        }
        if (pst != pvNil && pst->exbrk == kexbrMaterial &&
            LOWORD(wParam) == kexbrIdCustomColor && HIWORD(wParam) == BN_CLICKED)
        {
            PGOB pgob = vapp.Pkwa()->PgobFromHid(kidSpltGlass);
            if (pgob != pvNil && pgob->FIs(kclsESLT))
            {
                vpcex->EnqueueCid(cidEaselPickColor, (PESLT)pgob, pvNil);
                _LogExternalBrowser("material custom-color button");
            }
            return 0;
        }
        if (pst != pvNil && (pst->exbrk == kexbrActor || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject) &&
            LOWORD(wParam) == kexbrIdBrowserList && HIWORD(wParam) == LBN_DBLCLK)
        {
            LRESULT ilb = SendMessage(pst->hwndList, LB_GETCURSEL, 0, 0);
            if (ilb != LB_ERR)
            {
                LRESULT iarid = SendMessage(pst->hwndList, LB_GETITEMDATA, (WPARAM)ilb, 0);
                PSTDIO pstdio = vpapp->Pstdio();
                if (iarid != LB_ERR && pstdio != pvNil && pstdio->Pmvie() != pvNil)
                {
                    PMVIE pmvie = pstdio->Pmvie();
                    int32_t arid, cactRef;
                    STN stn;
                    if (pmvie->FGetArid((int32_t)iarid, &arid, &stn, &cactRef))
                    {
                        pmvie->FToggleObjectSelectable(arid, fTrue);
                        _LogExternalBrowser("double-click selectability kind=%d arid=%d selectable=%d",
                                            (int)pst->exbrk, (int)arid,
                                            (int)pmvie->FObjectSelectable(arid));
                    }
                }
            }
            return 0;
        }
        if (pst != pvNil && (pst->exbrk == kexbrActor || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject) && LOWORD(wParam) == kexbrIdBrowserList &&
            HIWORD(wParam) == LBN_SELCHANGE)
        {
            LRESULT ilb = SendMessage(pst->hwndList, LB_GETCURSEL, 0, 0);
            if (ilb != LB_ERR)
            {
                LRESULT ithd = SendMessage(pst->hwndList, LB_GETITEMDATA, (WPARAM)ilb, 0);
                if (ithd != LB_ERR)
                    _ApplyExternalBrowserSelection(pst, (int32_t)ithd,
                        GetKeyState(VK_SHIFT) < 0 ? kexbrselAdd : kexbrselReplace);
            }
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if (pst != pvNil && pst->exbrk != kexbrProp && pst->exbrk != kexbrObject &&
            !(pst->exbrk == kexbrActor && !pst->fShowThumbnails))
        {
            const int32_t xp = (int16_t)LOWORD(lParam);
            const int32_t yp = (int16_t)HIWORD(lParam);
            const int32_t xpGrid = Lw4DMMExternalToolUi200(hwnd, 6);
            const int32_t ypGrid = Lw4DMMExternalToolUi200(
                hwnd, pst->exbrk == kexbrActor ? kypExbrActorControlsBottom : kypExbrMaterialGridTop);
            const int32_t dxpCell = Lw4DMMExternalToolUi200(
                hwnd, pst->exbrk == kexbrActor ? 100 : kdxpExbrMaterialFrame);
            const int32_t dypCell = Lw4DMMExternalToolUi200(
                hwnd, pst->exbrk == kexbrActor ? 92 : kdypExbrMaterialFrame);
            if (xp >= xpGrid && yp >= ypGrid)
            {
                const int32_t icol = (xp - xpGrid) / dxpCell;
                const int32_t irow = (yp - ypGrid) / dypCell;
                if (icol >= 0 && icol < 4 && irow >= 0 && irow < _CrowPageExternalBrowser(pst))
                {
                    const int32_t iitem = (pst->irowFirst + irow) * 4 + icol;
                    if (iitem >= 0 && iitem < (int32_t)pst->rgithd.size())
                    {
                        _ApplyExternalBrowserSelection(pst, pst->rgithd[iitem],
                            GetKeyState(VK_SHIFT) < 0 ? kexbrselAdd : kexbrselReplace);
                        return 0;
                    }
                }
            }
        }
        break;

    case WM_LBUTTONDBLCLK:
        if (pst != pvNil && pst->exbrk == kexbrActor && pst->fShowThumbnails)
        {
            const int32_t xp = (int16_t)LOWORD(lParam);
            const int32_t yp = (int16_t)HIWORD(lParam);
            const int32_t xpGrid = Lw4DMMExternalToolUi200(hwnd, 6);
            const int32_t ypGrid = Lw4DMMExternalToolUi200(hwnd, kypExbrActorControlsBottom);
            const int32_t dxpCell = Lw4DMMExternalToolUi200(hwnd, 100);
            const int32_t dypCell = Lw4DMMExternalToolUi200(hwnd, 92);
            if (xp >= xpGrid && yp >= ypGrid)
            {
                const int32_t icol = (xp - xpGrid) / dxpCell;
                const int32_t irow = (yp - ypGrid) / dypCell;
                const int32_t iitem = (pst->irowFirst + irow) * 4 + icol;
                if (icol >= 0 && icol < 4 && irow >= 0 && irow < _CrowPageExternalBrowser(pst) &&
                    iitem >= 0 && iitem < (int32_t)pst->rgithd.size())
                {
                    PSTDIO pstdio = vpapp->Pstdio();
                    PMVIE pmvie = pstdio != pvNil ? pstdio->Pmvie() : pvNil;
                    int32_t arid, cactRef;
                    STN stn;
                    if (pmvie != pvNil && pmvie->FGetArid(pst->rgithd[iitem], &arid, &stn, &cactRef))
                    {
                        pmvie->FToggleObjectSelectable(arid, fTrue);
                        _LogExternalBrowser("thumbnail double-click selectability arid=%d selectable=%d",
                                            (int)arid, (int)pmvie->FObjectSelectable(arid));
                    }
                    return 0;
                }
            }
        }
        break;

    case WM_RBUTTONUP:
        if (pst != pvNil && pst->exbrk == kexbrActor && pst->fShowThumbnails && GetKeyState(VK_SHIFT) < 0)
        {
            const int32_t xp = (int16_t)LOWORD(lParam);
            const int32_t yp = (int16_t)HIWORD(lParam);
            const int32_t xpGrid = Lw4DMMExternalToolUi200(hwnd, 6);
            const int32_t ypGrid = Lw4DMMExternalToolUi200(hwnd, kypExbrActorControlsBottom);
            const int32_t dxpCell = Lw4DMMExternalToolUi200(hwnd, 100);
            const int32_t dypCell = Lw4DMMExternalToolUi200(hwnd, 92);
            if (xp >= xpGrid && yp >= ypGrid)
            {
                const int32_t icol = (xp - xpGrid) / dxpCell;
                const int32_t irow = (yp - ypGrid) / dypCell;
                const int32_t iitem = (pst->irowFirst + irow) * 4 + icol;
                if (icol >= 0 && icol < 4 && irow >= 0 && irow < _CrowPageExternalBrowser(pst) &&
                    iitem >= 0 && iitem < (int32_t)pst->rgithd.size())
                {
                    _ApplyExternalBrowserSelection(pst, pst->rgithd[iitem], kexbrselRemove);
                    return 0;
                }
            }
        }
        break;

    case WM_CONTEXTMENU:
        if (pst != pvNil && pst->hwndList != hNil && (HWND)wParam == pst->hwndList &&
            GetKeyState(VK_SHIFT) < 0 &&
            (pst->exbrk == kexbrActor || pst->exbrk == kexbrProp || pst->exbrk == kexbrObject))
        {
            POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
            if (pt.x == -1 && pt.y == -1)
                GetCursorPos(&pt);
            ScreenToClient(pst->hwndList, &pt);
            LRESULT hit = SendMessage(pst->hwndList, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
            if (HIWORD(hit) == 0)
            {
                const int32_t ilb = LOWORD(hit);
                LRESULT iarid = SendMessage(pst->hwndList, LB_GETITEMDATA, (WPARAM)ilb, 0);
                if (iarid != LB_ERR)
                {
                    _ApplyExternalBrowserSelection(pst, (int32_t)iarid, kexbrselRemove);
                    return 0;
                }
            }
        }
        break;

    case WM_MOUSEWHEEL:
        if (pst != pvNil && pst->exbrk != kexbrProp && pst->exbrk != kexbrObject &&
            !(pst->exbrk == kexbrActor && !pst->fShowThumbnails))
        {
            const int32_t dz = GET_WHEEL_DELTA_WPARAM(wParam);
            if (dz != 0)
            {
                const int32_t cstep = LwMax(1, (dz < 0 ? -dz : dz) / WHEEL_DELTA);
                _ScrollExternalBrowser(pst, pst->irowFirst + (dz > 0 ? -cstep : cstep));
            }
            return 0;
        }
        break;

    case WM_VSCROLL:
        if (pst != pvNil && pst->exbrk != kexbrProp && pst->exbrk != kexbrObject &&
            !(pst->exbrk == kexbrActor && !pst->fShowThumbnails))
        {
            int32_t irow = pst->irowFirst;
            switch (LOWORD(wParam))
            {
            case SB_LINEUP:
                irow--;
                break;
            case SB_LINEDOWN:
                irow++;
                break;
            case SB_PAGEUP:
                irow -= _CrowPageExternalBrowser(pst);
                break;
            case SB_PAGEDOWN:
                irow += _CrowPageExternalBrowser(pst);
                break;
            case SB_THUMBPOSITION:
            case SB_THUMBTRACK: {
                SCROLLINFO si;
                ClearPb(&si, SIZEOF(si));
                si.cbSize = SIZEOF(si);
                si.fMask = SIF_TRACKPOS;
                GetScrollInfo(hwnd, SB_VERT, &si);
                irow = si.nTrackPos;
                break;
            }
            default:
                return 0;
            }
            _ScrollExternalBrowser(pst, irow);
            return 0;
        }
        break;

    case WM_PAINT:
        if (pst != pvNil)
        {
            _PaintExternalBrowser(pst);
            return 0;
        }
        break;

    case WM_DESTROY:
        if (pst != pvNil)
        {
            KillTimer(hwnd, kexbrTimerContext);
            _SaveExternalBrowserPrefs(pst);
            if (vhwndBrowserCameraFollowOwner == hwnd)
            {
                vhwndBrowserCameraFollowOwner = hNil;
                PSTDIO pstdio = vpapp->Pstdio();
                if (pstdio != pvNil && pstdio->Pmvie() != pvNil)
                    pstdio->Pmvie()->SetBrowserCameraFollow(aridNil, fFalse);
            }
            HWND *phwnd = _PhwndForExternalBrowser(pst->exbrk);
            if (phwnd != pvNil && *phwnd == hwnd)
                *phwnd = hNil;
            if (pst->pbcls != pvNil)
            {
                pst->pbcl = pvNil;
                ReleasePpo(&pst->pbcls);
            }
            else
                ReleasePpo(&pst->pbcl);
            ReleasePpo(&pst->pbclAux);
            ReleasePpo(&pst->pcrm);
            ReleasePpo(&pst->pgobThumbHost);
            delete pst;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
    }

    return DefWindowProc(hwnd, wm, wParam, lParam);
}

static bool _FEnsureExternalBrowserClass(void)
{
    static bool fRegistered = fFalse;
    if (fRegistered)
        return fTrue;

    WNDCLASSEX wc;
    ClearPb(&wc, SIZEOF(wc));
    wc.cbSize = SIZEOF(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = _ExternalBrowserWndProc;
    wc.hInstance = GetModuleHandle(pvNil);
    wc.hCursor = LoadCursor(hNil, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kszExternalBrowserClass;
    if (!RegisterClassEx(&wc))
    {
        const DWORD dwErr = GetLastError();
        if (dwErr != ERROR_CLASS_ALREADY_EXISTS)
        {
            _LogExternalBrowser("RegisterClassEx failed error=%lu", (unsigned long)dwErr);
            return fFalse;
        }
    }
    fRegistered = fTrue;
    _LogExternalBrowser("window class ready");
    return fTrue;
}

static bool _FInitExternalBrowserList(EXBRSTATE *pst)
{
    AssertVarMem(pst);

    // Actor and prop windows are views of the movie roll call.  Both retain a
    // CRM so their existing template GOKDs can be drawn on demand.  Props do
    // not need a global catalogue: 3-D words intentionally have no prop
    // thumbnail and remain text-only rows.
    CKI cki;
    cki.cno = cnoNil;
    pst->pcrm = CRM::PcrmNew(1);
    if (pst->pcrm == pvNil)
    {
        _LogExternalBrowser("list init kind=%d failed: CRM allocation", (int)pst->exbrk);
        return fFalse;
    }

    switch (pst->exbrk)
    {
    case kexbrActor:
        cki.ctg = kctgTmth;
        pst->pbcl = BCL::PbclNew(pst->pcrm, &cki, ctgNil, pvNil, fTrue);
        break;
    case kexbrProp:
        // The item set still comes from the movie roll call; this tolerant
        // prop thumbnail catalogue exists only to populate the CRM used by
        // PgokNew when Show thumbnails is enabled.
        cki.ctg = kctgPrth;
        pst->pbcl = BCL::PbclNew(pst->pcrm, &cki, ctgNil, pvNil, fTrue);
        break;
    case kexbrMaterial:
        cki.ctg = kctgMtth;
        pst->pbcl = BCL::PbclNew(pst->pcrm, &cki, ctgNil, pvNil, fTrue);
        break;
    case kexbrObject: {
        // The combined browser draws thumbnails from both original content
        // catalogues. Keep both BCLs alive so the shared CRM keeps the actor
        // and prop CRFs/resources available for PgokNew throughout the
        // browser window lifetime.
        cki.ctg = kctgTmth;
        pst->pbcl = BCL::PbclNew(pst->pcrm, &cki, ctgNil, pvNil, fTrue);
        if (pst->pbcl != pvNil)
        {
            CKI ckiProp;
            ckiProp.ctg = kctgPrth;
            ckiProp.cno = cnoNil;
            pst->pbclAux = BCL::PbclNew(pst->pcrm, &ckiProp, ctgNil, pvNil, fTrue);
        }
        break;
    }
    default:
        return fFalse;
    }
    if (pst->pbcl == pvNil || (pst->exbrk == kexbrObject && pst->pbclAux == pvNil))
    {
        _LogExternalBrowser("list init kind=%d failed: browser list allocation/build primary=%p aux=%p",
                            (int)pst->exbrk, pst->pbcl, pst->pbclAux);
        ReleasePpo(&pst->pbclAux);
        ReleasePpo(&pst->pbcl);
        ReleasePpo(&pst->pcrm);
        return fFalse;
    }
    _LogExternalBrowser("list init kind=%d succeeded resource-items=%d", (int)pst->exbrk, (int)pst->pbcl->IthdMac());
    return fTrue;
}

bool FExternalContentBrowsersCombined(void)
{
    _EnsureExternalBrowserGlobalPrefsLoaded();
    return vfCombineActorsPropsBrowsers;
}

void SetExternalContentBrowsersCombined(bool fCombine)
{
    _EnsureExternalBrowserGlobalPrefsLoaded();
    fCombine = FPure(fCombine);
    if (vfCombineActorsPropsBrowsers == fCombine)
    {
        _WriteExternalBrowserCombinedPref();
        return;
    }
    vfCombineActorsPropsBrowsers = fCombine;
    _WriteExternalBrowserCombinedPref();
    if (fCombine)
    {
        CloseExternalContentBrowser(kexbrActor);
        CloseExternalContentBrowser(kexbrProp);
    }
    else
    {
        CloseExternalContentBrowser(kexbrObject);
    }
}

void CancelExternalBrowserCameraFollowForFreeCam(void)
{
    const HWND rghwnd[] = {vhwndActorBrowser, vhwndPropBrowser, vhwndObjectBrowser};
    for (int32_t i = 0; i < (int32_t)(SIZEOF(rghwnd) / SIZEOF(rghwnd[0])); i++)
    {
        HWND hwnd = rghwnd[i];
        if (hwnd == hNil || !IsWindow(hwnd))
            continue;
        EXBRSTATE *pst = (EXBRSTATE *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (pst == pvNil || !pst->fMoveCam)
            continue;
        pst->fMoveCam = fFalse;
        if (pst->hwndMoveCam != hNil)
            SendMessage(pst->hwndMoveCam, BM_SETCHECK, BST_UNCHECKED, 0);
        _SaveExternalBrowserPrefs(pst);
        _LogExternalBrowser("camera follow checkbox immediately unchecked for Free Cam kind=%d", (int)pst->exbrk);
    }
    vhwndBrowserCameraFollowOwner = hNil;
}

void CloseExternalContentBrowser(int32_t exbrk)
{
    HWND *phwnd = _PhwndForExternalBrowser((EXBRK)exbrk);
    if (phwnd == pvNil || *phwnd == hNil || !IsWindow(*phwnd))
        return;
    DestroyWindow(*phwnd);
}

void MarkExternalContentBrowserMem(void)
{
    // EXBRSTATE lives outside the Kauai object graph. Explicitly mark the
    // Kauai objects retained by every live native browser so debug builds do
    // not report them as lost when the mouse enters the external window.
    const HWND rghwnd[] = {vhwndActorBrowser, vhwndPropBrowser, vhwndMaterialBrowser, vhwndObjectBrowser};
    for (int32_t ihwnd = 0; ihwnd < SIZEOF(rghwnd) / SIZEOF(rghwnd[0]); ihwnd++)
    {
        HWND hwnd = rghwnd[ihwnd];
        if (hwnd == hNil || !IsWindow(hwnd))
            continue;

        EXBRSTATE *pst = (EXBRSTATE *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (pst == pvNil)
            continue;

        MarkMemObj(pst->pcrm);
        MarkMemObj(pst->pbcl);
        MarkMemObj(pst->pbclAux);
        MarkMemObj(pst->pbcls);
        MarkMemObj(pst->pgobThumbHost);
    }
}

bool FShowExternalContentBrowser(int32_t exbrk)
{
    _EnsureExternalBrowserGlobalPrefsLoaded();
    _LogExternalBrowser("show requested kind=%d", (int)exbrk);

    EXBRK kind = (EXBRK)exbrk;
    if (vfCombineActorsPropsBrowsers && (kind == kexbrActor || kind == kexbrProp))
        kind = kexbrObject;
    if (!_FExternalBrowserContextAlive(kind))
        return fFalse;
    if (!_FEnsureExternalBrowserClass())
        return fFalse;

    HWND *phwnd = _PhwndForExternalBrowser(kind);
    if (phwnd == pvNil)
        return fFalse;
    if (*phwnd != hNil && IsWindow(*phwnd))
    {
        _LogExternalBrowser("reusing existing window kind=%d hwnd=%p", (int)kind, *phwnd);
        ShowWindow(*phwnd, SW_SHOWNORMAL);
        SetForegroundWindow(*phwnd);
        return fTrue;
    }

    EXBRSTATE *pst = new EXBRSTATE;
    if (pst == pvNil)
        return fFalse;
    pst->exbrk = kind;
    _LoadExternalBrowserPrefs(pst);
    if (!_FInitExternalBrowserList(pst))
    {
        _LogExternalBrowser("show kind=%d failed during list initialization", (int)kind);
        delete pst;
        return fFalse;
    }
    if (kind == kexbrActor || kind == kexbrProp || kind == kexbrMaterial || kind == kexbrObject)
    {
        pst->pgobThumbHost = _PgobExternalBrowserThumbHost();
        if (pst->pgobThumbHost == pvNil)
        {
            _LogExternalBrowser("show kind=%d failed: thumbnail host allocation", (int)kind);
            if (pst->pbcls != pvNil)
            {
                pst->pbcl = pvNil;
                ReleasePpo(&pst->pbcls);
            }
            else
                ReleasePpo(&pst->pbcl);
            ReleasePpo(&pst->pbclAux);
            ReleasePpo(&pst->pcrm);
            delete pst;
            return fFalse;
        }
    }

    const achar *pszTitle;
    int32_t dxpClient, dypClient;
    DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
    const DWORD dwExStyle = WS_EX_TOOLWINDOW;
    if (kind == kexbrActor)
    {
        pszTitle = PszLit("4DMM Actor Browser");
        dxpClient = 412;
        dypClient = 450;
        dwStyle |= WS_VSCROLL;
    }
    else if (kind == kexbrProp)
    {
        pszTitle = PszLit("4DMM Prop Browser");
        dxpClient = 410;
        dypClient = 450;
    }
    else if (kind == kexbrObject)
    {
        pszTitle = PszLit("4DMM Object Browser");
        dxpClient = 410;
        dypClient = 450;
    }
    else
    {
        pszTitle = PszLit("4DMM 3D Word Color / Texture Browser");
        // 4 x 8 cells at the exact 78 x 44 preview-frame pitch used by the easel.
        dxpClient = 6 + 4 * kdxpExbrMaterialFrame + 6 + 25;
        dypClient = kypExbrMaterialGridTop + 8 * kdypExbrMaterialFrame + 6;
        dwStyle |= WS_VSCROLL;
    }

    RECT rcsWnd = {0, 0, dxpClient, dypClient};
    AdjustWindowRectEx(&rcsWnd, dwStyle, fFalse, dwExStyle);
    const int32_t dxp = rcsWnd.right - rcsWnd.left;
    const int32_t dyp = rcsWnd.bottom - rcsWnd.top;

    HWND hwndOwner = _Hwnd4DMMExternalBrowserOwner();
    RECT rcsOwner;
    if (hwndOwner == hNil || !GetWindowRect(hwndOwner, &rcsOwner))
    {
        rcsOwner.left = 0;
        rcsOwner.top = 0;
        rcsOwner.right = GetSystemMetrics(SM_CXSCREEN);
        rcsOwner.bottom = GetSystemMetrics(SM_CYSCREEN);
        hwndOwner = hNil;
    }

    // Prefer placing the browser beside 3DMM, but always clamp it onto the
    // current monitor. In fullscreen 640x480 there is literally no desktop
    // space to the right of the app, and the original prototype therefore
    // could have created a perfectly valid browser entirely off-screen.
    RECT rcsWork;
    rcsWork.left = 0;
    rcsWork.top = 0;
    rcsWork.right = GetSystemMetrics(SM_CXSCREEN);
    rcsWork.bottom = GetSystemMetrics(SM_CYSCREEN);
    HMONITOR hmon = MonitorFromWindow(hwndOwner != hNil ? hwndOwner : (HWND)vwig.hwndApp, MONITOR_DEFAULTTONEAREST);
    if (hmon != hNil)
    {
        MONITORINFO mi;
        ClearPb(&mi, SIZEOF(mi));
        mi.cbSize = SIZEOF(mi);
        if (GetMonitorInfo(hmon, &mi))
            rcsWork = mi.rcWork;
    }

    int32_t xp = rcsOwner.right + 8;
    int32_t yp = rcsOwner.top + 40;
    if (xp + dxp > rcsWork.right)
        xp = rcsOwner.left - dxp - 8;
    if (xp < rcsWork.left)
        xp = rcsWork.right - dxp;
    if (yp + dyp > rcsWork.bottom)
        yp = rcsWork.bottom - dyp;
    xp = LwMax((int32_t)rcsWork.left, xp);
    yp = LwMax((int32_t)rcsWork.top, yp);

    _LogExternalBrowser("CreateWindowEx kind=%d pos=(%d,%d) size=%dx%d work=(%ld,%ld)-(%ld,%ld)",
                        (int)kind, xp, yp, dxp, dyp, (long)rcsWork.left, (long)rcsWork.top,
                        (long)rcsWork.right, (long)rcsWork.bottom);
    HWND hwnd = CreateWindowEx(dwExStyle, kszExternalBrowserClass, pszTitle, dwStyle,
                               xp, yp, dxp, dyp, hwndOwner, hNil, GetModuleHandle(pvNil), pst);
    if (hwnd == hNil)
    {
        _LogExternalBrowser("CreateWindowEx kind=%d failed error=%lu", (int)kind, (unsigned long)GetLastError());
        if (pst->pbcls != pvNil)
        {
            pst->pbcl = pvNil;
            ReleasePpo(&pst->pbcls);
        }
        else
            ReleasePpo(&pst->pbcl);
        ReleasePpo(&pst->pbclAux);
        ReleasePpo(&pst->pcrm);
        ReleasePpo(&pst->pgobThumbHost);
        delete pst;
        return fFalse;
    }

    Scale4DMMExternalToolWindow200(hwnd);
    // WM_CREATE populated list metrics before the 200%-tool font was applied.
    // Rebuild once with the final font/geometry so horizontal extents and
    // owner-draw row heights match the scaled tool window from first paint.
    _RebuildExternalBrowserItems(pst);
    _SyncExternalBrowserSelection(pst);
    *phwnd = hwnd;
    _LogExternalBrowser("window created kind=%d hwnd=%p items=%d", (int)kind, hwnd, (int)pst->rgithd.size());
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    return fTrue;
}
#endif // KAUAI_WIN32

/** 3DMMv1.0: *************************************************************************
 *
 * Handle Browser Ready command
 * A Browser Ready command signals the invocation of an empty browser
 *
 * Parameters:
 *	pcmd - Pointer to the command to process.
 *	pcmd[0] = kid of Browser (type)
 *  pcmd[1] = kid first frame.  Thumb kid is this + kidBrowserThumbOffset
 *  pcmd[2] = kid of first control
 *  pcmd[3] = x,y offsets
 *
 * Returns:
 * 	fTrue if it handled the command, else fFalse.
 *
 **************************************************************************/
const int32_t kglpbrcnGrow = 5;

bool STDIO::FCmdBrowserReady(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    bool fSuccess = fFalse;
    PBRCN pbrcn = pvNil; // 3DMMv1.0: Browser context carryover
    PBRWD pbrwd = pvNil;
    CKI ckiRoot;
    TAG tag;
    PTAG ptag;
    PMVU pmvu;
    int32_t thumSelect;
    int32_t sid = ((APP *)vpappb)->SidProduct();
    int32_t brwdid = pcmd->rglw[0];

    vapp.BeginLongOp();

    if (pvNil == _pmvie)
        goto LFail;

    AssertPo(_pmvie, 0);

    // 3DMMv1.0: Optionally Save/Retrieve Browser Context
    if (_pglpbrcn == pvNil)
    {
        if (pvNil == (_pglpbrcn = GL::PglNew(SIZEOF(PBRCN), kglpbrcnGrow)))
            goto LFail;
    }

    // 3DMMv1.0: Include optional argument pbrcn if you want context carryover
    pbrcn = _PbrcnFromBrwdid(brwdid);
    AssertNilOrPo(pbrcn, 0);

    switch (brwdid)
    {
    case kidBrwsBackground:
        // 3DMMv1.0: Search for background thumbs of any cno
        ckiRoot.cno = cnoNil;
        ckiRoot.ctg = kctgBkth;

        if (pvNil == _pmvie->Pscen())
        {
            thumSelect = (int32_t)cnoNil;
            TrashVar(&sid);
        }
        else
        {
            Assert(pvNil != _pmvie->Pscen()->Pbkgd(), "Pbkgd() Nil");
            thumSelect = _pmvie->Pscen()->Pbkgd()->Cno();
            AssertDo(_pmvie->Pscen()->FGetTagBkgd(&tag), "Missing background event");
            sid = tag.sid;
        }

        pbrwd = (PBRWD)(BRWB::PbrwbNew(_pcrm));
        if (pvNil == pbrwd)
            goto LFail;

        // 3DMMv1.0: Create BRCNL for context carryover (optional choice)
        if (pbrcn == pvNil)
            pbrcn = NewObj BRCNL;

        // 3DMMv1.0: Selection is cno based
        if (!((PBRWB)pbrwd)->FInit(pcmd, kbwsCnoRoot, thumSelect, sid, ckiRoot, ctgNil, this, (PBRCNL)pbrcn))
        {
            goto LFail;
        }
        break;

    case kidBrwsCamera:
        if (pvNil == _pmvie->Pscen())
            goto LFail;
        AssertPo(_pmvie->Pscen(), 0);
        Assert(pvNil != _pmvie->Pscen()->Pbkgd(), "Pbkgd() Nil");

        // 3DMMv1.0: Search for camera views, children of the current bkgd
        ckiRoot.ctg = kctgBkth;
        ckiRoot.cno = _pmvie->Pscen()->Pbkgd()->Cno();

        thumSelect = _pmvie->Pscen()->Pbkgd()->Icam();
        pbrwd = (PBRWD)(BRWC::PbrwcNew(_pcrm));
        if (pvNil == pbrwd)
            goto LFail;

        // 3DMMv1.0: Create BRCNL for context carryover (optional choice)
        if (pbrcn == pvNil)
            pbrcn = NewObj BRCNL;
        AssertDo(_pmvie->Pscen()->FGetTagBkgd(&tag), "Missing background event");
        if (!((PBRWC)pbrwd)->FInit(pcmd, kbwsChid, thumSelect, tag.sid, ckiRoot, kctgCath, this, (PBRCNL)pbrcn))
        {
            goto LFail;
        }
        break;

    case kidBrwsProp:
        ckiRoot.ctg = kctgPrth;
        pbrwd = (PBRWD)(BRWP::PbrwpNew(_pcrm, kidPropGlass));
        goto LActor;
    case kidBrwsActor:
        ckiRoot.ctg = kctgTmth;
        pbrwd = (PBRWD)(BRWP::PbrwpNew(_pcrm, kidActorGlass));
    LActor:
        ckiRoot.cno = cnoNil;
        Assert(pvNil != _pmvie->Pscen(), "Actor browser requires scene");
        thumSelect = ivNil;
        if (pvNil == pbrwd)
            goto LFail;

        // 3DMMv1.0: Create BRCNL for context carryover (optional choice)
        if (pbrcn == pvNil)
            pbrcn = NewObj BRCNL;
        if (!((PBRWP)pbrwd)->FInit(pcmd, kbwsCnoRoot, thumSelect, 0, ckiRoot, ctgNil, this, (PBRCNL)pbrcn))
        {
            goto LFail;
        }
        break;

    case kidBrwsAction:
        if ((pvNil == _pmvie->Pscen()) || (pvNil == _pmvie->Pscen()->PactrSelected()))
        {
            Bug("No actor selected in action browser");
            goto LFail;
        }
        thumSelect = _pmvie->Pscen()->PactrSelected()->AnidCur();
        pbrwd = (PBRWD)BRWA::PbrwaNew(_pcrm);

        if (pvNil == pbrwd)
            goto LFail;

        // 3DMMv1.0: Build the string table before initializing
        if (!((PBRWA)pbrwd)->FBuildGst(_pmvie->Pscen()))
            goto LFail;
        if (!((PBRWT)pbrwd)->FInit(pcmd, thumSelect, thumSelect, this))
            goto LFail;
        break;

    case kidSSorterBackground:
        if (SCRT::PscrtNew(brwdid, _pmvie, this, _pcrm) == pvNil)
            PushErc(ercSocCantInitSceneSort);
        vapp.EndLongOp();
        return fTrue;

    case kidBrwsFX:
        ckiRoot.ctg = kctgSfth;
        pbrwd = (PBRWD)(BRWM::PbrwmNew(_pcrm, kidFXGlass, stySfx, this));
        goto LMusic;
    case kidBrwsSpeech:
        ckiRoot.ctg = kctgSvth;
        pbrwd = (PBRWD)(BRWM::PbrwmNew(_pcrm, kidSpeechGlass, stySpeech, this));
        goto LMusic;
    case kidBrwsMidi:
        ckiRoot.ctg = kctgSmth;
        pbrwd = (PBRWD)(BRWM::PbrwmNew(_pcrm, kidMidiGlass, styMidi, this));
    LMusic:
        if (pvNil == pbrwd)
            goto LFail;

        // 3DMMv1.0: Search for background thumbs of any cno
        ckiRoot.cno = cnoNil;
        thumSelect = (int32_t)cnoNil;

        // 3DMMv1.0: Create BRCNL for context carryover (optional choice)
        if (pbrcn == pvNil)
            pbrcn = NewObj BRCNL;

        pmvu = (PMVU)(Pmvie()->PddgGet(0));
        ptag = pmvu->PtagTool();
        if (ptag->sid != ksidInvalid)
        {
            thumSelect = ptag->cno;
            sid = ptag->sid;
        }
        // 3DMMv1.0: Selection is cno based
        if (!((PBRWM)pbrwd)->FInit(pcmd, kbwsCnoRoot, thumSelect, sid, ckiRoot, ctgNil, this, (PBRCNL)pbrcn))
        {
            goto LFail;
        }
        break;

    //
    // 3DMMv1.0: The import browser is set up on top of the normal sound browser
    // 3DMMv1.0: rglw[1] = pfniMovie of movie to be scanned.
    //
    case kidBrwsImportFX:
        ckiRoot.ctg = kctgSfth;
        pbrwd = (PBRWD)(BRWI::PbrwiNew(_pcrm, kidSoundsImportGlass, stySfx));
        goto LImport;
    case kidBrwsImportSpeech:
        ckiRoot.ctg = kctgSvth;
        pbrwd = (PBRWD)(BRWI::PbrwiNew(_pcrm, kidSoundsImportGlass, stySpeech));
        goto LImport;
    case kidBrwsImportMidi:
        ckiRoot.ctg = kctgSmth;
        pbrwd = (PBRWD)(BRWI::PbrwiNew(_pcrm, kidSoundsImportGlass, styMidi));
    LImport:
        if (pvNil == pbrwd)
            goto LFail;
        // 3DMMv1.0: Build the string table before initializing the BRWD
        if (!((PBRWI)pbrwd)->FInit(pcmd, ckiRoot, this))
        {
            goto LFail;
        }
        break;

    case kidRollCallProp:
        Assert(pvNil == _pbrwrProp, "Roll Call browser already up");
        _pbrwrProp = BRWR::PbrwrNew(_pcrm, kidRollCallProp);
        pbrwd = (PBRWD)_pbrwrProp;
        if (pvNil == pbrwd)
            goto LFail;

        // 3DMMv1.0: Create the cno map from tmpl-->gokd
        if (_pglcmg == pvNil)
        {
            if (pvNil == (_pglcmg = GL::PglNew(SIZEOF(CMG), kglcmgGrow)))
                goto LFail;
            _pglcmg->SetMinGrow(kglcmgGrow);
        }

        if (!_pbrwrProp->FInit(pcmd, kctgPrth, ivNil, this))
            goto LFail;
        break;

    case kidRollCallActor:
        Assert(pvNil == _pbrwrActr, "Roll Call browser already up");
        _pbrwrActr = BRWR::PbrwrNew(_pcrm, kidRollCallActor);
        pbrwd = (PBRWD)_pbrwrActr;
        if (pvNil == pbrwd)
            goto LFail;

        // 3DMMv1.0: Create the cno map from tmpl-->gokd
        if (_pglcmg == pvNil)
        {
            if (pvNil == (_pglcmg = GL::PglNew(SIZEOF(CMG), kglcmgGrow)))
                goto LFail;
            _pglcmg->SetMinGrow(kglcmgGrow);
        }

        if (!_pbrwrActr->FInit(pcmd, kctgTmth, ivNil, this))
            goto LFail;
        break;

    default:
        RawRtn();
        break;
    }

    Assert(pvNil != pbrwd, "Logic error");
    pbrwd->FDraw(); // 3DMMv1.0: Ignore failure : reported elsewhere

    //
    // 3DMMv1.0: Optionally Add new browser to the gl for context
    // 3DMMv1.0: carryover between browser instantiations
    //
    if (pvNil != pbrcn && pvNil == _PbrcnFromBrwdid(brwdid))
    {
        if (!_pglpbrcn->FAdd(&pbrcn))
        {
            goto LFail;
        }
    }

    fSuccess = fTrue;
LFail:
    if (!fSuccess)
    {
        ReleasePpo(&pbrcn);
        ReleasePpo(&pbrwd);
    }
    vpcex->EnqueueCid(cidBrowserVisible, pvNil, pvNil, fSuccess ? 1 : 0); // 3DMMv1.0: For projects
    vapp.EndLongOp();
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Destroy browser context (when Studio destructs)
 *
 **************************************************************************/
void STDIO::ReleaseBrcn(void)
{
    int32_t ipbrcn;
    PBRCN pbrcn;

    if (pvNil == _pglpbrcn)
        return;

    for (ipbrcn = 0; ipbrcn < _pglpbrcn->IvMac(); ipbrcn++)
    {
        _pglpbrcn->Get(ipbrcn, &pbrcn);
        ReleasePpo(&pbrcn);
    }
    ReleasePpo(&_pglpbrcn);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Locate a browser pbrwd
 *
 **************************************************************************/
PBRCN STDIO::_PbrcnFromBrwdid(int32_t brwdid)
{
    AssertThis(0);
    int32_t ipbrcn;
    PBRCN pbrcn;

    for (ipbrcn = 0; ipbrcn < _pglpbrcn->IvMac(); ipbrcn++)
    {
        _pglpbrcn->Get(ipbrcn, &pbrcn);
        if (pbrcn->brwdid == brwdid)
        {
            return pbrcn;
        }
    }
    return pvNil;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Apply a Camera Selection.	(Browser callback)
 * thumSelect is an index and a chid
 *
 **************************************************************************/
void BRWC::_ApplySelection(int32_t thumSelect, int32_t sid)
{
    AssertThis(0);

    PMVU pmvu;

    _pstdio->Pmvie()->Pscen()->FChangeCam(thumSelect);

    // 3DMMv1.0: Update the tool
    pmvu = (PMVU)(_pstdio->Pmvie()->PddgActive());
    AssertPo(pmvu, 0);
    pmvu->SetTool(toolDefault);

    // 3DMMv1.0: Update the UI
    _pstdio->Pmvie()->Pmcc()->ChangeTool(toolDefault);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Apply a Background Selection.	(Browser callback)
 * thumSelect is a cno
 *
 **************************************************************************/
void BRWB::_ApplySelection(int32_t thumSelect, int32_t sid)
{
    AssertThis(0);
#if defined(BRENDER_MODERN_14)
    BrModernLog("BRWB::_ApplySelection background cno=0x%08lX sid=%ld",
                (unsigned long)thumSelect, (long)sid);
#endif

    TAGF tagf;
    CMD cmd;
    PMVU pmvu;

    tagf.sid = sid;
    tagf._pcrf = pvNil;
    tagf.ctg = kctgBkgd;
    tagf.cno = (CNO)thumSelect;

    ClearPb(&cmd, SIZEOF(cmd));
    cmd.cid = cidNewScene;
    cmd.pcmh = _pstdio;
    Assert(SIZEOF(TAGF) <= SIZEOF(cmd.rglw), "Insufficient space in rglw");
    *((TAGF *)&cmd.rglw) = tagf;

    vpcex->EnqueueCmd(&cmd);

    // 3DMMv1.0: Update the tool
    pmvu = (PMVU)(_pstdio->Pmvie()->PddgActive());
    AssertPo(pmvu, 0);
    pmvu->SetTool(toolDefault);

    // 3DMMv1.0: Update the UI
    _pstdio->Pmvie()->Pmcc()->ChangeTool(toolDefault);

    return;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Apply an Actor Selection.
 *
 * thumSelect is a cno
 *
 *
 **************************************************************************/
void BRWP::_ApplySelection(int32_t thumSelect, int32_t sid)
{
    AssertThis(0);

    TAG tag;
    PMVU pmvu;

    pmvu = (PMVU)(_pstdio->Pmvie()->PddgGet(0));
    if (pmvu == pvNil)
    {
        Warn("No pmvu");
        return;
    }

    AssertPo(pmvu, 0);
    tag.sid = sid;
    tag.pcrf = pvNil;
    tag.ctg = kctgTmpl;
    tag.cno = (CNO)thumSelect;

    if (sid == ksidUseCrf)
    {
        // Native VXP catalogue rows point at a movie-owned TMPL. Recover the
        // exact persisted roll-call TAG, including its authoritative CRF,
        // instead of routing it through installed-product/SID resolution.
        PMVIE pmvie = _pstdio->Pmvie();
        const bool fWantProp = (_ckiRoot.ctg == kctgPrth);
        bool fFoundVxp = fFalse;
        for (int32_t iarid = 0; iarid < pmvie->CmactrMac(); ++iarid)
        {
            int32_t arid = aridNil;
            int32_t cactRef = 0;
            STN stn;
            TAG tagT;
            if (!pmvie->FGetArid(iarid, &arid, &stn, &cactRef, &tagT) ||
                !pmvie->FIsVxpBrwsIarid(iarid) || pmvie->FIsIaridTdt(iarid) ||
                pmvie->FIsPropBrwsIarid(iarid) != fWantProp ||
                tagT.sid != ksidUseCrf || tagT.ctg != kctgTmpl ||
                tagT.cno != (CNO)thumSelect)
            {
                continue;
            }
            tag = tagT;
            fFoundVxp = fTrue;
            MVIE::MultiLog(pmvie,
                "vxp_browser select kind=%s iarid=%ld arid=%ld cno=%ld refs=%ld name=%s",
                fWantProp ? "prop" : "actor", (long)iarid, (long)arid,
                (long)tag.cno, (long)cactRef, stn.Psz());
            break;
        }
        if (!fFoundVxp)
        {
            MVIE::MultiLog(pmvie, "vxp_browser select FAIL cno=%ld reason=rollcall_tag_missing",
                           (long)thumSelect);
            goto LFail;
        }
    }

    if (!_pstdio->Pmvie()->FInsActr(&tag))
    {
        MVIE::MultiLog(_pstdio->Pmvie(), "vxp_browser insert FAIL sid=%ld cno=%ld",
                       (long)tag.sid, (long)tag.cno);
        goto LFail;
    }

    if (tag.sid == ksidUseCrf)
        MVIE::MultiLog(_pstdio->Pmvie(), "vxp_browser insert OK cno=%ld", (long)tag.cno);
    pmvu->StartPlaceActor(fTrue);

LFail:
    return;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Apply an Action Selection.
 *
 * thumSelect is a chid
 *
 **************************************************************************/
void BRWA::_ApplySelection(int32_t thumSelect, int32_t sid)
{
    AssertThis(0);
    AssertPo(_pstdio->Pmvie(), 0);

    PACTR pactr;
    PMVU pmvu;
    PGOK pgok;

    // 3DMMv1.0: Apply the action to the actor
    pactr = _pstdio->Pmvie()->Pscen()->PactrSelected();
    if (!pactr->FSetAction(thumSelect, _celnStart, fFalse))
        return; // 3DMMv1.0: Error reported earlier

    // 3DMMv1.0: Update the tool
    pmvu = (PMVU)(_pstdio->Pmvie()->PddgActive());
    AssertPo(pmvu, 0);
    pmvu->SetTool(toolRecordSameAction);

    // 3DMMv1.0: Update the UI
    _pstdio->Pmvie()->Pmcc()->ChangeTool(toolRecordSameAction);

    // 3DMMv1.0: Reset the studio action button state	(record will be depressed)
    pgok = (PGOK)vapp.Pkwa()->PgobFromHid(kidActorsActionBrowser);

    if ((pgok != pvNil) && pgok->FIs(kclsGOK))
    {
        Assert(pgok->FIs(kclsGOK), "Invalid class");
        pgok->FChangeState(kstDefault);
    }

    return;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Apply a Music Selection.
 *
 * thumSelect is a cnoContent
 *
 **************************************************************************/
void BRWM::_ApplySelection(int32_t thumSelect, int32_t sid)
{
    AssertThis(0);
    AssertPo(_pstdio->Pmvie(), 0);

    PGOK pgok;
    PMVU pmvu;
    TAG tag;
    bool fClick = fTrue;

    pmvu = (PMVU)(_pstdio->Pmvie()->PddgGet(0));
    AssertPo(pmvu, 0);

    tag.ctg = kctgMsnd;
    tag.cno = (CNO)thumSelect;
    tag.sid = sid;
    if (ksidUseCrf != sid)
        tag.pcrf = pvNil;
    else
    {
        if (!_pstdio->Pmvie()->FEnsureAutosave(&_pcrf))
            return;
        AssertDo(vptagm->FOpenTag(&tag, _pcrf), "Should never fail when not copying the tag");
    }

    // 3DMMv1.0: Set the tool to "play once", if necessary
    pgok = (PGOK)vpapp->Pkwa()->PgobFromHid(kidSoundsLooping);
    if (pgok != pvNil && pgok->FIs(kclsGOK) && (pgok->Sno() == kstSelected))
    {
        fClick = fFalse;
    }

    pgok = (PGOK)vpapp->Pkwa()->PgobFromHid(kidSoundsAttachToCell);
    if (pgok != pvNil && pgok->FIs(kclsGOK) && (pgok->Sno() == kstSelected) && (_sty != styMidi))
    {
        fClick = fFalse;
    }

    if (fClick)
    {
        pgok = (PGOK)vpapp->Pkwa()->PgobFromHid(kidSoundsPlayOnce);
        if (pgok != pvNil && pgok->FIs(kclsGOK) && (pgok->Sno() != kstSelected))
        {
            AssertPo(pgok, 0);
            vpcex->EnqueueCid(cidClicked, pgok, pvNil, pvNil);
        }
    }

    pmvu->SetTagTool(&tag);
    TAGM::CloseTag(&tag);
    return;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Apply an Import Music Selection.
 * Copy the msnd chunk from the open BRWI movie to the current movie
 * Then notify the underlying sound browser to update
 *
 * thumSelect is a cnoContent
 *
 **************************************************************************/
void BRWI::_ApplySelection(int32_t thumSelect, int32_t sid)
{
    AssertThis(0);
    AssertPo(_pstdio->Pmvie(), 0);
    CNO cnoDest;
    int32_t kidBrws;

    switch (_sty)
    {
    case styMidi:
        kidBrws = kidMidiGlass;
        break;
    case stySfx:
        kidBrws = kidFXGlass;
        break;
    case stySpeech:
        kidBrws = kidSpeechGlass;
        break;
    default:
        Bug("Invalid _sty");
        break;
    }

    if (pvNil == _pcrf || pvNil == _pcrf->Pcfl())
        return;

    // 3DMMv1.0: Copy	sound from _pcrf->Pcfl() to current movie
    vpappb->BeginLongOp();
    if (!_pstdio->Pmvie()->FCopyMsndFromPcfl(_pcrf->Pcfl(), (CNO)thumSelect, &cnoDest))
    {
        vpappb->EndLongOp();
        return;
    }
    vpappb->EndLongOp();

    // 3DMMv1.0: Select the item, extend lists and hilite it
    vpcex->EnqueueCid(cidBrowserSelectThum, vpappb->PcmhFromHid(kidBrws), pvNil, cnoDest, sid, 1, 1);

    return;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Apply a Roll Call Selection.
 * thumSelect equals ithum
 *
 **************************************************************************/
void BRWR::_ApplySelection(int32_t thumSelect, int32_t sid)
{
    AssertThis(0);

    PMVU pmvu;
    PMVIE pmvie = _pstdio->Pmvie();
    int32_t arid;
    STN stn;
    int32_t cactRef;

    int32_t iarid = _IaridFromIthum(thumSelect);
    if (!pmvie->FGetArid(iarid, &arid, &stn, &cactRef))
        return;

    _fApplyingSel = fTrue;
    pmvu = (PMVU)pmvie->PddgActive();
    pmvie->FChooseArid(arid);
    if (!pmvu->FActrMode())
    {
        pmvu->SetTool(toolCompose);
        _pstdio->ChangeTool(toolCompose);
    }
    _fApplyingSel = fFalse;
}
