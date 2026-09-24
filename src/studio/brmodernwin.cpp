/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    brmodernwin.cpp: Experimental BRender 1.4 OpenGL viewport bridge

    Keeps the Win32/Kauai editor intact while BRender 1.4 renders through
    glrend into the existing -v window. Normally the GPU result is read back
    into the BWLD RGB buffer. Native external-resolution modes deliberately
    keep the full-resolution GPU frame external and black the embedded view.

***************************************************************************/
#include "studio.h"

#if defined(KAUAI_WIN32) && defined(BRENDER_MODERN_14)

#include <windows.h>
#include <shellapi.h>
#include <GL/gl.h>
#include <brglrend.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>

extern void DiagLogBRender(const char *pszFormat, ...);

namespace
{
const int32_t kdxpModernFrontDefault = 544;
const int32_t kdypModernFrontDefault = 306;
const uint32_t kcbPrimitiveHeap = 1500 * 1024;

static int32_t vdxpModernFront = kdxpModernFrontDefault;
static int32_t vdypModernFront = kdypModernFrontDefault;
static bool vfModernExternalOnly = false;

static bool vfModernLogEnabled = false;
static bool vfShadowLogEnabled = false;
static volatile LONG vlModernLogSequence = 0;
static volatile LONG vlShadowLogSequence = 0;
static volatile LONG vlModernExceptionLogging = 0;
static PVOID vpModernVectoredHandler = NULL;
static uint32_t vcModernFrames = 0;
static bool vfModernCameraInitDeferredLogged = false;
static uint32_t vcModernFrameDumps = 0;
static uint32_t vdwModernLastCpuDumpHash = 0;

// Keep the flight recorder open and buffered while diagnostics are enabled.
// The previous implementation opened, flushed and closed modern_br.log for
// every single line, which turned complex movie playback into a filesystem
// benchmark.  Periodic flushes retain crash usefulness without paying that
// cost hundreds/thousands of times per second.
static FILE *vpModernLogFile = NULL;
static FILE *vpShadowLogFile = NULL;
static SRWLOCK vsrwModernLog = SRWLOCK_INIT;
static SRWLOCK vsrwShadowLog = SRWLOCK_INIT;
static uint32_t vcModernLogLinesSinceFlush = 0;
const uint32_t kcModernLogLinesPerFlush = 128;

static br_diaghandler *vpModernPreviousDiagHandler = NULL;
static void ModernLogFlush(void);
static void ShadowLogV(const char *pszFormat, va_list ap);
static void ShadowLogFlush(void);

static const char *PszShadowDiagMessage(const char *pszMessage)
{
    if (pszMessage == NULL)
        return NULL;
    if (strncmp(pszMessage, "SHADOW ", 7) == 0)
        return pszMessage;
    if (strncmp(pszMessage, "Warning: SHADOW ", 16) == 0)
        return pszMessage + 9;
    return NULL;
}

static void BR_CALLBACK ModernDiagWarning(const char *pszMessage)
{
    const char *pszSafe = pszMessage != NULL ? pszMessage : "(null)";
    BrModernLog("BRENDER WARNING: %s", pszSafe);
    if (vfShadowLogEnabled)
    {
        const char *pszShadow = PszShadowDiagMessage(pszSafe);
        if (pszShadow != NULL)
            BrShadowLog("%s", pszShadow);
    }

    if (vpModernPreviousDiagHandler != NULL && vpModernPreviousDiagHandler->warning != NULL)
        vpModernPreviousDiagHandler->warning(pszSafe);
}

static void BR_CALLBACK ModernDiagFailure(const char *pszMessage)
{
    const char *pszSafe = pszMessage != NULL ? pszMessage : "(null)";
    BrModernLog("BRENDER FAILURE: %s", pszSafe);
    if (vfShadowLogEnabled)
        BrShadowLog("BRENDER FAILURE: %s", pszSafe);
    ModernLogFlush();
    ShadowLogFlush();

    // Preserve BRender's normal fatal semantics after recording the failure.
    // The stock stdio handler calls BrEnd() and exits with code 10.
    if (vpModernPreviousDiagHandler != NULL && vpModernPreviousDiagHandler->failure != NULL)
        vpModernPreviousDiagHandler->failure(pszSafe);

    // A failure callback is not supposed to return. Keep that contract even if
    // a nonstandard previous handler unexpectedly does.
    BrModernLog("BRENDER FAILURE: previous diagnostic handler returned; forcing shutdown");
    ModernLogFlush();
    BrEnd();
    ExitProcess(10);
}

static br_diaghandler vModernDiagHandler = {
    "4DMM Modern BRender flight recorder",
    ModernDiagWarning,
    ModernDiagFailure,
};

static bool FModernDiagPath(const char *pszName, char *pszPath, size_t cchPath)
{
    if (pszName == NULL || pszName[0] == '\0' || pszPath == NULL || cchPath == 0)
        return false;

    char szExe[MAX_PATH];
    DWORD cch = GetModuleFileNameA(NULL, szExe, (DWORD)sizeof(szExe));
    if (cch == 0 || cch >= sizeof(szExe))
        return false;

    char *pchSlash = strrchr(szExe, '\\');
    char *pchSlashAlt = strrchr(szExe, '/');
    if (pchSlashAlt != NULL && (pchSlash == NULL || pchSlashAlt > pchSlash))
        pchSlash = pchSlashAlt;
    if (pchSlash != NULL)
        *pchSlash = '\0';
    else
        strcpy_s(szExe, sizeof(szExe), ".");

    char szDir[MAX_PATH];
    if (sprintf_s(szDir, sizeof(szDir), "%s\\logs", szExe) < 0)
        return false;
    if (!CreateDirectoryA(szDir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return false;
    return sprintf_s(pszPath, cchPath, "%s\\%s", szDir, pszName) >= 0;
}

static bool FModernDumpRgb888Bmp(PBPMP pbpmp, const char *pszName)
{
    if (!vfModernLogEnabled || pbpmp == NULL || pbpmp->pixels == NULL ||
        pbpmp->type != BR_PMT_RGB_888 || pbpmp->width == 0 || pbpmp->height == 0 ||
        pbpmp->row_bytes == 0 || pszName == NULL)
        return false;

    char szPath[MAX_PATH];
    if (!FModernDiagPath(pszName, szPath, sizeof(szPath)))
        return false;

    FILE *pfile = NULL;
    if (fopen_s(&pfile, szPath, "wb") != 0 || pfile == NULL)
        return false;

    const int32_t dxp = pbpmp->width;
    const int32_t dyp = pbpmp->height;
    const uint32_t cbRowFile = (uint32_t)((dxp * 3 + 3) & ~3);
    const uint32_t cbPixels = cbRowFile * (uint32_t)dyp;

    BITMAPFILEHEADER bfh = {};
    BITMAPINFOHEADER bih = {};
    bfh.bfType = 0x4D42;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + cbPixels;
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = dxp;
    bih.biHeight = dyp;
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = cbPixels;

    fwrite(&bfh, sizeof(bfh), 1, pfile);
    fwrite(&bih, sizeof(bih), 1, pfile);

    const uint8_t rgbPad[3] = {0, 0, 0};
    const uint8_t *pbBase = (const uint8_t *)pbpmp->pixels;
    const int32_t cbRowMemory = pbpmp->row_bytes;
    for (int32_t y = dyp - 1; y >= 0; --y)
    {
        const uint8_t *pbRow = pbBase + (ptrdiff_t)y * cbRowMemory;
        fwrite(pbRow, (size_t)dxp * 3, 1, pfile);
        const uint32_t cbPad = cbRowFile - (uint32_t)dxp * 3;
        if (cbPad != 0)
            fwrite(rgbPad, cbPad, 1, pfile);
    }

    fclose(pfile);
    BrModernLog("DIAG BMP wrote %s wh=%ldx%ld row=%ld", szPath, (long)dxp, (long)dyp, (long)cbRowMemory);
    return true;
}

static uint32_t DwModernSparsePixelHash(PBPMP pbpmp, uint32_t *pcNonZero)
{
    if (pcNonZero != NULL)
        *pcNonZero = 0;
    if (pbpmp == NULL || pbpmp->pixels == NULL || pbpmp->row_bytes <= 0 ||
        pbpmp->width == 0 || pbpmp->height == 0)
        return 0;

    const uint8_t *pbBase = (const uint8_t *)pbpmp->pixels;
    const int32_t cbPixel = (pbpmp->type == BR_PMT_RGB_888) ? 3 : 1;
    const int32_t dyStep = (pbpmp->height > 16) ? (pbpmp->height / 16) : 1;
    const int32_t dxStep = (pbpmp->width > 32) ? (pbpmp->width / 32) : 1;
    uint32_t dwHash = 2166136261u;
    uint32_t cNonZero = 0;

    for (int32_t y = 0; y < pbpmp->height; y += dyStep)
    {
        const uint8_t *pbRow = pbBase + y * pbpmp->row_bytes;
        for (int32_t x = 0; x < pbpmp->width; x += dxStep)
        {
            const uint8_t *pb = pbRow + x * cbPixel;
            for (int32_t ib = 0; ib < cbPixel; ++ib)
            {
                const uint8_t b = pb[ib];
                if (b != 0)
                    ++cNonZero;
                dwHash ^= b;
                dwHash *= 16777619u;
            }
        }
    }

    if (pcNonZero != NULL)
        *pcNonZero = cNonZero;
    return dwHash;
}

struct MODERNAPEACTORSTATS
{
    uint32_t cTotal;
    uint32_t cModel;
    uint32_t cDrawableModel;
    uint32_t cLight;
    uint32_t cCamera;
    uint32_t cOther;
};

static void CountModernApeActorTree(PBACT pbact, MODERNAPEACTORSTATS *pstats)
{
    if (pstats == NULL)
        return;
    for (PBACT pact = pbact; pact != NULL; pact = pact->next)
    {
        ++pstats->cTotal;
        if (pact->type == BR_ACTOR_MODEL)
        {
            ++pstats->cModel;
            if (pact->model != NULL && pact->render_style != BR_RSTYLE_NONE)
                ++pstats->cDrawableModel;
        }
        else if (pact->type == BR_ACTOR_LIGHT)
            ++pstats->cLight;
        else if (pact->type == BR_ACTOR_CAMERA)
            ++pstats->cCamera;
        else
            ++pstats->cOther;
        if (pact->children != NULL)
            CountModernApeActorTree(pact->children, pstats);
    }
}

static const uint8_t *PbModernPixelmapRow(const br_pixelmap *pbpmp, int32_t yp)
{
    if (pbpmp == NULL || pbpmp->pixels == NULL || yp < 0 || yp >= pbpmp->height || pbpmp->row_bytes == 0)
        return NULL;
    // Match BRender's DevicePixelmapMemAddress semantics exactly. For an
    // inverted memory pixelmap, BRender has already moved pixels to the last
    // scanline and made row_bytes negative.
    return (const uint8_t *)pbpmp->pixels + (ptrdiff_t)(pbpmp->base_y + yp) * pbpmp->row_bytes +
           (ptrdiff_t)pbpmp->base_x * 3;
}

static uint8_t *PbModernPixelmapRow(br_pixelmap *pbpmp, int32_t yp)
{
    return (uint8_t *)PbModernPixelmapRow((const br_pixelmap *)pbpmp, yp);
}

static bool FModernCopyRgb888Rows(br_pixelmap *pdst, const br_pixelmap *psrc, int32_t dxp, int32_t dyp)
{
    if (pdst == NULL || psrc == NULL || pdst->type != BR_PMT_RGB_888 || psrc->type != BR_PMT_RGB_888 ||
        pdst->pixels == NULL || psrc->pixels == NULL || dxp <= 0 || dyp <= 0 ||
        dxp > pdst->width || dxp > psrc->width || dyp > pdst->height || dyp > psrc->height)
        return false;

    const size_t cbRow = (size_t)dxp * 3;
    for (int32_t yp = 0; yp < dyp; ++yp)
    {
        uint8_t *pbDst = PbModernPixelmapRow(pdst, yp);
        const uint8_t *pbSrc = PbModernPixelmapRow(psrc, yp);
        if (pbDst == NULL || pbSrc == NULL)
            return false;
        memcpy(pbDst, pbSrc, cbRow);
    }
    return true;
}

static bool FModernScaleRgb888Rows(br_pixelmap *pdst, const br_pixelmap *psrc)
{
    if (pdst == NULL || psrc == NULL || pdst->type != BR_PMT_RGB_888 || psrc->type != BR_PMT_RGB_888 ||
        pdst->pixels == NULL || psrc->pixels == NULL || pdst->width == 0 || pdst->height == 0 ||
        psrc->width == 0 || psrc->height == 0)
        return false;

    const int32_t dxpDst = pdst->width;
    const int32_t dypDst = pdst->height;
    const int32_t dxpSrc = psrc->width;
    const int32_t dypSrc = psrc->height;

    // Only the legacy 2D background raster needs resampling. The BRender
    // scene itself is rendered directly into the full-resolution destination.
    // Nearest-source sampling keeps the source image's authored colours exact
    // and avoids adding a second filtering/sharpening policy to old movies.
    // Exact nearest-neighbour mapping without a 64-bit divide for every
    // destination pixel. Native 2x is 665,856 destination pixels, so the old
    // implementation paid hundreds of thousands of integer divisions on
    // every frame just to scale the static 3DMM background. The accumulator
    // produces the same floor(dst * src / dstSize) source coordinates.
    int32_t ypSrc = 0;
    int32_t yAccum = 0;
    for (int32_t ypDst = 0; ypDst < dypDst; ++ypDst)
    {
        uint8_t *pbDst = PbModernPixelmapRow(pdst, ypDst);
        const uint8_t *pbSrc = PbModernPixelmapRow(psrc, ypSrc);
        if (pbDst == NULL || pbSrc == NULL)
            return false;

        int32_t xpSrc = 0;
        int32_t xAccum = 0;
        for (int32_t xpDst = 0; xpDst < dxpDst; ++xpDst)
        {
            const uint8_t *pbSrcPixel = pbSrc + (ptrdiff_t)xpSrc * 3;
            uint8_t *pbDstPixel = pbDst + (ptrdiff_t)xpDst * 3;
            pbDstPixel[0] = pbSrcPixel[0];
            pbDstPixel[1] = pbSrcPixel[1];
            pbDstPixel[2] = pbSrcPixel[2];

            xAccum += dxpSrc;
            while (xAccum >= dxpDst && xpSrc + 1 < dxpSrc)
            {
                xAccum -= dxpDst;
                ++xpSrc;
            }
        }

        yAccum += dypSrc;
        while (yAccum >= dypDst && ypSrc + 1 < dypSrc)
        {
            yAccum -= dypDst;
            ++ypSrc;
        }
    }
    return true;
}

static void ModernFillRgb888Bytes(br_pixelmap *pbpmp, uint8_t b)
{
    if (pbpmp == NULL || pbpmp->pixels == NULL || pbpmp->type != BR_PMT_RGB_888)
        return;
    for (int32_t yp = 0; yp < pbpmp->height; ++yp)
    {
        uint8_t *pbRow = PbModernPixelmapRow(pbpmp, yp);
        if (pbRow != NULL)
            memset(pbRow, b, (size_t)pbpmp->width * 3);
    }
}

static bool FModernRgb888AllBytesEqual(const br_pixelmap *pbpmp, uint8_t b)
{
    if (pbpmp == NULL || pbpmp->pixels == NULL || pbpmp->type != BR_PMT_RGB_888)
        return false;
    const size_t cbRow = (size_t)pbpmp->width * 3;
    for (int32_t yp = 0; yp < pbpmp->height; ++yp)
    {
        const uint8_t *pbRow = PbModernPixelmapRow(pbpmp, yp);
        if (pbRow == NULL)
            return false;
        for (size_t ib = 0; ib < cbRow; ++ib)
        {
            if (pbRow[ib] != b)
                return false;
        }
    }
    return true;
}

static void ModernLogGlErrors(const char *pszStage)
{
    GLenum err = GL_NO_ERROR;
    int cErrors = 0;
    while ((err = glGetError()) != GL_NO_ERROR && cErrors < 16)
    {
        BrModernLog("GL ERROR stage=%s code=0x%04X", pszStage != NULL ? pszStage : "unknown", (unsigned)err);
        ++cErrors;
    }
    if (cErrors == 0)
        BrModernLog("GL stage=%s error=NONE", pszStage != NULL ? pszStage : "unknown");
}

static void ModernLogActorTree(const br_actor *pact, uint32_t depth, uint32_t *pcActors)
{
    if (pact == NULL || pcActors == NULL || *pcActors >= 256)
        return;

    const uint32_t iActor = (*pcActors)++;
    const br_model *pmodel = pact->model;
    BrModernLog("ACTOR PREFLIGHT idx=%lu depth=%lu actor=%p type=%u parent=%p children=%p next=%p model=%p material=%p identifier=%p model_nv=%lu model_nf=%lu model_flags=0x%04X prepared=%p stored=%p",
                (unsigned long)iActor, (unsigned long)depth, pact, (unsigned)pact->type,
                pact->parent, pact->children, pact->next, pmodel, pact->material, pact->identifier,
                pmodel != NULL ? (unsigned long)pmodel->nvertices : 0UL,
                pmodel != NULL ? (unsigned long)pmodel->nfaces : 0UL,
                pmodel != NULL ? (unsigned)pmodel->flags : 0U,
                pmodel != NULL ? pmodel->prepared : NULL,
                pmodel != NULL ? pmodel->stored : NULL);

    for (const br_actor *pchild = pact->children;
         pchild != NULL && *pcActors < 256;
         pchild = pchild->next)
        ModernLogActorTree(pchild, depth + 1, pcActors);
}

#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#endif
#ifndef GL_MAX_VERTEX_ATTRIBS
#define GL_MAX_VERTEX_ATTRIBS 0x8869
#endif
#ifndef GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#endif

bool FModernLogPath(char *pszPath, size_t cchPath)
{
    return FModernDiagPath("modern_br.log", pszPath, cchPath);
}

void ModernLogV(const char *pszFormat, va_list ap)
{
    if (!vfModernLogEnabled || pszFormat == NULL)
        return;

    char szMessage[4096];
    if (vsnprintf_s(szMessage, sizeof(szMessage), _TRUNCATE, pszFormat, ap) < 0)
        szMessage[sizeof(szMessage) - 1] = '\0';

    SYSTEMTIME st;
    GetLocalTime(&st);
    const LONG seq = InterlockedIncrement(&vlModernLogSequence) - 1;

    AcquireSRWLockExclusive(&vsrwModernLog);
    if (vpModernLogFile == NULL)
    {
        char szPath[MAX_PATH];
        if (FModernLogPath(szPath, sizeof(szPath)) &&
            fopen_s(&vpModernLogFile, szPath, "a") == 0 && vpModernLogFile != NULL)
        {
            // Let the CRT amortize small diagnostic writes into large blocks.
            setvbuf(vpModernLogFile, NULL, _IOFBF, 64 * 1024);
            vcModernLogLinesSinceFlush = 0;
        }
    }

    if (vpModernLogFile != NULL)
    {
        fprintf(vpModernLogFile, "%08ld %02u:%02u:%02u.%03u T%lu %s\n", (long)seq,
                (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
                (unsigned)st.wMilliseconds, (unsigned long)GetCurrentThreadId(), szMessage);

        ++vcModernLogLinesSinceFlush;
        if (vcModernLogLinesSinceFlush >= kcModernLogLinesPerFlush)
        {
            fflush(vpModernLogFile);
            vcModernLogLinesSinceFlush = 0;
        }
    }
    ReleaseSRWLockExclusive(&vsrwModernLog);
}

static bool FShadowLogPath(char *pszPath, size_t cchPath)
{
    return FModernDiagPath("shadow.log", pszPath, cchPath);
}

static void ShadowLogV(const char *pszFormat, va_list ap)
{
    if (!vfShadowLogEnabled || pszFormat == NULL)
        return;

    char szMessage[4096];
    if (vsnprintf_s(szMessage, sizeof(szMessage), _TRUNCATE, pszFormat, ap) < 0)
        szMessage[sizeof(szMessage) - 1] = '\0';

    SYSTEMTIME st;
    GetLocalTime(&st);
    const LONG seq = InterlockedIncrement(&vlShadowLogSequence) - 1;

    AcquireSRWLockExclusive(&vsrwShadowLog);
    if (vpShadowLogFile != NULL)
    {
        fprintf(vpShadowLogFile, "%08ld %02u:%02u:%02u.%03u T%lu %s\n", (long)seq,
                (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
                (unsigned)st.wMilliseconds, (unsigned long)GetCurrentThreadId(), szMessage);
        fflush(vpShadowLogFile);
    }
    ReleaseSRWLockExclusive(&vsrwShadowLog);
}

static void ShadowLogFlush(void)
{
    AcquireSRWLockExclusive(&vsrwShadowLog);
    if (vpShadowLogFile != NULL)
        fflush(vpShadowLogFile);
    ReleaseSRWLockExclusive(&vsrwShadowLog);
}

static void ModernLogFlush(void)
{
    AcquireSRWLockExclusive(&vsrwModernLog);
    if (vpModernLogFile != NULL)
    {
        fflush(vpModernLogFile);
        vcModernLogLinesSinceFlush = 0;
    }
    ReleaseSRWLockExclusive(&vsrwModernLog);
}

static void ModernLogClose(void)
{
    AcquireSRWLockExclusive(&vsrwModernLog);
    if (vpModernLogFile != NULL)
    {
        fflush(vpModernLogFile);
        fclose(vpModernLogFile);
        vpModernLogFile = NULL;
        vcModernLogLinesSinceFlush = 0;
    }
    ReleaseSRWLockExclusive(&vsrwModernLog);
}

void ModernLogExceptionInternal(EXCEPTION_POINTERS *pep, const char *pszWhere)
{
    if ((!vfModernLogEnabled && !vfShadowLogEnabled) || pep == NULL || pep->ExceptionRecord == NULL)
        return;
    if (InterlockedExchange(&vlModernExceptionLogging, 1) != 0)
        return;

    EXCEPTION_RECORD *per = pep->ExceptionRecord;
    MEMORY_BASIC_INFORMATION mbi = {};
    HMODULE hmodFault = NULL;
    char szModule[MAX_PATH] = {};
    uintptr_t rva = 0;
    if (VirtualQuery(per->ExceptionAddress, &mbi, sizeof(mbi)) != 0)
    {
        hmodFault = (HMODULE)mbi.AllocationBase;
        if (hmodFault != NULL)
        {
            GetModuleFileNameA(hmodFault, szModule, sizeof(szModule));
            rva = (uintptr_t)per->ExceptionAddress - (uintptr_t)hmodFault;
        }
    }

    BrModernLog("EXCEPTION where=%s code=0x%08lX address=%p module=%s base=%p rva=0x%llX flags=0x%08lX params=%lu",
                pszWhere != NULL ? pszWhere : "unknown",
                (unsigned long)per->ExceptionCode, per->ExceptionAddress,
                szModule[0] != '\0' ? szModule : "unknown", hmodFault,
                (unsigned long long)rva, (unsigned long)per->ExceptionFlags,
                (unsigned long)per->NumberParameters);
    if (vfShadowLogEnabled)
        BrShadowLog("SHADOW EXCEPTION where=%s code=0x%08lX address=%p module=%s base=%p rva=0x%llX flags=0x%08lX params=%lu",
                    pszWhere != NULL ? pszWhere : "unknown",
                    (unsigned long)per->ExceptionCode, per->ExceptionAddress,
                    szModule[0] != '\0' ? szModule : "unknown", hmodFault,
                    (unsigned long long)rva, (unsigned long)per->ExceptionFlags,
                    (unsigned long)per->NumberParameters);
    for (DWORD i = 0; i < per->NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; ++i)
    {
        BrModernLog("EXCEPTION parameter[%lu]=0x%llX", (unsigned long)i,
                    (unsigned long long)per->ExceptionInformation[i]);
        if (vfShadowLogEnabled)
            BrShadowLog("SHADOW EXCEPTION parameter[%lu]=0x%llX", (unsigned long)i,
                        (unsigned long long)per->ExceptionInformation[i]);
    }

    if (pep->ContextRecord != NULL)
    {
#if defined(_M_IX86)
        CONTEXT *pcx = pep->ContextRecord;
        BrModernLog("EXCEPTION regs EIP=%08lX ESP=%08lX EBP=%08lX EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX ESI=%08lX EDI=%08lX",
                    (unsigned long)pcx->Eip, (unsigned long)pcx->Esp, (unsigned long)pcx->Ebp,
                    (unsigned long)pcx->Eax, (unsigned long)pcx->Ebx, (unsigned long)pcx->Ecx,
                    (unsigned long)pcx->Edx, (unsigned long)pcx->Esi, (unsigned long)pcx->Edi);
        if (vfShadowLogEnabled)
            BrShadowLog("SHADOW EXCEPTION regs EIP=%08lX ESP=%08lX EBP=%08lX EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX ESI=%08lX EDI=%08lX",
                        (unsigned long)pcx->Eip, (unsigned long)pcx->Esp, (unsigned long)pcx->Ebp,
                        (unsigned long)pcx->Eax, (unsigned long)pcx->Ebx, (unsigned long)pcx->Ecx,
                        (unsigned long)pcx->Edx, (unsigned long)pcx->Esi, (unsigned long)pcx->Edi);
#elif defined(_M_X64)
        CONTEXT *pcx = pep->ContextRecord;
        BrModernLog("EXCEPTION regs RIP=%016llX RSP=%016llX RBP=%016llX",
                    (unsigned long long)pcx->Rip, (unsigned long long)pcx->Rsp,
                    (unsigned long long)pcx->Rbp);
        if (vfShadowLogEnabled)
            BrShadowLog("SHADOW EXCEPTION regs RIP=%016llX RSP=%016llX RBP=%016llX",
                        (unsigned long long)pcx->Rip, (unsigned long long)pcx->Rsp,
                        (unsigned long long)pcx->Rbp);
#endif
    }

    ModernLogFlush();
    ShadowLogFlush();
    InterlockedExchange(&vlModernExceptionLogging, 0);
}

LONG CALLBACK ModernVectoredExceptionHandler(EXCEPTION_POINTERS *pep)
{
    if (pep != NULL && pep->ExceptionRecord != NULL)
    {
        const DWORD code = pep->ExceptionRecord->ExceptionCode;
        if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
            code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_FLT_DIVIDE_BY_ZERO)
        {
            ModernLogExceptionInternal(pep, "vectored-first-chance");
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

#ifndef WGL_CONTEXT_MAJOR_VERSION_ARB
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
#endif

typedef HGLRC(WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int *);

struct MODERNGL
{
    HWND hwnd;
    HDC hdc;
    HGLRC hglrc;
    br_pixelmap *pscreen;
    br_pixelmap *panchor;
    br_pixelmap *pcolour;
    br_pixelmap *pdepth;
    br_pixelmap *pcpuStage;
    br_pixelmap *pdisplayExternal;
    // Dedicated CPU-visible copy of the last completed canonical native movie
    // frame. Keep this separate from pcpuStage: preview/offscreen renders reuse
    // pcpuStage and must never overwrite the frame owned by the 4x compositor.
    br_pixelmap *pcompositeCpu;
    // Native-resolution cache for the currently visible APE-backed Kauai
    // editor preview (3D Word / Costume Changer / Action). This cache is
    // independent from the canonical movie cache so offscreen/editor renders
    // can never replace the main movie frame.
    br_pixelmap *ppreviewCpu;
    // StretchDIBits requires DWORD-aligned scanlines. BRender RGB888 memory
    // pixelmaps are aligned to groups of four PIXELS (12 bytes), which only
    // happens to equal the DIB stride for some widths. Keep a retained DIB
    // staging buffer for widths such as Action's 782px native preview where
    // the two stride rules differ (BRender 2352 bytes vs DIB 2348 bytes).
    uint8_t *pbPreviewDib;
    size_t cbPreviewDib;
    int32_t cbPreviewDibRow;
    void *pvPreviewOwner;
    RECT rcPreviewSource;
    RECT rcPreviewOwnerSource;
    int32_t dxpBuffer;
    int32_t dypBuffer;
    bool fRendererStarted;
    bool fHaveFrame;
    bool fHaveExternalFrame;
    bool fHaveCompositeCpuFrame;
    bool fHavePreviewCpuFrame;
    uint32_t iPreviewFrameSerial;
    uint32_t iPreviewPaintLoggedSerial;
    uint8_t rgbPrimitiveHeap[kcbPrimitiveHeap];
};

MODERNGL vgl = {};

bool FBadWglProc(PROC pfn)
{
    return pfn == NULL || pfn == (PROC)1 || pfn == (PROC)2 || pfn == (PROC)3 || pfn == (PROC)-1;
}

PROC PfnWgl(const char *psz)
{
    PROC pfn = wglGetProcAddress(psz);
    if (!FBadWglProc(pfn))
        return pfn;

    static HMODULE vhmodOpenGL = LoadLibraryA("opengl32.dll");
    PROC pfnFallback = vhmodOpenGL == NULL ? NULL : GetProcAddress(vhmodOpenGL, psz);
    if (pfnFallback == NULL)
        BrModernLog("GL GetProcAddress FAILED name=%s gle=%lu", psz != NULL ? psz : "(null)",
                    (unsigned long)GetLastError());
    return pfnFallback;
}

void QueryCurrentGLVersion(int32_t *pmajor, int32_t *pminor)
{
    *pmajor = 0;
    *pminor = 0;

    const char *pszVersion = (const char *)glGetString(GL_VERSION);
    if (pszVersion != NULL)
        sscanf(pszVersion, "%d.%d", pmajor, pminor);
}

br_error BR_CALLBACK CreateContext(br_pixelmap *, br_device_gl_context_info *pinfo, void *)
{
    BrModernLog("GL CreateContext enter hwnd=%p valid=%d pinfo=%p", vgl.hwnd,
                vgl.hwnd != NULL ? (int)IsWindow(vgl.hwnd) : 0, pinfo);
    if (vgl.hwnd == NULL || !IsWindow(vgl.hwnd))
    {
        BrModernLog("GL CreateContext FAIL invalid viewport HWND");
        return BRE_FAIL;
    }

    vgl.hdc = GetDC(vgl.hwnd);
    BrModernLog("GL GetDC hwnd=%p -> hdc=%p gle=%lu", vgl.hwnd, vgl.hdc,
                (unsigned long)GetLastError());
    if (vgl.hdc == NULL)
        return BRE_FAIL;

    if (GetPixelFormat(vgl.hdc) == 0)
    {
        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cAlphaBits = 8;
        pfd.cDepthBits = 24;
        pfd.cStencilBits = 8;
        pfd.iLayerType = PFD_MAIN_PLANE;

        const int ipf = ChoosePixelFormat(vgl.hdc, &pfd);
        BrModernLog("GL ChoosePixelFormat -> %d gle=%lu", ipf, (unsigned long)GetLastError());
        if (ipf == 0 || !SetPixelFormat(vgl.hdc, ipf, &pfd))
        {
            BrModernLog("GL SetPixelFormat FAIL ipf=%d gle=%lu", ipf, (unsigned long)GetLastError());
            ReleaseDC(vgl.hwnd, vgl.hdc);
            vgl.hdc = NULL;
            return BRE_FAIL;
        }
        BrModernLog("GL SetPixelFormat OK ipf=%d", ipf);
    }
    else
    {
        BrModernLog("GL existing pixel format=%d", GetPixelFormat(vgl.hdc));
    }

    HGLRC hglrcBootstrap = wglCreateContext(vgl.hdc);
    BrModernLog("GL wglCreateContext bootstrap=%p gle=%lu", hglrcBootstrap,
                (unsigned long)GetLastError());
    if (hglrcBootstrap == NULL || !wglMakeCurrent(vgl.hdc, hglrcBootstrap))
    {
        BrModernLog("GL bootstrap MakeCurrent FAIL context=%p gle=%lu", hglrcBootstrap,
                    (unsigned long)GetLastError());
        if (hglrcBootstrap != NULL)
            wglDeleteContext(hglrcBootstrap);
        ReleaseDC(vgl.hwnd, vgl.hdc);
        vgl.hdc = NULL;
        return BRE_FAIL;
    }

    BrModernLog("GL bootstrap MakeCurrent OK context=%p", hglrcBootstrap);
    PFNWGLCREATECONTEXTATTRIBSARBPROC pfnCreateContextAttribs =
        (PFNWGLCREATECONTEXTATTRIBSARBPROC)PfnWgl("wglCreateContextAttribsARB");
    BrModernLog("GL wglCreateContextAttribsARB=%p", pfnCreateContextAttribs);

    HGLRC hglrc = NULL;
    br_token tokProfile = BRT_OPENGL_PROFILE_COMPATIBILITY;

    if (pfnCreateContextAttribs != NULL)
    {
        // The self-contained 4DMM shader fallback embeds BRender's native
        // GLSL 4.30 sources, so request 4.3 first.  This also works when the
        // normal SPIR-V -> GLSL 3.30 build-time path is available.
        const int rgAttribCore43[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0};
        hglrc = pfnCreateContextAttribs(vgl.hdc, 0, rgAttribCore43);
        BrModernLog("GL request 4.3 core -> %p gle=%lu", hglrc, (unsigned long)GetLastError());
        tokProfile = BRT_OPENGL_PROFILE_CORE;

        if (hglrc == NULL)
        {
            const int rgAttribCompat43[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
                WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
                0};
            hglrc = pfnCreateContextAttribs(vgl.hdc, 0, rgAttribCompat43);
            BrModernLog("GL request 4.3 compatibility -> %p gle=%lu", hglrc, (unsigned long)GetLastError());
            tokProfile = BRT_OPENGL_PROFILE_COMPATIBILITY;
        }

        // Keep a final 3.3 fallback for installations where glslang and
        // spirv-cross generated the ordinary BRender 1.4 GLSL 3.30 shaders.
        if (hglrc == NULL)
        {
            const int rgAttribCore33[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
                WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                0};
            hglrc = pfnCreateContextAttribs(vgl.hdc, 0, rgAttribCore33);
            BrModernLog("GL request 3.3 core -> %p gle=%lu", hglrc, (unsigned long)GetLastError());
            tokProfile = BRT_OPENGL_PROFILE_CORE;
        }
    }

    if (hglrc != NULL)
    {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(hglrcBootstrap);
        if (!wglMakeCurrent(vgl.hdc, hglrc))
        {
            BrModernLog("GL final MakeCurrent FAIL context=%p gle=%lu", hglrc,
                        (unsigned long)GetLastError());
            wglDeleteContext(hglrc);
            ReleaseDC(vgl.hwnd, vgl.hdc);
            vgl.hdc = NULL;
            return BRE_FAIL;
        }
    }
    else
    {
        hglrc = hglrcBootstrap;
        tokProfile = BRT_OPENGL_PROFILE_COMPATIBILITY;
    }

    vgl.hglrc = hglrc;
    BrModernLog("GL final context current=%p requested_profile_token=%lu", vgl.hglrc,
                (unsigned long)tokProfile);

    int32_t major = 0;
    int32_t minor = 0;
    QueryCurrentGLVersion(&major, &minor);

    pinfo->native = hglrc;
    pinfo->major = major;
    pinfo->minor = minor;
    pinfo->profile = tokProfile;

    const char *pszVendor = (const char *)glGetString(GL_VENDOR);
    const char *pszRenderer = (const char *)glGetString(GL_RENDERER);
    const char *pszVersion = (const char *)glGetString(GL_VERSION);
    const char *pszGlsl = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    GLint maxTexture = 0;
    GLint maxAttribs = 0;
    GLint maxTexUnits = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxAttribs);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxTexUnits);
    BrModernLog("GL identity vendor=%s renderer=%s version=%s glsl=%s",
                pszVendor != NULL ? pszVendor : "(null)",
                pszRenderer != NULL ? pszRenderer : "(null)",
                pszVersion != NULL ? pszVersion : "(null)",
                pszGlsl != NULL ? pszGlsl : "(null)");
    BrModernLog("GL limits max_texture=%ld max_vertex_attribs=%ld max_combined_tex_units=%ld",
                (long)maxTexture, (long)maxAttribs, (long)maxTexUnits);
    DiagLogBRender("BRender14 GL context created hwnd=%p hdc=%p hglrc=%p version=%ld.%ld profile=%lu",
                   vgl.hwnd, vgl.hdc, vgl.hglrc, (long)major, (long)minor, (unsigned long)tokProfile);
    BrModernLog("GL CreateContext SUCCESS version=%ld.%ld profile_token=%lu",
                (long)major, (long)minor, (unsigned long)tokProfile);
    return BRE_OK;
}

void BR_CALLBACK DeleteContext(br_pixelmap *, void *pvContext, void *)
{
    HGLRC hglrc = (HGLRC)pvContext;
    BrModernLog("GL DeleteContext enter context=%p current=%p", hglrc, wglGetCurrentContext());
    if (wglGetCurrentContext() == hglrc)
        wglMakeCurrent(NULL, NULL);
    if (hglrc != NULL)
        wglDeleteContext(hglrc);
    if (vgl.hglrc == hglrc)
        vgl.hglrc = NULL;
}

br_error BR_CALLBACK MakeCurrent(br_pixelmap *, void *pvContext, void *)
{
    const BOOL fOk = (vgl.hdc != NULL && wglMakeCurrent(vgl.hdc, (HGLRC)pvContext));
    if (!fOk)
        BrModernLog("GL MakeCurrent callback FAIL hdc=%p context=%p gle=%lu", vgl.hdc, pvContext,
                    (unsigned long)GetLastError());
    return fOk ? BRE_OK : BRE_FAIL;
}

void (*BR_CALLBACK GetProcAddressGL(const char *psz, void *))(void)
{
    return (void (*)(void))PfnWgl(psz);
}

br_error BR_CALLBACK Resize(br_pixelmap *, br_int_32, br_int_32, void *)
{
    // The command-line render resolution is fixed for the lifetime of the
    // renderer. User window resizing never changes the actual movie render
    // resolution.
    return BRE_OK;
}

void BR_CALLBACK SwapBuffersGL(br_pixelmap *, void *)
{
    if (vgl.hdc != NULL)
    {
        const BOOL fOk = SwapBuffers(vgl.hdc);
        if (!fOk)
            BrModernLog("GL SwapBuffers FAIL hdc=%p gle=%lu", vgl.hdc, (unsigned long)GetLastError());
    }
}

void BR_CALLBACK FreeContextUser(br_pixelmap *, void *)
{
    if (vgl.hdc != NULL && vgl.hwnd != NULL)
        ReleaseDC(vgl.hwnd, vgl.hdc);
    vgl.hdc = NULL;
}

br_device_gl_ext_procs vrgGLProcs = {
    CreateContext,
    DeleteContext,
    MakeCurrent,
    GetProcAddressGL,
    Resize,
    SwapBuffersGL,
    NULL,
    FreeContextUser,
    NULL,
    NULL};

void FreeRenderBuffers(void)
{
    BrModernLog("GL FreeRenderBuffers colour=%p depth=%p cpu_stage=%p size=%ldx%ld", vgl.pcolour, vgl.pdepth,
                vgl.pcpuStage, (long)vgl.dxpBuffer, (long)vgl.dypBuffer);
    if (vgl.pcpuStage != NULL)
        BrPixelmapFree(vgl.pcpuStage);
    if (vgl.pdepth != NULL)
        BrPixelmapFree(vgl.pdepth);
    if (vgl.pcolour != NULL)
        BrPixelmapFree(vgl.pcolour);
    vgl.pcpuStage = NULL;
    vgl.pdepth = NULL;
    vgl.pcolour = NULL;
    vgl.dxpBuffer = 0;
    vgl.dypBuffer = 0;
    vgl.fHaveFrame = false;
}

bool FEnsureExternalDisplayBuffer(void)
{
    if (!vfModernExternalOnly)
        return true;
    if (vgl.pscreen == NULL)
        return false;

    // Only -resolution 4x has the scaled presentation property. Native 1080p
    // keeps its existing direct GL/DWM path and pays no compositor-readback
    // cost from this experiment.
    const bool fNeedCompositeCpu = vwig.hwndApp != NULL &&
                                   GetPropA(vwig.hwndApp, "4DMMUiScaleWindow") != NULL;
    const bool fDisplayReady = vgl.pdisplayExternal != NULL &&
                               vgl.pdisplayExternal->width == vdxpModernFront &&
                               vgl.pdisplayExternal->height == vdypModernFront;
    const bool fCompositeReady = !fNeedCompositeCpu ||
                                 (vgl.pcompositeCpu != NULL &&
                                  vgl.pcompositeCpu->pixels != NULL &&
                                  vgl.pcompositeCpu->type == BR_PMT_RGB_888 &&
                                  vgl.pcompositeCpu->width == vdxpModernFront &&
                                  vgl.pcompositeCpu->height == vdypModernFront);
    if (fDisplayReady && fCompositeReady)
        return true;

    if (vgl.pdisplayExternal != NULL)
        BrPixelmapFree(vgl.pdisplayExternal);
    vgl.pdisplayExternal = NULL;
    if (vgl.pcompositeCpu != NULL)
        BrPixelmapFree(vgl.pcompositeCpu);
    vgl.pcompositeCpu = NULL;
    vgl.fHaveExternalFrame = false;
    vgl.fHaveCompositeCpuFrame = false;

    vgl.pdisplayExternal = BrPixelmapMatchTypedSized(vgl.pscreen, BR_PMMATCH_OFFSCREEN,
                                                     BR_PMT_RGB_888,
                                                     vdxpModernFront, vdypModernFront);
    if (vgl.pdisplayExternal == NULL)
    {
        BrModernLog("GL external display cache allocation FAILED requested=%ldx%ld",
                    (long)vdxpModernFront, (long)vdypModernFront);
        return false;
    }

    vgl.pdisplayExternal->origin_x = (br_int_16)(vdxpModernFront / 2);
    vgl.pdisplayExternal->origin_y = (br_int_16)(vdypModernFront / 2);

    if (fNeedCompositeCpu)
    {
        vgl.pcompositeCpu = BrPixelmapAllocate(BR_PMT_RGB_888,
                                               vdxpModernFront, vdypModernFront,
                                               NULL, BR_PMAF_NORMAL);
        if (vgl.pcompositeCpu == NULL || vgl.pcompositeCpu->pixels == NULL)
        {
            BrModernLog("GL persistent composite CPU allocation FAILED requested=%ldx%ld result=%p pixels=%p",
                        (long)vdxpModernFront, (long)vdypModernFront,
                        vgl.pcompositeCpu,
                        vgl.pcompositeCpu != NULL ? vgl.pcompositeCpu->pixels : NULL);
            if (vgl.pcompositeCpu != NULL)
                BrPixelmapFree(vgl.pcompositeCpu);
            vgl.pcompositeCpu = NULL;
            BrPixelmapFree(vgl.pdisplayExternal);
            vgl.pdisplayExternal = NULL;
            return false;
        }
    }

    BrModernLog("GL external caches ready display=%p composite_cpu=%p composite_pixels=%p wh=%ldx%ld row=%ld",
                vgl.pdisplayExternal, vgl.pcompositeCpu,
                vgl.pcompositeCpu != NULL ? vgl.pcompositeCpu->pixels : NULL,
                (long)vdxpModernFront, (long)vdypModernFront,
                vgl.pcompositeCpu != NULL ? (long)vgl.pcompositeCpu->row_bytes : 0L);
    return true;
}

static bool FModernScaledPresentation(HWND *phwndPresentation, int32_t *pnum, int32_t *pden)
{
    if (vwig.hwndApp == NULL || !IsWindow(vwig.hwndApp))
        return false;
    HWND hwndPresentation = (HWND)GetPropA(vwig.hwndApp, "4DMMUiScaleWindow");
    const int32_t num = (int32_t)(INT_PTR)GetPropA(vwig.hwndApp, "4DMMUiScaleNumerator");
    const int32_t den = (int32_t)(INT_PTR)GetPropA(vwig.hwndApp, "4DMMUiScaleDenominator");
    if (hwndPresentation == NULL || !IsWindow(hwndPresentation) || den <= 0 || num <= den)
        return false;
    if (phwndPresentation != NULL)
        *phwndPresentation = hwndPresentation;
    if (pnum != NULL)
        *pnum = num;
    if (pden != NULL)
        *pden = den;
    return true;
}

static bool FEnsurePreviewCpuBuffer(int32_t dxp, int32_t dyp)
{
    if (vgl.ppreviewCpu != NULL && vgl.ppreviewCpu->pixels != NULL &&
        vgl.ppreviewCpu->width == dxp && vgl.ppreviewCpu->height == dyp)
        return true;
    if (vgl.ppreviewCpu != NULL)
        BrPixelmapFree(vgl.ppreviewCpu);
    vgl.ppreviewCpu = BrPixelmapAllocate(BR_PMT_RGB_888, dxp, dyp, NULL, BR_PMAF_NORMAL);
    vgl.fHavePreviewCpuFrame = false;
    if (vgl.ppreviewCpu == NULL || vgl.ppreviewCpu->pixels == NULL)
    {
        if (vgl.ppreviewCpu != NULL)
            BrPixelmapFree(vgl.ppreviewCpu);
        vgl.ppreviewCpu = NULL;
        BrModernLog("APE native preview cache allocation FAILED wh=%ldx%ld", (long)dxp, (long)dyp);
        return false;
    }
    BrModernLog("APE native preview cache allocated wh=%ldx%ld row=%ld",
                (long)dxp, (long)dyp, (long)vgl.ppreviewCpu->row_bytes);
    return true;
}

static bool FEnsurePreviewDibBuffer(int32_t cbRow, int32_t dyp)
{
    if (cbRow <= 0 || dyp <= 0)
        return false;

    const size_t cbNeed = (size_t)cbRow * (size_t)dyp;
    if (cbNeed / (size_t)dyp != (size_t)cbRow)
        return false;

    if (vgl.pbPreviewDib != NULL && vgl.cbPreviewDib >= cbNeed)
    {
        vgl.cbPreviewDibRow = cbRow;
        return true;
    }

    HANDLE hheap = GetProcessHeap();
    if (hheap == NULL)
        return false;

    void *pvNew = vgl.pbPreviewDib != NULL
                    ? HeapReAlloc(hheap, 0, vgl.pbPreviewDib, cbNeed)
                    : HeapAlloc(hheap, 0, cbNeed);
    if (pvNew == NULL)
        return false;

    vgl.pbPreviewDib = (uint8_t *)pvNew;
    vgl.cbPreviewDib = cbNeed;
    vgl.cbPreviewDibRow = cbRow;
    return true;
}

static bool FPaintPreviewCpu(HDC hdc, int32_t xpDst, int32_t ypDst,
                             int32_t dxpDst, int32_t dypDst)
{
    PSTDIO pstdio = vapp.Pstdio();
    PMVIE pmvie = pstdio != pvNil ? pstdio->Pmvie() : pvNil;

    if (hdc == NULL || !vgl.fHavePreviewCpuFrame || vgl.ppreviewCpu == NULL ||
        vgl.ppreviewCpu->pixels == NULL || vgl.ppreviewCpu->type != BR_PMT_RGB_888 ||
        dxpDst <= 0 || dypDst <= 0)
    {
        MVIE::MultiLog(pmvie,
            "ape_present_reject serial=%lu hdc=%p have=%d cache=%p pixels=%p type=%ld dest=%ldx%ld",
            (unsigned long)vgl.iPreviewFrameSerial, hdc, (int)vgl.fHavePreviewCpuFrame,
            vgl.ppreviewCpu, vgl.ppreviewCpu != NULL ? vgl.ppreviewCpu->pixels : NULL,
            vgl.ppreviewCpu != NULL ? (long)vgl.ppreviewCpu->type : -1L,
            (long)dxpDst, (long)dypDst);
        return false;
    }

    const int32_t dxpSrc = vgl.ppreviewCpu->width;
    const int32_t dypSrc = vgl.ppreviewCpu->height;
    const int32_t cbPixelRow = dxpSrc * 3;
    const int32_t cbDibRow = (cbPixelRow + 3) & ~3;
    const int32_t cbBRenderRow = vgl.ppreviewCpu->row_bytes;
    const uint8_t *pbDib = NULL;
    const char *pszPath = "native";

    // BRender 1.4 RGB888 memory pixelmaps use a four-PIXEL alignment rule,
    // while Win32 24-bit DIBs use four-BYTE scanline alignment. They match for
    // many viewport widths (which is why 3D Word and Costume already worked),
    // but not Action's current native width. Repack only when necessary.
    if (cbBRenderRow == cbDibRow && cbBRenderRow > 0)
    {
        pbDib = (const uint8_t *)vgl.ppreviewCpu->pixels;
    }
    else
    {
        if (!FEnsurePreviewDibBuffer(cbDibRow, dypSrc))
        {
            MVIE::MultiLog(pmvie,
                "ape_present_repack_fail serial=%lu source=%ldx%ld br_row=%ld dib_row=%ld bytes=%lu",
                (unsigned long)vgl.iPreviewFrameSerial, (long)dxpSrc, (long)dypSrc,
                (long)cbBRenderRow, (long)cbDibRow,
                (unsigned long)((size_t)cbDibRow * (size_t)dypSrc));
            return false;
        }

        for (int32_t yp = 0; yp < dypSrc; ++yp)
        {
            const uint8_t *pbSrc = PbModernPixelmapRow(vgl.ppreviewCpu, yp);
            uint8_t *pbDst = vgl.pbPreviewDib + (size_t)yp * (size_t)cbDibRow;
            if (pbSrc == NULL)
            {
                MVIE::MultiLog(pmvie,
                    "ape_present_repack_fail serial=%lu row=%ld source_row_null br_row=%ld dib_row=%ld",
                    (unsigned long)vgl.iPreviewFrameSerial, (long)yp,
                    (long)cbBRenderRow, (long)cbDibRow);
                return false;
            }
            memcpy(pbDst, pbSrc, (size_t)cbPixelRow);
            if (cbDibRow > cbPixelRow)
                memset(pbDst + cbPixelRow, 0, (size_t)(cbDibRow - cbPixelRow));
        }
        pbDib = vgl.pbPreviewDib;
        pszPath = "repack";
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = dxpSrc;
    bmi.bmiHeader.biHeight = -dypSrc;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;
    const int iOldMode = SetStretchBltMode(hdc, COLORONCOLOR);
    SetLastError(ERROR_SUCCESS);
    const int cScan = StretchDIBits(hdc, xpDst, ypDst, dxpDst, dypDst,
                                    0, 0, dxpSrc, dypSrc,
                                    pbDib, &bmi, DIB_RGB_COLORS, SRCCOPY);
    const DWORD dwGle = GetLastError();
    if (iOldMode != 0)
        SetStretchBltMode(hdc, iOldMode);
    const bool fPainted = cScan != GDI_ERROR;
    if (!fPainted)
    {
        MVIE::MultiLog(pmvie,
            "ape_present_gdi_fail serial=%lu path=%s source=%ldx%ld br_row=%ld dib_row=%ld dest=(%ld,%ld) %ldx%ld gle=%lu",
            (unsigned long)vgl.iPreviewFrameSerial, pszPath,
            (long)dxpSrc, (long)dypSrc, (long)cbBRenderRow, (long)cbDibRow,
            (long)xpDst, (long)ypDst, (long)dxpDst, (long)dypDst,
            (unsigned long)dwGle);
        return false;
    }

    if (vgl.iPreviewPaintLoggedSerial != vgl.iPreviewFrameSerial)
    {
        uint32_t cNonZero = 0;
        const uint32_t dwHash = DwModernSparsePixelHash(vgl.ppreviewCpu, &cNonZero);
        MVIE::MultiLog(pmvie,
            "ape_present_frame serial=%lu path=%s cache=%ldx%ld br_row=%ld dib_row=%ld dest=(%ld,%ld) %ldx%ld scans=%ld hash=0x%08lX nonzero_samples=%lu",
            (unsigned long)vgl.iPreviewFrameSerial, pszPath,
            (long)dxpSrc, (long)dypSrc, (long)cbBRenderRow, (long)cbDibRow,
            (long)xpDst, (long)ypDst, (long)dxpDst, (long)dypDst,
            (long)cScan, (unsigned long)dwHash, (unsigned long)cNonZero);
        vgl.iPreviewPaintLoggedSerial = vgl.iPreviewFrameSerial;
    }
    return true;
}

static void InvalidatePreviewPresentationRect(const RECT *prcSource)
{
    if (prcSource == NULL || prcSource->right <= prcSource->left ||
        prcSource->bottom <= prcSource->top)
        return;
    HWND hwndPresentation = NULL;
    int32_t num = 0;
    int32_t den = 0;
    if (!FModernScaledPresentation(&hwndPresentation, &num, &den) ||
        hwndPresentation == NULL || !IsWindow(hwndPresentation))
        return;
    RECT rcDst;
    rcDst.left = MulDiv(prcSource->left, num, den);
    rcDst.top = MulDiv(prcSource->top, num, den);
    rcDst.right = MulDiv(prcSource->right, num, den);
    rcDst.bottom = MulDiv(prcSource->bottom, num, den);
    InvalidateRect(hwndPresentation, &rcDst, FALSE);
}

static void PresentPreviewPresentationRectNow(const RECT *prcSource)
{
    if (prcSource == NULL || prcSource->right <= prcSource->left ||
        prcSource->bottom <= prcSource->top)
        return;

    HWND hwndPresentation = NULL;
    int32_t num = 0;
    int32_t den = 0;
    if (!FModernScaledPresentation(&hwndPresentation, &num, &den) ||
        hwndPresentation == NULL || !IsWindow(hwndPresentation))
        return;

    RECT rcDst;
    rcDst.left = MulDiv(prcSource->left, num, den);
    rcDst.top = MulDiv(prcSource->top, num, den);
    rcDst.right = MulDiv(prcSource->right, num, den);
    rcDst.bottom = MulDiv(prcSource->bottom, num, den);

    // A completed native APE readback is the presentation commit point.  Do
    // not depend on WM_PAINT to perform that commit: Action renders from a
    // nested Kauai browser loop where v160 proved RedrawWindow(UPDATENOW) can
    // report success without dispatching a presentation paint at all.  Paint
    // the retained native cache directly into the presentation now. WM_PAINT
    // remains the normal replay path for exposure/resize/ordinary invalidation.
    // This same contract removes the message-queue gap that becomes visible as
    // a one-frame 3D Word flicker during fast typing or key-repeat Backspace.
    BOOL fDirectPaint = FALSE;
    HDC hdcPresentation = GetDC(hwndPresentation);
    if (hdcPresentation != NULL)
    {
        fDirectPaint = FPaintPreviewCpu(
            hdcPresentation,
            rcDst.left, rcDst.top,
            rcDst.right - rcDst.left,
            rcDst.bottom - rcDst.top) ? TRUE : FALSE;
        if (fDirectPaint)
            GdiFlush();
        ReleaseDC(hwndPresentation, hdcPresentation);
    }
    else
    {
        PSTDIO pstdioDc = vapp.Pstdio();
        MVIE::MultiLog(pstdioDc != pvNil ? pstdioDc->Pmvie() : pvNil,
            "ape_present_getdc_fail serial=%lu hwnd=%p gle=%lu dest=(%ld,%ld)-(%ld,%ld)",
            (unsigned long)vgl.iPreviewFrameSerial, hwndPresentation,
            (unsigned long)GetLastError(),
            (long)rcDst.left, (long)rcDst.top, (long)rcDst.right, (long)rcDst.bottom);
    }

    // Keep the old synchronous invalidation only as an error fallback.  It is
    // useful if a transient Windows/GDI state prevents the direct commit, but
    // it is no longer part of the normal APE frame-delivery path.
    BOOL fFallbackRedraw = FALSE;
    if (!fDirectPaint)
    {
        fFallbackRedraw = RedrawWindow(hwndPresentation, &rcDst, NULL,
            RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }

    PSTDIO pstdio = vapp.Pstdio();
    MVIE::MultiLog(pstdio != pvNil ? pstdio->Pmvie() : pvNil,
        "ape_present_commit serial=%lu hwnd=%p direct=%d fallback_redraw=%d dest=(%ld,%ld)-(%ld,%ld)",
        (unsigned long)vgl.iPreviewFrameSerial, hwndPresentation,
        (int)fDirectPaint, (int)fFallbackRedraw,
        (long)rcDst.left, (long)rcDst.top, (long)rcDst.right, (long)rcDst.bottom);
}

static void InvalidateScaledPresentationAll(void)
{
    HWND hwndPresentation = NULL;
    int32_t num = 0;
    int32_t den = 0;
    if (!FModernScaledPresentation(&hwndPresentation, &num, &den) ||
        hwndPresentation == NULL || !IsWindow(hwndPresentation))
        return;
    InvalidateRect(hwndPresentation, NULL, FALSE);
}

bool FEnsureRenderBuffers(int32_t dxp, int32_t dyp)
{
    if (vgl.pcolour != NULL && vgl.pdepth != NULL && vgl.pcpuStage != NULL &&
        vgl.dxpBuffer == dxp && vgl.dypBuffer == dyp)
        return true;

    BrModernLog("GL FEnsureRenderBuffers create requested=%ldx%ld screen=%p", (long)dxp, (long)dyp, vgl.pscreen);
    FreeRenderBuffers();

    vgl.pcolour = BrPixelmapMatchTypedSized(vgl.pscreen, BR_PMMATCH_OFFSCREEN, BR_PMT_RGB_888, dxp, dyp);
    BrModernLog("GL colour buffer result=%p", vgl.pcolour);
    if (vgl.pcolour == NULL)
        return false;

    vgl.pcolour->origin_x = (br_int_16)(dxp / 2);
    vgl.pcolour->origin_y = (br_int_16)(dyp / 2);

    vgl.pdepth = BrPixelmapMatch(vgl.pcolour, BR_PMMATCH_DEPTH);
    BrModernLog("GL depth buffer result=%p", vgl.pdepth);
    if (vgl.pdepth == NULL)
    {
        FreeRenderBuffers();
        return false;
    }

    vgl.pdepth->origin_x = (br_int_16)(dxp / 2);
    vgl.pdepth->origin_y = (br_int_16)(dyp / 2);

    // Never hand glrend the application-constructed 1995 BWLD pixelmap.
    // Use a real modern BRender addressable memory pixelmap as the CPU/GPU
    // staging object and memcpy between that and Kauai's framebuffer.
    vgl.pcpuStage = BrPixelmapAllocate(BR_PMT_RGB_888, dxp, dyp, NULL, BR_PMAF_NORMAL);
    BrModernLog("GL CPU staging pixelmap result=%p pixels=%p row=%ld flags=0x%08lX",
                vgl.pcpuStage, vgl.pcpuStage != NULL ? vgl.pcpuStage->pixels : NULL,
                vgl.pcpuStage != NULL ? (long)vgl.pcpuStage->row_bytes : 0L,
                vgl.pcpuStage != NULL ? (unsigned long)vgl.pcpuStage->flags : 0UL);
    if (vgl.pcpuStage == NULL || vgl.pcpuStage->pixels == NULL)
    {
        FreeRenderBuffers();
        return false;
    }

    vgl.dxpBuffer = dxp;
    vgl.dypBuffer = dyp;
    BrModernLog("GL FEnsureRenderBuffers SUCCESS colour=%p depth=%p cpu_stage=%p origins=(%d,%d)/(%d,%d)",
                vgl.pcolour, vgl.pdepth, vgl.pcpuStage,
                (int)vgl.pcolour->origin_x, (int)vgl.pcolour->origin_y,
                (int)vgl.pdepth->origin_x, (int)vgl.pdepth->origin_y);
    return true;
}

bool FEnsureRenderer(void)
{
    BrModernLog("GL FEnsureRenderer enter started=%d hwnd=%p valid=%d screen=%p anchor=%p",
                (int)vgl.fRendererStarted, vgl.hwnd,
                vgl.hwnd != NULL ? (int)IsWindow(vgl.hwnd) : 0, vgl.pscreen, vgl.panchor);
    if (vgl.fRendererStarted)
        return true;
    if (vgl.hwnd == NULL || !IsWindow(vgl.hwnd))
    {
        BrModernLog("GL FEnsureRenderer defer: no valid -v HWND");
        return false;
    }

    // Avoid C99 designated initializers here: this file is compiled as C++
    // by the 3DMM Studio target, including MSVC builds.
    br_token_value rgtv[5] = {};
    rgtv[0].t = BRT_WIDTH_I32;
    rgtv[0].v.i32 = vdxpModernFront;
    rgtv[1].t = BRT_HEIGHT_I32;
    rgtv[1].v.i32 = vdypModernFront;
    rgtv[2].t = BRT_PIXEL_TYPE_U8;
    rgtv[2].v.u8 = BR_PMT_RGB_888;
    rgtv[3].t = BRT_OPENGL_EXT_PROCS_P;
    rgtv[3].v.h = &vrgGLProcs;
    rgtv[4].t = BR_NULL_TOKEN;

    BrModernLog("GL BrDevBeginTV begin device=glrend width=%ld height=%ld pixel_type=%u ext_procs=%p external_only=%d",
                (long)vdxpModernFront, (long)vdypModernFront, (unsigned)BR_PMT_RGB_888, &vrgGLProcs,
                (int)vfModernExternalOnly);
    const br_error errDev = BrDevBeginTV(&vgl.pscreen, "glrend", rgtv);
    BrModernLog("GL BrDevBeginTV return err=%ld screen=%p", (long)errDev, vgl.pscreen);
    if (errDev != BRE_OK || vgl.pscreen == NULL)
    {
        DiagLogBRender("BRender14 glrend BrDevBeginTV failed");
        return false;
    }

    vgl.pscreen->origin_x = (br_int_16)(vgl.pscreen->width / 2);
    vgl.pscreen->origin_y = (br_int_16)(vgl.pscreen->height / 2);

    // Keep one permanent same-device destination alive for the renderer.
    // Actual BWLDs use independently-sized offscreen buffers below.
    BrModernLog("GL create renderer anchor begin screen=%p", vgl.pscreen);
    vgl.panchor = BrPixelmapMatchTypedSized(vgl.pscreen, BR_PMMATCH_OFFSCREEN, BR_PMT_RGB_888, 16, 16);
    BrModernLog("GL create renderer anchor result=%p", vgl.panchor);
    if (vgl.panchor == NULL)
    {
        BrPixelmapFree(vgl.pscreen);
        vgl.pscreen = NULL;
        return false;
    }

    BrModernLog("GL BrRendererBegin begin anchor=%p heap=%p bytes=%lu", vgl.panchor,
                vgl.rgbPrimitiveHeap, (unsigned long)sizeof(vgl.rgbPrimitiveHeap));
    BrRendererBegin(vgl.panchor, NULL, NULL, vgl.rgbPrimitiveHeap, sizeof(vgl.rgbPrimitiveHeap));
    BrModernLog("GL BrRendererBegin returned");
    vgl.fRendererStarted = true;
    DiagLogBRender("BRender14 glrend renderer started");
    return true;
}

} // namespace

void BrShadowLog(const char *pszFormat, ...)
{
    if (!vfShadowLogEnabled || pszFormat == NULL)
        return;
    va_list ap;
    va_start(ap, pszFormat);
    ShadowLogV(pszFormat, ap);
    va_end(ap);
}

bool FBrShadowLogEnabled(void)
{
    return vfShadowLogEnabled;
}

void BrShadowLogSetEnabled(bool fEnable)
{
    if (!fEnable)
    {
        ShadowLogFlush();
        AcquireSRWLockExclusive(&vsrwShadowLog);
        if (vpShadowLogFile != NULL)
        {
            fclose(vpShadowLogFile);
            vpShadowLogFile = NULL;
        }
        ReleaseSRWLockExclusive(&vsrwShadowLog);
        vfShadowLogEnabled = false;
        if (!vfModernLogEnabled && vpModernVectoredHandler != NULL)
        {
            RemoveVectoredExceptionHandler(vpModernVectoredHandler);
            vpModernVectoredHandler = NULL;
        }
        return;
    }

    if (vfShadowLogEnabled)
        return;

    char szPath[MAX_PATH];
    if (!FShadowLogPath(szPath, sizeof(szPath)))
        return;

    FILE *pfile = NULL;
    if (fopen_s(&pfile, szPath, "w") != 0 || pfile == NULL)
        return;

    setvbuf(pfile, NULL, _IOFBF, 32 * 1024);
    vpShadowLogFile = pfile;
    vfShadowLogEnabled = true;
    InterlockedExchange(&vlShadowLogSequence, 0);

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(vpShadowLogFile, "4DMM shadow renderer diagnostics\n");
    fprintf(vpShadowLogFile, "session %04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu\n",
            (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
            (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
            (unsigned)st.wMilliseconds, (unsigned long)GetCurrentProcessId());
    fflush(vpShadowLogFile);

    if (vpModernVectoredHandler == NULL)
        vpModernVectoredHandler = AddVectoredExceptionHandler(1, ModernVectoredExceptionHandler);

    BrShadowLog("SHADOW runtime diagnostics enabled command_line=%s", GetCommandLineA());
}

void BrModernLog(const char *pszFormat, ...)
{
    if (!vfModernLogEnabled || pszFormat == NULL)
        return;
    va_list ap;
    va_start(ap, pszFormat);
    ModernLogV(pszFormat, ap);
    va_end(ap);
}

bool FBrModernLogEnabled(void)
{
    return vfModernLogEnabled;
}

void BrModernLogSetEnabled(bool fEnable)
{
    if (!fEnable)
    {
        if (vfModernLogEnabled)
            BrModernLog("modern BR logging disabled");
        ModernLogClose();
        vfModernLogEnabled = false;
        if (!vfShadowLogEnabled && vpModernVectoredHandler != NULL)
        {
            RemoveVectoredExceptionHandler(vpModernVectoredHandler);
            vpModernVectoredHandler = NULL;
        }
        return;
    }

    if (vfModernLogEnabled)
        return;

    vfModernLogEnabled = true;
    InterlockedExchange(&vlModernLogSequence, 0);
    vcModernFrames = 0;
    vcModernFrameDumps = 0;
    vdwModernLastCpuDumpHash = 0;
    char szPath[MAX_PATH];
    if (FModernLogPath(szPath, sizeof(szPath)))
    {
        FILE *pfile = NULL;
        if (fopen_s(&pfile, szPath, "a") == 0 && pfile != NULL)
        {
            SYSTEMTIME st;
            GetLocalTime(&st);
            fprintf(pfile, "\n===== MODERN BR SESSION %04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu =====\n",
                    (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                    (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
                    (unsigned)st.wMilliseconds, (unsigned long)GetCurrentProcessId());
            fclose(pfile);
        }
    }

    vpModernVectoredHandler = AddVectoredExceptionHandler(1, ModernVectoredExceptionHandler);
    BrModernLog("modern BR logging ENABLED pid=%lu exe=%p vectored_handler=%p",
                (unsigned long)GetCurrentProcessId(), GetModuleHandleA(NULL), vpModernVectoredHandler);
    BrModernLog("BUILD ID 4DMM BRender14 v165 padded-readback-flag-contract + historical-tdt-framing");
    BrModernLog("SCALAR TOKEN ABI based_fixed=%d opacity_scalar=%u opacity_fixed=%u opacity_float=%u m2v_scalar=%u m2v_fixed=%u m2v_float=%u",
                (int)BASED_FIXED, (unsigned)BRT_AS_SCALAR(OPACITY), (unsigned)BRT_AS_FIXED(OPACITY),
                (unsigned)BRT_AS_FLOAT(OPACITY), (unsigned)BRT_AS_MATRIX34_SCALAR(MODEL_TO_VIEW),
                (unsigned)BRT_AS_MATRIX34_FIXED(MODEL_TO_VIEW), (unsigned)BRT_AS_MATRIX34_FLOAT(MODEL_TO_VIEW));
    BrModernLog("ABI sizeof(br_scalar)=%lu sizeof(br_angle)=%lu sizeof(br_model)=%lu sizeof(br_face)=%lu scalar_one=0x%08lX",
                (unsigned long)sizeof(br_scalar), (unsigned long)sizeof(br_angle),
                (unsigned long)sizeof(br_model), (unsigned long)sizeof(br_face),
                (unsigned long)(uint32_t)BR_SCALAR(1.0));
    BrModernLog("command_line=%s", GetCommandLineA());
}

void BrModernLogBootstrapFromCommandLine(void)
{
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == NULL)
        return;
    for (int i = 1; i < argc; ++i)
    {
        if (_wcsicmp(argv[i], L"-modern_br_log") == 0 || _wcsicmp(argv[i], L"/modern_br_log") == 0 ||
            _wcsicmp(argv[i], L"-modern_br_logs") == 0 || _wcsicmp(argv[i], L"/modern_br_logs") == 0)
        {
            BrModernLogSetEnabled(true);
            break;
        }
    }
    LocalFree(argv);
}

void BrModernLogException(void *pvExceptionPointers, const char *pszWhere)
{
    ModernLogExceptionInternal((EXCEPTION_POINTERS *)pvExceptionPointers, pszWhere);
}

void BrModernInstallDiagHandler(void)
{
    if (!vfModernLogEnabled && !vfShadowLogEnabled)
        return;

    br_diaghandler *pOld = BrDiagHandlerSet(&vModernDiagHandler);
    if (pOld != &vModernDiagHandler)
        vpModernPreviousDiagHandler = pOld;

    BrModernLog("BRender diagnostic handler installed previous=%p current=%p",
                vpModernPreviousDiagHandler, &vModernDiagHandler);
}

/*
 * BRender calls these from BrBegin/BrEnd.  Defining them in the executable
 * keeps driver registration at the application boundary, as BRender's own
 * sample programs do, without creating a core<->driver circular dependency.
 */
extern "C" void BR_CALLBACK _BrBeginHook(void)
{
    BrModernLog("BRender _BrBeginHook enter");
    BrDevAddStatic(NULL, BrDrv1GLBegin, NULL);
    BrModernLog("BRender _BrBeginHook BrDevAddStatic returned");
}

extern "C" void BR_CALLBACK _BrEndHook(void)
{
    BrModernLog("BRender _BrEndHook");
    ModernLogFlush();
}

void BrModernViewportSetWindow(void *pvHwnd)
{
    HWND hwnd = (HWND)pvHwnd;
    BrModernLog("viewport SetWindow requested=%p current=%p requested_valid=%d", hwnd, vgl.hwnd,
                hwnd != NULL ? (int)IsWindow(hwnd) : 0);
    if (hwnd == vgl.hwnd)
        return;

    BrModernViewportShutdown();
    vgl.hwnd = hwnd;
    BrModernLog("viewport SetWindow committed hwnd=%p", vgl.hwnd);
}

void BrModernViewportConfigure(int32_t dxp, int32_t dyp, bool fExternalOnly)
{
    if (dxp <= 0 || dyp <= 0)
    {
        dxp = kdxpModernFrontDefault;
        dyp = kdypModernFrontDefault;
        fExternalOnly = false;
    }

    if (vgl.fRendererStarted && (dxp != vdxpModernFront || dyp != vdypModernFront))
        BrModernViewportShutdown();

    vdxpModernFront = dxp;
    vdypModernFront = dyp;
    vfModernExternalOnly = fExternalOnly;
    BrModernLog("viewport Configure render=%ldx%ld external_only=%d",
                (long)vdxpModernFront, (long)vdypModernFront, (int)vfModernExternalOnly);
}

bool FBrModernViewportReady(void)
{
    return vgl.fRendererStarted && vgl.pscreen != NULL && vgl.hwnd != NULL && IsWindow(vgl.hwnd);
}

bool FBrModernViewportRender(void *pvOwner, PBACT pbactWorld, PBACT pbactCamera, PBPMP pbpmpCpu, bool fPresent)
{
    const uint32_t iFrame = vcModernFrames++;
    const bool fDetailed = (iFrame < 120) || ((iFrame % 60) == 0);
    bool fDumpThisFrame = false;
    uint32_t iDump = 0;
    if (fDetailed)
        BrModernLog("FRAME %lu enter owner=%p world=%p camera=%p cpu=%p present=%d hwnd=%p ready=%d",
                    (unsigned long)iFrame, pvOwner, pbactWorld, pbactCamera, pbpmpCpu, (int)fPresent,
                    vgl.hwnd, (int)FBrModernViewportReady());
    if (pbactWorld == pvNil || pbactCamera == pvNil || pbpmpCpu == pvNil)
    {
        BrModernLog("FRAME %lu FAIL null input world=%p camera=%p cpu=%p",
                    (unsigned long)iFrame, pbactWorld, pbactCamera, pbpmpCpu);
        return false;
    }

    // BWLD creates its camera actor before any movie/scene has supplied the
    // authored camera clipping range and FOV.  The legacy renderer tolerated
    // that transient zeroed camera because it normally did not traverse the
    // scene until later.  The 4x presentation can cause an earlier editor
    // paint while a button is tracking, which made modern BRender reach
    // CameraToScreenMatrix4 with hither_z == 0 and abort the entire process.
    // Do not invent a temporary camera.  Simply defer the GPU frame until
    // BWLD::SetCamera has installed real scene camera data.
    if (pbactCamera->type != BR_ACTOR_CAMERA || pbactCamera->type_data == pvNil)
    {
        if (!vfModernCameraInitDeferredLogged)
        {
            BrModernLog("FRAME %lu deferred: camera actor not initialized type=%u type_data=%p",
                        (unsigned long)iFrame, (unsigned)pbactCamera->type, pbactCamera->type_data);
            vfModernCameraInitDeferredLogged = true;
        }
        return false;
    }

    const br_camera *pcam = (const br_camera *)pbactCamera->type_data;
    if (pcam->hither_z <= 0 || pcam->yon_z <= pcam->hither_z || pcam->field_of_view == 0)
    {
        if (!vfModernCameraInitDeferredLogged)
        {
            BrModernLog("FRAME %lu deferred: scene camera not ready hither=0x%08lX yon=0x%08lX fov=0x%04X aspect=0x%08lX",
                        (unsigned long)iFrame, (unsigned long)pcam->hither_z,
                        (unsigned long)pcam->yon_z, (unsigned)pcam->field_of_view,
                        (unsigned long)pcam->aspect);
            vfModernCameraInitDeferredLogged = true;
        }
        return false;
    }
    if (vfModernCameraInitDeferredLogged)
    {
        BrModernLog("FRAME %lu scene camera ready; resuming modern render hither=0x%08lX yon=0x%08lX fov=0x%04X",
                    (unsigned long)iFrame, (unsigned long)pcam->hither_z,
                    (unsigned long)pcam->yon_z, (unsigned)pcam->field_of_view);
        vfModernCameraInitDeferredLogged = false;
    }

    if (pbpmpCpu->type != BR_PMT_RGB_888 || pbpmpCpu->pixels == pvNil)
    {
        DiagLogBRender("BRender14 GPU render requires RGB888 CPU BWLD buffer, type=%u pixels=%p",
                       (unsigned)pbpmpCpu->type, pbpmpCpu->pixels);
        return false;
    }
    if (fDetailed)
    {
        BrModernLog("FRAME %lu CPU pixelmap type=%u pixels=%p row=%ld wh=%ux%u origin=(%d,%d) flags=0x%08lX",
                    (unsigned long)iFrame, (unsigned)pbpmpCpu->type, pbpmpCpu->pixels,
                    (long)pbpmpCpu->row_bytes, (unsigned)pbpmpCpu->width, (unsigned)pbpmpCpu->height,
                    (int)pbpmpCpu->origin_x, (int)pbpmpCpu->origin_y, (unsigned long)pbpmpCpu->flags);
        uint32_t cNonZero = 0;
        const uint32_t dwHash = DwModernSparsePixelHash(pbpmpCpu, &cNonZero);
        const uint8_t *pb = (const uint8_t *)pbpmpCpu->pixels;
        BrModernLog("FRAME %lu CPU BEFORE upload sparse_hash=0x%08lX nonzero_samples=%lu first_bytes=%02X %02X %02X %02X %02X %02X",
                    (unsigned long)iFrame, (unsigned long)dwHash, (unsigned long)cNonZero,
                    (unsigned)pb[0], (unsigned)pb[1], (unsigned)pb[2],
                    (unsigned)pb[3], (unsigned)pb[4], (unsigned)pb[5]);
        if (vcModernFrameDumps < 16 && (vcModernFrameDumps == 0 || dwHash != vdwModernLastCpuDumpHash))
        {
            iDump = vcModernFrameDumps++;
            vdwModernLastCpuDumpHash = dwHash;
            fDumpThisFrame = true;
            char szDump[64];
            sprintf_s(szDump, sizeof(szDump), "modern_%02lu_cpu_before.bmp", (unsigned long)iDump);
            FModernDumpRgb888Bmp(pbpmpCpu, szDump);
        }
    }
    if (!FEnsureRenderer())
    {
        BrModernLog("FRAME %lu renderer unavailable", (unsigned long)iFrame);
        return false;
    }
    if (fDetailed)
        BrModernLog("FRAME %lu renderer ready screen=%p context=%p", (unsigned long)iFrame,
                    vgl.pscreen, vgl.hglrc);

    const int32_t dxpSource = pbpmpCpu->width;
    const int32_t dypSource = pbpmpCpu->height;
    const bool fNativeExternalFrame = vfModernExternalOnly && fPresent;
    int32_t previewNum = 1;
    int32_t previewDen = 1;
    const bool fNativeApePreview = !fPresent && pvOwner != NULL &&
        pvOwner == vgl.pvPreviewOwner &&
        FModernScaledPresentation(NULL, &previewNum, &previewDen);
    // In standard/scaled Modern mode the OpenGL HWND is now a hidden WGL
    // context host and presentation is owned by the Kauai/BWLD readback or
    // scaled compositor. Only swap a frontbuffer when that host is actually
    // visible (currently the native-1080p compatibility path). This removes
    // the last ordinary-mode dependency on a second visible GL surface.
    const bool fDirectPresent = fPresent && vgl.hwnd != NULL && IsWindowVisible(vgl.hwnd);
    const int32_t dxp = fNativeExternalFrame ? vdxpModernFront :
                        fNativeApePreview ? MulDiv(dxpSource, previewNum, previewDen) : dxpSource;
    const int32_t dyp = fNativeExternalFrame ? vdypModernFront :
                        fNativeApePreview ? MulDiv(dypSource, previewNum, previewDen) : dypSource;
    if (fDetailed && fNativeApePreview)
        BrModernLog("FRAME %lu APE native preview owner=%p source=%ldx%ld render=%ldx%ld scale=%ld/%ld",
                    (unsigned long)iFrame, pvOwner, (long)dxpSource, (long)dypSource,
                    (long)dxp, (long)dyp, (long)previewNum, (long)previewDen);
    if (!FEnsureRenderBuffers(dxp, dyp))
    {
        DiagLogBRender("BRender14 failed to create %ldx%ld GL render buffers", (long)dxp, (long)dyp);
        return false;
    }

    bool fBackgroundStaged = false;
    if (fNativeApePreview)
    {
        // 3D Word / Costume / Action APEs are self-contained black-backed 3D
        // previews. Their legacy BWLD RGB buffer is only a compatibility
        // surface and can contain stale/partial low-resolution pixels from
        // Kauai's old dirty-rectangle path. Scaling that buffer into the native
        // target is what produced the v152 solid square / rainbow-confetti
        // backgrounds. Start the native APE target from a deterministic black
        // surface and let BRender draw the preview geometry at full resolution.
        BrPixelmapFill(vgl.pcpuStage, BR_COLOUR_RGB(0, 0, 0));
        fBackgroundStaged = true;
    }
    else
    {
        fBackgroundStaged = (dxp == dxpSource && dyp == dypSource)
            ? FModernCopyRgb888Rows(vgl.pcpuStage, pbpmpCpu, dxp, dyp)
            : FModernScaleRgb888Rows(vgl.pcpuStage, pbpmpCpu);
    }
    if (!fBackgroundStaged)
    {
        BrModernLog("FRAME %lu FAIL staging legacy BWLD RGB888 background source=%ldx%ld target=%ldx%ld",
                    (unsigned long)iFrame, (long)dxpSource, (long)dypSource, (long)dxp, (long)dyp);
        return false;
    }
    if (fDetailed)
    {
        uint32_t cStageNonZero = 0;
        const uint32_t dwStageHash = DwModernSparsePixelHash(vgl.pcpuStage, &cStageNonZero);
        BrModernLog("FRAME %lu CPU STAGE before upload sparse_hash=0x%08lX nonzero_samples=%lu first=%02X %02X %02X",
                    (unsigned long)iFrame, (unsigned long)dwStageHash, (unsigned long)cStageNonZero,
                    (unsigned)((uint8_t *)vgl.pcpuStage->pixels)[0],
                    (unsigned)((uint8_t *)vgl.pcpuStage->pixels)[1],
                    (unsigned)((uint8_t *)vgl.pcpuStage->pixels)[2]);
    }

    BrRendererFrameBegin();

    if (fNativeApePreview)
    {
        // APE frames must be replacement frames, never incremental layers.
        // The memory->device RectangleCopy used for the black legacy backdrop
        // is not a reliable GL clear on every glrend path. Explicitly clear the
        // device colour target so a longer 3D Word cannot inherit the previous
        // shorter word and create the tunnel/meat-grinder accumulation.
        BrPixelmapFill(vgl.pcolour, BR_COLOUR_RGB(0, 0, 0));
        if (fDetailed)
            ModernLogGlErrors("ape-colour-clear");
    }

    // Preserve the legacy 3DMM background image.  The background Z bitmap is
    // intentionally NOT imported in v3 yet, so actors may temporarily draw in
    // front of background geometry.  The source is a genuine BRender memory
    // pixelmap, not 3DMM's hand-built public structure.
    if (fDetailed)
        BrModernLog("FRAME %lu background upload begin colour=%p cpu_stage=%p legacy_cpu=%p source=%ldx%ld target=%ldx%ld",
                    (unsigned long)iFrame, vgl.pcolour, vgl.pcpuStage, pbpmpCpu,
                    (long)dxpSource, (long)dypSource, (long)dxp, (long)dyp);
    BrPixelmapRectangleCopy(vgl.pcolour,
                            -vgl.pcolour->origin_x, -vgl.pcolour->origin_y,
                            vgl.pcpuStage,
                            0, 0,
                            dxp, dyp);
    if (fDetailed)
    {
        ModernLogGlErrors("background-upload");

        // Probe the colour target immediately, before BrZbSceneRender gets a
        // chance to touch it.  This distinguishes a failed memory->GL upload
        // from a later renderer operation clearing/overwriting a good upload.
        ModernFillRgb888Bytes(vgl.pcpuStage, 0xA5);
        BrPixelmapRectangleCopy(vgl.pcpuStage,
                                0, 0,
                                vgl.pcolour,
                                -vgl.pcolour->origin_x, -vgl.pcolour->origin_y,
                                dxp, dyp);
        const bool fUploadProbeUntouched = FModernRgb888AllBytesEqual(vgl.pcpuStage, 0xA5);
        uint32_t cUploadNonZero = 0;
        const uint32_t dwUploadHash = DwModernSparsePixelHash(vgl.pcpuStage, &cUploadNonZero);
        const uint8_t *pbUpload = (const uint8_t *)vgl.pcpuStage->pixels;
        BrModernLog("FRAME %lu GPU AFTER BACKGROUND UPLOAD sparse_hash=0x%08lX nonzero_samples=%lu sentinel_untouched=%d first_bytes=%02X %02X %02X %02X %02X %02X",
                    (unsigned long)iFrame, (unsigned long)dwUploadHash, (unsigned long)cUploadNonZero,
                    (int)fUploadProbeUntouched,
                    (unsigned)pbUpload[0], (unsigned)pbUpload[1], (unsigned)pbUpload[2],
                    (unsigned)pbUpload[3], (unsigned)pbUpload[4], (unsigned)pbUpload[5]);
        ModernLogGlErrors("background-upload-readback");
    }
    if (fDetailed)
        BrModernLog("FRAME %lu background upload returned; depth fill begin depth=%p",
                    (unsigned long)iFrame, vgl.pdepth);
    BrPixelmapFill(vgl.pdepth, 0xFFFFFFFFu);
    if (fDetailed)
        ModernLogGlErrors("depth-fill");

    if (fDetailed)
    {
        uint32_t cTopActors = 0;
        for (PBACT p = pbactWorld->children; p != pvNil && cTopActors < 4096; p = p->next)
            ++cTopActors;
        BrModernLog("FRAME %lu DIRECT_GL_RENDER BEGIN world_children=%p top_level=%lu colour=%p depth=%p; modern GL target, presentation may be embedded",
                    (unsigned long)iFrame, pbactWorld->children, (unsigned long)cTopActors, vgl.pcolour, vgl.pdepth);
    }
    if (iFrame < 12)
    {
        uint32_t cActors = 0;
        ModernLogActorTree(pbactWorld, 0, &cActors);
        BrModernLog("FRAME %lu ACTOR PREFLIGHT complete count=%lu%s",
                    (unsigned long)iFrame, (unsigned long)cActors,
                    cActors >= 256 ? " TRUNCATED" : "");
    }
    BrZbSceneRender(pbactWorld, pbactCamera, vgl.pcolour, vgl.pdepth);
    if (fDetailed)
        ModernLogGlErrors("scene-render");
    if (fDetailed)
        BrModernLog("FRAME %lu BrZbSceneRender returned; BrRendererFrameEnd BEGIN",
                    (unsigned long)iFrame);
    BrRendererFrameEnd();
    if (fDetailed)
    {
        ModernLogGlErrors("frame-end");
        BrModernLog("FRAME %lu BrRendererFrameEnd returned", (unsigned long)iFrame);
    }

    if (fNativeExternalFrame)
    {
        // High-resolution mode deliberately decouples the GL render target from
        // 3DMM's 544x306 editor buffer. Do not shrink the configured native
        // GPU frame back into the editor. Keep a same-device display copy so unrelated
        // offscreen preview renders cannot replace the external movie frame.
        if (!FEnsureExternalDisplayBuffer())
            return false;

        BrPixelmapRectangleCopy(vgl.pdisplayExternal,
                                -vgl.pdisplayExternal->origin_x, -vgl.pdisplayExternal->origin_y,
                                vgl.pcolour,
                                -vgl.pcolour->origin_x, -vgl.pcolour->origin_y,
                                dxp, dyp);
        vgl.fHaveExternalFrame = true;

        // v48 changes only the ownership of the native movie pixels. Preserve
        // the known-good v47 Kauai/DWM UI and input path exactly, but keep a
        // stable CPU copy of the completed 2176x1224 frame so the destination
        // application HWND can paint the movie layer itself. This is the first
        // real-compositor step and intentionally does not touch buttons, GOBs,
        // modal loops, or source-window refresh behavior.
        if (vgl.pcompositeCpu != NULL && vgl.pcompositeCpu->pixels != NULL)
        {
            BrPixelmapRectangleCopy(vgl.pcompositeCpu,
                                    0, 0,
                                    vgl.pcolour,
                                    -vgl.pcolour->origin_x, -vgl.pcolour->origin_y,
                                    dxp, dyp);
            vgl.fHaveCompositeCpuFrame = true;
            if (fDetailed)
            {
                uint32_t cCompositeNonZero = 0;
                const uint32_t dwCompositeHash = DwModernSparsePixelHash(vgl.pcompositeCpu, &cCompositeNonZero);
                BrModernLog("FRAME %lu native compositor cache sparse_hash=0x%08lX nonzero_samples=%lu",
                            (unsigned long)iFrame, (unsigned long)dwCompositeHash,
                            (unsigned long)cCompositeNonZero);
            }

            // Invalidate only. Do not synchronously repaint from inside the
            // BRender frame, which would re-enter Kauai and recreate the
            // modal/input deadlocks from the abandoned v29-v46 branch.
            HWND hwndPresentation = vwig.hwndApp != NULL
                                        ? (HWND)GetPropA(vwig.hwndApp, "4DMMUiScaleWindow")
                                        : NULL;
            if (hwndPresentation != NULL && IsWindow(hwndPresentation))
                InvalidateRect(hwndPresentation, NULL, FALSE);
        }

        // Keep the canonical 544x306 Kauai BWLD framebuffer alive too. Older
        // external-only revisions deliberately zeroed it after producing the
        // native GL frame, which is why exposing a native 3D Word / Costume /
        // Action easel could reveal a dead black movie viewport behind it. The
        // high-resolution renderer remains authoritative; this is simply its
        // downsampled compatibility image for the original Kauai surface.
        bool fLegacyViewportUpdated = false;
        if (vgl.pcompositeCpu != NULL && vgl.fHaveCompositeCpuFrame)
            fLegacyViewportUpdated = FModernScaleRgb888Rows(pbpmpCpu, vgl.pcompositeCpu);
        if (fDetailed)
            BrModernLog("FRAME %lu native legacy-BWLD compatibility update=%d source=%ldx%ld native=%ldx%ld",
                        (unsigned long)iFrame, (int)fLegacyViewportUpdated,
                        (long)dxpSource, (long)dypSource, (long)dxp, (long)dyp);
        vgl.fHaveFrame = true;
        if (fDirectPresent)
        {
            if (fDetailed)
                BrModernLog("FRAME %lu native external-only present BEGIN screen=%p display=%p render=%ldx%ld requested=%d direct=%d",
                            (unsigned long)iFrame, vgl.pscreen, vgl.pdisplayExternal,
                            (long)dxp, (long)dyp, (int)fPresent, (int)fDirectPresent);
            BrPixelmapDoubleBuffer(vgl.pscreen, vgl.pdisplayExternal);
            if (fDetailed)
                BrModernLog("FRAME %lu native external-only present returned", (unsigned long)iFrame);
        }
        else if (fDetailed && fPresent)
        {
            BrModernLog("FRAME %lu direct present skipped: hidden embedded GL host; compositor/cache owns pixels",
                        (unsigned long)iFrame);
        }
        if (fDetailed)
            BrModernLog("FRAME %lu SUCCESS native external-only source=%ldx%ld render=%ldx%ld",
                        (unsigned long)iFrame, (long)dxpSource, (long)dypSource, (long)dxp, (long)dyp);
        return true;
    }

    // Read the GPU frame into the real BRender memory pixelmap first, then
    // memcpy it back to Kauai.  Fill the stage with a sentinel so a silently
    // rejected device->memory copy is distinguishable from a legitimately
    // black GPU frame in modern_br.log.
    ModernFillRgb888Bytes(vgl.pcpuStage, 0xA5);
    if (fDetailed)
        BrModernLog("FRAME %lu GPU readback BEGIN device=%p cpu_stage=%p sentinel=A5",
                    (unsigned long)iFrame, vgl.pcolour, vgl.pcpuStage);
    BrPixelmapRectangleCopy(vgl.pcpuStage,
                            0, 0,
                            vgl.pcolour,
                            -vgl.pcolour->origin_x, -vgl.pcolour->origin_y,
                            dxp, dyp);
    const bool fReadbackUntouched = FModernRgb888AllBytesEqual(vgl.pcpuStage, 0xA5);
    if (fDetailed)
    {
        ModernLogGlErrors("gpu-readback");
        uint32_t cStageNonZero = 0;
        const uint32_t dwStageHash = DwModernSparsePixelHash(vgl.pcpuStage, &cStageNonZero);
        const uint8_t *pbStage = (const uint8_t *)vgl.pcpuStage->pixels;
        BrModernLog("FRAME %lu GPU STAGE readback sparse_hash=0x%08lX nonzero_samples=%lu sentinel_untouched=%d first_bytes=%02X %02X %02X %02X %02X %02X",
                    (unsigned long)iFrame, (unsigned long)dwStageHash, (unsigned long)cStageNonZero,
                    (int)fReadbackUntouched,
                    (unsigned)pbStage[0], (unsigned)pbStage[1], (unsigned)pbStage[2],
                    (unsigned)pbStage[3], (unsigned)pbStage[4], (unsigned)pbStage[5]);
    }
    if (!fReadbackUntouched)
    {
        if (fNativeApePreview)
        {
            uint32_t cStageNonZero = 0;
            const uint32_t dwStageHash = DwModernSparsePixelHash(vgl.pcpuStage, &cStageNonZero);
            MODERNAPEACTORSTATS stats = {};
            CountModernApeActorTree(pbactWorld != NULL ? pbactWorld->children : NULL, &stats);
            PSTDIO pstdioApe = vapp.Pstdio();
            const long cbTightRgb = (long)dxp * 3L;
            const long cbGlPack4Row = (cbTightRgb + 3L) & ~3L;
            const long cbStageRow = vgl.pcpuStage != pvNil ? (long)vgl.pcpuStage->row_bytes : 0L;
            MVIE::MultiLog(pstdioApe != pvNil ? pstdioApe->Pmvie() : pvNil,
                "ape_gpu_frame owner=%p source=%ldx%ld render=%ldx%ld hash=0x%08lX nonzero_samples=%lu stage_row=%ld tight_row=%ld gl_pack4_row=%ld br_vs_gl4=%ld actors=%lu models=%lu drawable=%lu lights=%lu cameras=%lu other=%lu",
                pvOwner, (long)dxpSource, (long)dypSource, (long)dxp, (long)dyp,
                (unsigned long)dwStageHash, (unsigned long)cStageNonZero,
                cbStageRow, cbTightRgb, cbGlPack4Row, cbStageRow - cbGlPack4Row,
                (unsigned long)stats.cTotal, (unsigned long)stats.cModel,
                (unsigned long)stats.cDrawableModel, (unsigned long)stats.cLight,
                (unsigned long)stats.cCamera, (unsigned long)stats.cOther);

            if (!FEnsurePreviewCpuBuffer(dxp, dyp) ||
                !FModernCopyRgb888Rows(vgl.ppreviewCpu, vgl.pcpuStage, dxp, dyp))
            {
                BrModernLog("FRAME %lu FAIL caching native APE preview source=%ldx%ld render=%ldx%ld",
                            (unsigned long)iFrame, (long)dxpSource, (long)dypSource,
                            (long)dxp, (long)dyp);
                return false;
            }

            // The scaled presentation owns APE 3D pixels now. Do not shrink
            // this native frame back into the 640x480 BWLD and then let Kauai
            // draw a second low-resolution copy behind it. Keep only a black
            // compatibility surface for dirty-region/layout behavior. Picking
            // uses BRender geometry, not these RGB bytes.
            BrPixelmapFill(pbpmpCpu, BR_COLOUR_RGB(0, 0, 0));
            vgl.fHavePreviewCpuFrame = true;
            ++vgl.iPreviewFrameSerial;
            if (vgl.iPreviewFrameSerial == 0)
                ++vgl.iPreviewFrameSerial;
            uint32_t cCacheNonZero = 0;
            const uint32_t dwCacheHash = DwModernSparsePixelHash(vgl.ppreviewCpu, &cCacheNonZero);
            PSTDIO pstdioCache = vapp.Pstdio();
            MVIE::MultiLog(pstdioCache != pvNil ? pstdioCache->Pmvie() : pvNil,
                "ape_cache_commit serial=%lu hash=0x%08lX nonzero_samples=%lu preview=(%ld,%ld)-(%ld,%ld)",
                (unsigned long)vgl.iPreviewFrameSerial, (unsigned long)dwCacheHash,
                (unsigned long)cCacheNonZero, (long)vgl.rcPreviewSource.left,
                (long)vgl.rcPreviewSource.top, (long)vgl.rcPreviewSource.right,
                (long)vgl.rcPreviewSource.bottom);
            PresentPreviewPresentationRectNow(&vgl.rcPreviewSource);
            if (fDetailed)
                BrModernLog("FRAME %lu APE native-only cache committed; legacy BWLD RGB surface cleared",
                            (unsigned long)iFrame);
        }
        else if (!FModernCopyRgb888Rows(pbpmpCpu, vgl.pcpuStage, dxp, dyp))
        {
            BrModernLog("FRAME %lu FAIL copying BRender staging readback into legacy BWLD RGB888",
                        (unsigned long)iFrame);
            return false;
        }
    }
    else
    {
        BrModernLog("FRAME %lu GPU readback left staging sentinel untouched; preserving legacy CPU framebuffer",
                    (unsigned long)iFrame);
        if (fNativeApePreview)
        {
            static uint32_t cApeReadbackUntouchedLogged = 0;
            if (cApeReadbackUntouchedLogged < 64)
            {
                PSTDIO pstdioReadback = vapp.Pstdio();
                const long cbStageRow = vgl.pcpuStage != pvNil ? (long)vgl.pcpuStage->row_bytes : 0L;
                const long cbTightRgb = (long)dxp * 3L;
                const unsigned long grfStage = vgl.pcpuStage != pvNil ? (unsigned long)vgl.pcpuStage->flags : 0UL;
                MVIE::MultiLog(pstdioReadback != pvNil ? pstdioReadback->Pmvie() : pvNil,
                    "ape_readback_untouched owner=%p source=%ldx%ld render=%ldx%ld stage_row=%ld tight_row=%ld padding=%ld stage_flags=0x%08lX linear=%d whole=%d serial=%lu",
                    pvOwner, (long)dxpSource, (long)dypSource, (long)dxp, (long)dyp,
                    cbStageRow, cbTightRgb, cbStageRow - cbTightRgb, grfStage,
                    (int)((grfStage & BR_PMF_LINEAR) != 0),
                    (int)((grfStage & BR_PMF_ROW_WHOLEPIXELS) != 0),
                    (unsigned long)vgl.iPreviewFrameSerial);
                ++cApeReadbackUntouchedLogged;
            }
        }
    }
    if (fDetailed)
    {
        uint32_t cNonZero = 0;
        const uint32_t dwHash = DwModernSparsePixelHash(pbpmpCpu, &cNonZero);
        const uint8_t *pb = (const uint8_t *)pbpmpCpu->pixels;
        BrModernLog("FRAME %lu GPU/CPU bridge result sparse_hash=0x%08lX nonzero_samples=%lu first_bytes=%02X %02X %02X %02X %02X %02X",
                    (unsigned long)iFrame, (unsigned long)dwHash, (unsigned long)cNonZero,
                    (unsigned)pb[0], (unsigned)pb[1], (unsigned)pb[2],
                    (unsigned)pb[3], (unsigned)pb[4], (unsigned)pb[5]);
        if (fDumpThisFrame)
        {
            char szDump[64];
            sprintf_s(szDump, sizeof(szDump), "modern_%02lu_gpu_after.bmp", (unsigned long)iDump);
            FModernDumpRgb888Bmp(pbpmpCpu, szDump);
        }
    }

    vgl.fHaveFrame = true;
    if (fDirectPresent)
    {
        if (fDetailed)
            BrModernLog("FRAME %lu present BEGIN screen=%p colour=%p requested=%d direct=%d",
                        (unsigned long)iFrame, vgl.pscreen, vgl.pcolour,
                        (int)fPresent, (int)fDirectPresent);
        BrPixelmapDoubleBuffer(vgl.pscreen, vgl.pcolour);
        if (fDetailed)
            BrModernLog("FRAME %lu present returned", (unsigned long)iFrame);
    }
    else if (fDetailed && fPresent)
    {
        BrModernLog("FRAME %lu direct present skipped: hidden embedded GL host; BWLD/readback owns pixels",
                    (unsigned long)iFrame);
    }
    if (fDetailed)
        BrModernLog("FRAME %lu SUCCESS", (unsigned long)iFrame);
    return true;
}

void BrModernPreviewActivate(void *pvOwner, int32_t xpSource, int32_t ypSource,
                             int32_t dxpSource, int32_t dypSource,
                             int32_t xpOwnerSource, int32_t ypOwnerSource,
                             int32_t dxpOwnerSource, int32_t dypOwnerSource)
{
    if (pvOwner == NULL || dxpSource <= 0 || dypSource <= 0)
        return;

    const RECT rcOld = vgl.rcPreviewSource;
    const bool fHadOldFrame = vgl.pvPreviewOwner != NULL && vgl.fHavePreviewCpuFrame;
    const bool fOwnerChanged = vgl.pvPreviewOwner != pvOwner;
    if (fHadOldFrame && (fOwnerChanged || rcOld.left != xpSource || rcOld.top != ypSource ||
                         rcOld.right != xpSource + dxpSource || rcOld.bottom != ypSource + dypSource))
        InvalidatePreviewPresentationRect(&rcOld);

    vgl.pvPreviewOwner = pvOwner;
    vgl.rcPreviewSource.left = xpSource;
    vgl.rcPreviewSource.top = ypSource;
    vgl.rcPreviewSource.right = xpSource + dxpSource;
    vgl.rcPreviewSource.bottom = ypSource + dypSource;
    if (dxpOwnerSource > 0 && dypOwnerSource > 0)
    {
        vgl.rcPreviewOwnerSource.left = xpOwnerSource;
        vgl.rcPreviewOwnerSource.top = ypOwnerSource;
        vgl.rcPreviewOwnerSource.right = xpOwnerSource + dxpOwnerSource;
        vgl.rcPreviewOwnerSource.bottom = ypOwnerSource + dypOwnerSource;
    }
    else
    {
        SetRectEmpty(&vgl.rcPreviewOwnerSource);
    }
    if (fOwnerChanged)
    {
        vgl.fHavePreviewCpuFrame = false;
        // Entering a 3D Word/Costume/Action easel retires the main movie as a
        // presentation layer for the duration of that easel. Invalidate the
        // whole scaled client once so even exposed margins cannot retain a
        // stale movie frame around the modal editor.
        InvalidateScaledPresentationAll();
    }
    // Do not invalidate a stable APE before its replacement frame exists.
    // The completed cache commit below invalidates exactly once. Invalidating
    // here exposed the cleared/old surface between clicks/keystrokes and caused
    // the one-frame 3D Word flicker.
}

void BrModernPreviewDeactivate(void *pvOwner)
{
    if (pvOwner == NULL || vgl.pvPreviewOwner != pvOwner)
        return;
    if (vgl.rcPreviewSource.right > vgl.rcPreviewSource.left &&
        vgl.rcPreviewSource.bottom > vgl.rcPreviewSource.top)
        InvalidatePreviewPresentationRect(&vgl.rcPreviewSource);
    vgl.pvPreviewOwner = NULL;
    vgl.fHavePreviewCpuFrame = false;
    SetRectEmpty(&vgl.rcPreviewSource);
    SetRectEmpty(&vgl.rcPreviewOwnerSource);
    // BeginPaint clips drawing to the invalid region. Repainting only the old
    // preview hole would leave the rest of the main viewport black after the
    // easel closes, so restore the entire cached movie layer in one pass.
    InvalidateScaledPresentationAll();
}

bool FBrModernPreviewGetActiveSourceRect(int32_t *pxpSource, int32_t *pypSource,
                                         int32_t *pdxpSource, int32_t *pdypSource)
{
    if (vgl.pvPreviewOwner == NULL ||
        vgl.rcPreviewSource.right <= vgl.rcPreviewSource.left ||
        vgl.rcPreviewSource.bottom <= vgl.rcPreviewSource.top)
        return false;
    if (pxpSource != NULL)
        *pxpSource = vgl.rcPreviewSource.left;
    if (pypSource != NULL)
        *pypSource = vgl.rcPreviewSource.top;
    if (pdxpSource != NULL)
        *pdxpSource = vgl.rcPreviewSource.right - vgl.rcPreviewSource.left;
    if (pdypSource != NULL)
        *pdypSource = vgl.rcPreviewSource.bottom - vgl.rcPreviewSource.top;
    return true;
}

bool FBrModernPreviewGetOwnerSourceRect(int32_t *pxpSource, int32_t *pypSource,
                                        int32_t *pdxpSource, int32_t *pdypSource)
{
    if (vgl.pvPreviewOwner == NULL ||
        vgl.rcPreviewOwnerSource.right <= vgl.rcPreviewOwnerSource.left ||
        vgl.rcPreviewOwnerSource.bottom <= vgl.rcPreviewOwnerSource.top)
        return false;
    if (pxpSource != NULL)
        *pxpSource = vgl.rcPreviewOwnerSource.left;
    if (pypSource != NULL)
        *pypSource = vgl.rcPreviewOwnerSource.top;
    if (pdxpSource != NULL)
        *pdxpSource = vgl.rcPreviewOwnerSource.right - vgl.rcPreviewOwnerSource.left;
    if (pdypSource != NULL)
        *pdypSource = vgl.rcPreviewOwnerSource.bottom - vgl.rcPreviewOwnerSource.top;
    return true;
}

bool FBrModernPreviewGetSourceRect(int32_t *pxpSource, int32_t *pypSource,
                                   int32_t *pdxpSource, int32_t *pdypSource)
{
    if (vgl.pvPreviewOwner == NULL || !vgl.fHavePreviewCpuFrame ||
        vgl.ppreviewCpu == NULL || vgl.ppreviewCpu->pixels == NULL ||
        vgl.rcPreviewSource.right <= vgl.rcPreviewSource.left ||
        vgl.rcPreviewSource.bottom <= vgl.rcPreviewSource.top)
        return false;
    if (pxpSource != NULL)
        *pxpSource = vgl.rcPreviewSource.left;
    if (pypSource != NULL)
        *pypSource = vgl.rcPreviewSource.top;
    if (pdxpSource != NULL)
        *pdxpSource = vgl.rcPreviewSource.right - vgl.rcPreviewSource.left;
    if (pdypSource != NULL)
        *pdypSource = vgl.rcPreviewSource.bottom - vgl.rcPreviewSource.top;
    return true;
}

bool FBrModernPreviewPaintLatestFrame(void *pvHdc, int32_t xpDst, int32_t ypDst,
                                      int32_t dxpDst, int32_t dypDst)
{
    return FPaintPreviewCpu((HDC)pvHdc, xpDst, ypDst, dxpDst, dypDst);
}

void BrModernViewportClearCachedFrame(void)
{
    vgl.fHaveCompositeCpuFrame = false;
    vgl.fHaveExternalFrame = false;
    vgl.fHaveFrame = false;
    if (vgl.pcompositeCpu != NULL)
        BrPixelmapFill(vgl.pcompositeCpu, BR_COLOUR_RGB(0, 0, 0));
    if (vgl.pdisplayExternal != NULL)
        BrPixelmapFill(vgl.pdisplayExternal, BR_COLOUR_RGB(0, 0, 0));

    HWND hwndPresentation = vwig.hwndApp != NULL
                                ? (HWND)GetPropA(vwig.hwndApp, "4DMMUiScaleWindow")
                                : NULL;
    if (hwndPresentation != NULL && IsWindow(hwndPresentation))
        InvalidateRect(hwndPresentation, NULL, FALSE);
}

bool FBrModernViewportPaintLatestFrame(void *pvHdc, int32_t xpDst, int32_t ypDst,
                                       int32_t dxpDst, int32_t dypDst)
{
    HDC hdc = (HDC)pvHdc;
    if (hdc == NULL || !vfModernExternalOnly || !vgl.fHaveCompositeCpuFrame ||
        vgl.pcompositeCpu == NULL || vgl.pcompositeCpu->pixels == NULL ||
        vgl.pcompositeCpu->type != BR_PMT_RGB_888 || vgl.pcompositeCpu->row_bytes <= 0 ||
        dxpDst <= 0 || dypDst <= 0)
    {
        return false;
    }

    const int32_t dxpSrc = vgl.pcompositeCpu->width;
    const int32_t dypSrc = vgl.pcompositeCpu->height;
    if (dxpSrc <= 0 || dypSrc <= 0)
        return false;

    // BrPixelmapAllocate may pad rows, while StretchDIBits expects ordinary DIB
    // stride rules. The native 2176-pixel RGB888 row is naturally DWORD aligned
    // (6528 bytes), so the current 4x target can be passed directly. Reject any
    // future layout that does not satisfy that contract rather than drawing a
    // subtly sheared frame.
    const int32_t cbDibRow = (dxpSrc * 3 + 3) & ~3;
    if (vgl.pcompositeCpu->row_bytes != cbDibRow)
        return false;

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = dxpSrc;
    bmi.bmiHeader.biHeight = -dypSrc;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    const int iOldMode = SetStretchBltMode(hdc, COLORONCOLOR);
    const int cScan = StretchDIBits(hdc,
                                    xpDst, ypDst, dxpDst, dypDst,
                                    0, 0, dxpSrc, dypSrc,
                                    vgl.pcompositeCpu->pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
    if (iOldMode != 0)
        SetStretchBltMode(hdc, iOldMode);
    return cScan != GDI_ERROR;
}

void BrModernViewportPresent(void)
{
    if (!FBrModernViewportReady())
        return;

    if (vfModernExternalOnly)
    {
        if (vgl.fHaveExternalFrame && vgl.pdisplayExternal != NULL)
        {
            BrModernLog("viewport repaint present native external screen=%p display=%p",
                        vgl.pscreen, vgl.pdisplayExternal);
            BrPixelmapDoubleBuffer(vgl.pscreen, vgl.pdisplayExternal);
        }
        return;
    }

    if (vgl.fHaveFrame && vgl.pcolour != NULL)
    {
        BrModernLog("viewport repaint present screen=%p colour=%p", vgl.pscreen, vgl.pcolour);
        BrPixelmapDoubleBuffer(vgl.pscreen, vgl.pcolour);
    }
}

void BrModernViewportShutdown(void)
{
    BrModernLog("viewport Shutdown enter started=%d hwnd=%p hdc=%p hglrc=%p screen=%p anchor=%p colour=%p depth=%p cpu_stage=%p display=%p composite_cpu=%p",
                (int)vgl.fRendererStarted, vgl.hwnd, vgl.hdc, vgl.hglrc, vgl.pscreen,
                vgl.panchor, vgl.pcolour, vgl.pdepth, vgl.pcpuStage, vgl.pdisplayExternal, vgl.pcompositeCpu);
    if (vgl.fRendererStarted)
    {
        BrModernLog("viewport Shutdown BrRendererEnd BEGIN");
        BrRendererEnd();
        BrModernLog("viewport Shutdown BrRendererEnd returned");
        vgl.fRendererStarted = false;
    }

    FreeRenderBuffers();

    if (vgl.pdisplayExternal != NULL)
        BrPixelmapFree(vgl.pdisplayExternal);
    vgl.pdisplayExternal = NULL;
    if (vgl.pcompositeCpu != NULL)
        BrPixelmapFree(vgl.pcompositeCpu);
    vgl.pcompositeCpu = NULL;
    if (vgl.ppreviewCpu != NULL)
        BrPixelmapFree(vgl.ppreviewCpu);
    vgl.ppreviewCpu = NULL;
    if (vgl.pbPreviewDib != NULL)
        HeapFree(GetProcessHeap(), 0, vgl.pbPreviewDib);
    vgl.pbPreviewDib = NULL;
    vgl.cbPreviewDib = 0;
    vgl.cbPreviewDibRow = 0;
    vgl.pvPreviewOwner = NULL;
    vgl.fHavePreviewCpuFrame = false;
    SetRectEmpty(&vgl.rcPreviewSource);
    vgl.fHaveExternalFrame = false;
    vgl.fHaveCompositeCpuFrame = false;

    if (vgl.panchor != NULL)
        BrPixelmapFree(vgl.panchor);
    vgl.panchor = NULL;

    if (vgl.pscreen != NULL)
        BrPixelmapFree(vgl.pscreen);
    vgl.pscreen = NULL;

    vgl.hglrc = NULL;
    vgl.hdc = NULL;
    vgl.fHaveFrame = false;
    BrModernLog("viewport Shutdown complete");
}

#endif // KAUAI_WIN32 && BRENDER_MODERN_14
