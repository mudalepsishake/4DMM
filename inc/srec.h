/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    srec.h: Sound Recording class

    Primary Author: ****** (based on ***** original srec)
    Review Status: reviewed

    BASE ---> SREC

***************************************************************************/
#ifndef SREC_H
#define SREC_H

#ifdef HAS_AUDIOMAN
#include "audioman.h"
#endif // 3DMMEx: HAS_AUDIOMAN

#ifdef KAUAI_SDL
#include <miniaudio.h>
#endif // 3DMMEx: KAUAI_SDL

/** 3DMMv1.0: **************************************
    RIFF Header helper class
****************************************/
#ifdef MAC
#define RIFF_TAG 'RIFF'
#define WAVE_TAG 'WAVE'
#define FMT__TAG 'fmt '
#define DATA_TAG 'data'
#define FACT_TAG 'fact'
#else
#define RIFF_TAG 'FFIR' // 3DMMv1.0: RIFF
#define WAVE_TAG 'EVAW' // 3DMMv1.0: WAVE
#define FMT__TAG ' tmf' // 3DMMv1.0: fmt_
#define DATA_TAG 'atad' // 3DMMv1.0: data
#define FACT_TAG 'tcaf' // 3DMMv1.0: fact
#endif

#ifdef KAUAI_WIN32

#pragma pack(push, _SOCPACK_)
#pragma pack(1)

class RIFF
{
  private:
    DWORD _dwRiffTag;
    DWORD _dwRiffLength;
    DWORD _dwWaveTag;
    DWORD _dwFmtTag;
    DWORD _dwFmtLength;
    WAVEFORMATEX _wfx;
    DWORD _dwDataTag;
    DWORD _dwDataLength;

  public:
    void Set(int32_t cchan, int32_t csampSec, int32_t cbSample, DWORD dwLength)
    {
        _dwRiffTag = RIFF_TAG;
        _dwRiffLength = sizeof(RIFF) + dwLength;
        _dwWaveTag = WAVE_TAG;
        _dwFmtTag = FMT__TAG;
        _dwFmtLength = sizeof(WAVEFORMATEX);
        _wfx.wFormatTag = WAVE_FORMAT_PCM;
        _wfx.nChannels = (uint16_t)cchan;
        _wfx.nSamplesPerSec = csampSec;
        _wfx.nAvgBytesPerSec = csampSec * cbSample * cchan;
        _wfx.nBlockAlign = (uint16_t)cchan * (uint16_t)cbSample;
        _wfx.wBitsPerSample = (uint16_t)LwMul(8, cbSample);
        _wfx.cbSize = 0;
        _dwDataTag = DATA_TAG;
        _dwDataLength = dwLength;
    }

    LPWAVEFORMATEX PwfxGet()
    {
        return &_wfx;
    };

    DWORD Cb()
    {
        return sizeof(RIFF) + _dwDataLength;
    };
};
#pragma pack(pop, _SOCPACK_)

#endif // 3DMMEx: KAUAI_WIN32

/** 3DMMv1.0: **************************************
    The sound recording class
****************************************/
typedef class SREC *PSREC;
#define SREC_PAR BASE
#define kclsSREC KLCONST4('S', 'R', 'E', 'C')
class SREC : public SREC_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    int32_t _csampSec; // 3DMMv1.0: sampling rate (number of samples per second)
    int32_t _cchan;    // 3DMMv1.0: 1 = mono, 2 = stereo
    int32_t _cbSample; // 3DMMv1.0: bytes per sample (1 = 8 bit, 2 = 16 bit, etc)
    uint32_t _dtsMax;  // 3DMMv1.0: maximum length to record
    bool _fRecording;
    bool _fPlaying;
    bool _fHaveSound;   // 3DMMv1.0: have you recorded a sound yet?
    bool _fBufferAdded; // 3DMMv1.0: have added record buffer

#ifdef KAUAI_WIN32
    HWAVEIN _hwavein; // 3DMMv1.0: handle to wavein device
    WAVEHDR _wavehdr; // 3DMMv1.0: wave hdr for buffer

#if defined(HAS_AUDIOMAN)
    LPMIXER _pmixer;     // 3DMMv1.0: pointer to Audioman Mixer
    LPCHANNEL _pchannel; // 3DMMv1.0: pointer to Audioman Channel
    LPSOUND _psnd;       // 3DMMv1.0: psnd for current sound
#endif                   // 3DMMEx: HAS_AUDIOMAN
    RIFF *_priff;        // 3DMMv1.0: pointer to riff in memory

    bool _FOpenRecord();
    bool _FCloseRecord();
    static void _WaveInProc(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2);
#endif // 3DMMEx: KAUAI_WIN32

#ifdef KAUAI_SDL

    ma_format _format;

    bool _fInitDevice = fFalse;
    ma_device _device;

    // 3DMMEx: This buffer holds recorded PCM audio frames
    HQ _hqBuffer = hqNil;
    int32_t _ibBuffer = 0;

    ma_uint32 _cFrame;    // 3DMMEx: Number of frames recorded
    ma_uint32 _cFrameMac; // 3DMMEx: Maximum number of frames to record

    bool _fInitPlaybackBuffer = fFalse;
    ma_audio_buffer _playbackBuffer;

    bool _fInitPlaybackSound = fFalse;
    ma_sound _sound;

    // 3DMMEx: Called when new audio frames are available
    static void OnDataProc(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount);

#endif // 3DMMEx: KAUAI_SDL

  protected:
    bool _FInit(int32_t csampSec, int32_t cchan, int32_t cbSample, uint32_t dtsMax);
    void _UpdateStatus(void);

  public:
    static PSREC PsrecNew(int32_t csampSec, int32_t cchan, int32_t cbSample, uint32_t dtsMax);
    ~SREC(void);

    bool FStart(void);
    bool FStop(void);
    bool FPlay(void);
    bool FRecording(void);
    bool FPlaying(void);
    bool FSave(PFNI pfni);
    bool FHaveSound(void)
    {
        return _fHaveSound;
    }
};

#endif // 3DMMEx: SREC_H
