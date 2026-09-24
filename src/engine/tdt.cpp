/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    tdt.cpp: Three-D Text class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    TDT, the 3-D Text class, is a derived class of TMPL.  Most clients
    (ACTR, MVIE, etc) can treat TDTs like regular TMPLs.  But they have
    some extra functionality and work internally very differently from
    TMPLs.  Chunkwise, all the information for a TDT is in the TMPL
    chunk or the single TDT child chunk:

    TMPL // template info
     |
     |
     +---TDT  (chid 0) // TDT-specific info (shape and tag to TDF)

    In addition to the usual TMPL fields, TDTs have a _tagTdf and a _tdts.
    _tagTdf tells what font to use for the TDT, and _tdts tells what shape
    to draw the TDT in.  TDTs on file are very small, so it is practical to
    store them in the user's document.

    Rather than fetching ACTNs and the default costume from child chunks
    of the TMPL, TDTs generate them in memory and store them in _pactnCache
    and _pmtrlDefault.  _pactnCache keeps a copy of the last requested
    action so that it doesn't have to be continuously recomputed.

    The user can change a TDT's text, shape, and/or font with FChange().
    When this happens, all the internal lists affecting the TDT's shape,
    costume, etc., are changed via _FInitLists().  After changing a TDT,
    you should call FAdjustBody on any BODYs based on that TDT.  In
    Socrates, there should only be one BODY per TDT, so this shouldn't be
    a problem.

***************************************************************************/
#include "soc.h"
ASSERTNAME

RTCLASS(TDT)

extern void DiagLogBRender(const char *pszFormat, ...);

const CHID kchidTdt = 0; // 3DMMv1.0: CHID of TDT under TMPL chunk

// 3DMMv1.0: All actions have a step size of kdwrStep, except tdaWalk
const BRS kdwrStepWalk = BR_SCALAR(1.0); // 3DMMv1.0: step size for walk action
const BRS kdwrStep = BR_SCALAR(5.0);     // 3DMMv1.0: step size for all other actions

PGST TDT::_pgstAction = pvNil;

/***************************************************************************
    Construct a TDT.
***************************************************************************/
TDT::TDT(void)
{
    ClearPb(_rgpmodlLitCache, SIZEOF(_rgpmodlLitCache));
}

/***************************************************************************
    Release actorlight28's per-character tessellated-model cache.
***************************************************************************/
void TDT::_ReleaseLitModelCache(void)
{
    for (int32_t imodl = 0; imodl < kcmodlLitCache; imodl++)
        ReleasePpo(&_rgpmodlLitCache[imodl]);
}


// True-colour 3D Text tessellation.
//
// The original glyph meshes were designed for 1995-era flat/directional
// lighting.  A large planar glyph face can therefore be represented by only a
// couple of triangles.  Gouraud lighting evaluates the lights only at those
// vertices and interpolates the resulting colours across the triangle.  With a
// point light whose projection falls inside a giant flattened glyph, all four
// corner samples can be almost identical even though the physically correct
// intensity should rise toward the middle of the face.
//
// Under -c, subdivide each TDT triangle before BRender prepares it.  This gives
// the existing point/directional lights interior vertices to evaluate, without
// changing the glyph outline, material, movie data, or actor transforms.
//
// actorlight30 reserves tessellation for the pathological giant '_' panel/floor
// only.  That case still needs two subdivision levels (1 -> 16) so a localized
// light has interior samples across the broad planar face.  Ordinary 3D Text and
// Wackydings now use their original geometry.  v29 proved that even 4x ordinary
// glyph geometry remained the dominant rendered lit-vertex cost.  -logs still
// records the source and resulting prepared face counts for the dense '_' case.
static BRV _BrvTdtMidpoint(const BRV *pbrv0, const BRV *pbrv1)
{
    BRV brv;
    ClearPb(&brv, SIZEOF(brv));

    for (int32_t iax = 0; iax < 3; iax++)
    {
        brv.p.v[iax] = BR_MUL(BR_ADD(pbrv0->p.v[iax], pbrv1->p.v[iax]),
                              BR_SCALAR(0.5));
    }
    for (int32_t iax = 0; iax < 2; iax++)
    {
        brv.map.v[iax] = BR_MUL(BR_ADD(pbrv0->map.v[iax], pbrv1->map.v[iax]),
                                BR_SCALAR(0.5));
    }

    brv.index = (uint8_t)(((int32_t)pbrv0->index + (int32_t)pbrv1->index + 1) / 2);
    brv.red = (uint8_t)(((int32_t)pbrv0->red + (int32_t)pbrv1->red + 1) / 2);
    brv.grn = (uint8_t)(((int32_t)pbrv0->grn + (int32_t)pbrv1->grn + 1) / 2);
    brv.blu = (uint8_t)(((int32_t)pbrv0->blu + (int32_t)pbrv1->blu + 1) / 2);
    return brv;
}

static void _EmitTdtTriangle(const BRV *pbrv0, const BRV *pbrv1, const BRV *pbrv2,
                             uint16_t grfsm, int32_t csub,
                             BRV *prgbrv, BRF *prgbrf,
                             int32_t *pibrv, int32_t *pibrf)
{
    if (csub > 0)
    {
        BRV brv01 = _BrvTdtMidpoint(pbrv0, pbrv1);
        BRV brv12 = _BrvTdtMidpoint(pbrv1, pbrv2);
        BRV brv20 = _BrvTdtMidpoint(pbrv2, pbrv0);

        _EmitTdtTriangle(pbrv0, &brv01, &brv20, grfsm, csub - 1,
                         prgbrv, prgbrf, pibrv, pibrf);
        _EmitTdtTriangle(&brv01, pbrv1, &brv12, grfsm, csub - 1,
                         prgbrv, prgbrf, pibrv, pibrf);
        _EmitTdtTriangle(&brv20, &brv12, pbrv2, grfsm, csub - 1,
                         prgbrv, prgbrf, pibrv, pibrf);
        _EmitTdtTriangle(&brv01, &brv12, &brv20, grfsm, csub - 1,
                         prgbrv, prgbrf, pibrv, pibrf);
        return;
    }

    int32_t ibrv = *pibrv;
    int32_t ibrf = *pibrf;

    prgbrv[ibrv] = *pbrv0;
    prgbrv[ibrv + 1] = *pbrv1;
    prgbrv[ibrv + 2] = *pbrv2;

    ClearPb(&prgbrf[ibrf], SIZEOF(BRF));
    prgbrf[ibrf].vertices[0] = (uint16_t)ibrv;
    prgbrf[ibrf].vertices[1] = (uint16_t)(ibrv + 1);
    prgbrf[ibrf].vertices[2] = (uint16_t)(ibrv + 2);
    prgbrf[ibrf].material = pvNil;
    prgbrf[ibrf].smoothing = grfsm;
    prgbrf[ibrf].flags = 0;

    *pibrv += 3;
    *pibrf += 1;
}

