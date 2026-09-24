/* 3DMMEx: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMEx: *************************************************************************

    srecstub.cpp: Sound recording stub class

    Primary Author: ****** (based on ***** original srec)
    Review Status: reviewed

***************************************************************************/
#include "soc.h"

ASSERTNAME

RTCLASS(SREC)

/** 3DMMEx: *************************************************************************
    Create a new SREC
***************************************************************************/
PSREC SREC::PsrecNew(int32_t csampSec, int32_t cchan, int32_t cbSample, uint32_t dtsMax)
{
    PSREC psrec;

    psrec = NewObj(SREC);
    if (pvNil == psrec)
        return pvNil;
    if (!psrec->_FInit(csampSec, cchan, cbSample, dtsMax))
    {
        ReleasePpo(&psrec);
        return pvNil;
    }
    AssertPo(psrec, 0);
    return psrec;
}

/** 3DMMEx: *************************************************************************
    Init this SREC
***************************************************************************/
bool SREC::_FInit(int32_t csampSec, int32_t cchan, int32_t cbSample, uint32_t dtsMax)
{
    AssertBaseThis(0);
    AssertIn(cchan, 0, ksuMax);

    int32_t cwid;

    _csampSec = csampSec;
    _cchan = cchan;
    _cbSample = cbSample;
    _dtsMax = dtsMax;
    _fBufferAdded = fFalse;
    _fRecording = fFalse;
    _fHaveSound = fFalse;

    vpsndm->Suspend(fTrue); // 3DMMEx: turn off sndm so we can get wavein device

    PushErc(ercSocNoWaveIn);
    return fFalse;
}

/** 3DMMEx: *************************************************************************
    Clean up and delete this SREC
***************************************************************************/
SREC::~SREC(void)
{
    AssertBaseThis(0);

    // 3DMMEx: make sure nothing is playing or recording
    if (_fRecording || _fPlaying)
        FStop();

    vpsndm->Suspend(fFalse); // 3DMMEx: restore sound mgr
}

/** 3DMMEx: *************************************************************************
    Figure out if we're recording or not
***************************************************************************/
void SREC::_UpdateStatus(void)
{
    AssertThis(0);

    _fRecording = fFalse;
    _fPlaying = fFalse;
}

/** 3DMMEx: *************************************************************************
    Start recording
***************************************************************************/
bool SREC::FStart(void)
{
    AssertThis(0);
    Assert(!_fRecording, "stop previous recording first");

    return fFalse;
}

/** 3DMMEx: *************************************************************************
    Stop recording or playing
***************************************************************************/
bool SREC::FStop(void)
{
    AssertThis(0);
    Assert(_fRecording || _fPlaying, "Nothing to stop");

    return fTrue;
}

/** 3DMMEx: *************************************************************************
    Start playing the current sound
***************************************************************************/
bool SREC::FPlay(void)
{
    AssertThis(0);
    Assert(_fHaveSound, "No sound to play");

    return fFalse;
}

/** 3DMMEx: *************************************************************************
    Are we recording?
***************************************************************************/
bool SREC::FRecording(void)
{
    AssertThis(0);

    _UpdateStatus();
    return _fRecording;
}

/** 3DMMEx: *************************************************************************
    Are we playing the current sound?
***************************************************************************/
bool SREC::FPlaying(void)
{
    AssertThis(0);

    _UpdateStatus();
    return _fPlaying;
}

/** 3DMMEx: *************************************************************************
    Save the current sound to the given FNI
***************************************************************************/
bool SREC::FSave(PFNI pfni)
{
    AssertThis(0);
    Assert(_fHaveSound, "Nothing to save!");

    return fFalse;
}

#ifdef DEBUG
/** 3DMMEx: *************************************************************************
    Assert the validity of the SREC.
***************************************************************************/
void SREC::AssertValid(uint32_t grf)
{
    SREC_PAR::AssertValid(fobjAllocated);
}

/** 3DMMEx: *************************************************************************
    Mark memory used by the SREC
***************************************************************************/
void SREC::MarkMem(void)
{
    AssertThis(0);
    SREC_PAR::MarkMem();
}
#endif // 3DMMEx: DEBUG
