/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    Sound manager class implementation.

***************************************************************************/
#include "frame.h"
ASSERTNAME

RTCLASS(SNDV)
RTCLASS(SNDM)
RTCLASS(SNDMQ)
RTCLASS(SNQUE)

int32_t SNDV::_siiLast;

/** 3DMMv1.0: *************************************************************************
    This is the volume to use as the system volume if it is determined
    by a device that the system lies when asked what the current
    volume setting is. Eg, for many drivers, midiOutGetVolume on Win95
    always returns full volume. When we determine that this is the case,
    we'll use this value instead. Any device that determines that its
    reading is valid should set this so other devices can use the same
    value.
***************************************************************************/
uint32_t vluSysVolFake = (uint32_t)-1;

// 3DMM audio v1: the Help-machine McZee voice toggle also owns Studio UI sounds.
// Keep these product property/class values local so Kauai does not depend on Studio headers.
static const int32_t kprid3dmmMcZeeAudio = 0x23503;
static const int32_t kflg3dmmMcZeeAudio = 0x01;
static const int32_t kscl3dmmUISound = 10000;

static bool _F3dmmUISoundSuppressed(void)
{
    int32_t grfAudio = 0;
    return vpappb != pvNil && vpappb->FGetProp(kprid3dmmMcZeeAudio, &grfAudio) &&
           (grfAudio & kflg3dmmMcZeeAudio) != 0;
}

/** 3DMMv1.0: *************************************************************************
    Start a synchronized group.
***************************************************************************/
void SNDV::BeginSynch(void)
{
}

/** 3DMMv1.0: *************************************************************************
    End a synchronized group.
***************************************************************************/
void SNDV::EndSynch(void)
{
}

/** 3DMMv1.0: *************************************************************************
    Constructor for the sound manager.
***************************************************************************/
SNDM::SNDM(void)
{
}