static PMODL _PmodlTdtLitSubdivided(PMODL pmodlSource, CHID chidModl, int32_t chGlyph)
{
    static int32_t cdiagTdtLight6 = 0;
    const int32_t kcdiagTdtLight6Max = 48;

    if (pvNil == pmodlSource)
        return pvNil;

#if defined(BRENDER_MODERN_14)
    // The old software Gouraud path needed artificial interior vertices for
    // giant '_' floors. Modern glrend lights per fragment, so that workaround
    // only bloats geometry and breaks the legacy glyph pick/selection shape.
    return pvNil;
#endif

    PBMDL pbmdl = pmodlSource->Pbmdl();
    if (pvNil == pbmdl || pvNil == pbmdl->prepared_vertices ||
        pvNil == pbmdl->prepared_faces || pbmdl->nprepared_faces <= 0)
    {
        if (cdiagTdtLight6 < kcdiagTdtLight6Max)
        {
            DiagLogBRender("tdtlight6 TDT tess SKIP chid=%ld glyph=%ld source=%p model=%p prepared_v=%ld prepared_f=%ld",
                           (long)chidModl, (long)chGlyph, pmodlSource, pbmdl,
                           pbmdl != pvNil ? (long)pbmdl->nprepared_vertices : -1L,
                           pbmdl != pvNil ? (long)pbmdl->nprepared_faces : -1L);
            cdiagTdtLight6++;
        }
        return pvNil;
    }

    // Each leaf triangle owns three vertices.  Two subdivision levels are
    // enough to provide interior lighting samples; tdtlight5 proved that the
    // tessellated replacement is the model BRender actually renders.
    const int32_t cbrfSource = pbmdl->nprepared_faces;
    const bool fDensePlanarGlyph = chGlyph == '_';
    int32_t csub = 0;
    int32_t cfacMul = 1;

    // actorlight30: v29 proved that even one subdivision level on ordinary
    // 3D Text/Wackydings still leaves rendered lit-vertex volume as the main
    // frame-time cost.  The original shading failure that required interior
    // samples was the giant flattened '_' floor/wall glyph.  Keep the dense
    // 16x tessellation only for that pathological planar case and render all
    // other glyphs with their original prepared geometry.
    if (!fDensePlanarGlyph)
        return pvNil;

    if (cbrfSource <= (ksuMax - 1) / (16 * 3))
    {
        csub = 2;
        cfacMul = 16;
    }
    else
    {
        if (cdiagTdtLight6 < kcdiagTdtLight6Max)
        {
            DiagLogBRender("tdtlight6 TDT tess SKIP_LIMIT chid=%ld glyph=%ld source=%p prepared_v=%ld prepared_f=%ld",
                           (long)chidModl, (long)chGlyph, pmodlSource,
                           (long)pbmdl->nprepared_vertices, (long)pbmdl->nprepared_faces);
            cdiagTdtLight6++;
        }
        return pvNil;
    }

    const int32_t cbrfNew = LwMul(cbrfSource, cfacMul);
    const int32_t cbrvNew = LwMul(cbrfNew, 3);
    BRV *prgbrv = pvNil;
    BRF *prgbrf = pvNil;
    PMODL pmodl = pvNil;
    int32_t ibrvNew = 0;
    int32_t ibrfNew = 0;

    if (!FAllocPv((void **)&prgbrv, LwMul(cbrvNew, SIZEOF(BRV)), fmemClear, mprNormal) ||
        !FAllocPv((void **)&prgbrf, LwMul(cbrfNew, SIZEOF(BRF)), fmemClear, mprNormal))
    {
        if (cdiagTdtLight6 < kcdiagTdtLight6Max)
        {
            DiagLogBRender("tdtlight6 TDT tess ALLOC_FAIL chid=%ld glyph=%ld source=%p sub=%ld out_v=%ld out_f=%ld",
                           (long)chidModl, (long)chGlyph, pmodlSource, (long)csub,
                           (long)cbrvNew, (long)cbrfNew);
            cdiagTdtLight6++;
        }
        goto LDone;
    }

    for (int32_t ibrf = 0; ibrf < cbrfSource; ibrf++)
    {
        BRF *pbrf = &pbmdl->prepared_faces[ibrf];
        BRV *pbrv0 = &pbmdl->prepared_vertices[pbrf->vertices[0]];
        BRV *pbrv1 = &pbmdl->prepared_vertices[pbrf->vertices[1]];
        BRV *pbrv2 = &pbmdl->prepared_vertices[pbrf->vertices[2]];

        _EmitTdtTriangle(pbrv0, pbrv1, pbrv2, pbrf->smoothing, csub,
                         prgbrv, prgbrf, &ibrvNew, &ibrfNew);
    }

    Assert(ibrvNew == cbrvNew, "TDT subdivided vertex count mismatch");
    Assert(ibrfNew == cbrfNew, "TDT subdivided face count mismatch");
    pmodl = MODL::PmodlNew(cbrvNew, prgbrv, cbrfNew, prgbrf);

    if (cdiagTdtLight6 < kcdiagTdtLight6Max)
    {
        PBMDL pbmdlNew = pmodl != pvNil ? pmodl->Pbmdl() : pvNil;
        DiagLogBRender("tdtlight6 TDT tess chid=%ld glyph=%ld source=%p src_model=%p src_v=%ld src_f=%ld sub=%ld requested_v=%ld requested_f=%ld result=%p result_model=%p prepared_v=%ld prepared_f=%ld",
                       (long)chidModl, (long)chGlyph, pmodlSource, pbmdl,
                       (long)pbmdl->nprepared_vertices, (long)pbmdl->nprepared_faces,
                       (long)csub, (long)cbrvNew, (long)cbrfNew,
                       pmodl, pbmdlNew,
                       pbmdlNew != pvNil ? (long)pbmdlNew->nprepared_vertices : -1L,
                       pbmdlNew != pvNil ? (long)pbmdlNew->nprepared_faces : -1L);
        cdiagTdtLight6++;
    }

LDone:
    FreePpv((void **)&prgbrv);
    FreePpv((void **)&prgbrf);
    return pmodl;
}


/** 3DMMv1.0: *************************************************************************
    Set the GST of action names for TDTs
***************************************************************************/
bool TDT::FSetActionNames(PGST pgstAction)
{
    AssertPo(pgstAction, 0);
    Assert(pvNil == _pgstAction, "you already set the action names");

    _pgstAction = pgstAction;
    _pgstAction->AddRef();
    return fTrue;
}

