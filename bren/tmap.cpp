/** 3DMMv1.0: *************************************************************************

    Texture map (br_pixmap wrapper) class

***************************************************************************/
#include "bren.h"

ASSERTNAME

RTCLASS(TMAP)

/** 3DMMv1.0: *************************************************************************
    A PFNRPO to read TMAP objects.
***************************************************************************/
bool TMAP::FReadTmap(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);

    PTMAP ptmap;

    *pcb = pblck->Cb(fTrue);
    if (pvNil == ppbaco)
        return fTrue;
    ptmap = PtmapRead(pcrf->Pcfl(), ctg, cno);
    if (pvNil == ptmap)
    {
        TrashVar(ppbaco);
        TrashVar(pcb);
        return fFalse;
    }
    AssertPo(ptmap, 0);
    *ppbaco = ptmap;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Read a TMAP from a chunk
***************************************************************************/
PTMAP TMAP::PtmapRead(PCFL pcfl, CTG ctg, CNO cno, bool fKeepIndexed)
{
#if defined(BRENDER_MODERN_14)
    BrModernLog("TMAP::PtmapRead BEGIN ctg=0x%08lX cno=0x%08lX keep_indexed=%d",
                (unsigned long)ctg, (unsigned long)cno, (int)fKeepIndexed);
#endif
    TMAPF tmapf;
    BLCK blck;
    TMAP *ptmap;

    ptmap = NewObj TMAP;
    if (pvNil == ptmap)
        goto LFail;

    if (!pcfl->FFind(ctg, cno, &blck) || !blck.FUnpackData())
        goto LFail;
    if (!blck.FReadRgb(&tmapf, SIZEOF(TMAPF), 0))
        goto LFail;

    if (kboCur != tmapf.bo)
        SwapBytesBom(&tmapf, kbomTmapf);
    Assert(kboCur == tmapf.bo, "bad TMAPF");

#if defined(BRENDER_MODERN_14)
    // TMAP embeds an application-owned memory pixelmap.  Explicitly initialise
    // the post-1995 fields so glrend never mistakes it for a device pixelmap.
    ptmap->_bpmp._reserved = 0;
    ptmap->_bpmp.mip_offset = 0;
    ptmap->_bpmp.user = pvNil;
    ptmap->_bpmp.stored = pvNil;
#endif
    ptmap->_bpmp.identifier = (char *)ptmap; // 3DMMv1.0: to get TMAP from a (BPMP *)
    if (!FAllocPv((void **)&ptmap->_bpmp.pixels, LwMul(tmapf.cbRow, tmapf.dyp), fmemClear, mprNormal))
    {
        goto LFail;
    }
    ptmap->_bpmp.map = pvNil;
    ptmap->_bpmp.row_bytes = tmapf.cbRow;
    ptmap->_bpmp.type = tmapf.type;
    ptmap->_bpmp.flags = tmapf.grftmap;
    ptmap->_bpmp.base_x = tmapf.xpLeft;
    ptmap->_bpmp.base_y = tmapf.ypTop;
    ptmap->_bpmp.width = tmapf.dxp;
    ptmap->_bpmp.height = tmapf.dyp;
    ptmap->_bpmp.origin_x = tmapf.xpOrigin;
    ptmap->_bpmp.origin_y = tmapf.ypOrigin;

    if (!blck.FReadRgb(ptmap->_bpmp.pixels, LwMul(tmapf.cbRow, tmapf.dyp), SIZEOF(TMAPF)))
    {
        goto LFail;
    }

    // The bundled 1995 BRender library has smooth RGB888 primitives for
    // solid materials and an unlit RGB888 texture primitive, but it does not
    // contain the later indexed-texture + RGB888-shade-table primitive.
    // Convert ordinary colour maps to RGB888 at runtime; the global shade
    // table is explicitly requested with fKeepIndexed and stays untouched.
    if (BWLD::FTrueColorMode() && !fKeepIndexed && !ptmap->_FConvertToRgb888())
        goto LFail;
#if defined(BRENDER_MODERN_14)
    BrModernLog("TMAP::PtmapRead SUCCESS tmap=%p bpmp=%p type=%u pixels=%p row=%ld wh=%ux%u flags=0x%04X stored=%p",
                ptmap, &ptmap->_bpmp, (unsigned)ptmap->_bpmp.type, ptmap->_bpmp.pixels,
                (long)ptmap->_bpmp.row_bytes, (unsigned)ptmap->_bpmp.width, (unsigned)ptmap->_bpmp.height,
                (unsigned)ptmap->_bpmp.flags, ptmap->_bpmp.stored);
#endif
    return ptmap;
LFail:
#if defined(BRENDER_MODERN_14)
    BrModernLog("TMAP::PtmapRead FAIL ctg=0x%08lX cno=0x%08lX", (unsigned long)ctg, (unsigned long)cno);
#endif
    ReleasePpo(&ptmap);
    return pvNil;
}