/** 3DMMv1.0: *************************************************************************
    Destructor for the sound manager.
***************************************************************************/
SNDM::~SNDM(void)
{
    AssertBaseThis(0);
    SNDMPE sndmpe;
    int32_t isndmpe;

    if (pvNil != _pglsndmpe)
    {
        for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
        {
            _pglsndmpe->Get(isndmpe, &sndmpe);
            ReleasePpo(&sndmpe.psndv);
        }
        ReleasePpo(&_pglsndmpe);
    }
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a SNDM.
***************************************************************************/
void SNDM::AssertValid(uint32_t grf)
{
    SNDM_PAR::AssertValid(0);
    AssertPo(_pglsndmpe, 0);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory for the SNDM.
***************************************************************************/
void SNDM::MarkMem(void)
{
    AssertValid(0);
    int32_t isndmpe;
    SNDMPE sndmpe;

    SNDM_PAR::MarkMem();
    MarkMemObj(_pglsndmpe);
    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        MarkMemObj(sndmpe.psndv);
    }
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Create the sound manager.
***************************************************************************/
PSNDM SNDM::PsndmNew(void)
{
    PSNDM psndm;

    if (pvNil == (psndm = NewObj SNDM))
        return pvNil;

    if (!psndm->_FInit())
        ReleasePpo(&psndm);

    AssertNilOrPo(psndm, 0);
    return psndm;
}

/** 3DMMv1.0: *************************************************************************
    Initialize the sound manager.
***************************************************************************/
bool SNDM::_FInit(void)
{
    AssertBaseThis(0);

    if (pvNil == (_pglsndmpe = GL::PglNew(SIZEOF(SNDMPE))))
        return fFalse;

    _pglsndmpe->SetMinGrow(1);
    _fActive = fTrue;

    AssertThis(0);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Find the device that sounds of the given ctg are to be played on.
***************************************************************************/
bool SNDM::_FFindCtg(CTG ctg, SNDMPE *psndmpe, int32_t *pisndmpe)
{
    AssertThis(0);
    AssertNilOrVarMem(psndmpe);
    AssertNilOrVarMem(pisndmpe);
    int32_t isndmpe;
    SNDMPE sndmpe;

    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        if (sndmpe.ctg == ctg)
        {
            if (pvNil != psndmpe)
                *psndmpe = sndmpe;
            if (pvNil != pisndmpe)
                *pisndmpe = isndmpe;
            return fTrue;
        }
    }

    TrashVar(psndmpe);
    if (pvNil != pisndmpe)
        *pisndmpe = _pglsndmpe->IvMac();
    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Add a device to the device map to handle the particular ctg.
***************************************************************************/
bool SNDM::FAddDevice(CTG ctg, PSNDV psndv)
{
    AssertThis(0);
    AssertPo(psndv, 0);
    SNDMPE sndmpe;
    int32_t isndmpe;
    int32_t cact;

    psndv->AddRef();
    if (_FFindCtg(ctg, &sndmpe, &isndmpe))
    {
        ReleasePpo(&sndmpe.psndv);
        sndmpe.psndv = psndv;
        _pglsndmpe->Put(isndmpe, &sndmpe);
    }
    else
    {
        sndmpe.ctg = ctg;
        sndmpe.psndv = psndv;
        if (!_pglsndmpe->FInsert(isndmpe, &sndmpe))
        {
            ReleasePpo(&psndv);
            return fFalse;
        }
    }

    psndv->Activate(_fActive);
    for (cact = 0; cact < _cactSuspend; cact++)
        psndv->Suspend(fTrue);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Return the sound device that is registered for the given ctg.
***************************************************************************/
PSNDV SNDM::PsndvFromCtg(CTG ctg)
{
    AssertThis(0);
    SNDMPE sndmpe;

    if (!_FFindCtg(ctg, &sndmpe))
        return pvNil;

    return sndmpe.psndv;
}

/** 3DMMv1.0: *************************************************************************
    Remove the sound device for the given ctg.
***************************************************************************/
void SNDM::RemoveSndv(CTG ctg)
{
    AssertThis(0);
    SNDMPE sndmpe;
    int32_t isndmpe;

    if (!_FFindCtg(ctg, &sndmpe, &isndmpe))
        return;

    _pglsndmpe->Delete(isndmpe);
    ReleasePpo(&sndmpe.psndv);
}

/** 3DMMv1.0: *************************************************************************
    Return whether the sound manager is active.
***************************************************************************/
bool SNDM::FActive(void)
{
    AssertThis(0);

    return _fActive;
}

/** 3DMMv1.0: *************************************************************************
    Activate or deactivate the sound manager.
***************************************************************************/
void SNDM::Activate(bool fActive)
{
    AssertThis(0);
    SNDMPE sndmpe;
    int32_t isndmpe;

    if (FPure(fActive) == FPure(_fActive))
        return;

    _fActive = FPure(fActive);
    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        sndmpe.psndv->Activate(fActive);
    }
}

/** 3DMMv1.0: *************************************************************************
    Suspend or resume the sound manager.
***************************************************************************/
void SNDM::Suspend(bool fSuspend)
{
    AssertThis(0);
    SNDMPE sndmpe;
    int32_t isndmpe;

    if (fSuspend)
        _cactSuspend++;
    else
        _cactSuspend--;

    Assert(_cactSuspend >= (FPure(fSuspend) ? 1 : 0), "bad _cactSuspend");
    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        sndmpe.psndv->Suspend(fSuspend);
    }
}

/** 3DMMv1.0: *************************************************************************
    Set the volume of all the devices.
***************************************************************************/
void SNDM::SetVlm(int32_t vlm)
{
    AssertThis(0);
    SNDMPE sndmpe;
    int32_t isndmpe;

    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        sndmpe.psndv->SetVlm(vlm);
    }
}

/** 3DMMv1.0: *************************************************************************
    Get the max of the volumes of all the devices.
***************************************************************************/
int32_t SNDM::VlmCur(void)
{
    AssertThis(0);
    SNDMPE sndmpe;
    int32_t isndmpe;
    int32_t vlm;

    vlm = 0;
    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        vlm = LwMax(vlm, sndmpe.psndv->VlmCur());
    }

    return vlm;
}

/** 3DMMv1.0: *************************************************************************
    Play the given sound.
***************************************************************************/
int32_t SNDM::SiiPlay(PRCA prca, CTG ctg, CNO cno, int32_t sqn, int32_t vlm, int32_t cactPlay, uint32_t dtsStart,
                      int32_t spr, int32_t scl)
{
    AssertThis(0);
    AssertPo(prca, 0);
    SNDMPE sndmpe;

    // The movie sound engine uses its own sound classes (0/1/2). Only the
    // dedicated Studio UI class is suppressed here, so Sounds-tab/movie audio
    // continues to play normally.
    if (scl == kscl3dmmUISound && _F3dmmUISoundSuppressed())
        return siiNil;

    if (!_FFindCtg(ctg, &sndmpe))
        return _SiiAlloc();

    return sndmpe.psndv->SiiPlay(prca, ctg, cno, sqn, vlm, cactPlay, dtsStart, spr, scl);
}

