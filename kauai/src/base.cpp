/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Base classes.  Base class that all other classes are derived from and
    a base linked list class for implementing singly linked lists.

***************************************************************************/
#include "util.h"
ASSERTNAME

#include <thread>

#ifdef DEBUG
int32_t vcactSuspendAssertValid = 0;
int32_t vcactAVSave = 0;
int32_t vcactAV = kswMax;

/** 3DMMv1.0: ***************************************************************************
    Debugging stuff to track leaked allocated objects.
******************************************************************************/
// 3DMMv1.0: Debugging object info.
const int32_t kclwStackDoi = 10;
struct DOI
{
    int16_t swMagic;       // 3DMMv1.0: magic number == kswMagicMem
    int16_t cactRef;       // 3DMMv1.0: for marking memory and asserting on unused objects
    PSZS pszsFile;         // 3DMMv1.0: file NewObj appears in
    int32_t lwLine;        // 3DMMv1.0: line NewObj appears on
    int32_t cbTot;         // 3DMMv1.0: total size of the block, including the DOI
    std::thread::id tid;   // 3DMMv1.0: thread that allocated this
    int32_t rglwStack[10]; // 3DMMv1.0: what we get from following the EBP/A6 chain
    DOI *pdoiNext;         // 3DMMv1.0: singly linked list
    DOI **ppdoiPrev;
};

#define kcbBaseDebug SIZEOF(DOI)

kpriv void _AssertDoi(DOI *pdoi, bool tLinked);
inline DOI *_PdoiFromBase(void *pv)
{
    return (DOI *)PvSubBv(pv, kcbBaseDebug);
}
inline BASE *_PbaseFromDoi(DOI *pdoi)
{
    return (BASE *)PvAddBv(pdoi, kcbBaseDebug);
}
kpriv void _LinkDoi(DOI *pdoi, DOI **ppdoiFirst);
kpriv void _UnlinkDoi(DOI *pdoi);

DOI *_pdoiFirst;    // 3DMMv1.0: Head of linked list of all allocated objects.
DOI *_pdoiFirstRaw; // 3DMMv1.0: Head of linked list of raw newly allocated objects.

inline void _Enter(void)
{
    vmutxBase.Enter();
}
inline void _Leave(void)
{
    vmutxBase.Leave();
}

#define klwMagicAllocatedBase KLCONST4('A', 'L', 'O', 'C')
#define klwMagicNonAllocBase KLCONST4('N', 'O', 'A', 'L')

#else //! 3DMMv1.0: DEBUG
#define kcbBaseDebug 0
#endif //! 3DMMv1.0: DEBUG

RTCLASS(BLL)

/** 3DMMv1.0: *************************************************************************
    Returns the run-time class id (cls) of the class.
***************************************************************************/
int32_t BASE::Cls(void)
{
    return kclsBASE;
}

/** 3DMMv1.0: *************************************************************************
    Returns true iff cls is kclsBASE.
***************************************************************************/
bool BASE::FIs(int32_t cls)
{
    return kclsBASE == cls;
}

/** 3DMMv1.0: *************************************************************************
    Static method. Returns true iff cls is kclsBASE.
***************************************************************************/
bool BASE::FWouldBe(int32_t cls)
{
    return kclsBASE == cls;
}

