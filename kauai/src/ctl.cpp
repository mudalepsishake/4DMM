/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Standard controls.

***************************************************************************/
#include "frame.h"
ASSERTNAME

// 3DMMv1.0: what we set the system max to for scroll bars
const int32_t _klwMaxScroll = 20000; // 3DMMv1.0: should be less than 32K

#ifdef WIN
achar _szCtlProp[] = PszLit("CTL");
#endif // 3DMMv1.0: WIN

RTCLASS(CTL)
RTCLASS(SCB)
RTCLASS(WSB)

/** 3DMMv1.0: *************************************************************************
    Constructor for a control.
***************************************************************************/
CTL::CTL(PGCB pgcb) : GOB(pgcb)
{
    _hctl = hNil;
}

/** 3DMMv1.0: *************************************************************************
    Destructor for controls.
***************************************************************************/
CTL::~CTL(void)
{
    if (_hctl != hNil)
    {
#ifdef MAC
        RC rc;
        GNV gnv(this);

        rc.Zero();
        gnv.ClipRc(&rc);
        gnv.Set();
        DisposeControl(_hctl);
        gnv.Restore();
#endif // 3DMMv1.0: MAC
#ifdef WIN
        RemoveProp(_hctl, _szCtlProp);
        DestroyWindow(_hctl);
#endif // 3DMMv1.0: WIN
        _hctl = hNil;
    }
}

