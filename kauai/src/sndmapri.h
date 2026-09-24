/** 3DMMEx: *************************************************************************
    Author: Ben Stone
    Project: Kauai

    Miniaudio sound device implementation

***************************************************************************/
#ifndef SNDMAPRI_H
#define SNDMAPRI_H

#define AssertMaSuccess(var, msg) AssertVar(var == MA_SUCCESS, msg, &var)

// 3DMMEx: Cached object containing raw sound data
typedef class MiniaudioCachedSound *PMiniaudioCachedSound;
#define MiniaudioCachedSound_PAR BACO
#define kclsMiniaudioCachedSound KLCONST4('m', 'a', 's', 'n')
class MiniaudioCachedSound : public MiniaudioCachedSound_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(MiniaudioCachedSound)
  public:
    int32_t CbMem(void);

    // 3DMMEx: Load sound data from a chunky resource file
    static bool FReadMiniaudioCachedSound(PCRF pcrf, CTG ctg, CNO cno, PBLCK pblck, PBACO *ppbaco, int32_t *pcb);

    static PMiniaudioCachedSound PMiniaudioCachedSoundNew(PFLO pflo, bool fPacked);

    // 3DMMEx: Return the block containing raw sound data
    PBLCK Pblck();

  protected:
    MiniaudioCachedSound();
    virtual ~MiniaudioCachedSound();

    // 3DMMEx: Block containing the sound data
    BLCK _blckData;
};

// 3DMMEx: Scale volume to a float from 0.0 to 2.0
float ScaleVlm(int32_t vlm);

#endif // 3DMMEx: SNDMAPRI_H