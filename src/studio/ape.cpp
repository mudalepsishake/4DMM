/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    ape.cpp: Actor preview entity class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    The APE is used to preview a single actor.  The APE doesn't actually
    contain an ACTR...more of an "ACTR Jr.": a _ptmpl, _pbody, _anid, and
    a _pbwld to display in.  If fCycleCels is set to fTrue in PapeNew(),
    the APE will cycle through the cels of the current action.

    APE supports some simple editing operations, such as changing
    materials and TDT properties.  The client must query these changes
    before destroying the APE to make changes to the real actor.

    The APE is currently used by the action browser, TDT easel, and
    costume changer easel.

***************************************************************************/
#include "studio.h"
ASSERTNAME

#define kdtimFrame (kdtimSecond / kfps) // 3DMMv1.0: clock ticks per frame

const BRA kaFov = BR_ANGLE_DEG(60.0); // 3DMMv1.0: camera field of view

RTCLASS(APE)

#if defined(BRENDER_MODERN_14)
static bool FGet4DMMModernApeOwnerRc(PGOB pgobApe, RC *prcOwner)
{
    if (pgobApe == pvNil || prcOwner == pvNil || vapp.Pkwa() == pvNil)
        return fFalse;

    PGOB pgobWorkspace = vapp.Pkwa()->PgobFromHid(kidWorkspace);
    if (pgobWorkspace == pvNil)
        return fFalse;

    RC rcPreview;
    pgobApe->GetRc(&rcPreview, cooHwnd);
    if (rcPreview.FEmpty())
        return fFalse;

    bool fFound = fFalse;
    for (PGOB pgobWalk = pgobApe->PgobPar(); pgobWalk != pvNil; pgobWalk = pgobWalk->PgobPar())
    {
        if (pgobWalk == pgobWorkspace || pgobWalk == vapp.Pkwa())
            break;
        RC rcCandidate;
        pgobWalk->GetRcVis(&rcCandidate, cooHwnd);
        if (rcCandidate.FEmpty() ||
            rcCandidate.xpLeft > rcPreview.xpLeft || rcCandidate.ypTop > rcPreview.ypTop ||
            rcCandidate.xpRight < rcPreview.xpRight || rcCandidate.ypBottom < rcPreview.ypBottom)
            continue;

        // Keep walking upward. The highest child beneath the workspace is the
        // complete 3D Word/Costume/Action easel, not merely the preview frame.
        *prcOwner = rcCandidate;
        fFound = fTrue;
    }
    return fFound;
}
#endif

BEGIN_CMD_MAP(APE, GOB)
ON_CID_GEN(cidAlarm, &APE::FCmdNextCel, pvNil)
END_CMD_MAP_NIL()

/** 3DMMv1.0: *************************************************************************
    Create a new APE
***************************************************************************/
PAPE APE::PapeNew(PGCB pgcb, PTMPL ptmpl, PCOST pcost, int32_t anid, bool fCycleCels, PRCA prca)
{
    AssertVarMem(pgcb);
    AssertPo(ptmpl, 0);
    AssertNilOrPo(pcost, 0);
    AssertNilOrPo(prca, 0);

    PAPE pape;

    pape = NewObj APE(pgcb);
    if (pvNil == pape)
        return pvNil;
    if (!pape->_FInit(ptmpl, pcost, anid, fCycleCels, prca))
    {
        ReleasePpo(&pape);
        return pvNil;
    }
    return pape;
}

