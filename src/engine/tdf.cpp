/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    tdf.cpp: Three-D Font class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!


    TDFs (3-D Fonts) are simply collections of models, one model per ASCII
    character.	The TDF class holds general font information such as the
    count of characters in the font and the maximum height of the
    characters.  It also holds an array of widths and heights of every
    character, to allow proportional spacing.  Fetching a letter's model
    from a TDF involves	looking for a child chunk of the TDF chunk with a
    CHID equal to the ASCII value of the desired character:

    TDF  // Contains font info (width and height of characters)
     |
     +---BMDL (chid 0) // MODL for ASCII character 0
     |
     +---BMDL (chid 1) // MODL for ASCII character 1
     .
     .
     .

***************************************************************************/
#include "soc.h"
ASSERTNAME

RTCLASS(TDF)

const int32_t kcchTdfDefault = 256;     // 3DMMv1.0: for size estimates and authoring
const BRS kdxrSpacing = BR_SCALAR(0.0); // 3DMMv1.0: horizontal space between chars
const BRS kdyrLeading = BR_SCALAR(0.5); // 3DMMv1.0: vertical space between chars

/** 3DMMv1.0: **************************************
    3-D Font On File
****************************************/
struct TDFF
{
    int16_t bo;
    int16_t osk;
    int32_t cch;
    BRS dyrMax;
    // 3DMMv1.0: These variable-length arrays follow the TDFF in the TDF chunk
    // 3DMMv1.0:  BRS rgdxr[cch];
    // 3DMMv1.0:  BRS rgdyr[cch];
};
VERIFY_STRUCT_SIZE(TDFF, 12);
const BOM kbomTdff = 0x5F000000; // 3DMMv1.0: don't forget to swap rgdxr & rgdyr!

/** 3DMMv1.0: *************************************************************************
    A PFNRPO to read a TDF from a file.
***************************************************************************/
bool TDF::FReadTdf(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);

    TDF *ptdf;

    // 3DMMv1.0: Estimate TDF size in memory.
    if (pblck->FPacked())
        *pcb = SIZEOF(TDF) + LwMul(kcchTdfDefault, SIZEOF(BRS) + SIZEOF(BRS));
    else
        *pcb = pblck->Cb();
    if (pvNil == ppbaco)
        return fTrue;
    ptdf = NewObj TDF;
    if (pvNil == ptdf || !ptdf->_FInit(pblck))
    {
        TrashVar(ppbaco);
        TrashVar(pcb);
        ReleasePpo(&ptdf);
        return fFalse;
    }
    AssertPo(ptdf, 0);
    *pcb = SIZEOF(TDF) + LwMul(ptdf->_cch, SIZEOF(BRS) + SIZEOF(BRS));
    *ppbaco = ptdf;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Initialize the font.  Does not clean up on failure because the
    destructor will.