/** 3DMMv1.0: *************************************************************************
    Constructor for a BASE object.  Sets _cactRef to 1 and in debug sets
    the magic number to indicate whether the thing was allocated.
***************************************************************************/
BASE::BASE(void)
{
    _cactRef = 1;
#ifdef DEBUG
    DOI *pdoi = _pdoiFirstRaw;

    if (pvNil != pdoi)
    {
        // 3DMMv1.0: Note that we have to check _pdoiFirstRaw before entering the
        // 3DMMv1.0: mutx so we don't try to enter the mutx before it's been
        // 3DMMv1.0: initialized (during global object construction).
        _Enter();

        // 3DMMv1.0: see if this is in the raw list - note that we have to refresh
        // 3DMMv1.0: _pdoiFirstRaw in case another thread grabbed it before we got
        // 3DMMv1.0: the critical section
        for (pdoi = _pdoiFirstRaw; pdoi != pvNil && this != _PbaseFromDoi(pdoi); pdoi = pdoi->pdoiNext)
        {
            _AssertDoi(pdoi, tYes);
        }

        if (pvNil != pdoi)
        {
            // 3DMMv1.0: this is us!
            _UnlinkDoi(pdoi);
            _LinkDoi(pdoi, &_pdoiFirst);

            _lwMagic = klwMagicAllocatedBase;
            AssertValid(0);
        }

        _Leave();
    }

    if (pvNil == pdoi)
    {
        _lwMagic = klwMagicNonAllocBase;
        // 3DMMv1.0: don't call AssertValid here, since this may be during
        // 3DMMv1.0: global initialization (FAssertProc and/or AssertPvCb may not be
        // 3DMMv1.0: callable).
    }

#endif // 3DMMv1.0: DEBUG
}

/** 3DMMv1.0: *************************************************************************
    Increments the reference count.
***************************************************************************/
void BASE::AddRef(void)
{
    AssertThis(0);
    _cactRef++;

    // 3DMMv1.0: NOTE: some classes allow _cactRef == 0, so we can't assert _cactRef > 1
    Assert(_cactRef > 0, "_cactRef not positive");
}

/** 3DMMv1.0: *************************************************************************
    Decrement the reference count and delete it if the reference count goes
    to zero.
***************************************************************************/
void BASE::Release(void)
{
    AssertThis(0);
    if (--_cactRef <= 0)
    {
        AssertThis(fobjAllocated);
        delete this;
    }
}

/** 3DMMv1.0: *************************************************************************
    Used to allocate all objects.  Clears the block and in debug, adds
    magic number, reference count for marking and inserts in linked list
    of allocated objects for object leakage tracking.
***************************************************************************/
#ifdef DEBUG
void *BASE::operator new(size_t cb, PSZS pszsFile, int32_t lwLine) noexcept
#else  //! 3DMMv1.0: DEBUG
void *BASE::operator new(size_t cb) noexcept
#endif //! 3DMMv1.0: DEBUG
{
    AssertVarMem(pszsFile);
    void *pv;

#ifdef DEBUG
    // 3DMMv1.0: do failure simulation
    _Enter();

    if (vdmglob.dmaglBase.FFail())
    {
        _Leave();
        PushErc(ercOomNew);
        return pvNil;
    }

    _Leave();
#endif // 3DMMv1.0: DEBUG

#ifdef WIN
    if ((pv = (void *)GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, cb + kcbBaseDebug)) == pvNil)
#else  //! 3DMMv1.0: WIN
    if ((pv = malloc(cb + kcbBaseDebug)) == pvNil)
#endif //! 3DMMv1.0: WIN
        PushErc(ercOomNew);
    else
    {
#ifndef WIN
        memset(pv, 0, cb + kcbBaseDebug);
#endif
#ifdef DEBUG
        _Enter();

        DOI *pdoi = (DOI *)pv;

        pdoi->swMagic = kswMagicMem;
        pdoi->cactRef = 0;
        pdoi->pszsFile = pszsFile;
        pdoi->lwLine = lwLine;
        pdoi->cbTot = cb + kcbBaseDebug;
        pdoi->tid = std::this_thread::get_id();
        pdoi->ppdoiPrev = pvNil;
        _LinkDoi(pdoi, &_pdoiFirstRaw);
        pv = _PbaseFromDoi(pdoi);

#if defined(WIN) && defined(IN_80386)
        // 3DMMv1.0: follow the EBP chain....
        int32_t *plw;
        int32_t ilw;

        __asm { mov plw,ebp }
        for (ilw = 0; ilw < kclwStackDoi; ilw++)
        {
            if (pvNil == plw || IsBadReadPtr(plw, 2 * SIZEOF(int32_t)) || *plw <= (int32_t)plw)
            {
                pdoi->rglwStack[ilw] = 0;
                plw = pvNil;
            }
            else
            {
                pdoi->rglwStack[ilw] = plw[1];
                plw = (int32_t *)*plw;
            }
        }
#endif // 3DMMEx: WIN && IN_80386

        // 3DMMv1.0: update statistics
        vdmglob.dmaglBase.Allocate(cb);

        _Leave();
#endif // 3DMMv1.0: DEBUG
#ifndef WIN
        ClearPb(pv, cb);
#endif //! 3DMMv1.0: WIN
    }
    return pv;
}

