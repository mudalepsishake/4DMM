/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    Video playback.

***************************************************************************/
#ifndef VIDEO_H
#define VIDEO_H

/** 3DMMv1.0: *************************************************************************
    Generic video class. This is an interface that supports the GVDS
    (video stream) and GVDW (video window) classes.
***************************************************************************/
typedef class GVID *PGVID;
#define GVID_PAR CMH
#define kclsGVID KLCONST4('G', 'V', 'I', 'D')
class GVID : public GVID_PAR
{
    RTCLASS_DEC

  protected:
    GVID(int32_t hid);
    ~GVID(void)
    {
    }

  public:
    /** 3DMMEx: *************************************************************************
     Create a video - either an HWND based one, or just a video stream,
     depending on fHwndBased. pgobBase is assumed to be valid for the life
     of the video.
     ***************************************************************************/
    static PGVID PgvidNew(PFNI pfni, PGOB pgobBase, bool fHwndBased = fFalse, int32_t hid = hidNil);

    // 3DMMEx: Return the number of frames in the video.
    virtual int32_t NfrMac(void) = 0;

    // 3DMMEx: Return the current frame of the video.
    virtual int32_t NfrCur(void) = 0;

    /** 3DMMEx: *************************************************************************
     Advance to a particular frame.  If we are playing, stop playing.  This
     only changes internal state and doesn't mark anything.
    ***************************************************************************/
    virtual void GotoNfr(int32_t nfr) = 0;

    // 3DMMEx: Return whether or not the video is playing.
    virtual bool FPlaying(void) = 0;

    /** 3DMMEx: *************************************************************************
     Start playing at the current frame.  This assumes the gob is valid
     until the video is stopped or nuked.  The gob should call this video's
     Draw method in its Draw method.
     ***************************************************************************/
    virtual bool FPlay(RC *prc = pvNil) = 0;

    // 3DMMEx: Stop playing.
    virtual void Stop(void) = 0;

    // 3DMMEx: Call this to draw the current state of the video image.
    virtual void Draw(PGNV pgnv, RC *prc) = 0;

    // 3DMMEx: Get the normal rectangle for the movie (top-left at (0, 0)).
    virtual void GetRc(RC *prc) = 0;

    // 3DMMEx: Set the rectangle to play into.
    virtual void SetRcPlay(RC *prc) = 0;
};

/** 3DMMv1.0: **************************************
    Video stream class.
****************************************/
typedef class GVDS *PGVDS;
#define GVDS_PAR GVID
#define kclsGVDS KLCONST4('G', 'V', 'D', 'S')
class GVDS : public GVDS_PAR
{
    RTCLASS_DEC
    CMD_MAP_DEC(GVDS)
    ASSERT

  protected:
    int32_t _nfrMac;
    int32_t _nfrCur;
    int32_t _nfrMarked;
    int32_t _dxp;
    int32_t _dyp;

    PGOB _pgobBase;
    RC _rcPlay;
    uint32_t _tsPlay;

    bool _fPlaying;

#ifdef KAUAI_WIN32
    int32_t _dnfr;
    PAVIFILE _pavif;
    PAVISTREAM _pavis;
    PGETFRAME _pavig;
    HDRAWDIB _hdd;
#endif // 3DMMEx: KAUAI_WIN32

    GVDS(int32_t hid);
    ~GVDS(void);

    // 3DMMEx: Initialize a video stream object.
    virtual bool _FInit(PFNI pfni, PGOB pgobBase);

  public:
    // 3DMMEx: Create a new video stream object.
    static PGVDS PgvdsNew(PFNI pfni, PGOB pgobBase, int32_t hid = hidNil);

    // 3DMMEx: Return the number of frames in the video.
    virtual int32_t NfrMac(void) override;

    // 3DMMEx: Return the current frame of the video.
    virtual int32_t NfrCur(void) override;

    /** 3DMMEx: *************************************************************************
     Advance to a particular frame.  If we are playing, stop playing.  This
     only changes internal state and doesn't mark anything.
     ***************************************************************************/
    virtual void GotoNfr(int32_t nfr) override;

    // 3DMMEx: Return whether or not the video is playing.
    virtual bool FPlaying(void) override;

    /** 3DMMEx: *************************************************************************
     Start playing at the current frame.  This assumes the gob is valid
     until the video is stopped or nuked.  The gob should call this video's
     Draw method in its Draw method.
     ***************************************************************************/
    virtual bool FPlay(RC *prc = pvNil) override;

    // 3DMMEx: Stop playing.
    virtual void Stop(void) override;

    // 3DMMEx: Call this to draw the current state of the video image.
    virtual void Draw(PGNV pgnv, RC *prc) override;

    // 3DMMEx: Get the normal rectangle for the movie (top-left at (0, 0)).
    virtual void GetRc(RC *prc) override;

    // 3DMMEx: Set the rectangle to play into.
    virtual void SetRcPlay(RC *prc) override;

    // 3DMMEx: Intercepts all commands, so we get to play our movie no matter what.
    virtual bool FCmdAll(PCMD pcmd);
};

/** 3DMMv1.0: **************************************
    Video in a window class.
****************************************/
typedef class GVDW *PGVDW;
#define GVDW_PAR GVID
#define kclsGVDW KLCONST4('G', 'V', 'D', 'W')
class GVDW : public GVDW_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    KWND _hwndMovie;
    int32_t _lwDevice;
    int32_t _dxp;
    int32_t _dyp;
    RC _rc;
    RC _rcPlay;
    int32_t _nfrMac;
    PGOB _pgobBase;
    int32_t _cactPal;

    bool _fDeviceOpen : 1;
    bool _fPlaying : 1;
    bool _fVisible : 1;

    GVDW(int32_t hid);
    ~GVDW(void);

    // 3DMMEx: Initialize the GVDW.
    virtual bool _FInit(PFNI pfni, PGOB pgobBase);

    // 3DMMEx: Position the hwnd associated with the video to match the GOB's position.
    virtual void _SetRc(void);

  public:
    // 3DMMEx: Create a new video window.
    static PGVDW PgvdwNew(PFNI pfni, PGOB pgobBase, int32_t hid = hidNil);

    // 3DMMEx: Return the number of frames in the video.
    virtual int32_t NfrMac(void) override;

    // 3DMMEx: Return the current frame of the video.
    virtual int32_t NfrCur(void) override;

    /** 3DMMEx: *************************************************************************
     Advance to a particular frame.  If we are playing, stop playing.  This
     only changes internal state and doesn't mark anything.
     ***************************************************************************/
    virtual void GotoNfr(int32_t nfr) override;

    // 3DMMEx: Return whether or not the video is playing.
    virtual bool FPlaying(void) override;

    /** 3DMMEx: *************************************************************************
     Start playing at the current frame.  This assumes the gob is valid
     until the video is stopped or nuked.  The gob should call this video's
     Draw method in its Draw method.
     ***************************************************************************/
    virtual bool FPlay(RC *prc = pvNil) override;

    // 3DMMEx: Stop playing.
    virtual void Stop(void) override;

    // 3DMMEx: Call this to draw the current state of the video image.
    virtual void Draw(PGNV pgnv, RC *prc) override;

    // 3DMMEx: Get the normal rectangle for the movie (top-left at (0, 0)).
    virtual void GetRc(RC *prc) override;

    // 3DMMEx: Set the rectangle to play into.
    virtual void SetRcPlay(RC *prc) override;
};

#endif //! 3DMMv1.0: VIDEO_H