/** 3DMMv1.0: *************************************************************************
    Stop the given sound instance.
***************************************************************************/
void SNDM::Stop(int32_t sii)
{
    AssertThis(0);
    SNDMPE sndmpe;
    int32_t isndmpe;

    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        sndmpe.psndv->Stop(sii);
    }
}

/** 3DMMv1.0: *************************************************************************
    Stop all sounds of the given queue and class (one or both may be nil).
***************************************************************************/
void SNDM::StopAll(int32_t sqn, int32_t scl)
{
    AssertThis(0);
    SNDMPE sndmpe;
    int32_t isndmpe;

    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        sndmpe.psndv->StopAll(sqn, scl);
    }
}

/** 3DMMv1.0: *************************************************************************
    Pause the given sound.
***************************************************************************/
void SNDM::Pause(int32_t sii)
{
    AssertThis(0);
    SNDMPE sndmpe;
    int32_t isndmpe;

    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        sndmpe.psndv->Pause(sii);
    }
}

/** 3DMMv1.0: *************************************************************************
    Pause all sounds of the given queue and class (one or both may be nil).
***************************************************************************/
void SNDM::PauseAll(int32_t sqn, int32_t scl)
{
    AssertThis(0);
    SNDMPE sndmpe;
    int32_t isndmpe;

    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        sndmpe.psndv->PauseAll(sqn, scl);
    }
}

/** 3DMMv1.0: *************************************************************************
    Resume the given sound.
***************************************************************************/
void SNDM::Resume(int32_t sii)
{
    AssertThis(0);
    SNDMPE sndmpe;
    int32_t isndmpe;

    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        sndmpe.psndv->Resume(sii);
    }
}

/** 3DMMv1.0: *************************************************************************
    Resume all sounds of the given queue and class (one or both may be nil).
***************************************************************************/
void SNDM::ResumeAll(int32_t sqn, int32_t scl)
{
    AssertThis(0);
    SNDMPE sndmpe;
    int32_t isndmpe;

    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        sndmpe.psndv->ResumeAll(sqn, scl);
    }
}

/** 3DMMv1.0: *************************************************************************
    Return whether the given sound is playing.
***************************************************************************/
bool SNDM::FPlaying(int32_t sii)
{
    AssertThis(0);
    SNDMPE sndmpe;
    int32_t isndmpe;

    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        if (sndmpe.psndv->FPlaying(sii))
            return fTrue;
    }

    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Return whether any sounds of the given queue and class are playing
    (one or both of (sqn, scl) may be nil).
***************************************************************************/
bool SNDM::FPlayingAll(int32_t sqn, int32_t scl)
{
    AssertThis(0);
    SNDMPE sndmpe;
    int32_t isndmpe;

    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        if (sndmpe.psndv->FPlayingAll(sqn, scl))
            return fTrue;
    }

    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Free anything that's no longer in use.
***************************************************************************/
void SNDM::Flush(void)
{
    AssertThis(0);
    SNDMPE sndmpe;
    int32_t isndmpe;

    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        sndmpe.psndv->Flush();
    }
}

/** 3DMMv1.0: *************************************************************************
    Start a synchronized group.
***************************************************************************/
void SNDM::BeginSynch(void)
{
    AssertThis(0);
    SNDMPE sndmpe;
    int32_t isndmpe;

    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        sndmpe.psndv->BeginSynch();
    }
}

/** 3DMMv1.0: *************************************************************************
    End a synchronized group.
***************************************************************************/
void SNDM::EndSynch(void)
{
    AssertThis(0);
    SNDMPE sndmpe;
    int32_t isndmpe;

    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        sndmpe.psndv->EndSynch();
    }
}

/** 3DMMv1.0: *************************************************************************
    A convenient base class for a multiple queue sound device.
***************************************************************************/