/** 3DMMv1.0: **************************************
    3-D Text On File...this gets put in
    a child chunk of a TMPL
****************************************/
struct TDTF
{
    int16_t bo;
    int16_t osk;
    int32_t tdts;
    TAGF tagTdf;
};
VERIFY_STRUCT_SIZE(TDTF, 24);
const BOM kbomTdtf = (0x5C000000 | kbomTag >> 6);

/** 3DMMv1.0: *************************************************************************
    Return a list of all tags embedded in this TDT.  Note that a
    return value of pvNil does not mean an error occurred, but simply that
    this TDT has no embedded tags.

    Actually, as currently implemented, this function only returns pvNil
    if an error occurs.  The point is, look at *pfError, not the return
    value.
***************************************************************************/
PGL TDT::PgltagFetch(PCFL pcfl, CTG ctg, CNO cno, bool *pfError)
{
    AssertPo(pcfl, 0);
    AssertVarMem(pfError);

    PGL pgltag;
    KID kid;
    BLCK blck;
    TDTF tdtf;
    TAG tdt;

    *pfError = fFalse;
    pgltag = GL::PglNew(SIZEOF(TAG));
    if (pvNil == pgltag)
        goto LFail;
    if (!pcfl->FGetKidChidCtg(ctg, cno, kchidTdt, kctgTdt, &kid))
        goto LFail;
    if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
        goto LFail;
    if (!blck.FUnpackData())
        goto LFail;
    if (blck.Cb() < SIZEOF(TDTF))
        goto LFail;
    if (!blck.FReadRgb(&tdtf, SIZEOF(TDTF), 0))
        goto LFail;
    if (kboCur != tdtf.bo)
        SwapBytesBom(&tdtf, kbomTdtf);
    Assert(kboCur == tdtf.bo, "bad TDTF");
    DeserializeTagfToTag(&tdtf.tagTdf, &tdt);
    if (!pgltag->FAdd(&tdt))
        goto LFail;
    return pgltag;
LFail:
    *pfError = fTrue;
    ReleasePpo(&pgltag);
    return pvNil;
}

/** 3DMMv1.0: *************************************************************************
    Create a new TDT
***************************************************************************/
PTDT TDT::PtdtNew(PSTN pstn, int32_t tdts, PTAG ptagTdf)
{
    AssertPo(pstn, 0);
    AssertIn(tdts, 0, tdtsLim);
    AssertVarMem(ptagTdf);

    PTDT ptdt;

    ptdt = NewObj TDT;
    if (pvNil == ptdt)
        return pvNil;
    ptdt->_stn = *pstn;

    // 3DMMv1.0: This is a bit of a hack, but it makes life easier.  Without it,
    // 3DMMv1.0: the code would have to deal with body part sets with no body
    // 3DMMv1.0: parts in them, and TMPL and BODY would have problems with that
    // 3DMMv1.0: (for one thing, they would compute _cbset incorrectly).
    if (ptdt->_stn.Cch() == 0)
    {
        achar chSpace = ChLit(' ');
        ptdt->_stn.SetRgch(&chSpace, 1);
    }

    ptdt->_tdts = tdts;
    ptdt->_tagTdf = *ptagTdf;
    if (!ptdt->_FInitLists())
    {
        ReleasePpo(&ptdt);
        return pvNil;
    }
    AssertPo(ptdt, 0);

    return ptdt;
}

