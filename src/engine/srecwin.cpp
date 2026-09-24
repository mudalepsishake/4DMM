/* 3DMMEx: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMEx: *************************************************************************

    srec.cpp: Sound recording class

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
    _hwavein = pvNil;
    _priff = pvNil;
    _fBufferAdded = fFalse;
    _fRecording = fFalse;
    _fHaveSound = fFalse;

    vpsndm->Suspend(fTrue); // 3DMMEx: turn off sndm so we can get wavein device

    // 3DMMEx: See if sound recording is possible at all
    cwid = waveInGetNumDevs();
    if (0 == cwid)
    {
        PushErc(ercSocNoWaveIn);
        return fFalse;
    }

    // 3DMMEx: allocate a 10 second buffer
    _wavehdr.dwBufferLength = (cchan * csampSec * cbSample * dtsMax) / 1000;
    if (!FAllocPv((void **)&_priff, sizeof(RIFF) + _wavehdr.dwBufferLength, fmemClear, mprNormal))
        return fFalse;

    _wavehdr.lpData = reinterpret_cast<LPSTR>(PvAddBv(_priff, sizeof(RIFF)));

    // 3DMMEx: init RIFF structure
    _priff->Set(_cchan, _csampSec, _cbSample, 0);

    if (fFalse == _FOpenRecord())
    {
        return fFalse;
    }

    // 3DMMEx: get audioman
    _pmixer = GetAudioManMixer();
    if (pvNil == _pmixer)
        return fFalse;

    // 3DMMEx: get a channel
    _pmixer->AllocChannel(&_pchannel);
    if (pvNil == _pchannel)
        return fFalse;

    return fTrue;
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

    if (_hwavein)
        _FCloseRecord();

    ReleasePpo(&_pchannel);
    ReleasePpo(&_pmixer);
    FreePpv((void **)&_priff);
    vpsndm->Suspend(fFalse); // 3DMMEx: restore sound mgr
}

/** 3DMMEx: *************************************************************************
    Open Device for recording
***************************************************************************/
bool SREC::_FOpenRecord(void)
{
    AssertBaseThis(0);

    _fRecording = fFalse;

    if (pvNil == _hwavein)
    {
        // 3DMMEx: open a wavein device
        if (waveInOpen(&_hwavein, WAVE_MAPPER, _priff->PwfxGet(), (DWORD_PTR)_WaveInProc, (DWORD_PTR)this,
                       CALLBACK_FUNCTION))
        {
            // 3DMMEx: it doesn't support this format
            return fFalse;
        }

        // 3DMMEx: prepare header on block of data
        _wavehdr.dwUser = (DWORD_PTR)this;
        if (waveInPrepareHeader(_hwavein, &_wavehdr, sizeof(WAVEHDR)))
        {
            waveInClose(_hwavein);
            _hwavein = pvNil;
            return false;
        }
    }

    // 3DMMEx: add buffer to device
    if (!_fBufferAdded)
        if (waveInAddBuffer(_hwavein, &_wavehdr, sizeof(WAVEHDR)))
        {
            _FCloseRecord();
            _fRecording = fFalse;
            _hwavein = pvNil;
            return fFalse;
        }
        else
            _fBufferAdded = fTrue;

    return true;
}

/** 3DMMEx: *************************************************************************
    Close Device for recording
***************************************************************************/
bool SREC::_FCloseRecord(void)
{
    AssertThis(0);

    if (_hwavein)
    {
        // 3DMMEx: stop if necessary
        waveInReset(_hwavein);

        // 3DMMEx: unprepare header
        waveInUnprepareHeader(_hwavein, &_wavehdr, sizeof(WAVEHDR));
        _fRecording = fFalse;

        // 3DMMEx: close
        waveInClose(_hwavein);
        _hwavein = pvNil;
    }

    return fTrue;
}

/** 3DMMEx: *************************************************************************
    Figure out if we're recording or not
***************************************************************************/
void SREC::_UpdateStatus(void)
{
    AssertThis(0);

    // 3DMMEx: ------------------------------------
    // 3DMMEx: Check playing mode
    // 3DMMEx: ------------------------------------
    if ((_fPlaying) && !_pchannel->IsPlaying())
    {
        // 3DMMEx: then we just stopped
        Sleep(250L);            // 3DMMEx: sleep a little bit to cover AudioMan bug
        vpsndm->Suspend(fTrue); // 3DMMEx: suspend sound mgr
    }
    _fPlaying = _pchannel->IsPlaying();

    // 3DMMEx: ------------------------------------
    // 3DMMEx: 	Check Recording mode
    // 3DMMEx: If we are recording, AND our HaveSound flag
    // 3DMMEx: is set, then we must have just finished, so
    // 3DMMEx: process the data, and turn off the recording flag
    // 3DMMEx: ------------------------------------
    if ((_fRecording) && (_fHaveSound))
    {
        LPSOUND psnd = pvNil;     // 3DMMEx: original psnd
        LPSOUND psndBias = pvNil; // 3DMMEx: psnd Bias correction filter
        LPSOUND psndTrim = pvNil; // 3DMMEx: psnd Trim filter

        _fRecording = fFalse;
        if (_wavehdr.dwBytesRecorded == 0)
        {
            _fHaveSound = fFalse;
            return;
        }

        // 3DMMEx: using the Audioman APIs, apply the gain and Trim filter, and save it back out
        // 3DMMEx: to a different temp file.
        _wavehdr.dwBytesRecorded -=
            8 *
            (_cchan * _cbSample); // 3DMMEx: chop off last 8 samples worth, since some audio cards put garbage on end of data
        _priff->Set(_cchan, _csampSec, _cbSample, _wavehdr.dwBytesRecorded);

        // 3DMMEx: now use AudioMan API to load the temp file, apply a trim filter and place
        // 3DMMEx: trimmed sound out to our temp file
        if (FAILED(AllocSoundFromMemory(&psnd, (LPBYTE)_priff, _priff->Cb())))
        {
            PushErc(ercOomNew);
            _fHaveSound = fFalse;
            return;
        }
        _fHaveSound = fTrue;

        if (FAILED(AllocBiasFilter(&psndBias, psnd)))
        {
            // 3DMMEx: then just return the sound raw
            _psnd = psnd;
            return;
        }

        // 3DMMEx: release the original sound, since it's now owned by the psndGain
        ReleasePpo(&psnd);

        if (FAILED(AllocTrimFilter(&_psnd, psndBias)))
        {
            // 3DMMEx: then just return the sound with the bias filter on it...
            _psnd = psndBias;
            return;
        }
        // 3DMMEx: release the psndBias, since it's now owned by the psndTrim
        ReleasePpo(&psndBias);
    }
}