/** 3DMMv1.0: *************************************************************************
    Set up the APE
***************************************************************************/
bool APE::_FInit(PTMPL ptmpl, PCOST pcost, int32_t anid, bool fCycleCels, PRCA prca)
{
    AssertBaseThis(0);
    AssertPo(ptmpl, 0);
    AssertNilOrPo(pcost, 0);

    int32_t cbset;
    int32_t ibset;
    RC rc;
    GMS gms;

    _fCycleCels = fCycleCels;
    _fModernFirstDrawRefresh = fTrue;
    _prca = prca;
    _ptmpl = ptmpl;
    _ptmpl->AddRef();
    _pbody = _ptmpl->PbodyCreate(); // 3DMMv1.0: note: also sets default costume
    if (pvNil == _pbody)
        return fFalse;
    if (pvNil != pcost)
        pcost->Set(_pbody);
    cbset = _pbody->Cbset();

    // 3DMMv1.0: If there is exactly one accessory body part set, set _ibsetOnlyAcc
    // 3DMMv1.0: to that ibset.  Else set it to ivNil.
    _ibsetOnlyAcc = ivNil;
    for (ibset = 0; ibset < cbset; ibset++)
    {
        if (_ptmpl->FBsetIsAccessory(ibset))
        {
            if (ivNil == _ibsetOnlyAcc)
            {
                // 3DMMv1.0: First one found.  Remember it and keep looking
                _ibsetOnlyAcc = ibset;
            }
            else
            {
                // 3DMMv1.0: Found a second accessory ibset.  Forget it.
                _ibsetOnlyAcc = ivNil;
                break;
            }
        }
    }

    _pglgms = GL::PglNew(SIZEOF(GMS), cbset);
    if (pvNil == _pglgms)
        return fFalse;
    AssertDo(_pglgms->FSetIvMac(cbset), "PglNew should have ensured space");
    TrashVar(&gms);
    gms.fValid = fFalse;
    for (ibset = 0; ibset < cbset; ibset++)
        _pglgms->Put(ibset, &gms);

    GetRc(&rc, cooLocal);
    _pbwld = BWLD::PbwldNew(rc.Dxp(), rc.Dyp());
    if (pvNil == _pbwld)
        return fFalse;

    // 3DMMv1.0: Add a light source to the BWLD
    _blit.type = BR_LIGHT_DIRECT;
    _blit.colour = BR_COLOUR_RGB(0xff, 0xff, 0xff);
    _blit.attenuation_c = rOne;
    _bact.type = BR_ACTOR_LIGHT;
    _bact.type_data = &_blit;
    _bact.t.type = BR_TRANSFORM_MATRIX34;
    BrMatrix34Identity(&_bact.t.t.mat);
    BrMatrix34PostRotateX(&_bact.t.t.mat, BR_ANGLE_DEG(-40.0));
    BrMatrix34PostRotateY(&_bact.t.t.mat, BR_ANGLE_DEG(-40.0));
    _pbwld->AddActor(&_bact);
    BrLightEnable(&_bact);
    _pbody->SetBwld(_pbwld);
    _pbody->Show();
    // APEs are preview surfaces, never selectable movie actors. Do not inherit
    // a BODY selection-box state into 3D Word, Costume or Action rendering.
    _pbody->Unhilite();

    _InitView();
    if (!FSetAction(anid))
        return fFalse;
    if (fCycleCels)
    {
        _clok.Start(0);
        if (!_clok.FSetAlarm(0, this))
            return fFalse;
    }
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Destroy the APE
***************************************************************************/
APE::~APE()
{
    AssertBaseThis(0);

#if defined(BRENDER_MODERN_14)
    // APE is shared by the 3D Word easel, Costume Changer and Action easel.
    // Retire its native scaled preview before the BWLD identity disappears.
    BrModernPreviewDeactivate(_pbwld);
#endif

    // 3DMMv1.0: If _blit is initialized, then it was added to _pbwld and should
    // 3DMMv1.0: be removed and disabled.  We check whether _blit is initialized by
    // 3DMMv1.0: seeing if _blit.type has been changed from the initial 0.
    Assert(BR_LIGHT_DIRECT != 0, "need a new test for whether _blit was added");
    if (_blit.type == BR_LIGHT_DIRECT)
    {
        BrLightDisable(&_bact);
        BrActorRemove(&_bact);
    }
    ReleasePpo(&_ptmpl);
    ReleasePpo(&_pbody);
    ReleasePpo(&_pbwld);
    ReleasePpo(&_pglgms);
}

/** 3DMMv1.0: *************************************************************************
    Load the brush with ptagMtrl (for a stock material)
***************************************************************************/
void APE::SetToolMtrl(PTAG ptagMtrl)
{
    AssertThis(0);
    AssertVarMem(ptagMtrl);

    _apet.apt = aptGms;
    _apet.gms.fValid = fTrue;
    _apet.gms.fMtrl = fTrue;
    _apet.gms.tagMtrl = *ptagMtrl;
    TrashVar(&_apet.gms.cmid);
}

/** 3DMMv1.0: *************************************************************************
    Load the brush with cmid (for a custom material)
***************************************************************************/
void APE::SetToolCmtl(int32_t cmid)
{
    AssertThis(0);

    _apet.apt = aptGms;
    _apet.gms.fValid = fTrue;
    _apet.gms.fMtrl = fFalse;
    _apet.gms.cmid = cmid;
    TrashVar(&_apet.gms.tagMtrl);
}

/** 3DMMv1.0: *************************************************************************
    Load the brush with the aptIncCmtl tool
***************************************************************************/
void APE::SetToolIncCmtl(void)
{
    AssertThis(0);

    _apet.apt = aptIncCmtl;
    TrashVar(&_apet.gms);
    _apet.gms.fValid = fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Load the brush with the aptIncAccessory tool
***************************************************************************/
void APE::SetToolIncAccessory(void)
{
    AssertThis(0);

    _apet.apt = aptIncAccessory;
    TrashVar(&_apet.gms);
    _apet.gms.fValid = fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Change the actor's action to anid
***************************************************************************/
bool APE::FSetAction(int32_t anid)
{
    AssertThis(0);

    if (!_ptmpl->FSetActnCel(_pbody, anid, 0))
        return fFalse;
    _anid = anid;
    _celn = 0;
    // Preserve the historical APE behavior: frame the first cel when a new
    // action is selected, then keep that camera fixed while the action cycles.
    // The Modern-only per-cel refit lived in FDisplayCel() and is deliberately
    // not repeated there.
    _SetScale();
    _UpdateView();
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Time to move to the next cel of the action.  Resets alarm
***************************************************************************/
bool APE::FCmdNextCel(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    if (_fCycleCels)
    {
        FDisplayCel(_celn + 1);
    }

    _clok.FSetAlarm(kdtimFrame, this);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Time to move to the next cel of the action.
***************************************************************************/
bool APE::FDisplayCel(int32_t celn)
{
    AssertThis(0);

    // 3DMMv1.0: Someone else will push an error if this fails
    if (_ptmpl->FSetActnCel(_pbody, _anid, celn))
    {
        _celn = celn;
#if defined(BRENDER_MODERN_14)
        if (_fCycleCels)
        {
            PSTDIO pstdioActionCamera = vapp.Pstdio();
            MVIE::MultiLog(pstdioActionCamera != pvNil ? pstdioActionCamera->Pmvie() : pvNil,
                           "ape_action_camera_static action=%ld cel=%ld scale_skipped=1",
                           (long)_anid, (long)_celn);
        }
#endif
        _UpdateView();
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Enable or disable cel cycling.
***************************************************************************/
void APE::SetCycleCels(bool fOn)
{
    AssertThis(0);
    _fCycleCels = fOn;
}

/** 3DMMv1.0: *************************************************************************
    Go to the default view
***************************************************************************/
void APE::_InitView(void)
{
    AssertThis(0);

    BMAT34 bmat34;
    BRA xa, ya, za;

    _iview = 0;
    _ptmpl->GetRestOrien(&xa, &ya, &za);
    BrMatrix34Identity(&bmat34);
    BrMatrix34PostRotateX(&bmat34, xa);
    BrMatrix34PostRotateY(&bmat34, ya);
    BrMatrix34PostRotateZ(&bmat34, za);
    _pbody->LocateOrient(rZero, rZero, rZero, &bmat34);
    _SetScale();
    _UpdateView();
}

/** 3DMMv1.0: *************************************************************************
    Set a custom view
***************************************************************************/
void APE::SetCustomView(BRA xa, BRA ya, BRA za)
{
    AssertThis(0);

    BMAT34 bmat34;
    BRA xaTmpl, yaTmpl, zaTmpl;

    _iview = -1;
    _ptmpl->GetRestOrien(&xaTmpl, &yaTmpl, &zaTmpl);
    BrMatrix34Identity(&bmat34);
    BrMatrix34PostRotateX(&bmat34, xaTmpl);
    BrMatrix34PostRotateY(&bmat34, yaTmpl);
    BrMatrix34PostRotateZ(&bmat34, zaTmpl);
    BrMatrix34PostRotateX(&bmat34, xa);
    BrMatrix34PostRotateY(&bmat34, ya);
    BrMatrix34PostRotateZ(&bmat34, za);
    _pbody->LocateOrient(rZero, rZero, rZero, &bmat34);
    _SetScale();
    _UpdateView();
}

/** 3DMMv1.0: *************************************************************************
    Rotate the BODY to see a different view
***************************************************************************/
void APE::ChangeView(void)
{
    AssertThis(0);

    BMAT34 bmat34;
    BRA xa, ya, za;

    _iview = (_iview + 1) % 5;
    _ptmpl->GetRestOrien(&xa, &ya, &za);
    BrMatrix34Identity(&bmat34);
    BrMatrix34PostRotateX(&bmat34, xa);
    BrMatrix34PostRotateY(&bmat34, ya);
    BrMatrix34PostRotateZ(&bmat34, za);

    switch (_iview)
    {
    case 0:
        _InitView();
        return;
    case 1:
        BrMatrix34PostRotateY(&bmat34, BR_ANGLE_DEG(90.0));
        break;
    case 2:
        BrMatrix34PostRotateY(&bmat34, BR_ANGLE_DEG(180.0));
        break;
    case 3:
        BrMatrix34PostRotateY(&bmat34, BR_ANGLE_DEG(270.0));
        break;
    case 4:
        BrMatrix34PostRotateX(&bmat34, BR_ANGLE_DEG(90.0));
        break;
    }

    _pbody->LocateOrient(rZero, rZero, rZero, &bmat34);
    _SetScale();
    _UpdateView();
}

/** 3DMMv1.0: *************************************************************************
    Position the camera so the actor is centered and properly sized
***************************************************************************/
void APE::_SetScale(void)
{
    AssertThis(0);

    RC rcView;
    BCB bcbBody; // 3DMMv1.0: 3-D bounds of BODY
    BRS dxrBody, dyrBody, dzrBody;
    BRS rDydxBody;
    BRS xrCam, yrCam, zrCam;
    BRS zrHither, zrYon;
    BMAT34 bmat34Camera;
    BRS dxrView, dyrView;
    BRS rDydxView;
    BRA aFov;
    BRS rAtan;

    _pbody->GetBcbBounds(&bcbBody, fTrue);
    dxrBody = bcbBody.xrMax - bcbBody.xrMin;
    dyrBody = bcbBody.yrMax - bcbBody.yrMin;
    dzrBody = bcbBody.zrMax - bcbBody.zrMin;

    rDydxBody = BrsDiv(dyrBody, dxrBody);

    // 3DMMv1.0: The x and y camera distances are functions of the body only
    xrCam = BrsHalf(bcbBody.xrMax + bcbBody.xrMin);
    yrCam = BrsHalf(bcbBody.yrMax + bcbBody.yrMin);

    GetRc(&rcView, cooLocal);
    dxrView = BrIntToScalar(rcView.Dxp());
    dyrView = BrIntToScalar(rcView.Dyp());
    rDydxView = BrsDiv(dyrView, dxrView);

    aFov = BrScalarToAngle(BrsHalf(BrAngleToScalar(kaFov)));
    rAtan = BrsDiv(BR_COS(aFov), BR_SIN(aFov));

    if (rDydxBody > rDydxView)
    {
        // 3DMMv1.0: Tall actor : Compute z based on y
        zrCam = BrsMul(BrsHalf(dyrBody), rAtan);
    }
    else
    {
        // 3DMMv1.0: Wide actor : Compute z based on x
        // 3DMMv1.0: Fit the actor width into a narrow rc
        dxrBody = BrsMul(dxrBody, rDydxView);
        zrCam = BrsMul(BrsHalf(dxrBody), rAtan);
    }

    // 3DMMv1.0: Adjust for body depth
    zrCam += bcbBody.zrMax;

    // 3DMMv1.0: Set hither and yon.  Note: there's probably a good algorithm to
    // 3DMMv1.0:    compute the optimal values for zrHither and zrYon, but I
    // 3DMMv1.0:    don't have time to figure it out right now. :-) The goal is
    // 3DMMv1.0:    to make zrHither as large as possible and zrYon as small as
    // 3DMMv1.0:    possible without intersecting the actor.  -*****
    if (zrCam < BR_SCALAR(10.0))
        zrHither = BR_SCALAR(0.1);
    else
        zrHither = rOne;
    zrYon = BR_SCALAR(1000.0);

    BrMatrix34Translate(&bmat34Camera, xrCam, yrCam, zrCam);
    _pbwld->SetCamera(&bmat34Camera, zrHither, zrYon, kaFov);

#if defined(BRENDER_MODERN_14)
    // Keep every Modern APE on the original 3DMM camera-fit algorithm.  Earlier
    // TDT-only scale margins were diagnostic workarounds; the remaining top
    // obstruction is a compositor overlap, not authored camera framing.
    // Log the geometry/camera values without changing them.
    {
        PSTDIO pstdioScale = vapp.Pstdio();
        MVIE::MultiLog(pstdioScale != pvNil ? pstdioScale->Pmvie() : pvNil,
            "ape_scale tdt=%d cycle=%d action=%ld view=%ldx%ld body=(%.6g,%.6g,%.6g)-(%.6g,%.6g,%.6g) body_whd=(%.6g,%.6g,%.6g) camera=(%.6g,%.6g,%.6g) hither=%.6g yon=%.6g historical_framing=1 tdt_vertical_margin=1.00",
            (int)(_ptmpl != pvNil && _ptmpl->FIsTdt()), (int)_fCycleCels, (long)_anid,
            (long)rcView.Dxp(), (long)rcView.Dyp(),
            (double)BrScalarToFloat(bcbBody.xrMin), (double)BrScalarToFloat(bcbBody.yrMin),
            (double)BrScalarToFloat(bcbBody.zrMin), (double)BrScalarToFloat(bcbBody.xrMax),
            (double)BrScalarToFloat(bcbBody.yrMax), (double)BrScalarToFloat(bcbBody.zrMax),
            (double)BrScalarToFloat(bcbBody.xrMax - bcbBody.xrMin),
            (double)BrScalarToFloat(bcbBody.yrMax - bcbBody.yrMin),
            (double)BrScalarToFloat(bcbBody.zrMax - bcbBody.zrMin),
            (double)BrScalarToFloat(xrCam), (double)BrScalarToFloat(yrCam),
            (double)BrScalarToFloat(zrCam), (double)BrScalarToFloat(zrHither),
            (double)BrScalarToFloat(zrYon));
    }
#endif
}

/** 3DMMv1.0: *************************************************************************
    Render and force redraw of the APE
***************************************************************************/
void APE::_UpdateView(void)
{
    AssertThis(0);

#if defined(BRENDER_MODERN_14)
    // During APE construction _InitView()/FSetAction() can render before the
    // enclosing easel has drawn. Activating the native compositor that early is
    // what exposes the one/two-frame material-colour square (and then the actor
    // or default TDT) on top of the movie before the Action/3D Word chrome
    // exists. Keep those setup renders private. Draw() performs the first native
    // activation once the real preview/easel geometry is authoritative.
    if (!_fModernFirstDrawRefresh)
    {
        RC rcPreview;
        RC rcOwner;
        GetRc(&rcPreview, cooHwnd);
        const bool fHaveOwner = FGet4DMMModernApeOwnerRc(this, &rcOwner);
        BrModernPreviewActivate(_pbwld, rcPreview.xpLeft, rcPreview.ypTop,
                                rcPreview.Dxp(), rcPreview.Dyp(),
                                fHaveOwner ? rcOwner.xpLeft : 0,
                                fHaveOwner ? rcOwner.ypTop : 0,
                                fHaveOwner ? rcOwner.Dxp() : 0,
                                fHaveOwner ? rcOwner.Dyp() : 0);
    }
    else
    {
        PSTDIO pstdioDeferred = vapp.Pstdio();
        MVIE::MultiLog(pstdioDeferred != pvNil ? pstdioDeferred->Pmvie() : pvNil,
                       "ape_present_deferred_until_first_draw tdt=%d cycle=%d action=%ld cel=%ld",
                       (int)(_ptmpl != pvNil && _ptmpl->FIsTdt()),
                       (int)_fCycleCels, (long)_anid, (long)_celn);
    }
#endif
    _pbwld->Render();
    _pbwld->MarkRenderedRegn(this, 0, 0);
}

/** 3DMMv1.0: *************************************************************************
    Draw the contents of the APE's bwld
***************************************************************************/
void APE::Draw(PGNV pgnv, RC *prcClip)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    AssertVarMem(prcClip);

#if defined(BRENDER_MODERN_14)
    // Keep the native rectangle current even when the APE itself did not need
    // a new frame. APEs are often constructed and initially rendered while
    // their browser/easel is still being laid out. Recompute the camera and
    // rerender once from the first real Draw(), when the final preview RC is
    // authoritative. This particularly fixes Action's off-frame first cache.
    RC rcPreview;
    RC rcOwner;
    GetRc(&rcPreview, cooHwnd);
    const bool fHaveOwner = FGet4DMMModernApeOwnerRc(this, &rcOwner);
    BrModernPreviewActivate(_pbwld, rcPreview.xpLeft, rcPreview.ypTop,
                            rcPreview.Dxp(), rcPreview.Dyp(),
                            fHaveOwner ? rcOwner.xpLeft : 0,
                            fHaveOwner ? rcOwner.ypTop : 0,
                            fHaveOwner ? rcOwner.Dxp() : 0,
                            fHaveOwner ? rcOwner.Dyp() : 0);
    if (_fModernFirstDrawRefresh)
    {
        _fModernFirstDrawRefresh = fFalse;
        _SetScale();
        _pbwld->Render();
        _pbwld->MarkRenderedRegn(this, 0, 0);

        BCB bcbBody;
        RC rcBodyRendered;
        _pbody->GetBcbBounds(&bcbBody, fTrue);
        const bool fBodyInView = _pbody->FIsInView();
        _pbody->GetRcBounds(&rcBodyRendered);
        PSTDIO pstdio = vapp.Pstdio();
        MVIE::MultiLog(pstdio != pvNil ? pstdio->Pmvie() : pvNil,
            "ape_first_visible_refresh apt=%ld tdt=%d cycle=%d action=%ld cel=%ld rect=(%ld,%ld)-(%ld,%ld) owner_valid=%d owner=(%ld,%ld)-(%ld,%ld) bounds=(%.6g,%.6g,%.6g)-(%.6g,%.6g,%.6g) in_view=%d rendered_rc=(%ld,%ld)-(%ld,%ld)",
            (long)_apet.apt, (int)(_ptmpl != pvNil && _ptmpl->FIsTdt()),
            (int)_fCycleCels, (long)_anid, (long)_celn,
            (long)rcPreview.xpLeft, (long)rcPreview.ypTop,
            (long)rcPreview.xpRight, (long)rcPreview.ypBottom,
            (int)fHaveOwner,
            fHaveOwner ? (long)rcOwner.xpLeft : 0L,
            fHaveOwner ? (long)rcOwner.ypTop : 0L,
            fHaveOwner ? (long)rcOwner.xpRight : 0L,
            fHaveOwner ? (long)rcOwner.ypBottom : 0L,
            (double)BrScalarToFloat(bcbBody.xrMin),
            (double)BrScalarToFloat(bcbBody.yrMin),
            (double)BrScalarToFloat(bcbBody.zrMin),
            (double)BrScalarToFloat(bcbBody.xrMax),
            (double)BrScalarToFloat(bcbBody.yrMax),
            (double)BrScalarToFloat(bcbBody.zrMax),
            (int)fBodyInView, (long)rcBodyRendered.xpLeft, (long)rcBodyRendered.ypTop,
            (long)rcBodyRendered.xpRight, (long)rcBodyRendered.ypBottom);
    }
#endif
    _pbwld->Draw(pgnv, prcClip, 0, 0);
}

/** 3DMMv1.0: *************************************************************************
    Set the cursor appropriately
***************************************************************************/
bool APE::FCmdMouseMove(PCMD_MOUSE pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PBACT pbact;
    PBODY pbody;
    int32_t ibset;
    int32_t ibsetAcc;

    if (pvNil != _prca)
    {
        SetCursCno(_prca, kcrsDefault);
        if (_pbwld->FClickedActor(pcmd->xp, pcmd->yp, &pbact))
        {
            pbody = BODY::PbodyFromBact(pbact, &ibset);
            Assert(pbody == _pbody, "what BODY is this?");
            if (_apet.apt == aptIncCmtl)
            {
                if (_ptmpl->CcmidOfBset(ibset) > 1)
                    SetCursCno(_prca, kcrsCostume);
            }
            else if (_apet.apt == aptIncAccessory)
            {
                // 3DMMv1.0: Change cursor if there's only one accessory
                // 3DMMv1.0: body part set or if cursor is on either an accessory
                // 3DMMv1.0: body part set or the parent of one.
                if (_ibsetOnlyAcc != ivNil || _ptmpl->FIbsetAccOfIbset(ibset, &ibsetAcc))
                {
                    SetCursCno(_prca, kcrsHand);
                }
            }
        }
    }
    return APE_PAR::FCmdMouseMove(pcmd);
}

/** 3DMMv1.0: *************************************************************************
    Handle mouse-down.  Do the right thing depending on the current tool.
***************************************************************************/
bool APE::FCmdTrackMouse(PCMD_MOUSE pcmd)
{
    AssertThis(0);

    PBACT pbact;
    PBODY pbody;
    int32_t ibset;
    int32_t ibsetAcc;
    GMS gms;

    if (pcmd->cid != cidMouseDown)
    {
        Bug("Should only get a mousedown here!");
        return fTrue;
    }
    if (!_pbwld->FClickedActor(pcmd->xp, pcmd->yp, &pbact))
    {
#if defined(BRENDER_MODERN_14)
        RC rcPreview;
        GetRc(&rcPreview, cooHwnd);
        PSTDIO pstdio = vapp.Pstdio();
        MVIE::MultiLog(pstdio != pvNil ? pstdio->Pmvie() : pvNil,
            "ape_pick miss apt=%ld tdt=%d local=(%ld,%ld) source_rect=(%ld,%ld)-(%ld,%ld)",
            (long)_apet.apt, (int)(_ptmpl != pvNil && _ptmpl->FIsTdt()),
            (long)pcmd->xp, (long)pcmd->yp,
            (long)rcPreview.xpLeft, (long)rcPreview.ypTop,
            (long)rcPreview.xpRight, (long)rcPreview.ypBottom);
#endif
        return fTrue;
    }
    pbody = BODY::PbodyFromBact(pbact, &ibset);
    Assert(pbody == _pbody, "what BODY is this?");
#if defined(BRENDER_MODERN_14)
    {
        RC rcPreview;
        GetRc(&rcPreview, cooHwnd);
        PSTDIO pstdio = vapp.Pstdio();
        MVIE::MultiLog(pstdio != pvNil ? pstdio->Pmvie() : pvNil,
            "ape_pick hit apt=%ld tdt=%d local=(%ld,%ld) ibset=%ld source_rect=(%ld,%ld)-(%ld,%ld)",
            (long)_apet.apt, (int)(_ptmpl != pvNil && _ptmpl->FIsTdt()),
            (long)pcmd->xp, (long)pcmd->yp, (long)ibset,
            (long)rcPreview.xpLeft, (long)rcPreview.ypTop,
            (long)rcPreview.xpRight, (long)rcPreview.ypBottom);
    }
#endif

    switch (_apet.apt)
    {
    case aptNil:
        break;

    case aptGms:
        gms = _apet.gms;
        Assert(gms.fValid, 0);
        if (_FApplyGms(&gms, ibset))
            _pglgms->Put(ibset, &gms);
        break;

    case aptIncCmtl:
        _pglgms->Get(ibset, &gms);
        if (_FIncCmtl(&gms, ibset, fFalse))
            _pglgms->Put(ibset, &gms);
        break;

    case aptIncAccessory:
        if ((ibsetAcc = _ibsetOnlyAcc) != ivNil || _ptmpl->FIbsetAccOfIbset(ibset, &ibsetAcc))
        {
            _pglgms->Get(ibsetAcc, &gms);
            if (_FIncCmtl(&gms, ibsetAcc, fTrue))
                _pglgms->Put(ibsetAcc, &gms);
            _SetScale();
        }
        break;

    default:
        BugVar("weird tool", &_apet.apt);
        break;
    }

    _UpdateView();
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Apply a GMS to ibset
***************************************************************************/
bool APE::_FApplyGms(GMS *pgms, int32_t ibset)
{
    AssertThis(0);
    AssertVarMem(pgms);
    Assert(pgms->fValid, "bad gms");
    AssertIn(ibset, 0, _pbody->Cbset());

    PMTRL pmtrl;
    PCMTL pcmtl;

    if (pgms->fMtrl)
    {
        pmtrl = (PMTRL)vptagm->PbacoFetch(&pgms->tagMtrl, MTRL::FReadMtrl);
        if (pvNil == pmtrl)
            return fFalse;
        _pbody->SetPartSetMtrl(ibset, pmtrl);
        ReleasePpo(&pmtrl);
    }
    else // 3DMMv1.0: cmtl
    {
        pcmtl = _ptmpl->PcmtlFetch(pgms->cmid);
        if (pvNil == pcmtl)
            return fFalse;
        _pbody->SetPartSetCmtl(pcmtl);
        ReleasePpo(&pcmtl);
    }
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Fill in pgms with the next CMTL available for ibset and applies it
    to _pbody.
***************************************************************************/
bool APE::_FIncCmtl(GMS *pgms, int32_t ibset, bool fNextAccessory)
{
    AssertThis(0);
    AssertVarMem(pgms);
    AssertIn(ibset, 0, _pbody->Cbset());

    int32_t cmid;
    int32_t cmidNext;
    int32_t icmid;
    int32_t ccmid;
    PCMTL pcmtl;
    PCMTL pcmtlOld;
    PMTRL pmtrlOld;
    bool fMtrl;

    // 3DMMv1.0: Default cmidNext is default cmid for this body part set
    cmidNext = _ptmpl->CmidOfBset(ibset, 0);

    ccmid = _ptmpl->CcmidOfBset(ibset);

    // 3DMMv1.0: Need to find out what cmid (if any) is currently attached to ibset.
    // 3DMMv1.0: If the GMS is valid, use it.  Otherwise, figure out which cmid
    // 3DMMv1.0: generates the same pcmtl that is currently attached to the body.
    if (!pgms->fValid || pgms->fMtrl)
    {
        // 3DMMv1.0: GMS is useless...look at the body
        _pbody->GetPartSetMaterial(ibset, &fMtrl, &pmtrlOld, &pcmtlOld);
        if (!fMtrl)
        {
            for (icmid = 0; icmid < ccmid; icmid++)
            {
                cmid = _ptmpl->CmidOfBset(ibset, icmid);
                pcmtl = _ptmpl->PcmtlFetch(cmid);
                if (pvNil == pcmtl)
                    return fFalse;
                if (pcmtl == pcmtlOld)
                {
                    ReleasePpo(&pcmtl);
                    cmidNext = _CmidNext(ibset, icmid, fNextAccessory);
                    break;
                }
                ReleasePpo(&pcmtl);
            }
        }
    }
    else
    {
        // 3DMMv1.0: GMS has current cmid
        cmid = pgms->cmid;
        for (icmid = 0; icmid < ccmid; icmid++)
        {
            if (cmid == _ptmpl->CmidOfBset(ibset, icmid))
            {
                cmidNext = _CmidNext(ibset, icmid, fNextAccessory);
                break;
            }
        }
    }
    pcmtl = _ptmpl->PcmtlFetch(cmidNext);
    if (pvNil == pcmtl)
        return fFalse;
    _pbody->SetPartSetCmtl(pcmtl);
    ReleasePpo(&pcmtl);
    pgms->cmid = cmidNext;
    pgms->fMtrl = fFalse;
    pgms->fValid = fTrue;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Return the CMID that follows the given CMID.  If fNextAccessory, it
    returns the next CMID that has a different set of models than icmidCur.
    Otherwise it returns the next CMID that has the same set of models as
    icmidCur.
***************************************************************************/
int32_t APE::_CmidNext(int32_t ibset, int32_t icmidCur, bool fNextAccessory)
{
    AssertThis(0);

    int32_t cmidCur;
    int32_t ccmid;
    int32_t icmidNext;
    int32_t cmidNext;
    int32_t dicmid;

    cmidCur = _ptmpl->CmidOfBset(ibset, icmidCur);
    ccmid = _ptmpl->CcmidOfBset(ibset);

    for (dicmid = 1; dicmid < ccmid; dicmid++)
    {
        icmidNext = (icmidCur + dicmid) % ccmid;
        cmidNext = _ptmpl->CmidOfBset(ibset, icmidNext);
        if (fNextAccessory)
        {
            if (!_ptmpl->FSameAccCmids(cmidCur, cmidNext))
                return cmidNext;
        }
        else
        {
            if (_ptmpl->FSameAccCmids(cmidCur, cmidNext))
                return cmidNext;
        }
    }
    return cmidCur; // 3DMMv1.0: couldn't find anything...just use this cmid
}

/** 3DMMv1.0: *************************************************************************
    Change this APE's TDT properties
***************************************************************************/
bool APE::FChangeTdt(PSTN pstn, int32_t tdts, PTAG ptagTdf)
{
    AssertThis(0);
    Assert(_ptmpl->FIsTdt(), "FChangeTdt is only for TDTs");
    AssertNilOrPo(pstn, 0);
    AssertNilOrVarMem(ptagTdf);

    PTDT ptdtNew = pvNil;
    PBODY pbodyNew = pvNil;
    bool fTextEmpty = fFalse;
    int32_t cpartOld = 0;
    BCB bcbOld;
    BCB bcbNew;
    PSTDIO pstdioTdt = pvNil;

    ptdtNew = ((PTDT)_ptmpl)->PtdtDup();
    if (pvNil == ptdtNew)
        goto LFail;
    pbodyNew = _pbody->PbodyDup();
    if (pvNil == pbodyNew)
        goto LFail;

    if (!ptdtNew->FChange(pstn, tdts, ptagTdf))
        goto LFail;
    if (!ptdtNew->FAdjustBody(pbodyNew))
        goto LFail;
    if (!ptdtNew->FSetActnCel(pbodyNew, _anid, _celn))
        goto LFail;

    fTextEmpty = pstn != pvNil && pstn->Cch() == 0;
    cpartOld = _pbody->Cpart();
    _pbody->GetBcbBounds(&bcbOld, fTrue);

    ReleasePpo(&_ptmpl);
    _ptmpl = ptdtNew;

    // PbodyDup/Restore traditionally swaps a visible old BODY with a hidden
    // replacement and lets the discarded snapshot destructor remove the old
    // root afterward. That leaves a small interval with both roots attached to
    // the same BWLD. APE text editing can repaint synchronously in that interval,
    // producing exactly the accumulating previous-word geometry seen in the
    // native 3D Word preview. Make this replacement atomic from the world's
    // point of view: remove the old root first, swap while hidden, destroy the
    // old snapshot, then attach only the new root.
    if (_pbody->FVisible())
        _pbody->Hide();
    _pbody->Restore(pbodyNew);
    ReleasePpo(&pbodyNew);
    // TDT keeps one internal space part when the text field is empty because
    // the legacy BODY/TMPL format cannot represent zero parts. Do not render
    // that implementation-detail part in the editor preview. A subsequent
    // non-empty edit explicitly shows the replacement BODY again.
    if (!fTextEmpty)
        _pbody->Show();
    _pbody->Unhilite();

    _pbody->GetBcbBounds(&bcbNew, fTrue);
    pstdioTdt = vapp.Pstdio();
    MVIE::MultiLog(pstdioTdt != pvNil ? pstdioTdt->Pmvie() : pvNil,
        "ape_tdt_replace chars=%ld old_parts=%ld new_parts=%ld visible=%d old_bounds=(%.6g,%.6g,%.6g)-(%.6g,%.6g,%.6g) new_bounds=(%.6g,%.6g,%.6g)-(%.6g,%.6g,%.6g)",
        pstn != pvNil ? (long)pstn->Cch() : -1L, (long)cpartOld, (long)_pbody->Cpart(),
        (int)_pbody->FVisible(),
        (double)BrScalarToFloat(bcbOld.xrMin), (double)BrScalarToFloat(bcbOld.yrMin),
        (double)BrScalarToFloat(bcbOld.zrMin), (double)BrScalarToFloat(bcbOld.xrMax),
        (double)BrScalarToFloat(bcbOld.yrMax), (double)BrScalarToFloat(bcbOld.zrMax),
        (double)BrScalarToFloat(bcbNew.xrMin), (double)BrScalarToFloat(bcbNew.yrMin),
        (double)BrScalarToFloat(bcbNew.zrMin), (double)BrScalarToFloat(bcbNew.xrMax),
        (double)BrScalarToFloat(bcbNew.yrMax), (double)BrScalarToFloat(bcbNew.zrMax));

    if (fTextEmpty)
    {
        MVIE::MultiLog(pstdioTdt != pvNil ? pstdioTdt->Pmvie() : pvNil,
                       "ape_tdt_empty_preview body_hidden=1 internal_parts=%ld",
                       (long)_pbody->Cpart());
    }
    _SetScale();
    _UpdateView();
    return fTrue;
LFail:
    ReleasePpo(&ptdtNew);
    ReleasePpo(&pbodyNew);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Set material of this TDT to ptagMtrl
***************************************************************************/
bool APE::FSetTdtMtrl(PTAG ptagMtrl)
{
    AssertThis(0);
    Assert(_ptmpl->FIsTdt(), "FSetTdtMtrl is only for TDTs");
    Assert(_pbody->Cbset() == 1, "TDTs should only have one body part set");
    AssertVarMem(ptagMtrl);

    PMTRL pmtrl;
    GMS gms;

    pmtrl = (PMTRL)vptagm->PbacoFetch(ptagMtrl, MTRL::FReadMtrl);
    if (pvNil == pmtrl)
        return fFalse;
    _pbody->SetPartSetMtrl(0, pmtrl);
    ReleasePpo(&pmtrl);

    gms.fValid = fTrue;
    gms.fMtrl = fTrue;
    TrashVar(&gms.cmid);
    gms.tagMtrl = *ptagMtrl;
    _pglgms->Put(0, &gms);

    _UpdateView();
    return fTrue;
}

/***************************************************************************
    Apply a stock material directly to one preview body-part set and record
    the change so the owning easel can commit it.
***************************************************************************/
bool APE::FApplyMtrlToBset(int32_t ibset, PTAG ptagMtrl)
{
    AssertThis(0);
    AssertIn(ibset, 0, _pbody->Cbset());
    AssertVarMem(ptagMtrl);

    GMS gms;
    gms.fValid = fTrue;
    gms.fMtrl = fTrue;
    TrashVar(&gms.cmid);
    gms.tagMtrl = *ptagMtrl;
    if (!_FApplyGms(&gms, ibset))
        return fFalse;
    _pglgms->Put(ibset, &gms);
    _UpdateView();
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Get the CNO of the MTRL attached to this TDT.  Returns fFalse if there
    is no MTRL attached or the MTRL didn't come from a chunk.
***************************************************************************/
bool APE::FGetTdtMtrlCno(CNO *pcno)
{
    AssertThis(0);
    Assert(_ptmpl->FIsTdt(), "FGetTdtMtrlCno is only for TDTs");
    AssertVarMem(pcno);

    PMTRL pmtrl;
    PCMTL pcmtl;
    bool fMtrl;

    _pbody->GetPartSetMaterial(0, &fMtrl, &pmtrl, &pcmtl);
    if (!fMtrl)
        return fFalse;
    if (pvNil == pmtrl->Pcrf())
        return fFalse;
    *pcno = pmtrl->Cno();
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Return info about the TDT
***************************************************************************/
void APE::GetTdtInfo(PSTN pstn, int32_t *ptdts, PTAG ptagTdf)
{
    AssertThis(0);
    Assert(_ptmpl->FIsTdt(), "GetTdtInfo is only for TDTs");
    AssertNilOrVarMem(pstn);
    AssertNilOrVarMem(ptdts);
    AssertNilOrVarMem(ptagTdf);

    ((PTDT)_ptmpl)->GetInfo(pstn, ptdts, ptagTdf);
}

/** 3DMMv1.0: *************************************************************************
    Fills in pfMtrl, pcmid, and ptagMtrl with the material that was
    attached to ibset, if any.  If nothing was done to ibset, returns
    fFalse.
***************************************************************************/
bool APE::FGetMaterial(int32_t ibset, tribool *pfMtrl, int32_t *pcmid, TAG *ptagMtrl)
{
    AssertThis(0);
    AssertIn(ibset, 0, _pbody->Cbset());
    AssertVarMem(pfMtrl);
    AssertVarMem(pcmid);
    AssertVarMem(ptagMtrl);

    GMS gms;

    _pglgms->Get(ibset, &gms);
    if (!gms.fValid)
        return fFalse; // 3DMMv1.0: nothing new for this ibset
    *pfMtrl = (tribool)gms.fMtrl;
    *pcmid = gms.cmid;
    *ptagMtrl = gms.tagMtrl;

    return fTrue;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of the APE.
***************************************************************************/
void APE::AssertValid(uint32_t grf)
{
    APE_PAR::AssertValid(fobjAllocated);
    AssertPo(_pbwld, 0);
    AssertPo(_ptmpl, 0);
    AssertPo(_pbody, 0);
    AssertPo(_pglgms, 0);
    Assert(_pglgms->IvMac() == _pbody->Cbset(), "_pglgms wrong size");
    AssertNilOrPo(_prca, 0);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory used by the APE
***************************************************************************/
void APE::MarkMem(void)
{
    AssertThis(0);
    APE_PAR::MarkMem();
    MarkMemObj(_pbwld);
    MarkMemObj(_ptmpl);
    MarkMemObj(_pbody);
    MarkMemObj(_pglgms);
    MarkMemObj(_prca);
}
#endif // 3DMMv1.0: DEBUG