/** 3DMMv1.0: *************************************************************************
    Read the generic TMPL info and the TDT-specific info (tdts and tagTdf),
    then call _FInitLists to build the rest of the TDT.
***************************************************************************/
bool TDT::_FInit(PCFL pcfl, CTG ctgTmpl, CNO cnoTmpl)
{
    AssertBaseThis(0);
    AssertPo(pcfl, 0);

    KID kid;
    BLCK blck;
    TDTF tdtf;
    TAG tdt;

    if (!_FReadTmplf(pcfl, ctgTmpl, cnoTmpl))
        return fFalse;
    if (!pcfl->FGetKidChidCtg(ctgTmpl, cnoTmpl, kchidTdt, kctgTdt, &kid))
        return fFalse;
    if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
        return fFalse;
    if (!blck.FUnpackData())
        return fFalse;
    if (blck.Cb() < SIZEOF(TDTF))
        return fFalse;
    if (!blck.FReadRgb(&tdtf, SIZEOF(TDTF), 0))
        return fFalse;
    if (kboCur != tdtf.bo)
        SwapBytesBom(&tdtf, kbomTdtf);
    Assert(kboCur == tdtf.bo, "bad TDTF");
    DeserializeTagfToTag(&tdtf.tagTdf, &tdt);
    _tagTdf = tdt;
    _tdts = tdtf.tdts;

    if (!_FInitLists())
        return fFalse;

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Initialize or reinitialize the TDT data structures, using the current
    _stn and _tdts.  This function unwinds completely on failure (the
    TDT's members are untouched).
***************************************************************************/
bool TDT::_FInitLists(void)
{
    AssertBaseThis(0);
    AssertPo(&_stn, 0);
    AssertIn(_tdts, 0, tdtsLim);

    int32_t cch;
    PGL pglibactParNew = pvNil;
    PGL pglibsetNew = pvNil;
    PGG pggcmidNew = pvNil;
    PMTRL pmtrlDefaultNew = pvNil;

    pglibactParNew = _PglibactParBuild();
    if (pvNil == pglibactParNew)
        goto LFail;
    pglibsetNew = _PglibsetBuild();
    if (pvNil == pglibsetNew)
        goto LFail;
    pggcmidNew = _PggcmidBuild();
    if (pvNil == pggcmidNew)
        goto LFail;
    cch = _stn.Cch();
    if (pvNil == _pmtrlDefault)
    {
        pmtrlDefaultNew = MTRL::PmtrlNew(); // 3DMMv1.0: get default solid-color material
    }
    else
    {
        pmtrlDefaultNew = _pmtrlDefault; // 3DMMv1.0: keep _pmtrlDefault the same
        pmtrlDefaultNew->AddRef();
    }
    if (pvNil == pmtrlDefaultNew)
        goto LFail;

    // 3DMMv1.0: We're home free: the function will succeed. Update member variables.
    ReleasePpo(&_pglibactPar);
    _pglibactPar = pglibactParNew;
    ReleasePpo(&_pglibset);
    _pglibset = pglibsetNew;
    ReleasePpo(&_pggcmid);
    _pggcmid = pggcmidNew;
    ReleasePpo(&_pmtrlDefault);
    _pmtrlDefault = pmtrlDefaultNew;
    ReleasePpo(&_pactnCache);
    _tdaCache = tdaNil;
    _cactn = tdaLim;
    _ccmid = 1;
    _cbset = 1;
    _grftmpl |= ftmplTdt;

    // actorlight28: any successful TDT text/font/shape reinitialization makes
    // the character-position tessellation cache stale.  Clear it only after
    // the new lists have succeeded so FChange() failure still leaves the old
    // TDT and its cache untouched.
    _ReleaseLitModelCache();

    return fTrue;
LFail:
    ReleasePpo(&pglibactParNew);
    ReleasePpo(&pglibsetNew);
    ReleasePpo(&pggcmidNew);
    ReleasePpo(&pmtrlDefaultNew);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Get the given action.  If we've built it before, use the cached copy.
    Else build the action, cache it, and return it.
***************************************************************************/
PACTN TDT::_PactnFetch(int32_t tda)
{
    AssertThis(0);
    AssertIn(tda, 0, tdaLim);

    if (tda != _tdaCache)
    {
        ReleasePpo(&_pactnCache);
        _tdaCache = tdaNil;
        _pactnCache = _PactnBuild(tda);
    }
    if (pvNil != _pactnCache)
    {
        _pactnCache->AddRef();
        _tdaCache = tda;
    }
    return _pactnCache;
}

/** 3DMMv1.0: *************************************************************************
    Build the given action
***************************************************************************/
PACTN TDT::_PactnBuild(int32_t tda)
{
    AssertThis(0);
    AssertIn(tda, 0, tdaLim);

    PACTN pactn;
    PGG pggcel;
    PGL pglbmat34 = pvNil;
    uint32_t grfactn;

    pggcel = _PggcelBuild(tda);
    if (pvNil == pggcel)
        goto LFail;
    pglbmat34 = _Pglbmat34Build(tda);
    if (pvNil == pglbmat34)
        goto LFail;
    grfactn = factnStatic | factnRotateY;
    pactn = ACTN::PactnNew(pggcel, pglbmat34, grfactn);
    if (pvNil == pactn)
        goto LFail;
    ReleasePpo(&pggcel);
    ReleasePpo(&pglbmat34);
    return pactn;
LFail:
    ReleasePpo(&pggcel);
    ReleasePpo(&pglbmat34);
    return pvNil;
}

/** 3DMMv1.0: *************************************************************************
    Get the name of the given action
***************************************************************************/
bool TDT::FGetActnName(int32_t anid, PSTN pstn)
{
    AssertThis(0);
    AssertIn(anid, 0, tdaLim);
    AssertPo(pstn, 0);

    int32_t istn;
    int32_t anidT;

    for (istn = 0; istn < _pgstAction->IvMac(); istn++)
    {
        _pgstAction->GetExtra(istn, &anidT);
        if (anid == anidT)
        {
            _pgstAction->GetStn(istn, pstn);
            return fTrue;
        }
    }
    Warn("action name not found");
    pstn->SetNil();
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Fetch the given model for this TDT (use the TDT's current font)
***************************************************************************/
PMODL TDT::_PmodlFetch(CHID chidModl)
{
    AssertThis(0);
    AssertIn(chidModl, 0, _stn.Cch());

    PTDF ptdf;
    PMODL pmodl;

    // actorlight28: the tessellated true-colour glyph is invariant for this
    // TDT character position until FChange() rebuilds the TDT.  Returning the
    // cached actor-local MODL here prevents _PmodlTdtLitSubdivided() from
    // constructing and BrModelAdd'ing the same generated geometry every cel.
    if (BWLD::FTrueColorMode() && chidModl >= 0 && chidModl < kcmodlLitCache &&
        _rgpmodlLitCache[chidModl] != pvNil)
    {
        _rgpmodlLitCache[chidModl]->AddRef();
        return _rgpmodlLitCache[chidModl];
    }

    ptdf = (PTDF)vptagm->PbacoFetch(&_tagTdf, TDF::FReadTdf);
    if (pvNil == ptdf)
        return pvNil;
    pmodl = ptdf->PmodlFetch((uchar)_stn.Psz()[chidModl]);
    ReleasePpo(&ptdf);

    // tdtlight6: the RGB888 Gouraud probe showed the flattened underscore
    // reaching the rasterizer as only four huge visible triangles whose corner
    // lighting values were essentially identical (212/213).  That proves the
    // rasterizer is receiving the flat result; it is not losing a gradient.
    // Give BRender interior lighting samples by tessellating TDT glyph meshes
    // in true-colour mode.
    if (pvNil != pmodl && BWLD::FTrueColorMode())
    {
        PMODL pmodlSubdivided = _PmodlTdtLitSubdivided(
            pmodl, chidModl, (int32_t)_stn.Psz()[chidModl]);
        if (pvNil != pmodlSubdivided)
        {
            ReleasePpo(&pmodl);
            pmodl = pmodlSubdivided;

            // Cache owns one reference; the current caller owns the reference
            // already returned by PmodlNew().  Each TDT therefore keeps its own
            // prepared BRender model even when another TDT uses the same glyph.
            if (chidModl >= 0 && chidModl < kcmodlLitCache)
            {
                Assert(_rgpmodlLitCache[chidModl] == pvNil, "TDT lit model cache collision");
                pmodl->AddRef();
                _rgpmodlLitCache[chidModl] = pmodl;
            }
        }
    }

    return pmodl;
}

/** 3DMMv1.0: *************************************************************************
    Build the BACT tree GL for BODY creation.  TDTs all have the same
    body part tree: every part is a child of the root.
***************************************************************************/
PGL TDT::_PglibactParBuild(void)
{
    AssertBaseThis(0);

    int32_t cch = _stn.Cch();
    int32_t ich;
    int16_t ibactPar = ivNil;
    PGL pglibactPar;

    pglibactPar = GL::PglNew(SIZEOF(int16_t), cch); // 3DMMv1.0: ibacts are shorts
    if (pvNil == pglibactPar)
        return pvNil;
    AssertDo(pglibactPar->FSetIvMac(cch), "PglNew should have ensured space!");
    for (ich = 0; ich < cch; ich++)
        pglibactPar->Put(ich, &ibactPar);
    return pglibactPar;
}

/** 3DMMv1.0: *************************************************************************
    Build the body part set GL for BODY creation.  For TDTs, all body parts
    belong to a single body part set
***************************************************************************/
PGL TDT::_PglibsetBuild(void)
{
    AssertBaseThis(0);

    int32_t cch = _stn.Cch();
    int32_t ich;
    int16_t ibset = 0;
    PGL pglibset;

    pglibset = GL::PglNew(SIZEOF(int16_t), cch);
    if (pvNil == pglibset)
        return pvNil;
    AssertDo(pglibset->FSetIvMac(cch), "PglNew should have ensured space!");
    for (ich = 0; ich < cch; ich++)
        pglibset->Put(ich, &ibset);
    return pglibset;
}

/** 3DMMv1.0: *************************************************************************
    Build the costume GG for TMPL creation.  For TDTs, the costume is
    simple: all body part sets get cmid 0.
***************************************************************************/
PGG TDT::_PggcmidBuild(void)
{
    AssertBaseThis(0);

    int32_t cch = _stn.Cch();
    int32_t lwOne = 1;
    PGG pggcmid;
    int32_t cmid = 0;

    pggcmid = GG::PggNew(SIZEOF(int32_t), 1, SIZEOF(int32_t));
    if (pvNil == pggcmid)
        return pvNil;
    if (!pggcmid->FAdd(SIZEOF(int32_t), pvNil, &cmid, &lwOne))
    {
        ReleasePpo(&pggcmid);
        return pvNil;
    }
    return pggcmid;
}

/** 3DMMv1.0: *************************************************************************
    Build a GL of matrices for the action
***************************************************************************/
PGL TDT::_Pglbmat34Build(int32_t tda)
{
    AssertBaseThis(0);
    AssertIn(tda, 0, tdaLim);

    PTDF ptdf;
    int32_t cch = _stn.Cch();
    int32_t ich;
    BMAT34 bmat34;
    PGL pglbmat34 = pvNil;
    BRS dxrTotal;     // 3DMMv1.0: width of string (before scaling)
    BRS dxrTotal2;    // 3DMMv1.0: width of string (after scaling)
    BRS dxrHalf;      // 3DMMv1.0: half width of string (before scaling)
    BRS xrChar;       // 3DMMv1.0: "insertion point" of character in string
    BRS xrCharCenter; // 3DMMv1.0: origin of character in string (after scaling)
    BRS dxrChar;      // 3DMMv1.0: width of each character
    int32_t ccel;
    int32_t icel;
    BRS dyrChar;  // 3DMMv1.0: height of each character
    BRS dyrTotal; // 3DMMv1.0: total height of string, if vertical shape
    BRS dyrHalf;  // 3DMMv1.0: half of height of string, if vertical shape
    BRS yrChar;   // 3DMMv1.0: position of char in string

    ptdf = (PTDF)vptagm->PbacoFetch(&_tagTdf, TDF::FReadTdf);
    if (pvNil == ptdf)
        goto LFail;

    ccel = _CcelOfTda(tda);

    pglbmat34 = GL::PglNew(SIZEOF(BMAT34), LwMul(ccel, cch));
    if (pvNil == pglbmat34)
        goto LFail;
    AssertDo(pglbmat34->FSetIvMac(LwMul(ccel, cch)), "PglNew should have ensured space!");

    dxrTotal = rZero;
    for (ich = 0; ich < cch; ich++)
        dxrTotal += ptdf->DxrChar((uchar)_stn.Psz()[ich]);
    dxrHalf = BrsHalf(dxrTotal);

    dyrTotal = rZero;
    for (ich = 0; ich < cch; ich++)
        dyrTotal += ptdf->DyrChar((uchar)_stn.Psz()[ich]);
    dyrHalf = BrsHalf(dyrTotal);

    for (icel = 0; icel < ccel; icel++)
    {
        xrChar = -dxrHalf;
        yrChar = rZero;
        for (ich = 0; ich < cch; ich++)
        {
            dxrChar = ptdf->DxrChar((uchar)_stn.Psz()[ich]);
            dyrChar = ptdf->DyrChar((uchar)_stn.Psz()[ich]);
            xrCharCenter = xrChar + BrsHalf(dxrChar);
#if defined(BRENDER_MODERN_14)
            {
                static int32_t cdiagTdtSpacing = 0;
                if (cdiagTdtSpacing < 128 && icel == 0)
                {
                    DiagLogBRender("TDT spacing ich=%ld ch=%d advance=%.5f center=%.5f total=%.5f height=%.5f",
                                   (long)ich, (int)(unsigned char)_stn.Psz()[ich],
                                   (double)BrScalarToFloat(dxrChar), (double)BrScalarToFloat(xrCharCenter),
                                   (double)BrScalarToFloat(dxrTotal), (double)BrScalarToFloat(dyrChar));
                    cdiagTdtSpacing++;
                }
            }
#endif

            BrMatrix34Identity(&bmat34);
            // 3DMMv1.0: The only exception to my "apply shape, then action" approach
            // 3DMMv1.0: is tdaStretch, which has to change dxrTotal2 before calling
            // 3DMMv1.0: _ApplyShape.
            if (tda == tdaStretch)
            {
                BRS rPhase;
                BRS rTheta;
                BRA aTheta;
                BRS dxr;
                BRS rFractStretch;
                BRS rT;

                rPhase = BrsDiv(BrIntToScalar(icel), BrIntToScalar(ccel));
                rTheta = BrsMul(rPhase, krTwoPi) + krPi + krHalfPi;
                aTheta = BrRadianToAngle(rTheta);
                dxr = BR_SIN(aTheta);
                rFractStretch = BR_SCALAR(1.5) + BrsHalf(dxr);
                rT = BrsMul(xrCharCenter, rFractStretch);
                xrCharCenter = rT;
                rT = BrsMul(dxrTotal, rFractStretch);
                dxrTotal2 = rT;
            }
            else
            {
                dxrTotal2 = dxrTotal;
            }
            // 3DMMv1.0: apply shape
            _ApplyShape(&bmat34, _tdts, cch, ich, xrCharCenter, dxrTotal2, yrChar + dyrChar, ptdf->DyrMax(), dyrTotal);
            // 3DMMv1.0: apply transforms based on the action
            _ApplyAction(&bmat34, tda, ich, ccel, icel, xrCharCenter, dxrTotal2);

            if (_tdts == tdtsCircleZ)
            {
                BrMatrix34PostTranslate(&bmat34, rZero, BrsDiv(dxrTotal, krTwoPi) + ptdf->DyrMax(), rZero);
            }
            xrChar += dxrChar;
            yrChar += dyrChar;
            pglbmat34->Put(LwMul(icel, cch) + ich, &bmat34);
        }
    }
    ReleasePpo(&ptdf);
    return pglbmat34;
LFail:
    ReleasePpo(&ptdf);
    ReleasePpo(&pglbmat34);
    return pvNil;
}

/** 3DMMv1.0: *************************************************************************
    Build a GG of cels for the action
***************************************************************************/
PGG TDT::_PggcelBuild(int32_t tda)
{
    AssertBaseThis(0);
    AssertIn(tda, 0, tdaLim);

    int32_t cch = _stn.Cch();
    int32_t ich;
    PGG pggcel;
    CEL cel;
    CPS *prgcps = pvNil;
    int32_t iv;
    int32_t ccel;
    int32_t icel;

    ccel = _CcelOfTda(tda);

    pggcel = GG::PggNew(SIZEOF(CEL));
    if (pvNil == pggcel)
        goto LFail;
    if (!FAllocPv((void **)&prgcps, LwMul(cch, SIZEOF(CPS)), fmemClear, mprNormal))
    {
        goto LFail;
    }
    for (icel = 0; icel < ccel; icel++)
    {
        cel.chidSnd = 0;
        cel.dwr = (tda == tdaWalk ? kdwrStepWalk : kdwrStep);
        for (ich = 0; ich < cch; ich++)
        {
            prgcps[ich].chidModl = (int16_t)ich;
            prgcps[ich].imat34 = (int16_t)(LwMul(icel, cch) + ich);
        }
        if (!pggcel->FAdd(LwMul(cch, SIZEOF(CPS)), &iv, prgcps, &cel))
            goto LFail;
    }
    FreePpv((void **)&prgcps);
    return pggcel;
LFail:
    FreePpv((void **)&prgcps);
    ReleasePpo(&pggcel);
    return pvNil;
}

/** 3DMMv1.0: *************************************************************************
    Destruct the TDT
***************************************************************************/
TDT::~TDT(void)
{
    AssertBaseThis(0);

    _ReleaseLitModelCache();
    ReleasePpo(&_pmtrlDefault);
    ReleasePpo(&_pactnCache);
}

/** 3DMMv1.0: *************************************************************************
    Return a duplicate of this TDT
***************************************************************************/
PTDT TDT::PtdtDup(void)
{
    AssertThis(0);

    PTDT ptdtDup;

    ptdtDup = TDT::PtdtNew(&_stn, _tdts, &_tagTdf);
    AssertNilOrPo(ptdtDup, 0);

    return ptdtDup;
}

/** 3DMMv1.0: *************************************************************************
    Change the text, shape, and/or font of the TDT
***************************************************************************/
bool TDT::FChange(PSTN pstn, int32_t tdts, PTAG ptagTdf)
{
    AssertThis(0);
    AssertNilOrPo(pstn, 0);
    if (tdtsNil != tdts)
        AssertIn(tdts, 0, tdtsLim);
    AssertNilOrVarMem(ptagTdf);

    STN stnSave;
    int32_t tdtsSave;
    TAG tagTdfSave;

    stnSave = _stn;
    tdtsSave = _tdts;
    tagTdfSave = _tagTdf;

    if (pvNil != pstn)
        _stn = *pstn;
    if (tdtsNil != tdts)
        _tdts = tdts;
    if (pvNil != ptagTdf)
        _tagTdf = *ptagTdf;

    // 3DMMv1.0: This is a bit of a hack, but it makes life easier.  Without it,
    // 3DMMv1.0: the code would have to deal with body part sets with no body
    // 3DMMv1.0: parts in them, and TMPL and BODY would have problems with that
    // 3DMMv1.0: (for one thing, they would compute _cbset incorrectly).
    if (_stn.Cch() == 0)
    {
        achar chSpace = ChLit(' ');
        _stn.SetRgch(&chSpace, 1);
    }

    if (!_FInitLists()) // 3DMMv1.0: note: unwinds on failure
    {
        _stn = stnSave;
        _tdts = tdtsSave;
        _tagTdf = tagTdfSave;
        return fFalse;
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Get stats of this TDT
***************************************************************************/
void TDT::GetInfo(PSTN pstn, int32_t *ptdts, PTAG ptagTdf)
{
    AssertThis(0);
    AssertNilOrPo(pstn, 0);
    AssertNilOrVarMem(ptdts);
    AssertNilOrVarMem(ptagTdf);

    if (pvNil != pstn)
        *pstn = _stn;
    if (pvNil != ptdts)
        *ptdts = _tdts;
    if (pvNil != ptagTdf)
        *ptagTdf = _tagTdf;
}

/** 3DMMv1.0: *************************************************************************
    Adjust the given body's shape, since its owning TDT has changed
***************************************************************************/
bool TDT::FAdjustBody(PBODY pbody)
{
    AssertThis(0);
    AssertPo(pbody, 0);

    if (!pbody->FChangeShape(_pglibactPar, _pglibset))
        return fFalse;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Set the default costume.  This always succeeds for TDTs because they
    keep the PMTRL in memory.
***************************************************************************/
bool TDT::FSetDefaultCost(PBODY pbody)
{
    AssertThis(0);
    AssertPo(pbody, 0);

    pbody->SetPartSetMtrl(0, _pmtrlDefault);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Get a custom material
***************************************************************************/
PCMTL TDT::PcmtlFetch(int32_t cmid)
{
    AssertThis(0);
    AssertIn(cmid, 0, _ccmid);

    Bug("Shouldn't fetch CMTLs from a TDT");
    return pvNil;
}

/** 3DMMv1.0: *************************************************************************
    Write the TDT out as a TMPL hierarchy.
***************************************************************************/
bool TDT::FWrite(PCFL pcfl, CTG ctg, CNO *pcno)
{
    AssertThis(0);
    AssertPo(pcfl, 0);
    AssertVarMem(pcno);

    TDTF tdtf;
    CNO cnoTdt;
    BLCK blck;

    if (!_FWriteTmplf(pcfl, ctg, pcno))
        return fFalse;

    // 3DMMv1.0: Add TDT chunk
    tdtf.bo = kboCur;
    tdtf.osk = koskCur;
    tdtf.tdts = _tdts;
    SerializeTagToTagf(&_tagTdf, &tdtf.tagTdf);

    if (!pcfl->FAddChild(ctg, *pcno, kchidTdt, SIZEOF(TDTF), kctgTdt, &cnoTdt, &blck))
    {
        return fFalse;
    }
    if (!blck.FWrite(&tdtf))
    {
        pcfl->DeleteChild(ctg, *pcno, kctgTdt, cnoTdt, kchidTdt);
        return fFalse;
    }
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Return the number of cels in the given action
***************************************************************************/
int32_t TDT::_CcelOfTda(int32_t tda)
{
    AssertThis(0);
    AssertIn(tda, 0, tdaLim);

    int32_t ccel;

    switch (tda)
    {
    case tdaRest:
        ccel = 1;
        break;
    case tdaLetterRotX:
    case tdaLetterRotY:
    case tdaLetterRotZ:
    case tdaSwingX:
    case tdaSwingY:
    case tdaSwingZ:
    case tdaPulse:
    case tdaHop:
    case tdaStretch:
        ccel = 12;
        break;
    case tdaWordRotX:
    case tdaWordRotY:
    case tdaWordRotZ:
    case tdaWave:
    case tdaReveal:
    case tdaWalk:
        ccel = 24;
        break;
    default:
        Bug("Unknown tda");
        break;
    }
    return ccel;
}

/** 3DMMv1.0: *************************************************************************
    Apply some transformations to each character based on tda.	The
    transformation matrix is returned in pbmat34.  Some transformations are
    pre-applied and some are post-applied, depending on the desired effect.
***************************************************************************/
void TDT::_ApplyAction(BMAT34 *pbmat34, int32_t tda, int32_t ich, int32_t ccel, int32_t icel, BRS xrChar, BRS dxrText)
{
    AssertThis(0);
    AssertVarMem(pbmat34);
    AssertIn(tda, 0, tdaLim);
    AssertIn(ccel, 0, klwMax);
    AssertIn(icel, 0, ccel);

    BRS dxrTextHalf = BrsHalf(dxrText);
    // 3DMMv1.0: fraction of width of text that this char is at
    BRS xrFract = BrsDiv(xrChar + dxrTextHalf, dxrText);
    BRS rFractCel = BrsDiv(BrIntToScalar(icel), BrIntToScalar(ccel));
    BRS rSwing = BR_SIN(BrRadianToAngle(BrsMul(krTwoPi, rFractCel)));

    switch (tda)
    {
    case tdaRest:
        break;
    case tdaLetterRotX:
        BrMatrix34PreRotateX(pbmat34, BrRadianToAngle(BrsMul(rFractCel, krTwoPi)));
        break;
    case tdaLetterRotY:
        BrMatrix34PreRotateY(pbmat34, BrRadianToAngle(BrsMul(rFractCel, krTwoPi)));
        break;
    case tdaLetterRotZ:
        BrMatrix34PreRotateZ(pbmat34, BrRadianToAngle(BrsMul(rFractCel, krTwoPi)));
        break;
    case tdaSwingX:
        BrMatrix34PreShearY(pbmat34, BrsMul(krQuarter, rSwing), rZero);
        break;
    case tdaSwingY:
        BrMatrix34PreShearZ(pbmat34, rZero, BrsHalf(rSwing));
        break;
    case tdaSwingZ:
        BrMatrix34PreShearY(pbmat34, rZero, BrsHalf(rSwing));
        break;
    case tdaPulse:
        BrMatrix34PreScale(pbmat34, rOne + BrsMul(krQuarter, rSwing), rOne + BrsMul(krQuarter, rSwing),
                           rOne + BrsMul(krQuarter, rSwing));
        break;
    case tdaWordRotX:
        BrMatrix34PostRotateX(pbmat34, BrRadianToAngle(BrsMul(rFractCel, krTwoPi)));
        break;
    case tdaWordRotY:
        BrMatrix34PostRotateY(pbmat34, -BrRadianToAngle(BrsMul(rFractCel, krTwoPi)));
        break;
    case tdaWordRotZ:
        BrMatrix34PostRotateZ(pbmat34, BrRadianToAngle(BrsMul(rFractCel, krTwoPi)));
        break;
    case tdaWave: {
        BRS rTheta;
        BRA aTheta;
        BRS rScaleY;

        rTheta = BrsMul(xrFract - rFractCel, krThreePi);
        if (rTheta < -krTwoPi)
            rTheta += krThreePi;
        rTheta = LwBound(rTheta, rZero, krPi);
        aTheta = BrRadianToAngle(rTheta);
        rScaleY = rOne + BR_SIN(aTheta);
        BrMatrix34PreScale(pbmat34, rOne, rScaleY, rOne);
    }
    break;
    case tdaReveal: {
        BRS dxr;
        BRS rTheta;
        BRA aTheta;
        BRS rScaleY;

        if (icel < ccel / 2)
        {
            dxr = BrsDiv(BrIntToScalar(icel), BrIntToScalar(ccel / 2));
            dxr = BrsDiv(BrsMul(dxr, BR_SCALAR(4.0)), BR_SCALAR(3.0));
            rTheta = BrsMul(xrFract - dxr, krThreePi);
            rTheta += krPi + krHalfPi;
            rTheta = LwBound(rTheta, krHalfPi, krPi + krHalfPi);
        }
        else
        {
            dxr = BrsDiv(BrIntToScalar(icel - ccel / 2), BrIntToScalar(ccel / 2));
            dxr = BrsDiv(BrsMul(dxr, BR_SCALAR(4.0)), BR_SCALAR(3.0));
            rTheta = BrsMul(xrFract - dxr, krThreePi);
            rTheta += krPi;
            rTheta = LwBound(rTheta, -krHalfPi, krHalfPi);
        }
        aTheta = BrRadianToAngle(rTheta);
        rScaleY = BR_SCALAR(0.1) + BR_SCALAR(0.5) + BrsMul(BR_SCALAR(0.5), BR_SIN(aTheta));
        BrMatrix34PreScale(pbmat34, rOne, rScaleY, rOne);
    }
    break;
    case tdaWalk: {
        bool fOdd = (ich % 2 != 0);
        BRS rPhase;
        BRS rTheta;
        BRA aTheta;
        BRS dyr;
        BRS dzr;

        rPhase = BrsDiv(BrIntToScalar(icel), BrIntToScalar(ccel));
        rTheta = BrsMul(rPhase, krTwoPi);
        if (fOdd)
        {
            if (icel < ccel / 2)
                dzr = BrsMul(BrIntToScalar(ccel / 2 - icel), kdwrStepWalk);
            else
                dzr = BrsMul(BrIntToScalar(icel - ccel / 2), kdwrStepWalk);
        }
        else
        {
            if (icel < ccel / 2)
                dzr = BrsMul(BrIntToScalar(icel), kdwrStepWalk);
            else
                dzr = BrsMul(BrIntToScalar(ccel - icel), kdwrStepWalk);
        }
        if (fOdd)
            rTheta -= krPi;
        rTheta = LwBound(rTheta, rZero, krPi);
        aTheta = BrRadianToAngle(rTheta);
        dyr = BR_SIN(aTheta);
        BrMatrix34PreTranslate(pbmat34, rZero, dyr, dzr);
    }
    break;
    case tdaHop: {
        BRS rPhase;
        BRS rTheta;
        BRA aTheta;
        BRS dyr = rZero;
        BRS yrScale = rOne;

        if (icel < ccel * 2 / 3)
        {
            rPhase = BrsDiv(BrIntToScalar(icel), BrIntToScalar(ccel * 2 / 3));
            rTheta = BrsMul(rPhase, krPi);
            aTheta = BrRadianToAngle(rTheta);
            dyr = BR_SIN(aTheta);
        }
        else
        {
            rPhase = BrsDiv(BrIntToScalar(icel - (ccel * 2 / 3)), BrIntToScalar(ccel / 3));
            // 3DMMv1.0: rTheta goes from krPi to krTwoPi
            rTheta = BrsMul(rPhase, krPi) + krPi;
            aTheta = BrRadianToAngle(rTheta);
            yrScale = rOne + BrsDiv(BR_SIN(aTheta), rTwo);
        }
        BrMatrix34PreTranslate(pbmat34, rZero, dyr, rZero);
        BrMatrix34PreScale(pbmat34, rOne, yrScale, rOne);
    }
    break;
    case tdaStretch: {
        BRS rPhase;
        BRS rTheta;
        BRA aTheta;
        BRS dyr = rZero;
        BRS yrScale = rOne;

        rPhase = BrsDiv(BrIntToScalar(icel), BrIntToScalar(ccel));
        rTheta = BrsMul(rPhase, krTwoPi) + krPi + krHalfPi;
        aTheta = BrRadianToAngle(rTheta);
        dyr = BR_SIN(aTheta);

        BrMatrix34PreScale(pbmat34, BR_SCALAR(1.5) + BrsHalf(dyr), rOne, rOne);
    }
    break;
    default:
        Bug("Unknown tda");
        break;
    }
}

/** 3DMMv1.0: *************************************************************************
    Shape the text based on the given tdts. This function is called once
    per character in the TDT.  pbmat34 receives the transformation matrix
    for the character.  xrChar is the position of the current character
    (you could also think of this as the distance of the center of this
    character from the origin of the TDT).	dxrText is the width of the
    entire string.  dyr is the height of the font.
***************************************************************************/
void TDT::_ApplyShape(BMAT34 *pbmat34, int32_t tdts, int32_t cch, int32_t ich, BRS xrChar, BRS dxrText, BRS yrChar,
                      BRS dyrMax, BRS dyrTotal)
{
    AssertThis(0);
    AssertVarMem(pbmat34);
    AssertIn(tdts, 0, tdtsLim);
    AssertIn(cch, 0, kcchMaxStn);
    AssertIn(ich, 0, cch);

    BRS dxr = rZero;
    BRS dyr = rZero;
    BRS dzr = rZero;

    BRS dxrTextHalf = BrsHalf(dxrText);
    BRS dxrTextQuarter = BrsHalf(dxrTextHalf);
    // 3DMMv1.0: fraction of width of text that this char is at
    BRS xrFract = BrsDiv(xrChar + dxrTextHalf, dxrText);
    BRS rRadius = BrsDiv(dxrText, krTwoPi);

    switch (tdts)
    {
    case tdtsNormal:
        BrMatrix34PostTranslate(pbmat34, xrChar, rZero, rZero);
        break;
    case tdtsArchPositive:
        dyr = BrsMul(dxrTextQuarter, BR_SIN(BrRadianToAngle(BrsMul(krPi, xrFract))));
        BrMatrix34PostTranslate(pbmat34, xrChar, dyr, rZero);
        break;
    case tdtsArchNegative:
        dyr = BrsMul(dxrTextQuarter, rOne - BR_SIN(BrRadianToAngle(BrsMul(krPi, xrFract))));
        BrMatrix34PostTranslate(pbmat34, xrChar, dyr, rZero);
        break;
    case tdtsCircleY:
        BrMatrix34PostTranslate(pbmat34, rZero, rZero, rRadius);
        BrMatrix34PostRotateY(pbmat34, BrRadianToAngle(BrsMul(krTwoPi, xrFract)));
        break;
    case tdtsLargeMiddle:
        BrMatrix34PostTranslate(pbmat34, xrChar, rZero, rZero);
        BrMatrix34PostScale(pbmat34, rOne, rOne + BR_SIN(BrRadianToAngle(BrsMul(krPi, xrFract))), rOne);
        break;
    case tdtsArchZ:
        dzr = BrsMul(dxrTextQuarter, BR_SIN(BrRadianToAngle(BrsMul(krPi, xrFract))));
        BrMatrix34PostTranslate(pbmat34, xrChar, rZero, dzr);
        break;
    case tdtsCircleZ:
        BrMatrix34PostTranslate(pbmat34, rZero, rRadius, rZero);
        BrMatrix34PostRotateZ(pbmat34, -BrRadianToAngle(BrsMul(krTwoPi, xrFract)));
        break;
    case tdtsVertical:
        BrMatrix34PostTranslate(pbmat34, rZero, dyrTotal - yrChar, rZero);
        break;
    case tdtsGrowRight:
        BrMatrix34PostTranslate(pbmat34, xrChar, rZero, rZero);
        BrMatrix34PostScale(pbmat34, rOne, rOne + xrFract, rOne);
        break;
    case tdtsGrowLeft:
        BrMatrix34PostTranslate(pbmat34, xrChar, rZero, rZero);
        BrMatrix34PostScale(pbmat34, rOne, rOne + (rOne - xrFract), rOne);
        break;
    default:
        Bug("unknown tdts");
        break;
    }
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of the TDT.
***************************************************************************/
void TDT::AssertValid(uint32_t grf)
{
    TDT_PAR::AssertValid(fobjAllocated);
    AssertPo(_pgstAction, 0); // 3DMMv1.0: must set _pgstAction before creating TDTs
    Assert(_tagTdf.ctg != ctgNil, "TDT has bad _tagTdf");
    AssertPo(_pmtrlDefault, 0);
    if (tdaNil != _tdaCache)
    {
        AssertIn(_tdaCache, 0, tdaLim);
        AssertPo(_pactnCache, 0);
    }
    else
    {
        Assert(pvNil == _pactnCache, "_tdaCache is wrong");
    }
    AssertIn(_tdts, 0, tdtsLim);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the TDT
***************************************************************************/
void TDT::MarkMem(void)
{
    AssertThis(0);

    TDT_PAR::MarkMem();

    MarkMemObj(_pmtrlDefault);
    if (tdaNil != _tdaCache)
        MarkMemObj(_pactnCache);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the TDT action GST
***************************************************************************/
void TDT::MarkActionNames(void)
{
    MarkMemObj(_pgstAction);
}

#endif // 3DMMv1.0: DEBUG
