/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
/** 3DMMv1.0: ***************************************************************************\
 *	stdioscb.cpp
 *
 *	Author: ******
 *	Date: March, 1995
 *
 *	This file contains the studio scrollbars class SSCB.  These are the frame
 *	and scene scrollbar master controls.
 *
\*****************************************************************************/

#include "soc.h"
#include "studio.h"
ASSERTNAME

#if defined(KAUAI_WIN32)
namespace
{
const achar kszCreateFramesWndClass[] = "3DMMExCreateFramesDialog";
const int32_t kidCreateFramesCount = 0x6201;
const int32_t kidCreateFramesBlank = 0x6202;
const int32_t kidCreateFramesOk = 0x6203;
const int32_t kidCreateFramesCancel = 0x6204;

struct CREATEFRAMESDLG
{
    bool fDone;
    bool fAccepted;
    int32_t cfrm;
    bool fBlank;
};

LRESULT CALLBACK LresultCreateFramesWndProc(HWND hwnd, UINT wm,
                                             WPARAM wParam, LPARAM lParam)
{
    CREATEFRAMESDLG *pdlg =
        (CREATEFRAMESDLG *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch (wm)
    {
    case WM_NCCREATE:
        SetWindowLongPtrA(hwnd, GWLP_USERDATA,
                          (LONG_PTR)((CREATESTRUCTA *)lParam)->lpCreateParams);
        return fTrue;

    case WM_CREATE: {
        HFONT hfont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND hwndEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "1",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
            12, 12, 232, 24, hwnd, (HMENU)kidCreateFramesCount,
            GetModuleHandleA(pvNil), pvNil);
        HWND hwndBlank = CreateWindowExA(0, "BUTTON", "Create empty frames",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            12, 45, 232, 22, hwnd, (HMENU)kidCreateFramesBlank,
            GetModuleHandleA(pvNil), pvNil);
        HWND hwndOk = CreateWindowExA(0, "BUTTON", "OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            62, 78, 80, 26, hwnd, (HMENU)kidCreateFramesOk,
            GetModuleHandleA(pvNil), pvNil);
        HWND hwndCancel = CreateWindowExA(0, "BUTTON", "Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            152, 78, 80, 26, hwnd, (HMENU)kidCreateFramesCancel,
            GetModuleHandleA(pvNil), pvNil);
        SendMessageA(hwndEdit, WM_SETFONT, (WPARAM)hfont, fTrue);
        SendMessageA(hwndBlank, WM_SETFONT, (WPARAM)hfont, fTrue);
        SendMessageA(hwndOk, WM_SETFONT, (WPARAM)hfont, fTrue);
        SendMessageA(hwndCancel, WM_SETFONT, (WPARAM)hfont, fTrue);
        SendMessageA(hwndEdit, EM_SETSEL, 0, -1);
        SetFocus(hwndEdit);
        return 0;
    }

    case WM_COMMAND:
        if (pdlg == pvNil)
            return 0;
        if (LOWORD(wParam) == kidCreateFramesOk &&
            HIWORD(wParam) == BN_CLICKED)
        {
            achar rgch[32];
            GetWindowTextA(GetDlgItem(hwnd, kidCreateFramesCount),
                           rgch, SIZEOF(rgch));
            achar *pszEnd = pvNil;
            long cfrm = strtol(rgch, &pszEnd, 10);
            while (pszEnd != pvNil && *pszEnd == ' ')
                pszEnd++;
            if (cfrm <= 0 || cfrm >= klwMax ||
                pszEnd == rgch || (pszEnd != pvNil && *pszEnd != 0))
            {
                MessageBoxA(hwnd, "Enter a whole number greater than zero.",
                            "Create how many frame?", MB_OK | MB_ICONEXCLAMATION);
                SetFocus(GetDlgItem(hwnd, kidCreateFramesCount));
                SendMessageA(GetDlgItem(hwnd, kidCreateFramesCount),
                             EM_SETSEL, 0, -1);
                return 0;
            }
            pdlg->cfrm = (int32_t)cfrm;
            pdlg->fBlank =
                SendMessageA(GetDlgItem(hwnd, kidCreateFramesBlank),
                             BM_GETCHECK, 0, 0) == BST_CHECKED;
            pdlg->fAccepted = fTrue;
            pdlg->fDone = fTrue;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == kidCreateFramesCancel &&
            HIWORD(wParam) == BN_CLICKED)
        {
            pdlg->fDone = fTrue;
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        if (pdlg != pvNil)
            pdlg->fDone = fTrue;
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProcA(hwnd, wm, wParam, lParam);
}

bool FGetCreateFrameCount(HWND hwndParent, int32_t *pcfrm, bool *pfBlank)
{
    AssertVarMem(pcfrm);
    AssertVarMem(pfBlank);

    static bool fRegistered = fFalse;
    if (!fRegistered)
    {
        WNDCLASSA wc;
        ClearPb(&wc, SIZEOF(wc));
        wc.lpfnWndProc = LresultCreateFramesWndProc;
        wc.hInstance = GetModuleHandleA(pvNil);
        wc.hCursor = LoadCursorA(hNil, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kszCreateFramesWndClass;
        if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return fFalse;
        fRegistered = fTrue;
    }

    CREATEFRAMESDLG dlg;
    ClearPb(&dlg, SIZEOF(dlg));
    RECT rcParent;
    GetWindowRect(hwndParent, &rcParent);

    // The child controls are authored in client coordinates through y=104.
    // The old code treated 270x145 as an outer-window size, so the caption
    // and dialog frame consumed part of that height. On current Windows that
    // can leave the client area ending directly below the checkbox, clipping
    // both OK and Cancel. Size the non-client shell around the client area
    // instead, exactly like the other native 4DMM auxiliary dialogs.
    const DWORD dwStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD dwExStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    RECT rcWindow = {0, 0, 256, 116};
    AdjustWindowRectEx(&rcWindow, dwStyle, fFalse, dwExStyle);
    const int32_t dxp = rcWindow.right - rcWindow.left;
    const int32_t dyp = rcWindow.bottom - rcWindow.top;
    int32_t xp = rcParent.left + ((rcParent.right - rcParent.left) - dxp) / 2;
    int32_t yp = rcParent.top + ((rcParent.bottom - rcParent.top) - dyp) / 2;

    HWND hwnd = CreateWindowExA(dwExStyle,
        kszCreateFramesWndClass, "Create how many frame?",
        dwStyle, xp, yp, dxp, dyp, hwndParent,
        hNil, GetModuleHandleA(pvNil), &dlg);
    if (hwnd == hNil)
        return fFalse;

    EnableWindow(hwndParent, fFalse);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (!dlg.fDone && GetMessageA(&msg, hNil, 0, 0) > 0)
    {
        // This is a custom modal window rather than a dialog-template HWND,
        // so give Enter/Escape the ordinary dialog semantics explicitly.
        // Handle them before IsDialogMessage so the focused edit/checkbox
        // cannot swallow either key.
        if (msg.message == WM_KEYDOWN &&
            (msg.hwnd == hwnd || IsChild(hwnd, msg.hwnd)))
        {
            if (msg.wParam == VK_RETURN)
            {
                SendMessageA(hwnd, WM_COMMAND,
                             MAKEWPARAM(kidCreateFramesOk, BN_CLICKED),
                             (LPARAM)GetDlgItem(hwnd, kidCreateFramesOk));
                continue;
            }
            if (msg.wParam == VK_ESCAPE)
            {
                SendMessageA(hwnd, WM_COMMAND,
                             MAKEWPARAM(kidCreateFramesCancel, BN_CLICKED),
                             (LPARAM)GetDlgItem(hwnd, kidCreateFramesCancel));
                continue;
            }
        }
        if (!IsDialogMessageA(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    if (IsWindow(hwnd))
        DestroyWindow(hwnd);
    EnableWindow(hwndParent, fTrue);
    SetActiveWindow(hwndParent);

    if (!dlg.fAccepted)
        return fFalse;
    *pcfrm = dlg.cfrm;
    *pfBlank = dlg.fBlank;
    return fTrue;
}
}
#endif // KAUAI_WIN32


/** 3DMMv1.0: ***************************************************************************\
 *
 *	The studio scrollbars class.
 *
\*****************************************************************************/

RTCLASS(SSCB)

/** 3DMMv1.0: ***************************************************************************\
 *
 *	Constructor for the studio scrollbars.  This function is private; use
 *	PsscbNew() for public construction.
 *
\*****************************************************************************/
SSCB::SSCB(PMVIE pmvie)
{
    _pmvie = pmvie;
    _fBtnAddsFrames = fFalse;
    _fBtnInsertBefore = fFalse;
    _fBtnInsertBlank = fFalse;
    _fBtnBatchInsert = fFalse;
    _fBtnNativeEnd = fFalse;
    _fBtnNativeStartBlank = fFalse;
#ifdef SHOW_FPS
    _itsNext = 1;
    for (int32_t its = 0; its < kctsFps; its++)
        _rgfdsc[its].ts = 0;
#endif // 3DMMv1.0: SHOW_FPS
}

/** 3DMMv1.0: ***************************************************************************\
 *
 *	Public constructor for the studio scrollbars.
 *
 *	Parameters:
 *		pmvie	-- the owner movie
 *
 *	Returns:
 *		A pointer to the scrollbars object, pvNil if failed.
 *
\*****************************************************************************/
PSSCB SSCB::PsscbNew(PMVIE pmvie)
{
    AssertNilOrPo(pmvie, 0);

    PSSCB psscb;
    PGOB pgob;
    STN stn;
    GCB gcb;
    RC rcRel, rcAbs;
    int32_t hid;

    //
    // 3DMMv1.0: Create the view
    //
    if (pvNil == (psscb = NewObj SSCB(pmvie)))
        return pvNil;

    rcRel.xpLeft = rcRel.ypTop = 0;
    rcRel.xpRight = rcRel.ypBottom = krelOne;

    rcAbs.Set(0, 0, 0, 0);

    pgob = ((APP *)vpappb)->Pkwa()->PgobFromHid(kidFrameText);

    if (pgob == pvNil)
    {
        ReleasePpo(&psscb);
        return (pvNil);
    }

    hid = GOB::HidUnique();
    gcb.Set(hid, pgob, fgobNil, kginDefault, &rcAbs, &rcRel);

    if (pvNil == (psscb->_ptgobFrame = NewObj TGOB(&gcb)))
    {
        ReleasePpo(&psscb);
        return (pvNil);
    }

    pgob = ((APP *)vpappb)->Pkwa()->PgobFromHid(kidSceneText);

    if (pgob == pvNil)
    {
        ReleasePpo(&psscb);
        return (pvNil);
    }

    hid = GOB::HidUnique();
    gcb.Set(hid, pgob, fgobNil, kginDefault, &rcAbs, &rcRel);

    if (pvNil == (psscb->_ptgobScene = NewObj TGOB(&gcb)))
    {
        ReleasePpo(&psscb);
        return (pvNil);
    }

#ifdef SHOW_FPS
    pgob = ((APP *)vpappb)->Pkwa()->PgobFromHid(kidFps);

    if (pgob == pvNil)
    {
        ReleasePpo(&psscb);
        return (pvNil);
    }

    hid = GOB::HidUnique();
    gcb.Set(hid, pgob, fgobNil, kginDefault, &rcAbs, &rcRel);

    if (pvNil == (psscb->_ptgobFps = NewObj TGOB(&gcb)))
    {
        ReleasePpo(&psscb);
        return (pvNil);
    }
#endif // 3DMMv1.0: SHOW_FPS

    AssertPo(psscb, 0);
    return psscb;
}

/** 3DMMv1.0: **************************************************
 *
 * Destructor for studio scroll bars.
 *
 ****************************************************/
SSCB::~SSCB(void)
{
    AssertBaseThis(0);

    // 3DMMv1.0: Don't need to Release these tgobs, since they get destroyed with
    // 3DMMv1.0: the gob tree.
    _ptgobFrame = pvNil;
    _ptgobScene = pvNil;
#ifdef SHOW_FPS
    _ptgobFps = pvNil;
#endif // 3DMMv1.0: SHOW_FPS
}

/** 3DMMv1.0: ***************************************************************************\
 *
 *	FCmdScroll
 *		Handles scrollbar commands.  Cids to the SSCB are enqueued
 *		in the following format:
 *
 *		EnqueueCid(cid, khidSscb, chtt, param1, param2, param3);
 *
 *		where: cid  = cidFrameScrollbar or cidSceneScrollbar
 *		       chtt = the tool type
 *
 *	Parameters:
 *		pcmd -- pointer to command info
 *
 *	Returns:
 *		fTrue if the command was handled
 *
\*****************************************************************************/
bool SSCB::FCmdScroll(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    bool fScene;
    bool fThumbDrag = fFalse;
    bool fNativeInsert = fFalse;
    bool fNativeInsertBefore = fFalse;
    int32_t lwDest, cxScrollbar, lwDestOld, xp;
    int32_t cRangeDest, iRangeDestFirst;
    int32_t tool = -1;
    RC rc;
    PSCEN pscen = pvNil;

    // 3DMMv1.0: verify that the command is for the studio scrollbars
    if ((pcmd->cid != cidSceneScrollbar) && (pcmd->cid != cidFrameScrollbar) && (pcmd->cid != cidSceneThumb) &&
        (pcmd->cid != cidFrameThumb) && (pcmd->cid != cidStartScroll))
        return fFalse;

    fScene = ((pcmd->cid == cidSceneScrollbar) || (pcmd->cid == cidSceneThumb));

    if (pcmd->cid == cidFrameScrollbar)
    {
        vpcex->FlushCid(cidFrameScrollbar);
    }

    // 3DMMv1.0: need a valid movie ptr, also a ptr to the current scene
    // 3DMMv1.0: when dealing with frame scrollbar
    if ((pvNil == _pmvie) || (!fScene && (pvNil == (pscen = _pmvie->Pscen()))))
        return fTrue;

    if (pcmd->cid == cidStartScroll)
    {
        if ((pcmd->rglw[0] == chttFButtonFW) ||
            (pcmd->rglw[0] == chttFButtonRW))
        {
            uint32_t grfcust = (uint32_t)pcmd->rglw[1];
            bool fCmd = FPure(grfcust & fcustCmd);
            bool fBefore = FPure(pcmd->rglw[0] == chttFButtonRW);
            bool fBlank = FPure(grfcust & fcustShift);
            bool fBatch = FPure(grfcust & fcustOption);

            _fBtnInsertBefore = fBefore;
            _fBtnInsertBlank = fBlank;
            _fBtnBatchInsert = fBatch;
            _fBtnNativeEnd = fCmd && !fBatch && !fBlank && !fBefore &&
                             !_pmvie->FManualCameraMode() &&
                             pscen->Nfrm() == pscen->NfrmLast();
            _fBtnNativeStartBlank = fCmd && !fBatch && fBlank && fBefore &&
                                    !_pmvie->FManualCameraMode() &&
                                    pscen->Nfrm() == pscen->NfrmFirst();
            _fBtnAddsFrames = fCmd;
            StartNoAutoadjust();

            if (_fBtnNativeEnd || _fBtnNativeStartBlank)
            {
                // Restore the two edge gestures to the original 3DMM command
                // flow.  The switch below will execute the same
                // FGotoFrm(current +/- 1) path as the unmodified program.
                if (!_pmvie->FPrepareNativeFrameInsert(_fBtnNativeStartBlank))
                {
                    _fBtnAddsFrames = fFalse;
                    _fBtnNativeEnd = fFalse;
                    _fBtnNativeStartBlank = fFalse;
                    return fTrue;
                }
            }
            else
            {
                // Middle, blank-end and batch insertion are separate paths.
                return fTrue;
            }
        }
        else
        {
            return fTrue;
        }
    }
    else
    {
        EndNoAutoadjust();
    }

    // BUTTONHOLDGOB sends cidStartScroll first, then the real frame-scroll
    // command. Perform exactly one insertion on that first real command.
    if (_fBtnAddsFrames && pcmd->cid == cidFrameScrollbar &&
        ((pcmd->rglw[0] == chttFButtonFW) ||
         (pcmd->rglw[0] == chttFButtonRW)))
    {
        bool fBefore = _fBtnInsertBefore;
        bool fBlank = _fBtnInsertBlank;
        bool fBatch = _fBtnBatchInsert;
        _fBtnAddsFrames = fFalse;
        _fBtnInsertBefore = fFalse;
        _fBtnInsertBlank = fFalse;
        _fBtnBatchInsert = fFalse;
        _fBtnNativeEnd = fFalse;
        _fBtnNativeStartBlank = fFalse;

        int32_t cfrm = 1;
#if defined(KAUAI_WIN32)
        if (fBatch && !FGetCreateFrameCount(vwig.hwndApp, &cfrm, &fBlank))
            return fTrue;
#else
        if (fBatch)
            return fTrue;
#endif

        bool fInserted;
        bool fAtEnd = !fBefore &&
                      pscen->Nfrm() == pscen->NfrmLast();
        bool fAtStartBlank = fBefore && fBlank &&
                             pscen->Nfrm() == pscen->NfrmFirst();
        if (_pmvie->FManualCameraMode())
        {
            // Manual Camera Ctrl is its own save-and-advance transaction.
            // Do not bypass it through either native edge shortcut.
            fInserted = cfrm == 1 && !fBefore && !fBlank &&
                        _pmvie->FInsertFramesRelative(fFalse, fFalse, 1);
        }
        else if (fAtEnd)
            fInserted = _pmvie->FAddNativeEndFrames(cfrm, fBlank);
        else if (fAtStartBlank)
            fInserted = _pmvie->FAddNativeStartBlankFrames(cfrm);
        else
            fInserted = _pmvie->FInsertFramesRelative(fBefore, fBlank, cfrm);

        if (fInserted)
        {
            _pmvie->Pmcc()->PlayUISound(toolAddAFrame);
            Update();
        }
        return fTrue;
    }

    // 3DMMv1.0: what's the tool we are handling? (chtt is param0)
    switch (pcmd->rglw[0])
    {

    case chttFButtonFW:
    case chttSButtonFW:
        // 3DMMv1.0: forward one frame / scene
        if (fScene)
        {
            lwDest = _pmvie->Iscen() + 1;
            tool = toolFWAScene;
        }
        else
        {
            lwDest = pscen->Nfrm() + 1;
            if ((pscen->Nfrm() == pscen->NfrmLast()) &&
                _fBtnAddsFrames && _fBtnNativeEnd)
            {
                _fBtnAddsFrames = fFalse;
                _fBtnNativeEnd = fFalse;
                _fBtnNativeStartBlank = fFalse;
                fNativeInsert = fTrue;
                fNativeInsertBefore = fFalse;
                tool = toolAddAFrame;
                goto LExecuteCmd;
            }
            tool = toolFWAFrame;
        }
        break;

    case chttButtonFWEnd:
        // 3DMMv1.0: forward to end of scene / movie
        lwDest = klwMax;
        break;

    case chttFButtonRW:
    case chttSButtonRW:
        // 3DMMv1.0: back one frame / scene
        if (fScene)
        {
            lwDest = _pmvie->Iscen() - 1;
            tool = toolRWAScene;
        }
        else
        {
            lwDest = pscen->Nfrm() - 1;
            if ((pscen->Nfrm() == pscen->NfrmFirst()) &&
                _fBtnAddsFrames && _fBtnNativeStartBlank)
            {
                _fBtnAddsFrames = fFalse;
                _fBtnNativeEnd = fFalse;
                _fBtnNativeStartBlank = fFalse;
                fNativeInsert = fTrue;
                fNativeInsertBefore = fTrue;
                tool = toolAddAFrame;
                goto LExecuteCmd;
            }
            tool = toolRWAFrame;
        }
        break;

    case chttButtonRWEnd:
        // 3DMMv1.0: back to beginning of scene / movie
        lwDest = klwMin;
        break;

    case chttThumb:
        // 3DMMv1.0: released thumb tab - simulate a hit on scrollbar at this point
        // 3DMMv1.0: fall through

    case chttScrollbar:
        // 3DMMv1.0: hit scrollbar directly

        // 3DMMv1.0: calculate the source range of the mapping: mouse range along scrollbar
        cxScrollbar =
            _CxScrollbar(fScene ? kidSceneScrollbar : kidFrameScrollbar, fScene ? kidSceneThumb : kidFrameThumb);

        // 3DMMv1.0: param1 is the source value: the mouse click position
        // 3DMMv1.0: it should be within the source range (closed on the upper bound)
        xp = LwBound(pcmd->rglw[1], 0, cxScrollbar + 1);

        // 3DMMv1.0: calculate the destination range
        if (fScene)
        {
            // 3DMMv1.0: scenes: [0, ..., number_of_scenes - 1]
            cRangeDest = _pmvie->Cscen();
            iRangeDestFirst = 0;
        }
        else
        {
            // 3DMMv1.0: frames: [first_frame, ..., last_frame]
            cRangeDest = pscen->NfrmLast() - pscen->NfrmFirst() + 1;
            iRangeDestFirst = pscen->NfrmFirst();
        }

        // 3DMMv1.0: map the source value from the source range to the destination range
        lwDest = LwMulDiv(xp, cRangeDest, cxScrollbar) + iRangeDestFirst;

        // 3DMMv1.0: if param2 is 0, we are dragging the thumb tab, just need to update
        // 3DMMv1.0: the counters
        if (pcmd->rglw[2] == 0)
            fThumbDrag = fTrue;
        break;

    default:
        Assert(fFalse, "invalid chtt from studio scroll");
        break;
    }

    // 3DMMv1.0: restrict to valid range
    lwDestOld = lwDest;
    lwDest = LwMin(fScene ? _pmvie->Cscen() - 1 : pscen->NfrmLast(), LwMax(fScene ? 0 : pscen->NfrmFirst(), lwDest));
    if (lwDestOld != lwDest)
    {
        tool = -1;
    }

LExecuteCmd:
    if (!_pmvie->FPlaying())
    {
        STN stn;

        if (fScene)
        {

            if (fThumbDrag)
            {
                // 3DMMv1.0: update scene counter
                stn.FFormatSz(PszLit("%4d"), lwDest + 1);
                _ptgobScene->SetText(&stn);
                return fTrue;
            }
            else
            {
                if (tool != -1)
                {
                    _pmvie->Pmcc()->PlayUISound(tool);
                }

                // 3DMMv1.0: scene change
                if (!_pmvie->FSwitchScen(lwDest))
                {
                    return fFalse;
                }

                if (_pmvie->FSoundsEnabled())
                {
                    _pmvie->Pmsq()->PlayMsq();
                }
                else
                {
                    _pmvie->Pmsq()->FlushMsq();
                }
                Update();
            }
        }
        else
        {
            if (fThumbDrag)
            {
                // 3DMMv1.0: update frame counter
                stn.FFormatSz(PszLit("%4d"), lwDest - pscen->NfrmFirst() + 1);
                _ptgobFrame->SetText(&stn);
                return fTrue;
            }
            else
            {
                // 3DMMv1.0: frame change
                if (tool != -1)
                {
                    _pmvie->Pmcc()->PlayUISound(tool);
                }

                MVIE::LightEditorLog(_pmvie, "frame_seek begin from=%ld to=%ld tool=%ld",
                                     (long)pscen->Nfrm(), (long)lwDest, (long)tool);
                if (!pscen->FGotoFrm(lwDest))
                {
                    MVIE::LightEditorLog(_pmvie, "frame_seek FAILED to=%ld", (long)lwDest);
                    if (fNativeInsert)
                        _pmvie->ClearUndo();
                    return fFalse;
                }

                MVIE::LightEditorLog(_pmvie, "frame_seek complete at=%ld", (long)pscen->Nfrm());

                if (fNativeInsert &&
                    !_pmvie->FCompleteNativeFrameInsert(fNativeInsertBefore))
                {
                    return fFalse;
                }

                // Camera-track seeks change BWLD's camera after the scene has
                // finished replaying its ordinary frame events.  Render the
                // newly selected camera immediately so reverse single-frame
                // stepping and reverse scrollbar seeks cannot keep displaying
                // the previously rendered forward frame.
                if (_pmvie->FCameraTrackActive())
                {
                    _pmvie->Pbwld()->Render();
                }

                if (_pmvie->FSoundsEnabled())
                {
                    _pmvie->Pmsq()->PlayMsq();
                }
                else
                {
                    _pmvie->Pmsq()->FlushMsq();
                }
                Update();
            }
        }
    }

    return fTrue;
}

/** 3DMMv1.0: ***************************************************************************\
 *
 *	_CxScrollbar
 *		Calculates the slidable length of a scrollbar based on a non-zero
 *		width thumb tab which has its position associated with its leftmost
 *		pixel.
 *
 *	Parameters:
 *		kidScrollbar	- id for the scrollbar gob
 *		kidThumb		- id for the thumb tab gob
 *
 *	Returns:
 *		Length of the slidable region of the scrollbar
 *
\*****************************************************************************/
int32_t SSCB::_CxScrollbar(int32_t kidScrollbar, int32_t kidThumb)
{
    AssertThis(0);

    int32_t cxThumb, cxScrollbar;
    PGOB pgob;
    RC rc;

    // 3DMMv1.0: rightmost pos we can slide the thumb tab is the pos where it has
    // 3DMMv1.0: its right edge at the max pos of the scrollbar, or in other words, it
    // 3DMMv1.0: is its own width away from the max pos of the scrollbar

    // 3DMMv1.0: calculate the thumb tab width
    if (pvNil == (pgob = ((APP *)vpappb)->Pkwa()->PgobFromHid(kidThumb)))
        return 0;
    pgob->GetRc(&rc, cooLocal);
    cxThumb = rc.xpRight - rc.xpLeft + 1;
    // 3DMMv1.0: calculate the scrollbar width
    if (pvNil == (pgob = ((APP *)vpappb)->Pkwa()->PgobFromHid(kidScrollbar)))
        return 0;
    pgob->GetRc(&rc, cooLocal);
    cxScrollbar = rc.xpRight - rc.xpLeft + 1 - cxThumb;

    return cxScrollbar;
}

/** 3DMMv1.0: ***************************************************************************\
 *
 *	Update
 *		Update the studio scrollbars.
 *
 *	Parameters:
 *		None.
 *
 *	Returns:
 *		Nothing.
 *
\*****************************************************************************/
void SSCB::Update(void)
{
    AssertThis(0);

    PSCEN pscen;
    STN stn;
    PGOB pgob;
    RC rc;
    int32_t xp, dxp;
    int32_t cxScrollbar;

    // 3DMMv1.0: need a valid movie ptr, also a ptr to the current scene
    if ((pvNil == _pmvie) || (pvNil == (pscen = _pmvie->Pscen())))
        return;

    // Manual Camera is a user-toggle, but it may not remain active on a frame
    // occupied by a Depth Motion Tween.  Keep the native eye synchronized
    // with the movie's authoritative edit-mode state.
    if (_pmvie->FManualCameraMode() && _pmvie->FDepthMotionTweenActive())
        _pmvie->FSetManualCameraMode(fFalse);

    PGOK pgokManualCamera =
        (PGOK)((APP *)vpappb)->Pkwa()->PgobFromHid(kidManualCamera);
    if (pgokManualCamera != pvNil && pgokManualCamera->FIs(kclsGOK))
    {
        int32_t snoManualCamera =
            _pmvie->FManualCameraFrameActive() ? kstOpen : kstClosed;
        if (pgokManualCamera->Sno() != snoManualCamera)
            pgokManualCamera->FChangeState(snoManualCamera);
    }

    // The Depth Motion Tween eye is an indicator, not a user-toggle.  Keep it
    // synchronized with the selected frame whenever Studio updates its frame
    // and scene controls, including normal playback and committed seeking.
    PGOK pgokDepthMotionTween =
        (PGOK)((APP *)vpappb)->Pkwa()->PgobFromHid(kidDepthMotionTween);
    if (pgokDepthMotionTween != pvNil && pgokDepthMotionTween->FIs(kclsGOK))
    {
        int32_t snoDepthMotionTween =
            _pmvie->FDepthMotionTweenActive() ? kstOpen : kstClosed;
        if (pgokDepthMotionTween->Sno() != snoDepthMotionTween)
            pgokDepthMotionTween->FChangeState(snoDepthMotionTween);
    }

    // 3DMMv1.0: update the frame scrollbar
    if (pvNil != (pgob = ((APP *)vpappb)->Pkwa()->PgobFromHid(kidFrameScrollbar)))
    {
        // 3DMMv1.0: source range: [first_frame, ..., last_frame]
        int32_t cfrm = pscen->NfrmLast() - pscen->NfrmFirst() + 1;
        // 3DMMv1.0: source value: index of the current frame
        int32_t ifrm = pscen->Nfrm() - pscen->NfrmFirst();

        pgob->GetRc(&rc, cooParent);
        if (cfrm < 2)
        {
            // 3DMMv1.0: special case to avoid divide by zero in scaling
            // 3DMMv1.0: we use cfrm - 1 as the size of the destination range so
            // 3DMMv1.0: the highest value will get mapped to the right endpoint
            xp = 0;
        }
        else
        {
            // 3DMMv1.0: source value should be in source range
            AssertIn(ifrm, 0, cfrm);

            // 3DMMv1.0: calculate destination range: mouse positions on scrollbar
            cxScrollbar = _CxScrollbar(kidFrameScrollbar, kidFrameThumb);

            // 3DMMv1.0: map the source value from the source range to the destination range
            xp = LwMulDiv(ifrm, cxScrollbar, cfrm - 1);
        }

        // 3DMMv1.0: move the frame thumb tab to the appropriate position
        if (pvNil != (pgob = ((APP *)vpappb)->Pkwa()->PgobFromHid(kidFrameThumb)))
        {
            pgob->GetPos(&rc, pvNil);
            dxp = xp - rc.xpLeft;
            ((PGOK)pgob)->FSetRep(chidNil, fgokNoAnim, ctgNil, dxp, 0, 0);
        }
    }

    // 3DMMv1.0: update the scene scrollbar
    if (pvNil != (pgob = ((APP *)vpappb)->Pkwa()->PgobFromHid(kidSceneScrollbar)))
    {
        // 3DMMv1.0: source range: [0, ..., num_scenes - 1]
        int32_t cscen = _pmvie->Cscen();
        // 3DMMv1.0: source value: index of current scene
        int32_t iscen = _pmvie->Iscen();

        pgob->GetRc(&rc, cooParent);
        if (cscen < 2)
        {
            // 3DMMv1.0: special case to avoid divide by zero in scaling
            // 3DMMv1.0: we use cscen - 1 as the size of the destination range so
            // 3DMMv1.0: the highest value will get mapped to the right endpoint
            xp = 0;
        }
        else
        {
            // 3DMMv1.0: source value should be in source range
            AssertIn(iscen, 0, cscen);

            // 3DMMv1.0: calculate destination range: mouse positions on scrollbar
            cxScrollbar = _CxScrollbar(kidSceneScrollbar, kidSceneThumb);

            // 3DMMv1.0: map the source value from the source range to the destination range
            xp = LwMulDiv(iscen, cxScrollbar, cscen - 1);
        }

        // 3DMMv1.0: move the scene thumb tab to the appropriate position
        if (pvNil != (pgob = ((APP *)vpappb)->Pkwa()->PgobFromHid(kidSceneThumb)))
        {
            pgob->GetPos(&rc, pvNil);
            dxp = xp - rc.xpLeft;
            ((PGOK)pgob)->FSetRep(chidNil, fgokNoAnim, ctgNil, dxp, 0, 0);
        }
    }

    // 3DMMv1.0: update the other stuff, frame and scene counters, fps

    // 3DMMv1.0: update frame counter
    if (_fNoAutoadjust)
    {
        stn.FFormatSz(PszLit("%4d"), pscen->Nfrm() - _nfrmFirstOld + 1);
    }
    else
    {
        stn.FFormatSz(PszLit("%4d"), pscen->Nfrm() - pscen->NfrmFirst() + 1);
    }
    _ptgobFrame->SetText(&stn);

    // 3DMMv1.0: update scene counter
    stn.FFormatSz(PszLit("%4d"), _pmvie->Iscen() + 1);
    _ptgobScene->SetText(&stn);

#ifdef SHOW_FPS
    {
        int32_t cfrmTail, cfrmCur;
        uint32_t tsTail, tsCur;
        float fps;

        /* 3DMMv1.0: Get current info */
        tsCur = TsCurrent();
        cfrmCur = _pmvie->Cnfrm();

        /* 3DMMv1.0: Get least recent frame registered */
        tsTail = _rgfdsc[_itsNext].ts;
        cfrmTail = _rgfdsc[_itsNext].cfrm;

        /* 3DMMv1.0: Register current frame */
        _rgfdsc[_itsNext].ts = tsCur;
        _rgfdsc[_itsNext++].cfrm = cfrmCur;

        if (tsTail < _pmvie->TsStart())
        {
            tsTail = _pmvie->TsStart();
            cfrmTail = 0;
        }

        Assert(_itsNext <= kctsFps, "Bogus fps next state");
        if (_itsNext == kctsFps)
            _itsNext = 0;

        fps = ((float)((cfrmCur - cfrmTail) * kdtsSecond)) / ((float)(tsCur - tsTail));

        stn.FFormatSz(PszLit("%2d.%02d fps"), (int)fps, (int)((fps - (int)fps) * 100));
        _ptgobFps->SetText(&stn);
    }
#endif // 3DMMv1.0: SHOW_FPS
}

/** 3DMMv1.0: ***************************************************************************\
 *
 *	Change the owner movie for these scroll bars
 *
 *	Parameters:
 *		pmvie - new movie
 *
 *	Returns:
 *		Nothing.
 *
\*****************************************************************************/
void SSCB::SetMvie(PMVIE pmvie)
{
    _pmvie = pmvie;
    Update();
}

/** 3DMMv1.0: ***************************************************************************\
 *
 *	Start a period of no autoadjusting on the scroll bars
 *
 *	Parameters:
 *		Nothing.
 *
 *	Returns:
 *		Nothing.
 *
\*****************************************************************************/
void SSCB::StartNoAutoadjust(void)
{
    AssertThis(0);

    _fNoAutoadjust = fTrue;

    Assert(_pmvie->Pscen() != pvNil, "Bad scene");
    _nfrmFirstOld = _pmvie->Pscen()->NfrmFirst();
    Update();
}

void SSCB::SetSndFrame(bool fSoundInFrame)
{
    int32_t snoNew = fSoundInFrame ? kst2 : kst1;
    PGOK pgokThumb = (PGOK)vapp.Pkwa()->PgobFromHid(kidFrameThumb);

    if (pgokThumb != pvNil && pgokThumb->FIs(kclsGOK))
    {
        if (pgokThumb->Sno() != snoNew)
            pgokThumb->FChangeState(snoNew);
    }
    else
        Bug("Missing or invalid thumb GOB");
}

#ifdef DEBUG

/** 3DMMv1.0: ***************************************************************************\
 *
 *	Mark memory used by the SSCB
 *
 *	Parameters:
 *		None.
 *
 *	Returns:
 *		Nothing.
 *
\*****************************************************************************/
void SSCB::MarkMem(void)
{
    AssertThis(0);

    SSCB_PAR::MarkMem();
}

/** 3DMMv1.0: ***************************************************************************\
 *
 *	Assert the validity of the SSCB
 *
 *	Parameters:
 *		grf - bit array of options.
 *
 *	Returns:
 *		Nothing.
 *
\*****************************************************************************/
void SSCB::AssertValid(uint32_t grf)
{
    SSCB_PAR::AssertValid(fobjAllocated);
}

#endif // 3DMMv1.0: DEBUG
