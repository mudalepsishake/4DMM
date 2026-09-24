/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    Actor Edit.   Cut/Copy/Paste/Undo

    Primary authors:
        ACLP::(clipbd)	Seanse
        AUND::(undo)	Seanse
        ACTR::(undo)	Seanse
        ACTR::(vacuum)  *****
        ACTR::(dup/restore) *****
    Review Status:  Reviewed

***************************************************************************/

#include "soc.h"

ASSERTNAME
RTCLASS(AUND)
RTCLASS(GUND)

/** 3DMMv1.0: *************************************************************************

    Duplicate the actor from this frame through
    the end of subroute or (if fEntireScene) the end of the scene

***************************************************************************/
bool ACTR::FCopy(PACTR *ppactr, bool fEntireScene)
{
    AssertThis(0);
    AssertVarMem(ppactr);

    int32_t iaev;
    int32_t iaevLast;
    AEV aev;
    AEVACTN aevactn;
    AEVSND aevsnd;
    RPT rpt;
    RPT rptOld;
    RPT *prptSrc;
    RPT *prptDest;

    (*ppactr) = PactrNew(&_tagTmpl);

    if (*ppactr == pvNil)
    {
        return fFalse;
    }

    // 3DMMv1.0: If the current point is between nodes, a node will
    // 3DMMv1.0: be inserted.  Compute its dwr.
    _pglrpt->Get(_rtelCur.irpt, &rpt);
    rptOld.dwr = rpt.dwr;
    rpt.dwr = BrsSub(rptOld.dwr, _rtelCur.dwrOffset);

    //
    // 3DMMv1.0: Gather all earlier events & move to the current one
    // 3DMMv1.0: It is sufficient to begin with the current subroute
    // 3DMMv1.0: Note: Add events will require later translation of nfrm
    //
    for (iaev = _iaevAddCur; iaev < _iaevCur; iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);

        // 3DMMv1.0: Is this an event we want to copy?
        switch (aev.aet)
        {
        case aetActn: // 3DMMv1.0: Copy, editing current cel only
            _pggaev->Get(iaev, &aevactn);
            aevactn.celn = _celnCur;
            break;

        // 3DMMv1.0: copy
        case aetAdd:
        case aetCost:
        case aetPull:
        case aetSize:
        case aetRotF:
        case aetFreeze:
        case aetStep:
        case aetMove:
            break;

        // 3DMMv1.0: The following events are not automatically copied
        case aetSnd:
            _pggaev->Get(iaev, &aevsnd);
            // 3DMMv1.0: Update the cno for the chid from the original movie
            // 3DMMv1.0: The scene this came from may be lost later
            if (!_pscen->Pmvie()->FResolveSndTag(&aevsnd.tag, aevsnd.chid))
            {
                goto LFail;
            }
            _pggaev->Put(iaev, &aevsnd);
            if (aevsnd.celn == smmNil && iaev >= _iaevFrmMin)
                break;
            if (iaev > _iaevActnCur && aevsnd.celn != smmNil)
            {
                // 3DMMv1.0: Retain current motion match sounds
                break;
            }
            continue;
        case aetTweak:
        case aetRotH:
#ifdef BUG1950
            // 3DMMv1.0: REVIEW (*****):  Postponed till v2.0
            if (iaev >= _iaevCur) // 3DMMv1.0: Code change not yet verified
#else                             //! 3DMMv1.0: BUG1950
            if (iaev >= _iaevFrmMin)
#endif                            //! 3DMMv1.0: BUG1950
            {
                // 3DMMv1.0: Retain these events from this frame
                break;
            }
            continue;
        case aetRem:
            continue; // 3DMMv1.0: Do not copy

        default:
            Bug("Unknown event type... Copy or Not?");
            break;
        }

        // 3DMMv1.0: set the event to happen right here.
        aev.rtel.dnfrm = 0;
        aev.rtel.irpt = 0;
        aev.rtel.dwrOffset = 0;
        iaevLast = (*ppactr)->_pggaev->IvMac();

        // 3DMMv1.0: Insert aev.  Tag ref count will be updated.
        _pggaev->Lock();
        if (!(*ppactr)->_FInsertAev(iaevLast, _pggaev->Cb(iaev), (aev.aet == aetActn) ? &aevactn : _pggaev->QvGet(iaev),
                                    &aev, fFalse))
        {
            _pggaev->Unlock();
            goto LFail;
        }
        _pggaev->Unlock();
        (*ppactr)->_MergeAev(0, iaevLast);
    }

    // A loaded actor can be displayed with orientation state that is no
    // longer represented by the Add event alone.  Copying only the old event
    // stream therefore made a pasted prop jump back to an obsolete angle.
    // Capture the exact current orientation before appending future events.
    if ((*ppactr)->_pggaev->IvMac() > 0)
    {
        AEV aevFirst;
        (*ppactr)->_pggaev->GetFixed(0, &aevFirst);
        Assert(aevFirst.aet == aetAdd, "Copied actor must begin with Add");

        if (_fUseBmat34Cur)
        {
            bool fFoundRotH = fFalse;
            AEV aevT;

            for (int32_t iaevT = 1; iaevT < (*ppactr)->_pggaev->IvMac(); iaevT++)
            {
                (*ppactr)->_pggaev->GetFixed(iaevT, &aevT);
                if (aevT.aet == aetRotH)
                {
                    (*ppactr)->_pggaev->Put(iaevT, &_xfrm.bmat34Cur);
                    fFoundRotH = fTrue;
                }
            }

            if (!fFoundRotH)
            {
                AEV aevRot = aevFirst;
                aevRot.aet = aetRotH;
                iaevLast = (*ppactr)->_pggaev->IvMac();
                if (!(*ppactr)->_FInsertAev(iaevLast, kcbVarRot, &_xfrm.bmat34Cur, &aevRot, fFalse))
                {
                    goto LFail;
                }
            }
        }
        else
        {
            // Moving actors derive part of their orientation from the path.
            // Baking those path angles into the copied Add event preserves the
            // first-frame pose without freezing later route orientation.
            AEVADD aevaddCopy;
            (*ppactr)->_pggaev->Get(0, &aevaddCopy);
            aevaddCopy.xa = _xfrm.xaPath;
            aevaddCopy.ya = _xfrm.yaPath;
            aevaddCopy.za = _xfrm.zaPath;
            (*ppactr)->_pggaev->Put(0, &aevaddCopy);
        }
    }

    //
    // 3DMMv1.0: Copy remaining events, adjusting their locations
    //
    for (iaev = _iaevCur; iaev < _pggaev->IvMac(); iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);

        if ((!fEntireScene) && (aetAdd == aev.aet) && (*ppactr)->_pggaev->IvMac() > 0)
        {
            break;
        }

        // 3DMMv1.0: Adjust the event indicies.
        aev.rtel.irpt -= _rtelCur.irpt;

        if (aev.rtel.irpt == 0)
        {
            if (aev.rtel.dwrOffset == _rtelCur.dwrOffset)
            {
                aev.rtel.dwrOffset = rZero;
                if (aev.rtel.dnfrm < _rtelCur.dnfrm)
                {
                    aev.rtel.dnfrm = 0;
                }
                else
                {
                    aev.rtel.dnfrm -= _rtelCur.dnfrm;
                }
            }
            else
            {
                if (rpt.dwr == rZero)
                    aev.rtel.dwrOffset = rZero;
                else
                {
                    aev.rtel.dwrOffset = BrsSub(aev.rtel.dwrOffset, _rtelCur.dwrOffset);
                }
            }
        }

        // 3DMMv1.0: Insert will update the tag ref count
        iaevLast = (*ppactr)->_pggaev->IvMac();
        _pggaev->Lock();
        if (!(*ppactr)->_FInsertAev(iaevLast, _pggaev->Cb(iaev), _pggaev->QvGet(iaev), &aev, fFalse))
        {
            _pggaev->Unlock();
            goto LFail;
        }
        _pggaev->Unlock();
    }

    //
    // 3DMMv1.0: Add the point we are at.
    //
    if (!(*ppactr)->_pglrpt->FInsert(0, &rpt))
    {
        goto LFail;
    }

    //
    // 3DMMv1.0: Copy the rest of the subroute or route
    //
    if ((_rtelCur.irpt + 1) < _pglrpt->IvMac())
    {
        // 3DMMv1.0: Locate the amount of route to copy
        //
        int32_t irptLim = _pglrpt->IvMac();
        int32_t irpt;

        if (!fEntireScene)
        {
            for (irpt = _rtelCur.irpt; irpt < irptLim; irpt++)
            {
                _pglrpt->Get(irpt, &rpt);
                if (rZero == rpt.dwr)
                {
                    irptLim = irpt + 1;
                    break;
                }
            }
        }

        if (!(*ppactr)->_pglrpt->FSetIvMac((*ppactr)->_pglrpt->IvMac() + irptLim - (_rtelCur.irpt + 1)))
        {
            goto LFail;
        }

        prptSrc = (RPT *)_pglrpt->QvGet(_rtelCur.irpt + 1);
        prptDest = (RPT *)(*ppactr)->_pglrpt->QvGet(1);
        CopyPb(prptSrc, prptDest, LwMul(irptLim - (_rtelCur.irpt + 1), SIZEOF(RPT)));
    }
    else
    {
        // 3DMMv1.0: Mark the end of the path
        rpt.dwr = rZero;
        (*ppactr)->_pglrpt->Put(0, &rpt);
    }

    // Preserve the source actor's overall world-space route translation.
    // The original clipboard path discarded this value and ACTR::FPaste()
    // always rebuilt it around the generic insertion point.
    (*ppactr)->_dxyzFullRte = _dxyzFullRte;

    (*ppactr)->_SetStateRewound();

    AssertPo(*ppactr, 0);
    return fTrue;