#if defined(DEBUG) || defined(WIN)

/** 3DMMv1.0: *************************************************************************
    DEBUG : Unlink from linked list of allocated objects and free the memory.
***************************************************************************/
void BASE::operator delete(void *pv)
{
#ifdef DEBUG
    DOI *pdoi = _PdoiFromBase(pv);

    _UnlinkDoi(pdoi);

    // 3DMMv1.0: update statistics
    vdmglob.dmaglBase.Free(pdoi->cbTot - kcbBaseDebug);

    TrashPvCb(pdoi, pdoi->cbTot);
    pv = pdoi;
#endif // 3DMMv1.0: DEBUG
#ifdef WIN
    GlobalFree((HGLOBAL)pv);
#else  //! 3DMMv1.0: WIN
    free(pv);
#endif //! 3DMMv1.0: WIN
}

#ifdef DEBUG
void BASE::operator delete(void *pv, schar *pszsFile, int32_t lwLine)
{
    BASE::operator delete(pv);
}
#endif // 3DMMv1.0: DEBUG

#endif // 3DMMv1.0: DEBUG || WIN

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the this pointer is valid.
***************************************************************************/
void BASE::AssertValid(uint32_t grfobj)
{
    AssertVarMem(this);
    AssertIn(_cactRef, 0, kcbMax);
    if (_lwMagic != klwMagicAllocatedBase)
    {
        Assert(!(grfobj & fobjAllocated), "should be allocated");
        AssertVar(_lwMagic == klwMagicNonAllocBase, "_lwMagic wrong", &_lwMagic);
        AssertPvCb(this, SIZEOF(BASE));
        return;
    }
    Assert(!(grfobj & fobjNotAllocated), "should not be allocated");

    _Enter();

    DOI *pdoi = _PdoiFromBase(this);
    _AssertDoi(pdoi, tYes);

    _Leave();
}

/** 3DMMv1.0: *************************************************************************
    If this objects has already been marked, just return. If not, call
    its MarkMem method.
***************************************************************************/
void BASE::MarkMemStub(void)
{
    AssertThis(0);
    if (_lwMagic != klwMagicAllocatedBase)
    {
        AssertVar(_lwMagic == klwMagicNonAllocBase, "_lwMagic wrong", &_lwMagic);
        MarkMem();
        return;
    }

    DOI *pdoi = _PdoiFromBase(this);
    if (pdoi->cactRef == 0 && pdoi->tid == std::this_thread::get_id())
        MarkMem();
}

/** 3DMMv1.0: *************************************************************************
    Mark object.
***************************************************************************/
void BASE::MarkMem(void)
{
    AssertValid(0);
    if (_lwMagic == klwMagicAllocatedBase)
        _PdoiFromBase(this)->cactRef++;
}

