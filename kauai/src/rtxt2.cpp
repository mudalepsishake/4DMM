/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Rich text document and associated DDG, continued

***************************************************************************/
#include "frame.h"
ASSERTNAME

RTCLASS(TRUL)

const int32_t kdxpMax = 0x01000000;

/** 3DMMv1.0: *************************************************************************
    Character run data.
***************************************************************************/
typedef struct CHRD *PCHRD;
struct CHRD
{
    int32_t cpLim;
    int32_t cpLimDraw;
    int32_t xpLim;
    int32_t xpLimDraw;
};

/** 3DMMv1.0: *************************************************************************
    Character run class. This is used to format a line, draw a line,
    map between cp and xp on a line, etc.
***************************************************************************/
const int32_t kcchMaxChr = 128;
class CHR
{
    ASSERT

  private:
    CHP _chp;
    PAP _pap;
    PTXTB _ptxtb;
    PGNV _pgnv;
    bool _fMustAdvance : 1;
    bool _fBreak : 1;
    bool _fObject : 1;

    int32_t _cpMin;
    int32_t _cpLim;
    int32_t _cpLimFetch;
    int32_t _xpMin;
    int32_t _xpBreak;

    CHRD _chrd;
    CHRD _chrdBop;

    int32_t _dypAscent;
    int32_t _dypDescent;

    achar _rgch[kcchMaxChr];

    bool _FFit(void);
    void _SetToBop(void);
    void _SkipIgnores(void);
    void _DoTab(void);

  public:
    void Init(CHP *pchp, PAP *ppap, PTXTB ptxtb, PGNV pgnv, int32_t cpMin, int32_t cpLim, int32_t xpBase,
              int32_t xpLimLine, int32_t xpBreak);

