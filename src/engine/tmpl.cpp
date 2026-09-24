/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    tmpl.cpp: Actor template class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    Here's the TMPL chunk tree (* means more than one chunk can go here):

    TMPL - template flags
     |
     +--GLPI (chid 0) - parent IDs (BODY part hierarchy)
     |
     +--GLBS (chid 0) - body part sets for BODY
     |
     +--GGCM (chid 0) - custom costumes per body part set (GG of cmids)
     |
     +--CMTL* (chid <cmid>) - custom material...see mtrl.h
     |   |
     |   +--MTRL*
     |       |
     |       +--TMAP
     |
     +--MODL* - models used in this template
     |
     +--ACTN* (chid <anid>) - action for this template
         |
         +--GGCL (chid 0) - GG of cels for this action
         |
         +--GLXF (chid 0) - GL of transformation matrices for this action
         |
         +--GLMS (chid 0) - GL of motionmatch sounds for cels of this action

    About Actions: An action is an activity that a body can perform, such
    as walking, jumping, breathing, or resting.  Actions are broken down
    into cels, where each cel describes the position of each body part of
    the actor at one step or phase of the action.  After reaching the last
    cel, the action loops around to the beginning cel and repeats.  Cels
    consist of a list of cel part specs (CPS).  Each CPS refers to a single
    body part of an actor, such as a leg or head.  The CPS tells what
    BRender model to use for the body part for this cel and what matrix to
    use to orient it to its parent body part.  Each time the cel number is
    changed, TMPL reads each CPS of the new cel and updates each body part
    to use the new model and transformation matrix.

    About the GGCM: it is indexed by body part set, and tells how many
    custom materials are available for each body part set, and what the
    CMIDs are for those materials.  The first CMID in each body part set
    is the default one (for the default costume).  Example: If a body had
    3 body part sets (shirt, pants, and head), and there are 2 shirts,
    3 pants, and 1 head, the GGCM would be:

    0: fixed = 2, variable = (0, 1)
    1: fixed = 3, variable = (2, 3, 4)
    2: fixed = 1, variable = (5)

    where the fixed part is the count of available CMIDs for each body
    part set, and the numbers in the variable part are the CMIDs for the
    respective body part sets.  The default costume for this actor would
    be CMID 0 for the shirt, 2 for the pants, and 5 for the head.

***************************************************************************/
#include "soc.h"
ASSERTNAME

RTCLASS(ACTN)
RTCLASS(TMPL)

/** 3DMMv1.0: *************************************************************************
    Create a new action
***************************************************************************/
PACTN ACTN::PactnNew(PGG pggcel, PGL pglbmat34, uint32_t grfactn)
{
    AssertPo(pggcel, 0);
    AssertPo(pglbmat34, 0);

    PACTN pactn;

    pactn = NewObj ACTN;
    if (pvNil == pactn)
        goto LFail;
    pactn->_grfactn = grfactn;

    // 3DMMv1.0: Duplicate pggcel into pactn->_pggcel
    pactn->_pggcel = pggcel->PggDup();
    if (pvNil == pactn->_pggcel)
        goto LFail;

    // 3DMMv1.0: Duplicate pglbmat34 into pactn->_pglbmat34
    pactn->_pglbmat34 = pglbmat34->PglDup();
    if (pvNil == pactn->_pglbmat34)
        goto LFail;

    AssertPo(pactn, 0);

    return pactn;
LFail:
    ReleasePpo(&pactn);
    return pvNil;
}