/** 3DMMv1.0: *************************************************************************
    Assert that a doi is valid and optionally linked or unlinked.
***************************************************************************/
void _AssertDoi(DOI *pdoi, bool tLinked)
{
    _Enter();

    AssertPvCb(pdoi, SIZEOF(BASE) + kcbBaseDebug);
    AssertIn(pdoi->cbTot, SIZEOF(BASE) + kcbBaseDebug, kcbMax);
    AssertPvCb(pdoi, pdoi->cbTot);

    AssertVar(pdoi->swMagic == kswMagicMem, "magic number has been hammered", &pdoi->swMagic);
    AssertVar(pdoi->cactRef >= 0, "negative reference count", &pdoi->cactRef);
    if (pvNil == pdoi->ppdoiPrev)
        Assert(tLinked != tYes, "should be linked");
    else
    {
        Assert(tLinked != tNo, "should NOT be linked");

        AssertVarMem(pdoi->ppdoiPrev);
        AssertVar(*pdoi->ppdoiPrev == pdoi, "*ppdoiPrev is wrong", pdoi->ppdoiPrev);
        if (pdoi->pdoiNext != pvNil)
        {
            AssertVarMem(pdoi->pdoiNext);
            AssertVar(pdoi->pdoiNext->ppdoiPrev == &pdoi->pdoiNext, "ppdoiPrev in next is wrong",
                      &pdoi->pdoiNext->ppdoiPrev);
        }
    }

    _Leave();
}

/** 3DMMv1.0: *************************************************************************
    Link object into list.
***************************************************************************/
kpriv void _LinkDoi(DOI *pdoi, DOI **ppdoiFirst)
{
    _Enter();

    _AssertDoi(pdoi, tNo);
    AssertVarMem(ppdoiFirst);

    if (*ppdoiFirst != pvNil)
    {
        _AssertDoi(*ppdoiFirst, tYes);
        (*ppdoiFirst)->ppdoiPrev = &pdoi->pdoiNext;
    }
    pdoi->pdoiNext = *ppdoiFirst;
    pdoi->ppdoiPrev = ppdoiFirst;
    *ppdoiFirst = pdoi;
    _AssertDoi(pdoi, tYes);

    _Leave();
}

/** 3DMMv1.0: *************************************************************************
    Unlink object from list.
***************************************************************************/
kpriv void _UnlinkDoi(DOI *pdoi)
{
    _Enter();

    _AssertDoi(pdoi, tYes);
    *pdoi->ppdoiPrev = pdoi->pdoiNext;
    if (pvNil != pdoi->pdoiNext)
    {
        pdoi->pdoiNext->ppdoiPrev = pdoi->ppdoiPrev;
        _AssertDoi(pdoi->pdoiNext, tYes);
    }
    pdoi->ppdoiPrev = pvNil;
    pdoi->pdoiNext = pvNil;
    _AssertDoi(pdoi, tNo);

    _Leave();
}

/** 3DMMv1.0: *************************************************************************
    Called if anyone tries to copy a class with NOCOPY(cls) in its
    declaration.
***************************************************************************/
void __AssertOnCopy(void)
{
    Bug("Copying a non-copyable object");
}

/** 3DMMv1.0: *************************************************************************
    Asserts on unmarked allocated (BASE) objects.
***************************************************************************/
void AssertUnmarkedObjs(void)
{
    _Enter();

    STN stn;
    SZS szs;
    BASE *pbase;
    DOI *pdoi;
    DOI *pdoiLast;
    bool fAssert;
    int32_t cdoiLost = 0;
    auto tid = std::this_thread::get_id();

    Assert(_pdoiFirstRaw == pvNil, "Raw list is not empty!");

    // 3DMMv1.0: we want to traverse the list in the reverse order to report problems
    // 3DMMv1.0: find the end of the list and see if there are any lost blocks
    pdoiLast = pvNil;
    for (pdoi = _pdoiFirst; pvNil != pdoi; pdoi = pdoi->pdoiNext)
    {
        pdoiLast = pdoi;
        if (pdoi->cactRef == 0 && pdoi->tid == tid)
            cdoiLost++;
    }

    if (cdoiLost == 0)
    {
        // 3DMMv1.0: no lost blocks
        goto LDone;
    }

    stn.FFormatSz(PszLit("Total lost objects: %d. Press 'Debugger' for detail"), cdoiLost);
    stn.GetSzs(szs);
    fAssert = FAssertProc(__szsFile, __LINE__, szs, pvNil, 0);

    for (pdoi = pdoiLast;;)
    {
        pbase = _PbaseFromDoi(pdoi);
        AssertPo(pbase, fobjAllocated);

        if (pdoi->cactRef == 0 && pdoi->tid == tid)
        {
            if (fAssert)
            {
                stn.FFormatSz(PszLit("\nLost object: cls='%f', size=%d, ") PszLit("StackTrace=(use map file)"),
                              pbase->Cls(), pdoi->cbTot - SIZEOF(DOI));
                stn.GetSzs(szs);

                if (FAssertProc(pdoi->pszsFile, pdoi->lwLine, szs, pdoi->rglwStack, kclwStackDoi * SIZEOF(int32_t)))
                {
                    Debugger();
                }
            }

            MarkMemObj(pbase);
        }

        if (pdoi->ppdoiPrev == &_pdoiFirst)
            break;

        // 3DMMv1.0: UUUUGGGGH! We don't have a pointer to the previous DOI, we
        // 3DMMv1.0: have a pointer to the previous DOI's pdoiNext!
        pdoi = (DOI *)PvSubBv(pdoi->ppdoiPrev, offset(DOI, pdoiNext));
    }

LDone:
    _Leave();
}

