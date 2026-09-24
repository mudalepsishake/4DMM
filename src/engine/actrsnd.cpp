/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    Actor Sound.

    Primary Author: ******
    Status:  Reviewed

***************************************************************************/

#include "soc.h"

ASSERTNAME

/** 3DMMv1.0: *************************************************************************

    Add a sound event to the actor event list, and create an undo object

***************************************************************************/
bool ACTR::FSetSnd(PTAG ptag, tribool fLoop, tribool fQueue, tribool fMotionMatch, int32_t vlm, int32_t sty)
{
    AssertThis(0);
    AssertVarMem(ptag);
    AssertIn(sty, 0, styLim);

    PACTR pactrDup;

    if (!FDup(&pactrDup))
    {
        return fFalse;
    }

    if (!FSetSndCore(ptag, fLoop, fQueue, fMotionMatch, vlm, sty))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return fFalse;
    }

    if (!FCreateUndo(pactrDup, fTrue))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return fFalse;
    }

    ReleasePpo(&pactrDup);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Add a sound event to the actor event list
    For user sounds, the chid must also be fetched for the argument *ptag
    and stored with the event, since for user sounds, the cno can change
    (eg on scene import)
    Note: ptag is current because the sound browser has just completed building
    a current tag.

***************************************************************************/
bool ACTR::FSetSndCore(PTAG ptag, tribool fLoop, tribool fQueue, tribool fMotionMatch, int32_t vlm, int32_t sty)
{
    AssertThis(0);
    AssertVarMem(ptag);
    AssertIn(sty, 0, styLim);

    PMSND pmsnd = pvNil;
    AEVSND aevsnd;
    int32_t ccel;
    int32_t iaev;
    AEV *paev;
    int32_t sqn;
    int32_t cbVar;

    // 3DMMv1.0: Verify sound before including in the event list
    pmsnd = (PMSND)vptagm->PbacoFetch(ptag, MSND::FReadMsnd);
    if (pvNil == pmsnd)
        goto LFail;

    if (pmsnd->Sty() == styMidi)
    {
        // 3DMMv1.0: There are no midi actor sounds in the product
        PushErc(ercSocNoActrMidi);
        goto LFail;
    }

    // 3DMMv1.0: Verify space up front
    cbVar = kcbVarStep + kcbVarSnd;
    if (!_pggaev->FEnsureSpace(2, cbVar, fgrpNil))
        return fFalse;

    aevsnd.fLoop = fLoop;
    aevsnd.fQueue = fQueue;
    aevsnd.vlm = (vlm == vlmNil) ? pmsnd->Vlm() : vlm;
    aevsnd.tag = *ptag;
    aevsnd.fNoSound = pmsnd->FNoSound();
    aevsnd.sty = (sty == styNil) ? pmsnd->Sty() : sty;
    sty = aevsnd.sty;
    vlm = aevsnd.vlm;

    if (ptag->sid != ksidUseCrf)
        TrashVar(&aevsnd.chid);
    else
    {
        if (!_pscen->Pmvie()->FChidFromUserSndCno(aevsnd.tag.cno, &aevsnd.chid))
            goto LFail;
    }

    if (!fMotionMatch)
        aevsnd.celn = smmNil; // 3DMMv1.0: Not a motion match sound
    else
    {
        if (!_ptmpl->FGetCcelActn(_anidCur, &ccel))
            goto LFail;
        aevsnd.celn = _celnCur % ccel;
    }

    if (!_FAddDoAev(aetSnd, kcbVarSnd, &aevsnd))
        goto LFail;

    ReleasePpo(&pmsnd);

    // 3DMMv1.0: Remove "no sounds" of this sqn between here and the next
    // 3DMMv1.0: sound of this sqn
    sqn = MSND::SqnActr(sty, _arid);
    for (iaev = _iaevCur; iaev < _pggaev->IvMac(); iaev++)
    {
        AEVSND aevsndT;
        paev = (AEV *)_pggaev->QvFixedGet(iaev);
        if (aetAdd == paev->aet)
            break;
        if (aetSnd != paev->aet)
            continue;
        _pggaev->Get(iaev, &aevsndT);
        if (MSND::SqnActr(aevsndT.sty, _arid) != sqn)
            continue;

        // 3DMMv1.0: Quit when reach real sound of same sqn
        if (!aevsndT.fNoSound)
            break;
        _RemoveAev(iaev);
        iaev--;
    }

    // 3DMMv1.0: Enqueue the motion match sounds in the Msq
    // 3DMMv1.0: Allow the edit even if all sounds do not play
    if (!(_pscen->GrfScen() & fscenSounds))
        _FEnqueueSmmInMsq(); // 3DMMv1.0: Ignore errors : other sounds involved

    Pscen()->Pmvie()->Pmcc()->SetSndFrame(fTrue);
    return fTrue;

LFail:
    ReleasePpo(&pmsnd);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Enqueue the sound at event iaev.
    Enter motion match sounds in the smm gl.
    Enter non-motion match sounds in the Msq

***************************************************************************/
bool ACTR::_FEnqueueSnd(int32_t iaev)
{
    AssertThis(0);
    AssertIn(iaev, 0, _pggaev->IvMac());

    AEVSND aevsnd;
    PMSND pmsnd = pvNil;
    int32_t tool;

    _pggaev->Get(iaev, &aevsnd);

    //
    // 3DMMv1.0: Motion match sounds are not enqueued when event iaev is seen
    // 3DMMv1.0: Otherwise the sound would be played twice
    //
    if (aevsnd.celn != smmNil)
    {
        // 3DMMv1.0: Insert Smm for this cel in the gl
        return _FInsertSmm(iaev);
    }

    if (aevsnd.fLoop)
        tool = toolLooper;
    else
        tool = toolSounder;

    // 3DMMv1.0: If the scene was imported, the sound will need to be resolved
    if (aevsnd.tag.sid == ksidUseCrf)
    {
        if (!_pscen->Pmvie()->FResolveSndTag(&aevsnd.tag, aevsnd.chid))
        {
            Bug("Actrsnd: Expected to resolve snd tag");
            goto LFail;
        }
        _pggaev->Put(iaev, &aevsnd); // 3DMMv1.0: Update event
    }

    pmsnd = (PMSND)vptagm->PbacoFetch(&aevsnd.tag, MSND::FReadMsnd);
    if (pvNil == pmsnd)
        goto LFail;

    if (!_pscen->Pmvie()->Pmsq()->FEnqueue(pmsnd, _arid, aevsnd.fLoop, aevsnd.fQueue, aevsnd.vlm, pmsnd->Spr(tool),
                                           fTrue))
    {
        goto LFail;
    }

    ReleasePpo(&pmsnd);
    return fTrue;

LFail:
    ReleasePpo(&pmsnd);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Play the motion match sounds appropriate to the current cel

***************************************************************************/
bool ACTR::_FEnqueueSmmInMsq(void)
{
    AssertThis(0);

    int32_t celn;
    int32_t ccel;
    int32_t ismm;
    SMM *psmm;
    bool fSuccess = fTrue;
    PMSND pmsnd = pvNil;

    if (!_ptmpl->FGetCcelActn(_anidCur, &ccel))
        return fFalse;
    celn = _celnCur % ccel;

    for (ismm = 0; ismm < _pglsmm->IvMac(); ismm++)
    {
        psmm = (SMM *)(_pglsmm->QvGet(ismm));
        Assert(psmm->aevsnd.celn != smmNil, "Logic error in smm");
        if (psmm->aevsnd.celn != celn)
        {
            continue;
        }

        if (psmm->aevsnd.tag.sid == ksidUseCrf)
        {
            if (!_pscen->Pmvie()->FResolveSndTag(&psmm->aevsnd.tag, psmm->aevsnd.chid))
            {
                Bug("Actrsnd: Expected to resolve snd tag");
                goto LFail;
            }
            _pglsmm->Put(ismm, psmm); // 3DMMv1.0: Update event
        }

        pmsnd = (PMSND)vptagm->PbacoFetch(&psmm->aevsnd.tag, MSND::FReadMsnd);
        if (pvNil == pmsnd)
        {
            fSuccess = fFalse;
            continue;
        }

        // 3DMMv1.0: Motion match sounds need to be entered at low priority as sounds of the
        // 3DMMv1.0: same type take precedence over them
        if (!_pscen->Pmvie()->Pmsq()->FEnqueue(pmsnd, _arid, psmm->aevsnd.fLoop, psmm->aevsnd.fQueue, psmm->aevsnd.vlm,
                                               pmsnd->Spr(toolMatcher), fTrue, 0, fTrue))
        {
            // 3DMMv1.0: Continuing would result in additional error
            goto LFail;
        }

        ReleasePpo(&pmsnd);
    }

    return fSuccess;
LFail:
    ReleasePpo(&pmsnd);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Insert a motion match sound in the smm queue

***************************************************************************/
bool ACTR::_FInsertSmm(int32_t iaev)
{
    AssertThis(0);
    AssertIn(iaev, 0, _pggaev->IvMac());

    int32_t ismm;
    SMM smm;
    SMM *psmm;
    AEV *paev;
    AEVSND *paevsnd;

    paev = (AEV *)_pggaev->QvFixedGet(iaev);
    paevsnd = (AEVSND *)_pggaev->QvGet(iaev);

    smm.aev = *paev;
    smm.aevsnd = *paevsnd;

    for (ismm = 0; ismm < _pglsmm->IvMac(); ismm++)
    {
        psmm = (SMM *)_pglsmm->QvGet(ismm);
        if (psmm->aevsnd.celn != paevsnd->celn)
            continue;
        if (psmm->aevsnd.sty != paevsnd->sty)
            continue;

        // 3DMMv1.0: Replace the current smm with the new
        _pglsmm->Put(ismm, &smm);
        return fTrue;
    }

    // 3DMMv1.0: No sound for this cel was found
    // 3DMMv1.0: Add a new mm sound
    return _pglsmm->FAdd(&smm);
}

/** 3DMMv1.0: *************************************************************************

    Delete old motion sounds in event list

***************************************************************************/
bool ACTR::_FRemoveAevMm(int32_t anid)
{
    AssertThis(0);

    int32_t iaev;
    AEV *paev;
    AEVSND aevsnd;
    AEVACTN aevactn;

    // 3DMMv1.0: Remove motion match sounds from the previous action
    for (iaev = _iaevCur; iaev < _pggaev->IvMac(); iaev++)
    {
        paev = (AEV *)_pggaev->QvFixedGet(iaev);
        if (aetAdd == paev->aet)
            break;
        if (aetActn == paev->aet)
        {
            _pggaev->Get(iaev, &aevactn);
            if (aevactn.anid != anid)
                break;
            continue;
        }
        if (aetSnd != paev->aet)
            continue;

        // 3DMMv1.0: Test if sound is motion match
        _pggaev->Get(iaev, &aevsnd);
        if (smmNil == aevsnd.celn)
            continue;
        _RemoveAev(iaev);
        iaev--;
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Insert default motion match sounds in event list

***************************************************************************/
bool ACTR::_FAddAevDefMm(int32_t anid)
{
    AssertThis(0);
    TAG tag;
    PMSND pmsnd;
    int32_t iceln;
    int32_t ccel;
    int32_t vlm;
    int32_t sty;
    bool fSoundExists;

    // 3DMMv1.0: Insert new motion match sounds
    if (!_ptmpl->FGetCcelActn(anid, &ccel))
        return fFalse;

    for (iceln = 0; iceln < ccel; iceln++)
    {
        if (!_ptmpl->FGetSndActnCel(anid, iceln, &fSoundExists, &tag))
            continue; // 3DMMv1.0: Ignore failure
        if (!fSoundExists)
            continue;

        pmsnd = (PMSND)vptagm->PbacoFetch(&tag, MSND::FReadMsnd);
        if (pvNil == pmsnd)
            continue; // 3DMMv1.0: Ignore failure

        vlm = pmsnd->Vlm();
        sty = pmsnd->Sty();

        if (!FSetSndCore(&tag, tribool::tNo, (tribool)vlm, tribool::tNo, sty, tribool::tYes))
        {
            goto LFail; // 3DMMv1.0: Continuing pointless
        }

        ReleasePpo(&pmsnd);
    }

    return fTrue;

LFail:
    ReleasePpo(&pmsnd);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Set the Volume for sounds in the current frame of the specified sty

***************************************************************************/
bool ACTR::FSetVlmSnd(int32_t sty, bool fMotionMatch, int32_t vlm)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);
    AssertIn(vlm, 0, kvlmFull + 1);

    AEVSND aevsnd;
    bool fActnThisFrame = fFalse;
    int32_t celn;
    int32_t ccel;
    int32_t iaev;
    int32_t ismm;
    AEV aev;
    SMM smm;
    int32_t nfrmMM = ivNil;

    if (!_ptmpl->FGetCcelActn(_anidCur, &ccel))
        return fFalse;

    celn = _celnCur % ccel;

    // 3DMMv1.0: Set the volume for any events in the actor list for this frame
    for (iaev = _iaevFrmMin; iaev < _iaevCur; iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);
        if (aev.aet == aetActn)
            fActnThisFrame = fTrue;

        if (aev.aet != aetSnd)
            continue;

        _pggaev->Get(iaev, &aevsnd);
        if (FPure(aevsnd.celn != smmNil) != fMotionMatch)
            continue;

        if (aevsnd.sty != sty)
            continue;
        aevsnd.vlm = vlm;
        _pggaev->Put(iaev, &aevsnd);
    }

    // 3DMMv1.0: Set the volume for any earlier motion match sounds
    // 3DMMv1.0: which land in this cel
    if (fMotionMatch && !fActnThisFrame)
    {
        for (iaev = _iaevFrmMin - 1; iaev > 0; iaev--)
        {
            _pggaev->GetFixed(iaev, &aev);

            // 3DMMv1.0: Quit when the action changes
            if (aev.aet == aetActn || (nfrmMM != ivNil && nfrmMM > aev.nfrm))
                break;

            if (aev.aet != aetSnd)
                continue;
            _pggaev->Get(iaev, &aevsnd);

            if (aevsnd.sty != sty)
                continue;

            // 3DMMv1.0: Only adjust sounds that match this cel
            if (aevsnd.celn != celn)
                continue;
            aevsnd.vlm = vlm;
            _pggaev->Put(iaev, &aevsnd);
            nfrmMM = aev.nfrm;
            // 3DMMv1.0: There are no queued motion match sounds by spec
            break;
        }
    }

    if (fMotionMatch)
    {
        // 3DMMv1.0: Adjust the volume in the smm also
        for (ismm = 0; ismm < _pglsmm->IvMac(); ismm++)
        {
            _pglsmm->Get(ismm, &smm);
            Assert(smm.aevsnd.celn != smmNil, "Logic error in smm");

            if (smm.aevsnd.celn != celn)
                continue;

            if (smm.aevsnd.sty != sty)
                continue;

            // 3DMMv1.0: Set the volume in the smm
            smm.aevsnd.vlm = vlm;
            _pglsmm->Put(ismm, &smm);
        }
    }

    _pscen->MarkDirty();
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Query actor for current frame sounds

***************************************************************************/
bool ACTR::FQuerySnd(int32_t sty, bool fMotionMatch, PGL *pglTagSnd, int32_t *pvlm, bool *pfLoop)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);
    AssertVarMem(pvlm);
    AssertVarMem(pfLoop);

    PGL pgltag = pvNil;
    AEVSND aevsnd;
    int32_t ccel;
    int32_t celn;
    AEV aev;
    int32_t iaev;
    int32_t ismm;
    SMM smm;

    if (pvNil == (pgltag = GL::PglNew(SIZEOF(TAG), kctagSndGrow)))
        return fFalse;
    pgltag->SetMinGrow(kctagSndGrow);

    if (!fMotionMatch)
    {
        // 3DMMv1.0: Non-motion match sounds
        for (iaev = _iaevFrmMin; iaev < _iaevCur; iaev++)
        {
            _pggaev->GetFixed(iaev, &aev);
            if (aev.aet != aetSnd)
                continue;

            _pggaev->Get(iaev, &aevsnd);
            if (aevsnd.sty != sty)
                continue;

            // 3DMMv1.0: Handle motion match sounds later
            if (aevsnd.celn != smmNil)
                continue;
            if (aevsnd.tag.sid == ksidUseCrf)
            {
                if (!_pscen->Pmvie()->FResolveSndTag(&aevsnd.tag, aevsnd.chid))
                    goto LFail;
            }
            if (!pgltag->FAdd(&aevsnd.tag))
                goto LFail;
            *pvlm = aevsnd.vlm;
            *pfLoop = aevsnd.fLoop;
        }
    }

    if (fMotionMatch)
    {
        if (!_ptmpl->FGetCcelActn(_anidCur, &ccel))
            goto LFail;

        celn = _celnCur % ccel;

        // 3DMMv1.0: Motion match sounds
        for (ismm = 0; ismm < _pglsmm->IvMac(); ismm++)
        {
            _pglsmm->Get(ismm, &smm);
            if (smm.aevsnd.sty != sty)
                continue;
            if (smm.aevsnd.celn != celn)
                continue;
            if (smm.aevsnd.tag.sid == ksidUseCrf)
            {
                if (!_pscen->Pmvie()->FResolveSndTag(&smm.aevsnd.tag, smm.aevsnd.chid))
                    goto LFail;
            }
            if (!pgltag->FAdd(&smm.aevsnd.tag))
                goto LFail;
            *pvlm = smm.aevsnd.vlm;
            *pfLoop = smm.aevsnd.fLoop;
            break;
        }
    }

    if (pgltag->IvMac() == 0)
    {
        ReleasePpo(&pgltag);
        *pglTagSnd = pvNil;
        return fTrue;
    }

    *pglTagSnd = pgltag;
    return fTrue;

LFail:
    ReleasePpo(&pgltag);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Delete a sound in this frame

***************************************************************************/
bool ACTR::FDeleteSndCore(int32_t sty, bool fMotionMatch)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);

    AEVSND aevsnd;
    int32_t iaevFirst;
    int32_t iaev;
    int32_t ismm;
    int32_t ccel;
    int32_t celn;
    AEV aev;
    SMM smm;
    int32_t nfrmMM = ivNil; // 3DMMv1.0: For motion match, this may be an earlier frame

    // 3DMMv1.0: First the actor event list
    if (!_ptmpl->FGetCcelActn(_anidCur, &ccel))
        return fFalse;
    celn = _celnCur % ccel;

    iaevFirst = (fMotionMatch) ? _iaevActnCur : _iaevFrmMin;
    for (iaev = _iaevCur - 1; iaev >= iaevFirst; iaev--)
    {
        _pggaev->GetFixed(iaev, &aev);
        if (fMotionMatch && nfrmMM != ivNil && nfrmMM > aev.nfrm)
            break;

        if (aev.aet != aetSnd)
            continue;

        _pggaev->Get(iaev, &aevsnd);
        if (aevsnd.sty != sty)
            continue;
        if ((aevsnd.celn != smmNil) != fMotionMatch)
            continue;

        // 3DMMv1.0: Only delete the sound that match this cel
        if (fMotionMatch && aevsnd.celn != celn)
            continue;

        // 3DMMv1.0: Delete this sound & continue to remove any
        // 3DMMv1.0: other sounds which are possibly chained
        _RemoveAev(iaev);
        nfrmMM = aev.nfrm;
    }

    if (fMotionMatch)
    {
        // 3DMMv1.0: Also the motion match sound list : smm
        if (!_ptmpl->FGetCcelActn(_anidCur, &ccel))
            return fFalse;
        celn = _celnCur % ccel;

        // 3DMMv1.0: Motion match sounds
        for (ismm = 0; ismm < _pglsmm->IvMac(); ismm++)
        {
            _pglsmm->Get(ismm, &smm);
            if (smm.aevsnd.sty != sty)
                continue;
            if (smm.aevsnd.celn != celn)
                continue;
            _pglsmm->Delete(ismm);
            ismm--;
        }
    }

    _pscen->MarkDirty();
    return fTrue;
}

/** 3DMMv1.0: ****************************************************************************
    FSoundInFrm
        Enumerates all actor events for current frame looking for a sound
        event.

    Returns: fTrue if a sound event was found, fFalse otherwise

************************************************************ PETED ***********/
bool ACTR::FSoundInFrm(void)
{
    AssertThis(0);
    AssertPo(Pscen(), 0);

    /* 3DMMv1.0: Can't provide useful info if we're not at the current scene frame */
    if (Pscen()->Nfrm() != _nfrmCur)
        return fFalse;

    for (int32_t iaev = _iaevFrmMin; iaev < _iaevCur; iaev++)
    {
        AEV aev;

        _pggaev->GetFixed(iaev, &aev);
        if (aev.aet == aetSnd)
            return fTrue;
    }
    return fFalse;
}

/** 3DMMv1.0: ****************************************************************************
    Resolve all sound tags

******************************************************************************/
bool ACTR::FResolveAllSndTags(CNO cnoScen)
{
    AssertThis(0);
    int32_t iaev;
    AEV *paev;
    AEVSND aevsnd;
    bool fSuccess = fTrue;

    for (iaev = 0; iaev < _pggaev->IvMac(); iaev++)
    {
        paev = (AEV *)_pggaev->QvFixedGet(iaev);
        if (paev->aet != aetSnd)
            continue;
        _pggaev->Get(iaev, &aevsnd);
        if (aevsnd.tag.sid == ksidUseCrf)
        {
            if (!_pscen->Pmvie()->FResolveSndTag(&aevsnd.tag, aevsnd.chid, cnoScen))
            {
                fSuccess = fFalse;
                continue;
            }
            _pggaev->Put(iaev, &aevsnd);
        }
    }
    return fSuccess;
}