/** 3DMMv1.0: *************************************************************************
    A PFNRPO (chunky resource reader function) to read an ACTN from a file
***************************************************************************/
bool ACTN::FReadActn(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);

    ACTN *pactn;

    *pcb = pblck->Cb(fTrue); // 3DMMv1.0: estimate ACTN size (not a good estimate)
    if (pvNil == ppbaco)
        return fTrue;

    if (!pblck->FUnpackData())
        goto LFail;
    *pcb = pblck->Cb();

    pactn = NewObj ACTN;
    if (pvNil == pactn || !pactn->_FInit(pcrf->Pcfl(), ctg, cno))
    {
        ReleasePpo(&pactn);
    LFail:
        TrashVar(ppbaco);
        TrashVar(pcb);
        return fFalse;
    }
    AssertPo(pactn, 0);
    *ppbaco = pactn;
    *pcb = SIZEOF(ACTN) + pactn->_pggcel->CbOnFile() + pactn->_pglbmat34->CbOnFile();
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Read an ACTN from a chunk
***************************************************************************/
bool ACTN::_FInit(PCFL pcfl, CTG ctg, CNO cno)
{
    AssertPo(pcfl, 0);

    KID kid;
    BLCK blck;
    ACTNF actnf;
    int16_t bo;
    int32_t icel;

    if (!pcfl->FFind(ctg, cno, &blck) || !blck.FUnpackData())
        return fFalse;

    if (blck.Cb() < SIZEOF(ACTNF))
        return fFalse;
    if (!blck.FReadRgb(&actnf, SIZEOF(ACTNF), 0))
        return fFalse;
    if (kboOther == actnf.bo)
        SwapBytesBom(&actnf, kbomActnf);
    Assert(kboCur == actnf.bo, "bad ACTNF");
    _grfactn = actnf.grfactn;

    // 3DMMv1.0: read GG of cels (chid 0, ctg kctgGgcl):
    if (!pcfl->FGetKidChidCtg(ctg, cno, 0, kctgGgcl, &kid))
        return fFalse;
    if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
        return fFalse;
    _pggcel = GG::PggRead(&blck, &bo);
    if (pvNil == _pggcel)
        return fFalse;
    AssertBomRglw(kbomCel, SIZEOF(CEL));
    AssertBomRgsw(kbomCps, SIZEOF(CPS));
    if (kboOther == bo)
    {
        for (icel = 0; icel < _pggcel->IvMac(); icel++)
        {
            SwapBytesRglw(_pggcel->QvFixedGet(icel), SIZEOF(CEL) / SIZEOF(int32_t));
            SwapBytesRgsw(_pggcel->QvGet(icel), _pggcel->Cb(icel) / SIZEOF(int16_t));
        }
    }

    // 3DMMv1.0: read GL of transforms (chid 0, ctg kctgGlxf):
    if (!pcfl->FGetKidChidCtg(ctg, cno, 0, kctgGlxf, &kid))
        return fFalse;
    if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
        return fFalse;
    _pglbmat34 = GL::PglRead(&blck, &bo);
    if (pvNil == _pglbmat34)
        return fFalse;
    AssertBomRglw(kbomBmat34, SIZEOF(BMAT34));
    if (kboOther == bo)
    {
        SwapBytesRglw(_pglbmat34->QvGet(0), LwMul(_pglbmat34->IvMac(), SIZEOF(BMAT34) / SIZEOF(int32_t)));
    }

    // 3DMMv1.0: read (optional) GL of motion-match sounds (chid 0, ctg kctgGlms):
    if (pcfl->FGetKidChidCtg(ctg, cno, 0, kctgGlms, &kid))
    {
        if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
            return fFalse;
        _pgltagSnd = GL::PglRead(&blck, &bo);
        if (pvNil == _pgltagSnd)
            return fFalse;
        AssertBomRglw(kbomTag, SIZEOF(TAG));
        if (kboOther == bo)
        {
            SwapBytesRglw(_pgltagSnd->QvGet(0), LwMul(_pgltagSnd->IvMac(), SIZEOF(TAG) / SIZEOF(int32_t)));
        }
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Destructor
***************************************************************************/
ACTN::~ACTN(void)
{
    AssertBaseThis(0);
    ReleasePpo(&_pggcel);
    ReleasePpo(&_pglbmat34);
}

/** 3DMMv1.0: *************************************************************************
    Get a CEL
***************************************************************************/
void ACTN::GetCel(int32_t icel, CEL *pcel)
{
    AssertThis(0);
    AssertIn(icel, 0, Ccel());
    AssertVarMem(pcel);

    _pggcel->GetFixed(icel, pcel);
}

/** 3DMMv1.0: *************************************************************************
    Get a CPS
***************************************************************************/
void ACTN::GetCps(int32_t icel, int32_t icps, CPS *pcps)
{
    AssertThis(0);
    AssertIn(icel, 0, Ccel());
    AssertIn(icps, 0, _pggcel->Cb(icel) / SIZEOF(CPS));
    AssertVarMem(pcps);

    CPS *prgcps = (CPS *)_pggcel->QvGet(icel);
    *pcps = prgcps[icps];
}

/** 3DMMv1.0: *************************************************************************
    Get a sound for icel.  If there is no sound, ptag's CTG is set to
    ctgNil.
***************************************************************************/
void ACTN::GetSnd(int32_t icel, PTAG ptag)
{
    AssertThis(0);
    AssertIn(icel, 0, Ccel());
    AssertVarMem(ptag);

    CEL cel;

    ptag->ctg = ctgNil;
    if (pvNil != _pgltagSnd)
    {
        GetCel(icel, &cel);
        if (cel.chidSnd >= 0) // 3DMMv1.0: negative means no sound
            _pgltagSnd->Get(cel.chidSnd, ptag);
    }
}

/** 3DMMv1.0: *************************************************************************
    Get a transformation matrix
***************************************************************************/
void ACTN::GetMatrix(int32_t imat34, BMAT34 *pbmat34)
{
    AssertThis(0);
    AssertIn(imat34, 0, _pglbmat34->IvMac());
    AssertVarMem(pbmat34);

    _pglbmat34->Get(imat34, pbmat34);
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of the ACTN.
***************************************************************************/
void ACTN::AssertValid(uint32_t grf)
{
    ACTN_PAR::AssertValid(fobjAllocated);
    AssertPo(_pggcel, 0);
    AssertPo(_pglbmat34, 0);
    AssertNilOrPo(_pgltagSnd, 0);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the ACTN
***************************************************************************/
void ACTN::MarkMem(void)
{
    AssertThis(0);
    ACTN_PAR::MarkMem();
    MarkMemObj(_pggcel);
    MarkMemObj(_pglbmat34);
    MarkMemObj(_pgltagSnd);
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    A PFNRPO (chunky resource reader function) to read TMPL objects.
***************************************************************************/
bool TMPL::FReadTmpl(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);

    TMPL *ptmpl;
    KID kid;

    *pcb = pblck->Cb(fTrue); // 3DMMv1.0: estimate TMPL size (not a good estimate)
    if (pvNil == ppbaco)
        return fTrue;

    if (pcrf->Pcfl()->FGetKidChidCtg(ctg, cno, 0, kctgTdt, &kid))
        ptmpl = NewObj TDT;
    else
        ptmpl = NewObj TMPL;
    if (pvNil == ptmpl || !ptmpl->_FInit(pcrf->Pcfl(), ctg, cno))
    {
        ReleasePpo(&ptmpl);
        TrashVar(ppbaco);
        TrashVar(pcb);
        return fFalse;
    }
    AssertPo(ptmpl, 0);
    *ppbaco = ptmpl;
    if (ptmpl->_grftmpl & ftmplTdt)
    {
        *pcb = SIZEOF(TDT);
    }
    else
    {
        *pcb =
            SIZEOF(TMPL) + ptmpl->_pglibactPar->CbOnFile() + ptmpl->_pglibset->CbOnFile() + ptmpl->_pggcmid->CbOnFile();
    }
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Read a TMPL from a chunk
***************************************************************************/
bool TMPL::_FReadTmplf(PCFL pcfl, CTG ctg, CNO cno)
{
    AssertBaseThis(0);

    BLCK blck;
    TMPLF tmplf;

    if (!pcfl->FFind(ctg, cno, &blck) || !blck.FUnpackData())
        return fFalse;

    if (blck.Cb() < SIZEOF(TMPLF))
        return fFalse;
    if (!blck.FReadRgb(&tmplf, SIZEOF(TMPLF), 0))
        return fFalse;

    if (kboOther == tmplf.bo)
        SwapBytesBom(&tmplf, kbomTmplf);
    Assert(kboCur == tmplf.bo, "freaky tmplf!");
    _xaRest = tmplf.xaRest;
    _yaRest = tmplf.yaRest;
    _zaRest = tmplf.zaRest;
    _grftmpl = tmplf.grftmpl;
    if (!pcfl->FGetName(ctg, cno, &_stn))
        return fFalse;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Write the TMPLF chunk.  Creates a new chunk and returns the CNO in pcno.

    Note: In Socrates, normal actor templates are read-only, but this
    function will get called for TDTs.
***************************************************************************/
bool TMPL::_FWriteTmplf(PCFL pcfl, CTG ctg, CNO *pcno)
{
    AssertThis(0);
    AssertPo(pcfl, 0);
    AssertVarMem(pcno);

    TMPLF tmplf;

    // 3DMMv1.0: Add TMPL chunk
    tmplf.bo = kboCur;
    tmplf.osk = koskCur;
    tmplf.xaRest = _xaRest;
    tmplf.yaRest = _yaRest;
    tmplf.zaRest = _zaRest;
    tmplf.grftmpl = _grftmpl;

    if (!pcfl->FAddPv(&tmplf, SIZEOF(TMPLF), ctg, pcno))
        return fFalse;
    if (!pcfl->FSetName(kctgTmpl, *pcno, &_stn))
    {
        pcfl->Delete(ctg, *pcno);
        return fFalse;
    }
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Read a TMPL from a chunk
***************************************************************************/
bool TMPL::_FInit(PCFL pcfl, CTG ctg, CNO cno)
{
    AssertPo(pcfl, 0);

    KID kid;
    int16_t bo;
    BLCK blck;
    int32_t ibact;
    int16_t ibset;

    if (!_FReadTmplf(pcfl, ctg, cno))
        return fFalse;

    // 3DMMv1.0: read GLPI (parent tree)
    if (!pcfl->FGetKidChidCtg(ctg, cno, 0, kctgGlpi, &kid))
        return fFalse;
    if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
        return fFalse;
    _pglibactPar = GL::PglRead(&blck, &bo);
    if (pvNil == _pglibactPar)
        return fFalse;
    Assert(_pglibactPar->CbEntry() == SIZEOF(int16_t), "Bad _pglibactPar!");
    if (kboOther == bo)
        SwapBytesRgsw(_pglibactPar->QvGet(0), _pglibactPar->IvMac());

    // 3DMMv1.0: read GLBS (body-part-set ID list)
    if (!pcfl->FGetKidChidCtg(ctg, cno, 0, kctgGlbs, &kid))
        return fFalse;
    if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
        return fFalse;
    _pglibset = GL::PglRead(&blck, &bo);
    if (pvNil == _pglibset)
        return fFalse;
    Assert(_pglibset->CbEntry() == SIZEOF(int16_t), "Bad TMPL _pglibset!");
    if (kboOther == bo)
        SwapBytesRgsw(_pglibset->QvGet(0), _pglibset->IvMac());

#ifdef DEBUG
    // 3DMMv1.0: GLDC is obsolete
    if (pcfl->FGetKidChidCtg(ctg, cno, 0, kctgGldc, &kid))
        Warn("Obsolete GLDC structure...get rid of it");
#endif // 3DMMv1.0: DEBUG

    // 3DMMv1.0: _cbset is (highest entry in _pglibset) + 1.
    _cbset = -1;
    for (ibact = 0; ibact < _pglibset->IvMac(); ibact++)
    {
        _pglibset->Get(ibact, &ibset);
        if (ibset > _cbset)
            _cbset = ibset;
    }
    _cbset++;

    // 3DMMv1.0: Count custom costumes
    _ccmid = 0;
    while (pcfl->FGetKidChidCtg(ctg, cno, _ccmid, kctgCmtl, &kid))
        _ccmid++;

    // 3DMMv1.0: Count actions
    _cactn = 0;
    while (pcfl->FGetKidChidCtg(ctg, cno, _cactn, kctgActn, &kid))
        _cactn++;

    // 3DMMv1.0: read GGCM (costume info)
    if (!pcfl->FGetKidChidCtg(ctg, cno, 0, kctgGgcm, &kid))
    {
        // 3DMMv1.0: REVIEW *****: temp until Pete updates sitobren
        goto LBuildGgcm;
        // 3DMMv1.0:		return fFalse;
    }
    if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
        return fFalse;
    _pggcmid = GG::PggRead(&blck, &bo);
    if (pvNil == _pggcmid)
        return fFalse;
    Assert(_pggcmid->CbFixed() == SIZEOF(int32_t), "Bad TMPL _pggcmid");
    Assert(_pggcmid->IvMac() == _cbset, "Bad TMPL _pggcmid");
    if (kboOther == bo)
    {
        for (ibset = 0; ibset < _cbset; ibset++)
        {
            SwapBytesRglw(_pggcmid->QvFixedGet(ibset), 1);
            SwapBytesRglw(_pggcmid->QvGet(ibset), *(int32_t *)_pggcmid->QvFixedGet(ibset));
        }
    }
    return fTrue;
// 3DMMv1.0: REVIEW *****: temp code until Pete converts our TMPL content
LBuildGgcm:
    int32_t ikid;
    PCMTL pcmtl;
    PCRF pcrf;
    int32_t rgcmid[50];
    int32_t ccmid;

    Warn("missing GGCM...building one on the fly");

    pcrf = CRF::PcrfNew(pcfl, 0);
    if (pvNil == pcrf)
        return fFalse;

    _pggcmid = GG::PggNew(SIZEOF(int32_t));
    if (pvNil == _pggcmid)
    {
        ReleasePpo(&pcrf);
        return fFalse;
    }
    for (ibset = 0; ibset < _cbset; ibset++)
    {
        ikid = 0;
        ccmid = 0;

        while (pcfl->FGetKid(ctg, cno, ikid++, &kid))
        {
            if (kid.cki.ctg != kctgCmtl)
                continue;
            pcmtl = (PCMTL)pcrf->PbacoFetch(kid.cki.ctg, kid.cki.cno, CMTL::FReadCmtl);
            if (pvNil == pcmtl)
            {
                ReleasePpo(&pcrf);
                return fFalse;
            }
            if (pcmtl->Ibset() == ibset)
            {
                rgcmid[ccmid++] = kid.chid;
            }
            ReleasePpo(&pcmtl);
        }
        if (!_pggcmid->FAdd(ccmid * SIZEOF(int32_t), pvNil, rgcmid, &ccmid))
        {
            ReleasePpo(&pcrf);
            return fFalse;
        }
    }
    ReleasePpo(&pcrf);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Clean up and delete template
***************************************************************************/
TMPL::~TMPL(void)
{
    AssertBaseThis(0);
    ReleasePpo(&_pglibactPar);
    ReleasePpo(&_pglibset);
    ReleasePpo(&_pggcmid);
}

/** 3DMMv1.0: *************************************************************************
    Return a list of all tags embedded in this TMPL.  Note that a
    return value of pvNil does not mean an error occurred, but simply that
    this TMPL has no embedded tags.
***************************************************************************/
PGL TMPL::PgltagFetch(PCFL pcfl, CTG ctg, CNO cno, bool *pfError)
{
    AssertPo(pcfl, 0);
    AssertVarMem(pfError);

    KID kid;

    *pfError = fFalse;
    if (pcfl->FGetKidChidCtg(ctg, cno, 0, kctgTdt, &kid))
        return TDT::PgltagFetch(pcfl, ctg, cno, pfError);
    else
        return pvNil; // 3DMMv1.0: standard TMPLs have no embedded tags
}

/** 3DMMv1.0: *************************************************************************
    Creates a new tree of body parts (br_actors) based on this template.
    Note: ACTR also calls FSetDefaultCost after creating the body, but
    by calling it here, it is guaranteed that the body will have a material
    on each body part (no null pointers for bact->material).  So the user
    will never see a body part that isn't texture mapped.
***************************************************************************/
PBODY TMPL::PbodyCreate(void)
{
    AssertThis(0);
    PBODY pbody = BODY::PbodyNew(_pglibactPar, _pglibset);

    if (pvNil == pbody || !FSetDefaultCost(pbody))
    {
        ReleasePpo(&pbody);
        return pvNil;
    }
    return pbody;
}

/***************************************************************************
    Rebuild an existing BODY when a writable Actor Studio TMPL changes its
    part hierarchy. Stock 3DMM templates are immutable, so the original
    FChangeTagTmpl path never had to handle a non-TDT gaining BODY parts.
***************************************************************************/
bool TMPL::FConformBodyShape(PBODY pbody)
{
    AssertThis(0);
    AssertPo(pbody, 0);
    if (pbody == pvNil)
        return fFalse;
    if (pbody->Cpart() == _pglibactPar->IvMac())
        return fTrue;
    if (!pbody->FChangeShape(_pglibactPar, _pglibset))
        return fFalse;
    return FSetDefaultCost(pbody);
}

/***************************************************************************
    Reload the mutable BODY topology/costume map directly from this TMPL's
    Chunky source. Actor Studio can replace GLPI/GLBS/GGCM children while the
    TMPL itself is still
    live and therefore still owns the old cached GLs. Waiting for the CRF
    representation cache to die is not sufficient because the preview/live
    ACTR deliberately keeps this TMPL referenced.
***************************************************************************/
bool TMPL::FRefreshPartShape(void)
{
    AssertThis(0);
    PCRF pcrf = Pcrf();
    PCFL pcfl = pcrf != pvNil ? pcrf->Pcfl() : pvNil;
    if (pcfl == pvNil)
        return fFalse;

    KID kidPar;
    KID kidSet;
    KID kidGgcm;
    BLCK blckPar;
    BLCK blckSet;
    BLCK blckGgcm;
    int16_t boPar = kboCur;
    int16_t boSet = kboCur;
    int16_t boGgcm = kboCur;
    PGL pglPar = pvNil;
    PGL pglSet = pvNil;
    PGG pggCmid = pvNil;
    if (!pcfl->FGetKidChidCtg(Ctg(), Cno(), 0, kctgGlpi, &kidPar) ||
        !pcfl->FFind(kidPar.cki.ctg, kidPar.cki.cno, &blckPar) ||
        (pglPar = GL::PglRead(&blckPar, &boPar)) == pvNil ||
        !pcfl->FGetKidChidCtg(Ctg(), Cno(), 0, kctgGlbs, &kidSet) ||
        !pcfl->FFind(kidSet.cki.ctg, kidSet.cki.cno, &blckSet) ||
        (pglSet = GL::PglRead(&blckSet, &boSet)) == pvNil ||
        !pcfl->FGetKidChidCtg(Ctg(), Cno(), 0, kctgGgcm, &kidGgcm) ||
        !pcfl->FFind(kidGgcm.cki.ctg, kidGgcm.cki.cno, &blckGgcm) ||
        (pggCmid = GG::PggRead(&blckGgcm, &boGgcm)) == pvNil)
    {
        ReleasePpo(&pglPar);
        ReleasePpo(&pglSet);
        ReleasePpo(&pggCmid);
        return fFalse;
    }
    if (pglPar->CbEntry() != SIZEOF(int16_t) || pglSet->CbEntry() != SIZEOF(int16_t) ||
        pglPar->IvMac() != pglSet->IvMac() || pggCmid->CbFixed() != SIZEOF(int32_t))
    {
        ReleasePpo(&pglPar);
        ReleasePpo(&pglSet);
        ReleasePpo(&pggCmid);
        return fFalse;
    }
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

    int32_t cbset = -1;
    for (int32_t ipart = 0; ipart < pglSet->IvMac(); ++ipart)
    {
        int16_t ibset = 0;
        pglSet->Get(ipart, &ibset);
        if (ibset < 0)
        {
            ReleasePpo(&pglPar);
            ReleasePpo(&pglSet);
            ReleasePpo(&pggCmid);
            return fFalse;
        }
        cbset = LwMax(cbset, (int32_t)ibset);
    }
    ++cbset;
    if (pggCmid->IvMac() != cbset)
    {
        ReleasePpo(&pglPar);
        ReleasePpo(&pglSet);
        ReleasePpo(&pggCmid);
        return fFalse;
    }

    int32_t ccmid = 0;
    KID kid;
    for (int32_t ikid = 0; pcfl->FGetKid(Ctg(), Cno(), ikid, &kid); ++ikid)
        if (kid.cki.ctg == kctgCmtl)
            ++ccmid;

    ReleasePpo(&_pglibactPar);
    ReleasePpo(&_pglibset);
    ReleasePpo(&_pggcmid);
    _pglibactPar = pglPar;
    _pglibset = pglSet;
    _pggcmid = pggCmid;
    _cbset = cbset;
    _ccmid = ccmid;
    return fTrue;
}

/***************************************************************************
    Refresh the cached action count after Actor Studio appends an ACTN child
    to an already-live writable TMPL.  The original 3DMM templates are static,
    so _cactn historically only needed to be computed in _FInit().  Actor
    Studio deliberately makes a movie-local TMPL mutable, so that assumption
    no longer holds.
***************************************************************************/
void TMPL::RefreshActionCount(void)
{
    AssertThis(0);
    _cactn = 0;
    KID kid;
    PCFL pcfl = Pcrf() != pvNil ? Pcrf()->Pcfl() : pvNil;
    if (pcfl == pvNil)
        return;
    while (pcfl->FGetKidChidCtg(Ctg(), Cno(), _cactn, kctgActn, &kid))
        ++_cactn;
}

/** 3DMMv1.0: *************************************************************************
    Fills in the name of the given action
***************************************************************************/
bool TMPL::FGetActnName(int32_t anid, PSTN pstn)
{
    AssertThis(0);
    AssertIn(anid, 0, _cactn);
    AssertPo(pstn, 0);

    KID kid;

    if (!Pcrf()->Pcfl()->FGetKidChidCtg(Ctg(), Cno(), anid, kctgActn, &kid))
        return fFalse;
    return Pcrf()->Pcfl()->FGetName(kid.cki.ctg, kid.cki.cno, pstn);
}

/** 3DMMv1.0: *************************************************************************
    Reads an ACTN chunk from disk
***************************************************************************/
PACTN TMPL::_PactnFetch(int32_t anid)
{
    AssertThis(0);
    AssertIn(anid, 0, _cactn);

    uint64_t qwPerf = MVIE::FPerformanceMode() ? MVIE::PerfNow() : 0;
    KID kid;
    ACTN *pactn;
    CHID chidActn = anid;

    if (!Pcrf()->Pcfl()->FGetKidChidCtg(Ctg(), Cno(), chidActn, kctgActn, &kid))
    {
        if (MVIE::FPerformanceMode())
            MVIE::PerfRecordActionFetch(MVIE::PerfElapsedUs(qwPerf));
        return pvNil;
    }
    pactn = (ACTN *)Pcrf()->PbacoFetch(kid.cki.ctg, kid.cki.cno, ACTN::FReadActn);
    AssertNilOrPo(pactn, 0);
    if (MVIE::FPerformanceMode())
        MVIE::PerfRecordActionFetch(MVIE::PerfElapsedUs(qwPerf));
    return pactn;
}

/** 3DMMv1.0: *************************************************************************
    Reads a MODL chunk from disk
***************************************************************************/
PMODL TMPL::_PmodlFetch(CHID chidModl)
{
    AssertThis(0);

    uint64_t qwPerfFetch = MVIE::FPerformanceMode() ? MVIE::PerfNow() : 0;
    uint64_t qwPerfStage = qwPerfFetch;
    uint32_t cusecLookup = 0;
    uint32_t cusecPbaco = 0;
    KID kid;
    MODL *pmodl;

    if (!Pcrf()->Pcfl()->FGetKidChidCtg(Ctg(), Cno(), chidModl, kctgBmdl, &kid))
    {
        if (MVIE::FPerformanceMode())
        {
            cusecLookup = MVIE::PerfElapsedUs(qwPerfStage);
            MVIE::PerfRecordModelFetch(MVIE::PerfElapsedUs(qwPerfFetch), cusecLookup, 0, 0, 0);
        }
        return pvNil;
    }
    if (MVIE::FPerformanceMode())
    {
        cusecLookup = MVIE::PerfElapsedUs(qwPerfStage);
        qwPerfStage = MVIE::PerfNow();
    }
    pmodl = (MODL *)Pcrf()->PbacoFetch(kid.cki.ctg, kid.cki.cno, MODL::FReadModl);
    AssertNilOrPo(pmodl, 0);
    if (MVIE::FPerformanceMode())
    {
        cusecPbaco = MVIE::PerfElapsedUs(qwPerfStage);
        MVIE::PerfRecordModelFetch(MVIE::PerfElapsedUs(qwPerfFetch), cusecLookup, cusecPbaco,
                                   kid.cki.ctg, kid.cki.cno);
    }
    return pmodl;
}

/** 3DMMv1.0: *************************************************************************
    Sets up the body part tree to use the correct models and transformation
    matrices for the given cel of the given action.  Also returns the
    distance to the next cel in *pdwr.
***************************************************************************/
bool TMPL::FSetActnCel(BODY *pbody, int32_t anid, int32_t celn, BRS *pdwr)
{
    uint64_t qwPerfSetCel = MVIE::FPerformanceMode() ? MVIE::PerfNow() : 0;
    AssertThis(0);
    AssertPo(pbody, 0);
    AssertIn(anid, 0, _cactn);
    AssertNilOrVarMem(pdwr);

    int32_t icel;
    ACTN *pactn = pvNil;
    CEL cel;
    int16_t ibprt;
    int32_t cbprt = _pglibactPar->IvMac();
    CPS cps;
    PMODL *prgpmodl = pvNil;
    BMAT34 bmat34;
    bool fRet = fFalse;

    pactn = _PactnFetch(anid);
    if (pvNil == pactn)
        goto LEnd;
    icel = celn % pactn->Ccel();
    if (icel < 0)
        icel += pactn->Ccel();
    pactn->GetCel(icel, &cel);

    if (!FAllocPv((void **)&prgpmodl, LwMul(cbprt, SIZEOF(PMODL)), fmemClear, mprNormal))
    {
        goto LEnd;
    }
    for (ibprt = 0; ibprt < cbprt; ibprt++)
    {
        pactn->GetCps(icel, ibprt, &cps);
        if (chidNil == cps.chidModl)
        {
            // 3DMMv1.0: Appendages for accessories...don't smash
            // 3DMMv1.0: accessory models with action models
            Assert(pvNil == prgpmodl[ibprt], "fmemClear didn't work?");
        }
        else if (F4DMMActorStudioCpsModelHidden(cps.chidModl))
        {
            // 4DMM Actor Studio Cut keeps the original model CHID encoded in
            // the CPS but deliberately installs no model for this cel.
            Assert(pvNil == prgpmodl[ibprt], "fmemClear didn't work?");
        }
        else
        {
            prgpmodl[ibprt] = _PmodlFetch(cps.chidModl);
            if (pvNil == prgpmodl[ibprt])
                goto LEnd;
        }
    }

    if (pvNil != pdwr)
        *pdwr = cel.dwr;
    for (ibprt = 0; ibprt < cbprt; ibprt++)
    {
        pactn->GetCps(icel, ibprt, &cps);
        if (chidNil != cps.chidModl && F4DMMActorStudioCpsModelHidden(cps.chidModl))
            pbody->ClearPartModel(ibprt);
        else if (pvNil != prgpmodl[ibprt])
            pbody->SetPartModel(ibprt, prgpmodl[ibprt]);
        pactn->GetMatrix(cps.imat34, &bmat34);
        pbody->SetPartMatrix(ibprt, &bmat34);
    }
    fRet = fTrue;
LEnd:
    if (pvNil != prgpmodl)
    {
        for (ibprt = 0; ibprt < cbprt; ibprt++)
            ReleasePpo(&prgpmodl[ibprt]);
        FreePpv((void **)&prgpmodl);
    }
    ReleasePpo(&pactn);
    if (MVIE::FPerformanceMode())
        MVIE::PerfRecordSetActnCel(MVIE::PerfElapsedUs(qwPerfSetCel));
    return fRet;
}

/** 3DMMv1.0: *************************************************************************
    Retrieves the distance travelled by cel celn of action anid.
***************************************************************************/
bool TMPL::FGetDwrActnCel(int32_t anid, int32_t celn, BRS *pdwr)
{
    AssertThis(0);
    AssertIn(anid, 0, _cactn);
    AssertVarMem(pdwr);

    ACTN *pactn;
    int32_t icel;
    CEL cel;

    pactn = _PactnFetch(anid);
    if (pvNil == pactn)
        return fFalse;
    icel = celn % pactn->Ccel();
    if (icel < 0)
        icel += pactn->Ccel();
    pactn->GetCel(icel, &cel);
    ReleasePpo(&pactn);
    *pdwr = cel.dwr;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Retrieves the number of cels in this action
***************************************************************************/
bool TMPL::FGetCcelActn(int32_t anid, int32_t *pccel)
{
    AssertThis(0);
    AssertIn(anid, 0, _cactn);
    AssertVarMem(pccel);

    ACTN *pactn;

    pactn = _PactnFetch(anid);
    if (pvNil == pactn)
        return fFalse;
    *pccel = pactn->Ccel();
    ReleasePpo(&pactn);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Retrieves the number of cels in this action
***************************************************************************/
bool TMPL::FGetSndActnCel(int32_t anid, int32_t celn, bool *pfSoundExists, PTAG ptag)
{
    AssertThis(0);
    AssertIn(anid, 0, _cactn);
    AssertVarMem(pfSoundExists);
    AssertVarMem(ptag);

    ACTN *pactn;
    int32_t icel;

    *pfSoundExists = fFalse;
    pactn = _PactnFetch(anid);
    if (pvNil == pactn)
        return fFalse;
    icel = celn % pactn->Ccel();
    if (icel < 0)
        icel += pactn->Ccel();
    pactn->GetSnd(icel, ptag);
    if (ptag->ctg != ctgNil)
        *pfSoundExists = fTrue;
    ReleasePpo(&pactn);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Retrieves the distance travelled by cel celn of action anid.
***************************************************************************/
bool TMPL::FGetGrfactn(int32_t anid, uint32_t *pgrfactn)
{
    AssertThis(0);
    AssertIn(anid, 0, _cactn);
    AssertVarMem(pgrfactn);

    ACTN *pactn;

    pactn = _PactnFetch(anid);
    if (pvNil == pactn)
        return fFalse;
    *pgrfactn = pactn->Grfactn();
    ReleasePpo(&pactn);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Get orientation for template when actor has no path
***************************************************************************/
void TMPL::GetRestOrien(BRA *pxa, BRA *pya, BRA *pza)
{
    AssertThis(0);
    AssertVarMem(pxa);
    AssertVarMem(pya);
    AssertVarMem(pza);

    *pxa = _xaRest;
    *pya = _yaRest;
    *pza = _zaRest;
}

/** 3DMMv1.0: *************************************************************************
    Puts default costume on pbody
***************************************************************************/
bool TMPL::FSetDefaultCost(BODY *pbody)
{
    AssertThis(0);
    AssertPo(pbody, 0);

    int32_t ibset;
    int32_t cmid;
    PCMTL *prgpcmtl;
    bool fRet = fFalse;

    if (!FAllocPv((void **)&prgpcmtl, LwMul(_cbset, SIZEOF(PCMTL)), fmemClear, mprNormal))
    {
        goto LEnd;
    }
    for (ibset = 0; ibset < _cbset; ibset++)
    {
        cmid = CmidOfBset(ibset, 0);
        prgpcmtl[ibset] = PcmtlFetch(cmid);
        if (pvNil == prgpcmtl[ibset])
            goto LEnd;
        Assert(prgpcmtl[ibset]->Ibset() == ibset, "ibset's don't match");
    }
    for (ibset = 0; ibset < _cbset; ibset++)
        pbody->SetPartSetCmtl(prgpcmtl[ibset]);
    fRet = fTrue;
LEnd:
    if (pvNil != prgpcmtl)
    {
        for (ibset = 0; ibset < _cbset; ibset++)
            ReleasePpo(&prgpcmtl[ibset]);
        FreePpv((void **)&prgpcmtl);
    }
    return fRet;
}

/** 3DMMv1.0: *************************************************************************
    Returns the number of custom materials available for ibset
***************************************************************************/
int32_t TMPL::CcmidOfBset(int32_t ibset)
{
    AssertThis(0);
    AssertIn(ibset, 0, _cbset);

    return *(int32_t *)_pggcmid->QvFixedGet(ibset);
}

/** 3DMMv1.0: *************************************************************************
    Returns the icmid'th CMID available for ibset
***************************************************************************/
int32_t TMPL::CmidOfBset(int32_t ibset, int32_t icmid)
{
    AssertThis(0);
    AssertIn(ibset, 0, _cbset);
    AssertIn(icmid, 0, CcmidOfBset(ibset));

    int32_t *prgcmid;

    prgcmid = (int32_t *)_pggcmid->QvGet(ibset);
    return prgcmid[icmid];
}

/** 3DMMv1.0: *************************************************************************
    Tells whether ibset holds accessories by checking to see if one of
    its costumes has model children.
***************************************************************************/
bool TMPL::FBsetIsAccessory(int32_t ibset)
{
    AssertThis(0);
    AssertIn(ibset, 0, _cbset);

    int32_t cmid;
    KID kid;

    if (pvNil == Pcrf())
        return fFalse; // 3DMMv1.0: probably a TDT

    cmid = CmidOfBset(ibset, 0);
    if (!Pcrf()->Pcfl()->FGetKidChidCtg(Ctg(), Cno(), cmid, kctgCmtl, &kid))
    {
        return fFalse;
    }

    return CMTL::FHasModels(Pcrf()->Pcfl(), kid.cki.ctg, kid.cki.cno);
}

/** 3DMMv1.0: *************************************************************************
    Returns the ibset of the accessory associated with ibset, if any.  If
    ibset is itself an accessory, it is returned in *pibsetAcc.  Otherwise,
    if ibset is the parent of an accessory, that accessory is returned in
    *pibsetAcc.
***************************************************************************/
bool TMPL::FIbsetAccOfIbset(int32_t ibset, int32_t *pibsetAcc)
{
    AssertThis(0);
    AssertIn(ibset, 0, _cbset);
    AssertVarMem(pibsetAcc);

    int32_t ibsetT;
    int32_t ibact;
    int32_t ibactPar;
    int16_t ibsetOfIbact;
    int16_t ibsetOfIbactPar;

    if (FBsetIsAccessory(ibset))
    {
        *pibsetAcc = ibset;
        return fTrue;
    }

    for (ibsetT = 0; ibsetT < _cbset; ibsetT++)
    {
        if (FBsetIsAccessory(ibsetT))
        {
            // 3DMMv1.0: for each ibact in ibsetT, see if its parent is in ibset
            for (ibact = 0; ibact < _pglibactPar->IvMac(); ibact++)
            {
                ibsetOfIbact = *(int16_t *)_pglibset->QvGet(ibact);
                if (ibsetT == ibsetOfIbact)
                {
                    // 3DMMv1.0: see if ibact's parent in ibset
                    ibactPar = *(int16_t *)_pglibactPar->QvGet(ibact);
                    ibsetOfIbactPar = *(int16_t *)_pglibset->QvGet(ibactPar);
                    if (ibsetOfIbactPar == ibset)
                    {
                        // 3DMMv1.0: so ibset is a parent bset of ibsetT
                        *pibsetAcc = ibsetT;
                        return fTrue;
                    }
                }
            }
        }
    }
    return fFalse; // 3DMMv1.0: ibset is not a parent bset of any accessory bset.
}

/** 3DMMv1.0: *************************************************************************
    See if cmid1 and cmid2 are for the same accessory by comparing child
    model chunks
***************************************************************************/
bool TMPL::FSameAccCmids(int32_t cmid1, int32_t cmid2)
{
    AssertThis(0);

    KID kid1;
    KID kid2;

    if (!Pcrf()->Pcfl()->FGetKidChidCtg(Ctg(), Cno(), cmid1, kctgCmtl, &kid1) ||
        !Pcrf()->Pcfl()->FGetKidChidCtg(Ctg(), Cno(), cmid2, kctgCmtl, &kid2))
    {
        return fFalse; // 3DMMv1.0: safer to assume they're different
    }
    return CMTL::FEqualModels(Pcrf()->Pcfl(), kid1.cki.cno, kid2.cki.cno);
}

/** 3DMMv1.0: *************************************************************************
    Get a custom material.  The cmid is really the chid under the TMPL.
***************************************************************************/
PCMTL TMPL::PcmtlFetch(int32_t cmid)
{
    AssertThis(0);
    AssertIn(cmid, 0, _ccmid);

    PCMTL pcmtl;
    KID kid;

    if (!Pcrf()->Pcfl()->FGetKidChidCtg(Ctg(), Cno(), cmid, kctgCmtl, &kid))
    {
        return pvNil;
    }
    pcmtl = (PCMTL)Pcrf()->PbacoFetch(kid.cki.ctg, kid.cki.cno, CMTL::FReadCmtl);
    AssertNilOrPo(pcmtl, 0);
    return pcmtl;
}

/** 3DMMv1.0: *************************************************************************
    Puts the template's name into pstn
***************************************************************************/
void TMPL::GetName(PSTN pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    *pstn = _stn;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of the TMPL.
***************************************************************************/
void TMPL::AssertValid(uint32_t grftmpl)
{
    int32_t ibset;
    int32_t ccmid;

    TMPL_PAR::AssertValid(fobjAllocated);
    AssertPo(_pglibactPar, 0);
    AssertPo(_pglibset, 0);
    AssertPo(_pggcmid, 0);
    AssertPo(&_stn, 0);

    // 3DMMv1.0: Verify correctness of _pggcmid
    Assert(_pggcmid->IvMac() == _cbset, 0);
    for (ibset = 0; ibset < _cbset; ibset++)
    {
        ccmid = *(int32_t *)_pggcmid->QvFixedGet(ibset);
        Assert(_pggcmid->Cb(ibset) / SIZEOF(int32_t) == ccmid, 0);
    }
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the TMPL
***************************************************************************/
void TMPL::MarkMem(void)
{
    AssertThis(0);
    TMPL_PAR::MarkMem();
    MarkMemObj(_pglibactPar);
    MarkMemObj(_pglibset);
    MarkMemObj(_pggcmid);
}
#endif // 3DMMv1.0: DEBUG