    void GetNextRun(bool fMustAdvance = fFalse);
    bool FBreak(void)
    {
        return _fBreak;
    }
    void GetChrd(PCHRD pchrd, PCHRD pchrdBop = pvNil)
    {
        AssertNilOrVarMem(pchrd);
        AssertNilOrVarMem(pchrdBop);

        if (pvNil != pchrd)
            *pchrd = _chrd;
        if (pvNil != pchrdBop)
            *pchrdBop = _chrdBop;
    }
    int32_t DypAscent(void)
    {
        return _dypAscent;
    }
    int32_t DypDescent(void)
    {
        return _dypDescent;
    }
    int32_t XpMin(void)
    {
        return _xpMin;
    }
    int32_t XpBreak(void)
    {
        return _xpBreak;
    }
    achar *Prgch(void)
    {
        return _rgch;
    }
    int32_t CpMin(void)
    {
        return _cpMin;
    }
    bool FObject(void)
    {
        return _fObject;
    }
};

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a CHR.
***************************************************************************/
void CHR::AssertValid(uint32_t grf)
{
    AssertThisMem();
    AssertPo(_ptxtb, 0);
    AssertPo(_pgnv, 0);
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Initialize the CHR.
***************************************************************************/
void CHR::Init(CHP *pchp, PAP *ppap, PTXTB ptxtb, PGNV pgnv, int32_t cpMin, int32_t cpLim, int32_t xpBase,
               int32_t xpLimLine, int32_t xpBreak)
{
    AssertVarMem(pchp);
    AssertVarMem(ppap);
    AssertPo(ptxtb, 0);
    AssertPo(pgnv, 0);
    AssertIn(cpMin, 0, ptxtb->CpMac());
    AssertIn(cpLim, cpMin + 1, ptxtb->CpMac() + 1);
    Assert(xpBase <= xpLimLine, "xpBase > xpLimLine");

    RC rc;

    _chp = *pchp;
    _pap = *ppap;
    _ptxtb = ptxtb;
    _pgnv = pgnv;
    _fBreak = fFalse;

    _cpMin = cpMin;
    _cpLim = cpLim;
    _cpLimFetch = cpMin;
    _xpMin = xpBase;
    _xpBreak = LwMin(xpBreak, xpLimLine);

    // 3DMMv1.0: apply any indenting
    if (0 == xpBase)
    {
        switch (_pap.nd)
        {
        case ndFirst:
            if (_ptxtb->FMinPara(_cpMin))
                _xpMin += _pap.dxpTab;
            break;
        case ndRest:
            if (!_ptxtb->FMinPara(_cpMin))
                _xpMin += _pap.dxpTab;
            break;
        case ndAll:
            _xpMin += _pap.dxpTab;
            break;
        }
    }
    if (ndAll == _pap.nd)
        _xpBreak = LwMin(_xpBreak, xpLimLine - _pap.dxpTab);

    _chrd.cpLim = _chrd.cpLimDraw = _cpMin;
    _chrd.xpLim = _chrd.xpLimDraw = _xpMin;
    _chrdBop = _chrd;

    // 3DMMv1.0: get the vertical dimensions
    _pgnv->SetFont(_chp.onn, _chp.grfont, _chp.dypFont, tahLeft, tavBaseline);

#ifndef SOC_BUG_1500 // 3DMMv1.0: REVIEW shonk: Win95 bug workaround
    // 3DMMv1.0: If we don't draw to the _pgnv before getting the metrics, the metrics
    // 3DMMv1.0: can be different than after we draw!
    achar ch = kchSpace;
    _pgnv->DrawRgch(&ch, 1, 0, 0);
#endif //! 3DMMv1.0: REVIEW

    _pgnv->GetRcFromRgch(&rc, pvNil, 0);
    _dypAscent = LwMax(0, -rc.ypTop - _chp.dypOffset);
    _dypDescent = LwMax(0, rc.ypBottom + _chp.dypOffset);

    AssertThis(0);
}

/** 3DMMv1.0: *************************************************************************
    Get the next run (within the bounds we were inited with).
***************************************************************************/
void CHR::GetNextRun(bool fMustAdvance)
{
    AssertThis(0);
    uint32_t grfch;
    achar ch;
    RC rc;

    // 3DMMv1.0: Start the next run
    if (FIn(_chrd.cpLim, _cpMin + 1, _cpLimFetch))
        BltPb(_rgch + _chrd.cpLim - _cpMin, _rgch, (_cpLimFetch - _chrd.cpLim) * SIZEOF(achar));
    _cpMin = _chrd.cpLimDraw = _chrd.cpLim;
    _xpMin = _chrd.xpLimDraw = _chrd.xpLim;
    _xpBreak = LwMax(_xpBreak, _xpMin);
    _chrdBop = _chrd;

    if (_cpMin >= _cpLim)
        return;

    // 3DMMv1.0: Refill the buffer
    if (_cpLimFetch < _cpLim && _cpLimFetch < _cpMin + kcchMaxChr)
    {
        int32_t cpFetch = LwMax(_cpMin, _cpLimFetch);

        _cpLimFetch = LwMin(kcchMaxChr, _cpLim - _cpMin) + _cpMin;
        _ptxtb->FetchRgch(cpFetch, _cpLimFetch - cpFetch, _rgch + cpFetch - _cpMin);
    }

    _fMustAdvance = FPure(fMustAdvance);
    _fBreak = fFalse;
    _fObject = fFalse;

    _pgnv->SetFont(_chp.onn, _chp.grfont, _chp.dypFont, tahLeft, tavBaseline);
    for (;;)
    {
        if (_chrd.cpLim >= _cpLimFetch || (fchIgnore & (grfch = GrfchFromCh(ch = _rgch[_chrd.cpLim - _cpMin]))))
        {
            // 3DMMv1.0: we're out of characters or this is an ignoreable character -
            // 3DMMv1.0: return the run
            if (!_FFit())
                _SetToBop();
            else
                _SkipIgnores();
            break;
        }

        if (grfch & fchTab)
        {
            // 3DMMv1.0: handle the string of tabs, then return the run
            _DoTab();
            break;
        }

        if (grfch & fchBreak)
        {
            // 3DMMv1.0: This line must break after this character - return the run.
            if (!_FFit())
            {
                _SetToBop();
                break;
            }

            _chrd.cpLim++;
            _SkipIgnores();

            // 3DMMv1.0: this is a BOP
            _chrdBop = _chrd;
            _fBreak = fTrue;
            break;
        }

        if (grfch & fchMayBreak)
        {
            _chrd.cpLimDraw = ++_chrd.cpLim;
            if (_FFit())
            {
                // 3DMMv1.0: this is a BOP
                _chrdBop = _chrd;
                continue;
            }

            _fBreak = fTrue;
            if (grfch & fchWhiteOverhang)
            {
                // 3DMMv1.0: see if everything but this character fits
                int32_t xp = _chrd.xpLim;

                _chrd.cpLimDraw--;
                if (_FFit())
                {
                    // 3DMMv1.0: fits with the overhang
                    _chrd.xpLim = xp;
                    _chrdBop = _chrd;
                    _SkipIgnores();
                    break;
                }
            }

            _SetToBop();
            break;
        }

        if (kchObject == ch)
        {
            // 3DMMv1.0: this is an object character
            if (_chrd.cpLim > _cpMin)
            {
                // 3DMMv1.0: return the run before processing the object - objects
                // 3DMMv1.0: go in their own run.
                if (!_FFit())
                    _SetToBop();
                else
                    _chrdBop = _chrd;
                break;
            }

            // 3DMMv1.0: return just the object (if it really is an object)
            if (!_ptxtb->FGetObjectRc(_chrd.cpLim, _pgnv, &_chp, &rc))
            {
                // 3DMMv1.0: treat as a normal character
                _chrd.cpLimDraw = ++_chrd.cpLim;
                continue;
            }

            rc.Offset(0, _chp.dypOffset);
            Assert(!rc.FEmpty() && rc.xpRight >= 0, "bad rectangle for the object");

            if (fMustAdvance || _xpMin + rc.xpRight <= _xpBreak)
            {
                _fObject = fTrue;
                _chrd.cpLimDraw = ++_chrd.cpLim;
                _chrd.xpLim = _chrd.xpLimDraw = _xpMin + rc.xpRight;
                _chrdBop = _chrd;

                _dypAscent = LwMax(_dypAscent, -rc.ypTop);
                _dypDescent = LwMax(_dypDescent, rc.ypBottom);

                if (_xpMin + rc.xpRight >= _xpBreak)
                    _fBreak = fTrue;
                _SkipIgnores();
            }
            break;
        }

        // 3DMMv1.0: normal character
        _chrd.cpLimDraw = ++_chrd.cpLim;
    }

    AssertThis(0);
}

/** 3DMMv1.0: *************************************************************************
    Test whether everything from _cpMin to _chrd.cpLimDraw fits. Assumes the
    font is set in the _pgnv.
***************************************************************************/
bool CHR::_FFit(void)
{
    AssertThis(0);
    RC rc;

    if (_chrd.cpLimDraw == _cpMin)
        _chrd.xpLim = _chrd.xpLimDraw = _xpMin;
    else
    {
        _pgnv->GetRcFromRgch(&rc, _rgch, _chrd.cpLimDraw - _cpMin);
        _chrd.xpLim = _chrd.xpLimDraw = _xpMin + rc.Dxp();
    }

    return _chrd.xpLimDraw <= _xpBreak;
}

/** 3DMMv1.0: *************************************************************************
    Set the CHR to the last break opportunity. If there wasn't one and
    _fMustAdvance is true, gobble as many characters as we can, but at least
    one.
***************************************************************************/
void CHR::_SetToBop(void)
{
    AssertThis(0);

    _fBreak = fTrue;
    if (_chrdBop.cpLim <= _cpMin && _fMustAdvance)
    {
        // 3DMMv1.0: no break opportunity seen - gobble as many characters as we can,
        // 3DMMv1.0: but at least one.
        // 3DMMv1.0: do a binary search for the character to break at
        Assert(_chrd.cpLimDraw > _cpMin, "why is _chrd.cpLimDraw == _cpMin?");

        RC rc;
        int32_t ivMin, ivLim, iv;
        int32_t dxp = _xpBreak - _xpMin;

        for (ivMin = 0, ivLim = _chrd.cpLimDraw - _cpMin; ivMin < ivLim;)
        {
            iv = (ivMin + ivLim) / 2 + 1;
            AssertIn(iv, ivMin + 1, ivLim + 1);
            _pgnv->GetRcFromRgch(&rc, _rgch, iv);
            if (rc.Dxp() <= dxp)
                ivMin = iv;
            else
                ivLim = iv - 1;
        }
        AssertIn(ivMin, 0, _chrd.cpLimDraw - _cpMin + 1);
        if (ivMin == 0)
        {
            // 3DMMv1.0: nothing fits - use one character
            ivMin = 1;
        }

        // 3DMMv1.0: set _chrd.cpLim and _chrd.cpLimDraw, then set the _xp values
        _chrd.cpLim = _chrd.cpLimDraw = _cpMin + ivMin;
        _FFit();
    }
    else
        _chrd = _chrdBop;

    _SkipIgnores();
}

/** 3DMMv1.0: *************************************************************************
    Skip any trailing ignore characters. Just changes _chrd.cpLim.
***************************************************************************/
void CHR::_SkipIgnores(void)
{
    AssertThis(0);

    while (_chrd.cpLim < _cpLimFetch && (fchIgnore & GrfchFromCh(_rgch[_chrd.cpLim - _cpMin])))
    {
        _chrd.cpLim++;
    }

    if (_chrd.cpLim == _cpLimFetch)
    {
        achar ch;
        int32_t cpMac = _ptxtb->CpMac();

        while (_chrd.cpLim < cpMac)
        {
            _ptxtb->FetchRgch(_chrd.cpLim, 1, &ch);
            if (!(fchIgnore & GrfchFromCh(ch)))
                return;
            _chrd.cpLim++;
        }
    }
}

/** 3DMMv1.0: *************************************************************************
    Swallow as many tabs as possible.
***************************************************************************/
void CHR::_DoTab(void)
{
    AssertThis(0);

    if (!_FFit())
    {
        _SetToBop();
        return;
    }

    while (_chrd.cpLim < _cpLimFetch && (fchTab & GrfchFromCh(_rgch[_chrd.cpLim - _cpMin])))
    {
        _chrd.cpLim++;
        _chrd.xpLim = LwRoundAway(_chrd.xpLim + 1, _pap.dxpTab);
        if (_chrd.xpLim > _xpBreak)
        {
            // 3DMMv1.0: this tab would carry us over the edge.
            if (_chrd.cpLim == _cpMin + 1 && _fMustAdvance)
            {
                // 3DMMv1.0: the line is empty, so we have to force the tab onto the line
                _SkipIgnores();
                _fBreak = fTrue;
            }
            else
                _SetToBop();
            return;
        }

        // 3DMMv1.0: this is a BOP
        _chrd.xpLimDraw = _chrd.xpLim;
        _chrdBop = _chrd;
    }

    _SkipIgnores();
}

/** 3DMMv1.0: *************************************************************************
    Constructor for the text document display GOB.
***************************************************************************/
TXTG::TXTG(PTXTB ptxtb, PGCB pgcb) : TXTG_PAR(ptxtb, pgcb)
{
    AssertBaseThis(0);
    _ptxtb = ptxtb;
    _fMark = (kginMark == pgcb->_gin || kginDefault == pgcb->_gin && kginMark == GOB::GinDefault());
    _pgnv = pvNil;
}

/** 3DMMv1.0: *************************************************************************
    Destructor for TXTG.
***************************************************************************/
TXTG::~TXTG(void)
{
    AssertBaseThis(0);
    ReleasePpo(&_pgllin);
    ReleasePpo(&_pgnv);
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a TXTG.
***************************************************************************/
void TXTG::AssertValid(uint32_t grf)
{
    TXTG_PAR::AssertValid(0);
    AssertPo(_pgllin, 0);
    AssertIn(_ilinInval, 0, _pgllin->IvMac() + 1);
    AssertPo(_pgnv, 0);
    AssertNilOrPo(_ptrul, 0);
    // 3DMMv1.0: REVIEW shonk: TXTG::AssertValid: fill out.
}

/** 3DMMv1.0: *************************************************************************
    Mark memory for the TXTG.
***************************************************************************/
void TXTG::MarkMem(void)
{
    AssertValid(0);
    TXTG_PAR::MarkMem();
    MarkMemObj(_pgllin);
    MarkMemObj(_pgnv);
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Initialize the text document display gob.
***************************************************************************/
bool TXTG::_FInit(void)
{
    AssertBaseThis(0);
    PGPT pgpt;

    if (!TXTG_PAR::_FInit())
        return fFalse;
    if (pvNil == (_pgllin = GL::PglNew(SIZEOF(LIN))))
        return fFalse;

    // 3DMMv1.0: Allocate the GNV for formatting. Use an offscreen one iff _fMark
    // 3DMMv1.0: is set.
    if (_fMark)
    {
        RC rc(0, 0, 1, 1);

        if (pvNil == (pgpt = GPT::PgptNewOffscreen(&rc, 8)))
            return fFalse;
    }
    else
    {
        pgpt = Pgpt();
        pgpt->AddRef();
    }

    _pgnv = NewObj GNV(this, pgpt);
    ReleasePpo(&pgpt);

    if (pvNil == _pgnv)
        return fFalse;

    _pgllin->SetMinGrow(20);
    _ilinDisp = 0;
    _cpDisp = 0;
    _dypDisp = 0;
    _ilinInval = 0;
    if (_DxpDoc() > 0)
    {
        int32_t cpMac = _ptxtb->CpMac();
        _Reformat(0, cpMac, cpMac);
    }

    AssertThis(0);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Deactivate the TXTG - turn off the selection.
***************************************************************************/
void TXTG::_Activate(bool fActive)
{
    AssertThis(0);

    TXTG_PAR::_Activate(fActive);
    if (!fActive)
        _SwitchSel(fFalse, kginSysInval);
}

/** 3DMMv1.0: *************************************************************************
    Get the LIN for the given ilin.  If ilin is past the end of _pgllin,
    _CalcLine is called repeatedly and new lines are added to _pgllin.
    The actual index of the returned line is put in *pilinActual (if not nil).
***************************************************************************/
void TXTG::_FetchLin(int32_t ilin, LIN *plin, int32_t *pilinActual)
{
    AssertThis(0);
    AssertIn(ilin, 0, kcbMax);
    AssertVarMem(plin);
    AssertNilOrVarMem(pilinActual);

    int32_t cpLim, cpMac;
    int32_t dypTot;
    LIN *qlin;
    bool fAdd;
    int32_t ilinLim = LwMin(_pgllin->IvMac(), ilin + 1);

    if (pvNil != pilinActual)
        *pilinActual = ilin;

    if (_ilinInval < ilinLim)
    {
        qlin = (LIN *)_pgllin->QvGet(_ilinInval);
        if (_ilinInval == 0)
        {
            cpLim = 0;
            dypTot = 0;
        }
        else
        {
            qlin--;
            cpLim = qlin->cpMin + qlin->ccp;
            dypTot = qlin->dypTot + qlin->dyp;
            qlin++;
        }

        // 3DMMv1.0: adjust LINs up to ilinLim
        for (; _ilinInval < ilinLim; _ilinInval++, qlin++)
        {
            qlin->cpMin = cpLim;
            qlin->dypTot = dypTot;
            cpLim += qlin->ccp;
            dypTot += qlin->dyp;
        }
    }
    Assert(_ilinInval >= ilinLim, 0);

    if (ilin < _ilinInval)
    {
        *plin = *(LIN *)_pgllin->QvGet(ilin);
        return;
    }

    Assert(ilinLim == _pgllin->IvMac(), 0);
    if (ilinLim == 0)
    {
        cpLim = 0;
        dypTot = 0;
        ClearPb(plin, SIZEOF(LIN));
    }
    else
    {
        // 3DMMv1.0: get the LIN in case we don't actuall calc any lines below
        *plin = *(LIN *)_pgllin->QvGet(ilinLim - 1);
        cpLim = plin->cpMin + plin->ccp;
        dypTot = plin->dypTot + plin->dyp;
    }

    cpMac = _ptxtb->CpMac();
    fAdd = fTrue;
    for (; ilinLim <= ilin && cpLim < cpMac; ilinLim++)
    {
        _CalcLine(cpLim, dypTot, plin);
        fAdd = fAdd && _pgllin->FAdd(plin);
        cpLim += plin->ccp;
        dypTot += plin->dyp;
    }
    _ilinInval = _pgllin->IvMac();
    if (pvNil != pilinActual)
        *pilinActual = ilinLim - 1;
}

/** 3DMMv1.0: *************************************************************************
    Find the LIN that contains the given cpFind. pilin and/or plin can be nil.
    If fCalcLines is false, we won't calculate any new lines and the
    returned LIN may be before cpFind.
***************************************************************************/
void TXTG::_FindCp(int32_t cpFind, LIN *plin, int32_t *pilin, bool fCalcLines)
{
    AssertThis(0);
    AssertIn(cpFind, 0, _ptxtb->CpMac());
    AssertNilOrVarMem(pilin);
    AssertNilOrVarMem(plin);

    LIN *qlin;
    LIN lin;
    int32_t dypTot;
    int32_t cpLim;
    int32_t ilinMac;
    bool fAdd;

    // 3DMMv1.0: get the starting cp and dypTot values for _ilinInval
    qlin = (LIN *)_pgllin->QvGet(_ilinInval);
    if (_ilinInval == 0)
    {
        cpLim = 0;
        dypTot = 0;
    }
    else
    {
        qlin--;
        cpLim = qlin->cpMin + qlin->ccp;
        dypTot = qlin->dypTot + qlin->dyp;
        qlin++;
    }

    if (cpFind < cpLim)
    {
        // 3DMMv1.0: do a binary search to find the LIN containing cpFind
        int32_t ivMin, ivLim, iv;

        for (ivMin = 0, ivLim = _ilinInval; ivMin < ivLim;)
        {
            iv = (ivMin + ivLim) / 2;
            qlin = (LIN *)_pgllin->QvGet(iv);
            if (cpFind < qlin->cpMin)
                ivLim = iv;
            else if (cpFind >= qlin->cpMin + qlin->ccp)
                ivMin = iv + 1;
            else
            {
                if (pvNil != pilin)
                    *pilin = iv;
                if (pvNil != plin)
                    *plin = *qlin;
                return;
            }
        }

        Bug("Invalid LINs");
        cpLim = 0;
        dypTot = 0;
        _ilinInval = 0;
    }

    Assert(cpFind >= cpLim, "why isn't cpFind >= cpLim?");
    if (_ilinInval < (ilinMac = _pgllin->IvMac()))
    {
        // 3DMMv1.0: adjust LINs up to cpFind
        qlin = (LIN *)_pgllin->QvGet(_ilinInval);
        for (; _ilinInval < ilinMac && cpFind >= cpLim; _ilinInval++, qlin++)
        {
            qlin->cpMin = cpLim;
            qlin->dypTot = dypTot;
            cpLim += qlin->ccp;
            dypTot += qlin->dyp;
        }

        if (cpFind < cpLim)
        {
            AssertIn(cpFind, qlin[-1].cpMin, cpLim);
            if (pvNil != pilin)
                *pilin = _ilinInval - 1;
            if (pvNil != plin)
                *plin = qlin[-1];
            return;
        }
    }
    Assert(_ilinInval == ilinMac, "why isn't _ilinInval == ilinMac?");
    Assert(cpFind >= cpLim, "why isn't cpFind >= cpLim?");

    if (!fCalcLines)
    {
        if (pvNil != pilin)
            *pilin = ilinMac - (ilinMac > 0);
        if (pvNil != plin)
        {
            if (ilinMac > 0)
                *plin = *(LIN *)_pgllin->QvGet(ilinMac - 1);
            else
                ClearPb(plin, SIZEOF(LIN));
        }
        return;
    }

    // 3DMMv1.0: have to calculate some lines
    for (fAdd = fTrue; cpFind >= cpLim; ilinMac++)
    {
        _CalcLine(cpLim, dypTot, &lin);
        fAdd = fAdd && _pgllin->FAdd(&lin);
        cpLim += lin.ccp;
        dypTot += lin.dyp;
    }
    _ilinInval = _pgllin->IvMac();

    if (pvNil != pilin)
        *pilin = ilinMac - 1;
    if (pvNil != plin)
        *plin = lin;
}

/** 3DMMv1.0: *************************************************************************
    Find the LIN that contains the given dypFind value (measured from the top
    of the document). pilin and/or plin can be nil.
    If fCalcLines is false, we won't calculate any new lines and the
    returned LIN may be before dypFind.
***************************************************************************/
void TXTG::_FindDyp(int32_t dypFind, LIN *plin, int32_t *pilin, bool fCalcLines)
{
    AssertThis(0);
    AssertIn(dypFind, 0, kcbMax);
    AssertNilOrVarMem(pilin);
    AssertNilOrVarMem(plin);

    LIN *qlin;
    LIN lin;
    int32_t dypTot;
    int32_t cpLim, cpMac;
    int32_t ilinMac;
    bool fAdd;

    // 3DMMv1.0: get the starting cp and dypTot values for _ilinInval
    qlin = (LIN *)_pgllin->QvGet(_ilinInval);
    if (_ilinInval == 0)
    {
        cpLim = 0;
        dypTot = 0;
    }
    else
    {
        qlin--;
        cpLim = qlin->cpMin + qlin->ccp;
        dypTot = qlin->dypTot + qlin->dyp;
        qlin++;
    }

    if (dypFind < dypTot)
    {
        // 3DMMv1.0: do a binary search to find the LIN containing dypFind
        int32_t ivMin, ivLim, iv;

        for (ivMin = 0, ivLim = _ilinInval; ivMin < ivLim;)
        {
            iv = (ivMin + ivLim) / 2;
            qlin = (LIN *)_pgllin->QvGet(iv);
            if (dypFind < qlin->dypTot)
                ivLim = iv;
            else if (dypFind >= qlin->dypTot + qlin->dyp)
                ivMin = iv + 1;
            else
            {
                if (pvNil != pilin)
                    *pilin = iv;
                if (pvNil != plin)
                    *plin = *qlin;
                return;
            }
        }

        Bug("Invalid LINs");
        cpLim = 0;
        dypTot = 0;
        _ilinInval = 0;
    }

    Assert(dypFind >= dypTot, "why isn't dypFind >= dypTot?");
    if (_ilinInval < (ilinMac = _pgllin->IvMac()))
    {
        // 3DMMv1.0: adjust LINs up to cp
        qlin = (LIN *)_pgllin->QvGet(_ilinInval);
        for (; _ilinInval < ilinMac && dypFind >= dypTot; _ilinInval++, qlin++)
        {
            qlin->cpMin = cpLim;
            qlin->dypTot = dypTot;
            cpLim += qlin->ccp;
            dypTot += qlin->dyp;
        }

        if (dypFind < dypTot)
        {
            AssertIn(dypFind, qlin[-1].dypTot, dypTot);
            if (pvNil != pilin)
                *pilin = _ilinInval - 1;
            if (pvNil != plin)
                *plin = qlin[-1];
            return;
        }
    }
    Assert(_ilinInval == ilinMac, "why isn't _ilinInval == ilinMac?");
    Assert(dypFind >= dypTot, "why isn't dypFind >= dypTot?");

    if (!fCalcLines)
    {
        if (pvNil != pilin)
            *pilin = ilinMac - (ilinMac > 0);
        if (pvNil != plin)
        {
            if (ilinMac > 0)
                *plin = *(LIN *)_pgllin->QvGet(ilinMac - 1);
            else
                ClearPb(plin, SIZEOF(LIN));
        }
        return;
    }

    cpMac = _ptxtb->CpMac();
    if (cpLim >= cpMac)
    {
        if (pvNil != plin)
        {
            // 3DMMv1.0: Get a valid lin.
            if (ilinMac > 0)
                _pgllin->Get(ilinMac - 1, &lin);
            else
                ClearPb(&lin, SIZEOF(LIN));
        }
        _ilinInval = ilinMac;
    }
    else
    {
        Assert(dypFind >= dypTot && cpLim < cpMac, 0);
        for (fAdd = fTrue; dypFind >= dypTot && cpLim < cpMac; ilinMac++)
        {
            _CalcLine(cpLim, dypTot, &lin);
            fAdd = fAdd && _pgllin->FAdd(&lin);
            cpLim += lin.ccp;
            dypTot += lin.dyp;
        }
        _ilinInval = _pgllin->IvMac();
    }

    if (pvNil != pilin)
        *pilin = ilinMac - 1;
    if (pvNil != plin)
        *plin = lin;
}

/** 3DMMv1.0: *************************************************************************
    Recalculate the _pgllin after an edit.  Sets *pyp, *pdypIns, *pdypDel
    to indicate the vertical display space that was affected.
***************************************************************************/
void TXTG::_Reformat(int32_t cp, int32_t ccpIns, int32_t ccpDel, int32_t *pyp, int32_t *pdypIns, int32_t *pdypDel)
{
    AssertThis(0);
    AssertIn(cp, 0, _ptxtb->CpMac());
    AssertIn(ccpIns, 0, _ptxtb->CpMac() - cp + 1);
    AssertIn(ccpDel, 0, kcbMax);
    AssertNilOrVarMem(pyp);
    AssertNilOrVarMem(pdypIns);
    AssertNilOrVarMem(pdypDel);

    int32_t ypDel, dypDel, dypIns, dypCur;
    int32_t ccp, cpCur, cpNext, cpMac;
    int32_t ilin, ilinOld;
    LIN linOld, lin, linT;

    _fClear = _ptxtb->AcrBack() == kacrClear;
    _fXpValid = fFalse;
    cpMac = _ptxtb->CpMac();

    // 3DMMv1.0: Find the LIN that contains cp (if there is one) - don't calc any lines
    // 3DMMv1.0: to get the LIN.
    _FindCp(cp, &linOld, &ilinOld, fFalse);

    if (cp >= linOld.cpMin + linOld.ccp)
    {
        // 3DMMv1.0: the LIN for this cp was not cached - recalc from the beginning of
        // 3DMMv1.0: the last lin.
        ccpIns += cp - linOld.cpMin;
        ccpDel += cp - linOld.cpMin;
        cp = linOld.cpMin;
    }
    AssertIn(cp, linOld.cpMin, linOld.cpMin + linOld.ccp + (linOld.ccp == 0));

    // 3DMMv1.0: make sure the previous line is formatted correctly
    if (ilinOld > 0 && !_ptxtb->FMinPara(linOld.cpMin))
    {
        _FetchLin(ilinOld - 1, &lin);
        _CalcLine(lin.cpMin, lin.dypTot, &linT);
        if (linT.ccp != lin.ccp)
        {
            // 3DMMv1.0: edit affected previous line - so start
            // 3DMMv1.0: formatting from there
            ilinOld--;
            linOld = lin;
        }
    }
    AssertIn(cp, linOld.cpMin, cpMac);

    // 3DMMv1.0: remove deleted lines
    Assert(ilinOld <= _ilinInval, 0);
    _ilinInval = ilinOld;
    for (ccp = dypDel = 0; linOld.cpMin + ccp <= cp + ccpDel && ilinOld < _pgllin->IvMac();)
    {
        _pgllin->Get(ilinOld, &lin);
        dypDel += lin.dyp;
        ccp += lin.ccp;
        _pgllin->Delete(ilinOld);
    }

    // 3DMMv1.0: insert the new lines
    cpCur = linOld.cpMin;
    dypCur = linOld.dypTot;

    // 3DMMv1.0: cpNext is the cp of the next (possibly stale) LIN in _pgllin
    if (ilinOld < _pgllin->IvMac())
    {
        cpNext = linOld.cpMin + ccp - ccpDel + ccpIns;
        AssertIn(cpNext, linOld.cpMin, cpMac + 1);
    }
    else
        cpNext = cpMac;

    AssertIn(ilinOld, 0, _pgllin->IvMac() + 1);
    dypIns = 0;
    for (ilin = ilinOld;;)
    {
        AssertIn(cpCur, linOld.cpMin, cpMac + 1);
        AssertIn(dypCur, linOld.dypTot, kcbMax);
        while (cpNext < cpCur && ilin < _pgllin->IvMac())
        {
            _pgllin->Get(ilin, &lin);
            _pgllin->Delete(ilin);
            cpNext += lin.ccp;
            dypDel += lin.dyp;
        }
        AssertIn(cpNext, cpCur, cpMac + 1);

        if (ilin >= _pgllin->IvMac())
        {
            // 3DMMv1.0: no more LINs that might be preservable, so we may as well
            // 3DMMv1.0: stop trying.
            ilin = _pgllin->IvMac();
            if (cpNext < cpMac)
                dypDel = kswMax;
            if (cpCur < cpMac)
                dypIns = kswMax;
            break;
        }

        AssertIn(ilin, 0, _pgllin->IvMac());
        if (cpCur == cpNext)
        {
            // 3DMMv1.0: everything from here on should be correct - we still need to
            // 3DMMv1.0: set _ilinDisp
            break;
        }

        _CalcLine(cpCur, dypCur, &lin);
        if (!_pgllin->FInsert(ilin, &lin))
        {
            AssertIn(ilin, 0, _pgllin->IvMac());
            _pgllin->Get(ilin, &linT);
            cpNext += linT.ccp;
            dypDel += linT.dyp;
            _pgllin->Put(ilin, &lin);
        }

        dypIns += lin.dyp;
        cpCur += lin.ccp;
        dypCur += lin.dyp;
        ilin++;
    }
    _ilinInval = ilin;

    if (_cpDisp <= linOld.cpMin)
        ypDel = linOld.dypTot - _dypDisp;
    else
    {
        if (_cpDisp > cp)
        {
            if (_cpDisp >= cp + ccpDel)
                _cpDisp += ccpIns - ccpDel;
            else if (_cpDisp >= cp + ccpIns)
                _cpDisp = cp + ccpIns;
        }
        ypDel = 0;
        _FindCp(_cpDisp, &lin, &_ilinDisp);
        _cpDisp = lin.cpMin;
        dypDel = LwMax(0, linOld.dypTot + dypDel - _dypDisp);
        _dypDisp = lin.dypTot;
        dypIns = LwMax(0, linOld.dypTot + dypIns - _dypDisp);
    }

    if (pvNil != pyp)
        *pyp = ypDel;
    if (pvNil != pdypIns)
        *pdypIns = dypIns;
    if (pvNil != pdypDel)
        *pdypDel = dypDel;
}

/** 3DMMv1.0: *************************************************************************
    Calculate the end of the line, the left position of the line, the height
    of the line and the ascent of the line.
***************************************************************************/
void TXTG::_CalcLine(int32_t cpMin, int32_t dypBase, LIN *plin)
{
    AssertThis(0);
    AssertIn(cpMin, 0, _ptxtb->CpMac());
    AssertVarMem(plin);

    struct RUN
    {
        int32_t cpLim;
        int32_t xpLim;
        int32_t dypAscent;
        int32_t dypDescent;
    };

    int32_t dxpDoc;
    PAP pap;
    CHP chp;
    int32_t cpLimPap, cpLimChp;
    CHR chr;
    CHRD chrd, chrdBop;
    RUN run, runSure, runT;

    dxpDoc = _DxpDoc();
    _FetchPap(cpMin, &pap, pvNil, &cpLimPap);

    cpLimChp = cpMin;
    run.cpLim = cpMin;
    run.xpLim = 0;
    run.dypAscent = run.dypDescent = 0;
    runSure = run;

    for (;;)
    {
        // 3DMMv1.0: make sure the chp is valid
        if (run.cpLim >= cpLimChp)
        {
            _FetchChp(run.cpLim, &chp, pvNil, &cpLimChp);
            cpLimChp = LwMin(cpLimChp, cpLimPap);
            if (cpLimChp <= run.cpLim)
            {
                Bug("why is run.cpLim >= cpLimChp?");
                break;
            }

            chr.Init(&chp, &pap, _ptxtb, _pgnv, run.cpLim, cpLimChp, run.xpLim, dxpDoc, dxpDoc);
        }

        chr.GetNextRun(runSure.cpLim == cpMin);
        chr.GetChrd(&chrd, &chrdBop);

        if (chrd.cpLim == run.cpLim)
        {
            // 3DMMv1.0: didn't move forward - use runSure
            Assert(runSure.cpLim > cpMin, "why don't we have a BOP?");
            run = runSure;
            break;
        }

        runT = run;
        run.cpLim = chrd.cpLim;
        run.xpLim = chrd.xpLimDraw;
        run.dypAscent = LwMax(run.dypAscent, chr.DypAscent());
        run.dypDescent = LwMax(run.dypDescent, chr.DypDescent());

        if (chr.FBreak())
        {
            // 3DMMv1.0: we know that this is the end of the line - use run or runT
            if (run.xpLim > chr.XpBreak() && runT.cpLim > cpMin)
                run = runT;
            break;
        }

        if (chrdBop.cpLim > runT.cpLim)
        {
            // 3DMMv1.0: put chrdBop info into runSure
            runSure.cpLim = chrdBop.cpLim;
            runSure.xpLim = chrdBop.xpLimDraw;
            runSure.dypAscent = LwMax(runSure.dypAscent, chr.DypAscent());
            runSure.dypDescent = LwMax(runSure.dypDescent, chr.DypDescent());
        }
    }

    Assert(run.dypAscent > 0 || run.dypDescent > 0, "bad run");
    Assert(run.cpLim > cpMin, "why is cch zero?");

    switch (pap.jc)
    {
    default:
        plin->xpLeft = 0;
        break;
    case jcRight:
        plin->xpLeft = (int16_t)(dxpDoc - run.xpLim);
        break;
    case jcCenter:
        plin->xpLeft = (dxpDoc - run.xpLim) / 2;
        break;
    }

    plin->ccp = (int16_t)(run.cpLim - cpMin);
    plin->dyp = (int16_t)(run.dypAscent + run.dypDescent);
    if (pap.numLine != kdenLine)
        plin->dyp = (int16_t)LwMulDiv(plin->dyp, pap.numLine, kdenLine);
    plin->dyp += pap.dypExtraLine;
    if (_ptxtb->FMinPara(run.cpLim) || run.cpLim == _ptxtb->CpMac())
    {
        if (pap.numAfter != kdenAfter)
            plin->dyp = (int16_t)LwMulDiv(plin->dyp, pap.numAfter, kdenAfter);
        plin->dyp += pap.dypExtraAfter;
    }
    if (plin->dyp <= 0)
        plin->dyp = 1;
    plin->dypAscent = (int16_t)run.dypAscent;
    if (plin->dypAscent <= 0)
        plin->dypAscent = 1;
    plin->cpMin = cpMin;
    plin->dypTot = dypBase;
}

/** 3DMMv1.0: *************************************************************************
    Get the cp that the point is in.  If fClosest is true, this finds the
    cp boundary that the point is closest to (for traditional selection).
    If fClosest is false, it finds the character that the point is over.
***************************************************************************/
bool TXTG::_FGetCpFromPt(int32_t xp, int32_t yp, int32_t *pcp, bool fClosest)
{
    AssertThis(0);
    AssertVarMem(pcp);

    LIN lin;
    RC rc;

    GetRc(&rc, cooLocal);
    if (!FIn(yp, 0, rc.ypBottom))
        return fFalse;

    _FindDyp(_dypDisp + yp, &lin);
    if (_dypDisp + yp >= lin.dypTot + lin.dyp)
    {
        *pcp = _ptxtb->CpMac() - 1;
        return fTrue;
    }

    // 3DMMv1.0: we've found the line, now get the cp on the line
    return _FGetCpFromXp(xp, &lin, pcp, fClosest);
}

/** 3DMMv1.0: *************************************************************************
    Get the cp on the line given by *plin that the xp is in.  If fClosest
    is true, this finds the cp boundary that the point is closest to (for
    traditional selection).  If fClosest is false, it finds the character
    that the xp is over. This only returns false if fClosest is false and
    the xp is before the beginning of the line.
***************************************************************************/
bool TXTG::_FGetCpFromXp(int32_t xp, LIN *plin, int32_t *pcp, bool fClosest)
{
    AssertThis(0);
    AssertVarMem(plin);
    AssertIn(plin->cpMin, 0, _ptxtb->CpMac());
    AssertVarMem(plin);
    AssertIn(plin->ccp, 1, _ptxtb->CpMac() + 1 - plin->cpMin);
    AssertVarMem(pcp);

    CHP chp;
    PAP pap;
    CHR chr;
    int32_t cpCur, cpLimChp, cpLim;
    int32_t xpCur, dxpDoc;
    CHRD chrd;

    xp -= plin->xpLeft + kdxpIndentTxtg - _scvHorz;
    if (xp <= 0)
    {
        *pcp = plin->cpMin;
        return FPure(fClosest);
    }

    if (xp >= (dxpDoc = _DxpDoc()))
    {
        *pcp = _ptxtb->CpPrev(plin->cpMin + plin->ccp);
        return fTrue;
    }

    cpLimChp = cpCur = plin->cpMin;
    cpLim = cpCur + plin->ccp;
    xpCur = 0;
    _FetchPap(cpCur, &pap);

    for (;;)
    {
        // 3DMMv1.0: make sure the chp is valid
        if (cpCur >= cpLimChp)
        {
            if (cpCur >= cpLim)
            {
                // 3DMMv1.0: everything fit
                *pcp = LwMax(plin->cpMin, _ptxtb->CpPrev(cpLim));
                return fTrue;
            }

            _FetchChp(cpCur, &chp, pvNil, &cpLimChp);
            cpLimChp = LwMin(cpLimChp, cpLim);
            Assert(cpLimChp > cpCur, "why is cpCur >= cpLimChp?");
            chr.Init(&chp, &pap, _ptxtb, _pgnv, cpCur, cpLimChp, xpCur, dxpDoc, xp);
        }

        chr.GetNextRun(fTrue);
        chr.GetChrd(&chrd);

        if (chr.FBreak() && (chrd.xpLim >= chr.XpBreak() || chrd.cpLim >= cpLim))
        {
            int32_t cpPrev = _ptxtb->CpPrev(chrd.cpLim);

            if (!fClosest || chrd.cpLim >= cpLim)
                goto LPrev;

            if (cpPrev > cpCur)
            {
                CHRD chrdT;

                // 3DMMv1.0: get the length from cpCur to cpPrev
                chr.Init(&chp, &pap, _ptxtb, _pgnv, cpCur, cpPrev, xpCur, dxpDoc, xp);
                chr.GetNextRun(fTrue);
                chr.GetChrd(&chrdT);
                cpCur = chrdT.cpLim;
                xpCur = chrdT.xpLim;
            }

            Assert(xp >= xpCur && chrd.xpLim >= xp, "what?");
            if (xp - xpCur > chrd.xpLim - xp)
            {
                *pcp = chrd.cpLim;
                return fTrue;
            }
        LPrev:
            *pcp = LwMax(plin->cpMin, cpPrev);
            return fTrue;
        }
        xpCur = chrd.xpLim;
        cpCur = chrd.cpLim;
    }
}

/** 3DMMv1.0: *************************************************************************
    Get the vertical bounds of the line containing cp and the horizontal
    position of the cp on the line.  If fView is true, the values are in
    view coordinates. If fView is false, the values are in logical values
    (independent of the current scrolling of the view).
***************************************************************************/
void TXTG::_GetXpYpFromCp(int32_t cp, int32_t *pypMin, int32_t *pypLim, int32_t *pxp, int32_t *pypBaseLine, bool fView)
{
    AssertThis(0);
    AssertIn(cp, 0, _ptxtb->CpMac());
    AssertNilOrVarMem(pypMin);
    AssertNilOrVarMem(pypLim);
    AssertNilOrVarMem(pxp);
    AssertNilOrVarMem(pypBaseLine);

    LIN lin;
    int32_t xp;

    _FindCp(cp, &lin);
    xp = lin.xpLeft;
    if (fView)
    {
        lin.dypTot -= _dypDisp;
        xp -= _scvHorz;
    }

    if (pvNil != pypMin)
        *pypMin = lin.dypTot;
    if (pvNil != pypLim)
        *pypLim = lin.dypTot + lin.dyp;
    if (pvNil != pxp)
        *pxp = xp + _DxpFromCp(lin.cpMin, cp);
    if (pvNil != pypBaseLine)
        *pypBaseLine = lin.dypTot + lin.dypAscent;
}

/** 3DMMv1.0: *************************************************************************
    Find the xp location of the given cp. Assumes that cpLine is the start
    of the line containing cp. This includes a buffer on the left of
    kdxpIndentTxtg, but doesn't include centering or right justification
    correction.
***************************************************************************/
int32_t TXTG::_DxpFromCp(int32_t cpLine, int32_t cp)
{
    AssertThis(0);
    AssertIn(cpLine, 0, _ptxtb->CpMac());
    AssertIn(cp, cpLine, _ptxtb->CpMac());

    CHP chp;
    PAP pap;
    CHR chr;
    int32_t cpCur, cpLimChp, cpLim;
    int32_t xpCur;
    CHRD chrd;

    cpLimChp = cpCur = cpLine;
    cpLim = cp + (cp == cpLine);
    xpCur = 0;
    _FetchPap(cpCur, &pap);

    for (;;)
    {
        // 3DMMv1.0: make sure the chp is valid
        if (cpCur >= cpLimChp)
        {
            _FetchChp(cpCur, &chp, pvNil, &cpLimChp);
            cpLimChp = LwMin(cpLimChp, cpLim);
            Assert(cpLimChp > cpCur, "why is cpCur >= cpLimChp?");
            chr.Init(&chp, &pap, _ptxtb, _pgnv, cpCur, cpLimChp, xpCur, kdxpMax, kdxpMax);
        }

        chr.GetNextRun();
        chr.GetChrd(&chrd);

        if (chrd.cpLim >= cp || chr.FBreak())
        {
            if (cp == cpLine)
                return chr.XpMin() + kdxpIndentTxtg;
            else
                return chrd.xpLim + kdxpIndentTxtg;
        }
        cpCur = chrd.cpLim;
        xpCur = chrd.xpLim;
    }
}

/** 3DMMv1.0: *************************************************************************
    Replaces the characters between cp1 and cp2 with the given ones.
***************************************************************************/
bool TXTG::FReplace(achar *prgch, int32_t cch, int32_t cp1, int32_t cp2)
{
    AssertThis(0);
    AssertIn(cch, 0, kcbMax);
    AssertPvCb(prgch, cch);
    AssertIn(cp1, 0, _ptxtb->CpMac());
    AssertIn(cp2, 0, _ptxtb->CpMac());

    HideSel();
    SortLw(&cp1, &cp2);
    if (!_ptxtb->FReplaceRgch(prgch, cch, cp1, cp2 - cp1))
        return fFalse;

    cp1 += cch;
    SetSel(cp1, cp1);
    ShowSel();
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Invalidate the display from cp.  If we're the active TXTG, also redraw.
***************************************************************************/
void TXTG::InvalCp(int32_t cp, int32_t ccpIns, int32_t ccpDel)
{
    AssertThis(0);
    AssertIn(cp, 0, _ptxtb->CpMac() + 1);
    AssertIn(ccpIns, 0, _ptxtb->CpMac() + 1 - cp);
    AssertIn(ccpDel, 0, kcbMax);
    Assert(!_fSelOn, "selection is on in InvalCp!");
    int32_t cpAnchor, cpOther;

    // 3DMMv1.0: adjust the sel
    cpAnchor = _cpAnchor;
    cpOther = _cpOther;
    FAdjustIv(&cpAnchor, cp, ccpIns, ccpDel);
    FAdjustIv(&cpOther, cp, ccpIns, ccpDel);
    if (cpAnchor != _cpAnchor || cpOther != _cpOther)
        SetSel(cpAnchor, cpOther, ginNil);

    _ReformatAndDraw(cp, ccpIns, ccpDel);

    if (pvNil != _ptrul)
    {
        PAP pap;

        _FetchPap(LwMin(_cpAnchor, _cpOther), &pap);
        _ptrul->SetDxpTab(pap.dxpTab);
        _ptrul->SetDxpDoc(_DxpDoc());
    }
}

/** 3DMMv1.0: *************************************************************************
    Reformat the TXTG and update the display.  If this TXTG is not the
    active one, the display is invalidated instead of updated.
***************************************************************************/
void TXTG::_ReformatAndDraw(int32_t cp, int32_t ccpIns, int32_t ccpDel)
{
    RC rcLoc, rc;
    int32_t yp, dypIns, dypDel;
    RC rcUpdate(0, 0, 0, 0);

    // 3DMMv1.0: reformat
    _Reformat(cp, ccpIns, ccpDel, &yp, &dypIns, &dypDel);

    // 3DMMv1.0: determine the dirty rectangles and if we're active, update them
    GetRc(&rcLoc, cooLocal);
    if (!_fActive)
    {
        rc = rcLoc;
        rc.ypTop = yp;
        if (dypIns == dypDel)
            rc.ypBottom = yp + dypIns;
        InvalRc(&rc);
        return;
    }

    rc = rcLoc;
    rc.ypTop = yp;
    rc.ypBottom = yp + dypIns;
    if (dypIns != dypDel)
    {
        // 3DMMv1.0: Have some bits to blt vertically. If the background isn't clear,
        // 3DMMv1.0: but _fMark is set, still do the scroll, since _fMark is intended
        // 3DMMv1.0: to avoid flashing (allowing offscreen drawing) and scrolling doesn't
        // 3DMMv1.0: flash anyway.
        if (_fClear)
            rc.ypBottom = rcLoc.ypBottom;
        else
        {
            rc = rcLoc;
            rc.ypTop = LwMax(rc.ypTop, yp + LwMin(dypIns, dypDel));
            Scroll(&rc, 0, dypIns - dypDel, _fMark ? kginMark : kginDraw);
            rc.ypBottom = rc.ypTop;
            rc.ypTop = yp;
        }
    }

    if (!rc.FEmpty())
        InvalRc(&rc, _fClear || _fMark ? kginMark : kginDraw);

    _fXpValid = fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Perform a scroll according to scaHorz and scaVert.
***************************************************************************/
void TXTG::_Scroll(int32_t scaHorz, int32_t scaVert, int32_t scvHorz, int32_t scvVert)
{
    RC rc;
    int32_t dxp, dyp;

    GetRc(&rc, cooLocal);
    dxp = 0;
    switch (scaHorz)
    {
    case scaPageUp:
        dxp = -LwMulDiv(rc.Dxp(), 9, 10);
        goto LHorz;
    case scaPageDown:
        dxp = LwMulDiv(rc.Dxp(), 9, 10);
        goto LHorz;
    case scaLineUp:
        dxp = -rc.Dxp() / 10;
        goto LHorz;
    case scaLineDown:
        dxp = rc.Dxp() / 10;
        goto LHorz;
    case scaToVal:
        dxp = scvHorz - _scvHorz;
    LHorz:
        dxp = LwBound(_scvHorz + dxp, 0, _ScvMax(fFalse) + 1) - _scvHorz;
        _scvHorz += dxp;

        if (pvNil != _ptrul)
            _ptrul->SetXpLeft(kdxpIndentTxtg - _scvHorz);
        break;
    }

    dyp = 0;
    if (scaVert != scaNil)
    {
        int32_t cpT;
        LIN lin, linDisp;
        int32_t ilin;
        RC rc;

        switch (scaVert)
        {
        case scaToVal:
            cpT = LwBound(scvVert, 0, _ptxtb->CpMac());
            _FindCp(cpT, &lin, &ilin);
            dyp = lin.dypTot - _dypDisp;
            _ilinDisp = ilin;
            _cpDisp = lin.cpMin;
            _dypDisp = lin.dypTot;
            break;

        case scaPageDown:
            // 3DMMv1.0: scroll down a page
            GetRc(&rc, cooLocal);
            _FindDyp(rc.Dyp() + _dypDisp, &lin, &ilin);

            if (lin.cpMin <= _cpDisp)
            {
                // 3DMMv1.0: we didn't go anywhere so force going down a line
                _FetchLin(_ilinDisp + 1, &lin, &ilin);
            }
            else if (lin.dypTot + lin.dyp > _dypDisp + rc.Dyp() && ilin > _ilinDisp + 1)
            {
                // 3DMMv1.0: the line crosses the bottom of the ddg so back up one
                Assert(ilin > 0, 0);
                _FetchLin(ilin - 1, &lin, &ilin);
            }

            dyp = lin.dypTot - _dypDisp;
            _ilinDisp = ilin;
            _cpDisp = lin.cpMin;
            _dypDisp = lin.dypTot;
            break;

        case scaLineDown:
            // 3DMMv1.0: scroll down a line
            _FetchLin(_ilinDisp + 1, &lin, &_ilinDisp);
            dyp = lin.dypTot - _dypDisp;
            _cpDisp = lin.cpMin;
            _dypDisp = lin.dypTot;
            break;

        case scaPageUp:
            // 3DMMv1.0: scroll down a page
            if (_ilinDisp <= 0)
                break;
            GetRc(&rc, cooLocal);
            _FetchLin(_ilinDisp, &linDisp);
            Assert(linDisp.dypTot == _dypDisp, 0);

            // 3DMMv1.0: determine where to scroll to - try to keep the top line
            // 3DMMv1.0: visible, but scroll up at least one line
            dyp = LwMax(0, linDisp.dypTot + linDisp.dyp - rc.Dyp());
            _FindDyp(dyp, &lin, &ilin);
            if (lin.cpMin >= linDisp.cpMin)
            {
                // 3DMMv1.0: we didn't go anywhere so force going up a line
                _FetchLin(_ilinDisp - 1, &lin, &ilin);
            }
            else if (linDisp.dypTot + linDisp.dyp > lin.dypTot + rc.Dyp() && ilin < _ilinDisp - 1)
            {
                // 3DMMv1.0: the previous disp line crosses the bottom of the ddg, so move
                // 3DMMv1.0: down one line
                _FetchLin(ilin + 1, &lin, &ilin);
            }

            dyp = lin.dypTot - _dypDisp;
            _ilinDisp = ilin;
            _cpDisp = lin.cpMin;
            _dypDisp = lin.dypTot;
            break;

        case scaLineUp:
            // 3DMMv1.0: scroll up a line
            if (_ilinDisp <= 0)
                break;
            _FetchLin(_ilinDisp - 1, &lin, &_ilinDisp);
            dyp = lin.dypTot - _dypDisp;
            _cpDisp = lin.cpMin;
            _dypDisp = lin.dypTot;
            break;
        }

        AssertIn(_cpDisp, 0, _ptxtb->CpMac());
        _scvVert = _cpDisp;
    }

    _SetScrollValues();
    if (dxp != 0 || dyp != 0)
        _ScrollDxpDyp(dxp, dyp);
}

/** 3DMMv1.0: *************************************************************************
    Move the bits in the window.
***************************************************************************/
void TXTG::_ScrollDxpDyp(int32_t dxp, int32_t dyp)
{
    AssertThis(0);
    RC rcLoc, rcBad1, rcBad2;

    // 3DMMv1.0: determine the dirty rectangles and update them
    GetRc(&rcLoc, cooLocal);
    if (_fClear)
    {
        InvalRc(&rcLoc, kginMark);
        vpappb->UpdateMarked();
        return;
    }

    Scroll(&rcLoc, -dxp, -dyp, _fMark ? kginMark : kginDraw);
    if (_fMark)
        vpappb->UpdateMarked();
}

/** 3DMMv1.0: *************************************************************************
    Update the display of the document.
***************************************************************************/
void TXTG::Draw(PGNV pgnv, RC *prcClip)
{
    AssertPo(pgnv, 0);
    AssertVarMem(prcClip);

    DrawLines(pgnv, prcClip, kdxpIndentTxtg - _scvHorz, 0, _ilinDisp);
    if (_fSelOn)
        _InvertSel(pgnv);
    _SetScrollValues();
}

/** 3DMMv1.0: *************************************************************************
    Draws some lines of the document.
***************************************************************************/
void TXTG::DrawLines(PGNV pgnv, RC *prcClip, int32_t dxp, int32_t dyp, int32_t ilinMin, int32_t ilinLim,
                     uint32_t grftxtg)
{
    AssertPo(pgnv, 0);
    AssertVarMem(prcClip);
    CHP chp;
    PAP pap;
    CHR chr;
    CHRD chrd;
    RC rc;
    int32_t xpBase, xp, yp, xpChr, dxpDoc;
    int32_t cpLine, cpCur, cpLimChp, cpLim, cpLimLine;
    LIN lin;
    int32_t ilin;

    cpLim = _ptxtb->CpMac();
    dxpDoc = _DxpDoc();
    _FetchLin(ilinMin, &lin);
    cpLine = lin.cpMin;

    pgnv->FillRc(prcClip, _ptxtb->AcrBack());
    yp = dyp;
    for (ilin = ilinMin; ilin < ilinLim; ilin++)
    {
        if (yp >= prcClip->ypBottom || cpLine >= cpLim)
            break;

        _FetchLin(ilin, &lin);
        if (yp + lin.dyp <= prcClip->ypTop)
        {
            yp += lin.dyp;
            cpLine += lin.ccp;
            continue;
        }

        _FetchPap(cpLine, &pap);
        xpBase = lin.xpLeft + dxp;
        cpLimChp = cpCur = cpLine;
        cpLimLine = cpLine + lin.ccp;
        xpChr = 0;

        // 3DMMv1.0: draw the line
        for (;;)
        {
            // 3DMMv1.0: make sure the chp is valid
            if (cpCur >= cpLimChp)
            {
                _FetchChp(cpCur, &chp, pvNil, &cpLimChp);
                cpLimChp = LwMin(cpLimChp, cpLimLine);
                Assert(cpLimChp > cpCur, "why is cpCur >= cpLimChp?");
                chr.Init(&chp, &pap, _ptxtb, _pgnv, cpCur, cpLimChp, xpChr, dxpDoc, dxpDoc);
            }

            chr.GetNextRun(fTrue);
            chr.GetChrd(&chrd);

            if (chrd.cpLimDraw > cpCur)
            {
                // 3DMMv1.0: draw some text
                xp = xpBase + chr.XpMin();

                if (chr.FObject())
                {
                    // 3DMMv1.0: draw the object
                    _ptxtb->FDrawObject(chr.CpMin(), pgnv, &xp, yp + lin.dypAscent + chp.dypOffset, &chp, prcClip);
                }
                else
                {
                    pgnv->SetFont(chp.onn, chp.grfont, chp.dypFont, tahLeft, tavBaseline);

                    pgnv->DrawRgch(chr.Prgch(), chrd.cpLimDraw - chr.CpMin(), xp, yp + lin.dypAscent + chp.dypOffset,
                                   chp.acrFore, chp.acrBack);
                }
            }

            if (chrd.cpLim >= cpLimLine || chr.FBreak())
                break;

            cpCur = chrd.cpLim;
            xpChr = chrd.xpLim;
        }

        _DrawLinExtra(pgnv, prcClip, &lin, dxp, yp, grftxtg);

        yp += lin.dyp;
        cpLine = cpLimLine;
    }
}

/** 3DMMv1.0: *************************************************************************
    Gives a subclass an opportunity to draw extra stuff associated with
    the line. Default does nothing.
***************************************************************************/
void TXTG::_DrawLinExtra(PGNV pgnv, PRC prcClip, LIN *plin, int32_t dxp, int32_t yp, uint32_t grftxtg)
{
}

/** 3DMMv1.0: *************************************************************************
    Handle a mousedown in the TXTG.
***************************************************************************/
bool TXTG::FCmdTrackMouse(PCMD_MOUSE pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    RC rc;
    int32_t cp;
    int32_t scaHorz, scaVert;
    int32_t xp = pcmd->xp;
    int32_t yp = pcmd->yp;

    if (pcmd->cid == cidMouseDown)
    {
        Assert(vpcex->PgobTracking() == pvNil, "mouse already being tracked!");
        _fSelByWord = (pcmd->cact > 1) && !(pcmd->grfcust & fcustShift);
        vpcex->TrackMouse(this);
    }
    else
    {
        Assert(vpcex->PgobTracking() == this, "not tracking mouse!");
        Assert(pcmd->cid == cidTrackMouse, 0);
    }

    // 3DMMv1.0: do autoscrolling
    GetRc(&rc, cooLocal);
    if (!FIn(xp, rc.xpLeft, rc.xpRight))
    {
        scaHorz = (xp < rc.xpLeft) ? scaLineUp : scaLineDown;
        xp = LwBound(xp, rc.xpLeft, rc.xpRight);
    }
    else
        scaHorz = scaNil;
    if (!FIn(yp, rc.ypTop, rc.ypBottom))
    {
        scaVert = (yp < rc.ypTop) ? scaLineUp : scaLineDown;
        yp = LwBound(yp, rc.ypTop, rc.ypBottom);
    }
    else
        scaVert = scaNil;
    if (scaHorz != scaNil || scaVert != scaNil)
        _Scroll(scaHorz, scaVert);

    // 3DMMv1.0: set the selection
    if (_FGetCpFromPt(xp, yp, &cp, !_fSelByWord))
    {
        if (pcmd->cid != cidMouseDown || (pcmd->grfcust & fcustShift))
        {
            if (_fSelByWord)
            {
                cp = (cp < _cpAnchor) ? _ptxtb->CpPrev(cp + 1, fTrue) : _ptxtb->CpNext(cp, fTrue);
            }
            SetSel(_cpAnchor, cp);
        }
        else
        {
            if (_fSelByWord)
                cp = _ptxtb->CpPrev(cp + 1, fTrue);
            SetSel(cp, _fSelByWord ? _ptxtb->CpNext(cp, fTrue) : cp);
        }
        _fXpValid = fFalse;
    }
    _SwitchSel(fTrue); // 3DMMv1.0: make sure the selection is on

    if (!(pcmd->grfcust & fcustMouse))
        vpcex->EndMouseTracking();

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Do idle processing.  If this handler has the active selection, make sure
    the selection is on or off according to rglw[0] (non-zero means on)
    and set rglw[0] to false.  Always return false.
***************************************************************************/
bool TXTG::FCmdSelIdle(PCMD pcmd)
{
    AssertThis(0);

    // 3DMMv1.0: if rglw[1] is this one's hid, don't change the sel state.
    if (pcmd->rglw[1] != Hid())
    {
        if (!pcmd->rglw[0])
            _SwitchSel(fFalse, kginDefault);
        else if (_cpAnchor != _cpOther || _tsSel == 0)
            _SwitchSel(fTrue);
        else if (DtsCaret() < TsCurrent() - _tsSel)
            _SwitchSel(!_fSelOn);
    }
    pcmd->rglw[0] = fFalse;
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Get the current selection.
***************************************************************************/
void TXTG::GetSel(int32_t *pcpAnchor, int32_t *pcpOther)
{
    AssertThis(0);
    AssertVarMem(pcpAnchor);
    AssertVarMem(pcpOther);

    *pcpAnchor = _cpAnchor;
    *pcpOther = _cpOther;
}

/** 3DMMv1.0: *************************************************************************
    Set the selection.
***************************************************************************/
void TXTG::SetSel(int32_t cpAnchor, int32_t cpOther, int32_t gin)
{
    AssertThis(0);
    int32_t cpMac = _ptxtb->CpMac();

    cpAnchor = LwBound(cpAnchor, 0, cpMac);
    cpOther = LwBound(cpOther, 0, cpMac);

    if (cpAnchor == _cpAnchor && cpOther == _cpOther)
        return;

    if (_fSelOn)
    {
        if ((_fMark || _fClear) && gin == kginDraw)
            gin = kginMark;
        _pgnv->SetGobRc(this);
        if (_cpAnchor != cpAnchor || _cpAnchor == _cpOther || cpAnchor == cpOther)
        {
            _InvertSel(_pgnv, gin);
            _cpAnchor = cpAnchor;
            _cpOther = cpOther;
            _InvertSel(_pgnv, gin);
            _tsSel = TsCurrent();
        }
        else
        {
            // 3DMMv1.0: they have the same anchor and neither is an insertion
            _InvertCpRange(_pgnv, _cpOther, cpOther, gin);
            _cpOther = cpOther;
        }
    }
    else
    {
        _cpAnchor = cpAnchor;
        _cpOther = cpOther;
        _tsSel = 0L;
    }
}

/** 3DMMv1.0: *************************************************************************
    Make sure the selection is visible (at least the _cpOther end of it).
***************************************************************************/
void TXTG::ShowSel(void)
{
    AssertThis(0);
    int32_t cpScroll;
    int32_t ilinOther, ilinAnchor, ilin;
    int32_t dxpScroll, dypSel;
    int32_t xpMin, xpLim;
    RC rc;
    LIN linOther, linAnchor, lin;
    int32_t cpAnchor = _cpAnchor;

    // 3DMMv1.0: find the lines we want to show
    _FindCp(_cpOther, &linOther, &ilinOther);
    _FindCp(_cpAnchor, &linAnchor, &ilinAnchor);
    linOther.dypTot -= _dypDisp;
    linAnchor.dypTot -= _dypDisp;
    GetRc(&rc, cooLocal);

    cpScroll = _cpDisp;
    if (!FIn(linOther.dypTot, 0, rc.Dyp() - linOther.dyp) || !FIn(linAnchor.dypTot, 0, rc.Dyp() - linAnchor.dyp))
    {
        if (_cpOther < cpAnchor)
            dypSel = linAnchor.dypTot - linOther.dypTot + linAnchor.dyp;
        else
            dypSel = linOther.dypTot - linAnchor.dypTot + linOther.dyp;
        if (dypSel > rc.Dyp())
        {
            // 3DMMv1.0: just show _cpOther
            cpAnchor = _cpOther;
            ilinAnchor = ilinOther;
            linAnchor = linOther;
            dypSel = linOther.dyp;
        }

        if (linOther.dypTot < 0 || linAnchor.dypTot < 0)
        {
            // 3DMMv1.0: scroll up
            cpScroll = LwMin(linOther.cpMin, linAnchor.cpMin);
        }
        else if (linOther.dypTot + linOther.dyp >= rc.Dyp() || linAnchor.dypTot + linAnchor.dyp >= rc.Dyp())
        {
            // 3DMMv1.0: scroll down
            ilin = LwMin(ilinOther, ilinAnchor);
            cpScroll = LwMin(linOther.cpMin, linAnchor.cpMin);
            dypSel = rc.Dyp() - dypSel;
            for (;;)
            {
                if (--ilin < 0)
                    break;
                _FetchLin(ilin, &lin);
                if (lin.dyp > dypSel)
                    break;
                cpScroll -= lin.ccp;
                dypSel -= lin.dyp;
            }
        }
    }

    // 3DMMv1.0: now do the horizontal stuff
    xpMin = _DxpFromCp(linOther.cpMin, _cpOther) + linOther.xpLeft - _scvHorz;
    xpLim = _DxpFromCp(linAnchor.cpMin, cpAnchor) + linAnchor.xpLeft - _scvHorz;
    if (LwAbs(xpLim - xpMin) > rc.Dxp())
    {
        // 3DMMv1.0: can't show both
        if (xpMin > xpLim)
        {
            xpLim = xpMin;
            xpMin = xpLim - rc.Dxp();
        }
        else
            xpLim = xpMin + rc.Dxp();
    }
    else
        SortLw(&xpMin, &xpLim);
    dxpScroll = LwMax(LwMin(0, rc.xpRight - xpLim), rc.xpLeft - xpMin);

    if (dxpScroll != 0 || cpScroll != _cpDisp)
        _Scroll(scaToVal, scaToVal, _scvHorz - dxpScroll, cpScroll);
}

/** 3DMMv1.0: *************************************************************************
    Turn the selection off.
***************************************************************************/
void TXTG::HideSel(void)
{
    AssertThis(0);

    if (!_fSelOn)
        return;

    _SwitchSel(fFalse);
    _tsSel = 0;
}

/** 3DMMv1.0: *************************************************************************
    Turn the sel on or off according to fOn.
***************************************************************************/
void TXTG::_SwitchSel(bool fOn, int32_t gin)
{
    AssertThis(0);

    if (FPure(fOn) != FPure(_fSelOn))
    {
        if ((_fMark || _fClear) && gin == kginDraw)
            gin = kginMark;
        _pgnv->SetGobRc(this);
        _InvertSel(_pgnv, gin);
        _fSelOn = FPure(fOn);
        _tsSel = TsCurrent();
    }
}

/** 3DMMv1.0: *************************************************************************
    Invert the current selection.
***************************************************************************/
void TXTG::_InvertSel(PGNV pgnv, int32_t gin)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    RC rc, rcT;

    GetRc(&rc, cooLocal);
    if (_cpAnchor == _cpOther)
    {
        // 3DMMv1.0: insertion bar
        _GetXpYpFromCp(_cpAnchor, &rcT.ypTop, &rcT.ypBottom, &rcT.xpLeft);
        rcT.xpRight = --rcT.xpLeft + 2;
        if (rcT.FIntersect(&rc))
        {
            if (kginDraw == gin)
                pgnv->FillRc(&rcT, kacrInvert);
            else
                InvalRc(&rcT, gin);
        }
    }
    else
        _InvertCpRange(pgnv, _cpAnchor, _cpOther, gin);
}

/** 3DMMv1.0: *************************************************************************
    Invert a range.
***************************************************************************/
void TXTG::_InvertCpRange(PGNV pgnv, int32_t cp1, int32_t cp2, int32_t gin)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    AssertIn(cp1, 0, _ptxtb->CpMac());
    AssertIn(cp2, 0, _ptxtb->CpMac());
    RC rc1, rc2, rcClip, rcT, rcDoc;

    if (cp1 == cp2)
        return;
    SortLw(&cp1, &cp2);
    _GetXpYpFromCp(cp1, &rc1.ypTop, &rc1.ypBottom, &rc1.xpLeft);
    _GetXpYpFromCp(cp2, &rc2.ypTop, &rc2.ypBottom, &rc2.xpRight);
    GetRc(&rcClip, cooLocal);
    if (rc1.ypTop >= rcClip.ypBottom || rc2.ypBottom <= rcClip.ypTop)
        return;

    if (rc1.ypTop == rc2.ypTop && rc1.ypBottom == rc2.ypBottom)
    {
        // 3DMMv1.0: only one line involved
        rc1.xpRight = rc2.xpRight;
        if (rcT.FIntersect(&rc1, &rcClip))
        {
            if (kginDraw == gin)
                pgnv->HiliteRc(&rcT, kacrWhite);
            else
                InvalRc(&rcT, gin);
        }
        return;
    }

    // 3DMMv1.0: invert the sel on the first line
    rc1.xpRight = kdxpIndentTxtg - _scvHorz + _DxpDoc();
    if (rcT.FIntersect(&rc1, &rcClip))
    {
        if (kginDraw == gin)
            pgnv->HiliteRc(&rcT, kacrWhite);
        else
            InvalRc(&rcT, gin);
    }

    // 3DMMv1.0: invert the main rectangular block
    rc1.xpLeft = kdxpIndentTxtg - _scvHorz;
    rc1.ypTop = rc1.ypBottom;
    rc1.ypBottom = rc2.ypTop;
    if (rcT.FIntersect(&rc1, &rcClip))
    {
        if (kginDraw == gin)
            pgnv->HiliteRc(&rcT, kacrWhite);
        else
            InvalRc(&rcT, gin);
    }

    // 3DMMv1.0: invert the last line
    rc2.xpLeft = rc1.xpLeft;
    if (rcT.FIntersect(&rc2, &rcClip))
    {
        if (kginDraw == gin)
            pgnv->HiliteRc(&rcT, kacrWhite);
        else
            InvalRc(&rcT, gin);
    }
}

/** 3DMMv1.0: *************************************************************************
    Handle a key down.
***************************************************************************/
bool TXTG::FCmdKey(PCMD_KEY pcmd)
{
    const int32_t kcchInsBuf = 64;
    AssertThis(0);
    AssertVarMem(pcmd);
    uint32_t grfcust;
    int32_t vkDone;
    int32_t ichLim;
    int32_t cact;
    int32_t dcp, cpT;
    CMD cmd;
    LIN lin, linT;
    int32_t ilin, ilinT;
    RC rc;
    achar rgch[kcchInsBuf + 1];

    // 3DMMv1.0: keep fetching characters until we get a cursor key, delete key or
    // 3DMMv1.0: until the buffer is full.
    vkDone = vkNil;
    ichLim = 0;
    do
    {
        grfcust = pcmd->grfcust;
        switch (pcmd->vk)
        {
        // 3DMMv1.0: these keys all terminate the key fetching loop
        case kvkHome:
        case kvkEnd:
        case kvkLeft:
        case kvkRight:
        case kvkUp:
        case kvkDown:
        case kvkPageUp:
        case kvkPageDown:
        case kvkDelete:
        case kvkBack:
            vkDone = pcmd->vk;
            goto LInsert;

        default:
            if (chNil == pcmd->ch)
                break;
            for (cact = 0; cact < pcmd->cact && ichLim < kcchInsBuf; cact++)
            {
                rgch[ichLim++] = (achar)pcmd->ch;
#ifdef WIN
                if ((achar)pcmd->ch == kchReturn)
                    rgch[ichLim++] = kchLineFeed;
#endif // 3DMMv1.0: WIN
            }
            break;
        }

        pcmd = (PCMD_KEY)&cmd;
    } while (ichLim < kcchInsBuf && vpcex->FGetNextKey(&cmd));

LInsert:
    if (ichLim > 0)
    {
        // 3DMMv1.0: have some characters to insert
        FReplace(rgch, ichLim, _cpAnchor, _cpOther);
    }

    dcp = 0;
    switch (vkDone)
    {
    case kvkHome:
        if (grfcust & fcustCmd)
            dcp = -_cpOther;
        else
        {
            _FindCp(_cpOther, &lin);
            dcp = lin.cpMin - _cpOther;
        }
        _fXpValid = fFalse;
        goto LSetSel;

    case kvkEnd:
        if (grfcust & fcustCmd)
            dcp = _ptxtb->CpMac() - _cpOther;
        else
        {
            _FindCp(_cpOther, &lin);
            cpT = _ptxtb->CpPrev(lin.cpMin + lin.ccp);
            AssertIn(cpT, _cpOther, _ptxtb->CpMac());
            dcp = cpT - _cpOther;
        }
        _fXpValid = fFalse;
        goto LSetSel;

    case kvkLeft:
        dcp = _ptxtb->CpPrev(_cpOther, FPure(grfcust & fcustCmd)) - _cpOther;
        _fXpValid = fFalse;
        goto LSetSel;

    case kvkRight:
        dcp = _ptxtb->CpNext(_cpOther, FPure(grfcust & fcustCmd)) - _cpOther;
        _fXpValid = fFalse;
        goto LSetSel;

    case kvkUp:
    case kvkPageUp:
    case kvkDown:
    case kvkPageDown:
        // 3DMMv1.0: get the LIN for _cpOther and make sure _xpSel is up to date
        _FindCp(_cpOther, &lin, &ilin);
        if (!_fXpValid)
        {
            // 3DMMv1.0: get the xp of _cpOther
            _xpSel = lin.xpLeft + _DxpFromCp(lin.cpMin, _cpOther);
            _fXpValid = fTrue;
        }

        switch (vkDone)
        {
        case kvkUp:
            if (ilin == 0)
            {
                dcp = -_cpOther;
                goto LSetSel;
            }
            _FetchLin(ilin - 1, &lin);
            break;

        case kvkPageUp:
            if (ilin == 0)
            {
                dcp = -_cpOther;
                goto LSetSel;
            }
            GetRc(&rc, cooLocal);
            _FindDyp(LwMax(0, lin.dypTot - rc.Dyp() + lin.dyp), &linT, &ilinT);

            if (linT.cpMin >= lin.cpMin)
            {
                // 3DMMv1.0: we didn't go anywhere so force going up a line
                _FetchLin(ilin - 1, &lin);
            }
            else if (lin.dypTot + lin.dyp > linT.dypTot + rc.Dyp() && ilinT < ilin - 1)
            {
                // 3DMMv1.0: the previous line crosses the bottom of the ddg, so move
                // 3DMMv1.0: down one line
                _FetchLin(ilinT + 1, &lin);
            }
            else
                lin = linT;
            break;

        case kvkDown:
            if (lin.cpMin + lin.ccp >= _ptxtb->CpMac())
            {
                dcp = _ptxtb->CpMac() - _cpOther;
                goto LSetSel;
            }
            _FetchLin(ilin + 1, &lin);
            break;

        case kvkPageDown:
            if (lin.cpMin + lin.ccp >= _ptxtb->CpMac())
            {
                dcp = _ptxtb->CpMac() - _cpOther;
                goto LSetSel;
            }

            GetRc(&rc, cooLocal);
            _FindDyp(lin.dypTot + rc.Dyp(), &linT, &ilinT);

            if (linT.cpMin <= lin.cpMin)
            {
                // 3DMMv1.0: we didn't go anywhere so force going down a line
                _FetchLin(ilin + 1, &lin);
            }
            else if (linT.dypTot + linT.dyp > lin.dypTot + rc.Dyp() && ilinT > ilin + 1)
            {
                // 3DMMv1.0: the line crosses the bottom of the ddg so back up one line
                Assert(ilinT > 0, 0);
                _FetchLin(ilinT - 1, &lin);
            }
            else
                lin = linT;
            break;
        }

        // 3DMMv1.0: we have the line, now find the position on the line
        _FGetCpFromXp(_xpSel - _scvHorz, &lin, &dcp);
        dcp -= _cpOther;

    LSetSel:
        // 3DMMv1.0: move the selection
        if (grfcust & fcustShift)
        {
            // 3DMMv1.0: extend selection
            SetSel(_cpAnchor, _cpOther + dcp);
            ShowSel();
        }
        else
        {
            cpT = _cpOther;
            if (cpT == _cpAnchor || (grfcust & fcustCmd))
                cpT += dcp;
            else if ((dcp > 0) != (cpT > _cpAnchor))
                cpT = _cpAnchor;
            SetSel(cpT, cpT);
            ShowSel();
        }
        break;

    case kvkDelete:
    case kvkBack:
        if (_cpAnchor != _cpOther)
            dcp = _cpOther - _cpAnchor;
        else if (vkDone == kvkDelete)
        {
            dcp = _ptxtb->CpNext(_cpAnchor) - _cpAnchor;
            if (_cpAnchor + dcp >= _ptxtb->CpMac())
                dcp = 0;
        }
        else
            dcp = _ptxtb->CpPrev(_cpAnchor) - _cpAnchor;
        if (dcp != 0)
            FReplace(pvNil, 0, _cpAnchor, _cpAnchor + dcp);
        break;
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Return the maximum scroll value for this view of the doc.
***************************************************************************/
int32_t TXTG::_ScvMax(bool fVert)
{
    RC rc;
    int32_t dxp;

    if (fVert)
        return _ptxtb->CpMac() - 1;

    dxp = _DxpDoc() + 2 * kdxpIndentTxtg;
    GetRc(&rc, cooLocal);
    return LwMax(0, dxp - rc.Dxp());
}

/** 3DMMv1.0: *************************************************************************
    Return the logical width of the text "page".
***************************************************************************/
int32_t TXTG::_DxpDoc(void)
{
    return _ptxtb->DxpDef();
}

/** 3DMMv1.0: *************************************************************************
    Set the tab width.  Default does nothing.
***************************************************************************/
void TXTG::SetDxpTab(int32_t dxp)
{
    AssertThis(0);
}

/** 3DMMv1.0: *************************************************************************
    Set the document width.  Default calls SetDxpDef on the TXTB.
***************************************************************************/
void TXTG::SetDxpDoc(int32_t dxp)
{
    AssertThis(0);
    dxp = LwBound(dxp, 1, kcbMax);
    _ptxtb->SetDxpDef(dxp);
}

/** 3DMMv1.0: *************************************************************************
    Show or hide the ruler.
***************************************************************************/
void TXTG::ShowRuler(bool fShow)
{
    AssertThis(0);
    RC rcAbs, rcRel;
    int32_t dyp;

    if (FPure(fShow) == (_ptrul != pvNil))
        return;

    dyp = _DypTrul();
    if (fShow)
    {
        PGOB pgob;
        GCB gcb;

        pgob = PgobPar();
        if (pvNil == pgob || !pgob->FIs(kclsDSG))
            return;

        GetPos(&rcAbs, &rcRel);
        rcAbs.ypTop += dyp;
        SetPos(&rcAbs, &rcRel);

        rcRel.ypBottom = rcRel.ypTop;
        rcAbs.ypBottom = rcAbs.ypTop;
        rcAbs.ypTop -= dyp;
        gcb.Set(HidUnique(), pgob, fgobNil, kginDefault, &rcAbs, &rcRel);

        if (pvNil == (_ptrul = _PtrulNew(&gcb)))
            goto LFail;
    }
    else
    {
        ReleasePpo(&_ptrul);
    LFail:
        GetPos(&rcAbs, &rcRel);
        rcAbs.ypTop -= dyp;
        SetPos(&rcAbs, &rcRel);
    }
}

/** 3DMMv1.0: *************************************************************************
    Return the height of the ruler.
***************************************************************************/
int32_t TXTG::_DypTrul(void)
{
    AssertThis(0);
    return 0;
}

/** 3DMMv1.0: *************************************************************************
    Create the ruler.
***************************************************************************/
PTRUL TXTG::_PtrulNew(PGCB pgcb)
{
    AssertThis(0);
    return pvNil;
}

/** 3DMMv1.0: *************************************************************************
    Get the natural width and height of the view on the document.
***************************************************************************/
void TXTG::GetNaturalSize(int32_t *pdxp, int32_t *pdyp)
{
    AssertThis(0);
    AssertNilOrVarMem(pdxp);
    AssertNilOrVarMem(pdyp);
    LIN lin;

    if (pvNil != pdxp)
        *pdxp = _DxpDoc() + 2 * kdxpIndentTxtg;
    if (pvNil == pdyp)
        return;

    _FetchLin(kcbMax - 1, &lin);
    *pdyp = lin.dypTot + lin.dyp;
}

/** 3DMMv1.0: *************************************************************************
    Constructor for the plain line text document display gob.
***************************************************************************/
TXLG::TXLG(PTXTB ptxtb, PGCB pgcb, int32_t onn, uint32_t grfont, int32_t dypFont, int32_t cchTab)
    : TXLG_PAR(ptxtb, pgcb)
{
    RC rc;
    achar ch = kchSpace;
    GNV gnv(this);

    gnv.SetFont(onn, fontNil, dypFont);
    gnv.GetRcFromRgch(&rc, &ch, 1);
    _dxpChar = rc.Dxp();
    _onn = onn;
    _grfont = grfont;
    _dypFont = dypFont;
    _cchTab = LwBound(cchTab, 1, kcbMax);
}

/** 3DMMv1.0: *************************************************************************
    Static method to create a new plain line text doc display gob.
***************************************************************************/
PTXLG TXLG::PtxlgNew(PTXTB ptxtb, PGCB pgcb, int32_t onn, uint32_t grfont, int32_t dypFont, int32_t cchTab)
{
    PTXLG ptxlg;

    if (pvNil == (ptxlg = NewObj TXLG(ptxtb, pgcb, onn, grfont, dypFont, cchTab)))
        return pvNil;
    if (!ptxlg->_FInit())
    {
        ReleasePpo(&ptxlg);
        return pvNil;
    }
    ptxlg->Activate(fTrue);
    return ptxlg;
}

/** 3DMMv1.0: *************************************************************************
    Get the width of the logical "page".  For a TXLG, this is some big
    value, so we do no word wrap.
***************************************************************************/
int32_t TXLG::_DxpDoc(void)
{
    return kswMax;
}

/** 3DMMv1.0: *************************************************************************
    Set the tab width.
***************************************************************************/
void TXLG::SetDxpTab(int32_t dxp)
{
    AssertThis(0);
    int32_t cch;

    cch = LwBound(dxp / _dxpChar, 1, kswMax / _dxpChar);
    if (cch != _cchTab)
    {
        _cchTab = cch;
        InvalCp(0, _ptxtb->CpMac(), _ptxtb->CpMac());
    }
}

/** 3DMMv1.0: *************************************************************************
    Set the document width.  Does nothing.
***************************************************************************/
void TXLG::SetDxpDoc(int32_t dxp)
{
    AssertThis(0);
}

/** 3DMMv1.0: *************************************************************************
    Get the character properties for display.  These are the same for all
    characters in the TXLG.
***************************************************************************/
void TXLG::_FetchChp(int32_t cp, PCHP pchp, int32_t *pcpMin, int32_t *pcpLim)
{
    AssertIn(cp, 0, _ptxtb->CpMac());
    AssertVarMem(pchp);
    AssertNilOrVarMem(pcpMin);
    AssertNilOrVarMem(pcpLim);

    pchp->Clear();
    pchp->grfont = _grfont;
    pchp->onn = _onn;
    pchp->dypFont = _dypFont;
    pchp->acrFore = kacrBlack;
    pchp->acrBack = kacrWhite;
    if (pvNil != pcpMin)
        *pcpMin = 0;
    if (pvNil != pcpLim)
        *pcpLim = _ptxtb->CpMac();
}

/** 3DMMv1.0: *************************************************************************
    Get the paragraph properties for disply.  These are constant for all
    characters in the TXLG.
***************************************************************************/
void TXLG::_FetchPap(int32_t cp, PPAP ppap, int32_t *pcpMin, int32_t *pcpLim)
{
    AssertIn(cp, 0, _ptxtb->CpMac());
    AssertVarMem(ppap);
    AssertNilOrVarMem(pcpMin);
    AssertNilOrVarMem(pcpLim);

    ClearPb(ppap, SIZEOF(PAP));
    ppap->dxpTab = (int16_t)LwMul(_cchTab, _dxpChar);
    ppap->numLine = kdenLine;
    ppap->numAfter = kdenAfter;
    if (pvNil != pcpMin)
        *pcpMin = 0;
    if (pvNil != pcpLim)
        *pcpLim = _ptxtb->CpMac();
}

/** 3DMMv1.0: *************************************************************************
    Copy the selection.
***************************************************************************/
bool TXLG::_FCopySel(PDOCB *ppdocb)
{
    AssertThis(0);
    AssertNilOrVarMem(ppdocb);
    PTXPD ptxpd;

    if (_cpAnchor == _cpOther)
        return fFalse;

    if (pvNil == ppdocb)
        return fTrue;

    if (pvNil != (ptxpd = TXPD::PtxpdNew(pvNil)))
    {
        int32_t cpMin = LwMin(_cpAnchor, _cpOther);
        int32_t cpLim = LwMax(_cpAnchor, _cpOther);

        ptxpd->SuspendUndo();
        if (!ptxpd->FReplaceBsf(_ptxtb->Pbsf(), cpMin, cpLim - cpMin, 0, 0, fdocNil))
        {
            ReleasePpo(&ptxpd);
        }
        else
            ptxpd->ResumeUndo();
    }

    *ppdocb = ptxpd;
    return pvNil != *ppdocb;
}

/** 3DMMv1.0: *************************************************************************
    Delete the selection.
***************************************************************************/
void TXLG::_ClearSel(void)
{
    AssertThis(0);

    FReplace(pvNil, 0, _cpAnchor, _cpOther);
    ShowSel();
}

/** 3DMMv1.0: *************************************************************************
    Paste the selection.
***************************************************************************/
bool TXLG::_FPaste(PCLIP pclip, bool fDoIt, int32_t cid)
{
    AssertThis(0);
    AssertPo(pclip, 0);
    int32_t cp1, cp2;
    int32_t ccp;
    PTXTB ptxtb;

    if (cid != cidPaste || !pclip->FGetFormat(kclsTXTB))
        return fFalse;

    if (!fDoIt)
        return fTrue;

    if (!pclip->FGetFormat(kclsTXTB, (PDOCB *)&ptxtb))
        return fFalse;

    AssertPo(ptxtb, 0);
    if ((ccp = ptxtb->CpMac() - 1) <= 0)
    {
        ReleasePpo(&ptxtb);
        return fTrue;
    }

    HideSel();
    cp1 = LwMin(_cpAnchor, _cpOther);
    cp2 = LwMax(_cpAnchor, _cpOther);
    if (!_ptxtb->FReplaceTxtb(ptxtb, 0, ccp, cp1, cp2 - cp1))
    {
        ReleasePpo(&ptxtb);
        return fFalse;
    }
    ReleasePpo(&ptxtb);

    cp1 += ccp;
    SetSel(cp1, cp1);
    ShowSel();
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Constructor for a rich text document display gob.
***************************************************************************/
TXRG::TXRG(PTXRD ptxrd, PGCB pgcb) : TXRG_PAR(ptxrd, pgcb)
{
}

/** 3DMMv1.0: *************************************************************************
    Create a new rich text document display GOB.
***************************************************************************/
PTXRG TXRG::PtxrgNew(PTXRD ptxrd, PGCB pgcb)
{
    PTXRG ptxrg;

    if (pvNil == (ptxrg = NewObj TXRG(ptxrd, pgcb)))
        return pvNil;
    if (!ptxrg->_FInit())
    {
        ReleasePpo(&ptxrg);
        return pvNil;
    }
    ptxrg->Activate(fTrue);
    return ptxrg;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a TXRG.
***************************************************************************/
void TXRG::AssertValid(uint32_t grf)
{
    TXRG_PAR::AssertValid(0);
    AssertNilOrPo(_ptrul, 0);
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Get the character properties for displaying the given cp.
***************************************************************************/
void TXRG::_FetchChp(int32_t cp, PCHP pchp, int32_t *pcpMin, int32_t *pcpLim)
{
    ((PTXRD)_ptxtb)->FetchChp(cp, pchp, pcpMin, pcpLim);
}

/** 3DMMv1.0: *************************************************************************
    Get the paragraph properties for displaying the given cp.
***************************************************************************/
void TXRG::_FetchPap(int32_t cp, PPAP ppap, int32_t *pcpMin, int32_t *pcpLim)
{
    ((PTXRD)_ptxtb)->FetchPap(cp, ppap, pcpMin, pcpLim);
}

/** 3DMMv1.0: *************************************************************************
    Set the tab width for the currently selected paragraph(s).
***************************************************************************/
void TXRG::SetDxpTab(int32_t dxp)
{
    AssertThis(0);
    int32_t cpMin, cpLim, cpAnchor, cpOther;
    PAP papOld, papNew;

    dxp = LwRoundClosest(dxp, kdzpInch / 8);
    dxp = LwBound(dxp, kdzpInch / 8, 100 * kdzpInch);
    ClearPb(&papOld, SIZEOF(PAP));
    papNew = papOld;

    cpMin = LwMin(cpAnchor = _cpAnchor, cpOther = _cpOther);
    cpLim = LwMax(_cpAnchor, _cpOther);

    papNew.dxpTab = (int16_t)dxp;
    _SwitchSel(fFalse, ginNil);
    if (!((PTXRD)_ptxtb)->FApplyPap(cpMin, cpLim - cpMin, &papNew, &papOld, &cpMin, &cpLim))
    {
        _SwitchSel(fTrue, kginMark);
    }

    SetSel(cpAnchor, cpOther);
    ShowSel();
}

/** 3DMMv1.0: *************************************************************************
    Set the selection for the TXRG.  Invalidates _chpIns if the selection
    changes.
***************************************************************************/
void TXRG::SetSel(int32_t cpAnchor, int32_t cpOther, int32_t gin)
{
    AssertThis(0);
    int32_t cpMac = _ptxtb->CpMac();

    cpAnchor = LwBound(cpAnchor, 0, cpMac);
    cpOther = LwBound(cpOther, 0, cpMac);

    if (cpAnchor == _cpAnchor && cpOther == _cpOther)
        return;

    _fValidChp = fFalse;
    TXRG_PAR::SetSel(cpAnchor, cpOther, gin);
    if (pvNil != _ptrul)
    {
        PAP pap;

        _FetchPap(LwMin(_cpAnchor, _cpOther), &pap);
        _ptrul->SetDxpTab(pap.dxpTab);
    }
}

/** 3DMMv1.0: *************************************************************************
    Get the chp to use to replace the given range of characters. If the
    range is empty and not at the beginning of a paragraph, gets the chp
    of the previous character. Otherwise gets the chp of the character at
    LwMin(cp1, cp2).
***************************************************************************/
void TXRG::_FetchChpSel(int32_t cp1, int32_t cp2, PCHP pchp)
{
    AssertThis(0);
    AssertVarMem(pchp);

    int32_t cp = LwMin(cp1, cp2);

    if (cp1 == cp2 && cp1 > 0)
    {
        if (!_ptxtb->FMinPara(cp) || _ptxtb->FMinPara(cp2 = _ptxtb->CpNext(cp)) || cp2 >= _ptxtb->CpMac())
        {
            cp = _ptxtb->CpPrev(cp);
        }
    }

    // 3DMMv1.0: NOTE: don't just call _FetchChp here.  _FetchChp allows derived
    // 3DMMv1.0: classes to modify the chp used for display without affecting the
    // 3DMMv1.0: chp used for editing.  This chp is an editing chp.  Same note
    // 3DMMv1.0: applies several other places in this file.
    ((PTXRD)_ptxtb)->FetchChp(cp, pchp);
}

/** 3DMMv1.0: *************************************************************************
    Make sure the _chpIns is valid.
***************************************************************************/
void TXRG::_EnsureChpIns(void)
{
    AssertThis(0);

    if (!_fValidChp)
    {
        _FetchChpSel(_cpAnchor, _cpOther, &_chpIns);
        _fValidChp = fTrue;
    }
}

/** 3DMMv1.0: *************************************************************************
    Replaces the characters between cp1 and cp2 with the given ones.
***************************************************************************/
bool TXRG::FReplace(achar *prgch, int32_t cch, int32_t cp1, int32_t cp2)
{
    AssertIn(cch, 0, kcbMax);
    AssertPvCb(prgch, cch);
    AssertIn(cp1, 0, _ptxtb->CpMac());
    AssertIn(cp2, 0, _ptxtb->CpMac());
    CHP chp;
    bool fSetChpIns = fFalse;

    HideSel();
    SortLw(&cp1, &cp2);
    if (cp1 == LwMin(_cpAnchor, _cpOther) && cp2 == LwMax(_cpAnchor, _cpOther))
    {
        _EnsureChpIns();
        chp = _chpIns;
        fSetChpIns = fTrue;
    }
    else
        _FetchChpSel(cp1, cp2, &chp);

    if (!((PTXRD)_ptxtb)->FReplaceRgch(prgch, cch, cp1, cp2 - cp1, &chp))
        return fFalse;

    cp1 += cch;
    SetSel(cp1, cp1);
    ShowSel();

    if (fSetChpIns)
    {
        // 3DMMv1.0: preserve the _chpIns
        _chpIns = chp;
        _fValidChp = fTrue;
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Copy the selection.
***************************************************************************/
bool TXRG::_FCopySel(PDOCB *ppdocb)
{
    AssertNilOrVarMem(ppdocb);
    PTXRD ptxrd;

    if (_cpAnchor == _cpOther)
        return fFalse;

    if (pvNil == ppdocb)
        return fTrue;

    if (pvNil != (ptxrd = TXRD::PtxrdNew(pvNil)))
    {
        int32_t cpMin = LwMin(_cpAnchor, _cpOther);
        int32_t cpLim = LwMax(_cpAnchor, _cpOther);

        ptxrd->SuspendUndo();
        if (!ptxrd->FReplaceTxrd((PTXRD)_ptxtb, cpMin, cpLim - cpMin, 0, 0, fdocNil))
        {
            ReleasePpo(&ptxrd);
        }
        else
            ptxrd->ResumeUndo();
    }

    *ppdocb = ptxrd;
    return pvNil != *ppdocb;
}

/** 3DMMv1.0: *************************************************************************
    Delete the selection.
***************************************************************************/
void TXRG::_ClearSel(void)
{
    FReplace(pvNil, 0, _cpAnchor, _cpOther);
    ShowSel();
}

/** 3DMMv1.0: *************************************************************************
    Paste the selection.
***************************************************************************/
bool TXRG::_FPaste(PCLIP pclip, bool fDoIt, int32_t cid)
{
    AssertThis(0);
    int32_t cp1, cp2;
    int32_t ccp;
    PTXTB ptxtb;
    bool fRet;

    if (cid != cidPaste || !pclip->FGetFormat(kclsTXTB))
        return fFalse;

    if (!fDoIt)
        return fTrue;

    if (!pclip->FGetFormat(kclsTXRD, (PDOCB *)&ptxtb) && !pclip->FGetFormat(kclsTXTB, (PDOCB *)&ptxtb))
    {
        return fFalse;
    }

    AssertPo(ptxtb, 0);
    if ((ccp = ptxtb->CpMac() - 1) <= 0)
    {
        ReleasePpo(&ptxtb);
        return fTrue;
    }

    HideSel();
    cp1 = LwMin(_cpAnchor, _cpOther);
    cp2 = LwMax(_cpAnchor, _cpOther);
    _ptxtb->BumpCombineUndo();
    if (ptxtb->FIs(kclsTXRD))
    {
        fRet = ((PTXRD)_ptxtb)->FReplaceTxrd((PTXRD)ptxtb, 0, ccp, cp1, cp2 - cp1);
    }
    else
    {
        _EnsureChpIns();
        fRet = ((PTXRD)_ptxtb)->FReplaceTxtb(ptxtb, 0, ccp, cp1, cp2 - cp1, &_chpIns);
    }
    ReleasePpo(&ptxtb);

    if (fRet)
    {
        _ptxtb->BumpCombineUndo();
        cp1 += ccp;
        SetSel(cp1, cp1);
        ShowSel();
    }

    return fRet;
}

/** 3DMMv1.0: *************************************************************************
    Apply the given character properties to the current selection.
***************************************************************************/
bool TXRG::FApplyChp(PCHP pchp, PCHP pchpDiff)
{
    AssertThis(0);
    AssertVarMem(pchp);
    AssertNilOrVarMem(pchpDiff);

    int32_t cpMin, cpLim, cpAnchor, cpOther;
    uint32_t grfont;

    cpMin = LwMin(cpAnchor = _cpAnchor, cpOther = _cpOther);
    cpLim = LwMax(_cpAnchor, _cpOther);

    if (cpMin == cpLim)
    {
        if (pvNil == pchpDiff)
        {
            _chpIns = *pchp;
            _fValidChp = fTrue;
            return fTrue;
        }

        _EnsureChpIns();
        if (fontNil != (grfont = pchp->grfont ^ pchpDiff->grfont))
            _chpIns.grfont = (_chpIns.grfont & ~grfont) | (pchp->grfont & grfont);
        if (pchp->onn != pchpDiff->onn)
            _chpIns.onn = pchp->onn;
        if (pchp->dypFont != pchpDiff->dypFont)
            _chpIns.dypFont = pchp->dypFont;
        if (pchp->dypOffset != pchpDiff->dypOffset)
            _chpIns.dypOffset = pchp->dypOffset;
        if (pchp->acrFore != pchpDiff->acrFore)
            _chpIns.acrFore = pchp->acrFore;
        if (pchp->acrBack != pchpDiff->acrBack)
            _chpIns.acrBack = pchp->acrBack;
        AssertThis(0);
        return fTrue;
    }

    _SwitchSel(fFalse, ginNil);
    if (!((PTXRD)_ptxtb)->FApplyChp(cpMin, cpLim - cpMin, pchp, pchpDiff))
    {
        _SwitchSel(fTrue, kginMark);
        return fFalse;
    }
    _fValidChp = fFalse;
    SetSel(cpAnchor, cpOther);
    ShowSel();

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Apply the given paragraph properties to the current selection.
***************************************************************************/
bool TXRG::FApplyPap(PPAP ppap, PPAP ppapDiff, bool fExpand)
{
    AssertThis(0);
    AssertVarMem(ppap);
    AssertNilOrVarMem(ppapDiff);
    int32_t cpMin, cpLim, cpAnchor, cpOther;

    cpMin = LwMin(cpAnchor = _cpAnchor, cpOther = _cpOther);
    cpLim = LwMax(_cpAnchor, _cpOther);

    _SwitchSel(fFalse, ginNil);
    if (!((PTXRD)_ptxtb)->FApplyPap(cpMin, cpLim - cpMin, ppap, ppapDiff, pvNil, pvNil, fExpand))
    {
        _SwitchSel(fTrue, kginMark);
        return fFalse;
    }

    SetSel(cpAnchor, cpOther);
    ShowSel();

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Apply a character or paragraph property
***************************************************************************/
bool TXRG::FCmdApplyProperty(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    CHP chpOld, chpNew;
    PAP papOld, papNew;
    int32_t onn;
    STN stn;

    ClearPb(&papOld, SIZEOF(PAP));
    papNew = papOld;

    _EnsureChpIns();
    chpOld = chpNew = _chpIns;

    switch (pcmd->cid)
    {
    case cidPlain:
        chpNew.grfont = fontNil;
        chpOld.grfont = (uint32_t)~fontNil;
        goto LApplyChp;
    case cidBold:
        chpNew.grfont ^= fontBold;
        goto LApplyChp;
    case cidItalic:
        chpNew.grfont ^= fontItalic;
        goto LApplyChp;
    case cidUnderline:
        chpNew.grfont ^= fontUnderline;
        goto LApplyChp;
    case cidChooseFont:
        if (pvNil == pcmd->pgg || pcmd->pgg->IvMac() != 1 || !stn.FSetData(pcmd->pgg->QvGet(0), pcmd->pgg->Cb(0)))
        {
            break;
        }
        if (!vntl.FGetOnn(&stn, &onn))
            break;
        chpNew.onn = onn;
        chpOld.onn = ~onn;
        goto LApplyChp;
    case cidChooseSubSuper:
        // 3DMMv1.0: the amount to offset by is pcmd->rglw[0] ^ (1L << 31), so 0 can be
        // 3DMMv1.0: used to indicate that we're to ask the user.
        if (pcmd->rglw[0] == 0)
        {
            int32_t dyp = _chpIns.dypOffset;

            // 3DMMv1.0: ask the user for the amount to sub/superscript by
            if (!_FGetOtherSubSuper(&dyp))
                return fTrue;
            pcmd->rglw[0] = dyp ^ (1L << 31);
        }
        chpNew.dypOffset = pcmd->rglw[0] ^ (1L << 31);
        chpOld.dypOffset = ~chpNew.dypOffset;
        goto LApplyChp;
    case cidChooseFontSize:
        if (pcmd->rglw[0] == 0)
        {
            int32_t dyp = _chpIns.dypFont;

            // 3DMMv1.0: ask the user for the font size
            if (!_FGetOtherSize(&dyp))
                return fTrue;
            pcmd->rglw[0] = dyp;
        }
        if (!FIn(pcmd->rglw[0], 6, 256))
            return fTrue;
        chpNew.dypFont = pcmd->rglw[0];
        chpOld.dypFont = ~pcmd->rglw[0];
    LApplyChp:
        FApplyChp(&chpNew, &chpOld);
        break;

    case cidJustifyLeft:
        papNew.jc = jcLeft;
        papOld.jc = jcLim;
        goto LApplyPap;
    case cidJustifyCenter:
        papNew.jc = jcCenter;
        papOld.jc = jcLim;
        goto LApplyPap;
    case cidJustifyRight:
        papNew.jc = jcRight;
        papOld.jc = jcLim;
        goto LApplyPap;
    case cidIndentNone:
        papNew.nd = ndNone;
        papOld.nd = ndLim;
        goto LApplyPap;
    case cidIndentFirst:
        papNew.nd = ndFirst;
        papOld.nd = ndLim;
        goto LApplyPap;
    case cidIndentRest:
        papNew.nd = ndRest;
        papOld.nd = ndLim;
        goto LApplyPap;
    case cidIndentAll:
        papNew.nd = ndAll;
        papOld.nd = ndLim;
    LApplyPap:
        FApplyPap(&papNew, &papOld);
        break;
    }

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Get a font size from the user.
***************************************************************************/
bool TXRG::_FGetOtherSize(int32_t *pdypFont)
{
    AssertThis(0);
    AssertVarMem(pdypFont);

    TrashVar(pdypFont);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Get the amount to sub/superscript from the user.
***************************************************************************/
bool TXRG::_FGetOtherSubSuper(int32_t *pdypOffset)
{
    AssertThis(0);
    AssertVarMem(pdypOffset);

    TrashVar(pdypOffset);
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Apply a character or paragraph property
***************************************************************************/
bool TXRG::FSetColor(ACR *pacrFore, ACR *pacrBack)
{
    AssertThis(0);
    AssertNilOrPo(pacrFore, 0);
    AssertNilOrPo(pacrBack, 0);
    CHP chp, chpDiff;

    chp.Clear();
    chpDiff.Clear();

    if (pvNil != pacrFore)
    {
        chp.acrFore = *pacrFore;
        chpDiff.acrFore.SetToInvert();
        if (chpDiff.acrFore == chp.acrFore)
            chpDiff.acrFore.SetToClear();
    }
    if (pvNil != pacrBack)
    {
        chp.acrBack = *pacrBack;
        chpDiff.acrBack.SetToInvert();
        if (chpDiff.acrBack == chp.acrBack)
            chpDiff.acrBack.SetToClear();
    }

    return FApplyChp(&chp, &chpDiff);
}

/** 3DMMv1.0: *************************************************************************
    Enable, check/uncheck property commands.
***************************************************************************/
bool TXRG::FEnablePropCmd(PCMD pcmd, uint32_t *pgrfeds)
{
    PAP pap;
    bool fCheck;
    STN stn, stnT;

    _EnsureChpIns();
    ((PTXRD)_ptxtb)->FetchPap(LwMin(_cpAnchor, _cpOther), &pap);

    switch (pcmd->cid)
    {
    case cidPlain:
        fCheck = (_chpIns.grfont == fontNil);
        break;
    case cidBold:
        fCheck = FPure(_chpIns.grfont & fontBold);
        break;
    case cidItalic:
        fCheck = FPure(_chpIns.grfont & fontItalic);
        break;
    case cidUnderline:
        fCheck = FPure(_chpIns.grfont & fontUnderline);
        break;

    case cidChooseFont:
        if (pvNil == pcmd->pgg || pcmd->pgg->IvMac() != 1)
            break;
        if (!stnT.FSetData(pcmd->pgg->PvLock(0), pcmd->pgg->Cb(0)))
        {
            pcmd->pgg->Unlock();
            break;
        }
        pcmd->pgg->Unlock();
        vntl.GetStn(_chpIns.onn, &stn);
        fCheck = stn.FEqual(&stnT);
        break;

    case cidChooseFontSize:
        fCheck = (_chpIns.dypFont == pcmd->rglw[0]);
        break;
    case cidChooseSubSuper:
        fCheck = (_chpIns.dypOffset == (pcmd->rglw[0] ^ (1L << 31)));
        break;

    case cidJustifyLeft:
        fCheck = (pap.jc == jcLeft);
        break;
    case cidJustifyCenter:
        fCheck = (pap.jc == jcCenter);
        break;
    case cidJustifyRight:
        fCheck = (pap.jc == jcRight);
        break;
    case cidIndentNone:
        fCheck = (pap.nd == ndNone);
        break;
    case cidIndentFirst:
        fCheck = (pap.nd == ndFirst);
        break;
    case cidIndentRest:
        fCheck = (pap.nd == ndRest);
        break;
    case cidIndentAll:
        fCheck = (pap.nd == ndAll);
        break;
    }
    *pgrfeds = fCheck ? fedsEnable | fedsCheck : fedsEnable | fedsUncheck;

    return fTrue;
}
