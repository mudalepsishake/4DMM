/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Command execution.  Manages the command filter list and dispatching
    commands to command handlers.

***************************************************************************/
#ifndef CMD_H
#define CMD_H

/** 3DMMv1.0: *************************************************************************
    Command id, options and command struct
***************************************************************************/

// 3DMMv1.0: command handler forward declaration
class CMH;
typedef CMH *PCMH;

// 3DMMv1.0: command enable-disable status flags
enum
{
    fedsNil = 0,
    fedsDisable = 1,
    fedsEnable = 2,
    fedsUncheck = 4,
    fedsCheck = 8,
    fedsBullet = 16
};
const uint32_t kgrfedsMark = fedsUncheck | fedsCheck | fedsBullet;

// 3DMMv1.0: command
#define kclwCmd 4 // 3DMMv1.0: if this ever changes, change the CMD_TYPE macro also
struct CMD
{
    ASSERT

    PCMH pcmh;             // 3DMMv1.0: the target of the command - may be nil
    int32_t cid;           // 3DMMv1.0: the command id
    PGG pgg;               // 3DMMv1.0: additional parameters for the command
    int32_t rglw[kclwCmd]; // 3DMMv1.0: standard parameters
};
typedef CMD *PCMD;

// 3DMMv1.0: command on file - for saving recorded macros
struct CMDF
{
    int32_t cid;
    int32_t hid;
    int32_t cact;
    CHID chidGg; // 3DMMv1.0: child id of the pgg, 0 if none
    int32_t rglw[kclwCmd];
};

/** 3DMMv1.0: *************************************************************************
    Custom command types
***************************************************************************/
// 3DMMv1.0: used to define a new CMD structure.  Needs a trailing semicolon.
#define CMD_TYPE(foo, a, b, c, d)                                                                                      \
    struct CMD_##foo                                                                                                   \
    {                                                                                                                  \
        PCMH pcmh;                                                                                                     \
        int32_t cid;                                                                                                   \
        PGG pgg;                                                                                                       \
        int32_t a, b, c, d;                                                                                            \
    };                                                                                                                 \
    typedef CMD_##foo *PCMD_##foo

CMD_TYPE(KEY, ch, vk, grfcust, cact);   // 3DMMv1.0: defines CMD_KEY and PCMD_KEY
CMD_TYPE(BADKEY, ch, vk, grfcust, hid); // 3DMMv1.0: defines CMD_BADKEY and PCMD_BADKEY
CMD_TYPE(MOUSE, xp, yp, grfcust, cact); // 3DMMv1.0: defines CMD_MOUSE and PCMD_MOUSE

/** 3DMMv1.0: *************************************************************************
    Command Map stuff.  To attach a command map to a subclass of CMH,
    put a CMD_MAP_DEC(cls) in the definition of the class.  Then in the
    .cpp file, use BEGIN_CMD_MAP, ON_CID and END_CMD_MAP to define the
    command map.  This architecture was borrowed from MFC.
***************************************************************************/
enum
{
    fcmmNil = 0,
    fcmmThis = 1,
    fcmmNobody = 2,
    fcmmOthers = 4,
};
const uint32_t kgrfcmmAll = fcmmThis | fcmmNobody | fcmmOthers;

// 3DMMv1.0: for including a command map in this class
#define CMD_MAP_DEC(cls)                                                                                               \
  private:                                                                                                             \
    static CMME _rgcmme##cls[];                                                                                        \
                                                                                                                       \
  protected:                                                                                                           \
    static CMM _cmm##cls;                                                                                              \
    virtual CMM *Pcmm(void) override                                                                                   \
    {                                                                                                                  \
        return &_cmm##cls;                                                                                             \
    }

#define CMD_MAP_DEC_BASE(cls)                                                                                          \
  private:                                                                                                             \
    static CMME _rgcmme##cls[];                                                                                        \
                                                                                                                       \
  protected:                                                                                                           \
    static CMM _cmm##cls;                                                                                              \
    virtual CMM *Pcmm(void)                                                                                            \
    {                                                                                                                  \
        return &_cmm##cls;                                                                                             \
    }

// 3DMMv1.0: for defining the command map in a .cpp file
#define BEGIN_CMD_MAP_BASE(cls)                                                                                        \
    cls::CMM cls::_cmm##cls = {pvNil, cls::_rgcmme##cls};                                                              \
    cls::CMME cls::_rgcmme##cls[] = {
