/* 3DMMEx: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMEx: *************************************************************************

    portf.cpp: Portfolio handler

    Primary Author: ******
    Review Status: peted has reviewed. Final version not yet approved.

***************************************************************************/
#include "studio.h"
#include <CommCtrl.h>
#include <string.h>

#ifdef KAUAI_WIN32
// Implemented in utest.cpp. The 4x DWM presentation must stand down while
// Windows owns a real common file dialog, otherwise the independent scaled
// presentation competes with the dialog's original 640x480 owner window.
void Suspend4DMM4xPresentationForNativeDialog(void);
void Resume4DMM4xPresentationAfterNativeDialog(void);
#endif

ASSERTNAME

bool FPortGetFniOpen(FNI *pfni, LPCTSTR lpstrFilter, LPCTSTR lpstrTitle, FNI *pfniInitialDir, uint32_t grfPrevType,
                     CNO cnoWave);
bool FPortGetFniSave(FNI *pfni, LPCTSTR lpstrFilter, LPCTSTR lpstrTitle, LPCTSTR lpstrDefExt, PSTN pstnDefFileName,
                     uint32_t grfPrevType, CNO cnoWave);

UINT_PTR CALLBACK OpenHookProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void OpenPreview(HWND hwnd, PGNV pgnvOff, RECT *prcsPreview);
void RepaintPortfolio(HWND hwndCustom);

static WNDPROC lpBtnProc;
LRESULT CALLBACK SubClassBtnProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static WNDPROC lpPreviewProc;
LRESULT CALLBACK SubClassPreviewProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static WNDPROC lpDlgProc;
LRESULT CALLBACK SubClassDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

typedef struct dlginfo
{
    bool fIsOpen;         // 3DMMEx: fTrue if Open file, (ie not Save file)
    bool fDrawnBkgnd;     // 3DMMEx: fTrue if portfolio background bitmap has been displayed.
    RECT rcsDlg;          // 3DMMEx: Initial size of the portfolio common dlg window client area.
    uint32_t grfPrevType; // 3DMMEx: Bits for types of preview required, (eg movie, sound etc) == 0 if no preview
    CNO cnoWave;          // 3DMMEx: Wave file cno for audio when portfolio is invoked.
} DLGINFO;
typedef DLGINFO *PDLGINFO;

typedef struct moviefiledlginfo
{
    bool fIsOpen;
    bool fAllowLegacySave;
    HWND hwndPreview;
    int32_t dxpPreview;
    int32_t dypPreview;
} MOVIEFILEDLGINFO;
typedef MOVIEFILEDLGINFO *PMOVIEFILEDLGINFO;

static WNDPROC vlpfnMovieFilePreviewProc = pvNil;

static bool F4DMMMoviePathHasExtension(PCSZ pszPath, PCSZ pszExt)
{
    if (pszPath == pvNil || pszExt == pvNil)
        return fFalse;
    PCSZ pszSlashBack = strrchr(pszPath, '\\');
    PCSZ pszSlashFwd = strrchr(pszPath, '/');
    PCSZ pszSlash = pszSlashBack;
    if (pszSlashFwd != pvNil && (pszSlash == pvNil || pszSlashFwd > pszSlash))
        pszSlash = pszSlashFwd;
    PCSZ pszDot = strrchr(pszPath, '.');
    return pszDot != pvNil && (pszSlash == pvNil || pszDot > pszSlash) && _stricmp(pszDot, pszExt) == 0;
}

static HWND Hwnd4DMMMovieFileDialogOwner(bool *pfScaledOwner)
{
    HWND hwndOwner = vwig.hwndApp;
    bool fScaledOwner = fFalse;

    if (vwig.hwndApp != hNil && IsWindow(vwig.hwndApp))
    {
        HWND hwndScale = (HWND)GetPropA(vwig.hwndApp, "4DMMUiScaleWindow");
        if (hwndScale != hNil && IsWindow(hwndScale) && IsWindowVisible(hwndScale))
        {
            hwndOwner = hwndScale;
            fScaledOwner = fTrue;
        }
    }

    if (pfScaledOwner != pvNil)
        *pfScaledOwner = fScaledOwner;
    return hwndOwner;
}

static bool F4DMMForceMovieExtension(char *pszPath, int32_t cchPath, PCSZ pszExtNoDot)
{
    if (pszPath == pvNil || pszExtNoDot == pvNil || cchPath <= 1 || pszPath[0] == chNil)
        return fFalse;

    char *pchSlashBack = strrchr(pszPath, '\\');
    char *pchSlashFwd = strrchr(pszPath, '/');
    char *pchSlash = pchSlashBack;
    if (pchSlashFwd != pvNil && (pchSlash == pvNil || pchSlashFwd > pchSlash))
        pchSlash = pchSlashFwd;

    char *pchDot = strrchr(pszPath, '.');
    if (pchDot == pvNil || (pchSlash != pvNil && pchDot < pchSlash))
        pchDot = pszPath + strlen(pszPath);

    char szSuffix[16];
    if (sprintf_s(szSuffix, SIZEOF(szSuffix), ".%s", pszExtNoDot) < 0)
        return fFalse;

    const int32_t cchPrefix = (int32_t)(pchDot - pszPath);
    const int32_t cchSuffix = (int32_t)strlen(szSuffix);
    if (cchPrefix + cchSuffix >= cchPath)
        return fFalse;

    strcpy_s(pchDot, cchPath - cchPrefix, szSuffix);
    return fTrue;
}

static void Get4DMMMovieFilePreviewScale(int32_t *pnum, int32_t *pden)
{
    int32_t num = 1;
    int32_t den = 1;

    // File-preview size follows the actual 4DMM presentation height, not the
    // Windows desktop DPI. Thus -resolution 4.25x gives 4.25x and a 1080-high
    // presentation gives exactly 1080 / 480 = 2.25x.
    if (vwig.hwndApp != hNil && IsWindow(vwig.hwndApp))
    {
        HWND hwndScale = (HWND)GetPropA(vwig.hwndApp, "4DMMUiScaleWindow");
        RECT rcScale;
        if (hwndScale != hNil && IsWindow(hwndScale) && GetClientRect(hwndScale, &rcScale) && rcScale.bottom > 0)
        {
            num = rcScale.bottom;
            den = 480;
        }
        else
        {
            const int32_t numProp =
                (int32_t)(INT_PTR)GetPropA(vwig.hwndApp, "4DMMUiScaleNumerator");
            const int32_t denProp =
                (int32_t)(INT_PTR)GetPropA(vwig.hwndApp, "4DMMUiScaleDenominator");
            if (numProp > 0 && denProp > 0)
            {
                num = numProp;
                den = denProp;
            }
        }
    }

    if (num <= 0 || den <= 0)
    {
        num = 1;
        den = 1;
    }
    if (pnum != pvNil)
        *pnum = num;
    if (pden != pvNil)
        *pden = den;
}

// Photoshop's "Bicubic Smoother" is deliberately softer than a sharp
// reconstruction filter.  Use a cubic B-spline kernel here rather than GDI's
// HALFTONE scaler so enlarged 160x100 movie thumbnails get true cubic
// interpolation instead of integer-looking pixel replication.
static int32_t Lw4DMMFloorDouble(double d)
{
    int32_t lw = (int32_t)d;
    if ((double)lw > d)
        --lw;
    return lw;
}

static int32_t Lw4DMMClampThumbnailCoord(int32_t lw, int32_t lwMax)
{
    if (lw < 0)
        return 0;
    if (lw >= lwMax)
        return lwMax - 1;
    return lw;
}

static double D4DMMBicubicSmootherWeight(double d)
{
    if (d < 0.0)
        d = -d;
    if (d < 1.0)
        return (4.0 - 6.0 * d * d + 3.0 * d * d * d) / 6.0;
    if (d < 2.0)
    {
        const double q = 2.0 - d;
        return q * q * q / 6.0;
    }
    return 0.0;
}

static void BicubicSmooth4DMMThumbnail32(const uint8_t *pbSrc, int32_t dxpSrc, int32_t dypSrc,
                                         int32_t cbRowSrc, uint8_t *pbDst, int32_t dxpDst,
                                         int32_t dypDst, int32_t cbRowDst)
{
    if (pbSrc == pvNil || pbDst == pvNil || dxpSrc <= 0 || dypSrc <= 0 ||
        dxpDst <= 0 || dypDst <= 0 || cbRowSrc < LwMul(dxpSrc, 4) ||
        cbRowDst < LwMul(dxpDst, 4))
        return;

    const double dScaleX = (double)dxpSrc / (double)dxpDst;
    const double dScaleY = (double)dypSrc / (double)dypDst;

    for (int32_t ypDst = 0; ypDst < dypDst; ++ypDst)
    {
        const double ypSrc = ((double)ypDst + 0.5) * dScaleY - 0.5;
        const int32_t ypBase = Lw4DMMFloorDouble(ypSrc);
        uint8_t *pbDstRow = pbDst + LwMul(ypDst, cbRowDst);

        for (int32_t xpDst = 0; xpDst < dxpDst; ++xpDst)
        {
            const double xpSrc = ((double)xpDst + 0.5) * dScaleX - 0.5;
            const int32_t xpBase = Lw4DMMFloorDouble(xpSrc);
            double db = 0.0;
            double dg = 0.0;
            double dr = 0.0;

            for (int32_t jy = -1; jy <= 2; ++jy)
            {
                const int32_t yp = Lw4DMMClampThumbnailCoord(ypBase + jy, dypSrc);
                const double wy = D4DMMBicubicSmootherWeight(ypSrc - (double)(ypBase + jy));
                const uint8_t *pbSrcRow = pbSrc + LwMul(yp, cbRowSrc);

                for (int32_t jx = -1; jx <= 2; ++jx)
                {
                    const int32_t xp = Lw4DMMClampThumbnailCoord(xpBase + jx, dxpSrc);
                    const double wx = D4DMMBicubicSmootherWeight(xpSrc - (double)(xpBase + jx));
                    const double w = wx * wy;
                    const uint8_t *pb = pbSrcRow + LwMul(xp, 4);
                    db += w * pb[0];
                    dg += w * pb[1];
                    dr += w * pb[2];
                }
            }

            int32_t ib = (int32_t)(db + 0.5);
            int32_t ig = (int32_t)(dg + 0.5);
            int32_t ir = (int32_t)(dr + 0.5);
            if (ib < 0) ib = 0; else if (ib > 255) ib = 255;
            if (ig < 0) ig = 0; else if (ig > 255) ig = 255;
            if (ir < 0) ir = 0; else if (ir > 255) ir = 255;

            uint8_t *pbOut = pbDstRow + LwMul(xpDst, 4);
            pbOut[0] = (uint8_t)ib;
            pbOut[1] = (uint8_t)ig;
            pbOut[2] = (uint8_t)ir;
            pbOut[3] = 0;
        }
    }
}