LFail:
    ReleasePpo(ppactr);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Duplicate the entire actor from *this onto *ppactr
    This is not from this frame on.  The entire actor is duplicated.
    This does not duplicate the Brender body.
    Default is fReset = fFalse;

    NOTE:
    Upon exit, the new actor will still be in the scene.
    If (!fReset), all state information will have been retained.

***************************************************************************/
bool ACTR::FDup(PACTR *ppactr, bool fReset)
{
    AssertThis(0);
    AssertVarMem(ppactr);

    int32_t cactRef;
    PACTR pactrSrc = this;
    PACTR pactrDest;

    // 3DMMv1.0: Due to state var duplication, using NewObj, not PactrNew
    pactrDest = (*ppactr) = NewObj ACTR();
    if (*ppactr == pvNil)
    {
        return fFalse;
    }

    // 3DMMv1.0: AddRef only if attached to a scene
    if (pvNil != _pbody)
        _pbody->AddRef();
    _ptmpl->AddRef();
    TAGM::DupTag(&_tagTmpl);

    // 3DMMv1.0: Copy over all members
    // 3DMMv1.0: Note that both copies will point to the same *_pbody & *_ptmpl
    cactRef = pactrDest->_cactRef;
    *(pactrDest) = *pactrSrc;
    pactrDest->_cactRef = cactRef;
    pactrDest->_fTimeFrozen = fFalse;

    if (!pactrDest->_FCreateGroups())
    {
        goto LFail;
    }

    if (!_FDupCopy(pactrSrc, pactrDest))
    {
        goto LFail;
    }

    if (fReset)
    {
        (*ppactr)->Reset();
    }
    return fTrue;

LFail:
    ReleasePpo(ppactr);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Restore the actor from *ppactr onto *this

***************************************************************************/
void ACTR::Restore(PACTR pactr)
{
    AssertThis(0);
    AssertVarMem(pactr);

    int32_t cactRef;
    PACTR pactrSrc = pactr;
    PACTR pactrDest = this;

    Assert(pactr->_ptmpl == _ptmpl, "Restore ptmpl logic error");
    Assert(pactr->_pbody == _pbody, "Restore pbody logic error");

    // 3DMMv1.0: Copy over all members
    // 3DMMv1.0: Note that both copies will point to the same *_pbody & *_ptmpl
    cactRef = pactrDest->_cactRef;
    PGG pggaev = pactrDest->_pggaev;
    PGL pglrpt = pactrDest->_pglrpt;
    PGL pglsmm = pactrDest->_pglsmm;
    *(pactrDest) = *pactrSrc;
    pactrDest->_cactRef = cactRef;
    pactrDest->_pggaev = pggaev;
    pactrDest->_pglrpt = pglrpt;
    pactrDest->_pglsmm = pglsmm;

    // 3DMMv1.0: Swap the gl and gg structures
    SwapVars(&pactrSrc->_pggaev, &pactrDest->_pggaev);
    SwapVars(&pactrSrc->_pglrpt, &pactrDest->_pglrpt);
    SwapVars(&pactrSrc->_pglsmm, &pactrDest->_pglsmm);

    pactr->_pscen->Pmvie()->InvalViews();

    return;
}

/** 3DMMv1.0: *************************************************************************

    Restore this actor from an undo object pactrRestore.

***************************************************************************/
void ACTR::_RestoreFromUndo(PACTR pactrRestore)
{
    AssertBaseThis(0);
    AssertVarMem(pactrRestore);
    Assert(pactrRestore->_pbody == pvNil, "Not restoring from undo object");

    int32_t nfrmCur = _nfrmCur;
    PSCEN pscen = pactrRestore->_pscen;

    // 3DMMv1.0: Modify pactrRestore for Restore()
    pactrRestore->_pbody = _pbody;
    pactrRestore->_pscen = _pscen;
    _Hide(); // 3DMMv1.0: Added actor will show
    Restore(pactrRestore);

    // 3DMMv1.0: Restore pactrRestore to be unmodified
    pactrRestore->_pbody = pvNil;
    pactrRestore->_pscen = pscen;

    FGotoFrame(nfrmCur); // 3DMMv1.0: No further recovery meaningful

    // This is a low-level edit-failure recovery path, not the user-facing
    // Undo/Redo command. Do not rebuild Light Lab here: route/record/resize
    // operations can call this while abandoning a no-op/failed gesture.
    _pscen->Pmvie()->InvalViews();
}

/** 3DMMv1.0: *************************************************************************

    Copy the GG and GL structures for actor duplication/restoration

    NOTE:
    This is not from this frame on.  The entire actor is duplicated.

***************************************************************************/
bool ACTR::_FDupCopy(PACTR pactrSrc, PACTR pactrDest)
{
    AssertBaseThis(0);
    AssertPo(pactrDest->_pggaev, 0);
    AssertPo(pactrDest->_pglrpt, 0);

    RPT *prptSrc;
    RPT *prptDest;
    SMM *psmmSrc;
    SMM *psmmDest;

    //
    // 3DMMv1.0: Copy all events.
    //
    if (!pactrDest->_pggaev->FCopyEntries(pactrSrc->_pggaev, 0, 0, pactrSrc->_pggaev->IvMac()))
    {
        goto LFail;
    }

    {
        _pggaev->Lock();
        for (int32_t iaev = 0; iaev < _pggaev->IvMac(); iaev++)
        {
            PTAG ptag;

            if (_FIsIaevTag(_pggaev, iaev, &ptag))
                TAGM::DupTag(ptag);
        }
        _pggaev->Unlock();
    }

    //
    // 3DMMv1.0: Copy Route
    //
    if (pactrSrc->_pglrpt->IvMac() > 0)
    {
        if (!pactrDest->_pglrpt->FSetIvMac(pactrSrc->_pglrpt->IvMac()))
        {
            goto LFail;
        }

        prptSrc = (RPT *)pactrSrc->_pglrpt->QvGet(0);
        prptDest = (RPT *)pactrDest->_pglrpt->QvGet(0);
        CopyPb(prptSrc, prptDest, LwMul(pactrSrc->_pglrpt->IvMac(), SIZEOF(RPT)));
    }

    //
    // 3DMMv1.0: Copy Smm
    //
    if (pactrSrc->_pglsmm->IvMac() > 0)
    {
        if (!pactrDest->_pglsmm->FSetIvMac(pactrSrc->_pglsmm->IvMac()))
        {
            goto LFail;
        }

        psmmSrc = (SMM *)pactrSrc->_pglsmm->QvGet(0);
        psmmDest = (SMM *)pactrDest->_pglsmm->QvGet(0);
        CopyPb(psmmSrc, psmmDest, LwMul(pactrSrc->_pglsmm->IvMac(), SIZEOF(SMM)));
    }

    return fTrue;

LFail:
    if (this != pactrDest)
        ReleasePpo(&pactrDest);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Duplicate the indicated portion of the route from this frame on
    from actor "this" to actor *ppactr.
    Note **: The point on the route may not land on a node.  If between
    nodes, insert a point to make the two route sections identical.

***************************************************************************/
bool ACTR::FCopyRte(PACTR *ppactr, bool fEntireScene)
{
    AssertThis(0);
    AssertVarMem(ppactr);

    int32_t irpt;
    int32_t dnrpt;
    int32_t irptLim;
    RPT rpt;
    RPT rpt1;
    RPT rptNode;

    (*ppactr) = PactrNew(&_tagTmpl);
    if (*ppactr == pvNil)
    {
        return fFalse;
    }

    //
    // 3DMMv1.0: Insert the current point
    //
    _GetXyzFromRtel(&_rtelCur, &rpt.xyz);
    _pglrpt->Get(_rtelCur.irpt, &rptNode);
    rpt.dwr = rZero;

    if (_rtelCur.dwrOffset == rZero)
    {
        rpt.dwr = rptNode.dwr;
    }
    else if (rptNode.dwr > rZero && _rtelCur.irpt < _pglrpt->IvMac() - 1)
    {
        _pglrpt->Get(_rtelCur.irpt + 1, &rpt1);
        rpt.dwr = BR_LENGTH3(BrsSub(rpt1.xyz.dxr, rpt.xyz.dxr), BrsSub(rpt1.xyz.dyr, rpt.xyz.dyr),
                             BrsSub(rpt1.xyz.dzr, rpt.xyz.dzr));
        if (rZero == rpt.dwr)
        {
            rpt.dwr = rEps; // 3DMMv1.0: Epsilon.  Prevent pathological incorrect end-of-path
        }
    }

    if (!(*ppactr)->_pglrpt->FInsert(0, &rpt))
    {
        goto LFail;
    }

    if ((!fEntireScene) && rpt.dwr == rZero)
    {
        goto LEnd;
    }

    //
    // 3DMMv1.0: If copying subroute only, determine amount to copy
    //
    irptLim = _pglrpt->IvMac();
    if (!fEntireScene)
    {
        for (irpt = _rtelCur.irpt + 1; irpt < irptLim; irpt++)
        {
            _pglrpt->Get(irpt, &rpt);
            if (rZero == rpt.dwr)
            {
                irptLim = irpt + 1;
                break;
            }
        }
    }

    //
    // 3DMMv1.0: Copy indicated portion of the route
    //
    dnrpt = irptLim - (_rtelCur.irpt + 1);
    if (dnrpt > 0 && !(*ppactr)->_pglrpt->FEnsureSpace(dnrpt, fgrpNil))
        goto LFail;

    for (irpt = _rtelCur.irpt + 1; irpt < irptLim; irpt++)
    {
        _pglrpt->Get(irpt, &rpt);

        if (!(*ppactr)->_pglrpt->FInsert(irpt - _rtelCur.irpt, &rpt))
        {
            goto LFail;
        }

        if ((!fEntireScene) && (rZero == rpt.dwr))
        {
            break;
        }
    }

LEnd:
    (*ppactr)->_SetStateRewound();
    return fTrue;

LFail:
    ReleasePpo(ppactr);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Paste the rte from the clipboard pactr to the current actor's current
    frame onward.
    On failure, unwinding is expected to be via the dup'd actor from undo.
    This overwrites the end of the current subroute.
    NOTE:  To be meaningful, the pasted route section is translated to
    extend from the current point.

***************************************************************************/
bool ACTR::FPasteRte(PACTR pactr)
{
    AssertThis(0);
    AssertVarMem(pactr);

    AEV aev;
    RPT rpt;
    RPT rptCur;
    XYZ dxyz;
    int32_t iaev;
    int32_t irpt;
#ifdef STATIC
    bool fStatic;
#endif // 3DMMv1.0: STATIC
    int32_t crptDel = 0;
    int32_t crptNew = pactr->_pglrpt->IvMac() - 1;

    if (crptNew <= 0)
    {
        PushErc(ercSocNothingToPaste);
        return fFalse;
    }

    if (!_pglrpt->FEnsureSpace(crptNew, fgrpNil) || !_pggaev->FEnsureSpace(1, kcbVarStep, fgrpNil))
    {
        return fFalse;
    }

    //
    // 3DMMv1.0: May be positioned between nodes -> potentially
    // 3DMMv1.0: insert the current point
    //
    _GetXyzFromRtel(&_rtelCur, &rptCur.xyz);
    if (rZero != _rtelCur.dwrOffset)
    {
        BRS dwrCur = _rtelCur.dwrOffset;
        _rtelCur.irpt++;
        _rtelCur.dwrOffset = rZero;
        _rtelCur.dnfrm = 0;
        if (!_FInsertGgRpt(_rtelCur.irpt, &rptCur, dwrCur))
            return fFalse;
        _GetXyzFromRtel(&_rtelCur, &_xyzCur);
        _AdjustAevForRteIns(_rtelCur.irpt, 0);
    }

    //
    // 3DMMv1.0: Delete to the end of this *sub*route
    //
    for (irpt = _rtelCur.irpt + 1; irpt < _pglrpt->IvMac(); irpt++)
    {
        _pglrpt->Get(irpt, &rpt);
        crptDel++;
        if (rpt.dwr == rZero)
        {
            break;
        }
    }
    if (crptDel > 0)
    {
        _pglrpt->Delete(_rtelCur.irpt, crptDel);
    }

    //
    // 3DMMv1.0: Remove events until the end of the *sub*route
    // 3DMMv1.0: Space optimization: should precede paste
    // 3DMMv1.0: Update location pointer of events of later subroutes
    //
    for (iaev = _iaevCur; iaev < _pggaev->IvMac(); iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);
        if (aev.rtel.irpt > _rtelCur.irpt + crptDel)
        {
            aev.rtel.irpt += crptNew - crptDel;
            _pggaev->PutFixed(iaev, &aev);
            continue;
        }
        else
        {
            _RemoveAev(iaev, fFalse);
            iaev--;
        }
    }

    //
    // 3DMMv1.0: Paste in the new route.
    // 3DMMv1.0: Translate the points of this section of route
    // 3DMMv1.0: Adjust the aev presently
    //
    pactr->_pglrpt->Get(0, &rpt);
    dxyz.dxr = BrsSub(rptCur.xyz.dxr, rpt.xyz.dxr);
    dxyz.dyr = BrsSub(rptCur.xyz.dyr, rpt.xyz.dyr);
    dxyz.dzr = BrsSub(rptCur.xyz.dzr, rpt.xyz.dzr);
    for (irpt = 1; irpt <= crptNew; irpt++)
    {
        pactr->_pglrpt->Get(irpt, &rpt);
        rpt.xyz.dxr = BrsAdd(rpt.xyz.dxr, dxyz.dxr);
        rpt.xyz.dyr = BrsAdd(rpt.xyz.dyr, dxyz.dyr);
        rpt.xyz.dzr = BrsAdd(rpt.xyz.dzr, dxyz.dzr);
        AssertDo(_pglrpt->FInsert(_rtelCur.irpt + irpt, &rpt), "Logic error");
    }

    //
    // 3DMMv1.0: Set the right dwr distance from the current point to
    // 3DMMv1.0: the first point on the new section of route
    //
    _pglrpt->Get(_rtelCur.irpt + 1, &rpt);
    rptCur.dwr = BR_LENGTH3(BrsSub(rpt.xyz.dxr, rptCur.xyz.dxr), BrsSub(rpt.xyz.dyr, rptCur.xyz.dyr),
                            BrsSub(rpt.xyz.dzr, rptCur.xyz.dzr));
    if (rZero == rptCur.dwr)
        rptCur.dwr = rEps; // 3DMMv1.0: Epsilon.  Prevent pathological incorrect end-of-path
    _pglrpt->Put(_rtelCur.irpt, &rptCur);

#ifdef STATIC
    //
    // 3DMMv1.0: Force floating behavior on a static action
    //
    Assert(_iaevActnCur >= 0, "Actor has no action");
    if (!_FGetStatic(_anidCur, &fStatic))
        return fFalse;
    if (fStatic)
    {
        if (!FSetStep(kdwrNil))
            return fFalse;
    }
#else  //! 3DMMv1.0: STATIC
    // 3DMMv1.0: Force continuation onto newly pasted path
    if (!FSetStep(kdwrNil))
        return fFalse;
#endif //! 3DMMv1.0: STATIC

    //
    // 3DMMv1.0: Set new end of path freeze & step events
    //
    int32_t faevfrz = (int32_t)fTrue;
    BRS dwrStep = rZero;
    aev.aet = aetFreeze;
    aev.rtel.irpt = _rtelCur.irpt + crptNew;
    aev.rtel.dnfrm = 0;
    aev.rtel.dwrOffset = rZero;
    aev.nfrm = _nfrmCur; // 3DMMv1.0: will be updated in ComputeLifetime()
    if (!_FInsertAev(_iaevCur, kcbVarFreeze, &faevfrz, &aev))
        return fFalse;
    aev.aet = aetStep;
    if (!_FInsertAev(_iaevCur, kcbVarStep, &dwrStep, &aev))
        return fFalse;

    _fLifeDirty = fTrue;
    _pscen->InvalFrmRange();
    _pscen->MarkDirty();

    _PositionBody(&_xyzCur);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Put an already existing actor in this scene.

***************************************************************************/
bool ACTR::FPaste(int32_t nfrm, SCEN *pscen, bool fInPlace)
{
    AssertThis(0);

    AEV aev;
    RPT rpt;
    AEVADD aevadd;
    AEVSND aevsnd;
    BRS xrCam = rZero;
    BRS yrCam = rZero;
    BRS zrCam = kzrDefault;
    BRS xr, yr, zr;
    int32_t iaev;
    int32_t dnfrm;
#ifdef BUG1888
    PTMPL ptmpl;
    PCRF pcrf;
    TAG tag;

    //
    // 3DMMv1.0: Ensure that the tag to the TDT being pasted is in the current movie.
    //
    if (FIsTdt())
    {
        Assert(_tagTmpl.sid == ksidUseCrf, "TDTs should be stored in a document!");

        if (!pscen->Pmvie()->FEnsureAutosave(&pcrf))
        {
            return fFalse;
        }
        if (pcrf != _tagTmpl.pcrf)
        {
            // 3DMMv1.0: Need to save this actor's tagTmpl in this movie because it came from another movie

            tag = _tagTmpl;
            TAGM::DupTag(&tag);
            // 3DMMv1.0: Save the tag to the movie's _pcrfAutosave.  The tag now
            // 3DMMv1.0: points to the copy in this movie.
            if (!TAGM::FSaveTag(&tag, pcrf, fTrue))
            {
                TAGM::CloseTag(&tag);
                return fFalse;
            }
            // 3DMMv1.0: Get a template based on the new tag
            ptmpl = (PTMPL)vptagm->PbacoFetch(&tag, TMPL::FReadTmpl);
            if (pvNil == ptmpl)
            {
                TAGM::CloseTag(&tag);
                return fFalse;
            }
            // 3DMMv1.0: Change the actor to use the new tag and template
            TAGM::CloseTag(&_tagTmpl);
            _tagTmpl = tag;
            ReleasePpo(&_ptmpl);
            _ptmpl = ptmpl;
        }
    }
#endif // 3DMMv1.0: BUG1888

    //
    // 3DMMv1.0: Update lifetime
    //
    _nfrmFirst = nfrm;
    _fLifeDirty = fTrue;
    _nfrmCur = nfrm - 1;
    SetPscen(pscen);

    if (!fInPlace)
    {
        // Original 3DMM behavior: place the actor at the generic insertion
        // point and then attach it to the mouse for manual positioning.
        _GetNewOrigin(&xr, &yr, &zr);
    }

    Assert(_pggaev->IvMac() > 0, "Nothing to paste!");
    _pggaev->GetFixed(0, &aev);
    if (aev.aet != aetAdd)
        return fFalse;
    dnfrm = aev.nfrm - nfrm;

    if (!fInPlace)
    {
        //
        // 3DMMv1.0: Begin by locating the actor at (xr,yr,zr)
        // 3DMMv1.0: There are no Full path or Sub path translations at this point.
        // 3DMMv1.0: Note: In order to place a pasted actor at the insertion point,
        // 3DMMv1.0: the translation needs to compensate for the distance
        // 3DMMv1.0: recorded in each path point -> subtract the first path point.
        _pglrpt->Get(0, &rpt);
        _dxyzSubRte.dxr = rZero;
        _dxyzSubRte.dyr = rZero;
        _dxyzSubRte.dzr = rZero;
        _dxyzFullRte.dxr = BrsSub(xr, rpt.xyz.dxr);
        _dxyzFullRte.dyr = BrsSub(yr, rpt.xyz.dyr);
        _dxyzFullRte.dzr = BrsSub(zr, rpt.xyz.dzr);
        _pggaev->Get(0, &aevadd);
        _dxyzFullRte.dxr = BrsSub(_dxyzFullRte.dxr, aevadd.dxr);
        _dxyzFullRte.dyr = BrsSub(_dxyzFullRte.dyr, aevadd.dyr);
        _dxyzFullRte.dzr = BrsSub(_dxyzFullRte.dzr, aevadd.dzr);
    }

    //
    // 3DMMv1.0: Translate the new actor in time
    // 3DMMv1.0: Update sound events
    for (iaev = 0; iaev < _pggaev->IvMac(); iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);
        if (dnfrm != 0)
        {
            aev.nfrm -= dnfrm;
            _pggaev->PutFixed(iaev, &aev);
        }
        if (aetSnd != aev.aet)
            continue;
        _pggaev->Get(iaev, &aevsnd);
        if (aevsnd.tag.sid != ksidUseCrf)
            continue;
        // 3DMMv1.0: Save tag (this may be a new movie)
        if (!aevsnd.tag.pcrf->Pcfl()->FFind(aevsnd.tag.ctg, aevsnd.tag.cno))
        {
            PushErc(ercSocNoSndOnPaste);
            _RemoveAev(iaev);
            iaev--;
            continue;
        }
        if (!_pscen->Pmvie()->FSaveTagSnd(&aevsnd.tag))
        {
            Bug("Expected to locate user sound chunk");
            _RemoveAev(iaev);
            iaev--;
        }
        else
        {
            // 3DMMv1.0: Adopt this sound into the new scene
            if (!_pscen->Pmvie()->FChidFromUserSndCno(aevsnd.tag.cno, &aevsnd.chid))
                return fFalse;
            // 3DMMv1.0: Update event
            _pggaev->Put(iaev, &aevsnd);
        }
    }

    // Rotate newly inserted 3D spletters to face the camera.  Clipboard
    // paste-in-place must retain the copied orientation instead.
    if (_ptmpl->FIsTdt() && !fInPlace)
    {
        _pggaev->Get(0, &aevadd);
        aevadd.ya = _pscen->Pbkgd()->BraRotYCamera() + _pscen->Pmvie()->BraCameraTrackYaw();
        _pggaev->Put(0, &aevadd);
    }

    _UpdateXyzRte();
    _pscen->MarkDirty();
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Make an actor look like they were just read in, and never in a scene.

***************************************************************************/
void ACTR::Reset(void)
{
    _pscen = pvNil;
    ReleasePpo(&_pbody); // 3DMMv1.0: Sets _pbody = pvNil
    _nfrmCur = knfrmInvalid;

    _InitState();
}

//
//
//
//
// 3DMMv1.0:  BEGIN CLIPBOARD STUFF
//
//
//
//


#ifdef DEBUG
void GUND::MarkMem(void)
{
    GUND_PAR::MarkMem();
    MarkMemObj(_pglentry);
    MarkPv(_pogstate);
    if (_pglentry != pvNil)
    {
        for (int32_t i = 0; i < _pglentry->IvMac(); i++)
        {
            GUNDENTRY entry;
            _pglentry->Get(i, &entry);
            MarkMemObj(entry.paund);
        }
    }
}

void GUND::AssertValid(uint32_t grf)
{
    GUND_PAR::AssertValid(grf);
    AssertNilOrPo(_pglentry, 0);
}
#endif

RTCLASS(ACLP)

/** 3DMMv1.0: *************************************************************************

    Create an actor clipboard object
    This is from the current frame forward

***************************************************************************/
PACLP ACLP::PaclpNew(PACTR pactr, bool fRteOnly, bool fEntireScene)
{
    AssertPo(pactr, 0);
    Assert(!fRteOnly || !fEntireScene, "Expecting subroute only");

    PACLP paclp;
    PACTR pactrTmp;
    STN stn, stnCopyOf;

    paclp = NewObj ACLP();

    if (paclp == pvNil)
    {
        return (pvNil);
    }

    if (fRteOnly)
    {
        if (!pactr->FCopyRte(&pactrTmp, fEntireScene))
        {
            ReleasePpo(&paclp);
            return (pvNil);
        }
    }
    else
    {
        if (!pactr->FCopy(&pactrTmp, fEntireScene))
        {
            ReleasePpo(&paclp);
            return (pvNil);
        }
        AssertPo(pactrTmp, 0);

        pactr->GetName(&stn);

        pactr->Pscen()->Pmvie()->Pmcc()->GetStn(idsEngineCopyOf, &stnCopyOf);

        if (!FEqualRgb(stn.Psz(), stnCopyOf.Psz(), CchSz(stnCopyOf.Psz()) * SIZEOF(achar)))
        {
            paclp->_stnName = stnCopyOf;
            if (!paclp->_stnName.FAppendCh(kchSpace) || !paclp->_stnName.FAppendStn(&stn))
            {
                PushErc(ercSocNameTooLong);
                ReleasePpo(&paclp);
                return (pvNil);
            }
        }
        else
        {
            paclp->_stnName = stn;
        }
    }

    // A copied actor carries its attached Light Lab settings in the clipboard
    // object.  The pasted actor gets a new ARID, so store only the settings
    // here and bind them to the new ARID during paste.
    if (!fRteOnly && pactr->Pscen() != pvNil)
    {
        LIGHTLAB light;
        PMVIE pmvie = pactr->Pscen()->Pmvie();
        if (pmvie != pvNil && pmvie->FGetLightLabConfig(pmvie->Iscen(), pactr->Arid(), &light))
        {
            paclp->_fHasLightLab = fTrue;
            paclp->_fLightEnabled = light.fEnabled;
            paclp->_fLightGenerateShadows = light.fGenerateShadows;
            paclp->_fLightAttachmentHideable = light.fAttachmentHideable;
            paclp->_lightIntensity = light.intensity;
            paclp->_lightEdgeGradient = light.edgeGradient;
            paclp->_lightDiameter = light.diameter;
            paclp->_lightRange = light.range > 0.0f ? light.range : 500.0f;
            CopyPb(light.szShape, paclp->_szLightShape, SIZEOF(paclp->_szLightShape));
        }

        OBJECTPROPERTIES prop;
        if (pmvie != pvNil &&
            pmvie->FGetObjectProperties(pmvie->Iscen(), pactr->Arid(), &prop) &&
            (prop.fFlushOverlap || !prop.fCastShadows))
        {
            paclp->_fHasObjectProperties = fTrue;
            paclp->_fObjectFlushOverlap = prop.fFlushOverlap;
            paclp->_fObjectCastShadows = prop.fCastShadows;
        }
    }

    paclp->_pactr = pactrTmp;
    paclp->_fRteOnly = fRteOnly;
    AssertPo(paclp, 0);

    return (paclp);
}

/** 3DMMv1.0: *************************************************************************

    Destroys an actor clipboard object

***************************************************************************/
ACLP::~ACLP(void)
{
    ReleasePpo(&_pactr);
}

/** 3DMMv1.0: *************************************************************************

    Pastes an actor clipboard object

***************************************************************************/
bool ACLP::FPaste(PMVIE pmvie)
{
    AssertThis(0);
    AssertPo(pmvie, 0);

    PACTR pactrNew;

    if (_fRteOnly)
    {
        return (pmvie->FPasteActrPath(_pactr));
    }

    //
    // 3DMMv1.0: Duplicate the actor
    //
    if (!_pactr->FDup(&pactrNew, fTrue))
    {
        return (fFalse);
    }
    AssertPo(pactrNew, 0);

    if (!pmvie->FPasteActr(pactrNew, fTrue))
    {
        ReleasePpo(&pactrNew);
        return (fFalse);
    }

    if (!pmvie->FNameActr(pactrNew->Arid(), &_stnName))
    {
        pmvie->Pscen()->RemActrCore(pactrNew->Arid());
        ReleasePpo(&pactrNew);
        return (fFalse);
    }

    // Rebind an attached copied light to the pasted actor's new ARID.  Use
    // the core restore path so actor + light remain one clipboard operation
    // rather than creating a second independent "Light Settings" undo item.
    if (_fHasLightLab)
    {
        LIGHTLAB light;
        ClearPb(&light, SIZEOF(light));
        light.iscen = pmvie->Iscen();
        light.arid = pactrNew->Arid();
        light.fEnabled = _fLightEnabled;
        light.fGenerateShadows = _fLightGenerateShadows;
        light.fAttachmentHideable = _fLightAttachmentHideable;
        light.intensity = _lightIntensity;
        light.edgeGradient = _lightEdgeGradient;
        light.diameter = _lightDiameter;
        light.range = _lightRange;
        CopyPb(_szLightShape, light.szShape, SIZEOF(light.szShape));
        if (!pmvie->FRestoreLightLabConfigCore(&light))
        {
            pmvie->Pscen()->RemActrCore(pactrNew->Arid());
            ReleasePpo(&pactrNew);
            return fFalse;
        }
    }

    if (_fHasObjectProperties)
    {
        OBJECTPROPERTIES prop;
        ClearPb(&prop, SIZEOF(prop));
        prop.iscen = pmvie->Iscen();
        prop.arid = pactrNew->Arid();
        prop.fFlushOverlap = _fObjectFlushOverlap;
        prop.fCastShadows = _fObjectCastShadows;
        if (!pmvie->FSetObjectProperties(&prop, fFalse))
        {
            pmvie->Pscen()->RemActrCore(pactrNew->Arid());
            ReleasePpo(&pactrNew);
            return fFalse;
        }
    }

    // Paste-in-place finishes immediately instead of entering the old mouse
    // placement mode, so create the Add Actor undo record here.
    PSUNA psuna = SUNA::PsunaNew();
    PACTR pactrUndo = pvNil;

    if (psuna == pvNil || !pactrNew->FDup(&pactrUndo, fTrue))
    {
        PushErc(ercSocNotUndoable);
        ReleasePpo(&pactrUndo);
        pmvie->ClearUndo();
    }
    else
    {
        pactrUndo->SetArid(pactrNew->Arid());
        psuna->SetType(utAdd);
        psuna->SetActr(pactrUndo);
        if (_fHasLightLab)
        {
            psuna->SetLightLab(_fLightEnabled, _fLightGenerateShadows, _fLightAttachmentHideable,
                               _lightIntensity, _lightEdgeGradient, _lightDiameter, _lightRange, _szLightShape);
        }
        if (_fHasObjectProperties)
            psuna->SetObjectProperties(_fObjectFlushOverlap, _fObjectCastShadows);

        if (!pmvie->FAddUndo(psuna))
        {
            PushErc(ercSocNotUndoable);
            pmvie->ClearUndo();
        }
    }

    ReleasePpo(&psuna);
    ReleasePpo(&pactrNew);
    return (fTrue);
}

#ifdef DEBUG

/** 3DMMv1.0: **************************************************
 * Mark memory used by the ACLP
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void ACLP::MarkMem(void)
{
    ACLP_PAR::MarkMem();
    MarkMemObj(_pactr);
}

/** 3DMMv1.0: *************************************************************************
 * Assert the validity of the ACLP
 *
 * Parameters:
 *  grf - bit array of options
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void ACLP::AssertValid(uint32_t grf)
{
    ACLP_PAR::AssertValid(fobjAllocated);
    _pactr->AssertValid(grf);
}

#endif

/** 3DMMv1.0: *************************************************************************

    Create an undo object

***************************************************************************/
bool ACTR::FCreateUndo(PACTR pactrDup, bool fSndUndo, PSTN pstn)
{
    AssertPo(pactrDup, 0);
    AssertNilOrPo(pstn, 0);

    PAUND paund;

    paund = AUND::PaundNew();

    if (paund == pvNil)
    {
        return fFalse;
    }

    paund->SetPactr(pactrDup);
    paund->SetArid(_arid);
    paund->CaptureLightLab(_pscen->Pmvie(), _pscen->Pmvie()->Iscen(), _arid);
    paund->SetSndUndo(fSndUndo);
    if (pvNil != pstn)
        paund->SetStn(pstn);

    if (!_pscen->Pmvie()->FAddUndo(paund))
    {
        _pscen->Pmvie()->ClearUndo();
        ReleasePpo(&paund);
        return (fFalse);
    }

    ReleasePpo(&paund);

    //
    // 3DMMv1.0: Detach from the scene
    //
    pactrDup->Reset();

    return (fTrue);
}

/** 3DMMv1.0: *************************************************************************

    Add (or replace) an action, and create an undo object

***************************************************************************/
bool ACTR::FSetAction(int32_t anid, int32_t celn, bool fFreeze, PACTR *ppactrDup)
{
    AssertThis(0);
    AssertNilOrVarMem(ppactrDup);

    PACTR pactrDup;

    if (!FDup(&pactrDup))
    {
        return (fFalse);
    }

    if (!FSetActionCore(anid, celn, fFreeze))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return fFalse;
    }

    if (!FCreateUndo(pactrDup))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return (fFalse);
    }

    if (pvNil == ppactrDup)
        ReleasePpo(&pactrDup);
    else
        *ppactrDup = pactrDup;

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Add the event to the event list: Add actor on the stage, and create undo
    object.

***************************************************************************/
bool ACTR::FAddOnStage(void)
{
    AssertThis(0);

    PACTR pactrDup;

    if (!FDup(&pactrDup))
    {
        return (fFalse);
    }

    if (!FAddOnStageCore())
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return fFalse;
    }

    if (!FCreateUndo(pactrDup))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return (fFalse);
    }

    ReleasePpo(&pactrDup);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Normalize an actor.

***************************************************************************/
bool ACTR::FNormalize(uint32_t grfnorm)
{
    AssertThis(0);

    PACTR pactrDup;

    if (!FDup(&pactrDup))
    {
        return (fFalse);
    }

    if (!FNormalizeCore(grfnorm))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return fFalse;
    }

    if (!FCreateUndo(pactrDup))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return (fFalse);
    }

    ReleasePpo(&pactrDup);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Set the Costume for a body part
    Add the event to the event list

