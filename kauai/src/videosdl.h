/* 3DMMEx: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMEx: *************************************************************************
    Author: Mark Cave-Ayland
    Project: Kauai
    Copyright (c) Microsoft Corporation

    Video playback for SDL.

***************************************************************************/
#ifndef VIDEO_SDL_H
#define VIDEO_SDL_H

#include <atomic>
#include <thread>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include <glib.h>
#include <SDL2/SDL.h>

#include "gfx.h"
#include "sndma.h"
#include "video.h"

// 3DMMEx: New sample callback data
typedef struct NSCB
{
    GstElement *pipeline = NULL;
    Signal hevt;
    GstSample *vsample;
    GstSample *asample;
    std::atomic<bool> fFrameReady = false;
} NSCB;

/** 3DMMEx: **************************************
    GStreamer video playback for SDL.
****************************************/
typedef class GVGS *PGVGS;
#define GVGS_PAR GVID
#define kclsGVGS KLCONST4('G', 'V', 'G', 'S')
class GVGS : public GVGS_PAR
{
    RTCLASS_DEC
    CMD_MAP_DEC(GVGS)
    ASSERT
    MARKMEM

  protected:
    int32_t _dxp;
    int32_t _dyp;
    RC _rc;
    RC _rcPlay;
    int32_t _nfrMac;
    PGOB _pgobBase;
    int32_t _cactPal;

    std::atomic<bool> _fPlaying;
    std::atomic<bool> _fVisible;
    std::atomic<bool> _fDone;

    MUTX _mutx;

    std::thread _hth;
    PGNV _pgnv;
    SDL_Surface *_surface;

    NSCB _nscb;

    PMiniaudioStream _pastream;
    uint32_t _LuThread(void);

    GVGS(int32_t hid);
    ~GVGS(void);

    // 3DMMEx: Initialize the GVGS.
    virtual bool _FInit(PFNI pfni, PGOB pgobBase);

    // 3DMMEx: Position the hwnd associated with the video to match the GOB's position.
    virtual void _SetRc(void);

  public:
    // 3DMMEx: Create a new video window.
    static PGVGS PgvgsNew(PFNI pfni, PGOB pgobBase, int32_t hid = hidNil);

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

#endif //! 3DMMEx: VIDEO_SDL_H
