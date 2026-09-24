/** 3DMMEx: *************************************************************************
    Author: Ben Stone
    Project: Kauai

    Miniaudio sound playback device

***************************************************************************/
#ifndef SNDMA_H
#define SNDMA_H

#include <miniaudio.h>

// 3DMMEx: Manages a single instance of the miniaudio engine
typedef class MiniaudioManager *PMiniaudioManager;
#define MiniaudioManager_PAR BASE
#define kclsMiniaudioManager KLCONST4('m', 'a', 'm', 'a')
class MiniaudioManager : public MiniaudioManager_PAR
{
    RTCLASS_DEC
    NOCOPY(MiniaudioManager)

  public:
    MiniaudioManager();
    virtual ~MiniaudioManager();

    // 3DMMEx: Get the Miniaudio engine
    ma_engine *Pengine();

    static PMiniaudioManager Pmanager();

  protected:
    ma_engine _engine;
    bool _fInit;

    MUTX _mutxInit;
};

// 3DMMEx: Audio stream
typedef class MiniaudioStream *PMiniaudioStream;
#define MiniaudioStream_PAR BASE
#define kclsMiniaudioStream KLCONST4('m', 'a', 's', 't')
class MiniaudioStream : public MiniaudioStream_PAR
{
    RTCLASS_DEC
    NOCOPY(MiniaudioStream)

  public:
    virtual ~MiniaudioStream();

    // 3DMMEx: Create a new audio stream
    // 3DMMEx: If format/cchannel are not set, the stream will match the audio playback device
    static PMiniaudioStream PastreamNew(PMiniaudioManager pmanager, ma_format format = ma_format_unknown,
                                        ma_uint32 cchannel = 0, ma_uint32 csample = 0);

    bool FPlay();
    bool FStop();

    // 3DMMEx: Return the number of frames queued in the ringbuffer
    int GetPendingFrames();

    // 3DMMEx: Write audio into the ring buffer
    // 3DMMEx: NOTE: the input data needs to be the same format as the ma_engine uses for playback
    bool FWriteAudio(const void *pvframe, int32_t cframe);

    // 3DMMEx: Volume control
    int32_t GetVlm();
    void SetVlm(int32_t vlm);

    ma_uint32 Cchannel();
    ma_format Format();
    ma_uint32 SampleRate();

  protected:
    MiniaudioStream();

  private:
    bool _fInit = fFalse;
    PMiniaudioManager _pmanager;
    ma_sound _sound;
    ma_pcm_rb _buffer;
    int32_t _vlm;

    bool FInit(PMiniaudioManager pmanager, ma_format format, ma_uint32 cchannel, ma_uint32 csample);
};

typedef class MiniaudioCachedSound *PMiniaudioCachedSound;

// 3DMMEx: Size of block read cache
#define kcbCache 4096

// 3DMMEx: Decoder callbacks to read data from a BLCK
struct BLCKReadContext
{
    BLCK *pblck;
    int32_t ib;
    int32_t cb;

    // 3DMMEx: Read cache
    int32_t ibCache;
    int32_t cbCache;
    uint8_t rgbCache[kcbCache];
};

struct MiniaudioSoundInstance
{
    PMiniaudioCachedSound pbaco; // 3DMMEx: the sound to play
    int32_t sii;                 // 3DMMEx: the sound instance id
    int32_t vlm;                 // 3DMMEx: volume to play at
    int32_t cactPlay;            // 3DMMEx: how many times to play
    uint32_t dtsStart;           // 3DMMEx: offset to start at
    int32_t spr;                 // 3DMMEx: sound priority
    int32_t scl;                 // 3DMMEx: sound class
    int32_t sqn;                 // 3DMMEx: sound queue number
    int32_t tsStart;             // 3DMMEx: time when sound started playing

    bool fLoaded; // 3DMMEx: sound was successfully loaded

    BLCKReadContext readctx;
    ma_decoder decoder;
    ma_sound sound;
};

typedef class MiniaudioDevice *PMiniaudioDevice;

#define MiniaudioDevice_PAR SNDV
#define kclsMiniaudioDevice KLCONST4('m', 'a', 'd', 'v')
class MiniaudioDevice : public MiniaudioDevice_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    MiniaudioDevice();

    // 3DMMEx: Initialise the sound device
    virtual bool _FInit();

    bool _fInitialised = fFalse;
    int32_t _cactSuspend = 0;
    int32_t _vlm = 0;
    MUTX _mutx;

    PMiniaudioManager _pmanager = pvNil;
    ma_sound_group _soundgroup;

    // 3DMMEx: TODO: Replace with a dynamic array?
    MiniaudioSoundInstance _rgsndin[32] = {0};
    int32_t _csndinCur = 0;

    // 3DMMEx: Find a sound instance by ID
    MiniaudioSoundInstance *PsndinFromSii(int32_t sii, int32_t *pisndin);

    void Lock();
    void Unlock();

    // 3DMMEx: Returns True if there has been a sufficient delay since TsCurrentSystem() and tsStart.
    bool FDebounce(int32_t tsStart);

    bool FLoadSoundFromBlock(PBLCK pblck, BLCKReadContext *preadctx, ma_decoder *pdecoder, ma_sound *psound);

  public:
    // 3DMMEx: Create a new instance of the miniaudio sound device
    static PMiniaudioDevice PmadevNew();

    virtual ~MiniaudioDevice();

    virtual bool FActive(void) override;
    virtual void Activate(bool fActive) override;
    virtual void Suspend(bool fSuspend) override;
    virtual void SetVlm(int32_t vlm) override;
    virtual int32_t VlmCur(void) override;

    virtual int32_t SiiPlay(PRCA prca, CTG ctg, CNO cno, int32_t sqn = ksqnNone, int32_t vlm = kvlmFull,
                            int32_t cactPlay = 1, uint32_t dtsStart = 0, int32_t spr = 0,
                            int32_t scl = sclNil) override;

    virtual void Stop(int32_t sii) override;
    virtual void StopAll(int32_t sqn = sqnNil, int32_t scl = sclNil) override;

    virtual void Pause(int32_t sii) override;
    virtual void PauseAll(int32_t sqn = sqnNil, int32_t scl = sclNil) override;

    virtual void Resume(int32_t sii) override;
    virtual void ResumeAll(int32_t sqn = sqnNil, int32_t scl = sclNil) override;

    virtual bool FPlaying(int32_t sii) override;
    virtual bool FPlayingAll(int32_t sqn = sqnNil, int32_t scl = sclNil) override;

    virtual void Flush(void) override;
};

#endif // 3DMMEx: SNDMA_H
