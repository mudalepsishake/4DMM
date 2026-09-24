/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Some "random" stuff

***************************************************************************/
#include "util.h"

ASSERTNAME

RTCLASS(RND)
RTCLASS(SFL)

/** 3DMMv1.0: *************************************************************************
    Constructs a pseudo-random number generator.  If luSeed is zero,
    generates a seed from the current system time (TsCurrentSystem).
***************************************************************************/
RND::RND(uint32_t luSeed)
{
    if (0 == luSeed)
    {
        luSeed = TsCurrentSystem();
        SwapBytesRglw((int32_t *)&luSeed, 1);
    }
    _luSeed = luSeed;
    AssertThis(0);
}

/** 3DMMv1.0: *************************************************************************
    Return the next pseudo-random number within the range 0 to lwLim - 1,
    inclusive.
***************************************************************************/
int32_t RND::LwNext(int32_t lwLim)
{
    AssertThis(0);
    AssertIn(lwLim, 1, kcbMax);

    // 3DMMv1.0: high bits are more random than the low ones
    // 3DMMv1.0: See Knuth vol 2, page 102, line 24 of table 1.
    // 3DMMv1.0: value of kdluRand doesn't matter much
    const uint32_t kluRandMul = 1566083941L;
    const int32_t kdluRand = 2531011L;
    int32_t lw;

    _luSeed = _luSeed * kluRandMul + kdluRand;

    // 3DMMv1.0: multiply lw by lwLim and divide by 2^32
#ifdef IN_80386
    uint32_t luSeedT = _luSeed;
    __asm
    {
		mov		eax,luSeedT
		mul		lwLim
		mov		lw,edx
    }
#else  //! 3DMMv1.0: IN_80386
    double dou;

    dou = (double)_luSeed * lwLim / (double)0x40000000 / 4;
    lw = (int32_t)dou;
#endif //! 3DMMv1.0: IN_80386

    Assert(lw < lwLim, "random number out of range");
    return lw;
}

/** 3DMMv1.0: *************************************************************************
    Constructs a shuffled array.
***************************************************************************/
SFL::SFL(uint32_t luSeed) : RND(luSeed)
{
    _clw = 0;
    _ilw = 0;
    _fCustom = fFalse;
    _hqrglw = hqNil;
    AssertThis(0);
}

/** 3DMMv1.0: *************************************************************************
    Destructs a shuffled array.
***************************************************************************/
SFL::~SFL(void)
{
    AssertThis(0);
    FreePhq(&_hqrglw);
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a SFL.
***************************************************************************/
void SFL::AssertValid(uint32_t grf)
{
    SFL_PAR::AssertValid(0);
    if (_hqrglw != hqNil)
    {
        AssertHq(_hqrglw);
        Assert(CbOfHq(_hqrglw) == LwMul(_clw, SIZEOF(int32_t)), "HQ wrong size");
    }
    else
        Assert(0 == _clw, "_clw wrong");
}

/** 3DMMv1.0: *************************************************************************
    Mark memory for the SFL.
***************************************************************************/
void SFL::MarkMem(void)
{
    AssertValid(0);
    SFL_PAR::MarkMem();
    MarkHq(_hqrglw);
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Shuffle the numbers [0, lwLim).
***************************************************************************/
void SFL::Shuffle(int32_t lwLim)
{
    AssertThis(0);
    AssertIn(lwLim, 0, kcbMax);
    int32_t ilw;
    int32_t *qrglw;

    if (!_FEnsureHq(lwLim))
        return;
    _ilw = 0;
    Assert(_clw == lwLim, "wrong _clw");

    qrglw = (int32_t *)QvFromHq(_hqrglw);
    // 3DMMv1.0: fill the array with [0, _clw)
    for (ilw = 0; ilw < _clw; ilw++)
        qrglw[ilw] = ilw;

    _fCustom = fFalse;
    _ShuffleCore();
}

/** 3DMMv1.0: *************************************************************************
    Fill the SFL with the values in prglw and shuffle them.
***************************************************************************/
void SFL::ShuffleRglw(int32_t clw, int32_t *prglw)
{
    AssertThis(0);
    AssertIn(clw, 0, kcbMax);
    AssertPvCb(prglw, LwMul(clw, SIZEOF(int32_t)));

    if (!_FEnsureHq(clw))
        return;
    _ilw = 0;
    Assert(_clw == clw, "wrong _clw");

    // 3DMMv1.0: fill the HQ with the stuff in prglw
    CopyPb(prglw, QvFromHq(_hqrglw), LwMul(clw, SIZEOF(int32_t)));

    _fCustom = fTrue;
    _ShuffleCore();
}

/** 3DMMv1.0: *************************************************************************
    Shuffle the entries in the HQ.
***************************************************************************/
void SFL::_ShuffleCore(void)
{
    AssertThis(0);
    Assert(_clw > 0, 0);
    int32_t lw;
    int32_t ilw, ilwSwap;
    int32_t *qrglw;

    // 3DMMv1.0: swap stuff
    qrglw = (int32_t *)QvFromHq(_hqrglw);
    for (ilw = _clw; --ilw > 0;)
    {
        ilwSwap = RND::LwNext(ilw + 1);
        if (ilwSwap < ilw)
        {
            lw = qrglw[ilw];
            qrglw[ilw] = qrglw[ilwSwap];
            qrglw[ilwSwap] = lw;
        }
    }
}

/** 3DMMv1.0: *************************************************************************
    Make sure the HQ is the correct size and set clw appropriately.
***************************************************************************/
bool SFL::_FEnsureHq(int32_t clw)
{
    AssertThis(0);
    AssertIn(clw, 0, kcbMax);

    if (clw <= 0)
        goto LFail;

    if (_hqrglw == hqNil && !FAllocHq(&_hqrglw, LwMul(_clw = clw, SIZEOF(int32_t)), fmemNil, mprNormal))
    {
        goto LFail;
    }
    if (clw != _clw && !FResizePhq(&_hqrglw, LwMul(_clw = clw, SIZEOF(int32_t)), fmemNil, mprNormal))
    {
    LFail:
        // 3DMMv1.0: we are low on memory, so be nice and give some up
        FreePhq(&_hqrglw);
        _clw = 0;
        AssertThis(0);
        return fFalse;
        ;
    }
    AssertThis(0);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Returns the next number in a shuffled array of numbers.  If lwLim is
    zero, uses the numbers already in the SFL.  Otherwise, numbers
    range from 0 to (lwLim - 1), inclusive.
***************************************************************************/
int32_t SFL::LwNext(int32_t lwLim)
{
    AssertThis(0);
    AssertIn(lwLim, 0, kcbMax);

    if (lwLim > 0 && (lwLim != _clw || _fCustom))
    {
        // 3DMMv1.0: need to reshuffle with standard values
        Shuffle(lwLim);
        _ilw = 0;
        if (0 == _clw)
        {
            // 3DMMv1.0: shuffling failed, just use the regular random number
            return RND::LwNext(lwLim);
        }
    }
    else if (_clw == 0)
    {
        // 3DMMv1.0: no values in the HQ, just return 0
        _ilw = 0;
        return 0;
    }
    else if (_ilw >= _clw)
    {
        // 3DMMv1.0: need to reshuffle the values already in the HQ
        _ShuffleCore();
        _ilw = 0;
    }

    AssertIn(_ilw, 0, _clw);
    return ((int32_t *)QvFromHq(_hqrglw))[_ilw++];
}