***************************************************************************/
bool ACTR::FSetCostume(int32_t ibset, TAG *ptag, int32_t cmid, tribool fCmtl)
{
    AssertThis(0);
    Assert(fCmtl || ibset >= 0, "Invalid ibset argument");
    AssertVarMem(ptag);

    PACTR pactrDup;

    FDup(&pactrDup);

    if (!FSetCostumeCore(ibset, ptag, cmid, fCmtl))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return fFalse;
    }

    if (!FCreateUndo(pactrDup))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return (fFalse);
    }

    ReleasePpo(&pactrDup);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Delete the path and events from this frame and beyond
    **NOTE:  This does not send the actor offstage.	See FRemFromStageCore.

    On Input: fDeleteAll specifies full route vs subroute deletion
    Returns *pfAlive = false if all events and route for this actor
    have been deleted

***************************************************************************/
bool ACTR::FDelete(bool *pfAlive, bool fDeleteAll)
{
    AssertThis(0);
    PACTR pactrDup;
    int32_t iaevCurSav;
    AEV *paev;

    if (!FDup(&pactrDup))
    {
        if (pvNil != pfAlive)
            TrashVar(pfAlive);
        return fFalse;
    }

    // 3DMMv1.0: Unless we are deleting to the end of the scene,
    // 3DMMv1.0: we need to special case deletion that begins at
    // 3DMMv1.0: the same frame as the Add event - otherwise, the
    // 3DMMv1.0: code backs up one frame, putting the current frame
    // 3DMMv1.0: on the previous subroute.
    // 3DMMv1.0: Note: FDelete() does not require that the current
    // 3DMMv1.0: frame be	later than _nfrmFirst.
    if (_iaevAddCur >= 0 && !fDeleteAll)
    {
        paev = (AEV *)_pggaev->QvFixedGet(_iaevAddCur);
        if (_nfrmCur == paev->nfrm)
        {
            if (!_FDeleteEntireSubrte())
                goto LFail;
            if (pvNil != pfAlive)
                *pfAlive = FPure(_pggaev->IvMac() > 0);
            goto LDeleted;
        }
    }

    // 3DMMv1.0: Go to the previous frame to update state variables
    iaevCurSav = _iaevCur;
    if (!FGotoFrame(_nfrmCur - 1))
    {
        goto LFail;
    }

#ifndef BUG1870
    // 3DMMv1.0: The next two lines are obsolete & cause placement orientation bugs
    // 3DMMv1.0: Save the current orientation
    _SaveCurPathOrien();
#endif //! 3DMMv1.0: BUG1870

    DeleteFwdCore(fDeleteAll, pfAlive, iaevCurSav);

    // 3DMMv1.0: Return to original frame
    // 3DMMv1.0: _nfrmCur was decremented above
    if (!FGotoFrame(_nfrmCur + 1))
    {
        goto LFail;
    }

    /* 3DMMv1.0: Might have deleted some sound events */
    if (Pscen() != pvNil)
        Pscen()->UpdateSndFrame();

LDeleted:
    // 3DMMv1.0: The frame slider never required lifetime recomputation at this point.
    // 3DMMv1.0: Motion match sounds and prerendering both do, however.
    if (!_FComputeLifetime())
        PushErc(ercSocBadFrameSlider);

    if (!FCreateUndo(pactrDup))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return fFalse;
    }

    ReleasePpo(&pactrDup);
    return fTrue;

