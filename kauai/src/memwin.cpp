/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Windows memory management.

***************************************************************************/
#include "util.h"
ASSERTNAME

struct HQH
{
    int32_t cb;       // 3DMMv1.0: size of client area
    int32_t cactLock; // 3DMMv1.0: lock count
#ifdef DEBUG
    int32_t lwMagic; // 3DMMv1.0: for detecting memory trashing
#endif               // 3DMMv1.0: DEBUG
};

#ifdef DEBUG
int32_t vcactSuspendCheckPointers = 0;
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Allocates a new moveable block.
***************************************************************************/
#ifdef DEBUG
bool FAllocHqDebug(HQ *phq, int32_t cb, uint32_t grfmem, int32_t mpr, PSZS pszsFile, int32_t lwLine)
#else  //! 3DMMv1.0: DEBUG
bool FAllocHq(HQ *phq, int32_t cb, uint32_t grfmem, int32_t mpr)
#endif //! 3DMMv1.0: DEBUG
{
    AssertVarMem(phq);
    AssertIn(cb, 0, kcbMax);
    HQH *phqh;

    if (!FAllocPvDebug((void **)&phqh, cb + SIZEOF(HQH), grfmem, mpr, pszsFile, lwLine, &vdmglob.dmaglHq))
    {
        PushErc(ercOomHq);
        *phq = hqNil;
    }
    else
    {
        phqh->cb = cb;
        phqh->cactLock = 0;
        Debug(phqh->lwMagic = klwMagicMem;) *phq = (HQ)(phqh + 1);
        AssertHq(*phq);
    }

    return pvNil != *phq;
}

/** 3DMMv1.0: *************************************************************************
    Resizes the given hq.  *phq may change.  If fmemClear, clears any
    newly added space.
***************************************************************************/
bool FResizePhq(HQ *phq, int32_t cb, uint32_t grfmem, int32_t mpr)
{
    AssertVarMem(phq);
    AssertHq(*phq);
    AssertIn(cb, 0, kcbMax);
    bool fRet = fFalse;
    HQH *phqh = (HQH *)PvSubBv(*phq, SIZEOF(HQH));

    if (phqh->cactLock > 0)
    {
        Bug("Resizing locked HQ");
        PushErc(ercOomHq);
    }
    else if (!_FResizePpvDebug((void **)&phqh, cb + SIZEOF(HQH), phqh->cb + SIZEOF(HQH), grfmem, mpr, &vdmglob.dmaglHq))
    {
        PushErc(ercOomHq);
        AssertHq(*phq);
    }
    else
    {
        phqh->cb = cb;
        *phq = (HQ)(phqh + 1);
        AssertHq(*phq);
        fRet = fTrue;
    }

    return fRet;
}

/** 3DMMv1.0: *************************************************************************
    If hq is not nil, frees it.
***************************************************************************/
void FreePhq(HQ *phq)
{
    AssertVarMem(phq);

    if (*phq == hqNil)
        return;

    AssertHq(*phq);
    HQH *phqh = (HQH *)PvSubBv(*phq, SIZEOF(HQH));
    *phq = hqNil;
    Assert(phqh->cactLock == 0, "Freeing locked HQ");

    FreePpvDebug((void **)&phqh, &vdmglob.dmaglHq);
}

/** 3DMMv1.0: *************************************************************************
    Create a new HQ the same size as hqSrc and copy hqSrc into it.
***************************************************************************/
bool FCopyHq(HQ hqSrc, HQ *phqDst, int32_t mpr)
{
    AssertHq(hqSrc);
    AssertVarMem(phqDst);
    int32_t cb;

    if (!FAllocHq(phqDst, cb = CbOfHq(hqSrc), fmemNil, mpr))
        return fFalse;
    CopyPb(QvFromHq(hqSrc), QvFromHq(*phqDst), cb);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Return the size of the hq (the client area of the block).
***************************************************************************/
int32_t CbOfHq(HQ hq)
{
    AssertHq(hq);
    HQH *phqh = (HQH *)PvSubBv(hq, SIZEOF(HQH));
    return phqh->cb;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Returns a volatile pointer from an hq.
***************************************************************************/
void *QvFromHq(HQ hq)
{
    AssertHq(hq);
    return (void *)hq;
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Lock the hq and return a pointer to the data.
***************************************************************************/
void *PvLockHq(HQ hq)
{
    AssertHq(hq);
    HQH *phqh = (HQH *)PvSubBv(hq, SIZEOF(HQH));
    phqh->cactLock++;
    Assert(phqh->cactLock > 0, "overflow in cactLock");
    return (void *)hq;
}

/** 3DMMv1.0: *************************************************************************
    Unlock the hq.  Asserts and does nothing if the lock count is zero.
***************************************************************************/
void UnlockHq(HQ hq)
{
    AssertHq(hq);
    HQH *phqh = (HQH *)PvSubBv(hq, SIZEOF(HQH));
    Assert(phqh->cactLock > 0, "hq not locked");
    if (phqh->cactLock > 0)
        --phqh->cactLock;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert that a given hq is valid.
***************************************************************************/
void AssertHq(HQ hq)
{
    // 3DMMv1.0: make sure hq isn't nil
    if (hq == hqNil)
    {
        Bug("hq is nil");
        return;
    }

    // 3DMMv1.0: verify the HQH
    HQH *phqh = (HQH *)PvSubBv(hq, SIZEOF(HQH));
    if (phqh->lwMagic != klwMagicMem)
    {
        BugVar("beginning of hq block is trashed", phqh);
        return;
    }
    AssertIn(phqh->cactLock, 0, kcbMax);
    AssertPvAlloced(phqh, phqh->cb + SIZEOF(HQH));
}

/** 3DMMv1.0: *************************************************************************
    Increment the ref count on an hq.
***************************************************************************/
void MarkHq(HQ hq)
{
    if (hqNil != hq)
    {
        AssertHq(hq);
        MarkPv(PvSubBv(hq, SIZEOF(HQH)));
    }
}

/** 3DMMv1.0: *************************************************************************
    Make sure we can access a pointer's memory.  If cb is 0, pv can be
    anything (including nil).
***************************************************************************/
void AssertPvCb(const void *pv, int32_t cb)
{
    if (vcactSuspendCheckPointers == 0 && cb != 0)
    {
        // 3DMMEx: This assert has been disabled because AssertPvCb is called on pointers to
        // 3DMMEx: globals which were previously read/write but are now read-only.

        // 3DMMEx: AssertVar(!IsBadWritePtr(pv, cb), "no write access to ptr", &pv);
        // 3DMMv1.0:  I (ShonK) am assuming that write access implies read access for
        // 3DMMv1.0:  memory, so it would just be a waste of time to call this.
        // 3DMMv1.0:  AssertVar(!IsBadReadPtr(pv, cb), "no read access to ptr", &pv);
    }
}
#endif // 3DMMv1.0: DEBUG