static bool F4DMMDrawSelectedMovieThumbnail(HWND hwndPreview, HDC hdc)
{
    if (hwndPreview == hNil || hdc == hNil)
        return fFalse;

    RECT rcsPreview;
    GetClientRect(hwndPreview, &rcsPreview);
    FillRect(hdc, &rcsPreview, (HBRUSH)GetStockObject(BLACK_BRUSH));

    HWND hwndDlg = GetParent(hwndPreview);
    SZ szFile;
    szFile[0] = chNil;
    const LRESULT cchSelected = hwndDlg != hNil
                                    ? SendMessageA(hwndDlg, CDM_GETFILEPATH,
                                                   (WPARAM)kcchMaxSz, (LPARAM)szFile)
                                    : 0;
    if (hwndDlg == hNil || cchSelected <= 0 ||
        !F4DMMMoviePathHasExtension(szFile, ".3mm"))
    {
        return fFalse;
    }

    MVIE::MultiLog(pvNil, "movie_file_dialog preview_select path=%s dest=%ldx%ld",
                   szFile, (long)(rcsPreview.right - rcsPreview.left),
                   (long)(rcsPreview.bottom - rcsPreview.top));

    STN stnFile;
    FNI fni;
    stnFile.SetSz(szFile);
    if (!fni.FBuildFromPath(&stnFile, kftg3mm) || fni.FDir())
        return fFalse;

    ERS ersT;
    ERS *pers = vpers;
    vpers = &ersT;

    bool fPreviewed = fFalse;
    PCFL pcfl = CFL::PcflOpen(&fni, fcflNil);
    if (pcfl != pvNil)
    {
        CKI ckiMovie;
        KID kidScene;
        KID kidThumb;
        BLCK blck;
        if (pcfl->FGetCkiCtg(kctgMvie, 0, &ckiMovie) &&
            pcfl->FGetKidChidCtg(kctgMvie, ckiMovie.cno, 0, kctgScen, &kidScene) &&
            pcfl->FGetKidChidCtg(kctgScen, kidScene.cki.cno, 0, kctgThumbMbmp, &kidThumb) &&
            pcfl->FFind(kidThumb.cki.ctg, kidThumb.cki.cno, &blck))
        {
            PMBMP pmbmp = MBMP::PmbmpRead(&blck);
            if (pmbmp != pvNil)
            {
                // Render the authored 160x100 thumbnail into a top-down BGRA32 DIB at
                // exactly 1:1, then perform our own cubic B-spline enlargement. GDI
                // HALFTONE still looked much too much like enlarged source pixels at
                // 4DMM's large presentation scales; this is a real bicubic smoother.
                const int32_t dxpNative = 160;
                const int32_t dypNative = 100;
                const int32_t dxpDest = rcsPreview.right - rcsPreview.left;
                const int32_t dypDest = rcsPreview.bottom - rcsPreview.top;
                const DWORD dwScaleBegin = GetTickCount();

                BITMAPINFO bmiNative;
                ClearPb(&bmiNative, SIZEOF(bmiNative));
                bmiNative.bmiHeader.biSize = SIZEOF(BITMAPINFOHEADER);
                bmiNative.bmiHeader.biWidth = dxpNative;
                bmiNative.bmiHeader.biHeight = -dypNative; // top-down, matching preview coordinates
                bmiNative.bmiHeader.biPlanes = 1;
                bmiNative.bmiHeader.biBitCount = 32;
                bmiNative.bmiHeader.biCompression = BI_RGB;

                void *pvNativeBits = pvNil;
                HDC hdcNative = CreateCompatibleDC(hdc);
                HBITMAP hbmpNative = hdcNative != hNil
                                         ? CreateDIBSection(hdc, &bmiNative, DIB_RGB_COLORS,
                                                            &pvNativeBits, hNil, 0)
                                         : hNil;
                HGDIOBJ hgdiNativeOld = hNil;
                if (hdcNative != hNil && hbmpNative != hNil && pvNativeBits != pvNil)
                {
                    hgdiNativeOld = SelectObject(hdcNative, hbmpNative);
                    RECT rcNativeFill = {0, 0, dxpNative, dypNative};
                    FillRect(hdcNative, &rcNativeFill, (HBRUSH)GetStockObject(BLACK_BRUSH));

                    PGPT pgptNative = GPT::PgptNew(hdcNative);
                    if (pgptNative != pvNil)
                    {
                        GNV gnvNative(pgptNative);
                        RC rcNative(0, 0, dxpNative, dypNative);
                        gnvNative.DrawMbmp(pmbmp, &rcNative);
                        GPT::Flush();
                        ReleasePpo(&pgptNative);

                        BITMAPINFO bmiDest;
                        ClearPb(&bmiDest, SIZEOF(bmiDest));
                        bmiDest.bmiHeader.biSize = SIZEOF(BITMAPINFOHEADER);
                        bmiDest.bmiHeader.biWidth = dxpDest;
                        bmiDest.bmiHeader.biHeight = -dypDest;
                        bmiDest.bmiHeader.biPlanes = 1;
                        bmiDest.bmiHeader.biBitCount = 32;
                        bmiDest.bmiHeader.biCompression = BI_RGB;

                        void *pvDestBits = pvNil;
                        HDC hdcDest = CreateCompatibleDC(hdc);
                        HBITMAP hbmpDest = hdcDest != hNil
                                               ? CreateDIBSection(hdc, &bmiDest, DIB_RGB_COLORS,
                                                                  &pvDestBits, hNil, 0)
                                               : hNil;
                        HGDIOBJ hgdiDestOld = hNil;
                        if (hdcDest != hNil && hbmpDest != hNil && pvDestBits != pvNil)
                        {
                            hgdiDestOld = SelectObject(hdcDest, hbmpDest);
                            BicubicSmooth4DMMThumbnail32((const uint8_t *)pvNativeBits,
                                                         dxpNative, dypNative, dxpNative * 4,
                                                         (uint8_t *)pvDestBits,
                                                         dxpDest, dypDest, dxpDest * 4);
                            const BOOL fBlit = BitBlt(hdc, 0, 0, dxpDest, dypDest,
                                                      hdcDest, 0, 0, SRCCOPY);
                            MVIE::MultiLog(pvNil,
                                           "movie_file_dialog preview_draw path=%s native=%ldx%ld dest=%ldx%ld resample=BICUBIC_SMOOTHER_BSPLINE source_bpp=32 elapsed_ms=%lu ok=%d",
                                           szFile, (long)dxpNative, (long)dypNative,
                                           (long)dxpDest, (long)dypDest,
                                           (unsigned long)(GetTickCount() - dwScaleBegin),
                                           (int)fBlit);
                            fPreviewed = fBlit != FALSE;
                        }
                        else
                        {
                            MVIE::MultiLog(pvNil,
                                           "movie_file_dialog preview_draw bicubic_dest_alloc_fail path=%s hdc=%p hbmp=%p bits=%p dest=%ldx%ld",
                                           szFile, hdcDest, hbmpDest, pvDestBits,
                                           (long)dxpDest, (long)dypDest);
                        }

                        if (hgdiDestOld != hNil && hdcDest != hNil)
                            SelectObject(hdcDest, hgdiDestOld);
                        if (hbmpDest != hNil)
                            DeleteObject(hbmpDest);
                        if (hdcDest != hNil)
                            DeleteDC(hdcDest);
                    }
                }
                else
                {
                    MVIE::MultiLog(pvNil,
                                   "movie_file_dialog preview_draw bicubic_source_alloc_fail path=%s hdc=%p hbmp=%p bits=%p",
                                   szFile, hdcNative, hbmpNative, pvNativeBits);
                }

                if (hgdiNativeOld != hNil && hdcNative != hNil)
                    SelectObject(hdcNative, hgdiNativeOld);
                if (hbmpNative != hNil)
                    DeleteObject(hbmpNative);
                if (hdcNative != hNil)
                    DeleteDC(hdcNative);
                ReleasePpo(&pmbmp);
            }
        }
        ReleasePpo(&pcfl);
    }

    vpers = pers;
    return fPreviewed;
}

static LRESULT CALLBACK MovieFilePreviewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        F4DMMDrawSelectedMovieThumbnail(hwnd, ps.hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    default:
        break;
    }

    return vlpfnMovieFilePreviewProc != pvNil
               ? CallWindowProc(vlpfnMovieFilePreviewProc, hwnd, msg, wParam, lParam)
               : DefWindowProc(hwnd, msg, wParam, lParam);
}

