/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Chunky resource management.

    WARNING: BACOs should only be released or fetched from the main
    thread! CRFs are NOT thread safe! Alternatively, the BACO can be
    detached from the CRF (in the main thread), then later released
    in a different thread.

***************************************************************************/
#include "util.h"
ASSERTNAME

RTCLASS(BACO)
RTCLASS(GHQ)
RTCLASS(RCA)
RTCLASS(CRF)
RTCLASS(CRM)
RTCLASS(CABO)

/** 3DMMv1.0: *************************************************************************
    Constructor for base cacheable object.
***************************************************************************/
BACO::BACO(void)
{
    AssertBaseThis(fobjAllocated);
    _pcrf = pvNil;
    _crep = crepToss;
    _fAttached = fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Destructor.
***************************************************************************/
BACO::~BACO(void)
{
    AssertBaseThis(fobjAllocated);
    Assert(!_fAttached, "still attached");
    ReleasePpo(&_pcrf);
}

/** 3DMMv1.0: *************************************************************************
    Write the BACO to a FLO - just make the FLO a BLCK and write to
    the block.
***************************************************************************/
bool BACO::FWriteFlo(PFLO pflo)
{
    AssertThis(0);
    AssertPo(pflo, 0);
    BLCK blck(pflo);
    return FWrite(&blck);
}

/** 3DMMv1.0: *************************************************************************
    Placeholder function for BACO generic writer.
***************************************************************************/
bool BACO::FWrite(PBLCK pblck)
{
    AssertThis(0);
    RawRtn(); // 3DMMv1.0: Derived class should be defining this
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Placeholder function for BACO generic cb-getter.
***************************************************************************/
int32_t BACO::CbOnFile(void)
{
    AssertThis(0);
    RawRtn(); // 3DMMv1.0: Derived class should be defining this
    return 0;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a BACO.
***************************************************************************/
void BACO::AssertValid(uint32_t grf)
{
    BACO_PAR::AssertValid(fobjAllocated);
    Assert(!_fAttached || pvNil != _pcrf, "attached baco has no crf");
    AssertNilOrVarMem(_pcrf);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory for the BACO.
***************************************************************************/
void BACO::MarkMem(void)
{
    AssertValid(0);
    BACO_PAR::MarkMem();
    if (!_fAttached)
        MarkMemObj(_pcrf);
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Release a reference to the BACO.  If the reference count goes to zero
    and the BACO is not attached, it is deleted.
***************************************************************************/
void BACO::Release(void)
{
    AssertThis(0);
    if (_cactRef-- <= 0)
    {
        Bug("calling Release without an AddRef");
        _cactRef = 0;
    }
    if (_cactRef == 0)
    {
        if (!_fAttached)
            delete this;
        else
        {
            AssertPo(_pcrf, 0);
            _pcrf->BacoReleased(this);
        }
    }
}

/** 3DMMv1.0: *************************************************************************
    Detach a BACO from its CRF.
***************************************************************************/
void BACO::Detach(void)
{
    AssertThis(0);
    if (_fAttached)
    {
        AssertPo(_pcrf, 0);
        _pcrf->AddRef();
        _fAttached = fFalse;
        _pcrf->BacoDetached(this);
    }
    if (_cactRef <= 0)
        delete this;
}

/** 3DMMv1.0: *************************************************************************
    Set the crep for the BACO.
***************************************************************************/
void BACO::SetCrep(int32_t crep)
{
    AssertThis(0);
    // 3DMMv1.0: An AddRef followed by Release is done so that BacoReleased() is
    // 3DMMv1.0: called if this BACO's _cactRef is 0...if crep is crepToss, this
    // 3DMMv1.0: detaches this BACO from the cache.
    AddRef();
    _crep = crep;
    Release();
}

/** 3DMMv1.0: *************************************************************************
    Constructor for CRF.  Increments the open count on the CFL.
***************************************************************************/
CRF::CRF(PCFL pcfl, int32_t cbMax)
{
    AssertBaseThis(fobjAllocated);
    AssertPo(pcfl, 0);
    AssertIn(cbMax, 0, kcbMax);

    pcfl->AddRef();
    _pcfl = pcfl;
    _cbMax = cbMax;
}

/** 3DMMv1.0: *************************************************************************
    Destructor for the CRF.  Decrements the open count on the CFL and frees
    all the cached data.
***************************************************************************/
CRF::~CRF(void)
{
    AssertBaseThis(fobjAllocated);
    CRE cre;

    _cactRef++; // 3DMMv1.0: so we don't get "deleted" while detaching the BACOs
    if (pvNil != _pglcre)
    {
        while (_pglcre->IvMac() > 0)
        {
            _pglcre->Get(0, &cre);
            cre.pbaco->AddRef(); // 3DMMv1.0: so it doesn't go away when being detached
            cre.pbaco->Detach();
            cre.pbaco->_pcrf = pvNil; // 3DMMv1.0: we're going away!
            Debug(_cactRef--;) cre.pbaco->Release();
        }
        ReleasePpo(&_pglcre);
    }
    Assert(_cactRef == 1, "someone still refers to this CRF");
    ReleasePpo(&_pcfl);
}

/** 3DMMv1.0: *************************************************************************
    Static method to create a new chunky resource file cache.
***************************************************************************/
PCRF CRF::PcrfNew(PCFL pcfl, int32_t cbMax)
{
    AssertPo(pcfl, 0);
    AssertIn(cbMax, 0, kcbMax);
    PCRF pcrf;

    if (pvNil != (pcrf = NewObj CRF(pcfl, cbMax)) && pvNil == (pcrf->_pglcre = GL::PglNew(SIZEOF(CRE), 5)))
    {
        ReleasePpo(&pcrf);
    }
    AssertNilOrPo(pcrf, 0);
    return pcrf;
}

/** 3DMMv1.0: *************************************************************************
    Set the size of the cache. This is most effecient when cbMax is 0
    (all non-required BACOs are flushed) or is bigger than the current
    cbMax.
***************************************************************************/
void CRF::SetCbMax(int32_t cbMax)
{
    AssertThis(0);
    AssertIn(cbMax, 0, kcbMax);

    if (0 == cbMax)
    {
        CRE cre;
        int32_t icre;

        for (icre = _pglcre->IvMac(); icre-- > 0;)
        {
            _pglcre->Get(icre, &cre);
            AssertPo(cre.pbaco, 0);
            if (cre.pbaco->CactRef() == 0)
            {
                Assert(cre.pbaco->_fAttached, "BACO not attached!");
                cre.pbaco->Detach();

                // 3DMMv1.0: have to start over in case other BACOs got deleted or
                // 3DMMv1.0: reference counts went to zero
                icre = _pglcre->IvMac();
            }
        }
    }
    else if (_cbCur > cbMax)
        _FPurgeCb(_cbCur - cbMax, klwMax);

    _cbMax = cbMax;
}

/** 3DMMv1.0: *************************************************************************
    Pre-fetch the object.  Returns tYes if the chunk is successfully cached,
    tNo if the chunk isn't in the CRF and tMaybe if there wasn't room
    to cache the chunk.
***************************************************************************/
tribool CRF::TLoad(CTG ctg, CNO cno, PFNRPO pfnrpo, RSC rsc, int32_t crep)
{
    AssertThis(0);
    Assert(pvNil != pfnrpo, "bad pfnrpo");
    Assert(crep > crepToss, "crep too small");
    CRE cre;
    int32_t icre;
    BLCK blck;

    // 3DMMv1.0: see if this CRF contains this resource type
    if (rscNil != rsc && !_pcfl->FFind(kctgRsc, rsc))
        return tNo;

    // 3DMMv1.0: see if it's in the cache
    if (_FFindCre(ctg, cno, pfnrpo, &icre))
    {
        _pglcre->Get(icre, &cre);
        cre.pbaco->SetCrep(LwMax(cre.pbaco->_crep, crep));
        cre.cactRelease = _cactRelease++;
        _pglcre->Put(icre, &cre);
        return tYes;
    }

    // 3DMMv1.0: see if it's in the chunky file
    if (!_pcfl->FFind(ctg, cno, &blck))
        return tNo;

    // 3DMMv1.0: get the approximate size of the object
    if (!(*pfnrpo)(this, ctg, cno, &blck, pvNil, &cre.cb))
        return tMaybe;

    if (_cbCur + cre.cb > _cbMax)
    {
        if (!_FPurgeCb(_cbCur + cre.cb - _cbMax, crep - 1))
            return tMaybe;
    }

    if (!(*pfnrpo)(this, ctg, cno, &blck, &cre.pbaco, &cre.cb))
        return tMaybe;

    AssertPo(cre.pbaco, 0);
    AssertIn(cre.cb, 0, kcbMax);

    if (_cbCur + cre.cb > _cbMax && !_FPurgeCb(_cbCur + cre.cb - _cbMax, crep - 1))
    {
        ReleasePpo(&cre.pbaco);
        return tMaybe;
    }

    cre.pbaco->_pcrf = this;
    cre.pbaco->_ctg = ctg;
    cre.pbaco->_cno = cno;
    cre.pbaco->_crep = crep;

    AddRef(); // 3DMMv1.0: until the baco is attached it needs a reference count
    cre.pbaco->_fAttached = fFalse;
    cre.pfnrpo = pfnrpo;
    cre.cactRelease = _cactRelease++;

    // 3DMMv1.0: indexes may have changed, get the location to insert again
    AssertDo(!_FFindCre(ctg, cno, pfnrpo, &icre), "how did this happen?");

    if (!_pglcre->FInsert(icre, &cre))
    {
        // 3DMMv1.0: can't keep it loaded
        ReleasePpo(&cre.pbaco);
        return tMaybe;
    }

    _cbCur += cre.cb;
    cre.pbaco->_fAttached = fTrue;
    cre.pbaco->Release();
    Release(); // 3DMMv1.0: baco successfully attached, so release its reference count

    return tYes;
}

/** 3DMMv1.0: *************************************************************************
    Make sure the object is loaded and increment its reference count.  If
    successful, must be balanced with a call to ReleasePpo.
***************************************************************************/
PBACO CRF::PbacoFetch(CTG ctg, CNO cno, PFNRPO pfnrpo, bool *pfError, RSC rsc)
{
    AssertThis(0);
    Assert(pvNil != pfnrpo, "bad pfnrpo");
    AssertNilOrVarMem(pfError);
    CRE cre;
    int32_t icre;
    BLCK blck;

    if (pvNil != pfError)
        *pfError = fFalse;

    // 3DMMv1.0: see if this CRF contains this resource type
    if (rscNil != rsc && !_pcfl->FFind(kctgRsc, rsc))
        return pvNil;

    // 3DMMv1.0: see if it's in the cache
    if (_FFindCre(ctg, cno, pfnrpo, &icre))
    {
        _pglcre->Get(icre, &cre);
        AssertPo(cre.pbaco, 0);
        cre.pbaco->AddRef();
        return cre.pbaco;
    }

    // 3DMMv1.0: see if it's in the chunky file
    if (!_pcfl->FFind(ctg, cno, &blck))
        return pvNil;

    // 3DMMv1.0: get the object and its size
    if (!(*pfnrpo)(this, ctg, cno, &blck, &cre.pbaco, &cre.cb))
    {
        if (pvNil != pfError)
            *pfError = fTrue;
        PushErc(ercCrfCantLoad);
        return pvNil;
    }

    AssertPo(cre.pbaco, 0);
    AssertIn(cre.cb, 0, kcbMax);

    cre.pbaco->_pcrf = this;
    cre.pbaco->_ctg = ctg;
    cre.pbaco->_cno = cno;
    cre.pbaco->_crep = crepNormal;

    AddRef();
    cre.pbaco->_fAttached = fFalse;
    cre.pfnrpo = pfnrpo;

    // 3DMMv1.0: indexes may have changed, get the location to insert again
    AssertDo(!_FFindCre(ctg, cno, pfnrpo, &icre), "how did this happen?");

    if (!_pglcre->FInsert(icre, &cre))
    {
        // 3DMMv1.0: return the pbaco anyway.  when it's released it will go away
        if (pvNil != pfError)
            *pfError = fTrue;
        return cre.pbaco;
    }

    _cbCur += cre.cb;
    cre.pbaco->_fAttached = fTrue;
    Release();

    if (_cbCur > _cbMax)
    {
        // 3DMMv1.0: purge some stuff
        _FPurgeCb(_cbCur - _cbMax, klwMax);
    }

    return cre.pbaco;
}

/** 3DMMv1.0: *************************************************************************
    If the object is loaded, increment its reference count and return it.
    If it's not already loaded, just return nil.
***************************************************************************/
PBACO CRF::PbacoFind(CTG ctg, CNO cno, PFNRPO pfnrpo, RSC rsc)
{
    AssertThis(0);
    Assert(pvNil != pfnrpo, "bad pfnrpo");

    CRE cre;
    int32_t icre;

    // 3DMMv1.0: see if it's in the cache
    if (!_FFindCre(ctg, cno, pfnrpo, &icre) || rscNil != rsc && !_pcfl->FFind(kctgRsc, rsc))
    {
        return pvNil;
    }

    _pglcre->Get(icre, &cre);
    AssertPo(cre.pbaco, 0);
    cre.pbaco->AddRef();
    return cre.pbaco;
}

/** 3DMMv1.0: *************************************************************************
    If the baco indicated chunk is cached, set its crep.  Returns true
    iff the baco was cached.
***************************************************************************/
bool CRF::FSetCrep(int32_t crep, CTG ctg, CNO cno, PFNRPO pfnrpo, RSC rsc)
{
    AssertThis(0);
    Assert(pvNil != pfnrpo, "bad pfnrpo");

    CRE cre;
    int32_t icre;

    // 3DMMv1.0: see if it's in the cache
    if (!_FFindCre(ctg, cno, pfnrpo, &icre) || rscNil != rsc && !_pcfl->FFind(kctgRsc, rsc))
    {
        return fFalse;
    }

    _pglcre->Get(icre, &cre);
    AssertPo(cre.pbaco, 0);
    cre.pbaco->SetCrep(crep);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Return this if the chunk is in this crf, otherwise return nil. The
    caller is not given a reference count.
***************************************************************************/
PCRF CRF::PcrfFindChunk(CTG ctg, CNO cno, RSC rsc)
{
    AssertThis(0);

    if (!_pcfl->FFind(ctg, cno) || rscNil != rsc && !_pcfl->FFind(kctgRsc, rsc))
    {
        return pvNil;
    }

    return this;
}

/** 3DMMv1.0: *************************************************************************
    Check the _fAttached flag.  If it's false, make sure the BACO is not
    in the CRF.
***************************************************************************/
void CRF::BacoDetached(PBACO pbaco)
{
    AssertThis(0);
    AssertPo(pbaco, 0);
    Assert(pbaco->_pcrf == this, "BACO doesn't have right CRF");
    int32_t icre;
    CRE cre;

    if (pbaco->_fAttached)
    {
        Bug("who's calling BacoDetached?");
        return;
    }
    if (!_FFindBaco(pbaco, &icre))
    {
        Bug("why isn't the BACO in the CRF?");
        return;
    }
    _pglcre->Get(icre, &cre);
    _cbCur -= cre.cb;
    AssertIn(_cbCur, 0, kcbMax);
    _pglcre->Delete(icre);
}

/** 3DMMv1.0: *************************************************************************
    The BACO was released.  See if it should be flushed.
***************************************************************************/
void CRF::BacoReleased(PBACO pbaco)
{
    AssertThis(0);
    AssertPo(pbaco, 0);
    Assert(pbaco->_pcrf == this, "BACO doesn't have right CRF");
    int32_t icre;
    CRE cre;

    if (!pbaco->_fAttached || pbaco->CactRef() != 0)
    {
        Bug("who's calling BacoReleased?");
        return;
    }

    if (!_FFindBaco(pbaco, &icre))
    {
        Bug("why isn't the BACO in the CRF?");
        return;
    }
    _pglcre->Get(icre, &cre);
    cre.cactRelease = _cactRelease++;
    _pglcre->Put(icre, &cre);

    if (pbaco->_crep <= crepToss || _cbCur > _cbMax)
    {
        // 3DMMv1.0: toss it
        pbaco->Detach();
    }
}

/** 3DMMv1.0: *************************************************************************
    Find the cre corresponding to the (ctg, cno, pfnrpo).  Set *picre to
    its location (or where it would be if it were in the list).
***************************************************************************/
bool CRF::_FFindCre(CTG ctg, CNO cno, PFNRPO pfnrpo, int32_t *picre)
{
    AssertThis(0);
    AssertVarMem(picre);
    CRE *qrgcre, *qcre;
    int32_t icreMin, icreLim, icre;

    // 3DMMv1.0: Do a binary search.  The CREs are sorted by (ctg, cno, pfnrpo).
    qrgcre = (CRE *)_pglcre->QvGet(0);
    for (icreMin = 0, icreLim = _pglcre->IvMac(); icreMin < icreLim;)
    {
        icre = (icreMin + icreLim) / 2;
        qcre = qrgcre + icre;
        AssertPo(qcre->pbaco, 0);
        if (ctg < qcre->pbaco->_ctg)
            icreLim = icre;
        else if (ctg > qcre->pbaco->_ctg)
            icreMin = icre + 1;
        else if (cno < qcre->pbaco->_cno)
            icreLim = icre;
        else if (cno > qcre->pbaco->_cno)
            icreMin = icre + 1;
        else if (pfnrpo == qcre->pfnrpo)
        {
            *picre = icre;
            return fTrue;
        }
        else if (pfnrpo < qcre->pfnrpo)
            icreLim = icre;
        else
            icreMin = icre + 1;
    }

    *picre = icreMin;
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Find the cre corresponding to the BACO.  Set *picre to its location.
***************************************************************************/
bool CRF::_FFindBaco(PBACO pbaco, int32_t *picre)
{
    AssertThis(0);
    AssertPo(pbaco, 0);
    Assert(pbaco->_pcrf == this, "BACO doesn't have right CRF");
    AssertVarMem(picre);
    CTG ctg;
    CNO cno;
    CRE *qrgcre, *qcre;
    int32_t icreMin, icreLim, icre;

    ctg = pbaco->_ctg;
    cno = pbaco->_cno;

    // 3DMMv1.0: Do a binary search.  The CREs are sorted by (ctg, cno, pfnrpo).
    qrgcre = (CRE *)_pglcre->QvGet(0);
    for (icreMin = 0, icreLim = _pglcre->IvMac(); icreMin < icreLim;)
    {
        icre = (icreMin + icreLim) / 2;
        qcre = qrgcre + icre;
        AssertPo(qcre->pbaco, 0);
        if (ctg < qcre->pbaco->_ctg)
            icreLim = icre;
        else if (ctg > qcre->pbaco->_ctg)
            icreMin = icre + 1;
        else if (cno < qcre->pbaco->_cno)
            icreLim = icre;
        else if (cno > qcre->pbaco->_cno)
            icreMin = icre + 1;
        else if (pbaco == qcre->pbaco)
        {
            *picre = icre;
            return fTrue;
        }
        else
        {
            // 3DMMv1.0: we've found the (ctg, cno), now look for the BACO
            for (icreMin = icre; icreMin-- > 0;)
            {
                qcre = qrgcre + icreMin;
                if (qcre->pbaco->_ctg != ctg || qcre->pbaco->_cno != cno)
                    break;
                if (qcre->pbaco == pbaco)
                {
                    *picre = icreMin;
                    return fTrue;
                }
            }
            for (icreLim = icre; ++icreLim < _pglcre->IvMac();)
            {
                qcre = qrgcre + icreLim;
                if (qcre->pbaco->_ctg != ctg || qcre->pbaco->_cno != cno)
                    break;
                if (qcre->pbaco == pbaco)
                {
                    *picre = icreLim;
                    return fTrue;
                }
            }
            TrashVar(picre);
            return fFalse;
        }
    }

    TrashVar(picre);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Try to purge at least cbPurge bytes of space.  Doesn't free anything
    with a crep > crepLast or that is locked.
***************************************************************************/
bool CRF::_FPurgeCb(int32_t cbPurge, int32_t crepLast)
{
    AssertThis(0);
    AssertIn(cbPurge, 1, kcbMax);
    if (crepLast <= crepToss)
        return fFalse;

    CRE cre;
    int32_t icreMac;

    while (0 < (icreMac = _pglcre->IvMac()))
    {
        // 3DMMv1.0: We want to find the "best" element to free.  This is determined by
        // 3DMMv1.0: keeping a "best so far" element, which we compare each element to.
        // 3DMMv1.0: If the cre has a larger crep, it is worse, so just continue.
        // 3DMMv1.0: If the cre has a smaller crep, it is better.  When the crep values
        // 3DMMv1.0: are the same, we score it based on when the cre was last released
        // 3DMMv1.0: (how many releases have happened since the cre was last used) and
        // 3DMMv1.0: how different the cb is from cbPurge.  Each release is worth
        // 3DMMv1.0: kcbRelease bytes.  Bytes short of cbPurge are considered worse
        // 3DMMv1.0: (by a factor of 3) than bytes beyond cbPurge, so we favor elements
        // 3DMMv1.0: that are larger than cbPurge.
        // 3DMMv1.0: REVIEW shonk: tune kcbRelease and the weighting factor...
        const int32_t kcbRelease = 256;
        int32_t icre, crep;
        int32_t lw, dcb;
        int32_t lwBest = klwMax;
        int32_t icreBest = ivNil;
        int32_t crepBest = crepLast;

        for (icre = 0; icre < icreMac; icre++)
        {
            _pglcre->Get(icre, &cre);
            AssertPo(cre.pbaco, 0);
            if (cre.pbaco->CactRef() > 0 || (crep = cre.pbaco->_crep) > crepBest)
            {
                continue;
            }
            Assert(crep <= crepBest, 0);
            AssertIn(cre.cactRelease, 0, _cactRelease);

            dcb = cre.cb - cbPurge;
            lw = -LwMul(kcbRelease, LwMin(kcbMax / kcbRelease, _cactRelease - cre.cactRelease)) + LwMul(2, LwAbs(dcb)) -
                 dcb;
            if (crep < crepBest || lw < lwBest)
            {
                icreBest = icre;
                crepBest = crep;
                lwBest = lw;
            }
        }

        if (ivNil == icreBest)
            return fFalse;

        _pglcre->Get(icreBest, &cre);
        Assert(cre.pbaco->_fAttached, "BACO not attached!");
        cre.pbaco->Detach();

        if (0 >= (cbPurge -= cre.cb))
            return fTrue;
    }

    return fFalse;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a CRF (chunky resource file).
***************************************************************************/
void CRF::AssertValid(uint32_t grf)
{
    CRF_PAR::AssertValid(fobjAllocated);
    AssertPo(_pglcre, 0);
    AssertPo(_pcfl, 0);
    AssertIn(_cbMax, 0, kcbMax);
    AssertIn(_cbCur, 0, kcbMax);
    AssertIn(_cactRelease, 0, kcbMax);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by a CRF.
***************************************************************************/
void CRF::MarkMem(void)
{
    AssertThis(0);
    int32_t icre;
    CRE cre;

    CRF_PAR::MarkMem();
    MarkMemObj(_pglcre);
    MarkMemObj(_pcfl);

    for (icre = _pglcre->IvMac(); icre-- > 0;)
    {
        _pglcre->Get(icre, &cre);
        AssertPo(cre.pbaco, 0);
        Assert(cre.pbaco->_fAttached, "baco claims to not be attached!");
        cre.pbaco->_fAttached = fTrue; // 3DMMv1.0: safety to avoid infinite recursion
        MarkMemObj(cre.pbaco);
    }
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Destructor for Chunky resource manager.
***************************************************************************/
CRM::~CRM(void)
{
    AssertBaseThis(fobjAllocated);
    int32_t ipcrf;
    PCRF pcrf;

    if (pvNil != _pglpcrf)
    {
        for (ipcrf = _pglpcrf->IvMac(); ipcrf-- > 0;)
        {
            _pglpcrf->Get(ipcrf, &pcrf);
            AssertPo(pcrf, 0);
            ReleasePpo(&pcrf);
        }
        ReleasePpo(&_pglpcrf);
    }
}

/** 3DMMv1.0: *************************************************************************
    Static method to create a new CRM.
***************************************************************************/
PCRM CRM::PcrmNew(int32_t ccrfInit)
{
    AssertIn(ccrfInit, 0, kcbMax);
    PCRM pcrm;

    if (pvNil == (pcrm = NewObj CRM()))
        return pvNil;
    if (pvNil == (pcrm->_pglpcrf = GL::PglNew(SIZEOF(PCRF), ccrfInit)))
    {
        ReleasePpo(&pcrm);
        return pvNil;
    }
    AssertPo(pcrm, 0);
    return pcrm;
}

/** 3DMMv1.0: *************************************************************************
    Prefetch the object if there is room in the cache.  Assigns the fetched
    object the given priority (crep).
***************************************************************************/
tribool CRM::TLoad(CTG ctg, CNO cno, PFNRPO pfnrpo, RSC rsc, int32_t crep)
{
    AssertThis(0);
    Assert(pvNil != pfnrpo, "nil object reader");
    PCRF pcrf;
    tribool t;
    int32_t ipcrf;
    int32_t cpcrf = _pglpcrf->IvMac();

    for (ipcrf = 0; ipcrf < cpcrf; ipcrf++)
    {
        _pglpcrf->Get(ipcrf, &pcrf);
        AssertPo(pcrf, 0);
        t = pcrf->TLoad(ctg, cno, pfnrpo, rsc, crep);
        if (t != tNo)
            return t;
    }
    return tNo;
}

/** 3DMMv1.0: *************************************************************************
    Make sure the object is loaded and increment its reference count.  If
    successful, must be balanced with a call to ReleasePpo.  If this fails,
    and pfError is not nil, *pfError is set iff the chunk exists but
    couldn't be loaded.
***************************************************************************/
PBACO CRM::PbacoFetch(CTG ctg, CNO cno, PFNRPO pfnrpo, bool *pfError, RSC rsc)
{
    AssertThis(0);
    Assert(pvNil != pfnrpo, "nil object reader");
    AssertNilOrVarMem(pfError);
    PCRF pcrf;
    int32_t ipcrf;
    bool fError = fFalse;
    PBACO pbaco = pvNil;
    int32_t cpcrf = _pglpcrf->IvMac();

    for (ipcrf = 0; ipcrf < cpcrf; ipcrf++)
    {
        _pglpcrf->Get(ipcrf, &pcrf);
        AssertPo(pcrf, 0);
        pbaco = pcrf->PbacoFetch(ctg, cno, pfnrpo, &fError, rsc);
        if (pvNil != pbaco || fError)
            break;
    }
    if (pvNil != pfError)
        *pfError = fError;
    AssertNilOrPo(pbaco, 0);
    return pbaco;
}

/** 3DMMv1.0: *************************************************************************
    If the object is loaded, increment its reference count and return it.
    If it's not already loaded, just return nil.
***************************************************************************/
PBACO CRM::PbacoFind(CTG ctg, CNO cno, PFNRPO pfnrpo, RSC rsc)
{
    AssertThis(0);
    Assert(pvNil != pfnrpo, "nil object reader");

    PCRF pcrf;

    if (pvNil == (pcrf = PcrfFindChunk(ctg, cno, rsc)))
        return pvNil;

    return pcrf->PbacoFind(ctg, cno, pfnrpo);
}

/** 3DMMv1.0: *************************************************************************
    If the chunk is cached, set its crep.  Returns true iff the chunk
    was cached.
***************************************************************************/
bool CRM::FSetCrep(int32_t crep, CTG ctg, CNO cno, PFNRPO pfnrpo, RSC rsc)
{
    AssertThis(0);
    Assert(pvNil != pfnrpo, "nil object reader");
    PCRF pcrf;
    int32_t ipcrf;
    int32_t cpcrf = _pglpcrf->IvMac();

    for (ipcrf = 0; ipcrf < cpcrf; ipcrf++)
    {
        _pglpcrf->Get(ipcrf, &pcrf);
        AssertPo(pcrf, 0);
        if (pcrf->FSetCrep(crep, ctg, cno, pfnrpo, rsc))
            return fTrue;
    }
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Return which CRF the given chunk is in. The caller is not given a
    reference count.
***************************************************************************/
PCRF CRM::PcrfFindChunk(CTG ctg, CNO cno, RSC rsc)
{
    AssertThis(0);
    PCRF pcrf;
    int32_t ipcrf;
    int32_t cpcrf = _pglpcrf->IvMac();

    for (ipcrf = 0; ipcrf < cpcrf; ipcrf++)
    {
        _pglpcrf->Get(ipcrf, &pcrf);
        AssertPo(pcrf, 0);

        if (pcrf->Pcfl()->FFind(ctg, cno) && (rscNil == rsc || pcrf->Pcfl()->FFind(kctgRsc, rsc)))
        {
            return pcrf;
        }
    }

    return pvNil;
}

/** 3DMMv1.0: *************************************************************************
    Add a chunky file to the list of chunky resource files, by
    creating the chunky resource file object and adding it to the GL
***************************************************************************/
bool CRM::FAddCfl(PCFL pcfl, int32_t cbMax, int32_t *piv)
{
    AssertThis(0);
    AssertPo(pcfl, 0);
    AssertIn(cbMax, 0, kcbMax);
    AssertNilOrVarMem(piv);

    PCRF pcrf;

    if (pvNil == (pcrf = CRF::PcrfNew(pcfl, cbMax)))
    {
        TrashVar(piv);
        return fFalse;
    }
    if (!_pglpcrf->FAdd(&pcrf, piv))
    {
        ReleasePpo(&pcrf);
        return fFalse;
    }
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Get the icrf'th CRF.
***************************************************************************/
PCRF CRM::PcrfGet(int32_t icrf)
{
    AssertThis(0);
    AssertIn(icrf, 0, kcbMax);
    PCRF pcrf;

    if (!FIn(icrf, 0, _pglpcrf->IvMac()))
        return pvNil;

    _pglpcrf->Get(icrf, &pcrf);
    AssertPo(pcrf, 0);
    return pcrf;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Check the sanity of the CRM
***************************************************************************/
void CRM::AssertValid(uint32_t grfobj)
{
    CRM_PAR::AssertValid(grfobj | fobjAllocated);
    AssertPo(_pglpcrf, 0);
}

/** 3DMMv1.0: *************************************************************************
    mark the memory associated with the CRM
***************************************************************************/
void CRM::MarkMem(void)
{
    AssertThis(0);
    int32_t ipcrf;
    int32_t cpcrf;
    PCRF pcrf;

    CRM_PAR::MarkMem();
    MarkMemObj(_pglpcrf);

    for (ipcrf = 0, cpcrf = _pglpcrf->IvMac(); ipcrf < cpcrf; ipcrf++)
    {
        _pglpcrf->Get(ipcrf, &pcrf);
        AssertPo(pcrf, 0);
        MarkMemObj(pcrf);
    }
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    A PFNRPO to read GHQ objects.
***************************************************************************/
bool GHQ::FReadGhq(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);
    GHQ *pghq;
    HQ hq;

    *pcb = pblck->Cb(fTrue);
    if (pvNil == ppbaco)
        return fTrue;

    if (!pblck->FUnpackData() || hqNil == (hq = pblck->HqFree()))
    {
        TrashVar(pcb);
        TrashVar(ppbaco);
        return fFalse;
    }
    *pcb = CbOfHq(hq);

    if (pvNil == (pghq = NewObj GHQ(hq)))
    {
        FreePhq(&hq);
        TrashVar(pcb);
        TrashVar(ppbaco);
        return fFalse;
    }
    *ppbaco = pghq;
    return fTrue;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a GHQ.
***************************************************************************/
void GHQ::AssertValid(uint32_t grf)
{
    GHQ_PAR::AssertValid(grf);
    if (hqNil != hq)
        AssertHq(hq);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the GHQ.
***************************************************************************/
void GHQ::MarkMem(void)
{
    GHQ_PAR::MarkMem();
    MarkHq(hq);
}
#endif // 3DMMv1.0: DEBUG

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a CABO.
***************************************************************************/
void CABO::AssertValid(uint32_t grf)
{
    CABO_PAR::AssertValid(grf);
    AssertNilOrPo(po, 0);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the CABO.
***************************************************************************/
void CABO::MarkMem(void)
{
    CABO_PAR::MarkMem();
    MarkMemObj(po);
}
#endif // 3DMMv1.0: DEBUG