/** 3DMMv1.0: *************************************************************************
    Sets the OS control for the CTL.  If this fails, it frees the control.
***************************************************************************/
bool CTL::_FSetHctl(HCTL hctl)
{
    Assert(_hctl == hNil, "CTL already has an OS control");
    if (hctl != hNil)
    {
#ifdef MAC
        SetCRefCon(hctl, (int32_t)this);
#endif // 3DMMv1.0: MAC
#ifdef WIN
        if (!SetProp(hctl, _szCtlProp, (HANDLE)this))
        {
            DestroyWindow(hctl);
            return fFalse;
        }
#endif // 3DMMv1.0: WIN
        _hctl = hctl;
    }
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Return the CTL associated with the given HCTL.
***************************************************************************/
PCTL CTL::PctlFromHctl(HCTL hctl)
{
#ifdef MAC
    return (PCTL)GetCRefCon(hctl);
#endif // 3DMMv1.0: MAC
#ifdef WIN
    return (PCTL)GetProp(hctl, _szCtlProp);
#endif // 3DMMv1.0: WIN
}

/** 3DMMv1.0: *************************************************************************
    The control may have been moved - move the OS control.
***************************************************************************/
void CTL::_NewRc(void)
{
#ifdef WIN
    RC rc;
    RECT rcs;
    HWND hwnd;

    if (_hctl == hNil)
        return;

    hwnd = _HwndGetRc(&rc);
    Assert(hwnd != hNil, "control isn't based in an hwnd");
    rcs = RCS(rc);
#endif
#ifdef MAC
    RCS rcsOld = (*_hctl)->contrlRect;

    if (!EqualRect(&rcs, &rcsOld))
    {
        GNV gnv(this);

        // 3DMMv1.0: clip out everything - hide it, then move and size it
        // 3DMMv1.0: don't make it visible again - we do that in the Draw
        // 3DMMv1.0: routine
        rc.Zero();
        gnv.ClipRc(&rc);
        gnv.Set();
        HideControl(_hctl);
        MoveControl(_hctl, rcs.left, rcs.top);
        SizeControl(_hctl, rcs.right - rcs.left, rcs.bottom - rcs.top);
        gnv.Restore();
    }
#endif // 3DMMv1.0: MAC
#ifdef WIN
    MoveWindow(_hctl, rcs.left, rcs.top, rcs.right - rcs.left, rcs.bottom - rcs.top, fFalse);
    InvalidateRect(_hctl, pvNil, fFalse);
#endif // 3DMMv1.0: WIN
}

#ifdef MAC
/** 3DMMv1.0: *************************************************************************
    Draw routine for a control.
***************************************************************************/
void CTL::Draw(PGNV pgnv, RC *prcClip)
{
    if (_hctl == hNil)
        return;
    if (pgnv->Pgpt() == Pgpt())
    {
        pgnv->Set();
        if (!(*_hctl)->contrlVis)
            ShowControl(_hctl);
        else
            Draw1Control(_hctl);
        pgnv->Restore();
    }
    else
    {
        RC rc;
        GNV gnv(this);

        gnv.Set();
        if (!(*_hctl)->contrlVis)
            ShowControl(_hctl);
        else
            Draw1Control(_hctl);
        gnv.Restore();
        GetRcVis(&rc, cooLocal);
        pgnv->CopyPixels(&gnv, &rc, &rc);
    }
}
#endif // 3DMMv1.0: MAC

/** 3DMMv1.0: *************************************************************************
    Static method to create a scroll bar.
***************************************************************************/
PSCB SCB::PscbNew(PGCB pgcb, uint32_t grfscb, int32_t val, int32_t valMin, int32_t valMax)
{
    Assert(FPure(grfscb & fscbHorz) != FPure(grfscb & fscbVert), "exactly one of (fscbHorz,fscbVert) should be set");
    PSCB pscb;
    GCB gcb;

    if (grfscb & fscbStandardRc)
    {
        gcb.Set(pgcb->_hid, pgcb->_pgob, pgcb->_grfgob, pgcb->_gin);
        GetStandardRc(grfscb, &gcb._rcAbs, &gcb._rcRel);
        pgcb = &gcb;
    }

    if (pvNil == (pscb = NewObj SCB(pgcb)))
        return pvNil;

    if (!pscb->_FCreate(val, valMin, valMax, grfscb))
        ReleasePpo(&pscb);

    return pscb;
}

/** 3DMMv1.0: *************************************************************************
    Static method to return the normal width of a vertical scroll bar.
***************************************************************************/
int32_t SCB::DxpNormal(void)
{
#ifdef WIN
    static int _dxp = 0;

    if (_dxp > 0)
        return _dxp;
    return (_dxp = GetSystemMetrics(SM_CXVSCROLL));
#else // 3DMMv1.0: WIN
    return 16;
#endif
}

/** 3DMMv1.0: *************************************************************************
    Static method to return the normal width of a horizontal scroll bar.
***************************************************************************/
int32_t SCB::DypNormal(void)
{
#ifdef WIN
    static int _dyp = 0;

    if (_dyp > 0)
        return _dyp;
    return (_dyp = GetSystemMetrics(SM_CYHSCROLL));
#else // 3DMMv1.0: WIN
    return 16;
#endif
}

/** 3DMMv1.0: *************************************************************************
    Get the standard rectangles for document window scroll bars.  grfscb
    should contain fscbHorz or fscbVert.
***************************************************************************/
void SCB::GetStandardRc(uint32_t grfscb, RC *prcAbs, RC *prcRel)
{
    if (FPure(grfscb & fscbVert))
    {
        prcRel->ypTop = krelZero;
        prcRel->xpLeft = prcRel->xpRight = prcRel->ypBottom = krelOne;
        prcAbs->ypTop = -!(grfscb & fscbShowTop);
        prcAbs->xpLeft = -SCB::DxpNormal() + !(grfscb & fscbShowRight);
        prcAbs->xpRight = !(grfscb & fscbShowRight);
        prcAbs->ypBottom = -SCB::DypNormal() + 1 + !(grfscb & fscbShowBottom);
    }
    else
    {
        prcRel->xpLeft = krelZero;
        prcRel->ypTop = prcRel->xpRight = prcRel->ypBottom = krelOne;
        prcAbs->ypTop = -SCB::DypNormal() + !(grfscb & fscbShowBottom);
        prcAbs->xpLeft = -!(grfscb & fscbShowLeft);
        prcAbs->xpRight = -SCB::DxpNormal() + 1 + !(grfscb & fscbShowRight);
        prcAbs->ypBottom = !(grfscb & fscbShowBottom);
    }
}

/** 3DMMv1.0: *************************************************************************
    Get the standard client window rectangle (assuming the given set of
    scroll bars).
***************************************************************************/
void SCB::GetClientRc(uint32_t grfscb, RC *prcAbs, RC *prcRel)
{
    prcRel->ypTop = prcRel->xpLeft = krelZero;
    prcRel->ypBottom = prcRel->xpRight = krelOne;
    prcAbs->Zero();
    if (grfscb & fscbVert)
        prcAbs->xpRight = -SCB::DxpNormal() + !(grfscb & fscbShowRight);
    if (grfscb & fscbHorz)
        prcAbs->ypBottom = -SCB::DypNormal() + !(grfscb & fscbShowBottom);
}

/** 3DMMv1.0: *************************************************************************
    Create the actual system scroll bar.
***************************************************************************/
bool SCB::_FCreate(int32_t val, int32_t valMin, int32_t valMax, uint32_t grfscb)
{
    Assert(_Hctl() == hNil, "scb already created");
    bool redraw = fFalse;
#ifdef WIN
    RC rc;
    HWND hwnd;
    HCTL hctl;

    _fVert = FPure(grfscb & fscbVert);
    if ((hwnd = _HwndGetRc(&rc)) == hNil)
    {
        Bug("can only add controls to hwnd based gobs");
        return fFalse;
    }
#endif
#ifdef MAC
    GNV gnv(this);
    gnv.Set();
    hctl = NewControl(&hwnd->port, &rcs, (uint8_t *)"\p", fTrue, 0, 0, 0, scrollBarProc, 0);
    gnv.Restore();
    if (hctl == hNil || !_FSetHctl(hctl))
        return fFalse;
    ValidRc(pvNil);
    redraw = true;
#endif // 3DMMv1.0: MAC
#ifdef WIN
    RECT rcs(rc);
    hctl = CreateWindow(PszLit("SCROLLBAR"), PszLit(""), (_fVert ? SBS_VERT : SBS_HORZ) | WS_CHILD | WS_VISIBLE,
                        rcs.left, rcs.top, rcs.right - rcs.left, rcs.bottom - rcs.top, hwnd, hNil, vwig.hinst, pvNil);
    if (hctl == hNil || !_FSetHctl(hctl))
        return fFalse;
    SetScrollRange(hctl, SB_CTL, 0, _klwMaxScroll, fFalse);
    SetScrollPos(hctl, SB_CTL, 0, fFalse);
#endif // 3DMMv1.0: WIN

    SetValMinMax(val, valMin, valMax, redraw);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Set the value of the scroll bar.
***************************************************************************/
void SCB::SetVal(int32_t val, bool fRedraw)
{
    int32_t lwCur;

    val = LwMin(_valMax, LwMax(_valMin, val));
    if (val == _val)
        return;
    _val = val;
    lwCur = LwMulDiv(_klwMaxScroll, _val - _valMin, LwMax(1, _valMax - _valMin));

#ifdef MAC
    // 3DMMv1.0: REVIEW shonk: Mac: implement fRedraw false
    GNV gnv(this);
    gnv.Set();
    SetCtlValue(_Hctl(), (short)lwCur);
    gnv.Restore();
#endif // 3DMMv1.0: MAC
#ifdef WIN
    SetScrollPos(_Hctl(), SB_CTL, lwCur, fRedraw);
#endif // 3DMMv1.0: WIN
}

/** 3DMMv1.0: *************************************************************************
    Set the min and max of the scroll bar.
***************************************************************************/
void SCB::SetValMinMax(int32_t val, int32_t valMin, int32_t valMax, bool fRedraw)
{
    int32_t lwCur;

    valMax = LwMax(valMin, valMax);
    val = LwMin(valMax, LwMax(valMin, val));

    if (val == _val && valMin == _valMin && valMax == _valMax)
        return;
    _val = val;
    _valMin = valMin;
    _valMax = valMax;
    lwCur = LwMulDiv(_klwMaxScroll, _val - _valMin, LwMax(1, _valMax - _valMin));

#ifdef MAC
    // 3DMMv1.0: REVIEW shonk: Mac: implement fRedraw false
    GNV gnv(this);
    gnv.Set();
    if (_valMax == _valMin)
        SetCtlMax(_Hctl(), 0);
    else
        SetCtlMax(_Hctl(), (short)_klwMaxScroll);
    SetCtlValue(_Hctl(), (short)lwCur);
    gnv.Restore();
#endif // 3DMMv1.0: MAC
#ifdef WIN
    SetScrollPos(_Hctl(), SB_CTL, lwCur, fRedraw);
#endif // 3DMMv1.0: WIN
}

#ifdef MAC
/** 3DMMv1.0: *************************************************************************
    The hwnd has been activated or deactivated - redraw and validate.
***************************************************************************/
void SCB::_ActivateHwnd(bool fActive)
{
    if (_valMin < _valMax)
    {
        GNV gnv(this);
        int32_t lwCur;

        gnv.Set();
        SetCtlMax(_Hctl(), (short)(fActive ? _klwMaxScroll : 0));
        if (fActive)
        {
            lwCur = LwMulDiv(_klwMaxScroll, _val - _valMin, LwMax(1, _valMax - _valMin));
            SetCtlValue(_Hctl(), (short)lwCur);
        }
        gnv.Restore();
        if (!(*_Hctl())->contrlVis)
        {
            ValidRc(pvNil, kginDraw);
            InvalRc(pvNil, kginDraw); // 3DMMv1.0: this makes it visible
        }
    }
}

/** 3DMMv1.0: *************************************************************************
    Handle mouse tracking for a scroll bar.
***************************************************************************/
void SCB::MouseDown(int32_t xp, int32_t yp, int32_t cact, uint32_t grfcust)
{
    PTS pts;
    short in;
    PT pt;
    bool fDown, fLit;
    CMD cmd;

    GNV gnv(this);

    pts.h = (short)xp + (*_Hctl())->contrlRect.left;
    pts.v = (short)yp + (*_Hctl())->contrlRect.top;
    in = TestControl(_Hctl(), pts);
    switch (in)
    {
    default:
        break;

    case inThumb:
        gnv.Set();
        in = TrackControl(_Hctl(), pts, pvNil);
        gnv.Restore();

        // 3DMMv1.0: send the final position - this will get recorded
        // 3DMMv1.0: note that the _Hctl() has the wrong value so the
        // 3DMMv1.0: gob should call SetValue in response to this command
        vpcex->EnqueueCid(cidEndScroll, PgobPar(), pvNil, Hid(),
                          _valMin + LwMulDiv((int32_t)GetCtlValue(_Hctl()), _valMax - _valMin, _klwMaxScroll));
        break;

    case inUpButton:
    case inDownButton:
    case inPageUp:
    case inPageDown:
        fDown = fTrue;
        pt.xp = xp;
        pt.yp = yp;
        for (;; GetPtMouse(&pt, &fDown))
        {
            // 3DMMv1.0: get the new hilite state
            pts.h = (short)pt.xp + (*_Hctl())->contrlRect.left;
            pts.v = (short)pt.yp + (*_Hctl())->contrlRect.top;
            fLit = fDown && (TestControl(_Hctl(), pts) == in);

            // 3DMMv1.0: hilite the scroll bar appropriately
            gnv.Set();
            if (fLit)
                HiliteControl(_Hctl(), in);
            else
                HiliteControl(_Hctl(), 0);
            gnv.Restore();

            // 3DMMv1.0: see if we're done
            if (!fDown)
                break;

            if (fLit)
            {
                // 3DMMv1.0: send the command - this doesn't get recorded
                ClearPb(&cmd, SIZEOF(cmd));
                cmd.cid = cidDoScroll;
                cmd.pcmh = PgobPar();
                cmd.rglw[0] = Hid();
                switch (in)
                {
                case inUpButton:
                    cmd.rglw[1] = scaLineUp;
                    break;
                case inPageUp:
                    cmd.rglw[1] = scaPageUp;
                    break;
                case inDownButton:
                    cmd.rglw[1] = scaLineDown;
                    break;
                case inPageDown:
                    cmd.rglw[1] = scaPageDown;
                    break;
                default:
                    BugVar("what is the in value?", &in);
                    cmd.rglw[1] = scaNil;
                    break;
                }
                cmd.pcmh->FDoCmd(&cmd);
            }
        }

        // 3DMMv1.0: send the final position - this will get recorded
        vpcex->EnqueueCid(cidEndScroll, PgobPar(), pvNil, Hid(), _val);
        break;
    }
}
#endif // 3DMMv1.0: MAC

#ifdef WIN
/** 3DMMv1.0: *************************************************************************
    Called in response to a Win WM_HSCROLL or WM_VSCROLL message.
***************************************************************************/
void SCB::TrackScroll(int32_t sb, int32_t lwVal)
{
    AssertThis(0);
    CMD cmd;
    int32_t val;

    ClearPb(&cmd, SIZEOF(cmd));
    cmd.cid = cidDoScroll;
    cmd.pcmh = PgobPar();
    cmd.rglw[0] = Hid();

    switch (sb)
    {
    case SB_LINEUP:
        cmd.rglw[1] = scaLineUp;
        break;
    case SB_LINEDOWN:
        cmd.rglw[1] = scaLineDown;
        break;

    case SB_PAGEUP:
        cmd.rglw[1] = scaPageUp;
        break;
    case SB_PAGEDOWN:
        cmd.rglw[1] = scaPageDown;
        break;
    case SB_THUMBTRACK:
        lwVal = LwMax(0, LwMin(_klwMaxScroll, lwVal));
        cmd.rglw[1] = scaToVal;
        cmd.rglw[2] = _valMin + LwMulDiv(lwVal, _valMax - _valMin, _klwMaxScroll);
        break;

    // 3DMMv1.0: these values just push an end-scroll command
    case SB_THUMBPOSITION:
        _fSentEndScroll = fFalse;
        lwVal = LwMax(0, LwMin(_klwMaxScroll, lwVal));
        val = _valMin + LwMulDiv(lwVal, _valMax - _valMin, _klwMaxScroll);
        goto LEndScroll;
    case SB_TOP:
        _fSentEndScroll = fFalse;
        val = _valMin;
        goto LEndScroll;
    case SB_BOTTOM:
        _fSentEndScroll = fFalse;
        val = _valMax;
        goto LEndScroll;
    case SB_ENDSCROLL:
        // 3DMMv1.0: NOTE: _fSentEndScroll is so we don't send another EndScroll
        // 3DMMv1.0: here if we've already sent one (in response to an SB_THUMBPOSITION,
        // 3DMMv1.0: SB_TOP or SB_BOTTOM), since this value will generally be wrong.
        val = _val;
        goto LEndScroll;

    default:
        // 3DMMv1.0: do nothing
        return;
    }

    _fSentEndScroll = fFalse;
    cmd.pcmh->FDoCmd(&cmd);
    return;

LEndScroll:
    if (!_fSentEndScroll)
    {
        cmd.cid = cidEndScroll;
        cmd.rglw[1] = val;
        vpcex->EnqueueCmd(&cmd);
        _fSentEndScroll = fTrue;
    }
}
#endif // 3DMMv1.0: WIN

/** 3DMMv1.0: *************************************************************************
    Static method to create a window size box.
***************************************************************************/
PWSB WSB::PwsbNew(PGOB pgob, uint32_t grfgob)
{
    RC rcRel, rcAbs;
    PWSB pwsb;

    rcRel.xpLeft = rcRel.xpRight = rcRel.ypTop = rcRel.ypBottom = krelOne;
    rcAbs.xpLeft = -SCB::DxpNormal() + 1;
    rcAbs.ypTop = -SCB::DypNormal() + 1;
    rcAbs.xpRight = rcAbs.ypBottom = 1;

    GCB gcb(khidSizeBox, pgob, grfgob, kginDefault, &rcAbs, &rcRel);
    if ((pwsb = NewObj WSB(&gcb)) == pvNil)
        return pvNil;

    Assert(pwsb->PgobPar() != pvNil, "nil parent");
    Assert(pwsb->PgobPar()->Hwnd() != hNil, "parent of size box doesn't have an hwnd");

#ifdef WIN
    RC rc;
    RECT rcs;
    HWND hwnd;
    HCTL hctl;

    hwnd = pwsb->_HwndGetRc(&rc);
    rcs = RCS(rc);

    hctl = CreateWindow(PszLit("SCROLLBAR"), PszLit(""), SBS_SIZEBOX | WS_CHILD | WS_VISIBLE, rcs.left, rcs.top,
                        rcs.right - rcs.left, rcs.bottom - rcs.top, hwnd, hNil, vwig.hinst, pvNil);

    if (hctl == hNil || !pwsb->_FSetHctl(hctl))
        ReleasePpo(&pwsb);
#endif

    return pwsb;
}

#ifdef MAC
/** 3DMMv1.0: *************************************************************************
    Draw the size box icon.
***************************************************************************/
void WSB::Draw(PGNV pgnv, RC *prcClip)
{
    HWND hwnd;
    RC rc;

    hwnd = PgobPar()->Hwnd();
    Assert(hwnd != hNil, "hwnd nil");
    GetRcVis(&rc, cooLocal);

    if (pgnv->Pgpt() == Pgpt())
    {
        pgnv->ClipRc(&rc);
        pgnv->Set();
        DrawGrowIcon(&hwnd->port);
        pgnv->Restore();
    }
    else
    {
        GNV gnv(this);

        gnv.ClipRc(&rc);
        gnv.Set();
        DrawGrowIcon(&hwnd->port);
        gnv.Restore();
        pgnv->CopyPixels(&gnv, &rc, &rc);
    }
}

/** 3DMMv1.0: *************************************************************************
    The hwnd has been activated or deactivated - redraw and validate.
***************************************************************************/
void WSB::_ActivateHwnd(bool fActive)
{
    ValidRc(pvNil, kginDraw);
    InvalRc(pvNil, kginDraw);
}
#endif // 3DMMv1.0: MAC
