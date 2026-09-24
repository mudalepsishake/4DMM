/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: ***************************************************************************\
 *
 *	stdioscb.h
 *
 *	Author: ******
 *	Date: March, 1995
 *
 *	This file contains the studio scrollbar class SSCB.
 *
\*****************************************************************************/

#ifndef STDIOSCB_H
#define STDIOSCB_H

//
// 3DMMv1.0:	The studio scrollbar class.
//

const int32_t kctsFps = 20;

#define SSCB_PAR BASE
typedef class SSCB *PSSCB;
#define kclsSSCB KLCONST4('S', 'S', 'C', 'B')
class SSCB : public SSCB_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  private:
    int32_t _nfrmFirstOld;
    bool _fNoAutoadjust;

    bool _fBtnAddsFrames;
    bool _fBtnInsertBefore;
    bool _fBtnInsertBlank;
    bool _fBtnBatchInsert;
    bool _fBtnNativeEnd;
    bool _fBtnNativeStartBlank;

    //
    // 3DMMv1.0:	Private methods
    //
    int32_t _CxScrollbar(int32_t kidScrollbar, int32_t kidThumb);

  protected:
    PTGOB _ptgobFrame;
    PTGOB _ptgobScene;

#ifdef SHOW_FPS
    // 3DMMv1.0: Frame descriptor
    struct FDSC
    {
        uint32_t ts;
        int32_t cfrm;
    };

    PTGOB _ptgobFps;
    FDSC _rgfdsc[kctsFps];
    int32_t _itsNext;
#endif // 3DMMv1.0: SHOW_FPS

    PMVIE _pmvie;
    SSCB(PMVIE pmvie);

  public:
    //
    // 3DMMv1.0:	Constructors and destructors
    //
    static PSSCB PsscbNew(PMVIE pmvie);
    ~SSCB(void);

    //
    // 3DMMv1.0:	Notification
    //
    virtual void Update(void);
    void SetMvie(PMVIE pmvie);
    void StartNoAutoadjust(void);
    void EndNoAutoadjust(void)
    {
        AssertThis(0);
        _fNoAutoadjust = fFalse;
    }
    void SetSndFrame(bool fSoundInFrame);

    //
    // 3DMMv1.0:	Event handling
    //
    bool FCmdScroll(PCMD pcmd);
};

#endif // 3DMMv1.0: STDIOSCB_H