LFail:
    PushErc(ercSocGotoFrameFailure);
    ReleasePpo(&pactrDup);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Add the event to the event list: Remove actor from the stage, and an Undo.
    NOTE: This should be called <before> the call to place the actor offstage
***************************************************************************/
bool ACTR::FRemFromStage(void)
{
    AssertThis(0);

    PACTR pactr;

    if (!FDup(&pactr))
    {
        return (fFalse);
    }

    if (!FRemFromStageCore())
    {
        Restore(pactr);
        return fFalse;
    }

    if (!FCreateUndo(pactr))
    {
        Restore(pactr);
        ReleasePpo(&pactr);
        return (fFalse);
    }

    ReleasePpo(&pactr);
    return fTrue;
}

/** 3DMMv1.0: **************************************************
 *
 * Public constructor for actor undo objects.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PAUND AUND::PaundNew()
{
    PAUND paund;
    paund = NewObj AUND();
    AssertNilOrPo(paund, 0);
    return (paund);
}

/** 3DMMv1.0: **************************************************
 *
 * Destructor for actor undo objects
 *
 ****************************************************/
AUND::~AUND(void)
{
    AssertBaseThis(0);
    ReleasePpo(&_pactr);
}

void AUND::CaptureLightLab(PMVIE pmvie, int32_t iscen, int32_t arid)
{
    AssertNilOrPo(pmvie, 0);
    _fHadLightLab = fFalse;
    _iscenLightLab = iscen;
    if (pmvie == pvNil)
        return;

    LIGHTLAB light;
    if (!pmvie->FGetLightLabConfig(iscen, arid, &light))
        return;

    _fHadLightLab = fTrue;
    _fLightEnabled = light.fEnabled;
    _fLightGenerateShadows = light.fGenerateShadows;
    _fLightAttachmentHideable = light.fAttachmentHideable;
    _lightIntensity = light.intensity;
    _lightEdgeGradient = light.edgeGradient;
    _lightDiameter = light.diameter;
    _lightRange = light.range > 0.0f ? light.range : 500.0f;
    CopyPb(light.szShape, _szLightShape, SIZEOF(_szLightShape));
}