***************************************************************************/
bool TDF::_FInit(PBLCK pblck)
{
    AssertBaseThis(0);
    AssertPo(pblck, 0);

    TDFF tdff;
    int32_t cbrgdwr; // 3DMMv1.0: space taken by rgdxr or rgdyr

    if (!pblck->FUnpackData())
        return fFalse;
    if (pblck->Cb() < SIZEOF(TDFF))
    {
        PushErc(ercSocBadTdf);
        return fFalse;
    }
    if (!pblck->FReadRgb(&tdff, SIZEOF(TDFF), 0))
        return fFalse;
    if (kboCur != tdff.bo)
        SwapBytesBom(&tdff, kbomTdff);
    Assert(kboCur == tdff.bo, "bad TDFF");
    _cch = tdff.cch;
    cbrgdwr = LwMul(_cch, SIZEOF(BRS));
    if (pblck->Cb() != SIZEOF(TDFF) + cbrgdwr + cbrgdwr)
    {
        PushErc(ercSocBadTdf);
        return fFalse;
    }
    _dyrMax = tdff.dyrMax;

    // 3DMMv1.0: Read _prgdxr
    if (!FAllocPv((void **)&_prgdxr, cbrgdwr, fmemNil, mprNormal))
        return fFalse;
    if (!pblck->FReadRgb(_prgdxr, cbrgdwr, SIZEOF(TDFF)))
        return fFalse;
    AssertBomRglw(kbomBrs, SIZEOF(BRS));
    if (kboCur != tdff.bo)
        SwapBytesRglw(_prgdxr, _cch);

    // 3DMMv1.0: Read _prgdyr
    if (!FAllocPv((void **)&_prgdyr, cbrgdwr, fmemNil, mprNormal))
        return fFalse;
    if (!pblck->FReadRgb(_prgdyr, cbrgdwr, SIZEOF(TDFF) + cbrgdwr))
        return fFalse;
    AssertBomRglw(kbomBrs, SIZEOF(BRS));
    if (kboCur != tdff.bo)
        SwapBytesRglw(_prgdyr, _cch);

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    TDF destructor
***************************************************************************/
TDF::~TDF(void)
{
    AssertBaseThis(0);

    FreePpv((void **)&_prgdxr);
    FreePpv((void **)&_prgdyr);
}

/** 3DMMv1.0: *************************************************************************
    This authoring-only API creates a new TDF chunk in pcrf, with child
    models as specified in pglkid.  This function does not create a new
    TDF instance in memory...to do that, call FReadTdf with the values
    returned in pckiTdf.
***************************************************************************/
bool TDF::FCreate(PCRF pcrf, PGL pglkid, STN *pstn, CKI *pckiTdf)
{
    AssertPo(pcrf, 0);
    AssertPo(pglkid, 0);
    AssertPo(pstn, 0);
    AssertNilOrVarMem(pckiTdf);

    CKI ckiTdf;
    KID kid;
    KID kid2;
    TDFF tdff;
    BRS *prgdxr = pvNil;
    BRS *prgdyr = pvNil;
    PMODL pmodl;
    BLCK blck;
    int32_t cbrgdwr; // 3DMMv1.0: space taken by rgdxr or rgdyr
    int32_t ikid;
    int32_t ckid;
    CHID chidMax = 0;
    int32_t ikidLetteri = -1;

    // 3DMMv1.0: Find chidMax
    ckid = pglkid->IvMac();
    for (ikid = 0; ikid < ckid; ikid++)
    {
        pglkid->Get(ikid, &kid);
        if (kid.chid > chidMax)
            chidMax = kid.chid;
        if (kid.chid == (CHID)ChLit('i'))
            ikidLetteri = ikid;
    }

    tdff.bo = kboCur;
    tdff.osk = koskCur;
    tdff.cch = chidMax + 1;
    tdff.dyrMax = rZero;
    cbrgdwr = LwMul(tdff.cch, SIZEOF(BRS));
    if (!FAllocPv((void **)&prgdxr, cbrgdwr, fmemClear, mprNormal))
        goto LFail;
    if (!FAllocPv((void **)&prgdyr, cbrgdwr, fmemClear, mprNormal))
        goto LFail;

    // 3DMMv1.0: Create the TDF chunk
    ckiTdf.ctg = kctgTdf;
    if (!pcrf->Pcfl()->FAdd(SIZEOF(TDFF) + cbrgdwr + cbrgdwr, ckiTdf.ctg, &ckiTdf.cno, &blck))
    {
        goto LFail;
    }

    // 3DMMv1.0: Add the BMDL kids and remember widths, heights, and maximum height
    for (ikid = 0; ikid < ckid; ikid++)
    {
        pglkid->Get(ikid, &kid);
        pmodl = (PMODL)pcrf->PbacoFetch(kid.cki.ctg, kid.cki.cno, MODL::FReadModl);
        if (pmodl == pvNil)
            goto LFail;
        if (!pcrf->Pcfl()->FAdoptChild(ckiTdf.ctg, ckiTdf.cno, kid.cki.ctg, kid.cki.cno, kid.chid))
        {
            goto LFail;
        }
        if (pmodl->Dxr() == 0 && kid.chid == (CHID)ChLit(' ') && ikidLetteri != -1)
        {
            // 3DMMv1.0: Hack to turn null models into space characters:
            // 3DMMv1.0: space is the width and height of an "i"
            ReleasePpo(&pmodl);
            pglkid->Get(ikidLetteri, &kid2);
            pmodl = (PMODL)pcrf->PbacoFetch(kid2.cki.ctg, kid2.cki.cno, MODL::FReadModl);
            if (pvNil == pmodl)
                goto LFail;
        }
        prgdxr[kid.chid] = pmodl->Dxr() + kdxrSpacing;
        prgdyr[kid.chid] = pmodl->Dyr() + kdyrLeading;
        if (prgdyr[kid.chid] > tdff.dyrMax)
            tdff.dyrMax = prgdyr[kid.chid];
        ReleasePpo(&pmodl);
    }
    if (!blck.FWriteRgb(&tdff, SIZEOF(TDFF), 0))
        goto LFail;
    if (!blck.FWriteRgb(prgdxr, cbrgdwr, SIZEOF(TDFF)))
        goto LFail;
    if (!blck.FWriteRgb(prgdyr, cbrgdwr, SIZEOF(TDFF) + cbrgdwr))
        goto LFail;
    FreePpv((void **)&prgdxr);
    FreePpv((void **)&prgdyr);
    if (pvNil != pckiTdf)
        *pckiTdf = ckiTdf;
    return fTrue;
LFail:
    FreePpv((void **)&prgdxr);
    FreePpv((void **)&prgdyr);
    TrashVar(pckiTdf);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Get a model for a character from the font.  The chid is equal to the
    ASCII (or Unicode) value of the desired character.
***************************************************************************/
PMODL TDF::PmodlFetch(CHID chid)
{
    AssertThis(0);

    KID kid;

    if (!Pcrf()->Pcfl()->FGetKidChid(Ctg(), Cno(), chid, &kid))
    {
        STN stn;
        stn.FFormatSz(PszLit("Couldn't find BMDL for 3-D Font with chid %d."), chid);
        Warn(stn.Psz());
        PushErc(ercSocNoModlForChar);
        return pvNil;
    }
    PMODL pmodl = (PMODL)Pcrf()->PbacoFetch(kid.cki.ctg, kid.cki.cno, MODL::FReadModl);
#if defined(BRENDER_MODERN_14)
    if (pmodl != pvNil)
    {
        PBMDL pbmdl = pmodl->Pbmdl();
        if (pbmdl != pvNil && pbmdl->prepared != pvNil &&
            (pbmdl->pivot.v[0] != rZero || pbmdl->pivot.v[1] != rZero || pbmdl->pivot.v[2] != rZero))
        {
            // TDF BMDLs on disk are already BRender-1.x *prepared* glyphs.
            // Their serialized vertex positions already include the glyph
            // centering/variable-spacing adjustment performed by
            // MODL::AdjustTdfCharacter().  Our BR1.4 bridge necessarily loads
            // those prepared vertices as source vertices and re-prepares them;
            // ordinary BrModelUpdate then subtracts the serialized pivot a
            // second time.  That moves each glyph by a different amount even
            // though TDT's character advances are correct, producing the
            // irregular inter-letter spacing visible only in Modern BRender.
            //
            // Rebuild the Modern private prepared/stored geometry once with a
            // zero pivot, then restore the public legacy pivot without another
            // update.  3DMM keeps seeing its original model metadata while
            // glrend receives the exact legacy prepared glyph coordinates.
            br_vector3 bvec3Pivot = pbmdl->pivot;
            BrVector3Set(&pbmdl->pivot, rZero, rZero, rZero);
            BrModelUpdate(pbmdl, BR_MODU_PIVOT);
            pbmdl->pivot = bvec3Pivot;
            pmodl->MarkLegacyTdfPivotBridge();

            static int32_t cdiagPivotFix = 0;
            if (cdiagPivotFix < 96)
            {
                BrModernLog("TDF::PmodlFetch legacy prepared pivot bridge chid=%ld model=%p pivot=(%.5f,%.5f,%.5f)",
                            (long)chid, pbmdl,
                            (double)BrScalarToFloat(bvec3Pivot.v[0]),
                            (double)BrScalarToFloat(bvec3Pivot.v[1]),
                            (double)BrScalarToFloat(bvec3Pivot.v[2]));
                cdiagPivotFix++;
            }
        }
    }
#endif
    return pmodl;
}

/** 3DMMv1.0: *************************************************************************
    Return the width of the given character
***************************************************************************/
BRS TDF::DxrChar(int32_t ich)
{
    AssertThis(0);
    AssertIn(ich, 0, _cch);

    return _prgdxr[ich];
}

/** 3DMMv1.0: *************************************************************************
    Return the height of the given character
***************************************************************************/
BRS TDF::DyrChar(int32_t ich)
{
    AssertThis(0);
    AssertIn(ich, 0, _cch);

    return _prgdyr[ich];
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of the TDF.
***************************************************************************/
void TDF::AssertValid(uint32_t grf)
{
    TDF_PAR::AssertValid(fobjAllocated);
    AssertIn(_cch, 0, klwMax);
    AssertIn(_dyrMax, 0, BR_SCALAR_MAX);
    AssertPvCb(_prgdxr, LwMul(_cch, SIZEOF(BRS)));
    AssertPvCb(_prgdyr, LwMul(_cch, SIZEOF(BRS)));
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the TDF
***************************************************************************/
void TDF::MarkMem(void)
{
    AssertThis(0);
    TDF_PAR::MarkMem();
    MarkPv(_prgdxr);
    MarkPv(_prgdyr);
}
#endif // 3DMMv1.0: DEBUG
