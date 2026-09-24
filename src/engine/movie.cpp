/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

  movie.cpp

  Author: Sean Selitrennikoff

  Date: August, 1994

  This file contains all functionality for movie manipulation.

  THIS IS A CODE REVIEWED FILE

    Basic movie private classes:

        Movie Scene actions Undo Object (MUNS)

            BASE ---> UNDB ---> MUNB ---> MUNS


Note: The client of the movie engine should always do all actions through
MVIE level APIs, it should never use accessor functions to maniplate Scenes,
Actors, Text boxes, etc.

***************************************************************************/

#include "soc.h"
#include "stdiodef.h"
#include <stdio.h>
#include <float.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#if defined(KAUAI_WIN32)
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#endif

// Runtime Object Group parent attachment is implemented in body.cpp so BODY's
// existing world-space API can remain authoritative while its BRender root is
// reparented beneath one shared transform.
extern bool F4DMMBodyAttachToObjectGroupParent(PBODY pbody, PBACT pbactParent);
extern void Detach4DMMBodyFromObjectGroupParent(PBODY pbody);
extern bool F4DMMBodyGetObjectGroupLocalPose(PBODY pbody, BMAT34 *pbmat34Local);
extern bool F4DMMBodySetObjectGroupLocalPose(PBODY pbody, const BMAT34 *pbmat34Local);
extern void Set4DMMBodyShadowCasterExcluded(PBODY pbody, bool fExclude);
extern void Set4DMMBodyObjectShadowProperties(PBODY pbody, bool fCastShadows, bool fFlushOverlap);

#if defined(KAUAI_WIN32)
// Implemented by the native Studio browser layer. Free Cam uses this only to
// mirror the movie's authoritative camera-follow shutdown in visible browser
// checkboxes immediately.
void CancelExternalBrowserCameraFollowForFreeCam(void);
void SyncExternalContentBrowserSelection(void);
bool F4DMMConfirmWithDontAsk(const achar *pszText, bool *pfDontAsk);
bool F4DMMPackageVmm(PCSZ pszMovie, PCSZ pszSidecar, PCSZ pszDest);
void Refresh4DMMActorStudioAfterActionEdit(PMVIE pmvie, int32_t arid, int32_t anid,
                                            int32_t celn, int32_t ipart);
void Refresh4DMMActorStudioAfterActionEditSelection(PMVIE pmvie, int32_t arid, int32_t anid,
                                                     int32_t celn, int32_t ipart,
                                                     int32_t selectionKind,
                                                     int32_t ipartSelectionFirst,
                                                     int32_t cpartSelection);
void Refresh4DMMActorStudioAfterTopologyEdit(PMVIE pmvie, int32_t arid, CNO cnoTmpl,
                                              int32_t anid, int32_t celn, int32_t ipart);
PACTR Pactr4DMMActorStudioUndoTarget(PMVIE pmvie, CNO cnoTmpl);

static HWND Hwnd4DMMScaledInput(HWND hwndFallback)
{
    if (vwig.hwndApp != hNil && IsWindow(vwig.hwndApp))
    {
        HWND hwndScale = (HWND)GetPropA(vwig.hwndApp, "4DMMUiScaleWindow");
        if (hwndScale != hNil && IsWindow(hwndScale))
            return hwndScale;
    }
    return hwndFallback;
}

static HWND Hwnd4DMMNativeToolOwner(void)
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

static bool F4DMMEditorInputForeground(HWND hwndRootForeground, HWND hwndRootView, HWND hwndRootApp)
{
    if (hwndRootForeground == hwndRootView || hwndRootForeground == hwndRootApp)
        return fTrue;

    HWND hwndScale = Hwnd4DMMScaledInput(hNil);
    HWND hwndRootScale = hwndScale == hNil ? hNil : GetAncestor(hwndScale, GA_ROOT);
    return hwndRootScale != hNil && hwndRootForeground == hwndRootScale;
}
#endif
ASSERTNAME

#define krScaleMouseRecord BR_SCALAR(0.20)
#define krScaleMouseNonRecord BR_SCALAR(0.02)
#define kdwrMousePerSecond BR_SCALAR(100.0) // 3DMMv1.0: "mouse drag" speed for arrow keys

#define kdtsFrame (kdtsSecond / kfps)   // 3DMMv1.0: milliseconds per frame
#define kdtimFrame (kdtimSecond / kfps) // 3DMMv1.0: clock ticks per frame
#define kdtsCycleCels (kdtsFrame * 3)   // 3DMMv1.0: delay when cycling cels
#define kdtsVlmFade 3                   // 3DMMv1.0: number of seconds to fade
#define kdtimVlmFade (kdtimSecond / 4)  // 3DMMv1.0: number of clock ticks necessary to split 1 sec into 4 events
#define kzrMouseScalingFactor BR_SCALAR(100.0)
#define kdManualCameraUnitsPerSecond 300.0f
#define kfl4DMMDefaultCameraMouseSensitivity 0.07f
#define kdManualCameraPitchLimit 89.0f
#define kdFreeLookRollDegreesPerSecond 75.0f

const CHID kchidGstSource = 1;

// Command-line launch state is process-wide and must be available before the
// first MVIE object is constructed.  The old property-table handoff could
// silently read back false, leaving -u with the original one-level history.
static bool vfExtendedUndoMode = fFalse;
static bool vfMultiSelectMode = fFalse;
static bool vfPrecacheMode = fFalse;
static bool vfTestLightMode = fFalse;
static bool vfShadowMode = fFalse;
static bool vfSceneDynamicLightingActive = fFalse;
static bool vfSceneDefaultLightingShadersActive = fTrue;
static bool vfSceneLightLabCombineLegacyActive = fFalse;
static bool vfDiagnosticsMode = fFalse;
static bool vfLightEditorLogMode = fFalse;
static bool vfMultiLogMode = fFalse;
static bool vfPerformanceMode = fFalse;

// Scene lighting is a runtime policy, while mapped BRender materials can live
// in the registry/cache across scene switches.  Re-apply the mapped-material
// ambient/diffuse response when the scene master switch changes so CTRL+SHIFT+L
// really returns to normal 3DMM lighting immediately instead of waiting for a
// material to be reloaded.
static br_uint_32 BR_CALLBACK Refresh4DMMMappedMaterialLighting(br_material *pmat, void * /*pvUnused*/)
{
    if (pmat == pvNil || pmat->colour_map == pvNil)
        return 0;

    const bool fDynamic = MVIE::FSceneDynamicLightingActive();
    const bool fDefaultShaders = MVIE::FSceneDefaultLightingShadersActive();
    const bool fCombineLegacy = MVIE::FSceneLightLabCombineLegacyActive();

    if (fDynamic)
    {
        // Active Light Lab is intentionally independent from the legacy/default
        // shader toggle. The separate combine switch controls whether the
        // legacy background rig/material fill participates with the new lights.
        if (fCombineLegacy)
        {
            pmat->flags |= BR_MATF_LIGHT | BR_MATF_SMOOTH;
            pmat->flags &= ~BR_MATF_DITHER;
            pmat->ka = BR_UFRACTION(0.35);
            pmat->kd = BR_UFRACTION(0.60);
            pmat->ks = BR_UFRACTION(0.00);
            BrMaterialUpdate(pmat, BR_MATU_LIGHTING | BR_MATU_RENDERING);
            return 0;
        }

        // Preserve the stable v8 isolated-Light-Lab path. If this material is
        // already lit, change only the lighting response instead of rewriting
        // render flags while the lab is active. The only exception is a cached
        // material that the bright legacy-default mode previously de-lit; that
        // one must be restored to the lit pipeline once when Light Lab starts.
        const bool fWasLit = (pmat->flags & BR_MATF_LIGHT) != 0;
        if (!fWasLit)
        {
            pmat->flags |= BR_MATF_LIGHT | BR_MATF_SMOOTH;
            pmat->flags &= ~BR_MATF_DITHER;
        }
        pmat->ka = BR_UFRACTION(0.00);
        pmat->kd = BR_UFRACTION(1.00);
        pmat->ks = BR_UFRACTION(0.00);
        BrMaterialUpdate(pmat, fWasLit ? BR_MATU_LIGHTING : (BR_MATU_LIGHTING | BR_MATU_RENDERING));
        return 0;
    }

    if (fDefaultShaders)
    {
        pmat->flags |= BR_MATF_LIGHT | BR_MATF_SMOOTH;
        pmat->flags &= ~BR_MATF_DITHER;
        pmat->ka = BR_UFRACTION(0.35);
        pmat->kd = BR_UFRACTION(0.60);
        pmat->ks = BR_UFRACTION(0.00);
        BrMaterialUpdate(pmat, BR_MATU_LIGHTING | BR_MATU_RENDERING);
        return 0;
    }

    // Bright classic texture presentation for default-lighting mode only.
    pmat->flags &= ~(BR_MATF_LIGHT | BR_MATF_DITHER);
    pmat->ka = BR_UFRACTION(1.00);
    pmat->kd = BR_UFRACTION(0.00);
    pmat->ks = BR_UFRACTION(0.00);
    BrMaterialUpdate(pmat, BR_MATU_LIGHTING | BR_MATU_RENDERING);
    return 0;
}

static void Refresh4DMMMappedMaterialLightingPolicy(void)
{
    if (!BWLD::FTrueColorMode() || !BWLD::FActorLightMode())
        return;
    BrMaterialEnum((char *)"*", Refresh4DMMMappedMaterialLighting, pvNil);
}

struct PERFACC
{
    uint32_t cusecForceActors;
    uint32_t cusecForceTboxes;
    uint32_t cusecVisibility;
    uint32_t cusecSceneCamera;
    uint32_t cusecPrerender;
    uint32_t cusecLightPreparation;
    uint32_t cusecModelLighting;
    uint32_t cusecModelUpdate;
    uint32_t cusecModelUpdateAll;
    uint32_t cusecModelUpdateOther;
    uint32_t cusecSetActnCel;
    uint32_t cusecActionFetch;
    uint32_t cusecModelFetch;
    uint32_t cusecModelLookup;
    uint32_t cusecPbacoFetch;
    uint32_t cusecModelRead;
    uint32_t cusecModelDestroy;
    uint32_t cusecPartModel;
    uint32_t cusecPartModelContentCompare;
    uint32_t cusecPartModelGeometryCompare;
    uint32_t cusecPartMatrix;
    uint32_t cusecBRenderBegin;
    uint32_t cusecBRenderClean;
    uint32_t cusecBRenderScene;
    uint32_t cusecBRenderPost;
    uint32_t cusecBRenderTotal;
    int32_t cModelsRelit;
    int32_t cVerticesProcessed;
    int32_t cModelsRebuilt;
    int32_t cModelAddCalls;
    int32_t cModelAddQuickCalls;
    int32_t cModelUpdateAllCalls;
    int32_t cModelUpdateOtherCalls;
    int32_t cModelUpdateQuickCalls;
    int32_t cLightCalculations;
    int32_t cSetActnCelCalls;
    int32_t cActionFetchCalls;
    int32_t cModelFetchCalls;
    int32_t cModelReadCalls;
    int32_t cModelReadUnprepared;
    int32_t cModelReadPreprepared;
    int32_t cModelDestroyCalls;
    int32_t cPartModelCalls;
    int32_t cPartModelSamePointer;
    int32_t cPartModelSameSource;
    int32_t cPartModelSameResource;
    int32_t cPartModelResourceReuse;
    int32_t cPartModelSameFile;
    int32_t cPartModelFileReuse;
    int32_t cPartModelSameContent;
    int32_t cPartModelContentReuse;
    int32_t cPartModelContentCandidates;
    int32_t cPartModelOldCrf;
    int32_t cPartModelNewCrf;
    int32_t cPartModelOldFingerprint;
    int32_t cPartModelNewFingerprint;
    int32_t cPartModelFingerprintSizeMatch;
    int32_t cPartModelFingerprintHashAMatch;
    int32_t cPartModelFingerprintHashBMatch;
    uint32_t grfPartModelGeometryReject;
    int32_t cPartModelSameGeometry;
    int32_t cPartModelGeometryReuse;
    int32_t cPartModelChanged;
    int32_t cPartMatrixCalls;
    int32_t cUniqueModelRequests;
    int32_t cUniqueModelReads;
    int32_t cUniqueModelOverflow;
    uint64_t rgqwModelRequestKeys[1024];
    uint64_t rgqwModelReadKeys[1024];
    int32_t cBRenderCalls;
    int32_t cWorldChildrenMax;
    // actorlight39: -3dfix projection/depth diagnostics.  Keep these aggregate
    // per-frame so -perf remains cheap enough to reproduce camera-dependent bugs.
    int32_t c3DFixMeshCalls;
    int32_t c3DFixMeshAcceptInput;
    int32_t c3DFixForcedPartial;
    int32_t cSafeDivOverflows;
    int32_t cDepthGradientCalls;
    int32_t cDepthGradientOverflows;
    int32_t xr3DFixCamera;
    int32_t yr3DFixCamera;
    int32_t zr3DFixCamera;
    int32_t zr3DFixHither;
    int32_t zr3DFixYon;
};

static PERFACC vperfAcc;

// C-visible fast gate for per-vertex Source BRender instrumentation.
// BRender can test this integer without a C++ function call on every vertex.
extern "C" {
int vgLightingPerformanceEnabled = 0;
// actorlight27: source-BRender may use its documented quick-update model
// preparation path whenever -a is active.  This is independent of -perf.
int vgActorLightQuickModelUpdateEnabled = 0;
}

#if defined(KAUAI_WIN32)
static int32_t viDiagScene = ivNil;
static int32_t vnDiagFrame = ivNil;
static achar vszDiagPhase[160] = "startup";
static uint32_t vluDiagSequence = 0;
static LARGE_INTEGER vliPerfFrequency = {0};
static FILE *vpfilePerf = pvNil;
static FILE *vpfileLightEditor = pvNil;
static FILE *vpfileMulti = pvNil;
static uint32_t vluLightEditorSequence = 0;
static uint32_t vluMultiSequence = 0;
static uint32_t vluPerfSequence = 0;
static uint32_t vcPerfRowsSinceFlush = 0;

static bool FDiagPath(PCSZ pszFile, achar *pszPath, int32_t cchPath)
{
    if (pszFile == pvNil || pszPath == pvNil || cchPath <= 0)
        return fFalse;

    achar szExe[MAX_PATH];
    DWORD cch = GetModuleFileNameA(NULL, szExe, SIZEOF(szExe));
    if (cch == 0 || cch >= SIZEOF(szExe))
        return fFalse;

    achar *pchSlash = strrchr(szExe, '\\');
    achar *pchSlashAlt = strrchr(szExe, '/');
    if (pchSlashAlt != pvNil && (pchSlash == pvNil || pchSlashAlt > pchSlash))
        pchSlash = pchSlashAlt;
    if (pchSlash != pvNil)
        *pchSlash = chNil;
    else
        strcpy_s(szExe, SIZEOF(szExe), ".");

    achar szDir[MAX_PATH];
    if (0 > sprintf_s(szDir, SIZEOF(szDir), "%s\\logs", szExe))
        return fFalse;

    if (!CreateDirectoryA(szDir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return fFalse;

    return 0 <= sprintf_s(pszPath, cchPath, "%s\\%s", szDir, pszFile);
}

static void ResetDiagnosticsFiles(void)
{
    achar szPath[MAX_PATH];
    if (FDiagPath("session.log", szPath, SIZEOF(szPath)))
    {
        FILE *pfile = pvNil;
        if (0 == fopen_s(&pfile, szPath, "w") && pfile != pvNil)
        {
            fprintf(pfile, "3DMMEx -logs diagnostic session\n");
            fclose(pfile);
        }
    }
    if (FDiagPath("crash.log", szPath, SIZEOF(szPath)))
        DeleteFileA(szPath);
}
#endif // KAUAI_WIN32

static bool FActorsAndPropsTabOpen(void)
{
    // The secondary Actors/Props tool panel exists only while that cover is
    // open.  Checking the panel itself avoids coupling the engine to Studio's
    // concrete APP/GOK classes.
    return GOB::PgobFromHidScr(kidActorsBackground) != pvNil;
}

static bool FActorEditEaselOpen(void)
{
    // F is ordinary text inside the 3D Words / Actor Dresser / prop naming
    // easels.  Never let the Free Camera shortcut steal that keystroke while
    // either modal editing pane is alive.
    return GOB::PgobFromHidScr(kidSpltGlass) != pvNil ||
           GOB::PgobFromHidScr(kidCostGlass) != pvNil;
}

static bool vfDepthMotionTweenHandledError = fFalse;

#if defined(KAUAI_WIN32)
// One small, process-wide editor is sufficient because Studio presents one
// active movie at a time.  It edits only the selected scene's depth block and
// writes the full sidecar back through MVIE's canonical writer.
static const achar kszDepthMotionTweenWndClass[] = "3DMMExDepthMotionTweenEditor";
// Avoid the standard Win32 IDOK/IDCANCEL control IDs.  In an owned top-level
// tool window those reserved values can be swallowed by dialog-style message
// handling before our WM_COMMAND branch sees the Save click.
static const int32_t kidDepthMotionTweenEdit = 0x5101;
static const int32_t kidDepthMotionTweenSave = 0x5102;
static const int32_t kidDepthMotionTweenCancel = 0x5103;
static HWND vhwndDepthMotionTween = hNil;
static HWND vhwndDepthMotionTweenEdit = hNil;
static PMVIE vpmvieDepthMotionTween = pvNil;
static int32_t viscenDepthMotionTween = ivNil;
static int32_t vitweenDepthMotionTween = ivNil;
static CTTWEEN vctweenDepthMotionTweenDefault;

static const achar kszManualCameraFrameWndClass[] = "3DMMExManualCameraFrameEditor";
static const int32_t kidManualCameraFrameEdit = 0x5111;
static const int32_t kidManualCameraFrameOk = 0x5112;
static const int32_t kidManualCameraFrameCancel = 0x5113;
static HWND vhwndManualCameraFrame = hNil;
static HWND vhwndManualCameraFrameEdit = hNil;
static PMVIE vpmvieManualCameraFrame = pvNil;
static int32_t viscenManualCameraFrame = ivNil;
static int32_t vnfrmManualCameraFrame = ivNil;

// 4DMM Light Lab editor.  Object-bound lights are scene-local and persist
// in the movie's matching .3ct sidecar alongside camera-track data.
static const achar kszLightLabWndClass[] = "4DMMLightLabEditor";
static const int32_t kidLightLabEdit = 0x5121;
static const int32_t kidLightLabSave = 0x5122;
static const int32_t kidLightLabCancel = 0x5123;
static HWND vhwndLightLab = hNil;
static HWND vhwndLightLabEdit = hNil;
static WNDPROC vwprcLightLabEditPrev = pvNil;
static PMVIE vpmvieLightLab = pvNil;
static bool vfLightLabEnabled = fTrue;
static bool vfLightLabGenerateShadows = fFalse;
static bool vfLightLabAttachSelected = fFalse;
static bool vfLightLabAttachmentHideable = fFalse;
static int32_t varidLightLab = aridNil;
static int32_t vnLightLabIntensity = 100;
static float vflLightLabDiameter = 24.0f;       // full cone diameter, degrees
static float vflLightLabEdgeGradient = 4.0f;   // radial feather width, degrees
static float vflLightLabRange = 500.0f;          // outer radial range, world units
static int32_t vnfrmLightLabSpawn = 1;
static int32_t vnfrmLightLabDespawn = 0;
static bool vfLightLabEditingExisting = fFalse;
static achar vszLightLabShape[16] = "round";

// Free/Manual Camera mouse-look deliberately uses coalesced cursor-position
// sampling from the normal editor idle loop. Raw Input was previously routed
// through a message-only window so no physical mouse delta was lost, but that
// also queued one WM_INPUT per hardware poll on the main UI thread. High-poll
// mice could therefore starve Kauai's idle/render loop while the mouse moved,
// even though keyboard-only WASD movement remained smooth. Standard
// WM_MOUSEMOVE is coalesced by Win32; sampling/recentering once per idle tick
// keeps camera work bounded to one look update and one render per editor tick.
//
// Keep these counters intentionally low-overhead. They emit one summary per
// second to multi.log instead of logging individual mouse messages/deltas.
static uint32_t vtsCameraInputPerfStart = 0;
static uint32_t vcCameraInputPerfIdle = 0;
static uint32_t vcCameraInputPerfLook = 0;
static uint32_t vcCameraInputPerfRender = 0;
static uint32_t vcCameraInputPerfPaintFlush = 0;
static uint32_t vcCameraInputPerfPaintPending = 0;
static uint32_t vdtsCameraInputPerfRender = 0;
static uint32_t vdtsCameraInputPerfPaint = 0;
static uint32_t vdtsCameraInputPerfRenderMax = 0;
static uint32_t vdtsCameraInputPerfPaintMax = 0;
static int32_t vdxCameraInputPerfMax = 0;
static int32_t vdyCameraInputPerfMax = 0;

static void Close4DMMNativeToolWindow(HWND hwnd)
{
    HWND hwndOwner = GetWindow(hwnd, GW_OWNER);
    if (hwndOwner == hNil || !IsWindow(hwndOwner))
        hwndOwner = Hwnd4DMMNativeToolOwner();
    DestroyWindow(hwnd);
    // Return focus to the visible application root.  In scaled mode this is
    // the presentation HWND, not the hidden 640x480 Kauai source.
    if (hwndOwner != hNil && IsWindow(hwndOwner))
    {
        if (IsIconic(hwndOwner))
            ShowWindow(hwndOwner, SW_RESTORE);
        SetForegroundWindow(hwndOwner);
    }
}

static void UpdateLightLabEditorText(void)
{
    if (vhwndLightLabEdit == hNil)
        return;

    achar sz[512];
    sprintf_s(sz, SIZEOF(sz),
              "light enabled %d\r\n"
              "generate shadows %d\r\n"
              "attachment hide-able %d\r\n"
              "light beam intensity %d\r\n"
              "light edge gradient %.2f\r\n"
              "light diameter %.2f\r\n"
              "light range %.2f\r\n"
              "spawn frame %d\r\n"
              "despawn frame %d\r\n"
              "light shape %s",
              vfLightLabEnabled ? 1 : 0,
              vfLightLabGenerateShadows ? 1 : 0,
              vfLightLabAttachmentHideable ? 1 : 0,
              (int)vnLightLabIntensity,
              (double)vflLightLabEdgeGradient,
              (double)vflLightLabDiameter,
              (double)vflLightLabRange,
              (int)vnfrmLightLabSpawn,
              (int)vnfrmLightLabDespawn,
              vszLightLabShape);
    SetWindowTextA(vhwndLightLabEdit, sz);
}

static bool FSaveLightLabText(void)
{
    if (vpmvieLightLab == pvNil || vhwndLightLabEdit == hNil)
        return fFalse;

    int32_t cch = GetWindowTextLengthA(vhwndLightLabEdit);
    achar *psz = pvNil;
    if (!FAllocPv((void **)&psz, (cch + 1) * SIZEOF(achar), fmemClear, mprNormal))
        return fFalse;
    GetWindowTextA(vhwndLightLabEdit, psz, cch + 1);

    int nEnabled = vfLightLabEnabled ? 1 : 0;
    int nGenerateShadows = vfLightLabGenerateShadows ? 1 : 0;
    int nHideable = vfLightLabAttachmentHideable ? 1 : 0;
    int nIntensity = vnLightLabIntensity;
    float flEdge = vflLightLabEdgeGradient;
    float flDiameter = vflLightLabDiameter;
    float flRange = vflLightLabRange;
    int nfrmSpawn = vnfrmLightLabSpawn;
    int nfrmDespawn = vnfrmLightLabDespawn;
    achar szShape[16];
    strcpy_s(szShape, SIZEOF(szShape), vszLightLabShape);
    bool fEnabledSeen = fFalse, fGenerateShadowsSeen = fFalse, fHideableSeen = fFalse, fIntensitySeen = fFalse;
    bool fEdgeSeen = fFalse, fDiameterSeen = fFalse, fRangeSeen = fFalse, fShapeSeen = fFalse;
    bool fSpawnSeen = fFalse, fDespawnSeen = fFalse;

    achar *pszCtx = pvNil;
    for (achar *pszLine = strtok_s(psz, "\r\n", &pszCtx); pszLine != pvNil;
         pszLine = strtok_s(pvNil, "\r\n", &pszCtx))
    {
        int n = 0;
        float fl = 0.0f;
        achar szWord[16];
        if (sscanf_s(pszLine, "light enabled %d", &n) == 1)
        {
            nEnabled = n; fEnabledSeen = fTrue;
        }
        else if (sscanf_s(pszLine, "generate shadows %d", &n) == 1)
        {
            nGenerateShadows = n; fGenerateShadowsSeen = fTrue;
        }
        else if (sscanf_s(pszLine, "attachment hide-able %d", &n) == 1)
        {
            nHideable = n; fHideableSeen = fTrue;
        }
        else if (sscanf_s(pszLine, "light beam intensity %d", &n) == 1)
        {
            nIntensity = n; fIntensitySeen = fTrue;
        }
        else if (sscanf_s(pszLine, "light edge gradient %f", &fl) == 1)
        {
            flEdge = fl; fEdgeSeen = fTrue;
        }
        else if (sscanf_s(pszLine, "light diameter %f", &fl) == 1)
        {
            flDiameter = fl; fDiameterSeen = fTrue;
        }
        else if (sscanf_s(pszLine, "light range %f", &fl) == 1)
        {
            flRange = fl; fRangeSeen = fTrue;
        }
        else if (sscanf_s(pszLine, "spawn frame %d", &n) == 1)
        {
            nfrmSpawn = n; fSpawnSeen = fTrue;
        }
        else if (sscanf_s(pszLine, "despawn frame %d", &n) == 1)
        {
            nfrmDespawn = n; fDespawnSeen = fTrue;
        }
        else if (sscanf_s(pszLine, "light shape %15s", szWord, (unsigned)SIZEOF(szWord)) == 1)
        {
            strcpy_s(szShape, SIZEOF(szShape), szWord); fShapeSeen = fTrue;
        }
    }
    FreePpv((void **)&psz);

    const int32_t nfrmLastScene = (vpmvieLightLab->Pscen() != pvNil) ?
        vpmvieLightLab->Pscen()->NfrmLast() - vpmvieLightLab->Pscen()->NfrmFirst() + 1 : 0;
    if (!fEnabledSeen || !fGenerateShadowsSeen || !fHideableSeen || !fIntensitySeen || !fEdgeSeen ||
        !fDiameterSeen || !fRangeSeen || !fSpawnSeen || !fDespawnSeen || !fShapeSeen ||
        (nEnabled != 0 && nEnabled != 1) || (nGenerateShadows != 0 && nGenerateShadows != 1) ||
        (nHideable != 0 && nHideable != 1) ||
        nIntensity < 1 || nIntensity > 100 || nfrmSpawn < 1 || nfrmDespawn < 0 ||
        (nfrmLastScene > 0 && (nfrmSpawn > nfrmLastScene || nfrmDespawn > nfrmLastScene)) ||
        (nfrmDespawn > 0 && nfrmSpawn > nfrmDespawn) ||
        flDiameter <= 0.1f || flDiameter >= 170.0f || flEdge < 0.0f ||
        flEdge >= flDiameter * 0.5f || flRange < 1.0f || flRange > 30000.0f ||
        _stricmp(szShape, "round") != 0)
    {
        MessageBoxA(vhwndLightLab,
                    "Use: enabled 0/1, generate shadows 0/1, attachment hide-able 0/1, intensity 1-100, "
                    "diameter 0.1-169 degrees, edge gradient >= 0 and less than half the diameter, "
                    "range 1-30000 world units, spawn frame 1 through the last scene frame, "
                    "despawn frame 0 through the last scene frame (0 despawn = no end). "
                    "Only shape 'round' is supported by native BRender spotlights right now.",
                    "4DMM Light Lab", MB_OK | MB_ICONERROR);
        UpdateLightLabEditorText();
        return fFalse;
    }

    if (vpmvieLightLab->Pscen() == pvNil || vpmvieLightLab->Pscen()->PactrSelected() == pvNil)
    {
        MessageBoxA(vhwndLightLab,
                    "Select an actor, prop, or 3D word before saving a light.",
                    "4DMM Light Lab", MB_OK | MB_ICONERROR);
        return fFalse;
    }
    int32_t aridNew = vpmvieLightLab->Pscen()->PactrSelected()->Arid();
    if (nGenerateShadows != 0 &&
        !vpmvieLightLab->FLightLabShadowSlotAvailable(vpmvieLightLab->Iscen(), aridNew))
    {
        MessageBoxA(vhwndLightLab,
                    "Only one Light Lab object per scene can have generate shadows set to 1 right now. "
                    "Disable generate shadows on the other shadow light first.",
                    "4DMM Light Lab", MB_OK | MB_ICONERROR);
        return fFalse;
    }

    vfLightLabEnabled = nEnabled != 0;
    vfLightLabGenerateShadows = nGenerateShadows != 0;
    vfLightLabAttachSelected = fTrue;
    vfLightLabAttachmentHideable = nHideable != 0;
    varidLightLab = aridNew;
    vnLightLabIntensity = nIntensity;
    vflLightLabDiameter = flDiameter;
    vflLightLabEdgeGradient = flEdge;
    vflLightLabRange = flRange;
    vnfrmLightLabSpawn = nfrmSpawn;
    vnfrmLightLabDespawn = nfrmDespawn;
    strcpy_s(vszLightLabShape, SIZEOF(vszLightLabShape), szShape);

    if (vfLightLabAttachSelected && varidLightLab != aridNil)
    {
        LIGHTLAB light;
        ClearPb(&light, SIZEOF(light));
        light.iscen = vpmvieLightLab->Iscen();
        light.arid = varidLightLab;
        light.nfrmSpawn = vnfrmLightLabSpawn;
        light.nfrmDespawn = vnfrmLightLabDespawn;
        light.fEnabled = vfLightLabEnabled;
        light.fGenerateShadows = vfLightLabGenerateShadows;
        light.fAttachmentHideable = vfLightLabAttachmentHideable;
        light.intensity = vnLightLabIntensity;
        light.edgeGradient = vflLightLabEdgeGradient;
        light.diameter = vflLightLabDiameter;
        light.range = vflLightLabRange;
        strcpy_s(light.szShape, SIZEOF(light.szShape), vszLightLabShape);

        PACTR pactrSelected = vpmvieLightLab->Pscen()->PactrSelected();
        const int32_t nfrmCurAbs = vpmvieLightLab->Pscen()->Nfrm();
        const int32_t nfrmSceneFirstBefore = vpmvieLightLab->Pscen()->NfrmFirst();
        int32_t nfrmLifeFirst = nfrmCurAbs;
        int32_t nfrmLifeLast = nfrmCurAbs;
        const bool fCutEarlierLifetime = !vfLightLabEditingExisting && nfrmCurAbs > vpmvieLightLab->Pscen()->NfrmFirst() &&
            pactrSelected != pvNil && pactrSelected->FGetLifetime(&nfrmLifeFirst, &nfrmLifeLast) &&
            nfrmLifeFirst < nfrmCurAbs;

        bool fDontAsk = fFalse;
        if (fCutEarlierLifetime && !vpmvieLightLab->FDontAskLightLabCut())
        {
            achar szPrompt[256];
            const int32_t nfrmFirstRel = nfrmLifeFirst - vpmvieLightLab->Pscen()->NfrmFirst() + 1;
            const int32_t nfrmCurRel = nfrmCurAbs - vpmvieLightLab->Pscen()->NfrmFirst() + 1;
            if (nfrmCurAbs - nfrmLifeFirst == 1)
                sprintf_s(szPrompt, SIZEOF(szPrompt),
                          "This action will result in the selected object being cut from frame %d. Continue?",
                          (int)nfrmFirstRel);
            else
                sprintf_s(szPrompt, SIZEOF(szPrompt),
                          "This action will result in the selected object being cut from frames %d through %d. Continue?",
                          (int)nfrmFirstRel, (int)nfrmCurRel);
            if (!F4DMMConfirmWithDontAsk(szPrompt, &fDontAsk))
                return fFalse;
        }

        if (fCutEarlierLifetime)
        {
            // One full snapshot makes the actor lifetime trim, new Light Lab
            // metadata, and optional warning suppression a single undo step.
            if (!vpmvieLightLab->Pscen()->FAddSnapshotUndo(PszLit("Create Light")))
                return fFalse;
            bool fAlive = fFalse;
            pactrSelected->DeleteBackCore(&fAlive);
            vpmvieLightLab->Pscen()->PreserveFirstFrameBoundary(nfrmSceneFirstBefore);
            if (!fAlive || !vpmvieLightLab->FRestoreLightLabConfigCore(&light))
            {
                vpmvieLightLab->FUndo();
                vpmvieLightLab->ClearUndo();
                MessageBoxA(vhwndLightLab, "Unable to create this Light Lab object.",
                            "4DMM Light Lab", MB_OK | MB_ICONERROR);
                return fFalse;
            }
            if (fDontAsk)
                vpmvieLightLab->SetDontAskLightLabCutCore(fTrue);
        }
        else if (!vpmvieLightLab->FSetLightLabConfig(&light))
        {
            MessageBoxA(vhwndLightLab, "No more Light Lab slots are available in this movie.",
                        "4DMM Light Lab", MB_OK | MB_ICONERROR);
            return fFalse;
        }
    }

    vpmvieLightLab->FSetSceneLightsEnabled(vpmvieLightLab->Iscen(), fTrue, fFalse);
    vpmvieLightLab->RefreshTestLight();
    vpmvieLightLab->UpdateTestLightAttachment();
    if (vpmvieLightLab->Pbwld() != pvNil)
        vpmvieLightLab->Pbwld()->MarkDirty();
    if (vpmvieLightLab->Pmcc() != pvNil)
        vpmvieLightLab->Pmcc()->UpdateScrollbars();
    vpmvieLightLab->InvalViewsAndScb();
    vpmvieLightLab->MarkViews();
    return fTrue;
}

static LRESULT CALLBACK LresultLightLabEditWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    // The Light Lab text is a fixed property sheet rather than free-form prose.
    // Treat plain Enter as Apply/Save so a changed property immediately reaches
    // BRender; Shift+Enter retains ordinary multiline editing when needed.
    if (wm == WM_KEYDOWN && wParam == VK_RETURN && GetAsyncKeyState(VK_SHIFT) >= 0)
    {
        HWND hwndParent = GetParent(hwnd);
        if (hwndParent != hNil)
            SendMessage(hwndParent, WM_COMMAND, MAKEWPARAM(kidLightLabSave, BN_CLICKED), (LPARAM)hwnd);
        return 0;
    }
    return vwprcLightLabEditPrev != pvNil ?
        CallWindowProc(vwprcLightLabEditPrev, hwnd, wm, wParam, lParam) :
        DefWindowProc(hwnd, wm, wParam, lParam);
}

static LRESULT CALLBACK LresultLightLabWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    switch (wm)
    {
    case WM_CREATE:
        vhwndLightLab = hwnd;
        vhwndLightLabEdit = CreateWindowExA(
            WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            0, 0, 0, 0, hwnd, (HMENU)kidLightLabEdit, GetModuleHandleA(pvNil), pvNil);
        if (vhwndLightLabEdit == hNil)
            return -1;
        vwprcLightLabEditPrev = (WNDPROC)SetWindowLongPtrA(
            vhwndLightLabEdit, GWLP_WNDPROC, (LONG_PTR)LresultLightLabEditWndProc);
        SetPropA(vhwndLightLabEdit, "3DMMExNativeTextInput", (HANDLE)1);
        CreateWindowExA(0, "BUTTON", "Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        0, 0, 0, 0, hwnd, (HMENU)kidLightLabSave, GetModuleHandleA(pvNil), pvNil);
        CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        0, 0, 0, 0, hwnd, (HMENU)kidLightLabCancel, GetModuleHandleA(pvNil), pvNil);
        SendMessageA(vhwndLightLabEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
        SendMessageA(GetDlgItem(hwnd, kidLightLabSave), WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
        SendMessageA(GetDlgItem(hwnd, kidLightLabCancel), WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
        UpdateLightLabEditorText();
        return 0;
    case WM_SIZE:
    {
        int32_t dxp = LOWORD(lParam), dyp = HIWORD(lParam);
        const int32_t d8 = Lw4DMMExternalToolUi200(hwnd, 8);
        const int32_t d16 = Lw4DMMExternalToolUi200(hwnd, 16);
        const int32_t dxpButton = Lw4DMMExternalToolUi200(hwnd, 72);
        const int32_t dypButton = Lw4DMMExternalToolUi200(hwnd, 24);
        const int32_t xpButtons = LwMax(d8, (dxp - (dxpButton * 2 + d8)) / 2);
        const int32_t ypButtons = LwMax(Lw4DMMExternalToolUi200(hwnd, 130), dyp - dypButton - d8);
        MoveWindow(vhwndLightLabEdit, d8, d8,
                   LwMax(Lw4DMMExternalToolUi200(hwnd, 80), dxp - d16),
                   LwMax(Lw4DMMExternalToolUi200(hwnd, 90), ypButtons - d16), fTrue);
        MoveWindow(GetDlgItem(hwnd, kidLightLabSave), xpButtons, ypButtons, dxpButton, dypButton, fTrue);
        MoveWindow(GetDlgItem(hwnd, kidLightLabCancel), xpButtons + dxpButton + d8, ypButtons, dxpButton, dypButton, fTrue);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kidLightLabSave && (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0))
        {
            if (FSaveLightLabText())
                Close4DMMNativeToolWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == kidLightLabCancel && (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0))
        {
            Close4DMMNativeToolWindow(hwnd); return 0;
        }
        break;
    case WM_APP + 0x4E:
        // Kauai normally owns Escape before a native tool window sees it.
        // appbwin.cpp forwards Escape here specifically for Light Lab so the
        // documented editor contract is: Enter/Escape saves, title-bar Close cancels.
        if (FSaveLightLabText())
            Close4DMMNativeToolWindow(hwnd);
        return 0;
    case WM_CLOSE:
        Close4DMMNativeToolWindow(hwnd); return 0;
    case WM_DESTROY:
        vhwndLightLab = hNil; vhwndLightLabEdit = hNil; vwprcLightLabEditPrev = pvNil;
        vpmvieLightLab = pvNil; return 0;
    }
    return DefWindowProcA(hwnd, wm, wParam, lParam);
}

static bool FOpenLightLab(PMVIE pmvie)
{
    if (pmvie == pvNil || !BWLD::FTrueColorMode() || !BWLD::FActorLightMode() || !MVIE::FTestLightMode())
    {
        MessageBoxA(vwig.hwndApp,
                    "Light Lab currently requires -c -a -l.",
                    "4DMM Light Lab", MB_OK | MB_ICONINFORMATION);
        return fFalse;
    }

    // v36: every Light Lab light must belong to an object.  There is no
    // fallback/unattached street light anymore.
    int32_t aridSelected = pmvie->AridSelected();
    if (aridSelected == aridNil || pmvie->Pscen() == pvNil)
    {
        MessageBoxA(vwig.hwndApp,
                    "Select an actor, prop, or 3D word before creating/editing a light.",
                    "4DMM Light Lab", MB_OK | MB_ICONINFORMATION);
        return fFalse;
    }

    pmvie->FSetSceneLightsEnabled(pmvie->Iscen(), fTrue, fFalse);
    {
        LIGHTLAB light;
        if (pmvie->FGetLightLabConfig(pmvie->Iscen(), aridSelected, &light))
        {
            vfLightLabEditingExisting = fTrue;
            vfLightLabEnabled = light.fEnabled;
            vfLightLabGenerateShadows = light.fGenerateShadows;
            vfLightLabAttachSelected = fTrue;
            vfLightLabAttachmentHideable = light.fAttachmentHideable;
            varidLightLab = aridSelected;
            vnLightLabIntensity = light.intensity;
            vflLightLabDiameter = light.diameter;
            vflLightLabEdgeGradient = light.edgeGradient;
            vflLightLabRange = light.range > 0.0f ? light.range : 500.0f;
            vnfrmLightLabSpawn = LwMax(1, light.nfrmSpawn);
            vnfrmLightLabDespawn = LwMax(0, light.nfrmDespawn);
            strcpy_s(vszLightLabShape, SIZEOF(vszLightLabShape), light.szShape);
        }
        else
        {
            vfLightLabEditingExisting = fFalse;
            vfLightLabEnabled = fTrue;
            vfLightLabGenerateShadows = fFalse;
            vfLightLabAttachSelected = fTrue;
            vfLightLabAttachmentHideable = fFalse;
            varidLightLab = aridSelected;
            vnLightLabIntensity = 100;
            vflLightLabDiameter = 24.0f;
            vflLightLabEdgeGradient = 4.0f;
            vflLightLabRange = 500.0f;
            vnfrmLightLabSpawn = pmvie->Pscen()->Nfrm() - pmvie->Pscen()->NfrmFirst() + 1;
            vnfrmLightLabDespawn = 0;
            strcpy_s(vszLightLabShape, SIZEOF(vszLightLabShape), "round");
        }
    }

    if (vhwndLightLab != hNil && IsWindow(vhwndLightLab))
    {
        UpdateLightLabEditorText();
        ShowWindow(vhwndLightLab, SW_SHOWNORMAL);
        SetForegroundWindow(vhwndLightLab);
        SetFocus(vhwndLightLabEdit);
        return fTrue;
    }

    vpmvieLightLab = pmvie;
    HINSTANCE hinst = GetModuleHandleA(pvNil);
    WNDCLASSA wc;
    ClearPb(&wc, SIZEOF(wc));
    if (!GetClassInfoA(hinst, kszLightLabWndClass, &wc))
    {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = LresultLightLabWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(pvNil, IDC_IBEAM);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kszLightLabWndClass;
        if (RegisterClassA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return fFalse;
    }

    vhwndLightLab = CreateWindowExA(
        WS_EX_TOOLWINDOW, kszLightLabWndClass, "Light Lab - Enter/Esc Saves, Close Cancels",
        WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 255,
        Hwnd4DMMNativeToolOwner(), hNil, hinst, pvNil);
    if (vhwndLightLab == hNil)
    {
        vpmvieLightLab = pvNil;
        return fFalse;
    }
    Scale4DMMExternalToolWindow200(vhwndLightLab);
    {
        RECT rcClient;
        if (GetClientRect(vhwndLightLab, &rcClient))
            SendMessage(vhwndLightLab, WM_SIZE, SIZE_RESTORED,
                        MAKELPARAM(rcClient.right - rcClient.left, rcClient.bottom - rcClient.top));
    }
    ShowWindow(vhwndLightLab, SW_SHOWNORMAL);
    UpdateWindow(vhwndLightLab);
    SetFocus(vhwndLightLabEdit);
    return fTrue;
}

bool MVIE::FOpenLightLabEditor(void)
{
    AssertThis(0);
    return FOpenLightLab(this);
}

// 4DMM Object Properties editor.  The UI deliberately mirrors Light Lab's
// simple editable property sheet while the data is persisted per scene/ARID in
// the same .3ct sidecar.
static const achar kszObjectPropertiesWndClass[] = "4DMMObjectPropertiesEditor";
static const int32_t kidObjectPropertiesEdit = 0x5131;
static const int32_t kidObjectPropertiesSave = 0x5132;
static const int32_t kidObjectPropertiesCancel = 0x5133;
static HWND vhwndObjectProperties = hNil;
static HWND vhwndObjectPropertiesEdit = hNil;
static WNDPROC vwprcObjectPropertiesEditPrev = pvNil;
static PMVIE vpmvieObjectProperties = pvNil;
static int32_t varidObjectProperties = aridNil;
static bool vfObjectPropertiesFlushOverlap = fFalse;
static bool vfObjectPropertiesCastShadows = fTrue;

static void UpdateObjectPropertiesEditorText(void)
{
    if (vhwndObjectPropertiesEdit == hNil)
        return;
    achar sz[128];
    sprintf_s(sz, SIZEOF(sz),
              "flush overlap %d\r\n"
              "shadow casting %d",
              vfObjectPropertiesFlushOverlap ? 1 : 0,
              vfObjectPropertiesCastShadows ? 1 : 0);
    SetWindowTextA(vhwndObjectPropertiesEdit, sz);
}

static bool FSaveObjectPropertiesText(void)
{
    if (vpmvieObjectProperties == pvNil || vhwndObjectPropertiesEdit == hNil ||
        varidObjectProperties == aridNil)
        return fFalse;

    int32_t cch = GetWindowTextLengthA(vhwndObjectPropertiesEdit);
    achar *psz = pvNil;
    if (!FAllocPv((void **)&psz, (cch + 1) * SIZEOF(achar), fmemClear, mprNormal))
        return fFalse;
    GetWindowTextA(vhwndObjectPropertiesEdit, psz, cch + 1);

    int nFlush = vfObjectPropertiesFlushOverlap ? 1 : 0;
    int nCast = vfObjectPropertiesCastShadows ? 1 : 0;
    bool fFlushSeen = fFalse;
    bool fCastSeen = fFalse;
    achar *pszCtx = pvNil;
    for (achar *pszLine = strtok_s(psz, "\r\n", &pszCtx); pszLine != pvNil;
         pszLine = strtok_s(pvNil, "\r\n", &pszCtx))
    {
        int n = 0;
        if (sscanf_s(pszLine, "flush overlap %d", &n) == 1)
        {
            nFlush = n;
            fFlushSeen = fTrue;
        }
        else if (sscanf_s(pszLine, "shadow casting %d", &n) == 1)
        {
            nCast = n;
            fCastSeen = fTrue;
        }
    }
    FreePpv((void **)&psz);

    if (!fFlushSeen || !fCastSeen || (nFlush != 0 && nFlush != 1) || (nCast != 0 && nCast != 1))
    {
        MessageBoxA(vhwndObjectProperties,
                    "Use: flush overlap 0/1, shadow casting 0/1.",
                    "Object Properties", MB_OK | MB_ICONERROR);
        UpdateObjectPropertiesEditorText();
        return fFalse;
    }

    if (vpmvieObjectProperties->Pscen() == pvNil ||
        vpmvieObjectProperties->Pscen()->PactrFromArid(varidObjectProperties) == pvNil)
    {
        MessageBoxA(vhwndObjectProperties,
                    "The selected object is no longer available in this scene.",
                    "Object Properties", MB_OK | MB_ICONERROR);
        return fFalse;
    }

    OBJECTPROPERTIES prop;
    ClearPb(&prop, SIZEOF(prop));
    prop.iscen = vpmvieObjectProperties->Iscen();
    prop.arid = varidObjectProperties;
    prop.fFlushOverlap = nFlush != 0;
    prop.fCastShadows = nCast != 0;
    if (!vpmvieObjectProperties->FSetObjectProperties(&prop, fTrue))
    {
        MessageBoxA(vhwndObjectProperties,
                    "Unable to save Object Properties.",
                    "Object Properties", MB_OK | MB_ICONERROR);
        return fFalse;
    }

    vfObjectPropertiesFlushOverlap = prop.fFlushOverlap;
    vfObjectPropertiesCastShadows = prop.fCastShadows;
    return fTrue;
}

static LRESULT CALLBACK LresultObjectPropertiesEditWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    if (wm == WM_KEYDOWN && wParam == VK_RETURN && GetAsyncKeyState(VK_SHIFT) >= 0)
    {
        HWND hwndParent = GetParent(hwnd);
        if (hwndParent != hNil)
            SendMessage(hwndParent, WM_COMMAND, MAKEWPARAM(kidObjectPropertiesSave, BN_CLICKED), (LPARAM)hwnd);
        return 0;
    }
    return vwprcObjectPropertiesEditPrev != pvNil ?
        CallWindowProc(vwprcObjectPropertiesEditPrev, hwnd, wm, wParam, lParam) :
        DefWindowProc(hwnd, wm, wParam, lParam);
}

static LRESULT CALLBACK LresultObjectPropertiesWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    switch (wm)
    {
    case WM_CREATE:
        vhwndObjectProperties = hwnd;
        vhwndObjectPropertiesEdit = CreateWindowExA(
            WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            0, 0, 0, 0, hwnd, (HMENU)kidObjectPropertiesEdit, GetModuleHandleA(pvNil), pvNil);
        if (vhwndObjectPropertiesEdit == hNil)
            return -1;
        vwprcObjectPropertiesEditPrev = (WNDPROC)SetWindowLongPtrA(
            vhwndObjectPropertiesEdit, GWLP_WNDPROC, (LONG_PTR)LresultObjectPropertiesEditWndProc);
        SetPropA(vhwndObjectPropertiesEdit, "3DMMExNativeTextInput", (HANDLE)1);
        CreateWindowExA(0, "BUTTON", "Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        0, 0, 0, 0, hwnd, (HMENU)kidObjectPropertiesSave, GetModuleHandleA(pvNil), pvNil);
        CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        0, 0, 0, 0, hwnd, (HMENU)kidObjectPropertiesCancel, GetModuleHandleA(pvNil), pvNil);
        SendMessageA(vhwndObjectPropertiesEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
        SendMessageA(GetDlgItem(hwnd, kidObjectPropertiesSave), WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
        SendMessageA(GetDlgItem(hwnd, kidObjectPropertiesCancel), WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
        UpdateObjectPropertiesEditorText();
        return 0;
    case WM_SIZE:
    {
        int32_t dxp = LOWORD(lParam), dyp = HIWORD(lParam);
        const int32_t d8 = Lw4DMMExternalToolUi200(hwnd, 8);
        const int32_t d16 = Lw4DMMExternalToolUi200(hwnd, 16);
        const int32_t dxpButton = Lw4DMMExternalToolUi200(hwnd, 72);
        const int32_t dypButton = Lw4DMMExternalToolUi200(hwnd, 24);
        const int32_t xpButtons = LwMax(d8, (dxp - (dxpButton * 2 + d8)) / 2);
        const int32_t ypButtons = LwMax(Lw4DMMExternalToolUi200(hwnd, 130), dyp - dypButton - d8);
        MoveWindow(vhwndObjectPropertiesEdit, d8, d8,
                   LwMax(Lw4DMMExternalToolUi200(hwnd, 80), dxp - d16),
                   LwMax(Lw4DMMExternalToolUi200(hwnd, 90), ypButtons - d16), fTrue);
        MoveWindow(GetDlgItem(hwnd, kidObjectPropertiesSave), xpButtons, ypButtons, dxpButton, dypButton, fTrue);
        MoveWindow(GetDlgItem(hwnd, kidObjectPropertiesCancel), xpButtons + dxpButton + d8, ypButtons,
                   dxpButton, dypButton, fTrue);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kidObjectPropertiesSave && (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0))
        {
            if (FSaveObjectPropertiesText())
                Close4DMMNativeToolWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == kidObjectPropertiesCancel && (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0))
        {
            Close4DMMNativeToolWindow(hwnd);
            return 0;
        }
        break;
    case WM_APP + 0x4E:
        if (FSaveObjectPropertiesText())
            Close4DMMNativeToolWindow(hwnd);
        return 0;
    case WM_CLOSE:
        Close4DMMNativeToolWindow(hwnd);
        return 0;
    case WM_DESTROY:
        vhwndObjectProperties = hNil;
        vhwndObjectPropertiesEdit = hNil;
        vwprcObjectPropertiesEditPrev = pvNil;
        vpmvieObjectProperties = pvNil;
        varidObjectProperties = aridNil;
        return 0;
    }
    return DefWindowProcA(hwnd, wm, wParam, lParam);
}

bool MVIE::FOpenObjectPropertiesEditor(int32_t arid)
{
    AssertThis(0);
    if (Pscen() == pvNil || arid == aridNil || Pscen()->PactrFromArid(arid) == pvNil)
        return fFalse;

    OBJECTPROPERTIES prop;
    FGetObjectProperties(Iscen(), arid, &prop);
    vfObjectPropertiesFlushOverlap = prop.fFlushOverlap;
    vfObjectPropertiesCastShadows = prop.fCastShadows;
    varidObjectProperties = arid;
    vpmvieObjectProperties = this;

    if (vhwndObjectProperties != hNil && IsWindow(vhwndObjectProperties))
    {
        UpdateObjectPropertiesEditorText();
        ShowWindow(vhwndObjectProperties, SW_SHOWNORMAL);
        SetForegroundWindow(vhwndObjectProperties);
        SetFocus(vhwndObjectPropertiesEdit);
        return fTrue;
    }

    HINSTANCE hinst = GetModuleHandleA(pvNil);
    WNDCLASSA wc;
    ClearPb(&wc, SIZEOF(wc));
    if (!GetClassInfoA(hinst, kszObjectPropertiesWndClass, &wc))
    {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = LresultObjectPropertiesWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(pvNil, IDC_IBEAM);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kszObjectPropertiesWndClass;
        if (RegisterClassA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return fFalse;
    }

    vhwndObjectProperties = CreateWindowExA(
        WS_EX_TOOLWINDOW, kszObjectPropertiesWndClass, "Object Properties - Enter/Esc Saves, Close Cancels",
        WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 255,
        Hwnd4DMMNativeToolOwner(), hNil, hinst, pvNil);
    if (vhwndObjectProperties == hNil)
    {
        vpmvieObjectProperties = pvNil;
        varidObjectProperties = aridNil;
        return fFalse;
    }
    Scale4DMMExternalToolWindow200(vhwndObjectProperties);
    {
        RECT rcClient;
        if (GetClientRect(vhwndObjectProperties, &rcClient))
            SendMessage(vhwndObjectProperties, WM_SIZE, SIZE_RESTORED,
                        MAKELPARAM(rcClient.right - rcClient.left, rcClient.bottom - rcClient.top));
    }
    ShowWindow(vhwndObjectProperties, SW_SHOWNORMAL);
    UpdateWindow(vhwndObjectProperties);
    SetFocus(vhwndObjectPropertiesEdit);
    return fTrue;
}

static void UpdateDepthMotionTweenEditorText(void)
{
    if (vpmvieDepthMotionTween == pvNil || vhwndDepthMotionTween == hNil ||
        vhwndDepthMotionTweenEdit == hNil || viscenDepthMotionTween < 0)
    {
        return;
    }

    STN stnText;
    vpmvieDepthMotionTween->GetDepthMotionTweenText(
        viscenDepthMotionTween, vitweenDepthMotionTween,
        vitweenDepthMotionTween == ivNil ? &vctweenDepthMotionTweenDefault : pvNil,
        &stnText);
    SetWindowTextA(vhwndDepthMotionTweenEdit, stnText.Psz());
    SetWindowTextA(vhwndDepthMotionTween, "Z Axis Cam Motion Tween");
}

static bool FSaveDepthMotionTweenEditorText(void)
{
    if (vpmvieDepthMotionTween == pvNil || vhwndDepthMotionTweenEdit == hNil ||
        viscenDepthMotionTween < 0)
    {
        return fFalse;
    }

    int32_t cch = GetWindowTextLengthA(vhwndDepthMotionTweenEdit);
    achar *psz = pvNil;
    if (!FAllocPv((void **)&psz, (cch + 1) * SIZEOF(achar), fmemClear, mprNormal))
        return fFalse;

    GetWindowTextA(vhwndDepthMotionTweenEdit, psz, cch + 1);
    bool fRet = vpmvieDepthMotionTween->FSetDepthMotionTweenText(
        viscenDepthMotionTween, vitweenDepthMotionTween, psz);
    FreePpv((void **)&psz);

    if (!fRet)
    {
        UpdateDepthMotionTweenEditorText();
        if (!vfDepthMotionTweenHandledError)
        {
            MessageBoxA(vhwndDepthMotionTween,
                        "Bad value(s). Reverting to last known good values.",
                        "3DMMEx Z-Axis Motion Tween", MB_OK | MB_ICONERROR);
        }
        SetFocus(vhwndDepthMotionTweenEdit);
        SendMessageA(vhwndDepthMotionTweenEdit, EM_SETSEL, 0, -1);
        return fFalse;
    }

    // Show the exact canonical values that were committed to the .3ct.
    UpdateDepthMotionTweenEditorText();
    SetFocus(vhwndDepthMotionTweenEdit);
    return fTrue;
}

static LRESULT CALLBACK LresultDepthMotionTweenWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    switch (wm)
    {
    case WM_CREATE:
        vhwndDepthMotionTween = hwnd;
        vhwndDepthMotionTweenEdit =
            CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_MULTILINE |
                                ES_AUTOVSCROLL | ES_WANTRETURN,
                            0, 0, 0, 0, hwnd, (HMENU)kidDepthMotionTweenEdit,
                            GetModuleHandleA(pvNil), pvNil);
        if (vhwndDepthMotionTweenEdit == hNil)
            return -1;

        // Kauai normally converts all key messages into its own command stream
        // instead of dispatching them to ordinary Win32 controls.  Mark this
        // editor so APPB::_DispatchEvt can pass its keyboard messages through.
        SetPropA(vhwndDepthMotionTweenEdit, "3DMMExNativeTextInput", (HANDLE)1);
        SendMessageA(vhwndDepthMotionTweenEdit, EM_SETREADONLY, fFalse, 0);

        CreateWindowExA(0, "BUTTON", "Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        0, 0, 0, 0, hwnd, (HMENU)kidDepthMotionTweenSave,
                        GetModuleHandleA(pvNil), pvNil);
        CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        0, 0, 0, 0, hwnd, (HMENU)kidDepthMotionTweenCancel,
                        GetModuleHandleA(pvNil), pvNil);

        SendMessageA(vhwndDepthMotionTweenEdit, WM_SETFONT,
                     (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
        SendMessageA(GetDlgItem(hwnd, kidDepthMotionTweenSave), WM_SETFONT,
                     (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
        SendMessageA(GetDlgItem(hwnd, kidDepthMotionTweenCancel), WM_SETFONT,
                     (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
        UpdateDepthMotionTweenEditorText();
        return 0;

    case WM_SIZE:
    {
        int32_t dxp = LOWORD(lParam);
        int32_t dyp = HIWORD(lParam);
        const int32_t d8 = Lw4DMMExternalToolUi200(hwnd, 8);
        const int32_t d16 = Lw4DMMExternalToolUi200(hwnd, 16);
        const int32_t dxpButton = Lw4DMMExternalToolUi200(hwnd, 72);
        const int32_t dypButton = Lw4DMMExternalToolUi200(hwnd, 24);
        const int32_t dxpButtons = dxpButton * 2 + d8;
        const int32_t xpButtons = LwMax(d8, (dxp - dxpButtons) / 2);
        // Anchor the buttons to the actual bottom of the final client area.
        // The old minimum Y could push controls below a compact client, while
        // the fixed edit-height cap wasted most of a taller one.  Let the text
        // editor simply consume all usable space above the buttons.
        const int32_t ypButtons = LwMax(d8, dyp - dypButton - d8);
        const int32_t dypEdit = LwMax(Lw4DMMExternalToolUi200(hwnd, 40), ypButtons - d16);

        MoveWindow(vhwndDepthMotionTweenEdit, d8, d8,
                   LwMax(Lw4DMMExternalToolUi200(hwnd, 40), dxp - d16),
                   dypEdit, fTrue);
        MoveWindow(GetDlgItem(hwnd, kidDepthMotionTweenSave), xpButtons, ypButtons,
                   dxpButton, dypButton, fTrue);
        MoveWindow(GetDlgItem(hwnd, kidDepthMotionTweenCancel), xpButtons + dxpButton + d8,
                   ypButtons, dxpButton, dypButton, fTrue);
        return 0;
    }

    case WM_COMMAND:
        // Accept both ordinary BN_CLICKED notifications and command-style
        // activation so mouse clicks and keyboard activation use one path.
        if (LOWORD(wParam) == kidDepthMotionTweenSave &&
            (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0))
        {
            // A successful Save is made unmistakable by closing the editor.
            // Invalid input remains open after restoring the last good values.
            if (FSaveDepthMotionTweenEditorText())
                Close4DMMNativeToolWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == kidDepthMotionTweenCancel &&
            (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0))
        {
            // Cancel discards only unsaved text.  The in-memory track and .3ct
            // remain exactly as they were before this edit session.
            Close4DMMNativeToolWindow(hwnd);
            return 0;
        }
        break;

    case WM_APP + 0x4F:
        if (vpmvieDepthMotionTween != pvNil && viscenDepthMotionTween >= 0)
        {
            bool fRet = vitweenDepthMotionTween == ivNil ||
                        vpmvieDepthMotionTween->FDeleteDepthMotionTween(
                            viscenDepthMotionTween, vitweenDepthMotionTween);
            if (fRet)
                Close4DMMNativeToolWindow(hwnd);
        }
        return 0;

    case WM_CLOSE:
        // The title-bar X has the same non-destructive semantics as Cancel.
        Close4DMMNativeToolWindow(hwnd);
        return 0;

    case WM_DESTROY:
        vhwndDepthMotionTween = hNil;
        vhwndDepthMotionTweenEdit = hNil;
        vpmvieDepthMotionTween = pvNil;
        viscenDepthMotionTween = ivNil;
        vitweenDepthMotionTween = ivNil;
        ClearPb(&vctweenDepthMotionTweenDefault, SIZEOF(vctweenDepthMotionTweenDefault));
        return 0;
    }

    return DefWindowProcA(hwnd, wm, wParam, lParam);
}


static void UpdateManualCameraFrameEditorText(void)
{
    if (vpmvieManualCameraFrame == pvNil || vhwndManualCameraFrame == hNil ||
        vhwndManualCameraFrameEdit == hNil || viscenManualCameraFrame < 0 ||
        vnfrmManualCameraFrame < 1)
    {
        return;
    }

    STN stnText;
    STN stnTitle;
    vpmvieManualCameraFrame->GetManualCameraFrameText(
        viscenManualCameraFrame, vnfrmManualCameraFrame, &stnText);
    SetWindowTextA(vhwndManualCameraFrameEdit, stnText.Psz());
    stnTitle.FFormatSz(PszLit("3DMMEx Manual Camera - Frame %d"), vnfrmManualCameraFrame);
    SetWindowTextA(vhwndManualCameraFrame, stnTitle.Psz());
}

static bool FSaveManualCameraFrameEditorText(void)
{
    if (vpmvieManualCameraFrame == pvNil || vhwndManualCameraFrameEdit == hNil ||
        viscenManualCameraFrame < 0 || vnfrmManualCameraFrame < 1)
    {
        return fFalse;
    }

    int32_t cch = GetWindowTextLengthA(vhwndManualCameraFrameEdit);
    achar *psz = pvNil;
    if (!FAllocPv((void **)&psz, (cch + 1) * SIZEOF(achar), fmemClear, mprNormal))
        return fFalse;

    GetWindowTextA(vhwndManualCameraFrameEdit, psz, cch + 1);
    bool fRet = vpmvieManualCameraFrame->FSetManualCameraFrameText(
        viscenManualCameraFrame, vnfrmManualCameraFrame, psz);
    FreePpv((void **)&psz);

    if (!fRet)
    {
        UpdateManualCameraFrameEditorText();
        MessageBoxA(vhwndManualCameraFrame,
                    "Bad value(s). Reverting to last known good values.",
                    "3DMMEx Manual Camera", MB_OK | MB_ICONERROR);
        SetFocus(vhwndManualCameraFrameEdit);
        SendMessageA(vhwndManualCameraFrameEdit, EM_SETSEL, 0, -1);
        return fFalse;
    }
    return fTrue;
}

static LRESULT CALLBACK LresultManualCameraFrameWndProc(HWND hwnd, UINT wm,
                                                         WPARAM wParam, LPARAM lParam)
{
    switch (wm)
    {
    case WM_CREATE:
        vhwndManualCameraFrame = hwnd;
        vhwndManualCameraFrameEdit =
            CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_MULTILINE |
                                ES_AUTOVSCROLL | ES_WANTRETURN,
                            0, 0, 0, 0, hwnd, (HMENU)kidManualCameraFrameEdit,
                            GetModuleHandleA(pvNil), pvNil);
        if (vhwndManualCameraFrameEdit == hNil)
            return -1;
        SetPropA(vhwndManualCameraFrameEdit, "3DMMExNativeTextInput", (HANDLE)1);
        SendMessageA(vhwndManualCameraFrameEdit, EM_SETREADONLY, fFalse, 0);
        CreateWindowExA(0, "BUTTON", "OK",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        0, 0, 0, 0, hwnd, (HMENU)kidManualCameraFrameOk,
                        GetModuleHandleA(pvNil), pvNil);
        CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        0, 0, 0, 0, hwnd, (HMENU)kidManualCameraFrameCancel,
                        GetModuleHandleA(pvNil), pvNil);
        SendMessageA(vhwndManualCameraFrameEdit, WM_SETFONT,
                     (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
        SendMessageA(GetDlgItem(hwnd, kidManualCameraFrameOk), WM_SETFONT,
                     (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
        SendMessageA(GetDlgItem(hwnd, kidManualCameraFrameCancel), WM_SETFONT,
                     (WPARAM)GetStockObject(DEFAULT_GUI_FONT), fTrue);
        UpdateManualCameraFrameEditorText();
        return 0;

    case WM_SIZE:
    {
        int32_t dxp = LOWORD(lParam);
        int32_t dyp = HIWORD(lParam);
        const int32_t d8 = Lw4DMMExternalToolUi200(hwnd, 8);
        const int32_t d16 = Lw4DMMExternalToolUi200(hwnd, 16);
        const int32_t dxpButton = Lw4DMMExternalToolUi200(hwnd, 72);
        const int32_t dypButton = Lw4DMMExternalToolUi200(hwnd, 24);
        const int32_t dxpButtons = dxpButton * 2 + d8;
        const int32_t xpButtons = LwMax(d8, (dxp - dxpButtons) / 2);
        // Keep the buttons inside the real client rectangle.  The previous
        // 102-authored-pixel minimum could place them below the bottom edge
        // after the 200%-equivalent shell was created.
        const int32_t ypButtons = LwMax(d8, dyp - dypButton - d8);
        MoveWindow(vhwndManualCameraFrameEdit, d8, d8,
                   LwMax(Lw4DMMExternalToolUi200(hwnd, 60), dxp - d16),
                   LwMax(Lw4DMMExternalToolUi200(hwnd, 40), ypButtons - d16), fTrue);
        MoveWindow(GetDlgItem(hwnd, kidManualCameraFrameOk), xpButtons, ypButtons,
                   dxpButton, dypButton, fTrue);
        MoveWindow(GetDlgItem(hwnd, kidManualCameraFrameCancel),
                   xpButtons + dxpButton + d8, ypButtons,
                   dxpButton, dypButton, fTrue);
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == kidManualCameraFrameOk &&
            (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0))
        {
            if (FSaveManualCameraFrameEditorText())
                Close4DMMNativeToolWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == kidManualCameraFrameCancel &&
            (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0))
        {
            Close4DMMNativeToolWindow(hwnd);
            return 0;
        }
        break;

    case WM_APP + 0x4F:
        if (vpmvieManualCameraFrame != pvNil && viscenManualCameraFrame >= 0 &&
            vnfrmManualCameraFrame >= 1 &&
            vpmvieManualCameraFrame->FDeleteManualCameraFrame(
                viscenManualCameraFrame, vnfrmManualCameraFrame))
        {
            Close4DMMNativeToolWindow(hwnd);
        }
        return 0;

    case WM_CLOSE:
        Close4DMMNativeToolWindow(hwnd);
        return 0;

    case WM_DESTROY:
        vhwndManualCameraFrame = hNil;
        vhwndManualCameraFrameEdit = hNil;
        vpmvieManualCameraFrame = pvNil;
        viscenManualCameraFrame = ivNil;
        vnfrmManualCameraFrame = ivNil;
        return 0;
    }
    return DefWindowProcA(hwnd, wm, wParam, lParam);
}

// Movie-load cache examination progress.  Deliberately bare: no caption,
// status line, scene/frame text, percentage, or other text is displayed.
static const achar kszMovieCacheProgressWndClass[] = "3DMMExMovieCacheProgress";
static int32_t vlwMovieCacheProgress = 0; // 0..1000

static LRESULT CALLBACK LresultMovieCacheProgressWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam)
{
    switch (wm)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        FillRect(hdc, &rc, (HBRUSH)(COLOR_BTNFACE + 1));

        RECT rcBar = {8, 8, rc.right - 8, rc.bottom - 8};
        FillRect(hdc, &rcBar, (HBRUSH)(COLOR_WINDOW + 1));
        FrameRect(hdc, &rcBar, (HBRUSH)(COLOR_WINDOWFRAME + 1));

        RECT rcFill = rcBar;
        InflateRect(&rcFill, -2, -2);
        int32_t dxpFillMac = LwMax(0, rcFill.right - rcFill.left);
        rcFill.right = rcFill.left + LwMul(dxpFillMac, LwMin(1000, LwMax(0, vlwMovieCacheProgress))) / 1000;
        if (rcFill.right > rcFill.left)
        {
            HBRUSH hbrProgress = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
            FillRect(hdc, &rcFill, hbrProgress);
            DeleteObject(hbrProgress);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    }

    return DefWindowProcA(hwnd, wm, wParam, lParam);
}

static HWND HwndCreateMovieCacheProgress(void)
{
    HINSTANCE hinst = GetModuleHandleA(pvNil);
    WNDCLASSA wc;
    ClearPb(&wc, SIZEOF(wc));
    wc.lpfnWndProc = LresultMovieCacheProgressWndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(pvNil, IDC_WAIT);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kszMovieCacheProgressWndClass;
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return hNil;

    const int32_t dxpClient = 300;
    const int32_t dypClient = 34;
    RECT rcWindow = {0, 0, dxpClient, dypClient};
    AdjustWindowRectEx(&rcWindow, WS_POPUP | WS_BORDER, fFalse, WS_EX_TOOLWINDOW);
    int32_t dxpWindow = rcWindow.right - rcWindow.left;
    int32_t dypWindow = rcWindow.bottom - rcWindow.top;

    RECT rcOwner;
    HWND hwndOwner = Hwnd4DMMNativeToolOwner();
    if (hwndOwner == hNil || !GetWindowRect(hwndOwner, &rcOwner))
    {
        rcOwner.left = 0;
        rcOwner.top = 0;
        rcOwner.right = GetSystemMetrics(SM_CXSCREEN);
        rcOwner.bottom = GetSystemMetrics(SM_CYSCREEN);
        hwndOwner = hNil;
    }

    int32_t xp = rcOwner.left + ((rcOwner.right - rcOwner.left) - dxpWindow) / 2;
    int32_t yp = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - dypWindow) / 2;

    vlwMovieCacheProgress = 0;
    HWND hwnd = CreateWindowExA(WS_EX_TOOLWINDOW,
                                kszMovieCacheProgressWndClass, "",
                                WS_POPUP | WS_BORDER,
                                xp, yp, dxpWindow, dypWindow,
                                hwndOwner, hNil, hinst, pvNil);
    if (hwnd != hNil)
    {
        Scale4DMMExternalToolWindow200(hwnd);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(hwnd);
    }
    return hwnd;
}

static void UpdateMovieCacheProgress(HWND hwnd, int32_t lwProgress)
{
    if (hwnd == hNil)
        return;

    vlwMovieCacheProgress = LwMin(1000, LwMax(0, lwProgress));
    InvalidateRect(hwnd, pvNil, fFalse);
    UpdateWindow(hwnd);
}
#endif // KAUAI_WIN32

//
// 3DMMv1.0: How many pixels from edge to warp cursor back to center
//
const int32_t kdpInset = 50;

//
// 3DMMv1.0: Mouse scaling factor when rotating
//
const int32_t krRotateScaleFactor = BR_SCALAR(0.001);

//
// 3DMMv1.0: Mouse scaling factor when sooner/latering.
//
const int32_t krSoonerScaleFactor = BR_SCALAR(0.05);

//
// 3DMMv1.0: Number of ticks to pass before scrolling a single pixel
//
#define kdtsScrolling 5

//
//
// 3DMMv1.0: UNDO object for scene related actions:  Ins, New, and Rem
//
//
typedef class MUNS *PMUNS;

#define MUNS_PAR MUNB

enum MUNST
{
    munstInsScen,
    munstRemScen,
    munstSetBkgd
};

#define kclsMUNS KLCONST4('M', 'U', 'N', 'S')
class MUNS : public MUNS_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    int32_t _iscen;
    TAG _tag;
    PSCEN _pscen;
    MUNST _munst;
    bool _fRemovedSceneDefaultLightingShaders;
    bool _fRemovedSceneLightLabCombineLegacy;
    MUNS(void)
    {
        _fRemovedSceneDefaultLightingShaders = fFalse;
        _fRemovedSceneLightLabCombineLegacy = fFalse;
    }

  public:
    static PMUNS PmunsNew(void);
    ~MUNS(void);

    void SetIscen(int32_t iscen)
    {
        _iscen = iscen;
    }
    void SetPscen(PSCEN pscen)
    {
        _pscen = pscen;
        _pscen->AddRef();
    }
    void SetMunst(MUNST munst)
    {
        _munst = munst;
    }
    void SetRemovedSceneDefaultLightingShaders(bool fEnable)
    {
        _fRemovedSceneDefaultLightingShaders = FPure(fEnable);
    }
    void SetRemovedSceneLightLabCombineLegacy(bool fEnable)
    {
        _fRemovedSceneLightLabCombineLegacy = FPure(fEnable);
    }
    void SetTag(PTAG ptag)
    {
        _tag = *ptag;
    }

    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};


// Camera-track-only undo object.  Camera edits do not need to serialize and
// replace the entire SCEN chunk; swapping the complete .3ct state is enough
// and keeps continuous camera work lightweight and deterministic.
typedef class MUNC *PMUNC;
#define MUNC_PAR MUNB
#define kclsMUNC KLCONST4('M', 'U', 'N', 'C')
class MUNC : public MUNC_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    CTSTATE *_pctstate;
    STN _stnUndoName;
    MUNC(void)
    {
        _pctstate = pvNil;
    }

  public:
    static PMUNC PmuncNew(void);
    ~MUNC(void);
    bool FSave(PMVIE pmvie);
    void SetUndoName(const achar *pszUndoName)
    {
        _stnUndoName.SetSz(pszUndoName);
    }
    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};

// Actor Studio pose undo.  The v120 implementation kept the previous
// GGCL/GLXF child CNOs, but CFL recycles orphan child chunks as soon as their
// last ACTN relation is removed.  The undo record therefore pointed at dead or
// re-used chunk numbers.  Store the actual before/after BODY-part matrices
// instead and replay the one part through the same copy-on-write writer with
// undo creation suppressed.  This makes the record independent of Chunky CNO
// lifetime and keeps a drag as one ordinary UHW operation.
typedef class ASUN *PASUN;
#define ASUN_PAR MUNB
#define kclsASUN KLCONST4('A', 'S', 'U', 'N')
class ASUN : public ASUN_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    int32_t _arid;
    int32_t _anid;
    int32_t _celn;
    int32_t _ipart;
    int32_t _selectionKind;
    int32_t _ipartSelectionFirst;
    int32_t _cpartSelection;
    CNO _cnoTmpl;
    CNO _cnoAction;
    BMAT34 _bmat34Before;
    BMAT34 _bmat34After;
    bool _fApplyBefore;

    ASUN(void)
    {
        _arid = aridNil;
        _anid = ivNil;
        _celn = ivNil;
        _ipart = ivNil;
        _selectionKind = ivNil;
        _ipartSelectionFirst = ivNil;
        _cpartSelection = 0;
        _cnoTmpl = cnoNil;
        _cnoAction = cnoNil;
        BrMatrix34Identity(&_bmat34Before);
        BrMatrix34Identity(&_bmat34After);
        _fApplyBefore = fTrue;
    }

  public:
    static PASUN PasunNew(void);
    void SetState(int32_t arid, int32_t anid, int32_t celn, int32_t ipart,
                  CNO cnoTmpl, CNO cnoAction,
                  const BMAT34 *pbmat34Before, const BMAT34 *pbmat34After,
                  int32_t selectionKind, int32_t ipartSelectionFirst,
                  int32_t cpartSelection)
    {
        _arid = arid;
        _anid = anid;
        _celn = celn;
        _ipart = ipart;
        _selectionKind = selectionKind;
        _ipartSelectionFirst = ipartSelectionFirst;
        _cpartSelection = cpartSelection;
        _cnoTmpl = cnoTmpl;
        _cnoAction = cnoAction;
        _bmat34Before = *pbmat34Before;
        _bmat34After = *pbmat34After;
        _fApplyBefore = fTrue;
    }
    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};


// Actor Studio temporal BODY-part presence undo. Cut/Spawn only change CPS
// model CHIDs from the current cel through the end of one custom action; BODY
// topology and matrices remain intact. Store exact before/after CHID arrays so
// mixed visibility ranges round-trip rather than assuming every future cel had
// the same presence state.
typedef class ASCU *PASCU;
#define ASCU_PAR MUNB
#define kclsASCU KLCONST4('A', 'S', 'C', 'U')
class ASCU : public ASCU_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    int32_t _arid;
    int32_t _anid;
    int32_t _celnFirst;
    int32_t _ipart;
    int32_t _cpartPresence;
    int32_t *_prgipart;
    int32_t _selectionKind;
    int32_t _ipartSelectionFirst;
    int32_t _cpartSelection;
    CNO _cnoTmpl;
    int32_t _cchid;
    int16_t *_prgchidBefore;
    int16_t *_prgchidAfter;
    bool _fApplyBefore;
    bool _fCutOperation;

    ASCU(void)
    {
        _arid = aridNil;
        _anid = ivNil;
        _celnFirst = ivNil;
        _ipart = ivNil;
        _cpartPresence = 0;
        _prgipart = pvNil;
        _selectionKind = kasskPart;
        _ipartSelectionFirst = ivNil;
        _cpartSelection = 0;
        _cnoTmpl = cnoNil;
        _cchid = 0;
        _prgchidBefore = pvNil;
        _prgchidAfter = pvNil;
        _fApplyBefore = fTrue;
        _fCutOperation = fTrue;
    }

  public:
    static PASCU PascuNew(void);
    ~ASCU(void);
    bool FSetState(int32_t arid, int32_t anid, int32_t celnFirst,
                   const int32_t *prgipart, int32_t cpartPresence, CNO cnoTmpl,
                   const int16_t *prgchidBefore, const int16_t *prgchidAfter,
                   int32_t cchid, bool fCutOperation, int32_t selectionKind,
                   int32_t ipartSelectionFirst, int32_t cpartSelection);
    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};


// Actor Studio topology undo for BODY Duplicate. The duplicate operation
// appends one independent BODY set; Undo removes that exact appended part and
// Redo replays the proven v135 Duplicate path without nesting another undo.
// This deliberately stores semantic inputs rather than transient Chunky child
// CNOs, which may be recycled after topology replacement.
typedef class ASDU *PASDU;
#define ASDU_PAR MUNB
#define kclsASDU KLCONST4('A', 'S', 'D', 'U')
class ASDU : public ASDU_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    int32_t _arid;
    int32_t _anid;
    int32_t _celn;
    int32_t _ipartSource;
    int32_t _ipartDuplicate;
    CNO _cnoTmpl;
    bool _fKeepPositions;
    bool _fHaveFreeze;
    bool _fDuplicatePresent;
    BMAT34 _bmat34Freeze;

    ASDU(void)
    {
        _arid = aridNil;
        _anid = ivNil;
        _celn = ivNil;
        _ipartSource = ivNil;
        _ipartDuplicate = ivNil;
        _cnoTmpl = cnoNil;
        _fKeepPositions = fTrue;
        _fHaveFreeze = fFalse;
        _fDuplicatePresent = fTrue;
        BrMatrix34Identity(&_bmat34Freeze);
    }

  public:
    static PASDU PasduNew(void);
    void SetState(int32_t arid, int32_t anid, int32_t celn, int32_t ipartSource,
                  int32_t ipartDuplicate, CNO cnoTmpl, bool fKeepPositions,
                  const BMAT34 *pbmat34Freeze)
    {
        _arid = arid;
        _anid = anid;
        _celn = celn;
        _ipartSource = ipartSource;
        _ipartDuplicate = ipartDuplicate;
        _cnoTmpl = cnoTmpl;
        _fKeepPositions = FPure(fKeepPositions);
        _fHaveFreeze = pbmat34Freeze != pvNil;
        if (_fHaveFreeze)
            _bmat34Freeze = *pbmat34Freeze;
        _fDuplicatePresent = fTrue;
    }
    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};


// One Undo record for hierarchy-aware Actor Studio Duplicate.  The selected
// Object/OG is still duplicated by replaying the proven v135 one-part core,
// but the contiguous result is treated as one topology transaction and one
// Undo entry.
typedef class ASRU *PASRU;
#define ASRU_PAR MUNB
#define kclsASRU KLCONST4('A', 'S', 'R', 'U')
class ASRU : public ASRU_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    int32_t _arid;
    int32_t _anid;
    int32_t _celn;
    int32_t _ipartSource;
    int32_t _cpartSource;
    int32_t _selectionKind;
    int32_t _ipartDuplicate;
    CNO _cnoTmpl;
    bool _fKeepPositions;
    bool _fDuplicatePresent;

    ASRU(void)
    {
        _arid = aridNil;
        _anid = ivNil;
        _celn = ivNil;
        _ipartSource = ivNil;
        _cpartSource = 0;
        _selectionKind = kasskPart;
        _ipartDuplicate = ivNil;
        _cnoTmpl = cnoNil;
        _fKeepPositions = fTrue;
        _fDuplicatePresent = fTrue;
    }

  public:
    static PASRU PasruNew(void);
    void SetState(int32_t arid, int32_t anid, int32_t celn, int32_t ipartSource,
                  int32_t cpartSource, int32_t selectionKind, int32_t ipartDuplicate,
                  CNO cnoTmpl, bool fKeepPositions)
    {
        _arid = arid;
        _anid = anid;
        _celn = celn;
        _ipartSource = ipartSource;
        _cpartSource = cpartSource;
        _selectionKind = selectionKind;
        _ipartDuplicate = ipartDuplicate;
        _cnoTmpl = cnoTmpl;
        _fKeepPositions = FPure(fKeepPositions);
        _fDuplicatePresent = fTrue;
    }
    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};

struct ASDEACTIONSNAPSHOT
{
    CNO cnoAction;
    PGG pggcel;
    PGL pglbmat34;
};

// Real topology snapshot for Actor Studio Delete.  Chunk CNOs cannot be kept
// as undo state because orphan Chunky children are recyclable; retain decoded
// GL/GG payloads and write fresh children when Undo restores the old shape.
typedef class ASDE *PASDE;
#define ASDE_PAR MUNB
#define kclsASDE KLCONST4('A', 'S', 'D', 'E')
class ASDE : public ASDE_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    int32_t _arid;
    int32_t _anid;
    int32_t _celn;
    int32_t _ipartFirst;
    int32_t _cpartDelete;
    int32_t _selectionKind;
    int32_t _idObject;
    CNO _cnoTmpl;
    PGL _pglibactPar;
    PGL _pglibset;
    PGG _pggCmid;
    ASDEACTIONSNAPSHOT *_prgAction;
    int32_t _cactn;
    CUSTOMPART *_prgMeta;
    int32_t _cmeta;
    bool _fDeletedPresent;

    ASDE(void)
    {
        _arid = aridNil;
        _anid = ivNil;
        _celn = ivNil;
        _ipartFirst = ivNil;
        _cpartDelete = 0;
        _selectionKind = kasskPart;
        _idObject = 0;
        _cnoTmpl = cnoNil;
        _pglibactPar = pvNil;
        _pglibset = pvNil;
        _pggCmid = pvNil;
        _prgAction = pvNil;
        _cactn = 0;
        _prgMeta = pvNil;
        _cmeta = 0;
        _fDeletedPresent = fTrue;
    }

  public:
    static PASDE PasdeNew(void);
    ~ASDE(void);
    bool FSave(PMVIE pmvie, PACTR pactr, int32_t anid, int32_t celn,
               int32_t ipartFirst, int32_t cpartDelete, int32_t selectionKind);
    bool FRestore(PACTR pactr);
    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};

// One Undo record for a virtual Actor Studio Object/OG transform. Keeping the
// constituent BODY-part matrices together avoids one Undo entry per child.
typedef class ASMU *PASMU;
#define ASMU_PAR MUNB
#define kclsASMU KLCONST4('A', 'S', 'M', 'U')
class ASMU : public ASMU_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    int32_t _arid;
    int32_t _anid;
    int32_t _celn;
    int32_t _cpart;
    int32_t _selectionKind;
    int32_t _ipartSelectionFirst;
    int32_t _cpartSelection;
    CNO _cnoTmpl;
    CNO _cnoAction;
    int32_t _rgipart[kc4DMMActorStudioFramePartMax];
    BMAT34 _rgbmat34Before[kc4DMMActorStudioFramePartMax];
    BMAT34 _rgbmat34After[kc4DMMActorStudioFramePartMax];
    bool _fApplyBefore;

    ASMU(void)
    {
        _arid = aridNil;
        _anid = ivNil;
        _celn = ivNil;
        _cpart = 0;
        _selectionKind = ivNil;
        _ipartSelectionFirst = ivNil;
        _cpartSelection = 0;
        _cnoTmpl = cnoNil;
        _cnoAction = cnoNil;
        _fApplyBefore = fTrue;
    }

  public:
    static PASMU PasmuNew(void);
    void SetState(int32_t arid, int32_t anid, int32_t celn, CNO cnoTmpl, CNO cnoAction,
                  const int32_t *prgipart, const BMAT34 *prgbmat34Before,
                  const BMAT34 *prgbmat34After, int32_t cpart,
                  int32_t selectionKind, int32_t ipartSelectionFirst,
                  int32_t cpartSelection)
    {
        AssertIn(cpart, 1, kc4DMMActorStudioFramePartMax + 1);
        _arid = arid;
        _anid = anid;
        _celn = celn;
        _cpart = cpart;
        _selectionKind = selectionKind;
        _ipartSelectionFirst = ipartSelectionFirst;
        _cpartSelection = cpartSelection;
        _cnoTmpl = cnoTmpl;
        _cnoAction = cnoAction;
        CopyPb(prgipart, _rgipart, LwMul(cpart, SIZEOF(int32_t)));
        CopyPb(prgbmat34Before, _rgbmat34Before, LwMul(cpart, SIZEOF(BMAT34)));
        CopyPb(prgbmat34After, _rgbmat34After, LwMul(cpart, SIZEOF(BMAT34)));
        _fApplyBefore = fTrue;
    }
    virtual bool FDo(PDOCB pdocb) override;
    virtual bool FUndo(PDOCB pdocb) override;
    virtual void GetUndoName(PSTN pstn) override;
};

//
//
//
// 3DMMv1.0:  BEGIN MOVIE
//
//
//

RTCLASS(MVIE)
RTCLASS(MUNB)
RTCLASS(MUNS)
RTCLASS(MUNC)
RTCLASS(ASUN)
RTCLASS(ASCU)
RTCLASS(ASDU)
RTCLASS(ASRU)
RTCLASS(ASDE)
RTCLASS(ASMU)

BEGIN_CMD_MAP(MVIE, CMH)
ON_CID_ME(cidAlarm, &MVIE::FCmdAlarm, pvNil)
ON_CID_ME(cidRender, &MVIE::FCmdRender, pvNil)
ON_CID_GEN(cidSaveAndClose, pvNil, pvNil)
END_CMD_MAP_NIL()

//
// 3DMMv1.0: A file written by this version of movie.cpp receives this cvn.  Any
// 3DMMv1.0: file with this cvn value has exactly the same file format
//
const int16_t kcvnCur = 2;

//
// 3DMMv1.0: A file written by this version of movie.cpp can be read by any version
// 3DMMv1.0: of movie.cpp whose kcvnCur is >= to this (this should be <= kcvnCur)
//
const int16_t kcvnBack = 2;

//
// 3DMMv1.0: A file whose cvn is less than kcvnMin cannot be directly read by
// 3DMMv1.0: this version of movie.cpp (maybe a converter will read it).
// 3DMMv1.0: (this should be <= kcvnCur)
//
const int16_t kcvnMin = 1;

//
// 3DMMv1.0: Movie file prefix
//
struct MFP
{
    int16_t bo;  // 3DMMv1.0: byte order
    int16_t osk; // 3DMMv1.0: which system wrote this
    DVER dver;   // 3DMMv1.0: chunky file version
};
const BOM kbomMfp = 0x55000000;

//
// 3DMMv1.0: Used to keep track of the roll call list of the movie
//

// 3DMMEx: On-disk representation of MACTR
struct MACTRF
{
    int32_t arid;
    int32_t cactRef;
    uint32_t grfbrws; // 3DMMv1.0: browser properties
    TAGF tagTmpl;
};
VERIFY_STRUCT_SIZE(MACTRF, 28)

struct MACTR
{
    int32_t arid;
    int32_t cactRef;
    uint32_t grfbrws; // 3DMMv1.0: browser properties
    TAG tagTmpl;
};

typedef MACTR *PMACTR;

const BOM kbomMactr = (0xFC000000 | (kbomTag >> 4));

/****************************************************
 *
 * Set/query the process-wide extended undo launch mode.
 *
 ****************************************************/
void MVIE::SetExtendedUndoMode(bool fEnable)
{
    vfExtendedUndoMode = FPure(fEnable);
}

bool MVIE::FExtendedUndoMode(void)
{
    return vfExtendedUndoMode;
}

void MVIE::SetMultiSelectMode(bool fEnable)
{
    vfMultiSelectMode = FPure(fEnable);
}

bool MVIE::FMultiSelectMode(void)
{
    return vfMultiSelectMode;
}

int32_t MVIE::CObjectGroupMembers(int32_t idGroup) const
{
    int32_t c = 0;
    for (int32_t i = 0; i < _cObjectGroupMember; i++)
        if (_rgObjectGroupMember[i].idGroup == idGroup)
            c++;
    return c;
}

const OBJECTGROUPMEMBER *MVIE::PObjectGroupMember(int32_t idGroup, int32_t iMember) const
{
    if (iMember < 0)
        return pvNil;
    int32_t iFound = 0;
    for (int32_t i = 0; i < _cObjectGroupMember; i++)
    {
        if (_rgObjectGroupMember[i].idGroup != idGroup)
            continue;
        if (iFound++ == iMember)
            return &_rgObjectGroupMember[i];
    }
    return pvNil;
}

bool MVIE::FObjectInObjectGroup(int32_t arid, int32_t *pidGroup) const
{
    if (pidGroup != pvNil)
        *pidGroup = 0;
    if (arid == aridNil)
        return fFalse;

    for (int32_t i = 0; i < _cObjectGroupMember; i++)
    {
        if (_rgObjectGroupMember[i].arid != arid)
            continue;
        const int32_t idGroup = _rgObjectGroupMember[i].idGroup;
        for (int32_t iGroup = 0; iGroup < _cObjectGroup; iGroup++)
        {
            if (_rgObjectGroup[iGroup].id != idGroup || _rgObjectGroup[iGroup].iscen != _iscen)
                continue;
            if (pidGroup != pvNil)
                *pidGroup = idGroup;
            return fTrue;
        }
    }
    return fFalse;
}

bool MVIE::FCanBindSelectedObjectGroup(void)
{
    AssertThis(0);
    if (!FMultiSelectMode() || _pscenOpen == pvNil)
        return fFalse;

    const int32_t cSelected = _pscenOpen->CactrSelected();
    if (cSelected < 2 || _cObjectGroup >= kcObjectGroupMax ||
        _cObjectGroupMember + cSelected > kcObjectGroupMemberMax)
    {
        return fFalse;
    }

    for (int32_t iSel = 0; iSel < cSelected; iSel++)
    {
        PACTR pactr = _pscenOpen->PactrSelectedAt(iSel);
        if (pactr == pvNil || FObjectInObjectGroup(pactr->Arid()))
            return fFalse;
    }
    return fTrue;
}

bool MVIE::FCanJoinObjectToGroup(int32_t idGroup, int32_t arid) const
{
    AssertThis(0);
    if (!FMultiSelectMode() || _pscenOpen == pvNil || idGroup <= 0 || arid == aridNil ||
        _cObjectGroupMember >= kcObjectGroupMemberMax || FObjectInObjectGroup(arid))
    {
        return fFalse;
    }

    bool fGroupHere = fFalse;
    for (int32_t i = 0; i < _cObjectGroup; i++)
    {
        if (_rgObjectGroup[i].id == idGroup && _rgObjectGroup[i].iscen == _iscen)
        {
            fGroupHere = fTrue;
            break;
        }
    }
    if (!fGroupHere)
        return fFalse;

    PACTR pactr = _pscenOpen->PactrFromArid(arid);
    return pactr != pvNil && pactr->FOnStage();
}

bool MVIE::FCanAbandonObjectFromGroup(int32_t idGroup, int32_t arid) const
{
    AssertThis(0);
    if (!FMultiSelectMode() || _pscenOpen == pvNil || idGroup <= 0 || arid == aridNil)
        return fFalse;

    bool fGroupHere = fFalse;
    for (int32_t i = 0; i < _cObjectGroup; i++)
    {
        if (_rgObjectGroup[i].id == idGroup && _rgObjectGroup[i].iscen == _iscen)
        {
            fGroupHere = fTrue;
            break;
        }
    }
    if (!fGroupHere)
        return fFalse;

    for (int32_t i = 0; i < _cObjectGroupMember; i++)
        if (_rgObjectGroupMember[i].idGroup == idGroup && _rgObjectGroupMember[i].arid == arid)
            return fTrue;
    return fFalse;
}

bool MVIE::FBindSelectedObjectGroup(int32_t *pidGroup)
{
    AssertThis(0);
    if (pidGroup != pvNil)
        *pidGroup = 0;
    if (!FCanBindSelectedObjectGroup())
        return fFalse;

    const int32_t cSelected = _pscenOpen->CactrSelected();

    // Keep membership unambiguous in the first grouping format: an object may
    // belong to one bound group at a time. Nested groups can be layered later.
    OBJECTGROUP group;
    ClearPb(&group, SIZEOF(group));
    group.id = _idObjectGroupNext++;
    group.iscen = _iscen;
    group.fLocked = fFalse;
    sprintf_s(group.szName, SIZEOF(group.szName), "Object Group %d", (int)group.id);

    const int32_t iMemberFirst = _cObjectGroupMember;
    for (int32_t iSel = 0; iSel < cSelected; iSel++)
    {
        PACTR pactr = _pscenOpen->PactrSelectedAt(iSel);
        OBJECTGROUPMEMBER member;
        ClearPb(&member, SIZEOF(member));
        member.idGroup = group.id;
        member.arid = pactr->Arid();
        pactr->GetObjectGroupPose(&member.xr, &member.yr, &member.zr, &member.bmat34);
        _rgObjectGroupMember[_cObjectGroupMember++] = member;
    }

    if (_cObjectGroupMember - iMemberFirst != cSelected)
    {
        _cObjectGroupMember = iMemberFirst;
        return fFalse;
    }

    _rgObjectGroup[_cObjectGroup++] = group;

    // Bind is the moment the assembly becomes one transform authority. Build
    // its runtime BRender parent immediately from the just-captured bind pose.
    if (!FBeginObjectGroupTransform(group.id))
    {
        _cObjectGroup--;
        _cObjectGroupMember = iMemberFirst;
        _idObjectGroupNext = group.id;
        return fFalse;
    }

    // Selected members immediately switch from ordinary yellow to grouped magenta.
    for (int32_t iSel = 0; iSel < _pscenOpen->CactrSelected(); iSel++)
    {
        PACTR pactr = _pscenOpen->PactrSelectedAt(iSel);
        if (pactr != pvNil)
            pactr->Hilite();
    }
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    if (pidGroup != pvNil)
        *pidGroup = group.id;
    DiagLog("object_group bind id=%ld scene=%ld members=%ld name=%s",
            (long)group.id, (long)group.iscen, (long)cSelected, group.szName);
    MultiLog(this, "group_bind id=%ld scene=%ld members=%ld name=%s",
             (long)group.id, (long)group.iscen, (long)cSelected, group.szName);
    for (int32_t iMember = iMemberFirst; iMember < _cObjectGroupMember; iMember++)
    {
        const OBJECTGROUPMEMBER &member = _rgObjectGroupMember[iMember];
        MultiLog(this,
                 "bind_member id=%ld arid=%ld pos=(%.6g,%.6g,%.6g) M=[%.6g %.6g %.6g; %.6g %.6g %.6g; %.6g %.6g %.6g]",
                 (long)group.id, (long)member.arid,
                 (double)BrScalarToFloat(member.xr), (double)BrScalarToFloat(member.yr),
                 (double)BrScalarToFloat(member.zr),
                 (double)BrScalarToFloat(member.bmat34.m[0][0]), (double)BrScalarToFloat(member.bmat34.m[0][1]),
                 (double)BrScalarToFloat(member.bmat34.m[0][2]), (double)BrScalarToFloat(member.bmat34.m[1][0]),
                 (double)BrScalarToFloat(member.bmat34.m[1][1]), (double)BrScalarToFloat(member.bmat34.m[1][2]),
                 (double)BrScalarToFloat(member.bmat34.m[2][0]), (double)BrScalarToFloat(member.bmat34.m[2][1]),
                 (double)BrScalarToFloat(member.bmat34.m[2][2]));
    }
    return fTrue;
}

bool MVIE::FUnbindObjectGroup(int32_t idGroup)
{
    AssertThis(0);
    int32_t iGroup = ivNil;
    for (int32_t i = 0; i < _cObjectGroup; i++)
        if (_rgObjectGroup[i].id == idGroup) { iGroup = i; break; }
    if (iGroup == ivNil)
        return fFalse;

    // Convert every BODY root back to normal world space before membership is
    // removed and the shared BRender parent is freed.
    _DestroyObjectGroupRenderParent(idGroup);

    int32_t iDst = 0;
    for (int32_t i = 0; i < _cObjectGroupMember; i++)
        if (_rgObjectGroupMember[i].idGroup != idGroup)
            _rgObjectGroupMember[iDst++] = _rgObjectGroupMember[i];
    _cObjectGroupMember = iDst;

    for (int32_t i = iGroup + 1; i < _cObjectGroup; i++)
        _rgObjectGroup[i - 1] = _rgObjectGroup[i];
    _cObjectGroup--;

    // v234: group membership is runtime hierarchy state; Object Properties are
    // scene/ARID state. Reapply the latter after every group teardown so a
    // non-default shadow policy can never be mistaken for object visibility.
    UpdateObjectShadowProperties();

    // Re-evaluate the per-object selection material after membership disappears.
    if (_pscenOpen != pvNil)
    {
        for (int32_t iSel = 0; iSel < _pscenOpen->CactrSelected(); iSel++)
        {
            PACTR pactr = _pscenOpen->PactrSelectedAt(iSel);
            if (pactr != pvNil)
                pactr->Hilite();
        }
    }
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    DiagLog("object_group unbind id=%ld", (long)idGroup);
    return fTrue;
}

bool MVIE::FGetObjectGroupState(OBJECTGROUPSTATE *pstate) const
{
    AssertThis(0);
    AssertVarMem(pstate);
    if (pstate == pvNil)
        return fFalse;

    ClearPb(pstate, SIZEOF(*pstate));
    pstate->cObjectGroup = _cObjectGroup;
    pstate->cObjectGroupMember = _cObjectGroupMember;
    pstate->idObjectGroupNext = _idObjectGroupNext;
    if (_cObjectGroup > 0)
        CopyPb(_rgObjectGroup, pstate->rgObjectGroup, _cObjectGroup * SIZEOF(OBJECTGROUP));
    if (_cObjectGroupMember > 0)
        CopyPb(_rgObjectGroupMember, pstate->rgObjectGroupMember,
               _cObjectGroupMember * SIZEOF(OBJECTGROUPMEMBER));

    // Runtime hierarchy state is never an undo authority.  A membership undo
    // restores only the logical group table, then reconstructs fresh BRender
    // parents and complete BODY-local transforms from the current actor state.
    for (int32_t i = 0; i < pstate->cObjectGroup; i++)
        pstate->rgObjectGroup[i].pbactParent = pvNil;
    for (int32_t i = 0; i < pstate->cObjectGroupMember; i++)
    {
        pstate->rgObjectGroupMember[i].fBodyLocalValid = fFalse;
        ClearPb(&pstate->rgObjectGroupMember[i].bmat34BodyLocal,
                SIZEOF(pstate->rgObjectGroupMember[i].bmat34BodyLocal));
    }
    return fTrue;
}

bool MVIE::FRenameObjectGroup(int32_t idGroup, PCSZ pszName)
{
    AssertThis(0);
    if (pszName == pvNil)
        return fFalse;

    while (*pszName == ChLit(' ') || *pszName == ChLit('\t'))
        pszName++;
    achar szName[kcchObjectGroupName];
    strncpy_s(szName, SIZEOF(szName), pszName, _TRUNCATE);
    int32_t cch = (int32_t)strlen(szName);
    while (cch > 0 && (szName[cch - 1] == ChLit(' ') || szName[cch - 1] == ChLit('\t') ||
                       szName[cch - 1] == ChLit('\r') || szName[cch - 1] == ChLit('\n')))
        szName[--cch] = 0;
    if (cch == 0)
        return fFalse;

    for (int32_t i = 0; i < _cObjectGroup; i++)
    {
        if (_rgObjectGroup[i].id != idGroup)
            continue;
        strcpy_s(_rgObjectGroup[i].szName, SIZEOF(_rgObjectGroup[i].szName), szName);
        SetDirty();
        DiagLog("object_group rename id=%ld name=%s", (long)idGroup, szName);
        return fTrue;
    }
    return fFalse;
}

bool MVIE::FSelectObjectGroup(int32_t idGroup)
{
    AssertThis(0);
    if (_pscenOpen == pvNil)
        return fFalse;

    const OBJECTGROUP *pgroup = pvNil;
    for (int32_t i = 0; i < _cObjectGroup; i++)
        if (_rgObjectGroup[i].id == idGroup) { pgroup = &_rgObjectGroup[i]; break; }
    if (pgroup == pvNil || pgroup->iscen != _iscen)
        return fFalse;

    bool fFirst = fTrue;
    for (int32_t i = 0; i < _cObjectGroupMember; i++)
    {
        if (_rgObjectGroupMember[i].idGroup != idGroup)
            continue;
        PACTR pactr = _pscenOpen->PactrFromArid(_rgObjectGroupMember[i].arid);
        if (pactr == pvNil || !pactr->FOnStage())
            continue;
        if (fFirst)
        {
            _pscenOpen->SelectActr(pactr);
            fFirst = fFalse;
        }
        else
            _pscenOpen->SelectActrAdd(pactr);
    }
    return !fFirst;
}

bool MVIE::_FEnsureObjectGroupRenderParent(int32_t idGroup, const BMAT34 *pbmat34Parent)
{
    AssertThis(0);
    AssertVarMem(pbmat34Parent);
    if (_pscenOpen == pvNil || _pbwld == pvNil || idGroup <= 0)
        return fFalse;

    OBJECTGROUP *pgroup = pvNil;
    for (int32_t i = 0; i < _cObjectGroup; i++)
    {
        if (_rgObjectGroup[i].id == idGroup && _rgObjectGroup[i].iscen == _iscen)
        {
            pgroup = &_rgObjectGroup[i];
            break;
        }
    }
    if (pgroup == pvNil)
        return fFalse;

    // If this group already owns a parent, first put every member back into
    // world space. This lets us rebase the parent from the current .3mm actor
    // state without changing the visible pose, which is important after
    // Undo/Redo and after timeline evaluation.
    if (pgroup->pbactParent != pvNil)
    {
        for (int32_t i = 0; i < _cObjectGroupMember; i++)
        {
            if (_rgObjectGroupMember[i].idGroup != idGroup)
                continue;
            PACTR pactr = _pscenOpen->PactrFromArid(_rgObjectGroupMember[i].arid);
            if (pactr != pvNil && pactr->Pbody() != pvNil)
                Detach4DMMBodyFromObjectGroupParent(pactr->Pbody());
        }
    }
    else
    {
        PBACT pbactParent = BrActorAllocate(BR_ACTOR_NONE, pvNil);
        if (pbactParent == pvNil)
            return fFalse;
        pbactParent->type = BR_ACTOR_NONE;
        pbactParent->t.type = BR_TRANSFORM_MATRIX34;
        BrMatrix34Identity(&pbactParent->t.t.mat);
        pbactParent->identifier = pvNil;
        pbactParent->user = pbactParent; // self-pointer marks a 4DMM group parent
        pgroup->pbactParent = pbactParent;
        _pbwld->AddActor(pbactParent);
    }

    PBACT pbactParent = pgroup->pbactParent;
    if (pbactParent->prev == pvNil)
        _pbwld->AddActor(pbactParent);
    BrMatrix34Copy(&pbactParent->t.t.mat, pbmat34Parent);
    pbactParent->t.type = BR_TRANSFORM_MATRIX34;

    int32_t cAttached = 0;
    for (int32_t i = 0; i < _cObjectGroupMember; i++)
    {
        if (_rgObjectGroupMember[i].idGroup != idGroup)
            continue;
        PACTR pactr = _pscenOpen->PactrFromArid(_rgObjectGroupMember[i].arid);
        if (pactr == pvNil || pactr->Pbody() == pvNil)
            continue;
        if (!F4DMMBodyAttachToObjectGroupParent(pactr->Pbody(), pbactParent))
            return fFalse;
        cAttached++;
    }

    _pbwld->MarkDirty();
    MultiLog(this, "group_parent_sync id=%ld parent=%p members=%ld P=[%.6g %.6g %.6g; %.6g %.6g %.6g; %.6g %.6g %.6g; %.6g %.6g %.6g]",
             (long)idGroup, pbactParent, (long)cAttached,
             (double)BrScalarToFloat(pbactParent->t.t.mat.m[0][0]),
             (double)BrScalarToFloat(pbactParent->t.t.mat.m[0][1]),
             (double)BrScalarToFloat(pbactParent->t.t.mat.m[0][2]),
             (double)BrScalarToFloat(pbactParent->t.t.mat.m[1][0]),
             (double)BrScalarToFloat(pbactParent->t.t.mat.m[1][1]),
             (double)BrScalarToFloat(pbactParent->t.t.mat.m[1][2]),
             (double)BrScalarToFloat(pbactParent->t.t.mat.m[2][0]),
             (double)BrScalarToFloat(pbactParent->t.t.mat.m[2][1]),
             (double)BrScalarToFloat(pbactParent->t.t.mat.m[2][2]),
             (double)BrScalarToFloat(pbactParent->t.t.mat.m[3][0]),
             (double)BrScalarToFloat(pbactParent->t.t.mat.m[3][1]),
             (double)BrScalarToFloat(pbactParent->t.t.mat.m[3][2]));
    return cAttached >= 2;
}

void MVIE::_DestroyObjectGroupRenderParent(int32_t idGroup)
{
    AssertThis(0);
    OBJECTGROUP *pgroup = pvNil;
    for (int32_t i = 0; i < _cObjectGroup; i++)
    {
        if (_rgObjectGroup[i].id == idGroup)
        {
            pgroup = &_rgObjectGroup[i];
            break;
        }
    }
    if (pgroup == pvNil || pgroup->pbactParent == pvNil)
        return;

    PBACT pbactParent = pgroup->pbactParent;
    if (_pscenOpen != pvNil)
    {
        for (int32_t i = 0; i < _cObjectGroupMember; i++)
        {
            if (_rgObjectGroupMember[i].idGroup != idGroup)
                continue;
            PACTR pactr = _pscenOpen->PactrFromArid(_rgObjectGroupMember[i].arid);
            if (pactr != pvNil && pactr->Pbody() != pvNil)
                Detach4DMMBodyFromObjectGroupParent(pactr->Pbody());
        }
    }

    // Every group child should have been detached through BODY so its local
    // matrix was converted back to world space before the parent disappears.
    // Refuse to recursively free an unexpected BODY tree.
    if (pbactParent->children != pvNil)
    {
        DiagLog("object_group parent teardown retained children id=%ld parent=%p; leaving parent allocated",
                (long)idGroup, pbactParent);
        return;
    }

    if (pbactParent->prev != pvNil)
        BrActorRemove(pbactParent);
    BrActorFree(pbactParent);
    pgroup->pbactParent = pvNil;

    // The frozen child matrix belongs to the live BODY instances that were
    // just detached. Scene close/reopen and undo/redo can materialize new
    // BODYs, so force the next parent rebuild to recapture their complete
    // local matrices from the current .3mm actor state.
    for (int32_t i = 0; i < _cObjectGroupMember; i++)
    {
        if (_rgObjectGroupMember[i].idGroup != idGroup)
            continue;
        _rgObjectGroupMember[i].fBodyLocalValid = fFalse;
        ClearPb(&_rgObjectGroupMember[i].bmat34BodyLocal,
                SIZEOF(_rgObjectGroupMember[i].bmat34BodyLocal));
    }

    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
}

void MVIE::_DestroyObjectGroupRenderParents(void)
{
    AssertThis(0);
    for (int32_t i = 0; i < _cObjectGroup; i++)
    {
        if (_rgObjectGroup[i].pbactParent != pvNil)
            _DestroyObjectGroupRenderParent(_rgObjectGroup[i].id);
    }
}

void MVIE::_RebuildObjectGroupRenderParentsForCurrentScene(void)
{
    AssertThis(0);
    if (_pscenOpen == pvNil || _iscen == ivNil)
        return;

    // Repair stale sidecar membership before rebuilding runtime parents. A
    // pre-v132 delete could remove an ACTR while leaving its ARID in an Object
    // Group; selecting that dangling group later could repeatedly feed stale
    // membership into group operations. Members whose ACTR no longer exists cannot
    // be meaningful group state, and a group with fewer than two surviving
    // members is no longer a group at all.
    for (int32_t iGroup = 0; iGroup < _cObjectGroup;)
    {
        if (_rgObjectGroup[iGroup].iscen != _iscen)
        {
            ++iGroup;
            continue;
        }

        const int32_t idGroup = _rgObjectGroup[iGroup].id;
        int32_t iMemberDst = 0;
        int32_t cLive = 0;
        int32_t cRemoved = 0;
        for (int32_t iMember = 0; iMember < _cObjectGroupMember; ++iMember)
        {
            const OBJECTGROUPMEMBER &member = _rgObjectGroupMember[iMember];
            if (member.idGroup == idGroup && _pscenOpen->PactrFromArid(member.arid) == pvNil)
            {
                ++cRemoved;
                continue;
            }
            if (member.idGroup == idGroup)
                ++cLive;
            _rgObjectGroupMember[iMemberDst++] = member;
        }
        if (cRemoved > 0)
        {
            _cObjectGroupMember = iMemberDst;
            SetDirty();
            DiagLog("object_group repair id=%ld scene=%ld removed_missing_members=%ld live=%ld",
                    (long)idGroup, (long)_iscen, (long)cRemoved, (long)cLive);
        }

        if (cLive < 2)
        {
            DiagLog("object_group repair removing invalid group id=%ld scene=%ld live=%ld",
                    (long)idGroup, (long)_iscen, (long)cLive);
            FUnbindObjectGroup(idGroup);
            continue;
        }

        // FBeginObjectGroupTransform reconstructs the one parent frame from
        // the immutable bind pose and the current .3mm actor state, then
        // rebases all BODY roots beneath it without changing apparent world
        // pose.
        FBeginObjectGroupTransform(idGroup);
        ++iGroup;
    }
}


static void _NormalizeObjectGroupRotation(BMAT34 *pbmat34)
{
    AssertVarMem(pbmat34);

    double z0 = BrScalarToFloat(pbmat34->m[2][0]);
    double z1 = BrScalarToFloat(pbmat34->m[2][1]);
    double z2 = BrScalarToFloat(pbmat34->m[2][2]);
    double y0 = BrScalarToFloat(pbmat34->m[1][0]);
    double y1 = BrScalarToFloat(pbmat34->m[1][1]);
    double y2 = BrScalarToFloat(pbmat34->m[1][2]);

    double rz = sqrt(z0 * z0 + z1 * z1 + z2 * z2);
    if (rz <= 0.0000001)
    {
        BrMatrix34Identity(pbmat34);
        return;
    }
    z0 /= rz; z1 /= rz; z2 /= rz;

    double x0 = y1 * z2 - y2 * z1;
    double x1 = y2 * z0 - y0 * z2;
    double x2 = y0 * z1 - y1 * z0;
    double rx = sqrt(x0 * x0 + x1 * x1 + x2 * x2);
    if (rx <= 0.0000001)
    {
        BrMatrix34Identity(pbmat34);
        return;
    }
    x0 /= rx; x1 /= rx; x2 /= rx;

    y0 = z1 * x2 - z2 * x1;
    y1 = z2 * x0 - z0 * x2;
    y2 = z0 * x1 - z1 * x0;

    pbmat34->m[0][0] = BrFloatToScalar((float)x0);
    pbmat34->m[0][1] = BrFloatToScalar((float)x1);
    pbmat34->m[0][2] = BrFloatToScalar((float)x2);
    pbmat34->m[1][0] = BrFloatToScalar((float)y0);
    pbmat34->m[1][1] = BrFloatToScalar((float)y1);
    pbmat34->m[1][2] = BrFloatToScalar((float)y2);
    pbmat34->m[2][0] = BrFloatToScalar((float)z0);
    pbmat34->m[2][1] = BrFloatToScalar((float)z1);
    pbmat34->m[2][2] = BrFloatToScalar((float)z2);
    pbmat34->m[3][0] = rZero;
    pbmat34->m[3][1] = rZero;
    pbmat34->m[3][2] = rZero;
}


struct OBJECTGROUPHIGHPRECISION
{
    PMVIE pmvie;
    int32_t idGroup;
    bool fValid;
    double rgdRot[3][3];
    double rgdPivot[3];
    double rgdBindPivot[3];
    // Cumulative Object Group scale in the group's own bind-space axes.
    // Member BODYs still own their actual geometry scale; this vector keeps
    // member spacing coherent when a resized group is subsequently rotated or
    // normalized. It is reconstructed from live member positions in FBegin.
    double rgdScale[3];
};

static OBJECTGROUPHIGHPRECISION vObjectGroupHighPrecision = { pvNil, 0, fFalse };

bool MVIE::FSwapObjectGroupState(OBJECTGROUPSTATE *pstate)
{
    AssertThis(0);
    AssertVarMem(pstate);
    if (pstate == pvNil || pstate->cObjectGroup < 0 || pstate->cObjectGroup > kcObjectGroupMax ||
        pstate->cObjectGroupMember < 0 || pstate->cObjectGroupMember > kcObjectGroupMemberMax)
    {
        return fFalse;
    }

    OBJECTGROUPSTATE *pstateCur = pvNil;
    if (!FAllocPv((void **)&pstateCur, SIZEOF(*pstateCur), fmemClear, mprNormal))
        return fFalse;
    if (!FGetObjectGroupState(pstateCur))
    {
        FreePpv((void **)&pstateCur);
        return fFalse;
    }

    // Tear down the old runtime hierarchy while its logical table is still
    // authoritative, then install only the serialized/logical part of the
    // requested snapshot. Runtime parents/BODY locals are rebuilt below.
    _DestroyObjectGroupRenderParents();
    ClearPb(_rgObjectGroup, SIZEOF(_rgObjectGroup));
    ClearPb(_rgObjectGroupMember, SIZEOF(_rgObjectGroupMember));
    _cObjectGroup = pstate->cObjectGroup;
    _cObjectGroupMember = pstate->cObjectGroupMember;
    _idObjectGroupNext = pstate->idObjectGroupNext;
    if (_cObjectGroup > 0)
        CopyPb(pstate->rgObjectGroup, _rgObjectGroup, _cObjectGroup * SIZEOF(OBJECTGROUP));
    if (_cObjectGroupMember > 0)
        CopyPb(pstate->rgObjectGroupMember, _rgObjectGroupMember,
               _cObjectGroupMember * SIZEOF(OBJECTGROUPMEMBER));
    for (int32_t i = 0; i < _cObjectGroup; i++)
        _rgObjectGroup[i].pbactParent = pvNil;
    for (int32_t i = 0; i < _cObjectGroupMember; i++)
    {
        _rgObjectGroupMember[i].fBodyLocalValid = fFalse;
        ClearPb(&_rgObjectGroupMember[i].bmat34BodyLocal,
                SIZEOF(_rgObjectGroupMember[i].bmat34BodyLocal));
    }

    vObjectGroupHighPrecision.pmvie = pvNil;
    vObjectGroupHighPrecision.idGroup = 0;
    vObjectGroupHighPrecision.fValid = fFalse;

    bool fRebuilt = fTrue;
    if (_pscenOpen != pvNil && _iscen != ivNil)
    {
        for (int32_t i = 0; i < _cObjectGroup; i++)
        {
            if (_rgObjectGroup[i].iscen == _iscen && !FBeginObjectGroupTransform(_rgObjectGroup[i].id))
            {
                fRebuilt = fFalse;
                break;
            }
        }
    }

    if (!fRebuilt)
    {
        // Undo/redo must be atomic. If rebuilding the requested hierarchy
        // fails, restore the state that was live before this swap and rebuild
        // its runtime parents instead of leaving half of either membership set.
        _DestroyObjectGroupRenderParents();
        ClearPb(_rgObjectGroup, SIZEOF(_rgObjectGroup));
        ClearPb(_rgObjectGroupMember, SIZEOF(_rgObjectGroupMember));
        _cObjectGroup = pstateCur->cObjectGroup;
        _cObjectGroupMember = pstateCur->cObjectGroupMember;
        _idObjectGroupNext = pstateCur->idObjectGroupNext;
        if (_cObjectGroup > 0)
            CopyPb(pstateCur->rgObjectGroup, _rgObjectGroup, _cObjectGroup * SIZEOF(OBJECTGROUP));
        if (_cObjectGroupMember > 0)
            CopyPb(pstateCur->rgObjectGroupMember, _rgObjectGroupMember,
                   _cObjectGroupMember * SIZEOF(OBJECTGROUPMEMBER));
        for (int32_t i = 0; i < _cObjectGroup; i++)
            _rgObjectGroup[i].pbactParent = pvNil;
        for (int32_t i = 0; i < _cObjectGroupMember; i++)
        {
            _rgObjectGroupMember[i].fBodyLocalValid = fFalse;
            ClearPb(&_rgObjectGroupMember[i].bmat34BodyLocal,
                    SIZEOF(_rgObjectGroupMember[i].bmat34BodyLocal));
        }
        vObjectGroupHighPrecision.pmvie = pvNil;
        vObjectGroupHighPrecision.idGroup = 0;
        vObjectGroupHighPrecision.fValid = fFalse;
        _RebuildObjectGroupRenderParentsForCurrentScene();
        FreePpv((void **)&pstateCur);
        return fFalse;
    }

    // The undo object now owns the state that was live before this operation;
    // repeating the same swap therefore performs redo with no second format.
    CopyPb(pstateCur, pstate, SIZEOF(*pstate));
    FreePpv((void **)&pstateCur);

    if (_pscenOpen != pvNil)
    {
        for (int32_t iSel = 0; iSel < _pscenOpen->CactrSelected(); iSel++)
        {
            PACTR pactr = _pscenOpen->PactrSelectedAt(iSel);
            if (pactr != pvNil)
                pactr->Hilite();
        }
    }
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    MultiLog(this, "group_state_swap groups=%ld members=%ld next=%ld",
             (long)_cObjectGroup, (long)_cObjectGroupMember, (long)_idObjectGroupNext);
    return fTrue;
}

static double _ObjectGroupBrsToDouble(BRS v)
{
    return (double)v / (double)rOne;
}

static void _ObjectGroupMatrixFromBmat(const BMAT34 *pbmat34, double rgd[3][3])
{
    for (int32_t i = 0; i < 3; i++)
        for (int32_t j = 0; j < 3; j++)
            rgd[i][j] = _ObjectGroupBrsToDouble(pbmat34->m[i][j]);
}

static void _ObjectGroupMatrixNormalize(double rgd[3][3])
{
    double z0 = rgd[2][0], z1 = rgd[2][1], z2 = rgd[2][2];
    double y0 = rgd[1][0], y1 = rgd[1][1], y2 = rgd[1][2];
    double rz = sqrt(z0 * z0 + z1 * z1 + z2 * z2);
    if (rz <= 0.000000000001)
    {
        ClearPb(rgd, sizeof(double) * 9);
        rgd[0][0] = rgd[1][1] = rgd[2][2] = 1.0;
        return;
    }
    z0 /= rz; z1 /= rz; z2 /= rz;

    double x0 = y1 * z2 - y2 * z1;
    double x1 = y2 * z0 - y0 * z2;
    double x2 = y0 * z1 - y1 * z0;
    double rx = sqrt(x0 * x0 + x1 * x1 + x2 * x2);
    if (rx <= 0.000000000001)
    {
        ClearPb(rgd, sizeof(double) * 9);
        rgd[0][0] = rgd[1][1] = rgd[2][2] = 1.0;
        return;
    }
    x0 /= rx; x1 /= rx; x2 /= rx;

    y0 = z1 * x2 - z2 * x1;
    y1 = z2 * x0 - z0 * x2;
    y2 = z0 * x1 - z1 * x0;

    rgd[0][0] = x0; rgd[0][1] = x1; rgd[0][2] = x2;
    rgd[1][0] = y0; rgd[1][1] = y1; rgd[1][2] = y2;
    rgd[2][0] = z0; rgd[2][1] = z1; rgd[2][2] = z2;
}

static void _ObjectGroupMatrixMul(const double a[3][3], const double b[3][3], double out[3][3])
{
    double tmp[3][3];
    for (int32_t i = 0; i < 3; i++)
        for (int32_t j = 0; j < 3; j++)
            tmp[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
    CopyPb(tmp, out, sizeof(tmp));
}

static void _ObjectGroupMatrixTranspose(const double in[3][3], double out[3][3])
{
    double tmp[3][3];
    for (int32_t i = 0; i < 3; i++)
        for (int32_t j = 0; j < 3; j++)
            tmp[i][j] = in[j][i];
    CopyPb(tmp, out, sizeof(tmp));
}

static double _ObjectGroupAngleRadians(BRA angle)
{
    int32_t nAngle = (int32_t)angle;
    if (nAngle > 32767)
        nAngle -= 65536;
    else if (nAngle < -32768)
        nAngle += 65536;
    return ((double)nAngle) * (6.283185307179586476925286766559 / 65536.0);
}

static void _ObjectGroupPostRotate(double rgd[3][3], BRA xa, BRA ya, BRA za)
{
    double r[3][3];
    double tmp[3][3];
    if (xa != aZero)
    {
        const double a = _ObjectGroupAngleRadians(xa), c = cos(a), sn = sin(a);
        ClearPb(r, sizeof(r));
        r[0][0] = 1.0; r[1][1] = c; r[1][2] = sn; r[2][1] = -sn; r[2][2] = c;
        _ObjectGroupMatrixMul(rgd, r, tmp); CopyPb(tmp, rgd, sizeof(tmp));
    }
    if (ya != aZero)
    {
        const double a = _ObjectGroupAngleRadians(ya), c = cos(a), sn = sin(a);
        ClearPb(r, sizeof(r));
        r[0][0] = c; r[0][2] = -sn; r[1][1] = 1.0; r[2][0] = sn; r[2][2] = c;
        _ObjectGroupMatrixMul(rgd, r, tmp); CopyPb(tmp, rgd, sizeof(tmp));
    }
    if (za != aZero)
    {
        const double a = _ObjectGroupAngleRadians(za), c = cos(a), sn = sin(a);
        ClearPb(r, sizeof(r));
        r[0][0] = c; r[0][1] = sn; r[1][0] = -sn; r[1][1] = c; r[2][2] = 1.0;
        _ObjectGroupMatrixMul(rgd, r, tmp); CopyPb(tmp, rgd, sizeof(tmp));
    }
    _ObjectGroupMatrixNormalize(rgd);
}

static void _ObjectGroupApplyVector(const double v[3], const double m[3][3], double out[3])
{
    out[0] = v[0] * m[0][0] + v[1] * m[1][0] + v[2] * m[2][0];
    out[1] = v[0] * m[0][1] + v[1] * m[1][1] + v[2] * m[2][1];
    out[2] = v[0] * m[0][2] + v[1] * m[1][2] + v[2] * m[2][2];
}

static BRS _ObjectGroupDoubleToBrs(double v)
{
    const double d = v * (double)rOne;
    return (BRS)(d >= 0.0 ? d + 0.5 : d - 0.5);
}

static void _ObjectGroupMatrixToBmat(const double rgd[3][3], BMAT34 *pbmat34)
{
    BrMatrix34Identity(pbmat34);
    for (int32_t i = 0; i < 3; i++)
        for (int32_t j = 0; j < 3; j++)
            pbmat34->m[i][j] = _ObjectGroupDoubleToBrs(rgd[i][j]);
    pbmat34->m[3][0] = pbmat34->m[3][1] = pbmat34->m[3][2] = rZero;
}

static void _ObjectGroupBindLocalMatrix(const OBJECTGROUPMEMBER *pmember,
                                        const double rgdBindPivot[3], BMAT34 *pbmat34Local)
{
    AssertVarMem(pmember);
    AssertVarMem(rgdBindPivot);
    AssertVarMem(pbmat34Local);

    // At Bind the group coordinate frame has identity orientation and its
    // origin is the bind centroid. A member's immutable ACTR-local rotation
    // is therefore its logical bind orientation; only translation is rebased.
    // BODY-level rest orientation and size/stretch live in bmat34BodyLocal.
    BrMatrix34Copy(pbmat34Local, &pmember->bmat34);
    pbmat34Local->m[3][0] = _ObjectGroupDoubleToBrs(_ObjectGroupBrsToDouble(pmember->xr) - rgdBindPivot[0]);
    pbmat34Local->m[3][1] = _ObjectGroupDoubleToBrs(_ObjectGroupBrsToDouble(pmember->yr) - rgdBindPivot[1]);
    pbmat34Local->m[3][2] = _ObjectGroupDoubleToBrs(_ObjectGroupBrsToDouble(pmember->zr) - rgdBindPivot[2]);
}

bool MVIE::FBeginObjectGroupTransform(int32_t idGroup)
{
    AssertThis(0);
    if (_pscenOpen == pvNil || idGroup <= 0)
        return fFalse;

    // Keep the original group's bind centroid authoritative even when only a
    // subset of members exists on this frame.  Live-member centroids are used
    // only to recover scale; the parent origin itself remains the same frame
    // that v197 used for rigid rotation.
    double bindPivot[3] = { 0.0, 0.0, 0.0 };
    double liveBindPivot[3] = { 0.0, 0.0, 0.0 };
    double liveWorldPivot[3] = { 0.0, 0.0, 0.0 };
    int32_t cBind = 0;
    int32_t cLive = 0;
    const OBJECTGROUPMEMBER *pmemberBasis = pvNil;
    PACTR pactrBasis = pvNil;
    for (int32_t i = 0; i < _cObjectGroupMember; i++)
    {
        const OBJECTGROUPMEMBER *pmember = &_rgObjectGroupMember[i];
        if (pmember->idGroup != idGroup)
            continue;

        bindPivot[0] += _ObjectGroupBrsToDouble(pmember->xr);
        bindPivot[1] += _ObjectGroupBrsToDouble(pmember->yr);
        bindPivot[2] += _ObjectGroupBrsToDouble(pmember->zr);
        cBind++;

        PACTR pactr = _pscenOpen->PactrFromArid(pmember->arid);
        if (pactr == pvNil)
            continue;

        BRS xrCur, yrCur, zrCur;
        pactr->GetObjectGroupPose(&xrCur, &yrCur, &zrCur, pvNil);
        liveBindPivot[0] += _ObjectGroupBrsToDouble(pmember->xr);
        liveBindPivot[1] += _ObjectGroupBrsToDouble(pmember->yr);
        liveBindPivot[2] += _ObjectGroupBrsToDouble(pmember->zr);
        liveWorldPivot[0] += _ObjectGroupBrsToDouble(xrCur);
        liveWorldPivot[1] += _ObjectGroupBrsToDouble(yrCur);
        liveWorldPivot[2] += _ObjectGroupBrsToDouble(zrCur);
        cLive++;
        if (pmemberBasis == pvNil)
        {
            pmemberBasis = pmember;
            pactrBasis = pactr;
        }
    }
    if (cBind < 2 || cLive < 2 || pmemberBasis == pvNil || pactrBasis == pvNil)
        return fFalse;
    for (int32_t k = 0; k < 3; k++)
    {
        bindPivot[k] /= (double)cBind;
        liveBindPivot[k] /= (double)cLive;
        liveWorldPivot[k] /= (double)cLive;
    }

    BMAT34 bmatCur;
    pactrBasis->GetObjectGroupPose(pvNil, pvNil, pvNil, &bmatCur);

    double bindM[3][3], curM[3][3], bindInv[3][3], groupM[3][3];
    _ObjectGroupMatrixFromBmat(&pmemberBasis->bmat34, bindM);
    _ObjectGroupMatrixFromBmat(&bmatCur, curM);
    _ObjectGroupMatrixNormalize(bindM);
    _ObjectGroupMatrixNormalize(curM);
    for (int32_t i = 0; i < 3; i++)
        for (int32_t j = 0; j < 3; j++)
            bindInv[i][j] = bindM[j][i];
    _ObjectGroupMatrixMul(bindInv, curM, groupM);
    _ObjectGroupMatrixNormalize(groupM);

    // Recover cumulative group spacing scale independently of the BODY-local
    // geometry scale.  Centering on the live subset removes translation from
    // the fit, so absent members do not move the reconstructed parent frame.
    double groupInv[3][3];
    _ObjectGroupMatrixTranspose(groupM, groupInv);
    double scaleNumerator[3] = { 0.0, 0.0, 0.0 };
    double scaleDenominator[3] = { 0.0, 0.0, 0.0 };
    for (int32_t i = 0; i < _cObjectGroupMember; i++)
    {
        const OBJECTGROUPMEMBER *pmember = &_rgObjectGroupMember[i];
        if (pmember->idGroup != idGroup)
            continue;
        PACTR pactr = _pscenOpen->PactrFromArid(pmember->arid);
        if (pactr == pvNil)
            continue;

        BRS xrCur, yrCur, zrCur;
        pactr->GetObjectGroupPose(&xrCur, &yrCur, &zrCur, pvNil);
        const double bindLocal[3] = {
            _ObjectGroupBrsToDouble(pmember->xr) - liveBindPivot[0],
            _ObjectGroupBrsToDouble(pmember->yr) - liveBindPivot[1],
            _ObjectGroupBrsToDouble(pmember->zr) - liveBindPivot[2]
        };
        const double worldLocal[3] = {
            _ObjectGroupBrsToDouble(xrCur) - liveWorldPivot[0],
            _ObjectGroupBrsToDouble(yrCur) - liveWorldPivot[1],
            _ObjectGroupBrsToDouble(zrCur) - liveWorldPivot[2]
        };
        double groupLocal[3];
        _ObjectGroupApplyVector(worldLocal, groupInv, groupLocal);
        for (int32_t k = 0; k < 3; k++)
        {
            scaleNumerator[k] += bindLocal[k] * groupLocal[k];
            scaleDenominator[k] += bindLocal[k] * bindLocal[k];
        }
    }

    double groupScale[3] = { 1.0, 1.0, 1.0 };
    for (int32_t k = 0; k < 3; k++)
    {
        if (scaleDenominator[k] > 0.000000000001)
        {
            const double scale = scaleNumerator[k] / scaleDenominator[k];
            if (scale > 0.000001 && scale < 1000000.0)
                groupScale[k] = scale;
        }
    }

    // Recover the shared parent origin in the original all-member bind frame.
    // Averaging all live candidates reduces fixed-point noise while preserving
    // the v197 pivot even when some group members are absent on this frame.
    double groupPivot[3] = { 0.0, 0.0, 0.0 };
    int32_t cPivot = 0;
    for (int32_t i = 0; i < _cObjectGroupMember; i++)
    {
        const OBJECTGROUPMEMBER *pmember = &_rgObjectGroupMember[i];
        if (pmember->idGroup != idGroup)
            continue;
        PACTR pactr = _pscenOpen->PactrFromArid(pmember->arid);
        if (pactr == pvNil)
            continue;

        BRS xrCur, yrCur, zrCur;
        pactr->GetObjectGroupPose(&xrCur, &yrCur, &zrCur, pvNil);
        const double local[3] = {
            (_ObjectGroupBrsToDouble(pmember->xr) - bindPivot[0]) * groupScale[0],
            (_ObjectGroupBrsToDouble(pmember->yr) - bindPivot[1]) * groupScale[1],
            (_ObjectGroupBrsToDouble(pmember->zr) - bindPivot[2]) * groupScale[2]
        };
        double worldOffset[3];
        _ObjectGroupApplyVector(local, groupM, worldOffset);
        groupPivot[0] += _ObjectGroupBrsToDouble(xrCur) - worldOffset[0];
        groupPivot[1] += _ObjectGroupBrsToDouble(yrCur) - worldOffset[1];
        groupPivot[2] += _ObjectGroupBrsToDouble(zrCur) - worldOffset[2];
        cPivot++;
    }
    if (cPivot < 2)
        return fFalse;
    for (int32_t k = 0; k < 3; ++k)
        groupPivot[k] /= (double)cPivot;

    vObjectGroupHighPrecision.pmvie = this;
    vObjectGroupHighPrecision.idGroup = idGroup;
    vObjectGroupHighPrecision.fValid = fTrue;
    CopyPb(groupM, vObjectGroupHighPrecision.rgdRot, sizeof(groupM));
    for (int32_t k = 0; k < 3; k++)
    {
        vObjectGroupHighPrecision.rgdBindPivot[k] = bindPivot[k];
        vObjectGroupHighPrecision.rgdPivot[k] = groupPivot[k];
        vObjectGroupHighPrecision.rgdScale[k] = groupScale[k];
    }

    // Establish the real shared BRender parent at the reconstructed current
    // group pose. BODY attachment preserves each member's apparent world pose
    // while converting its root matrix into this parent's local space.
    BMAT34 bmat34Parent;
    _ObjectGroupMatrixToBmat(groupM, &bmat34Parent);
    bmat34Parent.m[3][0] = _ObjectGroupDoubleToBrs(vObjectGroupHighPrecision.rgdPivot[0]);
    bmat34Parent.m[3][1] = _ObjectGroupDoubleToBrs(vObjectGroupHighPrecision.rgdPivot[1]);
    bmat34Parent.m[3][2] = _ObjectGroupDoubleToBrs(vObjectGroupHighPrecision.rgdPivot[2]);
    if (!_FEnsureObjectGroupRenderParent(idGroup, &bmat34Parent))
    {
        _DestroyObjectGroupRenderParent(idGroup);
        vObjectGroupHighPrecision.fValid = fFalse;
        return fFalse;
    }

    // Reparenting preserves the current visible world pose, including BODY
    // rest orientation and all current stretch/squash and grow/shrink state.
    // Re-capture every time this shared frame is rebuilt: Undo/Redo swaps the
    // authoritative ACTR state, so keeping an older local matrix would simply
    // reapply the state that was supposed to have been undone.
    for (int32_t i = 0; i < _cObjectGroupMember; i++)
    {
        OBJECTGROUPMEMBER *pmember = &_rgObjectGroupMember[i];
        if (pmember->idGroup != idGroup)
            continue;
        PACTR pactr = _pscenOpen->PactrFromArid(pmember->arid);
        if (pactr == pvNil || pactr->Pbody() == pvNil)
            continue;

        if (!F4DMMBodyGetObjectGroupLocalPose(pactr->Pbody(), &pmember->bmat34BodyLocal))
        {
            _DestroyObjectGroupRenderParent(idGroup);
            vObjectGroupHighPrecision.fValid = fFalse;
            return fFalse;
        }
        pmember->fBodyLocalValid = fTrue;

        MultiLog(this,
                 "group_local_capture id=%ld arid=%ld M=[%.9g %.9g %.9g; %.9g %.9g %.9g; %.9g %.9g %.9g; %.9g %.9g %.9g]",
                 (long)idGroup, (long)pmember->arid,
                 (double)BrScalarToFloat(pmember->bmat34BodyLocal.m[0][0]),
                 (double)BrScalarToFloat(pmember->bmat34BodyLocal.m[0][1]),
                 (double)BrScalarToFloat(pmember->bmat34BodyLocal.m[0][2]),
                 (double)BrScalarToFloat(pmember->bmat34BodyLocal.m[1][0]),
                 (double)BrScalarToFloat(pmember->bmat34BodyLocal.m[1][1]),
                 (double)BrScalarToFloat(pmember->bmat34BodyLocal.m[1][2]),
                 (double)BrScalarToFloat(pmember->bmat34BodyLocal.m[2][0]),
                 (double)BrScalarToFloat(pmember->bmat34BodyLocal.m[2][1]),
                 (double)BrScalarToFloat(pmember->bmat34BodyLocal.m[2][2]),
                 (double)BrScalarToFloat(pmember->bmat34BodyLocal.m[3][0]),
                 (double)BrScalarToFloat(pmember->bmat34BodyLocal.m[3][1]),
                 (double)BrScalarToFloat(pmember->bmat34BodyLocal.m[3][2]));
    }

    MultiLog(this,
             "group_begin_hp id=%ld bind_pivot=(%.9g,%.9g,%.9g) pivot=(%.9g,%.9g,%.9g) scale=(%.9g,%.9g,%.9g)",
             (long)idGroup, bindPivot[0], bindPivot[1], bindPivot[2],
             vObjectGroupHighPrecision.rgdPivot[0], vObjectGroupHighPrecision.rgdPivot[1],
             vObjectGroupHighPrecision.rgdPivot[2], vObjectGroupHighPrecision.rgdScale[0],
             vObjectGroupHighPrecision.rgdScale[1], vObjectGroupHighPrecision.rgdScale[2]);
    return fTrue;
}

bool MVIE::FJoinObjectToGroup(int32_t idGroup, int32_t arid)
{
    AssertThis(0);
    if (!FCanJoinObjectToGroup(idGroup, arid))
        return fFalse;

    // Reconstruct the group's exact current parent frame before expressing the
    // new object in immutable bind space. This lets an object join a group that
    // has already been moved and rotated without either side jumping.
    if (!FBeginObjectGroupTransform(idGroup))
        return fFalse;

    PACTR pactr = _pscenOpen->PactrFromArid(arid);
    if (pactr == pvNil)
        return fFalse;

    BRS xrCur, yrCur, zrCur;
    BMAT34 bmat34Cur;
    pactr->GetObjectGroupPose(&xrCur, &yrCur, &zrCur, &bmat34Cur);

    double groupInv[3][3];
    _ObjectGroupMatrixTranspose(vObjectGroupHighPrecision.rgdRot, groupInv);
    double deltaWorld[3] = {
        _ObjectGroupBrsToDouble(xrCur) - vObjectGroupHighPrecision.rgdPivot[0],
        _ObjectGroupBrsToDouble(yrCur) - vObjectGroupHighPrecision.rgdPivot[1],
        _ObjectGroupBrsToDouble(zrCur) - vObjectGroupHighPrecision.rgdPivot[2]
    };
    double localBind[3];
    _ObjectGroupApplyVector(deltaWorld, groupInv, localBind);
    for (int32_t k = 0; k < 3; ++k)
    {
        if (vObjectGroupHighPrecision.rgdScale[k] > 0.000001)
            localBind[k] /= vObjectGroupHighPrecision.rgdScale[k];
    }

    OBJECTGROUPMEMBER member;
    ClearPb(&member, SIZEOF(member));
    member.idGroup = idGroup;
    member.arid = arid;
    member.xr = _ObjectGroupDoubleToBrs(vObjectGroupHighPrecision.rgdBindPivot[0] + localBind[0]);
    member.yr = _ObjectGroupDoubleToBrs(vObjectGroupHighPrecision.rgdBindPivot[1] + localBind[1]);
    member.zr = _ObjectGroupDoubleToBrs(vObjectGroupHighPrecision.rgdBindPivot[2] + localBind[2]);

    double currentM[3][3], bindM[3][3];
    _ObjectGroupMatrixFromBmat(&bmat34Cur, currentM);
    _ObjectGroupMatrixNormalize(currentM);
    _ObjectGroupMatrixMul(currentM, groupInv, bindM);
    _ObjectGroupMatrixNormalize(bindM);
    _ObjectGroupMatrixToBmat(bindM, &member.bmat34);

    _rgObjectGroupMember[_cObjectGroupMember++] = member;

    // Membership changes the centroid used as the group's bind origin. Rebase
    // every frozen BODY child from the visible world pose under the new parent
    // rather than carrying stale local translations from the previous member set.
    for (int32_t i = 0; i < _cObjectGroupMember; i++)
    {
        if (_rgObjectGroupMember[i].idGroup != idGroup)
            continue;
        _rgObjectGroupMember[i].fBodyLocalValid = fFalse;
        ClearPb(&_rgObjectGroupMember[i].bmat34BodyLocal,
                SIZEOF(_rgObjectGroupMember[i].bmat34BodyLocal));
    }
    vObjectGroupHighPrecision.fValid = fFalse;

    if (!FBeginObjectGroupTransform(idGroup))
    {
        _cObjectGroupMember--;
        vObjectGroupHighPrecision.fValid = fFalse;
        FBeginObjectGroupTransform(idGroup);
        return fFalse;
    }

    pactr->Hilite();
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    MultiLog(this, "group_join id=%ld arid=%ld members=%ld",
             (long)idGroup, (long)arid, (long)CObjectGroupMembers(idGroup));
    return fTrue;
}

bool MVIE::FAbandonObjectFromGroup(int32_t idGroup, int32_t arid)
{
    AssertThis(0);
    if (!FCanAbandonObjectFromGroup(idGroup, arid))
        return fFalse;

    const int32_t cMemberBefore = CObjectGroupMembers(idGroup);
    if (cMemberBefore <= 2)
    {
        // A one-member Object Group has no useful group semantics. Abandoning
        // from a pair therefore dissolves the group and leaves both objects in
        // their current world poses.
        const bool fOk = FUnbindObjectGroup(idGroup);
        if (fOk)
            MultiLog(this, "group_abandon_dissolve id=%ld arid=%ld", (long)idGroup, (long)arid);
        return fOk;
    }

    if (!FBeginObjectGroupTransform(idGroup))
        return fFalse;

    int32_t iRemove = ivNil;
    for (int32_t i = 0; i < _cObjectGroupMember; i++)
    {
        if (_rgObjectGroupMember[i].idGroup == idGroup && _rgObjectGroupMember[i].arid == arid)
        {
            iRemove = i;
            break;
        }
    }
    if (iRemove == ivNil)
        return fFalse;

    OBJECTGROUPMEMBER memberRemoved = _rgObjectGroupMember[iRemove];
    PACTR pactr = _pscenOpen->PactrFromArid(arid);
    if (pactr != pvNil && pactr->Pbody() != pvNil)
        Detach4DMMBodyFromObjectGroupParent(pactr->Pbody());

    for (int32_t i = iRemove + 1; i < _cObjectGroupMember; i++)
        _rgObjectGroupMember[i - 1] = _rgObjectGroupMember[i];
    _cObjectGroupMember--;

    for (int32_t i = 0; i < _cObjectGroupMember; i++)
    {
        if (_rgObjectGroupMember[i].idGroup != idGroup)
            continue;
        _rgObjectGroupMember[i].fBodyLocalValid = fFalse;
        ClearPb(&_rgObjectGroupMember[i].bmat34BodyLocal,
                SIZEOF(_rgObjectGroupMember[i].bmat34BodyLocal));
    }
    vObjectGroupHighPrecision.fValid = fFalse;

    if (!FBeginObjectGroupTransform(idGroup))
    {
        for (int32_t i = _cObjectGroupMember; i > iRemove; i--)
            _rgObjectGroupMember[i] = _rgObjectGroupMember[i - 1];
        _rgObjectGroupMember[iRemove] = memberRemoved;
        _cObjectGroupMember++;
        for (int32_t i = 0; i < _cObjectGroupMember; i++)
        {
            if (_rgObjectGroupMember[i].idGroup == idGroup)
                _rgObjectGroupMember[i].fBodyLocalValid = fFalse;
        }
        vObjectGroupHighPrecision.fValid = fFalse;
        FBeginObjectGroupTransform(idGroup);
        return fFalse;
    }

    // The removed member has just changed BRender parent while retaining the
    // same scene ARID. Restore its authoritative Object Properties immediately.
    UpdateObjectShadowProperties();

    if (pactr != pvNil)
        pactr->Hilite();
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    MultiLog(this, "group_abandon id=%ld arid=%ld members=%ld",
             (long)idGroup, (long)arid, (long)CObjectGroupMembers(idGroup));
    return fTrue;
}

bool MVIE::FGetObjectGroupPivot(int32_t idGroup, BRS *pxr, BRS *pyr, BRS *pzr)
{
    AssertThis(0);
    AssertNilOrVarMem(pxr);
    AssertNilOrVarMem(pyr);
    AssertNilOrVarMem(pzr);
    if (_pscenOpen == pvNil || idGroup <= 0)
        return fFalse;

    bool fGroupHere = fFalse;
    for (int32_t i = 0; i < _cObjectGroup; i++)
    {
        if (_rgObjectGroup[i].id == idGroup && _rgObjectGroup[i].iscen == _iscen)
        {
            fGroupHere = fTrue;
            break;
        }
    }
    if (!fGroupHere)
        return fFalse;

    int64_t xrSum = 0;
    int64_t yrSum = 0;
    int64_t zrSum = 0;
    int32_t cMember = 0;
    for (int32_t i = 0; i < _cObjectGroupMember; i++)
    {
        if (_rgObjectGroupMember[i].idGroup != idGroup)
            continue;
        PACTR pactr = _pscenOpen->PactrFromArid(_rgObjectGroupMember[i].arid);
        if (pactr == pvNil)
            continue;
        BRS xr, yr, zr;
        pactr->GetXyzWorld(&xr, &yr, &zr);
        xrSum += (int64_t)xr;
        yrSum += (int64_t)yr;
        zrSum += (int64_t)zr;
        cMember++;
    }
    if (cMember == 0)
        return fFalse;

    if (pxr != pvNil)
        *pxr = (BRS)(xrSum / cMember);
    if (pyr != pvNil)
        *pyr = (BRS)(yrSum / cMember);
    if (pzr != pvNil)
        *pzr = (BRS)(zrSum / cMember);
    return fTrue;
}

static void _ObjectGroupMemberTargetWorld(const OBJECTGROUPMEMBER *pmember,
                                          const double rgdScale[3],
                                          BRS *pxr, BRS *pyr, BRS *pzr)
{
    AssertVarMem(pmember);
    AssertVarMem(rgdScale);

    const double local[3] = {
        (_ObjectGroupBrsToDouble(pmember->xr) - vObjectGroupHighPrecision.rgdBindPivot[0]) * rgdScale[0],
        (_ObjectGroupBrsToDouble(pmember->yr) - vObjectGroupHighPrecision.rgdBindPivot[1]) * rgdScale[1],
        (_ObjectGroupBrsToDouble(pmember->zr) - vObjectGroupHighPrecision.rgdBindPivot[2]) * rgdScale[2]
    };
    double worldOffset[3];
    _ObjectGroupApplyVector(local, vObjectGroupHighPrecision.rgdRot, worldOffset);
    if (pxr != pvNil)
        *pxr = _ObjectGroupDoubleToBrs(vObjectGroupHighPrecision.rgdPivot[0] + worldOffset[0]);
    if (pyr != pvNil)
        *pyr = _ObjectGroupDoubleToBrs(vObjectGroupHighPrecision.rgdPivot[1] + worldOffset[1]);
    if (pzr != pvNil)
        *pzr = _ObjectGroupDoubleToBrs(vObjectGroupHighPrecision.rgdPivot[2] + worldOffset[2]);
}

static bool _FObjectGroupCaptureMemberLocal(ACTR *pactr, OBJECTGROUPMEMBER *pmember)
{
    if (pactr == pvNil || pmember == pvNil || pactr->Pbody() == pvNil)
        return fFalse;
    if (!F4DMMBodyGetObjectGroupLocalPose(pactr->Pbody(), &pmember->bmat34BodyLocal))
        return fFalse;
    pmember->fBodyLocalValid = fTrue;
    return fTrue;
}

bool MVIE::FMoveObjectGroup(int32_t idGroup, BRS dxr, BRS dyr, BRS dzr, bool *pfMoved, uint32_t grfmaf)
{
    AssertThis(0);
    AssertNilOrVarMem(pfMoved);
    if (pfMoved != pvNil)
        *pfMoved = fFalse;
    if (_pscenOpen == pvNil || idGroup <= 0)
        return fFalse;

    OBJECTGROUP *pgroup = pvNil;
    for (int32_t i = 0; i < _cObjectGroup; i++)
    {
        if (_rgObjectGroup[i].id == idGroup && _rgObjectGroup[i].iscen == _iscen)
        {
            pgroup = &_rgObjectGroup[i];
            break;
        }
    }
    if (pgroup == pvNil)
        return fFalse;
    if (!vObjectGroupHighPrecision.fValid || vObjectGroupHighPrecision.pmvie != this ||
        vObjectGroupHighPrecision.idGroup != idGroup)
    {
        if (!FBeginObjectGroupTransform(idGroup))
            return fFalse;
    }
    if (pgroup->pbactParent == pvNil)
        return fFalse;

    // Ground clamping must be applied once to the shared group delta.  Letting
    // each actor clamp independently would shear the supposedly rigid group.
    uint32_t grfmafMember = grfmaf;
    if (grfmaf & fmafGround)
    {
        for (int32_t i = 0; i < _cObjectGroupMember; i++)
        {
            if (_rgObjectGroupMember[i].idGroup != idGroup)
                continue;
            PACTR pactr = _pscenOpen->PactrFromArid(_rgObjectGroupMember[i].arid);
            if (pactr == pvNil)
                continue;
            BRS yr;
            pactr->GetXyzWorld(pvNil, &yr, pvNil);
            if (yr >= rZero && BrsAdd(yr, dyr) < rZero)
                dyr = LwMax(dyr, BrsSub(rZero, yr));
        }
        grfmafMember &= ~fmafGround;
    }

    // Move the one BRender parent first. The following legacy ACTR route
    // updates then express the same new world pose; BODY::LocateOrient converts
    // them back into parent-local matrices, keeping the children frozen inside
    // the group frame.
    if (pgroup->pbactParent != pvNil)
    {
        pgroup->pbactParent->t.t.mat.m[3][0] = BrsAdd(pgroup->pbactParent->t.t.mat.m[3][0], dxr);
        pgroup->pbactParent->t.t.mat.m[3][1] = BrsAdd(pgroup->pbactParent->t.t.mat.m[3][1], dyr);
        pgroup->pbactParent->t.t.mat.m[3][2] = BrsAdd(pgroup->pbactParent->t.t.mat.m[3][2], dzr);
        if (vObjectGroupHighPrecision.fValid && vObjectGroupHighPrecision.pmvie == this &&
            vObjectGroupHighPrecision.idGroup == idGroup)
        {
            vObjectGroupHighPrecision.rgdPivot[0] += _ObjectGroupBrsToDouble(dxr);
            vObjectGroupHighPrecision.rgdPivot[1] += _ObjectGroupBrsToDouble(dyr);
            vObjectGroupHighPrecision.rgdPivot[2] += _ObjectGroupBrsToDouble(dzr);
        }
        _pbwld->MarkDirty();
    }

    bool fAnyMoved = fFalse;
    int32_t cApplied = 0;
    for (int32_t i = 0; i < _cObjectGroupMember; i++)
    {
        if (_rgObjectGroupMember[i].idGroup != idGroup)
            continue;
        PACTR pactr = _pscenOpen->PactrFromArid(_rgObjectGroupMember[i].arid);
        if (pactr == pvNil)
            continue;
        bool fMoved = fFalse;
        if (!pactr->FMoveRoute(dxr, dyr, dzr, &fMoved, grfmafMember))
            return fFalse;

        BMAT34 bmat34Fallback;
        const BMAT34 *pbmat34Local = &_rgObjectGroupMember[i].bmat34BodyLocal;
        if (!_rgObjectGroupMember[i].fBodyLocalValid)
        {
            _ObjectGroupBindLocalMatrix(&_rgObjectGroupMember[i],
                                        vObjectGroupHighPrecision.rgdBindPivot, &bmat34Fallback);
            pbmat34Local = &bmat34Fallback;
        }
        if (pactr->Pbody() == pvNil ||
            !F4DMMBodySetObjectGroupLocalPose(pactr->Pbody(), pbmat34Local))
            return fFalse;

        fAnyMoved |= fMoved;
        cApplied++;
    }

    if (cApplied == 0)
        return fFalse;
    if (fAnyMoved)
    {
        // OBJECTGROUPMEMBER stores the bind-time local reference pose.  Do
        // not rewrite it while the rigid group moves; the live group
        // translation is represented by the actors themselves.
        SetDirty();
        InvalViews();
        MultiLog(this, "group_move id=%ld delta=(%.6g,%.6g,%.6g) members=%ld",
                 (long)idGroup, (double)BrScalarToFloat(dxr),
                 (double)BrScalarToFloat(dyr), (double)BrScalarToFloat(dzr),
                 (long)cApplied);
    }
    if (pfMoved != pvNil)
        *pfMoved = fAnyMoved;
    return fTrue;
}

bool MVIE::FScaleObjectGroup(int32_t idGroup, BRS brs, bool fExtended)
{
    AssertThis(0);
    if (_pscenOpen == pvNil || idGroup <= 0 || CObjectGroupMembers(idGroup) < 2)
        return fFalse;
    if (brs == rOne)
        return fFalse;
    if (!vObjectGroupHighPrecision.fValid || vObjectGroupHighPrecision.pmvie != this ||
        vObjectGroupHighPrecision.idGroup != idGroup)
    {
        if (!FBeginObjectGroupTransform(idGroup))
            return fFalse;
    }

    const double dScale = _ObjectGroupBrsToDouble(brs);
    if (dScale <= 0.0)
        return fFalse;
    double scaleNew[3] = {
        vObjectGroupHighPrecision.rgdScale[0] * dScale,
        vObjectGroupHighPrecision.rgdScale[1] * dScale,
        vObjectGroupHighPrecision.rgdScale[2] * dScale
    };
    const BRS rScaleMin = (fExtended || FExperimental100xShrink()) ? krScaleMinExtended : krScaleMin;
    const BRS rScaleMax = (fExtended || FExperimental100xGrow()) ? krScaleMax : krScaleMaxNormal;

    int32_t cApplied = 0;
    for (int32_t i = 0; i < _cObjectGroupMember; ++i)
    {
        OBJECTGROUPMEMBER *pmember = &_rgObjectGroupMember[i];
        if (pmember->idGroup != idGroup)
            continue;
        PACTR pactr = _pscenOpen->PactrFromArid(pmember->arid);
        if (pactr == pvNil)
            continue;

        BRS xrTarget, yrTarget, zrTarget;
        _ObjectGroupMemberTargetWorld(pmember, scaleNew, &xrTarget, &yrTarget, &zrTarget);
        BRS xrCur, yrCur, zrCur;
        pactr->GetObjectGroupPose(&xrCur, &yrCur, &zrCur, pvNil);

        if (!pactr->FScale(brs, rScaleMin, rScaleMax))
            return fFalse;
        bool fMoved = fFalse;
        const BRS dxr = BrsSub(xrTarget, xrCur);
        const BRS dyr = BrsSub(yrTarget, yrCur);
        const BRS dzr = BrsSub(zrTarget, zrCur);
        if ((dxr != rZero || dyr != rZero || dzr != rZero) &&
            !pactr->FMoveRoute(dxr, dyr, dzr, &fMoved, fmafNil))
            return fFalse;
        if (!_FObjectGroupCaptureMemberLocal(pactr, pmember))
            return fFalse;
        ++cApplied;
    }
    if (cApplied < 2)
        return fFalse;

    for (int32_t k = 0; k < 3; ++k)
        vObjectGroupHighPrecision.rgdScale[k] = scaleNew[k];
    SetDirty();
    InvalViewsAndScb();
    MultiLog(this, "group_scale id=%ld factor=%.9g total=(%.9g,%.9g,%.9g) members=%ld extended=%d",
             (long)idGroup, dScale, scaleNew[0], scaleNew[1], scaleNew[2],
             (long)cApplied, (int)fExtended);
    return fTrue;
}

bool MVIE::FSquashStretchObjectGroup(int32_t idGroup, BRS brs)
{
    AssertThis(0);
    if (_pscenOpen == pvNil || idGroup <= 0 || CObjectGroupMembers(idGroup) < 2)
        return fFalse;
    if (brs == rOne)
        return fTrue;
    if (brs == rZero)
        return fFalse;
    if (!vObjectGroupHighPrecision.fValid || vObjectGroupHighPrecision.pmvie != this ||
        vObjectGroupHighPrecision.idGroup != idGroup)
    {
        if (!FBeginObjectGroupTransform(idGroup))
            return fFalse;
    }

    const BRS brsy = BrsDiv(rOne, brs);
    const double dScaleXZ = _ObjectGroupBrsToDouble(brs);
    const double dScaleY = _ObjectGroupBrsToDouble(brsy);
    if (dScaleXZ <= 0.0 || dScaleY <= 0.0)
        return fFalse;
    double scaleNew[3] = {
        vObjectGroupHighPrecision.rgdScale[0] * dScaleXZ,
        vObjectGroupHighPrecision.rgdScale[1] * dScaleY,
        vObjectGroupHighPrecision.rgdScale[2] * dScaleXZ
    };

    int32_t cApplied = 0;
    for (int32_t i = 0; i < _cObjectGroupMember; ++i)
    {
        OBJECTGROUPMEMBER *pmember = &_rgObjectGroupMember[i];
        if (pmember->idGroup != idGroup)
            continue;
        PACTR pactr = _pscenOpen->PactrFromArid(pmember->arid);
        if (pactr == pvNil)
            continue;

        BRS xrTarget, yrTarget, zrTarget;
        _ObjectGroupMemberTargetWorld(pmember, scaleNew, &xrTarget, &yrTarget, &zrTarget);
        BRS xrCur, yrCur, zrCur;
        pactr->GetObjectGroupPose(&xrCur, &yrCur, &zrCur, pvNil);

        if (!pactr->FPull(brs, brsy, brs))
            return fFalse;
        bool fMoved = fFalse;
        const BRS dxr = BrsSub(xrTarget, xrCur);
        const BRS dyr = BrsSub(yrTarget, yrCur);
        const BRS dzr = BrsSub(zrTarget, zrCur);
        if ((dxr != rZero || dyr != rZero || dzr != rZero) &&
            !pactr->FMoveRoute(dxr, dyr, dzr, &fMoved, fmafNil))
            return fFalse;
        if (!_FObjectGroupCaptureMemberLocal(pactr, pmember))
            return fFalse;
        ++cApplied;
    }
    if (cApplied < 2)
        return fFalse;

    for (int32_t k = 0; k < 3; ++k)
        vObjectGroupHighPrecision.rgdScale[k] = scaleNew[k];
    SetDirty();
    InvalViewsAndScb();
    MultiLog(this, "group_pull id=%ld factors=(%.9g,%.9g,%.9g) total=(%.9g,%.9g,%.9g) members=%ld",
             (long)idGroup, dScaleXZ, dScaleY, dScaleXZ,
             scaleNew[0], scaleNew[1], scaleNew[2], (long)cApplied);
    return fTrue;
}

bool MVIE::FRotateObjectGroup(int32_t idGroup, BRA xa, BRA ya, BRA za, bool fFromHereFwd)
{
    AssertThis(0);
    if (_pscenOpen == pvNil || idGroup <= 0)
        return fFalse;
    if (xa == aZero && ya == aZero && za == aZero)
        return fTrue;

    OBJECTGROUP *pgroup = pvNil;
    for (int32_t i = 0; i < _cObjectGroup; i++)
    {
        if (_rgObjectGroup[i].id == idGroup && _rgObjectGroup[i].iscen == _iscen)
        {
            pgroup = &_rgObjectGroup[i];
            break;
        }
    }
    if (pgroup == pvNil || CObjectGroupMembers(idGroup) < 2)
        return fFalse;

    if (!vObjectGroupHighPrecision.fValid || vObjectGroupHighPrecision.pmvie != this ||
        vObjectGroupHighPrecision.idGroup != idGroup)
    {
        if (!FBeginObjectGroupTransform(idGroup))
            return fFalse;
    }

    // v103 had the correct parent/child model but still quantized the shared
    // group matrix to legacy 16.16 scalars on every mouse packet, then fed
    // that almost-orthonormal matrix back into the next packet.  The changing
    // quantisation error was visible between long parallel objects as a tiny
    // squeeze/expand cycle.  Keep the accumulated parent frame entirely in
    // double precision for the gesture and quantize only each final ACTR write.
    _ObjectGroupPostRotate(vObjectGroupHighPrecision.rgdRot, xa, ya, za);

    // This is the actual rigid-body transform. BODY roots remain frozen in
    // bind-local space; only this one BRender parent rotates/translates them.
    if (pgroup->pbactParent == pvNil)
        return fFalse;
    BMAT34 bmat34Parent;
    _ObjectGroupMatrixToBmat(vObjectGroupHighPrecision.rgdRot, &bmat34Parent);
    bmat34Parent.m[3][0] = _ObjectGroupDoubleToBrs(vObjectGroupHighPrecision.rgdPivot[0]);
    bmat34Parent.m[3][1] = _ObjectGroupDoubleToBrs(vObjectGroupHighPrecision.rgdPivot[1]);
    bmat34Parent.m[3][2] = _ObjectGroupDoubleToBrs(vObjectGroupHighPrecision.rgdPivot[2]);
    BrMatrix34Copy(&pgroup->pbactParent->t.t.mat, &bmat34Parent);
    pgroup->pbactParent->t.type = BR_TRANSFORM_MATRIX34;
    _pbwld->MarkDirty();

    BMAT34 bmatGroupLog;
    _ObjectGroupMatrixToBmat(vObjectGroupHighPrecision.rgdRot, &bmatGroupLog);
    MultiLog(this,
             "group_rotate_hp id=%ld angles=(%ld,%ld,%ld) from_here=%d pivot=(%.9g,%.9g,%.9g) G=[%.9g %.9g %.9g; %.9g %.9g %.9g; %.9g %.9g %.9g]",
             (long)idGroup, (long)xa, (long)ya, (long)za, (int)fFromHereFwd,
             vObjectGroupHighPrecision.rgdPivot[0], vObjectGroupHighPrecision.rgdPivot[1],
             vObjectGroupHighPrecision.rgdPivot[2],
             vObjectGroupHighPrecision.rgdRot[0][0], vObjectGroupHighPrecision.rgdRot[0][1], vObjectGroupHighPrecision.rgdRot[0][2],
             vObjectGroupHighPrecision.rgdRot[1][0], vObjectGroupHighPrecision.rgdRot[1][1], vObjectGroupHighPrecision.rgdRot[1][2],
             vObjectGroupHighPrecision.rgdRot[2][0], vObjectGroupHighPrecision.rgdRot[2][1], vObjectGroupHighPrecision.rgdRot[2][2]);

    int32_t cApplied = 0;
    for (int32_t i = 0; i < _cObjectGroupMember; i++)
    {
        const OBJECTGROUPMEMBER *pmember = &_rgObjectGroupMember[i];
        if (pmember->idGroup != idGroup)
            continue;
        PACTR pactr = _pscenOpen->PactrFromArid(pmember->arid);
        if (pactr == pvNil)
            continue;

        const double local[3] = {
            (_ObjectGroupBrsToDouble(pmember->xr) - vObjectGroupHighPrecision.rgdBindPivot[0]) *
                vObjectGroupHighPrecision.rgdScale[0],
            (_ObjectGroupBrsToDouble(pmember->yr) - vObjectGroupHighPrecision.rgdBindPivot[1]) *
                vObjectGroupHighPrecision.rgdScale[1],
            (_ObjectGroupBrsToDouble(pmember->zr) - vObjectGroupHighPrecision.rgdBindPivot[2]) *
                vObjectGroupHighPrecision.rgdScale[2]
        };
        double worldOffset[3];
        _ObjectGroupApplyVector(local, vObjectGroupHighPrecision.rgdRot, worldOffset);
        const double targetD[3] = {
            vObjectGroupHighPrecision.rgdPivot[0] + worldOffset[0],
            vObjectGroupHighPrecision.rgdPivot[1] + worldOffset[1],
            vObjectGroupHighPrecision.rgdPivot[2] + worldOffset[2]
        };
        const BRS xrNew = _ObjectGroupDoubleToBrs(targetD[0]);
        const BRS yrNew = _ObjectGroupDoubleToBrs(targetD[1]);
        const BRS zrNew = _ObjectGroupDoubleToBrs(targetD[2]);

        BRS xrCur, yrCur, zrCur;
        BMAT34 bmat34CurrentMember;
        pactr->GetObjectGroupPose(&xrCur, &yrCur, &zrCur, &bmat34CurrentMember);

        double bindMember[3][3];
        double targetMember[3][3];
        _ObjectGroupMatrixFromBmat(&pmember->bmat34, bindMember);
        _ObjectGroupMatrixNormalize(bindMember);
        _ObjectGroupMatrixMul(bindMember, vObjectGroupHighPrecision.rgdRot, targetMember);
        _ObjectGroupMatrixNormalize(targetMember);
        BMAT34 bmat34Target;
        _ObjectGroupMatrixToBmat(targetMember, &bmat34Target);

        MultiLog(this,
                 "group_member_hp id=%ld arid=%ld local=(%.9g,%.9g,%.9g) current=(%.9g,%.9g,%.9g) targetD=(%.9g,%.9g,%.9g) targetQ=(%.9g,%.9g,%.9g)",
                 (long)idGroup, (long)pmember->arid,
                 local[0], local[1], local[2],
                 _ObjectGroupBrsToDouble(xrCur), _ObjectGroupBrsToDouble(yrCur), _ObjectGroupBrsToDouble(zrCur),
                 targetD[0], targetD[1], targetD[2],
                 _ObjectGroupBrsToDouble(xrNew), _ObjectGroupBrsToDouble(yrNew), _ObjectGroupBrsToDouble(zrNew));

        if (!pactr->FSetObjectGroupOrientation(&bmat34Target, fFromHereFwd))
            return fFalse;

        const BRS dxr = BrsSub(xrNew, xrCur);
        const BRS dyr = BrsSub(yrNew, yrCur);
        const BRS dzr = BrsSub(zrNew, zrCur);
        bool fMoved = fFalse;
        if ((dxr != rZero || dyr != rZero || dzr != rZero) &&
            !pactr->FMoveRoute(dxr, dyr, dzr, &fMoved, fmafNil))
            return fFalse;

        // The ACTR calls above persist the correct logical world pose. They
        // are bookkeeping only for grouped rendering: restore the exact frozen
        // child transform so independent BODY matrices cannot breathe apart.
        BMAT34 bmat34Fallback;
        const BMAT34 *pbmat34Local = &pmember->bmat34BodyLocal;
        if (!pmember->fBodyLocalValid)
        {
            _ObjectGroupBindLocalMatrix(pmember, vObjectGroupHighPrecision.rgdBindPivot, &bmat34Fallback);
            pbmat34Local = &bmat34Fallback;
        }
        if (pactr->Pbody() == pvNil ||
            !F4DMMBodySetObjectGroupLocalPose(pactr->Pbody(), pbmat34Local))
            return fFalse;
        cApplied++;
    }

    if (cApplied < 2)
        return fFalse;

    SetDirty();
    InvalViews();
    return fTrue;
}

bool MVIE::FNormalizeObjectGroupRotation(int32_t idGroup)
{
    AssertThis(0);
    if (_pscenOpen == pvNil || idGroup <= 0 || CObjectGroupMembers(idGroup) < 2)
        return fFalse;

    OBJECTGROUP *pgroup = pvNil;
    for (int32_t i = 0; i < _cObjectGroup; i++)
    {
        if (_rgObjectGroup[i].id == idGroup && _rgObjectGroup[i].iscen == _iscen)
        {
            pgroup = &_rgObjectGroup[i];
            break;
        }
    }
    if (pgroup == pvNil)
        return fFalse;

    if (!vObjectGroupHighPrecision.fValid || vObjectGroupHighPrecision.pmvie != this ||
        vObjectGroupHighPrecision.idGroup != idGroup)
    {
        if (!FBeginObjectGroupTransform(idGroup))
            return fFalse;
    }
    if (pgroup->pbactParent == pvNil)
        return fFalse;

    ClearPb(vObjectGroupHighPrecision.rgdRot, sizeof(vObjectGroupHighPrecision.rgdRot));
    vObjectGroupHighPrecision.rgdRot[0][0] = 1.0;
    vObjectGroupHighPrecision.rgdRot[1][1] = 1.0;
    vObjectGroupHighPrecision.rgdRot[2][2] = 1.0;

    BMAT34 bmat34Parent;
    BrMatrix34Identity(&bmat34Parent);
    bmat34Parent.m[3][0] = _ObjectGroupDoubleToBrs(vObjectGroupHighPrecision.rgdPivot[0]);
    bmat34Parent.m[3][1] = _ObjectGroupDoubleToBrs(vObjectGroupHighPrecision.rgdPivot[1]);
    bmat34Parent.m[3][2] = _ObjectGroupDoubleToBrs(vObjectGroupHighPrecision.rgdPivot[2]);
    BrMatrix34Copy(&pgroup->pbactParent->t.t.mat, &bmat34Parent);
    pgroup->pbactParent->t.type = BR_TRANSFORM_MATRIX34;
    _pbwld->MarkDirty();

    int32_t cApplied = 0;
    for (int32_t i = 0; i < _cObjectGroupMember; i++)
    {
        const OBJECTGROUPMEMBER *pmember = &_rgObjectGroupMember[i];
        if (pmember->idGroup != idGroup)
            continue;
        PACTR pactr = _pscenOpen->PactrFromArid(pmember->arid);
        if (pactr == pvNil)
            continue;

        const double targetD[3] = {
            vObjectGroupHighPrecision.rgdPivot[0] +
                (_ObjectGroupBrsToDouble(pmember->xr) - vObjectGroupHighPrecision.rgdBindPivot[0]) *
                    vObjectGroupHighPrecision.rgdScale[0],
            vObjectGroupHighPrecision.rgdPivot[1] +
                (_ObjectGroupBrsToDouble(pmember->yr) - vObjectGroupHighPrecision.rgdBindPivot[1]) *
                    vObjectGroupHighPrecision.rgdScale[1],
            vObjectGroupHighPrecision.rgdPivot[2] +
                (_ObjectGroupBrsToDouble(pmember->zr) - vObjectGroupHighPrecision.rgdBindPivot[2]) *
                    vObjectGroupHighPrecision.rgdScale[2]
        };
        const BRS xrNew = _ObjectGroupDoubleToBrs(targetD[0]);
        const BRS yrNew = _ObjectGroupDoubleToBrs(targetD[1]);
        const BRS zrNew = _ObjectGroupDoubleToBrs(targetD[2]);

        BRS xrCur, yrCur, zrCur;
        pactr->GetObjectGroupPose(&xrCur, &yrCur, &zrCur, pvNil);

        double bindMember[3][3];
        _ObjectGroupMatrixFromBmat(&pmember->bmat34, bindMember);
        _ObjectGroupMatrixNormalize(bindMember);
        BMAT34 bmat34Target;
        _ObjectGroupMatrixToBmat(bindMember, &bmat34Target);
        if (!pactr->FSetObjectGroupOrientation(&bmat34Target, fTrue))
            return fFalse;

        const BRS dxr = BrsSub(xrNew, xrCur);
        const BRS dyr = BrsSub(yrNew, yrCur);
        const BRS dzr = BrsSub(zrNew, zrCur);
        bool fMoved = fFalse;
        if ((dxr != rZero || dyr != rZero || dzr != rZero) &&
            !pactr->FMoveRoute(dxr, dyr, dzr, &fMoved, fmafNil))
            return fFalse;

        BMAT34 bmat34Fallback;
        const BMAT34 *pbmat34Local = &pmember->bmat34BodyLocal;
        if (!pmember->fBodyLocalValid)
        {
            _ObjectGroupBindLocalMatrix(pmember, vObjectGroupHighPrecision.rgdBindPivot, &bmat34Fallback);
            pbmat34Local = &bmat34Fallback;
        }
        if (pactr->Pbody() == pvNil ||
            !F4DMMBodySetObjectGroupLocalPose(pactr->Pbody(), pbmat34Local))
            return fFalse;
        cApplied++;
    }

    if (cApplied < 2)
        return fFalse;

    SetDirty();
    InvalViews();
    MultiLog(this, "group_normalize_rotation id=%ld pivot=(%.9g,%.9g,%.9g) members=%ld",
             (long)idGroup, vObjectGroupHighPrecision.rgdPivot[0],
             vObjectGroupHighPrecision.rgdPivot[1], vObjectGroupHighPrecision.rgdPivot[2],
             (long)cApplied);
    return fTrue;
}

bool MVIE::FNormalizeObjectGroupScale(int32_t idGroup)
{
    AssertThis(0);
    if (_pscenOpen == pvNil || idGroup <= 0 || CObjectGroupMembers(idGroup) < 2)
        return fFalse;
    if (!vObjectGroupHighPrecision.fValid || vObjectGroupHighPrecision.pmvie != this ||
        vObjectGroupHighPrecision.idGroup != idGroup)
    {
        if (!FBeginObjectGroupTransform(idGroup))
            return fFalse;
    }

    const double scaleReset[3] = { 1.0, 1.0, 1.0 };
    int32_t cApplied = 0;
    for (int32_t i = 0; i < _cObjectGroupMember; ++i)
    {
        OBJECTGROUPMEMBER *pmember = &_rgObjectGroupMember[i];
        if (pmember->idGroup != idGroup)
            continue;
        PACTR pactr = _pscenOpen->PactrFromArid(pmember->arid);
        if (pactr == pvNil)
            continue;

        BRS xrTarget, yrTarget, zrTarget;
        _ObjectGroupMemberTargetWorld(pmember, scaleReset, &xrTarget, &yrTarget, &zrTarget);
        BRS xrCur, yrCur, zrCur;
        pactr->GetObjectGroupPose(&xrCur, &yrCur, &zrCur, pvNil);

        // Use Core so the enclosing GUND remains the only history entry for
        // the operation. ACTR::FNormalize() would create one AUND per member.
        if (!pactr->FNormalizeCore(fnormSize))
            return fFalse;
        bool fMoved = fFalse;
        const BRS dxr = BrsSub(xrTarget, xrCur);
        const BRS dyr = BrsSub(yrTarget, yrCur);
        const BRS dzr = BrsSub(zrTarget, zrCur);
        if ((dxr != rZero || dyr != rZero || dzr != rZero) &&
            !pactr->FMoveRoute(dxr, dyr, dzr, &fMoved, fmafNil))
            return fFalse;
        if (!_FObjectGroupCaptureMemberLocal(pactr, pmember))
            return fFalse;
        ++cApplied;
    }
    if (cApplied < 2)
        return fFalse;

    vObjectGroupHighPrecision.rgdScale[0] = 1.0;
    vObjectGroupHighPrecision.rgdScale[1] = 1.0;
    vObjectGroupHighPrecision.rgdScale[2] = 1.0;
    SetDirty();
    InvalViewsAndScb();
    MultiLog(this, "group_normalize_scale id=%ld pivot=(%.9g,%.9g,%.9g) members=%ld",
             (long)idGroup, vObjectGroupHighPrecision.rgdPivot[0],
             vObjectGroupHighPrecision.rgdPivot[1], vObjectGroupHighPrecision.rgdPivot[2],
             (long)cApplied);
    return fTrue;
}

struct CUSTOMBAKEPART
{
    PMODL pmodl;
    PMTRL pmtrl;
    BMAT34 bmat34;
    int32_t ipartParent; // Relative to this captured import; ivNil means custom BODY root.
    int32_t aridSource;
    int32_t ipartSource;
};

static void Release4DMMCustomBakeParts(CUSTOMBAKEPART **pprgpart, int32_t cpart)
{
    if (pprgpart == pvNil || *pprgpart == pvNil)
        return;
    for (int32_t ipart = 0; ipart < cpart; ++ipart)
    {
        ReleasePpo(&(*pprgpart)[ipart].pmodl);
        ReleasePpo(&(*pprgpart)[ipart].pmtrl);
    }
    FreePpv((void **)pprgpart);
}

// The TMPL costume tables require every BODY slot to reference a valid material
// set even when that BODY slot is a model-less hierarchy node. Give those nodes
// a harmless snapshot of the first visible material. The node's CPS model CHID
// stays chidNil, so the fallback material is never rendered; it only keeps the
// native 3DMM BODY/GLBS/GGCM contract intact without flattening the hierarchy.
static bool F4DMMCompleteCustomBakeMaterials(PMVIE pmvie, CUSTOMBAKEPART *prgpart,
                                              int32_t cpart, PCSZ pszSource)
{
    if (prgpart == pvNil || cpart <= 0)
        return fFalse;

    PMTRL pmtrlFallback = pvNil;
    for (int32_t ipart = 0; ipart < cpart; ++ipart)
    {
        if (prgpart[ipart].pmtrl != pvNil)
        {
            pmtrlFallback = prgpart[ipart].pmtrl;
            break;
        }
    }
    if (pmtrlFallback == pvNil)
        return fFalse;

    int32_t cHierarchyOnly = 0;
    int32_t cFallbackMaterials = 0;
    int32_t cEdges = 0;
    for (int32_t ipart = 0; ipart < cpart; ++ipart)
    {
        if (prgpart[ipart].ipartParent != ivNil)
            ++cEdges;
        if (prgpart[ipart].pmtrl == pvNil)
        {
            pmtrlFallback->AddRef();
            prgpart[ipart].pmtrl = pmtrlFallback;
            ++cFallbackMaterials;
        }
        if (prgpart[ipart].pmodl == pvNil)
            ++cHierarchyOnly;
    }

    MVIE::MultiLog(pmvie,
        "create_part_hierarchy_capture source=%s parts=%ld edges=%ld model_less=%ld fallback_material_nodes=%ld",
        pszSource != pvNil ? pszSource : PszLit("unknown"), (long)cpart, (long)cEdges,
        (long)cHierarchyOnly, (long)cFallbackMaterials);
    return fTrue;
}

static bool F4DMMCaptureObjectGroupBakeParts(PMVIE pmvie, int32_t idGroup,
                                              CUSTOMBAKEPART **pprgpart, int32_t *pcpart)
{
    if (pprgpart != pvNil)
        *pprgpart = pvNil;
    if (pcpart != pvNil)
        *pcpart = 0;
    if (pmvie == pvNil || pprgpart == pvNil || pcpart == pvNil || pmvie->Pscen() == pvNil ||
        idGroup <= 0)
        return fFalse;

    const OBJECTGROUP *pgroup = pvNil;
    for (int32_t i = 0; i < pmvie->CObjectGroups(); ++i)
    {
        const OBJECTGROUP *pgroupT = pmvie->PObjectGroup(i);
        if (pgroupT != pvNil && pgroupT->id == idGroup && pgroupT->iscen == pmvie->Iscen())
        {
            pgroup = pgroupT;
            break;
        }
    }
    if (pgroup == pvNil)
        return fFalse;

    // Capture from the exact live shared parent when the group is already
    // active. Re-running FBeginObjectGroupTransform() here reconstructs that
    // parent from the scale-free logical ACTR pose, which can discard the
    // group's current grow/shrink/stretch state just before Create Part reads
    // it. Only build a parent when one genuinely does not exist yet.
    if (pgroup->pbactParent == pvNil)
    {
        if (!pmvie->FBeginObjectGroupTransform(idGroup))
            return fFalse;
        for (int32_t i = 0; i < pmvie->CObjectGroups(); ++i)
        {
            const OBJECTGROUP *pgroupT = pmvie->PObjectGroup(i);
            if (pgroupT != pvNil && pgroupT->id == idGroup && pgroupT->iscen == pmvie->Iscen())
            {
                pgroup = pgroupT;
                break;
            }
        }
    }
    if (pgroup == pvNil || pgroup->pbactParent == pvNil)
        return fFalse;

    MVIE::MultiLog(pmvie,
        "create_part_live_pose_capture group=%ld frame=%ld members=%ld",
        (long)idGroup, (long)pmvie->Pscen()->Nfrm(),
        (long)pmvie->CObjectGroupMembers(idGroup));

    // Root-level source BODY nodes are baked through the shared Object Group
    // parent into group-relative coordinates. Descendants remain in their
    // authored local BODY coordinates. Recreating the source parent indexes in
    // GLPI then reproduces exactly one copy of each transform instead of the
    // old flattened sibling tree.
    PBACT pbactWorld = pgroup->pbactParent->parent;
    if (pbactWorld == pvNil)
        return fFalse;
    const BRS xrGroupOrigin = pgroup->pbactParent->t.t.mat.m[3][0];
    const BRS yrGroupOrigin = pgroup->pbactParent->t.t.mat.m[3][1];
    const BRS zrGroupOrigin = pgroup->pbactParent->t.t.mat.m[3][2];
    int32_t cpartMax = 0;
    const int32_t cmember = pmvie->CObjectGroupMembers(idGroup);
    for (int32_t imember = 0; imember < cmember; ++imember)
    {
        const OBJECTGROUPMEMBER *pmember = pmvie->PObjectGroupMember(idGroup, imember);
        PACTR pactr = pmember != pvNil ? pmvie->Pscen()->PactrFromArid(pmember->arid) : pvNil;
        if (pactr != pvNil && pactr->Pbody() != pvNil)
            cpartMax += pactr->Pbody()->Cpart();
    }
    if (cpartMax <= 0 || cpartMax > kc4DMMActorStudioFramePartMax)
        return fFalse;

    CUSTOMBAKEPART *prgpart = pvNil;
    if (!FAllocPv((void **)&prgpart, LwMul(cpartMax, SIZEOF(CUSTOMBAKEPART)), fmemClear, mprNormal))
        return fFalse;

    int32_t cpart = 0;
    for (int32_t imember = 0; imember < cmember; ++imember)
    {
        const OBJECTGROUPMEMBER *pmember = pmvie->PObjectGroupMember(idGroup, imember);
        PACTR pactr = pmember != pvNil ? pmvie->Pscen()->PactrFromArid(pmember->arid) : pvNil;
        PBODY pbody = pactr != pvNil ? pactr->Pbody() : pvNil;
        if (pbody == pvNil)
            continue;

        const int32_t ipartBase = cpart;
        for (int32_t ipart = 0; ipart < pbody->Cpart(); ++ipart)
        {
            const int32_t ipartParentSource = pbody->IpartParent(ipart);
            if (ipartParentSource != ivNil && !FIn(ipartParentSource, 0, ipart))
            {
                MVIE::MultiLog(pmvie,
                    "create_part_hierarchy_capture FAIL group=%ld arid=%ld source_part=%ld invalid_parent=%ld",
                    (long)idGroup, (long)pactr->Arid(), (long)ipart,
                    (long)ipartParentSource);
                Release4DMMCustomBakeParts(&prgpart, cpartMax);
                return fFalse;
            }

            CUSTOMBAKEPART &part = prgpart[cpart];
            part.ipartParent = ipartParentSource == ivNil ? ivNil : ipartBase + ipartParentSource;
            part.aridSource = pactr->Arid();
            part.ipartSource = ipart;

            if (ipartParentSource == ivNil)
            {
                if (!pbody->FGetPartBakeTransform(ipart, pbactWorld, &part.bmat34))
                {
                    MVIE::MultiLog(pmvie,
                        "create_part_hierarchy_capture FAIL group=%ld arid=%ld source_part=%ld stage=root_transform",
                        (long)idGroup, (long)pactr->Arid(), (long)ipart);
                    Release4DMMCustomBakeParts(&prgpart, cpartMax);
                    return fFalse;
                }
                part.bmat34.m[3][0] = BrsSub(part.bmat34.m[3][0], xrGroupOrigin);
                part.bmat34.m[3][1] = BrsSub(part.bmat34.m[3][1], yrGroupOrigin);
                part.bmat34.m[3][2] = BrsSub(part.bmat34.m[3][2], zrGroupOrigin);
            }
            else
            {
                // Child BODY nodes already contain the exact current cel's
                // local transform. Keeping this matrix local is what makes a
                // preserved parent/group node manipulate its descendants.
                pbody->GetPartMatrix(ipart, &part.bmat34);
            }

            PMODL pmodl = pvNil;
            PMTRL pmtrl = pvNil;
            BMAT34 bmat34Resource;
            BRB brb;
            const bool fHasModel = pbody->FGetPartModelBounds(ipart, &brb);
            if (pbody->FGetPartBakeData(ipart, pbactWorld, &pmodl, &pmtrl, &bmat34Resource))
            {
                part.pmodl = pmodl;
                part.pmtrl = pmtrl;
            }
            else if (fHasModel)
            {
                // A model exists but could not be wrapped/materialized. Treat
                // that as a real capture failure rather than silently turning
                // visible geometry into an "empty" hierarchy node.
                ReleasePpo(&pmodl);
                ReleasePpo(&pmtrl);
                MVIE::MultiLog(pmvie,
                    "create_part_hierarchy_capture FAIL group=%ld arid=%ld source_part=%ld stage=visible_resources",
                    (long)idGroup, (long)pactr->Arid(), (long)ipart);
                Release4DMMCustomBakeParts(&prgpart, cpartMax);
                return fFalse;
            }

            MVIE::MultiLog(pmvie,
                "create_part_capture_group group=%ld out_part=%ld arid=%ld source_part=%ld source_parent=%ld out_parent=%ld model=%p hierarchy_only=%d matrix_space=%s",
                (long)idGroup, (long)cpart, (long)pactr->Arid(), (long)ipart,
                (long)ipartParentSource, (long)part.ipartParent, part.pmodl,
                part.pmodl == pvNil ? 1 : 0,
                ipartParentSource == ivNil ? "group_root" : "body_local");
            ++cpart;
        }
    }

    if (cpart != cpartMax ||
        !F4DMMCompleteCustomBakeMaterials(pmvie, prgpart, cpart, PszLit("object_group")))
    {
        Release4DMMCustomBakeParts(&prgpart, cpartMax);
        return fFalse;
    }

    *pprgpart = prgpart;
    *pcpart = cpart;
    MVIE::MultiLog(pmvie, "create_part_capture_complete group=%ld members=%ld parts=%ld hierarchy=preserved",
                   (long)idGroup, (long)cmember, (long)cpart);
    return fTrue;
}

static bool F4DMMCaptureActorBakeParts(PMVIE pmvie, int32_t arid,
                                        CUSTOMBAKEPART **pprgpart, int32_t *pcpart)
{
    if (pprgpart != pvNil)
        *pprgpart = pvNil;
    if (pcpart != pvNil)
        *pcpart = 0;
    if (pmvie == pvNil || pprgpart == pvNil || pcpart == pvNil ||
        pmvie->Pscen() == pvNil || arid == aridNil)
        return fFalse;

    PACTR pactr = pmvie->Pscen()->PactrFromArid(arid);
    PBODY pbody = pactr != pvNil ? pactr->Pbody() : pvNil;
    if (pbody == pvNil || pbody->Cpart() <= 0 ||
        pbody->Cpart() > kc4DMMActorStudioFramePartMax)
        return fFalse;

    const int32_t cpart = pbody->Cpart();
    CUSTOMBAKEPART *prgpart = pvNil;
    if (!FAllocPv((void **)&prgpart, LwMul(cpart, SIZEOF(CUSTOMBAKEPART)),
                  fmemClear, mprNormal))
        return fFalse;

    for (int32_t ipart = 0; ipart < cpart; ++ipart)
    {
        const int32_t ipartParent = pbody->IpartParent(ipart);
        if (ipartParent != ivNil && !FIn(ipartParent, 0, ipart))
        {
            MVIE::MultiLog(pmvie,
                "create_part_hierarchy_capture FAIL arid=%ld source_part=%ld invalid_parent=%ld",
                (long)arid, (long)ipart, (long)ipartParent);
            Release4DMMCustomBakeParts(&prgpart, cpart);
            return fFalse;
        }

        CUSTOMBAKEPART &part = prgpart[ipart];
        part.ipartParent = ipartParent;
        part.aridSource = arid;
        part.ipartSource = ipart;
        if (ipartParent == ivNil)
        {
            if (!pbody->FGetPartBakeTransform(ipart, pvNil, &part.bmat34))
            {
                Release4DMMCustomBakeParts(&prgpart, cpart);
                return fFalse;
            }
        }
        else
        {
            pbody->GetPartMatrix(ipart, &part.bmat34);
        }

        PMODL pmodl = pvNil;
        PMTRL pmtrl = pvNil;
        BMAT34 bmat34Resource;
        BRB brb;
        const bool fHasModel = pbody->FGetPartModelBounds(ipart, &brb);
        if (pbody->FGetPartBakeData(ipart, pvNil, &pmodl, &pmtrl, &bmat34Resource))
        {
            part.pmodl = pmodl;
            part.pmtrl = pmtrl;
        }
        else if (fHasModel)
        {
            ReleasePpo(&pmodl);
            ReleasePpo(&pmtrl);
            MVIE::MultiLog(pmvie,
                "create_part_hierarchy_capture FAIL arid=%ld source_part=%ld stage=visible_resources",
                (long)arid, (long)ipart);
            Release4DMMCustomBakeParts(&prgpart, cpart);
            return fFalse;
        }

        MVIE::MultiLog(pmvie,
            "create_part_capture_actor arid=%ld out_part=%ld source_part=%ld source_parent=%ld model=%p hierarchy_only=%d matrix_space=%s pos=(%.9g,%.9g,%.9g)",
            (long)arid, (long)ipart, (long)ipart, (long)ipartParent,
            part.pmodl, part.pmodl == pvNil ? 1 : 0,
            ipartParent == ivNil ? "body_root" : "body_local",
            (double)BrScalarToFloat(part.bmat34.m[3][0]),
            (double)BrScalarToFloat(part.bmat34.m[3][1]),
            (double)BrScalarToFloat(part.bmat34.m[3][2]));
    }

    if (!F4DMMCompleteCustomBakeMaterials(pmvie, prgpart, cpart, PszLit("actor")))
    {
        Release4DMMCustomBakeParts(&prgpart, cpart);
        return fFalse;
    }

    *pprgpart = prgpart;
    *pcpart = cpart;
    MVIE::MultiLog(pmvie, "create_part_capture_actor_complete arid=%ld parts=%ld hierarchy=preserved",
                   (long)arid, (long)cpart);
    return fTrue;
}

static int32_t Id4DMMNextCustomImport(const CUSTOMPART *prgpart, int32_t cpart, int32_t idObject)
{
    int32_t idNext = 1;
    if (prgpart == pvNil)
        return idNext;
    for (int32_t i = 0; i < cpart; ++i)
    {
        if (prgpart[i].idObject == idObject && prgpart[i].idImport >= idNext)
            idNext = prgpart[i].idImport + 1;
    }
    return idNext;
}

static int32_t C4DMMBuildCustomImportMetadata(PMVIE pmvie, int32_t idObject,
                                                int32_t idGroup, int32_t idImport,
                                                int32_t ipartFirst,
                                                const CUSTOMBAKEPART *prgpart, int32_t cpart,
                                                CUSTOMPART *prgmeta, int32_t cmetaMax)
{
    if (pmvie == pvNil || idObject <= 0 || idImport <= 0 || ipartFirst < 0 ||
        prgpart == pvNil || cpart <= 0 || prgmeta == pvNil || cmetaMax <= 0)
        return 0;

    achar szGroup[kcch4DMMCustomName];
    ClearPb(szGroup, SIZEOF(szGroup));
    if (idGroup > 0)
    {
        for (int32_t i = 0; i < pmvie->CObjectGroups(); ++i)
        {
            const OBJECTGROUP *pgroup = pmvie->PObjectGroup(i);
            if (pgroup != pvNil && pgroup->id == idGroup && pgroup->iscen == pmvie->Iscen())
            {
                strncpy_s(szGroup, SIZEOF(szGroup), pgroup->szName, _TRUNCATE);
                break;
            }
        }
        if (szGroup[0] == 0)
            sprintf_s(szGroup, SIZEOF(szGroup), "Object Group %d", (int)idGroup);
    }

    int32_t cmeta = 0;
    for (int32_t i = 0; i < cpart; )
    {
        const int32_t arid = prgpart[i].aridSource;
        int32_t cpartSource = 1;
        while (i + cpartSource < cpart && prgpart[i + cpartSource].aridSource == arid)
            ++cpartSource;
        if (cmeta >= cmetaMax)
            return 0;

        CUSTOMPART &meta = prgmeta[cmeta++];
        ClearPb(&meta, SIZEOF(meta));
        meta.idObject = idObject;
        meta.iscen = pmvie->Iscen();
        meta.idGroup = idGroup > 0 ? idGroup : 0;
        meta.idImport = idImport;
        meta.aridSource = arid;
        meta.ipartFirst = ipartFirst + i;
        meta.cpart = cpartSource;
        if (szGroup[0] != 0)
            strcpy_s(meta.szGroupName, SIZEOF(meta.szGroupName), szGroup);

        PACTR pactrSource = pmvie->Pscen() != pvNil ? pmvie->Pscen()->PactrFromArid(arid) : pvNil;
        if (pactrSource != pvNil)
        {
            meta.fSourceTdt = pactrSource->Ptmpl() != pvNil && pactrSource->Ptmpl()->FIsTdt();
            STN stnName;
            pactrSource->GetName(&stnName);
            strncpy_s(meta.szObjectName, SIZEOF(meta.szObjectName), stnName.Psz(), _TRUNCATE);
        }
        if (meta.szObjectName[0] == 0)
            sprintf_s(meta.szObjectName, SIZEOF(meta.szObjectName), "Object %d", (int)arid);

        i += cpartSource;
    }
    return cmeta;
}

static bool F4DMMWriteGlChild(PCFL pcfl, CTG ctg, PGL pgl, CNO *pcno)
{
    if (pcfl == pvNil || pgl == pvNil || pcno == pvNil)
        return fFalse;
    BLCK blck;
    *pcno = cnoNil;
    return pcfl->FAdd(pgl->CbOnFile(), ctg, pcno, &blck) && pgl->FWrite(&blck);
}

static bool F4DMMWriteGgChild(PCFL pcfl, CTG ctg, PGG pgg, CNO *pcno)
{
    if (pcfl == pvNil || pgg == pvNil || pcno == pvNil)
        return fFalse;
    BLCK blck;
    *pcno = cnoNil;
    return pcfl->FAdd(pgg->CbOnFile(), ctg, pcno, &blck) && pgg->FWrite(&blck);
}

/***************************************************************************
    Clone one existing Chunky resource tree into the movie as a genuinely
    independent tree. Actor Studio's first handmade-part pass used the live
    MODL/MTRL wrappers' FWrite methods. That reconstructs equivalent resources,
    but it is not the same thing as preserving the exact authored BMDL/MTRL
    tree that the source actor is already rendering successfully.

    Round-tripping through a temporary CFL also handles the case where source
    and destination are the same movie file. FClone then assigns fresh CNOs
    instead of aliasing the source resource, while recursively carrying TMAP,
    TXXF, and any other children that belong to the resource.
***************************************************************************/
static bool F4DMMCloneResourceTree(PCRF pcrfSource, CTG ctg, CNO cnoSource,
                                    PCFL pcflDest, CNO *pcnoDest)
{
    if (pcnoDest != pvNil)
        *pcnoDest = cnoNil;
    if (pcrfSource == pvNil || pcflDest == pvNil || pcnoDest == pvNil || cnoSource == cnoNil)
        return fFalse;

    PCFL pcflSource = pcrfSource->Pcfl();
    if (pcflSource == pvNil || !pcflSource->FFind(ctg, cnoSource))
        return fFalse;

    PCFL pcflStage = CFL::PcflCreateTemp();
    CNO cnoStage = cnoNil;
    CNO cnoDest = cnoNil;
    const bool fCloned = pcflStage != pvNil &&
                         pcflSource->FClone(ctg, cnoSource, pcflStage, &cnoStage) &&
                         pcflStage->FClone(ctg, cnoStage, pcflDest, &cnoDest);
    ReleasePpo(&pcflStage);
    if (!fCloned || cnoDest == cnoNil ||
        (pcflSource == pcflDest && cnoDest == cnoSource))
    {
        // Never delete cnoSource here when source and destination are the same
        // file. A failed independence check must leave the authored resource
        // untouched so the caller can safely fall back to serialization.
        if (cnoDest != cnoNil && !(pcflSource == pcflDest && cnoDest == cnoSource) &&
            pcflDest->FFind(ctg, cnoDest))
            pcflDest->Delete(ctg, cnoDest);
        return fFalse;
    }

    *pcnoDest = cnoDest;
    return fTrue;
}

static bool F4DMMSynthesizeFreshTemplate(PMVIE pmvie, PCFL pcfl, PCSZ pszName, bool fProp,
                                         CUSTOMBAKEPART *prgpart, int32_t cpart,
                                         CNO *pcnoTmpl)
{
    if (pcnoTmpl != pvNil)
        *pcnoTmpl = cnoNil;
    if (pcfl == pvNil || pszName == pvNil || prgpart == pvNil || cpart <= 0 ||
        cpart > kc4DMMActorStudioFramePartMax || pcnoTmpl == pvNil)
        return fFalse;

    CNO cnoTmpl = cnoNil;
    CNO cnoPar = cnoNil;
    CNO cnoSet = cnoNil;
    CNO cnoGgcm = cnoNil;
    CNO cnoXf = cnoNil;
    CNO cnoGgcl = cnoNil;
    CNO cnoActn = cnoNil;
    PGL pglPar = pvNil;
    PGL pglSet = pvNil;
    PGL pglXf = pvNil;
    PGG pggCmid = pvNil;
    PGG pggCel = pvNil;
    CPS *prgcps = pvNil;
    bool fRet = fFalse;
    int32_t cHierarchyOnly = 0;
    int32_t cHierarchyEdges = 0;
    TMPLF tmplf;
    ACTNF actnf;
    CEL cel;
    STN stnName(pszName);
    STN stnAtRest(PszLit("At Rest"));
    PCSZ pszStage = PszLit("root");

    MVIE::MultiLog(pmvie, "create_part_synthesis_write begin name=%s type=%s parts=%ld",
                   pszName, fProp ? "prop" : "actor", (long)cpart);
    ClearPb(&tmplf, SIZEOF(tmplf));
    tmplf.bo = kboCur;
    tmplf.osk = koskCur;
    tmplf.grftmpl = fProp ? ftmplProp : 0;
    if (!pcfl->FAddPv(&tmplf, SIZEOF(tmplf), kctgTmpl, &cnoTmpl) ||
        !pcfl->FSetName(kctgTmpl, cnoTmpl, &stnName))
        goto LEnd;

    pszStage = PszLit("groups_alloc");
    pglPar = GL::PglNew(SIZEOF(int16_t), cpart);
    pglSet = GL::PglNew(SIZEOF(int16_t), cpart);
    pglXf = GL::PglNew(SIZEOF(BMAT34), cpart);
    pggCmid = GG::PggNew(SIZEOF(int32_t));
    pggCel = GG::PggNew(SIZEOF(CEL));
    if (pglPar == pvNil || pglSet == pvNil || pglXf == pvNil ||
        pggCmid == pvNil || pggCel == pvNil ||
        !FAllocPv((void **)&prgcps, LwMul(cpart, SIZEOF(CPS)), fmemClear, mprNormal))
        goto LEnd;

    pszStage = PszLit("part_chunks");
    cHierarchyOnly = 0;
    cHierarchyEdges = 0;
    for (int32_t ipart = 0; ipart < cpart; ++ipart)
    {
        const int32_t ipartParent = prgpart[ipart].ipartParent;
        if (ipartParent != ivNil && !FIn(ipartParent, 0, ipart))
        {
            MVIE::MultiLog(pmvie,
                "create_part_synthesis_hierarchy FAIL part=%ld parent=%ld parts=%ld",
                (long)ipart, (long)ipartParent, (long)cpart);
            goto LEnd;
        }
        int16_t ipar = ipartParent == ivNil ? (int16_t)ivNil : (int16_t)ipartParent;
        int16_t ibset = (int16_t)ipart; // One native material set per BODY node.
        int32_t imat = ivNil;
        if (!pglPar->FAdd(&ipar, pvNil) || !pglSet->FAdd(&ibset, pvNil) ||
            !pglXf->FAdd(&prgpart[ipart].bmat34, &imat) || imat > 0x7fff)
            goto LEnd;
        if (ipartParent != ivNil)
            ++cHierarchyEdges;

        // Model-less BODY nodes are hierarchy transforms, not discarded slots.
        // chidNil preserves the node without asking TMPL::FSetActnCel to fetch
        // a model for it; descendants still inherit this node's matrix through
        // the GLPI parent relation.
        const bool fHasModel = prgpart[ipart].pmodl != pvNil;
        prgcps[ipart].chidModl = fHasModel ? (int16_t)ipart : (int16_t)chidNil;
        prgcps[ipart].imat34 = (int16_t)imat;
        if (!fHasModel)
            ++cHierarchyOnly;

        if (fHasModel)
        {
            CNO cnoModel = cnoNil;
            const bool fTdfLiveModel = prgpart[ipart].pmodl->FLegacyTdfPivotBridge();
            bool fModelCloned = !fTdfLiveModel &&
                                F4DMMCloneResourceTree(prgpart[ipart].pmodl->Pcrf(),
                                                      kctgBmdl, prgpart[ipart].pmodl->Cno(),
                                                      pcfl, &cnoModel);
            if (!fModelCloned)
            {
                // A Modern TDF glyph has already been rebuilt in memory with a
                // zero-pivot private prepare while retaining its legacy public
                // pivot. Cloning its authored BMDL bypasses that bridge and makes
                // the custom template subtract the pivot again. Serialize the live
                // prepared glyph instead; FWriteActorStudioBake neutralizes the
                // serialized pivot only for that marked TDF case. Generated models
                // continue to use the same live-write fallback as before.
                uint8_t bPlaceholder = 0;
                cnoModel = cnoNil;
                if (!pcfl->FAddPv(&bPlaceholder, 1, kctgBmdl, &cnoModel) ||
                    !prgpart[ipart].pmodl->FWriteActorStudioBake(pcfl, kctgBmdl, cnoModel))
                    goto LEnd;
            }
            if (!pcfl->FAdoptChild(kctgTmpl, cnoTmpl, kctgBmdl, cnoModel, (CHID)ipart))
                goto LEnd;
        }

        // F4DMMCompleteCustomBakeMaterials guarantees even hierarchy-only
        // nodes have a valid material-set placeholder for BODY construction.
        if (prgpart[ipart].pmtrl == pvNil)
            goto LEnd;
        CMTLF cmtlf;
        ClearPb(&cmtlf, SIZEOF(cmtlf));
        cmtlf.bo = kboCur;
        cmtlf.osk = koskCur;
        cmtlf.ibset = ipart;
        CNO cnoCmtl = cnoNil;
        CNO cnoMtrl = cnoNil;
        bool fMtrlCloned = F4DMMCloneResourceTree(prgpart[ipart].pmtrl->Pcrf(),
                                                  kctgMtrl, prgpart[ipart].pmtrl->Cno(),
                                                  pcfl, &cnoMtrl);
        if (!fMtrlCloned && !prgpart[ipart].pmtrl->FWrite(pcfl, kctgMtrl, &cnoMtrl))
            goto LEnd;
        if (!pcfl->FAddPv(&cmtlf, SIZEOF(cmtlf), kctgCmtl, &cnoCmtl) ||
            !pcfl->FAdoptChild(kctgCmtl, cnoCmtl, kctgMtrl, cnoMtrl, 0) ||
            !pcfl->FAdoptChild(kctgTmpl, cnoTmpl, kctgCmtl, cnoCmtl, (CHID)ipart))
            goto LEnd;

        int32_t cmid = ipart;
        int32_t ccmid = 1;
        if (!pggCmid->FAdd(SIZEOF(cmid), pvNil, &cmid, &ccmid))
            goto LEnd;

        MVIE::MultiLog(pmvie,
            "create_part_synthesis_node part=%ld parent=%ld source_arid=%ld source_part=%ld model=%d chid=%ld",
            (long)ipart, (long)ipartParent, (long)prgpart[ipart].aridSource,
            (long)prgpart[ipart].ipartSource, fHasModel ? 1 : 0,
            (long)prgcps[ipart].chidModl);
    }
    MVIE::MultiLog(pmvie,
        "create_part_synthesis_hierarchy parts=%ld edges=%ld model_less=%ld",
        (long)cpart, (long)cHierarchyEdges, (long)cHierarchyOnly);

    pszStage = PszLit("action_groups");
    ClearPb(&cel, SIZEOF(cel));
    cel.chidSnd = chidNil;
    // Classic 3DMM gives even stationary body actions a positive route step
    // so Resume Last Action can record and replay X/Z motion. The handmade
    // template used to author dwr=0 here, which made custom actors animate in
    // place even after a route had been recorded.
    cel.dwr = BR_SCALAR(5.0);
    if (!pggCel->FAdd(LwMul(cpart, SIZEOF(CPS)), pvNil, prgcps, &cel) ||
        !F4DMMWriteGlChild(pcfl, kctgGlpi, pglPar, &cnoPar) ||
        !F4DMMWriteGlChild(pcfl, kctgGlbs, pglSet, &cnoSet) ||
        !F4DMMWriteGgChild(pcfl, kctgGgcm, pggCmid, &cnoGgcm) ||
        !F4DMMWriteGlChild(pcfl, kctgGlxf, pglXf, &cnoXf) ||
        !F4DMMWriteGgChild(pcfl, kctgGgcl, pggCel, &cnoGgcl) ||
        !pcfl->FAdoptChild(kctgTmpl, cnoTmpl, kctgGlpi, cnoPar, 0) ||
        !pcfl->FAdoptChild(kctgTmpl, cnoTmpl, kctgGlbs, cnoSet, 0) ||
        !pcfl->FAdoptChild(kctgTmpl, cnoTmpl, kctgGgcm, cnoGgcm, 0))
        goto LEnd;

    pszStage = PszLit("action_chunk");
    ClearPb(&actnf, SIZEOF(actnf));
    actnf.bo = kboCur;
    actnf.osk = koskCur;
    // Handmade actors/props must face their recorded route just like stock
    // 3DMM actors. At Rest is still a static BODY animation, but route motion
    // uses the action's RotateY flag to derive yaw from the X/Z direction.
    actnf.grfactn = factnStatic | factnRotateY;
    if (!pcfl->FAddPv(&actnf, SIZEOF(actnf), kctgActn, &cnoActn) ||
        !pcfl->FAdoptChild(kctgActn, cnoActn, kctgGlxf, cnoXf, 0) ||
        !pcfl->FAdoptChild(kctgActn, cnoActn, kctgGgcl, cnoGgcl, 0) ||
        !pcfl->FSetName(kctgActn, cnoActn, &stnAtRest) ||
        !pcfl->FAdoptChild(kctgTmpl, cnoTmpl, kctgActn, cnoActn, 0))
        goto LEnd;

    // Read the freshly authored template through the normal TMPL loader before
    // publishing its metadata. This catches malformed GLPI/GLBS/GGCM/ACTN
    // relationships here rather than leaving a custom-object entry that only
    // fails later when Actor Studio tries to open it.
    pszStage = PszLit("verify");
    {
        PCRF pcrfVerify = CRF::PcrfNew(pcfl, 0);
        PTMPL ptmplVerify = pvNil;
        PBODY pbodyVerify = pvNil;
        bool fVerify = fFalse;
        if (pcrfVerify != pvNil)
        {
            ptmplVerify = (PTMPL)pcrfVerify->PbacoFetch(kctgTmpl, cnoTmpl, TMPL::FReadTmpl);
            if (ptmplVerify != pvNil && ptmplVerify->Cactn() == 1)
            {
                pbodyVerify = ptmplVerify->PbodyCreate();
                fVerify = pbodyVerify != pvNil && pbodyVerify->Cpart() == cpart &&
                          ptmplVerify->FSetActnCel(pbodyVerify, 0, 0);
                if (fVerify)
                {
                    for (int32_t ipart = 0; ipart < cpart; ++ipart)
                    {
                        const int32_t ipartLoadedParent = pbodyVerify->IpartParent(ipart);
                        if (ipartLoadedParent != prgpart[ipart].ipartParent)
                        {
                            MVIE::MultiLog(pmvie,
                                "create_part_synthesis_verify hierarchy_mismatch part=%ld expected_parent=%ld loaded_parent=%ld",
                                (long)ipart, (long)prgpart[ipart].ipartParent,
                                (long)ipartLoadedParent);
                            fVerify = fFalse;
                            break;
                        }
                    }
                }
            }
        }
        ReleasePpo(&pbodyVerify);
        ReleasePpo(&ptmplVerify);
        ReleasePpo(&pcrfVerify);
        if (!fVerify)
            goto LEnd;
    }

    *pcnoTmpl = cnoTmpl;
    fRet = fTrue;
    MVIE::MultiLog(pmvie, "create_part_synthesis_write ok name=%s tmpl=%ld parts=%ld",
                   pszName, (long)cnoTmpl, (long)cpart);

LEnd:
    FreePpv((void **)&prgcps);
    ReleasePpo(&pglPar);
    ReleasePpo(&pglSet);
    ReleasePpo(&pglXf);
    ReleasePpo(&pggCmid);
    ReleasePpo(&pggCel);
    if (!fRet)
    {
        MVIE::MultiLog(pmvie, "create_part_synthesis_write fail name=%s stage=%s tmpl=%ld parts=%ld",
                       pszName, pszStage, (long)cnoTmpl, (long)cpart);
        if (cnoTmpl != cnoNil && pcfl->FFind(kctgTmpl, cnoTmpl))
            pcfl->Delete(kctgTmpl, cnoTmpl);
    }
    return fRet;
}

bool MVIE::FCreate4DMMCustomObjectPart(PCSZ pszName, bool fProp, int32_t templateKind,
                                      int32_t idTemplateCustom, int32_t sidTemplate,
                                      CTG ctgTemplate, CNO cnoTemplate, int32_t idGroup)
{
    AssertThis(0);
    if (pszName == pvNil || *pszName == 0 || idGroup <= 0 ||
        _c4DMMCustomObject >= kc4DMMCustomObjectMax ||
        _c4DMMCustomPart >= kc4DMMCustomPartMax ||
        _c4DMMCustomAction >= kc4DMMCustomActionMax || !FEnsureAutosave())
        return fFalse;

    const OBJECTGROUP *pgroup = pvNil;
    for (int32_t i = 0; i < _cObjectGroup; ++i)
    {
        if (_rgObjectGroup[i].id == idGroup && _rgObjectGroup[i].iscen == _iscen)
        {
            pgroup = &_rgObjectGroup[i];
            break;
        }
    }
    if (pgroup == pvNil)
        return fFalse;

    achar szName[kcch4DMMCustomName];
    strncpy_s(szName, SIZEOF(szName), pszName, _TRUNCATE);
    int32_t cch = (int32_t)strlen(szName);
    while (cch > 0 && (szName[cch - 1] == ChLit(' ') || szName[cch - 1] == ChLit('\t') ||
                       szName[cch - 1] == ChLit('\r') || szName[cch - 1] == ChLit('\n')))
        szName[--cch] = 0;
    if (cch == 0)
        return fFalse;

    for (int32_t i = 0; i < _c4DMMCustomObject; ++i)
        if (0 == _stricmp(_rg4DMMCustomObject[i].szName, szName))
            return fFalse;

    CUSTOMBAKEPART *prgpart = pvNil;
    int32_t cpart = 0;
    CNO cnoOwnedTmpl = cnoNil;
    if (!F4DMMCaptureObjectGroupBakeParts(this, idGroup, &prgpart, &cpart))
    {
        MultiLog(this, "create_part_synthesis fail stage=capture group=%ld name=%s", (long)idGroup, szName);
        return fFalse;
    }

    // v123's first native path intentionally establishes the fundamental
    // handmade-object case before template grafting: a selected Object Group
    // becomes a real movie-owned TMPL/BODY.  Template-derived synthesis is
    // kept gated instead of silently creating a metadata-only object or
    // pretending the selected template was applied.
    if (templateKind != kctkNone)
    {
        MultiLog(this,
            "create_part_synthesis deferred_template name=%s kind=%ld custom=%ld sid=%ld ctg=%lu cno=%ld",
            szName, (long)templateKind, (long)idTemplateCustom, (long)sidTemplate,
            (unsigned long)ctgTemplate, (long)cnoTemplate);
        Release4DMMCustomBakeParts(&prgpart, cpart);
        return fFalse;
    }

    PCFL pcfl = _pcrfAutoSave->Pcfl();
    if (pcfl == pvNil || !F4DMMSynthesizeFreshTemplate(this, pcfl, szName, fProp, prgpart, cpart, &cnoOwnedTmpl))
    {
        MultiLog(this, "create_part_synthesis fail stage=write_tmpl group=%ld name=%s parts=%ld",
                 (long)idGroup, szName, (long)cpart);
        Release4DMMCustomBakeParts(&prgpart, cpart);
        return fFalse;
    }
    const int32_t idObjectNew = _id4DMMCustomObjectNext;
    const int32_t idImport = Id4DMMNextCustomImport(_rg4DMMCustomPart, _c4DMMCustomPart, idObjectNew);
    CUSTOMPART rgmeta[kc4DMMActorStudioFramePartMax];
    const int32_t cmeta = C4DMMBuildCustomImportMetadata(this, idObjectNew, idGroup, idImport,
                                                         0, prgpart, cpart, rgmeta,
                                                         kc4DMMActorStudioFramePartMax);
    if (cmeta <= 0 || _c4DMMCustomPart + cmeta > kc4DMMCustomPartMax)
    {
        Release4DMMCustomBakeParts(&prgpart, cpart);
        if (pcfl != pvNil && cnoOwnedTmpl != cnoNil && pcfl->FFind(kctgTmpl, cnoOwnedTmpl))
            pcfl->Delete(kctgTmpl, cnoOwnedTmpl);
        return fFalse;
    }
    Release4DMMCustomBakeParts(&prgpart, cpart);

    CUSTOMOBJECT *pobj = &_rg4DMMCustomObject[_c4DMMCustomObject++];
    ClearPb(pobj, SIZEOF(*pobj));
    pobj->id = _id4DMMCustomObjectNext++;
    pobj->fProp = FPure(fProp);
    pobj->templateKind = templateKind;
    pobj->idTemplateCustom = idTemplateCustom;
    pobj->sidTemplate = sidTemplate;
    pobj->ctgTemplate = ctgTemplate;
    pobj->cnoTemplate = cnoTemplate;
    pobj->cnoOwnedTmpl = cnoOwnedTmpl;
    strcpy_s(pobj->szName, SIZEOF(pobj->szName), szName);

    for (int32_t imeta = 0; imeta < cmeta; ++imeta)
        _rg4DMMCustomPart[_c4DMMCustomPart++] = rgmeta[imeta];

    // Every action on a genuinely handmade movie-owned actor is writable by
    // definition. Mark the synthesized At Rest action as custom immediately
    // so the existing Actor Studio pose writer can edit it without requiring
    // a pointless Save As first.
    CUSTOMACTION *paction = &_rg4DMMCustomAction[_c4DMMCustomAction++];
    ClearPb(paction, SIZEOF(*paction));
    paction->cnoTmpl = cnoOwnedTmpl;
    paction->anid = 0;
    strcpy_s(paction->szName, SIZEOF(paction->szName), "At Rest");

    _pcrfAutoSave->FSetCrep(crepToss, kctgTmpl, cnoOwnedTmpl, TMPL::FReadTmpl);
    _fRequiresVmmSave = fTrue;
    _fAutosaveDirty = fTrue;
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    MultiLog(this,
        "create_part_synthesis ok object=%ld name=%s type=%s group=%ld parts=%ld owned_tmpl=%ld action0=At_Rest",
        (long)pobj->id, pobj->szName, pobj->fProp ? "prop" : "actor", (long)idGroup,
        (long)cpart, (long)cnoOwnedTmpl);
    return fTrue;
}

bool MVIE::FCreate4DMMCustomObjectPartFromActor(PCSZ pszName, bool fProp, int32_t templateKind,
                                                 int32_t idTemplateCustom, int32_t sidTemplate,
                                                 CTG ctgTemplate, CNO cnoTemplate, int32_t arid)
{
    AssertThis(0);
    if (pszName == pvNil || *pszName == 0 || arid == aridNil ||
        _c4DMMCustomObject >= kc4DMMCustomObjectMax ||
        _c4DMMCustomPart >= kc4DMMCustomPartMax ||
        _c4DMMCustomAction >= kc4DMMCustomActionMax || !FEnsureAutosave())
        return fFalse;

    achar szName[kcch4DMMCustomName];
    strncpy_s(szName, SIZEOF(szName), pszName, _TRUNCATE);
    int32_t cch = (int32_t)strlen(szName);
    while (cch > 0 && (szName[cch - 1] == ChLit(' ') || szName[cch - 1] == ChLit('\t') ||
                       szName[cch - 1] == ChLit('\r') || szName[cch - 1] == ChLit('\n')))
        szName[--cch] = 0;
    if (cch == 0)
        return fFalse;
    for (int32_t i = 0; i < _c4DMMCustomObject; ++i)
        if (0 == _stricmp(_rg4DMMCustomObject[i].szName, szName))
            return fFalse;

    CUSTOMBAKEPART *prgpart = pvNil;
    int32_t cpart = 0;
    CNO cnoOwnedTmpl = cnoNil;
    if (!F4DMMCaptureActorBakeParts(this, arid, &prgpart, &cpart))
    {
        MultiLog(this, "create_part_synthesis_actor fail stage=capture arid=%ld name=%s",
                 (long)arid, szName);
        return fFalse;
    }
    if (templateKind != kctkNone)
    {
        MultiLog(this,
            "create_part_synthesis_actor deferred_template name=%s kind=%ld custom=%ld sid=%ld ctg=%lu cno=%ld",
            szName, (long)templateKind, (long)idTemplateCustom, (long)sidTemplate,
            (unsigned long)ctgTemplate, (long)cnoTemplate);
        Release4DMMCustomBakeParts(&prgpart, cpart);
        return fFalse;
    }

    PCFL pcfl = _pcrfAutoSave->Pcfl();
    if (pcfl == pvNil ||
        !F4DMMSynthesizeFreshTemplate(this, pcfl, szName, fProp, prgpart, cpart, &cnoOwnedTmpl))
    {
        MultiLog(this, "create_part_synthesis_actor fail stage=write_tmpl arid=%ld name=%s parts=%ld",
                 (long)arid, szName, (long)cpart);
        Release4DMMCustomBakeParts(&prgpart, cpart);
        return fFalse;
    }
    const int32_t idObjectNew = _id4DMMCustomObjectNext;
    const int32_t idImport = Id4DMMNextCustomImport(_rg4DMMCustomPart, _c4DMMCustomPart, idObjectNew);
    CUSTOMPART rgmeta[kc4DMMActorStudioFramePartMax];
    const int32_t cmeta = C4DMMBuildCustomImportMetadata(this, idObjectNew, 0, idImport,
                                                         0, prgpart, cpart, rgmeta,
                                                         kc4DMMActorStudioFramePartMax);
    if (cmeta <= 0 || _c4DMMCustomPart + cmeta > kc4DMMCustomPartMax)
    {
        Release4DMMCustomBakeParts(&prgpart, cpart);
        if (pcfl != pvNil && cnoOwnedTmpl != cnoNil && pcfl->FFind(kctgTmpl, cnoOwnedTmpl))
            pcfl->Delete(kctgTmpl, cnoOwnedTmpl);
        return fFalse;
    }
    Release4DMMCustomBakeParts(&prgpart, cpart);

    CUSTOMOBJECT *pobj = &_rg4DMMCustomObject[_c4DMMCustomObject++];
    ClearPb(pobj, SIZEOF(*pobj));
    pobj->id = _id4DMMCustomObjectNext++;
    pobj->fProp = FPure(fProp);
    pobj->templateKind = templateKind;
    pobj->idTemplateCustom = idTemplateCustom;
    pobj->sidTemplate = sidTemplate;
    pobj->ctgTemplate = ctgTemplate;
    pobj->cnoTemplate = cnoTemplate;
    pobj->cnoOwnedTmpl = cnoOwnedTmpl;
    strcpy_s(pobj->szName, SIZEOF(pobj->szName), szName);

    for (int32_t imeta = 0; imeta < cmeta; ++imeta)
        _rg4DMMCustomPart[_c4DMMCustomPart++] = rgmeta[imeta];

    CUSTOMACTION *paction = &_rg4DMMCustomAction[_c4DMMCustomAction++];
    ClearPb(paction, SIZEOF(*paction));
    paction->cnoTmpl = cnoOwnedTmpl;
    paction->anid = 0;
    strcpy_s(paction->szName, SIZEOF(paction->szName), "At Rest");

    _pcrfAutoSave->FSetCrep(crepToss, kctgTmpl, cnoOwnedTmpl, TMPL::FReadTmpl);
    _fRequiresVmmSave = fTrue;
    _fAutosaveDirty = fTrue;
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    MultiLog(this,
        "create_part_synthesis_actor ok object=%ld name=%s type=%s arid=%ld parts=%ld owned_tmpl=%ld",
        (long)pobj->id, pobj->szName, pobj->fProp ? "prop" : "actor", (long)arid,
        (long)cpart, (long)cnoOwnedTmpl);
    return fTrue;
}

static bool F4DMMActorStudioSameTemplateIdentity(const TAG *ptagA, const TAG *ptagB)
{
    return ptagA != pvNil && ptagB != pvNil &&
           ptagA->sid == ptagB->sid && ptagA->ctg == ptagB->ctg && ptagA->cno == ptagB->cno;
}

bool MVIE::F4DMMCustomObjectIsHandmade(int32_t iObject) const
{
    AssertThis(0);
    if (!FIn(iObject, 0, _c4DMMCustomObject))
        return fFalse;
    const int32_t idObject = _rg4DMMCustomObject[iObject].id;
    for (int32_t iPart = 0; iPart < _c4DMMCustomPart; ++iPart)
        if (_rg4DMMCustomPart[iPart].idObject == idObject)
            return fTrue;
    return fFalse;
}

bool MVIE::FResolve4DMMReplacementTemplateTag(const TAG *ptagSource, TAG *ptagResolved) const
{
    AssertThis(0);
    if (ptagSource == pvNil || ptagResolved == pvNil || _pcrfAutoSave == pvNil)
        return fFalse;
    for (int32_t i = 0; i < _c4DMMCustomObject; ++i)
    {
        const CUSTOMOBJECT &obj = _rg4DMMCustomObject[i];
        if (F4DMMCustomObjectIsHandmade(i) || obj.cnoOwnedTmpl == cnoNil ||
            obj.templateKind != kctkDefault)
            continue;
        if (obj.sidTemplate != ptagSource->sid || obj.ctgTemplate != ptagSource->ctg ||
            obj.cnoTemplate != ptagSource->cno)
            continue;
        ClearPb(ptagResolved, SIZEOF(*ptagResolved));
        ptagResolved->sid = ksidUseCrf;
        ptagResolved->ctg = kctgTmpl;
        ptagResolved->cno = obj.cnoOwnedTmpl;
        return fTrue;
    }
    return fFalse;
}

bool MVIE::FOpen4DMMOwnedTemplateTag(CNO cnoTmpl, TAG *ptag) const
{
    AssertThis(0);
    if (ptag == pvNil || cnoTmpl == cnoNil || _pcrfAutoSave == pvNil ||
        !_pcrfAutoSave->Pcfl()->FFind(kctgTmpl, cnoTmpl))
        return fFalse;
    ClearPb(ptag, SIZEOF(*ptag));
    ptag->sid = ksidUseCrf;
    ptag->ctg = kctgTmpl;
    ptag->cno = cnoTmpl;
    return TAGM::FOpenTag(ptag, _pcrfAutoSave);
}

bool MVIE::FPromote4DMMReplacementTemplate(const TAG *ptagSource, const TAG *ptagReplacement,
                                            int32_t aridPrimary)
{
    AssertThis(0);
    if (ptagSource == pvNil || ptagReplacement == pvNil || _pgstmactr == pvNil)
        return fFalse;
    int32_t cRollChanged = 0;
    int32_t cSceneChanged = 0;
    for (int32_t imactr = 0; imactr < _pgstmactr->IvMac(); ++imactr)
    {
        MACTR mactr;
        _pgstmactr->GetExtra(imactr, &mactr);
        if (!F4DMMActorStudioSameTemplateIdentity(&mactr.tagTmpl, ptagSource))
            continue;
        TAGM::CloseTag(&mactr.tagTmpl);
        mactr.tagTmpl = *ptagReplacement;
        TAGM::DupTag(&mactr.tagTmpl);
        _pgstmactr->PutExtra(imactr, &mactr);
        ++cRollChanged;
    }
    if (Pscen() != pvNil && Pscen()->PglRollCall() != pvNil)
    {
        PGL pglpactr = Pscen()->PglRollCall();
        for (int32_t iactr = 0; iactr < pglpactr->IvMac(); ++iactr)
        {
            PACTR pactr = pvNil;
            pglpactr->Get(iactr, &pactr);
            if (pactr == pvNil || pactr->Arid() == aridPrimary)
                continue;
            TAG tagActor;
            pactr->GetTagTmpl(&tagActor);
            if (!F4DMMActorStudioSameTemplateIdentity(&tagActor, ptagSource))
                continue;
            TAG tagReplacement = *ptagReplacement;
            if (pactr->FChangeTagTmpl(&tagReplacement))
                ++cSceneChanged;
        }
    }
    if (Pmcc() != pvNil)
        Pmcc()->UpdateRollCall();
    MultiLog(this,
        "actor_studio_template_promote source_sid=%ld source_ctg=%lu source_cno=%ld replacement=%ld primary=%ld roll=%ld live=%ld",
        (long)ptagSource->sid, (unsigned long)ptagSource->ctg, (long)ptagSource->cno,
        (long)ptagReplacement->cno, (long)aridPrimary, (long)cRollChanged, (long)cSceneChanged);
    return fTrue;
}

bool F4DMMPathIsVmm(PSTN pstnPath)
{
    if (pstnPath == pvNil)
        return fFalse;
    const int32_t cch = pstnPath->Cch();
    const achar *psz = pstnPath->Psz();
    return cch >= 4 && psz[cch - 4] == ChLit('.') &&
           (psz[cch - 3] == ChLit('v') || psz[cch - 3] == ChLit('V')) &&
           (psz[cch - 2] == ChLit('m') || psz[cch - 2] == ChLit('M')) &&
           (psz[cch - 1] == ChLit('m') || psz[cch - 1] == ChLit('M'));
}

bool F4DMMFniIsVmm(PFNI pfni)
{
    if (pfni == pvNil)
        return fFalse;
    if (pfni->Ftg() == kftgVmm)
        return fTrue;
    STN stnPath;
    pfni->GetStnPath(&stnPath);
    return F4DMMPathIsVmm(&stnPath);
}

bool MVIE::FUseVmmSaveTarget(PFNI pfniVmm)
{
    AssertThis(0);
    AssertPo(pfniVmm, ffniFile);
    if (!F4DMMFniIsVmm(pfniVmm))
        return fFalse;

    // _FSetPfilSave only remembers an already-open FIL. VMM resolution reads
    // the wrapper through std::ifstream, so the original package may not yet
    // have a FIL in Kauai's open-file table. Open one explicitly in that case
    // so ordinary Save can still recognize/release the current VMM before the
    // atomic repack.
    if (!_FSetPfilSave(pfniVmm))
    {
        _pfilSave = FIL::PfilOpen(pfniVmm, ffilNil);
        if (_pfilSave == pvNil)
            return fFalse;
        _fFniSaveValid = fTrue;
        _fReadOnly = pfniVmm->FIsReadOnly();
    }
    _fRequiresVmmSave = fTrue;

    // Once the wrapper is the document target, keep the real same-basename
    // sidecar as the live metadata path. FResolveVmmMovie already mirrored
    // that authoritative sibling into the private cached .3mm for initial
    // parsing; subsequent edits/saves should no longer point at VMMCache.
    STN stnSidecar;
    pfniVmm->GetStnPath(&stnSidecar);
    for (int32_t ich = stnSidecar.Cch() - 1; ich >= 0; --ich)
    {
        const achar ch = stnSidecar.Psz()[ich];
        if (ch == ChLit('.'))
        {
            stnSidecar.Delete(ich);
            break;
        }
        if (ch == ChLit('\\') || ch == ChLit('/'))
            break;
    }
    if (stnSidecar.FAppendSz(PszLit(".3ct")))
    {
        _stnCameraTrackPath = stnSidecar;
        MultiLog(this, "vmm_sidecar_live path=%s", _stnCameraTrackPath.Psz());
    }

    _SetTitle(pfniVmm);
    return fTrue;
}

// Actor Studio pose data is stored as the ACTN's CHID-0 GGCL and GLXF.
// CFL::FAdoptChild adds a relation; it does not mean "replace the existing
// relation with this CHID". v119 therefore accumulated duplicate CHID-0
// children and the reader kept finding the original one. These helpers make
// that relationship canonical before every edit and every undo/redo swap.
static int32_t C4DMMActorStudioRemoveActionChildLinks(PCFL pcfl, CNO cnoAction, CTG ctgChild)
{
    if (pcfl == pvNil || cnoAction == cnoNil)
        return -1;
    int32_t cRemoved = 0;
    KID kid;
    while (pcfl->FGetKidChidCtg(kctgActn, cnoAction, 0, ctgChild, &kid))
    {
        pcfl->DeleteChild(kctgActn, cnoAction, kid.cki.ctg, kid.cki.cno, kid.chid);
        if (++cRemoved > 256)
            return -1;
    }
    return cRemoved;
}

static bool F4DMMActorStudioReplacePoseChildren(PCFL pcfl, CNO cnoAction,
                                                 CNO cnoGgclNew, CNO cnoGlxfNew,
                                                 CNO *pcnoGgclOld, CNO *pcnoGlxfOld,
                                                 int32_t *pcGgclOld = pvNil,
                                                 int32_t *pcGlxfOld = pvNil)
{
    if (pcnoGgclOld != pvNil)
        *pcnoGgclOld = cnoNil;
    if (pcnoGlxfOld != pvNil)
        *pcnoGlxfOld = cnoNil;
    if (pcGgclOld != pvNil)
        *pcGgclOld = 0;
    if (pcGlxfOld != pvNil)
        *pcGlxfOld = 0;
    if (pcfl == pvNil || cnoAction == cnoNil || cnoGgclNew == cnoNil || cnoGlxfNew == cnoNil)
        return fFalse;

    KID kidGgclOld;
    KID kidGlxfOld;
    if (!pcfl->FGetKidChidCtg(kctgActn, cnoAction, 0, kctgGgcl, &kidGgclOld) ||
        !pcfl->FGetKidChidCtg(kctgActn, cnoAction, 0, kctgGlxf, &kidGlxfOld))
        return fFalse;

    const int32_t cGgclRemoved = C4DMMActorStudioRemoveActionChildLinks(pcfl, cnoAction, kctgGgcl);
    const int32_t cGlxfRemoved = C4DMMActorStudioRemoveActionChildLinks(pcfl, cnoAction, kctgGlxf);
    if (cGgclRemoved <= 0 || cGlxfRemoved <= 0)
        goto LRestore;

    if (!pcfl->FAdoptChild(kctgActn, cnoAction, kctgGlxf, cnoGlxfNew, 0) ||
        !pcfl->FAdoptChild(kctgActn, cnoAction, kctgGgcl, cnoGgclNew, 0))
        goto LRestore;

    // Verify there is now exactly one authoritative CHID-0 relation of each
    // type. A second stale relation would put us straight back into v119.
    {
        KID kidVerifyGgcl;
        KID kidVerifyGlxf;
        if (!pcfl->FGetKidChidCtg(kctgActn, cnoAction, 0, kctgGgcl, &kidVerifyGgcl) ||
            !pcfl->FGetKidChidCtg(kctgActn, cnoAction, 0, kctgGlxf, &kidVerifyGlxf) ||
            kidVerifyGgcl.cki.cno != cnoGgclNew || kidVerifyGlxf.cki.cno != cnoGlxfNew)
            goto LRestore;
    }

    if (pcnoGgclOld != pvNil)
        *pcnoGgclOld = kidGgclOld.cki.cno;
    if (pcnoGlxfOld != pvNil)
        *pcnoGlxfOld = kidGlxfOld.cki.cno;
    if (pcGgclOld != pvNil)
        *pcGgclOld = cGgclRemoved;
    if (pcGlxfOld != pvNil)
        *pcGlxfOld = cGlxfRemoved;
    return fTrue;

LRestore:
    C4DMMActorStudioRemoveActionChildLinks(pcfl, cnoAction, kctgGgcl);
    C4DMMActorStudioRemoveActionChildLinks(pcfl, cnoAction, kctgGlxf);
    pcfl->FAdoptChild(kctgActn, cnoAction, kctgGlxf, kidGlxfOld.cki.cno, 0);
    pcfl->FAdoptChild(kctgActn, cnoAction, kctgGgcl, kidGgclOld.cki.cno, 0);
    return fFalse;
}

// Defined below with the rest of the Actor Studio frame helpers.
static bool F4DMMActorStudioReadActionGroups(PCFL pcfl, CNO cnoAction,
                                               PGG *ppggcel, PGL *ppglbmat34);

static bool F4DMMReplaceTmplChildForPartAppend(PCFL pcfl, CNO cnoTmpl, CTG ctgChild,
                                                CNO cnoNew, CNO *pcnoOld)
{
    if (pcnoOld != pvNil)
        *pcnoOld = cnoNil;
    if (pcfl == pvNil || cnoTmpl == cnoNil || cnoNew == cnoNil)
        return fFalse;
    KID kidOld;
    if (!pcfl->FGetKidChidCtg(kctgTmpl, cnoTmpl, 0, ctgChild, &kidOld))
        return fFalse;
    pcfl->DeleteChild(kctgTmpl, cnoTmpl, kidOld.cki.ctg, kidOld.cki.cno, kidOld.chid);
    if (!pcfl->FAdoptChild(kctgTmpl, cnoTmpl, ctgChild, cnoNew, 0))
    {
        pcfl->FAdoptChild(kctgTmpl, cnoTmpl, kidOld.cki.ctg, kidOld.cki.cno, 0);
        return fFalse;
    }
    if (pcnoOld != pvNil)
        *pcnoOld = kidOld.cki.cno;
    return fTrue;
}

struct CUSTOMAPPENDACTIONWRITE
{
    CNO cnoAction;
    CNO cnoCelOld;
    CNO cnoXfOld;
    CNO cnoCelNew;
    CNO cnoXfNew;
    bool fReplaced;
};

struct CUSTOMAPPENDRESOURCE
{
    CNO cnoModel;
    CNO cnoCmtl;
    int32_t chidModel;
    int32_t chidCmtl;
    bool fModelAdopted;
    bool fCmtlAdopted;
};

bool MVIE::FAdd4DMMCustomPartToObject(int32_t idObject, int32_t idGroup, int32_t aridSource,
                                       bool fUseVacatedModelChids)
{
    AssertThis(0);
    if (idObject <= 0 || (idGroup <= 0 && aridSource == aridNil) || !FEnsureAutosave() ||
        _c4DMMCustomPart >= kc4DMMCustomPartMax)
        return fFalse;

    CUSTOMOBJECT *pobj = pvNil;
    CUSTOMBAKEPART *prgpart = pvNil;
    int32_t cpartAdd = 0;
    PCFL pcfl = pvNil;
    CNO cnoTmpl = cnoNil;
    PGL pglPar = pvNil;
    PGL pglSet = pvNil;
    PGG pggCmid = pvNil;
    CUSTOMAPPENDACTIONWRITE *prgAction = pvNil;
    CUSTOMAPPENDRESOURCE *prgResource = pvNil;
    int32_t cpartOld = 0;
    int32_t cbsetOld = 0;
    int32_t cmidNext = 0;
    int32_t chidModelNext = 0;
    int32_t cactn = 0;
    CNO cnoParNew = cnoNil;
    CNO cnoSetNew = cnoNil;
    CNO cnoGgcmNew = cnoNil;
    CNO cnoParOld = cnoNil;
    CNO cnoSetOld = cnoNil;
    CNO cnoGgcmOld = cnoNil;
    bool fParReplaced = fFalse;
    bool fSetReplaced = fFalse;
    bool fGgcmReplaced = fFalse;
    bool fRet = fFalse;
    CUSTOMPART rgmeta[kc4DMMActorStudioFramePartMax];
    int32_t cmeta = 0;
    int32_t idImport = 0;
    BLCK blck;
    KID kid;

    for (int32_t i = 0; i < _c4DMMCustomObject; ++i)
    {
        if (_rg4DMMCustomObject[i].id == idObject && F4DMMCustomObjectIsHandmade(i))
        {
            pobj = &_rg4DMMCustomObject[i];
            break;
        }
    }
    if (pobj == pvNil || pobj->cnoOwnedTmpl == cnoNil)
        goto LEnd;

    if (idGroup > 0)
    {
        if (!F4DMMCaptureObjectGroupBakeParts(this, idGroup, &prgpart, &cpartAdd))
            goto LEnd;
    }
    else if (!F4DMMCaptureActorBakeParts(this, aridSource, &prgpart, &cpartAdd))
        goto LEnd;
    if (cpartAdd <= 0)
        goto LEnd;

    pcfl = _pcrfAutoSave != pvNil ? _pcrfAutoSave->Pcfl() : pvNil;
    cnoTmpl = pobj->cnoOwnedTmpl;
    if (pcfl == pvNil || !pcfl->FFind(kctgTmpl, cnoTmpl))
        goto LEnd;

    // Read mutable template topology and costume map.
    {
        KID kidPar;
        KID kidSet;
        KID kidGgcm;
        BLCK blckPar;
        BLCK blckSet;
        BLCK blckGgcm;
        int16_t boPar = kboCur;
        int16_t boSet = kboCur;
        int16_t boGgcm = kboCur;
        if (!pcfl->FGetKidChidCtg(kctgTmpl, cnoTmpl, 0, kctgGlpi, &kidPar) ||
            !pcfl->FGetKidChidCtg(kctgTmpl, cnoTmpl, 0, kctgGlbs, &kidSet) ||
            !pcfl->FGetKidChidCtg(kctgTmpl, cnoTmpl, 0, kctgGgcm, &kidGgcm) ||
            !pcfl->FFind(kidPar.cki.ctg, kidPar.cki.cno, &blckPar) ||
            !pcfl->FFind(kidSet.cki.ctg, kidSet.cki.cno, &blckSet) ||
            !pcfl->FFind(kidGgcm.cki.ctg, kidGgcm.cki.cno, &blckGgcm))
            goto LEnd;
        pglPar = GL::PglRead(&blckPar, &boPar);
        pglSet = GL::PglRead(&blckSet, &boSet);
        pggCmid = GG::PggRead(&blckGgcm, &boGgcm);
        if (pglPar == pvNil || pglSet == pvNil || pggCmid == pvNil ||
            pglPar->CbEntry() != SIZEOF(int16_t) || pglSet->CbEntry() != SIZEOF(int16_t) ||
            pggCmid->CbFixed() != SIZEOF(int32_t) || pglPar->IvMac() != pglSet->IvMac())
            goto LEnd;
        if (boPar == kboOther && pglPar->IvMac() > 0)
            SwapBytesRgsw(pglPar->QvGet(0), pglPar->IvMac());
        if (boSet == kboOther && pglSet->IvMac() > 0)
            SwapBytesRgsw(pglSet->QvGet(0), pglSet->IvMac());
        if (boGgcm == kboOther)
        {
            for (int32_t ibset = 0; ibset < pggCmid->IvMac(); ++ibset)
            {
                SwapBytesRglw(pggCmid->QvFixedGet(ibset), 1);
                SwapBytesRglw(pggCmid->QvGet(ibset), *(int32_t *)pggCmid->QvFixedGet(ibset));
            }
        }
    }

    cpartOld = pglPar->IvMac();
    cbsetOld = pggCmid->IvMac();
    if (cpartOld <= 0 || cpartOld + cpartAdd > kc4DMMActorStudioFramePartMax)
        goto LEnd;

    idImport = Id4DMMNextCustomImport(_rg4DMMCustomPart, _c4DMMCustomPart, idObject);
    cmeta = C4DMMBuildCustomImportMetadata(this, idObject, idGroup > 0 ? idGroup : 0,
                                           idImport, cpartOld, prgpart, cpartAdd, rgmeta,
                                           kc4DMMActorStudioFramePartMax);
    if (cmeta <= 0 || _c4DMMCustomPart + cmeta > kc4DMMCustomPartMax)
        goto LEnd;

    // Find the next free resource IDs. Actor Studio deletion deliberately
    // keeps old immutable BMDL children instead of renumbering them, so BODY
    // part index is NOT a safe model CHID after an imported range is deleted.
    // Normal Add-to-object therefore appends above the highest surviving BMDL
    // CHID. Template mode below intentionally preserves the old CHID==part
    // behavior because that historical collision is the requested template
    // replacement effect.
    for (int32_t ikid = 0; pcfl->FGetKid(kctgTmpl, cnoTmpl, ikid, &kid); ++ikid)
    {
        if (kid.cki.ctg == kctgCmtl && kid.chid >= cmidNext)
            cmidNext = kid.chid + 1;
        if (kid.cki.ctg == kctgBmdl && kid.chid >= 0 && kid.chid >= chidModelNext)
            chidModelNext = kid.chid + 1;
    }
    MultiLog(this,
        "create_part_add_resource_ids object=%ld mode=%s old_parts=%ld next_model_chid=%ld next_cmid=%ld",
        (long)idObject, fUseVacatedModelChids ? "template" : "add",
        (long)cpartOld, (long)chidModelNext, (long)cmidNext);

    for (cactn = 0; pcfl->FGetKidChidCtg(kctgTmpl, cnoTmpl, cactn, kctgActn, &kid); ++cactn)
        ;
    if (cactn <= 0 ||
        !FAllocPv((void **)&prgAction, LwMul(cactn, SIZEOF(CUSTOMAPPENDACTIONWRITE)), fmemClear, mprNormal) ||
        !FAllocPv((void **)&prgResource, LwMul(cpartAdd, SIZEOF(CUSTOMAPPENDRESOURCE)), fmemClear, mprNormal))
        goto LEnd;

    // Append the captured BODY topology, preserving parent relationships
    // inside this import. Root-level source nodes attach to the destination
    // BODY root; child/group nodes point at their newly appended parent.
    for (int32_t iadd = 0; iadd < cpartAdd; ++iadd)
    {
        const int32_t ipartNew = cpartOld + iadd;
        const int32_t ibsetNew = cbsetOld + iadd;
        const int32_t cmidNew = cmidNext + iadd;
        const int32_t chidModelNew = prgpart[iadd].pmodl == pvNil
                                         ? (int32_t)chidNil
                                         : (fUseVacatedModelChids ? ipartNew : chidModelNext++);
        const int32_t ipartParentRelative = prgpart[iadd].ipartParent;
        if (ipartParentRelative != ivNil && !FIn(ipartParentRelative, 0, iadd))
        {
            MultiLog(this,
                "create_part_add_hierarchy FAIL object=%ld add_part=%ld relative_parent=%ld",
                (long)idObject, (long)iadd, (long)ipartParentRelative);
            goto LRollback;
        }
        const int32_t ipartParentAbsolute = ipartParentRelative == ivNil
                                                ? ivNil
                                                : cpartOld + ipartParentRelative;
        int16_t ipar = ipartParentAbsolute == ivNil
                           ? (int16_t)ivNil
                           : (int16_t)ipartParentAbsolute;
        int16_t ibset = (int16_t)ibsetNew;
        int32_t ccmid = 1;
        CNO cnoModel = cnoNil;
        CNO cnoMtrl = cnoNil;
        CNO cnoCmtl = cnoNil;
        CMTLF cmtlf;
        ClearPb(&cmtlf, SIZEOF(cmtlf));
        cmtlf.bo = kboCur;
        cmtlf.osk = koskCur;
        cmtlf.ibset = ibsetNew;

        if (ibsetNew > 0x7fff || cmidNew > 0x7fff || ipartNew > 0x7fff ||
            (chidModelNew != (int32_t)chidNil && chidModelNew > 0x7fff) ||
            (ipartParentAbsolute != ivNil && ipartParentAbsolute > 0x7fff) ||
            !pglPar->FAdd(&ipar, pvNil) || !pglSet->FAdd(&ibset, pvNil) ||
            !pggCmid->FAdd(SIZEOF(cmidNew), pvNil, &cmidNew, &ccmid))
            goto LRollback;

        if (prgpart[iadd].pmodl != pvNil)
        {
            if (!F4DMMCloneResourceTree(prgpart[iadd].pmodl->Pcrf(), kctgBmdl,
                                         prgpart[iadd].pmodl->Cno(), pcfl, &cnoModel))
            {
                uint8_t bPlaceholder = 0;
                if (!pcfl->FAddPv(&bPlaceholder, 1, kctgBmdl, &cnoModel) ||
                    !prgpart[iadd].pmodl->FWrite(pcfl, kctgBmdl, cnoModel))
                    goto LRollback;
            }
            // Template mode intentionally keeps v181's old model CHID rule.
            // If Actor Studio has deleted an imported BODY range, its old BMDL
            // children remain in the TMPL. Reusing ipartNew therefore recreates
            // the wonderfully cursed "new object wears the deleted object's
            // geometry" pathway as an explicit feature. Normal Add uses the
            // collision-free chidModelNext value instead.
            KID kidModelCollision;
            const bool fExistingModelChid = pcfl->FGetKidChidCtg(
                kctgTmpl, cnoTmpl, (CHID)chidModelNew, kctgBmdl, &kidModelCollision);
            if (!pcfl->FAdoptChild(kctgTmpl, cnoTmpl, kctgBmdl, cnoModel, (CHID)chidModelNew))
                goto LRollback;
            prgResource[iadd].cnoModel = cnoModel;
            prgResource[iadd].chidModel = chidModelNew;
            prgResource[iadd].fModelAdopted = fTrue;
            MultiLog(this,
                "create_part_add_model_resource object=%ld mode=%s new_part=%ld model_chid=%ld collision_before=%d collision_cno=%ld new_cno=%ld",
                (long)idObject, fUseVacatedModelChids ? "template" : "add",
                (long)ipartNew, (long)chidModelNew, fExistingModelChid ? 1 : 0,
                fExistingModelChid ? (long)kidModelCollision.cki.cno : -1L, (long)cnoModel);
        }

        if (prgpart[iadd].pmtrl == pvNil)
            goto LRollback;
        if (!F4DMMCloneResourceTree(prgpart[iadd].pmtrl->Pcrf(), kctgMtrl,
                                     prgpart[iadd].pmtrl->Cno(), pcfl, &cnoMtrl) &&
            !prgpart[iadd].pmtrl->FWrite(pcfl, kctgMtrl, &cnoMtrl))
            goto LRollback;
        if (!pcfl->FAddPv(&cmtlf, SIZEOF(cmtlf), kctgCmtl, &cnoCmtl) ||
            !pcfl->FAdoptChild(kctgCmtl, cnoCmtl, kctgMtrl, cnoMtrl, 0) ||
            !pcfl->FAdoptChild(kctgTmpl, cnoTmpl, kctgCmtl, cnoCmtl, (CHID)cmidNew))
            goto LRollback;
        prgResource[iadd].cnoCmtl = cnoCmtl;
        prgResource[iadd].chidCmtl = cmidNew;
        prgResource[iadd].fCmtlAdopted = fTrue;

        MultiLog(this,
            "create_part_add_hierarchy object=%ld mode=%s add_part=%ld new_part=%ld relative_parent=%ld absolute_parent=%ld model=%d model_chid=%ld source_arid=%ld source_part=%ld",
            (long)idObject, fUseVacatedModelChids ? "template" : "add",
            (long)iadd, (long)ipartNew,
            (long)ipartParentRelative, (long)ipartParentAbsolute,
            prgpart[iadd].pmodl != pvNil ? 1 : 0, (long)chidModelNew,
            (long)prgpart[iadd].aridSource, (long)prgpart[iadd].ipartSource);
    }

    // Every existing action gains the imported parts. Their imported pose is
    // intentionally frozen across every existing frame until the user edits
    // those parts in Actor Studio.
    for (int32_t anid = 0; anid < cactn; ++anid)
    {
        KID kidAction;
        PGG pggcel = pvNil;
        PGG pggcelNew = pvNil;
        PGL pglbmat34 = pvNil;
        CPS rgcpsAdd[kc4DMMActorStudioFramePartMax];
        if (!pcfl->FGetKidChidCtg(kctgTmpl, cnoTmpl, anid, kctgActn, &kidAction) ||
            !F4DMMActorStudioReadActionGroups(pcfl, kidAction.cki.cno, &pggcel, &pglbmat34) ||
            pggcel->IvMac() <= 0)
        {
            ReleasePpo(&pggcel);
            ReleasePpo(&pglbmat34);
            goto LRollback;
        }
        pggcelNew = GG::PggNew(SIZEOF(CEL));
        if (pggcelNew == pvNil)
        {
            ReleasePpo(&pggcel);
            ReleasePpo(&pglbmat34);
            goto LRollback;
        }
        prgAction[anid].cnoAction = kidAction.cki.cno;
        for (int32_t iadd = 0; iadd < cpartAdd; ++iadd)
        {
            int32_t imat = ivNil;
            if (!pglbmat34->FAdd(&prgpart[iadd].bmat34, &imat) || imat > 0x7fff)
            {
                ReleasePpo(&pggcelNew);
                ReleasePpo(&pggcel);
                ReleasePpo(&pglbmat34);
                goto LRollback;
            }
            ClearPb(&rgcpsAdd[iadd], SIZEOF(CPS));
            rgcpsAdd[iadd].chidModl = prgpart[iadd].pmodl != pvNil
                                         ? (int16_t)prgResource[iadd].chidModel
                                         : (int16_t)chidNil;
            rgcpsAdd[iadd].imat34 = (int16_t)imat;
        }
        for (int32_t icel = 0; icel < pggcel->IvMac(); ++icel)
        {
            const int32_t cpartCel = pggcel->Cb(icel) / SIZEOF(CPS);
            CPS rgcps[kc4DMMActorStudioFramePartMax];
            CEL cel;
            if (cpartCel != cpartOld)
            {
                ReleasePpo(&pggcelNew);
                ReleasePpo(&pggcel);
                ReleasePpo(&pglbmat34);
                goto LRollback;
            }
            CopyPb(pggcel->QvGet(icel), rgcps, LwMul(cpartOld, SIZEOF(CPS)));
            CopyPb(rgcpsAdd, &rgcps[cpartOld], LwMul(cpartAdd, SIZEOF(CPS)));
            cel = *(CEL *)pggcel->QvFixedGet(icel);

            // As with Actor Studio Duplicate, expand into a fresh GG rather
            // than resizing live variable-length records in place. This also
            // keeps the source action untouched until its complete replacement
            // has been written and adopted.
            if (!pggcelNew->FAdd(LwMul(cpartOld + cpartAdd, SIZEOF(CPS)), pvNil, rgcps, &cel))
            {
                ReleasePpo(&pggcelNew);
                ReleasePpo(&pggcel);
                ReleasePpo(&pglbmat34);
                goto LRollback;
            }
        }
        if (!pcfl->FAdd(pglbmat34->CbOnFile(), kctgGlxf, &prgAction[anid].cnoXfNew, &blck) ||
            !pglbmat34->FWrite(&blck) ||
            !pcfl->FAdd(pggcelNew->CbOnFile(), kctgGgcl, &prgAction[anid].cnoCelNew, &blck) ||
            !pggcelNew->FWrite(&blck))
        {
            ReleasePpo(&pggcelNew);
            ReleasePpo(&pggcel);
            ReleasePpo(&pglbmat34);
            goto LRollback;
        }
        ReleasePpo(&pggcelNew);
        ReleasePpo(&pggcel);
        ReleasePpo(&pglbmat34);
    }

    if (!F4DMMWriteGlChild(pcfl, kctgGlpi, pglPar, &cnoParNew) ||
        !F4DMMWriteGlChild(pcfl, kctgGlbs, pglSet, &cnoSetNew) ||
        !F4DMMWriteGgChild(pcfl, kctgGgcm, pggCmid, &cnoGgcmNew))
        goto LRollback;

    for (int32_t anid = 0; anid < cactn; ++anid)
    {
        if (!F4DMMActorStudioReplacePoseChildren(pcfl, prgAction[anid].cnoAction,
                                                  prgAction[anid].cnoCelNew,
                                                  prgAction[anid].cnoXfNew,
                                                  &prgAction[anid].cnoCelOld,
                                                  &prgAction[anid].cnoXfOld))
            goto LRollback;
        prgAction[anid].fReplaced = fTrue;
    }
    if (!F4DMMReplaceTmplChildForPartAppend(pcfl, cnoTmpl, kctgGlpi, cnoParNew, &cnoParOld))
        goto LRollback;
    fParReplaced = fTrue;
    if (!F4DMMReplaceTmplChildForPartAppend(pcfl, cnoTmpl, kctgGlbs, cnoSetNew, &cnoSetOld))
        goto LRollback;
    fSetReplaced = fTrue;
    if (!F4DMMReplaceTmplChildForPartAppend(pcfl, cnoTmpl, kctgGgcm, cnoGgcmNew, &cnoGgcmOld))
        goto LRollback;
    fGgcmReplaced = fTrue;

    for (int32_t anid = 0; anid < cactn; ++anid)
        _pcrfAutoSave->FSetCrep(crepToss, kctgActn, prgAction[anid].cnoAction, ACTN::FReadActn);
    _pcrfAutoSave->FSetCrep(crepToss, kctgTmpl, cnoTmpl, TMPL::FReadTmpl);

    // If the mutable TMPL is already live, refresh it in place and make every
    // current-scene occurrence conform immediately.
    {
        PTMPL ptmplLive = (PTMPL)_pcrfAutoSave->PbacoFetch(kctgTmpl, cnoTmpl, TMPL::FReadTmpl);
        if (ptmplLive == pvNil || !ptmplLive->FRefreshPartShape())
        {
            ReleasePpo(&ptmplLive);
            goto LRollback;
        }

        // Verify the newly-authored GLPI topology through the same BODY loader
        // that Actor Studio will use. A malformed parent index is a rollback,
        // not something we allow to become persistent movie metadata.
        PBODY pbodyVerify = ptmplLive->PbodyCreate();
        bool fHierarchyVerify = pbodyVerify != pvNil &&
                                pbodyVerify->Cpart() == cpartOld + cpartAdd;
        if (fHierarchyVerify)
        {
            for (int32_t iadd = 0; iadd < cpartAdd; ++iadd)
            {
                const int32_t ipartExpectedParent = prgpart[iadd].ipartParent == ivNil
                                                        ? ivNil
                                                        : cpartOld + prgpart[iadd].ipartParent;
                const int32_t ipartLoadedParent = pbodyVerify->IpartParent(cpartOld + iadd);
                if (ipartLoadedParent != ipartExpectedParent)
                {
                    MultiLog(this,
                        "create_part_add_hierarchy verify_mismatch object=%ld part=%ld expected_parent=%ld loaded_parent=%ld",
                        (long)idObject, (long)(cpartOld + iadd),
                        (long)ipartExpectedParent, (long)ipartLoadedParent);
                    fHierarchyVerify = fFalse;
                    break;
                }
            }
        }
        ReleasePpo(&pbodyVerify);
        ReleasePpo(&ptmplLive);
        if (!fHierarchyVerify)
            goto LRollback;
    }
    if (Pscen() != pvNil && Pscen()->PglRollCall() != pvNil)
    {
        PGL pglpactr = Pscen()->PglRollCall();
        for (int32_t iactr = 0; iactr < pglpactr->IvMac(); ++iactr)
        {
            PACTR pactrLive = pvNil;
            pglpactr->Get(iactr, &pactrLive);
            if (pactrLive == pvNil)
                continue;
            TAG tagLive;
            pactrLive->GetTagTmpl(&tagLive);
            if (tagLive.sid != ksidUseCrf || tagLive.ctg != kctgTmpl || tagLive.cno != cnoTmpl)
                continue;
            TAG tagReload = tagLive;
            if (!pactrLive->FChangeTagTmpl(&tagReload))
                goto LRollback;
        }
    }

    for (int32_t imeta = 0; imeta < cmeta; ++imeta)
        _rg4DMMCustomPart[_c4DMMCustomPart++] = rgmeta[imeta];
    _fRequiresVmmSave = fTrue;
    _fAutosaveDirty = fTrue;
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    MultiLog(this,
        "create_part_add_to_object ok object=%ld mode=%s tmpl=%ld source_group=%ld source_arid=%ld old_parts=%ld added=%ld new_parts=%ld",
        (long)idObject, fUseVacatedModelChids ? "template" : "add",
        (long)cnoTmpl, (long)idGroup, (long)aridSource,
        (long)cpartOld, (long)cpartAdd, (long)(cpartOld + cpartAdd));
    fRet = fTrue;
    goto LEnd;

LRollback:
    // Restore authoritative template children before restoring action children.
    if (fGgcmReplaced && cnoGgcmOld != cnoNil)
    {
        CNO cnoIgnore = cnoNil;
        F4DMMReplaceTmplChildForPartAppend(pcfl, cnoTmpl, kctgGgcm, cnoGgcmOld, &cnoIgnore);
        fGgcmReplaced = fFalse;
    }
    if (fSetReplaced && cnoSetOld != cnoNil)
    {
        CNO cnoIgnore = cnoNil;
        F4DMMReplaceTmplChildForPartAppend(pcfl, cnoTmpl, kctgGlbs, cnoSetOld, &cnoIgnore);
        fSetReplaced = fFalse;
    }
    if (fParReplaced && cnoParOld != cnoNil)
    {
        CNO cnoIgnore = cnoNil;
        F4DMMReplaceTmplChildForPartAppend(pcfl, cnoTmpl, kctgGlpi, cnoParOld, &cnoIgnore);
        fParReplaced = fFalse;
    }
    if (prgAction != pvNil)
    {
        for (int32_t anid = cactn - 1; anid >= 0; --anid)
        {
            if (!prgAction[anid].fReplaced)
                continue;
            CNO cnoIgnoreCel = cnoNil;
            CNO cnoIgnoreXf = cnoNil;
            F4DMMActorStudioReplacePoseChildren(pcfl, prgAction[anid].cnoAction,
                                                  prgAction[anid].cnoCelOld,
                                                  prgAction[anid].cnoXfOld,
                                                  &cnoIgnoreCel, &cnoIgnoreXf);
            prgAction[anid].fReplaced = fFalse;
        }
    }
    if (prgResource != pvNil && pcfl != pvNil)
    {
        for (int32_t iadd = cpartAdd - 1; iadd >= 0; --iadd)
        {
            if (prgResource[iadd].fCmtlAdopted)
                pcfl->DeleteChild(kctgTmpl, cnoTmpl, kctgCmtl, prgResource[iadd].cnoCmtl,
                                  (CHID)prgResource[iadd].chidCmtl);
            if (prgResource[iadd].fModelAdopted)
                pcfl->DeleteChild(kctgTmpl, cnoTmpl, kctgBmdl, prgResource[iadd].cnoModel,
                                  (CHID)prgResource[iadd].chidModel);
        }
    }
    if (_pcrfAutoSave != pvNil && cnoTmpl != cnoNil)
    {
        _pcrfAutoSave->FSetCrep(crepToss, kctgTmpl, cnoTmpl, TMPL::FReadTmpl);
        // A failure can occur after a live mutable TMPL was refreshed. Restore
        // that same live object from the authoritative children we just put
        // back so a failed Add-to-object cannot leave Actor Studio displaying
        // topology that was rolled back on disk.
        PTMPL ptmplRollback = (PTMPL)_pcrfAutoSave->PbacoFetch(kctgTmpl, cnoTmpl, TMPL::FReadTmpl);
        if (ptmplRollback != pvNil)
        {
            ptmplRollback->FRefreshPartShape();
            ReleasePpo(&ptmplRollback);
        }
    }
    MultiLog(this,
        "create_part_add_to_object rollback object=%ld mode=%s tmpl=%ld source_group=%ld source_arid=%ld",
        (long)idObject, fUseVacatedModelChids ? "template" : "add",
        (long)cnoTmpl, (long)idGroup, (long)aridSource);

LEnd:
    Release4DMMCustomBakeParts(&prgpart, cpartAdd);
    ReleasePpo(&pglPar);
    ReleasePpo(&pglSet);
    ReleasePpo(&pggCmid);
    FreePpv((void **)&prgAction);
    FreePpv((void **)&prgResource);
    return fRet;
}

PASUN ASUN::PasunNew(void)
{
    PASUN pasun = NewObj ASUN();
    AssertNilOrPo(pasun, 0);
    return pasun;
}

void ASUN::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    pstn->SetSz(PszLit("Actor Studio Pose"));
}

bool ASUN::FDo(PDOCB pdocb)
{
    return FUndo(pdocb);
}

bool ASUN::FUndo(PDOCB pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);
    if (_pmvie == pvNil || _anid < 0 || _cnoAction == cnoNil)
    {
        MVIE::MultiLog(_pmvie,
            "actor_studio_pose_undo fail stage=state arid=%ld action=%ld actn_cno=%ld",
            (long)_arid, (long)_anid, (long)_cnoAction);
        return fFalse;
    }

    PACTR pactr = pvNil;
    if (_arid == aridNil)
    {
#if defined(KAUAI_WIN32)
        // Actor Studio deliberately edits movie-owned custom objects through a
        // detached ACTR.  That actor has no SCEN/ARID, so scene navigation is
        // both unnecessary and invalid.  Resolve the currently open detached
        // Actor Studio target by its writable TMPL instead.
        pactr = Pactr4DMMActorStudioUndoTarget(_pmvie, _cnoTmpl);
#endif
        if (pactr == pvNil)
        {
            MVIE::MultiLog(_pmvie,
                "actor_studio_pose_undo fail stage=find_detached tmpl=%ld",
                (long)_cnoTmpl);
            return fFalse;
        }
    }
    else
    {
        if (!_pmvie->FSwitchScen(_iscen))
        {
            MVIE::MultiLog(_pmvie,
                "actor_studio_pose_undo fail stage=switch_scene scene=%ld frame=%ld",
                (long)_iscen, (long)_nfrm);
            return fFalse;
        }
        if (_pmvie->Pscen() == pvNil || !_pmvie->Pscen()->FGotoFrm(_nfrm))
        {
            MVIE::MultiLog(_pmvie,
                "actor_studio_pose_undo fail stage=goto_frame scene=%ld frame=%ld",
                (long)_iscen, (long)_nfrm);
            return fFalse;
        }

        pactr = _pmvie->Pscen()->PactrFromArid(_arid);
        if (pactr == pvNil)
        {
            MVIE::MultiLog(_pmvie,
                "actor_studio_pose_undo fail stage=find_actor arid=%ld",
                (long)_arid);
            return fFalse;
        }
    }
    TAG tag;
    pactr->GetTagTmpl(&tag);
    if (tag.sid != ksidUseCrf || tag.cno != _cnoTmpl || tag.pcrf == pvNil)
    {
        MVIE::MultiLog(_pmvie,
            "actor_studio_pose_undo fail stage=template arid=%ld sid=%ld tmpl=%ld expected=%ld pcrf=%p",
            (long)_arid, (long)tag.sid, (long)tag.cno, (long)_cnoTmpl, tag.pcrf);
        return fFalse;
    }

    const BMAT34 *pbmat34Apply = _fApplyBefore ? &_bmat34Before : &_bmat34After;
    MVIE::MultiLog(_pmvie,
        "actor_studio_pose_undo begin arid=%ld tmpl=%ld action=%ld actn_cno=%ld cel=%ld part=%ld apply=%s selection_kind=%ld first=%ld selected_parts=%ld",
        (long)_arid, (long)_cnoTmpl, (long)_anid, (long)_cnoAction,
        (long)_celn, (long)_ipart, _fApplyBefore ? "before" : "after",
        (long)_selectionKind, (long)_ipartSelectionFirst, (long)_cpartSelection);

    if (!_pmvie->FActorStudioSetPartMatrixCore(pactr, _anid, _celn, _ipart,
                                                pbmat34Apply, fFalse))
    {
        MVIE::MultiLog(_pmvie,
            "actor_studio_pose_undo fail stage=apply arid=%ld action=%ld cel=%ld part=%ld apply=%s",
            (long)_arid, (long)_anid, (long)_celn, (long)_ipart,
            _fApplyBefore ? "before" : "after");
        return fFalse;
    }

    _fApplyBefore = !_fApplyBefore;
    MVIE::MultiLog(_pmvie,
        "actor_studio_pose_undo ok arid=%ld action=%ld cel=%ld part=%ld next=%s",
        (long)_arid, (long)_anid, (long)_celn, (long)_ipart,
        _fApplyBefore ? "before" : "after");
#if defined(KAUAI_WIN32)
    Refresh4DMMActorStudioAfterActionEditSelection(
        _pmvie, _arid, _anid, _celn, _ipart,
        _selectionKind, _ipartSelectionFirst, _cpartSelection);
#endif
    return fTrue;
}

#ifdef DEBUG
void ASUN::AssertValid(uint32_t grf)
{
    ASUN_PAR::AssertValid(grf);
}
#endif

PASCU ASCU::PascuNew(void)
{
    PASCU pascu = NewObj ASCU();
    AssertNilOrPo(pascu, 0);
    return pascu;
}

ASCU::~ASCU(void)
{
    FreePpv((void **)&_prgipart);
    FreePpv((void **)&_prgchidBefore);
    FreePpv((void **)&_prgchidAfter);
}

bool ASCU::FSetState(int32_t arid, int32_t anid, int32_t celnFirst,
                     const int32_t *prgipart, int32_t cpartPresence, CNO cnoTmpl,
                     const int16_t *prgchidBefore, const int16_t *prgchidAfter,
                     int32_t cchid, bool fCutOperation, int32_t selectionKind,
                     int32_t ipartSelectionFirst, int32_t cpartSelection)
{
    if (prgipart == pvNil || cpartPresence <= 0 ||
        cpartPresence > kc4DMMActorStudioFramePartMax ||
        prgchidBefore == pvNil || prgchidAfter == pvNil || cchid <= 0)
        return fFalse;
    const int32_t cvalue = LwMul(cpartPresence, cchid);
    if (cvalue <= 0 ||
        !FAllocPv((void **)&_prgipart, LwMul(cpartPresence, SIZEOF(int32_t)), fmemNil, mprNormal) ||
        !FAllocPv((void **)&_prgchidBefore, LwMul(cvalue, SIZEOF(int16_t)), fmemNil, mprNormal) ||
        !FAllocPv((void **)&_prgchidAfter, LwMul(cvalue, SIZEOF(int16_t)), fmemNil, mprNormal))
    {
        FreePpv((void **)&_prgipart);
        FreePpv((void **)&_prgchidBefore);
        FreePpv((void **)&_prgchidAfter);
        return fFalse;
    }
    CopyPb(prgipart, _prgipart, LwMul(cpartPresence, SIZEOF(int32_t)));
    CopyPb(prgchidBefore, _prgchidBefore, LwMul(cvalue, SIZEOF(int16_t)));
    CopyPb(prgchidAfter, _prgchidAfter, LwMul(cvalue, SIZEOF(int16_t)));
    _arid = arid;
    _anid = anid;
    _celnFirst = celnFirst;
    _ipart = prgipart[0];
    _cpartPresence = cpartPresence;
    _cnoTmpl = cnoTmpl;
    _cchid = cchid;
    _fApplyBefore = fTrue;
    _fCutOperation = FPure(fCutOperation);
    _selectionKind = selectionKind;
    _ipartSelectionFirst = ipartSelectionFirst;
    _cpartSelection = cpartSelection;
    return fTrue;
}

void ASCU::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    if (_selectionKind == kasskDefaultPartGroup)
        pstn->SetSz(_fCutOperation ? PszLit("Cut Part Group") : PszLit("Spawn Part Group"));
    else if (_selectionKind == kasskObject)
        pstn->SetSz(_fCutOperation ? PszLit("Cut Object") : PszLit("Spawn Object"));
    else
        pstn->SetSz(_fCutOperation ? PszLit("Cut BODY Part") : PszLit("Spawn BODY Part"));
}

bool ASCU::FDo(PDOCB pdocb)
{
    return FUndo(pdocb);
}

bool ASCU::FUndo(PDOCB pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);
    if (_pmvie == pvNil || _cnoTmpl == cnoNil || _anid < 0 || _celnFirst < 0 ||
        _ipart < 0 || _cpartPresence <= 0 || _prgipart == pvNil || _cchid <= 0 ||
        _prgchidBefore == pvNil || _prgchidAfter == pvNil)
        return fFalse;

    PACTR pactr = pvNil;
    if (_arid == aridNil)
    {
#if defined(KAUAI_WIN32)
        pactr = Pactr4DMMActorStudioUndoTarget(_pmvie, _cnoTmpl);
#endif
    }
    else
    {
        if (!_pmvie->FSwitchScen(_iscen) || _pmvie->Pscen() == pvNil ||
            !_pmvie->Pscen()->FGotoFrm(_nfrm))
            return fFalse;
        pactr = _pmvie->Pscen()->PactrFromArid(_arid);
    }
    if (pactr == pvNil)
        return fFalse;

    TAG tag;
    pactr->GetTagTmpl(&tag);
    if (tag.sid != ksidUseCrf || tag.ctg != kctgTmpl || tag.cno != _cnoTmpl)
        return fFalse;

    const int16_t *prgchidApply = _fApplyBefore ? _prgchidBefore : _prgchidAfter;
    MVIE::MultiLog(_pmvie,
        "actor_studio_part_presence_undo begin tmpl=%ld action=%ld cel_first=%ld anchor_part=%ld presence_parts=%ld cels=%ld apply=%s op=%s",
        (long)_cnoTmpl, (long)_anid, (long)_celnFirst, (long)_ipart,
        (long)_cpartPresence, (long)_cchid,
        _fApplyBefore ? "before" : "after", _fCutOperation ? "cut" : "spawn");
    if (!_pmvie->FActorStudioSetPartsModelChidsCore(
            pactr, _anid, _celnFirst, _prgipart, _cpartPresence,
            prgchidApply, _cchid))
        return fFalse;

    _fApplyBefore = !_fApplyBefore;
#if defined(KAUAI_WIN32)
    Refresh4DMMActorStudioAfterActionEditSelection(
        _pmvie, _arid, _anid, _celnFirst, _ipart,
        _selectionKind, _ipartSelectionFirst, _cpartSelection);
#endif
    MVIE::MultiLog(_pmvie,
        "actor_studio_part_presence_undo ok action=%ld cel_first=%ld anchor_part=%ld presence_parts=%ld next=%s",
        (long)_anid, (long)_celnFirst, (long)_ipart, (long)_cpartPresence,
        _fApplyBefore ? "before" : "after");
    return fTrue;
}

#ifdef DEBUG
void ASCU::MarkMem(void)
{
    ASCU_PAR::MarkMem();
    MarkPv(_prgipart);
    MarkPv(_prgchidBefore);
    MarkPv(_prgchidAfter);
}

void ASCU::AssertValid(uint32_t grf)
{
    ASCU_PAR::AssertValid(grf);
}
#endif

PASDU ASDU::PasduNew(void)
{
    PASDU pasdu = NewObj ASDU();
    AssertNilOrPo(pasdu, 0);
    return pasdu;
}

void ASDU::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    pstn->SetSz(PszLit("Duplicate BODY Part"));
}

bool ASDU::FDo(PDOCB pdocb)
{
    return FUndo(pdocb);
}

bool ASDU::FUndo(PDOCB pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);
    if (_pmvie == pvNil || _cnoTmpl == cnoNil || _ipartSource < 0)
        return fFalse;

    PACTR pactr = pvNil;
    if (_arid == aridNil)
    {
#if defined(KAUAI_WIN32)
        pactr = Pactr4DMMActorStudioUndoTarget(_pmvie, _cnoTmpl);
#endif
    }
    else
    {
        if (!_pmvie->FSwitchScen(_iscen) || _pmvie->Pscen() == pvNil ||
            !_pmvie->Pscen()->FGotoFrm(_nfrm))
            return fFalse;
        pactr = _pmvie->Pscen()->PactrFromArid(_arid);
    }
    if (pactr == pvNil)
        return fFalse;

    TAG tag;
    pactr->GetTagTmpl(&tag);
    if (tag.sid != ksidUseCrf || tag.ctg != kctgTmpl || tag.cno != _cnoTmpl)
        return fFalse;

    bool fResult = fFalse;
    int32_t ipartSelect = _ipartSource;
    if (_fDuplicatePresent)
    {
        MVIE::MultiLog(_pmvie,
            "actor_studio_duplicate_undo begin tmpl=%ld source=%ld duplicate=%ld op=remove",
            (long)_cnoTmpl, (long)_ipartSource, (long)_ipartDuplicate);
        fResult = _pmvie->FActorStudioDeletePartRangeCore(pactr, _ipartDuplicate, 1, fFalse);
        if (fResult)
            _fDuplicatePresent = fFalse;
    }
    else
    {
        const BMAT34 *pbmat34Freeze = _fHaveFreeze ? &_bmat34Freeze : pvNil;
        int32_t ipartNew = ivNil;
        MVIE::MultiLog(_pmvie,
            "actor_studio_duplicate_undo begin tmpl=%ld source=%ld op=recreate keep_positions=%d freeze=%d",
            (long)_cnoTmpl, (long)_ipartSource, (int)_fKeepPositions, (int)_fHaveFreeze);
        fResult = _pmvie->FActorStudioDuplicatePartCore(pactr, _anid, _celn, _ipartSource,
                                                        _fKeepPositions, pbmat34Freeze,
                                                        &ipartNew, fFalse);
        if (fResult)
        {
            _ipartDuplicate = ipartNew;
            ipartSelect = ipartNew;
            _fDuplicatePresent = fTrue;
        }
    }
    if (!fResult)
    {
        MVIE::MultiLog(_pmvie,
            "actor_studio_duplicate_undo fail tmpl=%ld source=%ld duplicate=%ld present=%d",
            (long)_cnoTmpl, (long)_ipartSource, (long)_ipartDuplicate, (int)_fDuplicatePresent);
        return fFalse;
    }

#if defined(KAUAI_WIN32)
    Refresh4DMMActorStudioAfterTopologyEdit(_pmvie, _arid, _cnoTmpl,
                                             _anid, _celn, ipartSelect);
#endif
    MVIE::MultiLog(_pmvie,
        "actor_studio_duplicate_undo ok tmpl=%ld source=%ld duplicate=%ld present=%d",
        (long)_cnoTmpl, (long)_ipartSource, (long)_ipartDuplicate, (int)_fDuplicatePresent);
    return fTrue;
}

#ifdef DEBUG
void ASDU::AssertValid(uint32_t grf)
{
    ASDU_PAR::AssertValid(grf);
}
#endif

PASRU ASRU::PasruNew(void)
{
    PASRU pasru = NewObj ASRU();
    AssertNilOrPo(pasru, 0);
    return pasru;
}

void ASRU::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    if (_selectionKind == kasskGroup)
        pstn->SetSz(PszLit("Duplicate Object Group"));
    else if (_selectionKind == kasskObject)
        pstn->SetSz(PszLit("Duplicate Object"));
    else
        pstn->SetSz(PszLit("Duplicate BODY Part"));
}

bool ASRU::FDo(PDOCB pdocb)
{
    return FUndo(pdocb);
}

bool ASRU::FUndo(PDOCB pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);
    if (_pmvie == pvNil || _cnoTmpl == cnoNil || _ipartSource < 0 || _cpartSource <= 0)
        return fFalse;

    PACTR pactr = pvNil;
    if (_arid == aridNil)
    {
#if defined(KAUAI_WIN32)
        pactr = Pactr4DMMActorStudioUndoTarget(_pmvie, _cnoTmpl);
#endif
    }
    else
    {
        if (!_pmvie->FSwitchScen(_iscen) || _pmvie->Pscen() == pvNil ||
            !_pmvie->Pscen()->FGotoFrm(_nfrm))
            return fFalse;
        pactr = _pmvie->Pscen()->PactrFromArid(_arid);
    }
    if (pactr == pvNil)
        return fFalse;

    TAG tag;
    pactr->GetTagTmpl(&tag);
    if (tag.sid != ksidUseCrf || tag.ctg != kctgTmpl || tag.cno != _cnoTmpl)
        return fFalse;

    bool fResult = fFalse;
    int32_t ipartSelect = _ipartSource;
    if (_fDuplicatePresent)
    {
        MVIE::MultiLog(_pmvie,
            "actor_studio_range_duplicate_undo begin tmpl=%ld source_first=%ld parts=%ld duplicate_first=%ld op=remove",
            (long)_cnoTmpl, (long)_ipartSource, (long)_cpartSource, (long)_ipartDuplicate);
        fResult = _pmvie->FActorStudioDeletePartRangeCore(
            pactr, _ipartDuplicate, _cpartSource, fFalse);
        if (fResult)
            _fDuplicatePresent = fFalse;
    }
    else
    {
        int32_t ipartNewFirst = ivNil;
        MVIE::MultiLog(_pmvie,
            "actor_studio_range_duplicate_undo begin tmpl=%ld source_first=%ld parts=%ld op=recreate kind=%ld keep_positions=%d",
            (long)_cnoTmpl, (long)_ipartSource, (long)_cpartSource,
            (long)_selectionKind, (int)_fKeepPositions);
        fResult = _pmvie->FActorStudioDuplicatePartRangeCore(
            pactr, _anid, _celn, _ipartSource, _cpartSource, _selectionKind,
            _fKeepPositions, &ipartNewFirst, fFalse);
        if (fResult)
        {
            _ipartDuplicate = ipartNewFirst;
            ipartSelect = ipartNewFirst;
            _fDuplicatePresent = fTrue;
        }
    }
    if (!fResult)
        return fFalse;

#if defined(KAUAI_WIN32)
    Refresh4DMMActorStudioAfterTopologyEdit(_pmvie, _arid, _cnoTmpl,
                                             _anid, _celn, ipartSelect);
#endif
    MVIE::MultiLog(_pmvie,
        "actor_studio_range_duplicate_undo ok tmpl=%ld source_first=%ld parts=%ld duplicate_first=%ld present=%d",
        (long)_cnoTmpl, (long)_ipartSource, (long)_cpartSource,
        (long)_ipartDuplicate, (int)_fDuplicatePresent);
    return fTrue;
}

#ifdef DEBUG
void ASRU::AssertValid(uint32_t grf)
{
    ASRU_PAR::AssertValid(grf);
}
#endif

PASDE ASDE::PasdeNew(void)
{
    PASDE pasde = NewObj ASDE();
    AssertNilOrPo(pasde, 0);
    return pasde;
}

ASDE::~ASDE(void)
{
    if (_prgAction != pvNil)
    {
        for (int32_t anid = 0; anid < _cactn; ++anid)
        {
            ReleasePpo(&_prgAction[anid].pggcel);
            ReleasePpo(&_prgAction[anid].pglbmat34);
        }
    }
    FreePpv((void **)&_prgAction);
    FreePpv((void **)&_prgMeta);
    ReleasePpo(&_pggCmid);
    ReleasePpo(&_pglibset);
    ReleasePpo(&_pglibactPar);
}

void ASDE::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    if (_selectionKind == kasskGroup)
        pstn->SetSz(PszLit("Delete Object Group"));
    else if (_selectionKind == kasskObject)
        pstn->SetSz(PszLit("Delete Object"));
    else
        pstn->SetSz(PszLit("Delete BODY Part"));
}

bool ASDE::FDo(PDOCB pdocb)
{
    return FUndo(pdocb);
}

#ifdef DEBUG
void ASDE::MarkMem(void)
{
    ASDE_PAR::MarkMem();
    MarkMemObj(_pglibactPar);
    MarkMemObj(_pglibset);
    MarkMemObj(_pggCmid);
    MarkPv(_prgAction);
    MarkPv(_prgMeta);
    if (_prgAction != pvNil)
    {
        for (int32_t anid = 0; anid < _cactn; ++anid)
        {
            MarkMemObj(_prgAction[anid].pggcel);
            MarkMemObj(_prgAction[anid].pglbmat34);
        }
    }
}

void ASDE::AssertValid(uint32_t grf)
{
    ASDE_PAR::AssertValid(grf);
}
#endif

PASMU ASMU::PasmuNew(void)
{
    PASMU pasmu = NewObj ASMU();
    AssertNilOrPo(pasmu, 0);
    return pasmu;
}

void ASMU::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    pstn->SetSz(PszLit("Actor Studio Group Pose"));
}

bool ASMU::FDo(PDOCB pdocb)
{
    return FUndo(pdocb);
}

bool ASMU::FUndo(PDOCB pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);
    if (_pmvie == pvNil || _anid < 0 || _cnoAction == cnoNil ||
        _cpart <= 0 || _cpart > kc4DMMActorStudioFramePartMax)
        return fFalse;

    PACTR pactr = pvNil;
    if (_arid == aridNil)
    {
#if defined(KAUAI_WIN32)
        pactr = Pactr4DMMActorStudioUndoTarget(_pmvie, _cnoTmpl);
#endif
    }
    else
    {
        if (!_pmvie->FSwitchScen(_iscen) || _pmvie->Pscen() == pvNil ||
            !_pmvie->Pscen()->FGotoFrm(_nfrm))
            return fFalse;
        pactr = _pmvie->Pscen()->PactrFromArid(_arid);
    }
    if (pactr == pvNil)
        return fFalse;

    TAG tag;
    pactr->GetTagTmpl(&tag);
    if (tag.sid != ksidUseCrf || tag.cno != _cnoTmpl || tag.pcrf == pvNil)
        return fFalse;

    const BMAT34 *prgbmat34Apply = _fApplyBefore ? _rgbmat34Before : _rgbmat34After;
    MVIE::MultiLog(_pmvie,
        "actor_studio_group_pose_undo begin arid=%ld tmpl=%ld action=%ld cel=%ld parts=%ld apply=%s selection_kind=%ld first=%ld selected_parts=%ld",
        (long)_arid, (long)_cnoTmpl, (long)_anid, (long)_celn, (long)_cpart,
        _fApplyBefore ? "before" : "after", (long)_selectionKind,
        (long)_ipartSelectionFirst, (long)_cpartSelection);
    if (!_pmvie->FActorStudioSetPartMatricesCore(pactr, _anid, _celn,
                                                  _rgipart, prgbmat34Apply,
                                                  _cpart, fFalse))
        return fFalse;

    _fApplyBefore = !_fApplyBefore;
#if defined(KAUAI_WIN32)
    Refresh4DMMActorStudioAfterActionEditSelection(
        _pmvie, _arid, _anid, _celn, _rgipart[0],
        _selectionKind, _ipartSelectionFirst, _cpartSelection);
#endif
    MVIE::MultiLog(_pmvie,
        "actor_studio_group_pose_undo ok action=%ld cel=%ld parts=%ld next=%s",
        (long)_anid, (long)_celn, (long)_cpart,
        _fApplyBefore ? "before" : "after");
    return fTrue;
}

#ifdef DEBUG
void ASMU::AssertValid(uint32_t grf)
{
    ASMU_PAR::AssertValid(grf);
}
#endif

static bool F4DMMActorStudioActionChildIndependent(PCFL pcfl, CNO cnoTmpl, int32_t anid,
                                                       int32_t cactn, CNO *pcnoAction,
                                                       int32_t *panidAlias = pvNil)
{
    if (pcnoAction != pvNil)
        *pcnoAction = cnoNil;
    if (panidAlias != pvNil)
        *panidAlias = ivNil;
    if (pcfl == pvNil || cnoTmpl == cnoNil || anid < 0 || anid >= cactn)
        return fFalse;

    KID kidAction;
    if (!pcfl->FGetKidChidCtg(kctgTmpl, cnoTmpl, anid, kctgActn, &kidAction))
        return fFalse;

    // v113-v115 could adopt the same ACTN chunk under a second action CHID.
    // Those aliases are not independent custom animations even if stale 4DCT
    // metadata says they are.  A writable action is editable only when its
    // ACTN CNO is unique within the TMPL.  v116's FClone path satisfies this.
    for (int32_t anidOther = 0; anidOther < cactn; ++anidOther)
    {
        if (anidOther == anid)
            continue;
        KID kidOther;
        if (pcfl->FGetKidChidCtg(kctgTmpl, cnoTmpl, anidOther, kctgActn, &kidOther) &&
            kidOther.cki.cno == kidAction.cki.cno)
        {
            if (panidAlias != pvNil)
                *panidAlias = anidOther;
            return fFalse;
        }
    }

    if (pcnoAction != pvNil)
        *pcnoAction = kidAction.cki.cno;
    return fTrue;
}

bool MVIE::FActorStudioActionIsCustom(PACTR pactr, int32_t anid) const
{
    if (pactr == pvNil || pactr->Ptmpl() == pvNil || anid < 0 || anid >= pactr->Ptmpl()->Cactn())
        return fFalse;
    TAG tag;
    pactr->GetTagTmpl(&tag);
    if (tag.sid != ksidUseCrf)
        return fFalse;

    bool fRecordedCustom = fFalse;
    for (int32_t i = 0; i < _c4DMMCustomAction; ++i)
    {
        if (_rg4DMMCustomAction[i].cnoTmpl == tag.cno && _rg4DMMCustomAction[i].anid == anid)
        {
            fRecordedCustom = fTrue;
            break;
        }
    }
    if (!fRecordedCustom)
        return fFalse;

    // A writable TMPL cloned from a stock actor still contains every stock
    // action at its original action ID. Older Actor Studio metadata could
    // mistakenly record some of those inherited IDs as custom (the visible
    // symptom was stock actions past the old A-Z range acquiring ` *`). If we
    // know the writable template's stock source, its original action count is
    // authoritative: real Save-As actions are appended after that boundary.
    for (int32_t i = 0; i < _c4DMMCustomObject; ++i)
    {
        const CUSTOMOBJECT &obj = _rg4DMMCustomObject[i];
        if (obj.cnoOwnedTmpl != tag.cno || obj.templateKind != kctkDefault ||
            obj.sidTemplate <= 0 || obj.ctgTemplate == ctgNil || obj.cnoTemplate == cnoNil)
            continue;

        TAG tagSource;
        ClearPb(&tagSource, SIZEOF(tagSource));
        tagSource.sid = obj.sidTemplate;
        tagSource.pcrf = pvNil;
        tagSource.ctg = obj.ctgTemplate;
        tagSource.cno = obj.cnoTemplate;
        PTMPL ptmplSource = vptagm != pvNil
            ? (PTMPL)vptagm->PbacoFetch(&tagSource, TMPL::FReadTmpl)
            : pvNil;
        if (ptmplSource != pvNil)
        {
            const int32_t cactnStock = ptmplSource->Cactn();
            ReleasePpo(&ptmplSource);
            if (anid < cactnStock)
                return fFalse;
        }
        break;
    }

    PCRF pcrf = tag.pcrf != pvNil ? tag.pcrf : _pcrfAutoSave;
    PCFL pcfl = pcrf != pvNil ? pcrf->Pcfl() : pvNil;
    // A stale v113-v115 metadata record is not enough. Those versions could
    // attach the same ACTN CNO under multiple action IDs, which made stock
    // actions appear as custom and put stray asterisks at the top of Actor
    // Studio. Only an independently owned ACTN can be edited as custom.
    return F4DMMActorStudioActionChildIndependent(pcfl, tag.cno, anid,
                                                   pactr->Ptmpl()->Cactn(), pvNil);
}

static bool F4DMMActorStudioTrimActionToFirstFrame(PCFL pcfl, CNO cnoAction)
{
    if (pcfl == pvNil || cnoAction == cnoNil)
        return fFalse;
    KID kidCel;
    BLCK blck;
    int16_t bo = kboCur;
    PGG pggcel = pvNil;
    CNO cnoCelNew = cnoNil;
    bool fRet = fFalse;
    bool fOldDetached = fFalse;

    if (!pcfl->FGetKidChidCtg(kctgActn, cnoAction, 0, kctgGgcl, &kidCel) ||
        !pcfl->FFind(kidCel.cki.ctg, kidCel.cki.cno, &blck))
        goto LEnd;
    pggcel = GG::PggRead(&blck, &bo);
    if (pggcel == pvNil || pggcel->IvMac() <= 0)
        goto LEnd;
    if (kboOther == bo)
    {
        AssertBomRglw(kbomCel, SIZEOF(CEL));
        AssertBomRgsw(kbomCps, SIZEOF(CPS));
        for (int32_t icel = 0; icel < pggcel->IvMac(); ++icel)
        {
            SwapBytesRglw(pggcel->QvFixedGet(icel), SIZEOF(CEL) / SIZEOF(int32_t));
            SwapBytesRgsw(pggcel->QvGet(icel), pggcel->Cb(icel) / SIZEOF(int16_t));
        }
    }
    while (pggcel->IvMac() > 1)
        pggcel->Delete(pggcel->IvMac() - 1);
    if (!pcfl->FAdd(pggcel->CbOnFile(), kctgGgcl, &cnoCelNew, &blck) || !pggcel->FWrite(&blck))
        goto LEnd;

    if (C4DMMActorStudioRemoveActionChildLinks(pcfl, cnoAction, kctgGgcl) <= 0)
        goto LEnd;
    fOldDetached = fTrue;
    if (!pcfl->FAdoptChild(kctgActn, cnoAction, kctgGgcl, cnoCelNew, 0))
        goto LEnd;
    fOldDetached = fFalse;
    cnoCelNew = cnoNil;
    fRet = fTrue;
LEnd:
    if (!fRet)
    {
        if (fOldDetached)
        {
            C4DMMActorStudioRemoveActionChildLinks(pcfl, cnoAction, kctgGgcl);
            pcfl->FAdoptChild(kctgActn, cnoAction, kctgGgcl, kidCel.cki.cno, 0);
        }
        if (cnoCelNew != cnoNil && pcfl->FFind(kctgGgcl, cnoCelNew))
            pcfl->Delete(kctgGgcl, cnoCelNew);
    }
    ReleasePpo(&pggcel);
    return fRet;
}

bool MVIE::FActorStudioSaveActionAs(PACTR pactr, int32_t anidSource, PCSZ pszName, int32_t *panidNew,
                                      bool fSingleFrame)
{
    AssertThis(0);
    AssertNilOrVarMem(panidNew);
    if (panidNew != pvNil)
        *panidNew = ivNil;
    if (pactr == pvNil || pactr->Ptmpl() == pvNil || pactr->Ptmpl()->FIsTdt() ||
        pszName == pvNil || *pszName == 0 || anidSource < 0 || anidSource >= pactr->Ptmpl()->Cactn())
        return fFalse;
    if (_c4DMMCustomAction >= kc4DMMCustomActionMax || !FEnsureAutosave())
        return fFalse;

    achar szName[kcch4DMMCustomName];
    strncpy_s(szName, SIZEOF(szName), pszName, _TRUNCATE);
    int32_t cch = (int32_t)strlen(szName);
    while (cch > 0 && (szName[cch - 1] == ChLit(' ') || szName[cch - 1] == ChLit('\t') ||
                       szName[cch - 1] == ChLit('\r') || szName[cch - 1] == ChLit('\n')))
        szName[--cch] = 0;
    if (cch == 0)
        return fFalse;

    PCFL pcflDest = _pcrfAutoSave->Pcfl();
    PTMPL ptmplSource = pactr->Ptmpl();
    const bool fSourceProp = ptmplSource->FIsProp();
    STN stnActorName;
    pactr->GetName(&stnActorName);
    TAG tagOld;
    pactr->GetTagTmpl(&tagOld);
    CNO cnoTmpl = cnoNil;

    const bool fAlreadyWritable = tagOld.sid == ksidUseCrf && tagOld.pcrf == _pcrfAutoSave &&
                                  pcflDest->FFind(tagOld.ctg, tagOld.cno);
    int32_t iOwnedObject = ivNil;
    if (fAlreadyWritable)
    {
        cnoTmpl = tagOld.cno;
        for (int32_t i = 0; i < _c4DMMCustomObject; ++i)
        {
            if (_rg4DMMCustomObject[i].cnoOwnedTmpl == cnoTmpl)
            {
                iOwnedObject = i;
                break;
            }
        }
        if (iOwnedObject == ivNil && _c4DMMCustomObject >= kc4DMMCustomObjectMax)
            return fFalse;
    }
    else
    {
        // A copied stock TMPL needs a persistent custom-object root immediately
        // after the actor is switched to it. Check capacity before mutating the
        // autosave CRF so a full metadata table cannot strand a copied subtree.
        if (_c4DMMCustomObject >= kc4DMMCustomObjectMax)
            return fFalse;

        // TMPL intentionally does not expose its backing file. Resolve the
        // source through the actor's authoritative TAG, then use CFL::FCopy
        // so the TMPL and its complete child tree are duplicated together.
        PCFL pcflSource = vptagm != pvNil ? vptagm->PcflFindTag4DMM(&tagOld) : pvNil;
        if (pcflSource == pvNil ||
            !pcflSource->FCopy(tagOld.ctg, tagOld.cno, pcflDest, &cnoTmpl))
            return fFalse;
    }
    const bool fCopiedTmpl = !fAlreadyWritable;

    // Do not create two user actions with the same display name on one
    // writable template. Stock action names are allowed to match because the
    // custom list is explicitly distinguished and sorted ahead of stock.
    for (int32_t i = 0; i < _c4DMMCustomAction; ++i)
    {
        if (_rg4DMMCustomAction[i].cnoTmpl == cnoTmpl &&
            0 == _stricmp(_rg4DMMCustomAction[i].szName, szName))
        {
            if (fCopiedTmpl && pcflDest->FFind(kctgTmpl, cnoTmpl))
                pcflDest->Delete(kctgTmpl, cnoTmpl);
            return fFalse;
        }
    }

    KID kidAction;
    if (!pcflDest->FGetKidChidCtg(kctgTmpl, cnoTmpl, anidSource, kctgActn, &kidAction))
    {
        if (fCopiedTmpl && pcflDest->FFind(kctgTmpl, cnoTmpl))
            pcflDest->Delete(kctgTmpl, cnoTmpl);
        return fFalse;
    }

    // FCopy preserves CNO identity when the same chunk already exists in the
    // destination.  The v114 diagnostic proved that conclusively:
    // source_cno=565, stage_cno=565, new_cno=565.  Renaming "the copy" was
    // therefore renaming the original ACTN.  FClone is the Chunky operation
    // used elsewhere when imported trees need genuinely new identities.
    // Round-trip through a temporary CFL so the final clone is allocated as a
    // new ACTN tree inside the writable movie CFL, including GGCL/GLXF/GLMS.
    CNO cnoActionNew = cnoNil;
    CNO cnoActionStage = cnoNil;
    PCFL pcflStage = CFL::PcflCreateTemp();
    bool fActionCopied = pcflStage != pvNil &&
                         pcflDest->FClone(kidAction.cki.ctg, kidAction.cki.cno,
                                          pcflStage, &cnoActionStage) &&
                         pcflStage->FClone(kidAction.cki.ctg, cnoActionStage,
                                           pcflDest, &cnoActionNew);
    ReleasePpo(&pcflStage);
    if (!fActionCopied || cnoActionNew == cnoNil || cnoActionNew == kidAction.cki.cno)
    {
        if (cnoActionNew != cnoNil && cnoActionNew != kidAction.cki.cno &&
            pcflDest->FFind(kctgActn, cnoActionNew))
            pcflDest->Delete(kctgActn, cnoActionNew);
        if (fCopiedTmpl && pcflDest->FFind(kctgTmpl, cnoTmpl))
            pcflDest->Delete(kctgTmpl, cnoTmpl);
        MultiLog(this,
            "actor_studio_action_clone fail tmpl=%ld source_cno=%ld stage_cno=%ld new_cno=%ld copied=%d independent=%d",
            (long)cnoTmpl, (long)kidAction.cki.cno, (long)cnoActionStage, (long)cnoActionNew,
            (int)fActionCopied, (int)(cnoActionNew != cnoNil && cnoActionNew != kidAction.cki.cno));
        return fFalse;
    }
    MultiLog(this,
        "actor_studio_action_clone ok tmpl=%ld source_cno=%ld stage_cno=%ld new_cno=%ld independent=1",
        (long)cnoTmpl, (long)kidAction.cki.cno, (long)cnoActionStage, (long)cnoActionNew);

    if (fSingleFrame && !F4DMMActorStudioTrimActionToFirstFrame(pcflDest, cnoActionNew))
    {
        if (pcflDest->FFind(kctgActn, cnoActionNew))
            pcflDest->Delete(kctgActn, cnoActionNew);
        if (fCopiedTmpl && pcflDest->FFind(kctgTmpl, cnoTmpl))
            pcflDest->Delete(kctgTmpl, cnoTmpl);
        MultiLog(this, "actor_studio_new_action trim_failed source=%ld cloned=%ld",
                 (long)anidSource, (long)cnoActionNew);
        return fFalse;
    }

    // _cactn is a historical cache on TMPL. Once Actor Studio makes a TMPL
    // writable, Save As can append actions while that object remains live, so
    // derive the next CHID from the authoritative CFL rather than a cached
    // count that v119 proved could remain stuck at 34.
    int32_t anidNew = 0;
    KID kidActionScan;
    while (pcflDest->FGetKidChidCtg(kctgTmpl, cnoTmpl, anidNew, kctgActn, &kidActionScan))
        ++anidNew;
    STN stnSourceBefore;
    const bool fHaveSourceName = pcflDest->FGetName(kctgActn, kidAction.cki.cno, &stnSourceBefore);
    STN stnName(szName);
    // Name the new standalone clone before adoption.  Then verify that the
    // source ACTN's name is unchanged.  If Chunky ever violates that contract,
    // fail closed instead of exposing stock content to an editor operation.
    if (!pcflDest->FSetName(kctgActn, cnoActionNew, &stnName))
    {
        pcflDest->Delete(kctgActn, cnoActionNew);
        if (fCopiedTmpl && pcflDest->FFind(kctgTmpl, cnoTmpl))
            pcflDest->Delete(kctgTmpl, cnoTmpl);
        return fFalse;
    }
    if (fHaveSourceName)
    {
        STN stnSourceAfter;
        if (!pcflDest->FGetName(kctgActn, kidAction.cki.cno, &stnSourceAfter) ||
            0 != _stricmp(stnSourceBefore.Psz(), stnSourceAfter.Psz()))
        {
            pcflDest->FSetName(kctgActn, kidAction.cki.cno, &stnSourceBefore);
            pcflDest->Delete(kctgActn, cnoActionNew);
            if (fCopiedTmpl && pcflDest->FFind(kctgTmpl, cnoTmpl))
                pcflDest->Delete(kctgTmpl, cnoTmpl);
            MultiLog(this,
                "actor_studio_action_clone source_name_changed source_cno=%ld before=%s after=%s",
                (long)kidAction.cki.cno, stnSourceBefore.Psz(), stnSourceAfter.Psz());
            return fFalse;
        }
    }
    if (!pcflDest->FAdoptChild(kctgTmpl, cnoTmpl, kctgActn, cnoActionNew, anidNew))
    {
        pcflDest->Delete(kctgActn, cnoActionNew);
        if (fCopiedTmpl && pcflDest->FFind(kctgTmpl, cnoTmpl))
            pcflDest->Delete(kctgTmpl, cnoTmpl);
        return fFalse;
    }

    auto RollbackWritableMutation = [&]()
    {
        _pcrfAutoSave->FSetCrep(crepToss, kctgTmpl, cnoTmpl, TMPL::FReadTmpl);
        if (fCopiedTmpl)
        {
            if (pcflDest->FFind(kctgTmpl, cnoTmpl))
                pcflDest->Delete(kctgTmpl, cnoTmpl);
        }
        else
        {
            pcflDest->DeleteChild(kctgTmpl, cnoTmpl, kctgActn, cnoActionNew, anidNew);
            // Some CFL paths leave an unparented chunk behind; remove it if it
            // still exists so a failed Save As cannot accumulate orphan ACTNs.
            if (pcflDest->FFind(kctgActn, cnoActionNew))
                pcflDest->Delete(kctgActn, cnoActionNew);
        }
    };

    // The actor may already own a cached representation of this writable
    // template. Toss only the cache entry; existing references remain valid,
    // and FChangeTagTmpl below fetches the new representation with the added
    // ACTN child.
    _pcrfAutoSave->FSetCrep(crepToss, kctgTmpl, cnoTmpl, TMPL::FReadTmpl);

    TAG tagNew;
    tagNew.sid = ksidUseCrf;
    tagNew.ctg = kctgTmpl;
    tagNew.cno = cnoTmpl;
    if (!TAGM::FOpenTag(&tagNew, _pcrfAutoSave))
    {
        RollbackWritableMutation();
        return fFalse;
    }
    const bool fChanged = pactr->FChangeTagTmpl(&tagNew);
    if (fChanged && pactr->Arid() != aridNil)
        ChangeActrTag(pactr->Arid(), &tagNew);
    if (!fChanged)
    {
        TAGM::CloseTag(&tagNew);
        RollbackWritableMutation();
        return fFalse;
    }

    // FChangeTagTmpl can legitimately leave the same already-live writable
    // TMPL object attached when Save As is used a second time. Refresh its
    // cached action count explicitly, then verify that the just-adopted CHID is
    // visible through the ordinary TMPL API before publishing metadata/UI.
    PTMPL ptmplWritable = pactr->Ptmpl();
    const int32_t cactnBeforeRefresh = ptmplWritable != pvNil ? ptmplWritable->Cactn() : -1;
    if (ptmplWritable == pvNil)
    {
        TAGM::CloseTag(&tagNew);
        RollbackWritableMutation();
        return fFalse;
    }
    ptmplWritable->RefreshActionCount();
    const int32_t cactnAfterRefresh = ptmplWritable->Cactn();
    STN stnVerifyNew;
    const bool fNewVisible = anidNew < cactnAfterRefresh &&
                             ptmplWritable->FGetActnName(anidNew, &stnVerifyNew);
    MultiLog(this,
        "actor_studio_action_count_refresh tmpl=%ld before=%ld after=%ld new_anid=%ld visible=%d name=%s",
        (long)cnoTmpl, (long)cactnBeforeRefresh, (long)cactnAfterRefresh, (long)anidNew,
        (int)fNewVisible, fNewVisible ? stnVerifyNew.Psz() : "<missing>");
    if (!fNewVisible)
    {
        TAGM::CloseTag(&tagNew);
        RollbackWritableMutation();
        ptmplWritable->RefreshActionCount();
        return fFalse;
    }

    // The first custom animation on a stock actor/prop promotes its writable
    // TMPL copy to a movie-owned custom object asset. This native template
    // carries every original action plus the new user action; the stock source
    // remains read-only and is never rewritten. Keeping the asset here also
    // prevents GC from discarding it if its last scene instance is removed.
    if (iOwnedObject == ivNil)
    {
        CUSTOMOBJECT *pobj = &_rg4DMMCustomObject[_c4DMMCustomObject++];
        ClearPb(pobj, SIZEOF(*pobj));
        pobj->id = _id4DMMCustomObjectNext++;
        pobj->fProp = fSourceProp;
        pobj->cnoOwnedTmpl = cnoTmpl;
        if (tagOld.sid != ksidUseCrf)
        {
            pobj->templateKind = kctkDefault;
            pobj->sidTemplate = tagOld.sid;
            pobj->ctgTemplate = tagOld.ctg;
            pobj->cnoTemplate = tagOld.cno;
        }
        else
        {
            pobj->templateKind = kctkNone;
            pobj->sidTemplate = 0;
            pobj->ctgTemplate = ctgNil;
            pobj->cnoTemplate = cnoNil;
        }
        strncpy_s(pobj->szName, SIZEOF(pobj->szName), stnActorName.Psz(), _TRUNCATE);
        if (pobj->szName[0] == 0)
            sprintf_s(pobj->szName, SIZEOF(pobj->szName), "Custom %s %ld",
                      pobj->fProp ? "Prop" : "Actor", (long)pobj->id);
        iOwnedObject = _c4DMMCustomObject - 1;
        MultiLog(this,
            "actor_studio_custom_object object=%ld name=%s type=%s tmpl=%ld source_sid=%ld source_ctg=%lu source_cno=%ld",
            (long)pobj->id, pobj->szName, pobj->fProp ? "prop" : "actor", (long)cnoTmpl,
            (long)pobj->sidTemplate, (unsigned long)pobj->ctgTemplate, (long)pobj->cnoTemplate);
    }

    if (tagOld.sid != ksidUseCrf)
        FPromote4DMMReplacementTemplate(&tagOld, &tagNew, pactr->Arid());
    TAGM::CloseTag(&tagNew);

    CUSTOMACTION *paction = &_rg4DMMCustomAction[_c4DMMCustomAction++];
    ClearPb(paction, SIZEOF(*paction));
    paction->cnoTmpl = cnoTmpl;
    paction->anid = anidNew;
    strcpy_s(paction->szName, SIZEOF(paction->szName), szName);

    _fRequiresVmmSave = fTrue;
    _fAutosaveDirty = fTrue;
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    MultiLog(this, "actor_studio_action_save_as arid=%ld source=%ld new=%ld tmpl=%ld name=%s",
             (long)pactr->Arid(), (long)anidSource, (long)anidNew, (long)cnoTmpl, szName);
    if (panidNew != pvNil)
        *panidNew = anidNew;
    return fTrue;
}

bool MVIE::FActorStudioNewAction(PACTR pactr, int32_t anidSource, PCSZ pszName, int32_t *panidNew)
{
    return FActorStudioSaveActionAs(pactr, anidSource, pszName, panidNew, fTrue);
}

bool MVIE::FActorStudioSaveAction(PACTR pactr, int32_t anid)
{
    AssertThis(0);
    if (!FActorStudioActionIsCustom(pactr, anid))
        return fFalse;
    _fRequiresVmmSave = fTrue;
    _fAutosaveDirty = fTrue;
    SetDirty();
    MultiLog(this, "actor_studio_action_save arid=%ld action=%ld", (long)pactr->Arid(), (long)anid);
    return fTrue;
}

static bool F4DMMActorStudioReadActionGroups(PCFL pcfl, CNO cnoAction,
                                               PGG *ppggcel, PGL *ppglbmat34)
{
    if (ppggcel != pvNil)
        *ppggcel = pvNil;
    if (ppglbmat34 != pvNil)
        *ppglbmat34 = pvNil;
    if (pcfl == pvNil || cnoAction == cnoNil || ppggcel == pvNil || ppglbmat34 == pvNil)
        return fFalse;

    KID kidCel;
    KID kidXf;
    BLCK blck;
    int16_t bo = kboCur;
    PGG pggcel = pvNil;
    PGL pglbmat34 = pvNil;

    if (!pcfl->FGetKidChidCtg(kctgActn, cnoAction, 0, kctgGgcl, &kidCel) ||
        !pcfl->FGetKidChidCtg(kctgActn, cnoAction, 0, kctgGlxf, &kidXf))
        goto LFail;
    if (!pcfl->FFind(kidCel.cki.ctg, kidCel.cki.cno, &blck))
        goto LFail;
    pggcel = GG::PggRead(&blck, &bo);
    if (pggcel == pvNil)
        goto LFail;
    if (kboOther == bo)
    {
        AssertBomRglw(kbomCel, SIZEOF(CEL));
        AssertBomRgsw(kbomCps, SIZEOF(CPS));
        for (int32_t icel = 0; icel < pggcel->IvMac(); ++icel)
        {
            SwapBytesRglw(pggcel->QvFixedGet(icel), SIZEOF(CEL) / SIZEOF(int32_t));
            SwapBytesRgsw(pggcel->QvGet(icel), pggcel->Cb(icel) / SIZEOF(int16_t));
        }
    }

    bo = kboCur;
    if (!pcfl->FFind(kidXf.cki.ctg, kidXf.cki.cno, &blck))
        goto LFail;
    pglbmat34 = GL::PglRead(&blck, &bo);
    if (pglbmat34 == pvNil)
        goto LFail;
    if (kboOther == bo)
    {
        AssertBomRglw(kbomBmat34, SIZEOF(BMAT34));
        if (pglbmat34->IvMac() > 0)
            SwapBytesRglw(pglbmat34->QvGet(0),
                          LwMul(pglbmat34->IvMac(), SIZEOF(BMAT34) / SIZEOF(int32_t)));
    }

    *ppggcel = pggcel;
    *ppglbmat34 = pglbmat34;
    return fTrue;
LFail:
    ReleasePpo(&pggcel);
    ReleasePpo(&pglbmat34);
    return fFalse;
}

static bool F4DMMActorStudioResolveActionFile(PMVIE pmvie, PCRF pcrfFallback,
                                               PACTR pactr, int32_t anid,
                                               PCFL *ppcfl, CNO *pcnoTmpl,
                                               CNO *pcnoAction, PCRF *ppcrf)
{
    if (ppcfl != pvNil)
        *ppcfl = pvNil;
    if (pcnoTmpl != pvNil)
        *pcnoTmpl = cnoNil;
    if (pcnoAction != pvNil)
        *pcnoAction = cnoNil;
    if (ppcrf != pvNil)
        *ppcrf = pvNil;
    if (pmvie == pvNil || pactr == pvNil || pactr->Ptmpl() == pvNil || anid < 0 ||
        anid >= pactr->Ptmpl()->Cactn())
        return fFalse;

    TAG tagTmpl;
    pactr->GetTagTmpl(&tagTmpl);
    PCRF pcrf = tagTmpl.sid == ksidUseCrf ?
        (tagTmpl.pcrf != pvNil ? tagTmpl.pcrf : pcrfFallback) : pvNil;
    PCFL pcfl = pcrf != pvNil ? pcrf->Pcfl() :
        (vptagm != pvNil ? vptagm->PcflFindTag4DMM(&tagTmpl) : pvNil);
    if (pcfl == pvNil)
        return fFalse;
    KID kidAction;
    if (!pcfl->FGetKidChidCtg(kctgTmpl, tagTmpl.cno, anid, kctgActn, &kidAction))
        return fFalse;
    if (ppcfl != pvNil)
        *ppcfl = pcfl;
    if (pcnoTmpl != pvNil)
        *pcnoTmpl = tagTmpl.cno;
    if (pcnoAction != pvNil)
        *pcnoAction = kidAction.cki.cno;
    if (ppcrf != pvNil)
        *ppcrf = pcrf;
    return fTrue;
}

bool MVIE::FActorStudioGetFrameSnapshot(PACTR pactr, int32_t anid, int32_t celn,
                                         ACTORSTUDIOFRAMESNAPSHOT *psnapshot) const
{
    AssertThis(0);
    if (psnapshot == pvNil)
        return fFalse;
    ClearPb(psnapshot, SIZEOF(*psnapshot));
    psnapshot->cnoTmplSource = cnoNil;
    psnapshot->anidSource = ivNil;
    PCFL pcfl = pvNil;
    CNO cnoTmpl = cnoNil;
    CNO cnoAction = cnoNil;
    PGG pggcel = pvNil;
    PGL pglbmat34 = pvNil;
    bool fRet = fFalse;
    int32_t icel = 0;
    if (!F4DMMActorStudioResolveActionFile((PMVIE)this, _pcrfAutoSave, pactr, anid,
                                            &pcfl, &cnoTmpl, &cnoAction, pvNil) ||
        !F4DMMActorStudioReadActionGroups(pcfl, cnoAction, &pggcel, &pglbmat34) ||
        pggcel->IvMac() <= 0)
        goto LEnd;
    icel = celn % pggcel->IvMac();
    if (icel < 0)
        icel += pggcel->IvMac();
    psnapshot->cpart = pggcel->Cb(icel) / SIZEOF(CPS);
    if (psnapshot->cpart <= 0 || psnapshot->cpart > kc4DMMActorStudioFramePartMax)
        goto LEnd;
    psnapshot->cel = *(CEL *)pggcel->QvFixedGet(icel);
    CopyPb(pggcel->QvGet(icel), psnapshot->rgcps, LwMul(psnapshot->cpart, SIZEOF(CPS)));
    for (int32_t ipart = 0; ipart < psnapshot->cpart; ++ipart)
    {
        const int32_t imat34 = psnapshot->rgcps[ipart].imat34;
        if (!FIn(imat34, 0, pglbmat34->IvMac()))
            goto LEnd;
        psnapshot->rgbmat34[ipart] = *(BMAT34 *)pglbmat34->QvGet(imat34);
    }
    psnapshot->fValid = fTrue;
    psnapshot->cnoTmplSource = cnoTmpl;
    psnapshot->anidSource = anid;
    fRet = fTrue;
LEnd:
    ReleasePpo(&pggcel);
    ReleasePpo(&pglbmat34);
    return fRet;
}

bool MVIE::FActorStudioWriteFrameSnapshot(PACTR pactr, int32_t anid, int32_t celn,
                                           const ACTORSTUDIOFRAMESNAPSHOT *psnapshot,
                                           int32_t asfw)
{
    AssertThis(0);
    if (pactr == pvNil || psnapshot == pvNil || !psnapshot->fValid ||
        !FIn(asfw, kasfwReplace, kasfwInsertAfter + 1) ||
        !FActorStudioActionIsCustom(pactr, anid) || !FEnsureAutosave())
        return fFalse;
    PCFL pcfl = pvNil;
    PCRF pcrf = pvNil;
    CNO cnoTmpl = cnoNil;
    CNO cnoAction = cnoNil;
    PGG pggcel = pvNil;
    PGL pglbmat34 = pvNil;
    CNO cnoCelNew = cnoNil;
    CNO cnoXfNew = cnoNil;
    BLCK blck;
    bool fChildrenReplaced = fFalse;
    bool fRet = fFalse;
    int32_t icel = 0;
    int32_t iInsert = 0;
    CPS rgcps[kc4DMMActorStudioFramePartMax];
    CEL celNew;

    if (!F4DMMActorStudioResolveActionFile(this, _pcrfAutoSave, pactr, anid, &pcfl, &cnoTmpl,
                                            &cnoAction, &pcrf) || pcfl == pvNil || pcrf == pvNil ||
        psnapshot->cnoTmplSource != cnoTmpl ||
        !F4DMMActorStudioReadActionGroups(pcfl, cnoAction, &pggcel, &pglbmat34) ||
        pggcel->IvMac() <= 0)
        goto LEnd;
    icel = celn % pggcel->IvMac();
    if (icel < 0)
        icel += pggcel->IvMac();
    if (pggcel->Cb(icel) / SIZEOF(CPS) != psnapshot->cpart ||
        psnapshot->cpart > kc4DMMActorStudioFramePartMax)
        goto LEnd;
    CopyPb(psnapshot->rgcps, rgcps, LwMul(psnapshot->cpart, SIZEOF(CPS)));
    for (int32_t ipart = 0; ipart < psnapshot->cpart; ++ipart)
    {
        BMAT34 bmat34 = psnapshot->rgbmat34[ipart];
        int32_t imat34 = ivNil;
        if (!pglbmat34->FAdd(&bmat34, &imat34) || imat34 > 0x7fff)
            goto LEnd;
        rgcps[ipart].imat34 = (int16_t)imat34;
    }
    celNew = psnapshot->cel;
    if (psnapshot->anidSource != anid)
        celNew.chidSnd = chidNil;
    if (asfw == kasfwReplace)
    {
        pggcel->Delete(icel);
        iInsert = icel;
    }
    else if (asfw == kasfwInsertBefore)
        iInsert = icel;
    else
        iInsert = icel + 1;
    if (!pggcel->FInsert(iInsert, LwMul(psnapshot->cpart, SIZEOF(CPS)), rgcps, &celNew))
        goto LEnd;
    if (!pcfl->FAdd(pglbmat34->CbOnFile(), kctgGlxf, &cnoXfNew, &blck) ||
        !pglbmat34->FWrite(&blck) ||
        !pcfl->FAdd(pggcel->CbOnFile(), kctgGgcl, &cnoCelNew, &blck) ||
        !pggcel->FWrite(&blck))
        goto LEnd;
    if (!F4DMMActorStudioReplacePoseChildren(pcfl, cnoAction, cnoCelNew, cnoXfNew,
                                              pvNil, pvNil))
        goto LEnd;
    fChildrenReplaced = fTrue;
    pcrf->FSetCrep(crepToss, kctgActn, cnoAction, ACTN::FReadActn);
    _fRequiresVmmSave = fTrue;
    _fAutosaveDirty = fTrue;
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    MultiLog(this,
        "actor_studio_frame_write arid=%ld tmpl=%ld action=%ld actn_cno=%ld frame=%ld mode=%ld source_action=%ld count=%ld",
        (long)pactr->Arid(), (long)cnoTmpl, (long)anid, (long)cnoAction,
        (long)icel, (long)asfw, (long)psnapshot->anidSource, (long)pggcel->IvMac());
    fRet = fTrue;
LEnd:
    if (!fRet && !fChildrenReplaced && pcfl != pvNil)
    {
        if (cnoCelNew != cnoNil && pcfl->FFind(kctgGgcl, cnoCelNew))
            pcfl->Delete(kctgGgcl, cnoCelNew);
        if (cnoXfNew != cnoNil && pcfl->FFind(kctgGlxf, cnoXfNew))
            pcfl->Delete(kctgGlxf, cnoXfNew);
    }
    ReleasePpo(&pggcel);
    ReleasePpo(&pglbmat34);
    return fRet;
}

bool MVIE::FActorStudioDeleteFrame(PACTR pactr, int32_t anid, int32_t celn)
{
    AssertThis(0);
    if (pactr == pvNil || !FActorStudioActionIsCustom(pactr, anid) || !FEnsureAutosave())
        return fFalse;
    PCFL pcfl = pvNil;
    PCRF pcrf = pvNil;
    CNO cnoTmpl = cnoNil;
    CNO cnoAction = cnoNil;
    PGG pggcel = pvNil;
    PGL pglbmat34 = pvNil;
    CNO cnoCelNew = cnoNil;
    CNO cnoXfNew = cnoNil;
    BLCK blck;
    bool fChildrenReplaced = fFalse;
    bool fRet = fFalse;
    int32_t icel = 0;
    if (!F4DMMActorStudioResolveActionFile(this, _pcrfAutoSave, pactr, anid, &pcfl, &cnoTmpl,
                                            &cnoAction, &pcrf) || pcfl == pvNil || pcrf == pvNil ||
        !F4DMMActorStudioReadActionGroups(pcfl, cnoAction, &pggcel, &pglbmat34) ||
        pggcel->IvMac() <= 1)
        goto LEnd;
    icel = celn % pggcel->IvMac();
    if (icel < 0)
        icel += pggcel->IvMac();
    pggcel->Delete(icel);
    if (!pcfl->FAdd(pglbmat34->CbOnFile(), kctgGlxf, &cnoXfNew, &blck) ||
        !pglbmat34->FWrite(&blck) ||
        !pcfl->FAdd(pggcel->CbOnFile(), kctgGgcl, &cnoCelNew, &blck) ||
        !pggcel->FWrite(&blck))
        goto LEnd;
    if (!F4DMMActorStudioReplacePoseChildren(pcfl, cnoAction, cnoCelNew, cnoXfNew,
                                              pvNil, pvNil))
        goto LEnd;
    fChildrenReplaced = fTrue;
    pcrf->FSetCrep(crepToss, kctgActn, cnoAction, ACTN::FReadActn);
    _fRequiresVmmSave = fTrue;
    _fAutosaveDirty = fTrue;
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    MultiLog(this, "actor_studio_frame_delete arid=%ld tmpl=%ld action=%ld frame=%ld count=%ld",
             (long)pactr->Arid(), (long)cnoTmpl, (long)anid, (long)icel, (long)pggcel->IvMac());
    fRet = fTrue;
LEnd:
    if (!fRet && !fChildrenReplaced && pcfl != pvNil)
    {
        if (cnoCelNew != cnoNil && pcfl->FFind(kctgGgcl, cnoCelNew))
            pcfl->Delete(kctgGgcl, cnoCelNew);
        if (cnoXfNew != cnoNil && pcfl->FFind(kctgGlxf, cnoXfNew))
            pcfl->Delete(kctgGlxf, cnoXfNew);
    }
    ReleasePpo(&pggcel);
    ReleasePpo(&pglbmat34);
    return fRet;
}

static bool F4DMMActorStudioReadTmplPartLists(PCFL pcfl, CNO cnoTmpl,
                                               PGL *ppglibactPar, PGL *ppglibset)
{
    if (ppglibactPar != pvNil)
        *ppglibactPar = pvNil;
    if (ppglibset != pvNil)
        *ppglibset = pvNil;
    if (pcfl == pvNil || ppglibactPar == pvNil || ppglibset == pvNil)
        return fFalse;
    KID kidPar;
    KID kidSet;
    BLCK blck;
    int16_t bo = kboCur;
    PGL pglibactPar = pvNil;
    PGL pglibset = pvNil;
    if (!pcfl->FGetKidChidCtg(kctgTmpl, cnoTmpl, 0, kctgGlpi, &kidPar) ||
        !pcfl->FGetKidChidCtg(kctgTmpl, cnoTmpl, 0, kctgGlbs, &kidSet) ||
        !pcfl->FFind(kidPar.cki.ctg, kidPar.cki.cno, &blck))
        goto LFail;
    pglibactPar = GL::PglRead(&blck, &bo);
    if (pglibactPar == pvNil || pglibactPar->CbEntry() != SIZEOF(int16_t))
        goto LFail;
    if (kboOther == bo && pglibactPar->IvMac() > 0)
        SwapBytesRgsw(pglibactPar->QvGet(0), pglibactPar->IvMac());
    bo = kboCur;
    if (!pcfl->FFind(kidSet.cki.ctg, kidSet.cki.cno, &blck))
        goto LFail;
    pglibset = GL::PglRead(&blck, &bo);
    if (pglibset == pvNil || pglibset->CbEntry() != SIZEOF(int16_t))
        goto LFail;
    if (kboOther == bo && pglibset->IvMac() > 0)
        SwapBytesRgsw(pglibset->QvGet(0), pglibset->IvMac());
    *ppglibactPar = pglibactPar;
    *ppglibset = pglibset;
    return fTrue;
LFail:
    ReleasePpo(&pglibactPar);
    ReleasePpo(&pglibset);
    return fFalse;
}

static bool F4DMMActorStudioReplaceTmplChild(PCFL pcfl, CNO cnoTmpl, CTG ctgChild,
                                              CNO cnoNew, CNO *pcnoOld)
{
    if (pcnoOld != pvNil)
        *pcnoOld = cnoNil;
    if (pcfl == pvNil || cnoTmpl == cnoNil || cnoNew == cnoNil)
        return fFalse;
    KID kidOld;
    if (!pcfl->FGetKidChidCtg(kctgTmpl, cnoTmpl, 0, ctgChild, &kidOld))
        return fFalse;
    pcfl->DeleteChild(kctgTmpl, cnoTmpl, kidOld.cki.ctg, kidOld.cki.cno, kidOld.chid);
    if (!pcfl->FAdoptChild(kctgTmpl, cnoTmpl, ctgChild, cnoNew, 0))
    {
        pcfl->FAdoptChild(kctgTmpl, cnoTmpl, kidOld.cki.ctg, kidOld.cki.cno, 0);
        return fFalse;
    }
    if (pcnoOld != pvNil)
        *pcnoOld = kidOld.cki.cno;
    return fTrue;
}

struct ASDUPCMTLWRITE
{
    int32_t cmidSource;
    int32_t cmidNew;
    CNO cnoCmtlNew;
    CNO cnoMtrlNew;
    CNO cnoBmdlNew;
    bool fAdopted;
};

/***************************************************************************
    Build a one-slot CMTL for a duplicated BODY part. CMTL child CHIDs are
    ordinal positions within a BODY set, so copying the source CMTL whole
    would also copy unrelated sibling slots when the source set has multiple
    BODY parts. Clone only the selected slot's authored MTRL/BMDL trees and
    retarget the new CMTL to the duplicate's independent one-part set.
***************************************************************************/
static bool F4DMMActorStudioCloneDuplicateCmtl(PCRF pcrf, PCFL pcfl,
                                                CNO cnoCmtlSource,
                                                int32_t ibmtlSource,
                                                int32_t ibsetNew,
                                                CNO *pcnoCmtlNew,
                                                CNO *pcnoMtrlNew,
                                                CNO *pcnoBmdlNew,
                                                bool *pfModelCloned)
{
    if (pcnoCmtlNew != pvNil)
        *pcnoCmtlNew = cnoNil;
    if (pcnoMtrlNew != pvNil)
        *pcnoMtrlNew = cnoNil;
    if (pcnoBmdlNew != pvNil)
        *pcnoBmdlNew = cnoNil;
    if (pfModelCloned != pvNil)
        *pfModelCloned = fFalse;
    if (pcrf == pvNil || pcfl == pvNil || cnoCmtlSource == cnoNil ||
        ibmtlSource < 0 || ibsetNew < 0 || pcnoCmtlNew == pvNil ||
        pcnoMtrlNew == pvNil || pcnoBmdlNew == pvNil)
        return fFalse;

    KID kidMtrl;
    KID kidBmdl;
    CNO cnoMtrlNew = cnoNil;
    CNO cnoBmdlNew = cnoNil;
    CNO cnoCmtlNew = cnoNil;
    bool fHaveBmdl = fFalse;
    bool fMtrlAdopted = fFalse;
    bool fBmdlAdopted = fFalse;
    bool fRet = fFalse;
    CMTLF cmtlf;

    if (!pcfl->FGetKidChidCtg(kctgCmtl, cnoCmtlSource, ibmtlSource, kctgMtrl, &kidMtrl))
        goto LEnd;
    fHaveBmdl = pcfl->FGetKidChidCtg(kctgCmtl, cnoCmtlSource, ibmtlSource,
                                      kctgBmdl, &kidBmdl);
    if (!F4DMMCloneResourceTree(pcrf, kctgMtrl, kidMtrl.cki.cno, pcfl, &cnoMtrlNew))
        goto LEnd;
    if (fHaveBmdl &&
        !F4DMMCloneResourceTree(pcrf, kctgBmdl, kidBmdl.cki.cno, pcfl, &cnoBmdlNew))
        goto LEnd;

    ClearPb(&cmtlf, SIZEOF(cmtlf));
    cmtlf.bo = kboCur;
    cmtlf.osk = koskCur;
    cmtlf.ibset = ibsetNew;
    if (!pcfl->FAddPv(&cmtlf, SIZEOF(cmtlf), kctgCmtl, &cnoCmtlNew))
        goto LEnd;
    if (!pcfl->FAdoptChild(kctgCmtl, cnoCmtlNew, kctgMtrl, cnoMtrlNew, 0))
        goto LEnd;
    fMtrlAdopted = fTrue;
    if (fHaveBmdl)
    {
        if (!pcfl->FAdoptChild(kctgCmtl, cnoCmtlNew, kctgBmdl, cnoBmdlNew, 0))
            goto LEnd;
        fBmdlAdopted = fTrue;
    }

    *pcnoCmtlNew = cnoCmtlNew;
    *pcnoMtrlNew = cnoMtrlNew;
    *pcnoBmdlNew = cnoBmdlNew;
    if (pfModelCloned != pvNil)
        *pfModelCloned = fHaveBmdl;
    fRet = fTrue;

LEnd:
    if (!fRet)
    {
        if (cnoCmtlNew != cnoNil)
        {
            if (fBmdlAdopted)
                pcfl->DeleteChild(kctgCmtl, cnoCmtlNew, kctgBmdl, cnoBmdlNew, 0);
            if (fMtrlAdopted)
                pcfl->DeleteChild(kctgCmtl, cnoCmtlNew, kctgMtrl, cnoMtrlNew, 0);
            if (pcfl->FFind(kctgCmtl, cnoCmtlNew))
                pcfl->Delete(kctgCmtl, cnoCmtlNew);
        }
        if (cnoMtrlNew != cnoNil && pcfl->FFind(kctgMtrl, cnoMtrlNew))
            pcfl->Delete(kctgMtrl, cnoMtrlNew);
        if (cnoBmdlNew != cnoNil && pcfl->FFind(kctgBmdl, cnoBmdlNew))
            pcfl->Delete(kctgBmdl, cnoBmdlNew);
    }
    return fRet;
}

struct ASACTIONPARTWRITE
{
    CNO cnoAction;
    CNO cnoCelOld;
    CNO cnoXfOld;
    CNO cnoCelNew;
    CNO cnoXfNew;
    bool fReplaced;
};

bool MVIE::FActorStudioDuplicatePart(PACTR pactr, int32_t anidCurrent, int32_t celnCurrent,
                                      int32_t ipartSource, bool fKeepPositions,
                                      const BMAT34 *pbmat34Freeze, int32_t *pipartNew)
{
    return FActorStudioDuplicatePartCore(pactr, anidCurrent, celnCurrent, ipartSource,
                                         fKeepPositions, pbmat34Freeze, pipartNew, fTrue);
}

bool MVIE::FActorStudioDuplicatePartCore(PACTR pactr, int32_t anidCurrent, int32_t celnCurrent,
                                          int32_t ipartSource, bool fKeepPositions,
                                          const BMAT34 *pbmat34Freeze, int32_t *pipartNew,
                                          bool fAddUndo)
{
    AssertThis(0);
    if (pipartNew != pvNil)
        *pipartNew = ivNil;
    if (pactr == pvNil || pactr->Ptmpl() == pvNil || pactr->Ptmpl()->FIsTdt() ||
        !FActorStudioActionIsCustom(pactr, anidCurrent) || ipartSource < 0 || !FEnsureAutosave())
        return fFalse;

    TAG tagTmpl;
    pactr->GetTagTmpl(&tagTmpl);
    PCRF pcrf = tagTmpl.pcrf != pvNil ? tagTmpl.pcrf : _pcrfAutoSave;
    PCFL pcfl = pcrf != pvNil ? pcrf->Pcfl() : pvNil;
    if (pcfl == pvNil || tagTmpl.sid != ksidUseCrf)
        return fFalse;

    ActorStudioDuplicateLog(this,
        "BEGIN pactr=%p arid=%ld tmpl=%ld action=%ld cel=%ld source_part=%ld keep_positions=%d",
        pactr, (long)pactr->Arid(), (long)tagTmpl.cno, (long)anidCurrent,
        (long)celnCurrent, (long)ipartSource, (int)fKeepPositions);

    PGL pglibactPar = pvNil;
    PGL pglibset = pvNil;
    PGG pggCmid = pvNil;
    PASDU pasdu = pvNil;
    ACTORSTUDIOFRAMESNAPSHOT snapshotAnchor;
    ClearPb(&snapshotAnchor, SIZEOF(snapshotAnchor));
    ASACTIONPARTWRITE *prgwrite = pvNil;
    ASDUPCMTLWRITE *prgcmtlWrite = pvNil;
    int32_t *prgcmidNew = pvNil;
    const int32_t cactn = pactr->Ptmpl()->Cactn();
    int32_t cpartOld = 0;
    int32_t ipartNew = ivNil;
    int16_t ibactParent = 0;
    int16_t ibsetSourceSw = 0;
    int16_t ibsetNewSw = 0;
    int32_t ibsetSource = ivNil;
    int32_t ibsetNew = ivNil;
    int32_t ibmtlSource = 0;
    int32_t cbprtSourceSet = 0;
    int32_t cbsetOld = 0;
    int32_t ccmidSource = 0;
    int32_t cmidNext = 0;
    int32_t cmtlCount = 0;
    CNO cnoParNew = cnoNil;
    CNO cnoSetNew = cnoNil;
    CNO cnoGgcmNew = cnoNil;
    CNO cnoParOld = cnoNil;
    CNO cnoSetOld = cnoNil;
    CNO cnoGgcmOld = cnoNil;
    PTMPL ptmplLive = pvNil;
    bool fParReplaced = fFalse;
    bool fSetReplaced = fFalse;
    bool fGgcmReplaced = fFalse;
    bool fRet = fFalse;
    BLCK blck;
    KID kidT;

    if (!F4DMMActorStudioReadTmplPartLists(pcfl, tagTmpl.cno, &pglibactPar, &pglibset))
        goto LEnd;

    // Duplicate must create a real one-part BODY set. Reusing the source
    // set makes its CMTLs too short for the enlarged set, which is exactly
    // where v134 died during the fresh TMPL::PbodyCreate default costume.
    {
        KID kidGgcm;
        BLCK blckGgcm;
        int16_t boGgcm = kboCur;
        if (!pcfl->FGetKidChidCtg(kctgTmpl, tagTmpl.cno, 0, kctgGgcm, &kidGgcm) ||
            !pcfl->FFind(kidGgcm.cki.ctg, kidGgcm.cki.cno, &blckGgcm))
            goto LEnd;
        pggCmid = GG::PggRead(&blckGgcm, &boGgcm);
        if (pggCmid == pvNil || pggCmid->CbFixed() != SIZEOF(int32_t))
            goto LEnd;
        if (boGgcm == kboOther)
        {
            for (int32_t ibsetSwap = 0; ibsetSwap < pggCmid->IvMac(); ++ibsetSwap)
            {
                SwapBytesRglw(pggCmid->QvFixedGet(ibsetSwap), 1);
                SwapBytesRglw(pggCmid->QvGet(ibsetSwap),
                               *(int32_t *)pggCmid->QvFixedGet(ibsetSwap));
            }
        }
    }

    cpartOld = pglibactPar->IvMac();
    cbsetOld = pggCmid->IvMac();
    ActorStudioDuplicateLog(this, "topology_read parts=%ld sets=%ld actions=%ld",
                            (long)cpartOld, (long)cbsetOld, (long)cactn);
    if (cpartOld <= 0 || cpartOld >= kc4DMMActorStudioFramePartMax ||
        pglibset->IvMac() != cpartOld || !FIn(ipartSource, 0, cpartOld))
        goto LEnd;
    ipartNew = cpartOld;
    pglibactPar->Get(ipartSource, &ibactParent);
    pglibset->Get(ipartSource, &ibsetSourceSw);
    ibsetSource = ibsetSourceSw;
    if (!FIn(ibsetSource, 0, cbsetOld))
        goto LEnd;

    // CMTL slots are ordered by BODY-part order within a set. Find the
    // selected part's slot and the total number of source-set parts so the
    // cloned CMTLs can be reduced to exactly one correct slot.
    ibmtlSource = 0;
    cbprtSourceSet = 0;
    for (int32_t ipart = 0; ipart < cpartOld; ++ipart)
    {
        int16_t ibsetT = 0;
        pglibset->Get(ipart, &ibsetT);
        if (ibsetT != ibsetSourceSw)
            continue;
        if (ipart < ipartSource)
            ++ibmtlSource;
        ++cbprtSourceSet;
    }
    ccmidSource = *(int32_t *)pggCmid->QvFixedGet(ibsetSource);
    if (ccmidSource <= 0 || pggCmid->Cb(ibsetSource) != LwMul(ccmidSource, SIZEOF(int32_t)))
        goto LEnd;

    // CMIDs are historically contiguous and TMPL::PcmtlFetch relies on that
    // invariant. Refuse to manufacture a sparse costume table.
    for (int32_t ikid = 0; pcfl->FGetKid(kctgTmpl, tagTmpl.cno, ikid, &kidT); ++ikid)
    {
        if (kidT.cki.ctg != kctgCmtl)
            continue;
        ++cmtlCount;
        cmidNext = LwMax(cmidNext, (int32_t)kidT.chid + 1);
    }
    if (cmidNext != cmtlCount)
    {
        MultiLog(this,
            "actor_studio_part_duplicate costume_reject sparse_cmids tmpl=%ld count=%ld next=%ld",
            (long)tagTmpl.cno, (long)cmtlCount, (long)cmidNext);
        ActorStudioDuplicateLog(this,
            "costume_reject sparse_cmids count=%ld next=%ld", (long)cmtlCount, (long)cmidNext);
        goto LEnd;
    }

    ibsetNew = cbsetOld;
    if (ibsetNew > 0x7fff || cmidNext > 0x7fff || ccmidSource > 0x7fff - cmidNext + 1)
        goto LEnd;
    ibsetNewSw = (int16_t)ibsetNew;
    if (!pglibactPar->FAdd(&ibactParent, pvNil) || !pglibset->FAdd(&ibsetNewSw, pvNil))
        goto LEnd;
    if (!fKeepPositions && pbmat34Freeze == pvNil &&
        !FActorStudioGetFrameSnapshot(pactr, anidCurrent, celnCurrent, &snapshotAnchor))
        goto LEnd;
    if (cactn <= 0 ||
        !FAllocPv((void **)&prgwrite, LwMul(cactn, SIZEOF(ASACTIONPARTWRITE)), fmemClear, mprNormal) ||
        !FAllocPv((void **)&prgcmtlWrite, LwMul(ccmidSource, SIZEOF(ASDUPCMTLWRITE)), fmemClear, mprNormal) ||
        !FAllocPv((void **)&prgcmidNew, LwMul(ccmidSource, SIZEOF(int32_t)), fmemClear, mprNormal))
        goto LEnd;

    MultiLog(this,
        "actor_studio_part_duplicate costume_plan tmpl=%ld source_part=%ld source_set=%ld source_slot=%ld source_set_parts=%ld variants=%ld new_set=%ld first_new_cmid=%ld",
        (long)tagTmpl.cno, (long)ipartSource, (long)ibsetSource, (long)ibmtlSource,
        (long)cbprtSourceSet, (long)ccmidSource, (long)ibsetNew, (long)cmidNext);
    ActorStudioDuplicateLog(this,
        "costume_plan source_set=%ld source_slot=%ld source_set_parts=%ld variants=%ld new_set=%ld first_new_cmid=%ld",
        (long)ibsetSource, (long)ibmtlSource, (long)cbprtSourceSet,
        (long)ccmidSource, (long)ibsetNew, (long)cmidNext);

    // Clone every costume variant used by the source set. Each clone is
    // collapsed to the selected part's one MTRL/BMDL slot and retargeted to
    // the new one-part set, so default costume application can never index
    // beyond the CMTL's material/model arrays.
    {
        int32_t *prgcmidSource = (int32_t *)pggCmid->QvGet(ibsetSource);
        for (int32_t icmid = 0; icmid < ccmidSource; ++icmid)
        {
            const int32_t cmidSource = prgcmidSource[icmid];
            const int32_t cmidNew = cmidNext + icmid;
            KID kidCmtlSource;
            bool fModelCloned = fFalse;
            PCMTL pcmtlCheck = pvNil;
            if (!pcfl->FGetKidChidCtg(kctgTmpl, tagTmpl.cno, cmidSource, kctgCmtl, &kidCmtlSource))
            {
                MultiLog(this,
                    "actor_studio_part_duplicate costume_clone_fail variant=%ld stage=source_cmtl source_cmid=%ld source_slot=%ld",
                    (long)icmid, (long)cmidSource, (long)ibmtlSource);
                ActorStudioDuplicateLog(this,
                    "costume_clone_fail variant=%ld stage=source_cmtl source_cmid=%ld source_slot=%ld",
                    (long)icmid, (long)cmidSource, (long)ibmtlSource);
                goto LEnd;
            }
            if (!F4DMMActorStudioCloneDuplicateCmtl(pcrf, pcfl, kidCmtlSource.cki.cno,
                    ibmtlSource, ibsetNew, &prgcmtlWrite[icmid].cnoCmtlNew,
                    &prgcmtlWrite[icmid].cnoMtrlNew, &prgcmtlWrite[icmid].cnoBmdlNew,
                    &fModelCloned))
            {
                MultiLog(this,
                    "actor_studio_part_duplicate costume_clone_fail variant=%ld stage=clone source_cmid=%ld source_cno=%ld source_slot=%ld",
                    (long)icmid, (long)cmidSource, (long)kidCmtlSource.cki.cno, (long)ibmtlSource);
                ActorStudioDuplicateLog(this,
                    "costume_clone_fail variant=%ld stage=clone source_cmid=%ld source_cno=%ld source_slot=%ld",
                    (long)icmid, (long)cmidSource, (long)kidCmtlSource.cki.cno, (long)ibmtlSource);
                goto LEnd;
            }

            prgcmtlWrite[icmid].cmidSource = cmidSource;
            prgcmtlWrite[icmid].cmidNew = cmidNew;
            prgcmidNew[icmid] = cmidNew;

            pcmtlCheck = (PCMTL)pcrf->PbacoFetch(kctgCmtl,
                prgcmtlWrite[icmid].cnoCmtlNew, CMTL::FReadCmtl);
            if (pcmtlCheck == pvNil || pcmtlCheck->Ibset() != ibsetNew || pcmtlCheck->Cbprt() != 1)
            {
                MultiLog(this,
                    "actor_studio_part_duplicate costume_clone_fail variant=%ld stage=validate new_cmid=%ld new_cno=%ld body_set=%ld cmtl_set=%ld cmtl_parts=%ld",
                    (long)icmid, (long)cmidNew, (long)prgcmtlWrite[icmid].cnoCmtlNew,
                    (long)ibsetNew, pcmtlCheck != pvNil ? (long)pcmtlCheck->Ibset() : -1L,
                    pcmtlCheck != pvNil ? (long)pcmtlCheck->Cbprt() : -1L);
                ActorStudioDuplicateLog(this,
                    "costume_clone_fail variant=%ld stage=validate new_cmid=%ld new_cno=%ld body_set=%ld cmtl_set=%ld cmtl_parts=%ld",
                    (long)icmid, (long)cmidNew, (long)prgcmtlWrite[icmid].cnoCmtlNew,
                    (long)ibsetNew, pcmtlCheck != pvNil ? (long)pcmtlCheck->Ibset() : -1L,
                    pcmtlCheck != pvNil ? (long)pcmtlCheck->Cbprt() : -1L);
                ReleasePpo(&pcmtlCheck);
                goto LEnd;
            }
            const bool fCheckHasModel = pcmtlCheck->FHasModels();
            ReleasePpo(&pcmtlCheck);

            MultiLog(this,
                "actor_studio_part_duplicate costume_clone variant=%ld source_cmid=%ld source_cno=%ld source_slot=%ld new_cmid=%ld new_cno=%ld mtrl_new=%ld bmdl_new=%ld model=%d check_model=%d",
                (long)icmid, (long)cmidSource, (long)kidCmtlSource.cki.cno,
                (long)ibmtlSource, (long)cmidNew, (long)prgcmtlWrite[icmid].cnoCmtlNew,
                (long)prgcmtlWrite[icmid].cnoMtrlNew, (long)prgcmtlWrite[icmid].cnoBmdlNew,
                (int)fModelCloned, (int)fCheckHasModel);
            ActorStudioDuplicateLog(this,
                "costume_clone variant=%ld source_cmid=%ld source_slot=%ld new_cmid=%ld new_cno=%ld mtrl_new=%ld bmdl_new=%ld model=%d",
                (long)icmid, (long)cmidSource, (long)ibmtlSource, (long)cmidNew,
                (long)prgcmtlWrite[icmid].cnoCmtlNew, (long)prgcmtlWrite[icmid].cnoMtrlNew,
                (long)prgcmtlWrite[icmid].cnoBmdlNew, (int)fModelCloned);
        }
        int32_t ccmidFixed = ccmidSource;
        if (!pggCmid->FAdd(LwMul(ccmidSource, SIZEOF(int32_t)), pvNil,
                           prgcmidNew, &ccmidFixed))
            goto LEnd;
    }

    for (int32_t anid = 0; anid < cactn; ++anid)
    {
        ActorStudioDuplicateLog(this, "action_expand begin action_index=%ld", (long)anid);
        KID kidAction;
        PGG pggcel = pvNil;
        PGG pggcelNew = pvNil;
        PGL pglbmat34 = pvNil;
        if (!pcfl->FGetKidChidCtg(kctgTmpl, tagTmpl.cno, anid, kctgActn, &kidAction) ||
            !F4DMMActorStudioReadActionGroups(pcfl, kidAction.cki.cno, &pggcel, &pglbmat34) ||
            pggcel->IvMac() <= 0)
        {
            ReleasePpo(&pggcel);
            ReleasePpo(&pglbmat34);
            goto LEnd;
        }
        pggcelNew = GG::PggNew(SIZEOF(CEL));
        if (pggcelNew == pvNil)
        {
            ReleasePpo(&pggcel);
            ReleasePpo(&pglbmat34);
            goto LEnd;
        }
        prgwrite[anid].cnoAction = kidAction.cki.cno;
        for (int32_t icel = 0; icel < pggcel->IvMac(); ++icel)
        {
            const int32_t cpartCel = pggcel->Cb(icel) / SIZEOF(CPS);
            if (cpartCel != cpartOld)
            {
                ReleasePpo(&pggcelNew);
                ReleasePpo(&pggcel);
                ReleasePpo(&pglbmat34);
                goto LEnd;
            }
            CPS rgcps[kc4DMMActorStudioFramePartMax];
            CopyPb(pggcel->QvGet(icel), rgcps, LwMul(cpartOld, SIZEOF(CPS)));
            CPS cpsNew = rgcps[ipartSource];
            BMAT34 bmat34;
            if (fKeepPositions)
            {
                const int32_t imatSource = cpsNew.imat34;
                if (!FIn(imatSource, 0, pglbmat34->IvMac()))
                {
                    ReleasePpo(&pggcelNew);
                    ReleasePpo(&pggcel);
                    ReleasePpo(&pglbmat34);
                    goto LEnd;
                }
                bmat34 = *(BMAT34 *)pglbmat34->QvGet(imatSource);
            }
            else
            {
                if (pbmat34Freeze != pvNil)
                    bmat34 = *pbmat34Freeze;
                else
                {
                    if (!FIn(ipartSource, 0, snapshotAnchor.cpart))
                    {
                        ReleasePpo(&pggcelNew);
                        ReleasePpo(&pggcel);
                        ReleasePpo(&pglbmat34);
                        goto LEnd;
                    }
                    // Keep this cel's model/geometry choice, but freeze only
                    // the transform to the current selected pose across every
                    // action and frame.
                    bmat34 = snapshotAnchor.rgbmat34[ipartSource];
                }
            }
            int32_t imatNew = ivNil;
            if (!pglbmat34->FAdd(&bmat34, &imatNew) || imatNew > 0x7fff)
            {
                ReleasePpo(&pggcelNew);
                ReleasePpo(&pggcel);
                ReleasePpo(&pglbmat34);
                goto LEnd;
            }
            cpsNew.imat34 = (int16_t)imatNew;
            rgcps[cpartOld] = cpsNew;
            CEL cel = *(CEL *)pggcel->QvFixedGet(icel);

            // GG records are variable-length. Rebuild the expanded GG from
            // scratch and leave the source group immutable until complete.
            if (!pggcelNew->FAdd(LwMul(cpartOld + 1, SIZEOF(CPS)), pvNil, rgcps, &cel))
            {
                ReleasePpo(&pggcelNew);
                ReleasePpo(&pggcel);
                ReleasePpo(&pglbmat34);
                goto LEnd;
            }
        }
        if (!pcfl->FAdd(pglbmat34->CbOnFile(), kctgGlxf, &prgwrite[anid].cnoXfNew, &blck) ||
            !pglbmat34->FWrite(&blck) ||
            !pcfl->FAdd(pggcelNew->CbOnFile(), kctgGgcl, &prgwrite[anid].cnoCelNew, &blck) ||
            !pggcelNew->FWrite(&blck))
        {
            ReleasePpo(&pggcelNew);
            ReleasePpo(&pggcel);
            ReleasePpo(&pglbmat34);
            goto LEnd;
        }
        ActorStudioDuplicateLog(this,
            "action_expand done action_index=%ld frames=%ld glxf_new=%ld ggcl_new=%ld",
            (long)anid, (long)pggcel->IvMac(), (long)prgwrite[anid].cnoXfNew,
            (long)prgwrite[anid].cnoCelNew);
        ReleasePpo(&pggcelNew);
        ReleasePpo(&pggcel);
        ReleasePpo(&pglbmat34);
    }

    ActorStudioDuplicateLog(this, "all_actions_expanded; writing template topology");
    if (!pcfl->FAdd(pglibactPar->CbOnFile(), kctgGlpi, &cnoParNew, &blck) ||
        !pglibactPar->FWrite(&blck) ||
        !pcfl->FAdd(pglibset->CbOnFile(), kctgGlbs, &cnoSetNew, &blck) ||
        !pglibset->FWrite(&blck) ||
        !F4DMMWriteGgChild(pcfl, kctgGgcm, pggCmid, &cnoGgcmNew))
        goto LEnd;
    for (int32_t anid = 0; anid < cactn; ++anid)
    {
        if (!F4DMMActorStudioReplacePoseChildren(pcfl, prgwrite[anid].cnoAction,
                                                  prgwrite[anid].cnoCelNew,
                                                  prgwrite[anid].cnoXfNew,
                                                  &prgwrite[anid].cnoCelOld,
                                                  &prgwrite[anid].cnoXfOld))
            goto LRollback;
        prgwrite[anid].fReplaced = fTrue;
    }

    // Publish the cloned CMTLs only after all replacement action resources
    // are safely written. Until this point the original TMPL remains valid.
    for (int32_t icmid = 0; icmid < ccmidSource; ++icmid)
    {
        if (!pcfl->FAdoptChild(kctgTmpl, tagTmpl.cno, kctgCmtl,
                               prgcmtlWrite[icmid].cnoCmtlNew,
                               (CHID)prgcmtlWrite[icmid].cmidNew))
            goto LRollback;
        prgcmtlWrite[icmid].fAdopted = fTrue;
    }
    if (!F4DMMActorStudioReplaceTmplChild(pcfl, tagTmpl.cno, kctgGlpi, cnoParNew, &cnoParOld))
        goto LRollback;
    fParReplaced = fTrue;
    if (!F4DMMActorStudioReplaceTmplChild(pcfl, tagTmpl.cno, kctgGlbs, cnoSetNew, &cnoSetOld))
        goto LRollback;
    fSetReplaced = fTrue;
    if (!F4DMMActorStudioReplaceTmplChild(pcfl, tagTmpl.cno, kctgGgcm, cnoGgcmNew, &cnoGgcmOld))
        goto LRollback;
    fGgcmReplaced = fTrue;

    ActorStudioDuplicateLog(this,
        "resource_commit complete glpi_new=%ld glbs_new=%ld ggcm_new=%ld old_parts=%ld new_parts=%ld old_sets=%ld new_sets=%ld cloned_variants=%ld",
        (long)cnoParNew, (long)cnoSetNew, (long)cnoGgcmNew,
        (long)cpartOld, (long)(cpartOld + 1), (long)cbsetOld, (long)(cbsetOld + 1),
        (long)ccmidSource);
    MultiLog(this,
        "actor_studio_part_duplicate topology_committed tmpl=%ld old_parts=%ld new_parts=%ld old_sets=%ld new_sets=%ld variants=%ld actions=%ld",
        (long)tagTmpl.cno, (long)cpartOld, (long)(cpartOld + 1),
        (long)cbsetOld, (long)(cbsetOld + 1), (long)ccmidSource, (long)cactn);
    for (int32_t anid = 0; anid < cactn; ++anid)
        pcrf->FSetCrep(crepToss, kctgActn, prgwrite[anid].cnoAction, ACTN::FReadActn);

    // Keep the already-live writable TMPL object. Refresh its topology and
    // costume map in place now that GLPI/GLBS/GGCM and CMTLs agree.
    ptmplLive = pactr->Ptmpl();
    ActorStudioDuplicateLog(this, "tmpl_refresh begin ptmpl=%p", ptmplLive);
    if (ptmplLive == pvNil || !ptmplLive->FRefreshPartShape())
        goto LRollback;
    ActorStudioDuplicateLog(this,
        "tmpl_refresh done ptmpl=%p parts=%ld sets=%ld variants_total=%ld",
        ptmplLive, (long)(cpartOld + 1), (long)(cbsetOld + 1), (long)(cmtlCount + ccmidSource));
    MultiLog(this,
        "actor_studio_part_duplicate tmpl_refresh_ok tmpl=%ld ptmpl=%p new_set=%ld",
        (long)tagTmpl.cno, ptmplLive, (long)ibsetNew);

    if (Pscen() != pvNil && Pscen()->PglRollCall() != pvNil)
    {
        PGL pglpactr = Pscen()->PglRollCall();
        for (int32_t iactr = 0; iactr < pglpactr->IvMac(); ++iactr)
        {
            PACTR pactrLive = pvNil;
            pglpactr->Get(iactr, &pactrLive);
            if (pactrLive == pvNil)
                continue;
            TAG tagLive;
            pactrLive->GetTagTmpl(&tagLive);
            if (!F4DMMActorStudioSameTemplateIdentity(&tagLive, &tagTmpl))
                continue;
            PTMPL ptmplActor = pactrLive->Ptmpl();
            if (ptmplActor == pvNil)
                goto LRollback;
            if (ptmplActor != ptmplLive && !ptmplActor->FRefreshPartShape())
                goto LRollback;
            MultiLog(this,
                "actor_studio_part_duplicate live_conform_begin arid=%ld ptmpl=%p body_parts=%ld",
                (long)pactrLive->Arid(), ptmplActor,
                pactrLive->Pbody() != pvNil ? (long)pactrLive->Pbody()->Cpart() : -1L);
            ActorStudioDuplicateLog(this, "live_conform begin arid=%ld body=%p old_parts=%ld",
                (long)pactrLive->Arid(), pactrLive->Pbody(),
                pactrLive->Pbody() != pvNil ? (long)pactrLive->Pbody()->Cpart() : -1L);
            if (!pactrLive->FRefreshBodyForTemplateMutation())
                goto LRollback;
            ActorStudioDuplicateLog(this, "live_conform done arid=%ld body=%p new_parts=%ld",
                (long)pactrLive->Arid(), pactrLive->Pbody(),
                pactrLive->Pbody() != pvNil ? (long)pactrLive->Pbody()->Cpart() : -1L);
            MultiLog(this,
                "actor_studio_part_duplicate live_conform_ok arid=%ld body_parts=%ld",
                (long)pactrLive->Arid(),
                pactrLive->Pbody() != pvNil ? (long)pactrLive->Pbody()->Cpart() : -1L);
        }
    }
    if (pactr->Arid() == aridNil)
    {
        ActorStudioDuplicateLog(this,
            "detached_rebuild deferred pactr=%p stale_body=%p stale_parts=%ld",
            pactr, pactr->Pbody(),
            pactr->Pbody() != pvNil ? (long)pactr->Pbody()->Cpart() : -1L);
    }

    if (fAddUndo)
    {
        pasdu = ASDU::PasduNew();
        if (pasdu == pvNil)
            goto LRollback;
        pasdu->SetState(pactr->Arid(), anidCurrent, celnCurrent, ipartSource,
                        ipartNew, tagTmpl.cno, fKeepPositions, pbmat34Freeze);
        if (!FAddUndo(pasdu))
        {
            MultiLog(this,
                "actor_studio_part_duplicate undo_add_failed tmpl=%ld source_part=%ld new_part=%ld",
                (long)tagTmpl.cno, (long)ipartSource, (long)ipartNew);
            ClearUndo();
            goto LRollback;
        }
    }

    _fRequiresVmmSave = fTrue;
    _fAutosaveDirty = fTrue;
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    MultiLog(this,
        "actor_studio_part_duplicate arid=%ld tmpl=%ld source_part=%ld new_part=%ld source_set=%ld new_set=%ld keep_positions=%d actions=%ld undo_record=%d",
        (long)pactr->Arid(), (long)tagTmpl.cno, (long)ipartSource, (long)ipartNew,
        (long)ibsetSource, (long)ibsetNew, (int)fKeepPositions, (long)cactn, (int)fAddUndo);
    if (pipartNew != pvNil)
        *pipartNew = ipartNew;
    ActorStudioDuplicateLog(this,
        "CORE_SUCCESS tmpl=%ld new_part=%ld new_set=%ld detached=%d",
        (long)tagTmpl.cno, (long)ipartNew, (long)ibsetNew, (int)(pactr->Arid() == aridNil));
    fRet = fTrue;
    goto LEnd;

LRollback:
    if (fGgcmReplaced && cnoGgcmOld != cnoNil)
    {
        CNO cnoIgnore = cnoNil;
        F4DMMActorStudioReplaceTmplChild(pcfl, tagTmpl.cno, kctgGgcm, cnoGgcmOld, &cnoIgnore);
        fGgcmReplaced = fFalse;
    }
    if (fSetReplaced && cnoSetOld != cnoNil)
    {
        CNO cnoIgnore = cnoNil;
        F4DMMActorStudioReplaceTmplChild(pcfl, tagTmpl.cno, kctgGlbs, cnoSetOld, &cnoIgnore);
        fSetReplaced = fFalse;
    }
    if (fParReplaced && cnoParOld != cnoNil)
    {
        CNO cnoIgnore = cnoNil;
        F4DMMActorStudioReplaceTmplChild(pcfl, tagTmpl.cno, kctgGlpi, cnoParOld, &cnoIgnore);
        fParReplaced = fFalse;
    }
    if (prgwrite != pvNil)
    {
        for (int32_t anid = cactn - 1; anid >= 0; --anid)
        {
            if (!prgwrite[anid].fReplaced)
                continue;
            CNO cnoIgnoreCel = cnoNil;
            CNO cnoIgnoreXf = cnoNil;
            F4DMMActorStudioReplacePoseChildren(pcfl, prgwrite[anid].cnoAction,
                                                  prgwrite[anid].cnoCelOld,
                                                  prgwrite[anid].cnoXfOld,
                                                  &cnoIgnoreCel, &cnoIgnoreXf);
            prgwrite[anid].fReplaced = fFalse;
        }
    }
    if (prgcmtlWrite != pvNil)
    {
        for (int32_t icmid = ccmidSource - 1; icmid >= 0; --icmid)
        {
            if (!prgcmtlWrite[icmid].fAdopted)
                continue;
            pcfl->DeleteChild(kctgTmpl, tagTmpl.cno, kctgCmtl,
                              prgcmtlWrite[icmid].cnoCmtlNew,
                              (CHID)prgcmtlWrite[icmid].cmidNew);
            prgcmtlWrite[icmid].fAdopted = fFalse;
        }
    }
    for (int32_t anid = 0; anid < cactn; ++anid)
        if (prgwrite != pvNil && prgwrite[anid].cnoAction != cnoNil)
            pcrf->FSetCrep(crepToss, kctgActn, prgwrite[anid].cnoAction, ACTN::FReadActn);

    ptmplLive = pactr != pvNil ? pactr->Ptmpl() : pvNil;
    if (ptmplLive != pvNil)
        ptmplLive->FRefreshPartShape();
    if (Pscen() != pvNil && Pscen()->PglRollCall() != pvNil)
    {
        PGL pglpactrRollback = Pscen()->PglRollCall();
        for (int32_t iactr = 0; iactr < pglpactrRollback->IvMac(); ++iactr)
        {
            PACTR pactrLive = pvNil;
            pglpactrRollback->Get(iactr, &pactrLive);
            if (pactrLive == pvNil)
                continue;
            TAG tagLive;
            pactrLive->GetTagTmpl(&tagLive);
            if (!F4DMMActorStudioSameTemplateIdentity(&tagLive, &tagTmpl))
                continue;
            PTMPL ptmplActor = pactrLive->Ptmpl();
            if (ptmplActor != pvNil && ptmplActor != ptmplLive)
                ptmplActor->FRefreshPartShape();
            pactrLive->FRefreshBodyForTemplateMutation();
        }
    }
    ActorStudioDuplicateLog(this, "ROLLBACK tmpl=%ld source_part=%ld",
                            (long)tagTmpl.cno, (long)ipartSource);
    MultiLog(this,
        "actor_studio_part_duplicate rollback arid=%ld tmpl=%ld source_part=%ld keep_positions=%d",
        (long)pactr->Arid(), (long)tagTmpl.cno, (long)ipartSource, (int)fKeepPositions);

LEnd:
    if (!fRet && pcfl != pvNil)
    {
        if (prgwrite != pvNil)
        {
            for (int32_t anid = 0; anid < cactn; ++anid)
            {
                if (!prgwrite[anid].fReplaced)
                {
                    if (prgwrite[anid].cnoCelNew != cnoNil && pcfl->FFind(kctgGgcl, prgwrite[anid].cnoCelNew))
                        pcfl->Delete(kctgGgcl, prgwrite[anid].cnoCelNew);
                    if (prgwrite[anid].cnoXfNew != cnoNil && pcfl->FFind(kctgGlxf, prgwrite[anid].cnoXfNew))
                        pcfl->Delete(kctgGlxf, prgwrite[anid].cnoXfNew);
                }
            }
        }
        if (!fParReplaced && cnoParNew != cnoNil && pcfl->FFind(kctgGlpi, cnoParNew))
            pcfl->Delete(kctgGlpi, cnoParNew);
        if (!fSetReplaced && cnoSetNew != cnoNil && pcfl->FFind(kctgGlbs, cnoSetNew))
            pcfl->Delete(kctgGlbs, cnoSetNew);
        if (!fGgcmReplaced && cnoGgcmNew != cnoNil && pcfl->FFind(kctgGgcm, cnoGgcmNew))
            pcfl->Delete(kctgGgcm, cnoGgcmNew);
        if (prgcmtlWrite != pvNil)
        {
            for (int32_t icmid = 0; icmid < ccmidSource; ++icmid)
            {
                if (prgcmtlWrite[icmid].cnoCmtlNew == cnoNil)
                    continue;
                if (prgcmtlWrite[icmid].fAdopted)
                {
                    pcfl->DeleteChild(kctgTmpl, tagTmpl.cno, kctgCmtl,
                                      prgcmtlWrite[icmid].cnoCmtlNew,
                                      (CHID)prgcmtlWrite[icmid].cmidNew);
                    prgcmtlWrite[icmid].fAdopted = fFalse;
                }
                pcrf->FSetCrep(crepToss, kctgCmtl,
                               prgcmtlWrite[icmid].cnoCmtlNew, CMTL::FReadCmtl);
                if (prgcmtlWrite[icmid].cnoMtrlNew != cnoNil)
                    pcrf->FSetCrep(crepToss, kctgMtrl,
                                   prgcmtlWrite[icmid].cnoMtrlNew, MTRL::FReadMtrl);
                if (prgcmtlWrite[icmid].cnoBmdlNew != cnoNil)
                    pcrf->FSetCrep(crepToss, kctgBmdl,
                                   prgcmtlWrite[icmid].cnoBmdlNew, MODL::FReadModl);
                if (pcfl->FFind(kctgCmtl, prgcmtlWrite[icmid].cnoCmtlNew))
                    pcfl->Delete(kctgCmtl, prgcmtlWrite[icmid].cnoCmtlNew);
                if (prgcmtlWrite[icmid].cnoMtrlNew != cnoNil &&
                    pcfl->FFind(kctgMtrl, prgcmtlWrite[icmid].cnoMtrlNew))
                    pcfl->Delete(kctgMtrl, prgcmtlWrite[icmid].cnoMtrlNew);
                if (prgcmtlWrite[icmid].cnoBmdlNew != cnoNil &&
                    pcfl->FFind(kctgBmdl, prgcmtlWrite[icmid].cnoBmdlNew))
                    pcfl->Delete(kctgBmdl, prgcmtlWrite[icmid].cnoBmdlNew);
            }
        }
    }
    if (!fRet)
        ActorStudioDuplicateLog(this, "END_FAIL tmpl=%ld source_part=%ld",
                                (long)tagTmpl.cno, (long)ipartSource);
    ReleasePpo(&pasdu);
    FreePpv((void **)&prgcmidNew);
    FreePpv((void **)&prgcmtlWrite);
    FreePpv((void **)&prgwrite);
    ReleasePpo(&pggCmid);
    ReleasePpo(&pglibactPar);
    ReleasePpo(&pglibset);
    return fRet;
}

bool MVIE::FActorStudioDuplicatePartRange(PACTR pactr, int32_t anidCurrent, int32_t celnCurrent,
                                           int32_t ipartFirst, int32_t cpartSource,
                                           int32_t selectionKind, bool fKeepPositions,
                                           int32_t *pipartNewFirst)
{
    return FActorStudioDuplicatePartRangeCore(pactr, anidCurrent, celnCurrent,
                                               ipartFirst, cpartSource, selectionKind,
                                               fKeepPositions, pipartNewFirst, fTrue);
}

bool MVIE::FActorStudioDuplicatePartRangeCore(PACTR pactr, int32_t anidCurrent, int32_t celnCurrent,
                                               int32_t ipartFirst, int32_t cpartSource,
                                               int32_t selectionKind, bool fKeepPositions,
                                               int32_t *pipartNewFirst, bool fAddUndo)
{
    AssertThis(0);
    if (pipartNewFirst != pvNil)
        *pipartNewFirst = ivNil;
    if (pactr == pvNil || pactr->Ptmpl() == pvNil || pactr->Ptmpl()->FIsTdt() ||
        cpartSource <= 0 || ipartFirst < 0 ||
        !FIn(selectionKind, kasskObject, kasskGroup + 1))
        return fFalse;

    TAG tagTmpl;
    pactr->GetTagTmpl(&tagTmpl);
    if (tagTmpl.sid != ksidUseCrf || tagTmpl.ctg != kctgTmpl)
        return fFalse;

    int32_t idObject = 0;
    for (int32_t iobj = 0; iobj < _c4DMMCustomObject; ++iobj)
    {
        if (_rg4DMMCustomObject[iobj].cnoOwnedTmpl == tagTmpl.cno &&
            F4DMMCustomObjectIsHandmade(iobj))
        {
            idObject = _rg4DMMCustomObject[iobj].id;
            break;
        }
    }
    if (idObject <= 0)
        return fFalse;

    const int32_t cmetaOld = _c4DMMCustomPart;
    int32_t cmetaClone = 0;
    int32_t idImportSource = 0;
    int32_t idGroupSource = 0;
    int32_t imetaObject = ivNil;

    if (selectionKind == kasskObject)
    {
        for (int32_t imeta = 0; imeta < cmetaOld; ++imeta)
        {
            const CUSTOMPART &meta = _rg4DMMCustomPart[imeta];
            if (meta.idObject == idObject && meta.ipartFirst == ipartFirst &&
                meta.cpart == cpartSource)
            {
                imetaObject = imeta;
                idImportSource = meta.idImport;
                idGroupSource = meta.idGroup;
                cmetaClone = 1;
                break;
            }
        }
        if (imetaObject == ivNil)
            return fFalse;
    }
    else
    {
        // A virtual OG row is the contiguous union of all provenance records
        // from one import/group pair. Verify that exact range before cloning.
        for (int32_t imeta = 0; imeta < cmetaOld; ++imeta)
        {
            const CUSTOMPART &meta = _rg4DMMCustomPart[imeta];
            if (meta.idObject == idObject && meta.idGroup > 0 && meta.cpart > 0 &&
                FIn(ipartFirst, meta.ipartFirst, meta.ipartFirst + meta.cpart))
            {
                idImportSource = meta.idImport;
                idGroupSource = meta.idGroup;
                break;
            }
        }
        if (idImportSource <= 0 || idGroupSource <= 0)
            return fFalse;

        int32_t ipartMetaFirst = 0x7fffffff;
        int32_t ipartMetaLim = 0;
        for (int32_t imeta = 0; imeta < cmetaOld; ++imeta)
        {
            const CUSTOMPART &meta = _rg4DMMCustomPart[imeta];
            if (meta.idObject != idObject || meta.idImport != idImportSource ||
                meta.idGroup != idGroupSource || meta.cpart <= 0)
                continue;
            ipartMetaFirst = LwMin(ipartMetaFirst, meta.ipartFirst);
            ipartMetaLim = LwMax(ipartMetaLim, meta.ipartFirst + meta.cpart);
            ++cmetaClone;
        }
        if (cmetaClone <= 0 || ipartMetaFirst != ipartFirst ||
            ipartMetaLim != ipartFirst + cpartSource)
            return fFalse;
    }

    if (_c4DMMCustomPart + cmetaClone > kc4DMMCustomPartMax)
        return fFalse;

    // Read the current part count once. The v135 core only appends, so every
    // source index in the selected range remains stable throughout this loop.
    PGL pglibactPar = pvNil;
    PGL pglibset = pvNil;
    PCRF pcrf = tagTmpl.pcrf != pvNil ? tagTmpl.pcrf : _pcrfAutoSave;
    PCFL pcfl = pcrf != pvNil ? pcrf->Pcfl() : pvNil;
    if (pcfl == pvNil || !F4DMMActorStudioReadTmplPartLists(
                           pcfl, tagTmpl.cno, &pglibactPar, &pglibset))
    {
        ReleasePpo(&pglibactPar);
        ReleasePpo(&pglibset);
        return fFalse;
    }
    const int32_t cpartBefore = pglibactPar->IvMac();
    ReleasePpo(&pglibactPar);
    ReleasePpo(&pglibset);
    if (ipartFirst + cpartSource > cpartBefore ||
        cpartBefore + cpartSource > kc4DMMActorStudioFramePartMax)
        return fFalse;

    MultiLog(this,
        "actor_studio_range_duplicate begin tmpl=%ld kind=%ld source_first=%ld parts=%ld append_first=%ld metadata=%ld keep_positions=%d",
        (long)tagTmpl.cno, (long)selectionKind, (long)ipartFirst,
        (long)cpartSource, (long)cpartBefore, (long)cmetaClone, (int)fKeepPositions);

    int32_t cpartDone = 0;
    for (int32_t i = 0; i < cpartSource; ++i)
    {
        int32_t ipartNew = ivNil;
        if (!FActorStudioDuplicatePartCore(pactr, anidCurrent, celnCurrent,
                                            ipartFirst + i, fKeepPositions,
                                            pvNil, &ipartNew, fFalse) ||
            ipartNew != cpartBefore + i)
        {
            if (cpartDone > 0)
                FActorStudioDeletePartRangeCore(pactr, cpartBefore, cpartDone, fFalse);
            MultiLog(this,
                "actor_studio_range_duplicate fail tmpl=%ld stage=part source=%ld completed=%ld",
                (long)tagTmpl.cno, (long)(ipartFirst + i), (long)cpartDone);
            return fFalse;
        }
        ++cpartDone;
    }

    const int32_t idImportNew = selectionKind == kasskGroup ?
        Id4DMMNextCustomImport(_rg4DMMCustomPart, cmetaOld, idObject) : idImportSource;
    int32_t cmetaAdded = 0;
    for (int32_t imeta = 0; imeta < cmetaOld; ++imeta)
    {
        const CUSTOMPART &metaSource = _rg4DMMCustomPart[imeta];
        const bool fMatch = selectionKind == kasskObject ? imeta == imetaObject :
            (metaSource.idObject == idObject && metaSource.idImport == idImportSource &&
             metaSource.idGroup == idGroupSource && metaSource.cpart > 0);
        if (!fMatch)
            continue;

        CUSTOMPART metaNew = metaSource;
        metaNew.ipartFirst = cpartBefore + (metaSource.ipartFirst - ipartFirst);
        if (selectionKind == kasskGroup)
            metaNew.idImport = idImportNew;
        _rg4DMMCustomPart[_c4DMMCustomPart++] = metaNew;
        ++cmetaAdded;
    }
    if (cmetaAdded != cmetaClone)
    {
        _c4DMMCustomPart = cmetaOld;
        FActorStudioDeletePartRangeCore(pactr, cpartBefore, cpartSource, fFalse);
        return fFalse;
    }

    PASRU pasru = pvNil;
    if (fAddUndo)
    {
        pasru = ASRU::PasruNew();
        if (pasru == pvNil)
        {
            _c4DMMCustomPart = cmetaOld;
            FActorStudioDeletePartRangeCore(pactr, cpartBefore, cpartSource, fFalse);
            return fFalse;
        }
        pasru->SetState(pactr->Arid(), anidCurrent, celnCurrent, ipartFirst,
                         cpartSource, selectionKind, cpartBefore, tagTmpl.cno,
                         fKeepPositions);
        if (!FAddUndo(pasru))
        {
            _c4DMMCustomPart = cmetaOld;
            FActorStudioDeletePartRangeCore(pactr, cpartBefore, cpartSource, fFalse);
            ReleasePpo(&pasru);
            return fFalse;
        }
    }
    ReleasePpo(&pasru);

    _fRequiresVmmSave = fTrue;
    _fAutosaveDirty = fTrue;
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    if (pipartNewFirst != pvNil)
        *pipartNewFirst = cpartBefore;
    MultiLog(this,
        "actor_studio_range_duplicate ok tmpl=%ld kind=%ld source_first=%ld parts=%ld duplicate_first=%ld metadata=%ld new_import=%ld undo=%d",
        (long)tagTmpl.cno, (long)selectionKind, (long)ipartFirst,
        (long)cpartSource, (long)cpartBefore, (long)cmetaAdded,
        (long)idImportNew, (int)fAddUndo);
    return fTrue;
}

bool ASDE::FSave(PMVIE pmvie, PACTR pactr, int32_t anid, int32_t celn,
                   int32_t ipartFirst, int32_t cpartDelete, int32_t selectionKind)
{
    if (pmvie == pvNil || pactr == pvNil || pactr->Ptmpl() == pvNil ||
        ipartFirst < 0 || cpartDelete <= 0)
        return fFalse;

    TAG tagTmpl;
    pactr->GetTagTmpl(&tagTmpl);
    PCRF pcrf = tagTmpl.pcrf != pvNil ? tagTmpl.pcrf : pmvie->_pcrfAutoSave;
    PCFL pcfl = pcrf != pvNil ? pcrf->Pcfl() : pvNil;
    if (pcfl == pvNil || tagTmpl.sid != ksidUseCrf || tagTmpl.ctg != kctgTmpl)
        return fFalse;

    _idObject = 0;
    for (int32_t iobj = 0; iobj < pmvie->_c4DMMCustomObject; ++iobj)
    {
        if (pmvie->_rg4DMMCustomObject[iobj].cnoOwnedTmpl == tagTmpl.cno &&
            pmvie->F4DMMCustomObjectIsHandmade(iobj))
        {
            _idObject = pmvie->_rg4DMMCustomObject[iobj].id;
            break;
        }
    }
    if (_idObject <= 0)
        return fFalse;

    if (!F4DMMActorStudioReadTmplPartLists(pcfl, tagTmpl.cno,
                                            &_pglibactPar, &_pglibset))
        return fFalse;
    if (_pglibactPar == pvNil || _pglibset == pvNil ||
        _pglibactPar->IvMac() != _pglibset->IvMac() ||
        ipartFirst + cpartDelete > _pglibactPar->IvMac())
        return fFalse;

    KID kidGgcm;
    BLCK blckGgcm;
    int16_t boGgcm = kboCur;
    if (!pcfl->FGetKidChidCtg(kctgTmpl, tagTmpl.cno, 0, kctgGgcm, &kidGgcm) ||
        !pcfl->FFind(kidGgcm.cki.ctg, kidGgcm.cki.cno, &blckGgcm))
        return fFalse;
    _pggCmid = GG::PggRead(&blckGgcm, &boGgcm);
    if (_pggCmid == pvNil || _pggCmid->CbFixed() != SIZEOF(int32_t))
        return fFalse;
    if (boGgcm == kboOther)
    {
        for (int32_t ibset = 0; ibset < _pggCmid->IvMac(); ++ibset)
        {
            SwapBytesRglw(_pggCmid->QvFixedGet(ibset), 1);
            SwapBytesRglw(_pggCmid->QvGet(ibset),
                           *(int32_t *)_pggCmid->QvFixedGet(ibset));
        }
    }

    _cactn = pactr->Ptmpl()->Cactn();
    if (_cactn <= 0 || !FAllocPv((void **)&_prgAction,
                                  LwMul(_cactn, SIZEOF(ASDEACTIONSNAPSHOT)),
                                  fmemClear, mprNormal))
        return fFalse;
    for (int32_t iactn = 0; iactn < _cactn; ++iactn)
    {
        KID kidAction;
        if (!pcfl->FGetKidChidCtg(kctgTmpl, tagTmpl.cno, iactn, kctgActn, &kidAction) ||
            !F4DMMActorStudioReadActionGroups(pcfl, kidAction.cki.cno,
                                               &_prgAction[iactn].pggcel,
                                               &_prgAction[iactn].pglbmat34))
            return fFalse;
        _prgAction[iactn].cnoAction = kidAction.cki.cno;
    }

    _cmeta = 0;
    for (int32_t imeta = 0; imeta < pmvie->_c4DMMCustomPart; ++imeta)
        if (pmvie->_rg4DMMCustomPart[imeta].idObject == _idObject)
            ++_cmeta;
    if (_cmeta > 0)
    {
        if (!FAllocPv((void **)&_prgMeta, LwMul(_cmeta, SIZEOF(CUSTOMPART)),
                      fmemClear, mprNormal))
            return fFalse;
        int32_t imetaDst = 0;
        for (int32_t imeta = 0; imeta < pmvie->_c4DMMCustomPart; ++imeta)
            if (pmvie->_rg4DMMCustomPart[imeta].idObject == _idObject)
                _prgMeta[imetaDst++] = pmvie->_rg4DMMCustomPart[imeta];
        if (imetaDst != _cmeta)
            return fFalse;
    }

    _arid = pactr->Arid();
    _anid = anid;
    _celn = celn;
    _ipartFirst = ipartFirst;
    _cpartDelete = cpartDelete;
    _selectionKind = selectionKind;
    _cnoTmpl = tagTmpl.cno;
    _fDeletedPresent = fTrue;
    MVIE::MultiLog(pmvie,
        "actor_studio_delete_undo snapshot tmpl=%ld object=%ld first=%ld parts=%ld actions=%ld metadata=%ld kind=%ld",
        (long)_cnoTmpl, (long)_idObject, (long)_ipartFirst, (long)_cpartDelete,
        (long)_cactn, (long)_cmeta, (long)_selectionKind);
    return fTrue;
}

bool ASDE::FRestore(PACTR pactr)
{
    if (_pmvie == pvNil || pactr == pvNil || pactr->Ptmpl() == pvNil ||
        _pglibactPar == pvNil || _pglibset == pvNil || _pggCmid == pvNil ||
        _prgAction == pvNil || _cactn <= 0)
        return fFalse;

    TAG tagTmpl;
    pactr->GetTagTmpl(&tagTmpl);
    PCRF pcrf = tagTmpl.pcrf != pvNil ? tagTmpl.pcrf : _pmvie->_pcrfAutoSave;
    PCFL pcfl = pcrf != pvNil ? pcrf->Pcfl() : pvNil;
    if (pcfl == pvNil || tagTmpl.sid != ksidUseCrf || tagTmpl.ctg != kctgTmpl ||
        tagTmpl.cno != _cnoTmpl || pactr->Ptmpl()->Cactn() != _cactn)
        return fFalse;

    int32_t cmetaOther = 0;
    for (int32_t imeta = 0; imeta < _pmvie->_c4DMMCustomPart; ++imeta)
        if (_pmvie->_rg4DMMCustomPart[imeta].idObject != _idObject)
            ++cmetaOther;
    if (cmetaOther + _cmeta > kc4DMMCustomPartMax)
        return fFalse;

    ASACTIONPARTWRITE *prgwrite = pvNil;
    CNO cnoParNew = cnoNil;
    CNO cnoSetNew = cnoNil;
    CNO cnoGgcmNew = cnoNil;
    CNO cnoParOld = cnoNil;
    CNO cnoSetOld = cnoNil;
    CNO cnoGgcmOld = cnoNil;
    bool fParReplaced = fFalse;
    bool fSetReplaced = fFalse;
    bool fGgcmReplaced = fFalse;
    bool fRet = fFalse;

    if (!FAllocPv((void **)&prgwrite, LwMul(_cactn, SIZEOF(ASACTIONPARTWRITE)),
                  fmemClear, mprNormal))
        goto LEnd;

    for (int32_t iactn = 0; iactn < _cactn; ++iactn)
    {
        BLCK blck;
        prgwrite[iactn].cnoAction = _prgAction[iactn].cnoAction;
        if (_prgAction[iactn].pggcel == pvNil || _prgAction[iactn].pglbmat34 == pvNil ||
            !pcfl->FAdd(_prgAction[iactn].pglbmat34->CbOnFile(), kctgGlxf,
                         &prgwrite[iactn].cnoXfNew, &blck) ||
            !_prgAction[iactn].pglbmat34->FWrite(&blck) ||
            !pcfl->FAdd(_prgAction[iactn].pggcel->CbOnFile(), kctgGgcl,
                         &prgwrite[iactn].cnoCelNew, &blck) ||
            !_prgAction[iactn].pggcel->FWrite(&blck))
            goto LEnd;
    }

    if (!F4DMMWriteGlChild(pcfl, kctgGlpi, _pglibactPar, &cnoParNew) ||
        !F4DMMWriteGlChild(pcfl, kctgGlbs, _pglibset, &cnoSetNew) ||
        !F4DMMWriteGgChild(pcfl, kctgGgcm, _pggCmid, &cnoGgcmNew))
        goto LEnd;

    for (int32_t iactn = 0; iactn < _cactn; ++iactn)
    {
        if (!F4DMMActorStudioReplacePoseChildren(pcfl, prgwrite[iactn].cnoAction,
                                                  prgwrite[iactn].cnoCelNew,
                                                  prgwrite[iactn].cnoXfNew,
                                                  &prgwrite[iactn].cnoCelOld,
                                                  &prgwrite[iactn].cnoXfOld))
            goto LRollback;
        prgwrite[iactn].fReplaced = fTrue;
    }
    if (!F4DMMActorStudioReplaceTmplChild(pcfl, _cnoTmpl, kctgGlpi,
                                           cnoParNew, &cnoParOld))
        goto LRollback;
    fParReplaced = fTrue;
    if (!F4DMMActorStudioReplaceTmplChild(pcfl, _cnoTmpl, kctgGlbs,
                                           cnoSetNew, &cnoSetOld))
        goto LRollback;
    fSetReplaced = fTrue;
    if (!F4DMMActorStudioReplaceTmplChild(pcfl, _cnoTmpl, kctgGgcm,
                                           cnoGgcmNew, &cnoGgcmOld))
        goto LRollback;
    fGgcmReplaced = fTrue;

    for (int32_t iactn = 0; iactn < _cactn; ++iactn)
        pcrf->FSetCrep(crepToss, kctgActn, prgwrite[iactn].cnoAction, ACTN::FReadActn);

    if (!pactr->Ptmpl()->FRefreshPartShape())
        goto LRollback;
    if (_pmvie->Pscen() != pvNil && _pmvie->Pscen()->PglRollCall() != pvNil)
    {
        PGL pglpactr = _pmvie->Pscen()->PglRollCall();
        for (int32_t iactr = 0; iactr < pglpactr->IvMac(); ++iactr)
        {
            PACTR pactrLive = pvNil;
            pglpactr->Get(iactr, &pactrLive);
            if (pactrLive == pvNil)
                continue;
            TAG tagLive;
            pactrLive->GetTagTmpl(&tagLive);
            if (!F4DMMActorStudioSameTemplateIdentity(&tagLive, &tagTmpl))
                continue;
            PTMPL ptmplLive = pactrLive->Ptmpl();
            if (ptmplLive == pvNil)
                goto LRollback;
            if (ptmplLive != pactr->Ptmpl() && !ptmplLive->FRefreshPartShape())
                goto LRollback;
            if (!pactrLive->FRefreshBodyForTemplateMutation())
                goto LRollback;
        }
    }

    {
        int32_t imetaDst = 0;
        for (int32_t imeta = 0; imeta < _pmvie->_c4DMMCustomPart; ++imeta)
        {
            if (_pmvie->_rg4DMMCustomPart[imeta].idObject == _idObject)
                continue;
            if (imetaDst != imeta)
                _pmvie->_rg4DMMCustomPart[imetaDst] = _pmvie->_rg4DMMCustomPart[imeta];
            ++imetaDst;
        }
        for (int32_t imeta = 0; imeta < _cmeta; ++imeta)
            _pmvie->_rg4DMMCustomPart[imetaDst++] = _prgMeta[imeta];
        if (imetaDst < _pmvie->_c4DMMCustomPart)
            ClearPb(&_pmvie->_rg4DMMCustomPart[imetaDst],
                    LwMul(_pmvie->_c4DMMCustomPart - imetaDst, SIZEOF(CUSTOMPART)));
        _pmvie->_c4DMMCustomPart = imetaDst;
    }

    _pmvie->_fRequiresVmmSave = fTrue;
    _pmvie->_fAutosaveDirty = fTrue;
    _pmvie->SetDirty();
    if (_pmvie->_pbwld != pvNil)
        _pmvie->_pbwld->MarkDirty();
    _pmvie->InvalViewsAndScb();
    MVIE::MultiLog(_pmvie,
        "actor_studio_delete_undo restore_ok tmpl=%ld first=%ld parts=%ld metadata=%ld",
        (long)_cnoTmpl, (long)_ipartFirst, (long)_cpartDelete, (long)_cmeta);
    fRet = fTrue;
    goto LEnd;

LRollback:
    if (fGgcmReplaced && cnoGgcmOld != cnoNil)
    {
        CNO cnoIgnore = cnoNil;
        if (F4DMMActorStudioReplaceTmplChild(pcfl, _cnoTmpl, kctgGgcm, cnoGgcmOld, &cnoIgnore))
            fGgcmReplaced = fFalse;
    }
    if (fSetReplaced && cnoSetOld != cnoNil)
    {
        CNO cnoIgnore = cnoNil;
        if (F4DMMActorStudioReplaceTmplChild(pcfl, _cnoTmpl, kctgGlbs, cnoSetOld, &cnoIgnore))
            fSetReplaced = fFalse;
    }
    if (fParReplaced && cnoParOld != cnoNil)
    {
        CNO cnoIgnore = cnoNil;
        if (F4DMMActorStudioReplaceTmplChild(pcfl, _cnoTmpl, kctgGlpi, cnoParOld, &cnoIgnore))
            fParReplaced = fFalse;
    }
    if (prgwrite != pvNil)
    {
        for (int32_t iactn = _cactn - 1; iactn >= 0; --iactn)
        {
            if (!prgwrite[iactn].fReplaced)
                continue;
            CNO cnoIgnoreCel = cnoNil;
            CNO cnoIgnoreXf = cnoNil;
            if (F4DMMActorStudioReplacePoseChildren(pcfl, prgwrite[iactn].cnoAction,
                                                      prgwrite[iactn].cnoCelOld,
                                                      prgwrite[iactn].cnoXfOld,
                                                      &cnoIgnoreCel, &cnoIgnoreXf))
                prgwrite[iactn].fReplaced = fFalse;
        }
    }
    if (pactr->Ptmpl() != pvNil)
        pactr->Ptmpl()->FRefreshPartShape();

LEnd:
    if (!fRet && pcfl != pvNil)
    {
        if (!fParReplaced && cnoParNew != cnoNil && pcfl->FFind(kctgGlpi, cnoParNew))
            pcfl->Delete(kctgGlpi, cnoParNew);
        if (!fSetReplaced && cnoSetNew != cnoNil && pcfl->FFind(kctgGlbs, cnoSetNew))
            pcfl->Delete(kctgGlbs, cnoSetNew);
        if (!fGgcmReplaced && cnoGgcmNew != cnoNil && pcfl->FFind(kctgGgcm, cnoGgcmNew))
            pcfl->Delete(kctgGgcm, cnoGgcmNew);
        if (prgwrite != pvNil)
        {
            for (int32_t iactn = 0; iactn < _cactn; ++iactn)
            {
                if (prgwrite[iactn].fReplaced)
                    continue;
                if (prgwrite[iactn].cnoCelNew != cnoNil && pcfl->FFind(kctgGgcl, prgwrite[iactn].cnoCelNew))
                    pcfl->Delete(kctgGgcl, prgwrite[iactn].cnoCelNew);
                if (prgwrite[iactn].cnoXfNew != cnoNil && pcfl->FFind(kctgGlxf, prgwrite[iactn].cnoXfNew))
                    pcfl->Delete(kctgGlxf, prgwrite[iactn].cnoXfNew);
            }
        }
    }
    FreePpv((void **)&prgwrite);
    return fRet;
}

bool ASDE::FUndo(PDOCB pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);
    if (_pmvie == pvNil || _cnoTmpl == cnoNil || _ipartFirst < 0 || _cpartDelete <= 0)
        return fFalse;

    PACTR pactr = pvNil;
    if (_arid == aridNil)
    {
#if defined(KAUAI_WIN32)
        pactr = Pactr4DMMActorStudioUndoTarget(_pmvie, _cnoTmpl);
#endif
    }
    else
    {
        if (!_pmvie->FSwitchScen(_iscen) || _pmvie->Pscen() == pvNil ||
            !_pmvie->Pscen()->FGotoFrm(_nfrm))
            return fFalse;
        pactr = _pmvie->Pscen()->PactrFromArid(_arid);
    }
    if (pactr == pvNil)
        return fFalse;

    bool fResult = fFalse;
    int32_t ipartSelect = _ipartFirst;
    if (_fDeletedPresent)
    {
        fResult = FRestore(pactr);
        if (fResult)
            _fDeletedPresent = fFalse;
    }
    else
    {
        fResult = _pmvie->FActorStudioDeletePartRangeCore(
            pactr, _ipartFirst, _cpartDelete, fFalse);
        if (fResult)
        {
            _fDeletedPresent = fTrue;
            ipartSelect = LwMax(0, _ipartFirst - 1);
        }
    }
    if (!fResult)
        return fFalse;

#if defined(KAUAI_WIN32)
    Refresh4DMMActorStudioAfterTopologyEdit(_pmvie, _arid, _cnoTmpl,
                                             _anid, _celn, ipartSelect);
#endif
    MVIE::MultiLog(_pmvie,
        "actor_studio_delete_undo toggle_ok tmpl=%ld first=%ld parts=%ld deleted_present=%d",
        (long)_cnoTmpl, (long)_ipartFirst, (long)_cpartDelete, (int)_fDeletedPresent);
    return fTrue;
}

/***************************************************************************
    Delete a contiguous BODY-part range from a writable handmade template.

    Model child CHIDs and material sets intentionally remain sparse/unused
    after deletion. CPS.chidModl and GLBS carry the actual references, so
    retaining those immutable authored resources is safer than renumbering an
    entire resource tree merely to make the integers pretty. GLPI, GLBS and
    every ACTN's per-frame CPS array are rebuilt to the new part count.
***************************************************************************/
bool MVIE::FActorStudioDeletePartRange(PACTR pactr, int32_t ipartFirst, int32_t cpartDelete,
                                           int32_t selectionKind, int32_t anid, int32_t celn)
{
    AssertThis(0);
    if (pactr == pvNil || ipartFirst < 0 || cpartDelete <= 0)
        return fFalse;

    PASDE pasde = ASDE::PasdeNew();
    if (pasde == pvNil)
        return fFalse;
    const int32_t anidSave = anid != ivNil ? anid : pactr->AnidCur();
    const int32_t celnSave = celn != ivNil ? celn : pactr->CelnCur();
    if (!pasde->FSave(this, pactr, anidSave, celnSave,
                      ipartFirst, cpartDelete, selectionKind))
    {
        ReleasePpo(&pasde);
        return fFalse;
    }
    if (!FActorStudioDeletePartRangeCore(pactr, ipartFirst, cpartDelete, fFalse))
    {
        ReleasePpo(&pasde);
        return fFalse;
    }
    if (!FAddUndo(pasde))
    {
        // FAddUndo has already attached this MVIE to the record. Restore the
        // topology snapshot rather than leaving a successful delete with no
        // way back merely because the undo queue could not accept the item.
        pasde->FUndo(this);
        ReleasePpo(&pasde);
        return fFalse;
    }
    ReleasePpo(&pasde);
    return fTrue;
}

bool MVIE::FActorStudioDeletePartRangeCore(PACTR pactr, int32_t ipartFirst, int32_t cpartDelete,
                                             bool fClearUndo)
{
    AssertThis(0);
    if (pactr == pvNil || pactr->Ptmpl() == pvNil || pactr->Ptmpl()->FIsTdt() ||
        ipartFirst < 0 || cpartDelete <= 0 || !FEnsureAutosave())
        return fFalse;

    TAG tagTmpl;
    pactr->GetTagTmpl(&tagTmpl);
    PCRF pcrf = tagTmpl.pcrf != pvNil ? tagTmpl.pcrf : _pcrfAutoSave;
    PCFL pcfl = pcrf != pvNil ? pcrf->Pcfl() : pvNil;
    if (pcfl == pvNil || tagTmpl.sid != ksidUseCrf)
        return fFalse;

    // Topology edits are valid for every movie-owned writable TMPL, not only
    // handmade Create-Part objects. A stock Actor/Prop promoted by Actor
    // Studio Save As has a CUSTOMOBJECT entry and owned TMPL but intentionally
    // has no CUSTOMPART provenance, so F4DMMCustomObjectIsHandmade() is false.
    // Duplicate BODY Part already works on that writable template; requiring
    // "handmade" here made its Undo remove path fail and poisoned every older
    // undo record behind it.
    int32_t idObject = 0;
    int32_t templateKind = kctkNone;
    bool fHandmadeObject = fFalse;
    for (int32_t iobj = 0; iobj < _c4DMMCustomObject; ++iobj)
    {
        if (_rg4DMMCustomObject[iobj].cnoOwnedTmpl != tagTmpl.cno)
            continue;
        idObject = _rg4DMMCustomObject[iobj].id;
        templateKind = _rg4DMMCustomObject[iobj].templateKind;
        fHandmadeObject = F4DMMCustomObjectIsHandmade(iobj);
        break;
    }
    if (idObject <= 0)
        return fFalse;

    PGL pglibactPar = pvNil;
    PGL pglibset = pvNil;
    PGG pggCmid = pvNil;
    ASACTIONPARTWRITE *prgwrite = pvNil;
    const int32_t cactn = pactr->Ptmpl()->Cactn();
    int32_t cpartOld = 0;
    int32_t cpartNew = 0;
    int32_t cbsetOld = 0;
    int32_t cbsetNew = 0;
    CNO cnoParNew = cnoNil;
    CNO cnoSetNew = cnoNil;
    CNO cnoGgcmNew = cnoNil;
    CNO cnoParOld = cnoNil;
    CNO cnoSetOld = cnoNil;
    CNO cnoGgcmOld = cnoNil;
    bool fParReplaced = fFalse;
    bool fSetReplaced = fFalse;
    bool fGgcmReplaced = fFalse;
    bool fRet = fFalse;

    MultiLog(this,
        "actor_studio_part_delete begin arid=%ld tmpl=%ld object=%ld template_kind=%ld handmade=%d first=%ld parts=%ld actions=%ld",
        (long)pactr->Arid(), (long)tagTmpl.cno, (long)idObject,
        (long)templateKind, (int)fHandmadeObject,
        (long)ipartFirst, (long)cpartDelete, (long)cactn);

    if (!F4DMMActorStudioReadTmplPartLists(pcfl, tagTmpl.cno, &pglibactPar, &pglibset))
        goto LEnd;
    {
        KID kidGgcm;
        BLCK blckGgcm;
        int16_t boGgcm = kboCur;
        if (!pcfl->FGetKidChidCtg(kctgTmpl, tagTmpl.cno, 0, kctgGgcm, &kidGgcm) ||
            !pcfl->FFind(kidGgcm.cki.ctg, kidGgcm.cki.cno, &blckGgcm))
            goto LEnd;
        pggCmid = GG::PggRead(&blckGgcm, &boGgcm);
        if (pggCmid == pvNil || pggCmid->CbFixed() != SIZEOF(int32_t))
            goto LEnd;
        if (boGgcm == kboOther)
        {
            for (int32_t ibsetSwap = 0; ibsetSwap < pggCmid->IvMac(); ++ibsetSwap)
            {
                SwapBytesRglw(pggCmid->QvFixedGet(ibsetSwap), 1);
                SwapBytesRglw(pggCmid->QvGet(ibsetSwap),
                               *(int32_t *)pggCmid->QvFixedGet(ibsetSwap));
            }
        }
    }
    cpartOld = pglibactPar->IvMac();
    cbsetOld = pggCmid->IvMac();
    if (cpartOld <= 1 || pglibset->IvMac() != cpartOld ||
        !FIn(ipartFirst, 0, cpartOld) || ipartFirst + cpartDelete > cpartOld ||
        cpartDelete >= cpartOld || cactn <= 0)
        goto LEnd;
    cpartNew = cpartOld - cpartDelete;

    // Remove topology entries first, then remap surviving parent indices.
    pglibactPar->Delete(ipartFirst, cpartDelete);
    pglibset->Delete(ipartFirst, cpartDelete);
    for (int32_t ipart = 0; ipart < cpartNew; ++ipart)
    {
        int16_t ipar = ivNil;
        pglibactPar->Get(ipart, &ipar);
        if (ipar >= ipartFirst && ipar < ipartFirst + cpartDelete)
            ipar = ivNil;
        else if (ipar >= ipartFirst + cpartDelete)
            ipar = (int16_t)(ipar - cpartDelete);
        pglibactPar->Put(ipart, &ipar);
    }

    // BODY::FRefreshPartShape requires the GGCM set table to end at the
    // highest set still referenced by GLBS. v135 Duplicate appends a dedicated
    // one-part set, so deleting that duplicate removes the highest GLBS set;
    // leaving the old trailing GGCM entry makes the template structurally
    // inconsistent and is the concrete source of the post-Duplicate delete
    // failure. Preserve sparse middle sets, but trim only unreferenced tail.
    {
        int32_t ibsetMax = -1;
        for (int32_t ipart = 0; ipart < cpartNew; ++ipart)
        {
            int16_t ibset = 0;
            pglibset->Get(ipart, &ibset);
            if (ibset < 0 || ibset >= cbsetOld)
                goto LEnd;
            ibsetMax = LwMax(ibsetMax, (int32_t)ibset);
        }
        cbsetNew = ibsetMax + 1;
        if (cbsetNew <= 0 || cbsetNew > cbsetOld)
            goto LEnd;
        while (pggCmid->IvMac() > cbsetNew)
            pggCmid->Delete(pggCmid->IvMac() - 1);
        if (pggCmid->IvMac() != cbsetNew)
            goto LEnd;
    }

    if (!FAllocPv((void **)&prgwrite, LwMul(cactn, SIZEOF(ASACTIONPARTWRITE)),
                  fmemClear, mprNormal))
        goto LEnd;

    // Rebuild each variable-length CEL instead of shrinking GG storage in
    // place. This is the same lesson that made v135 Duplicate reliable.
    for (int32_t anid = 0; anid < cactn; ++anid)
    {
        KID kidAction;
        PGG pggcel = pvNil;
        PGG pggcelNew = pvNil;
        PGL pglbmat34 = pvNil;
        if (!pcfl->FGetKidChidCtg(kctgTmpl, tagTmpl.cno, anid, kctgActn, &kidAction) ||
            !F4DMMActorStudioReadActionGroups(pcfl, kidAction.cki.cno, &pggcel, &pglbmat34) ||
            pggcel->IvMac() <= 0)
        {
            ReleasePpo(&pggcel);
            ReleasePpo(&pglbmat34);
            goto LEnd;
        }
        pggcelNew = GG::PggNew(SIZEOF(CEL));
        if (pggcelNew == pvNil)
        {
            ReleasePpo(&pggcel);
            ReleasePpo(&pglbmat34);
            goto LEnd;
        }
        prgwrite[anid].cnoAction = kidAction.cki.cno;
        for (int32_t icel = 0; icel < pggcel->IvMac(); ++icel)
        {
            const int32_t cpartCel = pggcel->Cb(icel) / SIZEOF(CPS);
            if (cpartCel != cpartOld)
            {
                ReleasePpo(&pggcelNew);
                ReleasePpo(&pggcel);
                ReleasePpo(&pglbmat34);
                goto LEnd;
            }
            CPS rgcps[kc4DMMActorStudioFramePartMax];
            CPS *prgcpsOld = (CPS *)pggcel->QvGet(icel);
            int32_t ipartDst = 0;
            for (int32_t ipartOld = 0; ipartOld < cpartOld; ++ipartOld)
            {
                if (FIn(ipartOld, ipartFirst, ipartFirst + cpartDelete))
                    continue;
                rgcps[ipartDst++] = prgcpsOld[ipartOld];
            }
            if (ipartDst != cpartNew)
            {
                ReleasePpo(&pggcelNew);
                ReleasePpo(&pggcel);
                ReleasePpo(&pglbmat34);
                goto LEnd;
            }
            CEL cel = *(CEL *)pggcel->QvFixedGet(icel);
            if (!pggcelNew->FAdd(LwMul(cpartNew, SIZEOF(CPS)), pvNil, rgcps, &cel))
            {
                ReleasePpo(&pggcelNew);
                ReleasePpo(&pggcel);
                ReleasePpo(&pglbmat34);
                goto LEnd;
            }
        }
        BLCK blck;
        if (!pcfl->FAdd(pglbmat34->CbOnFile(), kctgGlxf, &prgwrite[anid].cnoXfNew, &blck) ||
            !pglbmat34->FWrite(&blck) ||
            !pcfl->FAdd(pggcelNew->CbOnFile(), kctgGgcl, &prgwrite[anid].cnoCelNew, &blck) ||
            !pggcelNew->FWrite(&blck))
        {
            ReleasePpo(&pggcelNew);
            ReleasePpo(&pggcel);
            ReleasePpo(&pglbmat34);
            goto LEnd;
        }
        ReleasePpo(&pggcelNew);
        ReleasePpo(&pggcel);
        ReleasePpo(&pglbmat34);
    }

    if (!F4DMMWriteGlChild(pcfl, kctgGlpi, pglibactPar, &cnoParNew) ||
        !F4DMMWriteGlChild(pcfl, kctgGlbs, pglibset, &cnoSetNew) ||
        !F4DMMWriteGgChild(pcfl, kctgGgcm, pggCmid, &cnoGgcmNew))
        goto LEnd;

    for (int32_t anid = 0; anid < cactn; ++anid)
    {
        if (!F4DMMActorStudioReplacePoseChildren(pcfl, prgwrite[anid].cnoAction,
                                                  prgwrite[anid].cnoCelNew,
                                                  prgwrite[anid].cnoXfNew,
                                                  &prgwrite[anid].cnoCelOld,
                                                  &prgwrite[anid].cnoXfOld))
            goto LRollback;
        prgwrite[anid].fReplaced = fTrue;
    }
    if (!F4DMMActorStudioReplaceTmplChild(pcfl, tagTmpl.cno, kctgGlpi,
                                          cnoParNew, &cnoParOld))
        goto LRollback;
    fParReplaced = fTrue;
    if (!F4DMMActorStudioReplaceTmplChild(pcfl, tagTmpl.cno, kctgGlbs,
                                          cnoSetNew, &cnoSetOld))
        goto LRollback;
    fSetReplaced = fTrue;
    if (!F4DMMActorStudioReplaceTmplChild(pcfl, tagTmpl.cno, kctgGgcm,
                                          cnoGgcmNew, &cnoGgcmOld))
        goto LRollback;
    fGgcmReplaced = fTrue;

    for (int32_t anid = 0; anid < cactn; ++anid)
        pcrf->FSetCrep(crepToss, kctgActn, prgwrite[anid].cnoAction, ACTN::FReadActn);

    if (!pactr->Ptmpl()->FRefreshPartShape())
        goto LRollback;
    if (Pscen() != pvNil && Pscen()->PglRollCall() != pvNil)
    {
        PGL pglpactr = Pscen()->PglRollCall();
        for (int32_t iactr = 0; iactr < pglpactr->IvMac(); ++iactr)
        {
            PACTR pactrLive = pvNil;
            pglpactr->Get(iactr, &pactrLive);
            if (pactrLive == pvNil)
                continue;
            TAG tagLive;
            pactrLive->GetTagTmpl(&tagLive);
            if (!F4DMMActorStudioSameTemplateIdentity(&tagLive, &tagTmpl))
                continue;
            PTMPL ptmplActor = pactrLive->Ptmpl();
            if (ptmplActor == pvNil)
                goto LRollback;
            if (ptmplActor != pactr->Ptmpl() && !ptmplActor->FRefreshPartShape())
                goto LRollback;
            if (!pactrLive->FRefreshBodyForTemplateMutation())
                goto LRollback;
        }
    }

    // Adjust only this handmade object's provenance. Records outside the
    // deleted range shift left; overlapping Object records shrink or vanish.
    {
        // Compact in place. CUSTOMPART now carries provenance strings, so a
        // kc4DMMCustomPartMax temporary array would waste hundreds of KB of
        // the 32-bit process stack during an already topology-heavy edit.
        int32_t cmetaNew = 0;
        const int32_t cmetaOld = _c4DMMCustomPart;
        const int32_t ipartDeleteLim = ipartFirst + cpartDelete;
        for (int32_t imeta = 0; imeta < cmetaOld; ++imeta)
        {
            CUSTOMPART meta = _rg4DMMCustomPart[imeta];
            bool fKeep = fTrue;
            if (meta.idObject == idObject && meta.idImport > 0 && meta.cpart > 0)
            {
                const int32_t imetaFirst = meta.ipartFirst;
                const int32_t imetaLim = meta.ipartFirst + meta.cpart;
                if (imetaFirst >= ipartDeleteLim)
                    meta.ipartFirst -= cpartDelete;
                else if (imetaLim > ipartFirst)
                {
                    const int32_t cOverlap = LwMax(0L,
                        LwMin(imetaLim, ipartDeleteLim) - LwMax(imetaFirst, ipartFirst));
                    if (cOverlap >= meta.cpart)
                        fKeep = fFalse;
                    else
                    {
                        meta.cpart -= cOverlap;
                        if (imetaFirst >= ipartFirst)
                            meta.ipartFirst = ipartFirst;
                    }
                }
            }
            if (fKeep)
                _rg4DMMCustomPart[cmetaNew++] = meta;
        }
        if (cmetaNew < cmetaOld)
            ClearPb(&_rg4DMMCustomPart[cmetaNew],
                    LwMul(cmetaOld - cmetaNew, SIZEOF(CUSTOMPART)));
        _c4DMMCustomPart = cmetaNew;
    }

    // Ordinary structural edits invalidate pose-undo records whose part indices
    // refer to the old topology. Duplicate Undo/Redo is itself the topology
    // transaction, so its internal remove must preserve the active undo record.
    if (fClearUndo)
        ClearUndo();
    _fRequiresVmmSave = fTrue;
    _fAutosaveDirty = fTrue;
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    MultiLog(this,
        "actor_studio_part_delete commit tmpl=%ld object=%ld first=%ld removed=%ld old_parts=%ld new_parts=%ld old_sets=%ld new_sets=%ld clear_undo=%d",
        (long)tagTmpl.cno, (long)idObject, (long)ipartFirst, (long)cpartDelete,
        (long)cpartOld, (long)cpartNew, (long)cbsetOld, (long)cbsetNew, (int)fClearUndo);
    fRet = fTrue;
    goto LEnd;

LRollback:
    if (fGgcmReplaced && cnoGgcmOld != cnoNil)
    {
        CNO cnoIgnore = cnoNil;
        F4DMMActorStudioReplaceTmplChild(pcfl, tagTmpl.cno, kctgGgcm, cnoGgcmOld, &cnoIgnore);
        fGgcmReplaced = fFalse;
    }
    if (fSetReplaced && cnoSetOld != cnoNil)
    {
        CNO cnoIgnore = cnoNil;
        F4DMMActorStudioReplaceTmplChild(pcfl, tagTmpl.cno, kctgGlbs, cnoSetOld, &cnoIgnore);
        fSetReplaced = fFalse;
    }
    if (fParReplaced && cnoParOld != cnoNil)
    {
        CNO cnoIgnore = cnoNil;
        F4DMMActorStudioReplaceTmplChild(pcfl, tagTmpl.cno, kctgGlpi, cnoParOld, &cnoIgnore);
        fParReplaced = fFalse;
    }
    if (prgwrite != pvNil)
    {
        for (int32_t anid = cactn - 1; anid >= 0; --anid)
        {
            if (!prgwrite[anid].fReplaced)
                continue;
            CNO cnoIgnoreCel = cnoNil;
            CNO cnoIgnoreXf = cnoNil;
            F4DMMActorStudioReplacePoseChildren(pcfl, prgwrite[anid].cnoAction,
                                                  prgwrite[anid].cnoCelOld,
                                                  prgwrite[anid].cnoXfOld,
                                                  &cnoIgnoreCel, &cnoIgnoreXf);
            prgwrite[anid].fReplaced = fFalse;
        }
    }
    for (int32_t anid = 0; anid < cactn; ++anid)
        if (prgwrite != pvNil && prgwrite[anid].cnoAction != cnoNil)
            pcrf->FSetCrep(crepToss, kctgActn, prgwrite[anid].cnoAction, ACTN::FReadActn);
    if (pactr->Ptmpl() != pvNil)
        pactr->Ptmpl()->FRefreshPartShape();
    if (Pscen() != pvNil && Pscen()->PglRollCall() != pvNil)
    {
        PGL pglpactrRollback = Pscen()->PglRollCall();
        for (int32_t iactr = 0; iactr < pglpactrRollback->IvMac(); ++iactr)
        {
            PACTR pactrLive = pvNil;
            pglpactrRollback->Get(iactr, &pactrLive);
            if (pactrLive == pvNil)
                continue;
            TAG tagLive;
            pactrLive->GetTagTmpl(&tagLive);
            if (!F4DMMActorStudioSameTemplateIdentity(&tagLive, &tagTmpl))
                continue;
            PTMPL ptmplActor = pactrLive->Ptmpl();
            if (ptmplActor != pvNil && ptmplActor != pactr->Ptmpl())
                ptmplActor->FRefreshPartShape();
            pactrLive->FRefreshBodyForTemplateMutation();
        }
    }
    MultiLog(this,
        "actor_studio_part_delete rollback tmpl=%ld first=%ld parts=%ld",
        (long)tagTmpl.cno, (long)ipartFirst, (long)cpartDelete);

LEnd:
    if (!fRet && pcfl != pvNil)
    {
        if (!fParReplaced && cnoParNew != cnoNil && pcfl->FFind(kctgGlpi, cnoParNew))
            pcfl->Delete(kctgGlpi, cnoParNew);
        if (!fSetReplaced && cnoSetNew != cnoNil && pcfl->FFind(kctgGlbs, cnoSetNew))
            pcfl->Delete(kctgGlbs, cnoSetNew);
        if (!fGgcmReplaced && cnoGgcmNew != cnoNil && pcfl->FFind(kctgGgcm, cnoGgcmNew))
            pcfl->Delete(kctgGgcm, cnoGgcmNew);
        if (prgwrite != pvNil)
        {
            for (int32_t anid = 0; anid < cactn; ++anid)
            {
                if (!prgwrite[anid].fReplaced)
                {
                    if (prgwrite[anid].cnoCelNew != cnoNil && pcfl->FFind(kctgGgcl, prgwrite[anid].cnoCelNew))
                        pcfl->Delete(kctgGgcl, prgwrite[anid].cnoCelNew);
                    if (prgwrite[anid].cnoXfNew != cnoNil && pcfl->FFind(kctgGlxf, prgwrite[anid].cnoXfNew))
                        pcfl->Delete(kctgGlxf, prgwrite[anid].cnoXfNew);
                }
            }
        }
    }
    FreePpv((void **)&prgwrite);
    ReleasePpo(&pggCmid);
    ReleasePpo(&pglibactPar);
    ReleasePpo(&pglibset);
    return fRet;
}

bool MVIE::FActorStudioSetPartModelChidsCore(PACTR pactr, int32_t anid,
                                               int32_t celnFirst, int32_t ipart,
                                               const int16_t *prgchidModl,
                                               int32_t cchid)
{
    return FActorStudioSetPartsModelChidsCore(
        pactr, anid, celnFirst, &ipart, 1, prgchidModl, cchid);
}

bool MVIE::FActorStudioSetPartsModelChidsCore(PACTR pactr, int32_t anid,
                                                int32_t celnFirst,
                                                const int32_t *prgipart,
                                                int32_t cpartPresence,
                                                const int16_t *prgchidModl,
                                                int32_t cchid)
{
    AssertThis(0);
    AssertNilOrPo(pactr, 0);
    if (pactr == pvNil || pactr->Ptmpl() == pvNil || pactr->Ptmpl()->FIsTdt() ||
        prgipart == pvNil || cpartPresence <= 0 ||
        cpartPresence > kc4DMMActorStudioFramePartMax ||
        prgchidModl == pvNil || cchid <= 0 ||
        !FActorStudioActionIsCustom(pactr, anid) || !FEnsureAutosave())
        return fFalse;

    for (int32_t j = 0; j < cpartPresence; ++j)
    {
        if (prgipart[j] < 0)
            return fFalse;
        for (int32_t k = 0; k < j; ++k)
            if (prgipart[k] == prgipart[j])
                return fFalse;
    }

    PCFL pcfl = pvNil;
    PCRF pcrf = pvNil;
    CNO cnoTmpl = cnoNil;
    CNO cnoAction = cnoNil;
    PGG pggcel = pvNil;
    PGL pglbmat34 = pvNil;
    CNO cnoCelNew = cnoNil;
    bool fOldDetached = fFalse;
    bool fRet = fFalse;
    KID kidCelOld;
    BLCK blck;

    if (!F4DMMActorStudioResolveActionFile(this, _pcrfAutoSave, pactr, anid,
                                            &pcfl, &cnoTmpl, &cnoAction, &pcrf) ||
        pcfl == pvNil || pcrf == pvNil || cnoAction == cnoNil)
        goto LEnd;

    // Custom-action metadata alone is insufficient. Match the pose writers and
    // reject the historical aliased-ACTN state where two action CHIDs still
    // point at the same action chunk.
    {
        CNO cnoIndependent = cnoNil;
        int32_t anidAlias = ivNil;
        if (!F4DMMActorStudioActionChildIndependent(
                pcfl, cnoTmpl, anid, pactr->Ptmpl()->Cactn(),
                &cnoIndependent, &anidAlias) || cnoIndependent != cnoAction)
        {
            MultiLog(this,
                "actor_studio_part_presence reject_non_independent tmpl=%ld action=%ld alias=%ld",
                (long)cnoTmpl, (long)anid, (long)anidAlias);
            goto LEnd;
        }
    }

    if (!F4DMMActorStudioReadActionGroups(pcfl, cnoAction, &pggcel, &pglbmat34) ||
        pggcel == pvNil || pggcel->IvMac() <= 0)
        goto LEnd;
    if (celnFirst < 0 || celnFirst >= pggcel->IvMac() ||
        cchid != pggcel->IvMac() - celnFirst)
        goto LEnd;

    for (int32_t i = 0; i < cchid; ++i)
    {
        const int32_t icel = celnFirst + i;
        const int32_t cpartCel = pggcel->Cb(icel) / SIZEOF(CPS);
        CPS *prgcps = (CPS *)pggcel->QvGet(icel);
        for (int32_t j = 0; j < cpartPresence; ++j)
        {
            const int32_t ipart = prgipart[j];
            if (!FIn(ipart, 0, cpartCel))
                goto LEnd;
            prgcps[ipart].chidModl = prgchidModl[LwMul(i, cpartPresence) + j];
        }
    }

    if (!pcfl->FGetKidChidCtg(kctgActn, cnoAction, 0, kctgGgcl, &kidCelOld) ||
        !pcfl->FAdd(pggcel->CbOnFile(), kctgGgcl, &cnoCelNew, &blck) ||
        !pggcel->FWrite(&blck))
        goto LEnd;

    if (C4DMMActorStudioRemoveActionChildLinks(pcfl, cnoAction, kctgGgcl) <= 0)
        goto LEnd;
    fOldDetached = fTrue;
    if (!pcfl->FAdoptChild(kctgActn, cnoAction, kctgGgcl, cnoCelNew, 0))
        goto LEnd;

    {
        KID kidVerify;
        if (!pcfl->FGetKidChidCtg(kctgActn, cnoAction, 0, kctgGgcl, &kidVerify) ||
            kidVerify.cki.cno != cnoCelNew)
            goto LEnd;
    }
    fOldDetached = fFalse;

    pcrf->FSetCrep(crepToss, kctgActn, cnoAction, ACTN::FReadActn);
    _fRequiresVmmSave = fTrue;
    _fAutosaveDirty = fTrue;
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    MultiLog(this,
        "actor_studio_part_presence_write tmpl=%ld action=%ld cel_first=%ld anchor_part=%ld presence_parts=%ld cels=%ld ggcl_old=%ld ggcl_new=%ld",
        (long)cnoTmpl, (long)anid, (long)celnFirst, (long)prgipart[0],
        (long)cpartPresence, (long)cchid,
        (long)kidCelOld.cki.cno, (long)cnoCelNew);
    fRet = fTrue;

LEnd:
    if (!fRet)
    {
        if (fOldDetached && pcfl != pvNil && cnoAction != cnoNil)
        {
            C4DMMActorStudioRemoveActionChildLinks(pcfl, cnoAction, kctgGgcl);
            pcfl->FAdoptChild(kctgActn, cnoAction, kctgGgcl, kidCelOld.cki.cno, 0);
            fOldDetached = fFalse;
        }
        if (pcfl != pvNil && cnoCelNew != cnoNil && pcfl->FFind(kctgGgcl, cnoCelNew))
            pcfl->Delete(kctgGgcl, cnoCelNew);
    }
    ReleasePpo(&pggcel);
    ReleasePpo(&pglbmat34);
    return fRet;
}

bool MVIE::FActorStudioChangePartPresence(PACTR pactr, int32_t anid, int32_t celnFirst,
                                           int32_t ipart, bool fPresent,
                                           int32_t selectionKind,
                                           int32_t ipartSelectionFirst,
                                           int32_t cpartSelection)
{
    return FActorStudioChangePartsPresence(
        pactr, anid, celnFirst, &ipart, 1, fPresent,
        selectionKind, ipartSelectionFirst, cpartSelection);
}

bool MVIE::FActorStudioChangePartsPresence(PACTR pactr, int32_t anid, int32_t celnFirst,
                                            const int32_t *prgipart,
                                            int32_t cpartPresence, bool fPresent,
                                            int32_t selectionKind,
                                            int32_t ipartSelectionFirst,
                                            int32_t cpartSelection)
{
    AssertThis(0);
    const bool fCustom = pactr != pvNil && pactr->Ptmpl() != pvNil &&
        FActorStudioActionIsCustom(pactr, anid);
    if (pactr == pvNil || pactr->Ptmpl() == pvNil || !fCustom ||
        prgipart == pvNil || cpartPresence <= 0 ||
        cpartPresence > kc4DMMActorStudioFramePartMax)
    {
        MultiLog(this,
            "actor_studio_part_presence reject stage=precondition pactr=%p tmpl=%p action=%ld cel=%ld presence_parts=%ld mode=%s custom=%d",
            pactr, pactr != pvNil ? pactr->Ptmpl() : pvNil, (long)anid,
            (long)celnFirst, (long)cpartPresence, fPresent ? "spawn" : "cut",
            (int)fCustom);
        return fFalse;
    }

    for (int32_t j = 0; j < cpartPresence; ++j)
    {
        if (prgipart[j] < 0)
            return fFalse;
        for (int32_t k = 0; k < j; ++k)
            if (prgipart[k] == prgipart[j])
                return fFalse;
    }

    PCSZ pszFailStage = PszLit("snapshot");
    PCFL pcfl = pvNil;
    PCRF pcrf = pvNil;
    CNO cnoTmpl = cnoNil;
    CNO cnoAction = cnoNil;
    PGG pggcel = pvNil;
    PGL pglbmat34 = pvNil;
    int16_t *prgchidBefore = pvNil;
    int16_t *prgchidAfter = pvNil;
    PASCU pascu = pvNil;
    bool fRet = fFalse;
    int32_t cchid = 0;
    int32_t cChanged = 0;
    int32_t cvalue = 0;

    ACTORSTUDIOFRAMESNAPSHOT snapshot;
    if (!FActorStudioGetFrameSnapshot(pactr, anid, celnFirst, &snapshot) ||
        !snapshot.fValid)
        goto LEnd;
    for (int32_t j = 0; j < cpartPresence; ++j)
        if (!FIn(prgipart[j], 0, snapshot.cpart))
            goto LEnd;

    pszFailStage = PszLit("resolve_action");
    if (!F4DMMActorStudioResolveActionFile(this, _pcrfAutoSave, pactr, anid,
                                            &pcfl, &cnoTmpl, &cnoAction, &pcrf) ||
        pcfl == pvNil || pcrf == pvNil)
        goto LEnd;
    pszFailStage = PszLit("read_action_groups");
    if (!F4DMMActorStudioReadActionGroups(pcfl, cnoAction, &pggcel, &pglbmat34) ||
        pggcel == pvNil || celnFirst < 0 || celnFirst >= pggcel->IvMac())
        goto LEnd;

    cchid = pggcel->IvMac() - celnFirst;
    cvalue = LwMul(cchid, cpartPresence);
    pszFailStage = PszLit("alloc_presence_arrays");
    if (cchid <= 0 || cvalue <= 0 ||
        !FAllocPv((void **)&prgchidBefore, LwMul(cvalue, SIZEOF(int16_t)), fmemNil, mprNormal) ||
        !FAllocPv((void **)&prgchidAfter, LwMul(cvalue, SIZEOF(int16_t)), fmemNil, mprNormal))
        goto LEnd;

    for (int32_t i = 0; i < cchid; ++i)
    {
        const int32_t icel = celnFirst + i;
        const int32_t cpartCel = pggcel->Cb(icel) / SIZEOF(CPS);
        const CPS *prgcps = (const CPS *)pggcel->QvGet(icel);
        for (int32_t j = 0; j < cpartPresence; ++j)
        {
            const int32_t ipart = prgipart[j];
            if (!FIn(ipart, 0, cpartCel))
            {
                pszFailStage = PszLit("part_out_of_range");
                goto LEnd;
            }
            const int32_t ivalue = LwMul(i, cpartPresence) + j;
            const int16_t chidBefore = prgcps[ipart].chidModl;
            int16_t chidAfter = chidBefore;

            if (chidNil != chidBefore)
            {
                if (fPresent)
                {
                    if (F4DMMActorStudioCpsModelHidden(chidBefore))
                        chidAfter = Chid4DMMActorStudioShowModel(chidBefore);
                }
                else if (!F4DMMActorStudioCpsModelHidden(chidBefore))
                {
                    if ((((uint16_t)chidBefore) & ~kgrf4DMMActorStudioHiddenModel) == 0x7fff)
                    {
                        pszFailStage = PszLit("hidden_chid_overflow");
                        goto LEnd;
                    }
                    chidAfter = Chid4DMMActorStudioHideModel(chidBefore);
                }
            }

            prgchidBefore[ivalue] = chidBefore;
            prgchidAfter[ivalue] = chidAfter;
            if (chidAfter != chidBefore)
                ++cChanged;
        }
    }

    if (cChanged <= 0)
    {
        pszFailStage = PszLit("no_change");
        MultiLog(this,
            "actor_studio_part_presence no_change action=%ld cel_first=%ld anchor_part=%ld presence_parts=%ld mode=%s",
            (long)anid, (long)celnFirst, (long)prgipart[0],
            (long)cpartPresence, fPresent ? "spawn" : "cut");
        goto LEnd;
    }

    pszFailStage = PszLit("undo_state");
    pascu = ASCU::PascuNew();
    if (pascu == pvNil ||
        !pascu->FSetState(pactr->Arid(), anid, celnFirst,
                          prgipart, cpartPresence, cnoTmpl,
                          prgchidBefore, prgchidAfter, cchid, !fPresent,
                          selectionKind, ipartSelectionFirst, cpartSelection))
        goto LEnd;

    pszFailStage = PszLit("write_presence");
    if (!FActorStudioSetPartsModelChidsCore(
            pactr, anid, celnFirst, prgipart, cpartPresence,
            prgchidAfter, cchid))
        goto LEnd;

    pszFailStage = PszLit("add_undo");
    if (!FAddUndo(pascu))
    {
        const bool fRestored = FActorStudioSetPartsModelChidsCore(
            pactr, anid, celnFirst, prgipart, cpartPresence,
            prgchidBefore, cchid);
        MultiLog(this,
            "actor_studio_part_presence undo_add_failed action=%ld cel_first=%ld anchor_part=%ld presence_parts=%ld mode=%s restored=%d",
            (long)anid, (long)celnFirst, (long)prgipart[0],
            (long)cpartPresence, fPresent ? "spawn" : "cut", (int)fRestored);
        ClearUndo();
        goto LEnd;
    }

    MultiLog(this,
        "actor_studio_part_presence commit action=%ld cel_first=%ld anchor_part=%ld presence_parts=%ld cels=%ld changed=%ld mode=%s",
        (long)anid, (long)celnFirst, (long)prgipart[0], (long)cpartPresence,
        (long)cchid, (long)cChanged, fPresent ? "spawn" : "cut");
    fRet = fTrue;

LEnd:
    if (!fRet)
    {
        MultiLog(this,
            "actor_studio_part_presence reject stage=%s action=%ld cel_first=%ld anchor_part=%ld presence_parts=%ld cels=%ld changed=%ld mode=%s tmpl=%ld actn_cno=%ld",
            pszFailStage != pvNil ? pszFailStage : "unknown",
            (long)anid, (long)celnFirst,
            prgipart != pvNil && cpartPresence > 0 ? (long)prgipart[0] : -1L,
            (long)cpartPresence, (long)cchid, (long)cChanged,
            fPresent ? "spawn" : "cut", (long)cnoTmpl, (long)cnoAction);
    }
    ReleasePpo(&pascu);
    ReleasePpo(&pggcel);
    ReleasePpo(&pglbmat34);
    FreePpv((void **)&prgchidBefore);
    FreePpv((void **)&prgchidAfter);
    return fRet;
}

bool MVIE::FActorStudioCutPart(PACTR pactr, int32_t anid, int32_t celn, int32_t ipart,
                                int32_t selectionKind, int32_t ipartSelectionFirst,
                                int32_t cpartSelection)
{
    return FActorStudioChangePartPresence(
        pactr, anid, celn, ipart, fFalse,
        selectionKind, ipartSelectionFirst, cpartSelection);
}

bool MVIE::FActorStudioSpawnPart(PACTR pactr, int32_t anid, int32_t celn, int32_t ipart,
                                  int32_t selectionKind, int32_t ipartSelectionFirst,
                                  int32_t cpartSelection)
{
    return FActorStudioChangePartPresence(
        pactr, anid, celn, ipart, fTrue,
        selectionKind, ipartSelectionFirst, cpartSelection);
}

bool MVIE::FActorStudioCutParts(PACTR pactr, int32_t anid, int32_t celn,
                                 const int32_t *prgipart, int32_t cpartPresence,
                                 int32_t selectionKind, int32_t ipartSelectionFirst,
                                 int32_t cpartSelection)
{
    return FActorStudioChangePartsPresence(
        pactr, anid, celn, prgipart, cpartPresence, fFalse,
        selectionKind, ipartSelectionFirst, cpartSelection);
}

bool MVIE::FActorStudioSpawnParts(PACTR pactr, int32_t anid, int32_t celn,
                                   const int32_t *prgipart, int32_t cpartPresence,
                                   int32_t selectionKind, int32_t ipartSelectionFirst,
                                   int32_t cpartSelection)
{
    return FActorStudioChangePartsPresence(
        pactr, anid, celn, prgipart, cpartPresence, fTrue,
        selectionKind, ipartSelectionFirst, cpartSelection);
}

bool MVIE::FActorStudioSetPartMatrix(PACTR pactr, int32_t anid, int32_t celn, int32_t ipart,
                                      const BMAT34 *pbmat34, int32_t selectionKind,
                                      int32_t ipartSelectionFirst, int32_t cpartSelection)
{
    return FActorStudioSetPartMatrixCore(pactr, anid, celn, ipart, pbmat34, fTrue,
                                          selectionKind, ipartSelectionFirst, cpartSelection);
}

bool MVIE::FActorStudioSetPartMatrixCore(PACTR pactr, int32_t anid, int32_t celn, int32_t ipart,
                                          const BMAT34 *pbmat34, bool fAddUndo,
                                          int32_t selectionKind, int32_t ipartSelectionFirst,
                                          int32_t cpartSelection)
{
    AssertThis(0);
    AssertNilOrPo(pactr, 0);
    AssertVarMem(pbmat34);
    if (pactr == pvNil || pactr->Ptmpl() == pvNil || pactr->Ptmpl()->FIsTdt() ||
        ipart < 0 || !FActorStudioActionIsCustom(pactr, anid) || !FEnsureAutosave())
        return fFalse;

    TAG tagTmpl;
    pactr->GetTagTmpl(&tagTmpl);
    PCRF pcrfEdit = tagTmpl.pcrf != pvNil ? tagTmpl.pcrf : _pcrfAutoSave;
    PCFL pcfl = pcrfEdit != pvNil ? pcrfEdit->Pcfl() : pvNil;
    if (pcfl == pvNil || tagTmpl.sid != ksidUseCrf)
        return fFalse;

    CNO cnoAction = cnoNil;
    int32_t anidAlias = ivNil;
    if (!F4DMMActorStudioActionChildIndependent(pcfl, tagTmpl.cno, anid,
                                                 pactr->Ptmpl()->Cactn(), &cnoAction, &anidAlias))
    {
        MultiLog(this,
            "actor_studio_part_edit reject_non_independent tmpl=%ld action=%ld alias_action=%ld undo_record=%d",
            (long)tagTmpl.cno, (long)anid, (long)anidAlias, (int)fAddUndo);
        return fFalse;
    }

    KID kidCel;
    KID kidXf;
    BLCK blck;
    int16_t bo = kboCur;
    PGG pggcel = pvNil;
    PGL pglbmat34 = pvNil;
    bool fRet = fFalse;
    bool fChildrenReplaced = fFalse;
    CNO cnoCelNew = cnoNil;
    CNO cnoXfNew = cnoNil;
    CNO cnoCelOldCanonical = cnoNil;
    CNO cnoXfOldCanonical = cnoNil;
    int32_t cCelLinksOld = 0;
    int32_t cXfLinksOld = 0;
    PASUN pasun = pvNil;
    int32_t icel = 0;
    int32_t cpartCel = 0;
    CPS *prgcps = pvNil;
    int32_t imatOld = ivNil;
    int32_t imatNew = ivNil;
    int32_t crefMatrix = 0;
    BMAT34 bmat34Before;
    BMAT34 bmat34Write;

    if (!pcfl->FGetKidChidCtg(kctgActn, cnoAction, 0, kctgGgcl, &kidCel) ||
        !pcfl->FGetKidChidCtg(kctgActn, cnoAction, 0, kctgGlxf, &kidXf))
        goto LEnd;

    if (!pcfl->FFind(kidCel.cki.ctg, kidCel.cki.cno, &blck))
        goto LEnd;
    pggcel = GG::PggRead(&blck, &bo);
    if (pggcel == pvNil)
        goto LEnd;
    if (kboOther == bo)
    {
        AssertBomRglw(kbomCel, SIZEOF(CEL));
        AssertBomRgsw(kbomCps, SIZEOF(CPS));
        for (int32_t icelSwap = 0; icelSwap < pggcel->IvMac(); ++icelSwap)
        {
            SwapBytesRglw(pggcel->QvFixedGet(icelSwap), SIZEOF(CEL) / SIZEOF(int32_t));
            SwapBytesRgsw(pggcel->QvGet(icelSwap), pggcel->Cb(icelSwap) / SIZEOF(int16_t));
        }
    }

    if (!pcfl->FFind(kidXf.cki.ctg, kidXf.cki.cno, &blck))
        goto LEnd;
    pglbmat34 = GL::PglRead(&blck, &bo);
    if (pglbmat34 == pvNil)
        goto LEnd;
    if (kboOther == bo)
    {
        AssertBomRglw(kbomBmat34, SIZEOF(BMAT34));
        if (pglbmat34->IvMac() > 0)
            SwapBytesRglw(pglbmat34->QvGet(0),
                          LwMul(pglbmat34->IvMac(), SIZEOF(BMAT34) / SIZEOF(int32_t)));
    }

    if (pggcel->IvMac() <= 0)
        goto LEnd;
    icel = celn % pggcel->IvMac();
    if (icel < 0)
        icel += pggcel->IvMac();
    cpartCel = pggcel->Cb(icel) / SIZEOF(CPS);
    if (ipart >= cpartCel)
        goto LEnd;

    prgcps = (CPS *)pggcel->QvGet(icel);
    imatOld = prgcps[ipart].imat34;
    if (imatOld < 0 || imatOld >= pglbmat34->IvMac())
        goto LEnd;

    bmat34Before = *(BMAT34 *)pglbmat34->QvGet(imatOld);
    bmat34Write = *pbmat34;

    // Action files aggressively reuse identical matrices. Editing one matrix
    // in place can therefore bend several BODY parts or several frames at
    // once. Use copy-on-write: only overwrite a matrix with one CPS reference;
    // otherwise append a new matrix and point just this cel/part at it.
    crefMatrix = 0;
    for (int32_t icelScan = 0; icelScan < pggcel->IvMac(); ++icelScan)
    {
        const int32_t cpartScan = pggcel->Cb(icelScan) / SIZEOF(CPS);
        CPS *prgcpsScan = (CPS *)pggcel->QvGet(icelScan);
        for (int32_t ipartScan = 0; ipartScan < cpartScan; ++ipartScan)
            if (prgcpsScan[ipartScan].imat34 == imatOld)
                ++crefMatrix;
    }

    imatNew = imatOld;
    if (crefMatrix > 1)
    {
        if (!pglbmat34->FAdd(&bmat34Write, &imatNew) || imatNew > 0x7fff)
            goto LEnd;
        prgcps[ipart].imat34 = (int16_t)imatNew;
    }
    else
    {
        pglbmat34->Put(imatOld, &bmat34Write);
    }

    if (!pcfl->FAdd(pglbmat34->CbOnFile(), kctgGlxf, &cnoXfNew, &blck) ||
        !pglbmat34->FWrite(&blck))
        goto LEnd;
    if (!pcfl->FAdd(pggcel->CbOnFile(), kctgGgcl, &cnoCelNew, &blck) ||
        !pggcel->FWrite(&blck))
        goto LEnd;

    if (fAddUndo)
    {
        pasun = ASUN::PasunNew();
        if (pasun == pvNil)
            goto LEnd;
    }

    if (!F4DMMActorStudioReplacePoseChildren(pcfl, cnoAction, cnoCelNew, cnoXfNew,
                                              &cnoCelOldCanonical, &cnoXfOldCanonical,
                                              &cCelLinksOld, &cXfLinksOld))
        goto LEnd;
    fChildrenReplaced = fTrue;

    if (fAddUndo)
    {
        pasun->SetState(pactr->Arid(), anid, icel, ipart, tagTmpl.cno, cnoAction,
                        &bmat34Before, &bmat34Write, selectionKind,
                        ipartSelectionFirst, cpartSelection);
    }

    // ACTN is a CRF-cached BACO. The root ACTN CNO did not change, only its
    // GGCL/GLXF children did, so toss that representation before Actor Studio
    // asks TMPL to pose the preview again.
    pcrfEdit->FSetCrep(crepToss, kctgActn, cnoAction, ACTN::FReadActn);

    if (fAddUndo && !FAddUndo(pasun))
    {
        // Do not try to preserve orphan Chunky child CNOs for rollback: that
        // was the exact v120 undo bug. Replay the actual pre-edit matrix through
        // the same writer without creating another undo record.
        const bool fRestored = FActorStudioSetPartMatrixCore(pactr, anid, icel, ipart,
                                                              &bmat34Before, fFalse);
        MultiLog(this,
            "actor_studio_part_edit undo_add_failed arid=%ld action=%ld cel=%ld part=%ld restored=%d",
            (long)pactr->Arid(), (long)anid, (long)icel, (long)ipart, (int)fRestored);
        if (fRestored)
            fChildrenReplaced = fFalse;
        ClearUndo();
        goto LEnd;
    }

    _fRequiresVmmSave = fTrue;
    _fAutosaveDirty = fTrue;
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    MultiLog(this,
        "actor_studio_part_edit commit arid=%ld tmpl=%ld action=%ld actn_cno=%ld cel=%ld part=%ld matrix_old=%ld matrix_new=%ld refs=%ld ggcl_old=%ld ggcl_new=%ld glxf_old=%ld glxf_new=%ld stale_links=%ld/%ld undo_record=%d",
        (long)pactr->Arid(), (long)tagTmpl.cno, (long)anid, (long)cnoAction,
        (long)icel, (long)ipart, (long)imatOld, (long)imatNew, (long)crefMatrix,
        (long)cnoCelOldCanonical, (long)cnoCelNew, (long)cnoXfOldCanonical, (long)cnoXfNew,
        (long)cCelLinksOld, (long)cXfLinksOld, (int)fAddUndo);
    fRet = fTrue;

LEnd:
    if (!fRet)
    {
        // New chunks are safe to delete only while they are not the active
        // ACTN children. If an unexpected rollback failure leaves the edit
        // attached, preserve them rather than deleting live data underneath it.
        if (!fChildrenReplaced)
        {
            if (pcfl != pvNil && cnoCelNew != cnoNil && pcfl->FFind(kctgGgcl, cnoCelNew))
                pcfl->Delete(kctgGgcl, cnoCelNew);
            if (pcfl != pvNil && cnoXfNew != cnoNil && pcfl->FFind(kctgGlxf, cnoXfNew))
                pcfl->Delete(kctgGlxf, cnoXfNew);
        }
        MultiLog(this,
            "actor_studio_part_edit fail arid=%ld action=%ld cel=%ld part=%ld ggcl_new=%ld glxf_new=%ld active=%d undo_record=%d",
            pactr != pvNil ? (long)pactr->Arid() : -1L, (long)anid, (long)celn,
            (long)ipart, (long)cnoCelNew, (long)cnoXfNew,
            (int)fChildrenReplaced, (int)fAddUndo);
    }
    ReleasePpo(&pasun);
    ReleasePpo(&pggcel);
    ReleasePpo(&pglbmat34);
    return fRet;
}

bool MVIE::FActorStudioSetPartMatrices(PACTR pactr, int32_t anid, int32_t celn,
                                         const int32_t *prgipart,
                                         const BMAT34 *prgbmat34, int32_t cpart,
                                         int32_t selectionKind, int32_t ipartSelectionFirst,
                                         int32_t cpartSelection)
{
    return FActorStudioSetPartMatricesCore(pactr, anid, celn, prgipart, prgbmat34,
                                            cpart, fTrue, selectionKind,
                                            ipartSelectionFirst, cpartSelection);
}

bool MVIE::FActorStudioSetPartMatricesCore(PACTR pactr, int32_t anid, int32_t celn,
                                             const int32_t *prgipart,
                                             const BMAT34 *prgbmat34, int32_t cpart,
                                             bool fAddUndo, int32_t selectionKind,
                                             int32_t ipartSelectionFirst, int32_t cpartSelection)
{
    AssertThis(0);
    AssertNilOrPo(pactr, 0);
    if (pactr == pvNil || pactr->Ptmpl() == pvNil || pactr->Ptmpl()->FIsTdt() ||
        prgipart == pvNil || prgbmat34 == pvNil || cpart <= 0 ||
        cpart > kc4DMMActorStudioFramePartMax ||
        !FActorStudioActionIsCustom(pactr, anid) || !FEnsureAutosave())
        return fFalse;

    TAG tagTmpl;
    pactr->GetTagTmpl(&tagTmpl);
    PCRF pcrfEdit = tagTmpl.pcrf != pvNil ? tagTmpl.pcrf : _pcrfAutoSave;
    PCFL pcfl = pcrfEdit != pvNil ? pcrfEdit->Pcfl() : pvNil;
    if (pcfl == pvNil || tagTmpl.sid != ksidUseCrf)
        return fFalse;

    CNO cnoAction = cnoNil;
    int32_t anidAlias = ivNil;
    if (!F4DMMActorStudioActionChildIndependent(pcfl, tagTmpl.cno, anid,
                                                 pactr->Ptmpl()->Cactn(),
                                                 &cnoAction, &anidAlias))
        return fFalse;

    KID kidCel;
    KID kidXf;
    BLCK blck;
    int16_t bo = kboCur;
    PGG pggcel = pvNil;
    PGL pglbmat34 = pvNil;
    PASMU pasmu = pvNil;
    bool fRet = fFalse;
    bool fChildrenReplaced = fFalse;
    CNO cnoCelNew = cnoNil;
    CNO cnoXfNew = cnoNil;
    CNO cnoCelOld = cnoNil;
    CNO cnoXfOld = cnoNil;
    int32_t cCelLinksOld = 0;
    int32_t cXfLinksOld = 0;
    BMAT34 rgbmat34Before[kc4DMMActorStudioFramePartMax];
    int32_t icel = 0;
    int32_t cpartCel = 0;
    CPS *prgcps = pvNil;

    if (!pcfl->FGetKidChidCtg(kctgActn, cnoAction, 0, kctgGgcl, &kidCel) ||
        !pcfl->FGetKidChidCtg(kctgActn, cnoAction, 0, kctgGlxf, &kidXf) ||
        !pcfl->FFind(kidCel.cki.ctg, kidCel.cki.cno, &blck))
        goto LEnd;
    pggcel = GG::PggRead(&blck, &bo);
    if (pggcel == pvNil)
        goto LEnd;
    if (bo == kboOther)
    {
        AssertBomRglw(kbomCel, SIZEOF(CEL));
        AssertBomRgsw(kbomCps, SIZEOF(CPS));
        for (int32_t i = 0; i < pggcel->IvMac(); ++i)
        {
            SwapBytesRglw(pggcel->QvFixedGet(i), SIZEOF(CEL) / SIZEOF(int32_t));
            SwapBytesRgsw(pggcel->QvGet(i), pggcel->Cb(i) / SIZEOF(int16_t));
        }
    }

    if (!pcfl->FFind(kidXf.cki.ctg, kidXf.cki.cno, &blck))
        goto LEnd;
    bo = kboCur;
    pglbmat34 = GL::PglRead(&blck, &bo);
    if (pglbmat34 == pvNil)
        goto LEnd;
    if (bo == kboOther)
    {
        AssertBomRglw(kbomBmat34, SIZEOF(BMAT34));
        if (pglbmat34->IvMac() > 0)
            SwapBytesRglw(pglbmat34->QvGet(0),
                          LwMul(pglbmat34->IvMac(), SIZEOF(BMAT34) / SIZEOF(int32_t)));
    }

    if (pggcel->IvMac() <= 0)
        goto LEnd;
    icel = celn % pggcel->IvMac();
    if (icel < 0)
        icel += pggcel->IvMac();
    cpartCel = pggcel->Cb(icel) / SIZEOF(CPS);
    prgcps = (CPS *)pggcel->QvGet(icel);

    for (int32_t i = 0; i < cpart; ++i)
    {
        const int32_t ipart = prgipart[i];
        if (!FIn(ipart, 0, cpartCel))
            goto LEnd;
        for (int32_t j = 0; j < i; ++j)
            if (prgipart[j] == ipart)
                goto LEnd;
        const int32_t imatOld = prgcps[ipart].imat34;
        if (!FIn(imatOld, 0, pglbmat34->IvMac()))
            goto LEnd;
        rgbmat34Before[i] = *(BMAT34 *)pglbmat34->QvGet(imatOld);
    }

    // Always append independent matrices for group edits. That keeps shared
    // legacy GLXF entries immutable and makes the transaction deterministic.
    for (int32_t i = 0; i < cpart; ++i)
    {
        int32_t imatNew = ivNil;
        BMAT34 bmat34New = prgbmat34[i];
        if (!pglbmat34->FAdd(&bmat34New, &imatNew) || imatNew > 0x7fff)
            goto LEnd;
        prgcps[prgipart[i]].imat34 = (int16_t)imatNew;
    }

    if (!pcfl->FAdd(pglbmat34->CbOnFile(), kctgGlxf, &cnoXfNew, &blck) ||
        !pglbmat34->FWrite(&blck) ||
        !pcfl->FAdd(pggcel->CbOnFile(), kctgGgcl, &cnoCelNew, &blck) ||
        !pggcel->FWrite(&blck))
        goto LEnd;

    if (fAddUndo)
    {
        pasmu = ASMU::PasmuNew();
        if (pasmu == pvNil)
            goto LEnd;
    }
    if (!F4DMMActorStudioReplacePoseChildren(pcfl, cnoAction, cnoCelNew, cnoXfNew,
                                              &cnoCelOld, &cnoXfOld,
                                              &cCelLinksOld, &cXfLinksOld))
        goto LEnd;
    fChildrenReplaced = fTrue;

    if (fAddUndo)
        pasmu->SetState(pactr->Arid(), anid, icel, tagTmpl.cno, cnoAction,
                        prgipart, rgbmat34Before, prgbmat34, cpart, selectionKind,
                        ipartSelectionFirst, cpartSelection);

    pcrfEdit->FSetCrep(crepToss, kctgActn, cnoAction, ACTN::FReadActn);
    if (fAddUndo && !FAddUndo(pasmu))
    {
        const bool fRestored = FActorStudioSetPartMatricesCore(pactr, anid, icel,
                                                                prgipart, rgbmat34Before,
                                                                cpart, fFalse);
        MultiLog(this,
            "actor_studio_group_part_edit undo_add_failed action=%ld cel=%ld parts=%ld restored=%d",
            (long)anid, (long)icel, (long)cpart, (int)fRestored);
        if (fRestored)
            fChildrenReplaced = fFalse;
        ClearUndo();
        goto LEnd;
    }

    _fRequiresVmmSave = fTrue;
    _fAutosaveDirty = fTrue;
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    MultiLog(this,
        "actor_studio_group_part_edit commit arid=%ld tmpl=%ld action=%ld cel=%ld parts=%ld first=%ld ggcl_old=%ld ggcl_new=%ld glxf_old=%ld glxf_new=%ld undo_record=%d",
        (long)pactr->Arid(), (long)tagTmpl.cno, (long)anid, (long)icel,
        (long)cpart, (long)prgipart[0], (long)cnoCelOld, (long)cnoCelNew,
        (long)cnoXfOld, (long)cnoXfNew, (int)fAddUndo);
    fRet = fTrue;

LEnd:
    if (!fRet && !fChildrenReplaced)
    {
        if (pcfl != pvNil && cnoCelNew != cnoNil && pcfl->FFind(kctgGgcl, cnoCelNew))
            pcfl->Delete(kctgGgcl, cnoCelNew);
        if (pcfl != pvNil && cnoXfNew != cnoNil && pcfl->FFind(kctgGlxf, cnoXfNew))
            pcfl->Delete(kctgGlxf, cnoXfNew);
    }
    ReleasePpo(&pasmu);
    ReleasePpo(&pggcel);
    ReleasePpo(&pglbmat34);
    return fRet;
}

void MVIE::SetPrecacheMode(bool fEnable)
{
    vfPrecacheMode = FPure(fEnable);
}

bool MVIE::FPrecacheMode(void)
{
    return vfPrecacheMode;
}

void MVIE::SetTestLightMode(bool fEnable)
{
    vfTestLightMode = FPure(fEnable);
}

bool MVIE::FTestLightMode(void)
{
    return vfTestLightMode;
}

void MVIE::SetShadowMode(bool fEnable)
{
    vfShadowMode = FPure(fEnable);
}

bool MVIE::FShadowMode(void)
{
    return vfShadowMode;
}

bool MVIE::FSceneDynamicLightingActive(void)
{
    return vfTestLightMode && vfSceneDynamicLightingActive;
}

bool MVIE::FSceneFlatLightingActive(void)
{
    // v4: Light Lab OFF is the normal/default 3DMM lighting state again, not
    // the experimental v3w unlit-material view. Keep this compatibility query
    // false so newly created/read materials retain BR_MATF_LIGHT.
    return fFalse;
}

bool MVIE::FSceneDefaultLightingShadersActive(void)
{
    return vfSceneDefaultLightingShadersActive;
}

bool MVIE::FSceneLightLabCombineLegacyActive(void)
{
    return vfSceneLightLabCombineLegacyActive;
}

void MVIE::SetDiagnosticsMode(bool fEnable)
{
    bool fWasEnabled = vfDiagnosticsMode;
    vfDiagnosticsMode = FPure(fEnable);
#if defined(KAUAI_WIN32)
    if (vfDiagnosticsMode && !fWasEnabled)
    {
        viDiagScene = ivNil;
        vnDiagFrame = ivNil;
        strcpy_s(vszDiagPhase, SIZEOF(vszDiagPhase), "startup");
        vluDiagSequence = 0;
        ResetDiagnosticsFiles();
        DiagLog("diagnostics enabled");
    }
#endif // KAUAI_WIN32
}

bool MVIE::FDiagnosticsMode(void)
{
    return vfDiagnosticsMode;
}

void MVIE::SetLightEditorLogMode(bool fEnable)
{
    vfLightEditorLogMode = FPure(fEnable);
#if defined(KAUAI_WIN32)
    if (vpfileLightEditor != pvNil)
    {
        fflush(vpfileLightEditor);
        fclose(vpfileLightEditor);
        vpfileLightEditor = pvNil;
    }
    vluLightEditorSequence = 0;
    if (!vfLightEditorLogMode)
        return;

    achar szPath[MAX_PATH];
    if (!FDiagPath("light_editor.log", szPath, SIZEOF(szPath)) ||
        0 != fopen_s(&vpfileLightEditor, szPath, "w") || vpfileLightEditor == pvNil)
    {
        vfLightEditorLogMode = fFalse;
        return;
    }
    setvbuf(vpfileLightEditor, pvNil, _IOFBF, 32 * 1024);
    fprintf(vpfileLightEditor, "3DMMEx -logs_light_ed lighting/editor event log\n");
    fflush(vpfileLightEditor);
#endif
}

bool MVIE::FLightEditorLogMode(void)
{
    return vfLightEditorLogMode;
}

void MVIE::LightEditorLog(PMVIE pmvie, PCSZ pszFormat, ...)
{
    if (!vfLightEditorLogMode || pszFormat == pvNil)
        return;
#if defined(KAUAI_WIN32)
    if (vpfileLightEditor == pvNil)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    int32_t iscen = ivNil;
    int32_t nfrm = ivNil;
    int32_t aridSelected = aridNil;
    if (pmvie != pvNil)
    {
        iscen = pmvie->Iscen();
        if (pmvie->Pscen() != pvNil)
        {
            nfrm = pmvie->Pscen()->Nfrm();
            if (pmvie->Pscen()->PactrSelected() != pvNil)
                aridSelected = pmvie->Pscen()->PactrSelected()->Arid();
        }
    }

    fprintf(vpfileLightEditor, "%08lu %02u:%02u:%02u.%03u T%lu scene=%ld frame=%ld selected=%ld ",
            (unsigned long)vluLightEditorSequence++, (unsigned)st.wHour,
            (unsigned)st.wMinute, (unsigned)st.wSecond, (unsigned)st.wMilliseconds,
            (unsigned long)GetCurrentThreadId(), (long)iscen, (long)nfrm,
            (long)aridSelected);
    va_list ap;
    va_start(ap, pszFormat);
    vfprintf(vpfileLightEditor, pszFormat, ap);
    va_end(ap);
    fputc('\n', vpfileLightEditor);
    // Editor-light events are sparse. Flush each event so the final action
    // survives an access violation without paying -logs' open/close tax.
    fflush(vpfileLightEditor);
#else
    (void)pmvie;
    (void)pszFormat;
#endif
}

void MVIE::SetMultiLogMode(bool fEnable)
{
    vfMultiLogMode = FPure(fEnable);
#if defined(KAUAI_WIN32)
    if (vpfileMulti != pvNil)
    {
        fflush(vpfileMulti);
        fclose(vpfileMulti);
        vpfileMulti = pvNil;
    }
    vluMultiSequence = 0;
    if (!vfMultiLogMode)
        return;

    achar szPath[MAX_PATH];
    if (!FDiagPath("multi.log", szPath, SIZEOF(szPath)) ||
        0 != fopen_s(&vpfileMulti, szPath, "w") || vpfileMulti == pvNil)
    {
        vfMultiLogMode = fFalse;
        return;
    }
    setvbuf(vpfileMulti, pvNil, _IOFBF, 64 * 1024);
    fprintf(vpfileMulti, "4DMM -multi_log Object Groups transform log\n");
    fflush(vpfileMulti);
#endif
}

bool MVIE::FMultiLogMode(void)
{
    return vfMultiLogMode;
}

void MVIE::MultiLog(PMVIE pmvie, PCSZ pszFormat, ...)
{
    if (!vfMultiLogMode || pszFormat == pvNil)
        return;
#if defined(KAUAI_WIN32)
    if (vpfileMulti == pvNil)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    int32_t iscen = pmvie != pvNil ? pmvie->Iscen() : ivNil;
    int32_t nfrm = (pmvie != pvNil && pmvie->Pscen() != pvNil) ? pmvie->Pscen()->Nfrm() : ivNil;
    fprintf(vpfileMulti, "%08lu %02u:%02u:%02u.%03u T%lu scene=%ld frame=%ld ",
            (unsigned long)vluMultiSequence++, (unsigned)st.wHour,
            (unsigned)st.wMinute, (unsigned)st.wSecond, (unsigned)st.wMilliseconds,
            (unsigned long)GetCurrentThreadId(), (long)iscen, (long)nfrm);
    va_list ap;
    va_start(ap, pszFormat);
    vfprintf(vpfileMulti, pszFormat, ap);
    va_end(ap);
    fputc('\n', vpfileMulti);
    fflush(vpfileMulti);
#else
    (void)pmvie;
    (void)pszFormat;
#endif
}

void MVIE::ActorStudioDuplicateLog(PMVIE pmvie, PCSZ pszFormat, ...)
{
    if (pszFormat == pvNil)
        return;
#if defined(KAUAI_WIN32)
    achar szPath[MAX_PATH];
    if (!FDiagPath("actor_studio_duplicate.log", szPath, SIZEOF(szPath)))
        return;
    FILE *pfile = pvNil;
    if (0 != fopen_s(&pfile, szPath, "a") || pfile == pvNil)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    int32_t iscen = pmvie != pvNil ? pmvie->Iscen() : ivNil;
    int32_t nfrm = (pmvie != pvNil && pmvie->Pscen() != pvNil) ? pmvie->Pscen()->Nfrm() : ivNil;
    fprintf(pfile, "%04u-%02u-%02u %02u:%02u:%02u.%03u T%lu scene=%ld frame=%ld ",
            (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
            (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
            (unsigned)st.wMilliseconds, (unsigned long)GetCurrentThreadId(),
            (long)iscen, (long)nfrm);
    va_list ap;
    va_start(ap, pszFormat);
    vfprintf(pfile, pszFormat, ap);
    va_end(ap);
    fputc('\n', pfile);
    fflush(pfile);
    fclose(pfile);
#else
    (void)pmvie;
    (void)pszFormat;
#endif
}

void MVIE::SetPerformanceMode(bool fEnable)
{
    vfPerformanceMode = FPure(fEnable);
    vgLightingPerformanceEnabled = vfPerformanceMode ? 1 : 0;
#if defined(KAUAI_WIN32)
    if (vpfilePerf != pvNil)
    {
        fflush(vpfilePerf);
        fclose(vpfilePerf);
        vpfilePerf = pvNil;
    }

    if (!vfPerformanceMode)
        return;

    QueryPerformanceFrequency(&vliPerfFrequency);
    vluPerfSequence = 0;
    vcPerfRowsSinceFlush = 0;
    ClearPb(&vperfAcc, SIZEOF(vperfAcc));

    achar szPath[MAX_PATH];
    if (!FDiagPath("performance.csv", szPath, SIZEOF(szPath)))
    {
        vfPerformanceMode = fFalse;
        vgLightingPerformanceEnabled = 0;
        return;
    }

    if (0 != fopen_s(&vpfilePerf, szPath, "w") || vpfilePerf == pvNil)
    {
        vfPerformanceMode = fFalse;
        vgLightingPerformanceEnabled = 0;
        return;
    }

    setvbuf(vpfilePerf, pvNil, _IOFBF, 64 * 1024);
    fprintf(vpfilePerf,
            "seq,scene,frame,actors,tboxes,true_color,total_us,work_fps,text_current_us,update_marked_us,advance_us,final_camera_us,final_render_us,scene_force_actors_us,scene_force_tboxes_us,scene_visibility_us,scene_camera_us,scene_prerender_us,bwld_calls,bwld_children_max,bwld_begin_callbacks_us,bwld_clean_us,bwld_scene_render_us,bwld_post_us,bwld_total_us,bwld_other_us,over_6fps_budget_us,actor_tween_update_us,light_preparation_us,model_lighting_us,brender_model_update_us,brender_render_us,present_us,models_relit,vertices_processed,models_rebuilt,light_calculations,set_actn_cel_us,set_actn_cel_calls,action_fetch_us,action_fetch_calls,model_fetch_us,model_fetch_calls,model_lookup_us,pbaco_fetch_us,model_read_us,model_read_calls,model_read_unprepared,model_read_preprepared,model_destroy_us,model_destroy_calls,part_model_us,part_model_calls,part_model_same_pointer,part_model_same_source,part_model_same_resource,part_model_resource_reuse,part_model_same_file,part_model_file_reuse,part_model_geometry_compare_us,part_model_same_geometry,part_model_geometry_reuse,part_model_changed,part_matrix_us,part_matrix_calls,unique_model_requests,unique_model_reads,unique_model_overflow,part_model_content_compare_us,part_model_same_content,part_model_content_reuse,part_model_geometry_reject_mask,part_model_content_candidates,part_model_old_crf,part_model_new_crf,part_model_old_fingerprint,part_model_new_fingerprint,part_model_fingerprint_size_match,part_model_fingerprint_hashA_match,part_model_fingerprint_hashB_match,brender_model_add_calls,brender_model_add_quick_calls,brender_model_update_all_calls,brender_model_update_other_calls,brender_model_update_quick_calls,brender_model_update_all_us,brender_model_update_other_us,fix3d_enabled,camera_x_raw,camera_y_raw,camera_z_raw,camera_hither_raw,camera_yon_raw,fix3d_mesh_calls,fix3d_mesh_accept_input,fix3d_forced_partial,safe_div_overflows,depth_grad_calls,depth_grad_overflows\n");
    fflush(vpfilePerf);
#endif // KAUAI_WIN32
}

bool MVIE::FPerformanceMode(void)
{
    return vfPerformanceMode;
}

uint64_t MVIE::PerfNow(void)
{
#if defined(KAUAI_WIN32)
    if (!vfPerformanceMode)
        return 0;
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return (uint64_t)li.QuadPart;
#else
    return 0;
#endif // KAUAI_WIN32
}

uint32_t MVIE::PerfElapsedUs(uint64_t qwStart)
{
#if defined(KAUAI_WIN32)
    if (!vfPerformanceMode || qwStart == 0 || vliPerfFrequency.QuadPart <= 0)
        return 0;
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    uint64_t qwDelta = (uint64_t)li.QuadPart - qwStart;
    return (uint32_t)((qwDelta * 1000000ULL) / (uint64_t)vliPerfFrequency.QuadPart);
#else
    (void)qwStart;
    return 0;
#endif // KAUAI_WIN32
}

void MVIE::PerfAddSceneTimings(uint32_t cusecForceActors, uint32_t cusecForceTboxes,
                               uint32_t cusecVisibility, uint32_t cusecCamera,
                               uint32_t cusecPrerender)
{
    if (!vfPerformanceMode)
        return;
    vperfAcc.cusecForceActors += cusecForceActors;
    vperfAcc.cusecForceTboxes += cusecForceTboxes;
    vperfAcc.cusecVisibility += cusecVisibility;
    vperfAcc.cusecSceneCamera += cusecCamera;
    vperfAcc.cusecPrerender += cusecPrerender;
}

void MVIE::PerfAddBRenderTimings(uint32_t cusecBeginCallbacks, uint32_t cusecClean,
                                 uint32_t cusecSceneRender, uint32_t cusecPost,
                                 uint32_t cusecTotal, int32_t cWorldChildren)
{
    if (!vfPerformanceMode)
        return;
    vperfAcc.cusecBRenderBegin += cusecBeginCallbacks;
    vperfAcc.cusecBRenderClean += cusecClean;
    vperfAcc.cusecBRenderScene += cusecSceneRender;
    vperfAcc.cusecBRenderPost += cusecPost;
    vperfAcc.cusecBRenderTotal += cusecTotal;
    vperfAcc.cBRenderCalls++;
    if (cWorldChildren > vperfAcc.cWorldChildrenMax)
        vperfAcc.cWorldChildrenMax = cWorldChildren;
}

static void _PerfAddUniqueModelKey(uint64_t qwKey, uint64_t *prgqw, int32_t *pcqw, int32_t *pcOverflow)
{
    if (prgqw == pvNil || pcqw == pvNil || pcOverflow == pvNil)
        return;
    for (int32_t iqw = 0; iqw < *pcqw; iqw++)
    {
        if (prgqw[iqw] == qwKey)
            return;
    }
    if (*pcqw < 1024)
        prgqw[(*pcqw)++] = qwKey;
    else
        (*pcOverflow)++;
}

void MVIE::PerfRecordSetActnCel(uint32_t cusec)
{
    if (!vfPerformanceMode)
        return;
    vperfAcc.cusecSetActnCel += cusec;
    vperfAcc.cSetActnCelCalls++;
}

void MVIE::PerfRecordActionFetch(uint32_t cusec)
{
    if (!vfPerformanceMode)
        return;
    vperfAcc.cusecActionFetch += cusec;
    vperfAcc.cActionFetchCalls++;
}

void MVIE::PerfRecordModelFetch(uint32_t cusecTotal, uint32_t cusecLookup, uint32_t cusecPbaco, CTG ctg, CNO cno)
{
    if (!vfPerformanceMode)
        return;
    vperfAcc.cusecModelFetch += cusecTotal;
    vperfAcc.cusecModelLookup += cusecLookup;
    vperfAcc.cusecPbacoFetch += cusecPbaco;
    vperfAcc.cModelFetchCalls++;
    uint64_t qwKey = ((uint64_t)(uint32_t)ctg << 32) | (uint32_t)cno;
    _PerfAddUniqueModelKey(qwKey, vperfAcc.rgqwModelRequestKeys, &vperfAcc.cUniqueModelRequests,
                           &vperfAcc.cUniqueModelOverflow);
}

void MVIE::PerfRecordModelRead(uint32_t cusec, CTG ctg, CNO cno, bool fPreprepared)
{
    if (!vfPerformanceMode)
        return;
    vperfAcc.cusecModelRead += cusec;
    vperfAcc.cModelReadCalls++;
    if (fPreprepared)
        vperfAcc.cModelReadPreprepared++;
    else
        vperfAcc.cModelReadUnprepared++;
    uint64_t qwKey = ((uint64_t)(uint32_t)ctg << 32) | (uint32_t)cno;
    _PerfAddUniqueModelKey(qwKey, vperfAcc.rgqwModelReadKeys, &vperfAcc.cUniqueModelReads,
                           &vperfAcc.cUniqueModelOverflow);
}

void MVIE::PerfRecordModelDestroy(uint32_t cusec)
{
    if (!vfPerformanceMode)
        return;
    vperfAcc.cusecModelDestroy += cusec;
    vperfAcc.cModelDestroyCalls++;
}

void MVIE::PerfRecordPartModel(uint32_t cusec, bool fSamePointer, bool fSameSource,
                               bool fSameResource, bool fResourceReuse, bool fSameFile,
                               bool fFileReuse, bool fSameContent, bool fContentReuse,
                               uint32_t cusecContentCompare, bool fSameGeometry,
                               bool fGeometryReuse, uint32_t cusecGeometryCompare,
                               int32_t iGeometryReject)
{
    if (!vfPerformanceMode)
        return;
    vperfAcc.cusecPartModel += cusec;
    vperfAcc.cusecPartModelContentCompare += cusecContentCompare;
    vperfAcc.cusecPartModelGeometryCompare += cusecGeometryCompare;
    vperfAcc.cPartModelCalls++;
    if (fSamePointer)
        vperfAcc.cPartModelSamePointer++;
    if (fSameSource)
        vperfAcc.cPartModelSameSource++;
    if (fSameResource)
        vperfAcc.cPartModelSameResource++;
    if (fResourceReuse)
        vperfAcc.cPartModelResourceReuse++;
    if (fSameFile)
        vperfAcc.cPartModelSameFile++;
    if (fFileReuse)
        vperfAcc.cPartModelFileReuse++;
    if (fSameContent)
        vperfAcc.cPartModelSameContent++;
    if (fContentReuse)
        vperfAcc.cPartModelContentReuse++;
    if (iGeometryReject > 0 && iGeometryReject <= 31)
        vperfAcc.grfPartModelGeometryReject |= (1u << (iGeometryReject - 1));
    if (fSameGeometry)
        vperfAcc.cPartModelSameGeometry++;
    if (fGeometryReuse)
        vperfAcc.cPartModelGeometryReuse++;
    if (!fSamePointer && !fResourceReuse && !fFileReuse && !fContentReuse && !fGeometryReuse)
        vperfAcc.cPartModelChanged++;
}

void MVIE::PerfRecordPartModelProvenance(bool fOldCrf, bool fNewCrf, bool fOldFingerprint,
                                         bool fNewFingerprint, bool fSizeMatch,
                                         bool fHashAMatch, bool fHashBMatch)
{
    if (!vfPerformanceMode)
        return;
    vperfAcc.cPartModelContentCandidates++;
    if (fOldCrf)
        vperfAcc.cPartModelOldCrf++;
    if (fNewCrf)
        vperfAcc.cPartModelNewCrf++;
    if (fOldFingerprint)
        vperfAcc.cPartModelOldFingerprint++;
    if (fNewFingerprint)
        vperfAcc.cPartModelNewFingerprint++;
    if (fSizeMatch)
        vperfAcc.cPartModelFingerprintSizeMatch++;
    if (fHashAMatch)
        vperfAcc.cPartModelFingerprintHashAMatch++;
    if (fHashBMatch)
        vperfAcc.cPartModelFingerprintHashBMatch++;
}

void MVIE::PerfRecordPartMatrix(uint32_t cusec)
{
    if (!vfPerformanceMode)
        return;
    vperfAcc.cusecPartMatrix += cusec;
    vperfAcc.cPartMatrixCalls++;
}

// Low-overhead bridge for bren/bwld.cpp.  Unlike -logs, -perf does not open,
// flush and close a text file for every actor/render breadcrumb.
bool FPerformanceProfileEnabled(void)
{
    return MVIE::FPerformanceMode();
}

uint64_t QwPerformanceProfileNow(void)
{
    return MVIE::PerfNow();
}

uint32_t CusecPerformanceProfileElapsed(uint64_t qwStart)
{
    return MVIE::PerfElapsedUs(qwStart);
}

void RecordPerformanceBRender(uint32_t cusecBeginCallbacks, uint32_t cusecClean,
                              uint32_t cusecSceneRender, uint32_t cusecPost,
                              uint32_t cusecTotal, int32_t cWorldChildren)
{
    MVIE::PerfAddBRenderTimings(cusecBeginCallbacks, cusecClean, cusecSceneRender,
                                cusecPost, cusecTotal, cWorldChildren);
}

// actorlight39: low-overhead probes for the camera-height depth failure.
void RecordPerformance3DFixCamera(int32_t xr, int32_t yr, int32_t zr,
                                  int32_t zrHither, int32_t zrYon)
{
    if (!MVIE::FPerformanceMode())
        return;
    vperfAcc.xr3DFixCamera = xr;
    vperfAcc.yr3DFixCamera = yr;
    vperfAcc.zr3DFixCamera = zr;
    vperfAcc.zr3DFixHither = zrHither;
    vperfAcc.zr3DFixYon = zrYon;
}

extern "C" void RecordPerformance3DFixMesh(int fAcceptInput, int fForcedPartial)
{
    if (!MVIE::FPerformanceMode())
        return;
    vperfAcc.c3DFixMeshCalls++;
    if (fAcceptInput)
        vperfAcc.c3DFixMeshAcceptInput++;
    if (fForcedPartial)
        vperfAcc.c3DFixForcedPartial++;
}

extern "C" void RecordPerformanceSafeDivOverflow(void)
{
    if (MVIE::FPerformanceMode())
        vperfAcc.cSafeDivOverflows++;
}

extern "C" void RecordPerformanceDepthGradient(int fOverflow)
{
    if (!MVIE::FPerformanceMode())
        return;
    vperfAcc.cDepthGradientCalls++;
    if (fOverflow)
        vperfAcc.cDepthGradientOverflows++;
}

// Low-overhead C-linkage bridges used by the Source BRender lighting/model
// diagnostic patch. These are active only under -perf (and -logs, which now
// enables -perf automatically) so ordinary rendering does not pay timing cost.
extern "C" int FLightingPerformanceProfileEnabled(void)
{
    return MVIE::FPerformanceMode() ? 1 : 0;
}

extern "C" unsigned long long QwLightingPerformanceProfileNow(void)
{
    return (unsigned long long)MVIE::PerfNow();
}

extern "C" uint32_t CusecLightingPerformanceProfileElapsed(unsigned long long qwStart)
{
    return MVIE::PerfElapsedUs((uint64_t)qwStart);
}

extern "C" void RecordPerformanceLightPreparation(uint32_t cusec)
{
    if (!MVIE::FPerformanceMode())
        return;
    vperfAcc.cusecLightPreparation += cusec;
}

extern "C" void RecordPerformanceLighting(uint32_t cusec, int32_t cLightCalculations)
{
    if (!MVIE::FPerformanceMode())
        return;
    vperfAcc.cusecModelLighting += cusec;
    vperfAcc.cVerticesProcessed++;
    vperfAcc.cLightCalculations += cLightCalculations;
}

extern "C" void RecordPerformanceModelRelit(void)
{
    if (MVIE::FPerformanceMode())
        vperfAcc.cModelsRelit++;
}

extern "C" void RecordPerformanceModelAdd(int32_t fQuick)
{
    if (!MVIE::FPerformanceMode())
        return;
    vperfAcc.cModelAddCalls++;
    if (fQuick)
        vperfAcc.cModelAddQuickCalls++;
}

extern "C" void RecordPerformanceModelUpdateDetailed(uint32_t cusec, uint32_t grfUpdate, uint32_t grfModel)
{
    if (!MVIE::FPerformanceMode())
        return;
    vperfAcc.cusecModelUpdate += cusec;
    vperfAcc.cModelsRebuilt++;
    if (grfUpdate == BR_MODU_ALL)
    {
        vperfAcc.cModelUpdateAllCalls++;
        vperfAcc.cusecModelUpdateAll += cusec;
    }
    else
    {
        vperfAcc.cModelUpdateOtherCalls++;
        vperfAcc.cusecModelUpdateOther += cusec;
    }
    if (grfModel & BR_MODF_QUICK_UPDATE)
        vperfAcc.cModelUpdateQuickCalls++;
}

void MVIE::DiagLog(PCSZ pszFormat, ...)
{
    if (!vfDiagnosticsMode || pszFormat == pvNil)
        return;
#if defined(KAUAI_WIN32)
    achar szPath[MAX_PATH];
    if (!FDiagPath("session.log", szPath, SIZEOF(szPath)))
        return;

    FILE *pfile = pvNil;
    if (0 != fopen_s(&pfile, szPath, "a") || pfile == pvNil)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(pfile, "%08lu %02u:%02u:%02u.%03u T%lu ",
            (unsigned long)vluDiagSequence++, (unsigned)st.wHour,
            (unsigned)st.wMinute, (unsigned)st.wSecond,
            (unsigned)st.wMilliseconds, (unsigned long)GetCurrentThreadId());

    va_list ap;
    va_start(ap, pszFormat);
    vfprintf(pfile, pszFormat, ap);
    va_end(ap);
    fputc('\n', pfile);
    fflush(pfile);
    fclose(pfile);
#else
    (void)pszFormat;
#endif // KAUAI_WIN32
}

// Lightweight bridge used by the BRender world layer.  Keeping this as
// free functions avoids coupling bren/bwld.cpp to the MVIE class headers.
void DiagSetBRenderPhase(const char *pszPhase)
{
    if (!vfDiagnosticsMode || pszPhase == pvNil)
        return;
#if defined(KAUAI_WIN32)
    strcpy_s(vszDiagPhase, SIZEOF(vszDiagPhase), pszPhase);
    MVIE::DiagLog("BRENDER PHASE %s", vszDiagPhase);
#else
    (void)pszPhase;
#endif // KAUAI_WIN32
}

void DiagLogBRender(const char *pszFormat, ...)
{
    if (!vfDiagnosticsMode || pszFormat == pvNil)
        return;
#if defined(KAUAI_WIN32)
    achar sz[768];
    va_list ap;
    va_start(ap, pszFormat);
    vsprintf_s(sz, SIZEOF(sz), pszFormat, ap);
    va_end(ap);
    MVIE::DiagLog("BRENDER %s", sz);
#else
    (void)pszFormat;
#endif // KAUAI_WIN32
}

void MVIE::DiagSetContext(PMVIE pmvie, PCSZ pszPhase)
{
    if (!vfDiagnosticsMode)
        return;
#if defined(KAUAI_WIN32)
    if (pmvie != pvNil)
    {
        viDiagScene = pmvie->Iscen();
        vnDiagFrame = pmvie->Pscen() != pvNil ? pmvie->Pscen()->Nfrm() : ivNil;
    }
    if (pszPhase != pvNil)
        strcpy_s(vszDiagPhase, SIZEOF(vszDiagPhase), pszPhase);
    DiagLog("PHASE scene=%ld frame=%ld playing=%d stop=%d trans=%ld phase=%s",
            (long)viDiagScene, (long)vnDiagFrame,
            pmvie != pvNil ? (int)pmvie->FPlaying() : -1,
            pmvie != pvNil ? (int)pmvie->FStopPlaying() : -1,
            pmvie != pvNil ? (long)pmvie->Trans() : -1L, vszDiagPhase);
#else
    (void)pmvie;
    (void)pszPhase;
#endif // KAUAI_WIN32
}

void MVIE::DiagCrash(void *pvExceptionPointers)
{
    if (!vfDiagnosticsMode || pvExceptionPointers == pvNil)
        return;
#if defined(KAUAI_WIN32)
    EXCEPTION_POINTERS *pep = (EXCEPTION_POINTERS *)pvExceptionPointers;
    if (pep->ExceptionRecord == pvNil)
        return;

    achar szPath[MAX_PATH];
    if (!FDiagPath("crash.log", szPath, SIZEOF(szPath)))
        return;

    FILE *pfile = pvNil;
    if (0 != fopen_s(&pfile, szPath, "w") || pfile == pvNil)
        return;

    EXCEPTION_RECORD *per = pep->ExceptionRecord;
    uintptr_t luAddress = (uintptr_t)per->ExceptionAddress;
    MEMORY_BASIC_INFORMATION mbi;
    ClearPb(&mbi, SIZEOF(mbi));
    HMODULE hmodFault = hNil;
    achar szModule[MAX_PATH];
    szModule[0] = chNil;
    if (VirtualQuery(per->ExceptionAddress, &mbi, SIZEOF(mbi)) != 0)
    {
        hmodFault = (HMODULE)mbi.AllocationBase;
        if (hmodFault != hNil)
            GetModuleFileNameA(hmodFault, szModule, SIZEOF(szModule));
    }

    fprintf(pfile, "3DMMEx -logs crash report\n");
    fprintf(pfile, "last_scene=%ld\n", (long)viDiagScene);
    fprintf(pfile, "last_frame=%ld\n", (long)vnDiagFrame);
    fprintf(pfile, "last_phase=%s\n", vszDiagPhase);
    fprintf(pfile, "exception_code=0x%08lX\n", (unsigned long)per->ExceptionCode);
    fprintf(pfile, "exception_address=%p\n", per->ExceptionAddress);
    fprintf(pfile, "exception_flags=0x%08lX\n", (unsigned long)per->ExceptionFlags);
    if (hmodFault != hNil)
    {
        fprintf(pfile, "fault_module=%s\n", szModule[0] != chNil ? szModule : "unknown");
        fprintf(pfile, "fault_module_base=%p\n", hmodFault);
        fprintf(pfile, "fault_module_rva=0x%llX\n",
                (unsigned long long)(luAddress - (uintptr_t)hmodFault));
    }
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
                (unsigned long)pcx->Eax, (unsigned long)pcx->Ebx,
                (unsigned long)pcx->Ecx, (unsigned long)pcx->Edx,
                (unsigned long)pcx->Esi, (unsigned long)pcx->Edi);
#elif defined(_M_X64)
        CONTEXT *pcx = pep->ContextRecord;
        fprintf(pfile, "RIP=0x%llX RSP=0x%llX RBP=0x%llX\n",
                (unsigned long long)pcx->Rip, (unsigned long long)pcx->Rsp,
                (unsigned long long)pcx->Rbp);
#endif
    }
    fflush(pfile);
    fclose(pfile);
    DiagLog("CRASH code=0x%08lX address=%p module=%s rva=0x%llX",
            (unsigned long)per->ExceptionCode, per->ExceptionAddress,
            szModule[0] != chNil ? szModule : "unknown",
            hmodFault != hNil ? (unsigned long long)(luAddress - (uintptr_t)hmodFault) : 0ULL);
#else
    (void)pvExceptionPointers;
#endif // KAUAI_WIN32
}

int32_t MVIE::_ILightLabFind(int32_t iscen, int32_t arid)
{
    for (int32_t i = 0; i < _clightLab; i++)
    {
        if (_rglightLab[i].iscen == iscen && _rglightLab[i].arid == arid)
            return i;
    }
    return ivNil;
}

bool MVIE::_FLightLabActiveAtCurrentFrame(const LIGHTLAB &light) const
{
    // This helper is const, while the historical MVIE Pscen()/Iscen()
    // accessors are not const-qualified. Read the backing members directly
    // rather than discarding constness just to query the current scene/frame.
    if (_pscenOpen == pvNil || light.iscen != _iscen)
        return fFalse;

    const int32_t nfrm = _pscenOpen->Nfrm() - _pscenOpen->NfrmFirst() + 1;
    const bool fAfterSpawn = nfrm >= LwMax(1, light.nfrmSpawn);
    const bool fBeforeDespawn = (light.nfrmDespawn <= 0 || nfrm <= light.nfrmDespawn);
    return FPure(fAfterSpawn && fBeforeDespawn);
}

bool MVIE::FGetLightLabConfig(int32_t iscen, int32_t arid, LIGHTLAB *plight)
{
    AssertThis(0);
    AssertVarMem(plight);
    int32_t i = _ILightLabFind(iscen, arid);
    if (i == ivNil)
        return fFalse;
    *plight = _rglightLab[i];
    return fTrue;
}

bool MVIE::FLightLabShadowSlotAvailable(int32_t iscen, int32_t aridIgnore) const
{
    for (int32_t i = 0; i < _clightLab; ++i)
    {
        const LIGHTLAB &light = _rglightLab[i];
        if (light.iscen == iscen && light.arid != aridIgnore && light.fGenerateShadows)
            return fFalse;
    }
    return fTrue;
}

bool MVIE::FActorHasLight(int32_t iscen, int32_t arid)
{
    AssertThis(0);
    return _ILightLabFind(iscen, arid) != ivNil;
}

bool MVIE::FSceneLightsEnabled(int32_t iscen) const
{
    return FIn(iscen, 0, kc4DMMSceneSettingsMax) ? FPure(_rgfSceneLightsEnabled[iscen]) : fFalse;
}

bool MVIE::FSceneHideLightObjects(int32_t iscen) const
{
    return FIn(iscen, 0, kc4DMMSceneSettingsMax) ? FPure(_rgfSceneHideLightObjects[iscen]) : fFalse;
}

bool MVIE::FSceneDefaultLightingShaders(int32_t iscen) const
{
    return FIn(iscen, 0, kc4DMMSceneSettingsMax) ?
        FPure(_rgfSceneDefaultLightingShaders[iscen]) : FPure(_fDefaultLightingShaders);
}

bool MVIE::FSceneLightLabCombineLegacy(int32_t iscen) const
{
    return FIn(iscen, 0, kc4DMMSceneSettingsMax) ?
        FPure(_rgfSceneLightLabCombineLegacy[iscen]) : FPure(_fLightLabCombineLegacy);
}

bool MVIE::FAnySceneDefaultLightingShadersDisagree(bool fUseShaders) const
{
    fUseShaders = FPure(fUseShaders);
    // Cscen() predates const-correct accessors. This query does not mutate
    // MVIE, so use the backing scene count directly.
    for (int32_t iscen = 0; iscen < LwMin(_cscen, kc4DMMSceneSettingsMax); ++iscen)
    {
        if (_rgfSceneDefaultLightingShaders[iscen] != fUseShaders)
            return fTrue;
    }
    return fFalse;
}

void MVIE::SetDontAskLightLabCutCore(bool fEnable)
{
    AssertThis(0);
    _fDontAskLightLabCut = FPure(fEnable);
    SetDirty();
}

bool MVIE::FSetSceneLightsEnabled(int32_t iscen, bool fEnable, bool fUndo)
{
    AssertThis(0);
    if (!FIn(iscen, 0, kc4DMMSceneSettingsMax))
        return fFalse;
    fEnable = FPure(fEnable);
    if (_rgfSceneLightsEnabled[iscen] == fEnable)
        return fTrue;

    LightEditorLog(this, "scene_lighting request enable=%d undo=%d", (int)fEnable, (int)fUndo);
    if (fUndo && !FAddCameraTrackUndo(PszLit("Scene Lighting")))
        return fFalse;

    // Reset the background light installation while the old policy is still
    // visible to BKGD::TurnOffLights, then install the new policy cleanly.
    PBKGD pbkgd = (iscen == Iscen() && Pscen() != pvNil) ? Pscen()->Pbkgd() : pvNil;
    if (pbkgd != pvNil)
        pbkgd->TurnOffLights();

    _rgfSceneLightsEnabled[iscen] = fEnable;
    if (iscen == Iscen())
    {
        vfSceneDynamicLightingActive = fEnable;
        vfSceneLightLabCombineLegacyActive = FSceneLightLabCombineLegacy(iscen);
        Refresh4DMMMappedMaterialLightingPolicy();
        if (pbkgd != pvNil && _pbwld != pvNil)
            pbkgd->TurnOnLights(_pbwld);
        RefreshTestLight();
        UpdateTestLightAttachment();
        // The policy change itself is visible even when the scene has zero
        // Light Lab objects: authored/default lights are suppressed/restored and
        // mapped materials change response. Force the same complete redraw path
        // used by the other lighting-policy setters instead of waiting for a
        // later light creation or unrelated viewport event to wake the frame.
        if (_pbwld != pvNil)
            _pbwld->MarkDirty();
        InvalViewsAndScb();
    }
    SetDirty();
    MarkViews();
    return fTrue;
}

bool MVIE::FSetSceneHideLightObjects(int32_t iscen, bool fEnable, bool fUndo)
{
    AssertThis(0);
    if (!FIn(iscen, 0, kc4DMMSceneSettingsMax))
        return fFalse;
    fEnable = FPure(fEnable);
    if (_rgfSceneHideLightObjects[iscen] == fEnable)
        return fTrue;
    LightEditorLog(this, "light_object_visibility request hide=%d undo=%d hidden_count=%ld",
                   (int)fEnable, (int)fUndo, (long)_caridHiddenLightObjects);
    if (fUndo && !FAddCameraTrackUndo(PszLit("Light Object Visibility")))
        return fFalse;
    _rgfSceneHideLightObjects[iscen] = fEnable;
    if (iscen == Iscen())
        _UpdateHiddenLightObjects();
    SetDirty();
    MarkViews();
    return fTrue;
}

bool MVIE::FSetSceneDefaultLightingShaders(int32_t iscen, bool fEnable, bool fUndo)
{
    AssertThis(0);
    if (!FIn(iscen, 0, kc4DMMSceneSettingsMax))
        return fFalse;
    fEnable = FPure(fEnable);
    if (_rgfSceneDefaultLightingShaders[iscen] == fEnable)
        return fTrue;
    if (fUndo && !FAddCameraTrackUndo(PszLit("Scene Default Lighting")))
        return fFalse;

    _rgfSceneDefaultLightingShaders[iscen] = fEnable;
    if (iscen == Iscen())
    {
        vfSceneDefaultLightingShadersActive = fEnable;
        Refresh4DMMMappedMaterialLightingPolicy();
        if (_pbwld != pvNil)
            _pbwld->MarkDirty();
        InvalViewsAndScb();
    }
    LightEditorLog(this, "default_lighting scene=%ld shaders=%d", (long)iscen, (int)fEnable);
    SetDirty();
    MarkViews();
    return fTrue;
}

bool MVIE::FSetDefaultLightingShadersForMovie(bool fEnable, bool fDontAskAgain, bool fUndo)
{
    AssertThis(0);
    fEnable = FPure(fEnable);
    fDontAskAgain = FPure(fDontAskAgain);

    bool fChanges = _fDefaultLightingShaders != fEnable ||
                    (fDontAskAgain && !_fDontAskShaderPropagation);
    for (int32_t iscen = 0; !fChanges && iscen < LwMin(Cscen(), kc4DMMSceneSettingsMax); ++iscen)
        fChanges = _rgfSceneDefaultLightingShaders[iscen] != fEnable;
    if (!fChanges)
        return fTrue;
    if (fUndo && !FAddCameraTrackUndo(PszLit("Default Lighting Policy")))
        return fFalse;

    _fDefaultLightingShaders = fEnable;
    if (fDontAskAgain)
        _fDontAskShaderPropagation = fTrue;
    for (int32_t iscen = 0; iscen < LwMin(Cscen(), kc4DMMSceneSettingsMax); ++iscen)
        _rgfSceneDefaultLightingShaders[iscen] = fEnable;

    if (FIn(Iscen(), 0, kc4DMMSceneSettingsMax))
        vfSceneDefaultLightingShadersActive = _rgfSceneDefaultLightingShaders[Iscen()];
    Refresh4DMMMappedMaterialLightingPolicy();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    LightEditorLog(this, "default_lighting movie shaders=%d dont_ask=%d scenes=%ld",
                   (int)fEnable, (int)_fDontAskShaderPropagation, (long)Cscen());
    SetDirty();
    MarkViews();
    return fTrue;
}

bool MVIE::FSetSceneLightLabCombineLegacy(int32_t iscen, bool fEnable, bool fUndo)
{
    AssertThis(0);
    if (!FIn(iscen, 0, kc4DMMSceneSettingsMax))
        return fFalse;
    fEnable = FPure(fEnable);
    if (_rgfSceneLightLabCombineLegacy[iscen] == fEnable)
        return fTrue;
    if (fUndo && !FAddCameraTrackUndo(PszLit("Scene Light Lab Combination")))
        return fFalse;

    PBKGD pbkgd = (iscen == Iscen() && Pscen() != pvNil) ? Pscen()->Pbkgd() : pvNil;
    if (pbkgd != pvNil && FSceneDynamicLightingActive())
        pbkgd->TurnOffLights();

    _rgfSceneLightLabCombineLegacy[iscen] = fEnable;
    if (iscen == Iscen())
    {
        vfSceneLightLabCombineLegacyActive = fEnable;
        Refresh4DMMMappedMaterialLightingPolicy();
        if (pbkgd != pvNil && _pbwld != pvNil && FSceneDynamicLightingActive())
            pbkgd->TurnOnLights(_pbwld);
        RefreshTestLight();
        UpdateTestLightAttachment();
        if (_pbwld != pvNil)
            _pbwld->MarkDirty();
        InvalViewsAndScb();
    }
    LightEditorLog(this, "light_lab_combine scene=%ld legacy=%d", (long)iscen, (int)fEnable);
    SetDirty();
    MarkViews();
    return fTrue;
}

bool MVIE::FSetLightLabCombineLegacyForMovie(bool fEnable, bool fUndo)
{
    AssertThis(0);
    fEnable = FPure(fEnable);
    bool fChanges = _fLightLabCombineLegacy != fEnable;
    for (int32_t iscen = 0; !fChanges && iscen < LwMin(Cscen(), kc4DMMSceneSettingsMax); ++iscen)
        fChanges = _rgfSceneLightLabCombineLegacy[iscen] != fEnable;
    if (!fChanges)
        return fTrue;
    if (fUndo && !FAddCameraTrackUndo(PszLit("Light Lab Combination Policy")))
        return fFalse;

    PBKGD pbkgd = (Pscen() != pvNil) ? Pscen()->Pbkgd() : pvNil;
    if (pbkgd != pvNil && FSceneDynamicLightingActive())
        pbkgd->TurnOffLights();

    _fLightLabCombineLegacy = fEnable;
    for (int32_t iscen = 0; iscen < LwMin(Cscen(), kc4DMMSceneSettingsMax); ++iscen)
        _rgfSceneLightLabCombineLegacy[iscen] = fEnable;
    if (FIn(Iscen(), 0, kc4DMMSceneSettingsMax))
        vfSceneLightLabCombineLegacyActive = _rgfSceneLightLabCombineLegacy[Iscen()];
    Refresh4DMMMappedMaterialLightingPolicy();
    if (pbkgd != pvNil && _pbwld != pvNil && FSceneDynamicLightingActive())
        pbkgd->TurnOnLights(_pbwld);
    RefreshTestLight();
    UpdateTestLightAttachment();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    LightEditorLog(this, "light_lab_combine movie legacy=%d scenes=%ld",
                   (int)fEnable, (long)Cscen());
    SetDirty();
    MarkViews();
    return fTrue;
}

bool MVIE::FSetFreeLookAlwaysLastKnown(bool fEnable, bool fUndo)
{
    AssertThis(0);
    fEnable = FPure(fEnable);
    if (_fFreeLookAlwaysLastKnown == fEnable)
        return fTrue;
    if (fUndo && !FAddCameraTrackUndo(PszLit("Free Cam Start Policy")))
        return fFalse;
    _fFreeLookAlwaysLastKnown = fEnable;
    SetDirty();
    return fTrue;
}

bool MVIE::FIsLightAttachmentHidden(int32_t arid) const
{
    for (int32_t i = 0; i < _caridHiddenLightObjects; i++)
        if (_rgaridHiddenLightObjects[i] == arid)
            return fTrue;
    return fFalse;
}

bool MVIE::FObjectSelectable(int32_t arid) const
{
    if (arid == aridNil)
        return fTrue;
    for (int32_t i = 0; i < _caridNonSelectable; ++i)
        if (_rgaridNonSelectable[i] == arid)
            return fFalse;
    return fTrue;
}

bool MVIE::FSetObjectSelectable(int32_t arid, bool fSelectable, bool fUndo)
{
    AssertThis(0);
    if (arid == aridNil)
        return fFalse;

    int32_t iFound = ivNil;
    for (int32_t i = 0; i < _caridNonSelectable; ++i)
    {
        if (_rgaridNonSelectable[i] == arid)
        {
            iFound = i;
            break;
        }
    }

    fSelectable = FPure(fSelectable);
    const bool fWasSelectable = iFound == ivNil;
    if (fWasSelectable == fSelectable)
        return fTrue;
    if (!fSelectable && _caridNonSelectable >= kc4DMMNonSelectableMax)
        return fFalse;

    // Selectability lives in the .3ct sidecar. Reuse the existing complete
    // sidecar-state undo record so one toggle is one normal UHW operation and
    // redo naturally swaps back to the post-toggle state.
    if (fUndo && !FAddCameraTrackUndo(PszLit("Object Selectability")))
        return fFalse;

    if (fSelectable)
    {
        for (int32_t i = iFound + 1; i < _caridNonSelectable; ++i)
            _rgaridNonSelectable[i - 1] = _rgaridNonSelectable[i];
        --_caridNonSelectable;
        _rgaridNonSelectable[_caridNonSelectable] = aridNil;
    }
    else
    {
        _rgaridNonSelectable[_caridNonSelectable++] = arid;

        // A newly non-selectable object must stop being selected immediately.
        if (Pscen() != pvNil)
        {
            PACTR pactr = Pscen()->PactrFromArid(arid);
            if (pactr != pvNil && Pscen()->FActrSelected(arid))
            {
                if (Pscen()->CactrSelected() > 1)
                    Pscen()->SelectActrRemove(pactr);
                else
                    Pscen()->SelectActr(pvNil);
            }
        }
    }

    LightEditorLog(this, "object_selectability arid=%ld selectable=%d count=%ld undo=%d",
                   (long)arid, (int)fSelectable, (long)_caridNonSelectable, (int)fUndo);
    SetDirty();
    if (_pmcc != pvNil)
        _pmcc->UpdateRollCall();
    InvalViewsAndScb();
#if defined(KAUAI_WIN32)
    SyncExternalContentBrowserSelection();
#endif
    return fTrue;
}

bool MVIE::FToggleObjectSelectable(int32_t arid, bool fUndo)
{
    return FSetObjectSelectable(arid, !FObjectSelectable(arid), fUndo);
}

bool MVIE::FGetObjectProperties(int32_t iscen, int32_t arid, OBJECTPROPERTIES *pprop) const
{
    AssertThis(0);
    AssertVarMem(pprop);

    ClearPb(pprop, SIZEOF(*pprop));
    pprop->iscen = iscen;
    pprop->arid = arid;
    pprop->fFlushOverlap = fFalse;
    pprop->fCastShadows = fTrue;

    for (int32_t i = 0; i < _cObjectProperties; ++i)
    {
        if (_rgObjectProperties[i].iscen == iscen && _rgObjectProperties[i].arid == arid)
        {
            *pprop = _rgObjectProperties[i];
            return fTrue;
        }
    }
    return fTrue;
}

void MVIE::UpdateObjectShadowProperties(void)
{
    AssertThis(0);
    if (Pscen() == pvNil)
        return;

    PGL pglActor = Pscen()->PglRollCall();
    if (pglActor == pvNil)
        return;

    for (int32_t iactr = 0; iactr < pglActor->IvMac(); ++iactr)
    {
        PACTR pactr = pvNil;
        pglActor->Get(iactr, &pactr);
        if (pactr == pvNil || pactr->Pbody() == pvNil)
            continue;

        OBJECTPROPERTIES prop;
        FGetObjectProperties(Iscen(), pactr->Arid(), &prop);
        Set4DMMBodyObjectShadowProperties(pactr->Pbody(), prop.fCastShadows, prop.fFlushOverlap);
    }
}

bool MVIE::FSetObjectProperties(const OBJECTPROPERTIES *pprop, bool fUndo)
{
    AssertThis(0);
    AssertVarMem(pprop);
    if (pprop->arid == aridNil || pprop->iscen < 0)
        return fFalse;

    OBJECTPROPERTIES propNew = *pprop;
    propNew.fFlushOverlap = FPure(propNew.fFlushOverlap);
    propNew.fCastShadows = FPure(propNew.fCastShadows);

    int32_t iFound = ivNil;
    for (int32_t i = 0; i < _cObjectProperties; ++i)
    {
        if (_rgObjectProperties[i].iscen == propNew.iscen &&
            _rgObjectProperties[i].arid == propNew.arid)
        {
            iFound = i;
            break;
        }
    }

    const bool fDefault = !propNew.fFlushOverlap && propNew.fCastShadows;
    if (iFound == ivNil && fDefault)
        return fTrue;
    if (iFound != ivNil &&
        _rgObjectProperties[iFound].fFlushOverlap == propNew.fFlushOverlap &&
        _rgObjectProperties[iFound].fCastShadows == propNew.fCastShadows)
        return fTrue;
    if (iFound == ivNil && _cObjectProperties >= kc4DMMObjectPropertiesMax)
        return fFalse;

    if (fUndo && !FAddCameraTrackUndo(PszLit("Object Properties")))
        return fFalse;

    if (fDefault)
    {
        for (int32_t i = iFound + 1; i < _cObjectProperties; ++i)
            _rgObjectProperties[i - 1] = _rgObjectProperties[i];
        --_cObjectProperties;
        ClearPb(&_rgObjectProperties[_cObjectProperties], SIZEOF(OBJECTPROPERTIES));
    }
    else if (iFound != ivNil)
    {
        _rgObjectProperties[iFound] = propNew;
    }
    else
    {
        _rgObjectProperties[_cObjectProperties++] = propNew;
    }

    if (propNew.iscen == Iscen() && Pscen() != pvNil)
    {
        PACTR pactr = Pscen()->PactrFromArid(propNew.arid);
        if (pactr != pvNil && pactr->Pbody() != pvNil)
            Set4DMMBodyObjectShadowProperties(pactr->Pbody(), propNew.fCastShadows, propNew.fFlushOverlap);
    }

    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    SetDirty();
    InvalViewsAndScb();
    MarkViews();
    LightEditorLog(this, "object_properties scene=%ld arid=%ld flush_overlap=%d shadow_casting=%d undo=%d",
                   (long)propNew.iscen, (long)propNew.arid, (int)propNew.fFlushOverlap,
                   (int)propNew.fCastShadows, (int)fUndo);
    return fTrue;
}

void MVIE::SetDefaultMusicForNewScenes(bool fEnable)
{
    AssertThis(0);
    _fDefaultMusicForNewScenes = FPure(fEnable);
    SetDirty();
}

void MVIE::SetExperimental100xGrow(bool fEnable)
{
    AssertThis(0);
    _fExperimental100xGrow = FPure(fEnable);
    SetDirty();
}

void MVIE::SetExperimental100xShrink(bool fEnable)
{
    AssertThis(0);
    _fExperimental100xShrink = FPure(fEnable);
    SetDirty();
}

void MVIE::SetCameraMoveSpeed(float flSpeed)
{
    AssertThis(0);
    if (!(flSpeed >= 0.01f && flSpeed <= 10.0f))
        return;
    if (_flCameraMoveSpeed == flSpeed)
        return;
    _flCameraMoveSpeed = flSpeed;
    SetDirty();
}

void MVIE::SetCameraMouseSensitivity(float flSensitivity)
{
    AssertThis(0);
    if (!(flSensitivity >= 0.01f && flSensitivity <= 0.30f))
        return;
    if (_flCameraMouseSensitivity == flSensitivity)
        return;
    _flCameraMouseSensitivity = flSensitivity;
    SetDirty();
}

void MVIE::_ShowHiddenLightObjects(void)
{
    if (Pscen() != pvNil)
    {
        for (int32_t i = 0; i < _caridHiddenLightObjects; i++)
        {
            const int32_t arid = _rgaridHiddenLightObjects[i];
            PACTR pactr = Pscen()->PactrFromArid(arid);
            LightEditorLog(this, "light_object show arid=%ld actor_ptr=%p body_ptr=%p",
                           (long)arid, (void *)pactr,
                           pactr != pvNil ? (void *)pactr->Pbody() : pvNil);
            if (pactr != pvNil && pactr->Pbody() != pvNil && !pactr->Pbody()->FVisible())
                pactr->Show();
        }
    }
    _caridHiddenLightObjects = 0;
    ClearPb(_rgaridHiddenLightObjects, SIZEOF(_rgaridHiddenLightObjects));
}

void MVIE::_UpdateHiddenLightObjects(void)
{
    if (Pscen() == pvNil || !FSceneHideLightObjects(Iscen()))
    {
        _ShowHiddenLightObjects();
        return;
    }

    // First reveal attachments that no longer qualify.
    for (int32_t ih = _caridHiddenLightObjects - 1; ih >= 0; ih--)
    {
        int32_t arid = _rgaridHiddenLightObjects[ih];
        int32_t ilight = _ILightLabFind(Iscen(), arid);
        bool fWant = ilight != ivNil && _rglightLab[ilight].fAttachmentHideable &&
                     _FLightLabActiveAtCurrentFrame(_rglightLab[ilight]);
        if (!fWant)
        {
            PACTR pactr = Pscen()->PactrFromArid(arid);
            LightEditorLog(this, "light_object unhide_no_longer_eligible arid=%ld actor_ptr=%p body_ptr=%p",
                           (long)arid, (void *)pactr,
                           pactr != pvNil ? (void *)pactr->Pbody() : pvNil);
            if (pactr != pvNil && pactr->Pbody() != pvNil && !pactr->Pbody()->FVisible())
                pactr->Show();
            for (int32_t j = ih + 1; j < _caridHiddenLightObjects; j++)
                _rgaridHiddenLightObjects[j - 1] = _rgaridHiddenLightObjects[j];
            _caridHiddenLightObjects--;
        }
    }

    // Then add one Hide ref for each designated attachment not already ours.
    for (int32_t i = 0; i < _clightLab && _caridHiddenLightObjects < kclightLabMax; i++)
    {
        const LIGHTLAB &light = _rglightLab[i];
        if (light.iscen != Iscen() || light.arid == aridNil || !light.fAttachmentHideable ||
            !_FLightLabActiveAtCurrentFrame(light) || FIsLightAttachmentHidden(light.arid))
            continue;
        PACTR pactr = Pscen()->PactrFromArid(light.arid);
        if (pactr == pvNil || pactr->Pbody() == pvNil)
            continue;
        if (Pscen()->PactrSelected() == pactr)
            Pscen()->SelectActr(pvNil);
        LightEditorLog(this, "light_object hide arid=%ld actor_ptr=%p body_ptr=%p",
                       (long)light.arid, (void *)pactr, (void *)pactr->Pbody());
        pactr->Hide();
        _rgaridHiddenLightObjects[_caridHiddenLightObjects++] = light.arid;
    }
}

void MVIE::_RemoveInvalidLightLabForCurrentScene(void)
{
    if (Pscen() == pvNil)
        return;
    bool fRemoved = fFalse;
    for (int32_t i = _clightLab - 1; i >= 0; i--)
    {
        if (_rglightLab[i].iscen != Iscen())
            continue;
        if (_rglightLab[i].arid != aridNil && Pscen()->PactrFromArid(_rglightLab[i].arid) != pvNil)
            continue;
        for (int32_t j = i + 1; j < _clightLab; j++)
            _rglightLab[j - 1] = _rglightLab[j];
        _clightLab--;
        fRemoved = fTrue;
    }
    if (fRemoved)
    {
        SetDirty();
        _pmcc->UpdateRollCall();
    }
}

bool MVIE::FSetLightLabConfig(const LIGHTLAB *plight)
{
    AssertThis(0);
    AssertVarMem(plight);
    if (plight->arid == aridNil)
        return fFalse;
    if (plight->fGenerateShadows && !FLightLabShadowSlotAvailable(plight->iscen, plight->arid))
        return fFalse;
    int32_t i = _ILightLabFind(plight->iscen, plight->arid);
    if (i == ivNil && _clightLab >= kclightLabMax)
        return fFalse;

    LightEditorLog(this, "light_settings save arid=%ld enabled=%d shadows=%d hideable=%d spawn=%ld despawn=%ld intensity=%ld diameter=%.3f edge=%.3f range=%.3f slot=%ld",
                   (long)plight->arid, (int)plight->fEnabled, (int)plight->fGenerateShadows,
                   (int)plight->fAttachmentHideable,
                   (long)plight->nfrmSpawn, (long)plight->nfrmDespawn, (long)plight->intensity,
                   (double)plight->diameter, (double)plight->edgeGradient,
                   (double)(plight->range > 0.0f ? plight->range : 500.0f), (long)i);
    if (!FAddCameraTrackUndo(PszLit("Light Settings")))
        return fFalse;

    if (i == ivNil)
        i = _clightLab++;
    _rglightLab[i] = *plight;
    _rglightLab[i].nfrmSpawn = LwMax(1, _rglightLab[i].nfrmSpawn);
    _rglightLab[i].nfrmDespawn = LwMax(0, _rglightLab[i].nfrmDespawn);
    if (_rglightLab[i].range <= 0.0f)
        _rglightLab[i].range = 500.0f;
    SetDirty();
    if (_pmcc != pvNil)
        _pmcc->UpdateRollCall();
    _UpdateHiddenLightObjects();
    return fTrue;
}

bool MVIE::FRemoveLightLabConfigCore(int32_t iscen, int32_t arid)
{
    AssertThis(0);
    int32_t i = _ILightLabFind(iscen, arid);
    if (i == ivNil)
        return fFalse;
    for (int32_t j = i + 1; j < _clightLab; j++)
        _rglightLab[j - 1] = _rglightLab[j];
    _clightLab--;
    if (_clightLab >= 0 && _clightLab < kclightLabMax)
        ClearPb(&_rglightLab[_clightLab], SIZEOF(LIGHTLAB));
    SetDirty();
    RefreshTestLight();
    UpdateTestLightAttachment();
    if (_pmcc != pvNil)
        _pmcc->UpdateRollCall();
    MarkViews();
    return fTrue;
}

bool MVIE::FRestoreLightLabConfigCore(const LIGHTLAB *plight)
{
    AssertThis(0);
    AssertVarMem(plight);
    if (plight->arid == aridNil)
        return fFalse;
    if (plight->fGenerateShadows && !FLightLabShadowSlotAvailable(plight->iscen, plight->arid))
        return fFalse;
    int32_t i = _ILightLabFind(plight->iscen, plight->arid);
    if (i == ivNil)
    {
        if (_clightLab >= kclightLabMax)
            return fFalse;
        i = _clightLab++;
    }
    _rglightLab[i] = *plight;
    _rglightLab[i].nfrmSpawn = LwMax(1, _rglightLab[i].nfrmSpawn);
    _rglightLab[i].nfrmDespawn = LwMax(0, _rglightLab[i].nfrmDespawn);
    SetDirty();
    RefreshTestLight();
    UpdateTestLightAttachment();
    if (_pmcc != pvNil)
        _pmcc->UpdateRollCall();
    MarkViews();
    return fTrue;
}

void MVIE::_EnableTestLight(void)
{
    if (_fTestLightActive || !FSceneDynamicLightingActive() || _pbwld == pvNil ||
        !BWLD::FTrueColorMode() || !BWLD::FActorLightMode() ||
        Pscen() == pvNil || Pscen()->Pbkgd() == pvNil)
        return;

    // v36: dynamic Light Lab has no anonymous/default lamp. Every light must
    // bind to a real actor/prop/3D-word object in the current scene.
    for (int32_t i = 0; i < _clightLab; i++)
    {
        const LIGHTLAB &cfg = _rglightLab[i];
        if (cfg.iscen != Iscen() || !cfg.fEnabled || cfg.arid == aridNil ||
            !_FLightLabActiveAtCurrentFrame(cfg))
            continue;

        PACTR pactrLight = Pscen()->PactrFromArid(cfg.arid);
        if (pactrLight == pvNil || pactrLight->Pbody() == pvNil)
            continue;

        BACT *pbact = &_rgbactLightLab[i];
        BLIT *pblit = &_rgblitLightLab[i];
        ClearPb(pbact, SIZEOF(*pbact));
        ClearPb(pblit, SIZEOF(*pblit));
        pbact->type = BR_ACTOR_LIGHT;
        pbact->type_data = pblit;
        pbact->t.type = BR_TRANSFORM_MATRIX34;
        pactrLight->Pbody()->GetMatrix(&pbact->t.t.mat);
        for (int32_t iaxis = 0; iaxis < 3; iaxis++)
        {
            br_vector3 axis;
            axis.v[0] = pbact->t.t.mat.m[iaxis][0];
            axis.v[1] = pbact->t.t.mat.m[iaxis][1];
            axis.v[2] = pbact->t.t.mat.m[iaxis][2];
            BrVector3Normalise(&axis, &axis);
            pbact->t.t.mat.m[iaxis][0] = axis.v[0];
            pbact->t.t.mat.m[iaxis][1] = axis.v[1];
            pbact->t.t.mat.m[iaxis][2] = axis.v[2];
        }

#if defined(BRENDER_MODERN_14)
        pblit->type = BR_LIGHT_VIEW | BR_LIGHT_SPOT | BR_LIGHT_LINEAR_FALLOFF;
        if (cfg.fGenerateShadows && FShadowMode())
            pblit->type |= BR_LIGHT_SHADOW;
#else
        pblit->type = BR_LIGHT_VIEW | BR_LIGHT_SPOT;
#endif
        const int32_t bLight = LwMin(255, LwMax(1, LwMulDivAway(cfg.intensity, 255, 100)));
        pblit->colour = BR_COLOUR_RGB(bLight, bLight, bLight);
#if defined(BRENDER_MODERN_14)
        // Modern glrend supports real radial light ranges. Keep the authored
        // intensity independent from distance and use an 80%-to-100% feather.
        const float flRange = cfg.range > 0.0f ? cfg.range : 500.0f;
        pblit->attenuation_c = BR_SCALAR(1.0);
        pblit->attenuation_l = BR_SCALAR(0.0);
        pblit->attenuation_q = BR_SCALAR(0.0);
        pblit->radius_inner = BR_SCALAR(flRange * 0.80f);
        pblit->radius_outer = BR_SCALAR(flRange);
#else
        pblit->attenuation_c = BR_SCALAR(0.35);
        pblit->attenuation_l = BR_SCALAR(0.0020);
        pblit->attenuation_q = BR_SCALAR(0.00005);
#endif
        const float flOuterHalf = cfg.diameter * 0.5f;
        const float flInnerCandidate = flOuterHalf - cfg.edgeGradient;
        const float flInnerHalf = flInnerCandidate > 0.0f ? flInnerCandidate : 0.0f;
        pblit->cone_inner = BR_ANGLE_DEG(flInnerHalf);
        pblit->cone_outer = BR_ANGLE_DEG(flOuterHalf);

#if defined(BRENDER_MODERN_14)
        if (cfg.fGenerateShadows && FShadowMode() && FBrShadowLogEnabled())
        {
            static uint32_t cShadowLightDiag = 0;
            if (cShadowLightDiag < 96)
            {
                BrShadowLog(
                    "SHADOW APP light_install n=%lu scene=%ld frame=%ld arid=%ld range=%.3f outer_half=%.3f inner_half=%.3f "
                    "m0=(%.6g,%.6g,%.6g) m1=(%.6g,%.6g,%.6g) m2=(%.6g,%.6g,%.6g) t=(%.6g,%.6g,%.6g)",
                    (unsigned long)cShadowLightDiag++, (long)Iscen(),
                    Pscen() != pvNil ? (long)Pscen()->Nfrm() : -1L, (long)cfg.arid,
                    (double)(cfg.range > 0.0f ? cfg.range : 500.0f), (double)flOuterHalf, (double)flInnerHalf,
                    (double)BrScalarToFloat(pbact->t.t.mat.m[0][0]),
                    (double)BrScalarToFloat(pbact->t.t.mat.m[0][1]),
                    (double)BrScalarToFloat(pbact->t.t.mat.m[0][2]),
                    (double)BrScalarToFloat(pbact->t.t.mat.m[1][0]),
                    (double)BrScalarToFloat(pbact->t.t.mat.m[1][1]),
                    (double)BrScalarToFloat(pbact->t.t.mat.m[1][2]),
                    (double)BrScalarToFloat(pbact->t.t.mat.m[2][0]),
                    (double)BrScalarToFloat(pbact->t.t.mat.m[2][1]),
                    (double)BrScalarToFloat(pbact->t.t.mat.m[2][2]),
                    (double)BrScalarToFloat(pbact->t.t.mat.m[3][0]),
                    (double)BrScalarToFloat(pbact->t.t.mat.m[3][1]),
                    (double)BrScalarToFloat(pbact->t.t.mat.m[3][2]));
            }
        }
#endif

        _pbwld->AddActor(pbact);
        BrLightEnable(pbact);
        _rgfLightLabActive[i] = fTrue;
        _fTestLightActive = fTrue;
    }
    if (_fTestLightActive)
        _pbwld->MarkDirty();
}

void MVIE::_DisableTestLight(void)
{
    if (!_fTestLightActive)
        return;

    for (int32_t i = 0; i < kclightLabMax; i++)
    {
        if (_rgfLightLabActive[i])
        {
            BrLightDisable(&_rgbactLightLab[i]);
            BrActorRemove(&_rgbactLightLab[i]);
            _rgfLightLabActive[i] = fFalse;
        }
    }

    ClearPb(&_bactTestLight, SIZEOF(_bactTestLight));
    ClearPb(&_blitTestLight, SIZEOF(_blitTestLight));
    _fTestLightActive = fFalse;
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
}

void MVIE::RefreshTestLight(void)
{
#if defined(BRENDER_MODERN_14)
    // Light Lab attachment objects are authoring handles, not physical shadow
    // casters. Rebuild the marker policy from the authoritative current-scene
    // roll call whenever lights are refreshed. Clearing first also repairs
    // stale markers after Undo/Redo, actor replacement, or removing Light Lab
    // metadata from an object.
    int32_t cShadowExcluded = 0;
    if (Pscen() != pvNil)
    {
        PGL pglActor = Pscen()->PglRollCall();
        if (pglActor != pvNil)
        {
            for (int32_t iactr = 0; iactr < pglActor->IvMac(); ++iactr)
            {
                PACTR pactr = pvNil;
                pglActor->Get(iactr, &pactr);
                if (pactr != pvNil && pactr->Pbody() != pvNil)
                    Set4DMMBodyShadowCasterExcluded(pactr->Pbody(), fFalse);
            }
        }

        for (int32_t i = 0; i < _clightLab; ++i)
        {
            const LIGHTLAB &cfg = _rglightLab[i];
            if (cfg.iscen != Iscen() || cfg.arid == aridNil)
                continue;
            PACTR pactrLightObject = Pscen()->PactrFromArid(cfg.arid);
            if (pactrLightObject == pvNil || pactrLightObject->Pbody() == pvNil)
                continue;
            Set4DMMBodyShadowCasterExcluded(pactrLightObject->Pbody(), fTrue);
            ++cShadowExcluded;
        }

        // Reapply user Object Properties after any BODY replacement/scene load.
        // The BODY bridge preserves the system Light Object exclusion bit.
        UpdateObjectShadowProperties();
    }
    if (FShadowMode() && FBrShadowLogEnabled())
        BrShadowLog("SHADOW APP caster_policy scene=%ld light_objects_excluded=%ld",
                    (long)Iscen(), (long)cShadowExcluded);
#endif

    _DisableTestLight();
    if (FSceneDynamicLightingActive())
        _EnableTestLight();
}

void MVIE::UpdateTestLightAttachment(void)
{
    if (Pscen() == pvNil)
        return;

    // Some scene actors do not have a BODY yet at the first scene-load retry.
    // If a saved Light Lab record was skipped for that reason, activate it as
    // soon as its actor body becomes available.  This turns the old accidental
    // "first viewport click wakes the remaining lights" behavior into an idle
    // self-heal with no click required.
    bool fRefreshForFrame = fFalse;
    bool fMissingReadyLight = fFalse;
    if (FSceneDynamicLightingActive() && BWLD::FTrueColorMode() && BWLD::FActorLightMode())
    {
        for (int32_t i = 0; i < _clightLab; i++)
        {
            const LIGHTLAB &cfg = _rglightLab[i];
            if (cfg.iscen != Iscen())
                continue;

            const bool fShouldBeActive = cfg.fEnabled && cfg.arid != aridNil &&
                                         _FLightLabActiveAtCurrentFrame(cfg);
            if (FPure(_rgfLightLabActive[i]) != fShouldBeActive)
            {
                fRefreshForFrame = fTrue;
                break;
            }
            if (!fShouldBeActive || _rgfLightLabActive[i])
                continue;

            PACTR pactrLight = Pscen()->PactrFromArid(cfg.arid);
            if (pactrLight != pvNil && pactrLight->Pbody() != pvNil)
            {
                fMissingReadyLight = fTrue;
                break;
            }
        }
    }

    if (fRefreshForFrame || fMissingReadyLight)
        RefreshTestLight();

    _UpdateHiddenLightObjects();

    if (!_fTestLightActive)
        return;

    bool fChanged = fFalse;
    for (int32_t i = 0; i < _clightLab; i++)
    {
        if (!_rgfLightLabActive[i] || _rglightLab[i].iscen != Iscen())
            continue;
        PACTR pactrLight = Pscen()->PactrFromArid(_rglightLab[i].arid);
        if (pactrLight == pvNil || pactrLight->Pbody() == pvNil)
            continue;

        BMAT34 mat;
        pactrLight->Pbody()->GetMatrix(&mat);

        // Bound Object Groups render through one shared BRender parent. The
        // BODY root is therefore group-local even though ordinary ACTR state
        // continues to carry the corresponding logical world pose. Light Lab
        // follows the rendered attachment, so compose that parent here. This
        // makes light-object members move and rotate rigidly with their OG
        // instead of leaving the actual light behind while its visible prop
        // moves with the group.
        int32_t idLightGroup = 0;
        if (FObjectInObjectGroup(_rglightLab[i].arid, &idLightGroup))
        {
            for (int32_t iGroup = 0; iGroup < _cObjectGroup; ++iGroup)
            {
                const OBJECTGROUP &group = _rgObjectGroup[iGroup];
                if (group.id != idLightGroup || group.iscen != Iscen() || group.pbactParent == pvNil)
                    continue;
                BMAT34 matWorld;
                BrMatrix34Mul(&matWorld, &mat, &group.pbactParent->t.t.mat);
                mat = matWorld;
                break;
            }
        }
        for (int32_t iaxis = 0; iaxis < 3; iaxis++)
        {
            br_vector3 axis;
            axis.v[0] = mat.m[iaxis][0];
            axis.v[1] = mat.m[iaxis][1];
            axis.v[2] = mat.m[iaxis][2];
            BrVector3Normalise(&axis, &axis);
            mat.m[iaxis][0] = axis.v[0];
            mat.m[iaxis][1] = axis.v[1];
            mat.m[iaxis][2] = axis.v[2];
        }
        if (memcmp(&_rgbactLightLab[i].t.t.mat, &mat, SIZEOF(BMAT34)) != 0)
        {
            if (!FPlaying())
            {
                LightEditorLog(this,
                               "light_attachment move arid=%ld slot=%ld pos_raw=(%ld,%ld,%ld) colour=0x%08lX",
                               (long)_rglightLab[i].arid, (long)i,
                               (long)mat.m[3][0], (long)mat.m[3][1], (long)mat.m[3][2],
                               (unsigned long)_rgblitLightLab[i].colour);
            }
            _rgbactLightLab[i].t.t.mat = mat;
            fChanged = fTrue;
        }
    }
    if (fChanged && _pbwld != pvNil)
        _pbwld->MarkDirty();
}

void MVIE::ActorBodyReplaced(int32_t arid)
{
    AssertThis(0);
    // Hidden-light bookkeeping is keyed by arid, but actor undo/redo replaces
    // the BODY instance behind that arid. Drop our ownership marker without
    // Show()ing the replacement, then let _UpdateHiddenLightObjects add one
    // hide reference to the new BODY when the scene policy requires it.
    for (int32_t ih = 0; ih < _caridHiddenLightObjects; ih++)
    {
        if (_rgaridHiddenLightObjects[ih] != arid)
            continue;
        for (int32_t j = ih + 1; j < _caridHiddenLightObjects; j++)
            _rgaridHiddenLightObjects[j - 1] = _rgaridHiddenLightObjects[j];
        _caridHiddenLightObjects--;
        if (_caridHiddenLightObjects >= 0 && _caridHiddenLightObjects < kclightLabMax)
            _rgaridHiddenLightObjects[_caridHiddenLightObjects] = aridNil;
        break;
    }
    LightEditorLog(this, "actor_body_replaced arid=%ld has_light=%d hide_mode=%d",
                   (long)arid, (int)FActorHasLight(Iscen(), arid),
                   (int)FSceneHideLightObjects(Iscen()));
    _UpdateHiddenLightObjects();
    // The replacement BODY has a fresh BRender root, so rebuild both the
    // no-shadow Light Object marker and the actual Light Lab actor instead of
    // relying on the per-frame attachment updater to notice enough state.
    RefreshTestLight();
    UpdateTestLightAttachment();
}

/** 3DMMv1.0: **************************************************
 *
 * Constructor for movies.  This function is private, use PmvieNew()
 * for public construction.
 *
 ****************************************************/
MVIE::MVIE(void) : _clok(khidMvieClock)
{
    _aridLim = 0;
    _arid4DMMActorStudioPreaddedPlacement = aridNil;
    _cno = cnoNil;
    _iscen = ivNil;
    _wit = witNil;
    _trans = transNil;
    _vlmOrg = 0;
    _fTestLightActive = fFalse;
    ClearPb(&_bactTestLight, SIZEOF(_bactTestLight));
    ClearPb(&_blitTestLight, SIZEOF(_blitTestLight));
    _clightLab = 0;
    ClearPb(_rglightLab, SIZEOF(_rglightLab));
    _cObjectGroup = 0;
    _cObjectGroupMember = 0;
    _idObjectGroupNext = 1;
    ClearPb(_rgObjectGroup, SIZEOF(_rgObjectGroup));
    ClearPb(_rgObjectGroupMember, SIZEOF(_rgObjectGroupMember));
    _c4DMMCustomObject = 0;
    _id4DMMCustomObjectNext = 1;
    _c4DMMCustomPart = 0;
    _c4DMMCustomAction = 0;
    _fRequiresVmmSave = fFalse;
    ClearPb(_rg4DMMCustomObject, SIZEOF(_rg4DMMCustomObject));
    ClearPb(_rg4DMMCustomPart, SIZEOF(_rg4DMMCustomPart));
    ClearPb(_rg4DMMCustomAction, SIZEOF(_rg4DMMCustomAction));
    ClearPb(_rgbactLightLab, SIZEOF(_rgbactLightLab));
    ClearPb(_rgblitLightLab, SIZEOF(_rgblitLightLab));
    ClearPb(_rgfLightLabActive, SIZEOF(_rgfLightLabActive));
    ClearPb(_rgfSceneLightsEnabled, SIZEOF(_rgfSceneLightsEnabled));
    ClearPb(_rgfSceneHideLightObjects, SIZEOF(_rgfSceneHideLightObjects));
    ClearPb(_rgfSceneDefaultLightingShaders, SIZEOF(_rgfSceneDefaultLightingShaders));
    ClearPb(_rgfSceneLightLabCombineLegacy, SIZEOF(_rgfSceneLightLabCombineLegacy));
    _caridNonSelectable = 0;
    ClearPb(_rgaridNonSelectable, SIZEOF(_rgaridNonSelectable));
    _cObjectProperties = 0;
    ClearPb(_rgObjectProperties, SIZEOF(_rgObjectProperties));
    _fDefaultLightingShaders = fTrue;
    _fLightLabCombineLegacy = fFalse;
    _fFreeLookAlwaysLastKnown = fFalse;
    _fDontAskLightLabCut = fFalse;
    _fDontAskShaderPropagation = fFalse;
    _fDefaultMusicForNewScenes = fFalse;
    _fExperimental100xGrow = fFalse;
    _fExperimental100xShrink = fFalse;
    _flCameraMoveSpeed = 1.0f;
    _flCameraMouseSensitivity = kfl4DMMDefaultCameraMouseSensitivity;
    _caridHiddenLightObjects = 0;
    ClearPb(_rgaridHiddenLightObjects, SIZEOF(_rgaridHiddenLightObjects));
    _cctween = 0;
    _cctman = 0;
    _fManualCameraMode = fFalse;
    _fManualCameraRecording = fFalse;
    _fManualCameraRecordLiveValid = fFalse;
    _fManualCameraRecordExitMode = fFalse;
    _fManualCameraEditValid = fFalse;
    _fManualCameraEditHadSample = fFalse;
    _fFreeLookMode = fFalse;
    _fFreeLookOverride = fFalse;
    _fBrowserCameraFollow = fFalse;
    ClearPb(&_ctmanManualCameraRecordLive, SIZEOF(CTMAN));
    ClearPb(&_ctmanManualCameraEditStart, SIZEOF(CTMAN));
    ClearPb(&_ctmanManualCameraEditLive, SIZEOF(CTMAN));
    _iscenManualCameraEdit = ivNil;
    _nfrmManualCameraEdit = ivNil;
    ClearPb(&_ctmanFreeLookStart, SIZEOF(CTMAN));
    ClearPb(&_ctmanFreeLookLive, SIZEOF(CTMAN));
    _iscenFreeLook = ivNil;
    _nfrmFreeLook = ivNil;
    _aridBrowserCameraFollow = aridNil;
    _iscenManualCameraUndo = ivNil;
    _nfrmManualCameraUndo = ivNil;
    _stnCameraTrackPath.SetNil();

    int32_t fPlaybackOnly = fFalse;
    int32_t fUndoHistoryProp = fFalse;
    vpappb->FGetProp(kpridPlaybackOnly, &fPlaybackOnly);
    vpappb->FGetProp(kpridUndoHistory, &fUndoHistoryProp);

    // The direct process-wide flag is authoritative.  Keep the property as a
    // compatibility fallback for any existing script path that sets it.
    bool fUndoHistory = FExtendedUndoMode() || FPure(fUndoHistoryProp);

    if (fUndoHistory && !fPlaybackOnly)
    {
        SetCundbMax(50);
        EnableUndoHistoryWindow();
    }
    else
    {
        // Preserve original 3DMM behavior unless -u was explicitly supplied.
        SetCundbMax(1);
    }
}

/******************************************************************************
    _LoadCameraTrack

    Load a matching plain-text .3ct sidecar.  Version 1 format:

        3CT 1
        scene 1
        depth motion tween
        frame 1 z 0.0
        frame 60 z 20.0

    Scene numbers are one-based to match the 3DMM interface.  The legacy
    early-test spelling "scene 0" is still accepted as the first scene.
    Frame numbers are one-based.
******************************************************************************/
void MVIE::_LoadCameraTrack(PFNI pfni)
{
    AssertBaseThis(0);
    AssertPo(pfni, 0);

    STN stnPath;
    FILE *pfile;
    achar rgch[1024];
    int32_t ich;
    int32_t iscen = ivNil;
    int32_t nfrm;
    int32_t iscenT;
    int32_t version;
    float value;
    bool fHeader = fFalse;
    bool fSceneBaseKnown = fFalse;
    bool fLegacySceneZeroBased = fFalse;
    int32_t section = 0; // 0=legacy/depth, 1=depth, 2=manual, 3=Light Lab, 4=movie settings, 5=Object Group
    CTMAN *pctmanLast = pvNil;
    LIGHTLAB *plightLast = pvNil;
    OBJECTGROUP *pgroupLast = pvNil;
    OBJECTGROUPMEMBER *pmemberLast = pvNil;
    bool fManualImplicitNext = fFalse;
    CTTWEEN ctweenPending;
    bool fTweenPending = fFalse;
    int32_t cframeTween = 0;
    int32_t idTweenNext = 0;
    bool rgfSceneLightsSettingSeen[kc4DMMSceneSettingsMax];
    bool rgfSceneDefaultShadersSettingSeen[kc4DMMSceneSettingsMax];
    bool rgfSceneLightLabCombineSettingSeen[kc4DMMSceneSettingsMax];
    ClearPb(rgfSceneLightsSettingSeen, SIZEOF(rgfSceneLightsSettingSeen));
    ClearPb(rgfSceneDefaultShadersSettingSeen, SIZEOF(rgfSceneDefaultShadersSettingSeen));
    ClearPb(rgfSceneLightLabCombineSettingSeen, SIZEOF(rgfSceneLightLabCombineSettingSeen));

    auto FinalizeTween = [&]()
    {
        if (fTweenPending && cframeTween >= 2 &&
            ctweenPending.nfrmFirst < ctweenPending.nfrmLast &&
            _cctween < kcctweenMax)
        {
            _rgctween[_cctween++] = ctweenPending;
        }
        ClearPb(&ctweenPending, SIZEOF(ctweenPending));
        fTweenPending = fFalse;
        cframeTween = 0;
    };

    _cctween = 0;
    _cctman = 0;
    _clightLab = 0;
    // Runtime BRender parents contain BODY roots from the currently loaded
    // scene. Tear them down before replacing the sidecar group tables.
    _DestroyObjectGroupRenderParents();
    _cObjectGroup = 0;
    _cObjectGroupMember = 0;
    _idObjectGroupNext = 1;
    ClearPb(_rglightLab, SIZEOF(_rglightLab));
    ClearPb(_rgObjectGroup, SIZEOF(_rgObjectGroup));
    ClearPb(_rgObjectGroupMember, SIZEOF(_rgObjectGroupMember));
    _c4DMMCustomObject = 0;
    _id4DMMCustomObjectNext = 1;
    _c4DMMCustomPart = 0;
    _c4DMMCustomAction = 0;
    _fRequiresVmmSave = fFalse;
    ClearPb(_rg4DMMCustomObject, SIZEOF(_rg4DMMCustomObject));
    ClearPb(_rg4DMMCustomPart, SIZEOF(_rg4DMMCustomPart));
    ClearPb(_rg4DMMCustomAction, SIZEOF(_rg4DMMCustomAction));
    ClearPb(_rgfSceneLightsEnabled, SIZEOF(_rgfSceneLightsEnabled));
    ClearPb(_rgfSceneHideLightObjects, SIZEOF(_rgfSceneHideLightObjects));
    ClearPb(_rgfSceneDefaultLightingShaders, SIZEOF(_rgfSceneDefaultLightingShaders));
    ClearPb(_rgfSceneLightLabCombineLegacy, SIZEOF(_rgfSceneLightLabCombineLegacy));
    _caridNonSelectable = 0;
    ClearPb(_rgaridNonSelectable, SIZEOF(_rgaridNonSelectable));
    _cObjectProperties = 0;
    ClearPb(_rgObjectProperties, SIZEOF(_rgObjectProperties));
    _fDefaultLightingShaders = fTrue;
    _fLightLabCombineLegacy = fFalse;
    _fFreeLookAlwaysLastKnown = fFalse;
    _fDontAskLightLabCut = fFalse;
    _fDontAskShaderPropagation = fFalse;
    _fDefaultMusicForNewScenes = fFalse;
    _fExperimental100xGrow = fFalse;
    _fExperimental100xShrink = fFalse;
    _flCameraMoveSpeed = 1.0f;
    _flCameraMouseSensitivity = kfl4DMMDefaultCameraMouseSensitivity;
    _fManualCameraMode = fFalse;
    _fManualCameraRecording = fFalse;
    _fManualCameraRecordLiveValid = fFalse;
    _fManualCameraRecordExitMode = fFalse;
    _fManualCameraEditValid = fFalse;
    _fManualCameraEditHadSample = fFalse;
    _fFreeLookMode = fFalse;
    _fFreeLookOverride = fFalse;
    _fBrowserCameraFollow = fFalse;
    ClearPb(&_ctmanManualCameraRecordLive, SIZEOF(CTMAN));
    ClearPb(&_ctmanManualCameraEditStart, SIZEOF(CTMAN));
    ClearPb(&_ctmanManualCameraEditLive, SIZEOF(CTMAN));
    _iscenManualCameraEdit = ivNil;
    _nfrmManualCameraEdit = ivNil;
    ClearPb(&_ctmanFreeLookStart, SIZEOF(CTMAN));
    ClearPb(&_ctmanFreeLookLive, SIZEOF(CTMAN));
    _iscenFreeLook = ivNil;
    _nfrmFreeLook = ivNil;
    _aridBrowserCameraFollow = aridNil;
    _iscenManualCameraUndo = ivNil;
    _nfrmManualCameraUndo = ivNil;
    _stnCameraTrackPath.SetNil();
    for (int32_t i = 0; i < LwMin(Cscen(), kc4DMMSceneSettingsMax); ++i)
    {
        _rgfSceneDefaultLightingShaders[i] = _fDefaultLightingShaders;
        _rgfSceneLightLabCombineLegacy[i] = _fLightLabCombineLegacy;
    }
    pfni->GetStnPath(&stnPath);

    for (ich = stnPath.Cch() - 1; ich >= 0; ich--)
    {
        if (stnPath.Psz()[ich] == ChLit('.'))
        {
            stnPath.Delete(ich);
            break;
        }
        if (stnPath.Psz()[ich] == ChLit('\\') || stnPath.Psz()[ich] == ChLit('/'))
            break;
    }
    if (!stnPath.FAppendSz(PszLit(".3ct")))
        return;

    _stnCameraTrackPath = stnPath;
    pfile = fopen(stnPath.Psz(), "rt");
    if (pfile == pvNil)
        return;

    while (fgets(rgch, SIZEOF(rgch), pfile) != pvNil)
    {
        achar *pch = rgch;
        while (*pch == ChLit(' ') || *pch == ChLit('\t'))
            pch++;
        if (*pch == 0 || *pch == ChLit('\r') || *pch == ChLit('\n') ||
            *pch == ChLit('#') || *pch == ChLit(';'))
        {
            continue;
        }

        if (!fHeader)
        {
            if (sscanf(pch, "3CT %d", &version) != 1 || version != 1)
            {
                _cctween = 0;
                _cctman = 0;
                _cObjectGroup = 0;
                _cObjectGroupMember = 0;
                break;
            }
            fHeader = fTrue;
            continue;
        }

        if (sscanf(pch, "scene %d", &iscenT) == 1)
        {
            FinalizeTween();
            if (!fSceneBaseKnown)
            {
                fLegacySceneZeroBased = (iscenT == 0);
                fSceneBaseKnown = fTrue;
            }
            iscen = fLegacySceneZeroBased ? (iscenT >= 0 ? iscenT : ivNil)
                                           : (iscenT >= 1 ? iscenT - 1 : ivNil);
            section = 0;
            pctmanLast = pvNil;
            plightLast = pvNil;
            pgroupLast = pvNil;
            pmemberLast = pvNil;
            fManualImplicitNext = fFalse;
            continue;
        }

        if (strncmp(pch, "movie settings", 14) == 0)
        {
            FinalizeTween();
            section = 4;
            iscen = ivNil;
            pctmanLast = pvNil;
            plightLast = pvNil;
            pgroupLast = pvNil;
            pmemberLast = pvNil;
            continue;
        }

        if (section == 4)
        {
            int n = 0;
            float flCameraMoveSpeed = 0.0f;
            float flCameraMouseSensitivity = 0.0f;
            if (sscanf(pch, "default music new scenes %d", &n) == 1)
                _fDefaultMusicForNewScenes = n != 0;
            else if (sscanf(pch, "experimental 100x grow %d", &n) == 1)
                _fExperimental100xGrow = n != 0;
            else if (sscanf(pch, "experimental 100x shrink %d", &n) == 1)
                _fExperimental100xShrink = n != 0;
            else if (sscanf(pch, "camera movement speed %f", &flCameraMoveSpeed) == 1)
            {
                if (flCameraMoveSpeed >= 0.01f && flCameraMoveSpeed <= 10.0f)
                    _flCameraMoveSpeed = flCameraMoveSpeed;
            }
            else if (sscanf(pch, "camera mouse sensitivity %f", &flCameraMouseSensitivity) == 1)
            {
                if (flCameraMouseSensitivity >= 0.01f && flCameraMouseSensitivity <= 0.30f)
                    _flCameraMouseSensitivity = flCameraMouseSensitivity;
            }
            else if (sscanf(pch, "default lighting shaders %d", &n) == 1)
                _fDefaultLightingShaders = n != 0;
            else if (sscanf(pch, "light lab combine legacy lighting %d", &n) == 1)
                _fLightLabCombineLegacy = n != 0;
            else if (sscanf(pch, "freecam always last known %d", &n) == 1)
                _fFreeLookAlwaysLastKnown = n != 0;
            else if (sscanf(pch, "dont ask light lab cut %d", &n) == 1)
                _fDontAskLightLabCut = n != 0;
            else if (sscanf(pch, "dont ask shader propagation %d", &n) == 1)
                _fDontAskShaderPropagation = n != 0;
            else if (sscanf(pch, "non-selectable object %d", &n) == 1)
            {
                if (n >= 0 && _caridNonSelectable < kc4DMMNonSelectableMax && FObjectSelectable(n))
                    _rgaridNonSelectable[_caridNonSelectable++] = n;
            }
            else if (strncmp(pch, "custom object v1 ", 17) == 0 && _c4DMMCustomObject < kc4DMMCustomObjectMax)
            {
                int id = 0, fProp = 0, templateKind = 0, idTemplateCustom = 0;
                int sidTemplate = 0, cnoTemplate = 0, cnoOwnedTmpl = cnoNil;
                unsigned long ctgTemplate = 0;
                achar szName[kcch4DMMCustomName];
                ClearPb(szName, SIZEOF(szName));
                if (sscanf(pch + 17, "%d,%d,%d,%d,%d,%lu,%d,%d,%63[^\r\n]",
                           &id, &fProp, &templateKind, &idTemplateCustom, &sidTemplate,
                           &ctgTemplate, &cnoTemplate, &cnoOwnedTmpl, szName) == 9 && id > 0)
                {
                    CUSTOMOBJECT *pobj = &_rg4DMMCustomObject[_c4DMMCustomObject++];
                    ClearPb(pobj, SIZEOF(*pobj));
                    pobj->id = id;
                    pobj->fProp = fProp != 0;
                    pobj->templateKind = templateKind;
                    pobj->idTemplateCustom = idTemplateCustom;
                    pobj->sidTemplate = sidTemplate;
                    pobj->ctgTemplate = (CTG)ctgTemplate;
                    pobj->cnoTemplate = (CNO)cnoTemplate;
                    pobj->cnoOwnedTmpl = (CNO)cnoOwnedTmpl;
                    strcpy_s(pobj->szName, SIZEOF(pobj->szName), szName);
                    _id4DMMCustomObjectNext = LwMax(_id4DMMCustomObjectNext, id + 1);
                    _fRequiresVmmSave = fTrue;
                }
            }
            else if (strncmp(pch, "custom part v3 ", 15) == 0 && _c4DMMCustomPart < kc4DMMCustomPartMax)
            {
                CUSTOMPART part;
                achar szGroup[kcch4DMMCustomName];
                achar szObject[kcch4DMMCustomName];
                int fSourceTdt = 0;
                ClearPb(&part, SIZEOF(part));
                ClearPb(szGroup, SIZEOF(szGroup));
                ClearPb(szObject, SIZEOF(szObject));
                if (sscanf(pch + 15, "%d,%d,%d,%d,%d,%d,%d,%d|%63[^|]|%63[^\r\n]",
                           &part.idObject, &part.iscen, &part.idGroup, &part.idImport,
                           &part.aridSource, &part.ipartFirst, &part.cpart, &fSourceTdt,
                           szGroup, szObject) == 10 &&
                    part.idObject > 0 && part.idImport > 0 &&
                    part.ipartFirst >= 0 && part.cpart > 0)
                {
                    part.fSourceTdt = fSourceTdt != 0;
                    if (0 != strcmp(szGroup, "-"))
                        strcpy_s(part.szGroupName, SIZEOF(part.szGroupName), szGroup);
                    strcpy_s(part.szObjectName, SIZEOF(part.szObjectName), szObject);
                    _rg4DMMCustomPart[_c4DMMCustomPart++] = part;
                    _fRequiresVmmSave = fTrue;
                }
            }
            else if (strncmp(pch, "custom part v2 ", 15) == 0 && _c4DMMCustomPart < kc4DMMCustomPartMax)
            {
                CUSTOMPART part;
                achar szGroup[kcch4DMMCustomName];
                achar szObject[kcch4DMMCustomName];
                ClearPb(&part, SIZEOF(part));
                ClearPb(szGroup, SIZEOF(szGroup));
                ClearPb(szObject, SIZEOF(szObject));
                if (sscanf(pch + 15, "%d,%d,%d,%d,%d,%d,%d|%63[^|]|%63[^\r\n]",
                           &part.idObject, &part.iscen, &part.idGroup, &part.idImport,
                           &part.aridSource, &part.ipartFirst, &part.cpart,
                           szGroup, szObject) == 9 &&
                    part.idObject > 0 && part.idImport > 0 &&
                    part.ipartFirst >= 0 && part.cpart > 0)
                {
                    if (0 != strcmp(szGroup, "-"))
                        strcpy_s(part.szGroupName, SIZEOF(part.szGroupName), szGroup);
                    strcpy_s(part.szObjectName, SIZEOF(part.szObjectName), szObject);
                    _rg4DMMCustomPart[_c4DMMCustomPart++] = part;
                    _fRequiresVmmSave = fTrue;
                }
            }
            else if (strncmp(pch, "custom part v1 ", 15) == 0 && _c4DMMCustomPart < kc4DMMCustomPartMax)
            {
                CUSTOMPART part;
                ClearPb(&part, SIZEOF(part));
                if (sscanf(pch + 15, "%d,%d,%d", &part.idObject, &part.iscen, &part.idGroup) == 3 &&
                    part.idObject > 0 && part.idGroup >= 0)
                {
                    _rg4DMMCustomPart[_c4DMMCustomPart++] = part;
                    _fRequiresVmmSave = fTrue;
                }
            }
            else if (strncmp(pch, "custom action v1 ", 17) == 0 && _c4DMMCustomAction < kc4DMMCustomActionMax)
            {
                int cnoTmpl = 0, anid = 0;
                achar szName[kcch4DMMCustomName];
                ClearPb(szName, SIZEOF(szName));
                if (sscanf(pch + 17, "%d,%d,%63[^\r\n]", &cnoTmpl, &anid, szName) == 3 &&
                    cnoTmpl >= 0 && anid >= 0)
                {
                    CUSTOMACTION *paction = &_rg4DMMCustomAction[_c4DMMCustomAction++];
                    ClearPb(paction, SIZEOF(*paction));
                    paction->cnoTmpl = (CNO)cnoTmpl;
                    paction->anid = anid;
                    strcpy_s(paction->szName, SIZEOF(paction->szName), szName);
                    _fRequiresVmmSave = fTrue;
                }
            }
            continue;
        }

        if (iscen >= 0 && iscen < kc4DMMSceneSettingsMax)
        {
            int n = 0;
            if (sscanf(pch, "lights enabled %d", &n) == 1)
            {
                _rgfSceneLightsEnabled[iscen] = n != 0;
                rgfSceneLightsSettingSeen[iscen] = fTrue;
                continue;
            }
            if (sscanf(pch, "hide light attachments %d", &n) == 1)
            {
                _rgfSceneHideLightObjects[iscen] = n != 0;
                continue;
            }
            if (sscanf(pch, "default lighting shaders %d", &n) == 1)
            {
                _rgfSceneDefaultLightingShaders[iscen] = n != 0;
                rgfSceneDefaultShadersSettingSeen[iscen] = fTrue;
                continue;
            }
            if (sscanf(pch, "light lab combine legacy lighting %d", &n) == 1)
            {
                _rgfSceneLightLabCombineLegacy[iscen] = n != 0;
                rgfSceneLightLabCombineSettingSeen[iscen] = fTrue;
                continue;
            }
        }

        if (iscen >= 0 && strncmp(pch, "object properties v1 ", 21) == 0 &&
            _cObjectProperties < kc4DMMObjectPropertiesMax)
        {
            int arid = aridNil;
            int fFlush = 0;
            int fCast = 1;
            if (sscanf(pch + 21, "%d,%d,%d", &arid, &fFlush, &fCast) == 3 &&
                arid != aridNil && (fFlush == 0 || fFlush == 1) && (fCast == 0 || fCast == 1))
            {
                OBJECTPROPERTIES *pprop = &_rgObjectProperties[_cObjectProperties++];
                ClearPb(pprop, SIZEOF(*pprop));
                pprop->iscen = iscen;
                pprop->arid = arid;
                pprop->fFlushOverlap = fFlush != 0;
                pprop->fCastShadows = fCast != 0;
            }
            continue;
        }

        // v98 Object Groups use self-contained CSV-style records.  They do
        // not alter the active camera/light parser section, so adding group
        // metadata cannot swallow Manual Camera or Z-axis tween records.
        // The v97 multi-line reader below remains for backward compatibility.
        if (strncmp(pch, "object group v2 ", 16) == 0)
        {
            int id = 0;
            int fLocked = 0;
            const achar *pchFields = pch + 16;
            const achar *pchComma1 = strchr(pchFields, ChLit(','));
            const achar *pchComma2 = pchComma1 != pvNil ? strchr(pchComma1 + 1, ChLit(',')) : pvNil;
            if (iscen >= 0 && _cObjectGroup < kcObjectGroupMax &&
                pchComma2 != pvNil && sscanf(pchFields, "%d,%d", &id, &fLocked) == 2 && id > 0)
            {
                OBJECTGROUP *pgroup = &_rgObjectGroup[_cObjectGroup++];
                ClearPb(pgroup, SIZEOF(*pgroup));
                pgroup->iscen = iscen;
                pgroup->id = id;
                pgroup->fLocked = fLocked != 0;
                _idObjectGroupNext = LwMax(_idObjectGroupNext, id + 1);

                achar szName[kcchObjectGroupName];
                strncpy_s(szName, SIZEOF(szName), pchComma2 + 1, _TRUNCATE);
                int32_t cch = (int32_t)strlen(szName);
                while (cch > 0 && (szName[cch - 1] == ChLit('\r') || szName[cch - 1] == ChLit('\n')))
                    szName[--cch] = 0;
                if (cch > 0)
                    strcpy_s(pgroup->szName, SIZEOF(pgroup->szName), szName);
                else
                    sprintf_s(pgroup->szName, SIZEOF(pgroup->szName), "Object Group %d", id);
            }
            continue;
        }

        if (strncmp(pch, "object member v2 ", 17) == 0)
        {
            int idGroup = 0;
            int arid = aridNil;
            float v[15];
            const int cRead = sscanf(pch,
                "object member v2 %d,%d,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f",
                &idGroup, &arid,
                &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8],
                &v[9], &v[10], &v[11], &v[12], &v[13], &v[14]);
            bool fGroupExists = fFalse;
            for (int32_t iGroup = 0; iGroup < _cObjectGroup; iGroup++)
            {
                if (_rgObjectGroup[iGroup].id == idGroup && _rgObjectGroup[iGroup].iscen == iscen)
                {
                    fGroupExists = fTrue;
                    break;
                }
            }
            if (iscen >= 0 && cRead == 17 && arid != aridNil && fGroupExists &&
                _cObjectGroupMember < kcObjectGroupMemberMax)
            {
                OBJECTGROUPMEMBER *pmember = &_rgObjectGroupMember[_cObjectGroupMember++];
                ClearPb(pmember, SIZEOF(*pmember));
                pmember->idGroup = idGroup;
                pmember->arid = arid;
                pmember->xr = BrFloatToScalar(v[0]);
                pmember->yr = BrFloatToScalar(v[1]);
                pmember->zr = BrFloatToScalar(v[2]);
                int32_t iv = 3;
                for (int32_t ir = 0; ir < 4; ir++)
                    for (int32_t ic = 0; ic < 3; ic++)
                        pmember->bmat34.m[ir][ic] = BrFloatToScalar(v[iv++]);
            }
            continue;
        }

        if (strncmp(pch, "object group", 12) == 0)
        {
            FinalizeTween();
            section = 5;
            pctmanLast = pvNil;
            plightLast = pvNil;
            pgroupLast = pvNil;
            pmemberLast = pvNil;
            fManualImplicitNext = fFalse;
            if (iscen >= 0 && _cObjectGroup < kcObjectGroupMax)
            {
                pgroupLast = &_rgObjectGroup[_cObjectGroup++];
                ClearPb(pgroupLast, SIZEOF(*pgroupLast));
                pgroupLast->iscen = iscen;
                pgroupLast->id = _idObjectGroupNext++;
                sprintf_s(pgroupLast->szName, SIZEOF(pgroupLast->szName),
                          "Object Group %d", (int)pgroupLast->id);
            }
            continue;
        }

        if (iscen >= 0 && section == 5 && pgroupLast != pvNil)
        {
            int n = 0;
            if (sscanf(pch, "id %d", &n) == 1)
            {
                if (n > 0)
                {
                    pgroupLast->id = n;
                    _idObjectGroupNext = LwMax(_idObjectGroupNext, n + 1);
                }
                continue;
            }
            if (strncmp(pch, "name ", 5) == 0)
            {
                const achar *psz = pch + 5;
                achar szName[kcchObjectGroupName];
                strncpy_s(szName, SIZEOF(szName), psz, _TRUNCATE);
                int32_t cch = (int32_t)strlen(szName);
                while (cch > 0 && (szName[cch - 1] == ChLit('\r') || szName[cch - 1] == ChLit('\n')))
                    szName[--cch] = 0;
                if (cch > 0)
                    strcpy_s(pgroupLast->szName, SIZEOF(pgroupLast->szName), szName);
                continue;
            }
            if (sscanf(pch, "locked %d", &n) == 1)
            {
                pgroupLast->fLocked = n != 0;
                continue;
            }
            if (sscanf(pch, "member %d", &n) == 1)
            {
                pmemberLast = pvNil;
                if (_cObjectGroupMember < kcObjectGroupMemberMax)
                {
                    pmemberLast = &_rgObjectGroupMember[_cObjectGroupMember++];
                    ClearPb(pmemberLast, SIZEOF(*pmemberLast));
                    pmemberLast->idGroup = pgroupLast->id;
                    pmemberLast->arid = n;
                    BrMatrix34Identity(&pmemberLast->bmat34);
                }
                continue;
            }
            if (pmemberLast != pvNil)
            {
                float x, y, z;
                if (sscanf(pch, "position %f %f %f", &x, &y, &z) == 3)
                {
                    pmemberLast->xr = BrFloatToScalar(x);
                    pmemberLast->yr = BrFloatToScalar(y);
                    pmemberLast->zr = BrFloatToScalar(z);
                    continue;
                }
                float m[12];
                if (sscanf(pch, "matrix %f %f %f %f %f %f %f %f %f %f %f %f",
                           &m[0], &m[1], &m[2], &m[3], &m[4], &m[5],
                           &m[6], &m[7], &m[8], &m[9], &m[10], &m[11]) == 12)
                {
                    int32_t im = 0;
                    for (int32_t ir = 0; ir < 4; ir++)
                        for (int32_t ic = 0; ic < 3; ic++)
                            pmemberLast->bmat34.m[ir][ic] = BrFloatToScalar(m[im++]);
                    continue;
                }
            }
            // Unknown/non-group lines must fall through so the next section
            // header (Light Lab/manual/tween) can terminate this group.
        }

        if (strncmp(pch, "depth motion tween", 18) == 0 ||
            strncmp(pch, "z-axis motion tween", 19) == 0)
        {
            FinalizeTween();
            section = 1;
            pctmanLast = pvNil;
            plightLast = pvNil;
            pgroupLast = pvNil;
            pmemberLast = pvNil;
            fManualImplicitNext = fFalse;
            ClearPb(&ctweenPending, SIZEOF(ctweenPending));
            ctweenPending.iscen = iscen;
            ctweenPending.id = idTweenNext++;
            fTweenPending = iscen >= 0;
            continue;
        }

        if (strncmp(pch, "manual camera mode", 18) == 0)
        {
            FinalizeTween();
            section = 2;
            pctmanLast = pvNil;
            plightLast = pvNil;
            pgroupLast = pvNil;
            pmemberLast = pvNil;
            fManualImplicitNext = fFalse;
            continue;
        }

        if (strncmp(pch, "light object", 12) == 0)
        {
            FinalizeTween();
            section = 3;
            pctmanLast = pvNil;
            pgroupLast = pvNil;
            pmemberLast = pvNil;
            fManualImplicitNext = fFalse;
            plightLast = pvNil;
            if (iscen >= 0 && _clightLab < kclightLabMax)
            {
                plightLast = &_rglightLab[_clightLab++];
                ClearPb(plightLast, SIZEOF(*plightLast));
                plightLast->iscen = iscen;
                plightLast->arid = aridNil;
                plightLast->fEnabled = fTrue;
                plightLast->fGenerateShadows = fFalse;
                plightLast->fAttachmentHideable = fFalse;
                plightLast->intensity = 100;
                plightLast->diameter = 24.0f;
                plightLast->edgeGradient = 4.0f;
                plightLast->range = 500.0f;
                strcpy_s(plightLast->szShape, SIZEOF(plightLast->szShape), "round");
            }
            continue;
        }

        if (iscen >= 0 && section == 3 && plightLast != pvNil)
        {
            int n = 0;
            float fl = 0.0f;
            achar szWord[16];
            if (sscanf(pch, "arid %d", &n) == 1)
                plightLast->arid = n;
            else if (sscanf(pch, "enabled %d", &n) == 1)
                plightLast->fEnabled = n != 0;
            else if (sscanf(pch, "generate shadows %d", &n) == 1)
                plightLast->fGenerateShadows = n != 0;
            else if (sscanf(pch, "attachment hide-able %d", &n) == 1)
                plightLast->fAttachmentHideable = n != 0;
            else if (sscanf(pch, "intensity %d", &n) == 1)
                plightLast->intensity = LwMin(100, LwMax(1, n));
            else if (sscanf(pch, "edge gradient %f", &fl) == 1)
                plightLast->edgeGradient = fl;
            else if (sscanf(pch, "diameter %f", &fl) == 1)
                plightLast->diameter = fl;
            else if (sscanf(pch, "range %f", &fl) == 1)
                plightLast->range = fl > 0.0f ? fl : 500.0f;
            else if (sscanf(pch, "spawn frame %d", &n) == 1)
                plightLast->nfrmSpawn = LwMax(1, n);
            else if (sscanf(pch, "despawn frame %d", &n) == 1)
                plightLast->nfrmDespawn = LwMax(0, n);
            else if (sscanf_s(pch, "shape %15s", szWord, (unsigned)SIZEOF(szWord)) == 1)
                strcpy_s(plightLast->szShape, SIZEOF(plightLast->szShape), szWord);
            continue;
        }

        if (iscen >= 0 && section == 2 &&
            strncmp(pch, "implicit camera hold", 20) == 0)
        {
            pctmanLast = pvNil;
            fManualImplicitNext = fTrue;
            continue;
        }

        if (iscen >= 0 && section == 2 &&
            sscanf(pch, "frame %d x %f", &nfrm, &value) == 2 && nfrm >= 1)
        {
            pctmanLast = _PctmanFind(iscen, nfrm, fTrue);
            if (pctmanLast != pvNil)
            {
                pctmanLast->x = value;
                pctmanLast->fImplicit = fManualImplicitNext;
            }
            fManualImplicitNext = fFalse;
            continue;
        }
        if (iscen >= 0 && section == 2 &&
            sscanf(pch, "frame %d y %f", &nfrm, &value) == 2 && nfrm >= 1)
        {
            pctmanLast = _PctmanFind(iscen, nfrm, fTrue);
            if (pctmanLast != pvNil)
                pctmanLast->y = value;
            continue;
        }
        if (iscen >= 0 && section == 2 &&
            sscanf(pch, "frame %d z %f", &nfrm, &value) == 2 && nfrm >= 1)
        {
            pctmanLast = _PctmanFind(iscen, nfrm, fTrue);
            if (pctmanLast != pvNil)
                pctmanLast->z = value;
            continue;
        }
        if (iscen >= 0 && section == 2 && pctmanLast != pvNil &&
            sscanf(pch, "pitch %f", &value) == 1)
        {
            pctmanLast->pitch = value;
            continue;
        }
        if (iscen >= 0 && section == 2 && pctmanLast != pvNil &&
            sscanf(pch, "yaw %f", &value) == 1)
        {
            pctmanLast->yaw = value;
            continue;
        }

        if (iscen >= 0 && section <= 1)
        {
            float flBase = 0.0f;
            if (sscanf(pch, "x %f", &flBase) == 1)
            {
                if (!fTweenPending)
                {
                    ClearPb(&ctweenPending, SIZEOF(ctweenPending));
                    ctweenPending.iscen = iscen;
                    ctweenPending.id = idTweenNext++;
                    fTweenPending = fTrue;
                }
                ctweenPending.x = flBase;
                continue;
            }
            if (sscanf(pch, "y %f", &flBase) == 1)
            {
                if (!fTweenPending)
                {
                    ClearPb(&ctweenPending, SIZEOF(ctweenPending));
                    ctweenPending.iscen = iscen;
                    ctweenPending.id = idTweenNext++;
                    fTweenPending = fTrue;
                }
                ctweenPending.y = flBase;
                continue;
            }
            if (sscanf(pch, "z %f", &flBase) == 1)
            {
                if (!fTweenPending)
                {
                    ClearPb(&ctweenPending, SIZEOF(ctweenPending));
                    ctweenPending.iscen = iscen;
                    ctweenPending.id = idTweenNext++;
                    fTweenPending = fTrue;
                }
                ctweenPending.z = flBase;
                continue;
            }
            if (sscanf(pch, "pitch %f", &flBase) == 1)
            {
                if (!fTweenPending)
                {
                    ClearPb(&ctweenPending, SIZEOF(ctweenPending));
                    ctweenPending.iscen = iscen;
                    ctweenPending.id = idTweenNext++;
                    fTweenPending = fTrue;
                }
                ctweenPending.pitch = flBase;
                continue;
            }
        }

        if (iscen >= 0 && section <= 1 && sscanf(pch, "yaw %f", &value) == 1)
        {
            if (!fTweenPending)
            {
                ClearPb(&ctweenPending, SIZEOF(ctweenPending));
                ctweenPending.iscen = iscen;
                ctweenPending.id = idTweenNext++;
                fTweenPending = fTrue;
            }
            ctweenPending.yaw = value;
            continue;
        }

        if (iscen >= 0 && section <= 1 &&
            sscanf(pch, "frame %d z %f", &nfrm, &value) == 2 && nfrm >= 1)
        {
            if (!fTweenPending)
            {
                ClearPb(&ctweenPending, SIZEOF(ctweenPending));
                ctweenPending.iscen = iscen;
                ctweenPending.id = idTweenNext++;
                fTweenPending = fTrue;
            }
            if (cframeTween == 0)
            {
                ctweenPending.nfrmFirst = nfrm;
                ctweenPending.zFirst = value;
            }
            else
            {
                ctweenPending.nfrmLast = nfrm;
                ctweenPending.zLast = value;
            }
            cframeTween++;
            continue;
        }
    }

    FinalizeTween();
    fclose(pfile);

    // v36 no longer supports anonymous Light Lab records.  Drop old sidecar
    // records that were never attached to an actor.  Missing actor ARIDs are
    // removed when their scene is materialized, where they can be validated.
    for (int32_t ilight = _clightLab - 1; ilight >= 0; ilight--)
    {
        if (_rglightLab[ilight].arid != aridNil)
            continue;
        for (int32_t j = ilight + 1; j < _clightLab; j++)
            _rglightLab[j - 1] = _rglightLab[j];
        _clightLab--;
    }

    // Migration for pre-v36 .3ct files: those files had no explicit scene
    // master switch and any stored Light Lab record was implicitly active.
    // Preserve that behavior for existing movies, while genuinely new scenes
    // still start with lighting disabled.
    for (int32_t ilight = 0; ilight < _clightLab; ilight++)
    {
        int32_t iscenLight = _rglightLab[ilight].iscen;
        if (FIn(iscenLight, 0, kc4DMMSceneSettingsMax) && !rgfSceneLightsSettingSeen[iscenLight])
            _rgfSceneLightsEnabled[iscenLight] = fTrue;
    }

    // Normalize pre-v12 Light Lab records. Spawn frame 0 no longer has a
    // distinct meaning; frame 1 is always the first scene-relative frame.
    for (int32_t ilight = 0; ilight < _clightLab; ++ilight)
        if (_rglightLab[ilight].nfrmSpawn <= 0)
            _rglightLab[ilight].nfrmSpawn = 1;

    // Older .3ct files predate these per-scene policies. Missing scene records
    // inherit the movie-wide defaults rather than becoming accidental overrides.
    for (int32_t i = 0; i < LwMin(Cscen(), kc4DMMSceneSettingsMax); ++i)
    {
        if (!rgfSceneDefaultShadersSettingSeen[i])
            _rgfSceneDefaultLightingShaders[i] = _fDefaultLightingShaders;
        if (!rgfSceneLightLabCombineSettingSeen[i])
            _rgfSceneLightLabCombineLegacy[i] = _fLightLabCombineLegacy;
    }

    if (!fHeader)
    {
        _cctween = 0;
        _cctman = 0;
    }
    _SortCameraTrackTweens();
    UpdateObjectShadowProperties();
    DiagLog("3ct load complete tweens=%ld manual=%ld lights=%ld groups=%ld members=%ld object_props=%ld nonselectable=%ld",
            (long)_cctween, (long)_cctman, (long)_clightLab,
            (long)_cObjectGroup, (long)_cObjectGroupMember, (long)_cObjectProperties,
            (long)_caridNonSelectable);
}

/******************************************************************************
    FCameraTrackActive
******************************************************************************/
int32_t MVIE::_ItweenAtFrame(int32_t iscen, int32_t nfrm)
{
    for (int32_t i = 0; i < _cctween; i++)
    {
        if (_rgctween[i].iscen == iscen &&
            FIn(nfrm, _rgctween[i].nfrmFirst, _rgctween[i].nfrmLast + 1))
        {
            return i;
        }
    }
    return ivNil;
}

int32_t MVIE::_ItweenFindById(int32_t id)
{
    for (int32_t i = 0; i < _cctween; i++)
    {
        if (_rgctween[i].id == id)
            return i;
    }
    return ivNil;
}

int32_t MVIE::_ItweenNewId(void)
{
    int32_t id = 0;
    for (int32_t i = 0; i < _cctween; i++)
        id = LwMax(id, _rgctween[i].id + 1);
    return id;
}

bool MVIE::_FFrameCameraOccupied(int32_t iscen, int32_t nfrm,
                                 int32_t itweenIgnore)
{
    for (int32_t i = 0; i < _cctween; i++)
    {
        if (_rgctween[i].iscen == iscen && _rgctween[i].id != itweenIgnore &&
            FIn(nfrm, _rgctween[i].nfrmFirst, _rgctween[i].nfrmLast + 1))
        {
            return fTrue;
        }
    }
    for (int32_t i = 0; i < _cctman; i++)
    {
        if (_rgctman[i].iscen == iscen && _rgctman[i].nfrm == nfrm)
            return fTrue;
    }
    return fFalse;
}

/******************************************************************************
    FCameraTrackActive
******************************************************************************/
bool MVIE::FCameraTrackActive(void)
{
    AssertThis(0);
    if ((_fFreeLookOverride && _iscenFreeLook == _iscen) ||
        (_fManualCameraRecording && _fManualCameraRecordLiveValid))
    {
        return fTrue;
    }
    for (int32_t i = 0; i < _cctween; i++)
    {
        if (_rgctween[i].iscen == _iscen)
            return fTrue;
    }
    for (int32_t i = 0; i < _cctman; i++)
    {
        if (_rgctman[i].iscen == _iscen)
            return fTrue;
    }
    return fFalse;
}

/******************************************************************************
    FCameraTrackNeedsLiveActors

    Prerendered actors are safe while a custom camera is simply holding a
    fixed pose, but not while the camera is moving between authored samples.
    Keep the conservative live path for temporary/manual input and for the
    actual interpolated portions of depth/manual camera tracks.
******************************************************************************/
bool MVIE::FCameraTrackNeedsLiveActors(void)
{
    AssertThis(0);

    if (Pscen() == pvNil)
        return fFalse;

    int32_t nfrm = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;

    if ((_fFreeLookOverride && _iscenFreeLook == _iscen) ||
        (_fManualCameraRecording && _fManualCameraRecordLiveValid) ||
        (_fManualCameraMode && _fManualCameraEditValid &&
         _iscenManualCameraEdit == _iscen && _nfrmManualCameraEdit == nfrm))
    {
        return fTrue;
    }

    // A depth tween changes Z continuously between its two endpoints.  Treat
    // the endpoint frames as live as well so a prerender snapshot is never
    // reused across the first/last moving frame.
    for (int32_t i = 0; i < _cctween; i++)
    {
        const CTTWEEN &t = _rgctween[i];
        if (t.iscen != _iscen || t.nfrmLast <= t.nfrmFirst)
            continue;
        if (FIn(nfrm, t.nfrmFirst, t.nfrmLast + 1) && t.zFirst != t.zLast)
            return fTrue;
    }

    // Manual-camera samples interpolate from the first authored sample to the
    // last.  Once the final sample is reached the camera is a held pose again
    // and ordinary 3DMM prerendering may resume.
    int32_t nfrmFirst = 0;
    int32_t nfrmLast = 0;
    int32_t cSample = 0;
    for (int32_t i = 0; i < _cctman; i++)
    {
        if (_rgctman[i].iscen != _iscen)
            continue;
        if (cSample == 0)
            nfrmFirst = nfrmLast = _rgctman[i].nfrm;
        else
        {
            nfrmFirst = LwMin(nfrmFirst, _rgctman[i].nfrm);
            nfrmLast = LwMax(nfrmLast, _rgctman[i].nfrm);
        }
        cSample++;
    }
    if (cSample >= 2 && FIn(nfrm, nfrmFirst, nfrmLast + 1))
        return fTrue;

    return fFalse;
}

/******************************************************************************
    FDepthMotionTweenActive

    The indicator represents the actual interpolated interval, not the held Z
    value before/after it.  A tween requires at least two Z keys and is active
    from the first key through the last key, inclusive.
******************************************************************************/
bool MVIE::FDepthMotionTweenActive(void)
{
    AssertThis(0);
    if (Pscen() == pvNil)
        return fFalse;
    int32_t nfrm = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;
    return _ItweenAtFrame(Iscen(), nfrm) != ivNil;
}

/******************************************************************************
    GetDepthMotionTweenText

    The external editor intentionally shows only the selected scene's editable
    payload: frame/Z lines followed by yaw.  Header, scene number and the
    "depth motion tween" marker are supplied by the canonical writer.
******************************************************************************/
void MVIE::GetDepthMotionTweenText(int32_t iscen, int32_t itween,
                                       const CTTWEEN *pctweenDefault,
                                       PSTN pstnText)
{
    AssertThis(0);
    AssertVarMem(pstnText);

    CTTWEEN ctween;
    ClearPb(&ctween, SIZEOF(ctween));
    int32_t i = _ItweenFindById(itween);
    if (i != ivNil && _rgctween[i].iscen == iscen)
        ctween = _rgctween[i];
    else if (pctweenDefault != pvNil)
        ctween = *pctweenDefault;
    else
    {
        ctween.iscen = iscen;
        ctween.nfrmFirst = 1;
        ctween.nfrmLast = 2;
    }

    achar rgchText[512];
#if defined(UNICODE)
    swprintf(rgchText, CvFromRgv(rgchText),
             L"frame %d z %.6g\r\nframe %d z %.6g\r\nx %.6g\r\ny %.6g\r\nz %.6g\r\npitch %.6g\r\nyaw %.6g",
             (int)ctween.nfrmFirst, (double)ctween.zFirst,
             (int)ctween.nfrmLast, (double)ctween.zLast,
             (double)ctween.x, (double)ctween.y, (double)ctween.z,
             (double)ctween.pitch, (double)ctween.yaw);
#else
    snprintf(rgchText, CvFromRgv(rgchText),
             "frame %d z %.6g\r\nframe %d z %.6g\r\nx %.6g\r\ny %.6g\r\nz %.6g\r\npitch %.6g\r\nyaw %.6g",
             (int)ctween.nfrmFirst, (double)ctween.zFirst,
             (int)ctween.nfrmLast, (double)ctween.zLast,
             (double)ctween.x, (double)ctween.y, (double)ctween.z,
             (double)ctween.pitch, (double)ctween.yaw);
#endif
    pstnText->SetSz(rgchText);
}

/******************************************************************************
    FSetDepthMotionTweenText

    Replace one scene's start/end Z keys and yaw from the external editor,
    preserving all other scenes.  Only Save commits the canonical .3ct; Cancel
    and the title-bar X leave the last known good state untouched.
******************************************************************************/
bool MVIE::FSetDepthMotionTweenText(int32_t iscen, int32_t itween,
                                        const achar *pszText)
{
    AssertThis(0);
    AssertSz(pszText);
    vfDepthMotionTweenHandledError = fFalse;

    const achar *pchNonWhite = pszText;
    while (*pchNonWhite == ChLit(' ') || *pchNonWhite == ChLit('\t') ||
           *pchNonWhite == ChLit('\r') || *pchNonWhite == ChLit('\n'))
        pchNonWhite++;

    int32_t iTarget = _ItweenFindById(itween);
    if (*pchNonWhite == 0)
    {
        if (iTarget == ivNil)
            return fTrue;
        if (!FAddCameraTrackUndo(PszLit("Z-Axis Motion Tween")))
            return fFalse;
        for (int32_t i = iTarget + 1; i < _cctween; i++)
            _rgctween[i - 1] = _rgctween[i];
        _cctween--;
        SetDirty();
        ApplyCameraTrack();
        MarkViews();
        _pmcc->UpdateScrollbars();
        return fTrue;
    }

    CTTWEEN ctween;
    if (iTarget != ivNil)
        ctween = _rgctween[iTarget];
    else
    {
        ClearPb(&ctween, SIZEOF(ctween));
        ctween.iscen = iscen;
        ctween.id = _ItweenNewId();
    }

    bool fFrameFirst = fFalse;
    bool fFrameLast = fFalse;
    bool fYaw = fFalse;
    const achar *pch = pszText;
    while (*pch != 0)
    {
        achar rgch[256];
        int32_t ich = 0;
        while (*pch != 0 && *pch != ChLit('\r') && *pch != ChLit('\n'))
        {
            if (ich >= CvFromRgv(rgch) - 1)
                return fFalse;
            rgch[ich++] = *pch++;
        }
        rgch[ich] = 0;
        while (*pch == ChLit('\r') || *pch == ChLit('\n'))
            pch++;
        achar *line = rgch;
        while (*line == ChLit(' ') || *line == ChLit('\t'))
            line++;
        if (*line == 0)
            continue;

        achar extra;
        int32_t nfrm;
        float value;
        if (sscanf(line, "frame %d z %f %c", &nfrm, &value, &extra) == 2)
        {
            if (!fFrameFirst)
            {
                ctween.nfrmFirst = nfrm;
                ctween.zFirst = value;
                fFrameFirst = fTrue;
            }
            else if (!fFrameLast)
            {
                ctween.nfrmLast = nfrm;
                ctween.zLast = value;
                fFrameLast = fTrue;
            }
            else
                return fFalse;
        }
        else if (sscanf(line, "x %f %c", &value, &extra) == 1)
            ctween.x = value;
        else if (sscanf(line, "y %f %c", &value, &extra) == 1)
            ctween.y = value;
        else if (sscanf(line, "z %f %c", &value, &extra) == 1)
            ctween.z = value;
        else if (sscanf(line, "pitch %f %c", &value, &extra) == 1)
            ctween.pitch = value;
        else if (sscanf(line, "yaw %f %c", &value, &extra) == 1)
        {
            ctween.yaw = value;
            fYaw = fTrue;
        }
        else
            return fFalse;
    }

    if (!fFrameFirst || !fFrameLast || !fYaw || Pscen() == pvNil || Iscen() != iscen)
        return fFalse;
    int32_t nfrmMac = Pscen()->NfrmLast() - Pscen()->NfrmFirst() + 1;
    if (ctween.nfrmFirst < 1 || ctween.nfrmLast > nfrmMac ||
        ctween.nfrmFirst >= ctween.nfrmLast ||
        !(ctween.zFirst >= -FLT_MAX && ctween.zFirst <= FLT_MAX) ||
        !(ctween.zLast >= -FLT_MAX && ctween.zLast <= FLT_MAX) ||
        !(ctween.x >= -FLT_MAX && ctween.x <= FLT_MAX) ||
        !(ctween.y >= -FLT_MAX && ctween.y <= FLT_MAX) ||
        !(ctween.z >= -FLT_MAX && ctween.z <= FLT_MAX) ||
        !(ctween.pitch >= -FLT_MAX && ctween.pitch <= FLT_MAX) ||
        !(ctween.yaw >= -FLT_MAX && ctween.yaw <= FLT_MAX))
    {
        return fFalse;
    }

    bool fBadBeginning = _FFrameCameraOccupied(iscen, ctween.nfrmFirst, itween);
    bool fBadEnding = _FFrameCameraOccupied(iscen, ctween.nfrmLast, itween);
    bool fRangeCollision = fFalse;
    bool fBeginningCorrectionFound = !fBadBeginning;
    bool fEndingCorrectionFound = !fBadEnding;
    int32_t nfrmBeginningNew = ctween.nfrmFirst;
    int32_t nfrmEndingNew = ctween.nfrmLast;

    auto ShowTwoFreeFramesRequired = [&]()
    {
        vfDepthMotionTweenHandledError = fTrue;
#if defined(KAUAI_WIN32)
        MessageBoxA(vhwndDepthMotionTween,
            "Can't do that on the current frame because at least 2 frames free of camera positioning changes are required.",
            "3DMMEx Z-Axis Motion Tween", MB_OK | MB_ICONINFORMATION);
#endif
    };

    if (fBadBeginning)
    {
        // A beginning inside occupied camera data should move forward into the
        // nearest following free run whenever possible.  This is the useful
        // correction when the requested range straddles an existing tween.
        bool fFound = fFalse;
        for (int32_t nfrm = ctween.nfrmFirst + 1; nfrm < ctween.nfrmLast; nfrm++)
        {
            if (!_FFrameCameraOccupied(iscen, nfrm, itween))
            {
                nfrmBeginningNew = nfrm;
                fFound = fTrue;
                fBeginningCorrectionFound = fTrue;
                break;
            }
        }
        if (!fFound)
        {
            for (int32_t nfrm = ctween.nfrmFirst - 1; nfrm >= 1; nfrm--)
            {
                if (!_FFrameCameraOccupied(iscen, nfrm, itween))
                {
                    nfrmBeginningNew = nfrm;
                    fBeginningCorrectionFound = fTrue;
                    break;
                }
            }
        }
    }

    if (fBadEnding)
    {
        // Endpoints move backward first so the corrected range stops before
        // the occupied camera block instead of silently spanning across it.
        bool fFound = fFalse;
        for (int32_t nfrm = ctween.nfrmLast - 1; nfrm > ctween.nfrmFirst; nfrm--)
        {
            if (!_FFrameCameraOccupied(iscen, nfrm, itween))
            {
                nfrmEndingNew = nfrm;
                fFound = fTrue;
                fEndingCorrectionFound = fTrue;
                break;
            }
        }
        if (!fFound)
        {
            for (int32_t nfrm = ctween.nfrmLast + 1; nfrm <= nfrmMac; nfrm++)
            {
                if (!_FFrameCameraOccupied(iscen, nfrm, itween))
                {
                    nfrmEndingNew = nfrm;
                    fEndingCorrectionFound = fTrue;
                    break;
                }
            }
        }
    }

    if (!fBeginningCorrectionFound || !fEndingCorrectionFound)
    {
        ShowTwoFreeFramesRequired();
        return fFalse;
    }

    // Even corrected endpoints can still straddle another tween or manual
    // sample.  Clamp the proposed ending immediately before the first occupied
    // frame so accepting the dialog always produces one contiguous free range.
    int32_t nfrmCandidateFirst = fBadBeginning ? nfrmBeginningNew : ctween.nfrmFirst;
    int32_t nfrmCandidateLast = fBadEnding ? nfrmEndingNew : ctween.nfrmLast;
    if (nfrmCandidateFirst < nfrmCandidateLast)
    {
        for (int32_t nfrm = nfrmCandidateFirst + 1;
             nfrm < nfrmCandidateLast; nfrm++)
        {
            if (_FFrameCameraOccupied(iscen, nfrm, itween))
            {
                fRangeCollision = fTrue;
                nfrmEndingNew = nfrm - 1;
                break;
            }
        }
    }

    nfrmCandidateFirst = fBadBeginning ? nfrmBeginningNew : ctween.nfrmFirst;
    nfrmCandidateLast = (fBadEnding || fRangeCollision) ? nfrmEndingNew : ctween.nfrmLast;
    if (nfrmCandidateFirst >= nfrmCandidateLast)
    {
        ShowTwoFreeFramesRequired();
        return fFalse;
    }

    if (fBadBeginning || fBadEnding || fRangeCollision)
    {
        achar rgchMessage[1024];
        rgchMessage[0] = 0;
        int32_t cch = 0;
        auto AppendMessage = [&](const achar *pszFormat, int32_t value)
        {
            if (cch >= CvFromRgv(rgchMessage) - 1)
                return;
            int32_t cchT = snprintf(rgchMessage + cch,
                                    CvFromRgv(rgchMessage) - cch,
                                    pszFormat, (int)value);
            if (cchT > 0)
                cch = LwMin(CvFromRgv(rgchMessage) - 1, cch + cchT);
        };

        if (fBadBeginning)
        {
            AppendMessage(
                "Can't use frame %d as the beginning of this frame range because this frame already has camera positioning changes applied to it.\r\n",
                ctween.nfrmFirst);
        }
        if (fBadEnding)
        {
            AppendMessage(
                "Can't use frame %d as the ending of this frame range because this frame already has camera positioning changes applied to it.\r\n",
                ctween.nfrmLast);
        }
        else if (fRangeCollision)
        {
            AppendMessage(
                "Can't use frame %d as the ending of this frame range because this frame range includes camera positioning changes applied to an earlier frame.\r\n",
                ctween.nfrmLast);
        }
        if (fBadBeginning)
        {
            AppendMessage("Change beginning of frame range to %d instead?\r\n",
                          nfrmBeginningNew);
        }
        if (fBadEnding || fRangeCollision)
        {
            AppendMessage("Change ending of frame range to %d instead?",
                          nfrmEndingNew);
        }

        vfDepthMotionTweenHandledError = fTrue;
#if defined(KAUAI_WIN32)
        if (MessageBoxA(vhwndDepthMotionTween, rgchMessage,
                        "3DMMEx Z-Axis Motion Tween",
                        MB_YESNO | MB_ICONQUESTION) != IDYES)
        {
            return fFalse;
        }
#else
        return fFalse;
#endif
        if (fBadBeginning)
            ctween.nfrmFirst = nfrmBeginningNew;
        if (fBadEnding || fRangeCollision)
            ctween.nfrmLast = nfrmEndingNew;
    }

    if (ctween.nfrmFirst >= ctween.nfrmLast)
        return fFalse;
    for (int32_t nfrm = ctween.nfrmFirst; nfrm <= ctween.nfrmLast; nfrm++)
    {
        if (_FFrameCameraOccupied(iscen, nfrm, itween))
            return fFalse;
    }

    if (iTarget == ivNil && _cctween >= kcctweenMax)
        return fFalse;
    if (!FAddCameraTrackUndo(PszLit("Z-Axis Motion Tween")))
        return fFalse;

    if (iTarget == ivNil)
        _rgctween[_cctween++] = ctween;
    else
        _rgctween[iTarget] = ctween;
    _SortCameraTrackTweens();
    SetDirty();
    ApplyCameraTrack();
    MarkViews();
    _pmcc->UpdateScrollbars();
    return fTrue;
}

/******************************************************************************
    FOpenDepthMotionTweenEditor
******************************************************************************/
bool MVIE::FOpenDepthMotionTweenEditor(void)
{
    AssertThis(0);
#if defined(KAUAI_WIN32)
    if (_stnCameraTrackPath.Cch() == 0 || Iscen() < 0 || Pscen() == pvNil)
    {
        MessageBoxA(hNil, "Save the movie before editing its Z-Axis Motion Tween.",
                    "3DMMEx Z-Axis Motion Tween", MB_OK | MB_ICONINFORMATION);
        return fFalse;
    }

    int32_t nfrmCur = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;
    int32_t iTween = _ItweenAtFrame(Iscen(), nfrmCur);
    int32_t idTween = iTween == ivNil ? ivNil : _rgctween[iTween].id;
    CTTWEEN ctweenDefault;
    ClearPb(&ctweenDefault, SIZEOF(ctweenDefault));

    if (iTween == ivNil)
    {
        int32_t nfrmMac = Pscen()->NfrmLast() - Pscen()->NfrmFirst() + 1;
        if (_FFrameCameraOccupied(Iscen(), nfrmCur, ivNil))
        {
            MessageBoxA(vwig.hwndApp,
                "Can't do that on the current frame because at least 2 frames free of camera positioning changes are required.",
                "3DMMEx Z-Axis Motion Tween", MB_OK | MB_ICONINFORMATION);
            return fFalse;
        }
        int32_t nfrmEnd = nfrmCur;
        while (nfrmEnd < nfrmMac &&
               !_FFrameCameraOccupied(Iscen(), nfrmEnd + 1, ivNil))
        {
            nfrmEnd++;
        }
        if (nfrmEnd <= nfrmCur)
        {
            MessageBoxA(vwig.hwndApp,
                "Can't do that on the current frame because at least 2 frames free of camera positioning changes are required.",
                "3DMMEx Z-Axis Motion Tween", MB_OK | MB_ICONINFORMATION);
            return fFalse;
        }

        ctweenDefault.iscen = Iscen();
        ctweenDefault.id = ivNil;
        ctweenDefault.nfrmFirst = nfrmCur;
        ctweenDefault.nfrmLast = nfrmEnd;
        ctweenDefault.zFirst = 0.0f;
        ctweenDefault.zLast = 0.0f;

        // A new Z-axis tween begins exactly where the persistent camera was
        // dropped off by the preceding manual sample/tween.  Its frame Z
        // values are travel distances from this complete starting pose.
        CTMAN ctmanStart;
        if (_FGetCameraControlState(Iscen(), nfrmCur, &ctmanStart))
        {
            ctweenDefault.x = ctmanStart.x;
            ctweenDefault.y = ctmanStart.y;
            ctweenDefault.z = ctmanStart.z;
            ctweenDefault.pitch = ctmanStart.pitch;
            ctweenDefault.yaw = ctmanStart.yaw;
        }
    }

    if (vhwndDepthMotionTween != hNil && IsWindow(vhwndDepthMotionTween))
    {
        if (vpmvieDepthMotionTween == this && viscenDepthMotionTween == Iscen() &&
            vitweenDepthMotionTween == idTween)
        {
            ShowWindow(vhwndDepthMotionTween, SW_SHOWNORMAL);
            SetForegroundWindow(vhwndDepthMotionTween);
            SetFocus(vhwndDepthMotionTweenEdit);
            return fTrue;
        }
        DestroyWindow(vhwndDepthMotionTween);
    }

    vpmvieDepthMotionTween = this;
    viscenDepthMotionTween = Iscen();
    vitweenDepthMotionTween = idTween;
    vctweenDepthMotionTweenDefault = ctweenDefault;

    HINSTANCE hinst = GetModuleHandleA(pvNil);
    WNDCLASSA wc;
    ClearPb(&wc, SIZEOF(wc));
    if (!GetClassInfoA(hinst, kszDepthMotionTweenWndClass, &wc))
    {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = LresultDepthMotionTweenWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(pvNil, IDC_IBEAM);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kszDepthMotionTweenWndClass;
        if (RegisterClassA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return fFalse;
    }

    vhwndDepthMotionTween = CreateWindowExA(
        WS_EX_TOOLWINDOW, kszDepthMotionTweenWndClass, "Z Axis Cam Motion Tween",
        WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 260, 245,
        Hwnd4DMMNativeToolOwner(), hNil, hinst, pvNil);
    if (vhwndDepthMotionTween == hNil)
    {
        vpmvieDepthMotionTween = pvNil;
        viscenDepthMotionTween = ivNil;
        vitweenDepthMotionTween = ivNil;
        return fFalse;
    }
    Scale4DMMExternalToolWindow200(vhwndDepthMotionTween);
    // Scale4DMMExternalToolWindow200() scales child rectangles after resizing the
    // shell. Re-run the editor's existing WM_SIZE layout once so Save/Cancel
    // finish inside the final 200%-equivalent client area instead of below it.
    {
        RECT rcClient;
        if (GetClientRect(vhwndDepthMotionTween, &rcClient))
            SendMessage(vhwndDepthMotionTween, WM_SIZE, SIZE_RESTORED,
                        MAKELPARAM(rcClient.right - rcClient.left, rcClient.bottom - rcClient.top));
    }
    ShowWindow(vhwndDepthMotionTween, SW_SHOWNORMAL);
    UpdateWindow(vhwndDepthMotionTween);
    SetFocus(vhwndDepthMotionTweenEdit);
    return fTrue;
#else
    return fFalse;
#endif
}


bool MVIE::FDeleteDepthMotionTween(int32_t iscen, int32_t itween)
{
    AssertThis(0);
    if (_ItweenFindById(itween) == ivNil)
        return fTrue;
    return FSetDepthMotionTweenText(iscen, itween, PszLit(""));
}

/******************************************************************************
    Manual-camera frame editor and camera-only undo support.
******************************************************************************/
bool MVIE::FManualCameraFrameActive(void)
{
    AssertThis(0);
    if (Pscen() == pvNil)
        return fFalse;
    int32_t nfrm = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;
    CTMAN *pctman = _PctmanFind(Iscen(), nfrm, fFalse);
    return pctman != pvNil && !pctman->fImplicit;
}

void MVIE::GetManualCameraFrameText(int32_t iscen, int32_t nfrm, PSTN pstnText)
{
    AssertThis(0);
    AssertVarMem(pstnText);
    CTMAN *p = _PctmanFind(iscen, nfrm, fFalse);
    if (p == pvNil || p->fImplicit)
    {
        pstnText->SetNil();
        return;
    }
    achar rgch[384];
#if defined(UNICODE)
    swprintf(rgch, CvFromRgv(rgch),
             L"x %.6g\r\ny %.6g\r\nz %.6g\r\npitch %.6g\r\nyaw %.6g",
             (double)p->x, (double)p->y, (double)p->z,
             (double)p->pitch, (double)p->yaw);
#else
    snprintf(rgch, CvFromRgv(rgch),
             "x %.6g\r\ny %.6g\r\nz %.6g\r\npitch %.6g\r\nyaw %.6g",
             (double)p->x, (double)p->y, (double)p->z,
             (double)p->pitch, (double)p->yaw);
#endif
    pstnText->SetSz(rgch);
}

bool MVIE::FSetManualCameraFrameText(int32_t iscen, int32_t nfrm,
                                     const achar *pszText)
{
    AssertThis(0);
    AssertSz(pszText);
    const achar *pchNonWhite = pszText;
    while (*pchNonWhite == ChLit(' ') || *pchNonWhite == ChLit('\t') ||
           *pchNonWhite == ChLit('\r') || *pchNonWhite == ChLit('\n'))
        pchNonWhite++;

    CTMAN *pOld = _PctmanFind(iscen, nfrm, fFalse);
    if (*pchNonWhite == 0)
    {
        if (pOld == pvNil)
            return fTrue;
        if (!FAddCameraTrackUndo(PszLit("Manual Camera Frame")))
            return fFalse;
        int32_t dst = 0;
        for (int32_t i = 0; i < _cctman; i++)
        {
            if (_rgctman[i].iscen == iscen && _rgctman[i].nfrm == nfrm)
                continue;
            _rgctman[dst++] = _rgctman[i];
        }
        _cctman = dst;
        SetDirty();
        ApplyCameraTrack();
        MarkViews();
        _pmcc->UpdateScrollbars();
        return fTrue;
    }

    CTMAN m;
    ClearPb(&m, SIZEOF(m));
    m.iscen = iscen;
    m.nfrm = nfrm;
    m.fImplicit = fFalse;
    int32_t cline = 0;
    const achar *pch = pszText;
    while (*pch != 0)
    {
        achar line[256];
        int32_t ich = 0;
        while (*pch != 0 && *pch != ChLit('\r') && *pch != ChLit('\n'))
        {
            if (ich >= CvFromRgv(line) - 1)
                return fFalse;
            line[ich++] = *pch++;
        }
        line[ich] = 0;
        while (*pch == ChLit('\r') || *pch == ChLit('\n'))
            pch++;
        achar *q = line;
        while (*q == ChLit(' ') || *q == ChLit('\t')) q++;
        if (*q == 0) continue;
        int32_t nf;
        achar extra;
        if (cline == 0 &&
            (sscanf(q, "x %f %c", &m.x, &extra) == 1 ||
             (sscanf(q, "frame %d x %f %c", &nf, &m.x, &extra) == 2 && nf == nfrm))) {}
        else if (cline == 1 &&
                 (sscanf(q, "y %f %c", &m.y, &extra) == 1 ||
                  (sscanf(q, "frame %d y %f %c", &nf, &m.y, &extra) == 2 && nf == nfrm))) {}
        else if (cline == 2 &&
                 (sscanf(q, "z %f %c", &m.z, &extra) == 1 ||
                  (sscanf(q, "frame %d z %f %c", &nf, &m.z, &extra) == 2 && nf == nfrm))) {}
        else if (cline == 3 && sscanf(q, "pitch %f %c", &m.pitch, &extra) == 1) {}
        else if (cline == 4 && sscanf(q, "yaw %f %c", &m.yaw, &extra) == 1) {}
        else return fFalse;
        cline++;
    }
    if (cline != 5 ||
        !(m.x >= -FLT_MAX && m.x <= FLT_MAX) ||
        !(m.y >= -FLT_MAX && m.y <= FLT_MAX) ||
        !(m.z >= -FLT_MAX && m.z <= FLT_MAX) ||
        !(m.pitch >= -FLT_MAX && m.pitch <= FLT_MAX) ||
        !(m.yaw >= -FLT_MAX && m.yaw <= FLT_MAX))
        return fFalse;
    if (pOld == pvNil && _cctman >= kcctmanMax)
        return fFalse;
    if (!FAddCameraTrackUndo(PszLit("Manual Camera Frame")))
        return fFalse;
    CTMAN *p = _PctmanFind(iscen, nfrm, fTrue);
    if (p == pvNil)
        return fFalse;
    *p = m;
    SetDirty();
    ApplyCameraTrack();
    MarkViews();
    _pmcc->UpdateScrollbars();
    return fTrue;
}

bool MVIE::FDeleteManualCameraFrame(int32_t iscen, int32_t nfrm)
{
    AssertThis(0);
    CTMAN *pctman = _PctmanFind(iscen, nfrm, fFalse);
    if (pctman == pvNil || pctman->fImplicit)
        return fTrue;

    if (!FSetManualCameraFrameText(iscen, nfrm, PszLit("")))
        return fFalse;

    if (_fManualCameraMode && Pscen() != pvNil && Iscen() == iscen &&
        Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1 == nfrm)
    {
        _ClearManualCameraEdit();
        _FBeginManualCameraEdit();
        ApplyCameraTrack();
        MarkViews();
    }
    return fTrue;
}

bool MVIE::FDeleteManualCameraFrameCurrent(void)
{
    AssertThis(0);
    if (Pscen() == pvNil)
        return fFalse;
    const int32_t nfrm = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;
    return FDeleteManualCameraFrame(Iscen(), nfrm);
}

bool MVIE::FOpenManualCameraFrameEditor(void)
{
    AssertThis(0);
#if defined(KAUAI_WIN32)
    if (Pscen() == pvNil)
        return fFalse;
    int32_t nfrm = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;
    CTMAN *pctmanFrame = _PctmanFind(Iscen(), nfrm, fFalse);
    if (pctmanFrame == pvNil || pctmanFrame->fImplicit)
        return fFalse;

    if (vhwndManualCameraFrame != hNil && IsWindow(vhwndManualCameraFrame))
        DestroyWindow(vhwndManualCameraFrame);
    vpmvieManualCameraFrame = this;
    viscenManualCameraFrame = Iscen();
    vnfrmManualCameraFrame = nfrm;

    HINSTANCE hinst = GetModuleHandleA(pvNil);
    WNDCLASSA wc;
    ClearPb(&wc, SIZEOF(wc));
    if (!GetClassInfoA(hinst, kszManualCameraFrameWndClass, &wc))
    {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = LresultManualCameraFrameWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(pvNil, IDC_IBEAM);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kszManualCameraFrameWndClass;
        if (RegisterClassA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return fFalse;
    }
    vhwndManualCameraFrame = CreateWindowExA(
        WS_EX_TOOLWINDOW, kszManualCameraFrameWndClass, "3DMMEx Manual Camera",
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 260, 190,
        Hwnd4DMMNativeToolOwner(), hNil, hinst, pvNil);
    if (vhwndManualCameraFrame == hNil)
        return fFalse;
    Scale4DMMExternalToolWindow200(vhwndManualCameraFrame);
    {
        RECT rcClient;
        if (GetClientRect(vhwndManualCameraFrame, &rcClient))
            SendMessage(vhwndManualCameraFrame, WM_SIZE, SIZE_RESTORED,
                        MAKELPARAM(rcClient.right - rcClient.left, rcClient.bottom - rcClient.top));
    }
    ShowWindow(vhwndManualCameraFrame, SW_SHOWNORMAL);
    BringWindowToTop(vhwndManualCameraFrame);
    SetForegroundWindow(vhwndManualCameraFrame);
    UpdateWindow(vhwndManualCameraFrame);
    SetFocus(vhwndManualCameraFrameEdit);
    return fTrue;
#else
    return fFalse;
#endif
}

bool MVIE::FAddCameraTrackUndo(const achar *pszUndoName)
{
    AssertThis(0);
    AssertSz(pszUndoName);
    PMUNC pmunc = MUNC::PmuncNew();
    if (pmunc == pvNil)
        return fFalse;
    pmunc->SetUndoName(pszUndoName);
    bool fRet = pmunc->FSave(this) && FAddUndo(pmunc);
    ReleasePpo(&pmunc);
    return fRet;
}

bool MVIE::_FEnsureManualCameraFrameUndo(void)
{
    if (Pscen() == pvNil)
        return fFalse;
    int32_t nfrm = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;
    if (_iscenManualCameraUndo == Iscen() && _nfrmManualCameraUndo == nfrm)
        return fTrue;
    if (!FAddCameraTrackUndo(PszLit("Manual Camera Frame")))
        return fFalse;
    _iscenManualCameraUndo = Iscen();
    _nfrmManualCameraUndo = nfrm;
    return fTrue;
}

/******************************************************************************
    _FGetCameraTrackZ

    Evaluate the scene-local Z offset at the one-based frame number shown in
    the Studio UI.  Before the first key the first value is held; after the
    last key the last value is held.
******************************************************************************/
bool MVIE::_FGetCameraTrackZ(int32_t iscen, int32_t nfrm, BRS *pzrOffset)
{
    AssertThis(0);
    AssertVarMem(pzrOffset);
    int32_t iActive = _ItweenAtFrame(iscen, nfrm);
    if (iActive != ivNil)
    {
        const CTTWEEN &t = _rgctween[iActive];
        float r = (float)(nfrm - t.nfrmFirst) /
                  (float)(t.nfrmLast - t.nfrmFirst);
        *pzrOffset = BrFloatToScalar(t.zFirst + (t.zLast - t.zFirst) * r);
        return fTrue;
    }

    int32_t iPrev = ivNil;
    int32_t iNext = ivNil;
    for (int32_t i = 0; i < _cctween; i++)
    {
        if (_rgctween[i].iscen != iscen)
            continue;
        if (_rgctween[i].nfrmLast < nfrm &&
            (iPrev == ivNil || _rgctween[i].nfrmLast > _rgctween[iPrev].nfrmLast))
            iPrev = i;
        if (_rgctween[i].nfrmFirst > nfrm &&
            (iNext == ivNil || _rgctween[i].nfrmFirst < _rgctween[iNext].nfrmFirst))
            iNext = i;
    }
    if (iPrev != ivNil)
    {
        *pzrOffset = BrFloatToScalar(_rgctween[iPrev].zLast);
        return fTrue;
    }
    if (iNext != ivNil)
    {
        *pzrOffset = BrFloatToScalar(_rgctween[iNext].zFirst);
        return fTrue;
    }
    return fFalse;
}

/******************************************************************************
    _FGetCameraTrackYaw

    Return the scene-local yaw offset in degrees.  Zero means the ordinary
    3DMM camera heading selected for this scene.
******************************************************************************/
bool MVIE::_FGetCameraTrackYaw(int32_t iscen, int32_t nfrm, float *pyaw)
{
    AssertThis(0);
    AssertVarMem(pyaw);
    int32_t i = _ItweenAtFrame(iscen, nfrm);
    if (i != ivNil)
    {
        *pyaw = _rgctween[i].yaw;
        return fTrue;
    }
    int32_t iPrev = ivNil;
    int32_t iNext = ivNil;
    for (int32_t j = 0; j < _cctween; j++)
    {
        if (_rgctween[j].iscen != iscen)
            continue;
        if (_rgctween[j].nfrmLast < nfrm &&
            (iPrev == ivNil || _rgctween[j].nfrmLast > _rgctween[iPrev].nfrmLast))
            iPrev = j;
        if (_rgctween[j].nfrmFirst > nfrm &&
            (iNext == ivNil || _rgctween[j].nfrmFirst < _rgctween[iNext].nfrmFirst))
            iNext = j;
    }
    i = iPrev != ivNil ? iPrev : iNext;
    if (i == ivNil)
        return fFalse;
    *pyaw = _rgctween[i].yaw;
    return fTrue;
}

/******************************************************************************
    _FSetCameraTrackYaw
******************************************************************************/
bool MVIE::_FSetCameraTrackYaw(int32_t itween, float yaw)
{
    AssertThis(0);
    int32_t i = _ItweenFindById(itween);
    if (i == ivNil)
        return fFalse;
    _rgctween[i].yaw = yaw;
    return fTrue;
}

/******************************************************************************
    _PctmanFind

    Find an exact manual-camera sample, optionally inserting a zeroed sample in
    scene/frame order.  Keeping this array sorted makes sidecar writing and
    interpolation deterministic without another container subsystem.
******************************************************************************/
CTMAN *MVIE::_PctmanFind(int32_t iscen, int32_t nfrm, bool fCreate)
{
    AssertBaseThis(0);

    int32_t ictman;
    for (ictman = 0; ictman < _cctman; ictman++)
    {
        if (_rgctman[ictman].iscen == iscen && _rgctman[ictman].nfrm == nfrm)
            return &_rgctman[ictman];
        if (_rgctman[ictman].iscen > iscen ||
            (_rgctman[ictman].iscen == iscen && _rgctman[ictman].nfrm > nfrm))
            break;
    }

    if (!fCreate || _cctman >= kcctmanMax)
        return pvNil;

    int32_t ictmanMove;
    for (ictmanMove = _cctman; ictmanMove > ictman; ictmanMove--)
        _rgctman[ictmanMove] = _rgctman[ictmanMove - 1];

    ClearPb(&_rgctman[ictman], SIZEOF(CTMAN));
    _rgctman[ictman].iscen = iscen;
    _rgctman[ictman].nfrm = nfrm;
    _cctman++;
    return &_rgctman[ictman];
}

/******************************************************************************
    _FGetManualCameraState

    Manual samples begin at their first recorded frame, interpolate between
    recorded frames, and hold the final sample afterward.  Before the first
    sample, the ordinary depth-track/baseline camera remains authoritative.
******************************************************************************/
bool MVIE::_FGetManualCameraState(int32_t iscen, int32_t nfrm, CTMAN *pctman)
{
    AssertThis(0);
    AssertVarMem(pctman);

    int32_t ictmanPrev = ivNil;
    int32_t ictmanNext = ivNil;
    int32_t ictman;
    for (ictman = 0; ictman < _cctman; ictman++)
    {
        if (_rgctman[ictman].iscen != iscen)
            continue;
        if (_rgctman[ictman].nfrm <= nfrm)
            ictmanPrev = ictman;
        if (_rgctman[ictman].nfrm >= nfrm)
        {
            ictmanNext = ictman;
            break;
        }
    }

    if (ictmanPrev == ivNil)
        return fFalse;

    if (ictmanNext == ivNil || ictmanPrev == ictmanNext ||
        _rgctman[ictmanPrev].nfrm == _rgctman[ictmanNext].nfrm)
    {
        *pctman = _rgctman[ictmanPrev];
        pctman->nfrm = nfrm;
        return fTrue;
    }

    float r = (float)(nfrm - _rgctman[ictmanPrev].nfrm) /
              (float)(_rgctman[ictmanNext].nfrm - _rgctman[ictmanPrev].nfrm);
    pctman->iscen = iscen;
    pctman->nfrm = nfrm;
    pctman->x = _rgctman[ictmanPrev].x +
                (_rgctman[ictmanNext].x - _rgctman[ictmanPrev].x) * r;
    pctman->y = _rgctman[ictmanPrev].y +
                (_rgctman[ictmanNext].y - _rgctman[ictmanPrev].y) * r;
    pctman->z = _rgctman[ictmanPrev].z +
                (_rgctman[ictmanNext].z - _rgctman[ictmanPrev].z) * r;
    pctman->pitch = _rgctman[ictmanPrev].pitch +
                    (_rgctman[ictmanNext].pitch - _rgctman[ictmanPrev].pitch) * r;
    pctman->yaw = _rgctman[ictmanPrev].yaw +
                  (_rgctman[ictmanNext].yaw - _rgctman[ictmanPrev].yaw) * r;
    pctman->roll = _rgctman[ictmanPrev].roll +
                   (_rgctman[ictmanNext].roll - _rgctman[ictmanPrev].roll) * r;
    pctman->fImplicit = fFalse;
    return fTrue;
}

/******************************************************************************
    _FGetCameraControlState

    Return the effective scene-relative camera offsets for one displayed frame.
    This is the common starting point for Manual Camera recording and temporary
    Free Look, whether the frame currently comes from manual samples, a depth
    tween, or the scene's untouched camera.
******************************************************************************/
bool MVIE::_FGetDepthTweenPose(const CTTWEEN &ctween, int32_t nfrm, CTMAN *pctman)
{
    AssertThis(0);
    AssertVarMem(pctman);

    ClearPb(pctman, SIZEOF(CTMAN));
    pctman->iscen = ctween.iscen;
    pctman->nfrm = nfrm;
    pctman->x = ctween.x;
    pctman->y = ctween.y;
    pctman->z = ctween.z;
    pctman->pitch = ctween.pitch;
    pctman->yaw = ctween.yaw;
    pctman->roll = 0.0f;
    pctman->fImplicit = fFalse;

    float zTravel;
    if (nfrm <= ctween.nfrmFirst)
        zTravel = ctween.zFirst;
    else if (nfrm >= ctween.nfrmLast)
        zTravel = ctween.zLast;
    else
    {
        float r = (float)(nfrm - ctween.nfrmFirst) /
                  (float)(ctween.nfrmLast - ctween.nfrmFirst);
        zTravel = ctween.zFirst + (ctween.zLast - ctween.zFirst) * r;
    }

    // Express the yaw-oriented local-Z travel back in Manual Camera's
    // scene-relative x/y/z coordinate basis.  This lets every camera-control
    // type hand one complete pose to the next without a baseline snap.
    BMAT34 bmatYaw;
    BrMatrix34Identity(&bmatYaw);
    BrMatrix34PostRotateY(&bmatYaw,
                          BrDegreeToAngle(BrFloatToScalar(ctween.yaw)));
    float wx = -zTravel * BrScalarToFloat(bmatYaw.m[2][0]);
    float wy = -zTravel * BrScalarToFloat(bmatYaw.m[2][1]);
    float wz = -zTravel * BrScalarToFloat(bmatYaw.m[2][2]);
    pctman->x += wx;
    pctman->y += wy;
    pctman->z += -wz;
    return fTrue;
}

bool MVIE::_FGetPersistentCameraTrackPose(int32_t iscen, int32_t nfrm,
                                          CTMAN *pctman)
{
    AssertThis(0);
    AssertVarMem(pctman);

    int32_t itweenActive = _ItweenAtFrame(iscen, nfrm);
    if (itweenActive != ivNil)
        return _FGetDepthTweenPose(_rgctween[itweenActive], nfrm, pctman);

    int32_t itweenPrev = ivNil;
    int32_t ictmanPrev = ivNil;
    for (int32_t i = 0; i < _cctween; i++)
    {
        if (_rgctween[i].iscen == iscen && _rgctween[i].nfrmLast < nfrm &&
            (itweenPrev == ivNil ||
             _rgctween[i].nfrmLast > _rgctween[itweenPrev].nfrmLast))
        {
            itweenPrev = i;
        }
    }
    for (int32_t i = 0; i < _cctman; i++)
    {
        if (_rgctman[i].iscen == iscen && _rgctman[i].nfrm <= nfrm &&
            (ictmanPrev == ivNil || _rgctman[i].nfrm > _rgctman[ictmanPrev].nfrm))
        {
            ictmanPrev = i;
        }
    }

    if (itweenPrev == ivNil && ictmanPrev == ivNil)
        return fFalse;

    if (itweenPrev != ivNil &&
        (ictmanPrev == ivNil ||
         _rgctween[itweenPrev].nfrmLast >= _rgctman[ictmanPrev].nfrm))
    {
        return _FGetDepthTweenPose(_rgctween[itweenPrev],
                                   _rgctween[itweenPrev].nfrmLast, pctman);
    }

    // Preserve ordinary manual-camera interpolation only when no Z tween
    // begins before the next manual sample.  Otherwise hold the latest manual
    // pose until that tween takes over.
    int32_t ictmanNext = ivNil;
    for (int32_t i = 0; i < _cctman; i++)
    {
        if (_rgctman[i].iscen == iscen && _rgctman[i].nfrm > nfrm &&
            (ictmanNext == ivNil || _rgctman[i].nfrm < _rgctman[ictmanNext].nfrm))
        {
            ictmanNext = i;
        }
    }
    if (ictmanNext != ivNil)
    {
        for (int32_t i = 0; i < _cctween; i++)
        {
            if (_rgctween[i].iscen == iscen &&
                _rgctween[i].nfrmFirst > _rgctman[ictmanPrev].nfrm &&
                _rgctween[i].nfrmFirst <= _rgctman[ictmanNext].nfrm)
            {
                *pctman = _rgctman[ictmanPrev];
                pctman->nfrm = nfrm;
                return fTrue;
            }
        }
    }

    return _FGetManualCameraState(iscen, nfrm, pctman);
}

/******************************************************************************
    _FGetCameraControlState

    Return one complete persistent camera pose.  An untouched scene has a
    zero-offset pose, so callers such as Manual Camera can always begin editing
    even before a .3ct record exists.
******************************************************************************/
bool MVIE::_FGetCameraControlState(int32_t iscen, int32_t nfrm, CTMAN *pctman)
{
    AssertThis(0);
    AssertVarMem(pctman);

    if (_FGetPersistentCameraTrackPose(iscen, nfrm, pctman))
        return fTrue;

    ClearPb(pctman, SIZEOF(CTMAN));
    pctman->iscen = iscen;
    pctman->nfrm = nfrm;
    return fTrue;
}

/******************************************************************************
    _ClearFreeLookState

    Destroy both the active input mode and any retained temporary viewport
    override.  Free Look deliberately survives Tab/Esc on the same frame, so
    scene, camera, tab, and movie lifetime boundaries must clear it explicitly.
******************************************************************************/
void MVIE::_ClearFreeLookState(void)
{
    AssertThis(0);

    _fFreeLookMode = fFalse;
    _fFreeLookOverride = fFalse;
    _fBrowserCameraFollow = fFalse;
    _iscenFreeLook = ivNil;
    _nfrmFreeLook = ivNil;
    ClearPb(&_ctmanFreeLookStart, SIZEOF(CTMAN));
    ClearPb(&_ctmanFreeLookLive, SIZEOF(CTMAN));

    PMVU pmvu = PmvuCur();
    if (pmvu != pvNil)
        pmvu->EndManualCameraInput();
}

/******************************************************************************
    _FCaptureManualCameraFrame

    Record the current live camera at the beginning of the current pre-existing
    frame.  Record Mode never creates timeline frames; it only writes or
    replaces the manual sample attached to the frame playback has reached.
******************************************************************************/
bool MVIE::_FCaptureManualCameraFrame(void)
{
    AssertThis(0);

    if (!_fManualCameraRecording || !_fManualCameraRecordLiveValid || Pscen() == pvNil)
        return fFalse;

    int32_t nfrm = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;

    // A scene change selects a different native camera.  Start that scene from
    // its own saved camera state instead of carrying the previous scene's
    // baseline-relative offsets across the cut.
    if (_ctmanManualCameraRecordLive.iscen != Iscen())
    {
        if (!_FGetCameraControlState(Iscen(), nfrm, &_ctmanManualCameraRecordLive))
            return fFalse;
    }

    CTMAN *pctman = _PctmanFind(Iscen(), nfrm, fTrue);
    if (pctman == pvNil)
        return fFalse;

    _ctmanManualCameraRecordLive.iscen = Iscen();
    _ctmanManualCameraRecordLive.nfrm = nfrm;
    _ctmanManualCameraRecordLive.fImplicit = fFalse;
    *pctman = _ctmanManualCameraRecordLive;
    SetDirty();
    return fTrue;
}

/******************************************************************************
    _FinishManualCameraRecording
******************************************************************************/
void MVIE::_FinishManualCameraRecording(void)
{
    bool fExitMode = _fManualCameraRecordExitMode;
    _fManualCameraRecording = fFalse;
    _fManualCameraRecordLiveValid = fFalse;
    _fManualCameraRecordExitMode = fFalse;
    _iscenManualCameraUndo = ivNil;
    _nfrmManualCameraUndo = ivNil;
    ClearPb(&_ctmanManualCameraRecordLive, SIZEOF(CTMAN));

    if (fExitMode)
    {
        _ClearManualCameraEdit();
        _fManualCameraMode = fFalse;
        PMVU pmvu = PmvuCur();
        if (pmvu != pvNil)
            pmvu->EndManualCameraInput();
        InvalViewsAndScb();
    }
    else if (_fManualCameraMode && Pscen() != pvNil)
    {
        _FBeginManualCameraEdit();
        ApplyCameraTrack();
        MarkViews();
    }
}

/******************************************************************************
    _SortCameraTrackTweens

    Keep depth keys in canonical scene/frame order after a trim synthesizes a
    boundary key.  Evaluation scans independently, but the .3ct writer emits
    array order and should always write the surviving tween start before end.
******************************************************************************/
void MVIE::_SortCameraTrackTweens(void)
{
    AssertThis(0);
    for (int32_t i = 1; i < _cctween; i++)
    {
        CTTWEEN ctween = _rgctween[i];
        int32_t j = i;
        while (j > 0 &&
               (_rgctween[j - 1].iscen > ctween.iscen ||
                (_rgctween[j - 1].iscen == ctween.iscen &&
                 _rgctween[j - 1].nfrmFirst > ctween.nfrmFirst)))
        {
            _rgctween[j] = _rgctween[j - 1];
            j--;
        }
        _rgctween[j] = ctween;
    }
}

/******************************************************************************
    FGetCameraTrackState

    Copy the complete .3ct-backed state into an undo snapshot.  The scene
    serializer knows nothing about these arrays, so scene/frame undo records
    explicitly carry them alongside the ordinary SCEN chunk.
******************************************************************************/
bool MVIE::FGetCameraTrackState(CTSTATE *pctstate)
{
    AssertThis(0);
    AssertVarMem(pctstate);
    pctstate->cctween = _cctween;
    pctstate->cctman = _cctman;
    pctstate->clightLab = _clightLab;
    CopyPb(_rgctween, pctstate->rgctween, _cctween * SIZEOF(CTTWEEN));
    CopyPb(_rgctman, pctstate->rgctman, _cctman * SIZEOF(CTMAN));
    CopyPb(_rglightLab, pctstate->rglightLab, _clightLab * SIZEOF(LIGHTLAB));
    CopyPb(_rgfSceneLightsEnabled, pctstate->rgfSceneLightsEnabled, SIZEOF(_rgfSceneLightsEnabled));
    CopyPb(_rgfSceneHideLightObjects, pctstate->rgfSceneHideLightObjects, SIZEOF(_rgfSceneHideLightObjects));
    CopyPb(_rgfSceneDefaultLightingShaders, pctstate->rgfSceneDefaultLightingShaders,
           SIZEOF(_rgfSceneDefaultLightingShaders));
    CopyPb(_rgfSceneLightLabCombineLegacy, pctstate->rgfSceneLightLabCombineLegacy,
           SIZEOF(_rgfSceneLightLabCombineLegacy));
    pctstate->fDefaultLightingShaders = _fDefaultLightingShaders;
    pctstate->fLightLabCombineLegacy = _fLightLabCombineLegacy;
    pctstate->fFreeLookAlwaysLastKnown = _fFreeLookAlwaysLastKnown;
    pctstate->fDontAskLightLabCut = _fDontAskLightLabCut;
    pctstate->fDontAskShaderPropagation = _fDontAskShaderPropagation;
    pctstate->caridNonSelectable = _caridNonSelectable;
    CopyPb(_rgaridNonSelectable, pctstate->rgaridNonSelectable,
           _caridNonSelectable * SIZEOF(int32_t));
    pctstate->cObjectProperties = _cObjectProperties;
    CopyPb(_rgObjectProperties, pctstate->rgObjectProperties,
           _cObjectProperties * SIZEOF(OBJECTPROPERTIES));
    return fTrue;
}

/******************************************************************************
    SwapCameraTrackState

    Exchange the live arrays with an undo snapshot.  Swapping rather than
    copying one way makes the same history object naturally support redo.
******************************************************************************/
void MVIE::SwapCameraTrackState(CTSTATE *pctstate)
{
    AssertThis(0);
    LightEditorLog(this, "camera_light_undo_swap begin live_lights=%ld snap_lights=%ld hidden_count=%ld",
                   (long)_clightLab, pctstate != pvNil ? (long)pctstate->clightLab : -1L,
                   (long)_caridHiddenLightObjects);
    AssertVarMem(pctstate);
    AssertIn(pctstate->cctween, 0, kcctweenMax + 1);
    AssertIn(pctstate->cctman, 0, kcctmanMax + 1);
    AssertIn(pctstate->clightLab, 0, kclightLabMax + 1);
    AssertIn(pctstate->caridNonSelectable, 0, kc4DMMNonSelectableMax + 1);
    AssertIn(pctstate->cObjectProperties, 0, kc4DMMObjectPropertiesMax + 1);

    bool fObjectPropertiesChanges = _cObjectProperties != pctstate->cObjectProperties;
    if (!fObjectPropertiesChanges)
    {
        for (int32_t i = 0; i < _cObjectProperties; ++i)
        {
            if (memcmp(&_rgObjectProperties[i], &pctstate->rgObjectProperties[i], SIZEOF(OBJECTPROPERTIES)) != 0)
            {
                fObjectPropertiesChanges = fTrue;
                break;
            }
        }
    }

    bool fSelectabilityChanges = _caridNonSelectable != pctstate->caridNonSelectable;
    if (!fSelectabilityChanges)
    {
        for (int32_t i = 0; i < _caridNonSelectable; ++i)
        {
            if (_rgaridNonSelectable[i] != pctstate->rgaridNonSelectable[i])
            {
                fSelectabilityChanges = fTrue;
                break;
            }
        }
    }

    const bool fSceneIndexValid = FIn(Iscen(), 0, kc4DMMSceneSettingsMax);
    const bool fLightingPolicyChanges = fSceneIndexValid &&
        (_rgfSceneLightsEnabled[Iscen()] != pctstate->rgfSceneLightsEnabled[Iscen()] ||
         _rgfSceneDefaultLightingShaders[Iscen()] != pctstate->rgfSceneDefaultLightingShaders[Iscen()] ||
         _rgfSceneLightLabCombineLegacy[Iscen()] != pctstate->rgfSceneLightLabCombineLegacy[Iscen()]);
    PBKGD pbkgdLighting = (fLightingPolicyChanges && Pscen() != pvNil) ? Pscen()->Pbkgd() : pvNil;
    if (pbkgdLighting != pvNil)
        pbkgdLighting->TurnOffLights();

    int32_t cmax = LwMax(_cctween, pctstate->cctween);
    for (int32_t i = 0; i < cmax; i++)
    {
        CTTWEEN ctween = _rgctween[i];
        _rgctween[i] = pctstate->rgctween[i];
        pctstate->rgctween[i] = ctween;
    }
    cmax = LwMax(_cctman, pctstate->cctman);
    for (int32_t i = 0; i < cmax; i++)
    {
        CTMAN ctman = _rgctman[i];
        _rgctman[i] = pctstate->rgctman[i];
        pctstate->rgctman[i] = ctman;
    }
    int32_t c = _cctween;
    _cctween = pctstate->cctween;
    pctstate->cctween = c;
    c = _cctman;
    _cctman = pctstate->cctman;
    pctstate->cctman = c;
    cmax = LwMax(_clightLab, pctstate->clightLab);
    for (int32_t i = 0; i < cmax; i++)
    {
        LIGHTLAB light = _rglightLab[i];
        _rglightLab[i] = pctstate->rglightLab[i];
        pctstate->rglightLab[i] = light;
    }
    c = _clightLab;
    _clightLab = pctstate->clightLab;
    pctstate->clightLab = c;
    for (int32_t i = 0; i < kc4DMMSceneSettingsMax; i++)
    {
        bool f = _rgfSceneLightsEnabled[i];
        _rgfSceneLightsEnabled[i] = pctstate->rgfSceneLightsEnabled[i];
        pctstate->rgfSceneLightsEnabled[i] = f;
        f = _rgfSceneHideLightObjects[i];
        _rgfSceneHideLightObjects[i] = pctstate->rgfSceneHideLightObjects[i];
        pctstate->rgfSceneHideLightObjects[i] = f;
        f = _rgfSceneDefaultLightingShaders[i];
        _rgfSceneDefaultLightingShaders[i] = pctstate->rgfSceneDefaultLightingShaders[i];
        pctstate->rgfSceneDefaultLightingShaders[i] = f;
        f = _rgfSceneLightLabCombineLegacy[i];
        _rgfSceneLightLabCombineLegacy[i] = pctstate->rgfSceneLightLabCombineLegacy[i];
        pctstate->rgfSceneLightLabCombineLegacy[i] = f;
    }
    bool f = _fDefaultLightingShaders;
    _fDefaultLightingShaders = pctstate->fDefaultLightingShaders;
    pctstate->fDefaultLightingShaders = f;
    f = _fLightLabCombineLegacy;
    _fLightLabCombineLegacy = pctstate->fLightLabCombineLegacy;
    pctstate->fLightLabCombineLegacy = f;
    f = _fFreeLookAlwaysLastKnown;
    _fFreeLookAlwaysLastKnown = pctstate->fFreeLookAlwaysLastKnown;
    pctstate->fFreeLookAlwaysLastKnown = f;
    f = _fDontAskLightLabCut;
    _fDontAskLightLabCut = pctstate->fDontAskLightLabCut;
    pctstate->fDontAskLightLabCut = f;
    f = _fDontAskShaderPropagation;
    _fDontAskShaderPropagation = pctstate->fDontAskShaderPropagation;
    pctstate->fDontAskShaderPropagation = f;

    cmax = LwMax(_caridNonSelectable, pctstate->caridNonSelectable);
    for (int32_t i = 0; i < cmax; ++i)
    {
        int32_t arid = _rgaridNonSelectable[i];
        _rgaridNonSelectable[i] = pctstate->rgaridNonSelectable[i];
        pctstate->rgaridNonSelectable[i] = arid;
    }
    c = _caridNonSelectable;
    _caridNonSelectable = pctstate->caridNonSelectable;
    pctstate->caridNonSelectable = c;

    cmax = LwMax(_cObjectProperties, pctstate->cObjectProperties);
    for (int32_t i = 0; i < cmax; ++i)
    {
        OBJECTPROPERTIES prop = _rgObjectProperties[i];
        _rgObjectProperties[i] = pctstate->rgObjectProperties[i];
        pctstate->rgObjectProperties[i] = prop;
    }
    c = _cObjectProperties;
    _cObjectProperties = pctstate->cObjectProperties;
    pctstate->cObjectProperties = c;

    if (fSelectabilityChanges && Pscen() != pvNil)
    {
        // Redoing a "make non-selectable" operation must not leave a stale
        // selected/highlighted object merely because Undo made it selectable.
        for (int32_t isel = Pscen()->CactrSelected() - 1; isel >= 0; --isel)
        {
            PACTR pactr = Pscen()->PactrSelectedAt(isel);
            if (pactr != pvNil && !FObjectSelectable(pactr->Arid()))
                Pscen()->SelectActrRemove(pactr);
        }
    }

    vfSceneDynamicLightingActive = FIn(Iscen(), 0, kc4DMMSceneSettingsMax) && _rgfSceneLightsEnabled[Iscen()];
    vfSceneDefaultLightingShadersActive = FIn(Iscen(), 0, kc4DMMSceneSettingsMax) &&
                                          _rgfSceneDefaultLightingShaders[Iscen()];
    vfSceneLightLabCombineLegacyActive = FIn(Iscen(), 0, kc4DMMSceneSettingsMax) &&
                                         _rgfSceneLightLabCombineLegacy[Iscen()];
    if (fLightingPolicyChanges)
    {
        Refresh4DMMMappedMaterialLightingPolicy();
        if (pbkgdLighting != pvNil && _pbwld != pvNil)
            pbkgdLighting->TurnOnLights(_pbwld);
    }
    _UpdateHiddenLightObjects();
    _iscenManualCameraUndo = ivNil;
    _nfrmManualCameraUndo = ivNil;
    _SortCameraTrackTweens();
    RefreshTestLight();
    UpdateTestLightAttachment();
    if (fObjectPropertiesChanges)
    {
        UpdateObjectShadowProperties();
        if (_pbwld != pvNil)
            _pbwld->MarkDirty();
        LightEditorLog(this, "object_properties undo_swap count=%ld", (long)_cObjectProperties);
    }
    if (fSelectabilityChanges)
    {
        if (_pmcc != pvNil)
            _pmcc->UpdateRollCall();
#if defined(KAUAI_WIN32)
        SyncExternalContentBrowserSelection();
#endif
        LightEditorLog(this, "object_selectability undo_swap count=%ld", (long)_caridNonSelectable);
    }
    LightEditorLog(this, "camera_light_undo_swap end live_lights=%ld hidden_count=%ld hide_mode=%d lighting=%d",
                   (long)_clightLab, (long)_caridHiddenLightObjects,
                   (int)FSceneHideLightObjects(Iscen()), (int)FSceneLightsEnabled(Iscen()));
    MarkViews();
}

/******************************************************************************
    TrimCameraTrackAfter

    Remove camera data after the new scene endpoint.  If the cut lands inside
    a depth tween or between manual-camera samples, preserve the exact camera
    at the cut frame as a new terminal key.  Newly appended frames therefore
    hold the cut position instead of resurrecting deleted samples.
******************************************************************************/
void MVIE::TrimCameraTrackAfter(int32_t iscen, int32_t nfrmCut)
{
    AssertThis(0);
    Assert(nfrmCut >= 1, "Bad camera-track cut frame");
    bool fChanged = fFalse;

    // Capture the exact persistent camera pose before deleting later data.
    // Manual interpolation needs a terminal sample when its later key is cut.
    // A depth tween also needs one when the cut lands on its first frame,
    // because shortening that tween to one frame removes the tween entirely.
    CTMAN ctmanCut;
    bool fManualAfterCut = fFalse;
    for (int32_t i = 0; i < _cctman; i++)
    {
        if (_rgctman[i].iscen == iscen && _rgctman[i].nfrm > nfrmCut)
        {
            fManualAfterCut = fTrue;
            break;
        }
    }
    const int32_t itweenCutOld = _ItweenAtFrame(iscen, nfrmCut);
    const bool fTweenCrossCut = itweenCutOld != ivNil &&
                                _rgctween[itweenCutOld].nfrmLast > nfrmCut;
    const bool fBoundary = (fManualAfterCut || fTweenCrossCut) &&
                           _FGetPersistentCameraTrackPose(iscen, nfrmCut, &ctmanCut);

    int32_t dst = 0;
    for (int32_t i = 0; i < _cctween; i++)
    {
        CTTWEEN t = _rgctween[i];
        if (t.iscen == iscen)
        {
            if (t.nfrmFirst > nfrmCut)
            {
                fChanged = fTrue;
                continue;
            }
            if (t.nfrmLast > nfrmCut)
            {
                const double r = (double)(nfrmCut - t.nfrmFirst) /
                                 (double)(t.nfrmLast - t.nfrmFirst);
                t.zLast = (float)((double)t.zFirst +
                                  ((double)t.zLast - (double)t.zFirst) * r);
                t.nfrmLast = nfrmCut;
                fChanged = fTrue;
                if (t.nfrmFirst >= t.nfrmLast)
                    continue;
            }
        }
        _rgctween[dst++] = t;
    }
    _cctween = dst;

    dst = 0;
    for (int32_t i = 0; i < _cctman; i++)
    {
        if (_rgctman[i].iscen == iscen && _rgctman[i].nfrm > nfrmCut)
        {
            fChanged = fTrue;
            continue;
        }
        _rgctman[dst++] = _rgctman[i];
    }
    _cctman = dst;
    // If the cut destroyed the camera segment that used to own this frame,
    // preserve that exact pose as a manual boundary.  A surviving shortened
    // tween remains authoritative and needs no extra manual sample.
    if (fBoundary && _ItweenAtFrame(iscen, nfrmCut) == ivNil)
    {
        ctmanCut.iscen = iscen;
        ctmanCut.nfrm = nfrmCut;
        CTMAN *p = _PctmanFind(iscen, nfrmCut, fTrue);
        if (p != pvNil)
        {
            *p = ctmanCut;
            MVIE::MultiLog(this, "camera_trim_after manual_boundary scene=%ld frame=%ld",
                           (long)iscen, (long)nfrmCut);
        }
    }
    _SortCameraTrackTweens();
    if (fChanged)
        SetDirty();
}

/******************************************************************************
    TrimCameraTrackBefore

    Remove camera data before the new first frame and renumber surviving keys.
    When the cut occurs after a track has begun, frame 1 receives the exact
    interpolated camera state from the old cut frame so the remaining movie is
    visually identical, merely shorter.
******************************************************************************/
void MVIE::TrimCameraTrackBefore(int32_t iscen, int32_t nfrmCut)
{
    AssertThis(0);
    Assert(nfrmCut >= 1, "Bad camera-track cut frame");
    if (nfrmCut <= 1)
        return;

    bool fChanged = fFalse;
    int32_t dnfrm = nfrmCut - 1;

    // Capture the exact old cut-frame pose before any frame numbers are
    // rebased.  If the cut removes the camera segment that supplied this pose
    // (for example, cutting before the last frame of a depth tween), frame 1
    // must receive an equivalent manual-camera sample.
    CTMAN ctmanCut;
    const bool fBoundary = _FGetPersistentCameraTrackPose(iscen, nfrmCut, &ctmanCut);

    int32_t dst = 0;
    for (int32_t i = 0; i < _cctween; i++)
    {
        CTTWEEN t = _rgctween[i];
        if (t.iscen == iscen)
        {
            if (t.nfrmLast < nfrmCut)
            {
                fChanged = fTrue;
                continue;
            }
            if (t.nfrmFirst < nfrmCut)
            {
                const double r = (double)(nfrmCut - t.nfrmFirst) /
                                 (double)(t.nfrmLast - t.nfrmFirst);
                t.zFirst = (float)((double)t.zFirst +
                                   ((double)t.zLast - (double)t.zFirst) * r);
                t.nfrmFirst = 1;
                t.nfrmLast -= dnfrm;
            }
            else
            {
                t.nfrmFirst -= dnfrm;
                t.nfrmLast -= dnfrm;
            }
            fChanged = fTrue;
            if (t.nfrmFirst >= t.nfrmLast)
                continue;
        }
        _rgctween[dst++] = t;
    }
    _cctween = dst;

    dst = 0;
    for (int32_t i = 0; i < _cctman; i++)
    {
        CTMAN m = _rgctman[i];
        if (m.iscen == iscen)
        {
            if (m.nfrm < nfrmCut)
            {
                fChanged = fTrue;
                continue;
            }
            m.nfrm -= dnfrm;
            fChanged = fTrue;
        }
        _rgctman[dst++] = m;
    }
    _cctman = dst;
    // A surviving tween beginning on the new frame 1 already owns the exact
    // cut pose.  Otherwise preserve that pose explicitly, including the case
    // where the old cut frame was the final frame of a tween and the tween
    // therefore collapsed to zero duration.
    if (fBoundary && _ItweenAtFrame(iscen, 1) == ivNil)
    {
        ctmanCut.iscen = iscen;
        ctmanCut.nfrm = 1;
        CTMAN *p = _PctmanFind(iscen, 1, fTrue);
        if (p != pvNil)
        {
            *p = ctmanCut;
            MVIE::MultiLog(this, "camera_trim_before manual_boundary scene=%ld old_frame=%ld new_frame=1",
                           (long)iscen, (long)nfrmCut);
        }
    }
    _SortCameraTrackTweens();
    if (fChanged)
        SetDirty();
}

/******************************************************************************
    FInsertCameraTrackFrameAfter

    Insert one camera-track frame after nfrm while preserving every existing
    camera value.  A tween which crosses the insertion point is split into
    two mathematically equivalent linear segments; endpoint insertions use one
    exact manual sample where a one-frame segment cannot be represented.
******************************************************************************/
bool MVIE::FInsertCameraTrackFramesAfter(int32_t iscen, int32_t nfrm,
                                         int32_t cfrm)
{
    AssertThis(0);
    Assert(nfrm >= 1, "Bad camera-track insertion frame");
    AssertIn(cfrm, 0, klwMax);

    if (cfrm <= 0)
        return fTrue;

    int32_t itweenAtFrame = _ItweenAtFrame(iscen, nfrm);
    int32_t cExtraTweens = 0;
    if (itweenAtFrame != ivNil &&
        nfrm < _rgctween[itweenAtFrame].nfrmLast)
    {
        const CTTWEEN &t = _rgctween[itweenAtFrame];
        if (nfrm == t.nfrmFirst)
        {
            // The shifted original replaces the old tween. A separate hold is
            // valid only when it has at least two non-overlapping frames.
            cExtraTweens = cfrm >= 2 ? 1 : 0;
        }
        else
        {
            // Splitting the original produces left + right. Add a third hold
            // only when at least two free interior frames exist between them.
            cExtraTweens = 1 + (cfrm >= 3 ? 1 : 0);
        }
    }

    CTMAN ctmanAtFrame;
    bool fManualAtFrame = itweenAtFrame == ivNil &&
                          _FGetManualCameraState(iscen, nfrm, &ctmanAtFrame);
    bool fFutureManual = fFalse;
    for (int32_t i = 0; i < _cctman; i++)
    {
        if (_rgctman[i].iscen == iscen && _rgctman[i].nfrm > nfrm)
        {
            fFutureManual = fTrue;
            break;
        }
    }
    int32_t cExtraManual = fManualAtFrame && fFutureManual ? 1 : 0;

    if (_cctween + cExtraTweens > kcctweenMax ||
        _cctman + cExtraManual > kcctmanMax)
    {
        return fFalse;
    }

    // Exact manual samples later than the insertion point move with their old
    // timeline frames.  When another sample exists later, an implicit hold
    // key at the end of the inserted range prevents the old interpolation
    // from being stretched across the new frames.
    for (int32_t i = _cctman - 1; i >= 0; i--)
    {
        if (_rgctman[i].iscen == iscen && _rgctman[i].nfrm > nfrm)
            _rgctman[i].nfrm += cfrm;
    }

    CTTWEEN rgtweenNew[kcctweenMax];
    int32_t cctweenNew = 0;
    int32_t idNext = _ItweenNewId();
    for (int32_t i = 0; i < _cctween; i++)
    {
        CTTWEEN t = _rgctween[i];
        if (t.iscen != iscen)
        {
            rgtweenNew[cctweenNew++] = t;
            continue;
        }

        if (t.nfrmFirst > nfrm)
        {
            t.nfrmFirst += cfrm;
            t.nfrmLast += cfrm;
            rgtweenNew[cctweenNew++] = t;
            continue;
        }

        if (t.nfrmLast <= nfrm)
        {
            // The evaluator holds the final tween value until another camera
            // control changes it.  Extending the scene after a completed tween
            // therefore requires no redundant .3ct entry and cannot snap back
            // toward the baseline camera.
            rgtweenNew[cctweenNew++] = t;
            continue;
        }

        float r = (float)(nfrm - t.nfrmFirst) /
                  (float)(t.nfrmLast - t.nfrmFirst);
        float zCur = t.zFirst + (t.zLast - t.zFirst) * r;

        if (nfrm == t.nfrmFirst)
        {
            if (cfrm >= 2)
            {
                // Keep the inserted hold off the shifted original's first
                // frame. Camera tweens are inclusive and may not share a frame.
                CTTWEEN hold = t;
                hold.nfrmLast = nfrm + cfrm - 1;
                hold.zLast = t.zFirst;
                rgtweenNew[cctweenNew++] = hold;

                CTTWEEN right = t;
                right.id = idNext++;
                right.nfrmFirst = nfrm + cfrm;
                right.nfrmLast += cfrm;
                right.zFirst = t.zFirst;
                rgtweenNew[cctweenNew++] = right;
            }
            else
            {
                t.nfrmFirst += cfrm;
                t.nfrmLast += cfrm;
                rgtweenNew[cctweenNew++] = t;
            }
            continue;
        }

        // Keep the original motion through the selected frame, hold that exact
        // camera value across the free inserted interior, then resume the
        // shifted remainder. Endpoints remain owned only by left/right.
        CTTWEEN left = t;
        left.nfrmLast = nfrm;
        left.zLast = zCur;
        rgtweenNew[cctweenNew++] = left;

        if (cfrm >= 3)
        {
            CTTWEEN hold = t;
            hold.id = idNext++;
            hold.nfrmFirst = nfrm + 1;
            hold.nfrmLast = nfrm + cfrm - 1;
            hold.zFirst = zCur;
            hold.zLast = zCur;
            rgtweenNew[cctweenNew++] = hold;
        }

        CTTWEEN right = t;
        right.id = idNext++;
        right.nfrmFirst = nfrm + cfrm;
        right.nfrmLast += cfrm;
        right.zFirst = zCur;
        rgtweenNew[cctweenNew++] = right;
    }

    _cctween = cctweenNew;
    CopyPb(rgtweenNew, _rgctween, _cctween * SIZEOF(CTTWEEN));

    if (cExtraManual != 0)
    {
        ctmanAtFrame.iscen = iscen;
        ctmanAtFrame.nfrm = nfrm + cfrm;
        ctmanAtFrame.fImplicit = fTrue;
        CTMAN *pctman = _PctmanFind(iscen, ctmanAtFrame.nfrm, fTrue);
        if (pctman == pvNil)
            return fFalse;
        *pctman = ctmanAtFrame;
    }

    // Light Lab lifetimes use the same scene-relative frame coordinates as
    // the camera track. Keep finite boundaries attached to the original
    // timeline when frames are inserted. Zero remains the open-ended legacy
    // sentinel in either field.
    for (int32_t ilight = 0; ilight < _clightLab; ++ilight)
    {
        LIGHTLAB &light = _rglightLab[ilight];
        if (light.iscen != iscen)
            continue;
        if (light.nfrmSpawn > nfrm)
            light.nfrmSpawn += cfrm;
        if (light.nfrmDespawn > nfrm)
            light.nfrmDespawn += cfrm;
    }

    _SortCameraTrackTweens();
    _iscenManualCameraUndo = ivNil;
    _nfrmManualCameraUndo = ivNil;
    return fTrue;
}

/******************************************************************************
    FInsertCameraTrackFramesBefore

    Insert duplicate camera time before nfrm.  The inserted frames use the
    exact camera state of the old nfrm, while every old frame keeps its former
    camera value after moving forward by cfrm.
******************************************************************************/
bool MVIE::FInsertCameraTrackFramesBefore(int32_t iscen, int32_t nfrm,
                                          int32_t cfrm)
{
    AssertThis(0);
    Assert(nfrm >= 1, "Bad camera-track insertion frame");
    AssertIn(cfrm, 0, klwMax);
    if (cfrm <= 0)
        return fTrue;

    int32_t cExtraTweens = 0;
    for (int32_t i = 0; i < _cctween; i++)
    {
        const CTTWEEN &t = _rgctween[i];
        if (t.iscen != iscen)
            continue;
        if (t.nfrmFirst == nfrm)
        {
            // The shifted original replaces the old tween. A separate hold
            // needs at least two frames before the shifted first frame.
            cExtraTweens += cfrm >= 2 ? 1 : 0;
        }
        else if (t.nfrmFirst < nfrm && nfrm <= t.nfrmLast)
        {
            // Left replaces the original. Right is additional when motion
            // survives the boundary; the hold is additional only when valid.
            if (nfrm < t.nfrmLast)
                cExtraTweens++;
            if (cfrm >= 3)
                cExtraTweens++;
        }
    }

    int32_t itweenAtFrame = _ItweenAtFrame(iscen, nfrm);
    CTMAN ctmanAtFrame;
    bool fManualAtFrame = itweenAtFrame == ivNil &&
                          _FGetManualCameraState(iscen, nfrm, &ctmanAtFrame);
    bool fExactManual = _PctmanFind(iscen, nfrm, fFalse) != pvNil;
    bool fFutureManual = fFalse;
    for (int32_t i = 0; i < _cctman; i++)
    {
        if (_rgctman[i].iscen == iscen && _rgctman[i].nfrm > nfrm)
        {
            fFutureManual = fTrue;
            break;
        }
    }

    int32_t cExtraManual = 0;
    if (fManualAtFrame)
    {
        if (fExactManual)
            cExtraManual = 1; // old exact key shifts; duplicate it at new nfrm
        else if (fFutureManual)
            cExtraManual = 2; // preserve left interpolation, then hold to old nfrm
    }

    if (_cctween + cExtraTweens > kcctweenMax ||
        _cctman + cExtraManual > kcctmanMax)
    {
        return fFalse;
    }

    for (int32_t i = _cctman - 1; i >= 0; i--)
    {
        if (_rgctman[i].iscen == iscen && _rgctman[i].nfrm >= nfrm)
            _rgctman[i].nfrm += cfrm;
    }

    CTTWEEN rgtweenNew[kcctweenMax];
    int32_t cctweenNew = 0;
    int32_t idNext = _ItweenNewId();
    for (int32_t i = 0; i < _cctween; i++)
    {
        CTTWEEN t = _rgctween[i];
        if (t.iscen != iscen)
        {
            rgtweenNew[cctweenNew++] = t;
            continue;
        }

        if (t.nfrmFirst > nfrm)
        {
            t.nfrmFirst += cfrm;
            t.nfrmLast += cfrm;
            rgtweenNew[cctweenNew++] = t;
            continue;
        }

        if (t.nfrmFirst == nfrm)
        {
            if (cfrm >= 2)
            {
                CTTWEEN hold = t;
                hold.nfrmLast = nfrm + cfrm - 1;
                hold.zLast = t.zFirst;
                rgtweenNew[cctweenNew++] = hold;

                CTTWEEN right = t;
                right.id = idNext++;
                right.nfrmFirst += cfrm;
                right.nfrmLast += cfrm;
                rgtweenNew[cctweenNew++] = right;
            }
            else
            {
                t.nfrmFirst += cfrm;
                t.nfrmLast += cfrm;
                rgtweenNew[cctweenNew++] = t;
            }
            continue;
        }

        if (t.nfrmLast < nfrm)
        {
            rgtweenNew[cctweenNew++] = t;
            continue;
        }

        float r = (float)(nfrm - t.nfrmFirst) /
                  (float)(t.nfrmLast - t.nfrmFirst);
        float zAt = t.zFirst + (t.zLast - t.zFirst) * r;

        CTTWEEN left = t;
        left.nfrmLast = nfrm;
        left.zLast = zAt;
        rgtweenNew[cctweenNew++] = left;

        if (cfrm >= 3)
        {
            CTTWEEN hold = t;
            hold.id = idNext++;
            hold.nfrmFirst = nfrm + 1;
            hold.nfrmLast = nfrm + cfrm - 1;
            hold.zFirst = zAt;
            hold.zLast = zAt;
            rgtweenNew[cctweenNew++] = hold;
        }

        if (nfrm < t.nfrmLast)
        {
            CTTWEEN right = t;
            right.id = idNext++;
            right.nfrmFirst = nfrm + cfrm;
            right.nfrmLast += cfrm;
            right.zFirst = zAt;
            rgtweenNew[cctweenNew++] = right;
        }
    }

    _cctween = cctweenNew;
    CopyPb(rgtweenNew, _rgctween, _cctween * SIZEOF(CTTWEEN));

    if (cExtraManual != 0)
    {
        ctmanAtFrame.iscen = iscen;
        ctmanAtFrame.nfrm = nfrm;
        ctmanAtFrame.fImplicit = fTrue;
        CTMAN *pctman = _PctmanFind(iscen, nfrm, fTrue);
        if (pctman == pvNil)
            return fFalse;
        *pctman = ctmanAtFrame;

        if (!fExactManual)
        {
            ctmanAtFrame.nfrm = nfrm + cfrm;
            ctmanAtFrame.fImplicit = fTrue;
            pctman = _PctmanFind(iscen, ctmanAtFrame.nfrm, fTrue);
            if (pctman == pvNil)
                return fFalse;
            *pctman = ctmanAtFrame;
        }
    }

    // Frames inserted before nfrm duplicate the state visible at nfrm. A
    // light which begins exactly there therefore remains active throughout
    // the inserted copies, while a finite despawn at nfrm moves with the old
    // boundary. Zero sentinels are intentionally untouched.
    for (int32_t ilight = 0; ilight < _clightLab; ++ilight)
    {
        LIGHTLAB &light = _rglightLab[ilight];
        if (light.iscen != iscen)
            continue;
        if (light.nfrmSpawn > nfrm)
            light.nfrmSpawn += cfrm;
        if (light.nfrmDespawn >= nfrm && light.nfrmDespawn > 0)
            light.nfrmDespawn += cfrm;
    }

    _SortCameraTrackTweens();
    _iscenManualCameraUndo = ivNil;
    _nfrmManualCameraUndo = ivNil;
    return fTrue;
}

/******************************************************************************
    FPrependCameraTrackFrames

    The original Ctrl+Previous-at-first-frame path moves the scene's first
    frame backward.  Apply the same insertion transform to .3ct data so the
    new blank frame inherits the old first frame's camera instead of snapping
    to the unmodified baseline.
******************************************************************************/
bool MVIE::FPrependCameraTrackFrames(int32_t iscen, int32_t cfrm)
{
    AssertThis(0);
    return FInsertCameraTrackFramesBefore(iscen, 1, cfrm);
}

/******************************************************************************
    _ClearManualCameraEdit
******************************************************************************/
void MVIE::_ClearManualCameraEdit(void)
{
    _fManualCameraEditValid = fFalse;
    _fManualCameraEditHadSample = fFalse;
    _iscenManualCameraEdit = ivNil;
    _nfrmManualCameraEdit = ivNil;
    ClearPb(&_ctmanManualCameraEditStart, SIZEOF(CTMAN));
    ClearPb(&_ctmanManualCameraEditLive, SIZEOF(CTMAN));
}

/******************************************************************************
    _FBeginManualCameraEdit

    Begin one transactional manual-camera edit. Mouse/WASD only changes the
    live viewport copy until Enter/Tab, an arrow key or Ctrl commits it.
******************************************************************************/
bool MVIE::_FBeginManualCameraEdit(void)
{
    if (Pscen() == pvNil || FDepthMotionTweenActive())
        return fFalse;

    int32_t nfrm = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;
    CTMAN ctman;
    if (!_FGetCameraControlState(Iscen(), nfrm, &ctman))
        return fFalse;

    ctman.iscen = Iscen();
    ctman.nfrm = nfrm;
    ctman.fImplicit = fFalse;
    _ctmanManualCameraEditStart = ctman;
    _ctmanManualCameraEditLive = ctman;
    _iscenManualCameraEdit = Iscen();
    _nfrmManualCameraEdit = nfrm;
    CTMAN *pctmanExact = _PctmanFind(Iscen(), nfrm, fFalse);
    _fManualCameraEditHadSample = pctmanExact != pvNil && !pctmanExact->fImplicit;
    _fManualCameraEditValid = fTrue;
    return fTrue;
}

/******************************************************************************
    FCommitManualCameraEdit
******************************************************************************/
bool MVIE::FCommitManualCameraEdit(bool fExitMode)
{
    AssertThis(0);

    if (!_fManualCameraMode || !_fManualCameraEditValid || Pscen() == pvNil)
        return fFalse;
    if (_iscenManualCameraEdit != Iscen())
        return fFalse;

    CTMAN live = _ctmanManualCameraEditLive;
    live.iscen = _iscenManualCameraEdit;
    live.nfrm = _nfrmManualCameraEdit;
    live.fImplicit = fFalse;

    CTMAN *pold = _PctmanFind(live.iscen, live.nfrm, fFalse);
    bool fChanged = pold == pvNil || pold->fImplicit ||
                    pold->x != live.x || pold->y != live.y || pold->z != live.z ||
                    pold->pitch != live.pitch || pold->yaw != live.yaw;

    if (fChanged)
    {
        if (!FAddCameraTrackUndo(PszLit("Manual Camera Frame")))
            return fFalse;
        CTMAN *pdst = _PctmanFind(live.iscen, live.nfrm, fTrue);
        if (pdst == pvNil)
        {
            ClearUndo();
            return fFalse;
        }
        *pdst = live;
        SetDirty();
    }

    _ctmanManualCameraEditStart = live;
    _ctmanManualCameraEditLive = live;
    _fManualCameraEditHadSample = fTrue;
    _pmcc->UpdateScrollbars();
    ApplyCameraTrack();
    MarkViews();

    if (fExitMode)
    {
        _ClearManualCameraEdit();
        _fManualCameraMode = fFalse;
        PMVU pmvu = PmvuCur();
        if (pmvu != pvNil)
            pmvu->EndManualCameraInput();
        InvalViewsAndScb();
    }
    return fTrue;
}

/******************************************************************************
    CancelManualCameraEdit
******************************************************************************/
void MVIE::CancelManualCameraEdit(void)
{
    AssertThis(0);

    _ClearManualCameraEdit();
    _fManualCameraMode = fFalse;
    PMVU pmvu = PmvuCur();
    if (pmvu != pvNil)
        pmvu->EndManualCameraInput();
    if (Pscen() != pvNil)
    {
        ApplyCameraTrack();
        MarkViews();
    }
    InvalViewsAndScb();
}

/******************************************************************************
    FStepManualCamera
******************************************************************************/
bool MVIE::FStepManualCamera(int32_t dnfrm)
{
    AssertThis(0);
    if (!_fManualCameraMode || _fManualCameraRecording || Pscen() == pvNil ||
        (dnfrm != -1 && dnfrm != 1))
        return fFalse;

    int32_t nfrmDestAbs = Pscen()->Nfrm() + dnfrm;
    if (nfrmDestAbs < Pscen()->NfrmFirst() || nfrmDestAbs > Pscen()->NfrmLast())
        return fFalse;
    int32_t nfrmDest = nfrmDestAbs - Pscen()->NfrmFirst() + 1;

    // The arrow action always saves the current manual-camera frame first.
    // A depth-tween collision blocks only the move, not the save the user just
    // requested by pressing the arrow key.
    if (!FCommitManualCameraEdit(fFalse))
        return fFalse;

    if (_ItweenAtFrame(Iscen(), nfrmDest) != ivNil)
    {
#if defined(KAUAI_WIN32)
        achar rgch[256];
        sprintf(rgch,
                "Can't use %ld for manual camera mode because this frame currently has camera positioning changes which aren't from manual camera mode applied to it.",
                (long)nfrmDest);
        MessageBoxA(vwig.hwndApp, rgch, "Manual Camera Angle", MB_OK | MB_ICONEXCLAMATION);
#endif
        return fFalse;
    }

    _ClearManualCameraEdit();
    if (!Pscen()->FGotoFrm(nfrmDestAbs) || !_FBeginManualCameraEdit())
        return fFalse;
    ApplyCameraTrack();
    MarkViews();
    _pmcc->UpdateScrollbars();
    return fTrue;
}

bool MVIE::FSetManualCameraMode(bool fEnable)
{
    AssertThis(0);

    fEnable = FPure(fEnable);
    if (fEnable && (Pscen() == pvNil || FPlaying() || FDepthMotionTweenActive()))
        fEnable = fFalse;

    if (fEnable && (_fFreeLookMode || _fFreeLookOverride))
    {
        _ClearFreeLookState();
        if (Pscen() != pvNil)
        {
            ApplyCameraTrack();
            MarkViews();
        }
    }

    if (!fEnable && _fManualCameraRecording)
    {
        if (FPlaying() && !FStopPlaying())
            vpcex->EnqueueCid(cidPlay, pvNil);
        _FinishManualCameraRecording();
    }

    bool fWasEnabled = _fManualCameraMode;
    _fManualCameraMode = fEnable;
    if (_fManualCameraMode && (!fWasEnabled || !_fManualCameraEditValid))
    {
        // A deliberate entry click must also repair a stale mode flag whose
        // transactional frame state was already cleared.
        if (!_FBeginManualCameraEdit())
            _fManualCameraMode = fFalse;
    }
    else if (!_fManualCameraMode && fWasEnabled)
    {
        _ClearManualCameraEdit();
    }

    if (fWasEnabled != _fManualCameraMode)
    {
        _iscenManualCameraUndo = ivNil;
        _nfrmManualCameraUndo = ivNil;
    }

    PMVU pmvu = PmvuCur();
    if (pmvu != pvNil)
    {
        // Entering Manual Camera is idempotent.  A prior focus transition can
        // leave the mode flag set after input capture has been released; a
        // deliberate button click must always re-arm the viewport instead of
        // becoming a silent no-op.
        if (_fManualCameraMode)
            pmvu->BeginManualCameraInput();
        else if (fWasEnabled)
            pmvu->EndManualCameraInput();
    }

    if (_fManualCameraMode && Pscen() != pvNil)
    {
        Pscen()->SelectActr(pvNil);
        Pscen()->SelectTbox(pvNil);
        Pbwld()->MarkDirty();
        ApplyCameraTrack();
        InvalViewsAndScb();
    }

    return FPure(_fManualCameraMode);
}

/******************************************************************************
    FStartManualCameraRecording

    R begins playback without leaving Manual Camera Mode.  One scene/camera
    snapshot covers the complete recording pass, while each reached frame gets
    an exact manual-camera sample at its beginning.  No timeline frames are
    created here.
******************************************************************************/
bool MVIE::FStartManualCameraRecording(void)
{
    AssertThis(0);

    if (!_fManualCameraMode || _fManualCameraRecording || Pscen() == pvNil ||
        FPlaying() || FDepthMotionTweenActive())
    {
        return fFalse;
    }

    // Match Play()'s rewind rule before taking the undo snapshot and initial
    // camera sample, so recording from the movie endpoint starts at frame one.
    if ((Iscen() + 1 == Cscen()) && (Pscen()->Nfrm() == Pscen()->NfrmLast()))
    {
        if (!FSwitchScen(0) || !Pscen()->FGotoFrm(Pscen()->NfrmFirst()))
            return fFalse;
    }

    int32_t nfrm = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;
    if (_PctmanFind(Iscen(), nfrm, fFalse) == pvNil && _cctman >= kcctmanMax)
        return fFalse;
    if (_fManualCameraEditValid && _iscenManualCameraEdit == Iscen() &&
        _nfrmManualCameraEdit == nfrm)
    {
        _ctmanManualCameraRecordLive = _ctmanManualCameraEditLive;
    }
    else if (!_FGetCameraControlState(Iscen(), nfrm, &_ctmanManualCameraRecordLive))
    {
        return fFalse;
    }

    if (!FAddCameraTrackUndo(PszLit("Manual Camera Recording")))
        return fFalse;

    _fManualCameraRecording = fTrue;
    _fManualCameraRecordLiveValid = fTrue;
    _fManualCameraRecordExitMode = fFalse;
    _ClearManualCameraEdit();
    if (!_FCaptureManualCameraFrame())
    {
        _FinishManualCameraRecording();
        return fFalse;
    }

    // Do not enter the playback engine recursively from MVU::FCmdIdle.
    // Queue the same Studio command used by the Play button and playback-only
    // startup so playback begins after the current idle command unwinds.
    vpcex->EnqueueCid(cidPlay, pvNil);
    return fTrue;
}

/******************************************************************************
    RequestStopManualCameraRecording
******************************************************************************/
void MVIE::RequestStopManualCameraRecording(void)
{
    AssertThis(0);

    if (!_fManualCameraRecording)
        return;

    if (FPlaying())
    {
        // Match the start path: let Studio process the ordinary Play/Stop
        // command after this idle event instead of re-entering Play() here.
        if (!FStopPlaying())
            vpcex->EnqueueCid(cidPlay, pvNil);
    }
    else
    {
        _FinishManualCameraRecording();
    }
}

/******************************************************************************
    FStartFreeLook

    Free Look is a temporary viewport override.  It never writes a CTMAN
    sample, never dirties the movie, and survives Tab/Esc on the same frame so
    ordinary actor editing can continue from the alternate viewpoint.
******************************************************************************/
bool MVIE::FStartFreeLook(bool fOppositeStartPolicy)
{
    AssertThis(0);

    if (_fManualCameraMode || _fFreeLookMode || Pscen() == pvNil || FPlaying())
        return fFalse;

    int32_t nfrm = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;
    CTMAN ctmanSaved;
    if (!_FGetCameraControlState(Iscen(), nfrm, &ctmanSaved))
        return fFalse;

    // A retained override is the current editing viewpoint, but the reset
    // target is always the frame's actual saved camera state.  Keeping those
    // two concepts separate prevents repeated F sessions from turning the
    // previous temporary viewpoint into a counterfeit camera home.
    CTMAN ctmanLive = ctmanSaved;
    if (_fFreeLookOverride && _iscenFreeLook == Iscen() && _nfrmFreeLook == nfrm)
        ctmanLive = _ctmanFreeLookLive;

    // Normal F follows the Advanced Settings policy; Ctrl+F asks for the
    // opposite policy for this one transition.
    bool fUseLastKnown = FPure(_fFreeLookAlwaysLastKnown);
    if (fOppositeStartPolicy)
        fUseLastKnown = !fUseLastKnown;

    // Browser camera-follow modifies BWLD's live camera translation directly,
    // while Free Cam normally wakes up from the authored/.3ct CTMAN pose.
    // Capture that live translation before detaching browser-follow and fold
    // it back into CTMAN coordinates.  F therefore begins exactly where the
    // user is currently looking instead of jumping to the last Free Cam pose.
    if (!fUseLastKnown && _fBrowserCameraFollow && Pbwld() != pvNil && Pscen()->Pbkgd() != pvNil)
    {
        BMAT34 bmat34Base;
        BMAT34 bmat34Move;
        BMAT34 bmat34Current;
        BRS zrHither;
        BRS zrYon;
        BRA aFov;
        if (Pscen()->Pbkgd()->FGetCameraBase(&bmat34Base, &zrHither, &zrYon, &aFov))
        {
            Pscen()->Pbkgd()->GetMouseMatrix(&bmat34Move);
            Pbwld()->GetCamera(&bmat34Current, &zrHither, &zrYon, &aFov);

            const BRS dx = bmat34Current.m[3][0] - bmat34Base.m[3][0];
            const BRS dy = bmat34Current.m[3][1] - bmat34Base.m[3][1];
            const BRS dz = bmat34Current.m[3][2] - bmat34Base.m[3][2];
            const BRS xr = BrsMul(dx, bmat34Move.m[0][0]) +
                           BrsMul(dy, bmat34Move.m[0][1]) +
                           BrsMul(dz, bmat34Move.m[0][2]);
            const BRS yr = BrsMul(dx, bmat34Move.m[1][0]) +
                           BrsMul(dy, bmat34Move.m[1][1]) +
                           BrsMul(dz, bmat34Move.m[1][2]);
            const BRS zr = -(BrsMul(dx, bmat34Move.m[2][0]) +
                             BrsMul(dy, bmat34Move.m[2][1]) +
                             BrsMul(dz, bmat34Move.m[2][2]));
            ctmanLive.x = BrScalarToFloat(xr);
            ctmanLive.y = BrScalarToFloat(yr);
            ctmanLive.z = BrScalarToFloat(zr);
            DiagLog("freecam inherit browser camera scene=%ld frame=%ld xyz=(%.3f,%.3f,%.3f)",
                    (long)Iscen(), (long)nfrm, (double)ctmanLive.x,
                    (double)ctmanLive.y, (double)ctmanLive.z);
        }
    }

    // Free Cam must take exclusive control of the camera.  The external
    // actor/prop browser can otherwise keep translating the camera every
    // idle tick when "Move cam with selected object" is checked.
    if (_fBrowserCameraFollow)
        SetBrowserCameraFollow(aridNil, fFalse);
#if defined(KAUAI_WIN32)
    // Do not wait for the native browser's polling timer to mirror this.
    // Free Cam owns the camera immediately, so visibly/logically clear every
    // open browser follow checkbox in the same F-key transition.
    CancelExternalBrowserCameraFollowForFreeCam();
#endif

    _ctmanFreeLookStart = ctmanSaved;
    _ctmanFreeLookLive = ctmanLive;
    _iscenFreeLook = Iscen();
    _nfrmFreeLook = nfrm;
    _fFreeLookOverride = fTrue;
    _fFreeLookMode = fTrue;

    PMVU pmvu = PmvuCur();
    if (pmvu != pvNil)
        pmvu->BeginManualCameraInput();

    ApplyCameraTrack();
    MarkViews();
    return fTrue;
}

/******************************************************************************
    ResetFreeLook

    F restores the frame's actual saved camera viewpoint and deliberately
    leaves Free Look active.  Tab and Esc are the separate exit-without-revert
    controls.
******************************************************************************/
void MVIE::ResetFreeLook(void)
{
    AssertThis(0);

    if (!_fFreeLookMode || !_fFreeLookOverride || Pscen() == pvNil)
        return;

    int32_t nfrm = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;
    if (_iscenFreeLook != Iscen() || _nfrmFreeLook != nfrm)
        return;

    CTMAN ctmanSaved;
    if (!_FGetCameraControlState(Iscen(), nfrm, &ctmanSaved))
    {
        EndFreeLook(fTrue);
        return;
    }

    // F means the real camera attached to this frame, not the last temporary
    // place from which Free Look happened to be entered.
    _ctmanFreeLookStart = ctmanSaved;
    _ctmanFreeLookLive = ctmanSaved;
    ApplyCameraTrack();
    MarkViews();
}

/******************************************************************************
    EndFreeLook
******************************************************************************/
void MVIE::EndFreeLook(bool fRevert)
{
    AssertThis(0);

    if (fRevert)
    {
        _ClearFreeLookState();
    }
    else
    {
        // Leave the temporary viewpoint in the viewport for ordinary editing,
        // but release FPS input and the hidden/clipped cursor immediately.
        _fFreeLookMode = fFalse;
        PMVU pmvu = PmvuCur();
        if (pmvu != pvNil)
            pmvu->EndManualCameraInput();
    }

    if (fRevert && Pscen() != pvNil)
    {
        ApplyCameraTrack();
        MarkViews();
    }
}

/******************************************************************************
    FMoveManualCamera

    Manual Camera stores an exact sample on the current frame.  Record Mode and
    Free Look instead move a transient live camera: Record Mode commits it only
    when playback reaches the beginning of a frame, while Free Look never
    commits it at all.
******************************************************************************/
bool MVIE::FMoveManualCamera(float dForward, float dStrafe, bool fFly, bool fUpdateView)
{
    AssertThis(0);

    bool fFreeLook = _fFreeLookMode;
    bool fRecord = _fManualCameraMode && _fManualCameraRecording;
    bool fManual = _fManualCameraMode && !fRecord;
    if ((!fFreeLook && !fRecord && !fManual) || Pscen() == pvNil ||
        Pscen()->Pbkgd() == pvNil || Pbwld() == pvNil ||
        (FPlaying() && !fRecord) ||
        (_fManualCameraMode && FDepthMotionTweenActive()) ||
        (dForward == 0.0f && dStrafe == 0.0f))
    {
        return fFalse;
    }

    int32_t nfrm = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;
    CTMAN ctman;
    if (fFreeLook)
    {
        ctman = _ctmanFreeLookLive;
    }
    else if (fRecord)
    {
        if (!_fManualCameraRecordLiveValid)
            return fFalse;
        ctman = _ctmanManualCameraRecordLive;
    }
    else if (!_fManualCameraEditValid || _iscenManualCameraEdit != Iscen() ||
             _nfrmManualCameraEdit != nfrm)
    {
        return fFalse;
    }
    else
    {
        ctman = _ctmanManualCameraEditLive;
    }

    BMAT34 bmatBase;
    BMAT34 bmatMove;
    Pscen()->Pbkgd()->GetMouseMatrix(&bmatBase);
    bmatMove = bmatBase;
    bmatBase.m[3][0] = bmatBase.m[3][1] = bmatBase.m[3][2] = rZero;
    bmatMove.m[3][0] = bmatMove.m[3][1] = bmatMove.m[3][2] = rZero;

    // Yaw always steers WASD.  Shift's fly toggle controls only whether pitch
    // participates in movement and therefore whether altitude can change.
    if (fFly)
        BrMatrix34PostRotateX(&bmatMove, BrDegreeToAngle(BrFloatToScalar(ctman.pitch)));
    BrMatrix34PostRotateY(&bmatMove, BrDegreeToAngle(BrFloatToScalar(ctman.yaw)));

    float wx = dStrafe * BrScalarToFloat(bmatMove.m[0][0]) -
               dForward * BrScalarToFloat(bmatMove.m[2][0]);
    float wy = dStrafe * BrScalarToFloat(bmatMove.m[0][1]) -
               dForward * BrScalarToFloat(bmatMove.m[2][1]);
    float wz = dStrafe * BrScalarToFloat(bmatMove.m[0][2]) -
               dForward * BrScalarToFloat(bmatMove.m[2][2]);
    float yOld = ctman.y;

    ctman.x += wx * BrScalarToFloat(bmatBase.m[0][0]) +
               wy * BrScalarToFloat(bmatBase.m[0][1]) +
               wz * BrScalarToFloat(bmatBase.m[0][2]);
    ctman.y += wx * BrScalarToFloat(bmatBase.m[1][0]) +
               wy * BrScalarToFloat(bmatBase.m[1][1]) +
               wz * BrScalarToFloat(bmatBase.m[1][2]);
    ctman.z += -(wx * BrScalarToFloat(bmatBase.m[2][0]) +
                 wy * BrScalarToFloat(bmatBase.m[2][1]) +
                 wz * BrScalarToFloat(bmatBase.m[2][2]));
    if (!fFly)
        ctman.y = yOld;

    if (fFreeLook)
    {
        _ctmanFreeLookLive = ctman;
    }
    else if (fRecord)
    {
        _ctmanManualCameraRecordLive = ctman;
    }
    else
    {
        ctman.iscen = Iscen();
        ctman.nfrm = nfrm;
        _ctmanManualCameraEditLive = ctman;
    }

    if (fUpdateView)
    {
        ApplyCameraTrack();
        MarkViews();
    }
    return fTrue;
}

/******************************************************************************
    FLookManualCamera
******************************************************************************/
bool MVIE::FLookManualCamera(float dYaw, float dPitch, bool fUpdateView)
{
    AssertThis(0);

    bool fFreeLook = _fFreeLookMode;
    bool fRecord = _fManualCameraMode && _fManualCameraRecording;
    bool fManual = _fManualCameraMode && !fRecord;
    if ((!fFreeLook && !fRecord && !fManual) || Pscen() == pvNil ||
        Pscen()->Pbkgd() == pvNil || Pbwld() == pvNil ||
        (FPlaying() && !fRecord) ||
        (_fManualCameraMode && FDepthMotionTweenActive()) ||
        (dYaw == 0.0f && dPitch == 0.0f))
    {
        return fFalse;
    }

    int32_t nfrm = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;
    CTMAN ctman;
    if (fFreeLook)
    {
        ctman = _ctmanFreeLookLive;
    }
    else if (fRecord)
    {
        if (!_fManualCameraRecordLiveValid)
            return fFalse;
        ctman = _ctmanManualCameraRecordLive;
    }
    else if (!_fManualCameraEditValid || _iscenManualCameraEdit != Iscen() ||
             _nfrmManualCameraEdit != nfrm)
    {
        return fFalse;
    }
    else
    {
        ctman = _ctmanManualCameraEditLive;
    }

    ctman.yaw += dYaw;
    ctman.pitch += dPitch;
    if (ctman.pitch < -kdManualCameraPitchLimit)
        ctman.pitch = -kdManualCameraPitchLimit;
    if (ctman.pitch > kdManualCameraPitchLimit)
        ctman.pitch = kdManualCameraPitchLimit;

    if (fFreeLook)
    {
        _ctmanFreeLookLive = ctman;
    }
    else if (fRecord)
    {
        _ctmanManualCameraRecordLive = ctman;
    }
    else
    {
        ctman.iscen = Iscen();
        ctman.nfrm = nfrm;
        _ctmanManualCameraEditLive = ctman;
    }

    if (fUpdateView)
    {
        ApplyCameraTrack();
        MarkViews();
    }
    return fTrue;
}

/******************************************************************************
    FRollManualCamera

    Free Look-only barrel roll.  Q/E deliberately remain transient for now;
    this proves the camera transform before roll becomes authored .3ct data.
******************************************************************************/
bool MVIE::FRollManualCamera(float dRoll, bool fUpdateView)
{
    AssertThis(0);
    if (!_fFreeLookMode || !_fFreeLookOverride || Pscen() == pvNil ||
        Pscen()->Pbkgd() == pvNil || Pbwld() == pvNil || dRoll == 0.0f)
        return fFalse;

    _ctmanFreeLookLive.roll += dRoll;
    while (_ctmanFreeLookLive.roll > 180.0f)
        _ctmanFreeLookLive.roll -= 360.0f;
    while (_ctmanFreeLookLive.roll < -180.0f)
        _ctmanFreeLookLive.roll += 360.0f;

    if (fUpdateView)
    {
        ApplyCameraTrack();
        MarkViews();
    }
    return fTrue;
}

/******************************************************************************
    FInsertFrameRelative
******************************************************************************/
bool MVIE::FInsertFramesRelative(bool fBefore, bool fBlank, int32_t cfrm)
{
    AssertThis(0);
    AssertIn(cfrm, 0, klwMax);

    if (cfrm <= 0)
        return fTrue;
    if (Pscen() == pvNil || FPlaying())
        return fFalse;

    bool fManual = _fManualCameraMode;
    if (fManual)
    {
        if (cfrm != 1 || fBefore || fBlank ||
            !_fManualCameraEditValid || _iscenManualCameraEdit != Iscen() ||
            _nfrmManualCameraEdit != Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1)
        {
            return fFalse;
        }

        // Manual Camera Ctrl is one user action: save the current live sample,
        // insert one frame immediately after it, and begin a new unsaved edit
        // on the inserted frame.  The generic camera shifter deliberately does
        // not turn that inserted frame into an exact manual-camera sample.
        CTMAN *pold = _PctmanFind(_iscenManualCameraEdit,
                                  _nfrmManualCameraEdit, fFalse);
        if (pold == pvNil && _cctman >= kcctmanMax)
            return fFalse;
        if (!Pscen()->FAddSnapshotUndo(PszLit("Insert Frame")))
            return fFalse;

        CTMAN live = _ctmanManualCameraEditLive;
        live.iscen = _iscenManualCameraEdit;
        live.nfrm = _nfrmManualCameraEdit;
        live.fImplicit = fFalse;
        CTMAN *pdst = _PctmanFind(live.iscen, live.nfrm, fTrue);
        if (pdst == pvNil)
        {
            ClearUndo();
            return fFalse;
        }
        *pdst = live;
        SetDirty();
        _ClearManualCameraEdit();
    }

    bool fRet;
    if (fManual && !fBefore && !fBlank && cfrm == 1 &&
        Pscen()->Nfrm() == Pscen()->NfrmLast())
    {
        // Manual Camera Ctrl at the scene end keeps the original instant
        // FGotoFrm(last + 1) creation path.  The just-saved manual sample
        // naturally holds into the new frame without another .3ct key.
        fRet = Pscen()->FNativeAppendFramesCore(1, fFalse);
    }
    else
    {
        fRet = Pscen()->FInsertFramesAtCurrent(cfrm, fBefore, fBlank,
                                                !fManual);
    }
    if (!fRet)
    {
        if (fManual)
            _FBeginManualCameraEdit();
        return fFalse;
    }

    if (fManual)
    {
        if (!_FBeginManualCameraEdit())
        {
            _fManualCameraMode = fFalse;
            PMVU pmvu = PmvuCur();
            if (pmvu != pvNil)
                pmvu->EndManualCameraInput();
        }
    }

    ApplyCameraTrack();
    InvalViewsAndScb();
    return fTrue;
}

/******************************************************************************
    FPrepareNativeFrameInsert

    Install the lightweight undo record before the original Studio scrollbar
    path executes FGotoFrm(current +/- 1).  This deliberately does not create
    the frame itself; the edge gestures continue through the exact native code
    which shipped with 3D Movie Maker.
******************************************************************************/
bool MVIE::FPrepareNativeFrameInsert(bool fBefore)
{
    AssertThis(0);
    if (Pscen() == pvNil || FPlaying())
        return fFalse;
    return Pscen()->FAddNativeFrameUndo(fBefore, fBefore, 1);
}

/******************************************************************************
    FCompleteNativeFrameInsert

    The original prepend operation knows only about SCEN data.  After it has
    completed, shift the external camera timeline by one displayed frame.  An
    append requires no .3ct entry because the last known camera transform holds
    forward until another camera instruction changes it.
******************************************************************************/
bool MVIE::FCompleteNativeFrameInsert(bool fBefore)
{
    AssertThis(0);
    if (Pscen() == pvNil)
        return fFalse;

    if (!fBefore)
    {
        // The original end-extension path has already completed inside
        // SCEN::FGotoFrm().  Camera state naturally holds forward, so there is
        // deliberately no second scene operation and no manufactured .3ct key.
        return fTrue;
    }

    if (!FPrependCameraTrackFrames(Iscen(), 1))
    {
        // The lightweight undo record was installed before the native seek.
        // Use it as an atomic rollback if the external camera timeline cannot
        // be renumbered, then discard the failed operation from history.
        FUndo();
        ClearUndo();
        return fFalse;
    }

    ApplyCameraTrack();
    InvalViewsAndScb();
    return fTrue;
}

/******************************************************************************
    FAddNativeEndFrame

    Preserve the original 3DMM Ctrl+Next-at-scene-end path exactly: moving to
    NfrmLast()+1 is the frame creation operation.  No actor duplication pass,
    no visibility sweep and no camera sample is manufactured.
******************************************************************************/
bool MVIE::FAddNativeEndFrames(int32_t cfrm, bool fBlank)
{
    AssertThis(0);
    AssertIn(cfrm, 0, klwMax);
    if (cfrm <= 0)
        return fTrue;
    if (Pscen() == pvNil || FPlaying() ||
        Pscen()->Nfrm() != Pscen()->NfrmLast() ||
        Pscen()->Nfrm() > klwMax - cfrm)
    {
        return fFalse;
    }

    // A duplicate of a currently blank last frame must remain blank even when
    // Shift was not held.  Otherwise use the user's explicit checkbox/key.
    bool fBlankEffective = fBlank || Pscen()->FFrameBlank(Pscen()->Nfrm());
    if (!Pscen()->FAddNativeFrameUndo(fFalse, fBlankEffective, cfrm))
        return fFalse;

    if (!Pscen()->FNativeAppendFramesCore(cfrm, fBlankEffective))
    {
        ClearUndo();
        return fFalse;
    }

    // No camera key is added.  The evaluator keeps the last known camera
    // transform, including a completed tween endpoint, until another camera
    // instruction changes it.
    ApplyCameraTrack();
    InvalViewsAndScb();
    return fTrue;
}

/******************************************************************************
    FAddNativeStartBlankFrame

    Preserve the original 3DMM Ctrl+Previous-at-scene-start operation.  The
    old code simply seeks to NfrmFirst()-1; _MoveBackFirstFrame makes the new
    first frame empty and moves the persistent camera event backward.
******************************************************************************/
bool MVIE::FAddNativeStartBlankFrames(int32_t cfrm)
{
    AssertThis(0);
    AssertIn(cfrm, 0, klwMax);
    if (cfrm <= 0)
        return fTrue;
    if (Pscen() == pvNil || FPlaying() ||
        Pscen()->Nfrm() != Pscen()->NfrmFirst() ||
        Pscen()->Nfrm() < klwMin + cfrm)
    {
        return fFalse;
    }
    if (!Pscen()->FAddNativeFrameUndo(fTrue, fTrue, cfrm))
        return fFalse;
    if (!FPrependCameraTrackFrames(Iscen(), cfrm))
    {
        ClearUndo();
        return fFalse;
    }
    if (!Pscen()->FNativePrependBlankFramesCore(cfrm))
    {
        ClearUndo();
        return fFalse;
    }

    ApplyCameraTrack();
    InvalViewsAndScb();
    return fTrue;
}

/******************************************************************************
    FAppendManualCameraFrame

    Manual Camera Ctrl saves the current sample and inserts a held frame
    immediately after the current frame.
******************************************************************************/
bool MVIE::FAppendManualCameraFrame(void)
{
    AssertThis(0);
    if (!_fManualCameraMode || _fManualCameraRecording)
        return fFalse;
    return FInsertFramesRelative(fFalse, fFalse, 1);
}

/******************************************************************************
    _FWriteCameraTrack

    Rewrite the sidecar in a small canonical form.  Camera data and 4DMM
    Light Lab object records share this sidecar; comments/whitespace are not
    preserved.
******************************************************************************/
bool MVIE::_FWriteCameraTrack(void)
{
    AssertThis(0);
    if (_stnCameraTrackPath.Cch() == 0)
        return fFalse;
    FILE *pfile = fopen(_stnCameraTrackPath.Psz(), "wt");
    if (pfile == pvNil)
        return fFalse;

    fprintf(pfile, "3CT 1\n\n");
    fprintf(pfile, "movie settings\n");
    fprintf(pfile, "default music new scenes %d\n", _fDefaultMusicForNewScenes ? 1 : 0);
    fprintf(pfile, "experimental 100x grow %d\n", _fExperimental100xGrow ? 1 : 0);
    fprintf(pfile, "experimental 100x shrink %d\n", _fExperimental100xShrink ? 1 : 0);
    fprintf(pfile, "camera movement speed %.2f\n", (double)_flCameraMoveSpeed);
    fprintf(pfile, "camera mouse sensitivity %.2f\n", (double)_flCameraMouseSensitivity);
    fprintf(pfile, "default lighting shaders %d\n", _fDefaultLightingShaders ? 1 : 0);
    fprintf(pfile, "light lab combine legacy lighting %d\n", _fLightLabCombineLegacy ? 1 : 0);
    fprintf(pfile, "freecam always last known %d\n", _fFreeLookAlwaysLastKnown ? 1 : 0);
    fprintf(pfile, "dont ask light lab cut %d\n", _fDontAskLightLabCut ? 1 : 0);
    fprintf(pfile, "dont ask shader propagation %d\n", _fDontAskShaderPropagation ? 1 : 0);
    for (int32_t i = 0; i < _caridNonSelectable; ++i)
        fprintf(pfile, "non-selectable object %d\n", (int)_rgaridNonSelectable[i]);
    for (int32_t i = 0; i < _c4DMMCustomObject; ++i)
    {
        const CUSTOMOBJECT &obj = _rg4DMMCustomObject[i];
        fprintf(pfile, "custom object v1 %d,%d,%d,%d,%d,%lu,%d,%d,%s\n",
                (int)obj.id, obj.fProp ? 1 : 0, (int)obj.templateKind,
                (int)obj.idTemplateCustom, (int)obj.sidTemplate,
                (unsigned long)obj.ctgTemplate, (int)obj.cnoTemplate,
                (int)obj.cnoOwnedTmpl, obj.szName);
    }
    for (int32_t i = 0; i < _c4DMMCustomPart; ++i)
    {
        const CUSTOMPART &part = _rg4DMMCustomPart[i];
        if (part.idImport > 0 && part.cpart > 0)
        {
            fprintf(pfile, "custom part v3 %d,%d,%d,%d,%d,%d,%d,%d|%s|%s\n",
                    (int)part.idObject, (int)part.iscen, (int)part.idGroup,
                    (int)part.idImport, (int)part.aridSource, (int)part.ipartFirst,
                    (int)part.cpart, part.fSourceTdt ? 1 : 0,
                    part.szGroupName[0] != 0 ? part.szGroupName : "-",
                    part.szObjectName);
        }
        else
        {
            fprintf(pfile, "custom part v1 %d,%d,%d\n",
                    (int)part.idObject, (int)part.iscen, (int)part.idGroup);
        }
    }
    for (int32_t i = 0; i < _c4DMMCustomAction; ++i)
    {
        const CUSTOMACTION &action = _rg4DMMCustomAction[i];
        fprintf(pfile, "custom action v1 %d,%d,%s\n",
                (int)action.cnoTmpl, (int)action.anid, action.szName);
    }
    fprintf(pfile, "\n");

    int32_t iscenMax = -1;
    for (int32_t i = 0; i < _cctween; i++)
        iscenMax = LwMax(iscenMax, _rgctween[i].iscen);
    for (int32_t i = 0; i < _cctman; i++)
        iscenMax = LwMax(iscenMax, _rgctman[i].iscen);
    for (int32_t i = 0; i < _clightLab; i++)
        iscenMax = LwMax(iscenMax, _rglightLab[i].iscen);
    for (int32_t i = 0; i < _cObjectGroup; i++)
        iscenMax = LwMax(iscenMax, _rgObjectGroup[i].iscen);
    for (int32_t i = 0; i < _cObjectProperties; i++)
        iscenMax = LwMax(iscenMax, _rgObjectProperties[i].iscen);
    for (int32_t i = 0; i < LwMin(Cscen(), kc4DMMSceneSettingsMax); i++)
        if (_rgfSceneLightsEnabled[i] || _rgfSceneHideLightObjects[i] ||
            _rgfSceneDefaultLightingShaders[i] != _fDefaultLightingShaders ||
            _rgfSceneLightLabCombineLegacy[i] != _fLightLabCombineLegacy)
            iscenMax = LwMax(iscenMax, i);

    for (int32_t iscen = 0; iscen <= iscenMax; iscen++)
    {
        bool fHas = FIn(iscen, 0, kc4DMMSceneSettingsMax) &&
                    (_rgfSceneLightsEnabled[iscen] || _rgfSceneHideLightObjects[iscen] ||
                     _rgfSceneDefaultLightingShaders[iscen] != _fDefaultLightingShaders ||
                     _rgfSceneLightLabCombineLegacy[iscen] != _fLightLabCombineLegacy);
        for (int32_t i = 0; !fHas && i < _clightLab; i++)
            if (_rglightLab[i].iscen == iscen) { fHas = fTrue; break; }
        if (!fHas)
            for (int32_t i = 0; i < _cctween; i++)
                if (_rgctween[i].iscen == iscen) { fHas = fTrue; break; }
        if (!fHas)
            for (int32_t i = 0; i < _cctman; i++)
                if (_rgctman[i].iscen == iscen) { fHas = fTrue; break; }
        if (!fHas)
            for (int32_t i = 0; i < _cObjectGroup; i++)
                if (_rgObjectGroup[i].iscen == iscen) { fHas = fTrue; break; }
        if (!fHas)
            for (int32_t i = 0; i < _cObjectProperties; i++)
                if (_rgObjectProperties[i].iscen == iscen) { fHas = fTrue; break; }
        if (!fHas)
            continue;

        fprintf(pfile, "scene %d\n", (int)iscen + 1);
        fprintf(pfile, "lights enabled %d\n", FSceneLightsEnabled(iscen) ? 1 : 0);
        fprintf(pfile, "hide light attachments %d\n", FSceneHideLightObjects(iscen) ? 1 : 0);
        fprintf(pfile, "default lighting shaders %d\n", FSceneDefaultLightingShaders(iscen) ? 1 : 0);
        fprintf(pfile, "light lab combine legacy lighting %d\n", FSceneLightLabCombineLegacy(iscen) ? 1 : 0);

        for (int32_t i = 0; i < _cObjectProperties; ++i)
        {
            const OBJECTPROPERTIES &prop = _rgObjectProperties[i];
            if (prop.iscen != iscen)
                continue;
            fprintf(pfile, "object properties v1 %d,%d,%d\n",
                    (int)prop.arid, prop.fFlushOverlap ? 1 : 0, prop.fCastShadows ? 1 : 0);
        }

        // Light Lab records. Spawn is one-based; despawn 0 remains
        // open-ended and finite records can carry an explicit despawn frame.
        for (int32_t i = 0; i < _clightLab; i++)
        {
            if (_rglightLab[i].iscen != iscen)
                continue;
            const LIGHTLAB &light = _rglightLab[i];
            fprintf(pfile, "light object\n");
            fprintf(pfile, "arid %d\n", (int)light.arid);
            fprintf(pfile, "enabled %d\n", light.fEnabled ? 1 : 0);
            fprintf(pfile, "generate shadows %d\n", light.fGenerateShadows ? 1 : 0);
            fprintf(pfile, "attachment hide-able %d\n", light.fAttachmentHideable ? 1 : 0);
            fprintf(pfile, "intensity %d\n", (int)light.intensity);
            fprintf(pfile, "edge gradient %.6g\n", (double)light.edgeGradient);
            fprintf(pfile, "diameter %.6g\n", (double)light.diameter);
            fprintf(pfile, "range %.6g\n", (double)(light.range > 0.0f ? light.range : 500.0f));
            fprintf(pfile, "spawn frame %d\n", (int)light.nfrmSpawn);
            fprintf(pfile, "despawn frame %d\n", (int)light.nfrmDespawn);
            fprintf(pfile, "shape %s\n", light.szShape);
        }

        for (int32_t i = 0; i < _cctween; i++)
        {
            if (_rgctween[i].iscen != iscen)
                continue;
            const CTTWEEN &t = _rgctween[i];
            fprintf(pfile, "z-axis motion tween\n");
            fprintf(pfile, "frame %d z %.6g\n", (int)t.nfrmFirst, (double)t.zFirst);
            fprintf(pfile, "frame %d z %.6g\n", (int)t.nfrmLast, (double)t.zLast);
            fprintf(pfile, "x %.6g\n", (double)t.x);
            fprintf(pfile, "y %.6g\n", (double)t.y);
            fprintf(pfile, "z %.6g\n", (double)t.z);
            fprintf(pfile, "pitch %.6g\n", (double)t.pitch);
            fprintf(pfile, "yaw %.6g\n", (double)t.yaw);
        }

        bool fManual = fFalse;
        for (int32_t i = 0; i < _cctman; i++)
            if (_rgctman[i].iscen == iscen) { fManual = fTrue; break; }
        if (fManual)
        {
            fprintf(pfile, "manual camera mode\n");
            for (int32_t i = 0; i < _cctman; i++)
            {
                if (_rgctman[i].iscen != iscen)
                    continue;
                const CTMAN &m = _rgctman[i];
                if (m.fImplicit)
                    fprintf(pfile, "implicit camera hold\n");
                fprintf(pfile, "frame %d x %.6g\n", (int)m.nfrm, (double)m.x);
                fprintf(pfile, "frame %d y %.6g\n", (int)m.nfrm, (double)m.y);
                fprintf(pfile, "frame %d z %.6g\n", (int)m.nfrm, (double)m.z);
                fprintf(pfile, "pitch %.6g\n", (double)m.pitch);
                fprintf(pfile, "yaw %.6g\n", (double)m.yaw);
            }
        }

        // Keep Object Groups independent from the stateful camera parser. Each
        // record occupies one line; commas inside a group name are safe because
        // the name is simply the remainder after the second comma. Member pose
        // records are the immutable bind-space reference frame; live position
        // and orientation continue to persist through the ordinary .3mm actor
        // events.
        for (int32_t iGroup = 0; iGroup < _cObjectGroup; iGroup++)
        {
            const OBJECTGROUP &group = _rgObjectGroup[iGroup];
            if (group.iscen != iscen)
                continue;
            fprintf(pfile, "object group v2 %d,%d,%s\n",
                    (int)group.id, group.fLocked ? 1 : 0, group.szName);
            for (int32_t iMember = 0; iMember < _cObjectGroupMember; iMember++)
            {
                const OBJECTGROUPMEMBER &member = _rgObjectGroupMember[iMember];
                if (member.idGroup != group.id)
                    continue;
                fprintf(pfile, "object member v2 %d,%d,%.9g,%.9g,%.9g",
                        (int)group.id, (int)member.arid,
                        (double)BrScalarToFloat(member.xr),
                        (double)BrScalarToFloat(member.yr),
                        (double)BrScalarToFloat(member.zr));
                for (int32_t ir = 0; ir < 4; ir++)
                    for (int32_t ic = 0; ic < 3; ic++)
                        fprintf(pfile, ",%.9g", (double)BrScalarToFloat(member.bmat34.m[ir][ic]));
                fprintf(pfile, "\n");
            }
        }
        fprintf(pfile, "\n");
    }
    fclose(pfile);
    return fTrue;
}

/******************************************************************************
    SetBrowserCameraFollow / FUpdateBrowserCameraFollow

    The external roll-call browsers can temporarily lock the camera to a
    selected actor/prop/3-D word.  Preserve the current camera rotation and
    move only its translation so the target occupies the same camera-relative
    point used for a freshly inserted object.  Object rotation is deliberately
    ignored.
******************************************************************************/
void MVIE::SetBrowserCameraFollow(int32_t arid, bool fEnable)
{
    AssertThis(0);

    _fBrowserCameraFollow = FPure(fEnable);
    _aridBrowserCameraFollow = fEnable ? arid : aridNil;

    if (!fEnable)
    {
        // Restore the underlying authored/.3ct/Free Look camera immediately.
        ApplyCameraTrack();
        MarkViews();
    }
}

bool MVIE::FUpdateBrowserCameraFollow(void)
{
    AssertThis(0);

    if (!_fBrowserCameraFollow || _aridBrowserCameraFollow == aridNil ||
        Pscen() == pvNil || Pscen()->Pbkgd() == pvNil || Pbwld() == pvNil)
    {
        return fFalse;
    }

    PACTR pactr = Pscen()->PactrFromArid(_aridBrowserCameraFollow);
    // BODY::FIsInView() means "inside the last rendered camera bounds".
    // Camera-follow must also work for an onstage object that is currently
    // outside the viewport, because the whole point is to move the camera to
    // that object. Only reject objects that are not actually onstage now.
    if (pactr == pvNil || !pactr->FOnStage())
        return fFalse;

    BMAT34 bmat34Base;
    BMAT34 bmat34BaseInv;
    BMAT34 bmat34Camera;
    BRS zrHither;
    BRS zrYon;
    BRA aFov;
    if (!Pscen()->Pbkgd()->FGetCameraBase(&bmat34Base, &zrHither, &zrYon, &aFov))
        return fFalse;

    // The current rendered camera already contains any manual/.3ct/Free Look
    // rotation.  Follow changes translation only.
    Pbwld()->GetCamera(&bmat34Camera, &zrHither, &zrYon, &aFov);

    BRS xrPlace, yrPlace, zrPlace;
    Pscen()->Pbkgd()->GetDefaultActorPlacePoint(&xrPlace, &yrPlace, &zrPlace);

    BVEC3 bvec3PlaceWorld;
    BVEC3 bvec3PlaceCamera;
    BVEC3 bvec3PlaceCurrent;
    if (xrPlace == rZero && yrPlace == rZero && zrPlace == rZero)
    {
        // Match ACTR::_GetNewOrigin's legacy fallback for backgrounds that do
        // not author an APOS placement point.
        BRS zrCam = kzrDefault;
        if (BR_ABS(zrCam) > zrYon)
            zrCam = BR_CONST_DIV(BR_ADD(zrYon, zrHither), -2);
        bvec3PlaceCamera.v[0] = rZero;
        bvec3PlaceCamera.v[1] = rZero;
        bvec3PlaceCamera.v[2] = zrCam;
    }
    else
    {
        bvec3PlaceWorld.v[0] = xrPlace;
        bvec3PlaceWorld.v[1] = yrPlace;
        bvec3PlaceWorld.v[2] = zrPlace;
        BrMatrix34LPInverse(&bmat34BaseInv, &bmat34Base);
        BrMatrix34ApplyP(&bvec3PlaceCamera, &bvec3PlaceWorld, &bmat34BaseInv);
    }
    BrMatrix34ApplyP(&bvec3PlaceCurrent, &bvec3PlaceCamera, &bmat34Camera);
    if (pactr->FIsTdt())
        bvec3PlaceCurrent.v[1] += BR_SCALAR(10.0);

    BRS xrActor, yrActor, zrActor;
    pactr->GetXyzWorld(&xrActor, &yrActor, &zrActor);

    bmat34Camera.m[3][0] += xrActor - bvec3PlaceCurrent.v[0];
    bmat34Camera.m[3][1] += yrActor - bvec3PlaceCurrent.v[1];
    bmat34Camera.m[3][2] += zrActor - bvec3PlaceCurrent.v[2];

    Pbwld()->SetCamera(&bmat34Camera, zrHither, zrYon, aFov);
    Pbwld()->MarkDirty();
    return fTrue;
}

bool MVIE::FLiveCameraDisplacedFromFrame(void)
{
    AssertThis(0);
    if (_fBrowserCameraFollow)
        return fTrue;
    if (!_fFreeLookOverride || Pscen() == pvNil)
        return fFalse;

    const int32_t nfrm = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;
    if (_iscenFreeLook != Iscen() || _nfrmFreeLook != nfrm)
        return fFalse;

    CTMAN saved;
    if (!_FGetCameraControlState(Iscen(), nfrm, &saved))
        return fFalse;
    const float eps = 0.0001f;
    return FPure(fabs(_ctmanFreeLookLive.x - saved.x) > eps ||
                 fabs(_ctmanFreeLookLive.y - saved.y) > eps ||
                 fabs(_ctmanFreeLookLive.z - saved.z) > eps ||
                 fabs(_ctmanFreeLookLive.pitch - saved.pitch) > eps ||
                 fabs(_ctmanFreeLookLive.yaw - saved.yaw) > eps ||
                 fabs(_ctmanFreeLookLive.roll - saved.roll) > eps);
}

/******************************************************************************
    AdjustCameraTrackInsertionPoint

    Move a normal 3DMM actor insertion point from the scene's baseline camera
    space into the current tracked-camera space.  The point is transformed
    once at insertion time; the actor does not remain attached to the camera.
******************************************************************************/
void MVIE::AdjustCameraTrackInsertionPoint(BRS *pxr, BRS *pyr, BRS *pzr)
{
    AssertThis(0);
    AssertVarMem(pxr);
    AssertVarMem(pyr);
    AssertVarMem(pzr);

    if (Pscen() == pvNil || Pscen()->Pbkgd() == pvNil || Pbwld() == pvNil ||
        !FCameraTrackActive())
    {
        return;
    }

    BMAT34 bmat34Base;
    BMAT34 bmat34BaseInv;
    BMAT34 bmat34Tracked;
    BRS zrHither;
    BRS zrYon;
    BRA aFov;
    BVEC3 bvec3World;
    BVEC3 bvec3Camera;
    BVEC3 bvec3Tracked;

    if (!Pscen()->Pbkgd()->FGetCameraBase(&bmat34Base, &zrHither, &zrYon, &aFov))
        return;

    // Ensure BWLD contains the tracked camera for this exact scene/frame.
    ApplyCameraTrack();
    Pbwld()->GetCamera(&bmat34Tracked);

    // Camera matrices contain rotation and translation only, so the
    // low-precision rigid-transform inverse is the correct inverse here.
    BrMatrix34LPInverse(&bmat34BaseInv, &bmat34Base);

    bvec3World.v[0] = *pxr;
    bvec3World.v[1] = *pyr;
    bvec3World.v[2] = *pzr;

    BrMatrix34ApplyP(&bvec3Camera, &bvec3World, &bmat34BaseInv);
    BrMatrix34ApplyP(&bvec3Tracked, &bvec3Camera, &bmat34Tracked);

    *pxr = bvec3Tracked.v[0];
    *pyr = bvec3Tracked.v[1];
    *pzr = bvec3Tracked.v[2];
}

/******************************************************************************
    BraCameraTrackYaw

    Return the scene-local yaw adjustment used by the tracked camera.  New
    actors add this to 3DMM's normal face-the-camera insertion angle.
******************************************************************************/
BRA MVIE::BraCameraTrackYaw(void)
{
    AssertThis(0);

    if (Pscen() == pvNil)
        return aZero;

    int32_t nfrm = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;
    CTMAN ctman;
    if (_fFreeLookOverride && _iscenFreeLook == Iscen() && _nfrmFreeLook == nfrm)
        return BrDegreeToAngle(BrFloatToScalar(_ctmanFreeLookLive.yaw));
    if (_fManualCameraRecording && _fManualCameraRecordLiveValid)
        return BrDegreeToAngle(BrFloatToScalar(_ctmanManualCameraRecordLive.yaw));
    if (_FGetCameraControlState(Iscen(), nfrm, &ctman))
        return BrDegreeToAngle(BrFloatToScalar(ctman.yaw));
    return aZero;
}

/******************************************************************************
    FAlignCameraTrackYawToSelectedActor

    Treat the selected object's final transformed bounding box as a ruler.
    Choose its longest transformed local axis, project that axis onto the
    horizontal plane, choose the end facing into the camera's current forward
    180-degree view, then save the required scene-local camera yaw to .3ct.
******************************************************************************/
bool MVIE::FAlignCameraTrackYawToSelectedActor(void)
{
    AssertThis(0);

    if (Pscen() == pvNil || Pscen()->PactrSelected() == pvNil)
        return fFalse;

    PACTR pactr = Pscen()->PactrSelected();
    PBODY pbody = pactr->Pbody();
    BCB bcb;
    BMAT34 bmat34Actor;
    BMAT34 bmat34Camera;
    double rgdExtent[3];
    double rgdLengthSq[3];
    double ax;
    double az;
    double fx;
    double fz;
    double dAxisLen;
    double dForwardLen;
    double dDot;
    double dCross;
    double dYaw;
    int32_t iaxis;
    int32_t iaxisBest = 0;

    pbody->GetBcbBounds(&bcb, fFalse);
    pbody->GetMatrix(&bmat34Actor);

    rgdExtent[0] = BrScalarToFloat(bcb.xrMax - bcb.xrMin);
    rgdExtent[1] = BrScalarToFloat(bcb.yrMax - bcb.yrMin);
    rgdExtent[2] = BrScalarToFloat(bcb.zrMax - bcb.zrMin);

    for (iaxis = 0; iaxis < 3; iaxis++)
    {
        double x = BrScalarToFloat(bmat34Actor.m[iaxis][0]);
        double y = BrScalarToFloat(bmat34Actor.m[iaxis][1]);
        double z = BrScalarToFloat(bmat34Actor.m[iaxis][2]);
        rgdLengthSq[iaxis] = rgdExtent[iaxis] * rgdExtent[iaxis] * (x * x + y * y + z * z);
        if (rgdLengthSq[iaxis] > rgdLengthSq[iaxisBest])
            iaxisBest = iaxis;
    }

    ax = BrScalarToFloat(bmat34Actor.m[iaxisBest][0]);
    az = BrScalarToFloat(bmat34Actor.m[iaxisBest][2]);
    dAxisLen = sqrt(ax * ax + az * az);
    if (dAxisLen < 0.00001)
        return fFalse;
    ax /= dAxisLen;
    az /= dAxisLen;

    // BRender's local +Z is backward for 3DMM's camera convention.
    Pscen()->Pbkgd()->GetMouseMatrix(&bmat34Camera);
    fx = -BrScalarToFloat(bmat34Camera.m[2][0]);
    fz = -BrScalarToFloat(bmat34Camera.m[2][2]);
    dForwardLen = sqrt(fx * fx + fz * fz);
    if (dForwardLen < 0.00001)
        return fFalse;
    fx /= dForwardLen;
    fz /= dForwardLen;

    dDot = fx * ax + fz * az;
    if (dDot < 0.0)
    {
        ax = -ax;
        az = -az;
        dDot = -dDot;
    }

    // Signed world-Y rotation for BRender's row-vector matrix convention.
    dCross = fz * ax - fx * az;
    dYaw = atan2(dCross, dDot) * (180.0 / 3.14159265358979323846);

    int32_t nfrm = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;
    int32_t iTween = _ItweenAtFrame(Iscen(), nfrm);
    if (iTween == ivNil)
        return fFalse;
    if (!FAddCameraTrackUndo(PszLit("Z-Axis Motion Tween")))
        return fFalse;
    if (!_FSetCameraTrackYaw(_rgctween[iTween].id, (float)dYaw))
        return fFalse;

    SetDirty();
    ApplyCameraTrack();
    MarkViews();
    _pmcc->UpdateScrollbars();
    return fTrue;
}

/******************************************************************************
    ApplyCameraTrack

    Rebuild the rendered camera from the scene's unmodified selected camera on
    every frame, then apply the persistent .3ct state or a temporary live
    camera override.  Free Look has highest priority on its original frame,
    followed by Manual Camera Record Mode, ordinary manual samples, and depth
    tween data.
******************************************************************************/
void MVIE::ApplyCameraTrack(void)
{
    AssertThis(0);

    if (Pscen() == pvNil || Pscen()->Pbkgd() == pvNil || Pbwld() == pvNil)
        return;

    int32_t nfrmTrack = Pscen()->Nfrm() - Pscen()->NfrmFirst() + 1;

    // A Free Look viewpoint belongs only to the frame where F activated it.
    // The first frame/scene navigation clears the transient override, so going
    // forward and back returns to the camera actually saved on the timeline.
    if (_fFreeLookOverride &&
        (_iscenFreeLook != Iscen() || _nfrmFreeLook != nfrmTrack ||
         !FActorsAndPropsTabOpen()))
    {
        // Do not let a temporary camera survive the scene/frame/tab object
        // graph that created it.  ApplyCameraTrack is called from many editor
        // paths, so this is the central lifetime fence.
        _ClearFreeLookState();
    }

    CTMAN ctman;
    bool fCameraPose = fFalse;
    if (_fFreeLookOverride)
    {
        ctman = _ctmanFreeLookLive;
        fCameraPose = fTrue;
    }
    else if (_fManualCameraRecording && _fManualCameraRecordLiveValid)
    {
        ctman = _ctmanManualCameraRecordLive;
        fCameraPose = fTrue;
    }
    else if (_fManualCameraMode && _fManualCameraEditValid &&
             _iscenManualCameraEdit == Iscen() &&
             _nfrmManualCameraEdit == nfrmTrack)
    {
        ctman = _ctmanManualCameraEditLive;
        fCameraPose = fTrue;
    }
    else
    {
        fCameraPose = _FGetPersistentCameraTrackPose(Iscen(), nfrmTrack, &ctman);
    }

    BMAT34 bmat34;
    BMAT34 bmat34Move;
    BRS zrHither;
    BRS zrYon;
    BRA aFov;
    if (!Pscen()->Pbkgd()->FGetCameraBase(&bmat34, &zrHither, &zrYon, &aFov))
        return;
    Pscen()->Pbkgd()->GetMouseMatrix(&bmat34Move);

    BRS xrCamera = bmat34.m[3][0];
    BRS yrCamera = bmat34.m[3][1];
    BRS zrCamera = bmat34.m[3][2];

    if (fCameraPose)
    {
        // Every persistent camera control now resolves to the same complete
        // scene-relative pose.  Z-axis tweens differ only in how that pose is
        // evaluated over time, so transitions never snap back to baseline.
        bmat34.m[3][0] = bmat34.m[3][1] = bmat34.m[3][2] = rZero;
        BrMatrix34PostRotateX(&bmat34, BrDegreeToAngle(BrFloatToScalar(ctman.pitch)));
        BrMatrix34PostRotateY(&bmat34, BrDegreeToAngle(BrFloatToScalar(ctman.yaw)));
        if (ctman.roll != 0.0f)
            BrMatrix34PostRotateZ(&bmat34, BrDegreeToAngle(BrFloatToScalar(ctman.roll)));
        bmat34.m[3][0] = xrCamera;
        bmat34.m[3][1] = yrCamera;
        bmat34.m[3][2] = zrCamera;

        BRS xr = BrFloatToScalar(ctman.x);
        BRS yr = BrFloatToScalar(ctman.y);
        BRS zr = BrFloatToScalar(ctman.z);
        bmat34.m[3][0] += BrsMul(xr, bmat34Move.m[0][0]) +
                           BrsMul(yr, bmat34Move.m[1][0]) -
                           BrsMul(zr, bmat34Move.m[2][0]);
        bmat34.m[3][1] += BrsMul(xr, bmat34Move.m[0][1]) +
                           BrsMul(yr, bmat34Move.m[1][1]) -
                           BrsMul(zr, bmat34Move.m[2][1]);
        bmat34.m[3][2] += BrsMul(xr, bmat34Move.m[0][2]) +
                           BrsMul(yr, bmat34Move.m[1][2]) -
                           BrsMul(zr, bmat34Move.m[2][2]);
    }

    // Camera-track movement can bring the camera much closer to geometry than
    // the stock fixed scene cameras were authored for.  Preserve the stock
    // hither plane on ordinary untracked frames, but use a closer near plane
    // for manual, Free Look, depth-tween, and tracked-yaw camera positions so
    // nearby 3-D word faces are not clipped prematurely.
    if (fCameraPose)
    {
        const BRS zrTrackHitherMax = BR_SCALAR(0.1);
        if (zrHither > zrTrackHitherMax)
            zrHither = zrTrackHitherMax;
    }

    // Even an untracked frame is set explicitly to its base camera.  This is
    // what makes a temporary Free Look override disappear immediately when
    // the timeline moves to another frame.
    Pbwld()->SetCamera(&bmat34, zrHither, zrYon, aFov);
    Pbwld()->MarkDirty();
    if (_fBrowserCameraFollow)
        FUpdateBrowserCameraFollow();
}

/** 3DMMv1.0: ****************************************************************************
    _FSetPfilSave
        Given an FNI, looks for and remembers if found the FIL associated with
        it.  If the FIL was found, will also check to see if it's read-only.

    Returns:
        fFalse if the FIL wasn't found.

************************************************************ PETED ***********/
bool MVIE::_FSetPfilSave(PFNI pfni)
{
    AssertBaseThis(0);
    AssertPo(pfni, 0);

    int32_t lAttrib;
    STN stnFile;

    /* 3DMMv1.0: Look for the file and remember FIL if found */
    ReleasePpo(&_pfilSave);
    _pfilSave = FIL::PfilFromFni(pfni);
    if (_pfilSave == pvNil)
        return fFalse;
    _pfilSave->AddRef();
    _fFniSaveValid = fTrue;

    /* 3DMMv1.0: Remember whether FIL is read-only; only relevant if we actually found
        the FIL, since if we didn't, we'll prompt for a new filename later
        anyway */
    pfni->GetStnPath(&stnFile);
#ifdef MAC // 3DMMv1.0: MAC
    RawRtn();
#else
    _fReadOnly = pfni->FIsReadOnly();
#endif
    return fTrue;
}

/******************************************************************************
    _WarmMovieCache

    Examine every scene before the movie becomes visible.  Scene parsing,
    actor route evaluation, action/cel model selection, costumes, text boxes,
    cameras, and sounds all use the ordinary 3DMM code paths, which leaves
    their resources in the existing TAGM/CRF and operating-system caches.
    Nothing is played, saved, or added to undo history.
******************************************************************************/
void MVIE::_WarmMovieCache(void)
{
    AssertThis(0);

    if (_pcrfAutoSave == pvNil || _cscen <= 0 || _pscenOpen != pvNil)
        return;

#if defined(KAUAI_WIN32)
    HWND hwndProgress = HwndCreateMovieCacheProgress();
#else
    void *hwndProgress = pvNil;
#endif

    bool fAutosaveDirtySave = _fAutosaveDirty;
    bool fDirtySave = _fDirty;
    TRANS transSave = _trans;
    WIT witSave = _wit;
    int32_t dtsSave = _dts;
    int32_t lwProgressLast = -1;
    PCFL pcfl = _pcrfAutoSave->Pcfl();

    for (int32_t iscen = 0; iscen < _cscen; iscen++)
    {
        KID kid;

#if defined(KAUAI_WIN32)
        int32_t lwSceneStart = LwMul(iscen, 1000) / _cscen;
        UpdateMovieCacheProgress(hwndProgress, lwSceneStart);
#endif

        if (!pcfl->FGetKidChidCtg(kctgMvie, _cno, iscen, kctgScen, &kid))
            continue;

        PSCEN pscen = SCEN::PscenRead(this, _pcrfAutoSave, kid.cki.cno);
        if (pscen == pvNil)
            continue;

        // Temporarily make this the current scene.  FGotoFrm and camera-track
        // evaluation expect MVIE::Pscen() to identify the scene being examined.
        _pscenOpen = pscen;
        _iscen = iscen;

        // Fetch sounds through the ordinary replay path, but never start the
        // queue.  Pauses must not interrupt a synchronous load examination.
        pscen->Disable(fscenSounds | fscenPauses);

        bool fSceneReady = pscen->FPlayStartEvents();
        int32_t nfrmFirst = pscen->NfrmFirst();
        int32_t nfrmLast = pscen->NfrmLast();
        int32_t cnfrm = LwMax(1, nfrmLast - nfrmFirst + 1);

        if (fSceneReady)
        {
            for (int32_t nfrm = nfrmFirst; nfrm <= nfrmLast; nfrm++)
            {
                if (!pscen->FGotoFrm(nfrm))
                    break;

                // FGotoFrm resolves actors, actions, cels, models, materials,
                // backgrounds, cameras, and text.  Replay only the sound layer
                // so its decoders/resources are fetched too, then discard the
                // queue before it can produce audio.
                pscen->FReplayFrm(fscenSounds | fscenActrs);
                _pmsq->FlushMsq();

#if defined(KAUAI_WIN32)
                int32_t lwScenePart = LwMul(nfrm - nfrmFirst + 1, 1000) / cnfrm;
                int32_t lwProgress = (LwMul(iscen, 1000) + lwScenePart) / _cscen;
                if (lwProgress >= lwProgressLast + 2 || nfrm == nfrmLast)
                {
                    UpdateMovieCacheProgress(hwndProgress, lwProgress);
                    lwProgressLast = lwProgress;
                }
#endif
            }
        }

        _pmsq->FlushMsq();
        SCEN::Close(&_pscenOpen);
        _iscen = ivNil;
    }

    // The examination pass uses normal scene code, some of which may set
    // transient document flags.  Loading a movie must remain a clean action.
    _fAutosaveDirty = fAutosaveDirtySave;
    _fDirty = fDirtySave;
    _trans = transSave;
    _wit = witSave;
    _dts = dtsSave;
    _pmsq->FlushMsq();

#if defined(KAUAI_WIN32)
    UpdateMovieCacheProgress(hwndProgress, 1000);
    if (hwndProgress != hNil)
        DestroyWindow(hwndProgress);
#endif
}

/** 3DMMv1.0: **************************************************
 *
 * Reads a movie from a file.
 *
 * Parameters:
 *  pmcc - Pointer to the movie client class block to use.
 *	pfni - File to read from.
 *	cno - CNO of the movie chunk, cnoNil if using the
 *		the first one in the file.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie object.
 *
 ****************************************************/
PMVIE MVIE::PmvieNew(bool fHalfMode, PMCC pmcc, FNI *pfni, CNO cno)
{
    AssertNilOrPo(pfni, 0);
    AssertPo(pmcc, 0);

    bool fSuccess = fFalse, fBeganLongOp = fFalse;
    PMVIE pmvie;
    KID kid;
    CHID chid;
    TAGL *ptagl;
    PCFL pcfl = pvNil;
    BLCK blck;
    int16_t bo;
    int16_t osk;
    PGST pgstSource;

    //
    // 3DMMv1.0: Create the movie object
    //
    pmvie = NewObj MVIE;
    if (pmvie == pvNil)
    {
        goto LFail;
    }

    //
    // 3DMMv1.0: Create the GL for holding undo events
    //
    pmvie->_pglpundb = GL::PglNew(SIZEOF(PUNDB), 1);
    if (pmvie->_pglpundb == pvNil)
    {
        goto LFail;
    }

    //
    // 3DMMv1.0: Create GL of actors in the movie
    //
    if (pvNil == pfni)
    {
        pmvie->_pgstmactr = GST::PgstNew(SIZEOF(MACTR));
        if (pmvie->_pgstmactr == pvNil)
        {
            goto LFail;
        }
    }

    //
    // 3DMMv1.0: Create the brender world
    //
    pmvie->_pbwld = BWLD::PbwldNew(pmcc->Dxp(), pmcc->Dyp(), fFalse, fHalfMode);
    if (pvNil == pmvie->_pbwld)
    {
        goto LFail;
    }
    if (FTestLightMode())
        pmvie->_EnableTestLight();

    //
    // 3DMMv1.0: Create the movie sound queue
    //
    pmvie->_pmsq = MSQ::PmsqNew();
    if (pvNil == pmvie->_pmsq)
    {
        goto LFail;
    }

    //
    // 3DMMv1.0: Save other variables
    //
    pmvie->_pmcc = pmcc;
    pmcc->AddRef();

    //
    // 3DMMv1.0: Do we initialize from a file?
    //
    if (pfni == pvNil)
    {
        return (pmvie);
    }

    /* 3DMMv1.0: Don't bother putting up the wait cursor unless we're reading a movie */
    vpappb->BeginLongOp();
    fBeganLongOp = fTrue;

    //
    // 3DMMv1.0: Get file to read from
    //
    pcfl = CFL::PcflOpen(pfni, fcflNil);
    if (pcfl == pvNil)
    {
        goto LFail;
    }

    if (!pmvie->FVerifyVersion(pcfl, &cno))
        goto LFail;
    pmvie->_cno = cno;

    //
    // 3DMMv1.0: Keep a reference to the user's file
    //
    if (!pmvie->_FSetPfilSave(pfni))
    {
        Bug("Hey, we just opened this file!");
        goto LFail;
    }

    // Load an optional matching camera-track sidecar, for example:
    //     street-scene.3mm  ->  street-scene.3ct
    pmvie->_LoadCameraTrack(pfni);

    //
    // 3DMMv1.0: Note (by *****): CRF *must* have 0 cache size, because of
    // 3DMMv1.0: serious cache-coherency problems otherwise.  TMPL data is not
    // 3DMMv1.0: read-only, and chunk numbers change over time.
    //
    pmvie->_pcrfAutoSave = CRF::PcrfNew(pcfl, 0); // 3DMMv1.0: cache size must be 0
    if (pvNil == pmvie->_pcrfAutoSave)
    {
        goto LFail;
    }

    //
    // 3DMMv1.0: Merge this document's source title list
    //
    if (pcfl->FGetKidChidCtg(kctgMvie, cno, kchidGstSource, kctgGst, &kid) &&
        pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
    {
        pgstSource = GST::PgstRead(&blck, &bo, &osk);
        if (pvNil != pgstSource)
        {
            // 3DMMv1.0: Ignore result...we can survive failure
            vptagm->FMergeGstSource(pgstSource, bo, osk);
            ReleasePpo(&pgstSource);
        }
    }

    //
    // 3DMMv1.0: Get the movie roll-call
    //
    Assert(pcfl == pmvie->_pcrfAutoSave->Pcfl(), "pcfl isn't right");
    if (!FReadRollCall(pmvie->_pcrfAutoSave, cno, &pmvie->_pgstmactr, &pmvie->_aridLim))
    {
        pmvie->_pgstmactr = pvNil;
        goto LFail;
    }

    //
    // 3DMMv1.0: Get all the content tags
    //
    ptagl = pmvie->_PtaglFetch();
    if (ptagl == pvNil)
    {
        goto LFail;
    }

    //
    // 3DMMv1.0: Now bring all the tags into cache
    //
    if (!ptagl->FCacheTags())
    {
        ReleasePpo(&ptagl);
        goto LFail;
    }

    ReleasePpo(&ptagl);

    //
    // 3DMMv1.0: Set the movie title
    //
    pmvie->_SetTitle(pfni);

    //
    // 3DMMv1.0: Count the number of scenes in the movie
    //
    for (chid = 0; pcfl->FGetKidChidCtg(kctgMvie, cno, chid, kctgScen, &kid); chid++, pmvie->_cscen++)
    {
    }

    // Prime all scene resources only when the explicit -precache launch
    // option is present.  Normal movie loading remains unchanged by default.
    if (FPrecacheMode())
        pmvie->_WarmMovieCache();

    fSuccess = fTrue;

LFail:
    ReleasePpo(&pcfl);
    if (!fSuccess)
        ReleasePpo(&pmvie);
    if (fBeganLongOp)
        vpappb->EndLongOp();

    return (pmvie);
}

/** 3DMMEx: ****************************************************************************
    Deserialize rollcall from on-disk format
******************************************************************************/
PGST DeserializeRollCall(int16_t bo, PGST pgst)
{
    AssertPo(pgst, 0);

    int32_t imactr, imactrMac;
    MACTR mactr;
    MACTRF mactrf;
    PGST _pgst;
    STN stn;

    /* 3DMMEx: We need to return a new GST since cbExtra is different */
    _pgst = GST::PgstNew(SIZEOF(MACTR));
    if (_pgst == pvNil)
        return pvNil;

    imactrMac = pgst->IvMac();
    for (imactr = 0; imactr < imactrMac; imactr++)
    {
        pgst->GetExtra(imactr, &mactrf);
        if (bo == kboOther)
            SwapBytesBom(&mactrf, kbomMactr);

        mactr.arid = mactrf.arid;
        mactr.cactRef = mactrf.cactRef;
        mactr.grfbrws = mactrf.grfbrws;
        DeserializeTagfToTag(&mactrf.tagTmpl, &mactr.tagTmpl);

        pgst->GetStn(imactr, &stn);
        _pgst->FAddStn(&stn, &mactr);
    }

    return _pgst;
}

/** 3DMMEx: ****************************************************************************
    Serialize rollcall to on-disk format
******************************************************************************/
PGST SerializeRollCall(PGST pgst)
{
    AssertPo(pgst, 0);

    int32_t imactr, imactrMac;
    PGST _pgst;
    MACTR mactr;
    MACTRF mactrf;
    STN stn;

    _pgst = GST::PgstNew(SIZEOF(MACTRF));
    if (_pgst == pvNil)
        return pvNil;

    imactrMac = pgst->IvMac();
    for (imactr = 0; imactr < imactrMac; imactr++)
    {
        pgst->GetExtra(imactr, &mactr);

        mactrf.arid = mactr.arid;
        mactrf.cactRef = mactr.cactRef;
        mactrf.grfbrws = mactr.grfbrws;
        SerializeTagToTagf(&mactr.tagTmpl, &mactrf.tagTmpl);

        pgst->GetStn(imactr, &stn);
        _pgst->FAddStn(&stn, &mactrf);
    }

    return _pgst;
}

/** 3DMMv1.0: ****************************************************************************
    FReadRollCall
        Reads the roll call off file for a given movie.  Will swapbytes the
        extra data in the GST if necessary, and will report back on the
        highest arid found.

    Arguments:
        PCFL pcfl       -- the file the movie is on
        PCRF pcrf       -- the autosave CRF for the movie's ACTR tags
        CNO cno         -- the cno of the movie
        PGST *ppgst     -- the PGST to fill in
        long *paridLim  -- the max arid to update

    Returns: fTrue if there were no failures, fFalse otherwise

************************************************************ PETED ***********/
bool MVIE::FReadRollCall(PCRF pcrf, CNO cno, PGST *ppgst, int32_t *paridLim)
{
    AssertPo(pcrf, 0);
    AssertVarMem(ppgst);
    Assert(*ppgst == pvNil, "Overwriting existing GST");
    AssertNilOrVarMem(paridLim);

    int16_t bo;
    int32_t imactr, imactrMac;
    PCFL pcfl = pcrf->Pcfl();
    KID kid;
    BLCK blck;
    MACTR mactr;
    PGST pgst;

    if (!pcfl->FGetKidChidCtg(kctgMvie, cno, 0, kctgGst, &kid) || !pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
    {
        PushErc(ercSocBadFile);
        goto LFail;
    }

    pgst = GST::PgstRead(&blck, &bo);
    if (pgst == pvNil)
        goto LFail;

    *ppgst = DeserializeRollCall(bo, pgst);
    if (!*ppgst)
    {
        ReleasePpo(&pgst);
        goto LFail;
    }

    imactrMac = (*ppgst)->IvMac();
    for (imactr = 0; imactr < imactrMac; imactr++)
    {
        (*ppgst)->GetExtra(imactr, &mactr);

        if (paridLim != pvNil && mactr.arid >= *paridLim)
            *paridLim = mactr.arid + 1;

        // 3DMMv1.0: Open the tags, since they might be TDTs
        AssertDo(vptagm->FOpenTag(&mactr.tagTmpl, pcrf), "Should never fail when not copying the tag");

        (*ppgst)->PutExtra(imactr, &mactr);
    }
    ReleasePpo(&pgst);

    return fTrue;
LFail:
    TrashVar(ppgst);
    return fFalse;
}

/** 3DMMv1.0: ****************************************************************************
    Flush
        Ensures that the data has been written to disk.

************************************************************ PETED ***********/
void MVIE::Flush(void)
{
    if (_fFniSaveValid)
    {
        AssertPo(_pfilSave, 0);
        _pfilSave->Flush();
    }
}

/** 3DMMv1.0: **************************************************
 *
 * Nuke unused sounds from the file
 * Msnd chunks exist as children of the movie chunk.
 * They are also children of any scene which uses
 * them.
 * Note: The ref count does not reflect how many
 * scene or actor events reference the sound.
 *
 * Parms:
 *    bool fPurgeAll -- if fFalse, only purge invalid sounds
 *
 ****************************************************/
void MVIE::_DoSndGarbageCollection(bool fPurgeAll)
{
    AssertThis(0);

    // 3DMMv1.0: Before releasing, get rid of all msnd chunks with
    // 3DMMv1.0: cactref == 1 (the refcnt from being a child of the
    // 3DMMv1.0: movie chunk)

    int32_t ikid;

    if (pvNil == _pcrfAutoSave)
        return;

    PCFL pcfl = _pcrfAutoSave->Pcfl();
    if (pvNil == pcfl)
        return;

    /* 3DMMv1.0: Go backwards, since we might delete some */
    ikid = pcfl->Ckid(kctgMvie, _cno);
    while (ikid--)
    {
        KID kid;
        KID kidT;

        if (!pcfl->FGetKid(kctgMvie, _cno, ikid, &kid))
        {
            Bug("CFL returned bogus Ckid()");
            break;
        }

        if (kid.cki.ctg != kctgMsnd)
            continue;

        if (pcfl->CckiRef(kctgMsnd, kid.cki.cno) > 1)
            continue;

        /* 3DMMv1.0: Always purge MSNDs with no actual sound attached */
        if (fPurgeAll || !pcfl->FGetKidChid(kctgMsnd, kid.cki.cno, kchidSnd, &kidT))
        {
            pcfl->DeleteChild(kctgMvie, _cno, kctgMsnd, kid.cki.cno, kid.chid);
        }
    }

    return;
}

/** 3DMMv1.0: **************************************************
 *
 * Destructor for movies.
 *
 ****************************************************/
MVIE::~MVIE(void)
{
    AssertBaseThis(0);

    // Free Look may intentionally leave a temporary camera override active
    // after Tab/Esc.  Tear it down before the view and scene begin dying.
    if (_fFreeLookMode || _fFreeLookOverride)
        _ClearFreeLookState();

#if defined(KAUAI_WIN32)
    if (vpmvieDepthMotionTween == this && vhwndDepthMotionTween != hNil &&
        IsWindow(vhwndDepthMotionTween))
    {
        // Closing a movie discards any unsaved external-editor text.
        DestroyWindow(vhwndDepthMotionTween);
    }
    if (vpmvieLightLab == this && vhwndLightLab != hNil && IsWindow(vhwndLightLab))
        DestroyWindow(vhwndLightLab);
    if (vpmvieObjectProperties == this && vhwndObjectProperties != hNil && IsWindow(vhwndObjectProperties))
        DestroyWindow(vhwndObjectProperties);
    if (vpmvieManualCameraFrame == this && vhwndManualCameraFrame != hNil &&
        IsWindow(vhwndManualCameraFrame))
    {
        // v36 makes the Manual Camera editor an unowned tool window so it no
        // longer minimizes the main 3DMM window. Close it explicitly with its
        // movie to avoid leaving a native window holding a stale MVIE pointer.
        DestroyWindow(vhwndManualCameraFrame);
    }
#endif

    int32_t imactr;
    MACTR mactr;

    ReleasePpo(&_pcrfAutoSave);
    ReleasePpo(&_pfilSave);

    if (FPlaying())
    {
        AssertPo(Pmcc(), 0);
        Pmcc()->EnableAccel();
        Pmcc()->PlayStopped();
        vpsndm->StopAll();

        // 3DMMv1.0: if we were fading, then restore sound volume
        if (_vlmOrg)
        {
            vpsndm->SetVlm(_vlmOrg);
            _vlmOrg = 0;
        }
    }

    if (Pmcc() != pvNil)
    {
        Pmcc()->UpdateRollCall();
    }

    // The experimental light actor is embedded in MVIE and must be detached
    // before either the scene or BWLD tears down its actor tree.
    _DisableTestLight();

    if (Pscen() != pvNil)
    {

        // BODY roots owned by Object Group parents must return to world space
        // before SCEN destroys the actors and BWLD later destroys the parent.
        _DestroyObjectGroupRenderParents();

        //
        // 3DMMv1.0: Release open scene.
        //
        SCEN::Close(&_pscenOpen);
    }

    //
    // 3DMMv1.0: Release the roll call
    //
    if (_pgstmactr != pvNil)
    {
        for (imactr = 0; imactr < _pgstmactr->IvMac(); imactr++)
        {
            _pgstmactr->GetExtra(imactr, &mactr);
            vptagm->CloseTag(&mactr.tagTmpl);
        }
        ReleasePpo(&_pgstmactr);
    }

    //
    // 3DMMv1.0: Release the call back class
    //
    ReleasePpo(&_pmcc);

    //
    // 3DMMv1.0: Release the brender world
    //
    ReleasePpo(&_pbwld);

    //
    // 3DMMv1.0: Release the sound queue
    //
    ReleasePpo(&_pmsq);

    ReleasePpo(&_pglclrThumbPalette);

    return;
}

#ifdef DEBUG

/** 3DMMv1.0: **************************************************
 * Mark memory used by the MVIE
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void MVIE::MarkMem(void)
{
    AssertThis(0);

    MVIE_PAR::MarkMem();

    MarkMemObj(_pcrfAutoSave);

    MarkMemObj(_pfilSave);

    MarkMemObj(_pgstmactr);

    MarkMemObj(Pscen());

    MarkMemObj(Pbwld());

    MarkMemObj(_pmcc);

    MarkMemObj(_pmsq);

    MarkMemObj(_pglclrThumbPalette);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Assert the validity of the MVIE.
 *
 * Parameters:
 *	grf - Bit field of options
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MVIE::AssertValid(uint32_t grf)
{
    MVIE_PAR::AssertValid(fobjAllocated);

    AssertNilOrPo(_pcrfAutoSave, 0);
    // During movie construction the camera-track preview can run before the
    // roll-call GST has been installed.  That is a valid transient load state.
    AssertNilOrPo(_pgstmactr, 0);
    AssertNilOrPo(Pbwld(), 0);
    AssertPo(&_clok, 0);
    AssertPo(_pmcc, 0);
    AssertIn(_cctween, 0, kcctweenMax + 1);
}

#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
 *
 * Returns a list of all tags being used by this MVIE
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  A TAGL (list of tags that the movie uses)
 *
 **************************************************************************/
PTAGL MVIE::_PtaglFetch(void)
{
    AssertThis(0);
    Assert(_pcrfAutoSave != pvNil, "need pcrfAutosave");

    PTAGL ptagl;
    KID kid;
    CHID chid;

    ptagl = TAGL::PtaglNew();
    if (pvNil == ptagl)
        return pvNil;

    //
    // 3DMMv1.0: Add each scene's tags
    //
    for (chid = 0; _pcrfAutoSave->Pcfl()->FGetKidChidCtg(kctgMvie, _cno, chid, kctgScen, &kid); chid++)
    {
        if (!SCEN::FAddTagsToTagl(_pcrfAutoSave->Pcfl(), kid.cki.cno, ptagl))
        {
            ReleasePpo(&ptagl);
            return pvNil;
        }
    }

    return ptagl;
}

/** 3DMMv1.0: **************************************************
 *
 * Fetches the iarid'th actor in the movie.
 *
 * Parameters:
 *   iarid - The arid index to fetch.
 *   parid - Pointer to storage for the found arid.
 *	 pstn  - Pointer to the actors name.
 *	 pcactRef - Number of scenes the actor is in.
 *	 *ptagTmpl - Template tag
 * Returns:
 *	 fTrue if successful, else fFalse if out of range.
 *
 ****************************************************/
bool MVIE::FGetArid(int32_t iarid, int32_t *parid, PSTN pstn, int32_t *pcactRef, PTAG ptagTmpl)
{
    AssertThis(0);
    AssertPvCb(parid, SIZEOF(int32_t));
    AssertVarMem(pcactRef);

    MACTR mactr;

    if (iarid < 0 || iarid >= _pgstmactr->IvMac())
    {
        return fFalse;
    }

    _pgstmactr->GetStn(iarid, pstn);
    _pgstmactr->GetExtra(iarid, &mactr);
    *parid = mactr.arid;
    *pcactRef = mactr.cactRef;
    if (pvNil != ptagTmpl)
        *ptagTmpl = mactr.tagTmpl;
    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * User chose arid in the roll call.  If actor
 * exists in this scene and is onstage, select it.
 * Else if actor is offstage, bring it onstage.
 * If actor is not in this scene, create and add it.
 *
 * Parameters:
 *	arid - The arid to search for, or create.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool MVIE::FChooseArid(int32_t arid)
{
    AssertThis(0);
    AssertPo(Pscen(), 0);

    PACTR pactr, pactrDup;
    MACTR mactr;
    int32_t imactr;
    PMVU pmvu;

    pmvu = (PMVU)PddgGet(0);
    if (pmvu == pvNil)
    {
        return (fFalse);
    }
    AssertPo(pmvu, 0);

    pactr = Pscen()->PactrFromArid(arid);

    if (pvNil != pactr)
    {

        if (!pactr->FIsInView())
        {

            if (!pactr->FDup(&pactrDup, fTrue))
            {
                return (fFalse);
            }
            AssertPo(pactrDup, 0);

            if (!pactr->FAddOnStageCore())
            {
                ReleasePpo(&pactrDup);
                return fFalse;
            }
            Pscen()->SelectActr(pactr);
            AssertDo(FAddToRollCall(pactr, pvNil), "Should never fail");
            pmvu->StartPlaceActor();
            pmvu->SetActrUndo(pactrDup);
        }

        Pscen()->SelectActr(pactr);
        InvalViews();
        return (fTrue);
    }
    else
    {

        //
        // 3DMMv1.0: Search roll call for actor, and create
        // 3DMMv1.0: a new actor for it.
        //
        for (imactr = 0; imactr < _pgstmactr->IvMac(); imactr++)
        {
            _pgstmactr->GetExtra(imactr, &mactr);
            if (mactr.arid == arid)
            {
                pactr = ACTR::PactrNew(&(mactr.tagTmpl));

                if (pactr == pvNil)
                {
                    return (fFalse);
                }

                AssertPo(pactr, 0);

                pactr->SetArid(arid);

                if (!Pscen()->FAddActr(pactr))
                {
                    ReleasePpo(&pactr);
                    return (fFalse);
                }

                pmvu->StartPlaceActor();
                ReleasePpo(&pactr);
                return (fTrue);
            }
        }
    }

    return (fFalse);
}

/** 3DMMv1.0: **************************************************
 *
 * Return the arid of the selected actor.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   Arid of the selected actor, else aridNil.
 *
 ****************************************************/
int32_t MVIE::AridSelected(void)
{
    AssertThis(0);

    if ((pvNil != Pscen()) && (pvNil != Pscen()->PactrSelected()))
    {
        return Pscen()->PactrSelected()->Arid();
    }
    else
    {
        return aridNil;
    }
}

/** 3DMMv1.0: **************************************************
 *
 * Fetches the name of the actor with arid.
 *
 * Parameters:
 *   arid - The arid to fetch.
 *   pstn - Pointer to storage for the found name.
 *
 * Returns:
 *	 fTrue if successful, else fFalse if failure.
 *
 ****************************************************/
bool MVIE::FGetName(int32_t arid, PSTN pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    MACTR mactr;
    int32_t imactr;

    for (imactr = 0; imactr < _pgstmactr->IvMac(); imactr++)
    {
        _pgstmactr->GetExtra(imactr, &mactr);
        if (mactr.arid == arid)
        {
            _pgstmactr->GetStn(imactr, pstn);
            return (fTrue);
        }
    }

    return (fFalse);
}

/****************************************************
 *
 * Return the display name used by roll-call/content browsers.  The synthetic
 * Light Lab badge deliberately never enters the stored/editable name, so the
 * actor dresser, prop colour/name easel, and 3D Words editor stay clean.
 *
 ****************************************************/
bool MVIE::FGetDisplayName(int32_t arid, PSTN pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    if (!FGetName(arid, pstn))
        return fFalse;

    if (_ILightLabFind(Iscen(), arid) != ivNil)
    {
        STN stnBase = *pstn;
        STN stnDisplay;
        if (stnDisplay.FFormatSz(PszLit("(L) %s"), &stnBase))
            *pstn = stnDisplay;
    }
    if (!FObjectSelectable(arid))
    {
        STN stnBase = *pstn;
        STN stnDisplay;
        if (stnDisplay.FFormatSz(PszLit("(X) %s"), &stnBase))
            *pstn = stnDisplay;
    }
    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * Sets the name of the actor with arid.
 *
 * Parameters:
 *   arid - The arid to set.
 *   pstn - Pointer to the name.
 *
 * Returns:
 *	 fTrue if successful, else fFalse.
 *
 ****************************************************/
bool MVIE::FNameActr(int32_t arid, PSTN pstn)
{
    AssertThis(0);
    AssertIn(arid, 0, 500);
    AssertPo(pstn, 0);

    MACTR mactr;
    int32_t imactr;

    for (imactr = 0; imactr < _pgstmactr->IvMac(); imactr++)
    {

        _pgstmactr->GetExtra(imactr, &mactr);
        if (mactr.arid == arid)
        {

            if (_pgstmactr->FPutStn(imactr, pstn))
            {

                _pmcc->UpdateRollCall();
                return (fTrue);
            }

            return (fFalse);
        }
    }

    return (fFalse);
}

/** 3DMMv1.0: **************************************************
 *
 * Identifies whether the mactr is in prop browser
 *
 * Parameters:
 *   imactr - index in _pgstmactr
 *
 * Returns:
 *	 bool
 *
 ****************************************************/
bool MVIE::FIsPropBrwsIarid(int32_t iarid)
{
    AssertThis(0);
    AssertIn(iarid, 0, _pgstmactr->IvMac());

    MACTR mactr;
    _pgstmactr->GetExtra(iarid, &mactr);
    return FPure(mactr.grfbrws & fbrwsProp);
}

/** 3DMMv1.0: **************************************************
 *
 * Identifies whether the mactr is in 3dtext object
 *
 * Parameters:
 *   imactr - index in _pgstmactr
 *
 * Returns:
 *	 bool
 *
 ****************************************************/
bool MVIE::FIsIaridTdt(int32_t iarid)
{
    AssertThis(0);
    AssertIn(iarid, 0, _pgstmactr->IvMac());

    MACTR mactr;
    _pgstmactr->GetExtra(iarid, &mactr);
    return FPure(mactr.grfbrws & fbrwsTdt);
}

/***************************************************************************
    True when this roll-call entry was created by 4DMM's native VXP import.
    grfbrws is persisted in MACTR, so this marker survives save/reload with
    the template itself and does not need a second registry.
***************************************************************************/
bool MVIE::FIsVxpBrwsIarid(int32_t iarid)
{
    AssertThis(0);
    AssertIn(iarid, 0, _pgstmactr->IvMac());

    MACTR mactr;
    _pgstmactr->GetExtra(iarid, &mactr);
    return FPure(mactr.grfbrws & fbrwsVxp);
}

/** 3DMMv1.0: **************************************************
 *
 * Sets the tag of the actor with arid.
 *
 * Parameters:
 *   arid - The arid to set.
 *   ptag - Pointer to the TMPL tag.
 *
 * Returns:
 *	 none
 *
 ****************************************************/
void MVIE::ChangeActrTag(int32_t arid, PTAG ptag)
{
    AssertThis(0);
    AssertIn(arid, 0, 500);
    AssertVarMem(ptag);

    MACTR mactr;
    int32_t imactr;

    for (imactr = 0; imactr < _pgstmactr->IvMac(); imactr++)
    {

        _pgstmactr->GetExtra(imactr, &mactr);
        if (mactr.arid == arid)
        {

            TAGM::CloseTag(&mactr.tagTmpl);
            mactr.tagTmpl = *ptag;
            TAGM::DupTag(&mactr.tagTmpl);
            _pgstmactr->PutExtra(imactr, &mactr);
            return;
        }
    }

    Bug("no such actor");
}

/***************************************************************************
    Change only the actor/prop classification bit on a movie-owned cloned
    TMPL. VXP content itself is normal 3DMM Chunky data; importing as Actor
    versus Prop is therefore a template semantic choice, not a different
    archive format.
***************************************************************************/
static bool F4DMMSetImportedTemplatePropFlag(PCFL pcfl, CNO cnoTmpl, bool fProp)
{
    if (pcfl == pvNil || cnoTmpl == cnoNil)
        return fFalse;

    BLCK blck;
    TMPLF tmplf;
    if (!pcfl->FFind(kctgTmpl, cnoTmpl, &blck) || !blck.FUnpackData() ||
        blck.Cb() < SIZEOF(TMPLF) || !blck.FReadRgb(&tmplf, SIZEOF(TMPLF), 0))
        return fFalse;

    const bool fSwap = (tmplf.bo == kboOther);
    if (fSwap)
        SwapBytesBom(&tmplf, kbomTmplf);
    if (tmplf.bo != kboCur)
        return fFalse;

    if (fProp)
        tmplf.grftmpl |= ftmplProp;
    else
        tmplf.grftmpl &= ~((uint32_t)ftmplProp);

    // Preserve the source chunk's byte order if it was foreign-endian.
    if (fSwap)
        SwapBytesBom(&tmplf, kbomTmplf);
    return blck.FWriteRgb(&tmplf, SIZEOF(TMPLF), 0);
}


static bool F4DMMGetImportedTemplatePropFlag(PCFL pcfl, CNO cnoTmpl, bool *pfProp)
{
    if (pfProp != pvNil)
        *pfProp = fFalse;
    if (pcfl == pvNil || cnoTmpl == cnoNil || pfProp == pvNil)
        return fFalse;

    BLCK blck;
    TMPLF tmplf;
    if (!pcfl->FFind(kctgTmpl, cnoTmpl, &blck) || !blck.FUnpackData() ||
        blck.Cb() < SIZEOF(TMPLF) || !blck.FReadRgb(&tmplf, SIZEOF(TMPLF), 0))
        return fFalse;
    if (kboCur != tmplf.bo)
        SwapBytesBom(&tmplf, kbomTmplf);
    if (tmplf.bo != kboCur)
        return fFalse;
    *pfProp = FPure(tmplf.grftmpl & ftmplProp);
    return fTrue;
}

#if defined(KAUAI_WIN32)
static bool F4DMMVxp2SafeRelativePngPath(PCFL pcfl, CNO cnoT24m,
                                         std::filesystem::path *ppathRel)
{
    if (pcfl == pvNil || ppathRel == pvNil)
        return fFalse;

    BLCK blck;
    if (!pcfl->FFind(kctgT24M, cnoT24m, &blck) || !blck.FUnpackData())
        return fFalse;
    const int32_t cb = blck.Cb();
    if (cb <= 1 || cb > 4096)
        return fFalse;

    std::vector<char> rgch((size_t)cb + 1, 0);
    if (!blck.FReadRgb(rgch.data(), cb, 0) || rgch[(size_t)cb - 1] != 0)
        return fFalse;
    if (strlen(rgch.data()) + 1 != (size_t)cb)
        return fFalse;

    std::filesystem::path pathRel(rgch.data());
    pathRel = pathRel.lexically_normal();
    if (pathRel.empty() || pathRel.is_absolute() || pathRel.has_root_name() || pathRel.has_root_directory())
        return fFalse;
    for (const auto &part : pathRel)
    {
        if (part == "..")
            return fFalse;
    }
    const std::string ext = pathRel.extension().string();
    if (_stricmp(ext.c_str(), ".png") != 0)
        return fFalse;
    *ppathRel = pathRel;
    return fTrue;
}

static bool F4DMMVxp2EmbedTexture(PMVIE pmvie, PCFL pcfl, CNO cnoT24m,
                                  const std::filesystem::path &pathBase)
{
    if (pmvie == pvNil || pcfl == pvNil)
        return fFalse;

    KID kidExisting;
    if (pcfl->FGetKidChidCtg(kctgT24M, cnoT24m, 0, kctgT24D, &kidExisting))
    {
        MVIE::MultiLog(pmvie, "vxp2_import texture FAIL t24m=%ld reason=preembedded_data",
                       (long)cnoT24m);
        return fFalse;
    }

    std::filesystem::path pathRel;
    if (!F4DMMVxp2SafeRelativePngPath(pcfl, cnoT24m, &pathRel))
    {
        MVIE::MultiLog(pmvie, "vxp2_import texture FAIL t24m=%ld reason=bad_png_path",
                       (long)cnoT24m);
        return fFalse;
    }

    const std::filesystem::path pathPng = pathBase / pathRel;
    std::error_code ec;
    const uintmax_t cbFile = std::filesystem::file_size(pathPng, ec);
    const uintmax_t kcbVxp2PngMax = 512ull * 1024ull * 1024ull;
    if (ec || cbFile < 8 || cbFile > kcbVxp2PngMax)
    {
        MVIE::MultiLog(pmvie, "vxp2_import texture FAIL t24m=%ld reason=file_size bytes=%llu path=%s",
                       (long)cnoT24m, (unsigned long long)(ec ? 0 : cbFile), pathPng.string().c_str());
        return fFalse;
    }

    std::ifstream ifs(pathPng, std::ios::binary);
    if (!ifs)
    {
        MVIE::MultiLog(pmvie, "vxp2_import texture FAIL t24m=%ld reason=open_png path=%s",
                       (long)cnoT24m, pathPng.string().c_str());
        return fFalse;
    }
    std::vector<uint8_t> rgbPng((size_t)cbFile);
    if (!ifs.read((char *)rgbPng.data(), (std::streamsize)rgbPng.size()))
    {
        MVIE::MultiLog(pmvie, "vxp2_import texture FAIL t24m=%ld reason=read_png path=%s",
                       (long)cnoT24m, pathPng.string().c_str());
        return fFalse;
    }
    static const uint8_t krgbPngSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (memcmp(rgbPng.data(), krgbPngSig, SIZEOF(krgbPngSig)) != 0)
    {
        MVIE::MultiLog(pmvie, "vxp2_import texture FAIL t24m=%ld reason=not_png path=%s",
                       (long)cnoT24m, pathPng.string().c_str());
        return fFalse;
    }

    // Stay well below Chunky's 24-bit per-chunk payload ceiling.  T24D is an
    // implementation detail of movie persistence; VXP2 itself still contains
    // the original ordinary PNG file.
    const size_t kcbPartMax = 8u * 1024u * 1024u;
    size_t ib = 0;
    CHID chid = 0;
    while (ib < rgbPng.size())
    {
        const size_t cbRemain = rgbPng.size() - ib;
        const size_t cbPart = cbRemain < kcbPartMax ? cbRemain : kcbPartMax;
        CNO cnoPart = cnoNil;
        if (!pcfl->FAddPv(rgbPng.data() + ib, (int32_t)cbPart, kctgT24D, &cnoPart) ||
            !pcfl->FAdoptChild(kctgT24M, cnoT24m, kctgT24D, cnoPart, chid))
        {
            if (cnoPart != cnoNil && pcfl->FFind(kctgT24D, cnoPart))
                pcfl->Delete(kctgT24D, cnoPart);
            MVIE::MultiLog(pmvie, "vxp2_import texture FAIL t24m=%ld reason=embed_png part=%ld path=%s",
                           (long)cnoT24m, (long)chid, pathPng.string().c_str());
            return fFalse;
        }
        ib += cbPart;
        ++chid;
    }

    MVIE::MultiLog(pmvie, "vxp2_import texture OK t24m=%ld bytes=%llu parts=%ld path=%s",
                   (long)cnoT24m, (unsigned long long)rgbPng.size(), (long)chid,
                   pathRel.generic_string().c_str());
    return fTrue;
}

static bool F4DMMVxp2EmbedTexturesRecursive(PMVIE pmvie, PCFL pcfl, CTG ctg, CNO cno,
                                            const std::filesystem::path &pathBase,
                                            std::set<uint64_t> *pvisited, int32_t *pcTexture)
{
    if (pmvie == pvNil || pcfl == pvNil || pvisited == pvNil || pcTexture == pvNil)
        return fFalse;
    const uint64_t key = ((uint64_t)(uint32_t)ctg << 32) | (uint32_t)cno;
    if (!pvisited->insert(key).second)
        return fTrue;

    for (int32_t ikid = 0;; ++ikid)
    {
        KID kid;
        if (!pcfl->FGetKid(ctg, cno, ikid, &kid))
            break;
        if (kid.cki.ctg == kctgT24M)
        {
            const uint64_t keyT24m = ((uint64_t)(uint32_t)kctgT24M << 32) | (uint32_t)kid.cki.cno;
            if (pvisited->insert(keyT24m).second)
            {
                if (!F4DMMVxp2EmbedTexture(pmvie, pcfl, kid.cki.cno, pathBase))
                    return fFalse;
                ++*pcTexture;
            }
            continue;
        }
        if (kid.cki.ctg == kctgT24D)
            continue;
        if (!F4DMMVxp2EmbedTexturesRecursive(pmvie, pcfl, kid.cki.ctg, kid.cki.cno,
                                             pathBase, pvisited, pcTexture))
            return fFalse;
    }
    return fTrue;
}

static bool F4DMMVxp2EmbedTextures(PMVIE pmvie, PCFL pcfl, CNO cnoTmpl, PCSZ pszBase,
                                   int32_t *pcTexture)
{
    if (pcTexture != pvNil)
        *pcTexture = 0;
    if (pmvie == pvNil || pcfl == pvNil || cnoTmpl == cnoNil || pszBase == pvNil || pcTexture == pvNil)
        return fFalse;
    std::set<uint64_t> visited;
    return F4DMMVxp2EmbedTexturesRecursive(pmvie, pcfl, kctgTmpl, cnoTmpl,
                                           std::filesystem::path(pszBase), &visited, pcTexture);
}
#endif // KAUAI_WIN32

/***************************************************************************
    Import every TMPL root from one extracted VXP .3cn into this movie.

    The resource tree is cloned into the autosave CRF, so no installed VXP
    directory or transient SID remains necessary after this function returns.
    The imported object gets a normal movie-owned ksidUseCrf roll-call tag and
    therefore survives normal VMM save/reload through the existing movie path.
***************************************************************************/
bool MVIE::FImport4DMMVxpContent(PFNI pfniContent, bool fProp, int32_t *pcImported,
                                     bool fVxp2, PCSZ pszVxp2TextureBase)
{
    AssertThis(0);
    if (pcImported != pvNil)
        *pcImported = 0;
    if (pfniContent == pvNil || !FEnsureAutosave())
        return fFalse;

    STN stnPath;
    pfniContent->GetStnPath(&stnPath);
    MultiLog(this, "%s chunky_begin path=%s%s", fVxp2 ? "vxp2_import" : "vxp_import",
             stnPath.Psz(), fVxp2 ? " type=from_4cn" : (fProp ? " as=prop" : " as=actor"));

    PCFL pcflSource = CFL::PcflOpen(pfniContent, fcflNil);
    if (pcflSource == pvNil)
    {
        MultiLog(this, "vxp_import FAIL stage=open_3cn path=%s", stnPath.Psz());
        return fFalse;
    }
    PCRF pcrfSource = CRF::PcrfNew(pcflSource, 0);
    ReleasePpo(&pcflSource);
    if (pcrfSource == pvNil)
    {
        MultiLog(this, "vxp_import FAIL stage=create_source_crf path=%s", stnPath.Psz());
        return fFalse;
    }

    pcflSource = pcrfSource->Pcfl();
    PCFL pcflDest = _pcrfAutoSave != pvNil ? _pcrfAutoSave->Pcfl() : pvNil;
    if (pcflSource == pvNil || pcflDest == pvNil)
    {
        ReleasePpo(&pcrfSource);
        return fFalse;
    }

    int32_t cImported = 0;
    int32_t icki = 0;
    CKI cki;
    while (pcflSource->FGetCkiCtg(kctgTmpl, icki++, &cki))
    {
        STN stnName;
        if (!pcflSource->FGetName(kctgTmpl, cki.cno, &stnName) || stnName.Cch() <= 0)
        {
            MultiLog(this, "vxp_import skip tmpl=%ld reason=missing_name", (long)cki.cno);
            continue;
        }

        CNO cnoOwnedTmpl = cnoNil;
        if (!F4DMMCloneResourceTree(pcrfSource, kctgTmpl, cki.cno, pcflDest, &cnoOwnedTmpl))
        {
            MultiLog(this, "vxp_import FAIL stage=clone tmpl=%ld name=%s", (long)cki.cno, stnName.Psz());
            continue;
        }

        bool fPropThis = fProp;
        if (fVxp2)
        {
            if (!F4DMMGetImportedTemplatePropFlag(pcflDest, cnoOwnedTmpl, &fPropThis))
            {
                MultiLog(this, "vxp2_import FAIL stage=read_type source=%ld owned=%ld name=%s",
                         (long)cki.cno, (long)cnoOwnedTmpl, stnName.Psz());
                if (pcflDest->FFind(kctgTmpl, cnoOwnedTmpl))
                    pcflDest->Delete(kctgTmpl, cnoOwnedTmpl);
                continue;
            }
#if defined(KAUAI_WIN32)
            int32_t cTexture = 0;
            if (!F4DMMVxp2EmbedTextures(this, pcflDest, cnoOwnedTmpl, pszVxp2TextureBase, &cTexture))
            {
                MultiLog(this, "vxp2_import FAIL stage=embed_textures source=%ld owned=%ld name=%s",
                         (long)cki.cno, (long)cnoOwnedTmpl, stnName.Psz());
                if (pcflDest->FFind(kctgTmpl, cnoOwnedTmpl))
                    pcflDest->Delete(kctgTmpl, cnoOwnedTmpl);
                continue;
            }
            MultiLog(this, "vxp2_import textures_ready owned=%ld count=%ld type=%s name=%s",
                     (long)cnoOwnedTmpl, (long)cTexture, fPropThis ? "prop" : "actor", stnName.Psz());
#else
            if (pcflDest->FFind(kctgTmpl, cnoOwnedTmpl))
                pcflDest->Delete(kctgTmpl, cnoOwnedTmpl);
            continue;
#endif
        }
        else if (!F4DMMSetImportedTemplatePropFlag(pcflDest, cnoOwnedTmpl, fPropThis))
        {
            MultiLog(this, "vxp_import FAIL stage=set_type source=%ld owned=%ld name=%s",
                     (long)cki.cno, (long)cnoOwnedTmpl, stnName.Psz());
            if (pcflDest->FFind(kctgTmpl, cnoOwnedTmpl))
                pcflDest->Delete(kctgTmpl, cnoOwnedTmpl);
            continue;
        }
        _pcrfAutoSave->FSetCrep(crepToss, kctgTmpl, cnoOwnedTmpl, TMPL::FReadTmpl);

        TAG tag;
        tag.sid = ksidUseCrf;
        tag.ctg = kctgTmpl;
        tag.cno = cnoOwnedTmpl;
        if (!TAGM::FOpenTag(&tag, _pcrfAutoSave))
        {
            MultiLog(this, "vxp_import FAIL stage=open_owned_tag owned=%ld name=%s",
                     (long)cnoOwnedTmpl, stnName.Psz());
            if (pcflDest->FFind(kctgTmpl, cnoOwnedTmpl))
                pcflDest->Delete(kctgTmpl, cnoOwnedTmpl);
            continue;
        }

        PACTR pactr = ACTR::PactrNew(&tag);
        TAGM::CloseTag(&tag);
        if (pactr == pvNil)
        {
            MultiLog(this, "vxp_import FAIL stage=create_actor owned=%ld name=%s",
                     (long)cnoOwnedTmpl, stnName.Psz());
            if (pcflDest->FFind(kctgTmpl, cnoOwnedTmpl))
                pcflDest->Delete(kctgTmpl, cnoOwnedTmpl);
            continue;
        }

        // ACTR::PactrNew validates the TMPL/action metadata, but its BODY is
        // deliberately deferred until the actor enters a scene. Verify that
        // deferred boundary now, while the import is still transactional, so
        // a missing costume/model dependency cannot become a dead roll-call
        // entry that later crashes COST::FGet during placement.
        PBODY pbodyVerify = pactr->Ptmpl()->PbodyCreate();
        if (pbodyVerify == pvNil)
        {
            MultiLog(this, "vxp_import FAIL stage=verify_body owned=%ld name=%s",
                     (long)cnoOwnedTmpl, stnName.Psz());
            ReleasePpo(&pactr);
            if (pcflDest->FFind(kctgTmpl, cnoOwnedTmpl))
                pcflDest->Delete(kctgTmpl, cnoOwnedTmpl);
            continue;
        }
        MultiLog(this, "vxp_import verify_body OK owned=%ld parts=%ld actions=%ld name=%s",
                 (long)cnoOwnedTmpl, (long)pbodyVerify->Cpart(),
                 (long)pactr->Ptmpl()->Cactn(), stnName.Psz());
        ReleasePpo(&pbodyVerify);

        const bool fAdded = FAddToRollCall(pactr, &stnName);
        const int32_t aridImported = pactr->Arid();
        bool fMarkedVxp = fFalse;
        if (fAdded)
        {
            for (int32_t imactr = 0; imactr < _pgstmactr->IvMac(); ++imactr)
            {
                MACTR mactr;
                _pgstmactr->GetExtra(imactr, &mactr);
                if (mactr.arid != aridImported)
                    continue;
                mactr.grfbrws |= fbrwsVxp;
                _pgstmactr->PutExtra(imactr, &mactr);
                fMarkedVxp = fTrue;
                break;
            }
        }
        if (!fAdded || !fMarkedVxp)
        {
            MultiLog(this, "vxp_import FAIL stage=%s owned=%ld name=%s",
                     !fAdded ? "rollcall" : "mark_vxp", (long)cnoOwnedTmpl, stnName.Psz());
            if (fAdded)
                RemFromRollCall(pactr, fTrue);
            ReleasePpo(&pactr);
            if (pcflDest->FFind(kctgTmpl, cnoOwnedTmpl))
                pcflDest->Delete(kctgTmpl, cnoOwnedTmpl);
            continue;
        }
        ReleasePpo(&pactr);

        ++cImported;
        MultiLog(this, "vxp_import tmpl_ok source=%ld owned=%ld arid=%ld type=%s vxp_mark=1 name=%s",
                 (long)cki.cno, (long)cnoOwnedTmpl, (long)aridImported,
                 fPropThis ? "prop" : "actor", stnName.Psz());
    }

    ReleasePpo(&pcrfSource);
    if (pcImported != pvNil)
        *pcImported = cImported;
    if (cImported <= 0)
    {
        MultiLog(this, "vxp_import FAIL stage=no_templates path=%s", stnPath.Psz());
        return fFalse;
    }

    _fRequiresVmmSave = fTrue;
    _fAutosaveDirty = fTrue;
    SetDirty();
    if (_pbwld != pvNil)
        _pbwld->MarkDirty();
    InvalViewsAndScb();
    MultiLog(this, "%s chunky_ok path=%s count=%ld%s",
             fVxp2 ? "vxp2_import" : "vxp_import", stnPath.Psz(), (long)cImported,
             fVxp2 ? "" : (fProp ? " as=prop" : " as=actor"));
    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * This adds an actor to the roll call, if not already there, or
 * adds a reference count if it already exists.
 *
 * Parameters:
 *   Pointer to the actor to add.
 *
 * Returns:
 *   fTrue if successful, else fFalse indicating out of resources.
 *
 ****************************************************/
bool MVIE::FAddToRollCall(ACTR *pactr, PSTN pstn)
{
    AssertThis(0);
    AssertPo(pactr, 0);
    AssertNilOrPo(pstn, 0); // 3DMMv1.0: can be pvNil if the actor is already in the movie.

    MACTR mactr;
    int32_t imactr;

    if (pactr->Arid() != aridNil)
    {
        //
        // 3DMMv1.0: Search for an actor with the same arid
        //
        for (imactr = 0; imactr < _pgstmactr->IvMac(); imactr++)
        {

            _pgstmactr->GetExtra(imactr, &mactr);
            if (mactr.arid == pactr->Arid())
            {
                TAG tagTmpl;
                mactr.cactRef++;
                // 3DMMv1.0: TDTs sometimes need to update tagTmpl
                pactr->GetTagTmpl(&tagTmpl);
                if (fcmpEq != TAGM::FcmpCompareTags(&mactr.tagTmpl, &tagTmpl))
                {
                    TAGM::CloseTag(&mactr.tagTmpl);
                    mactr.tagTmpl = tagTmpl;
                    // 3DMMv1.0: ACTR::GetTagTmpl doesn't AddRef the pcrf, so do it here:
                    TAGM::DupTag(&tagTmpl);
                }
                _pgstmactr->PutExtra(imactr, &mactr);
                Pmcc()->UpdateRollCall();
#if defined(KAUAI_WIN32)
                // The legacy Add Actor path mutates the movie roll call without
                // changing browser focus/selection.  Push that authoritative
                // change to any open external Actor/Object browsers now rather
                // than waiting for an unrelated selection event.
                SyncExternalContentBrowserSelection();
#endif
                return (fTrue);
            }
        }
    }
    else
    {
        pactr->SetArid(_aridLim++);
    }

    AssertPo(pstn, 0);

    //
    // 3DMMv1.0: This is a new actor, add it to the roll call
    //
    mactr.arid = pactr->Arid();
    mactr.grfbrws = fbrwsNil;
    if (pactr->FIsPropBrws())
    {
        mactr.grfbrws |= fbrwsProp;
        if (pactr->FIsTdt())
        {
            mactr.grfbrws |= fbrwsTdt;
        }
    }
    mactr.cactRef = 1;
    pactr->GetTagTmpl(&mactr.tagTmpl);
    // 3DMMv1.0: Open the tag, since it might be a TDT
    AssertDo(vptagm->FOpenTag(&mactr.tagTmpl, _pcrfAutoSave), "Should never fail when not copying the tag");
    if (_pgstmactr->FAddStn(pstn, &mactr))
    {
        Pmcc()->UpdateRollCall();
#if defined(KAUAI_WIN32)
        SyncExternalContentBrowserSelection();
#endif
        return (fTrue);
    }

    return (fFalse);
}

/****************************************************
 * Expose an Actor-Studio-created actor in the ordinary movie roll call
 * while it is still floating in the normal placement tool. StartPlaceActor
 * intentionally removes a new actor from the roll call until placement; AS
 * wants the hired actor visible in the actor list immediately. The marker
 * lets the placement commit consume that already-added reference instead of
 * incrementing the roll-call count a second time.
 ****************************************************/
bool MVIE::F4DMMActorStudioExposeCurrentPlacementInRollCall(void)
{
    AssertThis(0);
    PACTR pactr = Pscen() != pvNil ? Pscen()->PactrSelected() : pvNil;
    if (pactr == pvNil || pactr->Arid() == aridNil)
        return fFalse;
    if (!FAddToRollCall(pactr, pvNil))
        return fFalse;
    _arid4DMMActorStudioPreaddedPlacement = pactr->Arid();
    MultiLog(this, "actor_studio_use_object rollcall_exposed arid=%ld",
             (long)_arid4DMMActorStudioPreaddedPlacement);
    return fTrue;
}

bool MVIE::F4DMMActorStudioConsumePreaddedPlacement(PACTR pactr)
{
    AssertThis(0);
    if (pactr == pvNil || _arid4DMMActorStudioPreaddedPlacement == aridNil ||
        pactr->Arid() != _arid4DMMActorStudioPreaddedPlacement)
        return fFalse;
    MultiLog(this, "actor_studio_use_object rollcall_commit_reuse arid=%ld",
             (long)_arid4DMMActorStudioPreaddedPlacement);
    _arid4DMMActorStudioPreaddedPlacement = aridNil;
    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * This removes an actor from the roll call, if the reference count
 * drops to zero, else it decrements the reference count.
 *
 * Parameters:
 *   Pointer to the actor to remove.
 *
 * Returns:
 *   None.
 *
 ****************************************************/
void MVIE::RemFromRollCall(ACTR *pactr, bool fDelIfOnlyRef)
{
    AssertThis(0);
    AssertPo(pactr, 0);

    MACTR mactr;
    int32_t imactr;

    //
    // 3DMMv1.0: Search for the actor in the roll call
    //
    for (imactr = 0; imactr < _pgstmactr->IvMac(); imactr++)
    {
        _pgstmactr->GetExtra(imactr, &mactr);
        if (mactr.arid == pactr->Arid())
        {
            mactr.cactRef--;
            Assert(mactr.cactRef >= 0, "Too many removes");
            if (fDelIfOnlyRef && mactr.cactRef == 0)
            {
                vptagm->CloseTag(&mactr.tagTmpl);
                _pgstmactr->Delete(imactr);
            }
            else
            {
                _pgstmactr->PutExtra(imactr, &mactr);
            }

            if (mactr.cactRef == 0)
            {
                Pmcc()->UpdateRollCall();
#if defined(KAUAI_WIN32)
                SyncExternalContentBrowserSelection();
#endif
            }
            return;
        }
    }

    Bug("Tried to remove an invalid actor");
}

/** 3DMMv1.0: **************************************************
 *
 * Changes the scene in the movie currently being referenced.
 *
 * Parameters:
 *	iscen - Scene number to go to.
 *
 * Returns:
 *
 *  fTrue, if successful, else fFalse, indicating the
 *  same scene is still open (if possible).
 *
 ****************************************************/
bool MVIE::FSwitchScen(int32_t iscen)
{
    AssertThis(0);
    Assert(iscen == ivNil || FIn(iscen, 0, Cscen()), "iscen out of range");
    Assert((iscen == ivNil) || (_pcrfAutoSave != pvNil), "Invalid save file");

    PSCEN pscen;
    KID kid;
    int32_t iscenOld;
    bool fRet = fTrue;

    if (iscen == _iscen)
    {
        return (fTrue);
    }

    //
    // 3DMMv1.0: Close the current scene.
    //
    iscenOld = _iscen;
    if (!_FCloseCurrentScene())
    {
        return (fFalse);
    }

    //
    // 3DMMv1.0: Stop all looping non-midi sounds
    //
    vpsndm->StopAll(sqnNil, sclLoopWav);

    //
    // 3DMMv1.0: If memory is low, release unreferenced content BACOs to reduce thrashing.
    // 3DMMv1.0: Note that APP::MemStat() is unavailable from the movie engine
    //
#ifdef WIN
    MEMORYSTATUS ms;
    ms.dwLength = SIZEOF(MEMORYSTATUS);
    GlobalMemoryStatus(&ms);
    if (ms.dwMemoryLoad == 100)
    {
        // 3DMMv1.0: No physical RAM to spare, so free some stuff
        vptagm->ClearCache(sidNil, ftagmMemory);
    }

#endif // 3DMMv1.0: WIN

    if (iscen == ivNil)
    {
        Assert(_pscenOpen == pvNil, "_FCloseCurrentScene didn't clear this");
        Assert(_iscen == ivNil, "_FCloseCurrentScene didn't clear this");
        return (fTrue);
    }

LRetry:

    // Background light setup occurs while the scene is being read, so publish
    // this target scene's saved lighting policy before BKGD::TurnOnLights runs.
    vfSceneDynamicLightingActive = FSceneLightsEnabled(iscen);
    vfSceneDefaultLightingShadersActive = FSceneDefaultLightingShaders(iscen);
    vfSceneLightLabCombineLegacyActive = FSceneLightLabCombineLegacy(iscen);
    Refresh4DMMMappedMaterialLightingPolicy();

    //
    // 3DMMv1.0: Get info for next scene and read it in.
    //
    AssertDo(_pcrfAutoSave->Pcfl()->FGetKidChidCtg(kctgMvie, _cno, iscen, kctgScen, &kid), "Should never fail");

    pscen = SCEN::PscenRead(this, _pcrfAutoSave, kid.cki.cno);

    if ((pscen == pvNil) || !pscen->FPlayStartEvents())
    {
        _pscenOpen = pvNil;
        _iscen = ivNil;

        if (pscen != pvNil)
        {
            SCEN::Close(&pscen);
        }

        if (iscenOld != ivNil)
        {
            iscen = iscenOld;
            iscenOld = ivNil;
            fRet = fFalse;
            goto LRetry;
        }

        _pmcc->SceneChange();
        return (fFalse);
    }

    _pscenOpen = pscen;
    _pmcc->SceneChange();
    _iscen = iscen;

    // Do not install persistent Light Lab actors until FGotoFrm has finished
    // materializing every actor BODY for the first frame.  Installing them
    // here used to make whichever bodies happened to exist first light up,
    // while later saved lights stayed dormant until the first viewport click
    // triggered another attachment/update pass.
    if (!pscen->FGotoFrm(pscen->NfrmFirst()))
    {

        _pscenOpen = pvNil;
        _iscen = ivNil;
        SCEN::Close(&pscen);

        if (iscenOld != ivNil)
        {
            iscen = iscenOld;
            iscenOld = ivNil;
            fRet = fFalse;
            goto LRetry;
        }

        return (fFalse);
    }

    // All first-frame BODY roots now exist. Recreate runtime Object Group
    // parents from persisted bind-space metadata before the first render.
    _RebuildObjectGroupRenderParentsForCurrentScene();

    // Object Properties are ordinary scene-local render policy and are not
    // conditional on Light Lab being enabled. Apply their BODY-root flags as
    // soon as the scene has materialized all first-frame bodies.
    UpdateObjectShadowProperties();

    // All first-frame actor bodies now exist. Remove anonymous/orphaned light
    // records and install the complete valid scene light set before the first
    // viewport click.
    _RemoveInvalidLightLabForCurrentScene();
    if (FTestLightMode())
    {
        RefreshTestLight();
        UpdateTestLightAttachment();
    }

    pscen->UpdateSndFrame();
    InvalViewsAndScb();

    return (fRet);
}

/** 3DMMv1.0: **************************************************
 *
 * Creates a new scene and inserts it as scene number iscen.
 *
 * Parameters:
 *	iscen - Scene number to insert the new scene as.
 *
 * Returns:
 *  fTrue, if successful, else fFalse.
 *
 ****************************************************/
bool MVIE::FNewScenInsCore(int32_t iscen)
{
    AssertThis(0);
    AssertIn(iscen, 0, Cscen() + 1);

    PSCEN pscen;

    //
    // 3DMMv1.0: Create the new scene.
    //
    pscen = SCEN::PscenNew(this);
    if (pscen == pvNil)
    {
        return (fFalse);
    }

    if (!FInsScenCore(iscen, pscen))
    {
        SCEN::Close(&pscen);
        return (fFalse);
    }

    SCEN::Close(&pscen);
    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * Moves up/down all the chids by one.
 *
 * Parameters:
 *	chid - chid number to start at.
 *	fDown- fTrue if move down, else move up.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void MVIE::_MoveChids(CHID chid, bool fDown)
{
    AssertThis(0);

    PCFL pcfl = _pcrfAutoSave->Pcfl();
    KID kid;
    CHID chidTmp;

    if (fDown)
    {
        //
        // 3DMMv1.0: Move down chids of all old scenes (increase by one)
        //
        for (chidTmp = _cscen; chidTmp > chid;)
        {
            chidTmp--;
            AssertDo(pcfl->FGetKidChidCtg(kctgMvie, _cno, chidTmp, kctgScen, &kid), "Should never fail");

            pcfl->ChangeChid(kctgMvie, _cno, kctgScen, kid.cki.cno, chidTmp, chidTmp + 1);
        }
    }
    else
    {
        //
        // 3DMMv1.0: Move up chids of all old scenes (decrease by one)
        //
        for (chidTmp = chid; chidTmp < (CHID)_cscen; chidTmp++)
        {
            AssertDo(pcfl->FGetKidChidCtg(kctgMvie, _cno, chidTmp + 1, kctgScen, &kid), "Should never fail");

            pcfl->ChangeChid(kctgMvie, _cno, kctgScen, kid.cki.cno, chidTmp + 1, chidTmp);
        }
    }
}

/** 3DMMv1.0: ****************************************************************************
    _FIsChild
        Enumerates the children of the MVIE chunk and reports whether the
        given (ctg, cno) chunk is an actual child of the MVIE chunk.

    Arguments:
        PCFL pcfl  --  the file on which to check
        CTG ctg    --  these are self-explanatory
        CNO cno

    Returns:  fTrue if the (ctg, cno) chunk is an immediate child of the MVIE

************************************************************ PETED ***********/
bool MVIE::_FIsChild(PCFL pcfl, CTG ctg, CNO cno)
{
    bool fIsChild = fFalse;
    int32_t ckid, ikid;
    KID kid;

    ckid = pcfl->Ckid(kctgMvie, _cno);
    for (ikid = 0; ikid < ckid; ikid++)
    {
        if (!pcfl->FGetKid(kctgMvie, _cno, ikid, &kid))
        {
            Bug("CFL returned bogus ckid");
            break;
        }

        if (kid.cki.ctg == ctg && kid.cki.cno == cno)
        {
            fIsChild = fTrue;
            break;
        }
    }

    return fIsChild;
}

/** 3DMMv1.0: **************************************************
 *
 * Adopt the scene user sounds as children of the movie.
 * Msnds are children of the movie unless they are
 * unused and deleted when the movie closes.
 *
 * Parameters
 * pcfl
 * cnoScen
 *
 *	Returns:
 * success or failure
 *
 ****************************************************/
bool MVIE::_FAdoptMsndInMvie(PCFL pcfl, CNO cnoScen)
{
    AssertThis(0);
    AssertPo(pcfl, 0);

    CHID chidMvie;
    int32_t ckid, ikid;
    KID kid;

    ckid = pcfl->Ckid(kctgScen, cnoScen);
    for (ikid = 0; ikid < ckid; ikid++)
    {
        if (!pcfl->FGetKid(kctgScen, cnoScen, ikid, &kid))
        {
            Bug("CFL returned bogus ckid");
            break;
        }

        if (kid.cki.ctg == kctgMsnd)
        {
            if (!_FIsChild(pcfl, kctgMsnd, kid.cki.cno))
            {
                // 3DMMv1.0: Adopt as a child of the movie
                chidMvie = _ChidMvieNewSnd();
                if (!pcfl->FAdoptChild(kctgMvie, _cno, kctgMsnd, kid.cki.cno, chidMvie))
                {
                    goto LFail;
                }
            }
        }
    }

    return fTrue;
LFail:
    return fFalse;
}

/** 3DMMv1.0: **************************************************
 *
 * Resolves a sound tag & chid to be a current tag
 *
 * Note: Msnds are children of the current scene.
 * Due to sound import, the cno can change.
 *
 * Parameters
 * ptag (with *ptag.cno possibly out of date)
 * chid (valid only for user snd)
 * cnoScen (if cnoNil, use current scene's cno
 *
 *	Returns:
 * Updated *ptag
 *
 ****************************************************/
bool MVIE::FResolveSndTag(PTAG ptag, CHID chid, CNO cnoScen, PCRF pcrf)
{
    AssertThis(0);
    AssertVarMem(ptag);
    AssertNilOrVarMem(pcrf);

    KID kidScen;
    KID kid;
    TAG tagNew = *ptag;
    PCFL pcfl;

    if (pvNil == pcrf)
        pcrf = _pcrfAutoSave;
    pcfl = pcrf->Pcfl();

    if (ptag->sid != ksidUseCrf)
        return fTrue;
    if (cnoNil == cnoScen)
    {
        if (!pcfl->FGetKidChidCtg(kctgMvie, _cno, _iscen, kctgScen, &kidScen))
            return fFalse;
        cnoScen = kidScen.cki.cno;
    }

    if (!pcfl->FGetKidChidCtg(kctgScen, cnoScen, chid, kctgMsnd, &kid))
        return fFalse;

    if (ptag->cno != kid.cki.cno)
    {
        // 3DMMv1.0: As the pcrf has not changed, it is not essential
        // 3DMMv1.0: to close & open the respective tags.
        tagNew.cno = kid.cki.cno;
        if (!TAGM::FOpenTag(&tagNew, pcrf))
            return fFalse;
        TAGM::CloseTag(ptag);
        *ptag = tagNew;
    }
    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * Finds the chid (child of the current scene) for a
 * given sound tag.  Adopt it if not found.
 * Meaningful for user sounds only
 *
 * Parameters
 * cno
 *
 *	Returns:
 * Updated *pchid
 *
 ****************************************************/
bool MVIE::FChidFromUserSndCno(CNO cno, CHID *pchid)
{
    AssertThis(0);
    AssertVarMem(pchid);

    KID kidScen;
    KID kid;
    int32_t ckid;
    int32_t ikid;
    PCFL pcfl = _pcrfAutoSave->Pcfl();

    if (!pcfl->FGetKidChidCtg(kctgMvie, _cno, _iscen, kctgScen, &kidScen))
        return fFalse;
    ckid = pcfl->Ckid(kctgScen, kidScen.cki.cno);
    for (ikid = 0; ikid < ckid; ikid++)
    {
        if (!pcfl->FGetKid(kctgScen, kidScen.cki.cno, ikid, &kid))
            return fFalse;
        if (kid.cki.ctg != kctgMsnd)
            continue;
        if (kid.cki.cno != cno)
            continue;
        *pchid = kid.chid;
        return fTrue;
    }

    *pchid = _ChidScenNewSnd();
    if (!pcfl->FAdoptChild(kctgScen, kidScen.cki.cno, kctgMsnd, cno, *pchid))
        return fFalse;
    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * Copies a sound file to the movie. (Importing snd)
 * Sounds are written as MSND children of the current
 * scene chunk
 *
 * Parameters:
 *	pfilSrc, sty
 *
 * Returns:
 *  *pcno = cno of chunk written
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
bool MVIE::FCopySndFileToMvie(PFIL pfilSrc, int32_t sty, CNO *pcno, PSTN pstn)
{
    AssertThis(0);
    AssertVarMem(pfilSrc);
    AssertVarMem(pcno);
    AssertNilOrPo(pstn, 0);
    Assert(_pcrfAutoSave != pvNil, "Bad working file.");

    PCFL pcfl;
    FNI fniSrc;
    CHID chid;
    KID kidScen;

    pcfl = _pcrfAutoSave->Pcfl();

    //
    // 3DMMv1.0: Ensure we will be writing to the temp file
    //
    if (!_FUseTempFile())
    {
        return fFalse;
    }

    pfilSrc->GetFni(&fniSrc);
    if (fniSrc.Ftg() == kftgMidi)
    {
        if (!MSND::FCopyMidi(pfilSrc, pcfl, pcno, pstn))
            goto LFail;
    }
    else
    {
        if (!MSND::FCopyWave(pfilSrc, pcfl, sty, pcno, pstn))
            goto LFail;
    }

    AssertDo(pcfl->FGetKidChidCtg(kctgMvie, _cno, _iscen, kctgScen, &kidScen), "Scene chunk doesn't exist!");

    chid = _ChidMvieNewSnd();
    if (!pcfl->FAdoptChild(kctgMvie, _cno, kctgMsnd, *pcno, chid))
    {
        pcfl->Delete(kctgMsnd, *pcno);
        return fFalse;
    }
    if (!pcfl->FSave(kctgSoc))
        return fFalse;

    _fGCSndsOnClose = fTrue;

    return fTrue;
LFail:
    if (!vpers->FIn(ercSocBadSoundFile))
        PushErc(ercSocCantCopyMsnd);
    return fFalse;
}

/** 3DMMv1.0: **************************************************
 *
 * Copy Msnd chunk from specified movie *pcfl to
 * current movie
 * Parameters:
 *	pcfl : Source file
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *	*pcnoDest
 *
 ****************************************************/
bool MVIE::FCopyMsndFromPcfl(PCFL pcflSrc, CNO cnoSrc, CNO *pcnoDest)
{
    AssertBaseThis(0);
    AssertPo(pcflSrc, 0);
    AssertVarMem(pcnoDest);

    PCFL pcflDest;
    KID kidScen;
    CHID chid;
    FNI fni;

    if (!FEnsureAutosave())
        return fFalse;
    pcflDest = _pcrfAutoSave->Pcfl();
    pcflSrc->GetFni(&fni);

    // 3DMMv1.0: Copy the msnd chunk from one movie to another
    // 3DMMv1.0: Wave or Midi
    if (!pcflSrc->FCopy(kctgMsnd, cnoSrc, pcflDest, pcnoDest))
        return fFalse;

    AssertDo(pcflDest->FGetKidChidCtg(kctgMvie, _cno, _iscen, kctgScen, &kidScen), "Scene chunk doesn't exist!");

    chid = _ChidMvieNewSnd(); // 3DMMv1.0: Find a unique chid for the new sound
    if (!pcflDest->FAdoptChild(kctgMvie, _cno, kctgMsnd, *pcnoDest, chid))
    {
        pcflDest->Delete(kctgMsnd, *pcnoDest);
        return fFalse;
    }
    if (!pcflDest->FSave(kctgSoc))
        return fFalse;
    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * Choose Scene Chid for New Sound
 * Note: _pcrfAutoSave is expected to be current
 * Parameters:
 *	none
 *
 * Returns:
 *  unique chid for new msnd chunk child of scene
 *
 ****************************************************/
CHID MVIE::_ChidScenNewSnd(void)
{
    AssertBaseThis(0);
    PCFL pcfl = _pcrfAutoSave->Pcfl();
    int32_t ckid;
    int32_t chid;
    KID kidScen;
    KID kid;

    if (!pcfl->FGetKidChidCtg(kctgMvie, _cno, _iscen, kctgScen, &kidScen))
        return fFalse;

    ckid = pcfl->Ckid(kctgScen, kidScen.cki.cno);
    for (chid = 0; chid < ckid; chid++)
    {
        if (!pcfl->FGetKidChidCtg(kctgScen, kidScen.cki.cno, chid, kctgMsnd, &kid))
            return (CHID)chid;
    }
    return (CHID)chid;
}

/** 3DMMv1.0: **************************************************
 *
 * Choose Mvie Chid for New Sound
 * Note: _pcrfAutoSave is expected to be current
 * Parameters:
 *	none
 *
 * Returns:
 *  unique chid for new msnd chunk child of scene
 *
 ****************************************************/
CHID MVIE::_ChidMvieNewSnd(void)
{
    AssertBaseThis(0);
    PCFL pcfl = _pcrfAutoSave->Pcfl();
    int32_t ckid;
    int32_t chid;
    KID kid;

    ckid = pcfl->Ckid(kctgMvie, _cno);
    for (chid = 0; chid < ckid; chid++)
    {
        if (!pcfl->FGetKidChidCtg(kctgMvie, _cno, chid, kctgMsnd, &kid))
            return (CHID)chid;
    }
    return (CHID)chid;
}

/** 3DMMv1.0: **************************************************
 *
 * Verify the version number of a file
 *
 * Parameters:
 *	pfni
 *	*pcno == cnoNil if using the first chunk in the file
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *  *pcno updated
 *
 ****************************************************/
bool MVIE::FVerifyVersion(PCFL pcfl, CNO *pcno)
{
    AssertBaseThis(0); // 3DMMv1.0: MVIE hasn't been loaded yet
    AssertPo(pcfl, 0);

    KID kid;
    CNO cnoMvie;
    MFP mfp;
    BLCK blck;

    // 3DMMv1.0: Get the cno of the first kid of the movie
    if (pvNil == pcno || cnoNil == *pcno)
    {
        if (!pcfl->FGetCkiCtg(kctgMvie, 0, &(kid.cki)))
        {
            PushErc(ercSocBadFile);
            return fFalse;
        }
        cnoMvie = kid.cki.cno;
        if (pvNil != pcno)
            *pcno = cnoMvie;
    }

    // 3DMMv1.0: Get version number of the file
    if (!pcfl->FFind(kctgMvie, cnoMvie, &blck) || !blck.FUnpackData() || (blck.Cb() != SIZEOF(MFP)) ||
        !blck.FReadRgb(&mfp, SIZEOF(MFP), 0))
    {
        PushErc(ercSocBadFile);
        return fFalse;
    }

    if (mfp.bo == kboOther)
    {
        SwapBytesBom(&mfp, kbomMfp);
    }

    // 3DMMv1.0: Check the version numbers
    if (!mfp.dver.FReadable(kcvnCur, kcvnMin))
    {
        PushErc(ercSocBadVersion);
        return fFalse;
    }
    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * Removes a scene from the movie.
 *
 * Parameters:
 *	iscen - Scene number to insert the new scene as.
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
bool MVIE::FRemScenCore(int32_t iscen)
{
    AssertThis(0);
    AssertIn(iscen, 0, Cscen());
    Assert(_pcrfAutoSave != pvNil, "Bad working file.");

    KID kid;
    PCFL pcfl;
    PSCEN pscen;
    int32_t iscenOld;

    pcfl = _pcrfAutoSave->Pcfl();

    //
    // 3DMMv1.0: Ensure we are using the temp file
    //
    if (!_FUseTempFile())
    {
        return (fFalse);
    }

    //
    // 3DMMv1.0: Close this scene if it is open and different.
    //
    iscenOld = _iscen;
    if (_iscen != iscen)
    {

        //
        // 3DMMv1.0: Get the scen to remove.
        //
        if (!FSwitchScen(iscen))
        {
            FSwitchScen(iscenOld);
            return (fFalse);
        }
    }

    //
    // 3DMMv1.0: Keep the scene in memory for a second.
    //
    pscen = Pscen();
    pscen->AddRef();

    //
    // 3DMMv1.0: Close the current scene
    //
    SCEN::Close(&_pscenOpen);
    _iscen = ivNil;

    //
    // 3DMMv1.0: Remove its actors from the roll call
    //
    pscen->RemActrsFromRollCall();
    ReleasePpo(&pscen);

    //
    // 3DMMv1.0: Remove the scene chunk.
    //
    AssertDo(pcfl->FGetKidChidCtg(kctgMvie, _cno, iscen, kctgScen, &kid), "Should never fail");
    pcfl->DeleteChild(kctgMvie, _cno, kctgScen, kid.cki.cno, iscen);

    //
    // 3DMMv1.0: Move up chids of all old scenes.
    //
    const int32_t cscenOld = _cscen;
    if (iscen < kc4DMMSceneSettingsMax)
    {
        const int32_t iscenLast = LwMin(cscenOld - 1, kc4DMMSceneSettingsMax - 1);
        for (int32_t i = iscen; i < iscenLast; ++i)
        {
            _rgfSceneDefaultLightingShaders[i] = _rgfSceneDefaultLightingShaders[i + 1];
            _rgfSceneLightLabCombineLegacy[i] = _rgfSceneLightLabCombineLegacy[i + 1];
        }
        if (iscenLast >= 0)
        {
            _rgfSceneDefaultLightingShaders[iscenLast] = _fDefaultLightingShaders;
            _rgfSceneLightLabCombineLegacy[iscenLast] = _fLightLabCombineLegacy;
        }
    }
    _cscen--;
    _MoveChids((CHID)iscen, fFalse);

    // Drop groups owned by the removed scene and move later groups down with
    // the native scene indices. Member rows are compacted at the same time.
    int32_t rgidRemoved[kcObjectGroupMax];
    int32_t cidRemoved = 0;
    int32_t iGroupDst = 0;
    for (int32_t iGroup = 0; iGroup < _cObjectGroup; iGroup++)
    {
        OBJECTGROUP group = _rgObjectGroup[iGroup];
        if (group.iscen == iscen)
        {
            if (cidRemoved < kcObjectGroupMax)
                rgidRemoved[cidRemoved++] = group.id;
            continue;
        }
        if (group.iscen > iscen)
            group.iscen--;
        _rgObjectGroup[iGroupDst++] = group;
    }
    _cObjectGroup = iGroupDst;
    int32_t iMemberDst = 0;
    for (int32_t iMember = 0; iMember < _cObjectGroupMember; iMember++)
    {
        bool fRemove = fFalse;
        for (int32_t i = 0; i < cidRemoved; i++)
            if (_rgObjectGroupMember[iMember].idGroup == rgidRemoved[i]) { fRemove = fTrue; break; }
        if (!fRemove)
            _rgObjectGroupMember[iMemberDst++] = _rgObjectGroupMember[iMember];
    }
    _cObjectGroupMember = iMemberDst;

    // Object Properties are scene-local just like Object Groups. Drop records
    // belonging to the removed scene and shift later scene indices down.
    int32_t iPropDst = 0;
    for (int32_t iProp = 0; iProp < _cObjectProperties; ++iProp)
    {
        OBJECTPROPERTIES prop = _rgObjectProperties[iProp];
        if (prop.iscen == iscen)
            continue;
        if (prop.iscen > iscen)
            --prop.iscen;
        _rgObjectProperties[iPropDst++] = prop;
    }
    _cObjectProperties = iPropDst;

    //
    // 3DMMv1.0: Save changes, if this fails, we don't care.  It only
    // 3DMMv1.0: matters when the user tries to truly save.
    //
    pcfl->FSave(kctgSoc);

    //
    // 3DMMv1.0: Switch to a different scene
    //
    if (Cscen() == 0)
    {
        return (fTrue);
    }

    if (iscen < Cscen())
    {
        FSwitchScen(iscen);
    }
    else
    {
        FSwitchScen(iscen - 1);
    }

    SetDirty();

    if (Pscen() == pvNil)
    {
        PushErc(ercSocSceneSwitch);
        return (fTrue);
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * Removes a scene from the movie and creates an
 * undo object for the action.  If it was the
 * currently open scene, then the scene is not open.
 * The next scene is opened.
 *
 * Parameters:
 *	iscen - Scene number to remove.
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
bool MVIE::FRemScen(int32_t iscen)
{
    AssertThis(0);
    AssertIn(iscen, 0, Cscen());

    KID kid;
    PMUNS pmuns;
    PSCEN pscen;

    if (_iscen == iscen)
    {
        pscen = Pscen();
        pscen->AddRef();
    }
    else
    {

        AssertDo(_pcrfAutoSave->Pcfl()->FGetKidChidCtg(kctgMvie, _cno, iscen, kctgScen, &kid), "Should never fail");

        pscen = SCEN::PscenRead(this, _pcrfAutoSave, kid.cki.cno);

        if ((pscen == pvNil) || !pscen->FPlayStartEvents())
        {
            SCEN::Close(&pscen);
            return (fFalse);
        }

        pscen->HideActors();
        pscen->HideTboxes();
    }

    pmuns = MUNS::PmunsNew();

    if (pmuns == pvNil)
    {
        ReleasePpo(&pscen);
        return (fTrue);
    }

    pmuns->SetIscen(iscen);
    pmuns->SetPscen(pscen);
    pmuns->SetMunst(munstRemScen);
    pmuns->SetRemovedSceneDefaultLightingShaders(FSceneDefaultLightingShaders(iscen));
    pmuns->SetRemovedSceneLightLabCombineLegacy(FSceneLightLabCombineLegacy(iscen));

    if (!FAddUndo(pmuns))
    {
        ReleasePpo(&pmuns);
        ReleasePpo(&pscen);
        return (fFalse);
    }

    ReleasePpo(&pmuns);
    ReleasePpo(&pscen);

    if (!FRemScenCore(iscen))
    {
        ClearUndo();
        return (fFalse);
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * Adds a new material to the user's document.
 *
 * Parameters:
 *	pmtrl - Pointer to the material to add.
 *  ptag - Pointer to the tag for the file.
 *
 * Returns:
 *  fTrue if success, fFalse if couldn't add the material
 *
 ****************************************************/
bool MVIE::FInsertMtrl(PMTRL pmtrl, PTAG ptag)
{
    AssertThis(0);
    AssertPo(pmtrl, 0);
    AssertVarMem(ptag);

    PCRF pcrf;
    PCFL pcfl;
    CNO cno;

    if (!FEnsureAutosave(&pcrf))
    {
        TrashVar(ptag);
        return (fFalse);
    }

    pcfl = pcrf->Pcfl();
    if (!pmtrl->FWrite(pcfl, kctgMtrl, &cno))
    {
        TrashVar(ptag);
        return fFalse;
    }

    ptag->sid = ksidUseCrf;
    ptag->ctg = kctgMtrl;
    ptag->cno = cno;

    if (!TAGM::FOpenTag(ptag, _pcrfAutoSave))
    {
        return fFalse;
    }

    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * Ensure an autosave file exists to use
 *
 * Parameters:
 *	Return the pcrf
 *
 * Returns:
 *  fTrue if success, fFalse if couldn't add the material
 *
 ****************************************************/
bool MVIE::FEnsureAutosave(PCRF *ppcrf)
{
    AssertThis(0);

    if (!_FMakeCrfValid())
    {
        return (fFalse);
    }

    if (pvNil != ppcrf)
        *ppcrf = _pcrfAutoSave;

    //
    // 3DMMv1.0: Switch to the autosave file
    //
    if (!_FUseTempFile())
    {
        return (fFalse);
    }
    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * Adds a new 3-D Text object to the user's document.
 *
 * Parameters:
 *  pstn - TDT text
 *  tdts - the TDT shape
 *  ptagTdf - a tag to the TDT's font
 *
 * Returns:
 *  fTrue if success, fFalse if couldn't add the TDT
 *
 ****************************************************/
bool MVIE::FInsTdt(PSTN pstn, int32_t tdts, PTAG ptagTdf)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    Assert(pstn->Cch() > 0, "can't insert 0-length TDT");
    AssertIn(tdts, 0, tdtsLim);
    AssertVarMem(ptagTdf);

    PCFL pcfl;
    CNO cno;
    PTDT ptdt;
    TAG tagTdt;

    ptdt = TDT::PtdtNew(pstn, tdts, ptagTdf);
    if (pvNil == ptdt)
        return fFalse;

    //
    // 3DMMv1.0: Make sure we have a file to switch to
    //
    if (!_FMakeCrfValid())
    {
        return (fFalse);
    }

    pcfl = _pcrfAutoSave->Pcfl();

    //
    // 3DMMv1.0: Switch to the autosave file
    //
    if (!_FUseTempFile())
    {
        return (fFalse);
    }

    if (!ptdt->FWrite(pcfl, kctgTmpl, &cno))
    {
        return fFalse;
    }
    ReleasePpo(&ptdt);

    tagTdt.sid = ksidUseCrf;
    tagTdt.ctg = kctgTmpl;
    tagTdt.cno = cno;

    if (!TAGM::FOpenTag(&tagTdt, _pcrfAutoSave))
    {
        return fFalse;
    }

    if (!FInsActr(&tagTdt))
        return fFalse;

    TAGM::CloseTag(&tagTdt);

    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * Edits the TDT attached to pactr.
 *
 * Parameters:
 *  pactr - Pointer to actor to change
 *  pstn - New TDT text
 *	tdts - New TDT shape
 *  ptagTdf - New TDT font
 *
 * Returns:
 *  fTrue if success, fFalse if couldn't change the TDT
 *
 ****************************************************/
bool MVIE::FChangeActrTdt(PACTR pactr, PSTN pstn, int32_t tdts, PTAG ptagTdf)
{
    AssertThis(0);
    AssertPo(pactr, 0);
    Assert(pactr->Ptmpl()->FIsTdt(), "actor must be a TDT");
    AssertPo(pstn, 0);
    AssertIn(tdts, 0, tdtsLim);
    AssertVarMem(ptagTdf);

    int32_t ich;
    bool fNonSpaceFound;
    PTDT ptdtNew;
    TAG tagTdtNew;
    PCFL pcfl;
    CNO cno;
    PACTR pactrDup;

    Assert(pactr == Pscen()->PactrSelected(), 0);
    fNonSpaceFound = fFalse;
    for (ich = 0; ich < pstn->Cch(); ich++)
    {
        if (pstn->Psz()[ich] != ChLit(' '))
        {
            fNonSpaceFound = fTrue;
            break;
        }
    }
    if (!fNonSpaceFound) // 3DMMv1.0: delete the actor
    {
        return FRemActr();
    }

    ptdtNew = TDT::PtdtNew(pstn, tdts, ptagTdf);
    if (pvNil == ptdtNew)
        return fFalse;

    //
    // 3DMMv1.0: Make sure we have a file to switch to
    //
    if (!_FMakeCrfValid())
    {
        return (fFalse);
    }

    pcfl = _pcrfAutoSave->Pcfl();

    //
    // 3DMMv1.0: Switch to the autosave file
    //
    if (!_FUseTempFile())
    {
        return (fFalse);
    }

    if (!ptdtNew->FWrite(pcfl, kctgTmpl, &cno))
    {
        return fFalse;
    }
    ReleasePpo(&ptdtNew);

    tagTdtNew.sid = ksidUseCrf;
    tagTdtNew.ctg = kctgTmpl;
    tagTdtNew.cno = cno;

    if (!TAGM::FOpenTag(&tagTdtNew, _pcrfAutoSave))
    {
        return fFalse;
    }

    if (!pactr->FDup(&pactrDup))
    {
        TAGM::CloseTag(&tagTdtNew);
        return fFalse;
    }

    if (!pactr->FChangeTagTmpl(&tagTdtNew))
    {
        TAGM::CloseTag(&tagTdtNew);
        ReleasePpo(&pactrDup);
        return fFalse;
    }

    // 3DMMv1.0: this sequence is a little strange to make unwinding easier:
    // 3DMMv1.0: we add the actor to the roll call with aridNil to get a new
    // 3DMMv1.0: entry, then remove the old entry (using pactrDup).
    pactr->SetArid(aridNil);
    if (!FAddToRollCall(pactr, pstn))
    {
        pactr->Restore(pactrDup);
        ReleasePpo(&pactrDup);
        TAGM::CloseTag(&tagTdtNew);
        return fFalse;
    }
    RemFromRollCall(pactrDup);
    ReleasePpo(&pactrDup);

    TAGM::CloseTag(&tagTdtNew);
    SetDirty();

    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * Closes and releases the current scene, if any
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if success, fFalse if couldn't autosave
 *
 ****************************************************/
bool MVIE::_FCloseCurrentScene(void)
{
    AssertThis(0);

    if (_fFreeLookMode || _fFreeLookOverride)
        _ClearFreeLookState();

    if (Pscen() != pvNil)
    {
        _ShowHiddenLightObjects();

        if (!FAutoSave(pvNil, fFalse)) // 3DMMv1.0: could not save...keep scene open
        {
            return fFalse;
        }

        _DestroyObjectGroupRenderParents();
        SCEN::Close(&_pscenOpen);
        _iscen = ivNil;
        vfSceneDynamicLightingActive = fFalse;
        vfSceneDefaultLightingShadersActive = fTrue;
        vfSceneLightLabCombineLegacyActive = fFalse;
    }
    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * Makes sure that the current file in use is a temp file.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if success, fFalse if couldn't switch
 *
 ****************************************************/
bool MVIE::_FUseTempFile(void)
{
    AssertThis(0);

    PCFL pcfl;
    KID kid;
    FNI fni;

    pcfl = _pcrfAutoSave->Pcfl();

    //
    // 3DMMv1.0: Make sure we are using the temporary file
    //
    if (!pcfl->FTemp())
    {

        if (!fni.FGetTemp())
        {
            return (fFalse);
        }

        if (!pcfl->FSave(kctgSoc, &fni))
        {
            return (fFalse);
        }

        // 3DMMv1.0: Set the Temp flag
        AssertDo(pcfl->FSetGrfcfl(fcflTemp, fcflTemp), 0);

        //
        // 3DMMv1.0: Update _cno
        //
        AssertDo(pcfl->FGetCkiCtg(kctgMvie, 0, &(kid.cki)), "Should never fail");
        _cno = kid.cki.cno;
    }

    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * Makes sure that there is a file to work with.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if success, fFalse if couldn't switch
 *
 ****************************************************/
bool MVIE::_FMakeCrfValid(void)
{
    AssertThis(0);

    PCFL pcfl;
    FNI fni;

    if (_pcrfAutoSave != pvNil)
    {
        return (fTrue);
    }

    //
    // 3DMMv1.0: Get a temp file
    //
    if (!fni.FGetTemp())
    {
        return (fFalse);
    }

    pcfl = CFL::PcflCreate(&fni, fcflTemp);
    if (pcfl == pvNil)
    {
        return (fFalse);
    }

    Assert(pcfl->FTemp(), "Bad CFL");

    //
    // 3DMMv1.0: Note (by *****): CRF *must* have 0 cache size, because of
    // 3DMMv1.0: serious cache-coherency problems otherwise.  TMPL data is not
    // 3DMMv1.0: read-only, and chunk numbers change over time.
    //
    _pcrfAutoSave = CRF::PcrfNew(pcfl, 0); // 3DMMv1.0: cache size must be 0
    if (pvNil == _pcrfAutoSave)
    {
        ReleasePpo(&pcfl);
        return (fFalse);
    }

    //
    // 3DMMv1.0: Create movie chunk
    //
    SetDirty();
    if (!FAutoSave())
    {
        ReleasePpo(&pcfl);
        return (fFalse);
    }

    ReleasePpo(&pcfl);
    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * Saves a movie to the temp file assigned to the movie.
 *
 * Parameters:
 *	pfni - File to save to, pvNil if to use a temp file.
 *	pCleanRollCall - Should actors that are not used be removed?
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
bool MVIE::FAutoSave(PFNI pfni, bool fCleanRollCall)
{
    AssertThis(0);
    AssertNilOrPo(_pcrfAutoSave, 0);
    AssertNilOrPo(pfni, ffniFile);

#ifdef BUG1848
    bool fRetry = fTrue;
#endif // 3DMMv1.0: BUG1848
    BLCK blck;
    CNO cno = cnoNil;
    CNO cnoScen = cnoNil;
    CNO cnoSource = cnoNil;
    MFP mfp;
    KID kidScen, kidGstRollCall, kidGstSource;
    PCFL pcfl;
    PGST pgstSource = pvNil;
    PGST pgstmactr = pvNil;
    PCSZ pszFailStage = PszLit("begin");

    if (_pcrfAutoSave == pvNil)
    {
        MultiLog(this, "movie_autosave fail stage=no_crf clean=%d dirty=%d autosave_dirty=%d",
                 (int)fCleanRollCall, (int)_fDirty, (int)_fAutosaveDirty);
        return (fFalse);
    }

    pcfl = _pcrfAutoSave->Pcfl();
    if (pfni != pvNil)
    {
        STN stnSaveDiag;
        pfni->GetStnPath(&stnSaveDiag);
        MultiLog(this,
            "movie_autosave begin dest=%s clean=%d scene=%ld frame=%ld scenes=%ld rollcall=%ld custom_objects=%ld custom_actions=%ld movie_cno=%ld dirty=%d autosave_dirty=%d pcfl_temp=%d",
            stnSaveDiag.Psz(), (int)fCleanRollCall, (long)Iscen(),
            Pscen() != pvNil ? (long)Pscen()->Nfrm() : -1L, (long)Cscen(),
            _pgstmactr != pvNil ? (long)_pgstmactr->IvMac() : -1L,
            (long)_c4DMMCustomObject, (long)_c4DMMCustomAction, (long)_cno,
            (int)_fDirty, (int)_fAutosaveDirty, (int)pcfl->FTemp());
    }
    else
    {
        MultiLog(this,
            "movie_autosave begin dest=<temp> clean=%d scene=%ld frame=%ld scenes=%ld rollcall=%ld custom_objects=%ld custom_actions=%ld movie_cno=%ld dirty=%d autosave_dirty=%d pcfl_temp=%d",
            (int)fCleanRollCall, (long)Iscen(),
            Pscen() != pvNil ? (long)Pscen()->Nfrm() : -1L, (long)Cscen(),
            _pgstmactr != pvNil ? (long)_pgstmactr->IvMac() : -1L,
            (long)_c4DMMCustomObject, (long)_c4DMMCustomAction, (long)_cno,
            (int)_fDirty, (int)_fAutosaveDirty, (int)pcfl->FTemp());
    }

    //
    // 3DMMv1.0: If no changes, then quit quick.
    //
    if (!_fAutosaveDirty && (pfni == pvNil) && !_fDocClosing)
    {
        MultiLog(this, "movie_autosave noop reason=clean_temp");
        return (fTrue);
    }

    vpappb->BeginLongOp();

    pszFailStage = PszLit("ensure_temp_file");
    if ((pfni == pvNil) && !pcfl->FTemp() && !_FUseTempFile())
    {
        MultiLog(this, "movie_autosave fail stage=%s el=%ld", pszFailStage, (long)pcfl->ElError());
        vpappb->EndLongOp();
        return (fFalse);
    }

#ifdef BUG1848
LRetry:
#endif // 3DMMv1.0: BUG1848
    //
    // 3DMMv1.0: Ensure movie chunk exists.
    //
    if (_cno == cnoNil)
    {
        pszFailStage = PszLit("movie_chunk_add");
        if (!pcfl->FAdd(SIZEOF(MFP), kctgMvie, &_cno, &blck))
        {
            goto LFail0;
        }

        mfp.bo = kboCur;
        mfp.osk = koskCur;
        mfp.dver.Set(kcvnCur, kcvnBack);

        pszFailStage = PszLit("movie_chunk_header_write");
        if (!blck.FWrite(&mfp))
        {
            goto LFail0;
        }
    }

    //
    // 3DMMv1.0: Save open scene.
    //
    if (Pscen() != pvNil)
    {

        //
        // 3DMMv1.0: Save scene in new chunk
        //
        pszFailStage = PszLit("scene_write");
        if (!Pscen()->FWrite(_pcrfAutoSave, &cnoScen))
        {
            goto LFail0;
        }
        MultiLog(this, "movie_autosave scene_write ok scene=%ld new_cno=%ld",
                 (long)_iscen, (long)cnoScen);

        //
        // 3DMMv1.0: Delete old chunk with this scene
        //
        AssertDo(pcfl->FGetKidChidCtg(kctgMvie, _cno, _iscen, kctgScen, &kidScen), "Should never fail");

        //
        // 3DMMv1.0: Update chid for movie
        //
        pszFailStage = PszLit("scene_adopt");
        if (!pcfl->FAdoptChild(kctgMvie, _cno, kctgScen, cnoScen, _iscen))
        {
            pcfl->Delete(kctgScen, cnoScen);
            goto LFail0;
        }
    }

    //
    // 3DMMv1.0: Save the movie roll-call
    //
    if (fCleanRollCall)
    {
        MACTR mactr;
        int32_t imactr;

        for (imactr = 0; imactr < _pgstmactr->IvMac();)
        {
            _pgstmactr->GetExtra(imactr, &mactr);
            if (mactr.cactRef == 0)
            {
                _pgstmactr->Delete(imactr);
                vptagm->CloseTag(&mactr.tagTmpl);
            }
            else
            {
                imactr++;
            }
        }

        _pmcc->UpdateRollCall();
    }

    if (_pgstmactr != pvNil)
    {
        MultiLog(this, "movie_autosave rollcall begin entries=%ld", (long)_pgstmactr->IvMac());
        for (int32_t imactrDiag = 0; imactrDiag < _pgstmactr->IvMac(); ++imactrDiag)
        {
            MACTR mactrDiag;
            STN stnDiag;
            _pgstmactr->GetExtra(imactrDiag, &mactrDiag);
            _pgstmactr->GetStn(imactrDiag, &stnDiag);
            const int32_t fChunkExists = mactrDiag.tagTmpl.sid == ksidUseCrf ?
                (int)pcfl->FFind(mactrDiag.tagTmpl.ctg, mactrDiag.tagTmpl.cno) : -1;
            MultiLog(this,
                "movie_autosave rollcall entry=%ld name=%s arid=%ld refs=%ld sid=%ld ctg=0x%08lX cno=%ld local_chunk_exists=%ld",
                (long)imactrDiag, stnDiag.Psz(), (long)mactrDiag.arid, (long)mactrDiag.cactRef,
                (long)mactrDiag.tagTmpl.sid, (unsigned long)mactrDiag.tagTmpl.ctg,
                (long)mactrDiag.tagTmpl.cno, (long)fChunkExists);
        }
    }

    //
    // 3DMMv1.0: Get old roll call if it exists.
    //
    if (!pcfl->FGetKidChidCtg(kctgMvie, _cno, 0, kctgGst, &kidGstRollCall))
    {
        kidGstRollCall.cki.cno = cnoNil;
    }

    pszFailStage = PszLit("rollcall_serialize");
    pgstmactr = SerializeRollCall(_pgstmactr);
    if (pgstmactr == pvNil)
    {
        goto LFail1;
    }

    pszFailStage = PszLit("rollcall_chunk_add");
    if (!pcfl->FAdd(pgstmactr->CbOnFile(), kctgGst, &cno, &blck))
    {
        ReleasePpo(&pgstmactr);
        goto LFail1;
    }

    pszFailStage = PszLit("rollcall_write_or_adopt");
    if (!pgstmactr->FWrite(&blck) || !pcfl->FAdoptChild(kctgMvie, _cno, kctgGst, cno, 0))
    {
        pcfl->Delete(kctgGst, cno);
        ReleasePpo(&pgstmactr);
        goto LFail1;
    }

    ReleasePpo(&pgstmactr);

    //
    // 3DMMv1.0: Save the known sources list
    //

    //
    // 3DMMv1.0: Get old sources list if it exists.
    //
    if (!pcfl->FGetKidChidCtg(kctgMvie, _cno, kchidGstSource, kctgGst, &kidGstSource))
    {
        kidGstSource.cki.cno = cnoNil;
    }

    pszFailStage = PszLit("source_list_get");
    pgstSource = vptagm->PgstSource();
    if (pgstSource == pvNil)
        goto LFail2;

    pszFailStage = PszLit("source_list_chunk_add");
    if (!pcfl->FAdd(pgstSource->CbOnFile(), kctgGst, &cnoSource, &blck))
    {
        goto LFail2;
    }

    pszFailStage = PszLit("source_list_write_or_adopt");
    if (!pgstSource->FWrite(&blck) || !pcfl->FAdoptChild(kctgMvie, _cno, kctgGst, cnoSource, kchidGstSource))
    {
        pcfl->Delete(kctgGst, cnoSource);
        goto LFail2;
    }

    pszFailStage = PszLit("movie_set_name");
    if (!pcfl->FSetName(kctgMvie, _cno, &_stnTitle))
    {
        goto LFail3;
    }

    //
    // 3DMMv1.0: Delete old scene if there is a scene.
    //
    if (Pscen() != pvNil)
    {
        pcfl->DeleteChild(kctgMvie, _cno, kctgScen, kidScen.cki.cno, _iscen);
    }

    //
    // 3DMMv1.0: Delete old roll call list if it exists.
    //
    if (kidGstRollCall.cki.cno != cnoNil)
    {
        pcfl->DeleteChild(kctgMvie, _cno, kctgGst, kidGstRollCall.cki.cno, 0);
    }

    //
    // 3DMMv1.0: Delete old sources list if it exists.
    //
    if (kidGstSource.cki.cno != cnoNil)
    {
        pcfl->DeleteChild(kctgMvie, _cno, kctgGst, kidGstSource.cki.cno, kchidGstSource);
    }

    //
    // 3DMMv1.0: If we fail, don't unwind, as everything is consistent.
    // 3DMMv1.0: Just let the client know we failed.
    //
    if (pfni != pvNil)
    {
        bool fSuccess;
        PFIL pfil;

        //
        // 3DMMv1.0: If we have this file open, then we need to release it
        // 3DMMv1.0: so we can do the save.  We will restore our open below.
        //
        pfil = FIL::PfilFromFni(pfni);
        if (pfil == _pfilSave)
        {
            ReleasePpo(&_pfilSave);
            _fFniSaveValid = fFalse;
        }
        else
        {
            pfil = pvNil;
        }
        //
        // 3DMMv1.0: All garbage collection -- ignore any errors, the file will just be
        // 3DMMv1.0: a widdle bigger than it has to be.
        //
        pszFailStage = PszLit("garbage_collection");
        _FDoGarbageCollection(pcfl);

        pszFailStage = PszLit("cfl_save_destination");
        fSuccess = pcfl->FSave(kctgSoc, pfni);

        if (pfil != pvNil)
        {
            _pfilSave = FIL::PfilFromFni(pfni);
            if (_pfilSave != pvNil)
            {
                _pfilSave->AddRef();
                _fFniSaveValid = fTrue;
            }
        }

        if (!fSuccess)
        {
            goto LFail0;
        }
    }
    else
    {
        pszFailStage = PszLit("cfl_save_temp");
        if (!pcfl->FSave(kctgSoc))
        {
            goto LFail0;
        }
    }

    _fAutosaveDirty = fFalse;
    _fDirty = fTrue;

    //
    // 3DMMv1.0: Set the movie title
    //
    _SetTitle(pfni);

    MultiLog(this, "movie_autosave ok scene=%ld movie_cno=%ld rollcall=%ld",
             (long)Iscen(), (long)_cno,
             _pgstmactr != pvNil ? (long)_pgstmactr->IvMac() : -1L);
    vpappb->EndLongOp();
    return (fTrue);

LFail3:
    pcfl->DeleteChild(kctgMvie, _cno, kctgGst, cnoSource, kchidGstSource);

LFail2:
    pcfl->DeleteChild(kctgMvie, _cno, kctgGst, cno, 0);

LFail1:
    if (Pscen() != pvNil)
    {
        pcfl->DeleteChild(kctgMvie, _cno, kctgScen, cnoScen, _iscen);
    }

LFail0:
    MultiLog(this,
        "movie_autosave FAIL stage=%s el=%ld scene=%ld frame=%ld movie_cno=%ld scene_new_cno=%ld rollcall_new_cno=%ld source_new_cno=%ld",
        pszFailStage != pvNil ? pszFailStage : "unknown", (long)pcfl->ElError(),
        (long)Iscen(), Pscen() != pvNil ? (long)Pscen()->Nfrm() : -1L,
        (long)_cno, (long)cnoScen, (long)cno, (long)cnoSource);
    if (pcfl->ElError() != elNil)
    {
        pcfl->ResetEl();
#ifdef BUG1848
        if (fRetry && pfni != pvNil)
        {
            FNI fniTemp;

            /* 3DMMv1.0: Effectively, move the temp file to the destination path */
            /* 3DMMv1.0: REVIEW seanse(peted): note that the autosave file could whined up
                on the floppy if we fail again (which SeanSe says is "Bad") */
            fniTemp = *pfni;
            if (fniTemp.FGetUnique(pfni->Ftg()) && pcfl->FSave(kctgSoc, &fniTemp))
            {
                pcfl->SetTemp(fTrue);
                vpers->Clear();
                fRetry = fFalse;
                goto LRetry;
            }
        }
#endif // 3DMMv1.0: BUG1848
        PushErc(ercSocSaveFailure);
    }

    vpappb->EndLongOp();
    return (fFalse);
}

/** 3DMMv1.0: **************************************************
 *
 * Do all garbage collection
 *
 * Parameters:
 *	pfni - File to remove chunks from
 *
 * Returns:
 *  fTrue on success, fFalse on failure
 *
 ****************************************************/
bool MVIE::_FDoGarbageCollection(PCFL pcfl)
{
    AssertThis(0);
    AssertPo(pcfl, 0);
    bool fSuccess, fHaveValid;

    // 3DMMv1.0: Material and Template garbage collection
    fSuccess = _FDoMtrlTmplGC(pcfl);

    // 3DMMv1.0: If closing, remove unused sounds
    if (_fDocClosing && FUnusedSndsUser(&fHaveValid))
        _DoSndGarbageCollection(!fHaveValid || Pmcc()->FQueryPurgeSounds());
    return fSuccess;
}

/** 3DMMv1.0: **************************************************
 *
 * Removes all MTRL and TMPL chunks that are not
 * referenced by any actors in this movie.
 *
 * Parameters:
 *	pfni - File to remove chunks from
 *
 * Returns:
 *  fTrue on success, fFalse on failure
 *
 ****************************************************/
bool MVIE::_FDoMtrlTmplGC(PCFL pcfl)
{
    AssertThis(0);
    AssertPo(pcfl, 0);

    PTAGL ptagl = pvNil;
    int32_t itag;
    TAG tag;
    int32_t icki1 = 0;
    int32_t icki2 = 0;
    CKI cki;
    PGL pglckiDoomed = pvNil;

    ptagl = _PtaglFetch(); // 3DMMv1.0: get all tags in user's document
    if (ptagl == pvNil)
        goto LEnd; // 3DMMv1.0: no work to do

    pglckiDoomed = GL::PglNew(SIZEOF(CKI), 0);
    if (pvNil == pglckiDoomed)
        goto LFail;

    while (pcfl->FGetCkiCtg(kctgMtrl, icki1++, &cki) || pcfl->FGetCkiCtg(kctgTmpl, icki2++, &cki))
    {
        // A user-created Actor Studio asset remains part of the VMM even if
        // its last scene instance is temporarily deleted.  Its writable TMPL
        // is therefore a document root alongside TMPLs referenced by roll-call
        // tags, not garbage merely because no ACTR currently points at it.
        bool fKeepCustomTmpl = fFalse;
        if (cki.ctg == kctgTmpl)
        {
            for (int32_t iCustom = 0; iCustom < _c4DMMCustomObject; ++iCustom)
            {
                if (_rg4DMMCustomObject[iCustom].cnoOwnedTmpl == cki.cno)
                {
                    fKeepCustomTmpl = fTrue;
                    break;
                }
            }
            // Native VXP import creates an ordinary movie-owned roll-call
            // template rather than Actor Studio sidecar metadata. A hired
            // imported object must remain in the movie even before its first
            // scene placement, so treat a live roll-call ksidUseCrf TMPL as a
            // document root too. This also matches the stated purpose of this
            // GC: remove templates not referenced by actors in the movie.
            if (!fKeepCustomTmpl && _pgstmactr != pvNil)
            {
                for (int32_t imactrKeep = 0; imactrKeep < _pgstmactr->IvMac(); ++imactrKeep)
                {
                    MACTR mactrKeep;
                    _pgstmactr->GetExtra(imactrKeep, &mactrKeep);
                    if (mactrKeep.cactRef > 0 &&
                        mactrKeep.tagTmpl.sid == ksidUseCrf &&
                        mactrKeep.tagTmpl.ctg == kctgTmpl &&
                        mactrKeep.tagTmpl.cno == cki.cno)
                    {
                        fKeepCustomTmpl = fTrue;
                        break;
                    }
                }
            }
        }
        if (fKeepCustomTmpl)
            continue;

        // 3DMMv1.0: We're only interested in ksidUseCrf tags
        for (itag = 0; itag < ptagl->Ctag(); itag++)
        {
            ptagl->GetTag(itag, &tag);
            if (tag.sid != ksidUseCrf)
            {
                break; // 3DMMv1.0: stop..we're out of the ksidUseCrf tags
            }
            if (tag.ctg == cki.ctg && tag.cno == cki.cno)
            {
                break; // 3DMMv1.0: stop..this tag is used in the movie
            }
        }
        // 3DMMv1.0: Remember, tags are sorted by sid.  So if we got past the
        // 3DMMv1.0: ksidUseCrf tags in the movie, this chunk must not be used
        // 3DMMv1.0: in the movie.  So put it on the blacklist.
        if (tag.sid != ksidUseCrf || itag == ptagl->Ctag())
        {
            // 3DMMv1.0: this chunk is not referenced by a ksidUseCrf tag, so kill it
            if (!pglckiDoomed->FAdd(&cki))
                goto LFail;
        }
    }
    // 3DMMv1.0: Get rid of the blacklisted chunks
    for (icki1 = 0; icki1 < pglckiDoomed->IvMac(); icki1++)
    {
        pglckiDoomed->Get(icki1, &cki);
        pcfl->Delete(cki.ctg, cki.cno);
        if (pcfl == _pcrfAutoSave->Pcfl()) // 3DMMv1.0: remove chunk from CRF cache
        {
            PFNRPO pfnrpo;

            if (kctgMtrl == cki.ctg)
            {
                pfnrpo = MTRL::FReadMtrl;
            }
            else if (kctgTmpl == cki.ctg)
            {
                pfnrpo = TMPL::FReadTmpl;
            }
            else
            {
                Bug("unexpected ctg");
            }
            // 3DMMv1.0: ignore failure of FSetCrep, because return value of fFalse
            // 3DMMv1.0: just means that the chunk is not stored in the CRF's cache
            _pcrfAutoSave->FSetCrep(crepToss, cki.ctg, cki.cno, pfnrpo);
        }
    }
LEnd:
    ReleasePpo(&ptagl);
    ReleasePpo(&pglckiDoomed);
    return fTrue;
LFail:
    ReleasePpo(&ptagl);
    ReleasePpo(&pglckiDoomed);
    return fFalse;
}

/** 3DMMv1.0: **************************************************
 *
 * Gets the file name to save the document to.
 *
 * Parameters:
 *	pfni - A pointer to a place to store the name
 *
 * Returns:
 *  fFalse if the fni is valid, else fTrue *and*
 *  pfni filled in.
 *
 ****************************************************/
bool MVIE::FGetFni(FNI *pfni)
{
    AssertThis(0);
    AssertPo(pfni, 0);

    if (_pfilSave != pvNil)
    {
        _pfilSave->GetFni(pfni);
    }

    return (_fFniSaveValid);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Saves a movie.
 *
 * Parameters:
 *  cid - type of save command issued for save
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
static bool F4DMMBuildSavePathWithExtension(PFNI pfniIn, PCSZ pszExt, uint32_t ftg, FNI *pfniOut)
{
    if (pfniIn == pvNil || pszExt == pvNil || pfniOut == pvNil)
        return fFalse;
    STN stnPath;
    pfniIn->GetStnPath(&stnPath);
    for (int32_t ich = stnPath.Cch() - 1; ich >= 0; --ich)
    {
        const achar ch = stnPath.Psz()[ich];
        if (ch == ChLit('.'))
        {
            stnPath.Delete(ich);
            break;
        }
        if (ch == ChLit('\\') || ch == ChLit('/'))
            break;
    }
    if (!stnPath.FAppendSz(PszLit(".")) || !stnPath.FAppendSz(pszExt))
        return fFalse;
    return pfniOut->FBuildFromPath(&stnPath, ftg);
}

bool MVIE::FSave(int32_t cid)
{
    AssertThis(0);

    // 3DMMv1.0: If we are processing a Save command and the current movie is read-only,
    // 3DMMv1.0: then treat this as a Save-As command and invoke the Save portfolio.

    if (cid == cidSave && FReadOnly())
        cid = cidSaveAs;

    // A movie that has acquired custom Actor Studio content can no longer be
    // saved as a plain .3mm. If it still points at an original 3MM, the first
    // normal Save becomes Save As so the portfolio can establish a .vmm.
    if (cid == cidSave && _fRequiresVmmSave)
    {
        FNI fniCur;
        if (!FGetFni(&fniCur) || !F4DMMFniIsVmm(&fniCur))
            cid = cidSaveAs;
    }

    // 3DMMv1.0: Now take the default action.
    return MVIE_PAR::FSave(cid);
}

/** 3DMMv1.0: **************************************************
 *
 * Saves a movie to the given fni.
 *
 * Parameters:
 *	pfni - File to write to.
 *  fSetFni - Should the file name be remembered.
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
bool MVIE::FSaveToFni(FNI *pfni, bool fSetFni)
{
    AssertThis(0);
    AssertNilOrPo(pfni, ffniFile);

    CKI cki;
    PCFL pcfl;

    if (_pcrfAutoSave == pvNil)
    {
        return (pvNil);
    }

    pcfl = _pcrfAutoSave->Pcfl();

    const bool fSaveVmm = pfni != pvNil && (_fRequiresVmmSave || F4DMMFniIsVmm(pfni));
    if (pfni != pvNil)
    {
        STN stnSaveDiag;
        pfni->GetStnPath(&stnSaveDiag);
        MultiLog(this, "movie_save_to_fni begin path=%s set_fni=%d vmm=%d requires_vmm=%d",
                 stnSaveDiag.Psz(), (int)fSetFni, (int)fSaveVmm, (int)_fRequiresVmmSave);
    }
    if (fSaveVmm)
    {
#if defined(KAUAI_WIN32)
        FNI fniVmm;
        if (F4DMMFniIsVmm(pfni))
        {
            STN stnVmmInput;
            pfni->GetStnPath(&stnVmmInput);
            if (!fniVmm.FBuildFromPath(&stnVmmInput, kftgVmm))
                return fFalse;
        }
        else if (!F4DMMBuildSavePathWithExtension(pfni, kszVmm, kftgVmm, &fniVmm))
            return fFalse;

        STN stnVmmPath;
        fniVmm.GetStnPath(&stnVmmPath);
        STN stnTempPath = stnVmmPath;
        for (int32_t ich = stnTempPath.Cch() - 1; ich >= 0; --ich)
        {
            const achar ch = stnTempPath.Psz()[ich];
            if (ch == ChLit('.')) { stnTempPath.Delete(ich); break; }
            if (ch == ChLit('\\') || ch == ChLit('/')) break;
        }
        achar szTmpSuffix[64];
        sprintf_s(szTmpSuffix, SIZEOF(szTmpSuffix), ".4dmm_save_%lu.3mm", (unsigned long)GetCurrentProcessId());
        if (!stnTempPath.FAppendSz(szTmpSuffix))
            return fFalse;
        FNI fniTemp;
        if (!fniTemp.FBuildFromPath(&stnTempPath, kftg3mm))
            return fFalse;

        STN stnTitleOld = _stnTitle;
        STN stnTrackOld = _stnCameraTrackPath;
        if (!FAutoSave(&fniTemp, fTrue))
        {
            MultiLog(this, "vmm_save fail stage=autosave_temp movie=%s", stnTempPath.Psz());
            return fFalse;
        }

        // A VMM permanently owns a same-basename .3ct beside it. This real
        // sibling is the authoritative live metadata file; F4DMMPackageVmm
        // embeds its bytes as a portability copy immediately before CHN2.
        STN stnSidecar = stnVmmPath;
        for (int32_t ich = stnSidecar.Cch() - 1; ich >= 0; --ich)
        {
            const achar ch = stnSidecar.Psz()[ich];
            if (ch == ChLit('.')) { stnSidecar.Delete(ich); break; }
            if (ch == ChLit('\\') || ch == ChLit('/')) break;
        }
        if (!stnSidecar.FAppendSz(PszLit(".3ct")))
        {
            _stnCameraTrackPath = stnTrackOld;
            _stnTitle = stnTitleOld;
            Pmcc()->UpdateTitle(&_stnTitle);
            remove(stnTempPath.Psz());
            return fFalse;
        }
        _stnCameraTrackPath = stnSidecar;
        MultiLog(this, "vmm_save sidecar_write begin path=%s", stnSidecar.Psz());
        if (!_FWriteCameraTrack())
        {
            MultiLog(this, "vmm_save fail stage=sidecar_write path=%s", stnSidecar.Psz());
            _stnCameraTrackPath = stnTrackOld;
            _stnTitle = stnTitleOld;
            Pmcc()->UpdateTitle(&_stnTitle);
            remove(stnTempPath.Psz());
            return fFalse;
        }
        MultiLog(this, "vmm_save sidecar_write ok path=%s", stnSidecar.Psz());

        // Match the legacy CFL save path's file-handle discipline. If this
        // VMM is already the document save target, release its FIL before the
        // atomic replacement so Windows cannot reject MoveFileEx because the
        // old container is still open. F4DMMPackageVmm separately closes its
        // std::ifstream before reaching MoveFileExA.
        PFIL pfilVmm = FIL::PfilFromFni(&fniVmm);
        const bool fVmmWasCurrentSave = pfilVmm != pvNil && pfilVmm == _pfilSave;
        if (fVmmWasCurrentSave)
        {
            ReleasePpo(&_pfilSave);
            _fFniSaveValid = fFalse;
        }

        MultiLog(this, "vmm_save package begin movie=%s sidecar=%s dest=%s current=%d",
                 stnTempPath.Psz(), stnSidecar.Psz(), stnVmmPath.Psz(), (int)fVmmWasCurrentSave);
        if (!F4DMMPackageVmm(stnTempPath.Psz(), stnSidecar.Psz(), stnVmmPath.Psz()))
        {
            MultiLog(this, "vmm_save fail stage=package dest=%s", stnVmmPath.Psz());
            if (fVmmWasCurrentSave)
                FUseVmmSaveTarget(&fniVmm);
            _stnCameraTrackPath = stnTrackOld;
            _stnTitle = stnTitleOld;
            Pmcc()->UpdateTitle(&_stnTitle);
            remove(stnTempPath.Psz());
            return fFalse;
        }
        remove(stnTempPath.Psz());
        MultiLog(this, "vmm_save package ok dest=%s sidecar=%s", stnVmmPath.Psz(), stnSidecar.Psz());

        // Save Copy must leave the currently-open movie's sidecar authority
        // untouched. Normal Save/Save As adopts the new VMM + sibling pair.
        if (!fSetFni)
            _stnCameraTrackPath = stnTrackOld;

        ClearUndo();
        AssertDo(pcfl->FSetGrfcfl(fcflAddToExtra, fcflAddToExtra | fcflTemp), 0);
        if (fSetFni)
        {
            FUseVmmSaveTarget(&fniVmm); // Ignore failure, matching legacy Save
        }
        else
        {
            if (fVmmWasCurrentSave)
                FUseVmmSaveTarget(&fniVmm);
            _stnTitle = stnTitleOld;
            Pmcc()->UpdateTitle(&_stnTitle);
        }
        AssertDo(pcfl->FGetCkiCtg(kctgMvie, 0, &cki), "Should never fail");
        _cno = cki.cno;
        _fRequiresVmmSave = fTrue;
        _fDirty = fFalse;
        MultiLog(this, "vmm_save path=%s set_fni=%d custom_objects=%ld custom_actions=%ld",
                 stnVmmPath.Psz(), (int)fSetFni, (long)_c4DMMCustomObject, (long)_c4DMMCustomAction);
        return fTrue;
#else
        Warn("VMM package saving is currently implemented only on Windows");
        return fFalse;
#endif
    }

    //
    // 3DMMv1.0: Update the file
    //
    if (!FAutoSave(pfni, fTrue))
    {
        MultiLog(this, "movie_save_to_fni fail stage=autosave_non_vmm");
        return (fFalse);
    }

    // Camera-track edits participate in the movie save transaction even though
    // the current format still lives in a matching .3ct sidecar.  Rebuild the
    // sidecar path from the actual save destination so Save As and Save Copy
    // produce matching camera-track files beside their .3mm files.
    bool fCameraTrackFileExists = fFalse;
    if (_stnCameraTrackPath.Cch() > 0)
    {
        FILE *pfileTrack = fopen(_stnCameraTrackPath.Psz(), "rt");
        if (pfileTrack != pvNil)
        {
            fCameraTrackFileExists = fTrue;
            fclose(pfileTrack);
        }
    }
    bool fHas4DMMSettings = _fDefaultMusicForNewScenes || _fExperimental100xGrow ||
                              _fExperimental100xShrink || _flCameraMoveSpeed != 1.0f ||
                              _flCameraMouseSensitivity != kfl4DMMDefaultCameraMouseSensitivity ||
                              _c4DMMCustomObject > 0 || _c4DMMCustomAction > 0;
    if (!fHas4DMMSettings)
        for (int32_t i = 0; i < LwMin(Cscen(), kc4DMMSceneSettingsMax); i++)
            if (_rgfSceneLightsEnabled[i] || _rgfSceneHideLightObjects[i]) { fHas4DMMSettings = fTrue; break; }

    if (_cctween > 0 || _cctman > 0 || _clightLab > 0 || _cObjectProperties > 0 ||
        fHas4DMMSettings || fCameraTrackFileExists)
    {
        STN stnCameraTrackPathOld = _stnCameraTrackPath;
        STN stnCameraTrackPath;
        int32_t ich;

        pfni->GetStnPath(&stnCameraTrackPath);
        for (ich = stnCameraTrackPath.Cch() - 1; ich >= 0; ich--)
        {
            if (stnCameraTrackPath.Psz()[ich] == ChLit('.'))
            {
                stnCameraTrackPath.Delete(ich);
                break;
            }
            if (stnCameraTrackPath.Psz()[ich] == ChLit('\\') ||
                stnCameraTrackPath.Psz()[ich] == ChLit('/'))
            {
                break;
            }
        }

        if (!stnCameraTrackPath.FAppendSz(PszLit(".3ct")))
            return fFalse;

        _stnCameraTrackPath = stnCameraTrackPath;
        if (!_FWriteCameraTrack())
        {
            _stnCameraTrackPath = stnCameraTrackPathOld;
            return fFalse;
        }

        // Save Copy must not redirect subsequent edits away from the movie
        // that remains open.  Normal Save and Save As keep the destination.
        if (!fSetFni)
            _stnCameraTrackPath = stnCameraTrackPathOld;
    }

    ClearUndo();

    // 3DMMv1.0: Set the AddToExtra flag and clear the Temp flag
    AssertDo(pcfl->FSetGrfcfl(fcflAddToExtra, fcflAddToExtra | fcflTemp), 0);

    if (fSetFni)
    {
        _FSetPfilSave(pfni); // 3DMMv1.0: Ignore failure
    }

    //
    // 3DMMv1.0: Update _cno
    //
    AssertDo(pcfl->FGetCkiCtg(kctgMvie, 0, &cki), "Should never fail");

    _cno = cki.cno;

    _fDirty = fFalse;

    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Called by docb.cpp to get an fni for a selected movie file using
 * the save portfolio.
 *
 * Parameters:
 *	pfni - fni for selected file
 *
 * Returns:
 *  TRUE  - User selected a file
 *  FALSE - User canceled, (or other error).
 *
 ***************************************************************************/
bool MVIE::FGetFniSave(FNI *pfni)
{
    AssertThis(0);
    AssertVarMem(pfni);

    if (_fRequiresVmmSave)
    {
        STN stnDefault = _stnTitle;
        if (!stnDefault.FAppendSz(PszLit(".vmm")))
            return fFalse;
        return _pmcc->GetFniSave(pfni, idsPortfMovieFilterLabel, idsPortfMovieFilterExt,
                                 idsPortfSaveMovieTitle, kszVmm, &stnDefault);
    }
    return (_pmcc->GetFniSave(pfni, idsPortfMovieFilterLabel, idsPortfMovieFilterExt, idsPortfSaveMovieTitle, ksz3mm,
                              &_stnTitle));
}

/** 3DMMv1.0: **************************************************
 *
 * Creates a new view on a movie.
 *
 * Parameters:
 *	pgcb - The creation block describing the gob placement
 *
 * Returns:
 *  A pointer to the view, otw pvNil on failure
 *
 ****************************************************/
PDDG MVIE::PddgNew(PGCB pgcb)
{
    AssertThis(0);
    AssertVarMem(pgcb);

    PDDG pddg = MVU::PmvuNew(this, pgcb, _pmcc->Dxp(), _pmcc->Dyp());
    if (pddg != pvNil)
        ShowUndoHistoryWindow();
    return pddg;
}

/** 3DMMv1.0: **************************************************
 *
 * Is not used.  Not supported.  Stubbed out here for
 * debugging.
 *
 * Parameters:
 *	None
 *
 * Returns:
 *  pvNil
 *
 ****************************************************/
PDMD MVIE::PdmdNew(void)
{
    Bug("Movie does not support DMDs, use multiple DDGs.");
    return (pvNil);
}

/** 3DMMv1.0: **************************************************
 *
 * Adds a single item to the undo list
 *
 * Parameters:
 *	pmund - A pointer to a movie undo item.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool MVIE::FAddUndo(PMUNB pmunb)
{
    AssertThis(0);

    pmunb->SetPmvie(this);
    pmunb->SetIscen(Iscen());

    AssertPo(pmunb, 0);

    if (Iscen() != ivNil)
    {
        pmunb->SetNfrm(Pscen()->Nfrm());
    }

    if (!DOCB::FAddUndo(pmunb))
    {
        Pmcc()->SetUndo(undoDisabled);
        return (fFalse);
    }

    Pmcc()->SetUndo(undoUndo);
    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * Clears out the undo buffer
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void MVIE::ClearUndo(void)
{
    AssertThis(0);

    MVIE_PAR::ClearUndo();
    Pmcc()->SetUndo(undoDisabled);
}

/** 3DMMv1.0: *************************************************************************
 *
 * This routine changes the current camera view
 *
 * Parameters:
 *	icam - The new camera to use.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool MVIE::FChangeCam(int32_t icam)
{
    AssertThis(0);
    AssertIn(icam, 0, kccamMax);
    AssertPo(Pscen(), 0);

    if (_fFreeLookMode || _fFreeLookOverride)
        _ClearFreeLookState();

    if (!Pscen()->FChangeCam(icam))
    {
        return (fFalse);
    }

    SetDirty();
    InvalViews();
    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * This command inserts a new text box into the open scene.
 *
 * Parameters:
 *	prc - The placement within the movie's view of the text box.
 *  fStory - Is this supposed to be a story text box?
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool MVIE::FInsTbox(RC *prc, bool fStory)
{
    AssertThis(0);
    AssertPvCb(prc, SIZEOF(RC));
    AssertPo(Pscen(), 0);

    PTBOX ptbox;

    ptbox = TBOX::PtboxNew(Pscen(), prc, fStory);

    if (ptbox == pvNil)
    {
        return (fFalse);
    }
    ptbox->SetDypFontDef(Pmcc()->DypTboxDef());

    AssertPo(ptbox, 0);

    if (!Pscen()->FAddTbox(ptbox))
    {
        ReleasePpo(&ptbox);
    }

    Pscen()->SelectTbox(ptbox);
    ptbox->AttachToMouse();
    ReleasePpo(&ptbox);

    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * This command removes the currently selected text box.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool MVIE::FNukeTbox(void)
{
    AssertThis(0);
    AssertPo(Pscen(), 0);

    PTBOX ptbox;

    ptbox = Pscen()->PtboxSelected();

    if (ptbox == pvNil)
    {
        PushErc(ercSocNoTboxSelected);
        return (fFalse);
    }

    return (Pscen()->FRemTbox(ptbox));
}

/** 3DMMv1.0: *************************************************************************
 *
 * This command hides the currently selected text box at the current frame.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool MVIE::FHideTbox(void)
{
    AssertThis(0);
    AssertPo(Pscen(), 0);

    PTBOX ptbox;

    ptbox = Pscen()->PtboxSelected();

    if (ptbox == pvNil)
    {
        PushErc(ercSocNoTboxSelected);
        return (fFalse);
    }

    return (ptbox->FHide());
}

/** 3DMMv1.0: *************************************************************************
 *
 * This command selects the itbox'th text box in the current frame.
 *
 * Parameters:
 *	itbox - Index value of the text box to select.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MVIE::SelectTbox(int32_t itbox)
{
    AssertThis(0);
    AssertPo(Pscen(), 0);

    PTBOX ptbox;
    int32_t cVis = 0;

    while (fTrue)
    {
        ptbox = Pscen()->PtboxFromItbox(itbox);
        if (ptbox == pvNil)
        {
            return;
        }

        if (ptbox->FIsVisible() && (cVis == itbox))
        {
            return;
        }

        if (ptbox->FIsVisible())
        {
            cVis++;
        }
    }

    Pscen()->SelectTbox(ptbox);
}

/** 3DMMv1.0: *************************************************************************
 *
 * This sets the color to apply to a text box.
 *
 * Parameters:
 *  acr - The destination color.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
void MVIE::SetPaintAcr(ACR acr)
{
    AssertThis(0);

    PMVU pmvu;

    pmvu = (PMVU)PddgGet(0);
    AssertPo(pmvu, 0);
    pmvu->SetPaintAcr(acr);
}

/** 3DMMv1.0: ****************************************************************************
    SetDypFontTextCur
        Sets the current textbox font size

    Arguments:
        long dypFont  --  the new textbox font size

************************************************************ PETED ***********/
void MVIE::SetDypFontTextCur(int32_t dypFont)
{
    AssertThis(0);

    PmvuFirst()->SetDypFontTextCur(dypFont);
}

/** 3DMMv1.0: ****************************************************************************
    SetStyleTextCur
        Sets the current textbox font style

    Arguments:
        long grfont  --  the new textbox font style

************************************************************ PETED ***********/
void MVIE::SetStyleTextCur(uint32_t grfont)
{
    AssertThis(0);

    PmvuFirst()->SetStyleTextCur(grfont);
}

/** 3DMMv1.0: ****************************************************************************
    SetOnnTextCur
        Sets the current textbox font face

    Arguments:
        long onn  -- the new textbox font face

************************************************************ PETED ***********/
void MVIE::SetOnnTextCur(int32_t onn)
{
    AssertThis(0);

    PmvuFirst()->SetOnnTextCur(onn);
}

/** 3DMMv1.0: ****************************************************************************
    PmvuCur
        Returns the active MVU for this movie

************************************************************ PETED ***********/
PMVU MVIE::PmvuCur(void)
{
    AssertThis(0);
    PMVU pmvu = (PMVU)PddgActive();

    AssertPo(pmvu, 0);
    Assert(pmvu->FIs(kclsMVU), "Current DDG isn't an MVU");
    return pmvu;
}

/** 3DMMv1.0: ****************************************************************************
    PmvuFirst
        Returns the first MVU for this movie

************************************************************ PETED ***********/
PMVU MVIE::PmvuFirst(void)
{
    AssertThis(0);
    PMVU pmvu = (PMVU)PddgGet(0);

    AssertPo(pmvu, 0);
    Assert(pmvu->FIs(kclsMVU), "First DDG isn't an MVU");
    return pmvu;
}

/** 3DMMv1.0: *************************************************************************
 *
 * This command inserts a new actor.
 *
 * Parameters:
 *	ptag - The tag of the actor to create.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool MVIE::FInsActr(PTAG ptag)
{
    AssertThis(0);
    AssertPvCb(ptag, SIZEOF(TAG));
    AssertPo(Pscen(), 0);

    PACTR pactr;
    TAG tagResolved;
    PTAG ptagUse = ptag;
    bool fResolvedOpened = fFalse;

    // Once a stock Actor/Prop has a movie-local writable replacement, that
    // replacement is the movie's authoritative version for future inserts.
    // The stock TAG remains untouched and is only the internal source/fallback.
    if (FResolve4DMMReplacementTemplateTag(ptag, &tagResolved))
    {
        if (!TAGM::FOpenTag(&tagResolved, _pcrfAutoSave))
            return fFalse;
        ptagUse = &tagResolved;
        fResolvedOpened = fTrue;
        MultiLog(this,
            "actor_studio_insert redirect source sid=%ld ctg=0x%08lX cno=%ld -> replacement cno=%ld",
            (long)ptag->sid, (unsigned long)ptag->ctg, (long)ptag->cno,
            (long)tagResolved.cno);
    }

    vpappb->BeginLongOp();

    //
    // 3DMMv1.0: Create the actor.
    //
    if (!vptagm->FCacheTagToHD(ptagUse))
    {
        vpappb->EndLongOp();
        if (fResolvedOpened)
            TAGM::CloseTag(&tagResolved);
        return (fFalse);
    }

    vpappb->EndLongOp();

    pactr = ACTR::PactrNew(ptagUse);
    if (fResolvedOpened)
        TAGM::CloseTag(&tagResolved);
    if (pactr == pvNil)
    {
        return (fFalse);
    }

    AssertPo(pactr, 0);
    if (!Pscen()->FAddActr(pactr))
    {
        ReleasePpo(&pactr);
        return (fFalse);
    }

    ReleasePpo(&pactr);

    SetDirty();
    InvalViewsAndScb();

    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Brings an actor on to the stage.
 *
 * Parameters:
 *	arid - Arid of the actor to bring on.  aridNil implies the
 *		currently selected actor.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool MVIE::FAddOnstage(int32_t arid)
{
    AssertThis(0);
    AssertPo(Pscen(), 0);

    ACTR *pactr;
    //
    // 3DMMv1.0: Get the actor
    //
    if (arid == aridNil)
    {
        pactr = Pscen()->PactrSelected();
    }
    else
    {
        pactr = Pscen()->PactrFromArid(arid);
        Pscen()->SelectActr(pactr);
    }

    if (pactr == pvNil)
    {
        PushErc(ercSocNoActrSelected);
        return (fFalse);
    }

    AssertPo(pactr, 0);

    if (!pactr->FAddOnStage())
    {
        return (fFalse);
    }

    SetDirty();
    InvalViews();

    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Removes the selected actor from the current scene
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool MVIE::FRemActr()
{
    AssertThis(0);
    AssertPo(Pscen(), 0);

    ACTR *pactr;

    //
    // 3DMMv1.0: Get the current actor
    //
    pactr = Pscen()->PactrSelected();
    AssertPo(pactr, 0);

    // Browser/command deletion reaches MVIE::FRemActr directly and therefore
    // bypasses MVU::_ClearSel.  v132 only intercepted the latter, which is why
    // deleting a Light Lab member left the Object Group alive with a
    // <missing object> row.  Treat membership here, at the common actor-delete
    // boundary, as authoritative: deleting any member deletes the whole group.
    int32_t idObjectGroup = 0;
    if (FObjectInObjectGroup(pactr->Arid(), &idObjectGroup) && idObjectGroup > 0)
    {
        const int32_t cMember = CObjectGroupMembers(idObjectGroup);
        int32_t *rgarid = pvNil;
        if (cMember <= 0 ||
            !FAllocPv((void **)&rgarid, LwMul(cMember, SIZEOF(int32_t)), fmemClear, mprNormal))
            return fFalse;

        int32_t cArid = 0;
        for (int32_t iMember = 0; iMember < cMember; ++iMember)
        {
            const OBJECTGROUPMEMBER *pmember = PObjectGroupMember(idObjectGroup, iMember);
            if (pmember != pvNil)
                rgarid[cArid++] = pmember->arid;
        }
        if (cArid <= 0)
        {
            FreePpv((void **)&rgarid);
            return fFalse;
        }

        PGUND pgund = GUND::PgundNew();
        if (pgund == pvNil || !pgund->FCaptureGroupDelete(this, idObjectGroup) ||
            !FAddUndo(pgund))
        {
            ReleasePpo(&pgund);
            FreePpv((void **)&rgarid);
            ClearUndo();
            PushErc(ercSocNotUndoable);
            return fFalse;
        }
        ReleasePpo(&pgund);

        if (!FUnbindObjectGroup(idObjectGroup))
        {
            FreePpv((void **)&rgarid);
            return fFalse;
        }

        int32_t cDeleted = 0;
        for (int32_t iMember = 0; iMember < cArid; ++iMember)
        {
            PACTR pactrMember = Pscen()->PactrFromArid(rgarid[iMember]);
            if (pactrMember == pvNil)
                continue;
            LIGHTLAB lightMember;
            const bool fLightMember =
                FGetLightLabConfig(Iscen(), pactrMember->Arid(), &lightMember);
            const int32_t aridDelete = pactrMember->Arid();
            // GUND owns complete actor snapshots for this deletion. Bypass
            // SCEN::FRemActr here because that wrapper creates one SUNA
            // "Delete Actor" entry per member; RemActrCore performs the same
            // authoritative removal while keeping the whole OG deletion one
            // user-visible Undo operation.
            Pscen()->RemActrCore(aridDelete);
            if (Pscen()->PactrFromArid(aridDelete) != pvNil)
            {
                MVIE::MultiLog(this,
                    "group_delete_central member_fail id=%ld arid=%ld deleted=%ld total=%ld",
                    (long)idObjectGroup, (long)aridDelete, (long)cDeleted, (long)cArid);
                FreePpv((void **)&rgarid);
                return fFalse;
            }
            if (fLightMember)
                FRemoveLightLabConfigCore(Iscen(), aridDelete);
            Pmcc()->ActorNuked();
            ++cDeleted;
        }

        MVIE::MultiLog(this, "group_delete_central id=%ld members=%ld deleted=%ld",
                       (long)idObjectGroup, (long)cArid, (long)cDeleted);
        FreePpv((void **)&rgarid);
        SetDirty();
        UpdateTestLightAttachment();
        if (_pbwld != pvNil)
            _pbwld->MarkDirty();
        InvalViewsAndScb();
        return cDeleted > 0;
    }

    if (!Pscen()->FRemActr(pactr->Arid()))
    {
        return (fFalse);
    }

    SetDirty();
    InvalViewsAndScb();
    Pmcc()->ActorNuked();

    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Rotates selected actor by degrees around an axis.
 *
 * Parameters:
 *  axis - Brender axis to rotate around.
 *	xa - Degrees to rotate by in X.
 *	ya - Degrees to rotate by in Y.
 *	za - Degrees to rotate by in Z.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool MVIE::FRotateActr(BRA xa, BRA ya, BRA za, bool fFromHereFwd)
{
    AssertThis(0);
    AssertPo(Pscen(), 0);

    ACTR *pactr;

    //
    // 3DMMv1.0: Get the current actor
    //
    pactr = Pscen()->PactrSelected();
    AssertPo(pactr, 0);

    if (!pactr->FRotate(xa, ya, za, fFromHereFwd))
    {
        return (fFalse);
    }

    SetDirty();
    InvalViews();

    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Squashes/Stretches the selected actor by a scalar.
 *
 * Parameters:
 *  brs - The scalar for squashing.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool MVIE::FSquashStretchActr(BRS brs)
{
    AssertThis(0);
    AssertPo(Pscen(), 0);

    ACTR *pactr;

    //
    // 3DMMv1.0: Get the current actor
    //
    pactr = Pscen()->PactrSelected();
    AssertPo(pactr, 0);

    if (brs == rZero)
    {
        return (fTrue);
    }

    BRS brsy = BrsDiv(rOne, brs);

    if (!pactr->FPull(brs, brsy, brs))
    {
        return (fFalse);
    }

    SetDirty();
    InvalViews();

    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Drags the actor in time
 *
 * Parameters:
 *  nfrm - The destination frame number.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool MVIE::FSoonerLaterActr(int32_t nfrm)
{
    AssertThis(0);
    AssertPo(Pscen(), 0);

    ACTR *pactr;
    PAUND paund;

    int32_t dnfrm = nfrm - Pscen()->Nfrm();

    //
    // 3DMMv1.0: Get the current actor
    //
    pactr = Pscen()->PactrSelected();
    AssertPo(pactr, 0);

    if (!pactr->FSoonerLater(dnfrm))
    {
        return (fFalse);
    }

    if (CundbUndo() > 0)
    {

        _pglpundb->Get(_ipundbLimDone - 1, &paund);

        if (paund->FIs(kclsAUND) && paund->FSoonerLater())
        {
            AssertPo(paund, 0);
            paund->SetNfrmLast(nfrm);
        }
    }

    SetDirty();
    InvalViews();

    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Scales the selected actor by a scalar.
 *
 * Parameters:
 *  brs - The scalar for scaling.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool MVIE::FScaleActr(BRS brs, bool fExtended)
{
    AssertThis(0);
    AssertPo(Pscen(), 0);

    ACTR *pactr;

    //
    // 3DMMv1.0: Get the current actor
    //
    pactr = Pscen()->PactrSelected();
    AssertPo(pactr, 0);

    const BRS rScaleMin = (fExtended || FExperimental100xShrink()) ? krScaleMinExtended : krScaleMin;
    const BRS rScaleMax = (fExtended || FExperimental100xGrow()) ? krScaleMax : krScaleMaxNormal;
    if (!pactr->FScale(brs, rScaleMin, rScaleMax))
    {
        return (fFalse);
    }

    SetDirty();
    InvalViewsAndScb();

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 *
 * Adds a sound to the background
 *
 * Parameters:
 *  ptag - tag to the MSND to insert
 *  fLoop - play snd over and over?
 *  fQueue - replace existing sounds, or queue afterwards?
 *  vlm - volume to play sound
 *  sty - sound type
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
bool MVIE::FAddBkgdSnd(PTAG ptag, tribool fLoop, tribool fQueue, int32_t vlm, int32_t sty)
{
    AssertThis(0);
    Assert(Pscen(), 0);

    if (vlm == vlmNil || sty == styNil)
    {
        PMSND pmsnd;

        pmsnd = (PMSND)vptagm->PbacoFetch(ptag, MSND::FReadMsnd);
        if (pmsnd == pvNil)
            return fFalse;
        if (vlm == vlmNil)
            vlm = pmsnd->Vlm();
        if (sty == styNil)
            sty = pmsnd->Sty();
        ReleasePpo(&pmsnd);
    }

    return Pscen()->FAddSnd(ptag, fLoop, fQueue, vlm, sty);
}

/** 3DMMv1.0: **************************************************
 *
 * Adds a sound to an actor
 *
 * Parameters:
 *  pactr - actor to attach sound to
 *  ptag - tag to the MSND to insert
 *  fLoop - play snd over and over?
 *  fQueue - replace existing sounds, or queue afterwards?
 *  vlm - volume to use (vlmNil -> use pmsnd volume)
 *  sty - type to use (styNil -> use pmsnd sty)
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
bool MVIE::FAddActrSnd(PTAG ptag, tribool fLoop, tribool fQueue, tribool fActnCel, int32_t vlm, int32_t sty)
{
    AssertThis(0);
    ACTR *pactr;

    //
    // 3DMMv1.0: Get the current actor
    //
    pactr = Pscen()->PactrSelected();
    AssertPo(pactr, 0);

    return pactr->FSetSnd(ptag, fLoop, fQueue, fActnCel, vlm, sty);
}

/** 3DMMv1.0: **************************************************
 *
 * Takes a scene and inserts it as scene number iscen,
 * and switches to the scene.
 *
 * Parameters:
 *	iscen - Scene number to insert the new scene as.
 *	pscen - Pointer to the scene to insert.
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
bool MVIE::FInsScenCore(int32_t iscen, SCEN *pscen)
{
    AssertThis(0);
    AssertIn(iscen, 0, Cscen() + 1);
    AssertPo(pscen, 0);
    Assert(pscen->Pmvie() == this, "Cannot insert a scene from another movie");

    CNO cnoScen;
    PCFL pcfl;
    bool rgfSceneDefaultLightingShadersOld[kc4DMMSceneSettingsMax];
    bool rgfSceneLightLabCombineLegacyOld[kc4DMMSceneSettingsMax];
    CopyPb(_rgfSceneDefaultLightingShaders, rgfSceneDefaultLightingShadersOld,
           SIZEOF(_rgfSceneDefaultLightingShaders));
    CopyPb(_rgfSceneLightLabCombineLegacy, rgfSceneLightLabCombineLegacyOld,
           SIZEOF(_rgfSceneLightLabCombineLegacy));

    //
    // 3DMMv1.0: Make sure we have a file to switch to
    //
    if (!_FMakeCrfValid())
    {
        goto LFail0;
    }

    pcfl = _pcrfAutoSave->Pcfl();

    //
    // 3DMMv1.0: Ensure we are using the temp file
    //
    if (!_FUseTempFile())
    {
        goto LFail0;
    }

    //
    // 3DMMv1.0: Save old scene.
    //
    if (!_FCloseCurrentScene())
    {
        goto LFail0;
    }

    //
    // 3DMMv1.0: Write it.
    //
    if (!pscen->FWrite(_pcrfAutoSave, &cnoScen))
    {
        goto LFail0;
    }

    //
    // 3DMMv1.0: Hide all bodies, textboxes, etc, created when the write updated the thumbnail.
    //
    pscen->AddRef();
    SCEN::Close(&pscen);

    _MoveChids((CHID)iscen, fTrue);

    // Keep the new per-scene 4DMM lighting policy indexed with the native
    // scene list. Deleted-scene undo restores its saved value after this core
    // insertion; genuinely new scenes inherit the current movie default.
    if (_cscen < kc4DMMSceneSettingsMax)
    {
        for (int32_t i = _cscen; i > iscen; --i)
        {
            _rgfSceneDefaultLightingShaders[i] = _rgfSceneDefaultLightingShaders[i - 1];
            _rgfSceneLightLabCombineLegacy[i] = _rgfSceneLightLabCombineLegacy[i - 1];
        }
        _rgfSceneDefaultLightingShaders[iscen] = _fDefaultLightingShaders;
        _rgfSceneLightLabCombineLegacy[iscen] = _fLightLabCombineLegacy;
    }
    _cscen++;

    //
    // 3DMMv1.0: Insert new scene as chid.
    //
    if (!pcfl->FAdoptChild(kctgMvie, _cno, kctgScen, cnoScen, iscen))
    {
        goto LFail2;
    }

    //
    // 3DMMv1.0: Save changes, if this fails, we don't care.  It only
    // 3DMMv1.0: matters when the user tries to truly save.
    //
    pcfl->FSave(kctgSoc);

    if (!FSwitchScen(iscen))
    {
        goto LFail3;
    }

    //
    // 3DMMv1.0: Fix up roll call, don't care about failure -- roll-call will just be messed up.
    //
    Pscen()->FAddActrsToRollCall();

    // Object Groups belong to the same scene indices as the native movie.
    // Shift existing group metadata only after all fallible insertion work has
    // succeeded so the rollback paths above remain unchanged.
    for (int32_t iGroup = 0; iGroup < _cObjectGroup; iGroup++)
        if (_rgObjectGroup[iGroup].iscen >= iscen)
            _rgObjectGroup[iGroup].iscen++;
    for (int32_t iProp = 0; iProp < _cObjectProperties; ++iProp)
        if (_rgObjectProperties[iProp].iscen >= iscen)
            _rgObjectProperties[iProp].iscen++;

    SetDirty();
    _pmcc->UpdateRollCall();

    if (FSoundsEnabled())
    {
        _pmsq->PlayMsq();
    }
    else
    {
        _pmsq->FlushMsq();
    }

    return (fTrue);

LFail3:
    CopyPb(rgfSceneDefaultLightingShadersOld, _rgfSceneDefaultLightingShaders,
           SIZEOF(_rgfSceneDefaultLightingShaders));
    CopyPb(rgfSceneLightLabCombineLegacyOld, _rgfSceneLightLabCombineLegacy,
           SIZEOF(_rgfSceneLightLabCombineLegacy));
    _cscen--;
    pcfl->DeleteChild(kctgMvie, _cno, kctgScen, cnoScen, iscen);
    _MoveChids((CHID)iscen, fFalse);
    pcfl->FSave(kctgSoc);

    if (_cscen > 0)
    {

        if (iscen < _cscen)
        {
            FSwitchScen(iscen);
        }
        else
        {
            FSwitchScen(_cscen - 1);
        }
    }

    return (fFalse);

LFail2:
    CopyPb(rgfSceneDefaultLightingShadersOld, _rgfSceneDefaultLightingShaders,
           SIZEOF(_rgfSceneDefaultLightingShaders));
    CopyPb(rgfSceneLightLabCombineLegacyOld, _rgfSceneLightLabCombineLegacy,
           SIZEOF(_rgfSceneLightLabCombineLegacy));
    _MoveChids((CHID)iscen, fFalse);
    pcfl->Delete(kctgScen, cnoScen);

LFail0:
    return (fFalse);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Adds a new scene after the current scene.
 *
 * Parameters:
 *	ptag - The tag of the scene to add.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool MVIE::FAddScen(PTAG ptag)
{
    AssertThis(0);
    AssertPvCb(ptag, SIZEOF(TAG));

    int32_t iscen;
    TAG tagOld;
    PMUNS pmuns;

    vpappb->BeginLongOp();

    //
    // 3DMMv1.0: Set the background
    //
    if (!BKGD::FCacheToHD(ptag))
    {
        vpappb->EndLongOp();
        return (fFalse);
    }

    vpappb->EndLongOp();

    //
    // 3DMMv1.0: Set default name
    //
    if ((Pscen() == pvNil) && (_stnTitle.Cch() == 0))
    {
        _SetTitle();
    }

    //
    // 3DMMv1.0: Check if this is supposed to overwrite the current scene
    //
    if ((Pscen() == pvNil) || (!Pscen()->FIsEmpty()))
    {

        iscen = Iscen();
        if (iscen == ivNil)
        {
            iscen = -1;
        }

        if (!FNewScenInsCore(iscen + 1))
        {
            return (fFalse);
        }

        if (!Pscen()->FSetBkgdCore(ptag, &tagOld))
        {
            FRemScenCore(iscen + 1);
            return (fFalse);
        }

        if (FSoundsEnabled())
        {
            _pmsq->PlayMsq();
        }
        else
        {
            _pmsq->FlushMsq();
        }

        pmuns = MUNS::PmunsNew();

        if (pmuns != pvNil)
        {
            pmuns->SetMunst(munstInsScen);
            pmuns->SetIscen(iscen + 1);
            pmuns->SetTag(ptag);

            if (!FAddUndo(pmuns))
            {
                ReleasePpo(&pmuns);
                FRemScenCore(iscen + 1);
                return (fFalse);
            }

            ReleasePpo(&pmuns);
        }
        else
        {
            FRemScenCore(iscen + 1);
            return (fFalse);
        }
    }
    else
    {

        if (!Pscen()->FSetBkgd(ptag))
        {
            Bug("warning: set background failed.");
            return (fFalse);
        }
    }

    SetDirty();
    InvalViewsAndScb();

    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Plays a movie.
 *
 * Parameters:
 *	None
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MVIE::Play()
{
    AssertThis(0);
    DiagSetContext(this, "MVIE::Play enter");

    if (FPlaying())
    {

        if (FStopPlaying())
        {
            //
            // 3DMMv1.0: We will stop it soon anyway.
            //
            return;
        }

        //
        // 3DMMv1.0: Kill playing timer
        //
        SetFStopPlaying(fTrue);

        if (_fPausing)
        {
            //
            // 3DMMv1.0: There is no clock timer, so call directly
            //
            FCmdRender(pvNil);
        }
    }
    else
    {

        if (Pscen() == pvNil)
        {
            Pmcc()->PlayStopped();
            return;
        }

        // 3DMMv1.0: reset any outstanding sounds (from listener preview)
        _pmsq->StopAll();
        // 3DMMv1.0: flush any outstanding sound messages (from listener preview)
        _pmsq->FlushMsq();

        //
        // 3DMMv1.0: Check for if we need to rewind
        //
        if ((Iscen() + 1 == Cscen()) && (Pscen()->Nfrm() == Pscen()->NfrmLast()))
        {
            if (!FSwitchScen(0))
            {
                Pmcc()->PlayStopped();
                return;
            }
            if (!Pscen()->FGotoFrm(Pscen()->NfrmFirst()))
            {
                Pmcc()->PlayStopped();
                return;
            }
        }

        //
        // 3DMMv1.0: Start playing timer
        //
        _fOldSoundsEnabled = FSoundsEnabled();
        SetFSoundsEnabled(fTrue);
        _cnfrm = 0;
        _tsStart = TsCurrent();
        SetFStopPlaying(fFalse);
        _clok.Start(0);
        SetFPlaying(fTrue);
        vpcex->EnqueueCid(cidMviePlaying, pvNil, pvNil, fTrue);

        if (!_clok.FSetAlarm(0, this))
        {
            Pmcc()->PlayStopped();
            SetFPlaying(fFalse);
            _FinishManualCameraRecording();
            _pmsq->PlayMsq();
            SetFSoundsEnabled(_fOldSoundsEnabled);
            return;
        }

        //
        // 3DMMv1.0: Check if we need to play the opening transition
        //
        _pmsq->SndOnLong();
        Pscen()->Enable(fscenPauses);
        Pscen()->SelectActr(pvNil);
        Pscen()->SelectTbox(pvNil);
        // 3DMMv1.0: Have to FReplayFrm *before* FStartPlaying because a camera view
        // 3DMMv1.0: change in FReplayFrm would wipe out prerendering (which FStartPlaying
        // 3DMMv1.0: initiates)
        DiagSetContext(this, "MVIE::Play before FReplayFrm/FStartPlaying");
        if (!Pscen()->FReplayFrm(fscenPauses | fscenSounds | fscenActrs) || !Pscen()->FStartPlaying())
        {
            Pmcc()->PlayStopped();
            SetFStopPlaying(fTrue);
            _FinishManualCameraRecording();
            _pmsq->PlayMsq();
            SetFSoundsEnabled(_fOldSoundsEnabled);
            return;
        }

        DiagSetContext(this, "MVIE::Play before ApplyCameraTrack");
        ApplyCameraTrack();
        DiagSetContext(this, "MVIE::Play before initial Render");
        Pbwld()->Render();
        DiagSetContext(this, "MVIE::Play after initial Render");

        Pmcc()->DisableAccel();

        if ((Iscen() == 0) && (Pscen()->Nfrm() == Pscen()->NfrmFirst()))
        {
            _pmcc->UpdateScrollbars();
            InvalViews();
            vpappb->UpdateMarked();
        }

        Pscen()->PlayBkgdSnd();
    }

    // 3DMMv1.0: Play sound queue
    if (FSoundsEnabled())
    {
        _pmsq->PlayMsq();
    }
    else
    {
        _pmsq->FlushMsq();
    }
    return;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Handle an alarm going off.
 *
 * This routine simply enqueues a command to render a frame.  We do it this
 * way because the clock pre-empts commands in the command queue.  If the
 * user mouse clicks, we need to process those clicks.  The best way to do
 * that is by doing rendering via the command queue.
 *
 * Parameters:
 *	pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 ***************************************************************************/
bool MVIE::FCmdAlarm(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    CMD cmd;

    if (FIdleSeen() || vpcex->PgobTracking() != pvNil)
    {

        SetFIdleSeen(fFalse);

        ClearPb(&cmd, SIZEOF(CMD));

        cmd.pcmh = this;
        cmd.cid = cidRender;
        vpcex->EnqueueCmd(&cmd);
    }
    else
    {

        //
        // 3DMMv1.0: Check again, asap.
        //
        if (!_clok.FSetAlarm(0, this))
        {
            PMVU pmvu;

            //
            // 3DMMv1.0: Things are in a bad way.
            //
            Pmcc()->EnableAccel();
            SetFPlaying(fFalse);
            SetFStopPlaying(fFalse);
            _FinishManualCameraRecording();
            SetFSoundsEnabled(_fOldSoundsEnabled);
            _clok.RemoveCmh(this);
            _fPausing = fFalse;
            _fScrolling = fFalse;
            _wit = witNil;
            pmvu = (PMVU)PddgGet(0);
            pmvu->PauseUntilClick(fFalse);
            Pscen()->Enable(fscenTboxes);
            Pscen()->Disable(fscenPauses);
            ApplyCameraTrack();

            //
            // 3DMMv1.0: Update views and scroll bars
            //
            InvalViewsAndScb();

            //
            // 3DMMv1.0: Clean up anything else
            //
            Pscen()->StopPlaying();
            vpcex->EnqueueCid(cidMviePlaying, pvNil, pvNil, fFalse);

            //
            // 3DMMv1.0: Set sound queue state
            //
            _pmsq->SndOnShort();

            Pmcc()->PlayStopped();
            vpsndm->StopAll();
        }
    }

    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Handle rendering a frame.
 *
 * This routine gets a little busy.  The basic premise is to render a
 * frame one frame ahead of the one currently displayed.  This means
 * that only actor stuff gets done first, then when the frame is to be
 * displayed, textboxes and sounds get started.  If there is a scrolling
 * text box, then we play nothing else until the scrolling is done, and
 * (finally) pauses in the frame.
 *
 * Parameters:
 *	pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 ***************************************************************************/
bool MVIE::FCmdRender(PCMD pcmd)
{
    AssertThis(0);
    AssertNilOrVarMem(pcmd);
    DiagSetContext(this, "MVIE::FCmdRender enter");

    PMVU pmvu;
    PTBOX ptbox;
    int32_t itbox;
    uint32_t tsCur = TsCurrent();
    uint64_t qwPerfFrame = 0;
    uint64_t qwPerfStage = 0;
    uint32_t cusecPerfTextCurrent = 0;
    uint32_t cusecPerfUpdateMarked = 0;
    uint32_t cusecPerfAdvance = 0;
    uint32_t cusecPerfFinalCamera = 0;
    uint32_t cusecPerfLightAttachment = 0;
    uint32_t cusecPerfFinalRender = 0;

    if (FPerformanceMode())
    {
        ClearPb(&vperfAcc, SIZEOF(vperfAcc));
        qwPerfFrame = PerfNow();
    }

    pmvu = (PMVU)PddgGet(0);
    AssertPo(pmvu, 0);

    if (FStopPlaying())
    {
    LStopPlaying:

        Pmcc()->EnableAccel();
        _clok.Stop();
        _clok.RemoveCmh(this);
        _fPausing = fFalse;
        _fScrolling = fFalse;
        _wit = witNil;
        SetFStopPlaying(fFalse);
        SetFPlaying(fFalse);
        _FinishManualCameraRecording();
        SetFSoundsEnabled(_fOldSoundsEnabled);
        pmvu->PauseUntilClick(fFalse);
        if (Pscen() != pvNil)
        {
            Pscen()->Enable(fscenTboxes);
            Pscen()->Disable(fscenPauses);
        }
        else
            Assert(_iscen == ivNil, "Bogus scene state");

        if (Pscen() != pvNil)
            ApplyCameraTrack();

        //
        // 3DMMv1.0: Update views and scroll bars
        //
        InvalViewsAndScb();

        //
        // 3DMMv1.0: Clean up anything else
        //
        if (Pscen() != pvNil)
            Pscen()->StopPlaying();
        vpcex->EnqueueCid(cidMviePlaying, pvNil, pvNil, fFalse);

        //
        // 3DMMv1.0: Set sound queue state
        //
        _pmsq->SndOnShort();

        Pmcc()->PlayStopped();
        vpsndm->StopAll();

        // 3DMMv1.0: if we were fading, then restore sound volume
        if (_vlmOrg)
        {
            vpsndm->SetVlm(_vlmOrg);
            _vlmOrg = 0;
        }

        return (fTrue);
    }

    //
    // 3DMMv1.0: if _vlmOrg is nonzero, then we are in the process of fading
    //
    if (_vlmOrg)
    {
        int32_t vlm;

        // 3DMMv1.0: get the current volume
        vlm = vpsndm->VlmCur();

        // 3DMMv1.0: massage it
        vlm -= _vlmOrg / (kdtsVlmFade * 4); // 3DMMv1.0: kdtsVolFade seconds * 4 volume changes a second = total number deltas

        // 3DMMv1.0: if new volume is below 0, then we are done
        if (vlm <= 0)
        {
            SetFStopPlaying(fTrue);
            vpsndm->SetVlm(0);
            goto LStopPlaying;
        }

        // 3DMMv1.0: set the volume to new level
        vpsndm->SetVlm(vlm);

        if (!_clok.FSetAlarm(kdtimVlmFade, this))
        {
            goto LStopPlaying;
        }

        return fTrue;
    }

    if (_fScrolling)
    {
        //
        // 3DMMv1.0: Do next text box scroll.
        //
        _fScrolling = fFalse;

        for (itbox = 0;; itbox++)
        {
            ptbox = Pscen()->PtboxFromItbox(itbox);
            AssertNilOrPo(ptbox, 0);
            if (ptbox == pvNil)
            {
                break;
            }

            if (ptbox->FNeedToScroll())
            {
                ptbox->Scroll();
                _fScrolling = fTrue;
            }
        }

        if (_fScrolling && !_clok.FSetAlarm(kdtsScrolling, this))
        {
            _fScrolling = fFalse;
            goto LStopPlaying;
        }
        else if (_fScrolling)
        {
            return fTrue;
        }
        else
        {
            goto LCheckForPause;
        }
    }

    //
    // 3DMMv1.0: If we are not pausing, then start the stuff for this frame
    //
    if (!_fPausing)
    {

        //
        // 3DMMv1.0: Now do all text boxes.
        //
        Pscen()->Enable(fscenTboxes);
        Pscen()->Disable(fscenActrs);
        DiagSetContext(this, "MVIE::FCmdRender before textbox FGotoFrm(current)");
        if (FPerformanceMode())
            qwPerfStage = PerfNow();
        if (!Pscen()->FGotoFrm(Pscen()->Nfrm()))
        {
            Pscen()->Enable(fscenActrs);
            goto LStopPlaying;
        }
        Pscen()->Enable(fscenActrs);
        if (FPerformanceMode())
            cusecPerfTextCurrent = PerfElapsedUs(qwPerfStage);
        DiagSetContext(this, "MVIE::FCmdRender after textbox FGotoFrm(current)");

        //
        // 3DMMv1.0: Draw the previous frame
        //
        MarkViews();

        //
        // 3DMMv1.0: Update scroll bars
        //
        _pmcc->UpdateScrollbars();

        //
        // 3DMMv1.0: Play outstanding sounds
        //
        if (FSoundsEnabled())
        {
            _pmsq->PlayMsq();
        }
        else
        {
            _pmsq->FlushMsq();
        }

        //
        // 3DMMv1.0: Flush to the screen
        //
        DiagSetContext(this, "MVIE::FCmdRender before UpdateMarked");
        if (FPerformanceMode())
            qwPerfStage = PerfNow();
        vpappb->UpdateMarked();
        if (FPerformanceMode())
            cusecPerfUpdateMarked = PerfElapsedUs(qwPerfStage);
        DiagSetContext(this, "MVIE::FCmdRender after UpdateMarked");

        //
        // 3DMMv1.0: Check for any text box scrolling
        //
        //
        for (itbox = 0;; itbox++)
        {
            ptbox = Pscen()->PtboxFromItbox(itbox);
            AssertNilOrPo(ptbox, 0);
            if (ptbox == pvNil)
            {
                break;
            }

            if (ptbox->FNeedToScroll())
            {
                if (!_clok.FSetAlarm(kdtsScrolling, this))
                {
                    goto LStopPlaying;
                }

                _fScrolling = fTrue;
                return (fTrue);
            }
        }

    LCheckForPause:

        //
        // 3DMMv1.0: Check for a pause
        //
        switch (_wit)
        {
        case witUntilClick:
            _wit = witNil;
            _fPausing = fTrue;
            pmvu->PauseUntilClick(fTrue);
            return (fTrue);
        case witUntilSnd:
            if (_pmsq->FPlaying(fFalse))
            {
                _fPausing = fTrue;
                if (!_clok.FSetAlarm(0, this))
                {
                    goto LStopPlaying;
                }
                return (fTrue);
            }

            _fPausing = fFalse;
            _wit = witNil;
            break;

        case witForTime:
            _wit = witNil;
            _fPausing = fTrue;
            _clok.FSetAlarm(_dts, this);
            return (fTrue);
        case witNil:
            _fPausing = fFalse;
            break;
        default:
            Bug("Bad Pause type");
        }
    }
    else
    {
        if (_wit == witUntilSnd)
        {
            goto LCheckForPause;
        }
        else
        {
            pmvu->PauseUntilClick(fFalse);
            _fPausing = fFalse;
        }
    }

    //
    // 3DMMv1.0: Advance everything to the next frame.
    // 3DMMv1.0:     Account for time spent between beginning of this routine and
    // 3DMMv1.0:     here.
    //
    if (!_clok.FSetAlarm(LwMax(0, kdtimFrame - LwMulDivAway((TsCurrent() - tsCur), kdtimSecond, kdtsSecond)), this, 0,
                         fTrue))
    {
        goto LStopPlaying;
    }

    if (FPerformanceMode())
        qwPerfStage = PerfNow();
    Pscen()->Disable(fscenTboxes);

    if (Pscen()->Nfrm() == Pscen()->NfrmLast())
    {

        if (Iscen() == (Cscen() - 1))
        {
            if (_fManualCameraRecording)
                _fManualCameraRecordExitMode = fTrue;

            // 3DMMv1.0: since this is the last scene/last frame, we want to
            // 3DMMv1.0: fade out music, by setting _vlmOrg we turn off rendering and fade out
            // 3DMMv1.0: music until VlmCur is 0, at which point we go into stop state.

            if (!vpsndm->FPlayingAll()) // 3DMMv1.0: there are no sounds playing
                SetFStopPlaying(fTrue); // 3DMMv1.0: there is nothing to fade
            else
            {
                _vlmOrg = vpsndm->VlmCur(); // 3DMMv1.0: get the current volume
                if ((0 == _vlmOrg))         // 3DMMv1.0: if there is volume to fade with
                    SetFStopPlaying(fTrue); // 3DMMv1.0: there is nothing to fade
            }
            return (fTrue);
        }

        _trans = Pscen()->Trans();

        if (FSwitchScen(Iscen() + 1))
        {
            AssertPo(Pscen(), 0);
            Pscen()->Disable(fscenTboxes);
            Pscen()->Enable(fscenPauses);
            if (!Pscen()->FReplayFrm(fscenPauses | fscenSounds | fscenActrs))
            {
                SetFStopPlaying(fTrue);
                return (fTrue);
            }

            Pscen()->PlayBkgdSnd();
        }
        else
        {
            SetFStopPlaying(fTrue);
            return (fTrue);
        }
    }
    else
    {

        DiagLog("MVIE::FCmdRender advance scene=%ld from_frame=%ld to_frame=%ld",
                (long)Iscen(), (long)Pscen()->Nfrm(), (long)(Pscen()->Nfrm() + 1));
        DiagSetContext(this, "MVIE::FCmdRender before FGotoFrm(next)");
        if (!Pscen()->FGotoFrm(Pscen()->Nfrm() + 1))
        {
            SetFStopPlaying(fTrue);
            return (fTrue);
        }
        DiagSetContext(this, "MVIE::FCmdRender after FGotoFrm(next)");
    }

    if (FPerformanceMode())
        cusecPerfAdvance = PerfElapsedUs(qwPerfStage);

    if (_fManualCameraRecording && !_FCaptureManualCameraFrame())
    {
        // Capacity or another capture failure stops the pass without creating
        // a frame the user did not already have.
        SetFStopPlaying(fTrue);
    }

    DiagSetContext(this, "MVIE::FCmdRender before ApplyCameraTrack");
    if (FPerformanceMode())
        qwPerfStage = PerfNow();
    ApplyCameraTrack();
    if (FPerformanceMode())
        cusecPerfFinalCamera = PerfElapsedUs(qwPerfStage);
    DiagSetContext(this, "MVIE::FCmdRender before Render");
    if (FPerformanceMode())
        qwPerfStage = PerfNow();
    UpdateTestLightAttachment();
    if (FPerformanceMode())
    {
        cusecPerfLightAttachment = PerfElapsedUs(qwPerfStage);
        vperfAcc.cusecLightPreparation += cusecPerfLightAttachment;
        qwPerfStage = PerfNow();
    }
    Pbwld()->Render();
    if (FPerformanceMode())
        cusecPerfFinalRender = PerfElapsedUs(qwPerfStage);
    DiagSetContext(this, "MVIE::FCmdRender after Render");
    _cnfrm++;

#if defined(KAUAI_WIN32)
    if (FPerformanceMode() && vpfilePerf != pvNil)
    {
        const uint32_t cusecTotal = PerfElapsedUs(qwPerfFrame);
        const double dWorkFps = cusecTotal > 0 ? 1000000.0 / (double)cusecTotal : 0.0;
        const int32_t cusecBudget = (1000000 / kfps);
        const int32_t cusecOverBudget = (int32_t)cusecTotal - cusecBudget;
        const uint32_t cusecBRenderKnown = vperfAcc.cusecBRenderBegin + vperfAcc.cusecBRenderClean +
                                           vperfAcc.cusecBRenderScene + vperfAcc.cusecBRenderPost;
        const uint32_t cusecBRenderOther = vperfAcc.cusecBRenderTotal > cusecBRenderKnown
                                               ? vperfAcc.cusecBRenderTotal - cusecBRenderKnown
                                               : 0;
        const int32_t cactr = Pscen() != pvNil ? Pscen()->Cactr() : 0;
        const int32_t ctbox = Pscen() != pvNil ? Pscen()->Ctbox() : 0;
        const int32_t nfrmPerf = Pscen() != pvNil ? Pscen()->Nfrm() : ivNil;

        fprintf(vpfilePerf,
                "%lu,%ld,%ld,%ld,%ld,%d,%lu,%.3f,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%ld,%ld,%lu,%lu,%lu,%lu,%lu,%lu,%ld,%lu,%lu,%lu,%lu,%lu,%lu,%ld,%ld,%ld,%ld,%lu,%ld,%lu,%ld,%lu,%ld,%lu,%lu,%lu,%ld,%ld,%ld,%lu,%ld,%lu,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%lu,%ld,%ld,%ld,%lu,%ld,%ld,%ld,%ld,%lu,%ld,%ld,%lu,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%lu,%lu,%d,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld\n",
                (unsigned long)vluPerfSequence++, (long)Iscen(), (long)nfrmPerf,
                (long)cactr, (long)ctbox, (int)BWLD::FTrueColorMode(),
                (unsigned long)cusecTotal, dWorkFps,
                (unsigned long)cusecPerfTextCurrent, (unsigned long)cusecPerfUpdateMarked,
                (unsigned long)cusecPerfAdvance, (unsigned long)cusecPerfFinalCamera,
                (unsigned long)cusecPerfFinalRender,
                (unsigned long)vperfAcc.cusecForceActors, (unsigned long)vperfAcc.cusecForceTboxes,
                (unsigned long)vperfAcc.cusecVisibility, (unsigned long)vperfAcc.cusecSceneCamera,
                (unsigned long)vperfAcc.cusecPrerender, (long)vperfAcc.cBRenderCalls,
                (long)vperfAcc.cWorldChildrenMax, (unsigned long)vperfAcc.cusecBRenderBegin,
                (unsigned long)vperfAcc.cusecBRenderClean, (unsigned long)vperfAcc.cusecBRenderScene,
                (unsigned long)vperfAcc.cusecBRenderPost, (unsigned long)vperfAcc.cusecBRenderTotal,
                (unsigned long)cusecBRenderOther, (long)cusecOverBudget,
                (unsigned long)vperfAcc.cusecForceActors,
                (unsigned long)vperfAcc.cusecLightPreparation,
                (unsigned long)vperfAcc.cusecModelLighting,
                (unsigned long)vperfAcc.cusecModelUpdate,
                (unsigned long)vperfAcc.cusecBRenderScene,
                (unsigned long)cusecPerfUpdateMarked,
                (long)vperfAcc.cModelsRelit, (long)vperfAcc.cVerticesProcessed,
                (long)vperfAcc.cModelsRebuilt, (long)vperfAcc.cLightCalculations,
                (unsigned long)vperfAcc.cusecSetActnCel, (long)vperfAcc.cSetActnCelCalls,
                (unsigned long)vperfAcc.cusecActionFetch, (long)vperfAcc.cActionFetchCalls,
                (unsigned long)vperfAcc.cusecModelFetch, (long)vperfAcc.cModelFetchCalls,
                (unsigned long)vperfAcc.cusecModelLookup, (unsigned long)vperfAcc.cusecPbacoFetch,
                (unsigned long)vperfAcc.cusecModelRead, (long)vperfAcc.cModelReadCalls,
                (long)vperfAcc.cModelReadUnprepared, (long)vperfAcc.cModelReadPreprepared,
                (unsigned long)vperfAcc.cusecModelDestroy, (long)vperfAcc.cModelDestroyCalls,
                (unsigned long)vperfAcc.cusecPartModel, (long)vperfAcc.cPartModelCalls,
                (long)vperfAcc.cPartModelSamePointer, (long)vperfAcc.cPartModelSameSource,
                (long)vperfAcc.cPartModelSameResource, (long)vperfAcc.cPartModelResourceReuse,
                (long)vperfAcc.cPartModelSameFile, (long)vperfAcc.cPartModelFileReuse,
                (unsigned long)vperfAcc.cusecPartModelGeometryCompare,
                (long)vperfAcc.cPartModelSameGeometry, (long)vperfAcc.cPartModelGeometryReuse,
                (long)vperfAcc.cPartModelChanged,
                (unsigned long)vperfAcc.cusecPartMatrix, (long)vperfAcc.cPartMatrixCalls,
                (long)vperfAcc.cUniqueModelRequests, (long)vperfAcc.cUniqueModelReads,
                (long)vperfAcc.cUniqueModelOverflow,
                (unsigned long)vperfAcc.cusecPartModelContentCompare,
                (long)vperfAcc.cPartModelSameContent, (long)vperfAcc.cPartModelContentReuse,
                (unsigned long)vperfAcc.grfPartModelGeometryReject,
                (long)vperfAcc.cPartModelContentCandidates,
                (long)vperfAcc.cPartModelOldCrf, (long)vperfAcc.cPartModelNewCrf,
                (long)vperfAcc.cPartModelOldFingerprint, (long)vperfAcc.cPartModelNewFingerprint,
                (long)vperfAcc.cPartModelFingerprintSizeMatch,
                (long)vperfAcc.cPartModelFingerprintHashAMatch,
                (long)vperfAcc.cPartModelFingerprintHashBMatch,
                (long)vperfAcc.cModelAddCalls, (long)vperfAcc.cModelAddQuickCalls,
                (long)vperfAcc.cModelUpdateAllCalls, (long)vperfAcc.cModelUpdateOtherCalls,
                (long)vperfAcc.cModelUpdateQuickCalls,
                (unsigned long)vperfAcc.cusecModelUpdateAll,
                (unsigned long)vperfAcc.cusecModelUpdateOther,
                (int)BWLD::F3DFixMode(),
                (long)vperfAcc.xr3DFixCamera, (long)vperfAcc.yr3DFixCamera,
                (long)vperfAcc.zr3DFixCamera, (long)vperfAcc.zr3DFixHither,
                (long)vperfAcc.zr3DFixYon,
                (long)vperfAcc.c3DFixMeshCalls, (long)vperfAcc.c3DFixMeshAcceptInput,
                (long)vperfAcc.c3DFixForcedPartial, (long)vperfAcc.cSafeDivOverflows,
                (long)vperfAcc.cDepthGradientCalls, (long)vperfAcc.cDepthGradientOverflows);
        if (++vcPerfRowsSinceFlush >= (uint32_t)kfps)
        {
            fflush(vpfilePerf);
            vcPerfRowsSinceFlush = 0;
        }
    }
#endif // KAUAI_WIN32

    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * This sets the costume of an actor.
 *
 * Parameters:
 *	ibprt - Id of the body part to set.
 *  ptag - Pointer to the tag of the costume, if fCustom != fTrue.
 *  cmid - The cmid of the costume, if fCustom == fTrue.
 *  fCustom - Is this a custom costume.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool MVIE::FCostumeActr(int32_t ibprt, PTAG ptag, int32_t cmid, tribool fCustom)
{
    AssertThis(0);
    AssertPo(Pscen(), 0);

    ACTR *pactr;

    //
    // 3DMMv1.0: Get the current actor
    //
    pactr = Pscen()->PactrSelected();
    AssertPo(pactr, 0);

    if (!pactr->FSetCostume(ibprt, ptag, cmid, fCustom))
    {
        return (fFalse);
    }

    SetDirty();
    InvalViews();

    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * This inserts a pause into the scene right now.
 *
 * Parameters:
 *	wit - The type of pause to insert, or witNil to clear pauses.
 *  dts - Number of clock ticks to pause.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool MVIE::FPause(WIT wit, int32_t dts)
{
    AssertThis(0);
    AssertPo(Pscen(), 0);

    if (!Pscen()->FPause(wit, dts))
    {
        return (fFalse);
    }

    SetDirty();
    return (fTrue);
}

#ifdef DEBUG
/** 3DMMv1.0: ****************************************************************************
    MarkMem
        Marks memory used by the CMVI

************************************************************ PETED ***********/
void CMVI::MarkMem(void)
{
    int32_t iv, ivMac;
    MVIED mvied;
    SCEND scend;

    ivMac = pglmvied->IvMac();
    for (iv = 0; iv < ivMac; iv++)
    {
        pglmvied->Get(iv, &mvied);
        MarkMemObj(mvied.pcrf);
    }
    MarkMemObj(pglmvied);

    ivMac = pglscend->IvMac();
    for (iv = 0; iv < ivMac; iv++)
    {
        pglscend->Get(iv, &scend);
        MarkMemObj(scend.pmbmp);
    }
    MarkMemObj(pglscend);
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: ****************************************************************************
    FAddToCmvi
        Generates a GL of SCENDs that describes this movie.  A movie client
        that wishes to makes wholesale changes to a movie may get this GL,
        rearrange it, including inserting references to new movie files, and
        pass it back to the movie via FSetCmvi to modify the movie.
        Adds the movie to the GL of movie descriptors.

    Arguments:
        PCMVI pcmvi     --  the CMVI to add the movie to
        long iscendIns  --  the point at which to start inserting scenes

    Returns: fTrue if it was successful, fFalse otherwise

************************************************************ PETED ***********/
bool MVIE::FAddToCmvi(PCMVI pcmvi, int32_t *piscendIns)
{
    AssertThis(0);
    AssertVarMem(pcmvi);
    AssertNilOrPo(pcmvi->pglscend, 0);

    int32_t iscen = 0, iscenMac = Cscen(), imvied;
    SCEND scend;
    MVIED mvied;
    PCFL pcfl;

    scend.imvied = ivNil;

#ifdef BUG1929
    /* 3DMMv1.0: Ensure that _pcrfAutoSave points to the temp file, not the original movie file */
    if (!_FUseTempFile())
        goto LFail;
#endif // 3DMMv1.0: BUG1929

    if (!FAutoSave(pvNil, fFalse))
        goto LFail;

    if ((pcmvi->pglscend == pvNil) && (pcmvi->pglscend = GL::PglNew(SIZEOF(SCEND))) == pvNil)
    {
        goto LFail;
    }

    if ((pcmvi->pglmvied == pvNil) && (pcmvi->pglmvied = GL::PglNew(SIZEOF(MVIED))) == pvNil)
    {
        goto LFail;
    }

    mvied.pcrf = _pcrfAutoSave;
    mvied.cno = _cno;
    mvied.aridLim = _aridLim;
    if (!pcmvi->pglmvied->FAdd(&mvied, &imvied))
        goto LFail;
    scend.imvied = imvied;
    mvied.pcrf->AddRef();

    pcfl = mvied.pcrf->Pcfl();
    scend.fNuked = fFalse;

    for (iscen = 0; iscen < iscenMac; iscen++, (*piscendIns)++)
    {
        bool fSuccess;
        KID kid;

        /* 3DMMv1.0: Get CNO */
        AssertDo(pcfl->FGetKidChidCtg(kctgMvie, _cno, iscen, kctgScen, &kid), "Not enough scene chunks for movie");
        scend.cno = kid.cki.cno;
        scend.chid = iscen;
        scend.pmbmp = pvNil;

        /* 3DMMv1.0: Get PMBMP and TRANS from the scene */
        if (iscen != Iscen())
        {
            BLCK blck;

            if (!SCEN::FTransOnFile(mvied.pcrf, scend.cno, &scend.trans))
                goto LFail;

            AssertDo(pcfl->FGetKidChidCtg(kctgScen, scend.cno, 0, kctgThumbMbmp, &kid),
                     "Scene doesn't have a thumbnail");
            if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
                goto LFail;
            if ((scend.pmbmp = MBMP::PmbmpRead(&blck)) == pvNil)
                goto LFail;
        }
        else
        {
            scend.trans = Pscen()->Trans();
            if ((scend.pmbmp = Pscen()->PmbmpThumbnail()) == pvNil)
                goto LFail;
            scend.pmbmp->AddRef();
        }

        /* 3DMMv1.0: Add to the list */
        fSuccess = _FInsertScend(pcmvi->pglscend, *piscendIns, &scend);
        ReleasePpo(&scend.pmbmp);
        if (!fSuccess)
            goto LFail;
    }

    return fTrue;
LFail:
    while (iscen--)
        _DeleteScend(pcmvi->pglscend, --(*piscendIns));
    if (pcmvi->pglscend != pvNil && pcmvi->pglscend->IvMac() == 0)
        ReleasePpo(&pcmvi->pglscend);
    if (scend.imvied != ivNil)
    {
        ReleasePpo(&mvied.pcrf);
        pcmvi->pglmvied->Delete(scend.imvied);
    }
    if (pcmvi->pglmvied != pvNil && pcmvi->pglmvied->IvMac() == 0)
        ReleasePpo(&pcmvi->pglmvied);
    return fFalse;
}

/** 3DMMv1.0: ****************************************************************************
    FSetCmvi
        Rebuilds the movie based on the given CMVI.  Any scenes
        marked for deletion are disowned by their MVIE chunk.  Any scenes that
        refer to a movie file other than this MVIE's auto save file are
        copied into this MVIE's auto save file.  SCEN chunks are given new
        CHIDs reflecting their new position within the movie.  The non-nuked
        scenes must appear in the GL in the order that they appear in the
        movie; other than that, there is no restriction on the order of the
        scenes (ie, nuked scenes can appear anywhere in the GL, even though
        currently the only client of this API keeps the nuked scenes at the
        end).

    Arguments:
        PCMVI pcmvi -- the CMVI that describes the new movie structure

    Returns: fTrue if it could accomplish all of the above, fFalse otherwise

************************************************************ PETED ***********/
bool MVIE::FSetCmvi(PCMVI pcmvi)
{
    AssertThis(0);
    AssertVarMem(pcmvi);
    AssertPo(pcmvi->pglmvied, 0);
    AssertPo(pcmvi->pglscend, 0);

    bool fRet = fFalse;
    int32_t iscend, iscendMac = pcmvi->pglscend->IvMac();
    int32_t iscenOld = Iscen();
    int32_t imvied, imviedMac = pcmvi->pglmvied->IvMac();
    int32_t aridMin = 0;
    CHID chidScen = 0;
    PCFL pcfl = _pcrfAutoSave->Pcfl();
    PCRF pcrf = _pcrfAutoSave;
    PGL pglmviedNew;

    pglmviedNew = pcmvi->pglmvied->PglDup();
    if (pglmviedNew == pvNil)
        goto LFail;

    /* 3DMMv1.0: Copy all the external movie chunks into this movie */
    for (imvied = 0; imvied < imviedMac; imvied++)
    {
        MVIED mvied;

        pglmviedNew->Get(imvied, &mvied);

        if (imvied > 0)
        {
            if (!mvied.pcrf->Pcfl()->FClone(kctgMvie, mvied.cno, pcfl, &mvied.cno))
                goto LFail;
            if (!_FAddMvieToRollCall(mvied.cno, aridMin))
                goto LFail;
            mvied.pcrf = pcrf;
            mvied.pcrf->AddRef();
            pglmviedNew->Put(imvied, &mvied);
        }
        else
        {
            Assert(mvied.pcrf == pcrf, "Invalid GL of MVIEDs");
            Assert(mvied.cno == _cno, "Invalid GL of MVIEDs");
        }
        aridMin += mvied.aridLim;
    }

    for (iscend = 0; iscend < iscendMac; iscend++)
    {
        CNO cnoScen = cnoNil;
        SCEND scend;
        MVIED mvied;

        pcmvi->pglscend->Get(iscend, &scend);
        pglmviedNew->Get(scend.imvied, &mvied);

        /* 3DMMv1.0: Was this scene imported? */
        if (scend.imvied > 0)
        {
            KID kid;

            if (!pcfl->FGetKidChidCtg(kctgMvie, mvied.cno, scend.chid, kctgScen, &kid))
            {
                goto LFail;
            }
            cnoScen = kid.cki.cno;

            /* 3DMMv1.0: If so, only bother keeping it if the user didn't delete it */
            if (!scend.fNuked)
            {
                PSCEN pscen;

                if (!pcfl->FAdoptChild(kctgMvie, _cno, kctgScen, cnoScen, chidScen++))
                    goto LFail;
                if (!_FAdoptMsndInMvie(pcfl, cnoScen))
                    goto LFail;
                if ((pscen = SCEN::PscenRead(this, pcrf, cnoScen)) == pvNil || !pscen->FPlayStartEvents(fTrue) ||
                    !pscen->FAddActrsToRollCall())
                {
                    PushErc(ercSocNoImportRollCall);
                }

                Pmcc()->EnableActorTools();
                Pmcc()->EnableTboxTools();
                ReleasePpo(&pscen);
            }
        }
        else
        {
            if (scend.fNuked)
            {
                PSCEN pscen;

                if (scend.chid != (CHID)iscenOld)
                {
                    pscen = SCEN::PscenRead(this, pcrf, scend.cno);
                    if (pscen == pvNil || !pscen->FPlayStartEvents(fTrue))
                        PushErc(ercSocNoNukeRollCall);
                }
                else
                {
                    pscen = _pscenOpen;
                    pscen->AddRef();
                }

                if (pscen != pvNil)
                {
                    pscen->RemActrsFromRollCall(fTrue);
                    ReleasePpo(&pscen);
                }
                pcfl->DeleteChild(kctgMvie, _cno, kctgScen, scend.cno, scend.chid);
            }
            else
            {
                /* 3DMMv1.0: Set the CHID to be the current scene number */
                cnoScen = scend.cno;
                pcfl->ChangeChid(kctgMvie, _cno, kctgScen, scend.cno, scend.chid, chidScen++);
            }
        }

        /* 3DMMv1.0: If we didn't delete the scene, go ahead and update its transition */
        if (scend.chid != (CHID)iscenOld || scend.imvied != 0)
        {
            if (!scend.fNuked)
            {
                Assert(mvied.pcrf == pcrf, "Scene's MVIE didn't get copied");
                Assert(cnoScen != cnoNil, "Didn't set the cnoScen");
                if (!SCEN::FSetTransOnFile(pcrf, cnoScen, scend.trans))
                    goto LFail;
            }
        }
        else
        {
            if (scend.fNuked)
            {
                /* 3DMMv1.0: Basically, do an _FCloseCurrentScene w/out the autosave */
                SCEN::Close(&_pscenOpen);
                _iscen = ivNil;
            }
            else
            {
                /* 3DMMv1.0: If this is the scene that *used* to be Iscen(), change the
                    transition in memory rather than on file */
                Pscen()->SetTransitionCore(scend.trans);
                _iscen = iscend;
            }
        }
    }

    _cscen = chidScen;
    _aridLim = aridMin;
    SetDirty();
    if (_cscen == 0)
    {
        Assert(_iscen == ivNil, 0);
        _pmcc->SceneNuked();
    }
    InvalViewsAndScb();
    Pmcc()->UpdateRollCall();

#ifdef DEBUG
    {
        int32_t ckid = pcfl->Ckid(kctgMvie, _cno);
        KID kid;
        CHID chidLast = chidNil;

        for (int32_t ikid = 0; ikid < ckid; ikid++)
        {
            if (pcfl->FGetKid(kctgMvie, _cno, ikid, &kid))
            {
                if (kid.cki.ctg == kctgScen)
                {
                    Assert(chidLast == chidNil || kid.chid > chidLast, "Found duplicate CHID in scene children");
                    chidLast = kid.chid;
                }
            }
            else
            {
                Bug("Can't guarantee validity of MVIE's SCEN children");
                break;
            }
        }
    }
#endif /* 3DMMv1.0: DEBUG */

    fRet = fTrue;
LFail:
    if (pglmviedNew != pvNil)
    {
        /* 3DMMv1.0: Remove any copied movies; leave the first MVIED alone */
        while (imvied-- > 1)
        {
            MVIED mvied;

            pglmviedNew->Get(imvied, &mvied);
            Assert(mvied.pcrf == pcrf, "Invalid MVIED during cleanup");
            pcfl->Delete(kctgMvie, mvied.cno);
            ReleasePpo(&mvied.pcrf);
        }
        ReleasePpo(&pglmviedNew);
    }
    return fRet;
}

/** 3DMMv1.0: ****************************************************************************
    _FAddMvieToRollCall
        Updates roll call (including remapping arids for the actors found in
        the new movie) for a given MVIE that's just been copied into this
        movie's file.

    Arguments:
        CNO cno       -- the CNO of the copied movie
        long aridMin  -- the new base arid for this movie's actors

    Returns: fTrue if it succeeds, fFalse otherwise

************************************************************ PETED ***********/
bool MVIE::_FAddMvieToRollCall(CNO cno, int32_t aridMin)
{
    AssertThis(0);

    int32_t imactr, imactrMac, icnoMac = 0;
    PCFL pcfl = _pcrfAutoSave->Pcfl();
    PGST pgstmactr = pvNil;

    /* 3DMMv1.0: Update the roll call GST */
    if (!FReadRollCall(_pcrfAutoSave, cno, &pgstmactr))
    {
        pgstmactr = pvNil;
        goto LFail;
    }
    imactrMac = pgstmactr->IvMac();
    for (imactr = 0; imactr < imactrMac; imactr++)
    {
        STN stn;
        MACTR mactr;

        pgstmactr->GetStn(imactr, &stn);
        pgstmactr->GetExtra(imactr, &mactr);

        mactr.arid += aridMin;
        mactr.cactRef = 0;
        if (!_pgstmactr->FAddStn(&stn, &mactr))
            goto LFail;
    }

    /* 3DMMv1.0: Remap all the arids on the file */
    if (aridMin > 0)
    {
        uint32_t grfcge, grfcgeIn = fcgeNil;
        PGL pglcno;
        CKI ckiParLast = {ctgNil, cnoNil}, ckiPar;
        KID kid;
        CGE cge;

        if ((pglcno = GL::PglNew(SIZEOF(CNO))) == pvNil)
            goto LFail;
        cge.Init(pcfl, kctgMvie, cno);
        while (cge.FNextKid(&kid, &ckiPar, &grfcge, fcgeNil))
        {
            if (grfcge & fcgePre)
            {

                /* 3DMMv1.0: If we've found an ACTR chunk, remap its arid */
                if (kid.cki.ctg == kctgActr)
                {
                    int32_t icno;
                    CNO cnoActr;

                    /* 3DMMv1.0: Only do a given chunk once */
                    Assert(icnoMac == pglcno->IvMac(), "icnoMac isn't up-to-date");
                    for (icno = 0; icno < icnoMac; icno++)
                    {
                        pglcno->Get(icno, &cnoActr);
                        if (kid.cki.cno == cnoActr)
                            break;
                    }
                    if (icno < icnoMac)
                        continue;
                    if (!pglcno->FAdd(&kid.cki.cno))
                        goto LFail1;

                    /* 3DMMv1.0: Change the arid */
                    if (!ACTR::FAdjustAridOnFile(pcfl, kid.cki.cno, aridMin))
                    {
                        /* 3DMMv1.0: Don't bother trying to fix the arids on file; the caller
                            should be deleting the copied MVIE chunk anyway */
                    LFail1:
                        ReleasePpo(&pglcno);
                        goto LFail;
                    }
                    icnoMac++;

                    /* 3DMMv1.0: Once we're at an ACTR chunk, set up so that we don't
                        enumerate down again until returning to our parent's
                        next sibling */
                    ckiParLast = ckiPar;
                    grfcgeIn = fcgeSkipToSib;
                }
            }
            else if (grfcge & fcgePost && grfcgeIn & fcgeSkipToSib)
            {
                if (ckiParLast.ctg != ckiPar.ctg || ckiParLast.cno != ckiPar.cno)
                    grfcgeIn = fcgeNil;
            }
        }
        ReleasePpo(&pglcno);
    }

    ReleasePpo(&pgstmactr);

    return fTrue;
LFail:
    /* 3DMMv1.0: NOTE: I could use more variables and make the loops below faster,
        but this is a failure case so I'm not very concerned about
        performance here */
    if (pgstmactr != pvNil)
    {
        MACTR mactr;

        /* 3DMMv1.0: Remove added entries to the movie's roll call */
        imactrMac = _pgstmactr->IvMac();
        while (imactr--)
        {
            _pgstmactr->GetExtra(--imactrMac, &mactr);
            vptagm->CloseTag(&mactr.tagTmpl);
            _pgstmactr->Delete(imactrMac);
            pgstmactr->Delete(imactr);
        }

        /* 3DMMv1.0: Close any other uncopied tags */
        imactrMac = pgstmactr->IvMac();
        while (imactrMac--)
        {
            pgstmactr->GetExtra(imactrMac, &mactr);
            vptagm->CloseTag(&mactr.tagTmpl);
        }
        ReleasePpo(&pgstmactr);
    }
    return fFalse;
}

/** 3DMMv1.0: ****************************************************************************
    EmptyCmvi
        Frees up the memory used by the CMVI.  For each scene in the
        GL of SCENDs, releases memory that the SCEND referred to.  Likewise
        for each MVIED in the GL of MVIEDs.

    Arguments:
        PCMVI pcmvi -- the CMVI to empty

    Returns: Sets the client's pointer to pvNil when finished

************************************************************ PETED ***********/
void CMVI::Empty(void)
{
    AssertPo(pglscend, 0);
    AssertPo(pglmvied, 0);

    PGL pgl;

    if ((pgl = pglscend) != pvNil)
    {
        int32_t iscend = pgl->IvMac();

        while (iscend-- > 0)
        {
            SCEND scend;

            pgl->Get(iscend, &scend);
            ReleasePpo(&scend.pmbmp);
        }
        ReleasePpo(&pgl);
        pglscend = pvNil;
    }

    if ((pgl = pglmvied) != pvNil)
    {
        int32_t imvied = pgl->IvMac();

        while (imvied-- > 0)
        {
            MVIED mvied;

            pgl->Get(imvied, &mvied);
            ReleasePpo(&mvied.pcrf);
        }
        ReleasePpo(&pgl);
        pglmvied = pvNil;
    }
}

/** 3DMMv1.0: ****************************************************************************
    _FInsertScend
        Inserts the given SCEND into a GL of SCENDs that was created by this
        movie.

    Arguments:
        PGL pglscend  -- the GL of SCENDs to insert into
        long iscend   -- the position at which to insert this SCEND
        PSCEND pscend -- the SCEND to insert

    Returns: fTrue if successful, fFalse otherwise

************************************************************ PETED ***********/
bool MVIE::_FInsertScend(PGL pglscend, int32_t iscend, PSCEND pscend)
{
    AssertPo(pglscend, 0);
    AssertPo(pscend->pmbmp, 0);

    if (!pglscend->FInsert(iscend, pscend))
        return fFalse;
    pscend->pmbmp->AddRef();
    return fTrue;
}

/** 3DMMv1.0: ****************************************************************************
    _DeleteScend
        Deletes the given SCEND from a GL of SCENDs that was created by this
        movie.

    Arguments:
        PGL pglscend -- the GL of SCENDs to delete from
        long iscend  -- which SCEND to delete

************************************************************ PETED ***********/
void MVIE::_DeleteScend(PGL pglscend, int32_t iscend)
{
    AssertPo(pglscend, 0);
    AssertIn(iscend, 0, pglscend->IvMac());

    SCEND scend;

    pglscend->Get(iscend, &scend);
    AssertPo(scend.pmbmp, 0);
    pglscend->Delete(iscend);
    ReleasePpo(&scend.pmbmp);
}

/** 3DMMv1.0: *************************************************************************
 *
 * This sets the transition type for the current scene.
 *
 * Parameters:
 *	trans - The transition type.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool MVIE::FSetTransition(TRANS trans)
{
    AssertThis(0);
    AssertIn(trans, 0, transLim);
    AssertPo(Pscen(), 0);

    if (!Pscen()->FSetTransition(trans))
    {
        return (fFalse);
    }

    SetDirty();
    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * This pastes an actor into the movie.
 *
 * Parameters:
 *	pactr - A pointer to the actor to paste.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool MVIE::FPasteActr(PACTR pactr, bool fInPlace)
{
    AssertThis(0);
    AssertPo(pactr, 0);
    AssertPo(Pscen(), 0);

    PMVU pmvu;

    pmvu = (PMVU)PddgGet(0);
    if (pmvu == pvNil)
    {
        return (fFalse);
    }
    AssertPo(pmvu, 0);

    //
    // 3DMMv1.0: Paste this actor in and select it.
    //
    if (!Pscen()->FPasteActr(pactr, fInPlace))
    {
        return (fFalse);
    }

    Pscen()->SelectActr(pactr);

    if (fInPlace)
    {
        // Clipboard copies now finish at the source transform immediately.
        // The clipboard layer creates the undo record after the copied actor
        // has received its final roll-call name.
        pmvu->SetTool(toolCompose);
        Pmcc()->ChangeTool(toolCompose);
        Pmcc()->EnableActorTools();
    }
    else
    {
        // Positioning a newly inserted actor must translate all subroutes.
        pmvu->StartPlaceActor(fTrue);
    }

    SetDirty();
    InvalViewsAndScb();

    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * This pastes an actor path onto the selected actor.
 *
 * Parameters:
 *	pactr - A pointer to the actor to paste from.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool MVIE::FPasteActrPath(PACTR pactr)
{
    AssertThis(0);
    AssertPo(pactr, 0);
    AssertPo(Pscen(), 0);
    AssertNilOrPo(Pscen()->PactrSelected(), 0);

    PACTR pactrDup;

    // 3DMMv1.0: REVIEW SeanSe(seanse): This is wrong.  Move the undo stuff into
    // 3DMMv1.0:     ACTR::FPasteRte() and make ACTR::FPasteRte into FPasteRteCore.
    if (Pscen()->PactrSelected() == pvNil)
    {
        PushErc(ercSocNoActrSelected);
        return (fFalse);
    }

    if (!Pscen()->PactrSelected()->FDup(&pactrDup))
    {
        return (fFalse);
    }

    //
    // 3DMMv1.0: Paste this actor in and select it.
    //
    if (!Pscen()->PactrSelected()->FPasteRte(pactr))
    {
        ReleasePpo(&pactrDup);
        return (fFalse);
    }

    if (!Pscen()->PactrSelected()->FCreateUndo(pactrDup))
    {
        Pscen()->PactrSelected()->Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return (fFalse);
    }

    SetDirty();
    InvalViewsAndScb();
    ReleasePpo(&pactrDup);

    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * This invalidates all the views on the movie.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MVIE::InvalViews(void)
{
    AssertThis(0);

    int32_t ipddg;
    PDDG pddg;

    for (ipddg = 0; pvNil != (pddg = PddgGet(ipddg)); ipddg++)
    {
        pddg->InvalRc(pvNil, pddg == PddgActive() ? kginMark : kginSysInval);
    }
}

/** 3DMMv1.0: *************************************************************************
 *
 * This invalidates scrollbars and all the views on the movie.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MVIE::InvalViewsAndScb(void)
{
    AssertThis(0);

    _pmcc->UpdateScrollbars();
    InvalViews();
}

/** 3DMMv1.0: *************************************************************************
 *
 * This updates all the views on the movie, updates will happen
 * asap.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MVIE::MarkViews(void)
{
    AssertThis(0);

    int32_t ipddg;
    PDDG pddg;

    //
    // 3DMMv1.0: Need this call in order to mark correctly the changed regions.
    //
    Pbwld()->Render();

#ifdef DEBUG
    if (FWriteBmps())
    {
        FNI fni;
        STN stn;

        if (stn.FFormatSz(PszLit("cel%04d.dib"), _lwBmp++))
        {
            if (fni.FBuildFromPath(&stn))
            {
                if (!Pbwld()->FWriteBmp(&fni))
                    SetFWriteBmps(fFalse);
            }
        }
    }
#endif // 3DMMv1.0: DEBUG

    for (ipddg = 0; pvNil != (pddg = PddgGet(ipddg)); ipddg++)
    {
        Pbwld()->MarkRenderedRegn(pddg, 0, 0);
    }
}

/** 3DMMv1.0: *************************************************************************
 *
 * This returns the current name of the movie.
 *
 * Parameters:
 *	pstnTitle - An stn to copy the name into.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MVIE::GetName(PSTN pstnTitle)
{
    AssertThis(0);
    AssertPo(pstnTitle, 0);

    *pstnTitle = _stnTitle;
}

/** 3DMMv1.0: ****************************************************************************
    ResetTitle
        Resets the movie title to whatever it's normal default would be (either
        from the filename or from the MCC string table).
************************************************************ PETED ***********/
void MVIE::ResetTitle(void)
{
    AssertThis(0);

    FNI fni;

    _stnTitle.SetNil();
    _SetTitle(FGetFni(&fni) ? &fni : pvNil);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Updates the external action menu.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MVIE::BuildActionMenu()
{
    AssertThis(0);
    int32_t arid = aridNil;

    if (pvNil != _pscenOpen && pvNil != _pscenOpen->PactrSelected())
    {
        arid = _pscenOpen->PactrSelected()->Arid();
    }
    _pmcc->ActorSelected(arid);
    _pmcc->UpdateAction();
}

const int32_t kdtsTrans = 4 * kdtsSecond;

/** 3DMMv1.0: *************************************************************************
 *
 * Does a transition.  Note that the rectangles should be the exact size
 * of the rendered area in order to get pushing, dissolve, etc to look perfect.
 *
 * Note: A cool fun number fact.  If you take a number from 1 - (klwPrime - 1),
 * multiply by klwPrimeRoot and modulo the result by klwPrime, you will get
 * a sequence of numbers which hits every number 1->(klwPrime-1) w/o repeating
 * until every number is hit.  This fact is used to create the dissolve effect.
 *
 * Parameters:
 *	pgnvDst - The destination GNV.
 *	pgnvSrc - The source GNV.
 *	prcDst - The clipping rectangle in the destination.
 *	prcSrc - The clipping rectangle in the source.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MVIE::DoTrans(PGNV pgnvDst, PGNV pgnvSrc, RC *prcDst, RC *prcSrc)
{
    AssertThis(0);
    AssertPo(pgnvDst, 0);
    AssertPo(pgnvSrc, 0);
    AssertVarMem(prcDst);
    AssertVarMem(prcSrc);

    PGL pglclrSystem = pvNil;
    PGL pglclrBkgd = pvNil;
    int32_t iclrMin;

    // The original transition path is palette-driven.  In particular, an
    // in-scene background change sets transCut solely so the next draw can
    // install the new 8-bit background palette.  -c has no indexed movie
    // palette to install: the new background has already been rendered into
    // RGB888 buffers by BWLD.  Running the legacy palette transaction anyway
    // makes a valid true-colour frame pass through SetActiveColors / palette
    // bookkeeping immediately after it is rendered.  Keep true-colour
    // transitions deliberately conservative for now: preserve transBlack,
    // and treat the other visual transitions as direct RGB copies.
    if (BWLD::FTrueColorMode())
    {
        if (_trans == transBlack)
            pgnvDst->FillRc(prcDst, kacrBlack);
        else
            pgnvDst->CopyPixels(pgnvSrc, prcSrc, prcDst);

        GPT::Flush();
        _trans = transNil;
        return;
    }

    pglclrSystem = GPT::PglclrGetPalette();
    if (Pscen() == pvNil || !Pscen()->Pbkgd()->FGetPalette(&pglclrBkgd, &iclrMin))
    {
        pglclrBkgd = pvNil;
    }
    if (pvNil != pglclrSystem && pvNil != pglclrBkgd)
    {
        Assert(pglclrBkgd->IvMac() + iclrMin <= pglclrSystem->IvMac(), "Background palette too large");
        CopyPb(pglclrBkgd->QvGet(0), pglclrSystem->QvGet(iclrMin), LwMul(SIZEOF(CLR), pglclrBkgd->IvMac()));
    }

    switch (_trans)
    {
    case transBlack:
        pgnvDst->FillRc(prcDst, kacrBlack);
        break;

    case transFadeToBlack:
        pgnvDst->Dissolve(0, 0, kacrBlack, pgnvSrc, prcSrc, prcDst, kdtsTrans / 2, pglclrSystem);
        break;

    case transFadeToWhite:
        pgnvDst->Dissolve(0, 0, kacrWhite, pgnvSrc, prcSrc, prcDst, kdtsTrans / 2, pglclrSystem);
        break;

    case transDissolve:
        pgnvDst->Dissolve(0, 0, kacrClear, pgnvSrc, prcSrc, prcDst, kdtsTrans, pglclrSystem);
        break;

    case transCut:
        pgnvDst->FillRc(prcDst, kacrBlack);
        GPT::SetActiveColors(pglclrSystem, fpalIdentity);
        pgnvDst->CopyPixels(pgnvSrc, prcSrc, prcDst);
        break;

    default:
        Bug("bad trans");
        break;
    }

    ReleasePpo(&pglclrSystem);
    ReleasePpo(&pglclrBkgd);

    _trans = transNil;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Used to query if an actor or tbox exists.
 *
 * Parameters:
 *	lwType - 1 if searching for an actor, else 0.
 *  lwId - Arid or Itbox to search for.
 *
 * Returns:
 *  1 if exists, else 0.
 *
 **************************************************************************/
int32_t MVIE::LwQueryExists(int32_t lwType, int32_t lwId)
{
    AssertThis(0);
    AssertIn(lwType, 0, 2);

    if (Pscen() == pvNil)
    {
        return (0);
    }
    AssertPo(Pscen(), 0);

    if (lwType == 1)
    {
        return (Pscen()->PactrFromArid(lwId) != pvNil ? 1 : 0);
    }

    return (Pscen()->PtboxFromItbox(lwId) != pvNil ? 1 : 0);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Used to query where an actor or tbox exists.
 *
 * Parameters:
 *	lwType - 1 if searching for an actor, else 0.
 *  lwId - Arid or Itbox to search for.
 *
 * Returns:
 *  -1 if nonexistent or non-visible, else x in high word, y in low word.
 *
 **************************************************************************/
int32_t MVIE::LwQueryLocation(int32_t lwType, int32_t lwId)
{
    AssertThis(0);
    AssertIn(lwType, 0, 2);

    PACTR pactr;
    PTBOX ptbox;
    int32_t xp, yp;
    RC rc;

    if (Pscen() == pvNil)
    {
        return (-1);
    }
    AssertPo(Pscen(), 0);

    if (lwType == 1)
    {
        RC rcBounds;
        int32_t cactGuessPt = 0;
        RND rnd;
        int32_t ibset;

        pactr = Pscen()->PactrFromArid(lwId);
        if (pactr == pvNil)
        {
            return (-1);
        }

        AssertPo(pactr, 0);
        if (!pactr->FIsInView())
        {
            return (-1);
        }

        pactr->GetCenter(&xp, &yp);
        pactr->GetRcBounds(&rcBounds);
        // 3DMMv1.0: The center of the actor may not be a selectable point (as in
        // 3DMMv1.0: an arched spletter), so try some random points if the center
        // 3DMMv1.0: point doesn't select the actor.
        while (pactr != Pscen()->PactrFromPt(xp, yp, &ibset) && cactGuessPt < 1000)
        {
            cactGuessPt++;
            xp = rnd.LwNext(rcBounds.Dxp() + rcBounds.xpLeft);
            yp = rnd.LwNext(rcBounds.Dyp() + rcBounds.ypTop);
        }
        if (cactGuessPt == 1000)
        {
            xp = -1;
            yp = -1;
        }
        return ((xp << 16) | yp);
    }

    ptbox = Pscen()->PtboxFromItbox(lwId);
    if (ptbox == pvNil)
    {
        return (-1);
    }

    AssertPo(ptbox, 0);
    if (!ptbox->FIsVisible())
    {
        return (-1);
    }

    ptbox->GetRc(&rc);
    return ((rc.xpLeft << 16) | rc.ypTop);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Used to set the position within the movie.
 *
 * Parameters:
 *	lwScene - Scene to go to.
 *  lwFrame - Frame to go to.
 *
 * Returns:
 *  0 if successful, else -1.
 *
 **************************************************************************/
int32_t MVIE::LwSetMoviePos(int32_t lwScene, int32_t lwFrame)
{
    AssertThis(0);

    if (!FSwitchScen(lwScene))
    {
        return (-1);
    }

    if (!Pscen()->FGotoFrm(lwFrame))
    {
        return (-1);
    }

    InvalViewsAndScb();

    return (0);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Unused user sounds in this movie?
 *
 * Parms:
 *     bool pfHaveValid -- bool to take whether any of the unused sounds were
 *                         actually usable
 **************************************************************************/

bool MVIE::FUnusedSndsUser(bool *pfHaveValid)
{
    AssertThis(0);
    AssertNilOrVarMem(pfHaveValid);

    bool fUnused = fFalse;
    int32_t icki, ccki;
    PCFL pcfl;

    if (pfHaveValid != pvNil)
        *pfHaveValid = fFalse;

    if (pvNil == _pcrfAutoSave)
        return fFalse;

    pcfl = _pcrfAutoSave->Pcfl();
    ccki = pcfl->CckiCtg(kctgMsnd);
    for (icki = 0; icki < ccki; icki++)
    {
        CKI cki;
        KID kid;

        AssertDo(pcfl->FGetCkiCtg(kctgMsnd, icki, &cki), "Should never fail");
        Assert(_FIsChild(pcfl, cki.ctg, cki.cno), "Not a child of MVIE chunk");
        if (pcfl->CckiRef(cki.ctg, cki.cno) < 2)
        {
            fUnused = fTrue;
            if (pfHaveValid != pvNil)
            {
                if (pcfl->FGetKidChid(cki.ctg, cki.cno, kchidSnd, &kid))
                {
                    *pfHaveValid = fTrue;
                    break;
                }
            }
            else
                break;
        }
    }
    return fUnused;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Used to set the title of the movie, based on a file name.
 *
 * Parameters:
 *  pfni - File name.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MVIE::_SetTitle(PFNI pfni)
{
    AssertThis(0);

    achar *pch;

    if (pfni == pvNil)
    {
        if (_stnTitle.Cch() == 0)
        {
            Pmcc()->GetStn(idsEngineDefaultTitle, &_stnTitle);
            Pmcc()->UpdateTitle(&_stnTitle);
        }
        return;
    }

    pfni->GetLeaf(&_stnTitle);

    for (pch = _stnTitle.Psz() + _stnTitle.Cch(); (pch > _stnTitle.Psz()) && (*pch != ChLit('.')); pch--)
    {
    }

    if (*pch == ChLit('.'))
    {
        _stnTitle.Delete(pch - _stnTitle.Psz());
    }

    if (_stnTitle.Cch() == 0)
    {
        Pmcc()->GetStn(idsEngineDefaultTitle, &_stnTitle);
    }

    Pmcc()->UpdateTitle(&_stnTitle);
}

//
//
//
// 3DMMv1.0:  BEGIN MVU GOODIES
//
//
//

BEGIN_CMD_MAP(MVU, DDG)
ON_CID_GEN(cidCopyRoute, &MVU::FCmdClip, pvNil)
ON_CID_GEN(cidCutTool, &MVU::FCmdClip, pvNil)
ON_CID_GEN(cidShiftCut, &MVU::FCmdClip, pvNil)
ON_CID_GEN(cidCopyTool, &MVU::FCmdClip, pvNil)
ON_CID_GEN(cidShiftCopy, &MVU::FCmdClip, pvNil)
ON_CID_GEN(cidPasteTool, &MVU::FCmdClip, pvNil)
ON_CID_GEN(cidClose, pvNil, pvNil)
ON_CID_GEN(cidSave, &MVU::FCmdSave, pvNil)
ON_CID_GEN(cidSaveAs, &MVU::FCmdSave, pvNil)
ON_CID_GEN(cidSaveCopy, &MVU::FCmdSave, pvNil)
ON_CID_GEN(cidIdle, &MVU::FCmdIdle, pvNil)
ON_CID_GEN(cidRollOff, &MVU::FCmdRollOff, pvNil)
ON_CID_GEN(cidLightLab, &MVU::FCmdLightLab, pvNil)
END_CMD_MAP_NIL()

RTCLASS(MVU)

/** 3DMMv1.0: **************************************************
 *
 * Destructor for movie view objects
 *
 ****************************************************/
MVU::~MVU(void)
{
    EndManualCameraInput();
    if (_tagTool.sid != ksidInvalid)
        TAGM::CloseTag(&_tagTool);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Create a new mvu.
 *
 * Parameters:
 *	pmvie - Pointer to the creating movie.
 *  pgcb - Pointer to the creation block describing placement, etc.
 *	dxp - Width of the rendered area.
 *	dyp - Height of the rendered area.
 *
 * Returns:
 *  A pointer to the view, otw pvNil on failure
 *
 ***************************************************************************/
MVU *MVU::PmvuNew(PMVIE pmvie, PGCB pgcb, int32_t dxp, int32_t dyp)
{
    AssertPo(pmvie, 0);
    AssertVarMem(pgcb);

    MVU *pmvu;
    BRS rgr[3][3] = {{rOne, rZero, rZero}, {rZero, rZero, rOne}, {rZero, -rOne, rZero}};

    //
    // 3DMMv1.0: Create the new view
    //
    if ((pmvu = NewObj MVU(pmvie, pgcb)) == pvNil)
        return pvNil;

    //
    // 3DMMv1.0: Init it
    //
    if (!pmvu->_FInit())
    {
        ReleasePpo(&pmvu);
        return (pvNil);
    }

    CopyPb(rgr, pmvu->_rgrAxis, SIZEOF(rgr));
    pmvu->_fRecordDefault = fTrue;
    pmvu->_fRespectGround = fFalse;
    pmvu->_fContinueInPlace = fFalse;
    pmvu->_fManualCameraMousePrimed = fFalse;
    pmvu->_fManualCameraCaptured = fFalse;
    pmvu->_fManualCameraFly = fFalse;
    pmvu->_fManualCameraShiftDown = fFalse;
    pmvu->_fManualCameraCtrlDown = fFalse;
    pmvu->_fManualCameraCtrlChordUsed = fFalse;
    pmvu->_fManualCameraDDown = fFalse;
    pmvu->_fManualCameraRDown = fFalse;
    pmvu->_fManualCameraEnterDown = fFalse;
    pmvu->_fManualCameraEDown = fFalse;
    pmvu->_fManualCameraLeftDown = fFalse;
    pmvu->_fManualCameraRightDown = fFalse;
    pmvu->_fFreeLookFDown = fFalse;
    pmvu->_fFreeLookLButtonDown = fFalse;
    pmvu->_fDeselectEscapeDown = fFalse;
    pmvu->_fMultiDeselectClick = fFalse;
    pmvu->_xpManualCameraPrev = 0;
    pmvu->_ypManualCameraPrev = 0;
    pmvu->_tsManualCameraLast = 0;
    pmvu->_dxp = dxp;
    pmvu->_dyp = dyp;
    pmvu->_tool = toolCompose;
    pmvu->_tagTool.sid = ksidInvalid;

    //
    // 3DMMv1.0: Make this the active view
    //
    pmvu->Activate(fTrue);
    return pmvu;
}

/******************************************************************************
    ResetManualCameraInput
******************************************************************************/
void MVU::ResetManualCameraInput(void)
{
    _fManualCameraMousePrimed = fFalse;
    _fManualCameraFly = fFalse;
    _fManualCameraShiftDown = fFalse;
    _fManualCameraCtrlDown = fFalse;
    _fManualCameraCtrlChordUsed = fFalse;
    _fManualCameraDDown = fFalse;
    _fManualCameraRDown = fFalse;
    _fManualCameraEnterDown = fFalse;
    _fManualCameraEDown = fFalse;
    _fManualCameraLeftDown = fFalse;
    _fManualCameraRightDown = fFalse;
    _fFreeLookFDown = fFalse;
    _fFreeLookLButtonDown = fFalse;
    _tsManualCameraLast = 0;
}

/******************************************************************************
    BeginManualCameraInput

    Capture the cursor inside the viewport, hide it, and center it so idle
    processing can consume relative mouse deltas like an FPS camera.
******************************************************************************/
void MVU::BeginManualCameraInput(void)
{
    AssertThis(0);

    ResetManualCameraInput();

#if defined(KAUAI_WIN32)
    HWND hwndContainer = HwndContainer();
    if (_fManualCameraCaptured || hwndContainer == hNil)
        return;
    HWND hwndInput = Hwnd4DMMScaledInput(hwndContainer);

    RECT rcClient;
    POINT ptTopLeft;
    POINT ptBottomRight;
    if (!GetClientRect(hwndInput, &rcClient))
        return;

    ptTopLeft.x = rcClient.left;
    ptTopLeft.y = rcClient.top;
    ptBottomRight.x = rcClient.right;
    ptBottomRight.y = rcClient.bottom;
    ClientToScreen(hwndInput, &ptTopLeft);
    ClientToScreen(hwndInput, &ptBottomRight);

    RECT rcCapture;
    rcCapture.left = ptTopLeft.x;
    rcCapture.top = ptTopLeft.y;
    rcCapture.right = ptBottomRight.x;
    rcCapture.bottom = ptBottomRight.y;

    SetFocus(hwndInput);
    SetCapture(hwndInput);
    ClipCursor(&rcCapture);
    vpappb->HideCurs();
    _fManualCameraCaptured = fTrue;
    vtsCameraInputPerfStart = TsCurrent();
    vcCameraInputPerfIdle = 0;
    vcCameraInputPerfLook = 0;
    vcCameraInputPerfRender = 0;
    vcCameraInputPerfPaintFlush = 0;
    vcCameraInputPerfPaintPending = 0;
    vdtsCameraInputPerfRender = 0;
    vdtsCameraInputPerfPaint = 0;
    vdtsCameraInputPerfRenderMax = 0;
    vdtsCameraInputPerfPaintMax = 0;
    vdxCameraInputPerfMax = 0;
    vdyCameraInputPerfMax = 0;
    MVIE::MultiLog(Pmvie(),
        "camera_input begin backend=coalesced_cursor mode=%s hwnd_container=%p hwnd_input=%p",
        Pmvie()->FFreeLookMode() ? "freecam" : "manual", hwndContainer, hwndInput);

    int32_t xpCenter = (rcCapture.left + rcCapture.right) / 2;
    int32_t ypCenter = (rcCapture.top + rcCapture.bottom) / 2;
    if (hwndInput != hwndContainer)
        SetCursorPos(xpCenter, ypCenter);
    else
        vpappb->PositionCurs(xpCenter, ypCenter);
    _fManualCameraShiftDown = GetAsyncKeyState(VK_SHIFT) < 0;
    _fManualCameraCtrlDown = GetAsyncKeyState(VK_CONTROL) < 0;
    _fManualCameraCtrlChordUsed = fFalse;
    _fManualCameraDDown = GetAsyncKeyState('D') < 0;
    _fManualCameraRDown = GetAsyncKeyState('R') < 0;
    _fFreeLookFDown = GetAsyncKeyState('F') < 0;
    _fFreeLookLButtonDown = GetAsyncKeyState(VK_LBUTTON) < 0;
#endif
}

/******************************************************************************
    EndManualCameraInput
******************************************************************************/
void MVU::EndManualCameraInput(void)
{
    AssertThis(0);

#if defined(KAUAI_WIN32)
    if (_fManualCameraCaptured)
    {
        const uint32_t tsEnd = TsCurrent();
        MVIE::MultiLog(Pmvie(),
            "camera_input end backend=coalesced_cursor mode=%s elapsed_ms=%lu idle=%lu look=%lu renders=%lu render_ms=%lu render_max_ms=%lu paint_pending=%lu paint_flush=%lu paint_ms=%lu paint_max_ms=%lu max_delta=%ldx%ld",
            Pmvie()->FFreeLookMode() ? "freecam" : "manual",
            (unsigned long)(vtsCameraInputPerfStart != 0 ? tsEnd - vtsCameraInputPerfStart : 0),
            (unsigned long)vcCameraInputPerfIdle, (unsigned long)vcCameraInputPerfLook,
            (unsigned long)vcCameraInputPerfRender,
            (unsigned long)vdtsCameraInputPerfRender, (unsigned long)vdtsCameraInputPerfRenderMax,
            (unsigned long)vcCameraInputPerfPaintPending, (unsigned long)vcCameraInputPerfPaintFlush,
            (unsigned long)vdtsCameraInputPerfPaint, (unsigned long)vdtsCameraInputPerfPaintMax,
            (long)vdxCameraInputPerfMax, (long)vdyCameraInputPerfMax);
        ClipCursor(NULL);
        HWND hwndContainer = HwndContainer();
        HWND hwndInput = Hwnd4DMMScaledInput(hwndContainer);
        if (GetCapture() == hwndInput || GetCapture() == hwndContainer)
            ReleaseCapture();
        vpappb->ShowCurs();
        _fManualCameraCaptured = fFalse;
    }
#endif

    ResetManualCameraInput();
}

/** 3DMMv1.0: *************************************************************************
 *
 * Set the tool type
 *
 * Parameters:
 *	tool - The new tool to use.
 *
 * Returns:
 *  None.
 *
 ***************************************************************************/
void MVU::SetTool(int32_t tool)
{
    AssertThis(0);
    AssertPo(Pmvie(), 0);
    AssertNilOrPo(Pmvie()->Pscen(), 0);

    int32_t lwMode; // 3DMMv1.0: -1 = Textbox mode, 0 = either mode, 1 = Actor mode
    PTBOX ptbox = pvNil;
    PACTR pactr = pvNil;

    if (Pmvie()->Pscen() != pvNil)
    {

        AssertNilOrPo(Pmvie()->Pscen()->PactrSelected(), 0);
        AssertNilOrPo(Pmvie()->Pscen()->PtboxSelected(), 0);

        pactr = Pmvie()->Pscen()->PactrSelected();
        ptbox = Pmvie()->Pscen()->PtboxSelected();

        if (pactr != pvNil)
        {
            _ptmplTool = pactr->Ptmpl();
            AssertPo(_ptmplTool, 0);
        }
    }
    else
    {
        _ptmplTool = pvNil;
    }

    //
    // 3DMMv1.0: Get old tool type
    //
    switch (_tool)
    {
    case toolSoonerLater:

        lwMode = 1;
        if (tool != toolSoonerLater)
        {
            Pmvie()->Pmcc()->EndSoonerLater();

            if ((pactr != pvNil) && pactr->FTimeFrozen())
            {
                pactr->SetTimeFreeze(fFalse);
                pactr->Hilite();
                Pmvie()->InvalViewsAndScb();
            }
        }
        break;

    case toolActorNuke:
    case toolCopyObject:
    case toolCopyRte:
    case toolCutObject:
    case toolPasteObject:
        lwMode = 0;
        break;

    case toolSceneNuke:
    case toolDefault:
    case toolSounder:
    case toolLooper:
    case toolMatcher:
    case toolListener:
    case toolSceneChop:
    case toolSceneChopBack:
    case toolAction:
    case toolActorSelect:
    case toolPlace:
    case toolCompose:
    case toolRecordSameAction:
    case toolRotateX:
    case toolRotateY:
    case toolRotateZ:
    case toolCostumeCmid:
    case toolSquashStretch:
    case toolResize:
    case toolNormalizeRot:
    case toolNormalizeSize:
    case toolActorEasel:

        lwMode = 1;
        break;

    case toolTboxMove:
    case toolTboxUpDown:
    case toolTboxLeftRight:
    case toolTboxFalling:
    case toolTboxRising:
    case toolTboxPaintText:
    case toolTboxFillBkgd:
    case toolTboxStory:
    case toolTboxCredit:
    case toolTboxFont:
    case toolTboxStyle:
    case toolTboxSize:
        lwMode = -1;
        break;

    default:
        Bug("Unknown tool type");
    }

    //
    // 3DMMv1.0: Check if we've changed primary tool type
    //
    switch (tool)
    {

    case toolActorNuke:
    case toolCopyObject:
    case toolCopyRte:
    case toolCutObject:
    case toolPasteObject:
        break;

    case toolSoonerLater:
        _fMouseDownSeen = fFalse;

    case toolSceneNuke:
    case toolDefault:
    case toolSounder:
    case toolLooper:
    case toolMatcher:
    case toolListener:
    case toolSceneChop:
    case toolSceneChopBack:
    case toolAction:
    case toolActorSelect:
    case toolPlace:
    case toolCompose:
    case toolRecordSameAction:
    case toolRotateX:
    case toolRotateY:
    case toolRotateZ:
    case toolCostumeCmid:
    case toolSquashStretch:
    case toolResize:
    case toolNormalizeRot:
    case toolNormalizeSize:
    case toolActorEasel:

        _fTextMode = fFalse;

        if (lwMode > 0)
        {
            break;
        }

        if (Pmvie()->Pscen() != pvNil)
        {
            Pmvie()->Pscen()->SelectActr(pactr);
        }
        break;

    case toolTboxMove:
    case toolTboxUpDown:
    case toolTboxLeftRight:
    case toolTboxFalling:
    case toolTboxRising:
    case toolTboxStory:
    case toolTboxCredit:
    case toolTboxFillBkgd:

        _fTextMode = fTrue;

        if (lwMode < 0)
        {
            break;
        }

    LSelectTbox:
        if (Pmvie()->Pscen() != pvNil)
        {
            Pmvie()->Pscen()->SelectTbox(ptbox);
        }
        break;

    case toolTboxPaintText:

        _fTextMode = fTrue;

        if ((lwMode < 0) && (ptbox != pvNil))
        {
            ptbox->FSetAcrText(AcrPaint());
            ptbox->Pscen()->Pmvie()->Pmcc()->PlayUISound(tool);
            break;
        }

        goto LSelectTbox;

    case toolTboxFont:
        _fTextMode = fTrue;
        if ((lwMode < 0) && (ptbox != pvNil))
        {
            ptbox->FSetOnnText(OnnTextCur());
            ptbox->Pscen()->Pmvie()->Pmcc()->PlayUISound(tool);
            break;
        }
        goto LSelectTbox;

    case toolTboxSize:
        _fTextMode = fTrue;
        if ((lwMode < 0) && (ptbox != pvNil))
        {
            ptbox->FSetDypFontText(DypFontTextCur());
            ptbox->Pscen()->Pmvie()->Pmcc()->PlayUISound(tool);
            break;
        }
        goto LSelectTbox;

    case toolTboxStyle:
        _fTextMode = fTrue;
        if ((lwMode < 0) && (ptbox != pvNil))
        {
            ptbox->FSetStyleText(GrfontStyleTextCur());
            ptbox->Pscen()->Pmvie()->Pmcc()->PlayUISound(tool);
            break;
        }
        goto LSelectTbox;

    default:
        Bug("Unknown tool type");
    }

    _tool = tool;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Change the current loaded tag attached to the cursor.  If the previous
 * tag was a "ksidUseCrf" tag, it must be closed to release its refcount on
 * the tag's pcrf.
 *
 * Parameters:
 *	ptag - The new tag to attach to the cursor
 *
 * Returns:
 *  None.
 *
 ***************************************************************************/
void MVU::SetTagTool(PTAG ptag)
{
    AssertThis(0);
    AssertVarMem(ptag);

    if (_tagTool.sid != ksidInvalid)
    {
        TAGM::CloseTag(&_tagTool);
    }

#ifdef DEBUG
    // 3DMMv1.0: Make sure the new tag has been opened, if it's a "ksidUseCrf" tag
    if (ptag->sid == ksidUseCrf)
    {
        AssertPo(ptag->pcrf, 0);
    }
#endif

    _tagTool = *ptag;
    if (_tagTool.sid != ksidInvalid)
        TAGM::DupTag(ptag);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Draw this view.
 *
 * Parameters:
 *	pgnv - The environment to write to.
 *  prcClip - The clipping rectangle.
 *
 * Returns:
 *  None.
 *
 ***************************************************************************/
void MVU::Draw(PGNV pgnv, RC *prcClip)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    AssertVarMem(prcClip);

    RC rcDest;

    //
    // 3DMMv1.0: Clear non-rendering areas
    //
    if (prcClip->xpRight > _dxp)
    {
        rcDest = *prcClip;
        rcDest.xpLeft = _dxp;
        pgnv->FillRc(&rcDest, kacrWhite);
    }
    if (prcClip->ypBottom > _dyp)
    {
        rcDest = *prcClip;
        rcDest.ypTop = _dyp;
        pgnv->FillRc(&rcDest, kacrWhite);
    }

    //
    // 3DMMv1.0: Render
    //
    if (Pmvie()->Pscen() != pvNil)
    {
        Pmvie()->Pbwld()->Render();
        Pmvie()->Pbwld()->Draw(pgnv, prcClip, 0, 0);

        //
        // 3DMMv1.0: This draws a currently being dragged out text box frame.
        //
        if (!_rcFrame.FEmpty())
        {
            pgnv->FrameRcApt(&_rcFrame, &vaptLtGray, kacrBlack, kacrWhite);
        }
    }
    else
    {
        rcDest.Set(0, 0, _dxp, _dyp);
        pgnv->FillRc(&rcDest, kacrBlack);
    }
}

/** 3DMMv1.0: *************************************************************************
 *
 * Warps the cursor to the center of this gob.  Also sets _xpPrev and
 * _ypPrev so that future mouse deltas are from the center.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *	None.
 *
 **************************************************************************/
static PMVU vpmvu4DMMHandWarpDiag = pvNil;
static int32_t vc4DMMHandWarpDiagRemaining = 0;
static int32_t vxp4DMMHandWarpInput = 0;
static int32_t vyp4DMMHandWarpInput = 0;
static int32_t vxp4DMMHandWarpPrev = 0;
static int32_t vyp4DMMHandWarpPrev = 0;
static uint32_t vts4DMMHandWarp = 0;

void MVU::WarpCursToCenter(void)
{
    AssertThis(0);

    PT pt;

    _xpPrev = _dxp / 2;
    _ypPrev = _dyp / 2;
    _dzrPrev = rZero;
    pt.xp = _xpPrev;
    pt.yp = _ypPrev;
    MapPt(&pt, cooLocal, cooGlobal);
    vpappb->PositionCurs(pt.xp, pt.yp);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Warps the cursor to the center of the given actor.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *	None.
 *
 **************************************************************************/
void MVU::WarpCursToActor(PACTR pactr)
{
    AssertThis(0);
    AssertPo(pactr, 0);

    PT pt;

    pactr->GetCenter(&pt.xp, &pt.yp);
    MapPt(&pt, cooLocal, cooGlobal);
    vpappb->PositionCurs(pt.xp, pt.yp);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Call this function when you have the cursor hidden and you want to
 * "reset" its position after using the mouse position to adjust an actor.
 * It updates _xpPrev and _ypPrev.  Then, if the cursor has gone outside
 * the gob, it warps the cursor to the center of the gob.  This way, the
 * actor doesn't seem to hit an invisible "wall" just because the (hidden)
 * cursor has hit the edge of the screen.
 *
 * Parameters:
 *	(xp, yp): current cursor position
 *
 * Returns:
 *	None.
 *
 **************************************************************************/
void MVU::AdjustCursor(int32_t xp, int32_t yp)
{
    AssertThis(0);

    RC rc;

    GetRc(&rc, cooLocal);
    rc.Inset(kdpInset, kdpInset); // 3DMMv1.0: warp before the cursor gets close to the gob's edge
    if (rc.FPtIn(xp, yp))
    {
        _xpPrev = xp;
        _ypPrev = yp;
        _dzrPrev = rZero;
    }
    else
    {
        if (Tool() == toolCompose)
        {
            vpmvu4DMMHandWarpDiag = this;
            vc4DMMHandWarpDiagRemaining = 3;
            vxp4DMMHandWarpInput = xp;
            vyp4DMMHandWarpInput = yp;
            vxp4DMMHandWarpPrev = _xpPrev;
            vyp4DMMHandWarpPrev = _ypPrev;
            vts4DMMHandWarp = TsCurrent();
            MVIE::MultiLog(Pmvie(),
                "main_hand_warp edge input=%ld,%ld prev=%ld,%ld center=%ld,%ld viewport=%ldx%ld camera_modes=%d,%d,%d",
                (long)xp, (long)yp, (long)_xpPrev, (long)_ypPrev,
                (long)(_dxp / 2), (long)(_dyp / 2), (long)_dxp, (long)_dyp,
                Pmvie()->FCameraTrackActive() ? 1 : 0,
                Pmvie()->FManualCameraMode() ? 1 : 0,
                Pmvie()->FFreeLookOverride() ? 1 : 0);
        }
        WarpCursToCenter();
    }
}

/** 3DMMv1.0: *************************************************************************
 *
 * Converts from mouse coordinates to world coordinates
 *
 * Parameters:
 *	dxrMouse - BRender scalar representation of mouse X coordinate
 *	dyrMouse - BRender scalar representation of mouse Y coordinate
 *	dzrMouse - BRender scalar representation of mouse Z coordinate
 *  pdxrWld  - Place to store world X coordinate.
 *  pdyrWld  - Place to store world Y coordinate.
 *  pdzrWld  - Place to store world Z coordinate.
 *  fRecord  - Is the conversion to be scaled according to the recording
 *				scaling factor, or the non-recording scaling factor.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
static BMAT34 vbmat344DMMMouseToWorld;
static bool vf4DMMMouseToWorldLiveCamera = fFalse;

void MVU::MouseToWorld(BRS dxrMouse, BRS dyrMouse, BRS dzrMouse, BRS *pdxrWld, BRS *pdyrWld, BRS *pdzrWld, bool fRecord)
{
    AssertThis(0);
    AssertVarMem(pdxrWld);
    AssertVarMem(pdyrWld);
    AssertVarMem(pdzrWld);

    BRS dxrScr, dyrScr, dzrScr;
    BMAT34 bmat34Cam;
    BRS rScaleMouse;

    dxrScr = BR_MAC3(dxrMouse, _rgrAxis[0][0], dyrMouse, _rgrAxis[0][1], dzrMouse, _rgrAxis[0][2]);
    dyrScr = BR_MAC3(dxrMouse, _rgrAxis[1][0], dyrMouse, _rgrAxis[1][1], dzrMouse, _rgrAxis[1][2]);
    dzrScr = BR_MAC3(dxrMouse, _rgrAxis[2][0], dyrMouse, _rgrAxis[2][1], dzrMouse, _rgrAxis[2][2]);

    rScaleMouse = fRecord ? krScaleMouseRecord : krScaleMouseNonRecord;

    //
    // 3DMMv1.0: apply some scaling so that 1 pixel of mouse movement is rScaleMouse world units
    //
    dxrScr = BrsMul(dxrScr, rScaleMouse);
    dyrScr = BrsMul(dyrScr, rScaleMouse);
    dzrScr = BrsMul(dzrScr, rScaleMouse);

    Pmvie()->Pscen()->Pbkgd()->GetMouseMatrix(&bmat34Cam);

    // 3DMM's authored cameras maintain a horizontal mouse basis in BKGD,
    // but Manual Camera / Free Look alter BWLD's live camera without changing
    // that stored basis.  Rebuild the same yaw-only mouse matrix from the
    // camera that is actually being rendered so left/right and forward/back
    // dragging remain camera-relative.  Pitch and roll are deliberately
    // discarded, matching vanilla 3DMM's GetMouseMatrix behavior.
    vf4DMMMouseToWorldLiveCamera = fFalse;
    if (Pmvie()->Pbwld() != pvNil &&
        (Pmvie()->FCameraTrackActive() || Pmvie()->FManualCameraMode() ||
         Pmvie()->FFreeLookOverride()))
    {
        BMAT34 bmat34Rendered;
        BREUL breul;

        Pmvie()->Pbwld()->GetCamera(&bmat34Rendered);
        breul.order = BR_EULER_YXY_R;
        BrMatrix34ToEuler(&breul, &bmat34Rendered);
        BrMatrix34RotateY(&bmat34Cam, breul.a + breul.c);
        vf4DMMMouseToWorldLiveCamera = fTrue;
    }
    vbmat344DMMMouseToWorld = bmat34Cam;

    *pdxrWld = BR_MAC3(dxrScr, bmat34Cam.m[0][0], dyrScr, bmat34Cam.m[1][0], dzrScr, bmat34Cam.m[2][0]);
    *pdyrWld = BR_MAC3(dxrScr, bmat34Cam.m[0][1], dyrScr, bmat34Cam.m[1][1], dzrScr, bmat34Cam.m[2][1]);
    *pdzrWld = BR_MAC3(dxrScr, bmat34Cam.m[0][2], dyrScr, bmat34Cam.m[1][2], dzrScr, bmat34Cam.m[2][2]);
}

bool MVU::_fKbdDelayed = fFalse;
int32_t MVU::_dtsKbdDelay;
int32_t MVU::_dtsKbdRepeat;

/** 3DMMv1.0: *************************************************************************
 *
 * Slows down keyboard auto-repeat as much as possible.  Saves user's
 * previous setting so it can be restored in RestoreKeyboardRepeat.
 *
 * Parameters:
 *  none
 *
 * Returns
 *  none
 *
 **************************************************************************/
void MVU::SlowKeyboardRepeat(void)
{
    if (_fKbdDelayed)
        return;
#ifdef WIN
    if (!SystemParametersInfo(SPI_GETKEYBOARDDELAY, 0, &_dtsKbdDelay, fFalse))
    {
        Bug("why could this fail?");
        return;
    }
    if (!SystemParametersInfo(SPI_SETKEYBOARDDELAY, klwMax, pvNil, fFalse))
    {
        Bug("why could this fail?");
        return;
    }
    if (!SystemParametersInfo(SPI_GETKEYBOARDSPEED, 0, &_dtsKbdRepeat, fFalse))
    {
        Bug("why could this fail?");
        return;
    }
    if (!SystemParametersInfo(SPI_SETKEYBOARDSPEED, 0, pvNil, fFalse))
    {
        Bug("why could this fail?");
        return;
    }
#endif
#ifdef MAC
    RawRtn();
#endif
    _fKbdDelayed = fTrue;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Restores the keyboard auto-repeat to the user's previous setting.
 *
 * Parameters:
 *  none
 *
 * Returns
 *  none
 *
 **************************************************************************/
void MVU::RestoreKeyboardRepeat(void)
{
    if (!_fKbdDelayed)
        return;
#ifdef WIN
    if (!SystemParametersInfo(SPI_SETKEYBOARDDELAY, _dtsKbdDelay, pvNil, fFalse))
    {
        Bug("why could this fail?");
        return;
    }
    if (!SystemParametersInfo(SPI_SETKEYBOARDSPEED, _dtsKbdRepeat, pvNil, fFalse))
    {
        Bug("why could this fail?");
        return;
    }
#endif
#ifdef MAC
    RawRtn();
#endif
    _fKbdDelayed = fFalse;
}

/** 3DMMv1.0: *************************************************************************
 *
 * An actor has just been added, so enter "place actor" mode, where the
 * actor floats with the cursor.
 *
 * Parameters:
 *  fEntireScene flags whether the whole scene's route is to be translated on
 *  positioning, or whether only the current subroute is to be translated.
 *
 * Returns
 *  None.
 *
 **************************************************************************/
void MVU::StartPlaceActor(bool fEntireScene)
{
    AssertThis(0);
    AssertPo(Pmvie()->Pscen(), 0);

    PACTR pactr = Pmvie()->Pscen()->PactrSelected();

    AssertPo(pactr, 0);

    vpappb->HideCurs();
    WarpCursToCenter();

    _fEntireScene = fEntireScene;
    SetTool(toolPlace);
    Pmvie()->RemFromRollCall(pactr, fFalse);
    Pmvie()->Pmcc()->NewActor();
    vpcex->TrackMouse(this);

    // 3DMMv1.0: While tracking the mouse, don't allow the cursor out of capture window.
    // 3DMMv1.0: If we don't do this, then the user can click on another window in the
    // 3DMMv1.0: middle of the cursor tracking, (if tracking with mouse btn up). Note,
    // 3DMMv1.0: this call clips the cursor movement to an area on the screen, so we are
    // 3DMMv1.0: assuming there is no way for the capture window to move during tracking.
#ifdef WIN
    RECT rectCapture;
    HWND hwndCapture = Hwnd4DMMScaledInput(HwndContainer());
    GetWindowRect(hwndCapture, &rectCapture);
    ClipCursor(&rectCapture);
#endif // 3DMMv1.0: WIN

    _fMouseDownSeen = fFalse;
    _tsLastSample = TsCurrent();

    return;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Undoes the place tool.
 *
 * Parameters:
 *  None.
 *
 * Returns
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
void MVU::EndPlaceActor()
{
    AssertThis(0);
    AssertPo(Pmvie()->Pscen(), 0);

    if (Tool() != toolPlace)
    {
        return;
    }

    vpappb->ShowCurs();
    WarpCursToCenter();
    SetTool(toolCompose);
    PACTR pactr = Pmvie()->Pscen()->PactrSelected();
    if (!Pmvie()->F4DMMActorStudioConsumePreaddedPlacement(pactr))
        AssertDo(Pmvie()->FAddToRollCall(pactr, pvNil), "Should never fail");
    Pmvie()->Pmcc()->ChangeTool(toolCompose);
    vpcex->EndMouseTracking();

    _fMouseDownSeen = fFalse;

    return;
}
/** 3DMMv1.0: *************************************************************************
 *
 * Track the mouse moves and set the cursor appropriately.
 *
 * Parameters:
 *	pcmd - The command information.
 *
 * Returns:
 *  fTrue - indicating that the command was processed.
 *
 ***************************************************************************/
bool MVU::FCmdMouseMove(PCMD_MOUSE pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PACTR pactr;
    int32_t ibset;
    PDOCB pdocb;

    AssertPo(Pmvie(), 0);
    if (Pmvie()->Pscen() == pvNil)
    {
        Pmvie()->Pmcc()->SetCurs(toolDefault);
        return (fTrue);
    }

    if (Pmvie()->FCameraInputActive())
    {
        // Mouse-look is sampled from FCmdIdle. While the cursor is captured and
        // hidden there is no cursor-shape work to do here; just swallow both
        // physical and recenter-generated WM_MOUSEMOVE traffic.
        return fTrue;
    }
    _fManualCameraMousePrimed = fFalse;

    if (Pmvie()->FPlaying())
    {
        Pmvie()->Pmcc()->SetCurs(toolDefault);
        return (fTrue);
    }

    switch (Tool())
    {
    case toolTboxStory:
    case toolTboxCredit:
    case toolTboxPaintText:
    case toolTboxFillBkgd:
    case toolTboxMove:
    case toolTboxFont:
    case toolTboxSize:
    case toolTboxStyle:
        Pmvie()->Pmcc()->SetCurs(toolDefault);
        break;

    case toolPlace:
        return (fFalse);

    case toolSceneNuke:
    case toolSceneChop:
    case toolSceneChopBack:
        Pmvie()->Pmcc()->SetCurs(Tool());
        break;

    case toolSounder:
    case toolLooper:
    case toolMatcher:
        if (_tagTool.sid == ksidInvalid)
            Pmvie()->Pmcc()->SetCurs(toolDefault);
        else
            Pmvie()->Pmcc()->SetCurs(Tool());
        break;

    case toolListener:
        Pmvie()->Pmcc()->SetCurs(Tool());
        // 3DMMv1.0: Audition sound if over an actor
        pactr = Pmvie()->Pscen()->PactrFromPt(pcmd->xp, pcmd->yp, &ibset);
        if (pvNil != pactr)
        {
            if (pactr != _pactrListener)
            {
                Pmvie()->Pmsq()->StopAll();
                pactr->FReplayFrame(fscenSounds); // 3DMMv1.0: Ignore audition error; non-fatal
                // 3DMMv1.0: Play outstanding sounds
                Pmvie()->Pmsq()->PlayMsq();
            }
            _fMouseOn = fFalse;
        }
        else if (!_fMouseOn)
        {
            _fMouseOn = fTrue;
            Pmvie()->Pmsq()->StopAll();
            Pmvie()->Pscen()->FReplayFrm(fscenSounds);
            // 3DMMv1.0: Play outstanding sounds
            Pmvie()->Pmsq()->PlayMsq();
        }

        _pactrListener = pactr;
        break;

    case toolSoonerLater:
        if (!_fTextMode)
        {
            AssertPo(Pmvie()->Pscen(), 0);
            pactr = Pmvie()->Pscen()->PactrFromPt(pcmd->xp, pcmd->yp, &ibset);
            AssertNilOrPo(pactr, 0);
            if (pactr == pvNil)
            {
                Pmvie()->Pmcc()->SetCurs(toolDefault);
                break;
            }
            else if (_fMouseDownSeen)
            {
                Pmvie()->Pmcc()->SetCurs(toolCompose);
            }
            else
            {
                Pmvie()->Pmcc()->SetCurs(Tool());
            }
        }
        else
        {
            Pmvie()->Pmcc()->SetCurs(toolDefault);
        }
        break;

    case toolAction:
    case toolActorNuke:
    case toolActorSelect:
    case toolCompose:
    case toolRecordSameAction:
    case toolRotateX:
    case toolRotateY:
    case toolRotateZ:
    case toolCostumeCmid:
    case toolSquashStretch:
    case toolResize:
    case toolNormalizeRot:
    case toolNormalizeSize:
    case toolCopyObject:
    case toolPasteObject:
    case toolCopyRte:
    case toolCutObject:
    case toolActorEasel:

        if (!_fTextMode)
        {
            AssertPo(Pmvie()->Pscen(), 0);
            pactr = Pmvie()->Pscen()->PactrFromPt(pcmd->xp, pcmd->yp, &ibset);
            AssertNilOrPo(pactr, 0);
            if (pactr == pvNil)
            {
                Pmvie()->Pmcc()->SetCurs(toolDefault);
                break;
            }
            else if ((Tool() == toolCompose) && (pcmd->grfcust & fcustCmd))
            {
                Pmvie()->Pmcc()->SetCurs(toolTweak);
            }
            else if ((Tool() == toolCompose) && (pcmd->grfcust & fcustShift))
            {
                Pmvie()->Pmcc()->SetCurs(toolComposeAll);
            }
            else if ((Tool() == toolPasteObject) && vpclip->FGetFormat(kclsACLP, &pdocb))
            {
                if (((PACLP)pdocb)->FRouteOnly())
                    Pmvie()->Pmcc()->SetCurs(toolPasteRte);
                else
                    Pmvie()->Pmcc()->SetCurs(Tool());
                ReleasePpo(&pdocb);
            }
            else
            {
                Pmvie()->Pmcc()->SetCurs(Tool());
            }
        }
        else
        {
            Pmvie()->Pmcc()->SetCurs(toolDefault);
        }
        break;

    case toolDefault:
        Pmvie()->Pmcc()->SetCurs(toolDefault);
        break;

    default:
        Bug("Unknown tool type");
    }

    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Track the mouse and do the appropriate command.
 *
 * Parameters:
 *	pcmdTrack - The command information.
 *
 * Returns:
 *  fTrue - indicating that the command was processed.
 *
 ***************************************************************************/
bool MVU::FCmdTrackMouse(PCMD_MOUSE pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    RC rc;

    if (Pmvie()->FCameraInputActive())
    {
        if (Pmvie()->FManualCameraMode() && Pmvie()->Pscen() != pvNil)
        {
            Pmvie()->Pscen()->SelectActr(pvNil);
            Pmvie()->Pscen()->SelectTbox(pvNil);
        }
        return fTrue;
    }

    if (pcmd->cid == cidMouseDown)
    {
        Assert(vpcex->PgobTracking() == pvNil, "mouse already being tracked!");
        vpcex->TrackMouse(this);
    }
    else
    {
        Assert(vpcex->PgobTracking() == this, "not tracking mouse!");
        Assert(pcmd->cid == cidTrackMouse, 0);
    }

    if ((pcmd->cid == cidMouseDown) || ((pcmd->grfcust & fcustMouse) && !_fMouseDownSeen))
    {
        _MouseDown(pcmd);
    }
    else
    {
        _MouseDrag(pcmd);
    }

    if (!(pcmd->grfcust & fcustMouse))
    {
        _MouseUp(pcmd);
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Handle positioning an actor when the place tool is in effect.
 *
 * Parameters:
 *  dxrWld, dyrWld, dzrWld - the change in position in worldspace
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MVU::_PositionActr(BRS dxrWld, BRS dyrWld, BRS dzrWld)
{
    AssertThis(0);
    Assert(Tool() == toolPlace, "Wrong tool in effect");

    PMVIE pmvie;
    PSCEN pscen;
    bool fMoved;
    PACTR pactr = pvNil;
    uint32_t grfmaf = fmafOrient;

    pmvie = Pmvie();
    AssertPo(pmvie, 0);

    pscen = pmvie->Pscen();
    AssertPo(pscen, 0);

    pactr = pscen->PactrSelected();
    AssertPo(pactr, 0);

    if (_fEntireScene)
    {
        grfmaf |= fmafEntireScene;
    }
    else
    {
        grfmaf |= fmafEntireSubrte;
    }

    if (FRespectGround())
    {
        grfmaf |= fmafGround;
    }

    // 3DMMv1.0: FMoveRouteCore cannot fail on fmafEntireSubrte as no events are added
    if (pactr->FMoveRoute(dxrWld, dyrWld, dzrWld, &fMoved, grfmaf) && fMoved)
    {
        Pmvie()->Pbwld()->MarkDirty();
        Pmvie()->MarkViews();
        pscen->Pbkgd()->ReuseActorPlacePoint();
    }
}

/** 3DMMv1.0: *************************************************************************
 *
 * Notify script that an actor was clicked.  Note that we sometimes call
 * this function with fDown fFalse even though the user hasn't mouseup'ed
 * yet, because of bringing up easels on mousedown.
 *
 * Parameters:
 *	pactr - the actor that was clicked
 *  fDown - fTrue if we're mousedown'ing the actor, fFalse if mouseup
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MVU::_ActorClicked(PACTR pactr, bool fDown)
{
    AssertThis(0);
    AssertPo(pactr, 0);

    uint32_t grftmpl = 0;

    if (pactr->Ptmpl()->FIsTdt())
    {
        grftmpl |= ftmplTdt;
    }

    if (pactr->Ptmpl()->FIsProp())
    {
        grftmpl |= ftmplProp;
    }

    if (fDown)
    {
        vpcex->EnqueueCid(cidActorClickedDown, pvNil, pvNil, pactr->Arid(), pactr->Ptmpl()->Cno(), grftmpl);
    }
    else
    {
        vpcex->EnqueueCid(cidActorClicked, pvNil, pvNil, pactr->Arid(), pactr->Ptmpl()->Cno(), grftmpl);
    }
}

/** 3DMMv1.0: *************************************************************************
 *
 * Handle Mousedown
 *
 * Parameters:
 *	pcmd - The mouse command
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MVU::_MouseDown(CMD_MOUSE *pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PACTR pactr = pvNil;
    PACTR pactrDup;
    PTBOX ptbox;
    PAUND paund;
    PT pt;
    int32_t ibset;
    PDOCB pdocb;

    _fMultiDeselectClick = fFalse;

    if (Pmvie()->FPlaying())
    {
        //
        // 3DMMv1.0: If we are in the middle of playing, ignore mouse down.
        //
        return;
    }

    AssertPo(Pmvie(), 0);
    if (pvNil == Pmvie()->Pscen())
    {
        return;
    }
    AssertPo(Pmvie()->Pscen(), 0);

    if (_fTextMode)
    {
        ptbox = Pmvie()->Pscen()->PtboxSelected();
    }
    else if ((Tool() != toolPlace) && (Tool() != toolSceneChop) && (Tool() != toolSceneChopBack))
    {

        //
        // 3DMMv1.0: Select the actor under the cursor
        //
        pactr = Pmvie()->Pscen()->PactrSelected();
        AssertNilOrPo(pactr, 0);

        if ((pactr != pvNil) && pactr->FTimeFrozen())
        {
            pactr->SetTimeFreeze(fFalse);
        }

        const bool fForceSelected = (pactr != pvNil) && (pcmd->grfcust & fcustOption) &&
            (Tool() == toolCompose || Tool() == toolRotateX || Tool() == toolRotateY ||
             Tool() == toolRotateZ || Tool() == toolResize || Tool() == toolSquashStretch ||
             Tool() == toolNormalizeRot || Tool() == toolNormalizeSize ||
             Tool() == toolActorNuke || Tool() == toolCutObject || Tool() == toolCopyObject ||
             Tool() == toolActorEasel || Tool() == toolAction);

        if (!fForceSelected)
        {
            pactrDup = Pmvie()->Pscen()->PactrFromPt(pcmd->xp, pcmd->yp, &ibset);

            //
            // 3DMMv1.0: Use previously selected actor if mouse in the actor.
            // 3DMMv1.0: Don't change the selected actor if we're using the default tool
            //
            if (((pactr == pvNil) || !pactr->FIsInView() || !pactr->FPtIn(pcmd->xp, pcmd->yp, &ibset)) &&
                Tool() != toolDefault)
            {
                pactr = pactrDup;
                AssertNilOrPo(pactr, 0);
            }

            const bool fMultiSelectionTool = MVIE::FMultiSelectMode() &&
                ((Tool() == toolCompose) || (Tool() == toolActorSelect));

            if (pvNil != pactr)
                _ActorClicked(pactr, fTrue);

            // Shift+click extends the unlimited mixed selection. SelectActrAdd also
            // makes the clicked object the primary hand/reposition target without
            // discarding the objects that were already selected.
            if (fMultiSelectionTool && (pactr != pvNil) && (pcmd->grfcust & fcustShift))
                Pmvie()->Pscen()->SelectActrAdd(pactr);
            else
                Pmvie()->Pscen()->SelectActr(pactr); // 3DMMv1.0: okay even if pactr is pvNil
            Pmvie()->Pbwld()->MarkDirty();
        }
    }

#ifdef DEBUG
    // 3DMMv1.0: Authoring hack to write out background starting pos data

    if (pvNil != pactr &&
        ((vpappb->GrfcustCur(fFalse) & (fcustShift | fcustCmd | fcustOption)) == (fcustShift | fcustCmd | fcustOption)))
    {
        BRS xr;
        BRS yr;
        BRS zr;

        pactr->Pbody()->GetPosition(&xr, &yr, &zr);
        if (!Pmvie()->Pscen()->Pbkgd()->FWritePlaceFile(xr, yr, zr))
        {
            Bug("ouch.  bkgd write failed.");
        }
        else
        {
            Warn("Wrote bkgd actor start point.");
        }
    }
#endif // 3DMMv1.0: DEBUG

    _xpPrev = pcmd->xp;
    _ypPrev = pcmd->yp;
    _dzrPrev = rZero;
    _grfcust = pcmd->grfcust;
    _tsLastSample = TsCurrent();

    switch (Tool())
    {

    // 3DMMv1.0: Sound tools get handled together:
    case toolSounder:
    case toolLooper:
    case toolMatcher:
        if (pactr == pvNil) // 3DMMv1.0: scene sound
        {
            tribool fLoop = (tribool)(Tool() == toolLooper);
            tribool fQueue = (tribool)FPure(_grfcust & fcustCmd);

            if ((Tool() == toolMatcher) && (ksidInvalid != _tagTool.sid))
            {
                PushErc(ercSocBadSceneSound);
                break;
            }
            if (ksidInvalid != _tagTool.sid)
            {
                Pmvie()->FAddBkgdSnd(&_tagTool, fLoop, fQueue, vlmNil, styNil);
            }
        }
        else // 3DMMv1.0: actor sound
        {
            tribool fLoop = (tribool)(Tool() == toolLooper);
            tribool fQueue = (tribool)FPure(_grfcust & fcustCmd);
            tribool fActnCel = (tribool)FPure(Tool() == toolMatcher);

            if (ksidInvalid != _tagTool.sid)
            {
                Pmvie()->FAddActrSnd(&_tagTool, fLoop, fQueue, fActnCel, vlmNil, styNil);
            }
        }
        break;

    case toolListener:
        // 3DMMv1.0: Start the listener easel
        vpcex->EndMouseTracking();
        RestoreKeyboardRepeat();
        if (pvNil != pactr)
        {
            _ActorClicked(pactr, fFalse);
        }
        Pmvie()->Pmcc()->StartListenerEasel();
        break;

    case toolActorSelect:
        Pmvie()->Pmcc()->PlayUISound(Tool());
        break;

    case toolSceneNuke:
        if (Pmvie()->FRemScen(Pmvie()->Iscen()))
        {
            Pmvie()->Pmcc()->UpdateRollCall();
            Pmvie()->Pmcc()->SceneNuked();
            Pmvie()->InvalViewsAndScb();
            Pmvie()->Pmcc()->PlayUISound(Tool());
        }
        break;

    case toolActorNuke:
        if (pactr == pvNil)
        {
            break;
        }

        Pmvie()->FRemActr();
        Pmvie()->Pmcc()->PlayUISound(Tool());
        break;

    case toolSceneChop:
        MVIE::MultiLog(Pmvie(), "scene_trim_viewport_click after scene=%ld frame=%ld xy=%ld,%ld",
                       (long)Pmvie()->Iscen(), (long)Pmvie()->Pscen()->Nfrm(),
                       (long)pcmd->xp, (long)pcmd->yp);
        if (Pmvie()->Pscen()->FChop())
        {
            Pmvie()->InvalViewsAndScb();
            Pmvie()->Pmcc()->PlayUISound(Tool());
        }
        break;

    case toolSceneChopBack:
        MVIE::MultiLog(Pmvie(), "scene_trim_viewport_click before scene=%ld frame=%ld xy=%ld,%ld",
                       (long)Pmvie()->Iscen(), (long)Pmvie()->Pscen()->Nfrm(),
                       (long)pcmd->xp, (long)pcmd->yp);
        if (Pmvie()->Pscen()->FChopBack())
        {
            Pmvie()->InvalViewsAndScb();
            Pmvie()->Pmcc()->PlayUISound(Tool());
        }
        break;

    case toolCutObject:
    case toolCopyObject:
    case toolCopyRte:
        if (!_fTextMode)
        {
            FDoClip(Tool());
        }
        break;

    case toolPasteObject:
        if (!_fTextMode)
        {
            if (vpclip->FGetFormat(kclsACLP, &pdocb) && ((PACLP)pdocb)->FRouteOnly() && (pactr != pvNil))
            {
                FDoClip(Tool());
                ReleasePpo(&pdocb);
            }
        }
        break;

    case toolTboxStory:
    case toolTboxCredit:
    case toolTboxPaintText:
    case toolTboxFillBkgd:
    case toolTboxMove:
    case toolTboxFont:
    case toolTboxSize:
    case toolTboxStyle:
        Pmvie()->Pscen()->SelectTbox(pvNil);
        break;

    case toolPlace:
        _fMouseDownSeen = fTrue;
        break;

    case toolSoonerLater:
        if ((pactr != pvNil) && !_fMouseDownSeen)
        {
            pactr->SetTimeFreeze(fTrue);
            pactr->Hilite();
            _fMouseDownSeen = fTrue;
            Pmvie()->Pmcc()->PlayUISound(Tool());
        }
        else
        {
            goto LEnd;
        }
        break;

    case toolCompose:
    case toolRotateX:
    case toolRotateY:
    case toolRotateZ:
    case toolResize:
    case toolSquashStretch:
        if (pactr != pvNil)
        {

            vpappb->HideCurs();

            //
            // Create one undo object for the thing this gesture will edit.
            // A bound -multi Object Group snapshots every member so moving,
            // rotating, resizing, or squash/stretching the assembly is one
            // undo/redo operation. CTRL+hand route tweaking remains the legacy
            // single-actor editor for now.
            //
            int32_t idObjectGroup = 0;
            const bool fGroupTransform = MVIE::FMultiSelectMode() &&
                ((Tool() == toolRotateX) || (Tool() == toolRotateY) || (Tool() == toolRotateZ) ||
                 (Tool() == toolResize) || (Tool() == toolSquashStretch) ||
                 ((Tool() == toolCompose) && !(_grfcust & fcustCmd))) &&
                Pmvie()->FObjectInObjectGroup(pactr->Arid(), &idObjectGroup);

            if (fGroupTransform)
            {
                // Establish one high-precision parent/group frame for the
                // entire gesture before any legacy fixed-point ACTR writes.
                // Every mouse packet in this drag derives from this frame.
                Pmvie()->FBeginObjectGroupTransform(idObjectGroup);

                PGUND pgund = GUND::PgundNew();
                if (pgund == pvNil || !pgund->FCaptureGroup(Pmvie(), idObjectGroup))
                {
                    ReleasePpo(&pgund);
                    Pmvie()->ClearUndo();
                    PushErc(ercSocNotUndoable);
                }
                else
                {
                    _paund = pgund;
                }
            }
            else
            {
                paund = AUND::PaundNew();
                if ((paund == pvNil) || !pactr->FDup(&pactrDup, fTrue))
                {
                    ReleasePpo(&paund);
                    Pmvie()->ClearUndo();
                    PushErc(ercSocNotUndoable);
                }
                else
                {
                    paund->SetPactr(pactrDup);
                    ReleasePpo(&pactrDup);
                    paund->SetArid(pactr->Arid());

                    //
                    // 3DMMv1.0: Store it.  We will only add it if there is a change done
                    // 3DMMv1.0: to the actor.
                    //
                    _paund = paund;
                }
            }

            if ((Tool() != toolResize) && (Tool() != toolSquashStretch))
            {
                Pmvie()->Pmcc()->PlayUISound(Tool(), _grfcust);
            }
            else
            {
                _lwLastTime = 0;
            }
        }
        break;

    case toolNormalizeRot:
        if (pactr != pvNil)
        {
            Pmvie()->Pmcc()->PlayUISound(Tool());
            int32_t idObjectGroup = 0;
            const bool fBoundGroup = MVIE::FMultiSelectMode() &&
                Pmvie()->FObjectInObjectGroup(pactr->Arid(), &idObjectGroup);
            if (fBoundGroup)
            {
                PGUND pgund = GUND::PgundNew();
                if (pgund == pvNil || !pgund->FCaptureGroup(Pmvie(), idObjectGroup))
                {
                    ReleasePpo(&pgund);
                    Pmvie()->ClearUndo();
                    PushErc(ercSocNotUndoable);
                    break;
                }
                if (Pmvie()->FNormalizeObjectGroupRotation(idObjectGroup))
                {
                    if (!Pmvie()->FAddUndo(pgund))
                    {
                        PushErc(ercSocNotUndoable);
                        Pmvie()->ClearUndo();
                    }
                }
                ReleasePpo(&pgund);
                Pmvie()->UpdateTestLightAttachment();
                Pmvie()->Pbwld()->MarkDirty();
                Pmvie()->MarkViews();
            }
            else
                pactr->FNormalize(fnormRotate);
        }
        break;
    case toolNormalizeSize:
        if (pactr != pvNil)
        {
            Pmvie()->Pmcc()->PlayUISound(Tool());
            int32_t idObjectGroup = 0;
            const bool fBoundGroup = MVIE::FMultiSelectMode() &&
                Pmvie()->FObjectInObjectGroup(pactr->Arid(), &idObjectGroup);
            if (fBoundGroup)
            {
                PGUND pgund = GUND::PgundNew();
                if (pgund == pvNil || !pgund->FCaptureGroup(Pmvie(), idObjectGroup))
                {
                    ReleasePpo(&pgund);
                    Pmvie()->ClearUndo();
                    PushErc(ercSocNotUndoable);
                    break;
                }
                if (Pmvie()->FNormalizeObjectGroupScale(idObjectGroup))
                {
                    if (!Pmvie()->FAddUndo(pgund))
                    {
                        PushErc(ercSocNotUndoable);
                        Pmvie()->ClearUndo();
                    }
                }
                ReleasePpo(&pgund);
                Pmvie()->UpdateTestLightAttachment();
                Pmvie()->Pbwld()->MarkDirty();
                Pmvie()->MarkViews();
            }
            else
                pactr->FNormalize(fnormSize);
        }
        break;

    case toolRecordSameAction:

        if (pactr != pvNil)
        {
            int32_t anidTool = pactr->AnidCur();
            int32_t anid = anidTool;
            int32_t celn = 0;
            bool fFrozen;

            SetAnidTool(pactr->AnidCur());
            _ptmplTool = pactr->Ptmpl();

            if ((pcmd->grfcust & fcustShift) && (pcmd->grfcust & fcustCmd) && FRecordDefault())
            {
                fFrozen = fFalse;
                _fCyclingCels = fFalse;
                _fSetFRecordDefault = fTrue;
                SetFRecordDefault(fFalse);
            }
            else
            {
                fFrozen = FPure(pcmd->grfcust & fcustShift);
                _fCyclingCels = FPure(pcmd->grfcust & fcustCmd);
            }

            if ((pactr->Ptmpl() != _ptmplTool) && !(_ptmplTool->FIsTdt() && pactr->Ptmpl()->FIsTdt()))
            {
                PushErc(ercSocActionNotApplicable);
                return;
            }

            vpappb->HideCurs();

            // 3DMMv1.0: first, call FSetAction
            if (anidTool == ivNil)
            {
                anid = pactr->AnidCur();
            }
            if (pactr->AnidCur() == anid)
            {
                celn = pactr->CelnCur();
            }

            // 3DMMv1.0: note that FSetAction creates an undo object
            // 3DMMv1.0: NOTE:  FSetAction() must be called on each use
            // 3DMMv1.0: of toolRecordSameAction. (It is not redundant).
            // 3DMMv1.0: Otherwise, resizing can break wysiwyg, as the final
            // 3DMMv1.0: path point probably won't be a complete cel's distance
            // 3DMMv1.0: from the previous step.
            Assert(pvNil == _pactrRestore, "_pactrRestore should not require releasing");
            ReleasePpo(&_pactrRestore); // 3DMMv1.0: To be safe
            if (!pactr->FSetAction(anid, celn, fFrozen, &_pactrRestore))
            {
                break; // 3DMMv1.0: an Oom erc has already been pushed
            }
            SetAnidTool(ivNil);
            _tsLast = TsCurrent();
            pactr->SetTsInsert(_tsLast);
            Pmvie()->Pmcc()->PlayUISound(Tool());
        }

        break;

    case toolAction:
        //
        // 3DMMv1.0: Start the action browser
        //
        vpcex->EndMouseTracking();
        RestoreKeyboardRepeat();

        if (pactr != pvNil)
        {
            _ActorClicked(pactr, fFalse);
            Pmvie()->Pscen()->SelectActr(pactr);
            _ptmplTool = pactr->Ptmpl();
            Pmvie()->Pmcc()->StartActionBrowser();
        }
        break;

    case toolCostumeCmid:
        if (pactr != pvNil)
        {
            TAG tag; // 3DMMv1.0: unused
            TrashVar(&tag);
            TrashVar(&ibset);
            Pmvie()->FCostumeActr(ibset, &tag, CmidTool(), tribool::tYes);
        }
        break;
    case toolActorEasel:
        if (pactr != pvNil)
        {
            bool fActrChanged;

            _ActorClicked(pactr, fFalse);
            Pmvie()->Pmcc()->ActorEasel(&fActrChanged);
            if (fActrChanged)
            {
                Pmvie()->SetDirty();
                Pmvie()->ClearUndo();
            }
        }
        break;

    case toolDefault:
        /* 3DMMv1.0: Do nothing */
        break;

    default:
        Bug("Tool unknown on mouse down");
    }

    if (Pmvie()->FSoundsEnabled())
    {
        Pmvie()->Pmsq()->PlayMsq();
    }
    else
    {
        Pmvie()->Pmsq()->FlushMsq();
    }
    _fMouseDownSeen = fTrue;

LEnd:
    Pmvie()->MarkViews();
}

/** 3DMMv1.0: *************************************************************************
 *
 * Handle Mouse drag (mouse move while button down)
 *
 * Parameters:
 *  pcmd - The mouse command
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
static uint32_t vc4DMMHandDiagSample = 0;

void MVU::_MouseDrag(CMD_MOUSE *pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PMVIE pmvie;
    PSCEN pscen;
    PACTR pactr = pvNil;
    BRS dxrMouse, dyrMouse, dzrMouse;
    BRS dxrWld, dyrWld, dzrWld; // 3DMMv1.0: amount moved from previous point in world space
    BRS zrActr, zrCam, dzrActr;
    bool fArrowKey = fFalse, fKeyDown = fFalse, fKeyUp = fFalse;
    RC rc;
    PT pt;

    if (Pmvie()->FPlaying() || Pmvie()->Pmcc()->FMinimized())
    {
        //
        // 3DMMv1.0: If we are in the middle of playing, or been minimized, ignore mouse dragging.
        //
        return;
    }

    pmvie = Pmvie();
    AssertPo(pmvie, 0);
    pscen = pmvie->Pscen();
    AssertNilOrPo(pscen, 0);

    if (pvNil == pscen)
    {
        return;
    }

    pactr = pscen->PactrSelected();

    AssertNilOrPo(pactr, 0);

    if (pactr == pvNil)
    {
        return;
    }

    dxrMouse = BrsSub(BrIntToScalar(pcmd->xp), BrIntToScalar(_xpPrev));
    dyrMouse = BrsSub(BrIntToScalar(_ypPrev), BrIntToScalar(pcmd->yp));
    dzrMouse = _dzrPrev;

    if (vpmvu4DMMHandWarpDiag == this && vc4DMMHandWarpDiagRemaining > 0 &&
        Tool() == toolCompose)
    {
        const int32_t iAfterWarp = 4 - vc4DMMHandWarpDiagRemaining;
        MVIE::MultiLog(Pmvie(),
            "main_hand_after_warp sample=%ld elapsed=%lu warp_input=%ld,%ld warp_prev=%ld,%ld center=%ld,%ld current=%ld,%ld delta=(%g,%g,%g)",
            (long)iAfterWarp, (unsigned long)(TsCurrent() - vts4DMMHandWarp),
            (long)vxp4DMMHandWarpInput, (long)vyp4DMMHandWarpInput,
            (long)vxp4DMMHandWarpPrev, (long)vyp4DMMHandWarpPrev,
            (long)_xpPrev, (long)_ypPrev, (long)pcmd->xp, (long)pcmd->yp,
            (double)BrScalarToFloat(dxrMouse), (double)BrScalarToFloat(dyrMouse),
            (double)BrScalarToFloat(dzrMouse));
        --vc4DMMHandWarpDiagRemaining;
        if (vc4DMMHandWarpDiagRemaining <= 0)
            vpmvu4DMMHandWarpDiag = pvNil;
    }

#if defined(KAUAI_WIN32)
    fKeyUp = (GetKeyState(VK_PRIOR) < 0);
    fKeyDown = (GetKeyState(VK_NEXT) < 0);
#elif defined(KAUAI_SDL)
    const uint8_t *rgbKeyStates = SDL_GetKeyboardState(pvNil);
    if (rgbKeyStates)
    {
        fKeyUp = FPure(rgbKeyStates[SDL_SCANCODE_PAGEUP]);
        fKeyDown = FPure(rgbKeyStates[SDL_SCANCODE_PAGEDOWN]);
    }
#else
    RawRtn();
#endif

    //
    // Get the "mouse Z" by sampling Page Up / Page Down. If either key
    // 3DMMv1.0: is down, the number of pixels moved is the number of seconds
    // 3DMMv1.0: since the keyboard was last sampled times kdwrMousePerSecond.
    //
    uint32_t dts = LwMax(1, TsCurrent() - _tsLastSample);
    BRS drSec = BrsDiv(BrIntToScalar(dts), BR_SCALAR(kdtsSecond));
    if (fKeyUp)
    {
        fArrowKey = fTrue;
        dzrMouse += BrsMul(drSec, kdwrMousePerSecond);
    }
    if (fKeyDown)
    {
        fArrowKey = fTrue;
        dzrMouse -= BrsMul(drSec, kdwrMousePerSecond);
    }
    if (fArrowKey && Tool() == toolRecordSameAction && !_fCyclingCels && pactr->FIsModeRecord())
    {
        if ((BrsAbs(dxrMouse) * 4 < BrsAbs(dzrMouse)) && (BrsAbs(dyrMouse) * 4 < BrsAbs(dzrMouse)))
        {
            // When Page Up / Page Down are used, infinitesimal mouse movement
            // should not be observed or determine path direction.
            dxrMouse = rZero;
            dyrMouse = rZero;
        }
    }
    _dzrPrev = dzrMouse;
    _tsLastSample = TsCurrent();

    MouseToWorld(dxrMouse, dyrMouse, dzrMouse, &dxrWld, &dyrWld, &dzrWld, Tool() == toolRecordSameAction);

    //
    // Scale movement based on the thing being manipulated.  A bound Object
    // Group uses its shared pivot depth so the hand/rotate response does not
    // change depending on which member happened to be clicked.
    //
    int32_t idScaleGroup = 0;
    const bool fScaleAsGroup = MVIE::FMultiSelectMode() &&
        ((Tool() == toolCompose) || (Tool() == toolRotateX) ||
         (Tool() == toolRotateY) || (Tool() == toolRotateZ)) &&
        Pmvie()->FObjectInObjectGroup(pactr->Arid(), &idScaleGroup) &&
        Pmvie()->FGetObjectGroupPivot(idScaleGroup, pvNil, pvNil, &zrActr);
    if (!fScaleAsGroup)
        pactr->GetXyzWorld(pvNil, pvNil, &zrActr);
    pscen->Pbkgd()->GetCameraPos(pvNil, pvNil, &zrCam);
    dzrActr = BrsAbs(BrsSub(zrActr, zrCam));
    dzrActr = BrsDiv(dzrActr, kzrMouseScalingFactor);
    if (dzrActr < rOne)
    {
        dzrActr = rOne;
    }
    dxrWld = BrsMul(dxrWld, BrsMul(dzrActr, BR_SCALAR(1.1)));
    dyrWld = BrsMul(dyrWld, BrsMul(dzrActr, BR_SCALAR(1.1)));
    dzrWld = BrsMul(dzrWld, BrsMul(dzrActr, BR_SCALAR(1.1)));

    switch (Tool())
    {
    default:
        Bug("Tool unknown on mouse move");
        break;

    case toolDefault:
    case toolActorSelect:
        break;

    case toolSceneNuke:
    case toolActorNuke:
    case toolSceneChop:
    case toolSceneChopBack:
    case toolCutObject:
    case toolCopyObject:
    case toolPasteObject:
    case toolCopyRte:
    case toolTboxPaintText:
    case toolTboxFillBkgd:
    case toolTboxMove:
    case toolTboxFont:
    case toolTboxSize:
    case toolTboxStory:
    case toolTboxCredit:
    case toolTboxStyle:
    case toolActorEasel:
    case toolSounder:
    case toolLooper:
    case toolMatcher:
        break;

    case toolPlace:
        _PositionActr(dxrWld, dyrWld, dzrWld);
        AdjustCursor(pcmd->xp, pcmd->yp);
        break;

    case toolCompose: {
        uint32_t grfmaf = fmafNil;
        bool fMoved{};

        if (_fRespectGround)
        {
            grfmaf |= fmafGround;
        }

        if (_grfcust & fcustCmd)
        {
            AdjustCursor(pcmd->xp, pcmd->yp);

            if (pactr->FTweakRoute(dxrWld, dyrWld, dzrWld, grfmaf))
            {
                if (fMoved)
                {
                    if ((_paund != pvNil) && !Pmvie()->FAddUndo(_paund))
                    {
                        PushErc(ercSocNotUndoable);
                        Pmvie()->ClearUndo();
                    }

                    ReleasePpo(&_paund);
                }
            }

            Pmvie()->UpdateTestLightAttachment();
            Pmvie()->MarkViews();
        }
        else
        {

            if (_grfcust & fcustShift)
            {
                grfmaf |= fmafEntireSubrte;
            }

            int32_t idObjectGroup = 0;
            const bool fBoundGroup = MVIE::FMultiSelectMode() &&
                Pmvie()->FObjectInObjectGroup(pactr->Arid(), &idObjectGroup);

            BRS xrActrBefore = rZero;
            BRS yrActrBefore = rZero;
            BRS zrActrBefore = rZero;
            if (!fBoundGroup)
                pactr->GetXyzWorld(&xrActrBefore, &yrActrBefore, &zrActrBefore);

            // A bound group translates every member by the same world delta,
            // preserving the original relative placement.  Ungrouped actors
            // keep the exact legacy hand/reposition path.
            const bool fMoveOk = fBoundGroup
                ? Pmvie()->FMoveObjectGroup(idObjectGroup, dxrWld, dyrWld, dzrWld, &fMoved, grfmaf)
                : pactr->FMoveRoute(dxrWld, dyrWld, dzrWld, &fMoved, grfmaf);
            if (fMoveOk)
            {
                if (fMoved)
                {
                    if (!fBoundGroup)
                    {
                        BRS xrActrAfter;
                        BRS yrActrAfter;
                        BRS zrActrAfter;
                        pactr->GetXyzWorld(&xrActrAfter, &yrActrAfter, &zrActrAfter);
                        const BRS dxrActual = BrsSub(xrActrAfter, xrActrBefore);
                        const BRS dyrActual = BrsSub(yrActrAfter, yrActrBefore);
                        const BRS dzrActual = BrsSub(zrActrAfter, zrActrBefore);
                        const BRS rRouteTolerance = BR_SCALAR(0.25);
                        const bool fRouteMismatch =
                            BrsAbs(BrsSub(dxrActual, dxrWld)) > rRouteTolerance ||
                            BrsAbs(BrsSub(dzrActual, dzrWld)) > rRouteTolerance;

                        BRS rMouseMax = BrsAbs(dxrMouse);
                        if (BrsAbs(dyrMouse) > rMouseMax)
                            rMouseMax = BrsAbs(dyrMouse);
                        if (BrsAbs(dzrMouse) > rMouseMax)
                            rMouseMax = BrsAbs(dzrMouse);
                        BRS rWorldMax = BrsAbs(dxrWld);
                        if (BrsAbs(dyrWld) > rWorldMax)
                            rWorldMax = BrsAbs(dyrWld);
                        if (BrsAbs(dzrWld) > rWorldMax)
                            rWorldMax = BrsAbs(dzrWld);
                        BRS rExpectedWorldMax = BrsMul(rMouseMax, krScaleMouseNonRecord);
                        rExpectedWorldMax = BrsMul(rExpectedWorldMax,
                                                   BrsMul(dzrActr, BR_SCALAR(1.1)));
                        const bool fWorldMapAnomaly = rWorldMax > BR_SCALAR(0.5) &&
                            rWorldMax > BrsMul(rExpectedWorldMax, BR_SCALAR(4.0));

                        const bool fCameraRelative = Pmvie()->FCameraTrackActive() ||
                            Pmvie()->FManualCameraMode() || Pmvie()->FFreeLookOverride();
                        const bool fSparseRightSample = dxrMouse > rZero && fCameraRelative &&
                            (((++vc4DMMHandDiagSample) & 0x07) == 0);
                        if (fRouteMismatch || fWorldMapAnomaly || fSparseRightSample)
                        {
                            BMAT34 bmat34Diag;
                            Pmvie()->Pbwld()->GetCamera(&bmat34Diag);
                            MVIE::MultiLog(Pmvie(),
                                "%s arid=%ld mouse_xy=%ld,%ld prev_xy=%ld,%ld mouse_delta=(%g,%g,%g) req_world=(%g,%g,%g) actual_world=(%g,%g,%g) before=(%g,%g,%g) after=(%g,%g,%g) depth_scale=%g expected_world_max=%g camera_modes=%d,%d,%d mouse_live=%d camera_pos=(%g,%g,%g) camera_xz=(%g,%g,%g,%g) mouse_xz=(%g,%g,%g,%g)",
                                (fRouteMismatch || fWorldMapAnomaly) ? "main_hand_jump" : "main_hand_sample",
                                (long)pactr->Arid(), (long)pcmd->xp, (long)pcmd->yp,
                                (long)_xpPrev, (long)_ypPrev,
                                (double)BrScalarToFloat(dxrMouse), (double)BrScalarToFloat(dyrMouse),
                                (double)BrScalarToFloat(dzrMouse),
                                (double)BrScalarToFloat(dxrWld), (double)BrScalarToFloat(dyrWld),
                                (double)BrScalarToFloat(dzrWld),
                                (double)BrScalarToFloat(dxrActual), (double)BrScalarToFloat(dyrActual),
                                (double)BrScalarToFloat(dzrActual),
                                (double)BrScalarToFloat(xrActrBefore), (double)BrScalarToFloat(yrActrBefore),
                                (double)BrScalarToFloat(zrActrBefore),
                                (double)BrScalarToFloat(xrActrAfter), (double)BrScalarToFloat(yrActrAfter),
                                (double)BrScalarToFloat(zrActrAfter),
                                (double)BrScalarToFloat(dzrActr),
                                (double)BrScalarToFloat(rExpectedWorldMax),
                                Pmvie()->FCameraTrackActive() ? 1 : 0,
                                Pmvie()->FManualCameraMode() ? 1 : 0,
                                Pmvie()->FFreeLookOverride() ? 1 : 0,
                                vf4DMMMouseToWorldLiveCamera ? 1 : 0,
                                (double)BrScalarToFloat(bmat34Diag.m[3][0]),
                                (double)BrScalarToFloat(bmat34Diag.m[3][1]),
                                (double)BrScalarToFloat(bmat34Diag.m[3][2]),
                                (double)BrScalarToFloat(bmat34Diag.m[0][0]),
                                (double)BrScalarToFloat(bmat34Diag.m[0][2]),
                                (double)BrScalarToFloat(bmat34Diag.m[2][0]),
                                (double)BrScalarToFloat(bmat34Diag.m[2][2]),
                                (double)BrScalarToFloat(vbmat344DMMMouseToWorld.m[0][0]),
                                (double)BrScalarToFloat(vbmat344DMMMouseToWorld.m[0][2]),
                                (double)BrScalarToFloat(vbmat344DMMMouseToWorld.m[2][0]),
                                (double)BrScalarToFloat(vbmat344DMMMouseToWorld.m[2][2]));
                        }
                    }

                    if ((_paund != pvNil) && !Pmvie()->FAddUndo(_paund))
                    {
                        PushErc(ercSocNotUndoable);
                        Pmvie()->ClearUndo();
                    }

                    ReleasePpo(&_paund);

                    AdjustCursor(pcmd->xp, pcmd->yp);
                    Pmvie()->Pbwld()->MarkDirty();
                    if (Pmvie()->FBrowserCameraFollowActive())
                        Pmvie()->FUpdateBrowserCameraFollow();
                    Pmvie()->UpdateTestLightAttachment();
                    Pmvie()->MarkViews();
                }
            }
        }
    }
    break;

    case toolRotateX:
    case toolRotateY:
    case toolRotateZ: {
        BRS brs;
        BRA xa, ya, za;

        brs = BrsMul(dxrMouse + dyrMouse, -krRotateScaleFactor);

        xa = aZero;
        ya = aZero;
        za = aZero;

        switch (Tool())
        {
        case toolRotateX:
            xa = BrScalarToAngle(brs);
            break;
        case toolRotateY:
            ya = -BrScalarToAngle(brs);
            break;
        case toolRotateZ:
            za = BrScalarToAngle(brs);
            break;
        }

        int32_t idObjectGroup = 0;
        const bool fBoundGroup = MVIE::FMultiSelectMode() &&
            pmvie->FObjectInObjectGroup(pactr->Arid(), &idObjectGroup);
        const bool fRotated = fBoundGroup
            ? pmvie->FRotateObjectGroup(idObjectGroup, xa, ya, za, FPure(_grfcust & fcustCmd))
            : pmvie->FRotateActr(xa, ya, za, FPure(_grfcust & fcustCmd));
        if (fRotated)
        {
            if ((_paund != pvNil) && !Pmvie()->FAddUndo(_paund))
            {
                PushErc(ercSocNotUndoable);
                Pmvie()->ClearUndo();
            }

            ReleasePpo(&_paund);
        }

        Pmvie()->UpdateTestLightAttachment();
        Pmvie()->Pbwld()->MarkDirty();
        Pmvie()->MarkViews();
        AdjustCursor(pcmd->xp, pcmd->yp);
    }
    break;

    case toolResize: {
        BRS brs;
        BRS brs2;

        brs = BrsMul(dxrMouse + dyrMouse, krRotateScaleFactor);
        brs2 = BrsAdd(brs, rOne);

        //
        // 3DMMv1.0: Play UI sound
        //
        if ((((dxrMouse + dyrMouse) < 0) && !(_lwLastTime < 0)) || (((dxrMouse + dyrMouse) > 0) && !(_lwLastTime > 0)))
        {
            _lwLastTime = dxrMouse + dyrMouse;
            Pmvie()->Pmcc()->StopUISound();
            Pmvie()->Pmcc()->PlayUISound(Tool(), (dxrMouse + dyrMouse > 0) ? 0 : fcustShift);
        }

        // CTRL+Grow temporarily opts into the experimental 100x ceiling.
        // The movie setting can hard-enable the same ceiling without CTRL.
        int32_t idObjectGroup = 0;
        const bool fBoundGroup = MVIE::FMultiSelectMode() &&
            pmvie->FObjectInObjectGroup(pactr->Arid(), &idObjectGroup);
        const bool fScaled = fBoundGroup
            ? pmvie->FScaleObjectGroup(idObjectGroup, brs2, FPure(_grfcust & fcustCmd))
            : pmvie->FScaleActr(brs2, FPure(_grfcust & fcustCmd));
        if (fScaled)
        {
            if ((_paund != pvNil) && !Pmvie()->FAddUndo(_paund))
            {
                PushErc(ercSocNotUndoable);
                Pmvie()->ClearUndo();
            }

            ReleasePpo(&_paund);
        }

        Pmvie()->UpdateTestLightAttachment();
        Pmvie()->Pbwld()->MarkDirty();
        Pmvie()->MarkViews();
        AdjustCursor(pcmd->xp, pcmd->yp);
    }
    break;

    case toolSquashStretch: {
        BRS brs;
        BRS brs2;

        brs = BrsMul(-dxrMouse - dyrMouse, krRotateScaleFactor);
        brs2 = BrsAdd(brs, rOne);

        //
        // 3DMMv1.0: Play UI sound
        //
        if ((((-dxrMouse - dyrMouse) < 0) && !(_lwLastTime < 0)) ||
            (((-dxrMouse - dyrMouse) > 0) && !(_lwLastTime > 0)))
        {
            _lwLastTime = -dxrMouse - dyrMouse;
            Pmvie()->Pmcc()->StopUISound();
            Pmvie()->Pmcc()->PlayUISound(Tool(), (-dxrMouse - dyrMouse < 0) ? 0 : fcustShift);
        }

        int32_t idObjectGroup = 0;
        const bool fBoundGroup = MVIE::FMultiSelectMode() &&
            pmvie->FObjectInObjectGroup(pactr->Arid(), &idObjectGroup);
        const bool fPulled = fBoundGroup
            ? pmvie->FSquashStretchObjectGroup(idObjectGroup, brs2)
            : pmvie->FSquashStretchActr(brs2);
        if (fPulled)
        {
            if ((_paund != pvNil) && !Pmvie()->FAddUndo(_paund))
            {
                PushErc(ercSocNotUndoable);
                Pmvie()->ClearUndo();
            }

            ReleasePpo(&_paund);
        }

        Pmvie()->UpdateTestLightAttachment();
        Pmvie()->Pbwld()->MarkDirty();
        Pmvie()->MarkViews();
        AdjustCursor(pcmd->xp, pcmd->yp);
    }
    break;

    case toolSoonerLater:
    case toolNormalizeRot:
    case toolNormalizeSize:
    case toolCostumeCmid:
        break;

    case toolRecordSameAction: {
        bool fLonger;
        bool fStep;
        uint32_t tsCurrent = TsCurrent();
        uint32_t grfmaf = 0;
        bool fFrozen = FPure((pcmd->grfcust & fcustShift) && !(pcmd->grfcust & fcustCmd));
        BRS xrActrBefore = rZero;
        BRS yrActrBefore = rZero;
        BRS zrActrBefore = rZero;
        BMAT34 bmat34CamBefore;

        if ((pactr->Ptmpl() != _ptmplTool) && !(_ptmplTool->FIsTdt() && pactr->Ptmpl()->FIsTdt()))
        {
            return;
        }

        // 3DMMv1.0: If have stopped cycling cels
        if (_fCyclingCels && !(pcmd->grfcust & fcustCmd))
        {
            _fCyclingCels = fFalse;
            _tsLast = tsCurrent;
        }

        // 3DMMv1.0: Start recording unless we're cycling cels
        if (!_fCyclingCels && !pactr->FIsModeRecord() && pactr->FIsRecordValid(dxrWld, dyrWld, dzrWld, tsCurrent))
        {
            if (!pactr->FBeginRecord(tsCurrent, FRecordDefault(), _pactrRestore))
                break; // 3DMMv1.0: an Oom erc has already been pushed

            // 3DMMv1.0: If rerecording, nuke the remainder of the subroute
            if (FRecordDefault())
                pactr->DeleteFwdCore(fFalse);

            Pmvie()->Pmcc()->Recording(fTrue, FRecordDefault());
        }

        if (_fCyclingCels)
        {
            if (pcmd->grfcust & fcustCmd) // 3DMMv1.0: still cycling
            {
                if ((tsCurrent - _tsLast) < kdtsCycleCels)
                {
                    break;
                }
                _tsLast = tsCurrent;
                if (!pactr->FSetActionCore(pactr->AnidCur(), pactr->CelnCur() + 1, fFrozen))
                {
                    break; // 3DMMv1.0: an Oom erc has already been pushed
                }
                Pmvie()->MarkViews();
            }
        }
        else if (pactr->FIsModeRecord()) // 3DMMv1.0: just recording
        {
            if ((tsCurrent - _tsLast) < kdtsFrame)
            {
                break;
            }
            _tsLast = tsCurrent;
            if (fFrozen)
            {
                grfmaf |= fmafFreeze;
            }

            if (_fRespectGround)
            {
                grfmaf |= fmafGround;
            }

            if (_fContinueInPlace)
            {
                // Keep the ordinary Continue Animation recorder so cursor-driven
                // rotation, action timing, and cel advancement remain unchanged.
                // After the frame advances, cancel only the actor route movement
                // and replace it with the tracked camera's frame displacement.
                pactr->GetXyzWorld(&xrActrBefore, &yrActrBefore, &zrActrBefore);

                // FRecordMove does not advance the displayed BWLD camera by
                // itself.  Sample the applied camera at the current frame first.
                Pmvie()->ApplyCameraTrack();
                Pmvie()->Pbwld()->GetCamera(&bmat34CamBefore);
            }

            if (!pactr->FRecordMove(dxrWld, dyrWld, dzrWld, grfmaf, tsCurrent, &fLonger, &fStep, _pactrRestore))
            {
                // 3DMMv1.0: Oom erc already pushed
                break;
            }
            if (fLonger) // 3DMMv1.0: If a point was added to the path
            {
                if (_fContinueInPlace)
                {
                    BRS xrActrAfter;
                    BRS yrActrAfter;
                    BRS zrActrAfter;
                    BRS dxrComp;
                    BRS dyrComp;
                    BRS dzrComp;
                    BMAT34 bmat34CamAfter;
                    bool fMoved;

                    pactr->GetXyzWorld(&xrActrAfter, &yrActrAfter, &zrActrAfter);

                    // FRecordMove advanced the scene frame.  Reapply the .3ct
                    // camera so the second sample is the next displayed frame.
                    Pmvie()->ApplyCameraTrack();
                    Pmvie()->Pbwld()->GetCamera(&bmat34CamAfter);

                    dxrComp = BrsSub(BrsSub(bmat34CamAfter.m[3][0], bmat34CamBefore.m[3][0]),
                                      BrsSub(xrActrAfter, xrActrBefore));
                    dyrComp = BrsSub(BrsSub(bmat34CamAfter.m[3][1], bmat34CamBefore.m[3][1]),
                                      BrsSub(yrActrAfter, yrActrBefore));
                    dzrComp = BrsSub(BrsSub(bmat34CamAfter.m[3][2], bmat34CamBefore.m[3][2]),
                                      BrsSub(zrActrAfter, zrActrBefore));

                    if (!pactr->FMoveRoute(dxrComp, dyrComp, dzrComp, &fMoved, fmafNil))
                    {
                        // 3DMMv1.0: Oom erc already pushed
                        break;
                    }
                }

                // 3DMMv1.0: update scroll bars
                Pmvie()->Pmcc()->UpdateScrollbars();
                if (fStep && !_fContinueInPlace)
                    AdjustCursor(pcmd->xp, pcmd->yp);
                Pmvie()->MarkViews();
            }
        }
    }
    break;
    }
}

/** 3DMMv1.0: *************************************************************************
 *
 * Handle Mouseup
 *
 * Parameters:
 *  pcmd - The mouse command.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MVU::_MouseUp(CMD_MOUSE *pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PMVIE pmvie;
    PSCEN pscen;
    PACTR pactr = pvNil;
    PACTR pactrDup;
    PSUNA psuna;
    RC rcPlacement;
    bool fPlacementInView = fFalse;
    bool fModernKeepNewActor = fFalse;
    bool fRollCallAdded = fFalse;

    pmvie = Pmvie();
    AssertPo(pmvie, 0);

    _grfcust = fcustNil;

    if (Tool() != toolPlace || _fMouseDownSeen)
        RestoreKeyboardRepeat();

    if (_fPause)
    {

        Assert(Pmvie()->FPlaying(), "Bad Pause type");

        //
        // 3DMMv1.0: If we are pausing in the middle of playing, restart playing
        //
        if (!pmvie->Pclok()->FSetAlarm(0, pmvie))
        {
            CMD cmd;

            pmvie->SetFStopPlaying(fTrue);
            cmd.pcmh = pmvie;
            cmd.cid = cidAlarm;
            pmvie->FCmdAlarm(&cmd);
        }

        goto LEndTracking;
    }

    if (Pmvie()->FPlaying())
    {
        goto LEndTracking;
    }

    Pmvie()->Pmcc()->StopUISound();

    pscen = pmvie->Pscen();
    AssertNilOrPo(pscen, 0);
    if (pvNil == pscen)
    {
        goto LEndTracking;
    }

    if (_fMultiDeselectClick)
    {
        _fMultiDeselectClick = fFalse;
        goto LEndTracking;
    }

    pactr = pscen->PactrSelected();
    AssertNilOrPo(pactr, 0);
    if (pvNil != pactr && Tool() != toolPlace)
    {
        _ActorClicked(pactr, fFalse);
    }

    switch (Tool())
    {
    case toolDefault:
    case toolActorSelect:
        break;

    case toolPlace:

        if (!_fMouseDownSeen)
        {
            return;
        }

        pactr = Pmvie()->Pscen()->PactrSelected();
        AssertPo(pactr, 0);
        pactrDup = _pactrUndo;
        AssertNilOrPo(pactrDup, 0);

        SetTool(toolCompose);
        Pmvie()->Pmcc()->ChangeTool(toolCompose);

        //
        // Now check if the actor is out of view.  Legacy BRender populated
        // BODY bounds through its render-bounds callback; modern glrend does
        // not provide that callback itself, so v3s also logs the exact state
        // seen by this destructive placement decision.
        //
        pactr->GetRcBounds(&rcPlacement);
        fPlacementInView = pactr->FIsInView();
#if defined(BRENDER_MODERN_14)
        fModernKeepNewActor = (!fPlacementInView && _pactrUndo == pvNil && pactr->FOnStage());
#else
        fModernKeepNewActor = fFalse;
#endif
        DiagLogBRender(
            "MVU::_MouseUp toolPlace precommit actor=%p arid=%ld body=%p onstage=%d inview=%d bounds=(%ld,%ld)-(%ld,%ld) undo_actor=%p modern_keep=%d",
            pactr, (long)pactr->Arid(), pactr->Pbody(), (int)pactr->FOnStage(), (int)fPlacementInView,
            (long)rcPlacement.xpLeft, (long)rcPlacement.ypTop, (long)rcPlacement.xpRight,
            (long)rcPlacement.ypBottom, _pactrUndo, (int)fModernKeepNewActor);

        if (!fPlacementInView && !fModernKeepNewActor)
        {

            //
            // 3DMMv1.0: _pactrUndo is pvNil if this is a new actor, else it is
            // 3DMMv1.0: an actor from the roll call.
            //
            if (_pactrUndo != pvNil)
            {
                Pmvie()->Pscen()->FAddActrCore(_pactrUndo); // 3DMMv1.0: Replace old actor with saved version.
                vpcex->EnqueueCid(cidActorPlacedOutOfView, pvNil, pvNil, _pactrUndo->Arid());
                ReleasePpo(&_pactrUndo);
            }
            else
            {
                pactr->AddRef();
                if (!Pmvie()->F4DMMActorStudioConsumePreaddedPlacement(pactr))
                    AssertDo(Pmvie()->FAddToRollCall(pactr, pvNil), "Should never fail");
                Pmvie()->Pscen()->RemActrCore(pactr->Arid());
                vpcex->EnqueueCid(cidActorPlacedOutOfView, pvNil, pvNil, pactr->Arid());
                ReleasePpo(&pactr);
            }

            WarpCursToCenter();
            vpappb->ShowCurs();
            break;
        }
        else if (_pactrUndo == pvNil)
        {
            //
            // 3DMMv1.0: _pactrUndo is pvNil if this is a new actor, else it is
            // 3DMMv1.0: an actor from the roll call.
            //
            fRollCallAdded = Pmvie()->F4DMMActorStudioConsumePreaddedPlacement(pactr) ||
                             Pmvie()->FAddToRollCall(pactr, pvNil);
            DiagLogBRender("MVU::_MouseUp toolPlace FAddToRollCall actor=%p arid=%ld result=%d modern_fallback=%d",
                           pactr, (long)pactr->Arid(), (int)fRollCallAdded, (int)fModernKeepNewActor);
            AssertDo(fRollCallAdded, "Should never fail");
        }

        //
        // 3DMMv1.0: Now build an undo object for the placing of the actor
        //
        psuna = SUNA::PsunaNew();

        if ((psuna == pvNil) || ((_pactrUndo == pvNil) && !pactr->FDup(&pactrDup, fTrue)))
        {

            PushErc(ercSocNotUndoable);
            ReleasePpo(&pactrDup);
            Pmvie()->ClearUndo();
        }
        else
        {

            pactrDup->SetArid(pactr->Arid());
            psuna->SetType(_pactrUndo == pvNil ? utAdd : utRep);
            psuna->SetActr(pactrDup);
            if (_pactrUndo != pvNil)
            {
                pactrDup->AddRef();
            }

            if (!Pmvie()->FAddUndo(psuna))
            {
                PushErc(ercSocNotUndoable);
                Pmvie()->ClearUndo();
            }

            Pmvie()->Pmcc()->EnableActorTools();
        }

        ReleasePpo(&_pactrUndo);
        ReleasePpo(&psuna);
        WarpCursToActor(pactr);
        vpappb->ShowCurs();
        pactr->GetRcBounds(&rcPlacement);
        DiagLogBRender(
            "MVU::_MouseUp toolPlace COMMIT actor=%p arid=%ld body=%p onstage=%d inview=%d bounds=(%ld,%ld)-(%ld,%ld) selected=%p",
            pactr, (long)pactr->Arid(), pactr->Pbody(), (int)pactr->FOnStage(), (int)pactr->FIsInView(),
            (long)rcPlacement.xpLeft, (long)rcPlacement.ypTop, (long)rcPlacement.xpRight,
            (long)rcPlacement.ypBottom, Pmvie()->Pscen()->PactrSelected());
        vpcex->EnqueueCid(cidActorPlaced, pvNil, pvNil, pactr->Arid());
        break;

    case toolCompose:
    case toolRotateX:
    case toolRotateY:
    case toolRotateZ:
    case toolResize:
    case toolSquashStretch:
        if (pactr != pvNil)
        {
            WarpCursToActor(pactr);
            vpappb->ShowCurs();
        }
        break;

    case toolRecordSameAction:

        if (pvNil != pactr)
        {

            if ((pactr->Ptmpl() != _ptmplTool) && !(_ptmplTool->FIsTdt() && pactr->Ptmpl()->FIsTdt()))
            {
                break;
            }

            pactr->FEndRecord(FRecordDefault(), _pactrRestore); // 3DMMv1.0: On error, Oom already pushed
            ReleasePpo(&_pactrRestore);
            Pmvie()->InvalViewsAndScb();
            WarpCursToActor(pactr);
            vpappb->ShowCurs();
        }

        if (_fSetFRecordDefault)
        {
            _fSetFRecordDefault = fFalse;
            SetFRecordDefault(fTrue);
        }

        Pmvie()->Pmcc()->Recording(fFalse, FRecordDefault());

        break;

    case toolSoonerLater:
        if (pactr != pvNil)
        {
            Assert(pactr->FTimeFrozen(), "Something odd is going on");

            PACTR pactrDup;
            PAUND paund;

            paund = AUND::PaundNew();
            if ((paund == pvNil) || !pactr->FDup(&pactrDup, fTrue))
            {
                Pmvie()->ClearUndo();
                PushErc(ercSocNotUndoable);
            }
            else
            {
                paund->SetPactr(pactrDup);
                ReleasePpo(&pactrDup);
                paund->SetArid(pactr->Arid());
                paund->SetSoonerLater(fTrue);
                paund->SetNfrmLast(Pmvie()->Pscen()->Nfrm());

                if (!Pmvie()->FAddUndo(paund))
                {
                    Pmvie()->ClearUndo();
                    PushErc(ercSocNotUndoable);
                }
            }

            ReleasePpo(&paund);

            Pmvie()->Pbwld()->MarkDirty();
            Pmvie()->MarkViews();

            Pmvie()->Pmcc()->StartSoonerLater();
        }

        break;

    case toolNormalizeRot:
    case toolNormalizeSize:
    case toolCostumeCmid:
    case toolSceneNuke:
    case toolActorNuke:
    case toolSceneChop:
    case toolSceneChopBack:
    case toolCutObject:
    case toolCopyObject:
    case toolPasteObject:
    case toolCopyRte:
    case toolTboxPaintText:
    case toolTboxFillBkgd:
    case toolTboxMove:
    case toolTboxFont:
    case toolTboxSize:
    case toolTboxStyle:
    case toolActorEasel:
    case toolTboxStory:
    case toolTboxCredit:
    case toolSounder:
    case toolLooper:
    case toolMatcher:
        break;

    default:
        Bug("Tool unknown on mouse up");
    }
    AssertNilOrPo(_paund, 0);
    ReleasePpo(&_paund); // 3DMMv1.0: If you just did a mousedown then mouseup, we have a leftover
                         // 3DMMv1.0: undo object that we don't want.  So nuke it.

LEndTracking:

    vpcex->EndMouseTracking();

    // Light actors do not need to chase a dragged object every mouse packet,
    // but they must snap to the final transform as soon as the drag ends.
    Pmvie()->UpdateTestLightAttachment();
    if (Pmvie()->Pbwld() != pvNil)
        Pmvie()->Pbwld()->MarkDirty();
    Pmvie()->MarkViews();

    // 3DMMv1.0: Remove any cursor clipping we may have begun when mouse tracking started.
#ifdef WIN
    ClipCursor(NULL);
#endif // 3DMMv1.0: WIN
}

/** 3DMMv1.0: *************************************************************************
 *
 * Handles the Cut, Copy, Paste and Clear commands, by setting the appropriate
 * tool.
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it processed the command, else fFalse.
 *
 **************************************************************************/
bool MVU::FCmdClip(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PTBOX ptbox;
    PDOCB pdocb;
    bool fOV = fFalse;
    CMD cmd;

    //
    // 3DMMv1.0: Check for O-V model for text in text box.
    //
    if (Pmvie()->Pscen() != pvNil)
    {
        AssertPo(Pmvie()->Pscen(), 0);

        if (FTextMode())
        {
            ptbox = Pmvie()->Pscen()->PtboxSelected();

            if ((ptbox != pvNil) && !ptbox->FIsVisible())
            {
                ptbox = pvNil;
            }
        }
        else
        {
            ptbox = pvNil;
        }

        AssertNilOrPo(ptbox, 0);

        if (pcmd->cid == cidPasteTool && vpclip->FGetFormat(kclsACLP, &pdocb))
        {
            fOV = !((PACLP)pdocb)->FRouteOnly();
            ReleasePpo(&pdocb);
        }
        else if (((ptbox != pvNil) && ptbox->FTextSelected()) ||
                 ((pcmd->cid == cidPasteTool) && vpclip->FGetFormat(kclsTCLP)))
        {
            if (ptbox != pvNil)
            {
                CMD cmd = *pcmd;

                cmd.pcmh = ptbox->PddgGet(0);
                vpcex->EnqueueCmd(&cmd);
            }
            fOV = fTrue;
        }
    }

    cmd = *pcmd;

    switch (pcmd->cid)
    {
    case cidCutTool:
        if (fOV)
        {
            cmd.cid = cidCut;
            vpcex->EnqueueCmd(&cmd);
        }
        else
        {
            SetTool(toolCutObject);
        }
        break;

    case cidCopyTool:
        if (fOV)
        {
            cmd.cid = cidCopy;
            vpcex->EnqueueCmd(&cmd);
        }
        else
        {
            SetTool(toolCopyObject);
        }

        break;

    case cidPasteTool:
        if (fOV)
        {

            if (vpclip->FGetFormat(kclsTCLP) || vpclip->FGetFormat(kclsACLP))
            {
                FDoClip(toolPasteObject);
            }
            else
            {
                cmd.cid = cidPaste;
                vpcex->EnqueueCmd(&cmd);
            }
        }
        else
        {
            SetTool(toolPasteObject);
        }

        break;

    case cidCopyRoute:
        SetTool(toolCopyRte);
        break;

    case cidPaste:

        FDoClip(toolPasteObject);
        break;

    case cidShiftCut:
        _grfcust = fcustShift;
    case cidCut:
        FDoClip(toolCutObject);
        _grfcust = fcustNil;
        break;

    case cidShiftCopy:
        _grfcust = fcustShift;
    case cidCopy:
        FDoClip(toolCopyObject);
        _grfcust = fcustNil;
        break;

    default:
        Bug("Unknown command");
    }
    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Handles the Cut, Copy, Paste and Clear commands.
 *
 * Parameters:
 *  tool - The tool to apply.
 *
 * Returns:
 *  fTrue if it processed the command, else fFalse.
 *
 **************************************************************************/
bool MVU::FDoClip(int32_t tool)
{
    AssertThis(0);

    PDOCB pdocb = pvNil;

    switch (tool)
    {
    case toolCutObject:
    case toolCopyObject:
    case toolCopyRte:

        if (tool == toolCutObject && Pmvie()->Pscen() != pvNil)
        {
            PACTR pactrLight = Pmvie()->Pscen()->PactrSelected();
            LIGHTLAB light;
            if (pactrLight != pvNil &&
                Pmvie()->FGetLightLabConfig(Pmvie()->Iscen(), pactrLight->Arid(), &light))
            {
                // Light objects are scene-wide authoring objects.  Copy is
                // fine, but Cut would create a frame-local lifetime which a
                // scene-wide light cannot represent.  Delete remains valid.
                return fTrue;
            }
        }

        //
        // 3DMMv1.0: copy the selection
        //
        if (!_FCopySel(&pdocb, tool == toolCopyRte))
        {
            return fTrue;
        }
        vpclip->Set(pdocb);
        ReleasePpo(&pdocb);

        if (tool == toolCutObject)
        {
            _ClearSel();
        }

        Pmvie()->Pmcc()->PlayUISound(tool);

        break;

    case toolPasteObject:
        if (!vpclip->FDocIsClip(pvNil))
        {
            PTCLP ptclp;

            if (vpclip->FGetFormat(kclsTCLP, (PDOCB *)&ptclp))
            {
                AssertPo(ptclp, 0);

                if (Pmvie()->Pscen() == pvNil)
                {
                    ReleasePpo(&ptclp);
                    return (fFalse);
                }

                if (ptclp->FPaste(Pmvie()->Pscen()))
                {
                    ReleasePpo(&ptclp);
                    Pmvie()->Pmcc()->EnableTboxTools();
                    Pmvie()->Pmcc()->PlayUISound(tool);
                    return (fTrue);
                }

                ReleasePpo(&ptclp);
                Pmvie()->Pmcc()->PlayUISound(tool);
                return (fFalse);
            }
            else
            {
                _FPaste(vpclip);
                Pmvie()->Pmcc()->PlayUISound(tool);
            }
        }
        break;

    default:
        Bug("Bad Tool");
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Handles the Undo and Redo commands.
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it processed the command, else fFalse.
 *
 **************************************************************************/
bool MVU::FCmdUndo(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    bool fRet;

    MVIE::LightEditorLog(Pmvie(), "undo_command begin cid=%ld undo_count=%ld redo_count=%ld",
                         (long)pcmd->cid, (long)Pmvie()->CundbUndo(), (long)Pmvie()->CundbRedo());
    if (pcmd->cid == cidUndo)
    {
        Pmvie()->Pmcc()->PlayUISound(toolUndo);
    }
    else
    {
        Pmvie()->Pmcc()->PlayUISound(toolRedo);
    }

    fRet = MVU_PAR::FCmdUndo(pcmd);
    MVIE::LightEditorLog(Pmvie(), "undo_command end cid=%ld result=%d undo_count=%ld redo_count=%ld",
                         (long)pcmd->cid, (int)fRet,
                         (long)Pmvie()->CundbUndo(), (long)Pmvie()->CundbRedo());
    Pmvie()->RefreshTestLight();
    Pmvie()->UpdateTestLightAttachment();
    Pmvie()->Pmcc()->SetUndo(Pmvie()->CundbUndo() != 0   ? undoUndo
                             : Pmvie()->CundbRedo() != 0 ? undoRedo
                                                         : undoDisabled);

    Pmvie()->InvalViewsAndScb();
    return (fRet);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Handles the Copying whatever is currently selected.
 *
 * Don't worry about tboxes, because if a tbox is selected it will
 * get the cut/copy/paste command.
 *
 * Parameters:
 *  ppdocb - Pointer to a place to store a pointer to the resulting docb.
 *	fRteOnly - fTrue if to copy an actors route only, else fFalse.
 *
 * Returns:
 *  fTrue if it was successful, else fFalse.
 *
 **************************************************************************/
bool MVU::_FCopySel(PDOCB *ppdocb, bool fRteOnly)
{
    AssertThis(0);

    PACTR pactr;
    PACLP paclp;

    if (FTextMode())
    {
        return (fFalse);
    }

    if (Pmvie()->Pscen() == pvNil)
    {
        return (fFalse);
    }

    pactr = Pmvie()->Pscen()->PactrSelected();
    AssertNilOrPo(pactr, 0);

    if ((pactr == pvNil) || !pactr->FIsInView())
    {
        PushErc(ercSocNoActrSelected);
        return (fFalse);
    }

    paclp = ACLP::PaclpNew(pactr, fRteOnly, FPure(_grfcust & fcustShift));
    AssertNilOrPo(paclp, 0);

    *ppdocb = (PDOCB)paclp;

    return (paclp != pvNil);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Handles the deleting whatever is currently selected.
 *
 * Don't worry about tboxes, because if a tbox is selected it will
 * get the cut/copy/paste command.
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MVU::_ClearSel()
{
    AssertThis(0);

    PACTR pactr;
    bool fAlive;
    bool fEnableSounds;

    if (Pmvie()->Pscen() == pvNil)
    {
        return;
    }

    pactr = Pmvie()->Pscen()->PactrSelected();

    if (pactr == pvNil)
    {
        PushErc(ercSocNoActrSelected);
        return;
    }

    AssertPo(pactr, 0);

    // A bound Object Group is one editing object. Deleting any one of its
    // members therefore deletes every member with the ordinary actor-delete
    // semantics, then removes the group itself. Capture membership before the
    // actor undo records so Undo naturally restores the actors first and the
    // group relationship last.
    int32_t idObjectGroup = 0;
    if (MVIE::FMultiSelectMode() &&
        Pmvie()->FObjectInObjectGroup(pactr->Arid(), &idObjectGroup) && idObjectGroup > 0)
    {
        const int32_t cMember = Pmvie()->CObjectGroupMembers(idObjectGroup);
        int32_t *rgarid = pvNil;
        if (cMember <= 0 ||
            !FAllocPv((void **)&rgarid, LwMul(cMember, SIZEOF(int32_t)), fmemClear, mprNormal))
            return;

        int32_t cArid = 0;
        for (int32_t iMember = 0; iMember < cMember; ++iMember)
        {
            const OBJECTGROUPMEMBER *pmember = Pmvie()->PObjectGroupMember(idObjectGroup, iMember);
            if (pmember != pvNil)
                rgarid[cArid++] = pmember->arid;
        }
        if (cArid <= 0)
        {
            FreePpv((void **)&rgarid);
            return;
        }

        PGUND pgund = GUND::PgundNew();
        if (pgund == pvNil || !pgund->FCaptureMembership(Pmvie(), PszLit("Delete Object Group")) ||
            !Pmvie()->FAddUndo(pgund))
        {
            ReleasePpo(&pgund);
            FreePpv((void **)&rgarid);
            Pmvie()->ClearUndo();
            PushErc(ercSocNotUndoable);
            return;
        }
        ReleasePpo(&pgund);

        if (!Pmvie()->FUnbindObjectGroup(idObjectGroup))
        {
            FreePpv((void **)&rgarid);
            return;
        }

        fEnableSounds = !(FPure(Pmvie()->Pscen()->GrfScen() & fscenSounds));
        Pmvie()->Pscen()->Disable(fscenSounds);
        int32_t cDeleted = 0;
        for (int32_t iMember = 0; iMember < cArid; ++iMember)
        {
            PACTR pactrMember = Pmvie()->Pscen()->PactrFromArid(rgarid[iMember]);
            if (pactrMember == pvNil)
                continue;

            LIGHTLAB lightMember;
            const bool fLightMember =
                Pmvie()->FGetLightLabConfig(Pmvie()->Iscen(), pactrMember->Arid(), &lightMember);
            bool fMemberAlive = fFalse;
            if (!pactrMember->FDelete(&fMemberAlive,
                                      fLightMember || FPure(_grfcust & fcustShift)))
                continue;

            if (!fMemberAlive)
            {
                const int32_t aridDeleted = pactrMember->Arid();
                Pmvie()->Pscen()->RemActrCore(aridDeleted);
                if (fLightMember)
                    Pmvie()->FRemoveLightLabConfigCore(Pmvie()->Iscen(), aridDeleted);
            }
            else
            {
                // Match the normal single-object Delete path: a surviving
                // actor route is taken offstage at the cut point.
                pactrMember->FRemFromStageCore();
            }
            ++cDeleted;
        }
        if (fEnableSounds)
            Pmvie()->Pscen()->Enable(fscenSounds);

        MVIE::MultiLog(Pmvie(), "group_delete id=%ld members=%ld deleted=%ld",
                 (long)idObjectGroup, (long)cArid, (long)cDeleted);
        FreePpv((void **)&rgarid);
        Pmvie()->Pscen()->MarkDirty();
        Pmvie()->UpdateTestLightAttachment();
        if (Pmvie()->Pbwld() != pvNil)
            Pmvie()->Pbwld()->MarkDirty();
        Pmvie()->InvalViewsAndScb();
        return;
    }

    LIGHTLAB lightDeleted;
    bool fLightObject = Pmvie()->FGetLightLabConfig(Pmvie()->Iscen(), pactr->Arid(), &lightDeleted);

    fEnableSounds = !(FPure(Pmvie()->Pscen()->GrfScen() & fscenSounds));
    Pmvie()->Pscen()->Disable(fscenSounds);
    if (!pactr->FDelete(&fAlive, fLightObject || FPure(_grfcust & fcustShift)))
    {
        if (fEnableSounds)
            Pmvie()->Pscen()->Enable(fscenSounds);
        return;
    }
    if (fEnableSounds)
        Pmvie()->Pscen()->Enable(fscenSounds);

    if (!fAlive)
    {
        int32_t aridDeleted = pactr->Arid();
        Pmvie()->Pscen()->RemActrCore(aridDeleted);
        if (fLightObject)
            Pmvie()->FRemoveLightLabConfigCore(Pmvie()->Iscen(), aridDeleted);
    }
    else
    {
        //
        // 3DMMv1.0: According to design, if this fails, it is
        // 3DMMv1.0: ok to leave the actor on the stage.
        //
        pactr->FRemFromStageCore();
    }

    Pmvie()->Pscen()->MarkDirty();
    Pmvie()->InvalViews();
}

/** 3DMMv1.0: *************************************************************************
 *
 * Handles the Pasting whatever is currently in the clipboard.
 *
 * Don't worry about tboxes, because if a tbox is selected it will
 * get the cut/copy/paste command.
 *
 * Parameters:
 *  pdocb - The pointer to the resulting docb.
 *
 * Returns:
 *  fTrue if it was successful, else fFalse.
 *
 **************************************************************************/
bool MVU::_FPaste(PCLIP pclip)
{
    AssertThis(0);
    AssertPo(pclip, 0);

    PACLP paclp;
    PTCLP ptclp;
    PACTR pactr;
    bool fRet;

    if (pclip->FGetFormat(kclsACLP, (PDOCB *)&paclp))
    {
        AssertPo(paclp, 0);

        if (Pmvie()->Pscen() == pvNil)
        {
            PushErc(ercSocNoScene);
            return (fFalse);
        }

        if (paclp->FRouteOnly() && FTextMode())
        {
            PushErc(ercSocCannotPasteThatHere);
            ReleasePpo(&paclp);
            return (fTrue);
        }

        pactr = Pmvie()->Pscen()->PactrSelected();
        AssertNilOrPo(pactr, 0);

        if (paclp->FRouteOnly() && ((pactr == pvNil) || !pactr->FIsInView()))
        {
            PushErc(ercSocNoActrSelected);
            ReleasePpo(&paclp);
            return (fTrue);
        }

        fRet = paclp->FPaste(Pmvie());
        ReleasePpo(&paclp);
        return fRet;
    }

    if (pclip->FGetFormat(kclsTCLP, (PDOCB *)&ptclp))
    {
        AssertPo(ptclp, 0);

        if (Pmvie()->Pscen() == pvNil)
        {
            PushErc(ercSocNoScene);
            return (fFalse);
        }

        fRet = ptclp->FPaste(Pmvie()->Pscen());
        ReleasePpo(&ptclp);
        return fRet;
    }

    PushErc(ercSocCannotPasteThatHere);
    return (fFalse);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Handle a close command.
 *
 * Parameters:
 *	fAssumeYes - Should the dialog assume yes.
 *
 * Returns:
 * 	fTrue if the client should close this document.
 *
 **************************************************************************/
bool MVU::FCloseDoc(bool fAssumeYes, bool fSaveDDG)
{
    AssertThis(0);
    bool fRet;
    FNI fni;

    //
    // 3DMMv1.0: FQueryClose calls FAutosave depending on the result of the query.
    // 3DMMv1.0: FAutosave needs to know whether the doc is closing as unused user
    // 3DMMv1.0: sounds are to be deleted from the movie only upon close.
    //
    Pmvie()->SetDocClosing(fTrue);
    // 3DMMv1.0: If not dirty, flush snds on close without user query
    // 3DMMv1.0: Irrelevant if there are no user sounds in the movie or if
    // 3DMMv1.0: the file is read-only (can't save to the original file)
    if (!Pmvie()->FDirty() && Pmvie()->FUnusedSndsUser() && !Pmvie()->FReadOnly() && Pmvie()->FGetFni(&fni))
    {
        vpappb->BeginLongOp();
        fRet = _pdocb->FSave(); // 3DMMv1.0: Flush sounds
        goto LSaved;
    }

    if (Pmvie()->Cscen() > 0)
    {
        fRet = _pdocb->FQueryClose(fAssumeYes ? fdocAssumeYes : fdocNil);
    }
    else
    {
        fRet = fTrue;
    }
    vpappb->BeginLongOp();

LSaved:
    Pmvie()->SetDocClosing(fFalse);
    if (fRet && !fSaveDDG)
    {
        // 3DMMv1.0: Beware: the following line destroys the this pointer!
        _pdocb->CloseAllDdg();
    }
    vpappb->EndLongOp();
    return fRet;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Handle a save, save as or save a copy command.
 *
 * Parameters:
 *	pcmd - The command to process
 *
 * Returns:
 *  fTrue.
 *
 **************************************************************************/
bool MVU::FCmdSave(PCMD pcmd)
{
#if defined(KAUAI_WIN32)
    // 4DMM object selectability chord. Keep menu/button Save behavior intact;
    // only the actual Ctrl+S accelerator is repurposed when exactly one scene
    // object is selected.
    if (pcmd != pvNil && pcmd->cid == cidSave && GetAsyncKeyState(VK_CONTROL) < 0 &&
        GetAsyncKeyState('S') < 0 && Pmvie()->Pscen() != pvNil &&
        Pmvie()->Pscen()->CactrSelected() == 1)
    {
        PACTR pactr = Pmvie()->Pscen()->PactrSelected();
        if (pactr != pvNil)
        {
            Pmvie()->FSetObjectSelectable(pactr->Arid(), fFalse);
            return fTrue;
        }
    }
#endif

    if (Pmvie()->Cscen() < 1)
    {
        PushErc(ercSocSaveFailure);
    }
    else
    {
        _pdocb->FSave(pcmd->cid);
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Note that we have had an idle loop.
 *
 * Parameters:
 *	pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fFalse.
 *
 ***************************************************************************/
bool MVU::FCmdLightLab(PCMD pcmd)
{
    AssertThis(0);
    TrashVar(pcmd);
#if defined(KAUAI_WIN32)
    FOpenLightLab(Pmvie());
#endif
    return fTrue;
}

bool MVU::FCmdIdle(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    Pmvie()->SetFIdleSeen(fTrue);

#if defined(KAUAI_WIN32)
    bool fManual = Pmvie()->FManualCameraMode();
    bool fFreeLook = Pmvie()->FFreeLookMode();

    if (fManual || fFreeLook)
    {
        bool fInvalid = Pmvie()->Pscen() == pvNil;
        if (fManual)
        {
            fInvalid = fInvalid || Pmvie()->FDepthMotionTweenActive() ||
                       (Pmvie()->FPlaying() && !Pmvie()->FManualCameraRecording());
        }
        else
        {
            fInvalid = fInvalid || Pmvie()->FPlaying() || !FActorsAndPropsTabOpen();
        }

        if (fInvalid)
        {
            if (fManual)
                Pmvie()->FSetManualCameraMode(fFalse);
            else
                Pmvie()->EndFreeLook(fTrue);
            Pmvie()->Pmcc()->UpdateScrollbars();
            return fFalse;
        }

        HWND hwndForeground = GetForegroundWindow();
        HWND hwndRootView = GetAncestor(HwndContainer(), GA_ROOT);
        HWND hwndRootApp = vwig.hwndApp == hNil ? hNil : GetAncestor(vwig.hwndApp, GA_ROOT);
        HWND hwndRootForeground = hwndForeground == hNil ? hNil : GetAncestor(hwndForeground, GA_ROOT);
        if (!F4DMMEditorInputForeground(hwndRootForeground, hwndRootView, hwndRootApp))
        {
            // The Scene-tab button lives in the editor shell while Manual
            // Camera captures the separate viewport.  Both top-level windows
            // belong to this session and are valid foreground roots.  Only an
            // actual switch to another application should cancel input.
            if (fManual)
                Pmvie()->FSetManualCameraMode(fFalse);
            else
                Pmvie()->EndFreeLook(fFalse);
            Pmvie()->Pmcc()->UpdateScrollbars();
            return fFalse;
        }

        if (!_fManualCameraCaptured)
            BeginManualCameraInput();
        ++vcCameraInputPerfIdle;

        if (fManual)
        {
            bool fEscDown = GetAsyncKeyState(VK_ESCAPE) < 0;
            if (fEscDown)
            {
                Pmvie()->CancelManualCameraEdit();
                Pmvie()->Pmcc()->UpdateScrollbars();
                return fFalse;
            }

            bool fEnterDown = GetAsyncKeyState(VK_RETURN) < 0;
            bool fTabDown = GetAsyncKeyState(VK_TAB) < 0;
            if ((fEnterDown || fTabDown) && !_fManualCameraEnterDown &&
                !Pmvie()->FManualCameraRecording())
            {
                Pmvie()->FCommitManualCameraEdit(fTrue);
                Pmvie()->Pmcc()->UpdateScrollbars();
                return fFalse;
            }
            _fManualCameraEnterDown = fEnterDown || fTabDown;
        }
        else
        {
            // Free Cam exit keys:
            //   F   = exit and keep the temporary camera position
            //   Tab = exit and keep the temporary camera position
            //   Esc = exit and restore the frame's saved camera position
            bool fTabExit = (GetAsyncKeyState(VK_TAB) & 0x8001) != 0;
            bool fEscExit = (GetAsyncKeyState(VK_ESCAPE) & 0x8001) != 0;
            if (fTabExit || fEscExit)
            {
                if (fEscExit)
                    _fDeselectEscapeDown = fTrue;
                Pmvie()->EndFreeLook(fEscExit);
                Pmvie()->Pmcc()->UpdateScrollbars();
                return fFalse;
            }
        }

        bool fShiftDown = GetAsyncKeyState(VK_SHIFT) < 0;
        if (fShiftDown && !_fManualCameraShiftDown)
            _fManualCameraFly = !_fManualCameraFly;
        _fManualCameraShiftDown = fShiftDown;

        if (fManual)
        {
            bool fRDown = GetAsyncKeyState('R') < 0;
            if (fRDown && !_fManualCameraRDown)
            {
                if (Pmvie()->FManualCameraRecording())
                    Pmvie()->RequestStopManualCameraRecording();
                else if (!Pmvie()->FPlaying())
                    Pmvie()->FStartManualCameraRecording();
            }
            _fManualCameraRDown = fRDown;

            if (!Pmvie()->FPlaying())
            {
                bool fEDown = GetAsyncKeyState('E') < 0;
                if (fEDown && !_fManualCameraEDown)
                {
                    if (Pmvie()->FCommitManualCameraEdit(fTrue))
                        Pmvie()->FOpenManualCameraFrameEditor();
                    Pmvie()->Pmcc()->UpdateScrollbars();
                    _fManualCameraEDown = fTrue;
                    return fFalse;
                }
                _fManualCameraEDown = fEDown;

                bool fLeftDown = GetAsyncKeyState(VK_LEFT) < 0;
                bool fRightDown = GetAsyncKeyState(VK_RIGHT) < 0;
                if (fLeftDown && !_fManualCameraLeftDown)
                    Pmvie()->FStepManualCamera(-1);
                else if (fRightDown && !_fManualCameraRightDown)
                    Pmvie()->FStepManualCamera(1);
                _fManualCameraLeftDown = fLeftDown;
                _fManualCameraRightDown = fRightDown;

                bool fCtrlDown = GetAsyncKeyState(VK_CONTROL) < 0;
                bool fDDown = GetAsyncKeyState('D') < 0;
                if (fCtrlDown && !_fManualCameraCtrlDown)
                    _fManualCameraCtrlChordUsed = fFalse;

                if (fCtrlDown && fDDown &&
                    (!_fManualCameraCtrlDown || !_fManualCameraDDown))
                {
                    Pmvie()->FDeleteManualCameraFrameCurrent();
                    _fManualCameraCtrlChordUsed = fTrue;
                    Pmvie()->Pmcc()->UpdateScrollbars();
                }

                // Ctrl by itself retains the old "append this manual-camera
                // frame" behavior, but commit it on release so Ctrl+D can be
                // an unambiguous delete chord.
                if (!fCtrlDown && _fManualCameraCtrlDown && !_fManualCameraCtrlChordUsed)
                    Pmvie()->FAppendManualCameraFrame();
                if (!fCtrlDown)
                    _fManualCameraCtrlChordUsed = fFalse;
                _fManualCameraCtrlDown = fCtrlDown;
                _fManualCameraDDown = fDDown;
            }
        }
        else
        {
            bool fFDown = GetAsyncKeyState('F') < 0;
            bool fLButtonDown = GetAsyncKeyState(VK_LBUTTON) < 0;
            if ((fFDown && !_fFreeLookFDown) ||
                (fLButtonDown && !_fFreeLookLButtonDown))
            {
                // F, Tab, and left click all end Free Cam identically: keep the
                // temporary viewpoint.  Preserve held-input latches after
                // EndManualCameraInput() resets them so the same input cannot
                // immediately re-trigger while it is still physically down.
                Pmvie()->EndFreeLook(fFalse);
                _fFreeLookFDown = fFDown;
                _fFreeLookLButtonDown = fLButtonDown;
                Pmvie()->Pmcc()->UpdateScrollbars();
                return fFalse;
            }
            _fFreeLookFDown = fFDown;
            _fFreeLookLButtonDown = fLButtonDown;
        }

        // Mouse look, Q/E roll, and WASD may all change the camera in this
        // same idle tick.  Update state first and render once at the end.
        // Previously look + WASD each called MarkViews(), rendering the entire
        // lit scene twice and starving Raw Input until its pitch delta dumped.
        bool fCameraViewDirty = fFalse;

        // Poll only the latest coalesced cursor position once per idle tick.
        // This is shared by Free Cam and Manual Camera, so both camera modes
        // avoid the old high-poll-rate WM_INPUT message flood.
        RECT rcClient;
        POINT ptTopLeft;
        POINT ptBottomRight;
        HWND hwndContainer = HwndContainer();
        HWND hwndInput = Hwnd4DMMScaledInput(hwndContainer);
        if (GetClientRect(hwndInput, &rcClient))
        {
            ptTopLeft.x = rcClient.left;
            ptTopLeft.y = rcClient.top;
            ptBottomRight.x = rcClient.right;
            ptBottomRight.y = rcClient.bottom;
            ClientToScreen(hwndInput, &ptTopLeft);
            ClientToScreen(hwndInput, &ptBottomRight);
            int32_t xpCenter = (ptTopLeft.x + ptBottomRight.x) / 2;
            int32_t ypCenter = (ptTopLeft.y + ptBottomRight.y) / 2;
            POINT ptCursor;
            GetCursorPos(&ptCursor);
            int32_t dxp = ptCursor.x - xpCenter;
            int32_t dyp = ptCursor.y - ypCenter;
            if (dxp != 0 || dyp != 0)
            {
                ++vcCameraInputPerfLook;
                const int32_t dxpAbs = dxp < 0 ? -dxp : dxp;
                const int32_t dypAbs = dyp < 0 ? -dyp : dyp;
                if (dxpAbs > vdxCameraInputPerfMax)
                    vdxCameraInputPerfMax = dxpAbs;
                if (dypAbs > vdyCameraInputPerfMax)
                    vdyCameraInputPerfMax = dypAbs;
                const float flMouseSensitivity = Pmvie()->CameraMouseSensitivity();
                if (Pmvie()->FLookManualCamera(
                        (float)-dxp * flMouseSensitivity,
                        (float)-dyp * flMouseSensitivity,
                        fFalse))
                    fCameraViewDirty = fTrue;
                if (hwndInput != hwndContainer)
                    SetCursorPos(xpCenter, ypCenter);
                else
                    vpappb->PositionCurs(xpCenter, ypCenter);
            }
        }

        uint32_t ts = TsCurrent();
        uint32_t dts = 0;
        if (_tsManualCameraLast == 0)
        {
            _tsManualCameraLast = ts;
        }
        else
        {
            dts = ts - _tsManualCameraLast;
            _tsManualCameraLast = ts;
            if (dts > 100)
                dts = 100;
        }

        if (fFreeLook)
        {
            float rollDirection = 0.0f;
            if (GetAsyncKeyState('Q') < 0)
                rollDirection -= 1.0f;
            if (GetAsyncKeyState('E') < 0)
                rollDirection += 1.0f;
            if (rollDirection != 0.0f)
            {
                if (Pmvie()->FRollManualCamera(
                        rollDirection * kdFreeLookRollDegreesPerSecond *
                        (float)dts / (float)kdtsSecond, fFalse))
                    fCameraViewDirty = fTrue;
            }
        }

        float forward = 0.0f;
        float strafe = 0.0f;
        if (GetAsyncKeyState('W') < 0)
            forward += 1.0f;
        if (GetAsyncKeyState('S') < 0)
            forward -= 1.0f;
        if (GetAsyncKeyState('D') < 0 && !(fManual && GetAsyncKeyState(VK_CONTROL) < 0))
            strafe += 1.0f;
        if (GetAsyncKeyState('A') < 0)
            strafe -= 1.0f;

        if (forward != 0.0f || strafe != 0.0f)
        {
            float scale = kdManualCameraUnitsPerSecond * Pmvie()->CameraMoveSpeed() *
                          (float)dts / (float)kdtsSecond;
            if (forward != 0.0f && strafe != 0.0f)
                scale *= 0.70710678118f;
            if (Pmvie()->FMoveManualCamera(forward * scale, strafe * scale,
                                             _fManualCameraFly, fFalse))
                fCameraViewDirty = fTrue;
        }

        if (fCameraViewDirty)
        {
            // Render first, then synchronously flush only the already-invalid
            // visible presentation surface.  The native renderer deliberately
            // invalidates instead of painting from inside its BRender frame to
            // avoid re-entry.  At this point that frame has fully returned, so
            // UpdateWindow is safe and prevents a continuous mouse-message
            // stream from starving WM_PAINT while Free/Manual Camera is moving.
            const uint32_t tsRenderBegin = TsCurrent();
            Pmvie()->ApplyCameraTrack();
            Pmvie()->MarkViews();
            const uint32_t dtsRender = TsCurrent() - tsRenderBegin;
            vdtsCameraInputPerfRender += dtsRender;
            if (dtsRender > vdtsCameraInputPerfRenderMax)
                vdtsCameraInputPerfRenderMax = dtsRender;
            ++vcCameraInputPerfRender;

#if defined(KAUAI_WIN32)
            HWND hwndPresentation = Hwnd4DMMScaledInput(hNil);
            if (hwndPresentation != hNil && IsWindow(hwndPresentation) &&
                GetUpdateRect(hwndPresentation, pvNil, fFalse))
            {
                ++vcCameraInputPerfPaintPending;
                const uint32_t tsPaintBegin = TsCurrent();
                if (UpdateWindow(hwndPresentation))
                    ++vcCameraInputPerfPaintFlush;
                const uint32_t dtsPaint = TsCurrent() - tsPaintBegin;
                vdtsCameraInputPerfPaint += dtsPaint;
                if (dtsPaint > vdtsCameraInputPerfPaintMax)
                    vdtsCameraInputPerfPaintMax = dtsPaint;
            }
#endif
        }

        if (vtsCameraInputPerfStart == 0)
            vtsCameraInputPerfStart = ts;
        else if (ts - vtsCameraInputPerfStart >= 1000)
        {
            MVIE::MultiLog(Pmvie(),
                "camera_input perf backend=coalesced_cursor mode=%s elapsed_ms=%lu idle=%lu look=%lu renders=%lu render_ms=%lu render_max_ms=%lu paint_pending=%lu paint_flush=%lu paint_ms=%lu paint_max_ms=%lu max_delta=%ldx%ld",
                fFreeLook ? "freecam" : "manual",
                (unsigned long)(ts - vtsCameraInputPerfStart),
                (unsigned long)vcCameraInputPerfIdle, (unsigned long)vcCameraInputPerfLook,
                (unsigned long)vcCameraInputPerfRender,
                (unsigned long)vdtsCameraInputPerfRender, (unsigned long)vdtsCameraInputPerfRenderMax,
                (unsigned long)vcCameraInputPerfPaintPending, (unsigned long)vcCameraInputPerfPaintFlush,
                (unsigned long)vdtsCameraInputPerfPaint, (unsigned long)vdtsCameraInputPerfPaintMax,
                (long)vdxCameraInputPerfMax, (long)vdyCameraInputPerfMax);
            vtsCameraInputPerfStart = ts;
            vcCameraInputPerfIdle = 0;
            vcCameraInputPerfLook = 0;
            vcCameraInputPerfRender = 0;
            vcCameraInputPerfPaintFlush = 0;
            vcCameraInputPerfPaintPending = 0;
            vdtsCameraInputPerfRender = 0;
            vdtsCameraInputPerfPaint = 0;
            vdtsCameraInputPerfRenderMax = 0;
            vdtsCameraInputPerfPaintMax = 0;
            vdxCameraInputPerfMax = 0;
            vdyCameraInputPerfMax = 0;
        }
    }
    else
    {
        if (_fManualCameraCaptured)
            EndManualCameraInput();

        // Keep all bound Light Lab objects following their actors/props during
        // ordinary editor interaction.  Ctrl+L itself is handled by the normal
        // Kauai accelerator table so it cannot interfere with Ctrl+C/V/Z/etc.
        Pmvie()->UpdateTestLightAttachment();

        // F/Tab intentionally leave the temporary viewpoint available for
        // editing, but only inside Actors & Props.  Once that workspace closes,
        // discard the override before another editor subsystem sees stale
        // camera state.  Esc resets the viewpoint immediately while Free Cam
        // is active and therefore leaves no retained override here.
        if (Pmvie()->FFreeLookOverride() && !FActorsAndPropsTabOpen())
        {
            Pmvie()->EndFreeLook(fTrue);
            Pmvie()->Pmcc()->UpdateScrollbars();
            return fFalse;
        }

        HWND hwndForeground = GetForegroundWindow();
        HWND hwndRootForeground = hwndForeground == hNil ? hNil : GetAncestor(hwndForeground, GA_ROOT);
        HWND hwndRootView = GetAncestor(HwndContainer(), GA_ROOT);
        HWND hwndRootApp = vwig.hwndApp == hNil ? hNil : GetAncestor(vwig.hwndApp, GA_ROOT);
        const bool fEditorInputForeground = F4DMMEditorInputForeground(hwndRootForeground, hwndRootView, hwndRootApp);

        // With normal cursor input active, Esc deselects the current object only
        // when the main 4DMM app/view owns focus. Native tool windows handle
        // their own Escape key and must never leak the same press into the main
        // viewport selection state.
        bool fEscDown = GetAsyncKeyState(VK_ESCAPE) < 0;
        if (fEditorInputForeground && fEscDown && !_fDeselectEscapeDown && Pmvie()->Pscen() != pvNil &&
            Pmvie()->Pscen()->PactrSelected() != pvNil)
        {
            Pmvie()->Pscen()->SelectActr(pvNil);
            Pmvie()->InvalViewsAndScb();
        }
        _fDeselectEscapeDown = fEscDown;

        bool fFDown = GetAsyncKeyState('F') < 0;
        if (fFDown && !_fFreeLookFDown && !Pmvie()->FPlaying() && fEditorInputForeground &&
            Pmvie()->Pscen() != pvNil && FActorsAndPropsTabOpen() && !FActorEditEaselOpen())
        {
            const bool fOppositeStartPolicy = GetAsyncKeyState(VK_CONTROL) < 0;
            Pmvie()->FStartFreeLook(fOppositeStartPolicy);
        }
        _fFreeLookFDown = fFDown;
    }
#endif

    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
 *
 * Note that the mouse is no longer on the view.
 *
 * Parameters:
 *	pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fFalse.
 *
 ***************************************************************************/
bool MVU::FCmdRollOff(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    if (_fMouseOn && (Tool() == toolListener))
    {
        Pmvie()->Pmsq()->StopAll();
    }

    _fMouseOn = fFalse;
    _fManualCameraMousePrimed = fFalse;

    return (fFalse);
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
 *
 * Assert the validity of the MVU.
 *
 * Parameters:
 *  grf - Bit field of options
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MVU::AssertValid(uint32_t grf)
{
    MVU_PAR::AssertValid(fobjAllocated);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Mark memory used by the MVU
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MVU::MarkMem(void)
{
    AssertThis(0);
    MVU_PAR::MarkMem();
    MarkMemObj(Pmvie());
    MarkMemObj(_paund);
    MarkMemObj(_pactrUndo);
}
#endif // 3DMMv1.0: DEBUG

//
//
//
// 3DMMv1.0: UNDO STUFF
//
//
//


PMUNC MUNC::PmuncNew(void)
{
    return NewObj MUNC();
}

MUNC::~MUNC(void)
{
    AssertBaseThis(0);
    FreePpv((void **)&_pctstate);
}

bool MUNC::FSave(PMVIE pmvie)
{
    AssertPo(pmvie, 0);
    if (!FAllocPv((void **)&_pctstate, SIZEOF(CTSTATE), fmemClear, mprNormal))
        return fFalse;
    return pmvie->FGetCameraTrackState(_pctstate);
}

void MUNC::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    if (_stnUndoName.Cch() > 0)
        *pstn = _stnUndoName;
    else
        pstn->SetSz(PszLit("Camera Change"));
}

bool MUNC::FDo(PDOCB pdocb)
{
    AssertThis(0);
    if (_pmvie == pvNil || _pctstate == pvNil)
        return fFalse;
    STN stnUndo;
    GetUndoName(&stnUndo);
    MVIE::LightEditorLog(_pmvie, "camera_light_undo do name=%s scene=%ld frame=%ld",
                         stnUndo.Psz(), (long)_iscen, (long)_nfrm);
    _pmvie->SwapCameraTrackState(_pctstate);
    _pmvie->SetDirty();
    if (_iscen >= 0 && _iscen < _pmvie->Cscen() && _pmvie->FSwitchScen(_iscen))
    {
        if (_pmvie->Pscen() != pvNil)
            _pmvie->Pscen()->FGotoFrm(_nfrm);
        _pmvie->ApplyCameraTrack();
        _pmvie->InvalViewsAndScb();
    }
    return fTrue;
}

bool MUNC::FUndo(PDOCB pdocb)
{
    return FDo(pdocb);
}

#ifdef DEBUG
void MUNC::MarkMem(void)
{
    AssertThis(0);
    MUNC_PAR::MarkMem();
    MarkPv(_pctstate);
}

void MUNC::AssertValid(uint32_t grf)
{
}
#endif

/** 3DMMv1.0: **************************************************
 *
 * Public constructor for movie undo objects for scene
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PMUNS MUNS::PmunsNew()
{
    PMUNS pmuns;
    pmuns = NewObj MUNS();
    return (pmuns);
}

/** 3DMMv1.0: **************************************************
 *
 * Destructor for movies undo objects
 *
 ****************************************************/
MUNS::~MUNS(void)
{
    AssertBaseThis(0);
    ReleasePpo(&_pscen);
}

/** 3DMMv1.0: **************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	pdocb - The owning document.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
/***************************************************************************
    Plain-English history name for movie-level scene operations.
***************************************************************************/
void MUNS::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    switch (_munst)
    {
    case munstInsScen:
        pstn->SetSz(PszLit("Insert Scene"));
        break;
    case munstRemScen:
        pstn->SetSz(PszLit("Delete Scene"));
        break;
    case munstSetBkgd:
        pstn->SetSz(PszLit("Change Scene Background"));
        break;
    default:
        pstn->SetSz(PszLit("Edit Movie"));
        break;
    }
}

bool MUNS::FDo(PDOCB pdocb)
{
    AssertThis(0);

    TAG tagOld;

    switch (_munst)
    {
    case munstInsScen:
        if (!_pmvie->FNewScenInsCore(_iscen))
        {
            goto LFail;
        }
        if (!_pmvie->Pscen()->FSetBkgdCore(&_tag, &tagOld))
        {
            _pmvie->FRemScenCore(_iscen);
            goto LFail;
        }

        _pmvie->Pmcc()->SceneUnnuked();
        break;

    case munstRemScen:
        if (!_pmvie->FRemScenCore(_iscen))
        {
            goto LFail;
        }
        _pmvie->Pmcc()->SceneNuked();
        break;

    default:
        Bug("Unknown munst");
        goto LFail;
    }

    _pmvie->Pmsq()->FlushMsq();
    return (fTrue);

LFail:
    _pmvie->Pmsq()->FlushMsq();
    _pmvie->ClearUndo(); // 3DMMv1.0:  After this, _pmvie is invalid
    return (fFalse);
}

/** 3DMMv1.0: **************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	pdocb - The owning document.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool MUNS::FUndo(PDOCB pdocb)
{
    AssertThis(0);

    switch (_munst)
    {
    case munstInsScen:
        if (!_pmvie->FRemScenCore(_iscen))
        {
            goto LFail;
        }
        _pmvie->Pmcc()->SceneNuked();
        break;

    case munstRemScen:
        if (!_pmvie->FInsScenCore(_iscen, _pscen))
        {
            goto LFail;
        }
        if (!_pmvie->FSetSceneDefaultLightingShaders(_iscen, _fRemovedSceneDefaultLightingShaders, fFalse) ||
            !_pmvie->FSetSceneLightLabCombineLegacy(_iscen, _fRemovedSceneLightLabCombineLegacy, fFalse))
            goto LFail;
        _pmvie->Pmcc()->SceneUnnuked();

        break;

    default:
        Bug("Unknown munst");
        goto LFail;
    }

    _pmvie->Pmsq()->FlushMsq();
    return (fTrue);

LFail:
    _pmvie->Pmsq()->FlushMsq();
    _pmvie->ClearUndo(); // 3DMMv1.0:  After this, _pmvie is invalid
    return (fFalse);
}

#ifdef DEBUG
/** 3DMMv1.0: **************************************************
 * Mark memory used by the MUNS
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void MUNS::MarkMem(void)
{
    AssertThis(0);
    MUNS_PAR::MarkMem();
    MarkMemObj(_pscen);
}

/** 3DMMv1.0: *************************************************************************
 *
 * Assert the validity of the MUNS.
 *
 * Parameters:
 *  grf - Bit field of options
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MUNS::AssertValid(uint32_t grf)
{
    AssertNilOrPo(_pscen, 0);
}
#endif // 3DMMv1.0: DEBUG

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
 *
 * Assert the validity of the MUNB.
 *
 * Parameters:
 *  grf - Bit field of options
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MUNB::AssertValid(uint32_t grf)
{
    MUNB_PAR::AssertValid(fobjAllocated);
    AssertPo(_pmvie, 0);
}
#endif // 3DMMv1.0: DEBUG
