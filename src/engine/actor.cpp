/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Actor Engine

    Primary Author : *****
    Review Status: Reviewed

    Actors contain:
    1. A path, pglrpt, which is a gl of route points, the nodes of the path.
    2. An event list, pggaev, which is a list of variable size events
        occurring on or between nodes on the path.
    3. A motion match list of sounds applicable during the current action.
    4. State variables

    See Movie.doc for a detailed description of the functionality.

    Routes consists of one or more subroutes which can be separately
    translated, deleted, etc.

    This is a *where* based model of editing.  Events occur at specified
    locations along the path.  The path is linear between nodes.  Each
    node represents the point at which the actor was located in the original
    path recording session.  It is not redefined on resizing or motion fill.
    Instead, the event list is edited.  The mouse input is however, smoothed
    in movie before actor is called.

    The one exception to events being executed based on location is the
    aetAdd event, which is executed at its specified time.
    Due to motion fill inheriting the static or non-static path property,
    note that it is (no longer) possible for motion fill to cause an
    actor to not reach the end of a subroute.

    Misc comments:

    Actors begin each subroute with the	default costume, reimposed
    automatically by the code at every new subroute.	Any
    initialization events are then CHANGE events, post applied via
    events in the event stream.	When a new subroute is created, the
    new initialization events are copied from the previous subroute,
    if one exists, and from the following subroute if no previous subroute
    exists.

    Subroutes can be pushed in time by record, rerecord, & sooner/later.
    An actor is represented at at most one location at any given time.

    All events located <= _rtelCur on the path are executed at the end
    of any given frame. (See FGotoFrame()).

    Each subroute has an aetAdd event as its first event in the event list.

    Rotation transformations are absolute, not cumulative. The transformation
    effective at the current frame is stored in _xfrm.  There are now two
    fundamentally different types of rotation.  The aetRotF represent forward
    rotation and have path orientation applied concurrently.  The aetRotH
    apply to static segments only and do not have path orientation post
    applied.  _xfrm represents both, and _fUseBmat34 flags (for each frame)
    which transformation is to be applied when the body is positioned.
    The aetRotH events take precedence over aetRotF events if both exist in the
    same frame. This is necessary to allow the aetRotF events to control future
    path rotation while still allowing single frame orientation.  The aetRotF
    events nuke the aetRotH events, however, in order to allow the user to
    visibly see the forward rotation currently being applied.

    Costume changes are per body part set.

    Freeze inhibits cel advancement.
    Step size events control floating as well as cel advancement in place.
    Each subpath is terminated with a step=0 and a freeze event.

    Each actor is visible (not hidden, though possibly out of view) between
    aetAdd and aetRem events.

    The stretch events, aetPull, do not alter step size.  Uniform stretching,
    aetSize events, do.

    Move events are used to translate the path forward of the event by the
    delta-xyz value in the event.  These are therefore cumulative, so that
    (unlike rotations), a move at frame 50 followed by a move at frame 20
    will result in frame 50 -to- end-of-subroute being translated by the sum
    of the translations at frames 20 and 50.  Tweak events do not translate
    the path, but instead change the current display location only.

    This code carefully maintains wysiwyg.  There are two parts to this.
    One part is the maintenance of exact location, so that if a recorded
    actor follows a specific path (eg, through rather than into doorways)
    that path is retained exactly.  A second part is that by <calculating>
    exact locations (unaltered by numerical roundoff), a carefully edited
    and synchronized actor will not end up one frame off in time on replay.
    Both were considered high priority be design.

    Actors currently proceed to the end of their path, taking a potentially
    partial step at the end.  As a result, to maintain wysiwyg, actors
    display at the location of aetActn and aetStep events.
    NOTE: This UI decision probably added more complexity to the
    code than it gained in UI functionality.  Had actors always
    displayed only at full step increments, these scenarios would not
    include wysiwyg issues.

    Frame numbers are stored with the events, but are not guaranteed to be
    correct for frame numbers larger than current frame _nfrmCur.
    FComputeLifetime() guarantees these for all frames, but FComputeLifetime()
    can fail -> the code should not rely on future nfrm values.

    The route is translated by _dxyzFullRte in class ACTR.
    Each subroute is additionally translated by _dxyzSubRte.
    _dxyzRte combines the overall actor translation for efficiency only.

***************************************************************************/
#include "frame.h"
#include "soc.h"
#include <cmath>

ASSERTNAME

// body.cpp keeps the alternate magenta grouped-object hilite material private to
// the BODY implementation; ACTR only tells the next Hilite() which material
// this BODY should use.
extern void Set4DMMGroupedHiliteForNextBody(bool fGrouped);

RTCLASS(ACTR)

// Keep actor rotation matrices as pure rotations.  BRender's legacy
// fixed-point normalizer did not remove the visible scale drift in the
// Windows x86 build, so do the same Gram-Schmidt repair in double precision
// and quantize back to BRender scalars only once at the end.
static void _NormalizeActorRotation(BMAT34 *pbmat34)
{
    AssertVarMem(pbmat34);

    double z0 = BrScalarToFloat(pbmat34->m[2][0]);
    double z1 = BrScalarToFloat(pbmat34->m[2][1]);
    double z2 = BrScalarToFloat(pbmat34->m[2][2]);
    double y0 = BrScalarToFloat(pbmat34->m[1][0]);
    double y1 = BrScalarToFloat(pbmat34->m[1][1]);
    double y2 = BrScalarToFloat(pbmat34->m[1][2]);

    double rz = std::sqrt(z0 * z0 + z1 * z1 + z2 * z2);
    if (rz <= 0.0000001)
    {
        BrMatrix34Identity(pbmat34);
        return;
    }
    z0 /= rz;
    z1 /= rz;
    z2 /= rz;

    // X = old Y cross normalized Z.  This preserves the actor's handedness
    // while removing scale and shear from the accumulated basis.
    double x0 = y1 * z2 - y2 * z1;
    double x1 = y2 * z0 - y0 * z2;
    double x2 = y0 * z1 - y1 * z0;
    double rx = std::sqrt(x0 * x0 + x1 * x1 + x2 * x2);
    if (rx <= 0.0000001)
    {
        BrMatrix34Identity(pbmat34);
        return;
    }
    x0 /= rx;
    x1 /= rx;
    x2 /= rx;

    // Y = Z cross X.
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
}