/** 3DMMv1.0: *************************************************************************
    Destructor for a multiple queue sound device.
***************************************************************************/
SNDMQ::~SNDMQ(void)
{
    AssertBaseThis(0);
    int32_t isnqd;
    SNQD snqd;

    if (pvNil != _pglsnqd)
    {
        for (isnqd = 0; isnqd < _pglsnqd->IvMac(); isnqd++)
        {
            _pglsnqd->Get(isnqd, &snqd);
            ReleasePpo(&snqd.psnque);
        }
        ReleasePpo(&_pglsnqd);
    }
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a SNDMQ.
***************************************************************************/
void SNDMQ::AssertValid(uint32_t grf)
{
    SNDMQ_PAR::AssertValid(0);
    AssertPo(_pglsnqd, 0);
}

/** 3DMMv1.0: *************************************************************************
    Mark memory for the SNDMQ.
***************************************************************************/
void SNDMQ::MarkMem(void)
{
    AssertValid(0);
    int32_t isnqd;
    SNQD snqd;

    SNDMQ_PAR::MarkMem();
    MarkMemObj(_pglsnqd);
    for (isnqd = 0; isnqd < _pglsnqd->IvMac(); isnqd++)
    {
        _pglsnqd->Get(isnqd, &snqd);
        MarkMemObj(snqd.psnque);
    }
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Initialize the multiple queue device.
***************************************************************************/
bool SNDMQ::_FInit(void)
{
    AssertBaseThis(0);

    if (pvNil == (_pglsnqd = GL::PglNew(SIZEOF(SNQD))))
        return fFalse;

    _fActive = fTrue;
    AssertThis(0);

    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Ensure a queue exists for the given sqn and return it.
***************************************************************************/
bool SNDMQ::_FEnsureQueue(int32_t sqn, SNQD *psnqd, int32_t *pisnqd)
{
    AssertThis(0);
    AssertNilOrVarMem(psnqd);
    AssertNilOrVarMem(pisnqd);

    int32_t isnqd, isnqdEmpty;
    SNQD snqd;

    isnqdEmpty = ivNil;
    for (isnqd = _pglsnqd->IvMac(); isnqd-- > 0;)
    {
        _pglsnqd->Get(isnqd, &snqd);
        if (snqd.sqn == sqn && sqn != ksqnNone)
        {
            // 3DMMv1.0: we found it
            goto LFound;
        }

        if (ivNil == isnqdEmpty && !snqd.psnque->FPlayingAll())
        {
            // 3DMMv1.0: here's an empty queue
            isnqdEmpty = isnqd;
            if (sqn == ksqnNone)
                break;
        }
    }

    if (ivNil != (isnqd = isnqdEmpty))
    {
        _pglsnqd->Get(isnqd, &snqd);
        snqd.sqn = sqn;
        _pglsnqd->Put(isnqd, &snqd);
        goto LFound;
    }

    snqd.sqn = sqn;
    if (pvNil == (snqd.psnque = _PsnqueNew()) || !_pglsnqd->FAdd(&snqd, &isnqd))
    {
        ReleasePpo(&snqd.psnque);
        TrashVar(psnqd);
        TrashVar(pisnqd);
        return fFalse;
    }

LFound:
    if (pvNil != psnqd)
        *psnqd = snqd;
    if (pvNil != pisnqd)
        *pisnqd = isnqd;
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Return whether the device is active.
***************************************************************************/
bool SNDMQ::FActive(void)
{
    AssertThis(0);

    return _fActive;
}

/** 3DMMv1.0: *************************************************************************
    Activate or deactivate the device.
***************************************************************************/
void SNDMQ::Activate(bool fActive)
{
    AssertThis(0);

    if (FPure(fActive) == FPure(_fActive))
        return;

    _fActive = FPure(fActive);
    _Suspend(_cactSuspend > 0 || !_fActive);
}

/** 3DMMv1.0: *************************************************************************
    Suspend or resume the device.
***************************************************************************/
void SNDMQ::Suspend(bool fSuspend)
{
    AssertThis(0);

    if (fSuspend)
        _cactSuspend++;
    else
        _cactSuspend--;

    Assert(_cactSuspend >= (FPure(fSuspend) ? 1 : 0), "bad _cactSuspend");
    _Suspend(_cactSuspend > 0 || !_fActive);
}

/** 3DMMv1.0: *************************************************************************
    Play the given sound.
***************************************************************************/
int32_t SNDMQ::SiiPlay(PRCA prca, CTG ctg, CNO cno, int32_t sqn, int32_t vlm, int32_t cactPlay, uint32_t dtsStart,
                       int32_t spr, int32_t scl)
{
    AssertThis(0);
    AssertPo(prca, 0);
    int32_t isnqd;
    SNQD snqd;
    int32_t sii = _SiiAlloc();

    if (sqn == sqnNil)
        sqn = ksqnNone;

    if (_FEnsureQueue(sqn, &snqd, &isnqd))
    {
        snqd.psnque->Enqueue(sii, prca, ctg, cno, vlm, cactPlay, dtsStart, spr, scl);
    }
    else
        Warn("couldn't allocate queue");

    return sii;
}

/** 3DMMv1.0: *************************************************************************
    Stop the given sound instance.
***************************************************************************/
void SNDMQ::Stop(int32_t sii)
{
    AssertThis(0);
    int32_t isnqd;
    SNQD snqd;

    for (isnqd = 0; isnqd < _pglsnqd->IvMac(); isnqd++)
    {
        _pglsnqd->Get(isnqd, &snqd);
        snqd.psnque->Stop(sii);
    }
}

/** 3DMMv1.0: *************************************************************************
    Stop all sounds of the given queue and class (one or both may be nil).
***************************************************************************/
void SNDMQ::StopAll(int32_t sqn, int32_t scl)
{
    AssertThis(0);
    int32_t isnqd;
    SNQD snqd;

    for (isnqd = 0; isnqd < _pglsnqd->IvMac(); isnqd++)
    {
        _pglsnqd->Get(isnqd, &snqd);
        if (sqnNil == sqn || snqd.sqn == sqn)
            snqd.psnque->StopAll(scl);
    }
}

/** 3DMMv1.0: *************************************************************************
    Pause the given sound.
***************************************************************************/
void SNDMQ::Pause(int32_t sii)
{
    AssertThis(0);
    int32_t isnqd;
    SNQD snqd;

    for (isnqd = 0; isnqd < _pglsnqd->IvMac(); isnqd++)
    {
        _pglsnqd->Get(isnqd, &snqd);
        snqd.psnque->Pause(sii);
    }
}

/** 3DMMv1.0: *************************************************************************
    Pause all sounds of the given queue and class (one or both may be nil).
***************************************************************************/
void SNDMQ::PauseAll(int32_t sqn, int32_t scl)
{
    AssertThis(0);
    int32_t isnqd;
    SNQD snqd;

    for (isnqd = 0; isnqd < _pglsnqd->IvMac(); isnqd++)
    {
        _pglsnqd->Get(isnqd, &snqd);
        if (sqnNil == sqn || snqd.sqn == sqn)
            snqd.psnque->PauseAll(scl);
    }
}

/** 3DMMv1.0: *************************************************************************
    Resume the given sound.
***************************************************************************/
void SNDMQ::Resume(int32_t sii)
{
    AssertThis(0);
    int32_t isnqd;
    SNQD snqd;

    for (isnqd = 0; isnqd < _pglsnqd->IvMac(); isnqd++)
    {
        _pglsnqd->Get(isnqd, &snqd);
        snqd.psnque->Resume(sii);
    }
}

/** 3DMMv1.0: *************************************************************************
    Resume all sounds of the given queue and class (one or both may be nil).
***************************************************************************/
void SNDMQ::ResumeAll(int32_t sqn, int32_t scl)
{
    AssertThis(0);
    int32_t isnqd;
    SNQD snqd;

    for (isnqd = 0; isnqd < _pglsnqd->IvMac(); isnqd++)
    {
        _pglsnqd->Get(isnqd, &snqd);
        if (sqnNil == sqn || snqd.sqn == sqn)
            snqd.psnque->ResumeAll(scl);
    }
}

/** 3DMMv1.0: *************************************************************************
    Return whether the given sound is playing.
***************************************************************************/
bool SNDMQ::FPlaying(int32_t sii)
{
    AssertThis(0);
    int32_t isnqd;
    SNQD snqd;

    for (isnqd = 0; isnqd < _pglsnqd->IvMac(); isnqd++)
    {
        _pglsnqd->Get(isnqd, &snqd);
        if (snqd.psnque->FPlaying(sii))
            return fTrue;
    }

    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Return whether any sounds of the given queue and class are playing
    (one or both of (sqn, scl) may be nil).
***************************************************************************/
bool SNDMQ::FPlayingAll(int32_t sqn, int32_t scl)
{
    AssertThis(0);
    int32_t isnqd;
    SNQD snqd;

    for (isnqd = 0; isnqd < _pglsnqd->IvMac(); isnqd++)
    {
        _pglsnqd->Get(isnqd, &snqd);
        if ((sqnNil == sqn || snqd.sqn == sqn) && snqd.psnque->FPlayingAll(scl))
        {
            return fTrue;
        }
    }

    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Free anything that's no longer being used.
***************************************************************************/
void SNDMQ::Flush()
{
    AssertThis(0);
    int32_t isnqd;
    SNQD snqd;

    // 3DMMv1.0: Don't free the last channel
    for (isnqd = _pglsnqd->IvMac(); isnqd-- > 0;)
    {
        _pglsnqd->Get(isnqd, &snqd);
        snqd.psnque->Flush();
        if (!snqd.psnque->FPlayingAll() && _pglsnqd->IvMac() > 1)
        {
            ReleasePpo(&snqd.psnque);
            _pglsnqd->Delete(isnqd);
        }
    }
}

/** 3DMMv1.0: *************************************************************************
    Constructor for an sound queue.
***************************************************************************/
SNQUE::SNQUE(void)
{
}

/** 3DMMv1.0: *************************************************************************
    Destructor for an sound queue.
***************************************************************************/
SNQUE::~SNQUE(void)
{
    AssertBaseThis(0);

    _Enter();
    if (pvNil != _pglsndin)
    {
        int32_t isndin;
        SNDIN sndin;

        for (isndin = _pglsndin->IvMac(); isndin-- > 0;)
        {
            _pglsndin->Get(isndin, &sndin);
            ReleasePpo(&sndin.pbaco);
        }
        ReleasePpo(&_pglsndin);
    }
    _Leave();
}

#ifdef DEBUG
/** 3DMMv1.0: *************************************************************************
    Assert the validity of a SNQUE.
***************************************************************************/
void SNQUE::AssertValid(uint32_t grf)
{
    SNQUE_PAR::AssertValid(0);

    _Enter();
    AssertPo(_pglsndin, 0);
    AssertIn(_isndinCur, 0, _pglsndin->IvMac() + 1);
    _Leave();
}

/** 3DMMv1.0: *************************************************************************
    Mark memory for the SNQUE.
***************************************************************************/
void SNQUE::MarkMem(void)
{
    AssertValid(0);
    int32_t isndin;
    SNDIN sndin;

    SNQUE_PAR::MarkMem();

    _Enter();
    MarkMemObj(_pglsndin);
    for (isndin = 0; isndin < _pglsndin->IvMac(); isndin++)
    {
        _pglsndin->Get(isndin, &sndin);
        MarkMemObj(sndin.pbaco);
    }
    _Leave();
}
#endif // 3DMMv1.0: DEBUG

/** 3DMMv1.0: *************************************************************************
    Initialize the sound queue. Allocate the _pglsndin.
***************************************************************************/
bool SNQUE::_FInit(void)
{
    AssertBaseThis(0);

    if (pvNil == (_pglsndin = GL::PglNew(SIZEOF(SNDIN))))
        return fFalse;

    AssertThis(0);
    return fTrue;
}

/** 3DMMv1.0: *************************************************************************
    Enter any critical section that's necessary to ensure access to
    member variables.
***************************************************************************/
void SNQUE::_Enter(void)
{
    AssertBaseThis(0);
}

/** 3DMMv1.0: *************************************************************************
    Leave any critical section that's necessary to ensure access to
    member variables.
***************************************************************************/
void SNQUE::_Leave(void)
{
    AssertBaseThis(0);
}

/** 3DMMv1.0: *************************************************************************
    Free any sounds below _isndinCur.
***************************************************************************/
void SNQUE::_Flush(void)
{
    AssertThis(0);
    SNDIN sndin;

    _Enter();

    while (_isndinCur > 0)
    {
        _isndinCur--;
        _pglsndin->Get(_isndinCur, &sndin);
        _pglsndin->Delete(_isndinCur);
        ReleasePpo(&sndin.pbaco);
    }

    _Leave();
}

/** 3DMMv1.0: *************************************************************************
    Put the given sound on the queue.
***************************************************************************/
void SNQUE::Enqueue(int32_t sii, PRCA prca, CTG ctg, CNO cno, int32_t vlm, int32_t cactPlay, uint32_t dtsStart,
                    int32_t spr, int32_t scl)
{
    AssertThis(0);
    AssertPo(prca, 0);
    SNDIN sndin;
    int32_t isndin;

    if (pvNil == (sndin.pbaco = _PbacoFetch(prca, ctg, cno)))
        return;

    _Enter();
    _Flush();

    sndin.sii = sii;
    sndin.vlm = vlm;
    sndin.cactPlay = cactPlay;
    sndin.dtsStart = dtsStart;
    sndin.spr = spr;
    sndin.scl = scl;
    sndin.cactPause = 0;
    if (!_pglsndin->FAdd(&sndin, &isndin))
    {
        ReleasePpo(&sndin.pbaco);
        _Leave();
        return;
    }

    _Queue(isndin);

    _Leave();
}

/** 3DMMv1.0: *************************************************************************
    Return the priority of the frontmost sound in the queue.
***************************************************************************/
int32_t SNQUE::SprCur(void)
{
    AssertThis(0);
    SNDIN sndin;
    int32_t spr;

    _Enter();
    _Flush();

    if (_pglsndin->IvMac() >= _isndinCur)
        spr = klwMin;
    else
    {
        _pglsndin->Get(_isndinCur, &sndin);
        spr = sndin.spr;
    }

    _Leave();
    return spr;
}

/** 3DMMv1.0: *************************************************************************
    If the given sound is in our queue, nuke it.
***************************************************************************/
void SNQUE::Stop(int32_t sii)
{
    AssertThis(0);
    int32_t isndin;
    SNDIN sndin;

    _Enter();
    _Flush();

    for (isndin = _pglsndin->IvMac(); isndin-- > _isndinCur;)
    {
        _pglsndin->Get(isndin, &sndin);
        if (sndin.sii == sii)
        {
            if (0 <= sndin.cactPause)
            {
                sndin.cactPause = -1;
                _pglsndin->Put(isndin, &sndin);
                _Queue(isndin);
            }

            _Leave();
            return;
        }
    }

    _Leave();
}

/** 3DMMv1.0: *************************************************************************
    Nuke all sounds of the given sound class. sclNil means nuke all.
***************************************************************************/
void SNQUE::StopAll(int32_t scl)
{
    AssertThis(0);
    int32_t isndin;
    SNDIN sndin;
    int32_t isndinMin = klwMax;

    _Enter();
    _Flush();

    for (isndin = _pglsndin->IvMac(); isndin-- > _isndinCur;)
    {
        _pglsndin->Get(isndin, &sndin);
        if (0 <= sndin.cactPause && (sclNil == scl || sndin.scl == scl))
        {
            sndin.cactPause = -1;
            _pglsndin->Put(isndin, &sndin);
            isndinMin = isndin;
        }
    }

    if (isndinMin < klwMax)
        _Queue(isndinMin);

    _Leave();
}

/** 3DMMv1.0: *************************************************************************
    If the given sound is in our queue, pause it.
***************************************************************************/
void SNQUE::Pause(int32_t sii)
{
    AssertThis(0);
    int32_t isndin;
    SNDIN sndin;

    _Enter();
    _Flush();

    for (isndin = _pglsndin->IvMac(); isndin-- > _isndinCur;)
    {
        _pglsndin->Get(isndin, &sndin);
        if (sndin.sii == sii)
        {
            if (0 <= sndin.cactPause)
            {
                sndin.cactPause++;
                _pglsndin->Put(isndin, &sndin);
                if (1 == sndin.cactPause)
                    _PauseQueue(isndin);
            }

            _Leave();
            return;
        }
    }

    _Leave();
}

/** 3DMMv1.0: *************************************************************************
    Pause all sounds of the given sound class. sclNil means nuke all.
***************************************************************************/
void SNQUE::PauseAll(int32_t scl)
{
    AssertThis(0);
    int32_t isndin;
    SNDIN sndin;
    int32_t isndinMin = klwMax;

    _Enter();
    _Flush();

    for (isndin = _pglsndin->IvMac(); isndin-- > _isndinCur;)
    {
        _pglsndin->Get(isndin, &sndin);
        if (0 <= sndin.cactPause && (sclNil == scl || sndin.scl == scl))
        {
            if (0 == sndin.cactPause++)
                isndinMin = isndin;
            _pglsndin->Put(isndin, &sndin);
        }
    }

    if (isndinMin < klwMax)
        _PauseQueue(isndinMin);

    _Leave();
}

/** 3DMMv1.0: *************************************************************************
    If the given sound is in our queue, make sure it's not paused.
***************************************************************************/
void SNQUE::Resume(int32_t sii)
{
    AssertThis(0);
    int32_t isndin;
    SNDIN sndin;

    _Enter();
    _Flush();

    for (isndin = _pglsndin->IvMac(); isndin-- > _isndinCur;)
    {
        _pglsndin->Get(isndin, &sndin);
        if (sndin.sii == sii)
        {
            if (0 < sndin.cactPause)
            {
                sndin.cactPause--;
                _pglsndin->Put(isndin, &sndin);
                if (0 == sndin.cactPause)
                    _ResumeQueue(isndin);
            }

            _Leave();
            return;
        }
    }

    _Leave();
}

/** 3DMMv1.0: *************************************************************************
    Resume all sounds of the given sound class. sclNil means nuke all.
***************************************************************************/
void SNQUE::ResumeAll(int32_t scl)
{
    AssertThis(0);
    int32_t isndin;
    SNDIN sndin;
    int32_t isndinMin = klwMax;

    _Enter();
    _Flush();

    for (isndin = _pglsndin->IvMac(); isndin-- > _isndinCur;)
    {
        _pglsndin->Get(isndin, &sndin);
        if (0 < sndin.cactPause && (sclNil == scl || sndin.scl == scl))
        {
            if (0 == --sndin.cactPause)
                isndinMin = isndin;
            _pglsndin->Put(isndin, &sndin);
        }
    }

    if (isndinMin < klwMax)
        _ResumeQueue(isndinMin);

    _Leave();
}

/** 3DMMv1.0: *************************************************************************
    Return whether the given sound is in our queue.
***************************************************************************/
bool SNQUE::FPlaying(int32_t sii)
{
    AssertThis(0);
    int32_t isndin;
    SNDIN sndin;

    _Enter();
    _Flush();

    for (isndin = _pglsndin->IvMac(); isndin-- > _isndinCur;)
    {
        _pglsndin->Get(isndin, &sndin);
        if (sndin.sii == sii)
        {
            _Leave();
            return 0 <= sndin.cactPause;
        }
    }

    _Leave();

    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Return whether any sounds of the given sound class are in our queue.
    sclNil means any sounds at all.
***************************************************************************/
bool SNQUE::FPlayingAll(int32_t scl)
{
    AssertThis(0);
    int32_t isndin;
    SNDIN sndin;

    _Enter();
    _Flush();

    for (isndin = _pglsndin->IvMac(); isndin-- > _isndinCur;)
    {
        _pglsndin->Get(isndin, &sndin);
        if (0 <= sndin.cactPause && (sclNil == scl || sndin.scl == scl))
        {
            _Leave();
            return fTrue;
        }
    }

    _Leave();

    return fFalse;
}

/** 3DMMv1.0: *************************************************************************
    Free anything that's not being used. This should only be called from
    the main thread.
***************************************************************************/
void SNQUE::Flush(void)
{
    AssertThis(0);

    _Flush();
}

/** 3DMMv1.0: *************************************************************************
    Scale the given system volume by the given Kauai volume.
***************************************************************************/
uint32_t LuVolScale(uint32_t luVol, int32_t vlm)
{
    Assert(kvlmFull == 0x10000, "this code assumes kvlmFull is 0x10000");
    uint32_t luHigh, luLow;
    uint16_t suHigh, suLow;

    MulLu(SuLow(luVol), vlm, &luHigh, &luLow);
    suLow = (luHigh > 0) ? (uint16_t)(-1) : SuHigh(luLow);

    MulLu(SuHigh(luVol), vlm, &luHigh, &luLow);
    suHigh = (luHigh > 0) ? (uint16_t)(-1) : SuHigh(luLow);

    return LuHighLow(suHigh, suLow);
}