/** 3DMMv1.0: *************************************************************************
    Clears all marks on allocated (BASE) objects.
***************************************************************************/
void UnmarkAllObjs(void)
{
    _Enter();

    BASE *pbase;
    DOI *pdoi;
    auto tid = std::this_thread::get_id();

    Assert(_pdoiFirstRaw == pvNil, "Raw list is not empty!");
    for (pdoi = _pdoiFirst; pvNil != pdoi; pdoi = pdoi->pdoiNext)
    {
        pbase = _PbaseFromDoi(pdoi);
        AssertPo(pbase, fobjAllocated);

        if (pdoi->tid == tid)
            pdoi->cactRef = 0;
    }

    _Leave();
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Linked list element constructor
***************************************************************************/
BLL::BLL(void)
{
    _ppbllPrev = pvNil;
    _pbllNext = pvNil;
}

/** 3DMMv1.0: *************************************************************************
    Remove the element from the linked list
***************************************************************************/
BLL::~BLL(void)
{
    // 3DMMv1.0: unlink the thing
    if (_ppbllPrev != pvNil)
        _Attach(pvNil);
}

/** 3DMMv1.0: *************************************************************************
    Remove the element from the linked list (if it's in one) and reattach
    it at ppbllPrev (if not pvNil).
***************************************************************************/
void BLL::_Attach(void *ppbllPrev)
{
    AssertThis(0);
    PBLL *ppbll = (PBLL *)ppbllPrev;
    AssertNilOrVarMem(ppbll);

    // 3DMMv1.0: unlink the thing
    if (_ppbllPrev != pvNil)
    {
        Assert(*_ppbllPrev == this, "links corrupt");
        if ((*_ppbllPrev = _pbllNext) != pvNil)
        {
            Assert(_pbllNext->_ppbllPrev == &_pbllNext, "links corrupt 2");
            _pbllNext->_ppbllPrev = _ppbllPrev;
        }
    }

    // 3DMMv1.0: link the thing
    if ((_ppbllPrev = ppbll) == pvNil)
    {
        // 3DMMv1.0: not in a linked list
        _pbllNext = pvNil;
        return;
    }
    if ((_pbllNext = *ppbll) != pvNil)
    {
        Assert(_pbllNext->_ppbllPrev == ppbll, "links corrupt 3");
        _pbllNext->_ppbllPrev = &_pbllNext;
    }
    *ppbll = this;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Check the links.
***************************************************************************/
void BLL::AssertValid(uint32_t grf)
{
    BLL_PAR::AssertValid(grf);

    if (_pbllNext != pvNil)
    {
        AssertVarMem(_pbllNext);
        Assert(_pbllNext->_ppbllPrev == &_pbllNext, "links corrupt");
    }
    if (_ppbllPrev != pvNil)
    {
        AssertVarMem(_ppbllPrev);
        Assert(*_ppbllPrev == this, "links corrupt 2");
    }
}
#endif // 3DMMv1.0: DEBUG
