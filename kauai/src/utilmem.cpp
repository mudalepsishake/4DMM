/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Shared (between Mac and Win) memory allocation routines.

    The APIs in this module implement fixed block (non-moveable,
    non-resizeable) memory management.  On win, we use GlobalAlloc; on Mac
    we use ::operator new.  The win hq is based on FAllocPv.  The mac
    hq is a Mac handle.  _FResizePpv is win only (needed for
    resizing HQs) and considered private to the memory management code
    (if the implementation of Win HQs changes, it will go away).
***************************************************************************/
#include "util.h"
ASSERTNAME

#include <thread>

#ifdef DEBUG
const int32_t kclwStackMbh = 5;

// 3DMMv1.0: memory block header
struct MBH
{
    int32_t cb;                      // 3DMMv1.0: size of block, including header and footer
    PSZS pszsFile;                   // 3DMMv1.0: source file that allocation request is coming from
    int32_t lwLine;                  // 3DMMv1.0: line in file that allocation request is coming from
    std::thread::id tid;             // 3DMMv1.0: thread id
    MBH *pmbhPrev;                   // 3DMMv1.0: previous allocated block (in doubly linked list)
    MBH *pmbhNext;                   // 3DMMv1.0: next allocated block
    int32_t rglwStack[kclwStackMbh]; // 3DMMv1.0: the EBP/A6 chain
    int16_t cactRef;                 // 3DMMv1.0: for marking memory
    int16_t swMagic;                 // 3DMMv1.0: magic number, to detect memory trashing
};

// 3DMMv1.0: memory block footer
struct MBF
{
    int16_t swMagic; // 3DMMv1.0: magic number, to detect memory trashing
};

MBH *_pmbhFirst; // 3DMMv1.0: head of the doubly linked list

kpriv void _LinkMbh(MBH *pmbh);
kpriv void _UnlinkMbh(MBH *pmbh, MBH *pmbhOld);
kpriv void _AssertMbh(MBH *pmbh);
#endif // 3DMMv1.0: DEBUG

#ifdef MAC
#define malloc(cb) ::operator new(cb)
#define free(pv) delete (pv)
#endif // 3DMMv1.0: MAC
#ifdef WIN
#define malloc(cb) (void *)GlobalAlloc(GMEM_FIXED, cb)
#define free(pv) GlobalFree((HGLOBAL)pv)
#define _msize(pv) GlobalSize((HGLOBAL)pv)
#define realloc(pv, cb) (void *)GlobalReAlloc((HGLOBAL)pv, cb, GMEM_MOVEABLE)
#endif // 3DMMv1.0: WIN

PFNLIB vpfnlib = pvNil;
bool _fInLiberator = fFalse;

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Do simulated failure testing.
***************************************************************************/
bool DMAGL::FFail(void)
{
    bool fRet = fFalse;

    vmutxMem.Enter();
    if (cactFail > 0)
    {
        if (cactDo <= 0)
        {
            cactFail--;
            fRet = fTrue;
        }
        else
            cactDo--;
    }
    vmutxMem.Leave();

    return fRet;
}

/** 3DMMv1.0: *************************************************************************
    Update values after an allocation
***************************************************************************/
void DMAGL::Allocate(int32_t cbT)
{
    vmutxMem.Enter();
    if (cvRun < ++cv)
        cvRun = cv;
    cvTot++;
    if (cbRun < (cb += cbT))
        cbRun = cb;
    vmutxMem.Leave();
}

/** 3DMMv1.0: *************************************************************************
    Update values after a resize
***************************************************************************/
void DMAGL::Resize(int32_t dcb)
{
    vmutxMem.Enter();
    if (cbRun < (cb += dcb))
        cbRun = cb;
    vmutxMem.Leave();
}