#define BEGIN_CMD_MAP(cls, clsBase)                                                                                    \
    cls::CMM cls::_cmm##cls = {&(clsBase::_cmm##clsBase), cls::_rgcmme##cls};                                          \
    cls::CMME cls::_rgcmme##cls[] = {

#define ON_CID(cid, pfncmd, pfneds, grfcmm) {cid, (PFNCMD)pfncmd, (PFNEDS)pfneds, grfcmm},
#define ON_CID_ME(cid, pfncmd, pfneds) {cid, (PFNCMD)pfncmd, (PFNEDS)pfneds, fcmmThis},
#define ON_CID_GEN(cid, pfncmd, pfneds) {cid, (PFNCMD)pfncmd, (PFNEDS)pfneds, fcmmThis | fcmmNobody},
#define ON_CID_ALL(cid, pfncmd, pfneds) {cid, (PFNCMD)pfncmd, (PFNEDS)pfneds, kgrfcmmAll},

#define END_CMD_MAP(pfncmdDef, pfnedsDef, grfcmm)                                                                      \
    {                                                                                                                  \
        cidNil, (PFNCMD)pfncmdDef, (PFNEDS)pfnedsDef, grfcmm                                                           \
    }                                                                                                                  \
    }                                                                                                                  \
    ;
#define END_CMD_MAP_NIL()                                                                                              \
    {                                                                                                                  \
        cidNil, pvNil, pvNil, fcmmNil                                                                                  \
    }                                                                                                                  \
    }                                                                                                                  \
    ;

/** 3DMMv1.0: *************************************************************************
    Command handler class
***************************************************************************/
#define CMH_PAR BASE
#define kclsCMH KLCONST3('C', 'M', 'H')
class CMH : public CMH_PAR
{
    RTCLASS_DEC
    ASSERT

  private:
    static int32_t _hidLast; // 3DMMv1.0: for HidUnique
    int32_t _hid;            // 3DMMv1.0: handler id

  protected:
    // 3DMMv1.0: command function
    typedef bool (CMH::*PFNCMD)(PCMD pcmd);

    // 3DMMv1.0: command enabler function
    typedef bool (CMH::*PFNEDS)(PCMD pcmd, uint32_t *pgrfeds);

    // 3DMMv1.0: command map entry
    struct CMME
    {
        int32_t cid;
        PFNCMD pfncmd;
        PFNEDS pfneds;
        uint32_t grfcmm;
    };

    // 3DMMv1.0: command map
    struct CMM
    {
        CMM *pcmmBase;
        CMME *prgcmme;
    };

    CMD_MAP_DEC_BASE(CMH)

  protected:
    virtual bool _FGetCmme(int32_t cid, uint32_t grfcmmWanted, CMME *pcmme);

  public:
    CMH(int32_t hid);
    ~CMH(void);

    // 3DMMv1.0: return indicates whether the command was handled, not success
    virtual bool FDoCmd(PCMD pcmd);
    virtual bool FEnableCmd(PCMD pcmd, uint32_t *pgrfeds);

    int32_t Hid(void)
    {
        return _hid;
    }

    static int32_t HidUnique(int32_t ccmh = 1);
};

/** 3DMMv1.0: *************************************************************************
    Command execution manager (dispatcher)
***************************************************************************/
// 3DMMv1.0: command stream recording error codes.
enum
{
    recNil,
    recFileError,
    recMemError,
    recWrongPlatform,
    recAbort,
    recLim
};

typedef class CEX *PCEX;
#define CEX_PAR BASE
#define kclsCEX KLCONST3('C', 'E', 'X')
class CEX : public CEX_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(CEX)

  protected:
    // 3DMMv1.0: an entry in the command handler list
    struct CMHE
    {
        PCMH pcmh;
        int32_t cmhl;
        uint32_t grfcmm;
    };

    // 3DMMv1.0: command recording/playback state
    enum
    {
        rsNormal,
        rsRecording,
        rsPlaying,
        rsLim
    };

    // 3DMMv1.0: recording and playback
    int32_t _rs;    // 3DMMv1.0: recording/playback state
    int32_t _rec;   // 3DMMv1.0: recording/playback errors
    PCFL _pcfl;     // 3DMMv1.0: the file we are recording to or playing from
    PGL _pglcmdf;   // 3DMMv1.0: the command stream
    CNO _cno;       // 3DMMv1.0: which macro is being played
    int32_t _icmdf; // 3DMMv1.0: current command for recording or playback
    CHID _chidLast; // 3DMMv1.0: last chid used for recording
    int32_t _cact;  // 3DMMv1.0: number of times on this command
    CMD _cmd;       // 3DMMv1.0: previous command recorded or played

    // 3DMMv1.0: dispatching
    CMD _cmdCur;        // 3DMMv1.0: command being dispatched
    int32_t _icmheNext; // 3DMMv1.0: next command handler to dispatch to
    PGOB _pgobTrack;    // 3DMMv1.0: the gob that is tracking the mouse