/** 3DMMv1.0: *************************************************************************

    Constructor for ACTR - private.

***************************************************************************/
ACTR::ACTR(void)
{
    _arid = aridNil;
    _tagTmpl.sid = ksidInvalid;
    _tagSnd.sid = ksidInvalid;
    _nfrmCur = _nfrmFirst = knfrmInvalid;
    _nfrmLast = klwMin;
    _fLifeDirty = fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Destructor for an ACTR

***************************************************************************/
ACTR::~ACTR(void)
{
    AssertBaseThis(0);

    _CloseTags();
    ReleasePpo(&_pbody);
    ReleasePpo(&_pggaev);
    ReleasePpo(&_pglrpt);
    ReleasePpo(&_pglsmm);
    ReleasePpo(&_ptmpl);
}

/** 3DMMv1.0: *************************************************************************

    Initialize the actor
    The actor will not yet be grounded to any initial scene frame.

***************************************************************************/
bool ACTR::_FInit(TAG *ptagTmpl)
{
    AssertBaseThis(0);
    AssertVarMem(ptagTmpl);

    _ptmpl = (PTMPL)vptagm->PbacoFetch(ptagTmpl, TMPL::FReadTmpl);
    if (pvNil == _ptmpl)
        return fFalse;

    AssertPo(_ptmpl, 0);
    _tagTmpl = *ptagTmpl;
    TAGM::DupTag(&_tagTmpl);

    if (!_FCreateGroups())
        return fFalse;

    _SetStateRewound();
    if (!_ptmpl->FGetGrfactn(0, &_grfactn))
        return fFalse;

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Initialize the transformation matrix & factors
    (Sets the absolute scale = 1, and use the rest orientation)

***************************************************************************/
void ACTR::_InitXfrm(void)
{
    AssertThis(0);

    _InitXfrmRot(&_xfrm.bmat34Fwd);
    _InitXfrmRot(&_xfrm.bmat34Cur);
    _xfrm.aevpull.rScaleX = rOne;
    _xfrm.aevpull.rScaleY = rOne;
    _xfrm.aevpull.rScaleZ = rOne;
    _xfrm.rScaleStep = rOne;
    _xfrm.xaPath = aZero;
    _xfrm.yaPath = aZero;
    _xfrm.zaPath = aZero;
}

/** 3DMMv1.0: *************************************************************************

    Initialize the rotation part of the transformation
    The rest orientation is now applied post user-rotations so that
    user rotations can be with respect to the actor's coordinate system

***************************************************************************/
void ACTR::_InitXfrmRot(BMAT34 *pbmat34)
{
    AssertThis(0);
    AssertVarMem(pbmat34);
    BrMatrix34Identity(pbmat34);
}

/** 3DMMv1.0: *************************************************************************

    Allocate a new actor
    The actor is not attached to a scene until SetPscen is called.
    The actor will not have an identifiable ID until SetArid is called.
    Note that an actor which is not yet added to a scene is neither
    grounded in time (_nfrmFirst) nor space (_dxyzFullRte).

***************************************************************************/
PACTR ACTR::PactrNew(TAG *ptagTmpl)
{
    AssertVarMem(ptagTmpl);

    PACTR pactr;

    if ((pactr = NewObj ACTR()) == pvNil)
        return pvNil;

    if (!pactr->_FInit(ptagTmpl))
    {
        ReleasePpo(&pactr);
        return pvNil;
    }

    return pactr;
}

/** 3DMMv1.0: *************************************************************************

    Create the groups _pggaev, _pglrpt and _pglsmm

***************************************************************************/
bool ACTR::_FCreateGroups(void)
{
    AssertBaseThis(0);

    if (pvNil == (_pggaev = GG::PggNew(SIZEOF(AEV), kcaevInit, kcbVarAdd)))
        return fFalse;

    if (pvNil == (_pglrpt = GL::PglNew(SIZEOF(RPT), kcrptGrow)))
        return fFalse;
    _pglrpt->SetMinGrow(kcrptGrow);

    if (pvNil == (_pglsmm = GL::PglNew(SIZEOF(SMM), kcsmmGrow)))
        return fFalse;
    _pglsmm->SetMinGrow(kcsmmGrow);

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Set the owning scene and the brender world for the body.

***************************************************************************/
void ACTR::SetPscen(SCEN *pscen)
{
    AssertBaseThis(0);

    AssertVarMem(pscen);
    Assert(_pscen == pvNil || _pscen == pscen, "SetPscen logic error");

    if (_pscen != pvNil)
    {
        Assert(_pbody != pvNil, "Bad body pointer");
        return;
    }

    Assert(_pbody == pvNil, "Bad body pointer");
    _pscen = pscen;

    // 3DMMv1.0: Create the body parts
    if (pvNil == (_pbody = _ptmpl->PbodyCreate()))
        return;

    _pbody->SetBwld(pscen->Pmvie()->Pbwld());
}

/** 3DMMv1.0: *************************************************************************

    Hide the Actor and initialize the Actor State Variables

***************************************************************************/
void ACTR::_InitState(void)
{
    AssertBaseThis(0);

    _Hide();
    _fLifeDirty = fTrue;
    if (_ptmpl != pvNil && _pbody != pvNil)
        _ptmpl->FSetDefaultCost(_pbody);
    _SetStateRewound();
}

/** 3DMMv1.0: *************************************************************************

    Set the actor state variables to the rewound position

***************************************************************************/
void ACTR::_SetStateRewound(void)
{
    AssertBaseThis(0);

    _fFrozen = fFalse;
    _fModeRecord = fFalse;
    _anidCur = _celnCur = 0;
    _rtelCur.irpt = 0;
    _rtelCur.dwrOffset = rZero;
    _rtelCur.dnfrm = -1;
    if (_pglrpt->IvMac() > 0)
    {
        RPT rpt;
        _pglrpt->Get(0, &rpt);
        _xyzCur = rpt.xyz;
    }

    _iaevCur = 0;
    _iaevActnCur = _iaevAddCur = ivNil;
    _dwrStep = rZero;
    _iaevFrmMin = 0;
    _InitXfrm();
}

/** 3DMMv1.0: *************************************************************************

    Prepare the actor for display in frame nfrm.

    nfrm can be any frame number in the movie - it is actor independent.
    NOTE: The state of this actor is expected to be current for frame _nfrmCur
    at the time this routine is called.
    FGotoFrame exits with _nfrmCur == nfrm.
    (A scene level interrogation of all actors should find
    that each actor's _nfrmCur is the same).

    Note: The scope of fPosition dirty spans multiple calls to _FDoFrm

***************************************************************************/
bool ACTR::FGotoFrame(int32_t nfrm, bool *pfSoundInFrame)
{
    AssertThis(0);
    AssertIn(nfrm, klwMin, klwMax);

    bool fPositionBody;
    bool fSuccess = fTrue;
    bool fPositionDirty = fFalse;
    bool fQuickMethodValid;
    int32_t iaevT = -1;
    int32_t iaev;
    AEV *paev;

    if (nfrm == _nfrmCur)
        return fTrue;

    // 3DMMv1.0: Initialization
    if (nfrm < _nfrmCur || _nfrmCur == knfrmInvalid)
    {
        bool fMidPath = FPure(_pggaev->IvMac() > 0 && _iaevCur > 0 && nfrm > _nfrmFirst);

        // 3DMMv1.0: Note: If nfrm < _nfrmFirst, we may be adding
        // 3DMMv1.0: a new earliest add event.
        if (nfrm < _nfrmFirst && _fOnStage)
            _Hide();

        if (nfrm > _nfrmFirst && _nfrmCur != knfrmInvalid)
        {
            Assert(0 < _iaevCur, "Invalid state variables in FGotoFrame()");
            // 3DMMv1.0: Optimize if there are no events in the current frame
            paev = (AEV *)_pggaev->QvFixedGet(_iaevCur - 1);
            if (paev->nfrm < nfrm)
            {
                if (!_FQuickBackupToFrm(nfrm, &fQuickMethodValid))
                    return fFalse;
                // 3DMMv1.0: Vertical segments can prevent _FQuickBackupToFrm()
                // 3DMMv1.0: from being valid.  If so, use default method.
                if (fQuickMethodValid)
                    return fTrue;
            }
        }

        // 3DMMv1.0: Will be walking forward from the nearest earlier add event
        // 3DMMv1.0: Search backward to find it.
        if (fMidPath)
        {
            for (iaev = _iaevCur - 1; iaev >= 0; iaev--)
            {
                paev = (AEV *)_pggaev->QvFixedGet(iaev);
                if (paev->aet == aetAdd && paev->nfrm <= nfrm)
                {
                    iaevT = iaev;
                    break;
                }
            }
        }
        _SetStateRewound();
        if (fMidPath)
        {
            AssertIn(iaevT, 0, _pggaev->IvMac());
            _iaevFrmMin = _iaevCur = _iaevAddCur = iaevT;
            paev = (AEV *)_pggaev->QvFixedGet(_iaevCur);
            _rtelCur = paev->rtel;
            _nfrmCur = paev->nfrm;
            Assert(paev->aet == aetAdd, "Illegal _iaevAddCur state var");
            _rtelCur.dnfrm--;
            _GetXyzFromRtel(&_rtelCur, &_xyzCur);
        }
        else
        {
            _nfrmCur = LwMin(nfrm, _nfrmFirst);
        }
        _ptmpl->FSetDefaultCost(_pbody);
    }
    else
    {
        _nfrmCur++;
        _iaevFrmMin = _iaevCur; // 3DMMv1.0: Save 1st event of this frame
    }

    if (nfrm < _nfrmFirst)
        return fTrue;

    // 3DMMv1.0: Trivial case: no events for this actor
    // 3DMMv1.0: _nfrmCur always reflects the movie's current frame
    if (_pggaev->IvMac() == 0)
    {
        _nfrmCur = nfrm;
        return fTrue;
    }

    while (_nfrmCur <= nfrm)
    {
        fPositionBody = (_nfrmCur == nfrm);
        if (!_FDoFrm(fPositionBody, &fPositionDirty, pfSoundInFrame))
            fSuccess = fFalse;

        if (nfrm == _nfrmCur)
            break;

        _nfrmCur++;
        _iaevFrmMin = _iaevCur;
        AssertIn(_iaevActnCur, -1, _pggaev->IvMac());
    }

    return fSuccess;
}

/***************************************************************************

    Insert cfrm held copies of nfrm immediately after it.  This is the middle-
    insertion equivalent of the original edge extension: later event times move
    forward, while two tiny state boundaries hold the actor's route position and
    animation cel through the inserted frames.  No actor or GG is duplicated.

***************************************************************************/
bool ACTR::FInsertHeldFramesAfter(int32_t nfrm, int32_t cfrm)
{
    AssertThis(0);
    AssertIn(nfrm, klwMin, klwMax);
    AssertIn(cfrm, 0, klwMax);

    if (cfrm <= 0)
        return fTrue;
    if (nfrm > klwMax - cfrm)
        return fFalse;

    if (_nfrmCur != nfrm && !FGotoFrame(nfrm))
        return fFalse;

    bool fOnStage = _fOnStage;
    bool fFrozen = _fFrozen;
    BRS dwrStep = _dwrStep;
    RTEL rtelHold = _rtelCur;
    int32_t iaevCurOld = _iaevCur;
    int32_t iaevSubLim = _pggaev->IvMac();
    AEV aev;

    // Find the end of the current subroute before changing any indices.  RTEL
    // values restart at each Add event, so a static-time adjustment must never
    // leak into a later subroute which happens to use the same route indices.
    for (int32_t iaev = iaevCurOld; iaev < _pggaev->IvMac(); iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);
        if (aev.aet == aetAdd)
        {
            iaevSubLim = iaev;
            break;
        }
    }

    int32_t caevHold = 0;
    int32_t cbHold = 0;
    if (fOnStage && !fFrozen)
    {
        caevHold += 2;
        cbHold += 2 * kcbVarFreeze;
    }
    if (fOnStage && dwrStep != rZero)
    {
        caevHold += 2;
        cbHold += 2 * kcbVarStep;
    }
    if (caevHold > 0 && !_pggaev->FEnsureSpace(caevHold, cbHold, fgrpNil))
        return fFalse;

    // Move every later absolute event with its old frame.  Only static-time
    // coordinates in this subroute need the matching dnfrm adjustment.
    for (int32_t iaev = 0; iaev < _pggaev->IvMac(); iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);
        bool fChanged = fFalse;
        if (aev.nfrm > nfrm)
        {
            aev.nfrm += cfrm;
            fChanged = fTrue;
        }
        if (iaev >= iaevCurOld && iaev < iaevSubLim &&
            aev.rtel.irpt == rtelHold.irpt &&
            aev.rtel.dwrOffset == rtelHold.dwrOffset &&
            aev.rtel.dnfrm > rtelHold.dnfrm)
        {
            aev.rtel.dnfrm += cfrm;
            fChanged = fTrue;
        }
        if (fChanged)
            _pggaev->PutFixed(iaev, &aev);
    }

    if (_nfrmFirst > nfrm)
        _nfrmFirst += cfrm;

    if (fOnStage && caevHold > 0)
    {
        int32_t iaevIns = iaevCurOld;
        AEV aevNew;
        aevNew.nfrm = nfrm;
        aevNew.rtel = rtelHold;

        // These events are deliberately inserted after every original event on
        // nfrm.  They capture the already-rendered frame state and hold it from
        // the following frame onward.
        if (!fFrozen)
        {
            int32_t fFreeze = (int32_t)fTrue;
            aevNew.aet = aetFreeze;
            if (!_FInsertAev(iaevIns++, kcbVarFreeze, &fFreeze, &aevNew, fFalse))
                return fFalse;
        }
        if (dwrStep != rZero)
        {
            BRS dwrZero = rZero;
            aevNew.aet = aetStep;
            if (!_FInsertAev(iaevIns++, kcbVarStep, &dwrZero, &aevNew, fFalse))
                return fFalse;
        }

        RTEL rtelRestore = rtelHold;
        rtelRestore.dnfrm += cfrm;
        int32_t iaevRestore = iaevIns;
        for (; iaevRestore < _pggaev->IvMac(); iaevRestore++)
        {
            AEV aevT;
            _pggaev->GetFixed(iaevRestore, &aevT);
            if (aevT.aet == aetAdd || aevT.rtel > rtelRestore)
                break;
        }

        // _FDoFrm advances route/cel state before processing this frame's
        // events.  Restoring on the last inserted frame keeps that frame held,
        // then resumes motion on the following (shifted original) frame.
        aevNew.nfrm = nfrm + cfrm;
        aevNew.rtel = rtelRestore;
        if (!fFrozen)
        {
            int32_t fFreeze = (int32_t)fFalse;
            aevNew.aet = aetFreeze;
            if (!_FInsertAev(iaevRestore++, kcbVarFreeze, &fFreeze, &aevNew, fFalse))
                return fFalse;
        }
        if (dwrStep != rZero)
        {
            aevNew.aet = aetStep;
            if (!_FInsertAev(iaevRestore, kcbVarStep, &dwrStep, &aevNew, fFalse))
                return fFalse;
        }
    }

    // Every cached actor cursor referred to the pre-insertion event stream.
    // Rewind once and rebuild through the normal playback evaluator instead of
    // trying to patch those indices by hand.
    _fLifeDirty = fTrue;
    _pscen->InvalFrmRange();
    _InitState();
    _nfrmCur = knfrmInvalid;
    return FGotoFrame(nfrm);
}

/** 3DMMv1.0: *************************************************************************

    Backup To a smaller frame.  	(Optimization)
    Return *pfQuickMethodValid fTrue on success.
    Return *pfQuickMethodValid fFalse to if this method invalid here.

***************************************************************************/
bool ACTR::_FQuickBackupToFrm(int32_t nfrm, bool *pfQuickMethodValid)
{
    AssertThis(0);
    AssertVarMem(pfQuickMethodValid);
    Assert(nfrm < _nfrmCur, "Illegal call to _FBackupToFrm");

    int32_t ifrm;
    RTEL rtelT;
    XYZ xyzOld = _xyzCur;
    XYZ xyzT;
    int32_t dnfrm = _nfrmCur - nfrm;

#ifdef DEBUG
    AEV *paev;
    paev = (AEV *)_pggaev->QvFixedGet(_iaevCur - 1);
    Assert(paev->nfrm < nfrm, "Invalid Call to _FQuickBackupToFrm()");
#endif // 3DMMv1.0: DEBUG

    // 3DMMv1.0: Don't try to back up during frames beyond the actor's lifetime
    if (nfrm >= _nfrmLast)
    {
        // 3DMMv1.0: GotoFrame() must always exit with _nfrmCur being valid
        // 3DMMv1.0: Otherwise, edits will be located at incorrect frames
        _nfrmCur = nfrm;
#ifdef BUG1906
        _rtelCur.dnfrm -= dnfrm;
#else  //! 3DMMv1.0: BUG1906
        _rtelCur.dnfrm--;
#endif //! 3DMMv1.0: BUG1906
        return fTrue;
    }

    // 3DMMv1.0: There are no events between here and the destination frame
    // 3DMMv1.0: _iaevFrmMin need not change

    // 3DMMv1.0: Walk to the destination location
    // 3DMMv1.0: Set the cel of the action
    for (ifrm = _nfrmCur - 1; ifrm >= nfrm; ifrm--)
    {
        if (!_FGetRtelBack(&_rtelCur, fTrue))
            goto LFail;
    }

    _GetXyzFromRtel(&_rtelCur, &_xyzCur);
    _nfrmCur = nfrm;

    if (dnfrm > 1 || xyzOld.dxr != _xyzCur.dxr || xyzOld.dzr != _xyzCur.dzr)
    {
        // 3DMMv1.0: Check for transitions to vertical motion
        if (!_FGetRtelBack(&rtelT, fFalse))
            goto LFail;

        _GetXyzFromRtel(&rtelT, &xyzT);

        if (_xyzCur.dxr == xyzT.dxr && _xyzCur.dzr == xyzT.dzr)
        {
            // 3DMMv1.0: Vertical motion next	(backing up)
            // 3DMMv1.0: -> Require _fUseBmat34Cur == fTrue, but bmat34Cur is not
            // 3DMMv1.0: yet computed.
            // 3DMMv1.0: -> Quick backup insufficient.
            *pfQuickMethodValid = fFalse;
            return fTrue;
        }
    }

    // 3DMMv1.0: Send motion match sounds to Msq to play
    if (!(_pscen->GrfScen() & fscenSounds) && (nfrm <= _nfrmLast) && _fOnStage)
        _FEnqueueSmmInMsq(); // 3DMMv1.0: Ignore failure

    // 3DMMv1.0: Position the actor
    _PositionBody(&_xyzCur);
    if (_fOnStage)
    {
        if (!_ptmpl->FSetActnCel(_pbody, _anidCur, _celnCur, pvNil))
            goto LFail;
    }
    *pfQuickMethodValid = fTrue;
    return fTrue;

LFail:
    *pfQuickMethodValid = fFalse;
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Compute xyz for one step backwards from the current location, as per
    state variables

***************************************************************************/
bool ACTR::_FGetRtelBack(RTEL *prtel, bool fUpdateStateVar)
{
    AssertThis(0);
    AssertVarMem(prtel);

    int32_t celnSav = _celnCur;
    int32_t nfrmSav = _nfrmCur;
    RTEL rtelSav = _rtelCur;
    AEV *paev;
    BRS dwrStep;
    RTEL rtelAdd;
    BRS dwrT;

    paev = (AEV *)_pggaev->QvFixedGet(_iaevAddCur);
    rtelAdd = paev->rtel;

    if (!_fFrozen)
        _celnCur--;
    if (rZero == _dwrStep || (rtelAdd.irpt == _rtelCur.irpt && rtelAdd.dwrOffset == _rtelCur.dwrOffset))
    {
        _nfrmCur--;
        _rtelCur.dnfrm--;
        goto LEnd;
    }

    // 3DMMv1.0: Set the location to display this actor
    if (!_FGetDwrPlay(&dwrStep))
        goto LFail;

    dwrT = BrsSub(_rtelCur.dwrOffset, dwrStep);

    if (dwrT >= rZero)
        _rtelCur.dwrOffset = dwrT;
    else
    {
        RPT rpt;
        while (dwrT < rZero)
        {
            if (_rtelCur.irpt <= 0)
            {
                Bug("Corrupted event list");
                _rtelCur.irpt = 0;
                dwrT = rZero;
                break;
            }
            _rtelCur.irpt--;
            _pglrpt->Get(_rtelCur.irpt, &rpt);
            dwrT = BrsAdd(rpt.dwr, dwrT);
        }
        _rtelCur.dwrOffset = dwrT;
    }
    _nfrmCur--;

LEnd:
    *prtel = _rtelCur;
    if (!fUpdateStateVar)
    {
        _celnCur = celnSav;
        _nfrmCur = nfrmSav;
        _rtelCur = rtelSav;
    }
    return fTrue;

LFail:
    _celnCur = celnSav;
    _nfrmCur = nfrmSav;
    _rtelCur = rtelSav;
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Update the internal actor state variables to frame _nfrmCur.
    Ready the actor to display in frame _nfrmCur if fPositionBody is fTrue.
    Sets *pfPositionDirty if a change is encountered.
    Clears *pfPositionDirty after updating Brender.

    Assumes previous frame correctly rendered.
    Note: PositionBody is delayed until after Events are processed - Tweak,
    Actn, Step and Transform events all can alter the location or orientation.
    Also, new Action Events precede Cel Positioning

    Note: The scope of *pfPositionDirty spans multiple _FDoFrm calls

***************************************************************************/
bool ACTR::_FDoFrm(bool fPositionBody, bool *pfPositionDirty, bool *pfSoundInFrame)
{
    AssertThis(0);
    AssertVarMem(pfPositionDirty);

    AEV aev;
    BRS dwr;
    int32_t iaev;
    int32_t iaevAdd;
    bool fEndRoute;
    XYZ xyzOld = _xyzCur;
    bool fFreezeThisCel = _fFrozen;
    bool fEndSubEvents = fFalse;
    bool fSuccess = fTrue;
    bool fAdvanceCel;

    // 3DMMv1.0: Obtain distance to move.	This may be shortened on encountering
    // 3DMMv1.0: a aetActn event later in this same frame.
    fSuccess = _FGetDwrPlay(&dwr);
    _AdvanceRtel(dwr, &_rtelCur, _iaevCur, _nfrmCur, &fEndRoute);
    _GetXyzFromRtel(&_rtelCur, &_xyzCur);

    // 3DMMv1.0: Use the pre-path rotation matrix if the actor moves
    // 3DMMv1.0: so that the path orientation can later be post applied.
    _fUseBmat34Cur = (dwr == rZero || (xyzOld.dxr == _xyzCur.dxr && xyzOld.dzr == _xyzCur.dzr));

    // 3DMMv1.0: Locate the next Add event before entering the next loop
    // 3DMMv1.0: Add events are executed when their absolute frame number == _nfrmCur
    if (!_fModeRecord)
    {
        iaevAdd = _iaevCur - 1;
        while (_FFindNextAevAet(aetAdd, iaevAdd + 1, &iaevAdd))
        {
            if (!_FIsAddNow(iaevAdd))
                break;

            // 3DMMv1.0: Add is now
            _iaevCur = iaevAdd;
            if (!_FDoAevCur())
                return fFalse;

            *pfPositionDirty = fTrue;
        }

        // 3DMMv1.0: Process any events through a dwr step size, unless an aetActn event
        // 3DMMv1.0: shortens that distance.
        // 3DMMv1.0: An aetActn Event will change _rtelCur at the time it is executed
        for (iaev = _iaevCur; iaev < _pggaev->IvMac(); iaev++)
        {
            _pggaev->GetFixed(iaev, &aev);
            if (aev.aet == aetAdd)
            {
                if (!_FIsAddNow(iaev))
                    break;

                // 3DMMv1.0: Add is now
                if (!_FDoAevCur())
                    return fFalse;

                *pfPositionDirty = fTrue;
                continue;
            }

            if (aev.rtel > _rtelCur)
                break;

            if (aetRotF == aev.aet || aetSize == aev.aet || aetPull == aev.aet || aetRotH == aev.aet ||
                aetMove == aev.aet)
            {
                // 3DMMv1.0: The xyz position is not necessarily changing.
                // 3DMMv1.0: Specifically enforce Brender updating
                *pfPositionDirty = fTrue;
            }
            else if ((aetActn == aev.aet) || (aetAdd == aev.aet))
            {
                // 3DMMv1.0: Do not increment _celnCur before displaying actor on new entrance/action
                fFreezeThisCel = fTrue;
                *pfPositionDirty = fTrue;
            }

            if (pfSoundInFrame != pvNil && aev.aet == aetSnd && fPositionBody)
                *pfSoundInFrame = fTrue;

            // 3DMMv1.0: Non-motion match sounds cannot depend on playing here as
            // 3DMMv1.0: there might not be a sound event at the ending frame.
            // 3DMMv1.0: Therefore _FDoAevCur() does not enqueue motion match sounds.
            // 3DMMv1.0: It enters mm-snds in the smm.  _FDoAevCur() enqueues non-mm snds.
            if (aev.aet != aetSnd || (fPositionBody && !(_pscen->GrfScen() & fscenSounds)))
            {
                // 3DMMv1.0: Play non sounds
                // 3DMMv1.0: Play sounds if this is the final frame (mm or non mm)
                if (!_FDoAevCur())
                    fSuccess = fFalse;
            }
            else
            {
                // 3DMMv1.0: Motion match sounds must be entered in the smm
                // 3DMMv1.0: whether this is the final frame or not
                AEVSND aevsnd;
                _pggaev->Get(iaev, &aevsnd);
                if (aevsnd.celn != smmNil)
                {
                    if (!_FDoAevCur())
                        fSuccess = fFalse;
                }
                else
                {
                    // 3DMMv1.0: Skip the current sound event
                    _iaevCur++;
                }
            }
        }
    }

    fEndSubEvents = _FIsDoneAevSub(_iaevCur, _rtelCur);
    fAdvanceCel = (!fFreezeThisCel && !(fEndRoute && fEndSubEvents));

    if (fAdvanceCel)
    {
        _celnCur++;
        if (_ccelCur > 1)
            *pfPositionDirty = fTrue;
    }

    // 3DMMv1.0: Force Brender to update if the xyz position has changed
    if (xyzOld != _xyzCur)
        *pfPositionDirty = fTrue;

    // 3DMMv1.0: Position even if hidden for clipping region detection
    // 3DMMv1.0: fPositionBody avoids extraneous positioning on intermed frames
    if (fPositionBody)
    {
        // 3DMMv1.0: Enqueue the motion match sounds from the smm.  Ie, enter in the msq
        if (!(_pscen->GrfScen() & fscenSounds) && (_nfrmCur <= _nfrmLast) && _fOnStage)
            _FEnqueueSmmInMsq();

        if (*pfPositionDirty)
        {
            _PositionBody(&_xyzCur);
            *pfPositionDirty = fFalse;
        }

        // 3DMMv1.0: Position the actor in the next cel
        // 3DMMv1.0: Do not do so if at the "end of route" & "end of events"
        if (_fOnStage)
        {
            if (!_ptmpl->FSetActnCel(_pbody, _anidCur, _celnCur, pvNil))
                fSuccess = fFalse;
        }
    }
    else if (*pfPositionDirty)
    {
        BMAT34 bmat34;
        _MatrixRotUpdate(&_xyzCur, &bmat34);
    }
    return fSuccess;
}

/** 3DMMv1.0: *************************************************************************

    FReplayFrame : Replays the sound for the current frame
        -> Re-enqueues sounds for the current frame
    grfscen - Things that are supposed to be played right now.

***************************************************************************/
bool ACTR::FReplayFrame(int32_t grfscen)
{
    AssertThis(0);

    AEV aev;
    int32_t iaev;

    // 3DMMv1.0: Check if there is anything to do
    if (!(grfscen & fscenSounds) || !_fOnStage)
        return fTrue;

    for (iaev = _iaevFrmMin; iaev < _iaevCur; iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);
        if (aev.aet != aetSnd)
            continue;

        if (!_FDoAevCore(iaev))
            return fFalse;
    }

    // 3DMMv1.0: Also send any motion match sounds to msq to play
    return _FEnqueueSmmInMsq();
}

/** 3DMMv1.0: *************************************************************************

    _FGetStatic : Returns true/false on success/failure
    Returns the bool value in *pfStatic

***************************************************************************/
bool ACTR::_FGetStatic(int32_t anid, bool *pfStatic)
{
    AssertThis(0);
    AssertIn(anid, 0, klwMax);
    AssertVarMem(pfStatic);

    uint32_t grfactn;

    if (!_ptmpl->FGetGrfactn(anid, &grfactn))
        return fFalse;

    if (ivNil == _iaevActnCur)
    {
        *pfStatic = fTrue;
        return fTrue;
    }

    *pfStatic = FPure(grfactn & factnStatic);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Is the actor in the last active frame of the subroute?

***************************************************************************/
bool ACTR::_FIsDoneAevSub(int32_t iaev, RTEL rtel)
{
    AssertBaseThis(0);
    AssertIn(iaev, 0, _pggaev->IvMac() + 1);

    AEV aev;
    if (iaev == _pggaev->IvMac())
        return fTrue;

    for (; iaev < _pggaev->IvMac(); iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);
        if (aev.aet == aetAdd)
            return fTrue;
        if (aev.rtel > rtel)
            return fFalse;
        // 3DMMv1.0: Event at current frame.  Keep looking.
    }

    // 3DMMv1.0: No further events exist at future frames for this subpath
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Is the actor at an AddOnStage event which can be applied now?

***************************************************************************/
bool ACTR::_FIsAddNow(int32_t iaev)
{
    AssertBaseThis(0);
    AssertIn(iaev, 0, _pggaev->IvMac());

    AEV *paev;

    if (_fLifeDirty)
    {
        // 3DMMv1.0: Update the nfrm values
        if (!_FComputeLifetime())
            return fFalse;
    }

    Assert(_pggaev->Cb(iaev) == SIZEOF(AEVADD), "Corrupt event list");
    paev = (AEV *)_pggaev->QvFixedGet(iaev);

    if (paev->nfrm <= _nfrmCur)
        return fTrue;

    return fFalse;
}

static bool F4DMMActorStudioOwnsTemplate(PSCEN pscen, const TAG *ptagTmpl)
{
    if (pscen == pvNil || ptagTmpl == pvNil || ptagTmpl->sid != ksidUseCrf ||
        ptagTmpl->ctg != kctgTmpl || ptagTmpl->cno == cnoNil)
        return fFalse;
    PMVIE pmvie = pscen->Pmvie();
    if (pmvie == pvNil)
        return fFalse;
    for (int32_t iobj = 0; iobj < pmvie->C4DMMCustomObjects(); ++iobj)
    {
        const CUSTOMOBJECT *pobj = pmvie->P4DMMCustomObject(iobj);
        if (pobj != pvNil && pobj->cnoOwnedTmpl == ptagTmpl->cno)
            return fTrue;
    }
    return fFalse;
}

static const BRS kdwr4DMMActorStudioDefault = BR_SCALAR(5.0);

/** 3DMMv1.0: *************************************************************************

    Return functional (sized) step size when playing
    Zero is a valid return value

***************************************************************************/
bool ACTR::_FGetDwrPlay(BRS *pdwr)
{
    AssertBaseThis(0);
    AssertVarMem(pdwr);

    *pdwr = rZero;

    if (kdwrNil == _dwrStep)
    {
        if (!_ptmpl->FGetDwrActnCel(_anidCur, _celnCur, pdwr))
            return fFalse;

        // Actor Studio's original handmade At Rest/action CELs were authored
        // with dwr=0. That is a valid animation pose, but it is not a usable
        // route step: after Resume Last Action records a route point, playback
        // advances by the template step and therefore leaves the actor pinned.
        // Stock 3DMM actions use a positive CEL step even for stationary body
        // animation. Match that behavior only for movie-owned AS templates.
        if (*pdwr <= rZero && F4DMMActorStudioOwnsTemplate(_pscen, &_tagTmpl))
            *pdwr = kdwr4DMMActorStudioDefault;
    }
    else
    {
        // Explicit step events, especially the terminal step=0 event at the
        // end of a subroute, must remain authoritative.
        *pdwr = _dwrStep;
    }

    *pdwr = BrsMul(*pdwr, _xfrm.rScaleStep);

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Return functional (sized) step size when recording.
    The returned size is expected to be authored > 0

***************************************************************************/
bool ACTR::_FGetDwrRecord(BRS *pdwr)
{
    AssertBaseThis(0);
    AssertVarMem(pdwr);

    *pdwr = rZero;

    if (!_ptmpl->FGetDwrActnCel(_anidCur, _celnCur, pdwr))
        return fFalse;

    if (*pdwr <= rZero && F4DMMActorStudioOwnsTemplate(_pscen, &_tagTmpl))
        *pdwr = kdwr4DMMActorStudioDefault;

    *pdwr = BrsMul(*pdwr, _xfrm.rScaleStep);

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Execute the current event.
    State Variables are updated.

***************************************************************************/
bool ACTR::_FDoAevCur(void)
{
    AssertBaseThis(0);

    if (!_FDoAevCore(_iaevCur))
        return fFalse;

    _iaevCur++;
    Assert(_iaevCur <= _pggaev->IvMac(), "_iaevCur bug");

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Execute the event iaev.

***************************************************************************/
bool ACTR::_FDoAevCore(int32_t iaev)
{
    AssertBaseThis(0);
    AssertIn(iaev, 0, _pggaev->IvMac());

    AEV aev;
    COST cost;

    _pggaev->GetFixed(iaev, &aev);
    if (aev.nfrm != _nfrmCur)
    {
        aev.nfrm = _nfrmCur;
        _pggaev->PutFixed(iaev, &aev);
    }
    AssertIn(aev.aet, 0, aetLim);

    switch (aev.aet)
    {
    case aetActn:
        AEVACTN aevactn;
        uint32_t grfactn;

        _pggaev->Get(iaev, &aevactn);

        // 3DMMv1.0: Empty the motion match sound list	if the action <changed>
        if (aevactn.anid != _anidCur)
            _pglsmm->FSetIvMac(0);

        if (!_ptmpl->FSetActnCel(_pbody, aevactn.anid, aevactn.celn))
        {
            return fFalse;
        }
        _celnCur = aevactn.celn;
        _anidCur = aevactn.anid;
        if (!_ptmpl->FGetCcelActn(_anidCur, &_ccelCur)) // 3DMMv1.0: Cache the cel count
            return fFalse;
        _iaevActnCur = iaev;

        // 3DMMv1.0: Force the location to the step event
        // 3DMMv1.0: Avoids incorrect event ordering on static segments
        _rtelCur = aev.rtel;
        _GetXyzFromRtel(&_rtelCur, &_xyzCur);
        if (!_ptmpl->FGetGrfactn(_anidCur, &grfactn))
        {
            return fFalse;
        }
        _grfactn = grfactn;
        break;

    case aetAdd:
        AEVADD aevadd;
        RPT rpt;
        // 3DMMv1.0: Save old costume in case of error
        if (!cost.FGet(_pbody))
            return fFalse;
        // 3DMMv1.0: Invoke the default costume/orientation
        if (!_ptmpl->FSetDefaultCost(_pbody))
            return fFalse;
        // 3DMMv1.0: Put the actor on stage.	Set up models.
        if (!_ptmpl->FSetActnCel(_pbody, _anidCur, _celnCur))
        {
            cost.Set(_pbody); // 3DMMv1.0: restore old costume
            return fFalse;
        }

        // 3DMMv1.0: Empty the motion match sound list
        _pglsmm->FSetIvMac(0);

        // 3DMMv1.0: Set the translation for the subroute
        _pggaev->Get(iaev, &aevadd);
        _dxyzSubRte.dxr = aevadd.dxr;
        _dxyzSubRte.dyr = aevadd.dyr;
        _dxyzSubRte.dzr = aevadd.dzr;
        _UpdateXyzRte();

        // 3DMMv1.0: Load the initial orientation
        _InitXfrm();
        _LoadAddOrien(&aevadd);

        // 3DMMv1.0: Set state variables
        _iaevFrmMin = _iaevAddCur = iaev;
        _rtelCur = aev.rtel;
        _GetXyzFromRtel(&_rtelCur, &_xyzCur);
        _pglrpt->Get(aev.rtel.irpt, &rpt);

        // 3DMMv1.0: Show the actor
        if (!_fOnStage)
            _pbody->Show();
        _fOnStage = fTrue;
        break;

    case aetRem:
        // 3DMMv1.0: Exit the actor from the stage
        _Hide();
        break;

    case aetCost:
        AEVCOST aevcost;
        _pggaev->Get(iaev, &aevcost);
        if (aevcost.fCmtl)
        {
            PCMTL pcmtl = _ptmpl->PcmtlFetch(aevcost.cmid);
            if (pvNil == pcmtl)
                return fFalse;
            _pbody->SetPartSetCmtl(pcmtl);
            ReleasePpo(&pcmtl);
        }
        else
        {
            PMTRL pmtrl;
            pmtrl = (PMTRL)vptagm->PbacoFetch(&aevcost.tag, MTRL::FReadMtrl);
            if (pvNil == pmtrl)
                return fFalse;
            _pbody->SetPartSetMtrl(aevcost.ibset, pmtrl);
            ReleasePpo(&pmtrl);
        }
        break;

    case aetRotF:
        // 3DMMv1.0: Actors are xformed in _FDoFrm, Rotate or Scale
        _pggaev->Get(iaev, &_xfrm.bmat34Fwd);
        _fUseBmat34Cur = fFalse;
        break;

    case aetRotH:
        // 3DMMv1.0: Actors are xformed in _FDoFrm, Rotate or Scale
        _pggaev->Get(iaev, &_xfrm.bmat34Cur);
        _fUseBmat34Cur = fTrue;
        break;

    case aetPull:
        // 3DMMv1.0: Actors are xformed in _FDoFrm, Rotate or Scale
        _pggaev->Get(iaev, &_xfrm.aevpull);
        break;

    case aetSize:
        // 3DMMv1.0: Actors are xformed in _FDoFrm, Rotate or Scale
        _pggaev->Get(iaev, &_xfrm.rScaleStep);
        break;

    case aetStep: // 3DMMv1.0: Exists for timing control (eg walk in place)
        _pggaev->Get(iaev, &_dwrStep);
        // 3DMMv1.0: Force the location to the step event
        // 3DMMv1.0: Avoids incorrect event ordering on static segments
        if (rZero == _dwrStep)
        {
            _rtelCur = aev.rtel;
            _GetXyzFromRtel(&_rtelCur, &_xyzCur);
        }
        break;

    case aetFreeze:
        int32_t fFrozen; // 3DMMv1.0: _fFrozen is a bit
        _pggaev->Get(iaev, &fFrozen);
        _fFrozen = FPure(fFrozen);
        break;

    case aetTweak:
        if (aev.rtel.dnfrm == _rtelCur.dnfrm)
            _pggaev->Get(iaev, &_xyzCur);
        // 3DMMv1.0: The actual locating of the actor is done in _FDoFrm or FTweakRoute
        break;

    case aetSnd:
        // 3DMMv1.0: Enqueue non-mm sounds
        if (!_FEnqueueSnd(iaev)) // 3DMMv1.0: Ignore failure
            return fFalse;
        break;

    case aetMove:
        XYZ dxyz;
        _pggaev->Get(iaev, &dxyz);
        _dxyzSubRte.dxr = BrsAdd(dxyz.dxr, _dxyzSubRte.dxr);
        _dxyzSubRte.dyr = BrsAdd(dxyz.dyr, _dxyzSubRte.dyr);
        _dxyzSubRte.dzr = BrsAdd(dxyz.dzr, _dxyzSubRte.dzr);
        _UpdateXyzRte();
        break;

    default:
        Bug("Unimplemented actor event");
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Add the specified event to the event list at the current frame, and
    execute the event

    NOTE:  Overwrites events of the same type if appropriate
    NOTE:  If the new action event is the same as the most recent
    previous action, the new action is still inserted.	This avoids
    repositioning the actor based on a past Set Action.

***************************************************************************/
bool ACTR::_FAddDoAev(int32_t aetNew, int32_t cbNew, void *pvVar)
{
    AssertBaseThis(0);
    AssertIn(aetNew, 0, aetLim);
    AssertIn(cbNew, 0, 100); // 3DMMv1.0: approximate upper bound
    AssertPvCb(pvVar, cbNew);
    Assert(_fOnStage || aetNew == aetAdd, "Error!  Beginning subroute with no Add Onstage event");

    AEV aev;
    int32_t iaevNew;
    // 3DMMv1.0: Setup fixed part of the gg
    aev.aet = aetNew;
    aev.rtel = _rtelCur;
    aev.nfrm = _nfrmCur;

    if (!_FInsertAev(_iaevCur, cbNew, pvVar, &aev))
        return fFalse;
    _MergeAev(_iaevFrmMin, _iaevCur, &iaevNew);

#ifdef BUG1870
    // 3DMMv1.0: REVIEW *****: V2.0
    // 3DMMv1.0:		Though the only situation in which this arises is aetMove,
    // 3DMMv1.0:		it might be that the FDoAevCore() should be called before
    // 3DMMv1.0:		merging events (so that any cumulative changes get executed
    // 3DMMv1.0:		only once, without requiring this special casing of aetMove.
    if (aetMove == aetNew)
    {
        // 3DMMv1.0: Skip the "do" of the FAddDoAev().
        // 3DMMv1.0: The 'Do' part currently merges events before
        // 3DMMv1.0: executing them, which in this case would cause
        // 3DMMv1.0: any existing same frame translaton to be added
        // 3DMMv1.0: to the state	variables twice.
        // 3DMMv1.0: Instead, adjust state var translation here.
        _dxyzSubRte.dxr = BrsAdd(((XYZ *)pvVar)->dxr, _dxyzSubRte.dxr);
        _dxyzSubRte.dyr = BrsAdd(((XYZ *)pvVar)->dyr, _dxyzSubRte.dyr);
        _dxyzSubRte.dzr = BrsAdd(((XYZ *)pvVar)->dzr, _dxyzSubRte.dzr);
    }
    else if (!_FDoAevCore(iaevNew))
        return fFalse;
#else  //! 3DMMv1.0: BUG1870
    if (!_FDoAevCore(iaevNew))
        return fFalse;
#endif //! 3DMMv1.0: BUG1870

    if (_iaevCur == iaevNew)
        _iaevCur++;

    _pscen->MarkDirty();

    Assert(aetNew != aetActn || _iaevActnCur == iaevNew, "_iaevActnCur not up to date");
    AssertIn(_iaevCur, 0, _pggaev->IvMac() + 1);
    Assert(!(_pggaev->IvMac() == 1 && _nfrmFirst != _nfrmCur), "check case");

    if (_nfrmCur < _nfrmFirst)
    {
        _pscen->InvalFrmRange();
        _nfrmFirst = _nfrmCur;
    }

    if (_nfrmCur > _nfrmLast)
    {
        _pscen->InvalFrmRange();
        _nfrmLast = _nfrmCur;
    }

    AssertThis(fobjAssertFull);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Merge event iaevNew among preceding events, beginning with
    event iaevFirst (which is usually the first event in the frame).
    Unless equal to pvNil, Return *piaevRtn to be the final index of the
    merged event
    NOTE:  Add events (only) are not merged.

***************************************************************************/
void ACTR::_MergeAev(int32_t iaevFirst, int32_t iaevNew, int32_t *piaevRtn)
{
    AssertBaseThis(0);
    AssertIn(iaevFirst, 0, _pggaev->IvMac());
    AssertIn(iaevNew, iaevFirst, _pggaev->IvMac());
    AssertNilOrVarMem(piaevRtn);

    AEV aev;
    AEV aevNew;
    void *pvVar;
    int32_t cbVar;
    int32_t iaev;
    BMAT34 bmat34;

    if ((_pggaev->IvMac() < iaevFirst) || (iaevFirst == iaevNew))
    {
        if (pvNil != piaevRtn)
            *piaevRtn = iaevNew;
        return;
    }

    _pggaev->GetFixed(iaevNew, &aevNew);

    //
    // 3DMMv1.0: Check if Aev is in the list already
    //
    for (iaev = iaevFirst; iaev < iaevNew; iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);

        if ((aev.aet != aevNew.aet) && (aev.aet != aetRem) && (aev.aet != aetRotH))
            continue;

        switch (aev.aet)
        {
        case aetRem:
            if (aevNew.aet == aetAdd && aevNew.nfrm != aev.nfrm)
            {
                _RemoveAev(iaev);
                iaevNew--;
                iaev--;
            }
            break;

        case aetAdd:
            // 3DMMv1.0: We cannot remove events associated with a same-frame Add event
            // 3DMMv1.0: or we will not retain costume etc information to propogate
            // 3DMMv1.0: forward (eg drag single pt actor offstage & roll call back)
            continue;
            break;

        case aetMove: // 3DMMv1.0: Move is accumulation of previous moves
            XYZ dxyz;
            XYZ dxyzNew;
            _pggaev->Get(iaev, &dxyz);
            _pggaev->Get(iaevNew, &dxyzNew);
            // 3DMMv1.0: Note: Merging moves should not alter state variables!
#ifndef BUG1870
            // 3DMMv1.0: Remove these lines of code
            _dxyzSubRte.dxr = BrsSub(_dxyzSubRte.dxr, dxyz.dxr);
            _dxyzSubRte.dyr = BrsSub(_dxyzSubRte.dyr, dxyz.dyr);
            _dxyzSubRte.dzr = BrsSub(_dxyzSubRte.dzr, dxyz.dzr);
#endif //! 3DMMv1.0: BUG1870
            dxyz.dxr = BrsAdd(dxyz.dxr, dxyzNew.dxr);
            dxyz.dyr = BrsAdd(dxyz.dyr, dxyzNew.dyr);
            dxyz.dzr = BrsAdd(dxyz.dzr, dxyzNew.dzr);
            _pggaev->Put(iaev, &dxyz);
            goto LDeleteNew;
            break;

        case aetCost:
            AEVCOST aevcost;
            AEVCOST aevcostNew;

            // 3DMMv1.0: Check that the body parts match
            _pggaev->Get(iaev, &aevcost);
            _pggaev->Get(iaevNew, &aevcostNew);

            if (aevcost.ibset != aevcostNew.ibset)
                continue;

            if (aev.rtel != aevNew.rtel)
                goto LDeleteOld;
            _pggaev->Put(iaev, &aevcostNew);
            goto LDeleteNew;
            break;

        case aetSnd:
            AEVSND aevsnd;
            AEVSND aevsndNew;
            int32_t ismm;
            SMM *psmm;
            // 3DMMv1.0: Check that the sound types match
            _pggaev->Get(iaev, &aevsnd);
            _pggaev->Get(iaevNew, &aevsndNew);
            if (MSND::SqnActr(aevsnd.sty, _arid) != MSND::SqnActr(aevsndNew.sty, _arid))
                continue;
            // 3DMMv1.0: Queued sounds need to have multiple events reside in a single frame
            if (aevsndNew.fQueue)
                continue;
            // 3DMMv1.0: Non queued sounds need to replace queued sounds of the same type
            // 3DMMv1.0: First, the _pggsmm needs to be updated
            for (ismm = 0; ismm < _pglsmm->IvMac(); ismm++)
            {
                psmm = (SMM *)_pglsmm->QvGet(ismm);
                if (psmm->aevsnd.sty == aevsnd.sty && aevsnd.celn == psmm->aevsnd.celn)
                {
                    _pglsmm->Delete(ismm);
                    break;
                }
            }
            _RemoveAev(iaev);
            iaevNew--;
            iaev--;
            continue;
            break;

        case aetRotH:
            if (aevNew.aet == aetRotF && aevNew.nfrm == aev.nfrm)
            {
                // 3DMMv1.0: Forward rotations must get rid of tweak rotations in the current frame
                _RemoveAev(iaev);
                iaevNew--;
                iaev--;
            }
            if (aevNew.aet != aetRotH)
                continue;
            if (aev.rtel != aevNew.rtel)
                goto LDeleteOld;
            _pggaev->Get(iaevNew, &bmat34);
            _pggaev->Put(iaev, &bmat34);
            goto LDeleteNew;
            break;

        case aetRotF:
            // 3DMMv1.0: New == old == forward-rotate
            // 3DMMv1.0: Need to replace the old rotation, but continue on to remove tweak-rotations
            _RemoveAev(iaev);
            iaevNew--;
            iaev--;
            continue;
            break;

        default:
            if (aev.rtel != aevNew.rtel)
            {
                goto LDeleteOld;
            }
            pvVar = _pggaev->QvGet(iaevNew, &cbVar);
            Assert(cbVar == _pggaev->Cb(iaev), "Wrong Var size");
            _pggaev->Put(iaev, pvVar);
            goto LDeleteNew;
        }
    }

    //
    // 3DMMv1.0: Leave it where it is.  No match found.
    //
    if (pvNil != piaevRtn)
        *piaevRtn = iaevNew;

    return;

LDeleteNew:
    if (pvNil != piaevRtn)
    {
        *piaevRtn = iaev;
        AssertIn(iaev, iaevFirst, iaevNew);
    }
    _RemoveAev(iaevNew);
    return;

LDeleteOld:
    _RemoveAev(iaev);
    iaevNew--;
    if (pvNil != piaevRtn)
        *piaevRtn = iaevNew;
}

/** 3DMMv1.0: *************************************************************************

    Add (or replace) an action
    Add the event to the event list

***************************************************************************/
bool ACTR::FSetActionCore(int32_t anid, int32_t celn, bool fFreeze)
{
    AssertThis(0);
    AssertIn(anid, 0, klwMax);
    AssertIn(celn, klwMin, klwMax);

    bool fStatic;
    bool fNewAction = fFalse;
    AEVACTN aevactn;
    int32_t anidPrev = _anidCur;
    int32_t iaevMin;

    int32_t cbVar = kcbVarActn + kcbVarFreeze + (kcbVarStep * 2);
    if (!_pggaev->FEnsureSpace(4, cbVar, fgrpNil))
        return fFalse;

    if (!_FGetStatic(anid, &fStatic))
        return fFalse;

    // 3DMMv1.0: If the action is changing:
    // 3DMMv1.0: Remove all motion match sounds of the previous action
    // 3DMMv1.0: Query the template and insert new sound events
    if (_anidCur != anid)
    {
        fNewAction = fTrue;
        if (!_FRemoveAevMm(anid))
            return fFalse;
    }

    // 3DMMv1.0: var part of gg
    aevactn.anid = anid;
    aevactn.celn = celn;

    // 3DMMv1.0: Add this action to the event list
    iaevMin = (_iaevFrmMin == _iaevAddCur) ? _iaevFrmMin + 1 : _iaevFrmMin;
    _PrepActnFill(iaevMin, _anidCur, anid, faetTweak | faetFreeze | faetActn);

    if (!_FAddDoAev(aetActn, kcbVarActn, &aevactn))
        return fFalse;

    // 3DMMv1.0: If the action is changing:
    // 3DMMv1.0: Query the template and insert new default motion match sound events
    if (fNewAction)
    {
        if (!_FAddAevDefMm(anid))
            return fFalse;
    }

    if (_nfrmCur != _nfrmLast)
    {
        _fLifeDirty = fTrue;
        _pscen->InvalFrmRange();
    }

    if (!fFreeze && !_FIsDoneAevSub(_iaevCur, _rtelCur))
    {
        if (!_FUnfreeze())
            return fFalse;
    }
    else
    {
        if (!_FFreeze())
            return fFalse;
    }

    // 3DMMv1.0: If the actor is in the middle of a static segment, the application of a
    // 3DMMv1.0: non-static action is supposed to make the actor start moving forward along
    // 3DMMv1.0: the remaining path.
    // 3DMMv1.0: The last frame in the subpath must retain its final step=0 event, however.
    if (!fStatic && !_ptmpl->FIsTdt() && !_FIsDoneAevSub(_iaevCur, _rtelCur))
    {
        if (!FSetStep(kdwrNil))
            return fFalse;
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Remove an actor from the stage
    NOTE: This is a low level API and has no effect on the event list

***************************************************************************/
void ACTR::_Hide(void)
{
    AssertBaseThis(0);

    if (_fOnStage && _pbody != pvNil)
        _pbody->Hide();
    _fOnStage = fFalse;
    return;
}

/** 3DMMv1.0: *************************************************************************

    Insert a node point *prpt in the route.
    Update *prpt.dwr, and the previous node's dwr.
    On input:
        *prpt.dwr == 0 iff the new node is a subpath terminating node
        *prpt.xyz has the new node coordinates
        dwrPrior is the distance from the previous node to the new one.

***************************************************************************/
bool ACTR::_FInsertGgRpt(int32_t irpt, RPT *prpt, BRS dwrPrior)
{
    AssertBaseThis(0);
    AssertIn(irpt, 0, _pglrpt->IvMac() + 1);
    AssertNilOrVarMem(prpt);

    RPT rpt;
    BRS dwrTotal = rZero; // 3DMMv1.0: Dist from prev node to node after the inserted node

    if (!_pglrpt->FInsert(irpt, prpt))
        return fFalse;

    if (0 < irpt)
    {
        _pglrpt->Get(irpt - 1, &rpt);
        dwrTotal = rpt.dwr;

        // 3DMMv1.0: Do not alter end of route dwr's
        if (rZero != rpt.dwr)
        {
            // 3DMMv1.0: Adjust the distance from the previous point here
            rpt.dwr = dwrPrior;
            if (rZero == rpt.dwr)
                rpt.dwr = rEps; // 3DMMv1.0: Epsilon.  Prevent pathological incorrect end-of-path
            _pglrpt->Put(irpt - 1, &rpt);
        }
        else
            Assert(dwrPrior == rZero, "Illegal distance");
    }

    if (irpt < _pglrpt->IvMac() - 1)
    {
        _pglrpt->Get(irpt + 1, &rpt);
        if (rZero != prpt->dwr) // 3DMMv1.0: If not at end of subroute
        {
            // 3DMMv1.0: Set the distance to the next point in this subroute
            Assert(rZero != dwrTotal, "Overwriting end of route");
            prpt->dwr = BrsSub(dwrTotal, dwrPrior);
            if (rZero >= prpt->dwr)
                prpt->dwr = rEps; // 3DMMv1.0: Epsilon.  Prevent pathological incorrect end-of-path
            _pglrpt->Put(irpt, prpt);
        }
    }
    else
        Assert(rZero == prpt->dwr, "Invalid end sub-node dwr");

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Set a Stepsize Event
    Add it to the event list

    dwrStep == 0 -> Stop the actor from forward motion.
    dwrStep == kdwrNil -> Use the scaled template step size
    Note that this is orthogonal and independent of freezing an actor

***************************************************************************/
bool ACTR::FSetStep(BRS dwrStep)
{
    AssertThis(0);
    Assert(kdwrNil == dwrStep || rZero <= dwrStep, "Invalid dwrStep argument");

    if (!_pggaev->FEnsureSpace(1, kcbVarStep, fgrpNil))
        return fFalse;

    return _FAddDoAev(aetStep, kcbVarStep, &dwrStep);
}

/** 3DMMv1.0: *************************************************************************

    Get the new origin for the to-be positioned actor.  Always place
    actors on the "floor" (Y = 0)...except 3-D Text actors, which are
    at Y = 1.0 meters.

***************************************************************************/
void ACTR::_GetNewOrigin(BRS *pxr, BRS *pyr, BRS *pzr)
{
    AssertThis(0);
    AssertVarMem(pxr);
    AssertVarMem(pyr);
    AssertVarMem(pzr);

    BRS xrCam = rZero;
    BRS yrCam = rZero;
    BRS zrCam = kzrDefault;
    BRS zrYon, zrHither;
    BMAT34 bmat34Cam;

    _pscen->Pbkgd()->GetActorPlacePoint(pxr, pyr, pzr);
    // 3DMMv1.0: A (0, 0, 0) place point means that one hasn't been authored yet,
    // 3DMMv1.0: so use the old system.
    if (*pxr == rZero && *pyr == rZero && *pzr == rZero)
    {
        _pscen->Pmvie()->Pbwld()->GetCamera(&bmat34Cam, &zrHither, &zrYon);

        Assert(zrCam < 0, "Non-negative placement value");
        Assert(zrHither >= 0 && zrYon >= 0, "Negative camera values");
        if (BR_ABS(zrCam) > zrYon)
            zrCam = BR_CONST_DIV(BR_ADD(zrYon, zrHither), -2);

        *pxr =
            BR_MAC3(xrCam, bmat34Cam.m[0][0], yrCam, bmat34Cam.m[1][0], zrCam, bmat34Cam.m[2][0]) + bmat34Cam.m[3][0];
        *pyr = rZero;
        *pzr =
            BR_MAC3(xrCam, bmat34Cam.m[0][2], yrCam, bmat34Cam.m[1][2], zrCam, bmat34Cam.m[2][2]) + bmat34Cam.m[3][2];
    }
    // Keep new actors, props and 3-D words at 3DMM's familiar insertion
    // point relative to the camera, even after a .3ct camera move.
    _pscen->Pmvie()->AdjustCameraTrackInsertionPoint(pxr, pyr, pzr);

    if (_ptmpl->FIsTdt())
        *pyr += BR_SCALAR(10.0); // 3DMMv1.0: 1.0 meters
}

/** 3DMMv1.0: *************************************************************************

    Add actor on the stage - ie, create a new subroute.
    Add the Add event to the event list
    Nukes existing subroute if obsoleted by current Add.

    Add the initialization events for costumes, xforms, etc from the
    preceding or subsequent subroute.
***************************************************************************/
bool ACTR::FAddOnStageCore(void)
{
    AssertThis(0);

    BRS xr;
    BRS yr;
    BRS zr;
    int32_t cbVar;
    AEVADD aevadd;
    bool fUpdateFrmRange = fFalse;

    cbVar = kcbVarAdd + kcbVarActn + kcbVarStep + kcbVarFreeze;
    if (_pggaev->IvMac() == 0)
    {
        if (!_pggaev->FEnsureSpace(4, cbVar, fgrpNil) || !_pglrpt->FEnsureSpace(1, fgrpNil))
            return fFalse;
    }
    else
    {
        cbVar += kcbVarCost + kcbVarRot + kcbVarSize + kcbVarPull;
        if (!_pggaev->FEnsureSpace(8, cbVar, fgrpNil) || !_pglrpt->FEnsureSpace(1, fgrpNil))
        {
            return fFalse;
        }
    }

    _GetNewOrigin(&xr, &yr, &zr);
    aevadd.dxr = BrsSub(xr, _dxyzFullRte.dxr);
    aevadd.dyr = BrsSub(yr, _dxyzFullRte.dyr);
    aevadd.dzr = BrsSub(zr, _dxyzFullRte.dzr);
    aevadd.xa = aevadd.za = aZero;

    // 3DMMv1.0: Rotate the actor to be facing the camera
    // 3DMMv1.0: NOTE: 3D spletter code uses this also.
    aevadd.ya = _pscen->Pbkgd()->BraRotYCamera() + _pscen->Pmvie()->BraCameraTrackYaw();

    if (_nfrmCur < _nfrmFirst)
    {
        fUpdateFrmRange = fTrue;
        _nfrmFirst = _nfrmCur;
    }
    if (_nfrmCur > _nfrmLast)
    {
        fUpdateFrmRange = fTrue;
        _nfrmLast = _nfrmCur;
    }

    RPT rptNil = {rZero, rZero, rZero, rZero};

    if (_fOnStage) // 3DMMv1.0: May have walked offstage
    {
        // 3DMMv1.0: Delete the remnant subroute (non-inclusive of current frame)
        _DeleteFwdCore(fFalse);
    }

    _rtelCur.dnfrm = 0;
    _rtelCur.dwrOffset = rZero;
    if (_iaevAddCur < 0)
        _rtelCur.irpt = 0;
    else
        _rtelCur.irpt++;

    AssertDo(_FInsertGgRpt(_rtelCur.irpt, &rptNil), "Logic error");
    _AdjustAevForRteIns(_rtelCur.irpt, 0);

    _GetXyzFromRtel(&_rtelCur, &_xyzCur);

    if (!_FAddDoAev(aetAdd, kcbVarAdd, &aevadd))
        return fFalse;

    // 3DMMv1.0: Copy costume and transform events forward if this is
    // 3DMMv1.0: the earliest Add event : each subroute needs all
    // 3DMMv1.0: initialization events.
    // 3DMMv1.0: Note: _iaevCur is already incremented at this point
    if (1 == _iaevCur)
    {
        // 3DMMv1.0: The earliest Add.  Gather later events and insert them
        if (!_FAddAevFromLater())
            return fFalse;
    }
    else
    {
        // 3DMMv1.0: Not the earliest Add.  Gather earlier events	and insert them
        uint32_t grfaet = faetActn | faetCost | faetPull | faetSize | faetRotF;
        if (!_FAddAevFromPrev(_iaevCur - 1, grfaet))
            return fFalse;
    }

    _PositionBody(&_xyzCur);

    if (!FSetStep(rZero))
        return fFalse;
    if (!_FFreeze())
        return fFalse;

    if (fUpdateFrmRange)
    {
        _pscen->InvalFrmRange();
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Collect events < iaevLim & insert them beginning at event number
    _iaevCur.
    NOTE:  The primary complication is to OMIT copying events which can
    never be reached due to static actions (stalling) in the previous
    subroute.  Due to initialization now spec'd to occur in every subroute,
    note that only the immediately preceding subroute is of concern.
    NOTE: This is optimized to add only the latest event of each given type.

***************************************************************************/
bool ACTR::_FAddAevFromPrev(int32_t iaevLim, uint32_t grfaet)
{
    AssertBaseThis(0);
    AssertIn(iaevLim, 0, _pggaev->IvMac() + 1);

    AEV aev;
    AEV *paev;
    AEV aevCur;
    int32_t cb;
    int32_t iaev;
    int32_t iaevAdd;
    int32_t iaevNew;
    int32_t iaevLast;
    bool fPrunedPrevSubrte = fFalse;

    // 3DMMv1.0: Locate the next active (not stalled) region of the subroute
    // 3DMMv1.0: Note: Not finding a previous aev is not a failure
    _FFindPrevAevAet(aetAdd, iaevLim, &iaevAdd);
    _FindAevLastSub(iaevAdd, iaevLim, &iaevLast);

    _pggaev->GetFixed(_iaevCur - 1, &aevCur);

    // 3DMMv1.0: It is more efficient to insert non-costume events backwards
    for (iaev = iaevLast; iaev > iaevAdd; iaev--)
    {
        if (0 == grfaet)
            break;

        Assert(iaev >= 0, "Logic error");
        _pggaev->GetFixed(iaev, &aev);
        if (!(grfaet & (1 << aev.aet)) || aev.aet == aetCost)
            continue;

        // 3DMMv1.0: Non-costume events are added only once
        grfaet ^= (1 << aev.aet);

        // 3DMMv1.0: Allocate space
        cb = _pggaev->Cb(iaev);
        aev.rtel = aevCur.rtel;
        // 3DMMv1.0: aev.nfrm is set by _FDoAevCore()

        if (!_FInsertAev(_iaevCur, cb, pvNil, &aev))
            return fFalse;

        // 3DMMv1.0: Insert event
        _pggaev->Put(_iaevCur, _pggaev->QvGet(iaev));

        // 3DMMv1.0: Merge events to avoid duplicates
        _MergeAev(_iaevFrmMin, _iaevCur, &iaevNew);

        if (!_FDoAevCore(iaevNew))
            return fFalse;

        if (iaevNew == _iaevCur)
            _iaevCur++;
    }

    if (!(grfaet & (1 << aetCost)))
        return fTrue;

    // 3DMMv1.0: Costumes needed to be gathered forward
    for (iaev = iaevAdd + 1; iaev <= iaevLast; iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);
        if (aev.aet != aetCost)
            continue;

        aev.rtel = aevCur.rtel;
        // 3DMMv1.0: aev.nfrm is set by _FDoAevCore()

        // 3DMMv1.0: Allocate space
        cb = _pggaev->Cb(iaev);
        if (!_FInsertAev(_iaevCur, cb, pvNil, &aev))
            return fFalse;

        // 3DMMv1.0: Insert event
        _pggaev->Put(_iaevCur, _pggaev->QvGet(iaev));

        // 3DMMv1.0: Merge events to avoid duplicates
        _MergeAev(_iaevFrmMin, _iaevCur, &iaevNew);

        if (!_FDoAevCore(iaevNew))
            return fFalse;

        if (iaevNew == _iaevCur)
            _iaevCur++;
    }

    // 3DMMv1.0: Remove redundant (same frame) events from previous Add
    if (iaevAdd < 0)
        return fTrue;

    // 3DMMv1.0: May need to delete entire previous subroute
    // 3DMMv1.0: if the prev Add occurred at the same frame
    paev = (AEV *)_pggaev->QvFixedGet(iaevAdd);
    if (_nfrmCur == paev->nfrm)
    {
        // 3DMMv1.0: Deleting the entire subroute
        _DelAddFrame(iaevAdd, _iaevAddCur);
        return fTrue;
    }

    // 3DMMv1.0: May need to prune out same-frame events from prev subrte
    for (iaev = _iaevAddCur - 1; iaev >= iaevAdd; iaev--)
    {
        paev = (AEV *)_pggaev->QvFixedGet(iaev);
        if (_nfrmCur == paev->nfrm)
        {
            fPrunedPrevSubrte = fTrue;
            _RemoveAev(iaev);
        }
    }

    if (fPrunedPrevSubrte)
    {
        // 3DMMv1.0: The previous path requires pruning
        // 3DMMv1.0: Terminating stop & freeze events need to be inserted
        int32_t nfrm = _nfrmCur;
        if (!FGotoFrame(_nfrmCur - 1))
            return fFalse;
        DeleteFwdCore(fFalse, pvNil, _iaevCur);
        return FGotoFrame(nfrm);
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Gather the initialization events of the subsequent subroute and insert
    them at the	current event index, _iaevCur.

***************************************************************************/
bool ACTR::_FAddAevFromLater(void)
{
    AssertBaseThis(0);

    AEV aev;
    int32_t iaev;
    int32_t iaevStart = -1;
    RTEL rtelAdd;
    bool fPositionBody = fFalse;

    Assert(1 == _iaevCur, "_FAddAevFromLater logic error");
    // 3DMMv1.0: Find the next Add event
    for (iaev = _iaevCur; iaev < _pggaev->IvMac(); iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);
        if (aev.aet == aetAdd)
        {
            iaevStart = iaev;
            rtelAdd = aev.rtel;
            break;
        }
    }

    if (iaevStart < 0)
        goto LEnd;

    // 3DMMv1.0: Stuff in the events from this next Add event
    for (iaev = iaevStart; iaev < _pggaev->IvMac(); iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);

        if (aev.rtel != rtelAdd)
        {
            break;
        }

        if (aetCost == aev.aet || aetPull == aev.aet || aetSize == aev.aet || aetRotF == aev.aet)
        {
            aev.rtel = _rtelCur;
            // 3DMMv1.0: aev.nfrm is set by _FDoAevCore()

            // 3DMMv1.0: Allocate space
            int32_t cbNew = _pggaev->Cb(iaev);
            if (!_FInsertAev(_iaevCur, cbNew, pvNil, &aev))
                return fFalse;
            iaev++;

            // 3DMMv1.0: Insert event
            _pggaev->Put(_iaevCur, _pggaev->QvGet(iaev));

            if (!_FDoAevCur())
                return fFalse;

            if (aev.aet != aetCost)
                fPositionBody = fTrue;
        }
    }

    if (fPositionBody)
    {
        _PositionBody(&_xyzCur);
    }

    Assert(_pggaev->IvMac() > 0, "Logic Error");

LEnd:
    return FSetActionCore(0, 0, 0);
}

/** 3DMMv1.0: *************************************************************************

    Locate the event of type aet with index closest to but smaller than iaevCur
    Return true if found, with its index in *piaevAdd

***************************************************************************/
bool ACTR::_FFindPrevAevAet(int32_t aet, int32_t iaevCur, int32_t *piaevAdd)
{
    AssertBaseThis(0);
    AssertIn(aet, 0, aetLim);
    AssertIn(iaevCur, 0, _pggaev->IvMac());
    AssertVarMem(piaevAdd);

    AEV aev;

    iaevCur--;
    for (; iaevCur >= 0; iaevCur--)
    {
        _pggaev->GetFixed(iaevCur, &aev);
        if (aev.aet == aet)
        {
            *piaevAdd = iaevCur;
            return fTrue;
        }
    }

    *piaevAdd = ivNil;
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Locate the event of type aet with index closest to but >= iaevCur.
    Return true if one is found, with its index in *piaevAdd

***************************************************************************/
bool ACTR::_FFindNextAevAet(int32_t aet, int32_t iaevCur, int32_t *piaevAdd)
{
    AssertBaseThis(0);
    AssertIn(aet, 0, aetLim);
    AssertIn(iaevCur, 0, _pggaev->IvMac() + 1);
    AssertVarMem(piaevAdd);

    AEV *paev;

    for (; iaevCur < _pggaev->IvMac(); iaevCur++)
    {
        paev = (AEV *)_pggaev->QvFixedGet(iaevCur);
        if (paev->aet == aet)
        {
            *piaevAdd = iaevCur;
            return fTrue;
        }
    }

    *piaevAdd = ivNil;
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Locate the last reachable event in the specified subroute
    Note: This is complicated by static actions.
    Input:
        Begin the search at iaevAdd.
        The limit of the search is iaevLim, which, if == ivNil, is
        the extent of the ggaev.
    Return its index in *piaevAdd

***************************************************************************/
void ACTR::_FindAevLastSub(int32_t iaevAdd, int32_t iaevLim, int32_t *piaevLast)
{
    AssertBaseThis(0);
    AssertIn(iaevAdd, 0, _pggaev->IvMac());
    if (iaevLim != ivNil)
        AssertIn(iaevLim, 0, _pggaev->IvMac() + 1);
    AssertVarMem(piaevLast);

    AEV aev;

    if (iaevLim == ivNil)
        iaevLim = _pggaev->IvMac();

    _pggaev->GetFixed(iaevLim - 1, &aev);

    if (_FIsStalled(iaevAdd, &aev.rtel, piaevLast))
    {
        // 3DMMv1.0: Last active event stored by _FIsStalled
        return;
    }

    *piaevLast = iaevLim - 1;
}

/** 3DMMv1.0: *************************************************************************

    Remove the actor from the stage
    Add the event to the event list

***************************************************************************/
bool ACTR::FRemFromStageCore(void)
{
    AssertThis(0);
    AEV *paev;

    if (!_fOnStage)
        return fTrue;

    if (!_pggaev->FEnsureSpace(1, kcbVarStep, fgrpNil)) // 3DMMv1.0: step
        return fFalse;

    AssertIn(_iaevAddCur, 0, _pggaev->IvMac());
    paev = (AEV *)_pggaev->QvFixedGet(_iaevAddCur);
    if (_nfrmCur == paev->nfrm)
    {
        if (!_FDeleteEntireSubrte())
            return fFalse;
    }

    if (_fOnStage)
        return _FAddDoAev(aetRem, kcbVarZero, pvNil);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Delete the current entire subroute & reposition actor accordingly.
    Based on current state variables

***************************************************************************/
bool ACTR::_FDeleteEntireSubrte(void)
{
    AssertThis(0);
    AEV *paev;
    int32_t nfrmSav;

    paev = (AEV *)_pggaev->QvFixedGet(_iaevAddCur);
    Assert(paev->nfrm == _nfrmCur, "Logic error: trying to delete whole route from the middle");

    // 3DMMv1.0: Delete forward from here	(exclusive of current point)
    _DeleteFwdCore(fFalse, pvNil, _iaevCur);

    // 3DMMv1.0: Delete events & path for the sole remaining Add frame
    _DelAddFrame(_iaevAddCur, _iaevCur);

    // 3DMMv1.0: A hide should be done separately from event execution
    // 3DMMv1.0: because no subroute exists in this case
    _Hide();
    if (_ptmpl != pvNil && _pbody != pvNil)
        _ptmpl->FSetDefaultCost(_pbody);
    _SetStateRewound();
    nfrmSav = _nfrmCur;
    _nfrmCur = knfrmInvalid;
    if (_pggaev->IvMac() > 0)
    {
        paev = (AEV *)_pggaev->QvFixedGet(0);
        Assert(paev->aet == aetAdd, "Corrupt event list");
        _nfrmFirst = paev->nfrm;
        return FGotoFrame(nfrmSav);
    }
    _nfrmFirst = knfrmInvalid;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Delete the events and point corresponding to a single point subroute

***************************************************************************/
void ACTR::_DelAddFrame(int32_t iaevAdd, int32_t iaevLim)
{
    AssertThis(0);
    int32_t iaev;
    int32_t irptAdd;
    AEV *paev;

    AssertIn(iaevAdd, 0, iaevLim);

    paev = (AEV *)_pggaev->QvFixedGet(iaevAdd);
    irptAdd = paev->rtel.irpt;

    // 3DMMv1.0: Delete events in current frame of subroute
    for (iaev = iaevLim - 1; iaev >= iaevAdd; iaev--)
    {
        _RemoveAev(iaev);
    }

    // 3DMMv1.0: Delete current point	if unused
    if (iaevAdd > 0)
    {
        paev = (AEV *)_pggaev->QvFixedGet(iaevAdd - 1);
        if (paev->rtel.irpt == irptAdd || (paev->rtel.irpt == (irptAdd - 1) && paev->rtel.dwrOffset > rZero))
        {
            return;
        }
    }
    _AdjustAevForRteDel(irptAdd, iaevAdd);
    _pglrpt->Delete(irptAdd);
    if (_rtelCur.irpt > irptAdd)
        _rtelCur.irpt--;

    return;
}

/** 3DMMv1.0: *************************************************************************

    Set the Costume for a body part
    Add the event to the event list

***************************************************************************/
bool ACTR::FSetCostumeCore(int32_t ibsetClicked, TAG *ptag, int32_t cmid, tribool fCmtl)
{
    AssertThis(0);
    Assert(fCmtl || ibsetClicked >= 0, "Invalid ibsetClicked argument");
    AssertVarMem(ptag);

    AEVCOST aevcost;
    PCMTL pcmtl;
    int32_t ibsetApply;

    // 3DMMv1.0: For custom materials, the ibset is a property of the CMTL itself,
    // 3DMMv1.0: so read it from the CMTL rather than using the clicked ibset.
    if (fCmtl)
    {
        pcmtl = _ptmpl->PcmtlFetch(cmid);
        if (pvNil == pcmtl)
            return fFalse;
        ibsetApply = pcmtl->Ibset();
        ReleasePpo(&pcmtl);
    }
    else
    {
        ibsetApply = ibsetClicked;
    }

    aevcost.ibset = ibsetApply;
    aevcost.tag = *ptag;
    aevcost.cmid = cmid;
    aevcost.fCmtl = fCmtl;

    if (!_pggaev->FEnsureSpace(1, kcbVarCost, fgrpNil))
        return fFalse;

    _PrepCostFill(_iaevCur, &aevcost);
    if (!_FAddDoAev(aetCost, kcbVarCost, &aevcost))
        return fFalse;

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Freeze the actor (don't advance cels)
    Add the event to the event list

***************************************************************************/
bool ACTR::_FFreeze(void)
{
    AssertThis(0);

    int32_t faevfrz = (int32_t)fTrue;
    return _FAddDoAev(aetFreeze, kcbVarFreeze, &faevfrz);
}

/** 3DMMv1.0: *************************************************************************

    Unfreeze the actor
    Add the event to the event list

***************************************************************************/
bool ACTR::_FUnfreeze(void)
{
    AssertThis(0);
    int32_t faevfrz = (int32_t)fFalse;
    return _FAddDoAev(aetFreeze, kcbVarFreeze, &faevfrz);
}

/** 3DMMv1.0: *************************************************************************

    Modify the add event to re-orient the actor
    The add event retains the single point orientation.

    On Input: If pxyz == pvNil, no averaging is done in orientation
    grfbra are the flags indicating angles of rotation.  In directions
    in which no rotation has taken place, the wa are invalid.

    WARNING: Rotations are non-abelian.  Ordering is x then y then z.
    If ordering other than this is desired, separate calls must be made.

***************************************************************************/
void ACTR::SetAddOrient(BRA xa, BRA ya, BRA za, uint32_t grfbra, XYZ *pdxyz)
{
    AssertThis(0);
    AssertIn(_iaevAddCur, 0, _pggaev->IvMac());

    AEVADD aevadd;

    _pggaev->Get(_iaevAddCur, &aevadd);

    if (pvNil == pdxyz)
    {
        aevadd.xa = xa;
        aevadd.ya = ya;
        aevadd.za = za;
    }
    else
    {
        BRS rWeight;
        BRS dwrNew = BR_LENGTH3(pdxyz->dxr, pdxyz->dyr, pdxyz->dzr);
        rWeight = BrsDiv(dwrNew, kdwrFast);
        rWeight = LwBound(rWeight, krOriWeightMin, rOne);

        if (grfbra & fbraRotateX)
            aevadd.xa = _BraAvgAngle(xa, aevadd.xa, rWeight);

        if (grfbra & fbraRotateY)
            aevadd.ya = _BraAvgAngle(ya, aevadd.ya, rWeight);

        if (grfbra & fbraRotateZ)
            aevadd.za = _BraAvgAngle(za, aevadd.za, rWeight);
    }

    // 3DMMv1.0: Modify the event
    _pggaev->Put(_iaevAddCur, &aevadd);
    _xfrm.xaPath = aevadd.xa;
    _xfrm.yaPath = aevadd.ya;
    _xfrm.zaPath = aevadd.za;
}

/** 3DMMv1.0: *************************************************************************

    Average two angles by (rw1 * a1 + (1-rw1)*a2)
    rw1 is the weighting for a1

***************************************************************************/
BRA ACTR::_BraAvgAngle(BRA a1, BRA a2, BRS rw1)
{
    AssertBaseThis(0);
    AssertIn(rw1, rZero, rOne + rEps);

    BRS rT = rZero;
    BRS r1 = BrAngleToScalar(a1);
    BRS r2 = BrAngleToScalar(a2);
    BRS rw2;

    rw2 = BrsSub(rOne, rw1);

    // 3DMMv1.0: Compensate for averaging across 0 degrees
    if (BrsAbs(BrsSub(r1, r2)) > rOneHalf)
    {
        if (r2 > r1)
        {
            // 3DMMv1.0: Add equiv of 360 degrees for each weight of r1
            rT = BrsAdd(rT, rw1);
        }
        else
        {
            // 3DMMv1.0: Add equiv of 360 degrees for each weight of r1
            rT = BrsAdd(rT, rw2);
        }
    }

    rT = BrsAdd(rT, BrsMul(r1, rw1));
    rT = BrsAdd(rT, BrsMul(r2, rw2));

    if (rT > rOne)
        rT = rT & rFractMax;

    return (BrScalarToAngle(rT));
}

/** 3DMMv1.0: *************************************************************************

    Rotate the actor
    Add the event to the event list
    If !fFromHereFwd, the rotation is to apply to the current frame only

    WARNING: Rotations are non-abelian.  Ordering is x then y then z.
    If ordering other than this is desired, separate calls must be made.

***************************************************************************/
bool ACTR::FRotate(BRA xa, BRA ya, BRA za, bool fFromHereFwd)
{
    AssertThis(0);

    BMAT34 *pbmat34;
    int32_t aet;

    if (!_pggaev->FEnsureSpace(1, kcbVarRot, fgrpNil))
        return fFalse;

    if (fFromHereFwd)
    {
        aet = aetRotF;
        pbmat34 = &_xfrm.bmat34Fwd;
        if (_fUseBmat34Cur) // 3DMMv1.0: _xfrm.bmat34Cur has last been used
        {
            // 3DMMv1.0: _xfrm.bmat34Cur stores the complete (no path added) orientation
            BrMatrix34Copy(pbmat34, &_xfrm.bmat34Cur);
            // 3DMMv1.0: Back the path orientation out from this matrix.
            // 3DMMv1.0: Otherwise the actor will jump in angle
            // 3DMMv1.0: This MUST be done in z then y then x order
            // 3DMMv1.0: Note: matrix inversion unnecessary
            if (_xfrm.zaPath != aZero)
                BrMatrix34PostRotateZ(pbmat34, -_xfrm.zaPath);
            if (_xfrm.yaPath != aZero)
                BrMatrix34PostRotateY(pbmat34, -_xfrm.yaPath);
            if (_xfrm.xaPath != aZero)
                BrMatrix34PostRotateX(pbmat34, -_xfrm.xaPath);
        }
    }
    else
    {
        // 3DMMv1.0: _xfrm.bmat34Cur is kept current at <all> frames so that
        // 3DMMv1.0: rotations can be post applied to it
        aet = aetRotH;
        pbmat34 = &_xfrm.bmat34Cur;
    }

    if (aZero != xa)
    {
        BrMatrix34PreRotateX(pbmat34, xa);
    }
    if (aZero != ya)
    {
        BrMatrix34PreRotateY(pbmat34, ya);
    }
    if (aZero != za)
    {
        BrMatrix34PreRotateZ(pbmat34, za);
    }

    // Repeated fixed-point matrix multiplies slowly pull the rotation basis
    // away from unit length and perpendicular axes.  That numerical drift
    // appears as unintended squash/stretch which grows as the actor is
    // rotated.  Rotation matrices contain no intentional actor scaling, so
    // restore an orthonormal basis before storing the rotation event.
    _NormalizeActorRotation(pbmat34);

    Assert(_iaevCur <= _pggaev->IvMac(), "_iaevCur bug");

    // 3DMMv1.0: Add the event
    if (fFromHereFwd)
        _PrepXfrmFill(aetRotF, pbmat34, kcbVarRot, _iaevCur, ivNil, faetNil);
    AssertDo(_FAddDoAev(aet, kcbVarRot, pbmat34), "Ensure space insufficient");

    _PositionBody(&_xyzCur);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    "Normalize" the actor
    The revert tool
    Insert normalize transformation events into the event list

***************************************************************************/
bool ACTR::FNormalizeCore(uint32_t grfnorm)
{
    AssertThis(0);
    int32_t rScaleStepOld = _xfrm.rScaleStep;
    uint32_t faet;
    int32_t cbVar = 0;

    if (grfnorm & fnormRotate)
        cbVar += kcbVarRot;
    if (grfnorm & fnormSize)
        cbVar += kcbVarSize + kcbVarPull;

    if (!_pggaev->FEnsureSpace(3, cbVar, fgrpNil))
        return fFalse;

    if (grfnorm & fnormRotate)
    {
        _InitXfrmRot(&_xfrm.bmat34Fwd);
        _InitXfrmRot(&_xfrm.bmat34Cur);
        _PrepXfrmFill(aetRotF, &_xfrm.bmat34Fwd, kcbVarRot, _iaevCur);
        AssertDo(_FAddDoAev(aetRotF, kcbVarRot, &_xfrm.bmat34Fwd), "EnsureSpace insufficient");
    }

    if (grfnorm & fnormSize)
    {
        _xfrm.aevpull.rScaleX = rOne;
        _xfrm.aevpull.rScaleY = rOne;
        _xfrm.aevpull.rScaleZ = rOne;
        faet = (_xfrm.rScaleStep != rOne) ? faetTweak : faetNil;
        _xfrm.rScaleStep = rOne;
        _PrepXfrmFill(aetPull, &_xfrm.aevpull, kcbVarPull, _iaevCur);
        _PrepXfrmFill(aetSize, &_xfrm.rScaleStep, kcbVarSize, _iaevCur, ivNil, faet);

        AssertDo(_FAddDoAev(aetPull, kcbVarPull, &_xfrm.aevpull), "EnsureSpace insufficient");
        AssertDo(_FAddDoAev(aetSize, kcbVarSize, &_xfrm.rScaleStep), "EnsureSpace insufficient");

        // 3DMMv1.0: Possibly extend life of actor if shrunk
        if (rScaleStepOld > rOne && pvNil != _pscen && _rtelCur.irpt < _pglrpt->IvMac() - 1) // 3DMMv1.0: optimization
        {
            _fLifeDirty = fTrue;
            _pscen->InvalFrmRange();
        }
    }

    Assert(_iaevCur <= _pggaev->IvMac(), "_iaevCur bug");

    _PositionBody(&_xyzCur);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Scale the actor.
    Add the event to the event list
    Bound the allowable scaling.

    Note that the stepsize and the actor lifetime is then subject to change,
    with the attendent specified tweak etc modifications.

***************************************************************************/
bool ACTR::FScale(BRS rScale, BRS rScaleMin, BRS rScaleMax)
{
    AssertThis(0);

    // A relative scale of exactly 1 is a true no-op.  Do not create an aetSize
    // event or re-read XYZ from the route; that route refresh can teleport a
    // moved/pasted actor merely by clicking it with the Resize tool.
    if (rScale == rOne)
        return fFalse;

    uint32_t faet = faetNil;
    int32_t rScaleStep;
    int32_t rScaleStepOld = _xfrm.rScaleStep;

    if (!_pggaev->FEnsureSpace(1, kcbVarSize, fgrpNil))
        return fFalse;

    rScale = LwBound(rScale, krScaleMinFactor, krScaleMaxFactor);

    rScaleStep = BrsMul(_xfrm.rScaleStep, rScale);
    // Ordinary growth is capped at the requested ceiling (normally 10x).
    // Existing oversized objects remain legal: their current scale becomes
    // the temporary upper bound so they can be shrunk gradually instead of
    // snapping from e.g. 50x straight back to 10x on the first drag tick.
    BRS rScaleLower = LwBound(rScaleMin, krScaleMinExtended, krScaleMin);
    BRS rScaleUpper = LwBound(rScaleMax, krScaleMaxNormal, krScaleMax);
    // Existing out-of-normal-range objects remain legal when the corresponding
    // extension is later disabled. They can be moved gradually back toward the
    // normal range instead of snapping on the next mouse sample.
    if (_xfrm.rScaleStep < rScaleLower)
        rScaleLower = _xfrm.rScaleStep;
    if (_xfrm.rScaleStep > rScaleUpper)
        rScaleUpper = _xfrm.rScaleStep;
    rScaleStep = LwBound(rScaleStep, rScaleLower, rScaleUpper);
    _xfrm.rScaleStep = rScaleStep;

    // 3DMMv1.0: Remove tweaks when a transformation that changes stepsize occurs
    faet = (_xfrm.rScaleStep != rScale) ? faetTweak : faetNil;
    _PrepXfrmFill(aetSize, &_xfrm.rScaleStep, kcbVarSize, _iaevCur, ivNil, faet);

    if (!_FAddDoAev(aetSize, kcbVarSize, &_xfrm.rScaleStep))
        return fFalse;

    Assert(_iaevCur <= _pggaev->IvMac(), "_iaevCur bug");

    // Scaling changes size, not route position. _xyzCur already contains the
    // actor's current composed move/tweak position. Re-reading XYZ from RTEL
    // here discards those edits on the first non-zero Resize drag and snaps a
    // moved/pasted actor back toward its route/spawn position.
    _PositionBody(&_xyzCur);

    // 3DMMv1.0: Possibly extend life of actor if shrunk
    if (rScaleStepOld != _xfrm.rScaleStep && pvNil != _pscen && _rtelCur.irpt < _pglrpt->IvMac() - 1) // 3DMMv1.0: optimization
    {
        _fLifeDirty = fTrue;
        _pscen->InvalFrmRange();
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Drag the actor forwards or backwards in time
    NOTE** Drag this subroute only -> sliding other subroutes in time

***************************************************************************/
bool ACTR::FSoonerLater(int32_t dnfrm)
{
    AssertThis(0);

    AEV aev;
    AEV *paevPrev;
    int32_t nfrmSav;
    int32_t iaev;
    int32_t fSuccess;
    int32_t dnfrmSub;
    int32_t dnfrmT;

    if (0 == dnfrm)
        return fTrue;

    if (_fLifeDirty)
    {
        if (!_FComputeLifetime(pvNil))
            PushErc(ercSocBadFrameSlider);
    }

    Assert(_iaevAddCur >= 0, "Invalid value for _iaevAddCur");

    // 3DMMv1.0: On a Sooner operation, slide the earlier subroutes
    // 3DMMv1.0: backward in time
    if (dnfrm < 0)
    {
        dnfrmT = (-dnfrm);
        // 3DMMv1.0: The times of past events need to be correct.
        for (iaev = _iaevCur - 1; dnfrmT > 0 && iaev >= 0; iaev--)
        {
            _pggaev->GetFixed(iaev, &aev);
            if (aev.aet != aetAdd || iaev == 0)
            {
                aev.nfrm -= dnfrmT;
                _pggaev->PutFixed(iaev, &aev);
                continue;
            }

            // 3DMMv1.0: Account for subroute gaps
            paevPrev = (AEV *)_pggaev->QvFixedGet(iaev - 1);
            dnfrmSub = aev.nfrm - (paevPrev->nfrm);
            aev.nfrm -= dnfrmT;
            _pggaev->PutFixed(iaev, &aev);
            dnfrmT -= (dnfrmSub - 1);
        }
    }
    else
    {
        // 3DMMv1.0: Later
        dnfrmT = dnfrm;
        for (iaev = _iaevAddCur; dnfrmT > 0 && iaev < _pggaev->IvMac(); iaev++)
        {
            _pggaev->GetFixed(iaev, &aev);
            if (aev.aet != aetAdd || iaev == _iaevAddCur)
            {
                nfrmSav = aev.nfrm;
                aev.nfrm += dnfrmT;
                _pggaev->PutFixed(iaev, &aev);
                continue;
            }
            // 3DMMv1.0: Adjust for the gap between subroutes
            dnfrmSub = aev.nfrm - nfrmSav;
            dnfrmT -= (dnfrmSub - 1);
            if (dnfrmT <= 0)
                break;
            aev.nfrm += dnfrmT;
            _pggaev->PutFixed(iaev, &aev);
        }
    }
    _pggaev->GetFixed(0, &aev);
    _nfrmFirst = aev.nfrm;
    _pggaev->GetFixed(_pggaev->IvMac() - 1, &aev);
    _nfrmLast = aev.nfrm;

    _nfrmCur += dnfrm;

    // 3DMMv1.0: Invalidate, but do not recompute the range
    _pscen->InvalFrmRange();

    if (fSuccess = _pscen->FGotoFrm(_pscen->Nfrm() + dnfrm))
    {
        _pscen->Pmvie()->Pmcc()->UpdateScrollbars();
    }
    return fSuccess;
}

/** 3DMMv1.0: *************************************************************************

    Pull, stretch, squash the actor.
    Add the event to the event list

    Bound the allowable scaling.

***************************************************************************/
bool ACTR::FPull(BRS rScaleX, BRS rScaleY, BRS rScaleZ)
{
    AssertThis(0);

    BRS rScaleXT;
    BRS rScaleYT;
    BRS rScaleZT;

    if (!_pggaev->FEnsureSpace(1, kcbVarPull, fgrpNil))
        return fFalse;

    rScaleX = LwBound(rScaleX, krScaleMinFactor, krScaleMaxFactor);
    rScaleY = LwBound(rScaleY, krScaleMinFactor, krScaleMaxFactor);
    rScaleZ = LwBound(rScaleZ, krScaleMinFactor, krScaleMaxFactor);

    rScaleXT = BrsMul(_xfrm.aevpull.rScaleX, rScaleX);
    rScaleYT = BrsMul(_xfrm.aevpull.rScaleY, rScaleY);
    rScaleZT = BrsMul(_xfrm.aevpull.rScaleZ, rScaleZ);

    _xfrm.aevpull.rScaleX = LwBound(rScaleXT, krPullMin, krPullMax);
    _xfrm.aevpull.rScaleY = LwBound(rScaleYT, krPullMin, krPullMax);
    _xfrm.aevpull.rScaleZ = LwBound(rScaleZT, krPullMin, krPullMax);

    _PrepXfrmFill(aetPull, &_xfrm.aevpull, kcbVarPull, _iaevCur);

    if (!_FAddDoAev(aetPull, kcbVarPull, &_xfrm.aevpull))
        return fFalse;

    Assert(_iaevCur <= _pggaev->IvMac(), "_iaevCur bug");

    _PositionBody(&_xyzCur);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Translate one point of the actor's path
    Add an event to the event list

    The (dxr,dyr,dzr) passed in is a delta distance from the previous
    location (set by GoToFrame, ie, determined by _rtelCur).

***************************************************************************/
bool ACTR::FTweakRoute(BRS dxr, BRS dyr, BRS dzr, uint32_t grfmaf)
{
    AssertThis(0);

    XYZ xyz;


    if (!_pggaev->FEnsureSpace(1, kcbVarTweak, fgrpNil))
        return fFalse;

    xyz.dxr = BrsAdd(_xyzCur.dxr, dxr);
    xyz.dyr = BrsAdd(_xyzCur.dyr, dyr);
    xyz.dzr = BrsAdd(_xyzCur.dzr, dzr);

    if ((grfmaf & fmafGround) && (BrsAdd(_xyzCur.dyr, _dxyzRte.dyr) >= rZero) &&
        (BrsAdd(xyz.dyr, _dxyzRte.dyr) < rZero))
    {
        xyz.dyr = -_dxyzRte.dyr;
    }

    if (!_FAddDoAev(aetTweak, kcbVarTweak, &xyz))
        return fFalse;

    _PositionBody(&xyz);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Update xyzRte from the overall origin & the subroute translation

***************************************************************************/
void ACTR::_UpdateXyzRte(void)
{
    AssertBaseThis(0);

    _dxyzRte.dxr = BrsAdd(_dxyzFullRte.dxr, _dxyzSubRte.dxr);
    _dxyzRte.dyr = BrsAdd(_dxyzFullRte.dyr, _dxyzSubRte.dyr);
    _dxyzRte.dzr = BrsAdd(_dxyzFullRte.dzr, _dxyzSubRte.dzr);
}

/** 3DMMv1.0: *************************************************************************

    Move the current subroute.	(Compose tool)

    If fmafOrient is set, the actor is oriented and moved.

    If fmafEntireSubrte is set, this does not add an event to the aev list.
    It alters the data in the Add event & _dxyzSubRte.	Return fTrue.

    If fmafEntireScene is set, _dxyzFullRte is modified. Return fTrue.

    If !fmafEntireSubrte && !fmafEntireScene, a move event is added
    to the event list. This can therefore fail.

    Return success or failure
    Return *pfmoved indicating whether or not the path moved.

***************************************************************************/
bool ACTR::FMoveRoute(BRS dxr, BRS dyr, BRS dzr, bool *pfMoved, uint32_t grfmaf)
{
    AssertThis(0);
    AssertNilOrVarMem(pfMoved);

    AEVADD aevadd;
    AEV *paev;
    int32_t iaev;
    bool fMoved;
    uint32_t grfbra;
    XYZ dxyz;
    BRS yrCurOld;
    BRA xa;
    BRA ya;
    BRA za;

    fMoved = (rZero != dxr || rZero != dyr || rZero != dzr);
    if (pvNil != pfMoved)
        *pfMoved = fMoved;

    if (!fMoved)
        return fTrue;

    if (!_pggaev->FEnsureSpace(2, kcbVarMove + kcbVarRot, fgrpNil))
        return fFalse;

    // 3DMMv1.0: Edit dyr to respect ground
    yrCurOld = BrsAdd(_xyzCur.dyr, _dxyzRte.dyr);
    if (FPure(grfmaf & fmafGround) && (yrCurOld >= rZero) && (BrsAdd(dyr, yrCurOld) < rZero))
    {
        dyr = BrsSub(rZero, yrCurOld);
    }

    // 3DMMv1.0: Update Actor's orientation
    if (FPure(grfmaf & fmafOrient) && !_ptmpl->FIsTdt())
    {
        dxyz.dxr = dxr;
        dxyz.dyr = dyr;
        dxyz.dzr = dzr;

        // 3DMMv1.0: Back the path orientation out from this matrix & replace
        // 3DMMv1.0: with new orientation
        if (_xfrm.zaPath != aZero)
            BrMatrix34PostRotateZ(&_xfrm.bmat34Cur, -_xfrm.zaPath);
        if (_xfrm.yaPath != aZero)
            BrMatrix34PostRotateY(&_xfrm.bmat34Cur, -_xfrm.yaPath);
        if (_xfrm.xaPath != aZero)
            BrMatrix34PostRotateX(&_xfrm.bmat34Cur, -_xfrm.xaPath);

        _ApplyRotFromVec(&dxyz, pvNil, &xa, &ya, &za, &grfbra);
        SetAddOrient(xa, ya, za, grfbra, &dxyz);

        // 3DMMv1.0: Force speed to be inversely proportional to angular rotation
        if (_iaevAddCur != ivNil)
        {
            BRS dwra, drxa, drya, drza;
            BRS ra1, ra2;
#ifdef DEBUG
            {
                AEV *paev;
                paev = (AEV *)_pggaev->QvFixedGet(_iaevAddCur);
                Assert(_nfrmCur == paev->nfrm, "Unsupported use of fmafOrient");
            }
#endif // 3DMMv1.0: DEBUG
       // 3DMMv1.0:  Set dwra = angular change (in scalar form)
            _pggaev->Get(_iaevAddCur, &aevadd);
            ra1 = BrAngleToScalar(xa);
            ra2 = BrAngleToScalar(aevadd.xa);
            drxa = BrsAbs(BrsSub(ra1, ra2));
            if (drxa > rOneHalf)
            {
                if (ra2 > ra1)
                    ra1 = BrsAdd(rOne, ra1);
                else
                    ra2 = BrsAdd(rOne, ra2);
                drxa = BrsAbs(BrsSub(ra1, ra2));
            }

            ra1 = BrAngleToScalar(ya);
            ra2 = BrAngleToScalar(aevadd.ya);
            drya = BrsAbs(BrsSub(ra1, ra2));
            if (drya > rOneHalf)
            {
                if (ra2 > ra1)
                    ra1 = BrsAdd(rOne, ra1);
                else
                    ra2 = BrsAdd(rOne, ra2);
                drya = BrsAbs(BrsSub(ra1, ra2));
            }

            ra1 = BrAngleToScalar(za);
            ra2 = BrAngleToScalar(aevadd.za);
            drza = BrsAbs(BrsSub(ra1, ra2));
            if (drza > rOneHalf)
            {
                if (ra2 > ra1)
                    ra1 = BrsAdd(rOne, ra1);
                else
                    ra2 = BrsAdd(rOne, ra2);
                drza = BrsAbs(BrsSub(ra1, ra2));
            }

            dwra = BR_LENGTH3(drxa, drya, drza);

            // 3DMMv1.0: Compute a bounded inverse of the angular change
            dwra = LwBound(dwra, krAngleMin, krAngleMax);
            dwra = BrsDiv(BrsRcp(dwra), krAngleMinRcp);
            AssertIn(dwra, rZero, BrsAdd(rOne, rEps));

            // 3DMMv1.0: Adjust the distances
            dxr = BrsMul(dxr, dwra);
            dyr = BrsMul(dyr, dwra);
            dzr = BrsMul(dzr, dwra);
        }

        // 3DMMv1.0: _xfrm.bmat34Cur is to hold the current full rotation
        _LoadAddOrien(&aevadd, fTrue);

        // 3DMMv1.0: Update any type of rotate event in this frame
        for (iaev = _iaevAddCur + 1; iaev < _iaevCur; iaev++)
        {
            paev = (AEV *)_pggaev->QvFixedGet(iaev);
            if (aetRotH == paev->aet)
                _pggaev->Put(iaev, &_xfrm.bmat34Cur);
            if (aetRotF == paev->aet)
            {
                // 3DMMv1.0: Insert orientation-rotation event
                if (!_FAddDoAev(aetRotH, kcbVarRot, &_xfrm.bmat34Cur))
                {
                    Bug("Should have ensured space");
                    return fFalse;
                }
            }
        }
    }

    // 3DMMv1.0: Update actor's position
    if (FPure(grfmaf & fmafEntireScene))
    {
        _dxyzFullRte.dxr = BrsAdd(dxr, _dxyzFullRte.dxr);
        _dxyzFullRte.dyr = BrsAdd(dyr, _dxyzFullRte.dyr);
        _dxyzFullRte.dzr = BrsAdd(dzr, _dxyzFullRte.dzr);
    }
    else
    {
        if (_iaevAddCur == ivNil)
            return fFalse;

        if (!FPure(grfmaf & fmafEntireSubrte))
        {
            // 3DMMv1.0: Move actor just from this frame on (this subpath only)
            // 3DMMv1.0: Note: FAddDoAev will update _dxyzSubRte
            dxyz.dxr = dxr;
            dxyz.dyr = dyr;
            dxyz.dzr = dzr;
            if (!_FAddDoAev(aetMove, kcbVarMove, &dxyz))
                return fFalse;
        }
        else
        {
            // 3DMMv1.0: Translating whole subroute
#ifdef DEBUG
            int32_t cbVar = _pggaev->Cb(_iaevAddCur);
            Assert(cbVar == kcbVarAdd, "Corrupt aev");
#endif // 3DMMv1.0: DEBUG
       // 3DMMv1.0:  Adjust the translation state variables
            _dxyzSubRte.dxr = BrsAdd(dxr, _dxyzSubRte.dxr);
            _dxyzSubRte.dyr = BrsAdd(dyr, _dxyzSubRte.dyr);
            _dxyzSubRte.dzr = BrsAdd(dzr, _dxyzSubRte.dzr);

            // 3DMMv1.0: Adjust the position in the add event
            _pggaev->Get(_iaevAddCur, &aevadd);
            aevadd.dxr = BrsAdd(aevadd.dxr, dxr);
            aevadd.dyr = BrsAdd(aevadd.dyr, dyr);
            aevadd.dzr = BrsAdd(aevadd.dzr, dzr);
            _pggaev->Put(_iaevAddCur, &aevadd);
        }
    }
    _UpdateXyzRte();

    _pscen->MarkDirty();

    // 3DMMv1.0: Update Brender model
    fMoved = FPure(rZero != dxr || rZero != dyr || rZero != dzr);

    if (fMoved)
        _PositionBody(&_xyzCur);

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Load starting point orientation into state variable _xfrm

***************************************************************************/
void ACTR::_LoadAddOrien(AEVADD *paevadd, bool fNoReset)
{
    AssertThis(0);

    // 3DMMv1.0: Set _xfrm.bmat34Cur always holds the current full rotation
    if (!fNoReset)
        _InitXfrmRot(&_xfrm.bmat34Cur);
    BrMatrix34PostRotateX(&_xfrm.bmat34Cur, paevadd->xa);
    BrMatrix34PostRotateY(&_xfrm.bmat34Cur, paevadd->ya);
    BrMatrix34PostRotateZ(&_xfrm.bmat34Cur, paevadd->za);
    _xfrm.xaPath = paevadd->xa;
    _xfrm.yaPath = paevadd->ya;
    _xfrm.zaPath = paevadd->za;
}

/** 3DMMv1.0: *************************************************************************

    Remove redundant events of a targeted type.	 (Actor uses this for
    Xfrm editing)
    NOTE***: This is not valid for Actn or Cost fill.  Special case routines
    exist for those event types.

    This is called _before_ inserting the new event.

    Note: For transforms, this code will rarely have effect unless the client
    uses discretized increments of observable size, ie, it is statistically
    unlikely that a match of the same size will be found. This code was
    requested in anticipation of discretized increments.

    Input: aet == the event type
        *pvVar == the variable part of the new event being inserted
        cbVar == the size of the variable part
        iaevMin == the first possible event to delete
        iaevCmp == ivNil or the event being replaced
        grfaet == faetNil or types of events to nuke along the way

***************************************************************************/
void ACTR::_PrepXfrmFill(int32_t aet, void *pvVar, int32_t cbVar, int32_t iaevMin, int32_t iaevCmp, uint32_t grfaet)
{
    AssertBaseThis(0);
    Assert(aet == aetSize || aet == aetRotF || aet == aetPull, "Illegal argument aet");
    AssertPvCb(pvVar, cbVar);
    AssertIn(cbVar, 0, 100); // 3DMMv1.0: Approximate bound
    AssertIn(iaevMin, 0, _pggaev->IvMac() + 1);
    AssertIn(iaevCmp, -1, _pggaev->IvMac() + 1);
    Assert(aet != aetActn && aet != aetCost, "Illegal aet argument");

    int32_t iaev;
    AEV aev;
    bool fReplacePrev = fTrue;
    void *pvVarCmp = pvNil;
    int32_t cb;

    _pggaev->Lock();

    if (ivNil != iaevCmp)
    {
        pvVarCmp = _pggaev->QvGet(iaevCmp, &cb);
        Assert(cb == cbVar, "Logic error");
    }
    else
    {
        // 3DMMv1.0: Locate the most current event of this type
        // 3DMMv1.0: and store the ptr in pvVarCmp
        for (iaev = 0; iaev < iaevMin - 1; iaev++)
        {
            _pggaev->GetFixed(iaev, &aev);
            if (aet != aev.aet)
                continue;

            pvVarCmp = _pggaev->QvGet(iaev, &cb);
            Assert(cb == cbVar, "Logic error");
        }
    }

    if (pvNil == pvVarCmp)
        pvVarCmp = pvVar;

    for (iaev = iaevMin; iaev < _pggaev->IvMac(); iaev++)
    {
        bool fDelete = fFalse;
        _pggaev->GetFixed(iaev, &aev);

        // 3DMMv1.0: Stage entrance events are boundaries to edits
        if (aetAdd == aev.aet)
            goto LEnd;

        switch (aev.aet)
        {
        case aetSize:
        case aetPull:
        case aetRotF:
            void *pv1;
            void *pv2;
            if (aev.aet != aet)
                continue;

            if (fReplacePrev)
            {
                pv1 = pvVarCmp;
                pv2 = _pggaev->QvGet(iaev);
                if (fcmpEq != FcmpCompareRgb(pv1, pv2, cbVar))
                {
                    // 3DMMv1.0: Prepare to test for a match with new event
                    fReplacePrev = fFalse;
                    pvVarCmp = pvVar;
                    fDelete = fFalse;
                }
                else
                {
                    fDelete = fTrue;
                    goto LDelete;
                }
            }

            Assert(!fReplacePrev, "Logic Error");
            pv1 = pvVarCmp;
            pv2 = _pggaev->QvGet(iaev);
            if (fcmpEq != FcmpCompareRgb(pv1, pv2, cbVar))
                goto LEnd;

            // 3DMMv1.0: Events equal : delete event at iaev
            fDelete = fTrue;
            break;

        case aetTweak:
            if ((grfaet & faetTweak) && (aev.rtel >= _rtelCur))
                fDelete = fTrue;
            break;
        case aetRotH:
            if ((grfaet & faetRotF) && (aev.rtel == _rtelCur))
                fDelete = fTrue;
            break;
        case aetFreeze:
            if ((grfaet & faetFreeze) && (aev.rtel >= _rtelCur))
                fDelete = fTrue;
            break;
        case aetStep:
            if ((grfaet & faetStep) && (aev.rtel >= _rtelCur))
                fDelete = fTrue;
            ;
            break;

        default:
            break;
        }
    LDelete:
        if (fDelete)
        {
            _RemoveAev(iaev);
            iaev--;
        }
    }

LEnd:
    _pggaev->Unlock();
}

/** 3DMMv1.0: *************************************************************************

    Insert Aev.  Update state variables
    Since _pggaev is getting a copy of a tag, a call to DupTag is required

***************************************************************************/
bool ACTR::_FInsertAev(int32_t iaev, int32_t cbNew, void *pvVar, void *paev, bool fUpdateState)
{
    AssertBaseThis(0);
    AssertIn(iaev, 0, _pggaev->IvMac() + 1);
    AssertIn(cbNew, 0, 100); // 3DMMv1.0: approximate bound
    if (pvNil != pvVar)
        AssertPvCb(pvVar, cbNew);
    AssertPvCb(paev, SIZEOF(AEV));

    PTAG ptag;

    if (!_pggaev->FInsert(iaev, cbNew, pvVar, paev))
        return fFalse;

    // 3DMMv1.0: If not simply allocating space
    if (pvVar != pvNil)
    {
        // 3DMMv1.0: Increment tag count
        _pggaev->Lock();
        if (_FIsIaevTag(_pggaev, iaev, &ptag))
            TAGM::DupTag(ptag);
        _pggaev->Unlock();

        if (fUpdateState)
        {
            if (iaev < _iaevCur)
                _iaevCur++;
            if (iaev <= _iaevActnCur)
                _iaevActnCur++;
            if (iaev <= _iaevAddCur)
                _iaevAddCur++;
            if (iaev < _iaevFrmMin)
                _iaevFrmMin++;
        }
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Remove Aev.  Update state variables

***************************************************************************/
void ACTR::_RemoveAev(int32_t iaev, bool fUpdateState)
{
    AssertBaseThis(0);
    AssertIn(iaev, 0, _pggaev->IvMac());

    bool fUpdateSndFrame;
    PTAG ptag;
    PAEV qaev;

    // 3DMMv1.0: First, close tags
    _pggaev->Lock();
    if (_FIsIaevTag(_pggaev, iaev, &ptag, &qaev))
        TAGM::CloseTag(ptag);

    /* 3DMMv1.0: Don't bother updating the frame sound indicator if we didn't change
        an event in the scene's current frame */
    fUpdateSndFrame = (qaev->aet == aetSnd) && (_pscen != pvNil) && (qaev->nfrm == _pscen->Nfrm());

    _pggaev->Unlock(); // 3DMMv1.0: qaev is invalid past here!
    TrashVar(&qaev);

    _pggaev->Delete(iaev);

    if (fUpdateState)
    {
        if (iaev < _iaevCur)
            _iaevCur--;
        if (iaev < _iaevActnCur)
            _iaevActnCur--;
        if (iaev < _iaevAddCur)
            _iaevAddCur--;
        if (iaev < _iaevFrmMin)
            _iaevFrmMin--;
    }

    if (fUpdateSndFrame)
        _pscen->UpdateSndFrame();
}

/** 3DMMv1.0: *************************************************************************

    Remove specified (eg, tweak, step, freeze) events for the current action
    If faetActn is set in the grfaet, also remove action events
    matching the replaced action followed by action events matching
    the new (current) action _anidCur.

    Note: This routine exists in addition to the more general PrepXfrmFill
    because actn events are	considered to match if the anid's match
    regardless of the value of the celn.

***************************************************************************/
void ACTR::_PrepActnFill(int32_t iaevMin, int32_t anidPrev, int32_t anidNew, uint32_t grfaet)
{
    AssertBaseThis(0);
    AssertIn(iaevMin, 0, _pggaev->IvMac() + 1);

    AEV aev;
    int32_t iaev;
    int32_t anid = anidPrev;

    for (iaev = iaevMin; iaev < _pggaev->IvMac(); iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);

        // 3DMMv1.0: Stage entrance events are boundaries to edits
        if (aetAdd == aev.aet)
            return;

        if (aetActn == aev.aet)
        {
            AEVACTN aevactn;
            _pggaev->Get(iaev, &aevactn);

            if (!(grfaet & faetActn))
            {
                if (aevactn.anid == anidNew)
                    continue;
                else
                    return;
            }

            if (aevactn.anid == anid)
            {
                _RemoveAev(iaev);
                iaev--;
            }
            else if (aevactn.anid == anidNew)
            {
                anid = anidNew;
                _RemoveAev(iaev);
                iaev--;
            }
            else
                return;
        }

        if (aev.rtel < _rtelCur)
            continue;

        if (aev.aet == aetTweak && (grfaet & faetTweak))
        {
            _RemoveAev(iaev);
            iaev--;
        }

        if (aev.aet == aetFreeze && (grfaet & faetFreeze))
        {
            // 3DMMv1.0: Do not remove end-of-subroute freeze events
            if (!_FIsDoneAevSub(iaev, aev.rtel))
            {
                _RemoveAev(iaev);
                iaev--;
            }
        }
    }
}

/** 3DMMv1.0: *************************************************************************

    Remove redundant costume changes
    Note:  	Delete all costume events that match the costume being replaced
            Then delete all subsequent costume events that match the
            costume being inserted

***************************************************************************/
void ACTR::_PrepCostFill(int32_t iaevMin, AEVCOST *paevcost)
{
    AssertBaseThis(0);
    AssertIn(iaevMin, 0, _pggaev->IvMac() + 1);
    AssertVarMem(paevcost);

    int32_t iaev;
    AEV aev;
    AEVCOST aevcost;
    bool fMtrl;
    bool fCmtl;
    MTRL *pmtrlCmp;
    CMTL *pcmtlCmp;
    MTRL *pmtrl = pvNil;
    CMTL *pcmtl = pvNil;
    bool fReplacePrev = fTrue;

    // 3DMMv1.0: Locate the most current costume for this body part
    _pbody->GetPartSetMaterial(paevcost->ibset, &fMtrl, &pmtrlCmp, &pcmtlCmp);
    fCmtl = !fMtrl;

    for (iaev = iaevMin; iaev < _pggaev->IvMac(); iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);

        // 3DMMv1.0: Stage entrance events are boundaries to edits
        if (aetAdd == aev.aet)
            goto LEnd;

        if (aetCost != aev.aet)
            continue;

        _pggaev->Get(iaev, &aevcost);
        if (aevcost.ibset != paevcost->ibset)
            continue;

        if (fReplacePrev)
        {
            // 3DMMv1.0: Delete the event if the costumes are the same
            if ((fCmtl == aevcost.fCmtl) &&
                ((!fCmtl && (pmtrlCmp == (pmtrl = (PMTRL)vptagm->PbacoFetch(&aevcost.tag, MTRL::FReadMtrl)))) ||
                 (fCmtl && (pcmtlCmp == (pcmtl = _ptmpl->PcmtlFetch(aevcost.cmid))))))
            {
                goto LDelete;
            }

            // 3DMMv1.0: This event does not match the one being replaced
            // 3DMMv1.0: Prepare to test for a match with the event being inserted
            fReplacePrev = fFalse;
            if (!paevcost->fCmtl)
            {
                pmtrlCmp = (PMTRL)vptagm->PbacoFetch(&paevcost->tag, MTRL::FReadMtrl);
                pcmtlCmp = pvNil;
            }
            else
            {
                pcmtlCmp = _ptmpl->PcmtlFetch(paevcost->cmid);
                pmtrlCmp = pvNil;
            }
        }

        ReleasePpo(&pcmtl);
        ReleasePpo(&pmtrl);
        Assert(!fReplacePrev, "Logic error");
        if (paevcost->fCmtl != aevcost.fCmtl ||
            (!aevcost.fCmtl && (pmtrlCmp != (pmtrl = (PMTRL)vptagm->PbacoFetch(&aevcost.tag, MTRL::FReadMtrl)))) ||
            (aevcost.fCmtl && (pcmtlCmp != (pcmtl = _ptmpl->PcmtlFetch(aevcost.cmid)))))
        {
            // 3DMMv1.0: If the costumes differ
            goto LEnd;
        }
    LDelete:
        ReleasePpo(&pcmtl);
        ReleasePpo(&pmtrl);
        _RemoveAev(iaev);
        iaev--;
    }
LEnd:
    if (!fReplacePrev)
    {
        ReleasePpo(&pmtrlCmp);
        ReleasePpo(&pcmtlCmp);
    }
    ReleasePpo(&pmtrl);
    ReleasePpo(&pcmtl);
}

/** 3DMMv1.0: *************************************************************************

    AdjustAevForRte for an already inserted point at irptAdjust
    adjust the rtel's of the subsequent affected events

***************************************************************************/
void ACTR::_AdjustAevForRteIns(int32_t irptAdjust, int32_t iaevMin)
{
    AssertBaseThis(0);
    int32_t irptMac = _pglrpt->IvMac();
    AssertIn(irptAdjust, 0, irptMac);
    AssertIn(iaevMin, 0, _pggaev->IvMac() + 1);

    int32_t iaev;
    AEV aev;
    RPT rptBack, rptAdjust;
    BRS dwrBack;

    if (irptAdjust > 0)
    {
        _pglrpt->Get(irptAdjust - 1, &rptBack);
        dwrBack = rptBack.dwr;
    }
    else
        dwrBack = rZero;

    _pglrpt->Get(irptAdjust, &rptAdjust);

    for (iaev = iaevMin; iaev < _pggaev->IvMac(); iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);
        if (aev.rtel.irpt >= irptAdjust) // 3DMMv1.0: offset from later point
        {
            aev.rtel.irpt++;
        }
        else if (aev.rtel.irpt < irptAdjust - 1)
        {
            continue;
        }
        else
        {
            if ((irptAdjust == irptMac - 1) || (rZero == dwrBack))
                continue;

            if (aev.rtel.dwrOffset >= dwrBack)
            {
                aev.rtel.dwrOffset = BrsSub(aev.rtel.dwrOffset, dwrBack);
                aev.rtel.irpt++;
            }
        }
        _pggaev->PutFixed(iaev, &aev);
    }
}
/** 3DMMv1.0: *************************************************************************

    AdjustAevForRte for a to-be deleted point at irptAdjust
    adjust the rtel's of the affected events

***************************************************************************/
void ACTR::_AdjustAevForRteDel(int32_t irptAdjust, int32_t iaevMin)
{
    AssertBaseThis(0);
    int32_t irptMac = _pglrpt->IvMac();
    AssertIn(irptAdjust, 0, irptMac);
    AssertIn(iaevMin, 0, _pggaev->IvMac() + 1);

    int32_t iaev;
    AEV aev;
    RPT rptBack, rptAdjust;
    BRS dwrBack, dwrFwd;

    if (0 == irptAdjust)
        dwrBack = rZero;
    else
    {
        _pglrpt->Get(irptAdjust - 1, &rptBack);
        dwrBack = rptBack.dwr;
    }

    _pglrpt->Get(irptAdjust, &rptAdjust);
    dwrFwd = rptAdjust.dwr;

    for (iaev = iaevMin; iaev < _pggaev->IvMac(); iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);
        if (aev.rtel.irpt > irptAdjust) // 3DMMv1.0: offset from later point
        {
            aev.rtel.irpt--;
        }
        else if (aev.rtel.irpt == irptAdjust)
        {
            if (rptAdjust.dwr == rZero)
            {
                // 3DMMv1.0: Point will be deleted.  Event must be also
                _RemoveAev(iaev);
                iaev--;
                continue;
            }
            aev.rtel.dwrOffset = BrsAdd(dwrBack, aev.rtel.dwrOffset);
            aev.rtel.irpt--;
        }
        else
            continue;

        _pggaev->PutFixed(iaev, &aev);
    }
}

/** 3DMMv1.0: *************************************************************************

    Locate a point on the path dwrStep further along the route from *prtel
    Events which potentially modify this step (Action, Add) begin at iaevCur.
    *pfEndRoute is set only when an attempt is made to move past the last
    node of the subroute.

    NOTE: This routine is designed to be independent of all frame dependent
    state variables.
    Spec: The end of a subroute is <always> reached in a moving action.
***************************************************************************/
void ACTR::_AdvanceRtel(BRS dwrStep, RTEL *prtel, int32_t iaevCur, int32_t nfrmCur, bool *pfEndRoute)
{
    AssertBaseThis(0);
    AssertVarMem(prtel);
    AssertNilOrVarMem(pfEndRoute);

    BRS dwrT;
    bool fEndRoute = fFalse;
    int32_t iaev;
    AEV aev;

    // 3DMMv1.0: If at start of path
    // 3DMMv1.0: Note: _nfrmFirst is independent with respect to the current frame
    if (nfrmCur <= _nfrmFirst)
    {
        prtel->irpt = 0;
        prtel->dwrOffset = rZero;
        prtel->dnfrm = 0;
        goto LDone;
    }

    // 3DMMv1.0: End of route means trying to move beyond the last point.
    if (rZero == dwrStep) // 3DMMv1.0: Not an "end-of-route"
    {
        prtel->dnfrm++;
        goto LDone;
    }

    // 3DMMv1.0: Move to the correct path segment
    dwrT = ((RPT *)_pglrpt->QvGet(prtel->irpt))->dwr;
    dwrT = BrsSub(dwrT, prtel->dwrOffset);
    if (rZero == dwrT)
    {
        Assert(prtel->dwrOffset == rZero, "Expected zero offset");
        prtel->dnfrm++;
        fEndRoute = fTrue;
        goto LDone;
    }

    while (dwrT <= dwrStep && rZero < dwrStep)
    {
        // 3DMMv1.0: Spec: Move the partial step
        // 3DMMv1.0: Not an "end-of-route" - didn't try to move beyond
        if (rZero == dwrT)
        {
            goto LDoneMove;
        }
        prtel->irpt++;
        AssertIn(prtel->irpt, 1, _pglrpt->IvMac());
        prtel->dwrOffset = rZero;
        dwrStep = BrsSub(dwrStep, dwrT);
        dwrT = ((RPT *)_pglrpt->QvGet(prtel->irpt))->dwr;
    }

    dwrT = ((RPT *)_pglrpt->QvGet(prtel->irpt))->dwr;
    prtel->dwrOffset = BrsAdd(prtel->dwrOffset, dwrStep);

LDoneMove:
    prtel->dnfrm = 0;

    if (!_fModeRecord && !_fRejoin)
    {
        // 3DMMv1.0: Spec: Ordinarily, the actor will display at the end of this step.
        // 3DMMv1.0: If Actn or Step events exist, the the actor is to display
        // 3DMMv1.0: at the location of the event.
        for (iaev = iaevCur; iaev < _pggaev->IvMac(); iaev++)
        {
            _pggaev->GetFixed(iaev, &aev);
            if ((aev.rtel <= *prtel) && (aev.aet == aetStep || aev.aet == aetActn))
            {
                *prtel = aev.rtel;
                break;
            }
            if (aev.rtel > *prtel)
                break;
        }
    }

LDone:
    if (pvNil != pfEndRoute)
        *pfEndRoute = fEndRoute;

    return;
}

/** 3DMMv1.0: *************************************************************************

    Convert a route location (rtel) to an xyz point (in *pxyz)

***************************************************************************/
void ACTR::_GetXyzFromRtel(RTEL *prtel, PXYZ pxyz)
{
    AssertBaseThis(0);
    AssertVarMem(prtel);
    AssertVarMem(pxyz);

    RPT rpt;
    RPT rptFirst;
    RPT rptSecond;
    BRS rFract;

    if (rZero == prtel->dwrOffset || (prtel->irpt) + 1 == _pglrpt->IvMac())
    {
        _pglrpt->Get(prtel->irpt, &rpt);
        *pxyz = rpt.xyz;
        return;
    }

    _pglrpt->Get(prtel->irpt, &rptFirst);
    _pglrpt->Get(1 + prtel->irpt, &rptSecond);
    Assert(rptFirst.dwr >= prtel->dwrOffset, "Possible offset bug in _GetXyzFromRtel");

    rFract = BrsDiv(prtel->dwrOffset, rptFirst.dwr);
    _GetXyzOnLine(&rptFirst.xyz, &rptSecond.xyz, rFract, pxyz);
}

/** 3DMMv1.0: *************************************************************************

    Find a point rFract fractional distance between two points.
    Store in *pxyz

***************************************************************************/
void ACTR::_GetXyzOnLine(PXYZ pxyzFirst, PXYZ pxyzSecond, BRS rFract, PXYZ pxyz)
{
    AssertBaseThis(0);
    AssertVarMem(pxyzFirst);
    AssertVarMem(pxyzSecond);
    AssertVarMem(pxyz);

    // 3DMMv1.0: New pt = first + (second - first) * fractoffset;
    pxyz->dxr = BrsSub(pxyzSecond->dxr, pxyzFirst->dxr);
    pxyz->dyr = BrsSub(pxyzSecond->dyr, pxyzFirst->dyr);
    pxyz->dzr = BrsSub(pxyzSecond->dzr, pxyzFirst->dzr);

    pxyz->dxr = BrsMul(rFract, pxyz->dxr);
    pxyz->dyr = BrsMul(rFract, pxyz->dyr);
    pxyz->dzr = BrsMul(rFract, pxyz->dzr);

    pxyz->dxr = BrsAdd(pxyz->dxr, pxyzFirst->dxr);
    pxyz->dyr = BrsAdd(pxyz->dyr, pxyzFirst->dyr);
    pxyz->dzr = BrsAdd(pxyz->dzr, pxyzFirst->dzr);
}

/** 3DMMv1.0: *************************************************************************

    Locate the actor (in Brender terms), first adjusting the location.
    Post-impose a rotation looking forward along the route

***************************************************************************/
void ACTR::_PositionBody(XYZ *pxyz)
{
    AssertBaseThis(0);
    AssertVarMem(pxyz);
    XYZ xyz;
    BMAT34 bmat34; // 3DMMv1.0: Final orientation matrix

    _MatrixRotUpdate(pxyz, &bmat34);

    xyz.dxr = BrsAdd(_dxyzRte.dxr, pxyz->dxr);
    xyz.dyr = BrsAdd(_dxyzRte.dyr, pxyz->dyr);
    xyz.dzr = BrsAdd(_dxyzRte.dzr, pxyz->dzr);
    _pbody->LocateOrient(xyz.dxr, xyz.dyr, xyz.dzr, &bmat34);
}

/** 3DMMv1.0: *************************************************************************

    Update the orientation matrices
    _xfrm.bmat34Cur must be kept current

***************************************************************************/
void ACTR::_MatrixRotUpdate(XYZ *pxyz, BMAT34 *pbmat34)
{
    AssertBaseThis(0);
    AssertVarMem(pxyz);
    AssertVarMem(pbmat34);

    RPT rpt;
    BRA xa, ya, za;
    BMAT34 bmat34TS; // 3DMMv1.0: Scaling matrix
    BMAT34 bmat34TR; // 3DMMv1.0: Rotation matrix
    bool fStretchSize;
    AEV *paev;

    _xyzCur = *pxyz;
    fStretchSize = (rOne != _xfrm.aevpull.rScaleX || rOne != _xfrm.aevpull.rScaleY || rOne != _xfrm.aevpull.rScaleZ ||
                    rOne != _xfrm.rScaleStep);

    // 3DMMv1.0: Set the orientation & rotation to zero-change
    BrMatrix34Identity(&bmat34TS);

    // 3DMMv1.0: Post apply the rest (face the camera) orientation
    _ptmpl->GetRestOrien(&xa, &ya, &za);
    BrMatrix34PostRotateX(&bmat34TS, xa);
    BrMatrix34PostRotateY(&bmat34TS, ya);
    BrMatrix34PostRotateZ(&bmat34TS, za);

    if (fStretchSize)
    {
        // 3DMMv1.0: Apply any current stretching/squashing
        // 3DMMv1.0: This must be applied BEFORE rotation to avoid stretching
        // 3DMMv1.0: the actor along skewed axes
        BrMatrix34PostScale(&bmat34TS, _xfrm.aevpull.rScaleX, _xfrm.aevpull.rScaleY, _xfrm.aevpull.rScaleZ);

        // 3DMMv1.0: Apply any current uniform sizing
        if (_xfrm.rScaleStep != rOne)
        {
            BrMatrix34PostScale(&bmat34TS, _xfrm.rScaleStep, _xfrm.rScaleStep, _xfrm.rScaleStep);
        }
    }

    // 3DMMv1.0: bmat34Cur is the all inclusive orientation matrix
    if (_fUseBmat34Cur)
    {
        // Rotation events created by older builds may already contain
        // fixed-point scale/shear drift.  Repair the pure rotation basis at
        // use time without touching the separate squash/stretch factors.
        _NormalizeActorRotation(&_xfrm.bmat34Cur);

        // 3DMMv1.0: Single frame rotate events or static segments
        BrMatrix34Mul(pbmat34, &bmat34TS, &_xfrm.bmat34Cur); // 3DMMv1.0: A = B * C
    }
    else
    {
        // 3DMMv1.0: Forward-rotate events or non-static segments
        BrMatrix34Copy(&bmat34TR, &_xfrm.bmat34Fwd); // 3DMMv1.0: copy to bmat34TR
        _NormalizeActorRotation(&bmat34TR);

        // 3DMMv1.0: Post apply the path orientation
        AssertIn(_iaevAddCur, 0, _pggaev->IvMac());
        paev = (AEV *)_pggaev->QvFixedGet(_iaevAddCur);
        _pglrpt->Get(paev->rtel.irpt, &rpt);
#ifdef BUG1899
        if (_ptmpl->FIsTdt() || (_rtelCur.irpt == paev->rtel.irpt && _rtelCur.dwrOffset == rZero))
#else  //! 3DMMv1.0: BUG1899
        if (_ptmpl->FIsTdt() || paev->nfrm == _nfrmCur || rpt.dwr == rZero)
#endif //! 3DMMv1.0: BUG1899
        {
            // 3DMMv1.0: Single point	subroute ->
            // 3DMMv1.0: Post apply single point orientation to event rotations
            AEVADD aevadd;
            _pggaev->Get(_iaevAddCur, &aevadd);
            BrMatrix34PostRotateX(&bmat34TR, aevadd.xa);
            BrMatrix34PostRotateY(&bmat34TR, aevadd.ya);
            BrMatrix34PostRotateZ(&bmat34TR, aevadd.za);
            // 3DMMv1.0: Save the path part of the orientation
            _xfrm.xaPath = aevadd.xa;
            _xfrm.yaPath = aevadd.ya;
            _xfrm.zaPath = aevadd.za;
        }
        else
        {
            // 3DMMv1.0: Orient along the route
            _CalcRteOrient(&bmat34TR, &_xfrm.xaPath, &_xfrm.yaPath, &_xfrm.zaPath);
        }

        // 3DMMv1.0: Now combine rotation with stretching
        BrMatrix34Mul(pbmat34, &bmat34TS, &bmat34TR);

        // 3DMMv1.0: Note: The result of the entire rotation (including path) needs to
        // 3DMMv1.0: be saved - otherwise, the actor will jump when the user first tries
        // 3DMMv1.0: to do a tweak-rotate edit.
        BrMatrix34Copy(&_xfrm.bmat34Cur, &bmat34TR); // 3DMMv1.0: Save final all-included rotation matrix
    }
}

/** 3DMMv1.0: *************************************************************************

    Calculate the Post-imposed rotation tangent to the route
    Calculation based on _iaevCur, _anidCur
    NOTE: *pbmat34 is not pre-initialized

***************************************************************************/
void ACTR::_CalcRteOrient(BMAT34 *pbmat34, BRA *pxa, BRA *pya, BRA *pza, uint32_t *pgrfbra)
{
    AssertBaseThis(0);
    AssertNilOrVarMem(pbmat34);
    AssertNilOrVarMem(pxa);
    AssertNilOrVarMem(pya);
    AssertNilOrVarMem(pza);

    int32_t irptPrev;
    int32_t irptAdd;
    XYZ xyz;
    AEV aev;
    RPT rpt;

    if (pvNil != pxa)
        *pxa = aZero;
    if (pvNil != pya)
        *pya = aZero;
    if (pvNil != pza)
        *pza = aZero;

    AssertIn(_iaevAddCur, 0, _pggaev->IvMac());
    _pggaev->GetFixed(_iaevAddCur, &aev);
    irptAdd = aev.rtel.irpt;
    irptPrev = (rZero < _rtelCur.dwrOffset || _rtelCur.irpt == aev.rtel.irpt) ? _rtelCur.irpt : _rtelCur.irpt - 1;

    Assert(rZero == _rtelCur.dwrOffset || irptPrev < _pglrpt->IvMac(), "Incorrect offset in path");

    // 3DMMv1.0: If at end of subpath, or if on a static segment,
    // 3DMMv1.0: retain the orientation the actor last had
    _pglrpt->Get(irptPrev, &rpt);
    if (rZero == rpt.dwr)
        irptPrev--;

    if (irptPrev < irptAdd)
        return; // 3DMMv1.0: Single point path

    //
    // 3DMMv1.0: Compute vector xyz as a weighted average of 3 vectors
    //
    xyz.dxr = rZero;
    xyz.dyr = rZero;
    xyz.dzr = rZero;

    _UpdateXyzTan(&xyz, irptPrev, rFour);

    if (irptPrev > irptAdd)
    {
        _UpdateXyzTan(&xyz, irptPrev - 1, rTwo);
        if (irptPrev - 1 > irptAdd)
        {
            _UpdateXyzTan(&xyz, irptPrev - 2, rOne);
        }
    }

    //
    // 3DMMv1.0: Apply the rotation determined by vector xyz
    //
    _ApplyRotFromVec(&xyz, pbmat34, pxa, pya, pza, pgrfbra);
    return;
}

/** 3DMMv1.0: *************************************************************************

    Update vector *pxyz by adding to it the normalized vector from nodes
    (irpt to irpt + 1) * weighting factor rw.	The result
    accumulates a weighted average approximation to the tangent to the
    route.
    Optimized for movie PLAYing

***************************************************************************/
void ACTR::_UpdateXyzTan(XYZ *pxyz, int32_t irpt, int32_t rw)
{
    AssertBaseThis(0);

    RPT rpt1;
    RPT rpt2;
    BRS dwr, dxr, dyr, dzr;

    Assert(irpt + 1 < _pglrpt->IvMac(), "irpt out of range");

    _pglrpt->Get(irpt, &rpt1);
    _pglrpt->Get(irpt + 1, &rpt2);

    dxr = BrsSub(rpt2.xyz.dxr, rpt1.xyz.dxr);
    dyr = BrsSub(rpt2.xyz.dyr, rpt1.xyz.dyr);
    dzr = BrsSub(rpt2.xyz.dzr, rpt1.xyz.dzr);

    //
    // 3DMMv1.0: Normalize the vector	(norm ~ max + 1/2 min)
    //
    dwr = BrsAbsMax3(dxr, dyr, dzr);
    if (dxr == dwr)
        dwr = BrsAdd(dwr, (BRS)(LwMax(LwAbs((int32_t)dyr), LwAbs((int32_t)dzr)) >> 1));
    else
    {
        if (dyr == dwr)
            dwr = BrsAdd(dwr, (BRS)(LwMax(LwAbs((int32_t)dxr), LwAbs((int32_t)dzr)) >> 1));
        else
            dwr = BrsAdd(dwr, (BRS)(LwMax(LwAbs((int32_t)dxr), LwAbs((int32_t)dyr)) >> 1));
    }
    dxr = BrsDiv(dxr, dwr);
    dyr = BrsDiv(dyr, dwr);
    dzr = BrsDiv(dzr, dwr);

    dxr = LwBound(dxr, -rOne, rOne);
    dyr = LwBound(dyr, -rOne, rOne);
    dzr = LwBound(dzr, -rOne, rOne);

    //
    // 3DMMv1.0: Apply the weight
    //
    if (rw != rOne)
    {
        dxr = BrsMul(rw, dxr);
        dyr = BrsMul(rw, dyr);
        dzr = BrsMul(rw, dzr);
    }

    pxyz->dxr = BrsAdd(pxyz->dxr, dxr);
    pxyz->dyr = BrsAdd(pxyz->dyr, dyr);
    pxyz->dzr = BrsAdd(pxyz->dzr, dzr);
}

/** 3DMMv1.0: *************************************************************************

    Compute the angles of rotation defined by the vector *pxyz
    Apply the rotation to matrix *pbmat34
    Return the angles of rotation in *pxa, *pya, *pza
    Return a mask of the axes rotated

    Note: Banking can be done by approximating the second derivative
    which can be done using additional arguments.

***************************************************************************/
void ACTR::_ApplyRotFromVec(XYZ *pxyz, BMAT34 *pbmat34, BRA *pxa, BRA *pya, BRA *pza, uint32_t *pgrfbra)
{
    AssertBaseThis(0);
    AssertNilOrVarMem(pbmat34);
    AssertVarMem(pxyz);
    AssertNilOrVarMem(pxa);
    AssertNilOrVarMem(pya);
    AssertNilOrVarMem(pza);

    BRS dwr;
    BRS rX, rY, rZ;
    BRFR eX, eY, eZ, eW;
    BRA aRot = aZero;

    if (pvNil != pgrfbra)
        *pgrfbra = 0;

    /// Use cached tmpl _grfactn to determine the axes to rotate around.
    // v149 proved handmade routes now record/play, but old Actor Studio
    // templates were authored without factnRotateY and therefore slid
    // sideways forever. Give only movie-owned AS templates the classic
    // route-facing Y flag at runtime so existing .3ct objects are upgraded
    // without rewriting their ACTN chunk merely to walk in the right direction.
    uint32_t grfactnRoute = _grfactn;
    if (F4DMMActorStudioOwnsTemplate(_pscen, &_tagTmpl))
        grfactnRoute |= factnRotateY;

    dwr = BrsAdd(BrsAbsMax3(pxyz->dxr, pxyz->dyr, pxyz->dzr), rOne);

    // 3DMMv1.0: BrScalarToFraction required by Brender's BR_ATAN2 limitation
    if (grfactnRoute & (factnRotateY | factnRotateZ))
    {
        rX = BrsDiv(pxyz->dxr, dwr);
        eX = BrScalarToFraction(rX);
    }
    if (grfactnRoute & (factnRotateX | factnRotateZ))
    {
        rY = BrsDiv(pxyz->dyr, dwr);
        eY = BrScalarToFraction(rY);
    }
    if (grfactnRoute & (factnRotateX | factnRotateY))
    {
        rZ = BrsDiv(pxyz->dzr, dwr);
        eZ = BrScalarToFraction(rZ);
    }

    if (grfactnRoute & factnRotateY)
    {
        if (pxyz->dxr != rZero || pxyz->dzr != rZero)
        {
            aRot = BR_ATAN2(eX, eZ);
            if (pvNil != pbmat34)
                BrMatrix34PostRotateY(pbmat34, aRot);

            if (pvNil != pgrfbra)
                *pgrfbra |= fbraRotateY;
        }
    }

    if (pvNil != pya)
        *pya = aRot;

    // 3DMMv1.0: Tilt the actor up / down
    if (grfactnRoute & factnRotateX)
    {
        if (pxyz->dyr != rZero)
        {
            eW = BrScalarToFraction(BrsAdd(BrsAbs(rX), BrsAbs(rZ)));
            aRot = BR_ATAN2(-eY, eW);
            if (pvNil != pbmat34)
                BrMatrix34PreRotateX(pbmat34, aRot);

            if (pvNil != pgrfbra)
                *pgrfbra |= fbraRotateX;
        }
    }
    if (pvNil != pxa)
        *pxa = aRot;
    if (pvNil != pza)
        *pxa = aZero;
}

/** 3DMMv1.0: *************************************************************************

    Get the actor's event lifetime
    Update nfrm values for all events
    Return false if the actor has not yet been placed in time
    pnfrmLast may be pvNil

***************************************************************************/
bool ACTR::FGetLifetime(int32_t *pnfrmFirst, int32_t *pnfrmLast)
{
    AssertThis(0);
    AssertNilOrVarMem(pnfrmFirst);
    AssertNilOrVarMem(pnfrmLast);

    if (klwMax == _nfrmFirst)
        return fFalse;

    if (pvNil != pnfrmFirst)
        *pnfrmFirst = _nfrmFirst;

    if (pvNil == pnfrmLast)
        return fTrue;

    if (!_fLifeDirty)
    {
        *pnfrmLast = _nfrmLast;
        Assert(_nfrmLast >= _nfrmFirst, "fLifeDirty incorrect");
        return fTrue;
    }

    if (!_FComputeLifetime(pnfrmLast))
        PushErc(ercSocBadFrameSlider);

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Compute the actor's event lifetime
    Update nfrm values for all events.	Recording or rerecording pushes
    out later subroutes, whereas recording off screen followed by an earlier
    add does not - note therefore that when roll calling invisible actors,
    the end of the existing subroute must first be truncated.

    On a false return, it is possible that the template could not be
    accessed, so the update is incomplete.

    pnfrmLast may be pvNil
    NOTE: Now that by spec, edits do not cross subroute boundaries,
    performance optimization can be achieved by storing the
    index if the first aev edited when _fLifeDirty is set.

    Note: There is some amount of redundant checking here now
    that subroutes are now step=0 terminated.  Zero termination implies that
    fEndSubRoute should be false, as actors should not be attempting to move
    past the end of their paths.  Note that FIsStalled() now flags the end
    of step=0 terminated subroutes; stalls mid path should now be impossible
    as by spec, motion fill now inherits the original movement along the path.

***************************************************************************/
bool ACTR::_FComputeLifetime(int32_t *pnfrmLast)
{
    AssertBaseThis(0);
    AssertNilOrVarMem(pnfrmLast);

    int32_t iaev;
    int32_t iaevAdd;
    BRS dwr;
    AEV aev;
    AEV *paev;
    RTEL rtel;
    bool fFreezeThisCel;
    int32_t anid = 0;
    int32_t celn = 0;
    int32_t nfrmPrevSub = 0;
    BRS rScale = rOne;
    BRS dwrStep = rZero;
    bool fEndSubRoute = fFalse;  // 3DMMv1.0: moving past end of subroute (see Note above)
    bool fDoneSubRoute = fFalse; // 3DMMv1.0: finished processing events on the current subroute

    // 3DMMv1.0: While recording, delay the upcoming lifetime calculation
    if (_fModeRecord)
    {
        if (pnfrmLast != pvNil)
            *pnfrmLast = _nfrmLast;
        _fLifeDirty = fTrue;
        return fTrue;
    }

    // 3DMMv1.0: Locate final subroute
    rtel.irpt = 0;
    rtel.dwrOffset = rZero;
    rtel.dnfrm = -1;
    iaevAdd = -1;
    // 3DMMv1.0: REVIEW *****(SeanSe): Why not start at the end and go backwards?
    for (iaev = 0; iaev < _pggaev->IvMac(); iaev++)
    {
        paev = (AEV *)_pggaev->QvFixedGet(iaev);
        if (aetAdd == paev->aet)
            iaevAdd = iaev;
    }

    if (iaevAdd < 0)
    {
        _nfrmLast = _nfrmFirst;
        if (pvNil != pnfrmLast)
            *pnfrmLast = _nfrmFirst;
        _fLifeDirty = fFalse;
        return fTrue;
    }

    // 3DMMv1.0: Loop through each frame
    int32_t fFrozen = fFalse;
    int32_t iaevNew = 0;
    RTEL rtelOld = rtel;
    rtelOld.dnfrm = rtel.dnfrm - 1;
    for (_nfrmLast = _nfrmFirst; ((rtel.irpt != _pglrpt->IvMac()) || (iaevNew != _pggaev->IvMac())); _nfrmLast++)
    {
        fFreezeThisCel = fFrozen;
        rtelOld = rtel;
        // 3DMMv1.0: Find distance to move
        // 3DMMv1.0: An aetActn event later in this same frame can modify this.
        if (kdwrNil == dwrStep)
        {
            if (!_ptmpl->FGetDwrActnCel(anid, celn, &dwr))
            {
                _fLifeDirty = fTrue;
                return fFalse;
            }
        }
        else
        {
            dwr = dwrStep;
        }
        dwr = BrsMul(dwr, rScale);
        _AdvanceRtel(dwr, &rtel, iaevNew, _nfrmLast, &fEndSubRoute);

        // 3DMMv1.0: Scan all events for this frame
        for (iaev = iaevNew; iaev < _pggaev->IvMac(); iaev++)
        {
            _pggaev->GetFixed(iaev, &aev);

            if ((aev.rtel > rtel) &&
                ((aetAdd != aev.aet) || (_fModeRecord && !fEndSubRoute) || (!fEndSubRoute && !fDoneSubRoute)))
            {
                // 3DMMv1.0: To push out subroutes, do not process add events until finished
                // 3DMMv1.0: with the previous subroute
                goto LEndFrame;
            }

            switch (aev.aet)
            {
            case aetAdd:
                fFreezeThisCel = fTrue;
                anid = celn = 0;
                if (aev.nfrm > _nfrmLast)
                {
                    goto LEndFrame;
                }
                else
                {
                    rtel = aev.rtel;
                    fDoneSubRoute = fEndSubRoute = fFalse;
                    nfrmPrevSub = _nfrmLast;
                }
                break;

            case aetTweak:
            case aetSnd:
            case aetRem:
            case aetCost:
            case aetRotF:
            case aetRotH:
            case aetPull:
            case aetMove:
                break;

            case aetFreeze:
                int32_t ffriz;
                _pggaev->Get(iaev, &ffriz);
                fFrozen = FPure(ffriz);
                break;

            case aetSize: // 3DMMv1.0: Uniform size transformation
                // 3DMMv1.0: Adjust step size : affects actor lifetime
                _pggaev->Get(iaev, &rScale);
                break;

            case aetActn:
                AEVACTN aevactn;

                _pggaev->Get(iaev, &aevactn);
                anid = aevactn.anid;
                celn = aevactn.celn;
                fFrozen = fFalse;
                fFreezeThisCel = fTrue;
                break;

            case aetStep:
                _pggaev->Get(iaev, &dwrStep);
                break;

            default:
                Assert(0, "Unknown event type");
                break;
            }

            if (aev.rtel > rtel)
                goto LEndFrame;

            if (aev.nfrm != _nfrmLast)
            {
                aev.nfrm = _nfrmLast;
                _pggaev->PutFixed(iaev, &aev);
            }
        }

    LEndFrame:

        // 3DMMv1.0: Stepsize and lifetime is a function of celn's
        if (!fFreezeThisCel && !(fEndSubRoute && _FIsDoneAevSub(iaev, rtel)))
        {
            celn++;
        }

        // 3DMMv1.0: Check for end of subroute or stalled actor (eg breathe in place forever)
#ifdef BUG1960
        // 3DMMv1.0: We are not finished updating events in the current subpath until we are either
        // 3DMMv1.0: - beyond the subroute (fEndSubRoute)
        // 3DMMv1.0: - out of events
        // 3DMMv1.0: - stopped with no way to reach more events in the subpath (ie, stalled)
        // 3DMMv1.0:   and finished updating later events at this same path location
        if (fEndSubRoute ||
            (dwrStep == rZero &&
             (iaev == _pggaev->IvMac() ||
              (!(aev.rtel.irpt == rtel.irpt && aev.rtel.dwrOffset == rtel.dwrOffset) && _FIsStalled(iaevNew, &rtel)))))
#else  //! 3DMMv1.0: BUG1960
        if (fEndSubRoute || ((dwrStep == rZero) && (iaev == _pggaev->IvMac() || _FIsStalled(iaevNew, &rtel))))
#endif //! 3DMMv1.0: BUG1960
        {
            if (!fDoneSubRoute)
                nfrmPrevSub = _nfrmLast;
            fDoneSubRoute = fTrue;

            // 3DMMv1.0: Are there more subroutes?
            if (!_FFindNextAevAet(aetAdd, iaev, &iaev))
            {
                // 3DMMv1.0: For non-static motions, fEndSubRoute is set once the actor is
                // 3DMMv1.0: beyond the end of the subroute.  Adjust back one frame.
                if (fEndSubRoute && (dwrStep != rZero) && (_nfrmLast > _nfrmFirst))
                    _nfrmLast--;

                if (pvNil != pnfrmLast)
                    *pnfrmLast = _nfrmLast;
                _fLifeDirty = fFalse;
                return fTrue;
            }

            // 3DMMv1.0: Add events jump in space.  Update rtel
            _pggaev->GetFixed(iaev, &aev);
            rtel = aev.rtel;

            // 3DMMv1.0: Initialization for _AdvanceRtel()
            anid = celn = 0;
            rtel.dnfrm--;
        }

        iaevNew = iaev;
    }

    Assert(0, "Logic error");
    if (pvNil != pnfrmLast)
        *pnfrmLast = _nfrmLast;
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    An actor is said to be stalled if more route exists, the step size is
    ever zero with no subsequent nonzero step size to follow.

    Determine whether an actor is stalled on a subroute
    Input: IaevFirst is	the first event to check on the subroute.
        *prtel is the route point to test for stalling
    Return fFalse if not stalled.
    If stalled, return the last active event.
***************************************************************************/
bool ACTR::_FIsStalled(int32_t iaevFirst, RTEL *prtel, int32_t *piaevLast)
{
    AssertBaseThis(0);
    AssertIn(iaevFirst, 0, _pggaev->IvMac());
    AssertNilOrVarMem(piaevLast);

    AEV aev;
    int32_t iaev;

    for (iaev = iaevFirst; iaev < _pggaev->IvMac(); iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);
        if ((aev.rtel.irpt > prtel->irpt) || (aev.rtel.dwrOffset > prtel->dwrOffset))
        {
            if (piaevLast != pvNil)
            {
                *piaevLast = iaev;
            }
            return fTrue;
        }

        if ((aev.rtel.irpt == prtel->irpt) && (aev.rtel.dwrOffset == prtel->dwrOffset))
        {
            if (aetStep == aev.aet)
            {
                BRS dwrAev;
                _pggaev->Get(iaev, &dwrAev);
                if (rZero != dwrAev)
                    return fFalse;
            }
        }
    }
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Has the mouse been down long enough or moved far enough to qualify this
    as a valid recording session (vs merely motion filling)?

    NOTE:  ** If sufficient time has not elapsed, intended motion fill will
    inadvertently delete the remaining subroute	is record is selected
    instead of rerecord.

    NOTE:  ** This requires that SetTsInsert() have been called on mouse
    down to initialize the timing of the recording session

***************************************************************************/
bool ACTR::FIsRecordValid(BRS dxr, BRS dyr, BRS dzr, uint32_t tsCurrent)
{
    AssertThis(0);

    if (_fModeRecord)
        return fTrue;

    BRS dwrMouse;

    // 3DMMv1.0: Is this truly a motion fill or is there sufficient time or
    // 3DMMv1.0: distance travelled to make this an authentic route record?
    dwrMouse = BR_LENGTH3(dxr, dyr, dzr);

    if ((dwrMouse < kdwrThreshRte) && ((tsCurrent - _tsInsert) < kdtsThreshRte))
        return fFalse;

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Begin a new route
    fReplace = ftrue if recording, fFalse if rerecording
    On failure, actor is restored from pactrRestore

***************************************************************************/
bool ACTR::FBeginRecord(uint32_t tsCurrent, bool fReplace, PACTR pactrRestore)
{
    AssertThis(0);
    Assert(tsCurrent >= 0, "Invalid type");

    RPT rpt;
    AEV aev;
    int32_t iaev;
    int32_t nfrmAdd;
    int32_t nfrmPrev;
    bool fClosestSubrte = fTrue;

    _pglrpt->Get(_rtelCur.irpt, &rpt);
    _fPathInserted = fFalse;

    // 3DMMv1.0: Do not rejoin to a one point offstage stub.
    _fRejoin = !fReplace && !(rZero == rpt.dwr);
    if (_fRejoin)
    {
        int32_t iaev;
        AEV aev;

        int32_t irptNext = _rtelCur.irpt + 1;

        for (iaev = _iaevCur; iaev < _pggaev->IvMac(); iaev++)
        {
            _pggaev->GetFixed(iaev, &aev);
            if (irptNext < aev.rtel.irpt)
                break;
            if (aetRem == aev.aet)
            {
                _fRejoin = fFalse;
                _RemoveAev(iaev);
                iaev--;
                _AdjustAevForRteDel(irptNext, _iaevCur);
                _pglrpt->Delete(irptNext);
            }
        }
    }

    // 3DMMv1.0: Recording must advance from the current point -> potentially
    // 3DMMv1.0: inserting the current point
    if (rZero != _rtelCur.dwrOffset)
    {
        BRS dwrCur = _rtelCur.dwrOffset;
        _GetXyzFromRtel(&_rtelCur, &rpt.xyz);
        _rtelCur.irpt++;
        _rtelCur.dwrOffset = rZero;
        _rtelCur.dnfrm = 0;
        if (!_FInsertGgRpt(_rtelCur.irpt, &rpt, dwrCur))
            goto LFail;
        _GetXyzFromRtel(&_rtelCur, &_xyzCur);
        _AdjustAevForRteIns(_rtelCur.irpt, 0);
    }

    // 3DMMv1.0: Determine the gaps between subroutes
    _dnfrmGap = 0;
    iaev = _iaevCur;
    if (_fLifeDirty)
    {
        // 3DMMv1.0: Validate future nfrm values
        if (!_FComputeLifetime())
        {
            PushErc(ercSocBadFrameSlider);
            goto LFail;
        }
    }

    for (iaev = _iaevCur; iaev < _pggaev->IvMac(); iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);
        if (aev.aet != aetAdd)
            continue;
        nfrmAdd = aev.nfrm;
        if (fClosestSubrte)
        {
            nfrmPrev = _nfrmCur;
            fClosestSubrte = fFalse;
        }
        else
        {
            _pggaev->GetFixed(iaev - 1, &aev);
            nfrmPrev = aev.nfrm;
        }
        _dnfrmGap -= (nfrmAdd - nfrmPrev);
    }

    _rtelInsert = _rtelCur;
    _fModeRecord = fTrue;
    _dxyzRaw.dxr = rZero;
    _dxyzRaw.dyr = rZero;
    _dxyzRaw.dzr = rZero;
    if (F4DMMActorStudioOwnsTemplate(_pscen, &_tagTmpl))
    {
        BRS dwrTemplate = rZero;
        _ptmpl->FGetDwrActnCel(_anidCur, _celnCur, &dwrTemplate);
        MVIE::MultiLog(_pscen->Pmvie(),
            "resume_custom begin arid=%ld tmpl=%ld action=%ld cel=%ld authored_dwr=%.6g step_state=%.6g replace=%d",
            (long)_arid, (long)_tagTmpl.cno, (long)_anidCur, (long)_celnCur,
            (double)BrScalarToFloat(dwrTemplate), (double)BrScalarToFloat(_dwrStep), (int)fReplace);
    }
    return fTrue;

LFail:
    _RestoreFromUndo(pactrRestore);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Add to a new route

    On Input:
    grfmaf flags the Frozen/unfrozen state.
    *pfStepFrm returns whether the frame was advanced
    *pfStepRte returns whether the route was extended.

    Note: During recording, dwr = rZero marks the end of the new subroute
    NOTE: On failure, actor is restored from pactrRestore

***************************************************************************/
bool ACTR::FRecordMove(BRS dxr, BRS dyr, BRS dzr, uint32_t grfmaf, uint32_t tsCurrent, bool *pfStepFrm, bool *pfStepRte,
                       PACTR pactrRestore)
{
    AssertThis(0);
    AssertVarMem(pfStepFrm);
    AssertVarMem(pfStepRte);

    RPT rptCur;
    XYZ xyzMouse;
    XYZ dxyzT;
    BRS dwrCur;
    RPT rptNew;
    BRS rFractMoved;
    BRS dwrMouse;

    Assert(_fOnStage, "Recording an actor that wasn't selectable??");
    if (!_fModeRecord)
        return fTrue; // 3DMMv1.0: Potential client call on motion fill

    if (!_pggaev->FEnsureSpace(2, kcbVarStep + kcbVarFreeze, fgrpNil) || !_pglrpt->FEnsureSpace(1, fgrpNil))
    {
        goto LFail;
    }

    // 3DMMv1.0: Smooth the unpruned raw mouse movement *before* we create the path
    dxyzT.dxr = dxr;
    dxyzT.dyr = dyr;
    dxyzT.dzr = dzr;
    dxr = BR_CONST_DIV(BrsAdd(_dxyzRaw.dxr, BR_CONST_MUL(2, dxr)), 3);
    dyr = BR_CONST_DIV(BrsAdd(_dxyzRaw.dyr, BR_CONST_MUL(2, dyr)), 3);
    dzr = BR_CONST_DIV(BrsAdd(_dxyzRaw.dzr, BR_CONST_MUL(2, dzr)), 3);

    if (_fFrozen && !(grfmaf & fmafFreeze))
        AssertDo(_FUnfreeze(), "Ensurespace should have ensured space");
    if (!_fFrozen && (grfmaf & fmafFreeze))
        AssertDo(_FFreeze(), "Ensurespace should have ensured space");

    if (pvNil != pfStepFrm)
        *pfStepFrm = fFalse;
    if (pvNil != pfStepRte)
        *pfStepRte = fFalse;

    // 3DMMv1.0: Update distance from previously current point
    Assert(_pglrpt->IvMac() > 0, "Illegal empty path");

    // 3DMMv1.0: In record mode, know prev point on a route Node
    // 3DMMv1.0: Determine if threshhold distance has been advanced
    _pglrpt->Get(_rtelCur.irpt, &rptCur);
    xyzMouse.dxr = BrsAdd(dxr, rptCur.xyz.dxr);
    xyzMouse.dyr = BrsAdd(dyr, rptCur.xyz.dyr);
    xyzMouse.dzr = BrsAdd(dzr, rptCur.xyz.dzr);
    dwrMouse = BR_LENGTH3(dxr, dyr, dzr);

    if (!_FGetDwrRecord(&dwrCur))
        goto LFail;

    if (dwrMouse < dwrCur)
    {
        if ((tsCurrent - _tsInsert) < kdtsThreshRte)
        {
            // 3DMMv1.0: Insufficient length and insufficient time to record a point
            return fTrue;
        }

        if (rZero != _dwrStep)
        {
            AssertDo(FSetStep(rZero), "EnsureSpace insufficient");
        }

        _fPathInserted = fTrue;
        if (pvNil != pfStepFrm)
        {
            *pfStepFrm = fTrue;
        }
        if (!_pscen->FGotoFrm(_pscen->Nfrm() + 1))
            goto LFail;
        return fTrue;
    }

    _tsInsert = tsCurrent;
    _dxyzRaw = dxyzT;

    //
    // 3DMMv1.0: Insert new point.  (Overlays last point if offstage)
    // 3DMMv1.0: Length is sufficient.  Truncate to correct step size
    //
    rFractMoved = BrsDiv(dwrCur, dwrMouse);
    Assert(rZero != dwrCur, "Zero length step illegal in FRecordMove");

    _GetXyzOnLine(&rptCur.xyz, &xyzMouse, rFractMoved, &rptNew.xyz);
    rptNew.dwr = rZero;

    // 3DMMv1.0: Adjust the point for ground zero.  The rule is if the mouse
    // 3DMMv1.0: is decreasing from above to below ground, stop at ground level
    // 3DMMv1.0: The original dwr is used *by design*
    if ((grfmaf & fmafGround) && (BrsAdd(rptCur.xyz.dyr, _dxyzRte.dyr) >= rZero) &&
        (BrsAdd(rptNew.xyz.dyr, _dxyzRte.dyr) < rZero))
    {
        rptNew.xyz.dyr = -_dxyzRte.dyr;
    }

    if (!_pglrpt->FInsert(1 + _rtelCur.irpt, &rptNew))
        goto LFail;

    if (kdwrNil != _dwrStep)
    {
        AssertDo(FSetStep(kdwrNil), "EnsureSpace insufficient");
    }

    // 3DMMv1.0: Reset distance in previous xyz entry
    rptCur.dwr = dwrCur;
    Assert(dwrCur != rZero, "Bug in InsertRoute");
    _pglrpt->Put(_rtelCur.irpt, &rptCur);

    _AdjustAevForRteIns(1 + _rtelCur.irpt, _iaevCur);
    if (!_fPathInserted && F4DMMActorStudioOwnsTemplate(_pscen, &_tagTmpl))
    {
        MVIE::MultiLog(_pscen->Pmvie(),
            "resume_custom first_route_point arid=%ld tmpl=%ld action=%ld cel=%ld dwr=%.6g delta=(%.6g,%.6g,%.6g)",
            (long)_arid, (long)_tagTmpl.cno, (long)_anidCur, (long)_celnCur,
            (double)BrScalarToFloat(dwrCur),
            (double)BrScalarToFloat(dxr), (double)BrScalarToFloat(dyr), (double)BrScalarToFloat(dzr));
    }
    _fPathInserted = fTrue;

    if (pvNil != pfStepFrm)
        *pfStepFrm = fTrue;
    if (pvNil != pfStepRte)
        *pfStepRte = fTrue;

    _dnfrmGap++;
    if (_dnfrmGap > 0)
        _nfrmLast++;

    if (_pscen->FGotoFrm(_pscen->Nfrm() + 1))
        return fTrue;

LFail:
    _RestoreFromUndo(pactrRestore);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    End recording a new route or route section	(mouse up)
    Insert a freeze event if at the end of the subroute.

    Input: fReplace is meaningful only if recording or rerecording (versus
    motion fill.  _fModeRecord is false on motion fill).
    fReplace = fTrue if recording, ie, if replacing the remaining subroute

    Path splicing rule:  Never rejoin across Add events
    Always reconnect to the existing subroute at the closest point which
    occurs later in the route than where the initial re-recording began.

    NOTE** Actn, Tweak, Freeze, Step & Rem events are deleted from the
    section of the route being replaced.  Other events are pushed up to
    the join point (if rejoining) or to the next dnfrm of the current point,
    if not rejoining.

    On Failure: The actor is restored from pactrRestore
***************************************************************************/
// 3DMMv1.0: REVIEW *****(*****): Ver 2.0 As motion fill is self sufficient,
// 3DMMv1.0: 		FEndRecord() should be able to exit immediately on !_fModeRecord.
// 3DMMv1.0:   	Also, pactrRestore should be state var _pactrRecordRestore.
bool ACTR::FEndRecord(bool fReplace, PACTR pactrRestore)
{
    AssertThis(0);

    AEV aev;
    BRS dwr;
    int32_t iaev;
    int32_t irpt;
    RPT rpt;
    RPT rptJoin;
    RPT rptCur;
    BRS dwrMin = kdwrMax;
    RTEL rtelJoin = _rtelCur;
    bool fJoin = fFalse;
    int32_t irptLim = _pglrpt->IvMac();
    int32_t iaevJoinFirst = _iaevCur;
    int32_t iaevNew;

    // 3DMMv1.0: Determine whether to rejoin to the path
    // 3DMMv1.0: REVIEW (*****): Can we assert _fOnStage?
    if (_fModeRecord && (!_fOnStage || fReplace))
    {
        // 3DMMv1.0: Delete remnant path
        _DeleteFwdCore(fFalse, pvNil, _iaevCur);
        if (!_FFreeze())
            goto LFail;
        if (!FSetStep(rZero))
            goto LFail;
        goto LEndRecord;
    }

    //
    // 3DMMv1.0: Motion Fill or Path-rejoining
    //
    // 3DMMv1.0: _fModeRecord == fFalse on motion fill
    if (!_fModeRecord || !_fPathInserted || !_fOnStage)
    {
        // 3DMMv1.0: Set Action necessarily destroys end of path freeze events
        // 3DMMv1.0: Reinsert an end of path freeze event	if at end of subroute
        if (_FIsDoneAevSub(_iaevCur, _rtelCur))
        {
            if (!_FFreeze())
                goto LFail;
        }
        goto LEndRecord; // 3DMMv1.0: Nothing inserted
    }

    // 3DMMv1.0: On !fReplace, force continuation to remainder of route.
    if (rZero == _dwrStep)
    {
        if (!FSetStep(kdwrNil))
            goto LFail;
    }

    if (!_fRejoin)
    {
        // 3DMMv1.0: Gather the events at later time for the current path point
        // 3DMMv1.0: Equivalent to setting the join time ahead
        rtelJoin.dnfrm = 1;
    }
    else
    {
        Assert((_rtelCur.irpt < _pglrpt->IvMac() - 1), "Not enough points to rejoin");

        for (iaev = _iaevCur; iaev < _pggaev->IvMac(); iaev++)
        {
            _pggaev->GetFixed(iaev, &aev);
            if (aev.aet == aetAdd)
            {
                irptLim = aev.rtel.irpt;
                break;
            }
        }

        // 3DMMv1.0: Locate the Join node
        rtelJoin.irpt = _rtelCur.irpt + 1;
        rtelJoin.dwrOffset = rZero;
        rtelJoin.dnfrm = 0;
        for (irpt = 1 + _rtelCur.irpt; irpt < irptLim; irpt++)
        {
            _pglrpt->Get(irpt, &rpt);
            dwr = BR_LENGTH3(BrsSub(_xyzCur.dxr, rpt.xyz.dxr), BrsSub(_xyzCur.dyr, rpt.xyz.dyr),
                             BrsSub(_xyzCur.dzr, rpt.xyz.dzr));
            if (dwr < dwrMin)
            {
                dwrMin = dwr;
                rtelJoin.irpt = irpt;
            }

            // 3DMMv1.0: Do not join across subroutes
            if (rZero == rpt.dwr)
                break;
        }

        Assert(rtelJoin > _rtelCur, "Invalid Join point");
        Assert(rZero == _rtelCur.dwrOffset, "_rtelCur invalid");

        // 3DMMv1.0: Update the dwr of the most recently recorded point to be
        // 3DMMv1.0: the length to the join point on the path
        // 3DMMv1.0: NOTE: This cannot be zero (it would signal end of path)
        _pglrpt->Get(_rtelCur.irpt, &rptCur);
        _pglrpt->Get(rtelJoin.irpt, &rptJoin);
        rptCur.dwr = BR_LENGTH3(BrsSub(_xyzCur.dxr, rptJoin.xyz.dxr), BrsSub(_xyzCur.dyr, rptJoin.xyz.dyr),
                                BrsSub(_xyzCur.dzr, rptJoin.xyz.dzr));

        if (rZero == rptCur.dwr)
        {
            // 3DMMv1.0: Prevent pathological end-of-route case
            rptCur.dwr = rEps;
        }
        _pglrpt->Put(_rtelCur.irpt, &rptCur);
    }

    //
    // 3DMMv1.0: Note: Motion fill exited earlier.  This is rejoin-recording only (which
    // 3DMMv1.0: may or may not actually have a path point to rejoin to).
    // 3DMMv1.0: Move displaced events forward to the join point
    // 3DMMv1.0: Spec: Replace is viewed in UI as Action replace, not just path replace.	->
    // 3DMMv1.0: Delete intervening action, tweak, freeze, step and rem events.
    //
    for (iaev = _iaevCur; iaev < _pggaev->IvMac(); iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);

#ifdef BUG1961
        // 3DMMv1.0: If we were rejoin-recording from a static segment, there is
        // 3DMMv1.0: no point to rejoin to (-> _fRejoin is fFalse).  So gather
        // 3DMMv1.0: events up to but not beyond the next add event.
        if (aev.aet == aetAdd || (_fRejoin && aev.rtel >= rtelJoin))
#else
        if (_fRejoin && aev.rtel >= rtelJoin)
#endif //! 3DMMv1.0: BUG1961
        {
            break;
        }

        if (aev.rtel > _rtelInsert)
        {
            if (aev.aet == aetTweak || aev.aet == aetStep || aev.aet == aetRem || aev.aet == aetActn ||
                aev.aet == aetFreeze)
            {
                _RemoveAev(iaev);
                iaev--;
                continue;
            }
            else
            {
                aev.rtel = rtelJoin;
                _pggaev->PutFixed(iaev, &aev);
                _MergeAev(iaevJoinFirst, iaev, &iaevNew);
                if (iaevNew < iaev)
                    iaev--;
            }
        }
    }

    if (_fRejoin)
    {
        // 3DMMv1.0: Delete the path segment before the join point
        for (irpt = rtelJoin.irpt - 1; irpt > _rtelCur.irpt; irpt--)
        {
            _AdjustAevForRteDel(irpt, _iaevCur);
            _pglrpt->Delete(irpt);
        }

        // 3DMMv1.0: Spec: Do an action fill forward.
        _PrepActnFill(iaevJoinFirst, _anidCur, _anidCur, faetTweak | faetFreeze | faetActn);
    }

#ifdef BUG1961
    if (!fReplace && !_fRejoin)
    {
        if (!_FFreeze())
            goto LFail;
        if (!FSetStep(rZero))
            goto LFail;
    }
#endif // 3DMMv1.0: BUG1961

LEndRecord:
    _fLifeDirty = fTrue;
#ifdef BUG1973
    _fRejoin = fFalse;
#endif // 3DMMv1.0: BUG1973
    _fModeRecord = fFalse;
    _pscen->MarkDirty();
    _pscen->InvalFrmRange();
    AssertThis(fobjAssertFull);
    return fTrue;

LFail:
    _RestoreFromUndo(pactrRestore);
#ifdef BUG1973
    _fRejoin = fFalse;
#endif
    _fModeRecord = fFalse; // 3DMMv1.0: Redundant safety net
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Delete the path and events beyond this frame - and -
    Insert a Freeze event to terminate the truncated event list

***************************************************************************/
void ACTR::DeleteFwdCore(bool fDeleteAll, bool *pfAlive, int32_t iaevCur)
{
    AssertThis(0);

    bool fAlive;
    int32_t iaev;
    AEV aev;

    _DeleteFwdCore(fDeleteAll, &fAlive, iaevCur);

    if (fAlive && _nfrmCur >= _nfrmFirst)
    {
        // 3DMMv1.0: If no space exists, truncation isn't changing a thing
        if (_pggaev->FEnsureSpace(2, kcbVarFreeze + kcbVarStep, fgrpNil))
        {
#ifdef BUG1932
            if (_iaevAddCur < 0 || _pggaev->IvMac() == 0)
            {
                // 3DMMv1.0: Safety net code : don't add events if bug exists
                Bug("Invalid fAlive value");
                goto LEnd;
            }
#ifdef DEBUG
            _pggaev->GetFixed(_iaevAddCur, &aev);
            Assert(aetAdd == aev.aet, "Should be an add event at _iaevAddCur");
#endif // 3DMMv1.0: DEBUG
            if (_fOnStage)
            {
                // 3DMMv1.0: Insert terminating events ONLY if the actor is shown, not hidden
                AssertDo(_FFreeze(), "Expected freeze event to succeed");
                AssertDo(FSetStep(rZero), "Expected set step event to succeed");
            }
            else
                goto LEnd;
#else  // 3DMMv1.0: BUG1932
            AssertDo(_FFreeze(), "Expected freeze event to succeed");
            AssertDo(FSetStep(rZero), "Expected set step event to succeed");
#endif //! 3DMMv1.0: BUG1932
       // 3DMMv1.0: By construct, step events SET the location of display
       // 3DMMv1.0: Tweak events override the last step event.
            for (iaev = _iaevCur - 1; iaev > 0; iaev--)
            {
                _pggaev->GetFixed(iaev, &aev);
                if (aev.rtel != _rtelCur)
                    break;
                if (aev.aet == aetTweak)
                {
                    _pggaev->Swap(iaev, _iaevCur - 1);
                    break;
                }
            }
        }
    }

LEnd:
    if (pvNil != pfAlive)
        *pfAlive = fAlive;
}

/** 3DMMv1.0: *************************************************************************

    Delete the path and events beyond this frame
    **NOTE:  This does not send the actor offstage.	See FRemFromStageCore.
    NOTE: While _fLifeDirty flags needed lifetime recomputation, scene's
    InvalFrmRange() is _not_ called due to the special artifact of the
    frame slider being spec'd to show extensions but not reductions.

    On Input:
        fDeleteAll specifies full route vs subroute deletion
        If one frame was backed up before the delete, iaevCur is the
        current event before the backup (else ivNil).
    Returns *pfAlive = false if all events and route for this actor
    have been deleted

***************************************************************************/
void ACTR::_DeleteFwdCore(bool fDeleteAll, bool *pfAlive, int32_t iaevCur)
{
    AssertThis(0);

    int32_t iaev, iaevDelLim;
    int32_t irpt, irptDelLim;
    int32_t irptDelFirst;
    RPT rpt;
    AEV *paev;

    if (ivNil == iaevCur)
        iaevCur = _iaevCur;

#ifndef BUG1870
    // 3DMMv1.0: Delete this section of code: Placement orientation should no
    // 3DMMv1.0: 		longer be overwritten.
    // 3DMMv1.0: Preserve the current orientation
    if (_iaevAddCur >= 0)
    {
        // 3DMMv1.0: If we are reducing the current subroute to a single point,
        // 3DMMv1.0: it is necessary to store the current orientation for wysiwyg
        paev = (AEV *)_pggaev->QvFixedGet(_iaevAddCur);
        if (paev->nfrm == _nfrmCur)
            _SaveCurPathOrien();
    }
#endif //! 3DMMv1.0: BUG1870

    // 3DMMv1.0: If !fDeleteAll & only one subroute left, redefine fDeleteAll
    iaevDelLim = _pggaev->IvMac();
    irptDelLim = _pglrpt->IvMac();
    if (!fDeleteAll)
    {
        fDeleteAll = fTrue;
        for (iaev = iaevCur; iaev < _pggaev->IvMac(); iaev++)
        {
            paev = (AEV *)_pggaev->QvFixedGet(iaev);
            if (aetAdd == paev->aet)
            {
                fDeleteAll = fFalse;
                irptDelLim = paev->rtel.irpt;
                iaevDelLim = iaev;
                break;
            }
        }
    }

    // 3DMMv1.0: Remove events beyond the current one
    // 3DMMv1.0: excluding the last freeze event
    if (0 < iaevDelLim)
    {
        for (iaev = iaevDelLim - 1; iaev >= _iaevCur; iaev--)
        {
            _RemoveAev(iaev);
            _fLifeDirty = fTrue;
        }
    }

    // 3DMMv1.0: Prune the corresponding route
    irptDelFirst = _rtelCur.irpt + 1;
    if (0 < irptDelLim)
    {
        // 3DMMv1.0: Note: Last remaining point on path is dependent on the event stream
        if (_iaevCur <= 0 && fDeleteAll)
        {
            _pglrpt->FSetIvMac(0);
            _fLifeDirty = fTrue;
            _rtelCur.irpt = -1;
            goto LDone;
        }
        else
        {
            // 3DMMv1.0: Delete the current path node only if no other events
            // 3DMMv1.0: use the same node
            if (_rtelCur.dwrOffset > rZero)
            {
                // 3DMMv1.0: Shorten the distance between the last two nodes
                _TruncateSubRte(irptDelLim);
                irptDelFirst++;
            }
            else
            {
                // 3DMMv1.0: Delete remaining subroute
                for (irpt = irptDelLim - 1; irpt >= irptDelFirst; irpt--)
                {
                    _AdjustAevForRteDel(irpt, 0);
                    _pglrpt->Delete(irpt);
                    _fLifeDirty = fTrue;
                }
            }
        }

        // 3DMMv1.0: Adjust the distance on the tail point
        if (_rtelCur.irpt >= 0)
        {
            Assert(_pglrpt->IvMac() > 0, "Logic Error");
            _pglrpt->Get(_rtelCur.irpt, &rpt);
            rpt.dwr = rZero;
            _pglrpt->Put(_rtelCur.irpt, &rpt);
        }
    }

LDone:
    _pscen->MarkDirty();
    if (_nfrmCur < _nfrmFirst)
    {
        if (fDeleteAll)
        {
            if (pvNil != pfAlive)
                *pfAlive = fFalse;
            _InitState();
            return;
        }
        else if (_pggaev->IvMac() > 0)
        {
            paev = (AEV *)_pggaev->QvFixedGet(0);
            _nfrmFirst = paev->nfrm;
            Assert(aetAdd == paev->aet, "Bug in ACTR::DeleteFwdCore");
        }
    }

    if (pvNil != pfAlive)
        *pfAlive = fTrue;

    return;
}

/** 3DMMv1.0: *************************************************************************

    Save the path specific part of the current orientation

***************************************************************************/
void ACTR::_SaveCurPathOrien(void)
{
    AssertBaseThis(0);

    RPT rpt;
    AEV aev;
    uint32_t grfbra = 0;

    if (_iaevAddCur >= 0 && !_ptmpl->FIsTdt())
    {
        _pggaev->GetFixed(_iaevAddCur, &aev);
        _pglrpt->Get(aev.rtel.irpt, &rpt);
        if (rZero != rpt.dwr) // 3DMMv1.0: ie, non-static path segment
        {
            // 3DMMv1.0: _xfrm.waPath is current and CalcRteOrient() cannot be called
            // 3DMMv1.0: in all cases
            SetAddOrient(_xfrm.xaPath, _xfrm.yaPath, _xfrm.zaPath, grfbra);
        }
    }
}

/** 3DMMv1.0: *************************************************************************

    Delete the path and events prior to this frame

***************************************************************************/
void ACTR::DeleteBackCore(bool *pfAlive)
{
    AssertThis(0);
    AssertNilOrVarMem(pfAlive);

    int32_t iaev;
    int32_t iaevNew;
    AEV aev;
    BRS dwrOffsetT;
    BRS dwrNew;
    BRS dwrOld;
    RPT rptOld;
    int32_t dnrpt;

    // 3DMMv1.0: Nop if not yet at first frame
    if (_nfrmCur <= _nfrmFirst)
    {
        if (pvNil != pfAlive)
            *pfAlive = FPure(_pggaev->IvMac() > 0);
        return;
    }

    // 3DMMv1.0: We need to update the nfrm field in aetAdd events in order to update
    // 3DMMv1.0: _nfrmFirst below.
    if (_fLifeDirty && !_FComputeLifetime())
    {
        // 3DMMv1.0: Below the new _nfrmFirst will be wrong, but that means the actor
        // 3DMMv1.0: will appear in the wrong frame (iff _fLifeDirty and the next subroute
        // 3DMMv1.0: got moved).  Not much we can do about it.
        PushErc(ercSocBadFrameSlider);
    }

    // 3DMMv1.0: If the current point is between nodes, a node will
    // 3DMMv1.0: be inserted as the new first node of the subroute.
    // 3DMMv1.0: Compute the dwr of the new node.
    _pglrpt->Get(_rtelCur.irpt, &rptOld);
    dwrOld = rptOld.dwr;
    dwrNew = BrsSub(dwrOld, _rtelCur.dwrOffset);

    // 3DMMv1.0: Preserve the current orientation
    _SaveCurPathOrien();

    // 3DMMv1.0: Adjust offsets for events occurring at points between the
    // 3DMMv1.0: previous and next nodes
    for (iaev = _iaevCur; iaev < _pggaev->IvMac(); iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);
        if (aev.rtel.irpt > _rtelCur.irpt)
            break;

        if (aev.rtel.dwrOffset == _rtelCur.dwrOffset)
        {
            aev.rtel.dwrOffset = rZero;
            if (aev.rtel.dnfrm < _rtelCur.dnfrm)
            {
                Bug("Event list out of order (dnfrm error)");
                aev.rtel.dnfrm = 0;
            }
            else
            {
                aev.rtel.dnfrm -= _rtelCur.dnfrm;
            }
        }

        if (dwrNew == rZero)
            aev.rtel.dwrOffset = rZero;
        else
        {
            // 3DMMv1.0: Set the event's dwrOffset to the correct
            // 3DMMv1.0: part of the new dwr
            dwrOffsetT = BrsSub(aev.rtel.dwrOffset, _rtelCur.dwrOffset);
            aev.rtel.dwrOffset = dwrOffsetT;
        }
        _pggaev->PutFixed(iaev, &aev);
    }

    //
    // 3DMMv1.0: Alter the route's new first point to be _rtelCur.
    // 3DMMv1.0: with the correct new dwr.  Then update _rtelCur.
    //
    _GetXyzFromRtel(&_rtelCur, &rptOld.xyz);
    rptOld.dwr = dwrNew;
    _pglrpt->Put(_rtelCur.irpt, &rptOld);
    dnrpt = _rtelCur.irpt;
    _rtelCur.dnfrm = 0;
    _rtelCur.dwrOffset = rZero;
    _rtelCur.irpt = 0;

    //
    // 3DMMv1.0: Merge the earlier events
    // 3DMMv1.0: Update the necessary state variables
    //
    for (iaev = 0; iaev < _iaevCur; iaev++)
    {
        _pggaev->GetFixed(iaev, &aev);
        if (!_fOnStage || iaev < _iaevAddCur)
        {
            _RemoveAev(iaev);
            iaev--;
            continue;
        }

        switch (aev.aet)
        {
        case aetRotH:
#ifdef BUG1870
            // 3DMMv1.0: Supercede orient-rotate with current rotation
            // 3DMMv1.0: 		so that static segment orientation will be preserved
            // 3DMMv1.0: Note: events prior to _iaevFrmMin need to be included
            // 3DMMv1.0: 		due to orient-rotations lasting the lifetime of static
            // 3DMMv1.0: 		segments.
            _pggaev->Put(iaev, &_xfrm.bmat34Cur);
            goto LDefault;
#endif // 3DMMv1.0: BUG1870
        case aetTweak:
        case aetSnd:
            if (iaev < _iaevFrmMin)
            {
                _RemoveAev(iaev);
                iaev--;
                break;
            }
            if (iaev > 0)
                goto LDefault;
            break;

        case aetActn:
            AEVACTN aevactn;
            _pggaev->Get(iaev, &aevactn);
            aevactn.celn = _celnCur;
            _pggaev->Put(iaev, &aevactn);
            goto LDefault;

        case aetRem:
            _RemoveAev(iaev);
            iaev--;
            Bug("Offstage case should be already handled");
            break;

        default:
        LDefault:
            aev.rtel = _rtelCur;
            aev.nfrm = _nfrmCur;
            _pggaev->PutFixed(iaev, &aev);
            if (iaev > 0)
                _MergeAev(0, iaev, &iaevNew);
            else
                iaevNew = iaev;

            if (iaevNew < iaev)
                iaev--;
            if (aev.aet == aetAdd)
                _iaevAddCur = iaevNew;
            if (aev.aet == aetActn)
                _iaevActnCur = iaevNew;
            break;
        }
    }

    //
    // 3DMMv1.0: Adjust the irpt's of the later events
    // 3DMMv1.0: Adjust the absolute beginning frame number
    // 3DMMv1.0: Delete the first section of the route
    // 3DMMv1.0: NOTE: There is no translation in time
    //
    if (dnrpt > 0)
    {
        // 3DMMv1.0: Delete the first section of the route
        // 3DMMv1.0: If offstage, also delete the current point
#ifdef BUG1866
        if (!_fOnStage)
            dnrpt++;
#endif // 3DMMv1.0: BUG1866
        for (iaev = _iaevCur; iaev < _pggaev->IvMac(); iaev++)
        {
            _pggaev->GetFixed(iaev, &aev);
            aev.rtel.irpt -= dnrpt;
            _pggaev->PutFixed(iaev, &aev);
        }
#ifndef BUG1866
        if (!_fOnStage)
            dnrpt++;
#endif //! 3DMMv1.0: BUG1866
        _pglrpt->Delete(0, dnrpt);
    }

    if (pvNil != pfAlive)
        *pfAlive = FPure(_pggaev->IvMac() > 0);

    if (_pggaev->IvMac() == 0)
    {
        _InitState();
    }
    else
    {
        // 3DMMv1.0: Adjust remaining state variables
        _pggaev->GetFixed(0, &aev);
        Assert(aev.aet == aetAdd, "An aetAdd event should be the first event");
        _nfrmFirst = aev.nfrm;
        _fLifeDirty = fTrue;
        _iaevFrmMin = 0;
    }

    if (!_FComputeLifetime())
        PushErc(ercSocBadFrameSlider);
    _pscen->InvalFrmRange();

    return;
}

/** 3DMMv1.0: *************************************************************************

    Truncate the last linear section of the subroute.
    Make the current point a node.

    Adjust event entries from the current rtel through events located
    at irptDelLim.
    Note:  This truncates the route only, not the event list.

***************************************************************************/
void ACTR::_TruncateSubRte(int32_t irptDelLim)
{
    AssertBaseThis(0);

    AEV aev;
    int32_t iaev;
    RPT rptNode1;
    RPT rptNode2;
    int32_t iaevLim = _iaevCur;
    int32_t irpt = _rtelCur.irpt;

    if (_rtelCur.dwrOffset == rZero)
        return;

    // 3DMMv1.0: Store the new length between nodes
    _pglrpt->Get(irpt, &rptNode1);
    rptNode1.dwr = _rtelCur.dwrOffset;
    _pglrpt->Put(irpt, &rptNode1);

    // 3DMMv1.0: Move node 2 rather than inserting a node
    _GetXyzFromRtel(&_rtelCur, &rptNode2.xyz);
    rptNode2.dwr = rZero;
    Assert(irpt + 1 < _pglrpt->IvMac(), "Error in truncation");
    _pglrpt->Put(irpt + 1, &rptNode2);

    // 3DMMv1.0: Update _rtelCur
    _rtelCur.dnfrm = 0;
    _rtelCur.dwrOffset = rZero;
    _rtelCur.irpt++;

    // 3DMMv1.0: Update aev's rtel's
    for (iaev = _iaevCur - 1; iaev >= 0; iaev--)
    {
        _pggaev->GetFixed(iaev, &aev);
        if (aev.rtel.irpt < irpt)
            break;
        if (aev.rtel.dwrOffset >= rptNode1.dwr)
        {
            aev.rtel.dwrOffset = rZero;
            aev.rtel.irpt++;
            _pggaev->PutFixed(iaev, &aev);
        }
    }

    // 3DMMv1.0: Delete remaining subroute
    for (irpt = irptDelLim - 1; irpt > _rtelCur.irpt; irpt--)
    {
        _AdjustAevForRteDel(irpt, 0);
        _pglrpt->Delete(irpt);
        _fLifeDirty = fTrue;
    }
}

/** 3DMMv1.0: *************************************************************************

    Is the mouse point within this actor.

***************************************************************************/
bool ACTR::FPtIn(int32_t xp, int32_t yp, int32_t *pibset)
{
    AssertThis(0);
    AssertVarMem(pibset);

    if (_pbody->FPtInBody(xp, yp, pibset))
        return fTrue;

    // 4DMM: BRender's legacy fixed-point ray picker becomes unreliable once
    // uniform object scaling is pushed beyond the original 10x authoring
    // ceiling.  For those oversized objects only, fall back to the actual
    // screen bounds produced by the most recent render.  Normal-sized actors
    // retain the original precise mesh/bounds picker.
    if (FUsesLargeScalePickFallback() && _pbody->FIsInView())
    {
        RC rc;
        _pbody->GetRcBounds(&rc);
        if (rc.FPtIn(xp, yp))
        {
            *pibset = ivNil;
            return fTrue;
        }
    }

    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    FMustRender.  Optimizaton: Is Rerendering necessary between _nfrmCur and
    nfrmLast, exclusive of the current frame?
    Returns fFalse only when rerendering *known* to be unnecessary.
    Otherwise returns fTrue.

***************************************************************************/
bool ACTR::FMustRender(int32_t nfrmRenderLast)
{
    AssertThis(0);
    Assert(nfrmRenderLast >= _nfrmCur, "Invalid argument to FMustRender");
    Assert(!_fLifeDirty, "FMustRender was called when nfrm values were invalid");

    AEV *paev;
    int32_t iaev;

    if (nfrmRenderLast == _nfrmCur)
        goto LStill;
    if (nfrmRenderLast < _nfrmFirst)
        goto LStill;
    if (_nfrmCur > _nfrmLast)
        goto LStill;
    if (_pglrpt->IvMac() == 0)
        goto LStill;

    // 3DMMv1.0: Intervening events?	Sounds don't affect rendering.
    if (_iaevCur < _pggaev->IvMac())
    {
        paev = (AEV *)_pggaev->QvFixedGet(_iaevCur);
        if (paev->nfrm <= nfrmRenderLast)
        {
            for (iaev = _iaevCur; iaev < _pggaev->IvMac(); iaev++)
            {
                paev = (AEV *)_pggaev->QvFixedGet(iaev);
                if (paev->nfrm > nfrmRenderLast)
                    break;
                if (aetSnd == paev->aet)
                    continue;
                if (paev->nfrm < _nfrmLast)
                    goto LMoving;
                // 3DMMv1.0: Freeze and step events affect future frames
                if (aetFreeze != paev->aet && aetStep != paev->aet)
                    goto LMoving;
            }
        }
    }

    if (_dwrStep != rZero)
        goto LMoving; // 3DMMv1.0: moving along path

    // 3DMMv1.0: Not advancing, but moving in place?
    if (_fFrozen || _ccelCur == 1)
        goto LStill;
LMoving:
    return fTrue;

LStill:
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************

    Get actor name

***************************************************************************/
void ACTR::GetName(PSTN pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    // Detached ACTRs are valid.  Actor Studio uses them deliberately when a
    // movie-owned handmade TMPL has no live scene occurrence yet.  In that
    // state _pscen is null, so there is no movie roll-call name to query; use
    // the template name directly instead of dereferencing a nonexistent
    // scene.
    if (_pscen == pvNil || _arid == aridNil || !_pscen->Pmvie()->FGetName(_arid, pstn))
    {
        Ptmpl()->GetName(pstn);
    }
}

/** 3DMMv1.0: *************************************************************************

    Get actor world coordinates

***************************************************************************/
void ACTR::GetXyzWorld(BRS *pxr, BRS *pyr, BRS *pzr)
{
    AssertThis(0);
    AssertNilOrVarMem(pxr);
    AssertNilOrVarMem(pyr);
    AssertNilOrVarMem(pzr);

    XYZ xyz;

    _GetXyzFromRtel(&_rtelCur, &xyz);

    if (pvNil != pxr)
        *pxr = BrsAdd(xyz.dxr, _dxyzRte.dxr);
    if (pvNil != pyr)
        *pyr = BrsAdd(xyz.dyr, _dxyzRte.dyr);
    if (pvNil != pzr)
        *pzr = BrsAdd(xyz.dzr, _dxyzRte.dzr);
}

/***************************************************************************

    Capture the complete current pose needed by 4DMM Object Groups.
    World translation is stored explicitly and the full 3x4 current actor
    matrix preserves pitch/yaw/roll without lossy Euler reconstruction.

***************************************************************************/
void ACTR::GetObjectGroupPose(BRS *pxr, BRS *pyr, BRS *pzr, BMAT34 *pbmat34)
{
    AssertThis(0);
    AssertNilOrVarMem(pbmat34);

    GetXyzWorld(pxr, pyr, pzr);
    if (pbmat34 != pvNil)
        BrMatrix34Copy(pbmat34, &_xfrm.bmat34Cur);
}

/***************************************************************************

    Set the actor's current orientation to an absolute matrix supplied by a
    bound Object Group.  Group transforms are composed in one shared frame,
    so feeding the result back through ordinary FRotate() would incorrectly
    reinterpret the same group delta through each member's local axes.

***************************************************************************/
bool ACTR::FSetObjectGroupOrientation(const BMAT34 *pbmat34, bool fFromHereFwd)
{
    AssertThis(0);
    AssertVarMem(pbmat34);

    if (!_pggaev->FEnsureSpace(1, kcbVarRot, fgrpNil))
        return fFalse;

    BMAT34 bmat34Target;
    BrMatrix34Copy(&bmat34Target, pbmat34);
    _NormalizeActorRotation(&bmat34Target);
    bmat34Target.m[3][0] = rZero;
    bmat34Target.m[3][1] = rZero;
    bmat34Target.m[3][2] = rZero;

    if (fFromHereFwd)
    {
        // bmat34Target is the complete orientation visible on this frame.
        // aetRotF stores the path-independent portion, so remove the current
        // path orientation exactly as FRotate() does when crossing from a
        // held/current rotation into a forward rotation.
        if (_xfrm.zaPath != aZero)
            BrMatrix34PostRotateZ(&bmat34Target, -_xfrm.zaPath);
        if (_xfrm.yaPath != aZero)
            BrMatrix34PostRotateY(&bmat34Target, -_xfrm.yaPath);
        if (_xfrm.xaPath != aZero)
            BrMatrix34PostRotateX(&bmat34Target, -_xfrm.xaPath);
        _NormalizeActorRotation(&bmat34Target);
        BrMatrix34Copy(&_xfrm.bmat34Fwd, &bmat34Target);
        _PrepXfrmFill(aetRotF, &_xfrm.bmat34Fwd, kcbVarRot, _iaevCur, ivNil, faetNil);
        if (!_FAddDoAev(aetRotF, kcbVarRot, &_xfrm.bmat34Fwd))
            return fFalse;
    }
    else
    {
        BrMatrix34Copy(&_xfrm.bmat34Cur, &bmat34Target);
        _PrepXfrmFill(aetRotH, &_xfrm.bmat34Cur, kcbVarRot, _iaevCur, ivNil, faetNil);
        if (!_FAddDoAev(aetRotH, kcbVarRot, &_xfrm.bmat34Cur))
            return fFalse;
    }

    _PositionBody(&_xyzCur);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************

    Hilite the actor

***************************************************************************/
void ACTR::Hilite(void)
{
    AssertThis(0);

    bool fGrouped = fFalse;
    if (MVIE::FMultiSelectMode() && _pscen != pvNil && _pscen->Pmvie() != pvNil)
        fGrouped = _pscen->Pmvie()->FObjectInObjectGroup(_arid);
    Set4DMMGroupedHiliteForNextBody(fGrouped);
    BODY::SetHiliteColor(_fTimeFrozen ? kiclrTimeFreezeHilite : kiclrNormalHilite);
    _pbody->Hilite();
}

/***************************************************************************
    Refresh this actor's existing BODY after Actor Studio mutates the writable
    TMPL in place.  This deliberately keeps _ptmpl and _tagTmpl alive rather
    than fetching/replacing the same template through TAGM while the detached
    Actor Studio preview and live scene occurrences may still reference it.
***************************************************************************/
bool ACTR::FRefreshBodyForTemplateMutation(void)
{
    AssertThis(0);
    if (_ptmpl == pvNil)
        return fFalse;
    if (_pbody == pvNil)
        return fTrue;

    // Topology growth briefly creates BODY parts before their action CEL has
    // supplied models/matrices. Never reattach that half-populated BODY to a
    // live BRender world. Hide once around the complete reshape + pose update
    // and restore the original visibility only after the new CEL is applied.
    const bool fWasVisible = _pbody->FVisible();
    if (fWasVisible)
        _pbody->Hide();

    bool fRet = fFalse;
    if (_ptmpl->FIsTdt())
    {
        if (!((PTDT)_ptmpl)->FAdjustBody(_pbody))
            goto LEnd;
    }
    else if (!_ptmpl->FConformBodyShape(_pbody))
        goto LEnd;
    if (!_ptmpl->FSetActnCel(_pbody, _anidCur, _celnCur))
        goto LEnd;
    _PositionBody(&_xyzCur);
    fRet = fTrue;

LEnd:
    if (fWasVisible && !_pbody->FVisible())
        _pbody->Show();
    return fRet;
}

/** 3DMMv1.0: *************************************************************************

    Change this actor's template.  This gets called if the actor is based
    on a 3-D Text template and the text gets edited.  In addition to
    changing the template itself, this function updates _pbody to conform
    to the new template, and removes all costume events on body part sets
    that no longer exist.  For instance, if a three-letter text actor was
    changed to two letters, all costume events on the third letter are
    deleted.

***************************************************************************/
bool ACTR::FChangeTagTmpl(TAG *ptagTmplNew)
{
    AssertThis(0);
    AssertVarMem(ptagTmplNew);

    PTMPL ptmpl;
    int32_t cbsetNew;
    int32_t iaev;
    AEV aev;
    AEVCOST aevcost;

    ptmpl = (PTMPL)vptagm->PbacoFetch(ptagTmplNew, TMPL::FReadTmpl);
    if (pvNil == ptmpl)
        return fFalse;
    if (_pbody != pvNil)
    {
        if (ptmpl->FIsTdt())
        {
            if (!((PTDT)ptmpl)->FAdjustBody(_pbody))
            {
                ReleasePpo(&ptmpl);
                return fFalse;
            }
        }
        else if (!ptmpl->FConformBodyShape(_pbody))
        {
            ReleasePpo(&ptmpl);
            return fFalse;
        }
        if (!ptmpl->FSetActnCel(_pbody, _anidCur, _celnCur))
        {
            // Actor Studio can switch from a stock TMPL to a document-local copy.
            // If the current action/cel cannot be applied, release the freshly
            // fetched representation before leaving the original actor intact.
            ReleasePpo(&ptmpl);
            return fFalse;
        }
        _PositionBody(&_xyzCur);
    }
    ReleasePpo(&_ptmpl);
    _ptmpl = ptmpl;
    TAGM::CloseTag(&_tagTmpl);
    _tagTmpl = *ptagTmplNew;
    TAGM::DupTag(ptagTmplNew);

    // A detached Actor Studio target has no scene BODY. Its template/tag can
    // still change safely; there is simply no costume state to reconcile.
    // If the new TMPL is not a TDT, we're likewise done.
    if (_pbody == pvNil || !ptmpl->FIsTdt())
        return fTrue;

    cbsetNew = _pbody->Cbset();
    // 3DMMv1.0: Need to remove any costume events acting on ibset >= cbsetNew
    // 3DMMv1.0: Loop backwards to prevent indexing problems since we are deleting
    // 3DMMv1.0: events in this GG as we go
    for (iaev = _pggaev->IvMac() - 1; iaev >= 0; iaev--)
    {
        _pggaev->GetFixed(iaev, &aev);
        if (aev.aet == aetCost)
        {
            _pggaev->Get(iaev, &aevcost);
            if (aevcost.ibset >= cbsetNew)
            {
                _RemoveAev(iaev, fTrue);
            }
        }
    }
    return fTrue;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************

    Assert the validity of the ACTR.

***************************************************************************/
void ACTR::AssertValid(uint32_t grfobj)
{

    ACTR_PAR::AssertValid(fobjAllocated);
    AssertNilOrPo(_pbody, 0);
    AssertNilOrPo(_ptmpl, 0);
    AssertPo(_pggaev, 0);
    AssertPo(_pglrpt, 0);
    AssertPo(_pglsmm, 0);

    int32_t iaevMac = _pggaev->IvMac();
    int32_t irptMac = _pglrpt->IvMac();
    bool fTracing = FPure(grfobj & fobjAssertFull);

    AssertIn(_iaevCur, -1, iaevMac + 1);
    if (irptMac > 0)
        AssertIn(_rtelCur.irpt, 0, irptMac);
    if (ivNil != _iaevActnCur)
        AssertIn(_iaevActnCur, 0, iaevMac);
    if (_iaevAddCur != ivNil)
        AssertIn(_iaevAddCur, 0, iaevMac);

    if (fTracing)
    {
        RPT rpt;
        int32_t irpt;
        int32_t iaev;
        AEV aev;
        bool mpaetfSeen[aetLim];
        RTEL rtel;

        ClearPb(mpaetfSeen, SIZEOF(mpaetfSeen));

        rtel.irpt = -1;
        rtel.dwrOffset = rZero;
        rtel.dnfrm = 0;

        // 3DMMv1.0: Supply a debug readable view of br_scalar path
        for (irpt = 0; irpt < irptMac; irpt++)
        {
            _pglrpt->Get(irpt, &rpt);
        }

        AssertIn(_rtelCur.irpt, 0, irptMac + 1); // 3DMMv1.0: Supply a debug readable view of the event stream
        _pggaev->GetFixed(0, &aev);
        Assert(aetAdd == aev.aet, "BUG: No add event at front of list");

        for (iaev = 0; iaev < _pggaev->IvMac(); iaev++)
        {
            _pggaev->GetFixed(iaev, &aev);
            Assert(rtel <= aev.rtel, "Illegal ordering in event list");
            AssertIn(aev.aet, 0, aetLim);

            // 3DMMv1.0: Verify uniqueness of event types in a single frame
            bool fNewFrame = (aev.aet == aetAdd || rtel != aev.rtel);

            if (mpaetfSeen[aev.aet] == fTrue)
            {
                switch (aev.aet)
                {
                case aetCost:
                case aetAdd:
                case aetSnd:
                    break;

                default:
                    Assert(fNewFrame, "Duplicate events in a single frame");
                }
            }

            if (fNewFrame)
            {
                ClearPb(mpaetfSeen, SIZEOF(mpaetfSeen));
            }

            mpaetfSeen[aev.aet] = fTrue;
            rtel = aev.rtel;
            _pglrpt->Get(aev.rtel.irpt, &rpt);
            Assert(aev.rtel.dwrOffset < rpt.dwr || rpt.dwr == rZero, "Invalid rtel.dwrOffset");

            // 3DMMv1.0: Variable portion of aev retrieved for debug viewing
            switch (aev.aet)
            {
            case aetAdd: {
                AEVADD aevadd;
                Assert(_pggaev->Cb(iaev) == kcbVarAdd, "Corrupt size in event list");
                _pggaev->Get(iaev, &aevadd);
                break;
            }
            case aetActn: {
                AEVACTN aevactn;
                _pggaev->Get(iaev, &aevactn);
                Assert(_pggaev->Cb(iaev) == kcbVarActn, "Corrupt size in event list");
                break;
            }
            case aetCost:
                Assert(_pggaev->Cb(iaev) == kcbVarCost, "Corrupt size in event list");
                break;
            case aetRotF:
            case aetRotH:
                Assert(_pggaev->Cb(iaev) == kcbVarRot, "Corrupt size in event list");
                break;
            case aetSize:
                Assert(_pggaev->Cb(iaev) == kcbVarSize, "Corrupt size in event list");
                break;
            case aetPull:
                Assert(_pggaev->Cb(iaev) == kcbVarPull, "Corrupt size in event list");
                break;
            case aetSnd:
                break;
            case aetFreeze: {
                int32_t faevfrz;
                _pggaev->Get(iaev, &faevfrz);
                Assert(_pggaev->Cb(iaev) == kcbVarFreeze, "Corrupt size in event list");
                break;
            }
            case aetTweak:
                Assert(_pggaev->Cb(iaev) == kcbVarTweak, "Corrupt size in event list");
                break;
            case aetStep: {
                BRS dwrAev;
                _pggaev->Get(iaev, &dwrAev);
                Assert(_pggaev->Cb(iaev) == kcbVarStep, "Corrupt size in event list");
                break;
            }
            case aetRem:
                break;
            case aetMove:
                Assert(_pggaev->Cb(iaev) == kcbVarMove, "Corrupt size in event list");
                break;
            default:
                Assert(0, "Unknown event type");
            }
        }
    }
}

/** 3DMMv1.0: *************************************************************************

    Mark memory used by the ACTR

***************************************************************************/
void ACTR::MarkMem(void)
{
    AssertThis(0);
    int32_t iaev;
    AEV *paev;
    AEVSND *paevsnd;

    ACTR_PAR::MarkMem();
    for (iaev = 0; iaev < _pggaev->IvMac(); iaev++)
    {
        paev = (AEV *)_pggaev->QvFixedGet(iaev);
        if (aetSnd == paev->aet)
        {
            paevsnd = (AEVSND *)_pggaev->QvGet(iaev);
            if (paevsnd->tag.sid == ksidUseCrf)
                paevsnd->tag.MarkMem();
        }
    }
    MarkMemObj(_pggaev);
    MarkMemObj(_pglrpt);
    MarkMemObj(_pbody);
    MarkMemObj(_ptmpl);
    MarkMemObj(_pglsmm);
    _tagTmpl.MarkMem();
}

#endif // 3DMMv1.0: DEBUG