void AUND::RestoreLightLab(void)
{
    if (!_fHadLightLab || _pmvie == pvNil || _iscenLightLab == ivNil)
        return;
    LIGHTLAB light;
    ClearPb(&light, SIZEOF(light));
    light.iscen = _iscenLightLab;
    light.arid = _arid;
    light.fEnabled = _fLightEnabled;
    light.fGenerateShadows = _fLightGenerateShadows;
    light.fAttachmentHideable = _fLightAttachmentHideable;
    light.intensity = _lightIntensity;
    light.edgeGradient = _lightEdgeGradient;
    light.diameter = _lightDiameter;
    light.range = _lightRange;
    CopyPb(_szLightShape, light.szShape, SIZEOF(light.szShape));
    _pmvie->FRestoreLightLabConfigCore(&light);
}

void AUND::RemoveLightLab(void)
{
    if (_fHadLightLab && _pmvie != pvNil && _iscenLightLab != ivNil)
        _pmvie->FRemoveLightLabConfigCore(_iscenLightLab, _arid);
}

/** 3DMMv1.0: **************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
/***************************************************************************
    Plain-English history name for actor edits.
***************************************************************************/
void AUND::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    if (_stn.Cch() != 0)
        pstn->SetSz(PszLit("Rename Actor"));
    else if (_fSoonerLater)
        pstn->SetSz(PszLit("Move Actor Earlier/Later"));
    else if (_fSndUndo)
        pstn->SetSz(PszLit("Edit Actor Sound"));
    else
        pstn->SetSz(PszLit("Edit Actor"));
}