#ifdef KAUAI_WIN32
    HWND _hwndCapture;    // 3DMMv1.0: the hwnd that we captured the mouse with
#endif                    // 3DMMEx: KAUAI_WIN32
    CMD _cmdLastTrack;    // 3DMMEx: Last cidTrackMouse command
    int32_t _tsLastTrack; // 3DMMEx: Time since last cidTrackMouse command

#ifdef KAUAI_SDL
    bool _fTrackingMouse;
#endif // 3DMMEx: KAUAI_SDL

    // 3DMMv1.0: filter list and command queue
    PGL _pglcmhe;       // 3DMMv1.0: the command filter list
    PGL _pglcmd;        // 3DMMv1.0: the command queue
    bool _fDispatching; // 3DMMv1.0: whether we're currently in FDispatchNextCmd

    // 3DMMv1.0: Modal filtering
    PGOB _pgobModal;

#ifdef DEBUG
    int32_t _ccmdMax; // 3DMMv1.0: running max
#endif                // 3DMMv1.0: DEBUG

    CEX(void);

    virtual bool _FInit(int32_t ccmdInit, int32_t ccmhInit);
    virtual bool _FFindCmhl(int32_t cmhl, int32_t *picmhe);

    virtual bool _FCmhOk(PCMH pcmh);
    virtual tribool _TGetNextCmd(void);
    virtual bool _FSendCmd(PCMH pcmh);
    virtual void _CleanUpCmd(void);
    virtual bool _FEnableCmd(PCMH pcmh, PCMD pcmd, uint32_t *pgrfeds);

    // 3DMMv1.0: command recording and playback
    bool _FReadCmd(PCMD pcmd);

  public:
    static PCEX PcexNew(int32_t ccmdInit, int32_t ccmhInit);
    ~CEX(void);

    // 3DMMv1.0: recording and play back
    bool FRecording(void)
    {
        return _rs == rsRecording;
    }
    bool FPlaying(void)
    {
        return _rs == rsPlaying;
    }
    void Record(PCFL pcfl);
    void Play(PCFL pcfl, CNO cno);
    void StopRecording(void);
    void StopPlaying(void);

    void RecordCmd(PCMD pcmd);

    // 3DMMv1.0: managing the filter list
    virtual bool FAddCmh(PCMH pcmh, int32_t cmhl, uint32_t grfcmm = fcmmNobody);
    virtual void RemoveCmh(PCMH pcmh, int32_t cmhl);
    virtual void BuryCmh(PCMH pcmh);

    // 3DMMv1.0: queueing and dispatching
    virtual void EnqueueCmd(PCMD pcmd);
    virtual void PushCmd(PCMD pcmd);
    virtual void EnqueueCid(int32_t cid, PCMH pcmh = pvNil, PGG pgg = pvNil, int32_t lw0 = 0, int32_t lw1 = 0,
                            int32_t lw2 = 0, int32_t lw3 = 0);
    virtual void PushCid(int32_t cid, PCMH pcmh = pvNil, PGG pgg = pvNil, int32_t lw0 = 0, int32_t lw1 = 0,
                         int32_t lw2 = 0, int32_t lw3 = 0);
    virtual bool FDispatchNextCmd(void);
    virtual bool FGetNextKey(PCMD pcmd);
    virtual bool FCidIn(int32_t cid);
    virtual void FlushCid(int32_t cid);

    // 3DMMv1.0: menu marking
    virtual uint32_t GrfedsForCmd(PCMD pcmd);
    virtual uint32_t GrfedsForCid(int32_t cid, PCMH pcmh = pvNil, PGG pgg = pvNil, int32_t lw0 = 0, int32_t lw1 = 0,
                                  int32_t lw2 = 0, int32_t lw3 = 0);

    // 3DMMv1.0: mouse tracking
    virtual void TrackMouse(PGOB pgob);
    virtual void EndMouseTracking(void);
    virtual PGOB PgobTracking(void);
    PGOB PgobModal(void)
    {
        return _pgobModal;
    }

    virtual void Suspend(bool fSuspend = fTrue);
    virtual void SetModalGob(PGOB pgob);
};

#endif //! 3DMMv1.0: CMD_H
