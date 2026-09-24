/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    The midi player device using a Midi Stream.

***************************************************************************/
#include "frame.h"
#include "mdev2pri.h"
ASSERTNAME

#if defined(KAUAI_WIN32)
#include "midistreamwin95.h"
#include "midistreamwinnt.h"
#elif defined(KAUAI_SDL)
#ifdef KAUAI_MIDI_FLUIDSYNTH
#include "midistreamfluidsynth.h"
#endif // 3DMMEx: KAUAI_MIDI_FLUIDSYNTH
#endif // 3DMMEx: KAUAI_WIN32

RTCLASS(MDWS)
RTCLASS(MSQUE)
RTCLASS(MDPS)
RTCLASS(MSMIX)
RTCLASS(MISI)

/** 3DMMv1.0: *************************************************************************
    Constructor for the midi stream device.
***************************************************************************/
MDPS::MDPS(void)
{
}

/** 3DMMv1.0: *************************************************************************
    Destructor for the midi stream device.
***************************************************************************/
MDPS::~MDPS(void)
{
    AssertBaseThis(0);
    ReleasePpo(&_pmsmix);
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a MDPS.
***************************************************************************/
void MDPS::AssertValid(uint32_t grf)
{
    MDPS_PAR::AssertValid(0);
    AssertPo(_pmsmix, 0);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory for the MDPS.
***************************************************************************/
void MDPS::MarkMem(void)
{
    AssertValid(0);
    MDPS_PAR::MarkMem();
    MarkMemObj(_pmsmix);
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Static method to create the midi stream device.
***************************************************************************/
PMDPS MDPS::PmdpsNew(void)
{
    PMDPS pmdps;

    if (pvNil == (pmdps = NewObj MDPS))
        return pvNil;

    if (!pmdps->_FInit())
        ReleasePpo(&pmdps);

    AssertNilOrPo(pmdps, 0);
    return pmdps;
}

/** 3DMMv1.0: *************************************************************************
    Initialize the midi stream device.
***************************************************************************/
bool MDPS::_FInit(void)
{
    AssertBaseThis(0);

    if (!MDPS_PAR::_FInit())
        return fFalse;

    // 3DMMv1.0: Create the midi stream output scheduler
    if (pvNil == (_pmsmix = MSMIX::PmsmixNew()))
        return fFalse;

    _Suspend(_cactSuspend > 0 || !_fActive);

    AssertThis(0);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Allocate a new midi stream queue.
***************************************************************************/
PSNQUE MDPS::_PsnqueNew(void)
{
    AssertThis(0);

    return MSQUE::PmsqueNew(_pmsmix);
}

/** 3DMMv1.0: *************************************************************************
    Activate or deactivate the midi stream device.
***************************************************************************/
void MDPS::_Suspend(bool fSuspend)
{
    AssertThis(0);

    _pmsmix->Suspend(fSuspend);
}

/** 3DMMv1.0: *************************************************************************
    Set the volume.
***************************************************************************/
void MDPS::SetVlm(int32_t vlm)
{
    AssertThis(0);

    _pmsmix->SetVlm(vlm);
}

/** 3DMMv1.0: *************************************************************************
    Get the current volume.
***************************************************************************/
int32_t MDPS::VlmCur(void)
{
    AssertThis(0);

    return _pmsmix->VlmCur();
}

/** 3DMMv1.0: *************************************************************************
    Constructor for a midi stream object.
***************************************************************************/
MDWS::MDWS(void)
{
}

/** 3DMMv1.0: *************************************************************************
    Destructor for a Win95 midi stream object.
***************************************************************************/
MDWS::~MDWS(void)
{
    ReleasePpo(&_pglmev);
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a MDWS.
***************************************************************************/
void MDWS::AssertValid(uint32_t grf)
{
    MDWS_PAR::AssertValid(0);
    AssertPo(_pglmev, 0);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory for the MDWS.
***************************************************************************/
void MDWS::MarkMem(void)
{
    AssertValid(0);
    MDWS_PAR::MarkMem();
    MarkMemObj(_pglmev);
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    A baco reader for a midi stream.
***************************************************************************/
bool MDWS::FReadMdws(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, fblckReadable);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);
    PMDWS pmdws;

    *pcb = pblck->Cb(fTrue);
    if (pvNil == ppbaco)
        return fTrue;

    if (!pblck->FUnpackData())
        goto LFail;

    if (pvNil == (pmdws = PmdwsRead(pblck)))
    {
    LFail:
        TrashVar(ppbaco);
        TrashVar(pcb);
        return fFalse;
    }
    *pcb = pmdws->_pglmev->IvMac() * SIZEOF(MEV) + SIZEOF(MDWS);

    *ppbaco = pmdws;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Read a midi stream from the given block.
***************************************************************************/
PMDWS MDWS::PmdwsRead(PBLCK pblck)
{
    AssertPo(pblck, 0);

    PMIDS pmids;
    PMDWS pmdws;

    if (pvNil == (pmids = MIDS::PmidsRead(pblck)))
        return pvNil;

    if (pvNil != (pmdws = NewObj MDWS) && !pmdws->_FInit(pmids))
        ReleasePpo(&pmdws);

    ReleasePpo(&pmids);
    AssertNilOrPo(pmdws, 0);

    return pmdws;
}

/** 3DMMv1.0: *************************************************************************
    Initialize the MDWS with the midi data in *pmids.
***************************************************************************/
bool MDWS::_FInit(PMIDS pmids)
{
    AssertPo(pmids, 0);

    MSTP mstp;
    uint32_t tsCur;
    MEV rgmev[100];
    PMEV pmev, pmevLim;
    MIDEV midev;
    bool fEvt;

    if (pvNil == (_pglmev = GL::PglNew(SIZEOF(MEV))))
        return fFalse;

    Assert(MEVT_SHORTMSG == 0, "this code assumes MEVT_SHORTMSG is 0 and it's not");

    ClearPb(rgmev, SIZEOF(rgmev));
    pmev = rgmev;
    pmevLim = rgmev + CvFromRgv(rgmev);

    // 3DMMv1.0: use 1 second per quarter. We'll use 1000 ticks per quarter when
    // 3DMMv1.0: setting up the stream. The net result is that milliseconds correspond
    // 3DMMv1.0: to ticks, so no conversion is necessary here.
    pmev->dwEvent = ((uint32_t)MEVT_TEMPO << 24) | 1000000;
    pmev++;

    mstp.Init(pmids, 0);
    tsCur = 0;
    for (;;)
    {
        fEvt = mstp.FGetEvent(&midev);
        if (pmev >= pmevLim || !fEvt)
        {
            // 3DMMv1.0: append the MEVs in rgmev to the _pglmev
            int32_t imev, cmev;

            imev = _pglmev->IvMac();
            cmev = pmev - rgmev;
            if (!_pglmev->FSetIvMac(imev + cmev))
                return fFalse;
            CopyPb(rgmev, _pglmev->QvGet(imev), LwMul(cmev, SIZEOF(MEV)));
            if (!fEvt)
            {
                // 3DMMv1.0: Add a final NOP so when we seek and there's only one
                // 3DMMv1.0: event left, it's not an important one.
                rgmev[0].dwDeltaTime = 0;
                rgmev[0].dwStreamID = 0;
                rgmev[0].dwEvent = (uint32_t)MEVT_NOP << 24;
                if (!_pglmev->FAdd(&rgmev[0]))
                    return fFalse;

                _pglmev->FEnsureSpace(0, fgrpShrink);
                break;
            }
            pmev = rgmev;
        }

        if (midev.cb > 0)
        {
            pmev->dwDeltaTime = midev.ts - tsCur;
            pmev->dwEvent = midev.lwSend & 0x00FFFFFF;
            pmev++;
            tsCur = midev.ts;
        }
    }

    _dts = tsCur;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Return a locked pointer to the data.
***************************************************************************/
void *MDWS::PvLockData(int32_t *pcb)
{
    AssertThis(0);
    AssertVarMem(pcb);

    *pcb = LwMul(_pglmev->IvMac(), SIZEOF(MEV));
    return _pglmev->PvLock(0);
}

/** 3DMMv1.0: *************************************************************************
    Balance a call to PvLockData.
***************************************************************************/
void MDWS::UnlockData(void)
{
    AssertThis(0);

    _pglmev->Unlock();
}

/** 3DMMv1.0: *************************************************************************
    Constructor for a midi stream queue.
***************************************************************************/
MSQUE::MSQUE(void)
{
}

/** 3DMMv1.0: *************************************************************************
    Destructor for a midi stream queue.
***************************************************************************/
MSQUE::~MSQUE(void)
{
    if (pvNil != _pmsmix)
    {
        _pmsmix->FPlay(this);
        ReleasePpo(&_pmsmix);
    }
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a MSQUE.
***************************************************************************/
void MSQUE::AssertValid(uint32_t grf)
{
    MSQUE_PAR::AssertValid(0);
    AssertPo(_pmsmix, 0);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory for the MSQUE.
***************************************************************************/
void MSQUE::MarkMem(void)
{
    AssertValid(0);
    MSQUE_PAR::MarkMem();
    MarkMemObj(_pmsmix);
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Static method to create a new midi stream queue.
***************************************************************************/
PMSQUE MSQUE::PmsqueNew(PMSMIX pmsmix)
{
    AssertPo(pmsmix, 0);
    PMSQUE pmsque;

    if (pvNil == (pmsque = NewObj MSQUE))
        return pvNil;

    if (!pmsque->_FInit(pmsmix))
        ReleasePpo(&pmsque);

    AssertNilOrPo(pmsque, 0);
    return pmsque;
}

/** 3DMMv1.0: *************************************************************************
    Initialize the midi stream queue.
***************************************************************************/
bool MSQUE::_FInit(PMSMIX pmsmix)
{
    AssertPo(pmsmix, 0);
    AssertBaseThis(0);

    if (!MSQUE_PAR::_FInit())
        return fFalse;

    _pmsmix = pmsmix;
    _pmsmix->AddRef();

    AssertThis(0);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Enter the critical section protecting member variables.
***************************************************************************/
void MSQUE::_Enter(void)
{
    _mutx.Enter();
}

/** 3DMMv1.0: *************************************************************************
    Leave the critical section protecting member variables.
***************************************************************************/
void MSQUE::_Leave(void)
{
    _mutx.Leave();
}

/** 3DMMv1.0: *************************************************************************
    Fetch the given sound chunk as an MDWS.
***************************************************************************/
PBACO MSQUE::_PbacoFetch(PRCA prca, CTG ctg, CNO cno)
{
    AssertThis(0);
    AssertPo(prca, 0);

    return prca->PbacoFetch(ctg, cno, &MDWS::FReadMdws);
}

/** 3DMMv1.0: *************************************************************************
    An item was added to or deleted from the queue.
***************************************************************************/
void MSQUE::_Queue(int32_t isndinMin)
{
    AssertThis(0);
    SNDIN sndin;

    _mutx.Enter();

    if (_isndinCur == isndinMin && pvNil != _pglsndin)
    {
        for (; _isndinCur < _pglsndin->IvMac(); _isndinCur++)
        {
            _pglsndin->Get(_isndinCur, &sndin);
            if (0 < sndin.cactPause)
                break;

            if (0 == sndin.cactPause && _pmsmix->FPlay(this, (PMDWS)sndin.pbaco, sndin.sii, sndin.spr, sndin.cactPlay,
                                                       sndin.dtsStart, sndin.vlm))
            {
                _tsStart = TsCurrentSystem() - sndin.dtsStart;
                goto LDone;
            }
        }

        _pmsmix->FPlay(this);
    }

LDone:
    _mutx.Leave();
}

/** 3DMMv1.0: *************************************************************************
    One or more items in the queue were paused.
***************************************************************************/
void MSQUE::_PauseQueue(int32_t isndinMin)
{
    AssertThis(0);
    SNDIN sndin;

    _mutx.Enter();

    if (_isndinCur == isndinMin && _pglsndin->IvMac() > _isndinCur)
    {
        _pglsndin->Get(_isndinCur, &sndin);
        sndin.dtsStart = TsCurrentSystem() - _tsStart;
        _pglsndin->Put(_isndinCur, &sndin);

        _Queue(isndinMin);
    }

    _mutx.Leave();
}

/** 3DMMv1.0: *************************************************************************
    One or more items in the queue were resumed.
***************************************************************************/
void MSQUE::_ResumeQueue(int32_t isndinMin)
{
    AssertThis(0);

    _Queue(isndinMin);
}

/** 3DMMv1.0: *************************************************************************
    Called by the MSMIX to tell us that the indicated sound is done.
    WARNING: this is called in an auxillary thread.
***************************************************************************/
void MSQUE::Notify(PMDWS pmdws)
{
    AssertThis(0);
    SNDIN sndin;

    _mutx.Enter();

    if (pvNil != _pglsndin && _pglsndin->IvMac() > _isndinCur)
    {
        _pglsndin->Get(_isndinCur, &sndin);
        if (pmdws == sndin.pbaco)
        {
            _isndinCur++;
            _Queue(_isndinCur);
        }
    }

    _mutx.Leave();
}

/** 3DMMv1.0: *************************************************************************
    Constructor for the midi stream output object.
***************************************************************************/
MSMIX::MSMIX(void)
{
    _vlmBase = kvlmFull;
    _vlmSound = kvlmFull;
}

/** 3DMMv1.0: *************************************************************************
    Destructor for the midi stream output object.
***************************************************************************/
MSMIX::~MSMIX(void)
{
    Assert(pvNil == _pmisi || !_pmisi->FActive(), "MISI still active!");

    if (_thrdCleanup.joinable())
    {
        // 3DMMv1.0: tell the thread to end and wait for it to finish
        _fDone = fTrue;
        _sgnlChanged.Set();
        _thrdCleanup.join();
    }

    if (pvNil != _pglmsos)
    {
        Assert(_pglmsos->IvMac() == 0, "MSMIX still has active sounds");
        ReleasePpo(&_pglmsos);
    }
    ReleasePpo(&_pmisi);
    ReleasePpo(&_pglmevKey);
}

/** 3DMMv1.0: *************************************************************************
    Static method to create a new MSMIX.
***************************************************************************/
PMSMIX MSMIX::PmsmixNew(void)
{
    PMSMIX pmsmix;

    if (pvNil == (pmsmix = NewObj MSMIX))
        return pvNil;

    if (!pmsmix->_FInit())
        ReleasePpo(&pmsmix);

    AssertNilOrPo(pmsmix, 0);
    return pmsmix;
}

/** 3DMMv1.0: *************************************************************************
    Initialize the MSMIX - allocate the pglmsos and the midi stream api
    object.
***************************************************************************/
bool MSMIX::_FInit(void)
{
    AssertBaseThis(0);

    if (pvNil == (_pglmsos = GL::PglNew(SIZEOF(MSOS))))
        return fFalse;
    _pglmsos->SetMinGrow(1);

#if defined(KAUAI_WIN32)
    if (pvNil == (_pmisi = WMS::PwmsNew(_MidiProc, (uintptr_t)this)) &&
        pvNil == (_pmisi = OMS::PomsNew(_MidiProc, (uintptr_t)this)))
    {
        return fFalse;
    }
#elif defined(KAUAI_SDL)

#if defined(KAUAI_MIDI_FLUIDSYNTH)
    if (pvNil == (_pmisi = FMS::PfmsNew(_MidiProc, (uintptr_t)this)))
    {
        return fFalse;
    }
#else
    Warn("No MIDI player available");
    return fFalse;
#endif // 3DMMEx: KAUAI_MIDI_FLUIDSYNTH

#endif

    if (_pmisi == pvNil)
    {
        Warn("No MIDI stream device available");
        return fFalse;
    }

    // 3DMMv1.0: create the thread
    _thrdCleanup = std::thread([this] { return this->_LuThread(); });

    return fTrue;
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a MSMIX.
***************************************************************************/
void MSMIX::AssertValid(uint32_t grf)
{
    MSMIX_PAR::AssertValid(0);
    _mutx.Enter();
    AssertPo(_pglmsos, 0);
    AssertPo(_pmisi, 0);
    AssertNilOrPo(_pglmevKey, 0);
    _mutx.Leave();
}

/** 3DMMv1.0: *************************************************************************
    Mark memory for the MSMIX.
***************************************************************************/
void MSMIX::MarkMem(void)
{
    AssertValid(0);

    MSMIX_PAR::MarkMem();

    _mutx.Enter();
    MarkMemObj(_pglmsos);
    MarkMemObj(_pmisi);
    MarkMemObj(_pglmevKey);
    _mutx.Leave();
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Suspend or resume the midi stream mixer.
***************************************************************************/
void MSMIX::Suspend(bool fSuspend)
{
    AssertThis(0);

    _mutx.Enter();

    if (fSuspend)
        _StopStream();
    if (!_pmisi->FActivate(!fSuspend) && !fSuspend)
        PushErc(ercSndMidiDeviceBusy);
    _Restart();

    _mutx.Leave();
}

/** 3DMMv1.0: *************************************************************************
    If we're currently playing a midi stream stop it. Assumes the mutx is
    already checked out exactly once.
***************************************************************************/
void MSMIX::_StopStream(void)
{
    AssertThis(0);

    if (!_fPlaying)
        return;

    // 3DMMv1.0: set _fPlaying to false first so the call back knows that we're
    // 3DMMv1.0: aborting the current stream - so it doesn't notify us.
    _fPlaying = fFalse;

    _pmisi->StopPlaying();

    // 3DMMv1.0: Wait for the buffers to be returned
    _fWaiting = fTrue;
    _mutx.Leave();

    while (_cpvOut > 0)
        std::this_thread::yield();

    _mutx.Enter();
    _fWaiting = fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Set the volume for the midi stream output device.
***************************************************************************/
void MSMIX::SetVlm(int32_t vlm)
{
    AssertThis(0);
    uint32_t luHigh, luLow;

    _mutx.Enter();

    _vlmBase = vlm;
    MulLu(_vlmBase, _vlmSound, &luHigh, &luLow);
    _pmisi->SetVlm(LuHighLow(SuLow(luHigh), SuHigh(luLow)));

    _mutx.Leave();
}

/** 3DMMv1.0: *************************************************************************
    Get the current volume.
***************************************************************************/
int32_t MSMIX::VlmCur(void)
{
    AssertThis(0);

    return _vlmBase;
}

/** 3DMMv1.0: *************************************************************************
    Play the given midi stream from the indicated queue.
***************************************************************************/
bool MSMIX::FPlay(PMSQUE pmsque, PMDWS pmdws, int32_t sii, int32_t spr, int32_t cactPlay, uint32_t dtsStart,
                  int32_t vlm)
{
    AssertThis(0);
    AssertPo(pmsque, 0);
    AssertNilOrPo(pmdws, 0);
    int32_t imsos;
    MSOS msos;
    bool fNew = fFalse;
    bool fRet = fTrue;

    if (pvNil != pmdws && pmdws->Dts() == 0)
        return fFalse;

    _mutx.Enter();

    // 3DMMv1.0: stop any current midi stream on this msque
    for (imsos = _pglmsos->IvMac(); imsos-- > 0;)
    {
        _pglmsos->Get(imsos, &msos);
        if (msos.pmsque == pmsque)
        {
            if (0 == imsos)
                _StopStream();
            _pglmsos->Get(imsos, &msos);
            _pglmsos->Delete(imsos);
            break;
        }
    }

    // 3DMMv1.0: start up the new midi stream
    if (pvNil != pmdws)
    {
        // 3DMMv1.0: find the position to insert the new one
        for (imsos = 0; imsos < _pglmsos->IvMac(); imsos++)
        {
            _pglmsos->Get(imsos, &msos);
            if (msos.spr < spr || msos.spr == spr && msos.sii < sii)
            {
                // 3DMMv1.0: insert before the current one
                break;
            }
        }

        if (0 == imsos)
        {
            fNew = fTrue;
            _StopStream();
        }

        ClearPb(&msos, SIZEOF(msos));
        msos.pmsque = pmsque;
        msos.pmdws = pmdws;
        msos.sii = sii;
        msos.spr = spr;
        msos.cactPlay = cactPlay;
        msos.dts = pmdws->Dts();
        msos.dtsStart = dtsStart;
        msos.vlm = vlm;
        msos.tsStart = TsCurrentSystem() - msos.dtsStart;

        if (!_pglmsos->FInsert(imsos, &msos))
        {
            fRet = fFalse;
            fNew = fFalse;
        }
    }

    _Restart(fNew);

    _mutx.Leave();
    return fRet;
}

/** 3DMMv1.0: *************************************************************************
    The sound list changed so make sure we're playing the first tune.
    Assumes the mutx is already checked out.
***************************************************************************/
void MSMIX::_Restart(bool fNew)
{
    AssertThis(0);

    if (_pmisi->FActive() && !_fPlaying && _pglmsos->IvMac() > 0)
    {
        // 3DMMv1.0: start playing the first MSOS
        MSOS msos;
        uint32_t tsCur = TsCurrentSystem();

        if (fNew)
        {
            _pglmsos->Get(0, &msos);
            msos.tsStart = tsCur - msos.dtsStart;
            _pglmsos->Put(0, &msos);
        }
        _SubmitBuffers(tsCur);
    }

    // 3DMMv1.0: signal the aux thread that the list changed
    _sgnlChanged.Set();
}

/** 3DMMv1.0: *************************************************************************
    Submit the buffer(s) for the current MSOS. Assumes the mutx is already
    checked out.
***************************************************************************/
void MSMIX::_SubmitBuffers(uint32_t tsCur)
{
    Assert(!_fPlaying, "already playing!");
    int32_t cb, cbSkip;
    MSOS msos;
    int32_t imsos;
    void *pvData;
    int32_t cactSkip;

    for (imsos = 0; imsos < _pglmsos->IvMac(); imsos++)
    {
        _pglmsos->Get(imsos, &msos);

        cactSkip = (tsCur - msos.tsStart) / msos.dts;
        if (cactSkip > 0)
        {
            uint32_t dtsSeek;

            // 3DMMv1.0: we need to skip at least one loop of this sound
            if (msos.cactPlay > 0 && (msos.cactPlay -= cactSkip) <= 0)
                goto LTryNext;

            dtsSeek = (tsCur - msos.tsStart) % msos.dts;
            msos.tsStart = tsCur - dtsSeek;
            _pglmsos->Put(imsos, &msos);
        }

        // 3DMMv1.0: Calling SetVlm causes us to tell the MISI about the new volume
        _vlmSound = msos.vlm;
        SetVlm(_vlmBase);

        cbSkip = 0;
        if (tsCur != msos.tsStart)
        {
            // 3DMMv1.0: have to seek into the stream
            if (!_FGetKeyEvents(msos.pmdws, tsCur - msos.tsStart, &cbSkip))
                goto LTryNext;

            cb = LwMul(_pglmevKey->IvMac(), SIZEOF(MEV));
            if (0 < cb)
            {
                pvData = _pglmevKey->PvLock(0);
                if (!_pmisi->FQueueBuffer(pvData, cb, 0, 1, 0))
                {
                    // 3DMMv1.0: streaming the key events failed
                    _pglmevKey->Unlock();
                    goto LTryNext;
                }

                _cpvOut++;
                _fPlaying = fTrue;
            }
        }

        _cpvOut++;
        pvData = msos.pmdws->PvLockData(&cb);
        if (_pmisi->FQueueBuffer(pvData, cb, cbSkip, msos.cactPlay, (uintptr_t)msos.pmdws))
        {
            // 3DMMv1.0: it worked!
            _fPlaying = fTrue;
            break;
        }

        // 3DMMv1.0: submitting the buffer failed
        msos.pmdws->UnlockData();
        _cpvOut--;

    LTryNext:
        // 3DMMv1.0: make this one disappear
        // 3DMMv1.0: stop the seek buffer from playing
        _StopStream();

        // 3DMMv1.0: make this MSOS have lowest possible priority and 0 time
        // 3DMMv1.0: remaining to play - we'll move it to the end of _pglmsos
        // 3DMMv1.0: in the code below.
        msos.tsStart = tsCur - msos.dtsStart;
        msos.cactPlay = 1;
        msos.sii = klwMin;
        msos.spr = klwMin;
        _pglmsos->Put(imsos, &msos);
    }

    if (_fPlaying && imsos > 0)
    {
        // 3DMMv1.0: move the skipped ones to the end of the list
        int32_t cmsos = _pglmsos->IvMac();

        AssertIn(imsos, 1, cmsos);
        SwapBlocks(_pglmsos->QvGet(0), LwMul(imsos, SIZEOF(MSOS)), LwMul(cmsos - imsos, SIZEOF(MSOS)));
    }
}

/** 3DMMv1.0: *************************************************************************
    Seek into the pmdws the given amount of time, and accumulate key events
    in _pglmevKey.
***************************************************************************/
bool MSMIX::_FGetKeyEvents(PMDWS pmdws, uint32_t dtsSeek, int32_t *pcbSkip)
{
    AssertPo(pmdws, 0);
    AssertVarMem(pcbSkip);

    // 3DMMv1.0: This keeps track of which events we've seen (so we only record the
    // 3DMMv1.0: most recent one. We record tempo changes, program changes and
    // 3DMMv1.0: controller changes.
    struct MKEY
    {
        uint16_t grfbitProgram;
        uint16_t grfbitChannelPressure;
        uint16_t grfbitPitchWheel;
        uint16_t fTempo : 1;

        uint16_t rggrfbitControl[120];
    };

    MKEY mkey;
    PMEV pmev;
    PMEV pmevMin;
    PMEV pmevLim;

    MEV rgmev[100];
    PMEV pmevDst;
    PMEV pmevLimDst;

    int32_t cb;
    uint32_t dts;
    int32_t igrfbit;
    uint16_t fbit;
    uint16_t *pgrfbit;
    uint8_t bT;

    ClearPb(&mkey, SIZEOF(mkey));
    ClearPb(rgmev, SIZEOF(rgmev));

    if (pvNil == _pglmevKey && (pvNil == (_pglmevKey = GL::PglNew(SIZEOF(MEV)))))
        return fFalse;
    _pglmevKey->FSetIvMac(0);

    pmevMin = (PMEV)pmdws->PvLockData(&cb);
    cb = LwRoundToward(cb, SIZEOF(MEV));
    pmevLim = (PMEV)PvAddBv(pmevMin, cb);

    dts = 0;
    for (pmev = pmevMin; pmev < pmevLim; pmev++)
    {
        dts += pmev->dwDeltaTime;
        if (dts >= dtsSeek)
            break;
    }

    if (pmev + 1 >= pmevLim)
    {
        // 3DMMv1.0: dtsSeek goes past the end!
        goto LFail;
    }

    // 3DMMv1.0: get the destination pointer - this walks backwards
    pmevLimDst = rgmev + CvFromRgv(rgmev);
    pmevDst = pmevLimDst;

    // 3DMMv1.0: put the first event in the key frame list with a smaller time
    --pmevDst;
    pmevDst->dwDeltaTime = dts - dtsSeek;
    pmevDst->dwEvent = pmev->dwEvent;
    *pcbSkip = BvSubPvs(pmev + 1, pmevMin);

    for (;;)
    {
        if (pmevDst <= rgmev || pmev <= pmevMin)
        {
            // 3DMMv1.0: destination buffer is full - write it out
            int32_t cmev, cmevNew;
            PMEV qrgmev;

            cmev = _pglmevKey->IvMac();
            cmevNew = pmevLimDst - pmevDst;

            if (!_pglmevKey->FSetIvMac(cmev + cmevNew))
            {
            LFail:
                _pglmevKey->FSetIvMac(0);
                _pglmevKey->FEnsureSpace(0, fgrpShrink);
                pmdws->UnlockData();
                return fFalse;
            }

            qrgmev = (PMEV)_pglmevKey->QvGet(0);
            BltPb(qrgmev, qrgmev + cmevNew, LwMul(cmev, SIZEOF(MEV)));
            CopyPb(pmevDst, qrgmev, LwMul(cmevNew, SIZEOF(MEV)));

            if (pmev <= pmevMin)
                break;

            pmevDst = pmevLimDst;
        }

        --pmev;
        switch (pmev->dwEvent >> 24)
        {
        case MEVT_SHORTMSG:
            bT = (uint8_t)pmev->dwEvent;

            // 3DMMv1.0: The high nibble of bT is the status value
            // 3DMMv1.0: The low nibble is the channel
            switch (bT & 0xF0)
            {
            case 0xB0: // 3DMMv1.0: control change
                igrfbit = BHigh(SuLow(pmev->dwEvent));
                if (!FIn(igrfbit, 0, CvFromRgv(mkey.rggrfbitControl)))
                    break;
                pgrfbit = &mkey.rggrfbitControl[igrfbit];
                goto LTest;

            case 0xC0: // 3DMMv1.0: program change
                pgrfbit = &mkey.grfbitProgram;
                goto LTest;

            case 0xD0: // 3DMMv1.0: channel pressure
                pgrfbit = &mkey.grfbitChannelPressure;
                goto LTest;

            case 0xE0: // 3DMMv1.0: pitch wheel
                pgrfbit = &mkey.grfbitPitchWheel;
            LTest:
                fbit = 1 << (bT & 0x0F);
                if (!(*pgrfbit & fbit))
                {
                    // 3DMMv1.0: first time we've seen this event on this channel
                    *pgrfbit |= fbit;
                    goto LCopy;
                }
                break;
            }
            break;

        case MEVT_TEMPO:
            if (!mkey.fTempo)
            {
                mkey.fTempo = fTrue;
            LCopy:
                pmevDst--;
                pmevDst->dwDeltaTime = 0;
                pmevDst->dwEvent = pmev->dwEvent;
            }
            break;
        }
    }

    _pglmevKey->FEnsureSpace(0, fgrpShrink);
    pmdws->UnlockData();
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Call back from the midi stream stuff.
***************************************************************************/
void MSMIX::_MidiProc(uintptr_t luUser, void *pvData, uintptr_t luData)
{
    PMSMIX pmsmix;
    PMDWS pmdws;

    pmsmix = (PMSMIX)luUser;
    AssertPo(pmsmix, 0);
    pmdws = (PMDWS)luData;
    AssertNilOrPo(pmdws, 0);

    pmsmix->_Notify(pvData, pmdws);
}

/** 3DMMv1.0: *************************************************************************
    The midi stream is done with the given header.
***************************************************************************/
void MSMIX::_Notify(void *pvData, PMDWS pmdws)
{
    AssertNilOrPo(pmdws, 0);
    MSOS msos;

    _mutx.Enter();

    Assert(_cpvOut > 0, "what buffer is this?");
    _cpvOut--;
    if (pvNil != pmdws)
    {
        AssertVar(_pglmsos->IvMac() > 0 && ((MSOS *)_pglmsos->QvGet(0))->pmdws == pmdws, "Wrong pmdws", &pmdws);
        pmdws->UnlockData();
    }
    else if (pvNil != _pglmevKey && pvData == _pglmevKey->QvGet(0))
        _pglmevKey->Unlock();
    else
    {
        Bug("Bad pvData/pmdws combo");
    }

    if (!_fPlaying)
    {
        // 3DMMv1.0: we don't need to notify or start the next sound.
        _mutx.Leave();
        return;
    }

    if (_fPlaying && _cpvOut == 0)
    {
        // 3DMMv1.0: all headers are in and we're supposed to be playing - so notify the
        // 3DMMv1.0: previous pmdws and start up the next one.
        _fPlaying = fFalse;
        if (0 < _pglmsos->IvMac())
        {
            _pglmsos->Get(0, &msos);
            _pglmsos->Delete(0);

            _mutx.Leave();

            // 3DMMv1.0: do the notify
            msos.pmsque->Notify(msos.pmdws);

            _mutx.Enter();
        }

        if (_pglmsos->IvMac() > 0)
            _Restart();
    }

    _mutx.Leave();
}

/** 3DMMv1.0: *************************************************************************
    AT: This thread just sleeps until the next sound is due to expire, then
    wakes up and nukes any expired sounds.
***************************************************************************/
uint32_t MSMIX::_LuThread(void)
{
    AssertThis(0);
    uint32_t tsCur;
    int32_t imsos;
    MSOS msos;
    int32_t cactSkip;
    uint32_t dtsNextStop = kluMax;

    for (;;)
    {
        _sgnlChanged.Wait(dtsNextStop);

        if (_fDone)
            return 0;

        _mutx.Enter();

        if (_fWaiting)
        {
            // 3DMMv1.0: we're waiting for buffers to be returned, so don't touch
            // 3DMMv1.0: anything!
            dtsNextStop = 1;
        }
        else
        {
            // 3DMMv1.0: See if any sounds have expired...
            tsCur = TsCurrentSystem();
            dtsNextStop = kluMax;
            for (imsos = _pglmsos->IvMac(); imsos-- > 0;)
            {
                if (imsos == 0 && _fPlaying)
                    break;
                _pglmsos->Get(imsos, &msos);

                cactSkip = (tsCur - msos.tsStart) / msos.dts;
                if (cactSkip > 0)
                {
                    uint32_t dtsSeek;

                    if (msos.cactPlay > 0 && (msos.cactPlay -= cactSkip) <= 0)
                    {
                        // 3DMMv1.0: this sound is done
                        _pglmsos->Delete(imsos);
                        _mutx.Leave();

                        // 3DMMv1.0: do the notify
                        msos.pmsque->Notify(msos.pmdws);

                        _mutx.Enter();
                        dtsNextStop = 0;
                        break;
                    }

                    // 3DMMv1.0: adjust the values in the MSOS
                    dtsSeek = (tsCur - msos.tsStart) % msos.dts;
                    msos.tsStart = tsCur - dtsSeek;
                    _pglmsos->Put(imsos, &msos);
                }

                dtsNextStop = LuMin(dtsNextStop, msos.dts - (tsCur - msos.tsStart));
            }
        }

        _mutx.Leave();
    }
}