bool AUND::FDo(PDOCB pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);

    int32_t nfrmTmp;
    bool fRet;

    if (!_fSoonerLater)
    {
        return (FUndo(pdocb));
    }

    nfrmTmp = _nfrm;
    _nfrm = _nfrmLast;

    fRet = FUndo(pdocb);

    _nfrm = nfrmTmp;

    return (fRet);
}

/** 3DMMv1.0: **************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool AUND::FUndo(PDOCB pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);

    PACTR pactr;
    PMVU pmvu;

    if (!_pmvie->FSwitchScen(_iscen))
    {
        return (fFalse);
    }

    if (!_pmvie->Pscen()->FGotoFrm(_nfrm))
    {
        _pmvie->ClearUndo();
        return (fFalse);
    }

    _pmvie->Pmsq()->FlushMsq();

    pactr = _pmvie->Pscen()->PactrFromArid(_arid);
    AssertNilOrPo(pactr, 0);
    bool fActorWasPresent = pactr != pvNil;

    if (pactr != pvNil)
    {
        pactr->AddRef();
    }

    //
    // 3DMMv1.0: Have scene replace the old actor with this one
    //
    if (_pactr == pvNil)
    {
        _pmvie->Pscen()->RemActrCore(pactr->Arid());
    }
    else
    {
        if (_stn.Cch() != 0)
        {
            // 3DMMv1.0: Undo actor name change
            STN stn;
            if (_pmvie->FGetName(_arid, &stn))
            {
                // 3DMMv1.0: If FNameActr fails, the actor will not have
                // 3DMMv1.0: the correct name...not great, but the user's document
                // 3DMMv1.0: won't be corrupted or anything.  Someone will push a
                // 3DMMv1.0: ercOom, so I ignore the return value here.
                _pmvie->FNameActr(_pactr->Arid(), &_stn);
                _stn = stn;
            }
        }

        if (_arid != _pactr->Arid())
        {
            _pmvie->Pscen()->RemActrCore(_arid);
            _arid = _pactr->Arid();
        }

        if (!_pmvie->Pscen()->FAddActrCore(_pactr))
        {
            ReleasePpo(&pactr);
            return (fFalse);
        }

        pmvu = (PMVU)_pmvie->PddgGet(0);
        AssertNilOrPo(pmvu, 0);

        if ((pmvu != pvNil) && !pmvu->FTextMode())
        {
            _pmvie->Pscen()->SelectActr(_pactr);
        }

        ReleasePpo(&_pactr);
    }

    _pmvie->Pscen()->InvalFrmRange();

    _pactr = pactr;

    if (pactr != pvNil)
    {
        pactr->Reset();
    }

    bool fActorIsPresent = _pmvie->Pscen()->PactrFromArid(_arid) != pvNil;
    if (!fActorWasPresent && fActorIsPresent)
        RestoreLightLab();
    else if (fActorWasPresent && !fActorIsPresent)
        RemoveLightLab();
    else if (fActorIsPresent && _pmvie->FActorHasLight(_iscen, _arid))
    {
        // An actual user Undo/Redo may replace the BODY that carries a Light
        // Lab attachment. Update that attachment in place, but do not tear
        // down/recreate every runtime light as v206 did from _RestoreFromUndo.
        _pmvie->UpdateTestLightAttachment();
    }

    // Geometry/pose Undo changes the shadow caster set even for ordinary
    // actors, so invalidate the BRender world without rebuilding Light Lab.
    if (_pmvie->Pbwld() != pvNil)
        _pmvie->Pbwld()->MarkDirty();
    _pmvie->InvalViews();

    if (_fSndUndo)
    {
        _pmvie->Pmsq()->PlayMsq();
    }
    else
    {
        _pmvie->Pmsq()->FlushMsq();
    }

    return (fTrue);
}

/** 3DMMv1.0: **************************************************
 * Set the actor for this undo object.
 *
 * Parameters:
 * 	pactr - Actor to use
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void AUND::SetPactr(PACTR pactr)
{
    AssertThis(0);

    _pactr = pactr;
    pactr->AddRef();
}


/***************************************************************************
    Bound Object Group transform undo.
***************************************************************************/
PGUND GUND::PgundNew(void)
{
    PGUND pgund = NewObj GUND();
    AssertNilOrPo(pgund, 0);
    return pgund;
}