/** 3DMMEx: *************************************************************************
    Figure out if we're recording or not
***************************************************************************/
void SREC::_WaveInProc(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
    // 3DMMEx: the psrec pointer is a pointer to the class which generated the event and owns the device
    SREC *psrec = (SREC *)dwInstance;

    switch (uMsg)
    {
    case WIM_DATA: {
        // 3DMMEx: any time we get a block of data, we are done, we set our flag
        // 3DMMEx: to true, allowing _UpdateStatus to notice that we are _fRecording and _fHaveSound
        // 3DMMEx: at which point it will process the data...
        psrec->_fHaveSound = fTrue;
        psrec->_fBufferAdded = fFalse;
    }
    }
}

/** 3DMMEx: *************************************************************************
    Start recording
***************************************************************************/
bool SREC::FStart(void)
{
    AssertThis(0);
    Assert(!_fRecording, "stop previous recording first");

    // 3DMMEx: make sure we are open
    if (_fPlaying)
        FStop();

    if (!_FOpenRecord())
        return fFalse;

    _fHaveSound = fFalse;
    _fRecording = fFalse;
    _wavehdr.dwBytesRecorded = 0;

    // 3DMMEx: now record data
    if (waveInStart(_hwavein))
        return fFalse;

    _fRecording = fTrue;

    return fTrue;
}

/** 3DMMEx: *************************************************************************
    Stop recording or playing
***************************************************************************/
bool SREC::FStop(void)
{
    AssertThis(0);
    Assert(_fRecording || _fPlaying, "Nothing to stop");

    // 3DMMEx: if we are recording
    if (_fRecording)
    {
        // 3DMMEx: then stop the recording device
        waveInStop(_hwavein);
    }
    else if (_fPlaying) // 3DMMEx: if we are playing
    {
        // 3DMMEx: then stop the playing device
        _pchannel->Stop();
    }

    // 3DMMEx: update status accordingly
    _UpdateStatus();

    return fTrue;
}

/** 3DMMEx: *************************************************************************
    Start playing the current sound
***************************************************************************/
bool SREC::FPlay(void)
{
    AssertThis(0);
    Assert(_fHaveSound, "No sound to play");

    // 3DMMEx: open the _fniTrim file with MCI
    _FCloseRecord();

    if (_psnd && _pchannel)
    {
        vpsndm->StopAll();       // 3DMMEx: stop any outstanding bogus sounds from button pushs
        vpsndm->Suspend(fFalse); // 3DMMEx: restore sound mgr

        _pchannel->Stop();             // 3DMMEx: stop our channel (should be nop)
        _pchannel->SetSoundSrc(_psnd); // 3DMMEx: give it our sound
        _pchannel->SetPosition(0);     // 3DMMEx: seek to the beginning

        if (FAILED(_pchannel->Play())) // 3DMMEx: play the sound
        {
            _UpdateStatus(); // 3DMMEx: this will check play status, and clean up accordingly
        }
        else
            _fPlaying = fTrue;
    }

    return _fPlaying;
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

    STN stn;

    if (_psnd)
    {
        pfni->GetStnPath(&stn);

        // 3DMMEx: now save _psnd to the FNI passed in
        SZS szs;
        stn.GetSzs(szs);
        if (FAILED(SoundToFileAsWave(_psnd, szs)))
        {
            PushErc(ercSocWaveSaveFailure);
            return fFalse;
        }
        return fTrue;
    }
    return fFalse;
}

#ifdef DEBUG
/** 3DMMEx: *************************************************************************
    Assert the validity of the SREC.
***************************************************************************/
void SREC::AssertValid(uint32_t grf)
{
    SREC_PAR::AssertValid(fobjAllocated);
    Assert(pvNil != _pmixer, "No mixer?");
    Assert(pvNil != _pchannel, "No Channel?");
}

/** 3DMMEx: *************************************************************************
    Mark memory used by the SREC
***************************************************************************/
void SREC::MarkMem(void)
{
    AssertThis(0);
    MarkPv(_priff);
    SREC_PAR::MarkMem();
}
#endif // 3DMMEx: DEBUG