/** 3DMMv1.0: *************************************************************************
    Create a TMAP from a BRender BPMP...used only for importing PIX's
***************************************************************************/
PTMAP TMAP::PtmapNewFromBpmp(BPMP *pbpmp)
{
    PTMAP ptmap;

    ptmap = NewObj TMAP;
    if (pvNil == ptmap)
        return pvNil;
    ptmap->_bpmp = *pbpmp;
    ptmap->_bpmp.identifier = (char *)ptmap;
    pbpmp->identifier = (char *)ptmap;
    ptmap->_fImported = fTrue;
    return ptmap;
}

/***************************************************************************
    Expand an indexed colour map through 3DMM's active palette into BRender's
    B,G,R RGB888 byte layout.  This is a runtime representation only.
***************************************************************************/
bool TMAP::_FConvertToRgb888(void)
{
    AssertBaseThis(0);

    if (_bpmp.type == BR_PMT_RGB_888)
        return fTrue;
    if (_bpmp.type != BR_PMT_INDEX_8 || _bpmp.pixels == pvNil || _bpmp.width == 0 || _bpmp.height == 0)
        return fFalse;

    PGL pglclr = GPT::PglclrGetPalette();
    if (pglclr == pvNil || pglclr->IvMac() < 256)
    {
        ReleasePpo(&pglclr);
        return fFalse;
    }

    const int32_t cbRowSrc = LwAbs(_bpmp.row_bytes);
    const int32_t cbRowDst = (LwMul(_bpmp.width, 3) + 3) & ~3;
    const uint8_t *prgbSrc = (const uint8_t *)_bpmp.pixels;
    uint8_t *prgbNew = pvNil;
    if (!FAllocPv((void **)&prgbNew, LwMul(cbRowDst, _bpmp.height), fmemClear, mprNormal))
    {
        ReleasePpo(&pglclr);
        return fFalse;
    }

    for (int32_t yp = 0; yp < _bpmp.height; yp++)
    {
        const uint8_t *prgbSrcRow = prgbSrc + LwMul(yp, cbRowSrc);
        uint8_t *prgbDstRow = prgbNew + LwMul(yp, cbRowDst);
        for (int32_t xp = 0; xp < _bpmp.width; xp++)
        {
            CLR clr;
            pglclr->Get(prgbSrcRow[xp], &clr);
            prgbDstRow[LwMul(xp, 3) + 0] = clr.bBlue;
            prgbDstRow[LwMul(xp, 3) + 1] = clr.bGreen;
            prgbDstRow[LwMul(xp, 3) + 2] = clr.bRed;
        }
    }

    FreePpv((void **)&_bpmp.pixels);
    _bpmp.pixels = prgbNew;
    _bpmp.row_bytes = (br_int_16)cbRowDst;
    _bpmp.type = BR_PMT_RGB_888;
    _bpmp.map = pvNil;
    ReleasePpo(&pglclr);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    destructor
***************************************************************************/
TMAP::~TMAP(void)
{
    if (_fImported)
    {
        // 3DMMv1.0: REVIEW *****: this crashes BRender...why?
        // 3DMMv1.0:		BrMemFree(_bpmp.pixels);
    }
    else
        FreePpv((void **)&_bpmp.pixels);
}

/** 3DMMv1.0: *************************************************************************
    Write a TMAP to a chunk
***************************************************************************/
bool TMAP::FWrite(PCFL pcfl, CTG ctg, CNO *pcno)
{
    AssertThis(0);
    BLCK blck;

    if (!pcfl->FAdd(SIZEOF(TMAPF) + LwMul(_bpmp.row_bytes, _bpmp.height), ctg, pcno, &blck))
    {
        return fFalse;
    }

    return FWrite(&blck);
}

/** 3DMMv1.0: *************************************************************************
    Write a TMAP to the given FLO
***************************************************************************/
bool TMAP::FWrite(PBLCK pblck)
{
    TMAPF tmapf;

    tmapf.bo = kboCur;
    tmapf.osk = koskCur;
    tmapf.cbRow = _bpmp.row_bytes;
    tmapf.type = _bpmp.type;
    tmapf.grftmap = _bpmp.flags;
    tmapf.xpLeft = _bpmp.base_x;
    tmapf.ypTop = _bpmp.base_y;
    tmapf.dxp = _bpmp.width;
    tmapf.dyp = _bpmp.height;
    tmapf.xpOrigin = _bpmp.origin_x;
    tmapf.ypOrigin = _bpmp.origin_y;
    if (!pblck->FWriteRgb(&tmapf, SIZEOF(TMAPF), 0))
        return fFalse;
    if (!pblck->FWriteRgb(_bpmp.pixels, LwMul(tmapf.cbRow, tmapf.dyp), SIZEOF(TMAPF)))
    {
        return fFalse;
    }
    return fTrue;
}

#ifndef MAC

#define CALCDIST(bRed1, bGreen1, bBlue1, bRed2, bGreen2, bBlue2)                                                       \
    (((bRed1) - (bRed2)) * ((bRed1) - (bRed2)) + ((bGreen1) - (bGreen2)) * ((bGreen1) - (bGreen2)) +                   \
     ((bBlue1) - (bBlue2)) * ((bBlue1) - (bBlue2)))

/* 3DMMv1.0:
 *	PtmapReadNative	--	Creates a TMAP object, reading the data from a .BMP file
 *
 *	input:
 *			pfni	--	the FNI to read the data from
 *			pglclr	--	the colors to map to.
 *
 *	output:
 *			returns the pointer to the new TMAP
 */
PTMAP TMAP::PtmapReadNative(FNI *pfni, PGL pglclr)
{
    uint8_t *prgb = pvNil;
    PTMAP ptmap = pvNil;
    int32_t dxp, dyp;
    bool fUpsideDown;
    int32_t iclrBest, igl;
    int32_t iprgb;
    int32_t dist, min;
    CLR clr, clrSrc;
    PGL pglclrSrc;
    PGL pglCache;

    AssertPo(pfni, 0);

    if (FReadBitmap(pfni, &prgb, &pglclrSrc, &dxp, &dyp, &fUpsideDown))
    {
        Assert(!fUpsideDown, 0);
        AssertPo(pglclrSrc, 0);

        if (pglclr != pvNil)
        {
            AssertIn(pglclr->IvMac(), 0, 257);

            //
            // 3DMMv1.0: Do a closest color match
            //

            pglCache = GL::PglNew(SIZEOF(int32_t), pglclrSrc->IvMac());

            if (pglCache != pvNil)
            {

                iclrBest = ivNil;
                for (igl = 0; igl < pglclrSrc->IvMac(); igl++)
                {
                    AssertDo(pglCache->FAdd(&iclrBest), "Ensured by creation");
                }
            }

            for (iprgb = 0; iprgb < (dxp * dyp); iprgb++)
            {

                if (pglCache != pvNil)
                {
                    pglCache->Get(prgb[iprgb], &iclrBest);

                    if (iclrBest != ivNil)
                    {
                        prgb[iprgb] = (uint8_t)iclrBest;
                        continue;
                    }
                }

                pglclrSrc->Get(prgb[iprgb], &clrSrc);

                iclrBest = ivNil;
                min = klwMax;

                for (igl = 0; igl < pglclr->IvMac(); igl++)
                {

                    pglclr->Get(igl, &clr);
                    dist = CALCDIST(clrSrc.bRed, clrSrc.bGreen, clrSrc.bBlue, clr.bRed, clr.bGreen, clr.bBlue);

                    if (dist <= min)
                    {
                        min = dist;
                        iclrBest = igl;
                    }
                }

                if (iclrBest != ivNil)
                {
                    AssertIn(iclrBest, 0, pglclr->IvMac());

                    if (pglCache != pvNil)
                    {
                        pglCache->Put(prgb[iprgb], &iclrBest);
                    }

                    prgb[iprgb] = (uint8_t)iclrBest;
                }
            }

            ReleasePpo(&pglCache);
        }

        ReleasePpo(&pglclrSrc);

        ptmap = TMAP::PtmapNew(prgb, dxp, dyp);
        if (ptmap != pvNil && BWLD::FTrueColorMode() && !ptmap->_FConvertToRgb888())
            ReleasePpo(&ptmap);
    }

    return ptmap;
}
#else  // 3DMMEx: !MAC
PTMAP TMAP::PtmapReadNative(FNI *pfni)
{
    RawRtn(); // 3DMMv1.0: REVIEW peted: NYI
    return pvNil;
}
#endif // 3DMMv1.0: MAC

/* 3DMMv1.0:
 *	PtmapNew	--	Given pixel data and attributes, creates a new TMAP with
 *		the given information.
 *
 *	input:
 *			prgbPixels	--	the actual pixels for the TMAP
 *			pbmh		--	The bitmap header from the .BMP file
 *
 *	output:
 *			returns the pointer to the new TMAP
 */
PTMAP TMAP::PtmapNew(uint8_t *prgbPixels, int32_t dxp, int32_t dyp)
{
    PTMAP ptmap;

    Assert(dxp <= ksuMax, "bitmap too wide");
    Assert(dyp <= ksuMax, "bitmap too high");

    if ((ptmap = NewObj TMAP) != pvNil)
    {
        ptmap->_fImported = fFalse;
#if defined(BRENDER_MODERN_14)
        ptmap->_bpmp._reserved = 0;
        ptmap->_bpmp.mip_offset = 0;
        ptmap->_bpmp.user = pvNil;
        ptmap->_bpmp.stored = pvNil;
#endif
        ptmap->_bpmp.identifier = (char *)ptmap;
        ptmap->_bpmp.pixels = prgbPixels;
        ptmap->_bpmp.map = pvNil;
        ptmap->_bpmp.row_bytes = (br_int_16)dxp;
        ptmap->_bpmp.type = BR_PMT_INDEX_8;
        ptmap->_bpmp.flags = BR_PMF_LINEAR;
        ptmap->_bpmp.base_x = ptmap->_bpmp.base_y = 0;
        ptmap->_bpmp.width = (br_uint_16)dxp;
        ptmap->_bpmp.height = (br_uint_16)dyp;
        ptmap->_bpmp.origin_x = ptmap->_bpmp.origin_y = 0;
    }
    AssertPo(ptmap, 0);
#if defined(BRENDER_MODERN_14)
    if (ptmap != pvNil)
        BrModernLog("TMAP::PtmapNew tmap=%p bpmp=%p pixels=%p wh=%ldx%ld",
                    ptmap, &ptmap->_bpmp, ptmap->_bpmp.pixels, (long)dxp, (long)dyp);
#endif
    return ptmap;
}

/** 3DMMv1.0: ****************************************************************************
    FWriteTmapChkFile
        Writes a stand-alone file with a TMAP chunk in it.  The file can
        be later read in by the CHCM class with the FILE command.

    Arguments:
        PFNI pfniDst   -- FNI indicating the name of the output file
        bool fCompress -- fTrue if the chunk date is to be compressed
        PMSNK pmsnkErr -- optional message sink to direct errors to

    Returns: fTrue if the file was written successfully

************************************************************ PETED ***********/
bool TMAP::FWriteTmapChkFile(PFNI pfniDst, bool fCompress, PMSNK pmsnkErr)
{
    AssertThis(0);
    AssertPo(pfniDst, ffniFile);
    AssertNilOrPo(pmsnkErr, 0);

    bool fRet = fFalse;
    int32_t lwSig;
    PCSZ pszErr = pvNil;
    FLO flo;

    if (pvNil == (flo.pfil = FIL::PfilCreate(pfniDst)))
    {
        pszErr = PszLit("Couldn't create destination file\n");
        goto LFail;
    }
    flo.fp = SIZEOF(int32_t);
    flo.cb = CbOnFile();

    if (fCompress)
    {
        BLCK blck;

        if (!blck.FSetTemp(flo.cb) || !FWrite(&blck))
        {
            pszErr = PszLit("allocation failure\n");
            goto LFail;
        }
        if (!blck.FPackData())
            lwSig = klwSigUnpackedFile;
        else
        {
            lwSig = klwSigPackedFile;
            flo.cb = blck.Cb(fTrue);
        }
        if (!flo.pfil->FWriteRgb(&lwSig, SIZEOF(int32_t), 0) || !blck.FWriteToFlo(&flo, fTrue))
        {
            pszErr = PszLit("writing to destination file failed\n");
            goto LFail;
        }
    }
    else
    {
        lwSig = klwSigUnpackedFile;
        if (!flo.pfil->FWriteRgb(&lwSig, SIZEOF(int32_t), 0) || !FWriteFlo(&flo))
        {
            pszErr = PszLit("writing to destination file failed\n");
            goto LFail;
        }
    }

    fRet = fTrue;
LFail:
    if (pszErr != pvNil && pmsnkErr != pvNil)
        pmsnkErr->ReportLine(pszErr);
    if (!fRet && pvNil != flo.pfil)
        flo.pfil->SetTemp();
    ReleasePpo(&flo.pfil);
    return fRet;
}

#ifdef NOT_YET_REVIEWED
uint8_t *TMAP::PrgbBuildInverseTable(void)
{
    uint8_t *prgb, *prgbT, iclr;
    int32_t cbRgb;

    if (_pbpmp->type != BR_PMT_RGB_888)
        return pvNil;

    if (!FAllocPv((void **)&prgb, cbRgb = _pbpmp->height, fmemNil, mprNormal))
        return pvNil;

    for (prgbT = prgb, iclr = 0; iclr < cbRgb; prgbT++, iclr++)
        *prgbT = iclr;

    _SortInverseTable(prgb, cbRgb, BR_COLOUR_RGB(0, 0, 0), BR_COLOUR_RGB(0xFF, 0xFF, 0xFF));
    return prgb;
}

void TMAP::_SortInverseTable(uint8_t *prgb, int32_t cbRgb, BRCLR brclrLo, BRCLR brclrHi)
{
    int32_t cbRgb1 = 0, cbRgb2 = 0;
    uint8_t *prgb2, *prgbRead, bT;
    BRCLR brclrPivot = brclrLo + (brclrHi - brclrLo) / 2;
    BRCLR *pbrclr;

    prgb2 = prgb + cbRgb;
    prgbRead = prgb;
    while (cbRgb--)
    {
        pbrclr = 0; // 3DMMv1.0: pbrclr from index *prgb;
        if (*pbrclr <= brclrPivot)
        {
            prgbRead++;
            cbRgb1++;
        }
        else
        {
            bT = *prgb2;
            *--prgb2 = *prgbRead;
            *prgbRead = bT;
            cbRgb2++;
        }
    }
    if (cbRgb1 > 1)
        _SortInverseTable(prgb, cbRgb1, brclrLo, brclrPivot);
    if (cbRgb2 > 1)
        _SortInverseTable(prgb2, cbRgb2, brclrPivot + 1, brclrHi);
}
#endif // 3DMMv1.0: NOT_YET_REVIEWED

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of the TMAP.
***************************************************************************/
void TMAP::AssertValid(uint32_t grf)
{
    TMAP_PAR::AssertValid(fobjAllocated);
    if (!_fImported)
        AssertPvCb(_bpmp.pixels, LwMul(_bpmp.row_bytes, _bpmp.height));
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the TMAP.
***************************************************************************/
void TMAP::MarkMem(void)
{
    AssertThis(0);
    TMAP_PAR::MarkMem();
    if (!_fImported)
        MarkPv(_bpmp.pixels);
}
#endif // 3DMMv1.0: DEBUG