GUND::~GUND(void)
{
    AssertBaseThis(0);
    if (_pglentry != pvNil)
    {
        for (int32_t i = 0; i < _pglentry->IvMac(); i++)
        {
            GUNDENTRY entry;
            _pglentry->Get(i, &entry);
            ReleasePpo(&entry.paund);
        }
        ReleasePpo(&_pglentry);
    }
    FreePpv((void **)&_pogstate);
}

bool GUND::FCaptureGroup(PMVIE pmvie, int32_t idGroup)
{
    AssertNilOrPo(pmvie, 0);
    if (pmvie == pvNil || pmvie->Pscen() == pvNil || idGroup <= 0)
        return fFalse;

    const OBJECTGROUP *pgroup = pvNil;
    for (int32_t i = 0; i < pmvie->CObjectGroups(); i++)
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

    _pglentry = GL::PglNew(SIZEOF(GUNDENTRY), 0);
    if (_pglentry == pvNil)
        return fFalse;

    const int32_t cMember = pmvie->CObjectGroupMembers(idGroup);
    for (int32_t iMember = 0; iMember < cMember; iMember++)
    {
        const OBJECTGROUPMEMBER *pmember = pmvie->PObjectGroupMember(idGroup, iMember);
        if (pmember == pvNil)
            continue;
        PACTR pactr = pmvie->Pscen()->PactrFromArid(pmember->arid);
        if (pactr == pvNil)
            continue;

        PACTR pactrDup = pvNil;
        PAUND paund = AUND::PaundNew();
        if (paund == pvNil || !pactr->FDup(&pactrDup, fTrue))
        {
            ReleasePpo(&paund);
            return fFalse;
        }
        paund->SetPactr(pactrDup);
        paund->SetArid(pmember->arid);
        paund->SetPmvie(pmvie);
        paund->SetIscen(pmvie->Iscen());
        paund->SetNfrm(pmvie->Pscen()->Nfrm());
        paund->CaptureLightLab(pmvie, pmvie->Iscen(), pmember->arid);
        ReleasePpo(&pactrDup);

        GUNDENTRY entry;
        entry.paund = paund;
        if (!_pglentry->FAdd(&entry))
        {
            ReleasePpo(&paund);
            return fFalse;
        }
        // Ownership of paund is now held by the raw pointer in _pglentry.
    }

    if (_pglentry->IvMac() < 2)
        return fFalse;

    _idGroup = idGroup;
    return fTrue;
}