/** 3DMMv1.0: *************************************************************************
    Update values after a block is freed
***************************************************************************/
void DMAGL::Free(int32_t cbT)
{
    --cv;
    cb -= cbT;
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Allocates a fixed block.
***************************************************************************/
#ifdef DEBUG
bool FAllocPvDebug(void **ppv, int32_t cb, uint32_t grfmem, int32_t mpr, PSZS pszsFile, int32_t lwLine, DMAGL *pdmagl)
#else  //! 3DMMv1.0: DEBUG
bool FAllocPv(void **ppv, int32_t cb, uint32_t grfmem, int32_t mpr)
#endif //! 3DMMv1.0: DEBUG
{
    AssertVarMem(ppv);
    AssertIn(cb, 0, kcbMax);
    int32_t cbFree;

    if (cb > kcbMax)
    {
        BugVar("who's allocating a humongous block?", &cb);
        goto LFail;
    }

#ifdef DEBUG
    // 3DMMv1.0: do simulated failure
    if (pdmagl->FFail())
        goto LFail;

    vmutxMem.Enter();
    if (pvNil != _pmbhFirst)
        _AssertMbh(_pmbhFirst);
    vmutxMem.Leave();

    Assert(cb + SIZEOF(MBH) + SIZEOF(MBF) > cb, 0);
    cb += SIZEOF(MBH) + SIZEOF(MBF);
#endif // 3DMMv1.0: DEBUG

    for (;;)
    {
        *ppv = malloc(cb);
        if (pvNil != *ppv || pvNil == vpfnlib)
            break;

        vmutxMem.Enter();
        if (_fInLiberator)
            cbFree = 0;
        else
        {
            _fInLiberator = fTrue;
            vmutxMem.Leave();
            cbFree = (*vpfnlib)(cb, mpr);
            vmutxMem.Enter();
            _fInLiberator = fFalse;
        }
        vmutxMem.Leave();

        if (cbFree <= 0)
            break;
    }

    if (pvNil == *ppv)
    {
    LFail:
        *ppv = pvNil;
        PushErc(ercOomPv);
        return fFalse;
    }

    if (grfmem & fmemClear)
        ClearPb(*ppv, cb);
#ifdef DEBUG
    else
        FillPb(*ppv, cb, kbGarbage);

    // 3DMMv1.0: fill in the header
    MBH *pmbh = (MBH *)*ppv;
    *ppv = pmbh + 1;
    pmbh->cb = cb;
    pmbh->swMagic = kswMagicMem;
    pmbh->pszsFile = pszsFile;
    pmbh->lwLine = lwLine;
    pmbh->tid = std::this_thread::get_id();

#if defined(WIN) && defined(IN_80386)
    // 3DMMv1.0: follow the EBP chain....
    int32_t *plw;
    int32_t ilw;

    __asm { mov plw,ebp }
    for (ilw = 0; ilw < kclwStackMbh; ilw++)
    {
        if (pvNil == plw || IsBadReadPtr(plw, 2 * SIZEOF(int32_t)) || *plw <= (int32_t)plw)
        {
            pmbh->rglwStack[ilw] = 0;
            plw = pvNil;
        }
        else
        {
            pmbh->rglwStack[ilw] = plw[1];
            plw = (int32_t *)*plw;
        }
    }
#endif // 3DMMEx: WIN && IN_80386

    // 3DMMv1.0: write the footer
    MBF mbf;
    mbf.swMagic = kswMagicMem;
    CopyPb(&mbf, PvAddBv(pmbh, cb - SIZEOF(MBF)), SIZEOF(MBF));

    // 3DMMv1.0: link the block
    _LinkMbh(pmbh);

    // 3DMMv1.0: update statistics
    pdmagl->Allocate(cb - SIZEOF(MBF) - SIZEOF(MBH));

    AssertPvAlloced(*ppv, cb - SIZEOF(MBF) - SIZEOF(MBH));
#endif // 3DMMv1.0: DEBUG

    return fTrue;
}

#ifndef MAC
/** 3DMMv1.0: *************************************************************************
    Resizes the given block.  *ppv may change.  If fmemClear, clears any
    newly added space.
***************************************************************************/
#ifdef DEBUG
bool _FResizePpvDebug(void **ppv, int32_t cbNew, int32_t cbOld, uint32_t grfmem, int32_t mpr, DMAGL *pdmagl)
#else  //! 3DMMv1.0: DEBUG
bool _FResizePpv(void **ppv, int32_t cbNew, int32_t cbOld, uint32_t grfmem, int32_t mpr)
#endif //! 3DMMv1.0: DEBUG
{
    AssertVarMem(ppv);
    AssertIn(cbNew, 0, kcbMax);
    AssertIn(cbOld, 0, kcbMax);
    AssertPvAlloced(*ppv, cbOld);
    int32_t cbFree;
    void *pvNew, *pvOld;

#ifdef DEBUG
    MBH *pmbh = (MBH *)PvSubBv(*ppv, SIZEOF(MBH));
    _AssertMbh(pmbh);
#endif // 3DMMv1.0: DEBUG

    if (cbNew > kcbMax)
    {
        BugVar("who's resizing a humongous block?", &cbNew);
        goto LFail;
    }

    pvOld = *ppv;
#ifdef DEBUG
    // 3DMMv1.0: do simulated failure - we can only fail if the block is growing
    if (cbNew > cbOld && pdmagl->FFail())
        goto LFail;

    // 3DMMv1.0: assert we don't overflow (the limit of kcbMax should ensure this)
    Assert(cbOld + SIZEOF(MBH) + SIZEOF(MBF) > cbOld, 0);
    Assert(cbNew + SIZEOF(MBH) + SIZEOF(MBF) > cbNew, 0);
    cbOld += SIZEOF(MBH) + SIZEOF(MBF);
    cbNew += SIZEOF(MBH) + SIZEOF(MBF);
    AssertVar(pmbh->cb == cbOld, "bad cbOld value passed to _FResizePpv", &cbOld);

    // 3DMMv1.0: trash the old stuff
    if (cbOld > cbNew)
        FillPb(PvAddBv(pmbh, cbNew), cbOld - cbNew, kbGarbage);
    pvOld = pmbh;
#endif // 3DMMv1.0: DEBUG

    for (;;)
    {
        pvNew = realloc(pvOld, cbNew);

        if (pvNil != pvNew || pvNil == vpfnlib)
            break;

        vmutxMem.Enter();
        if (_fInLiberator)
            cbFree = 0;
        else
        {
            _fInLiberator = fTrue;
            vmutxMem.Leave();
            cbFree = (*vpfnlib)(cbNew - cbOld, mpr);
            vmutxMem.Enter();
            _fInLiberator = fFalse;
        }
        vmutxMem.Leave();

        if (cbFree <= 0)
            break;
    }

    if (pvNil == pvNew)
    {
        Assert(cbOld < cbNew, "why did shrinking fail?");
    LFail:
        AssertPvAlloced(*ppv, cbOld - SIZEOF(MBH) - SIZEOF(MBF));
        PushErc(ercOomPv);

        return fFalse;
    }
    *ppv = pvNew;

    if ((grfmem & fmemClear) && cbOld < cbNew)
    {
        // 3DMMv1.0: Clear the new stuff
        ClearPb(PvAddBv(pvNew, cbOld), cbNew - cbOld);
    }

#ifdef DEBUG
    if ((grfmem & fmemClear) && cbOld < cbNew)
        ClearPb(PvAddBv(pvNew, cbOld - SIZEOF(MBF)), SIZEOF(MBF));
    else if (cbOld < cbNew)
    {
        // 3DMMv1.0: fill the new stuff with garbage
        FillPb(PvAddBv(pvNew, cbOld - SIZEOF(MBF)), cbNew - cbOld + SIZEOF(MBF), kbGarbage);
    }

    // 3DMMv1.0: update the header
    if (pvNew != pmbh)
    {
        _UnlinkMbh((MBH *)pvNew, pmbh);
        pmbh = (MBH *)pvNew;
        _LinkMbh(pmbh);
    }
    *ppv = pmbh + 1;
    pmbh->cb = cbNew;

    // 3DMMv1.0: write the footer
    MBF mbf;
    mbf.swMagic = kswMagicMem;
    CopyPb(&mbf, PvAddBv(pmbh, cbNew - SIZEOF(MBF)), SIZEOF(MBF));
    AssertPvAlloced(*ppv, cbNew - SIZEOF(MBF) - SIZEOF(MBH));

    // 3DMMv1.0: update statistics
    pdmagl->Resize(cbNew - cbOld);
#endif // 3DMMv1.0: DEBUG

    return fTrue;
}
#endif // 3DMMv1.0: MAC

/** 3DMMv1.0: *************************************************************************
    If *ppv is not nil, frees it and sets *ppv to nil.
***************************************************************************/
#ifdef DEBUG
void FreePpvDebug(void **ppv, DMAGL *pdmagl)
#else  //! 3DMMv1.0: DEBUG
void FreePpv(void **ppv)
#endif //! 3DMMv1.0: DEBUG
{
    AssertVarMem(ppv);
    if (*ppv == pvNil)
        return;

#ifdef DEBUG
    MBH *pmbh = (MBH *)PvSubBv(*ppv, SIZEOF(MBH));
    _AssertMbh(pmbh);
    _UnlinkMbh(pmbh, pmbh);

    // 3DMMv1.0: update statistics
    pdmagl->Free(pmbh->cb - SIZEOF(MBF) - SIZEOF(MBH));

    // 3DMMv1.0: fill the block with garbage before freeing it
    FillPb(pmbh, pmbh->cb, kbGarbage);
    *ppv = pmbh;
#endif // 3DMMv1.0: DEBUG

    free(*ppv);
    *ppv = pvNil;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Link the Mbh into the debug-only doubly linked list.
***************************************************************************/
kpriv void _LinkMbh(MBH *pmbh)
{
    AssertVarMem(pmbh);

    vmutxMem.Enter();
    pmbh->pmbhPrev = pvNil;
    pmbh->pmbhNext = _pmbhFirst;
    if (_pmbhFirst != pvNil)
    {
        Assert(_pmbhFirst->pmbhPrev == pvNil, "_pmbhFirst's prev is not nil");
        _pmbhFirst->pmbhPrev = pmbh;
    }
    _pmbhFirst = pmbh;
    vmutxMem.Leave();
}

/** 3DMMv1.0: *************************************************************************
    Unlink the MBH from the debug-only doubly linked list.  pmbhOld is the
    previous value of the linked block.  pmbhOld may not be a valid pointer
    now (when mem is resized).
***************************************************************************/
kpriv void _UnlinkMbh(MBH *pmbh, MBH *pmbhOld)
{
    AssertVarMem(pmbh);
    Assert(pmbhOld != pvNil, 0);

    vmutxMem.Enter();
    // 3DMMv1.0: update prev's next pointer
    if (pvNil == pmbh->pmbhPrev)
    {
        Assert(_pmbhFirst == pmbhOld, "prev is wrongly nil");
        _pmbhFirst = pmbh->pmbhNext;
    }
    else
    {
        Assert(_pmbhFirst != pmbhOld, "prev should be nil");
        Assert(pmbh->pmbhPrev->pmbhNext == pmbhOld, "prev's next wrong");
        pmbh->pmbhPrev->pmbhNext = pmbh->pmbhNext;
    }

    // 3DMMv1.0: update next's prev pointer
    if (pvNil != pmbh->pmbhNext)
    {
        Assert(pmbh->pmbhNext->pmbhPrev == pmbhOld, "next's prev wrong");
        pmbh->pmbhNext->pmbhPrev = pmbh->pmbhPrev;
    }
    vmutxMem.Leave();
}

/** 3DMMv1.0: *************************************************************************
    Validate the MBH and the block that it is the head of.
***************************************************************************/
void _AssertMbh(MBH *pmbh)
{
    int16_t sw;

    if (vcactSuspendCheckPointers != 0)
        return;

    vmutxMem.Enter();
    AssertVarMem(pmbh);
    Assert(pmbh->swMagic == kswMagicMem, "bad magic number");
    AssertIn(pmbh->cb - SIZEOF(MBH) - SIZEOF(MBF), 0, kcbMax);
#ifdef WIN
    Assert(pmbh->cb <= (int32_t)_msize(pmbh), "bigger than malloced block");
#endif
    AssertPvCb(pmbh, pmbh->cb);
    if (pmbh->pmbhPrev != pvNil)
    {
        AssertVarMem(pmbh->pmbhPrev);
        Assert(pmbh->pmbhPrev->pmbhNext == pmbh, "wrong next in prev");
        Assert(pmbh != _pmbhFirst, "first has prev!");
    }
    if (pmbh->pmbhNext != pvNil)
    {
        AssertVarMem(pmbh->pmbhNext);
        Assert(pmbh->pmbhNext->pmbhPrev == pmbh, "wrong prev in next");
    }

    ((uint8_t *)&sw)[0] = *(uint8_t *)PvAddBv(pmbh, pmbh->cb - 2);
    ((uint8_t *)&sw)[1] = *(uint8_t *)PvAddBv(pmbh, pmbh->cb - 1);
    Assert(sw == kswMagicMem, "bad tail magic number");
    vmutxMem.Leave();
}

/** 3DMMv1.0: *************************************************************************
    Assert the validity of an allocated fixed block.  Cb is the size.
    If cb is unknown, pass cvNil.
***************************************************************************/
void AssertPvAlloced(void *pv, int32_t cb)
{
    if (vcactSuspendCheckPointers != 0)
        return;

    Assert(pv != pvNil, "nil pv");
    MBH *pmbh = (MBH *)PvSubBv(pv, SIZEOF(MBH));
    _AssertMbh(pmbh);
    if (cb != cvNil)
        Assert(pmbh->cb == cb + SIZEOF(MBH) + SIZEOF(MBF), "wrong cb");
}

/** 3DMMv1.0: *************************************************************************
    Asserts on unmarked blocks.
***************************************************************************/
void AssertUnmarkedMem(void)
{
    MBH *pmbh;
    auto tid = std::this_thread::get_id();

    // 3DMMv1.0: enter the critical section
    vmutxMem.Enter();

    for (pmbh = _pmbhFirst; pmbh != pvNil; pmbh = pmbh->pmbhNext)
    {
        _AssertMbh(pmbh);
        if (pmbh->cactRef == 0 && pmbh->tid == tid)
        {
            STN stn;
            SZS szs;

            stn.FFormatSz(PszLit("\nLost block: size=%d, StackTrace=(use map file)"), pmbh->cb);
            stn.GetSzs(szs);

            if (FAssertProc(pmbh->pszsFile, pmbh->lwLine, szs, pmbh->rglwStack, kclwStackMbh * SIZEOF(int32_t)))
            {
                Debugger();
            }
        }
    }
#ifdef MAC
    _AssertUnmarkedHqs();
#endif
    // 3DMMv1.0: leave the critical section
    vmutxMem.Leave();
}

/** 3DMMv1.0: *************************************************************************
    Clears all marks on memory blocks.
***************************************************************************/
void UnmarkAllMem(void)
{
    MBH *pmbh;
    auto tid = std::this_thread::get_id();

    // 3DMMv1.0: enter the critical section
    vmutxMem.Enter();

    for (pmbh = _pmbhFirst; pmbh != pvNil; pmbh = pmbh->pmbhNext)
    {
        _AssertMbh(pmbh);
        if (pmbh->tid == tid)
            pmbh->cactRef = 0;
    }
#ifdef MAC
    _UnmarkAllHqs();
#endif

    // 3DMMv1.0: leave the critical section
    vmutxMem.Leave();
}

/** 3DMMv1.0: *************************************************************************
    Increment the ref count on an allocated pv.
***************************************************************************/
void MarkPv(void *pv)
{
    if (pvNil != pv)
    {
        AssertPvAlloced(pv, cvNil);
        MBH *pmbh = (MBH *)PvSubBv(pv, SIZEOF(MBH));
        pmbh->cactRef++;
    }
}
#endif // 3DMMv1.0: DEBUG