static void Layout4DMMMovieFilePreview(HWND hwndHook, PMOVIEFILEDLGINFO pinfo)
{
    if (hwndHook == hNil || pinfo == pvNil || pinfo->hwndPreview != hNil)
        return;

    HWND hwndDlg = GetParent(hwndHook);
    if (hwndDlg == hNil)
        return;

    int32_t scaleNum;
    int32_t scaleDen;
    Get4DMMMovieFilePreviewScale(&scaleNum, &scaleDen);

    // The original 3DMM file preview is a 16:10 160x100 presentation area.
    // Keep that exact logical size and scale it with the requested 4DMM output.
    pinfo->dxpPreview = MulDiv(160, scaleNum, scaleDen);
    pinfo->dypPreview = MulDiv(100, scaleNum, scaleDen);
    if (pinfo->dxpPreview < 160)
        pinfo->dxpPreview = 160;
    if (pinfo->dypPreview < 100)
        pinfo->dypPreview = 100;

    RECT rcClientOld;
    RECT rcWindowOld;
    if (!GetClientRect(hwndDlg, &rcClientOld) || !GetWindowRect(hwndDlg, &rcWindowOld))
        return;

    struct CHILDGEOM
    {
        HWND hwnd;
        RECT rc;
    };
    CHILDGEOM rgchild[96];
    int32_t cchild = 0;
    for (HWND hwndChild = GetWindow(hwndDlg, GW_CHILD);
         hwndChild != hNil && cchild < (int32_t)(SIZEOF(rgchild) / SIZEOF(rgchild[0]));
         hwndChild = GetWindow(hwndChild, GW_HWNDNEXT))
    {
        if (hwndChild == hwndHook)
            continue;
        RECT rc;
        if (!GetWindowRect(hwndChild, &rc))
            continue;
        MapWindowPoints(HWND_DESKTOP, hwndDlg, (POINT *)&rc, 2);
        rgchild[cchild].hwnd = hwndChild;
        rgchild[cchild].rc = rc;
        ++cchild;
    }

    const int32_t dxpMargin = 12;
    const int32_t dypMargin = 12;
    const int32_t dxpExtra = pinfo->dxpPreview + 2 * dxpMargin;
    const int32_t dxpWindowOld = rcWindowOld.right - rcWindowOld.left;
    const int32_t dypWindowOld = rcWindowOld.bottom - rcWindowOld.top;
    const int32_t dypNonClient = dypWindowOld - (rcClientOld.bottom - rcClientOld.top);
    int32_t dxpWindowNew = dxpWindowOld + dxpExtra;
    int32_t dypWindowNew = dypWindowOld;
    const int32_t dypNeeded = pinfo->dypPreview + 2 * dypMargin + dypNonClient;
    if (dypWindowNew < dypNeeded)
        dypWindowNew = dypNeeded;

    int32_t xp = rcWindowOld.left - (dxpWindowNew - dxpWindowOld) / 2;
    int32_t yp = rcWindowOld.top - (dypWindowNew - dypWindowOld) / 2;
    HMONITOR hmon = MonitorFromWindow(hwndDlg, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    ClearPb(&mi, SIZEOF(mi));
    mi.cbSize = SIZEOF(mi);
    if (hmon != hNil && GetMonitorInfo(hmon, &mi))
    {
        if (dxpWindowNew <= mi.rcWork.right - mi.rcWork.left)
            xp = LwMax((int32_t)mi.rcWork.left, LwMin(xp, (int32_t)mi.rcWork.right - dxpWindowNew));
        if (dypWindowNew <= mi.rcWork.bottom - mi.rcWork.top)
            yp = LwMax((int32_t)mi.rcWork.top, LwMin(yp, (int32_t)mi.rcWork.bottom - dypWindowNew));
    }

    SetWindowPos(hwndDlg, hNil, xp, yp, dxpWindowNew, dypWindowNew,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    // Keep the Windows common-dialog controls at their native layout on the
    // left; the added width belongs exclusively to 4DMM's movie preview.
    for (int32_t i = 0; i < cchild; ++i)
    {
        const RECT &rc = rgchild[i].rc;
        SetWindowPos(rgchild[i].hwnd, hNil, rc.left, rc.top,
                     rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    RECT rcClientNew;
    GetClientRect(hwndDlg, &rcClientNew);
    const int32_t xpPreview = rcClientOld.right + dxpMargin;
    const int32_t ypPreview = LwMax(dypMargin, (rcClientNew.bottom - pinfo->dypPreview) / 2);

    pinfo->hwndPreview = CreateWindowExA(0, "STATIC", "",
                                         WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                         xpPreview, ypPreview,
                                         pinfo->dxpPreview, pinfo->dypPreview,
                                         hwndDlg, hNil, vwig.hinst, pvNil);
    if (pinfo->hwndPreview != hNil)
    {
        vlpfnMovieFilePreviewProc =
            (WNDPROC)SetWindowLongPtrA(pinfo->hwndPreview, GWLP_WNDPROC, (LONG_PTR)MovieFilePreviewProc);
        MVIE::MultiLog(pvNil,
                       "movie_file_dialog preview size=%ldx%ld scale=%ld/%ld open=%d legacy_save=%d",
                       (long)pinfo->dxpPreview, (long)pinfo->dypPreview,
                       (long)scaleNum, (long)scaleDen, (int)pinfo->fIsOpen,
                       (int)pinfo->fAllowLegacySave);
        InvalidateRect(pinfo->hwndPreview, pvNil, fFalse);
        UpdateWindow(pinfo->hwndPreview);
    }
}

static PCSZ Psz4DMMMovieSaveExtension(PMOVIEFILEDLGINFO pinfo, DWORD nFilterIndex)
{
    if (pinfo == pvNil || !pinfo->fAllowLegacySave)
        return PszLit("vmm");
    return nFilterIndex == 2 ? PszLit("vmm") : PszLit("3mm");
}

static void Sync4DMMMovieSaveNameExtension(HWND hwndHook, PMOVIEFILEDLGINFO pinfo, OPENFILENAMEA *pofn)
{
    if (hwndHook == hNil || pinfo == pvNil || pofn == pvNil || pinfo->fIsOpen)
        return;

    HWND hwndDlg = GetParent(hwndHook);
    if (hwndDlg == hNil)
        return;

    SZ szSpec;
    szSpec[0] = chNil;
    const LRESULT cchSpec = SendMessageA(hwndDlg, CDM_GETSPEC, (WPARAM)kcchMaxSz, (LPARAM)szSpec);
    PCSZ pszExt = Psz4DMMMovieSaveExtension(pinfo, pofn->nFilterIndex);
    SendMessageA(hwndDlg, CDM_SETDEFEXT, 0, (LPARAM)pszExt);

    if (cchSpec <= 0 || szSpec[0] == chNil)
    {
        MVIE::MultiLog(pvNil,
                       "movie_file_dialog typechange filter=%lu ext=%s spec=<empty>",
                       (unsigned long)pofn->nFilterIndex, pszExt);
        return;
    }

    SZ szBefore;
    strcpy_s(szBefore, SIZEOF(szBefore), szSpec);
    if (F4DMMForceMovieExtension(szSpec, SIZEOF(szSpec), pszExt) &&
        _stricmp(szBefore, szSpec) != 0)
    {
        SendMessageA(hwndDlg, CDM_SETCONTROLTEXT, edt1, (LPARAM)szSpec);
        MVIE::MultiLog(pvNil,
                       "movie_file_dialog typechange filter=%lu ext=%s name_before=%s name_after=%s",
                       (unsigned long)pofn->nFilterIndex, pszExt, szBefore, szSpec);
    }
    else
    {
        MVIE::MultiLog(pvNil,
                       "movie_file_dialog typechange filter=%lu ext=%s name=%s unchanged=1",
                       (unsigned long)pofn->nFilterIndex, pszExt, szSpec);
    }
}

static UINT_PTR CALLBACK MovieFileDialogHookProc(HWND hwndHook, UINT msg, WPARAM /*wParam*/, LPARAM lParam)
{
    if (msg == WM_INITDIALOG)
    {
        OPENFILENAMEA *pofn = (OPENFILENAMEA *)lParam;
        if (pofn != pvNil)
            SetWindowLongPtrA(hwndHook, GWLP_USERDATA, (LONG_PTR)pofn->lCustData);
        return 0;
    }

    if (msg != WM_NOTIFY)
        return 0;

    PMOVIEFILEDLGINFO pinfo =
        (PMOVIEFILEDLGINFO)GetWindowLongPtrA(hwndHook, GWLP_USERDATA);
    LPOFNOTIFYA pnotify = (LPOFNOTIFYA)lParam;
    if (pinfo == pvNil || pnotify == pvNil)
        return 0;

    switch (pnotify->hdr.code)
    {
    case CDN_INITDONE:
        Layout4DMMMovieFilePreview(hwndHook, pinfo);
        if (!pinfo->fIsOpen)
            Sync4DMMMovieSaveNameExtension(hwndHook, pinfo, pnotify->lpOFN);
        break;

    case CDN_SELCHANGE:
    case CDN_FOLDERCHANGE:
        if (pinfo->hwndPreview != hNil)
        {
            InvalidateRect(pinfo->hwndPreview, pvNil, fFalse);
            UpdateWindow(pinfo->hwndPreview);
        }
        break;

    case CDN_TYPECHANGE:
        if (!pinfo->fIsOpen)
            Sync4DMMMovieSaveNameExtension(hwndHook, pinfo, pnotify->lpOFN);
        break;

    case CDN_FILEOK:
        if (!pinfo->fIsOpen && pnotify->lpOFN->lpstrFile != pvNil)
        {
            PCSZ pszExt = Psz4DMMMovieSaveExtension(pinfo, pnotify->lpOFN->nFilterIndex);
            SZ szBefore;
            strcpy_s(szBefore, SIZEOF(szBefore), pnotify->lpOFN->lpstrFile);
            const bool fForced = F4DMMForceMovieExtension(pnotify->lpOFN->lpstrFile,
                                                          (int32_t)pnotify->lpOFN->nMaxFile,
                                                          pszExt);
            MVIE::MultiLog(pvNil,
                           "movie_file_dialog fileok filter=%lu ext=%s before=%s after=%s forced=%d changed=%d",
                           (unsigned long)pnotify->lpOFN->nFilterIndex, pszExt,
                           szBefore, pnotify->lpOFN->lpstrFile,
                           (int)fForced,
                           (int)(fForced && _stricmp(szBefore, pnotify->lpOFN->lpstrFile) != 0));
        }
        break;

    default:
        break;
    }

    return 0;
}

static bool FPortGetFniMovieNative(FNI *pfni, bool fOpen, PCSZ pszTitle, PCSZ pszDefExt,
                                   PSTN pstnDefFileName, FNI *pfniInitialDir)
{
    AssertPo(pfni, 0);
    AssertNilOrPo(pstnDefFileName, 0);
    AssertNilOrPo(pfniInitialDir, 0);

    static const char kszMovieOpenFilter[] =
        "All types (*.*)\0*.*\0"
        "3D Movie Maker (Legacy) (*.3mm)\0*.3mm\0"
        "Virtual Movie Maker (*.vmm)\0*.vmm\0\0";
    static const char kszMovieSaveFilter[] =
        "3D Movie Maker (Legacy) (*.3mm)\0*.3mm\0"
        "Virtual Movie Maker (*.vmm)\0*.vmm\0\0";
    static const char kszMovieSaveVmmOnlyFilter[] =
        "Virtual Movie Maker (*.vmm)\0*.vmm\0\0";

    const bool fAllowLegacySave =
        fOpen || pszDefExt == pvNil || _stricmp(pszDefExt, "vmm") != 0;

    MOVIEFILEDLGINFO info;
    ClearPb(&info, SIZEOF(info));
    info.fIsOpen = fOpen;
    info.fAllowLegacySave = fAllowLegacySave;

    OPENFILENAMEA ofn;
    ClearPb(&ofn, SIZEOF(ofn));
    SZ szFile;
    szFile[0] = chNil;
    if (!fOpen && pstnDefFileName != pvNil)
        pstnDefFileName->GetSz(szFile);

    SZ szInitialDir;
    szInitialDir[0] = chNil;
    FNI fniInitial;
    bool fHaveInitialDir = fFalse;
    if (pfniInitialDir != pvNil)
    {
        fniInitial = *pfniInitialDir;
        fHaveInitialDir = fTrue;
    }
    else if (!fOpen)
    {
        vapp.GetFniUser(&fniInitial);
        fHaveInitialDir = fTrue;
    }
    if (fHaveInitialDir)
    {
        STN stnInitial;
        fniInitial.GetStnPath(&stnInitial);
        stnInitial.GetSz(szInitialDir);
    }

    bool fScaledOwner = fFalse;
    HWND hwndOwner = Hwnd4DMMMovieFileDialogOwner(&fScaledOwner);

    ofn.lStructSize = SIZEOF(ofn);
    ofn.hwndOwner = hwndOwner;
    ofn.hInstance = vwig.hinst;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = kcchMaxSz;
    ofn.lpstrTitle = pszTitle;
    ofn.lpstrInitialDir = szInitialDir[0] != chNil ? szInitialDir : pvNil;
    ofn.lpfnHook = MovieFileDialogHookProc;
    ofn.lCustData = (LPARAM)&info;
    ofn.Flags = OFN_EXPLORER | OFN_ENABLEHOOK | OFN_HIDEREADONLY | OFN_NOCHANGEDIR |
                OFN_PATHMUSTEXIST;

    if (fOpen)
    {
        ofn.lpstrFilter = kszMovieOpenFilter;
        ofn.nFilterIndex = 1;
        ofn.Flags |= OFN_FILEMUSTEXIST;
    }
    else
    {
        ofn.lpstrFilter = fAllowLegacySave ? kszMovieSaveFilter : kszMovieSaveVmmOnlyFilter;
        ofn.nFilterIndex = 1;
        ofn.lpstrDefExt = fAllowLegacySave ? "3mm" : "vmm";
        ofn.Flags |= OFN_OVERWRITEPROMPT;
    }

    RECT rcSourceWindow = {0, 0, 0, 0};
    RECT rcOwnerWindow = {0, 0, 0, 0};
    if (vwig.hwndApp != hNil && IsWindow(vwig.hwndApp))
        GetWindowRect(vwig.hwndApp, &rcSourceWindow);
    if (hwndOwner != hNil && IsWindow(hwndOwner))
        GetWindowRect(hwndOwner, &rcOwnerWindow);
    MVIE::MultiLog(pvNil,
                   "movie_file_dialog v171 begin mode=%s legacy_save=%d default_ext=%s owner=%p scaled_owner=%d source=%p source_visible=%d source_rect=(%ld,%ld)-(%ld,%ld) owner_rect=(%ld,%ld)-(%ld,%ld)",
                   fOpen ? "open" : "save", (int)fAllowLegacySave,
                   pszDefExt != pvNil ? pszDefExt : "", hwndOwner, (int)fScaledOwner,
                   vwig.hwndApp,
                   vwig.hwndApp != hNil && IsWindow(vwig.hwndApp) ? (int)IsWindowVisible(vwig.hwndApp) : 0,
                   (long)rcSourceWindow.left, (long)rcSourceWindow.top,
                   (long)rcSourceWindow.right, (long)rcSourceWindow.bottom,
                   (long)rcOwnerWindow.left, (long)rcOwnerWindow.top,
                   (long)rcOwnerWindow.right, (long)rcOwnerWindow.bottom);

    for (;;)
    {
        // The common dialog destroys the accessory HWND when it closes. If a
        // save-name validation error redisplays the dialog, create a fresh
        // preview pane for that new common-dialog instance.
        info.hwndPreview = hNil;
        const BOOL fOKed = fOpen ? GetOpenFileNameA(&ofn) : GetSaveFileNameA(&ofn);
        const DWORD dwDialogErr = fOKed ? NOERROR : CommDlgExtendedError();
        MVIE::MultiLog(pvNil,
                       "movie_file_dialog return mode=%s ok=%d error=0x%08lX filter=%lu path=%s owner=%p owner_visible=%d source_visible=%d",
                       fOpen ? "open" : "save", (int)fOKed,
                       (unsigned long)dwDialogErr, (unsigned long)ofn.nFilterIndex,
                       szFile, hwndOwner,
                       hwndOwner != hNil && IsWindow(hwndOwner) ? (int)IsWindowVisible(hwndOwner) : 0,
                       vwig.hwndApp != hNil && IsWindow(vwig.hwndApp) ? (int)IsWindowVisible(vwig.hwndApp) : 0);
        if (!fOKed)
        {
            const DWORD dwErr = dwDialogErr;
            pfni->SetNil();
            if (dwErr != NOERROR)
                PushErc(ercSocPortfolioFailed);
            return fFalse;
        }

        if (!fOpen)
        {
            SZ szBeforeNormalize;
            strcpy_s(szBeforeNormalize, SIZEOF(szBeforeNormalize), szFile);
            PCSZ pszSelectedExt = Psz4DMMMovieSaveExtension(&info, ofn.nFilterIndex);
            if (!F4DMMForceMovieExtension(szFile, SIZEOF(szFile), pszSelectedExt))
            {
                MVIE::MultiLog(pvNil,
                               "movie_file_dialog save_normalize FAIL filter=%lu ext=%s path=%s",
                               (unsigned long)ofn.nFilterIndex, pszSelectedExt, szBeforeNormalize);
                PushErc(ercSocInvalidFilename);
                pfni->SetNil();
                return fFalse;
            }
            MVIE::MultiLog(pvNil,
                           "movie_file_dialog save_normalize filter=%lu ext=%s before=%s after=%s changed=%d",
                           (unsigned long)ofn.nFilterIndex, pszSelectedExt,
                           szBeforeNormalize, szFile,
                           (int)(_stricmp(szBeforeNormalize, szFile) != 0));
        }

        STN stnPath;
        stnPath.SetSz(szFile);

        const bool fVmm = F4DMMMoviePathHasExtension(szFile, ".vmm");
        MVIE::MultiLog(pvNil, "movie_file_dialog select mode=%s path=%s filter=%lu vmm=%d",
                       fOpen ? "open" : "save", szFile,
                       (unsigned long)ofn.nFilterIndex, (int)fVmm);
        if (!pfni->FBuildFromPath(&stnPath, fVmm ? kftgVmm : (fOpen ? 0 : kftg3mm)))
        {
            PushErc(ercSocInvalidFilename);
            pfni->SetNil();
            return fFalse;
        }
        return fTrue;
    }
}

bool FPortDisplayWithIds(FNI *pfni, bool fOpen, int32_t lFilterLabel, int32_t lFilterExt, int32_t lTitle,
                         PCSZ lpstrDefExt, PSTN pstnDefFileName, FNI *pfniInitialDir, uint32_t grfPrevType, CNO cnoWave)
{
    STN stnTitle;
    STN stnFilterLabel;
    STN stnFilterExt;
    int cChLabel, cChExt;
    SZ szFilter;
    bool fRet;

    AssertVarMem(pfni);
    AssertNilOrPo(pfniInitialDir, 0);
    AssertNilOrPo(pstnDefFileName, 0);

    if (!vapp.FGetStnApp(lTitle, &stnTitle))
        return fFalse;

    // 3DMMEx: Filter string contain non-terminating null characters, so must
    // 3DMMEx: build up string from separate label and file extension strings.

    if (!vapp.FGetStnApp(lFilterLabel, &stnFilterLabel))
        return fFalse;
    if (!vapp.FGetStnApp(lFilterExt, &stnFilterExt))
        return fFalse;

    // 3DMMEx: Kauai does not like internal null chars in an STN. So build
    // 3DMMEx: up the final final string as an SZ.

    cChLabel = stnFilterLabel.Cch();
    cChExt = stnFilterExt.Cch();

    stnFilterLabel.GetSz(szFilter);
    stnFilterExt.GetSz(&szFilter[cChLabel + 1]);

    szFilter[cChLabel + cChExt + 2] = chNil;

    // The new movie Open/Save As path is an ordinary top-level Windows
    // common dialog owned by the scaled 4DMM presentation itself. It must not
    // tear down that presentation or expose/restore the hidden 640x480 Kauai
    // source window. Legacy non-movie portfolios still use the old suspension
    // path because they are customized around that source HWND.
    const bool fNativeMovieDialog = (lFilterExt == idsPortfMovieFilterExt);
#ifdef KAUAI_WIN32
    if (!fNativeMovieDialog)
        Suspend4DMM4xPresentationForNativeDialog();
#endif
    StopAllMovieSounds();
    vapp.EnsureInteractive();
    vapp.SetFInPortfolio(fTrue);
    // Movie Open/Save As is now an ordinary Windows Explorer-style common
    // dialog with a 4DMM thumbnail accessory pane. Other legacy portfolios
    // (sound, texture, etc.) retain their existing specialized UI.
    if (lFilterExt == idsPortfMovieFilterExt)
    {
        fRet = FPortGetFniMovieNative(pfni, fOpen, stnTitle.Prgch(), lpstrDefExt,
                                      pstnDefFileName, pfniInitialDir);
    }
    else if (fOpen)
    {
        fRet = FPortGetFniOpen(pfni, szFilter, stnTitle.Prgch(), pfniInitialDir, grfPrevType, cnoWave);
    }
    else
    {
        fRet = FPortGetFniSave(pfni, szFilter, stnTitle.Prgch(), lpstrDefExt, pstnDefFileName, grfPrevType, cnoWave);
    }

    // 3DMMEx: Make sure no portfolio related audio is still playing.
    vpsndm->StopAll(sqnNil, sclNil);
    vapp.SetFInPortfolio(fFalse);
#ifdef KAUAI_WIN32
    if (!fNativeMovieDialog)
        Resume4DMM4xPresentationAfterNativeDialog();
#endif

    // 3DMMEx: Let the script know what the outcome is.
    vpcex->EnqueueCid(cidPortfolioClosed, 0, 0, fRet);

    // 3DMMEx: We must also enqueue a message for the help system here.
    // 3DMMEx: Help doesn't get the above cidPortfolioClosed as it has
    // 3DMMEx: already been filtered.
    vpcex->EnqueueCid(cidPortfolioResult, 0, 0, fRet);

    return fRet;
}

bool FPortGetFniOpen(FNI *pfni, LPCTSTR lpstrFilter, LPCTSTR lpstrTitle, FNI *pfniInitialDir, uint32_t grfPrevType,
                     CNO cnoWave)
{
    SZ szFile;
    DLGINFO diPortfolio;
    OPENFILENAME ofn;
    STN stn;
    bool fOKed;
    STN stnInitialDir;
    SZ szInitialDir;

    AssertPo(pfni, 0);
    AssertNilOrPo(pfniInitialDir, 0);

    ClearPb(&ofn, SIZEOF(OPENFILENAME));
    ClearPb(&diPortfolio, SIZEOF(DLGINFO));

    szFile[0] = 0;
    ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400;
    ofn.hwndOwner = vwig.hwndApp;
    ofn.hInstance = vwig.hinst;
    ofn.nFilterIndex = 1L;
    ofn.lpstrCustomFilter = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = kcchMaxSz;
    ofn.lpstrFileTitle = NULL;
    ofn.lpfnHook = OpenHookProc;
    ofn.lpTemplateName = MAKEINTRESOURCE(dlidPortfolio);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER | OFN_ENABLEHOOK | OFN_ENABLETEMPLATE;

#ifdef KAUAI_SDL
    // 3DMMEx: Dialog customizations are not supported when using SDL
    ofn.lpfnHook = NULL;
    ofn.lpTemplateName = 0;
    ofn.Flags &= ~(OFN_ENABLEHOOK | OFN_ENABLETEMPLATE);
#endif // 3DMMEx: KAUAI_SDL

    // 3DMMEx: lpstrDefExt is used for appended to the user typed filename is
    // 3DMMEx: one is entered without an extension. As the user cannot type
    // 3DMMEx: anything during a portfolio open, we don't use the lpstrDefExt here.
    ofn.lpstrDefExt = NULL;

    // 3DMMEx: Initialize internal data for accessing from inside dlg hook proc.
    diPortfolio.fIsOpen = fTrue;
    diPortfolio.fDrawnBkgnd = fFalse;
    diPortfolio.grfPrevType = grfPrevType;
    diPortfolio.cnoWave = cnoWave;
    ofn.lCustData = (LPARAM)&diPortfolio;

    ofn.lpstrFilter = lpstrFilter;
    ofn.lpstrTitle = lpstrTitle;

    // 3DMMEx: Get the string for the initial directory if required.

    if (pfniInitialDir != pvNil)
    {
        pfniInitialDir->GetStnPath(&stnInitialDir);
        stnInitialDir.GetSz(szInitialDir);
        ofn.lpstrInitialDir = szInitialDir;
    }
    else
    {
        // 3DMMEx: Initial directory will be current directory.
        ofn.lpstrInitialDir = pvNil;
    }

    fOKed = (GetOpenFileName(&ofn) == FALSE ? fFalse : fTrue);

    if (!fOKed)
    {
        // 3DMMEx: Check if custom common dlg failed to initialize.	Don't rely on the returned
        // 3DMMEx: error being CDERR_INITIALIZATION, as who knows what error will be returned
        // 3DMMEx: in future versions.
        if (CommDlgExtendedError() != NOERROR)
        {
            // 3DMMEx: User never saw portfolio. Therefore attempt to display
            // 3DMMEx: common dlg with no customization.
            ofn.Flags &= ~(OFN_EXPLORER | OFN_ENABLEHOOK | OFN_ENABLETEMPLATE);

            fOKed = (GetOpenFileName(&ofn) == FALSE ? fFalse : fTrue);
        }
    }
    // 3DMMEx: If the user selected a file, build up the associated fni.
    if (fOKed)
    {
        stn.SetSz(ofn.lpstrFile);

        pfni->FBuildFromPath(&stn, 0);
    }

    // 3DMMEx: Update the app window to make sure no parts of the portfolio
    // 3DMMEx: window are left on the screen while the file is being opened.
    // 3DMMEx: Calling UpdateMarked() does not have the desired effect here.
    UpdateWindow(vwig.hwndApp);

    // 3DMMEx: Report error to user if never displayed the portfolio.
    if (CommDlgExtendedError() != NOERROR)
    {
        PushErc(ercSocPortfolioFailed);
    }

    // 3DMMEx: Only return TRUE if the user selected a file.
    return (fOKed);
}

/** 3DMMEx: *************************************************************************

 pfGetFniSave: Display the save portfolio.

 Arguments: pfni		- Output FNI for file selected
            lpstrFilter	- String containing files types to filter on
            lpstrTitle	- String containing title of portfolio
            lpstrDefExt	- String containing default extension for filenames
                            typed by user without an extension.
            pstnDefFileName - Ptr to default extension stn if required.
            grfPrevType	- Bits for types of preview required, (eg movie, sound etc) == 0 if no preview
            cnoWave     - Wave cno for audio when portfolio is invoked

 Returns: 	TRUE	- File selected
            FALSE	- User canceled portfolio, (or other error).

***************************************************************************/
bool FPortGetFniSave(FNI *pfni, LPCTSTR lpstrFilter, LPCTSTR lpstrTitle, LPCTSTR lpstrDefExt, PSTN pstnDefFileName,
                     uint32_t grfPrevType, CNO cnoWave)
{
    DLGINFO diPortfolio;
    OPENFILENAME ofn;
    bool fOKed;
    bool tRet;
    bool fRedisplayPortfolio = fFalse;
    bool fExplorer = fTrue;
    STN stnFile, stnErr;
    FNI fniUserDir;
    STN stnUserDir;
    SZ szUserDir;
    SZ szDefFileName;
    SZ szFileTitle;

    AssertPo(pfni, 0);
    AssertNilOrPo(pstnDefFileName, 0);

    ClearPb(&ofn, SIZEOF(OPENFILENAME));
    ClearPb(&diPortfolio, SIZEOF(DLGINFO));

    szFileTitle[0] = chNil;

    ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400;
    ofn.hwndOwner = vwig.hwndApp;
    ofn.hInstance = vwig.hinst;
    ofn.nFilterIndex = 1L;
    ofn.lpstrCustomFilter = NULL;
    ofn.nMaxFile = kcchMaxSz;
    ofn.lpstrFileTitle = szFileTitle;
    ofn.nMaxFileTitle = kcchMaxSz;
    ofn.lpfnHook = OpenHookProc;
    ofn.lpTemplateName = MAKEINTRESOURCE(dlidPortfolio);

    // 3DMMEx: Don't use OFN_OVERWRITEPROMPT here, otherwise we can't intercept
    // 3DMMEx: the btn press on the Save btn to display our own message. Note,
    // 3DMMEx: we don't do that either, because the help topic display mechanism
    // 3DMMEx: was not designed for use within a modal dlg box such as the portfolio.
    // 3DMMEx: Instead query overwrite after the portfolio has closed.
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER | OFN_ENABLEHOOK | OFN_ENABLETEMPLATE;

    // 3DMMEx: Do not allow the save portfolio to change the current directory.
    ofn.Flags |= OFN_NOCHANGEDIR;

#ifdef KAUAI_SDL
    // 3DMMEx: Dialog customizations are not supported when using SDL
    ofn.lpfnHook = NULL;
    ofn.lpTemplateName = 0;
    ofn.Flags &= ~(OFN_ENABLEHOOK | OFN_ENABLETEMPLATE);
#endif // 3DMMEx: KAUAI_SDL

    // 3DMMEx: Let the initial dir be the user's dir.
    vapp.GetFniUser(&fniUserDir);
    fniUserDir.GetStnPath(&stnUserDir);
    stnUserDir.GetSz(szUserDir);
    ofn.lpstrInitialDir = szUserDir;

    // 3DMMEx: Initialize internal data for accessing from inside dlg hook proc.
    diPortfolio.fIsOpen = fFalse;
    diPortfolio.fDrawnBkgnd = fFalse;
    diPortfolio.grfPrevType = grfPrevType;
    diPortfolio.cnoWave = cnoWave;
    ofn.lCustData = (LPARAM)&diPortfolio;

    ofn.lpstrFilter = lpstrFilter;
    ofn.lpstrTitle = lpstrTitle;
    ofn.lpstrDefExt = lpstrDefExt;

    // 3DMMEx: Set the the portfolio default save file name if required.
    if (pstnDefFileName != pvNil)
    {
        pstnDefFileName->GetSz(szDefFileName);
    }
    else
    {
        szDefFileName[0] = chNil;
    }

    ofn.lpstrFile = szDefFileName;

    // 3DMMEx: We may need to display the portfolio multiple times if the user
    // 3DMMEx: selects an existing file, and then says they don't want to overwrite it.

    do // 3DMMEx: Display portfolio
    {
        fRedisplayPortfolio = fFalse;

        // 3DMMEx: Now display the portfolio.
        fOKed = (GetSaveFileName(&ofn) == FALSE ? fFalse : fTrue);

        if (!fOKed)
        {
            DWORD dwRet;

            // 3DMMEx: Check if custom common dlg failed to initialize.	Don't rely on the returned
            // 3DMMEx: error being CDERR_INITIALIZATION, as who knows what error will be returned
            // 3DMMEx: in future versions.
            if ((dwRet = CommDlgExtendedError()) != NOERROR)
            {
                if (dwRet == FNERR_INVALIDFILENAME || dwRet == FNERR_BUFFERTOOSMALL)
                {
                    // 3DMMEx: Set the the portfolio default save file name if required.
                    if (pstnDefFileName != pvNil)
                    {
                        pstnDefFileName->GetSz(szDefFileName);
                    }
                    else
                    {
                        szDefFileName[0] = chNil;
                    }
                    ofn.lpstrFile = szDefFileName;

                    PushErc(ercSocInvalidFilename);
                    fRedisplayPortfolio = fTrue;
                }
                else if (dwRet == CDERR_INITIALIZATION && fExplorer)
                {
                    // 3DMMEx: User never saw portfolio. Therefore attempt to display
                    // 3DMMEx: common dlg with no customization.
                    ofn.Flags &= ~(OFN_EXPLORER | OFN_ENABLEHOOK | OFN_ENABLETEMPLATE);
                    fRedisplayPortfolio = fTrue;
                    fExplorer = fFalse;
                }
                else
                    PushErc(ercSocPortfolioFailed);

                vapp.DisplayErrors();
                continue;
            }
        }
        else
        {
            // 3DMMEx: Build stn if user selected a file.
            stnFile.SetSz(ofn.lpstrFile);

            // 3DMMEx: Query any attempt to overwrite an existing file now.
            if (CchSz(ofn.lpstrFile) != 0)
            {
                bool tExists;

                // 3DMMEx: We always save the file with the default extension. If the
                // 3DMMEx: user used a different extension, add the default on the end.
                if (ofn.Flags & OFN_EXTENSIONDIFFERENT)
                {
                    stnFile.FAppendCh('.');
                    stnFile.FAppendSz(lpstrDefExt);
                }

                // 3DMMEx: Make sure the pfni is built from the appended file name if applicable.
                pfni->FBuildFromPath(&stnFile, 0);

                if ((tExists = pfni->TExists()) != tNo)
                {
                    int32_t cch;
                    achar *pch;
                    // 3DMMEx: File already exists. Query user for overwrite.

                    // 3DMMEx: The default name supplied to the user will only
                    // 3DMMEx: be the file name without the path.
                    stnFile.SetSz(ofn.lpstrFileTitle);

                    /* 3DMMEx: Remove the extension */
                    cch = stnFile.Cch();
                    pch = stnFile.Psz() + cch;
                    while (cch--)
                    {
                        pch--;
                        if (*pch == ChLit('.'))
                        {
                            stnFile.Delete(cch);
                            break;
                        }
                    }
                    stnFile.GetSz(ofn.lpstrFile);

                    // 3DMMEx: Only query if we know for sure the file's there.  If
                    // 3DMMEx: it's not known for sure the file's there, just make the
                    // 3DMMEx: user pick a new name.
                    if (tExists == tYes)
                    {
                        AssertDo(vapp.FGetStnApp(idsReplaceFile, &stnErr), "String not present");
                        tRet = vapp.TModal(vapp.PcrmAll(), ktpcQueryOverwrite, &stnErr, bkYesNo, kstidQueryOverwrite,
                                           &stnFile);
                    }
                    else
                    {
                        vapp.DisplayErrors();
                        tRet = tNo;
                    }

                    // 3DMMEx: Redisplay the portfolio if no overwrite.
                    if (tRet == tNo)
                    {
                        fRedisplayPortfolio = fTrue;

                        // 3DMMEx: Make sure the app window is updated, otherwise the help topic
                        // 3DMMEx: may still be displayed while the portfolio is redisplayed.
                        InvalidateRect(vwig.hwndApp, NULL, TRUE);
                        UpdateWindow(vwig.hwndApp);
                    }
                }
            }
            else
            {
                // 3DMMEx: The user OKed the selection, and yet with have a zero length file name.
                // 3DMMEx: Something must have gone wrong with the common dialog. Treat this as a
                // 3DMMEx: cancel of the portfolio. (Remember we have not set up pfni in this case.)
                Bug("Portfolio selection of a file with no name");

                fOKed = fFalse;
            }
        }
    } while (fRedisplayPortfolio);

    // 3DMMEx: If the user selected a file, build up the associated fni.
    if (!fOKed)
    {
        pfni->SetNil();
    }

    // 3DMMEx: Update the app window to make sure no parts of the portfolio
    // 3DMMEx: window are left on the screen while the file is being saved.
    // 3DMMEx: Calling UpdateMarked() does not have the desired effect here.
    UpdateWindow(vwig.hwndApp);

    // 3DMMEx: Report error to user if never displayed the portfolio.
    if (CommDlgExtendedError() != NOERROR)
    {
        PushErc(ercSocPortfolioFailed);
    }

    // 3DMMEx: Only return fTrue if the user selected a file.
    return (fOKed);
}

/** 3DMMEx: *************************************************************************

 OpenHookProc: Hook proc for get open/save common dlg.

 Arguments: standard dialog proc args.

 Returns: TRUE  - Common dlg will ignore message
          FALSE - Common dlg will process this message after custom dlg.

 Note - on win95, hwndCustom is the handle to the custom dlg created as a
 child of the common dlg.

***************************************************************************/
UINT_PTR CALLBACK OpenHookProc(HWND hwndCustom, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG: {
        PDLGINFO pdiPortfolio;
        LONG lwStyle, lwExstyle;
        OPENFILENAME *lpOfn;
        HWND hwndDlg;
        RC rc;
        WNDPROC lpOtherBtnProc;

        lpOfn = (OPENFILENAME *)lParam;
        pdiPortfolio = (PDLGINFO)(lpOfn->lCustData);

        SetWindowLongPtr(hwndCustom, GWLP_USERDATA, (LONG_PTR)pdiPortfolio);

        hwndDlg = GetParent(hwndCustom);

        // 3DMMEx: Give ourselves a way to access the custom dlg hwnd
        // 3DMMEx: from the common dlg subclass wndproc.
        SetWindowLongPtr(hwndDlg, GWLP_USERDATA, (LONG_PTR)hwndCustom);

        // 3DMMEx: Hide common dlg controls that we're not interested in here. Use the Common Dialog
        // 3DMMEx: Message for hiding the control. The documentation on CDM_HIDECONTROL doesn't really
        // 3DMMEx: describe what the command actually does, so explictly disable the OK/Cancel btns here.

        SendMessage(hwndDlg, CDM_HIDECONTROL, stc2, 0L); // 3DMMEx: File type label
        EnableWindow(GetDlgItem(hwndDlg, stc2), FALSE);

        SendMessage(hwndDlg, CDM_HIDECONTROL, cmb1, 0L); // 3DMMEx: File type combo box
        EnableWindow(GetDlgItem(hwndDlg, cmb1), FALSE);

        SendMessage(hwndDlg, CDM_HIDECONTROL, cmb13, 0L); // 3DMMEx: Filename entry box
        EnableWindow(GetDlgItem(hwndDlg, cmb13), FALSE);

        SendMessage(hwndDlg, CDM_HIDECONTROL, chx1, 0L); // 3DMMEx: Open as Read-only check box
        EnableWindow(GetDlgItem(hwndDlg, chx1), FALSE);

        // 3DMMEx: Even though we are hiding the OK/Cancel buttons, do not disable them.
        // 3DMMEx: By doing this, we retain some default common dialog behavior. When the
        // 3DMMEx: user hits the Enter key the highlighted file will be selected, and when
        // 3DMMEx: the user hits the Escape key the portfolio is dismissed.

        SendMessage(hwndDlg, CDM_HIDECONTROL, IDOK, 0L);     // 3DMMEx: OK btn.
        SendMessage(hwndDlg, CDM_HIDECONTROL, IDCANCEL, 0L); // 3DMMEx: Cancel btn.

        SendMessage(hwndDlg, CDM_HIDECONTROL, stc3, 0L); // 3DMMEx: 'File Name'
        EnableWindow(GetDlgItem(hwndDlg, stc3), FALSE);

        if (pdiPortfolio->fIsOpen)
        {
            SendMessage(hwndDlg, CDM_HIDECONTROL, edt1, 0L); // 3DMMEx: File name edit ctrl
            EnableWindow(GetDlgItem(hwndDlg, edt1), FALSE);
        }

        // 3DMMEx: If no preview required then hide the preview window.

        if (pdiPortfolio->grfPrevType == 0)
        {
            SendMessage(hwndCustom, CDM_HIDECONTROL, IDC_PREVIEW, 0L);
            EnableWindow(GetDlgItem(hwndCustom, IDC_PREVIEW), FALSE);
        }

        // 3DMMEx: Give the main common dlg the required style for custom display.
        // 3DMMEx: This means remove the caption bar, the system menu and the thick border.
        // 3DMMEx: Note, We do not need a frame round the window, as we customize its
        // 3DMMEx: background entirely. Therefore we should remove WS_CAPTION, (which is
        // 3DMMEx: WS_BORDER | WS_DLGFRAME), and WS_EX_DLGMODALFRAME. However, if we do
        // 3DMMEx: this, then when the user navigates around the list box in the dialog,
        // 3DMMEx: the app window gets repainted. Presumably this is due to win95
        // 3DMMEx: invalidating an area slightly larger than the dlg without any border.
        // 3DMMEx: Therefore maintain the thin border.

        lwStyle = GetWindowLong(hwndDlg, GWL_STYLE);
        lwStyle &= ~(WS_DLGFRAME | WS_SYSMENU);
        SetWindowLong(hwndDlg, GWL_STYLE, lwStyle);

        lwExstyle = GetWindowLong(hwndDlg, GWL_EXSTYLE);
        lwExstyle &= ~WS_EX_DLGMODALFRAME;
        SetWindowLong(hwndDlg, GWL_EXSTYLE, lwExstyle);

        // 3DMMEx: IMPORTANT NOTE. Cannot move or size the portfolio here. Otherwise this
        // 3DMMEx: confuses win95 when it calculates whether all the controls fit in the
        // 3DMMEx: common dlg or not. Instead we must wait for the INITDONE notification.
        // 3DMMEx: Note, since the portfolio is full screen now, we don't need to move
        // 3DMMEx: the window anyway..

        // 3DMMEx: Subclass the push btns to prevent the background flashing in the default color.
        lpBtnProc =
            (WNDPROC)SetWindowLongPtr(GetDlgItem(hwndCustom, IDC_BUTTON1), GWLP_WNDPROC, (LONG_PTR)SubClassBtnProc);

        lpOtherBtnProc =
            (WNDPROC)SetWindowLongPtr(GetDlgItem(hwndCustom, IDC_BUTTON2), GWLP_WNDPROC, (LONG_PTR)SubClassBtnProc);
        Assert(lpBtnProc == lpOtherBtnProc, "Custom portfolio buttons (ok/cancel) have different window procs");

        lpOtherBtnProc =
            (WNDPROC)SetWindowLongPtr(GetDlgItem(hwndCustom, IDC_BUTTON3), GWLP_WNDPROC, (LONG_PTR)SubClassBtnProc);
        Assert(lpBtnProc == lpOtherBtnProc, "Custom portfolio buttons (ok/home) have different window procs");

        // 3DMMEx: Subclass the preview window to allow custom draw.
        lpPreviewProc =
            (WNDPROC)SetWindowLongPtr(GetDlgItem(hwndCustom, IDC_PREVIEW), GWLP_WNDPROC, (LONG_PTR)SubClassPreviewProc);

        // 3DMMEx: Subclass the main common dlg window to stop static control backgrounds being
        // 3DMMEx: fill with the current system color. Instead use a color that matches our
        // 3DMMEx: custom background bitmap.
        lpDlgProc = (WNDPROC)SetWindowLongPtr(hwndDlg, GWLP_WNDPROC, (LONG_PTR)SubClassDlgProc);

        // 3DMMEx: For the save portfolio we want the file name control to have focus when displayed.
        if (!pdiPortfolio->fIsOpen)
        {
            SetFocus(GetDlgItem(hwndDlg, edt1));

            // 3DMMEx: Select all the text in the file name edit control.
            SendMessage(GetDlgItem(hwndDlg, edt1), EM_SETSEL, 0, -1);

            return (1);
        }

        // 3DMMEx: Allow Windows to set the focus wherever it wants to.
        return (0);
    }
    case WM_COMMAND: {
        // 3DMMEx: If the user has clicked on one of our custom buttons, then
        // 3DMMEx: simulate a click on one of the hidden common dlg buttons.

        if (HIWORD(wParam) == BN_CLICKED)
        {
            switch (LOWORD(wParam))
            {
            case IDC_BUTTON1:

                PostMessage(GetParent(hwndCustom), WM_COMMAND, IDOK, 0);
                return (1);

            case IDC_BUTTON2:

                PostMessage(GetParent(hwndCustom), WM_COMMAND, IDCANCEL, 0);
                return (1);

            case IDC_BUTTON3: {
                FNI fniUserDir;
                STN stnUserDir;
                SZ szUserDir;
                SZ szCurFile;
                HWND hwndDlg = GetParent(hwndCustom);

                // 3DMMEx: The user has pressed the Go To User's Home Folder btn. So get a SZ
                // 3DMMEx: for the user's home folder.

                vapp.GetFniUser(&fniUserDir);
                fniUserDir.GetStnPath(&stnUserDir);
                stnUserDir.GetSz(szUserDir);

                // 3DMMEx: Ideally there would be some CDM_ message here to tell the common dialog
                // 3DMMEx: that the current folder is to be changed. Unfortunately there is no such
                // 3DMMEx: message. There is also no recommended way of doing this. Note that if we
                // 3DMMEx: call SetCurrentDirectory here, then the common dialog is oblivious of the
                // 3DMMEx: folder change.

                // 3DMMEx: SO, we shall simulate a folder change by the user. Do this by filling
                // 3DMMEx: the file name edit control with the target folder, and then sending an
                // 3DMMEx: OK message to the common dlg.

                // 3DMMEx: This would be enough to change the folder, but we would lose the current
                // 3DMMEx: contents of the file name control. Therefore store the control contents
                // 3DMMEx: before we overwrite it, and reset it afterwards. Ideally there would be
                // 3DMMEx: some message to retrieve the contents of a control, (eg CDM_GETCONTROLTEXT),
                // 3DMMEx: but there isn't, so use a regular Windows call for this.

                // 3DMMEx: Get the current text for the control.
                GetDlgItemText(hwndDlg, edt1, szCurFile, CvFromRgv(szCurFile));

                // 3DMMEx: Now change folder!
                SendMessage(hwndDlg, CDM_SETCONTROLTEXT, edt1, (LPARAM)szUserDir);
                SendMessage(hwndDlg, WM_COMMAND, IDOK, 0);

                // 3DMMEx: Restore the edit control text.
                SendMessage(hwndDlg, CDM_SETCONTROLTEXT, edt1, (LPARAM)szCurFile);

                return (1);
            }
            default:
                break;
            }
        }

        break;
    }
    case WM_DRAWITEM: {
        int iDlgId = (int)wParam;
        DRAWITEMSTRUCT *pDrawItem = (DRAWITEMSTRUCT *)lParam;
        CNO cnoDisplay = cnoNil;
        PMBMP pmbmp;

        // 3DMMEx: Custom draw the our push btns here.
        switch (iDlgId)
        {
        case IDC_BUTTON1:

            if (pDrawItem->itemState & ODS_SELECTED)
            {
                cnoDisplay = kcnoMbmpPortBtnOkSel;
            }
            else
            {
                cnoDisplay = kcnoMbmpPortBtnOk;
            }

            break;

        case IDC_BUTTON2:

            if (pDrawItem->itemState & ODS_SELECTED)
            {
                cnoDisplay = kcnoMbmpPortBtnCancelSel;
            }
            else
            {
                cnoDisplay = kcnoMbmpPortBtnCancel;
            }

            break;

        case IDC_BUTTON3:

            if (pDrawItem->itemState & ODS_SELECTED)
            {
                cnoDisplay = kcnoMbmpPortBtnHomeSel;
            }
            else
            {
                cnoDisplay = kcnoMbmpPortBtnHome;
            }

            break;

        default:

            break;
        }

        if (cnoDisplay != cnoNil)
        {
            // 3DMMEx: Select the appropriate bitmap to display.

            if ((pmbmp = (PMBMP)vpapp->PcrmAll()->PbacoFetch(kctgMbmp, cnoDisplay, MBMP::FReadMbmp)))
            {
                PGPT pgpt;
                HPEN hpen, hpenold;
                HBRUSH hbr, hbrold;
                HFONT hfnt, hfntold;

                // 3DMMEx: To ensure that we don't return from here with different objects
                // 3DMMEx: selected in the supplied hdc, save them here and restore later.
                // 3DMMEx: (Note that Kauai will attempt to delete things it finds selected
                // 3DMMEx: in the hdc passed to PgptNew).

                hpen = (HPEN)GetStockObject(NULL_PEN);
                Assert(hpen != hNil, "Portfolio - draw items GetStockObject(NULL_PEN) failed");
                hpenold = (HPEN)SelectObject(pDrawItem->hDC, hpen);

                hbr = (HBRUSH)GetStockObject(WHITE_BRUSH);
                Assert(hbr != hNil, "Portfolio - draw items GetStockObject(WHITE_BRUSH) failed");
                hbrold = (HBRUSH)SelectObject(pDrawItem->hDC, hbr);

                hfnt = (HFONT)GetStockObject(SYSTEM_FONT);
                Assert(hfnt != hNil, "Portfolio - draw items GetStockObject(SYSTEM_FONT) failed");
                hfntold = (HFONT)SelectObject(pDrawItem->hDC, hfnt);

                if ((pgpt = GPT::PgptNew(pDrawItem->hDC)) != pvNil)
                {
                    GNV gnv(pgpt);
                    PGNV pgnvOff;
                    PGPT pgptOff;
                    RC rcItem(pDrawItem->rcItem);

                    // 3DMMEx: Must create offscreen dc and blit into that. Then blit that
                    // 3DMMEx: to dlg on screen. If we blit straight from mbmp to screen,
                    // 3DMMEx: then screen flashes. (Due to white fillrect).

                    if ((pgptOff = GPT::PgptNewOffscreen(&rcItem, 8)) != pvNil)
                    {
                        if ((pgnvOff = NewObj GNV(pgptOff)) != pvNil)
                        {
                            pgnvOff->DrawMbmp(pmbmp, rcItem.xpLeft, rcItem.ypTop);

                            gnv.CopyPixels(pgnvOff, &rcItem, &rcItem);
                            GPT::Flush();

                            ReleasePpo(&pgnvOff);
                        }

                        ReleasePpo(&pgptOff);
                    }

                    ReleasePpo(&pgpt);
                }

                // 3DMMEx: Restore the currently secleted objects back into he supplied hdc.

                SelectObject(pDrawItem->hDC, hpenold);
                SelectObject(pDrawItem->hDC, hbrold);
                SelectObject(pDrawItem->hDC, hfntold);

                ReleasePpo(&pmbmp);
            }

            return (1);
        }

        break;
    }
    case WM_NOTIFY: {
        LPOFNOTIFY lpofNotify = (LPOFNOTIFY)lParam;

        switch (lpofNotify->hdr.code)
        {
        case CDN_INITDONE: {
            // 3DMMEx: Win95 has finished doing any resizing of the custom dlg and the controls.
            // 3DMMEx: So take any special action now to ensure the portfolio still looks good.

            PDLGINFO pdiPortfolio = (PDLGINFO)GetWindowLongPtr(hwndCustom, GWLP_USERDATA);
            RECT rcsApp;
            POINT ptBtn;
            int ypBtn;
            PMBMP pmbmpBtn;
            RC rcBmp;
            RECT rcsAppScreen, rcsPreview;
            int xOff = 0;
            int yOff = 0;
            LONG lStyle;
            PCRF pcrf;
            HWND hwndDlg = GetParent(hwndCustom);
            HWND hwndApp = GetParent(hwndDlg);
            HWND hwndPreview = GetDlgItem(hwndCustom, IDC_PREVIEW);

            // 3DMMEx: Store the current size of the portfolio. We need this later when we stretch
            // 3DMMEx: the background bitmap to match how win95 may have stretched the dlg controls.
            GetClientRect(hwndDlg, &(pdiPortfolio->rcsDlg));

            // 3DMMEx: Get the client area size of the app window.
            GetClientRect(hwndApp, &rcsApp);

            // 3DMMEx: Now move the custom buttons to always be to the right of the preview window.
            // 3DMMEx: Make sure we keep the buttons above the bottom of the screen.

            GetClientRect(hwndPreview, &rcsPreview);
            ptBtn.x = rcsPreview.right;
            ptBtn.y = (rcsPreview.top + rcsPreview.bottom) / 2;
            MapWindowPoints(hwndPreview, hwndCustom, (POINT *)&ptBtn, 1);

            // 3DMMEx: First the home button.
            if ((pmbmpBtn = (PMBMP)vpapp->PcrmAll()->PbacoFetch(kctgMbmp, kcnoMbmpPortBtnHome, MBMP::FReadMbmp)))
            {
                pmbmpBtn->GetRc(&rcBmp);

                ptBtn.x += (3 * rcBmp.Dxp()) / 4;
                ypBtn = min(ptBtn.y - (rcBmp.Dyp() / 2), rcsApp.bottom - (2 * rcBmp.Dyp()));

                SetWindowPos(GetDlgItem(hwndCustom, IDC_BUTTON3), 0, ptBtn.x, ypBtn, rcBmp.Dxp(), rcBmp.Dyp(),
                             SWP_NOZORDER);

                ReleasePpo(&pmbmpBtn);
            }

            // 3DMMEx: Now the cancel button.
            if ((pmbmpBtn = (PMBMP)vpapp->PcrmAll()->PbacoFetch(kctgMbmp, kcnoMbmpPortBtnCancel, MBMP::FReadMbmp)))
            {
                pmbmpBtn->GetRc(&rcBmp);

                ptBtn.x += (5 * rcBmp.Dxp()) / 4;
                ypBtn = min(ptBtn.y - (rcBmp.Dyp() / 2), rcsApp.bottom - (2 * rcBmp.Dyp()));

                SetWindowPos(GetDlgItem(hwndCustom, IDC_BUTTON2), 0, ptBtn.x, ypBtn, rcBmp.Dxp(), rcBmp.Dyp(),
                             SWP_NOZORDER);

                ReleasePpo(&pmbmpBtn);
            }

            // 3DMMEx: Now the ok button.
            if ((pmbmpBtn = (PMBMP)vpapp->PcrmAll()->PbacoFetch(kctgMbmp, kcnoMbmpPortBtnOk, MBMP::FReadMbmp)))
            {
                pmbmpBtn->GetRc(&rcBmp);

                ptBtn.x += (5 * rcBmp.Dxp()) / 4;
                ypBtn = min(ptBtn.y - (rcBmp.Dyp() / 2), rcsApp.bottom - (2 * rcBmp.Dyp()));

                SetWindowPos(GetDlgItem(hwndCustom, IDC_BUTTON1), 0, ptBtn.x, ypBtn, rcBmp.Dxp(), rcBmp.Dyp(),
                             SWP_NOZORDER);

                ReleasePpo(&pmbmpBtn);
            }

            // 3DMMEx: Note, win95 may have pushed the portfolio around depending on where the task bar is.
            // 3DMMEx: Ensure that the top left corner of the portfolio is in the top left of the client
            // 3DMMEx: area of the app window. Note that the common dlg is owned by the app window, but
            // 3DMMEx: it is not a child window itself.

            // 3DMMEx: Note, if we MapWindowPoints on (0,0) here, then the returned coords are shifted by
            // 3DMMEx: the task bar which is just what we don't want. So GetWindowRect instead.
            GetWindowRect(hwndApp, &rcsAppScreen);

            // 3DMMEx: If running in a window, then we must find the offset from the top left of the app
            // 3DMMEx: window's corner to its client area.
            lStyle = GetWindowLong(hwndApp, GWL_STYLE);

            if (lStyle & WS_CAPTION)
            {
                xOff = GetSystemMetrics(SM_CXDLGFRAME);

                yOff = GetSystemMetrics(SM_CYDLGFRAME);
                yOff += GetSystemMetrics(SM_CYCAPTION);
            }

            // 3DMMEx: Now move the common dialog itself. Resize the dialog too, to be the same size
            // 3DMMEx: as the app window.
            SetWindowPos(hwndDlg, 0, rcsAppScreen.left + xOff, rcsAppScreen.top + yOff, rcsApp.right, rcsApp.bottom,
                         SWP_NOZORDER);

            // 3DMMEx: Custom dialog is a child of the common dlg, so (0, 0) wil position it it the common
            // 3DMMEx: dialog's client area.
            SetWindowPos(hwndCustom, 0, 0, 0, rcsApp.right, rcsApp.bottom, SWP_NOZORDER);

            // 3DMMEx: Now play the sound associated with this portfolio is there is one.
            // 3DMMEx: Note that we are not currently queueing this sound or terminating
            // 3DMMEx: any currently playing sound.
            if (pdiPortfolio->cnoWave != cnoNil)
            {
                // 3DMMEx: There is a sound for the portfolio, so find it.
                if ((pcrf = ((APP *)vpappb)->PcrmAll()->PcrfFindChunk(kctgWave, pdiPortfolio->cnoWave)) != pvNil)
                {
                    vpsndm->SiiPlay(pcrf, kctgWave, pdiPortfolio->cnoWave, ksqnNone, kvlmFull, 1, 0, 0, ksclUISound);
                }
            }

            break;
        }
        case CDN_SELCHANGE: {
            HWND hwndPreview = GetDlgItem(hwndCustom, IDC_PREVIEW);

            // 3DMMEx: User has changed the file selected, so update the preview window.
            InvalidateRect(hwndPreview, NULL, FALSE);
            UpdateWindow(hwndPreview);

            break;
        }
#ifdef QUERYINPORTFOLIO
            // 3DMMEx: Do this if we ever have a way of querying the user with a help topic
            // 3DMMEx: from inside the portfolio.
        case CDN_FILEOK: {
            PDLGINFO pdiPortfolio = (PDLGINFO)GetWindowLong(hwndCustom, GWL_USERDATA);

            // 3DMMEx: User has hit OK or Save. Is the user trying to save over an existing file?
            if (pdiPortfolio->fIsOpen != fTrue)
            {
                // 3DMMEx: Neither CommDlg_OpenSave_GetFilePath nor CommDlg_OpenSave_GetSpec
                // 3DMMEx: always return the string with the default extension already added
                // 3DMMEx: if applicable. Therefore get the file name from the returned OFN
                // 3DMMEx: structure, as this stores the complete file name.

                if (CchSz(lpofNotify->lpOFN->lpstrFile) != 0)
                {
                    FNI fni;
                    STN stnFile, stnErr;
                    bool fHelp, tRet;
                    int32_t lSelect;

                    // 3DMMEx: Now does the specified file already exist?

                    stnFile.SetSz(lpofNotify->lpOFN->lpstrFile);

                    fni.FBuildFromPath(&stnFile, 0);

                    if (fni.TExists() != tNo)
                    {
                        if (vpappb->TGiveAlertSz("The selected file already exists.\n\nDo you want to overwrite it?",
                                                 bkYesNo, cokQuestion) != tYes)
                        {
                            // 3DMMEx: Move the focus back to the file name edit ctrl.
                            SetFocus(GetDlgItem(GetParent(hwndCustom), edt1));

                            // 3DMMEx: Let win95 know that the portfolio is to stay up.
                            SetWindowLong(hwndCustom, DWL_MSGRESULT, 1);
                            return (1);
                        }
                    }
                }
            }

            break;
        }
#endif // 3DMMEx: QUERYINPORTFOLIO
        default: {
            break;
        }
        }

        break;
    }
    case WM_ERASEBKGND:

        // 3DMMEx: We never want the background painted in the system colors.
        return (1);

    case WM_PAINT: {
        PDLGINFO pdiPortfolio;

        pdiPortfolio = (PDLGINFO)GetWindowLongPtr(hwndCustom, GWLP_USERDATA);

        // 3DMMEx: Repaint the entire portfolio.
        RepaintPortfolio(hwndCustom);

        // 3DMMEx: Now the background is drawn, disallow erasing of the background in the
        // 3DMMEx: system color before each future repaint.
        pdiPortfolio->fDrawnBkgnd = fTrue;

        return (1);
    }
    default:

        break;
    }

    return (0);
}

/** 3DMMEx: *************************************************************************

 RepaintPortfolio: Repaint the entire portfolio.

 Arguments: hwndCustom	- Handle to our custom dialog, ie child of the main common dlg.

 Returns: 	nothing

***************************************************************************/
void RepaintPortfolio(HWND hwndCustom)
{
    PAINTSTRUCT ps;
    TEXTMETRIC tmCaption;
    SZ szCaption;
    PDLGINFO pdiPortfolio = (PDLGINFO)GetWindowLongPtr(hwndCustom, GWLP_USERDATA);
    PMBMP pmbmp, pmbmpBtn;
    int iBtn;
    CNO cnoBack;

    // 3DMMEx: Draw the custom background for the common dlg.
    BeginPaint(hwndCustom, &ps);

    // 3DMMEx: Display the open or save portfolio background bitmap as appropriate.
    if (pdiPortfolio->fIsOpen)
    {
        cnoBack = kcnoMbmpPortBackOpen;
    }
    else
    {
        cnoBack = kcnoMbmpPortBackSave;
    }

    // 3DMMEx: Get the background bitmap first.
    if ((pmbmp = (PMBMP)vpapp->PcrmAll()->PbacoFetch(kctgMbmp, cnoBack, MBMP::FReadMbmp)))
    {
        PGPT pgpt;
        HPEN hpen, hpenold;
        HBRUSH hbr, hbrold;
        HFONT hfnt, hfntold;

        // 3DMMEx: To ensure that we don't return from here with different objects
        // 3DMMEx: selected in the supplied hdc, save them here and restore later.
        // 3DMMEx: (Note that Kauai will attempt to delete things it finds selected
        // 3DMMEx: in the hdc passed to PgptNew).

        hpen = (HPEN)GetStockObject(NULL_PEN);
        Assert(hpen != hNil, "Portfolio - draw background GetStockObject(NULL_PEN) failed");
        hpenold = (HPEN)SelectObject(ps.hdc, hpen);

        hbr = (HBRUSH)GetStockObject(WHITE_BRUSH);
        Assert(hbr != hNil, "Portfolio - draw background GetStockObject(WHITE_BRUSH) failed");
        hbrold = (HBRUSH)SelectObject(ps.hdc, hbr);

        hfnt = (HFONT)GetStockObject(SYSTEM_FONT);
        Assert(hfnt != hNil, "Portfolio - draw background GetStockObject(SYSTEM_FONT) failed");
        hfntold = (HFONT)SelectObject(ps.hdc, hfnt);

        if ((pgpt = GPT::PgptNew(ps.hdc)) != pvNil)
        {
            RC rcDisplay;
            RECT rcsPort, rcsPreview;
            GNV gnv(pgpt);
            PGNV pgnvOff;
            PGPT pgptOff;
            HWND hwndPreview;
            CNO cnoBtn;
            int iBtnId;

            // 3DMMEx: Get the current size of the Portfolio window.
            GetClientRect(hwndCustom, &rcsPort);
            rcDisplay = rcsPort;

            // 3DMMEx: Must create offscreen dc and blit into that. Then blit that to dlg on screen.
            // 3DMMEx: If we blit straight from	mbmp to screen, then screen flashes. (Due to white FILLRECT).

            if ((pgptOff = GPT::PgptNewOffscreen(&rcDisplay, 8)) != pvNil)
            {
                if ((pgnvOff = NewObj GNV(pgptOff)) != pvNil)
                {
                    RC rcOrgPort(pdiPortfolio->rcsDlg);
                    RC rcClip(ps.rcPaint);

                    // 3DMMEx: Win95 may initially have streched the portfolio due to font sizing.
                    // 3DMMEx: All the portfolio controls will also have been stretched. So we must
                    // 3DMMEx: now stretch the portfolio background bitmap into the size win95
                    // 3DMMEx: originally made it, to make the background fit the current size
                    // 3DMMEx: of the controls.

                    // 3DMMEx: Note, if the user has scaled down the font then the portfolio bitmap
                    // 3DMMEx: will not fill the entire portfolio area now. The area outside the portfolio
                    // 3DMMEx: will be black. While this is not ideal, it will be rare and everything will
                    // 3DMMEx: still work.
                    pgnvOff->DrawMbmp(pmbmp, &rcOrgPort);

                    // 3DMMEx: While we have this offscreen dc, paint all our custom display into it now.

                    // 3DMMEx: First the caption text. Position the text using its size.
                    GetWindowText(GetParent(hwndCustom), szCaption, CvFromRgv(szCaption));
                    GetTextMetrics(ps.hdc, &tmCaption);
                    pgnvOff->DrawRgch(szCaption, CchSz(szCaption), (tmCaption.tmAveCharWidth * 2),
                                      tmCaption.tmHeight / 4, kacrBlack, kacrClear);

                    // 3DMMEx: If we were to blit the current offscreen dc to the screen, then the
                    // 3DMMEx: display of the preview window and custom buttons will be overwritten.
                    // 3DMMEx: Those windows would immediately be repainted to make the portfolio
                    // 3DMMEx: whole again. However, the windows would flash between blitting the
                    // 3DMMEx: background bitmap and repainting them. Therefore add the preview
                    // 3DMMEx: and custom buttons to the offscreen dc now, so that when the dc
                    // 3DMMEx: is blitted to the screen, these controls are good.

                    // 3DMMEx: Note, that the preview window alone is repainted when the user selection
                    // 3DMMEx: changes. Also the custom buttons are redrawn when we get the notification
                    // 3DMMEx: to redraw them in the dlg hook proc.

                    // 3DMMEx: Update the preview window now.
                    hwndPreview = GetDlgItem(hwndCustom, IDC_PREVIEW);
                    GetClientRect(hwndPreview, &rcsPreview);
                    MapWindowPoints(hwndPreview, hwndCustom, (POINT *)&rcsPreview, 2);

                    OpenPreview(hwndCustom, pgnvOff, &rcsPreview);

                    // 3DMMEx: Now the custom buttons.
                    for (iBtn = 0; iBtn < 3; ++iBtn)
                    {
                        switch (iBtn)
                        {
                        case 0:

                            cnoBtn = kcnoMbmpPortBtnOk;
                            iBtnId = IDC_BUTTON1;

                            break;

                        case 1:

                            cnoBtn = kcnoMbmpPortBtnCancel;
                            iBtnId = IDC_BUTTON2;

                            break;

                        case 2:

                            cnoBtn = kcnoMbmpPortBtnHome;
                            iBtnId = IDC_BUTTON3;

                            break;

                        default:
                            continue;
                        }

                        if ((pmbmpBtn = (PMBMP)vpapp->PcrmAll()->PbacoFetch(kctgMbmp, cnoBtn, MBMP::FReadMbmp)))
                        {
                            HWND hwndBtn = GetDlgItem(hwndCustom, iBtnId);
                            RECT rcsBtn;
                            RC rcItem;

                            GetClientRect(hwndBtn, &rcsBtn);
                            MapWindowPoints(hwndBtn, hwndCustom, (POINT *)&rcsBtn, 2);
                            rcItem = rcsBtn;

                            pgnvOff->DrawMbmp(pmbmpBtn, rcItem.xpLeft, rcItem.ypTop);

                            ReleasePpo(&pmbmpBtn);
                        }
                    }

                    // 3DMMEx: Clip the final blit, to the area which actually needs repainting.
                    gnv.ClipRc(&rcClip);

                    // 3DMMEx: Now finally blit our portfolio image to the screen.
                    gnv.CopyPixels(pgnvOff, &rcDisplay, &rcDisplay);
                    GPT::Flush();

                    ReleasePpo(&pgnvOff);
                }

                ReleasePpo(&pgptOff);
            }

            ReleasePpo(&pgpt);
        }

        // 3DMMEx: Restore the currently secleted objects back into he supplied hdc.
        SelectObject(ps.hdc, hpenold);
        SelectObject(ps.hdc, hbrold);
        SelectObject(ps.hdc, hfntold);

        ReleasePpo(&pmbmp);
    }

    EndPaint(hwndCustom, &ps);

    return;
}

/** 3DMMEx: *************************************************************************

 OpenPreview: Generate display for the preview of a movie.

 Arguments: hwndCustom	- Handle to custom dlg window
            pgnvOff		- Offscreen dc for displaying preview in.
            prcsPreview - RCS for displaying preview.

 Returns: nothing.

***************************************************************************/
void OpenPreview(HWND hwndCustom, PGNV pgnvOff, RECT *prcsPreview)
{
    STN stn;
    PCFL pcfl;
    PMBMP pmbmp;
    FNI fni;
    SZ szFile;
    ERS ersT;
    ERS *pers;
    PDLGINFO pdiPortfolio = (PDLGINFO)GetWindowLongPtr(hwndCustom, GWLP_USERDATA);
    bool fPreviewed = fFalse;
    RC rcPreview(*prcsPreview);

    // 3DMMEx: If no preview is required, then do nothing here.
    if (pdiPortfolio->grfPrevType == 0)
        return;

    // 3DMMEx: Do not allow default error reporting to take place. This is due
    // 3DMMEx: to the fact that currently any queued errors do not appear
    // 3DMMEx: until the portfolio has been dismissed.

    pers = vpers;
    vpers = &ersT;

    // 3DMMEx: Clear the current contents of the preview window.
    pgnvOff->FillRc(&rcPreview, kacrBlack);

    // 3DMMEx: Get the currently selected file name.
    CommDlg_OpenSave_GetSpec(GetParent(hwndCustom), szFile, sizeof(szFile));

    // 3DMMEx: Note the above call returns the name of the last selected file, even if the
    // 3DMMEx: user has since selected a folder! If the user hits the OK btn in this case,
    // 3DMMEx: win95 opens the last selected file anyway, so we are consistent here.

    if (CchSz(szFile) != 0)
    {
        // 3DMMEx: Get an fni for the selected file.
        stn.SetSz(szFile);
        fni.FBuildFromPath(&stn, 0);

        // 3DMMEx: If the user specified a directory, then don't preview it.
        if (!fni.FDir())
        {
            // 3DMMEx: The name specifies a file. How should we preview it?
            if (pdiPortfolio->grfPrevType & fpfPortPrevMovie)
            {
                // 3DMMEx: Preview it as a movie if we can.
                if ((pcfl = CFL::PcflOpen(&fni, fcflNil)) != pvNil)
                {
                    CKI ckiMovie;
                    KID kidScene, kidThumb;
                    BLCK blck;

                    // 3DMMEx: Get the movie chunk from the open file.
                    if (pcfl->FGetCkiCtg(kctgMvie, 0, &ckiMovie))
                    {
                        // 3DMMEx: Now get the scene chunk and details.
                        if (pcfl->FGetKidChidCtg(kctgMvie, ckiMovie.cno, 0, kctgScen, &kidScene))
                        {
                            // 3DMMEx: Get the mbmp for the movie thumbnail.
                            if (pcfl->FGetKidChidCtg(kctgScen, kidScene.cki.cno, 0, kctgThumbMbmp, &kidThumb) &&
                                pcfl->FFind(kidThumb.cki.ctg, kidThumb.cki.cno, &blck))
                            {
                                if ((pmbmp = MBMP::PmbmpRead(&blck)) != pvNil)
                                {
                                    // 3DMMEx: Stretch the preview into the preview window.
                                    pgnvOff->DrawMbmp(pmbmp, &rcPreview);

                                    ReleasePpo(&pmbmp);

                                    fPreviewed = fTrue;
                                }
                            }
                        }
                    }

                    ReleasePpo(&pcfl);
                }
            }

            if (!fPreviewed && (pdiPortfolio->grfPrevType & fpfPortPrevTexture))
            {
                // 3DMMEx: Preview the file as a .bmp file if we can.
                if ((pmbmp = MBMP::PmbmpReadNative(&fni, 0, 0, 0, fmbmpNil)) != pvNil)
                {
                    // 3DMMEx: Stretch the bitmap in the preview window.
                    pgnvOff->DrawMbmp(pmbmp, &rcPreview);
                    ReleasePpo(&pmbmp);

                    fPreviewed = fTrue;
                }
            }

            // 3DMMEx: Currently portfolio is not required to preview sound files.
        }
    }

    // 3DMMEx: Restore the default error reporting.
    vpers = pers;

    return;
}

/** 3DMMEx: *************************************************************************

 SubClassBtnProc: Subclass proc for custom btn ctrls..

 Arguments: standard dialog proc args.

 Returns: TRUE/FALSE
***************************************************************************/
LRESULT CALLBACK SubClassBtnProc(HWND hwndBtn, UINT msg, WPARAM wParam, LPARAM lParam)
{

    switch (msg)
    {
    case WM_ERASEBKGND:

        // 3DMMEx: We draw the button entirely later, so don't change the screen
        // 3DMMEx: at all here, This prevents any flashing while dlg repainted.
        return (1);

    default:

        break;
    }

    return (CallWindowProc(lpBtnProc, hwndBtn, msg, wParam, lParam));
}

/** 3DMMEx: *************************************************************************

 SubClassPreviewProc: Subclass proc for preview window.

 Arguments: standard dialog proc args.

 Returns: TRUE/FALSE
***************************************************************************/
LRESULT CALLBACK SubClassPreviewProc(HWND hwndPreview, UINT msg, WPARAM wParam, LPARAM lParam)
{

    switch (msg)
    {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT rcsPreview;
        RC rcPreview;
        PGNV pgnvOff;
        PGPT pgpt, pgptOff;
        HPEN hpen, hpenold;
        HBRUSH hbr, hbrold;
        HFONT hfnt, hfntold;

        // 3DMMEx: Subclass this window so that when the user selects a file, we can only
        // 3DMMEx: invalidate and repaint this window, rather than repainting the entire dlg.
        BeginPaint(hwndPreview, &ps);

        // 3DMMEx: To ensure that we don't return from here with different objects
        // 3DMMEx: selected in the supplied hdc, save them here and restore later.
        // 3DMMEx: (Note that Kauai will attempt to delete things it finds selected
        // 3DMMEx: in the hdc passed to PgptNew).

        hpen = (HPEN)GetStockObject(NULL_PEN);
        Assert(hpen != hNil, "Portfolio - draw Preview GetStockObject(NULL_PEN) failed");
        hpenold = (HPEN)SelectObject(ps.hdc, hpen);

        hbr = (HBRUSH)GetStockObject(WHITE_BRUSH);
        Assert(hbr != hNil, "Portfolio - draw Preview GetStockObject(WHITE_BRUSH) failed");
        hbrold = (HBRUSH)SelectObject(ps.hdc, hbr);

        hfnt = (HFONT)GetStockObject(SYSTEM_FONT);
        Assert(hfnt != hNil, "Portfolio - draw Preview GetStockObject(SYSTEM_FONT) failed");
        hfntold = (HFONT)SelectObject(ps.hdc, hfnt);

        if ((pgpt = GPT::PgptNew(ps.hdc)) != pvNil)
        {
            GNV gnv(pgpt);

            GetClientRect(hwndPreview, &rcsPreview);
            rcPreview = rcsPreview;

            if ((pgptOff = GPT::PgptNewOffscreen(&rcPreview, 8)) != pvNil)
            {
                if ((pgnvOff = NewObj GNV(pgptOff)) != pvNil)
                {
                    // 3DMMEx: Get the preview image into our offscreen dc.
                    OpenPreview(GetParent(hwndPreview), pgnvOff, &rcsPreview);

                    // 3DMMEx: Now update the screen.
                    gnv.CopyPixels(pgnvOff, &rcPreview, &rcPreview);
                    GPT::Flush();

                    ReleasePpo(&pgnvOff);
                }

                ReleasePpo(&pgptOff);
            }

            ReleasePpo(&pgpt);
        }

        // 3DMMEx: Restore the currently secleted objects back into he supplied hdc.
        SelectObject(ps.hdc, hpenold);
        SelectObject(ps.hdc, hbrold);
        SelectObject(ps.hdc, hfntold);

        EndPaint(hwndPreview, &ps);

        return (0);
    }
    default:

        break;
    }

    return (CallWindowProc(lpPreviewProc, hwndPreview, msg, wParam, lParam));
}

/** 3DMMEx: *************************************************************************

 SubClassDlgProc: Subclass proc for common dlg window..

 Arguments: standard dialog proc args.

 Returns: TRUE/FALSE
***************************************************************************/
LRESULT CALLBACK SubClassDlgProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{

    switch (msg)
    {
    case WM_HELP: {
        // 3DMMEx: Do not invoke any help while the portfolio is displayed.
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;

        // 3DMMEx: Process this to ensure the common dlg static controls look as good
        // 3DMMEx: as possible on our custom portfolio background. We could explictly
        // 3DMMEx: do this only for the two static ctrl currently used,
        // 3DMMEx: (ie stc3 = File Name, stc4 = Look In), but by taking this general
        // 3DMMEx: approach, if any new static ctrls are added in future versions of
        // 3DMMEx: win95, they will also look ok. (Except for the fact they won't be
        // 3DMMEx: known to the current custom portfolio background). This assumes
        // 3DMMEx: that we never want any common dlg static controls to be drawn using
        // 3DMMEx: the default system colors, but that's ok due the extent of our
        // 3DMMEx: portfolio customization.

        // 3DMMEx: Do not overwrite the background when writing the static control text.
        SetBkMode(hdc, TRANSPARENT);

        // 3DMMEx: Note, on win95 that static text always appears black, regardless of
        // 3DMMEx: the current system color settings. However, rather than relying on
        // 3DMMEx: this always being true, set the text color explicitly here.
        SetTextColor(hdc, RGB(0, 0, 0));

        // 3DMMEx: Draw the control background in the light gray that matched the custom
        // 3DMMEx: background bitmap. Otherwise the background is drawn in the current
        // 3DMMEx: system color, which may not match the background at all.
        return ((LONG_PTR)GetStockObject(LTGRAY_BRUSH));
    }
    case WM_SYSCOMMAND: {
        // 3DMMEx: Is a screen saver trying to start?
        if (wParam == SC_SCREENSAVE)
        {
            // 3DMMEx: Disable the screen saver if we don't allow them to run.
            if (!vpapp->FAllowScreenSaver())
                return fTrue;
        }

        break;
    }
    case WM_ERASEBKGND: {
        // 3DMMEx: Since the user never sees the common dialog background, ensure that
        // 3DMMEx: it can never be erased in the system color. Force a repaint of the
        // 3DMMEx: custom dlg now, to prevent the common dlg controls appearing before
        // 3DMMEx: the portfolio background. Note that GetDlgItem(hwndDlg, <custom dlg id>)
        // 3DMMEx: returns zero here, as the Menu part of the custom dlg is zero.
        HWND hwndCustom = (HWND)GetWindowLongPtr(hwndDlg, GWLP_USERDATA);

        if (hwndCustom != 0)
            UpdateWindow(hwndCustom);

        return (1);
    }
    default:

        break;
    }

    return (CallWindowProc(lpDlgProc, hwndDlg, msg, wParam, lParam));
}