bool GUND::FCaptureGroupDelete(PMVIE pmvie, int32_t idGroup)
{
    AssertNilOrPo(pmvie, 0);
    if (pmvie == pvNil || _pglentry != pvNil || _pogstate != pvNil ||
        !FCaptureGroup(pmvie, idGroup))
        return fFalse;

    if (!FAllocPv((void **)&_pogstate, SIZEOF(*_pogstate), fmemClear, mprNormal) ||
        !pmvie->FGetObjectGroupState(_pogstate))
    {
        FreePpv((void **)&_pogstate);
        return fFalse;
    }

    _stnGroupUndoName.SetSz(PszLit("Delete Object Group"));
    _idGroup = idGroup;
    return fTrue;
}

bool GUND::FCaptureMembership(PMVIE pmvie, const achar *pszUndoName)
{
    AssertNilOrPo(pmvie, 0);
    if (pmvie == pvNil || _pglentry != pvNil || _pogstate != pvNil)
        return fFalse;

    if (!FAllocPv((void **)&_pogstate, SIZEOF(*_pogstate), fmemClear, mprNormal))
        return fFalse;
    if (!pmvie->FGetObjectGroupState(_pogstate))
    {
        FreePpv((void **)&_pogstate);
        return fFalse;
    }

    _stnGroupUndoName.SetSz(pszUndoName != pvNil ? pszUndoName : PszLit("Edit Object Group"));
    _idGroup = 0;
    return fTrue;
}

void GUND::GetUndoName(PSTN pstn)
{
    AssertPo(pstn, 0);
    if (_pogstate != pvNil && _stnGroupUndoName.Cch() > 0)
        *pstn = _stnGroupUndoName;
    else
        pstn->SetSz(PszLit("Edit Object Group"));
}

bool GUND::FDo(PDOCB pdocb)
{
    return FUndo(pdocb);
}

bool GUND::FUndo(PDOCB pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);

    if (_pmvie == pvNil)
        return fFalse;
    if (!_pmvie->FSwitchScen(_iscen))
        return fFalse;
    if (_pmvie->Pscen() == pvNil || !_pmvie->Pscen()->FGotoFrm(_nfrm))
    {
        _pmvie->ClearUndo();
        return fFalse;
    }

    _pmvie->Pmsq()->FlushMsq();

    const bool fHaveActorSnapshots = _pglentry != pvNil && _pglentry->IvMac() > 0;
    const bool fHaveMembershipSnapshot = _pogstate != pvNil;

    if (!fHaveActorSnapshots)
    {
        if (!fHaveMembershipSnapshot)
            return fFalse;
        if (!_pmvie->FSwapObjectGroupState(_pogstate))
        {
            _pmvie->ClearUndo();
            return fFalse;
        }
        _pmvie->UpdateTestLightAttachment();
        return fTrue;
    }
    if (_idGroup <= 0)
        return fFalse;

    // A combined Delete Object Group undo owns both the member ACTRs and the
    // logical membership table. On redo the live group must be dismantled
    // before its ACTRs disappear; on undo the ACTRs must be restored before
    // the saved membership table can rebuild a BRender parent around them.
    bool fGroupPresentBefore = fFalse;
    if (fHaveMembershipSnapshot)
    {
        for (int32_t iGroup = 0; iGroup < _pmvie->CObjectGroups(); ++iGroup)
        {
            const OBJECTGROUP *pgroup = _pmvie->PObjectGroup(iGroup);
            if (pgroup != pvNil && pgroup->id == _idGroup && pgroup->iscen == _iscen)
            {
                fGroupPresentBefore = fTrue;
                break;
            }
        }
        if (fGroupPresentBefore && !_pmvie->FSwapObjectGroupState(_pogstate))
        {
            _pmvie->ClearUndo();
            return fFalse;
        }
    }

    // Reuse the mature single-actor AUND swap logic for every member, but keep
    // the children private to this wrapper so the user's history contains one
    // Object Group operation rather than one entry per actor.
    for (int32_t i = 0; i < _pglentry->IvMac(); i++)
    {
        GUNDENTRY entry;
        _pglentry->Get(i, &entry);
        if (entry.paund == pvNil)
            return fFalse;
        entry.paund->SetPmvie(_pmvie);
        entry.paund->SetIscen(_iscen);
        entry.paund->SetNfrm(_nfrm);
        if (!entry.paund->FUndo(pdocb))
            return fFalse;
    }

    if (fHaveMembershipSnapshot && !fGroupPresentBefore)
    {
        if (!_pmvie->FSwapObjectGroupState(_pogstate))
        {
            _pmvie->ClearUndo();
            return fFalse;
        }
    }

    bool fGroupPresentAfter = fFalse;
    for (int32_t iGroup = 0; iGroup < _pmvie->CObjectGroups(); ++iGroup)
    {
        const OBJECTGROUP *pgroup = _pmvie->PObjectGroup(iGroup);
        if (pgroup != pvNil && pgroup->id == _idGroup && pgroup->iscen == _iscen)
        {
            fGroupPresentAfter = fTrue;
            break;
        }
    }

    if (fGroupPresentAfter)
    {
        // FSwapObjectGroupState already rebuilt runtime parents for a combined
        // membership snapshot. Actor-only GUND operations still need the old
        // explicit rebuild after their AUND children swap live ACTR state.
        if (!fHaveMembershipSnapshot && !_pmvie->FBeginObjectGroupTransform(_idGroup))
            return fFalse;
        _pmvie->FSelectObjectGroup(_idGroup);
    }
    else if (_pmvie->Pscen() != pvNil)
        _pmvie->Pscen()->SelectActr(pvNil);

    _pmvie->UpdateTestLightAttachment();
    if (fHaveMembershipSnapshot && _pmvie->Pmcc() != pvNil)
        _pmvie->Pmcc()->UpdateRollCall();
    if (_pmvie->Pbwld() != pvNil)
        _pmvie->Pbwld()->MarkDirty();
    _pmvie->SetDirty();
    _pmvie->InvalViewsAndScb();
    return fTrue;
}

#ifdef DEBUG
/** 3DMMv1.0: **************************************************
 * Mark memory used by the AUND
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void AUND::MarkMem(void)
{
    AssertThis(0);
    AUND_PAR::MarkMem();
    MarkMemObj(_pactr);
}

/** 3DMMv1.0: *************************************************************************
    Assert the validity of the AUND.
***************************************************************************/
void AUND::AssertValid(uint32_t grf)
{
    AssertNilOrPo(_pactr, 0);
}
#endif
