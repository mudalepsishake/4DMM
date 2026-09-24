/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    The base application class.  Most apps will need to subclass this.

***************************************************************************/
#ifndef APPB_H
#define APPB_H

/** 3DMMv1.0: *************************************************************************
    Misc types
***************************************************************************/
#ifdef KAUAI_WIN32
typedef MSG EVT;
#endif // 3DMMEx: KAUAI_WIN32

#ifdef KAUAI_SDL
typedef SDL_Event EVT;

// 3DMMEx: Logical size
const uint32_t kdxpLogical = 640;
const uint32_t kdypLogical = 480;
#endif // 3DMMEx: KAUAI_SDL

#ifdef MAC
typedef EventRecord EVT;
#endif // 3DMMv1.0: WIN
typedef EVT *PEVT;

// 3DMMEx: Window globals
struct WIG
{
    PCSZ pszCmdLine;
    KWND hwndApp;
    KWND hwndClient; // 3DMMv1.0: MDI client window

#ifdef WIN

    HINSTANCE hinst;
    HINSTANCE hinstPrev;

    int wShow;
    HDC hdcApp;
    HACCEL haccel;           // 3DMMv1.0: main accelerator table
    KWND hwndNextViewer;     // 3DMMv1.0: next clipboard viewer
    std::thread::id tidMain; // 3DMMv1.0: main thread

#endif // 3DMMv1.0: WIN
};
extern WIG vwig;

#ifdef WIN
/***************************************************************************
    4DMM native tool-window presentation helpers. The original Win32 tool
    windows were laid out while Windows desktop scaling was 200%. Keep their
    effective client/control geometry at that 200% design size independently
    from the main 4DMM -resolution presentation scale and the user's current
    desktop DPI.
***************************************************************************/
int32_t Lw4DMMExternalToolUi200(HWND hwnd, int32_t lw);
void Scale4DMMExternalToolWindow200(HWND hwnd);
#endif // 3DMMv1.0: WIN

/** 3DMMv1.0: *************************************************************************
    The base application class.
***************************************************************************/
const int32_t kcmhlAppb = klwMax; // 3DMMv1.0: appb goes at the end of the cmh list

enum
{
    fappNil = 0x0,
    fappOffscreen = 0x1,
    fappOnscreen = 0x2,
    fappStereoSound = 0x4,
};

typedef class APPB *PAPPB;
#define APPB_PAR CMH
#define kclsAPPB KLCONST4('A', 'P', 'P', 'B')
class APPB : public APPB_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(APPB)

  protected:
    // 3DMMv1.0: marked region - for fast updating
    struct MKRGN
    {
        KWND hwnd;
        PREGN pregn;
    };

    // 3DMMv1.0: map from a property id to its value
    struct PROP
    {
        int32_t prid;
        int32_t lw;
    };

    // 3DMMv1.0: modal context
    struct MODCX
    {
        int32_t cactLongOp;
        PCEX pcex;
        PUSAC pusac;
        uint32_t luScale;
    };

#ifdef DEBUG
    bool _fCheckForLostMem : 1; // 3DMMv1.0: whether to check for lost mem at idle
    bool _fInAssert : 1;        // 3DMMv1.0: whether we're in an assert
    bool _fRefresh : 1;         // 3DMMv1.0: whether to refresh the entire display
#endif                          // 3DMMv1.0: DEBUG
    bool _fQuit : 1;            // 3DMMv1.0: whether we're in the process of quitting
    bool _fOffscreen : 1;       // 3DMMv1.0: whether to do offscreen updates by default
    bool _fFullScreen : 1;      // 3DMMv1.0: when maximized, we hide the caption, etc
    bool _fToolTip : 1;         // 3DMMv1.0: whether we're in tool-tip mode
    bool _fForeground : 1;      // 3DMMv1.0: whether we're the foreground app
    bool _fEndModal : 1;        // 3DMMv1.0: set to end the topmost modal loop
    bool _fFlushCursor : 1;     // 3DMMEx: flush cursor events when setting cursor position

    PGL _pglmkrgn;           // 3DMMv1.0: list of marked regions for fast updating
    int32_t _onnDefFixed;    // 3DMMv1.0: default fixed pitch font
    int32_t _onnDefVariable; // 3DMMv1.0: default variable pitched font
    PGPT _pgptOff;           // 3DMMv1.0: cached offscreen GPT for offscreen updates
    int32_t _dxpOff;         // 3DMMv1.0: size of the offscreen GPT
    int32_t _dypOff;

    int32_t _xpMouse; // 3DMMv1.0: location of mouse on last reported mouse move
    int32_t _ypMouse;
    PGOB _pgobMouse;        // 3DMMv1.0: gob mouse was last over
    uint32_t _grfcustMouse; // 3DMMv1.0: cursor state on last mouse move

    // 3DMMv1.0: for determining the multiplicity of a click
    int32_t _tsMouse;   // 3DMMv1.0: time of last mouse click
    int32_t _cactMouse; // 3DMMv1.0: multiplicity of last mouse click

    // 3DMMv1.0: for tool tips
    uint32_t _tsMouseEnter;  // 3DMMv1.0: when the mouse entered _pgobMouse
    uint32_t _dtsToolTip;    // 3DMMv1.0: time lag for tool tip
    PGOB _pgobToolTipTarget; // 3DMMv1.0: if there is a tool tip up, it's for this gob

    PCURS _pcurs;        // 3DMMv1.0: current cursor
    PCURS _pcursWait;    // 3DMMv1.0: cursor to use for long operations
    int32_t _cactLongOp; // 3DMMv1.0: long operation count
    uint32_t _grfcust;   // 3DMMv1.0: current cursor state

    int32_t _gft;     // 3DMMv1.0: transition to apply during next fast update
    int32_t _lwGft;   // 3DMMv1.0: parameter for transition
    uint32_t _dtsGft; // 3DMMv1.0: how much time to give the transition
    PGL _pglclr;      // 3DMMv1.0: palette to transition to
    ACR _acr;         // 3DMMv1.0: intermediate color to transition to

    PGL _pglprop; // 3DMMv1.0: the properties

    PGL _pglmodcx;      // 3DMMv1.0: The modal context stack
    int32_t _lwModal;   // 3DMMv1.0: Return value from modal loop
    int32_t _cactModal; // 3DMMv1.0: how deep we are in application loops

    PSZ *_rgpszArgv = pvNil; // 3DMMEx: Command-line arguments
    int32_t _cpszArgv = 0;   // 3DMMEx: Number of command-line arguments

    // 3DMMv1.0: initialization, running and clean up
    virtual bool _FInit(uint32_t grfapp, uint32_t grfgob, int32_t ginDef);
#ifdef DEBUG
    virtual bool _FInitDebug(void);
#endif // 3DMMv1.0: DEBUG
    virtual bool _FInitOS(void);
    virtual bool _FInitMenu(void);
    virtual void _Loop(void);
    virtual void _CleanUp(void);
    virtual bool _FInitSound(int32_t wav);

    // 3DMMv1.0: event fetching and dispatching
    virtual bool _FGetNextEvt(PEVT pevt);
    virtual void _DispatchEvt(PEVT pevt);
    virtual bool _FTranslateKeyEvt(EVT *pevt, PCMD_KEY pcmd);

#ifdef MAC
    // 3DMMv1.0: event handlers
    virtual void _MouseDownEvt(EVT *pevt);
    virtual void _MouseUpEvt(EVT *pevt);
    virtual void _UpdateEvt(EVT *pevt);
    virtual void _ActivateEvt(EVT *pevt);
    virtual void _DiskEvt(EVT *pevt);
    virtual void _ActivateApp(EVT *pevt);
    virtual void _DeactivateApp(EVT *pevt);
    virtual void _MouseMovedEvt(EVT *pevt);
#endif

    // 3DMMv1.0: fast updating
    virtual void _FastUpdate(PGOB pgob, PREGN pregnClip, uint32_t grfapp = fappNil, PGPT pgpt = pvNil);
    virtual void _CopyPixels(PGNV pgvnSrc, RC *prcSrc, PGNV pgnvDst, RC *prcDst);
    void _MarkRegnRc(PREGN pregn, RC *prc, PGOB pgobCoo);
    void _UnmarkRegnRc(PREGN pregn, RC *prc, PGOB pgobCoo);

    // 3DMMv1.0: to borrow the common offscreen GPT
    virtual PGPT _PgptEnsure(RC *prc);

    // 3DMMv1.0: property list management
    bool _FFindProp(int32_t prid, PROP *pprop, int32_t *piprop = pvNil);
    bool _FSetProp(int32_t prid, int32_t lw);

    // 3DMMv1.0: tool tip support
    void _TakeDownToolTip(void);
    void _EnsureToolTip(void);

// 3DMMv1.0: window procs
#ifdef KAUAI_WIN32
    static LRESULT CALLBACK _LuWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK _LuMdiWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lParam);

    virtual bool _FFrameWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lw, int32_t *plwRet);
    virtual bool _FMdiWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lw, int32_t *plwRet);
    virtual bool _FCommonWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lw, int32_t *plwRet);

    // 3DMMv1.0: remove ourself from the clipboard viewer chain
    void _ShutDownViewer(void);
#endif // 3DMMEx: KAUAI_WIN32

    // 3DMMv1.0: Activation
    virtual void _Activate(bool fActive);

  public:
    APPB(void);
    ~APPB(void);

#ifdef MAC
    // 3DMMv1.0: setting up the heap
    static void _SetupHeap(int32_t cbExtraStack, int32_t cactMoreMasters);
    virtual void SetupHeap(void);
#else
    static void CreateConsole();
#endif

    bool FQuitting(void)
    {
        return _fQuit;
    }
    bool FForeground(void)
    {
        return _fForeground;
    }

    // 3DMMv1.0: initialization, running and quitting
    virtual void Run(uint32_t grfapp, uint32_t grfgob, int32_t ginDef);
    virtual void Quit(bool fForce);
    virtual void Abort(void);
    virtual void TopOfLoop(void);
    void SetArgv(PSZ *rgpszArgv, int32_t cpszArgv);
    bool FSetWindowIcon(const uint8_t *prgb, int32_t cb);

    // 3DMMv1.0: look for the next key event in the system event queue
    virtual bool FGetNextKeyFromOsQueue(PCMD_KEY pcmd);

    // 3DMMv1.0: Look for mouse events and get the mouse location
    // 3DMMv1.0: GrfcustCur() is synchronized with this
    void TrackMouse(PGOB pgob, PT *ppt);

    // 3DMMv1.0: app name
    virtual void GetStnAppName(PSTN pstn);

    // 3DMMv1.0: command handler stuff
    virtual void BuryCmh(PCMH pcmh);
    virtual PCMH PcmhFromHid(int32_t hid);

    // 3DMMv1.0: drawing
    virtual void UpdateHwnd(KWND hwnd, RC *prc, uint32_t grfapp = fappNil);
    virtual void MarkRc(RC *prc, PGOB pgobCoo);
    virtual void MarkRegn(PREGN pregn, PGOB pgobCoo);
    virtual void UnmarkRc(RC *prc, PGOB pgobCoo);
    virtual void UnmarkRegn(PREGN pregn, PGOB pgobCoo);
    virtual bool FGetMarkedRc(KWND hwnd, RC *prc);
    virtual void UpdateMarked(void);
    virtual void InvalMarked(KWND hwnd);
    virtual void SetGft(int32_t gft, int32_t lwGft, uint32_t dts = kdtsSecond, PGL pglclr = pvNil, ACR acr = kacrClear);

    // 3DMMv1.0: default fonts
    virtual int32_t OnnDefVariable(void);
    virtual int32_t OnnDefFixed(void);
    virtual int32_t DypTextDef(void);

    // 3DMMv1.0: basic alert handling
    virtual tribool TGiveAlertSz(const PCSZ psz, int32_t bk, int32_t cok);

    // 3DMMv1.0: common commands
    virtual bool FCmdQuit(PCMD pcmd);
    virtual bool FCmdIdle(PCMD pcmd);

#ifdef KAUAI_WIN32
    virtual bool FCmdShowClipboard(PCMD pcmd);
    virtual bool FEnableAppCmd(PCMD pcmd, uint32_t *pgrfeds);
    virtual bool FCmdChooseWnd(PCMD pcmd);
#endif // 3DMMEx: KAUAI_WIN32
#ifdef MAC
    virtual bool FCmdOpenDA(PCMD pcmd);
#endif // 3DMMv1.0: MAC

#ifdef DEBUG
    virtual bool FAssertProcApp(PSZS pszsFile, int32_t lwLine, PSZS pszsMsg, void *pv, int32_t cb);
    virtual void WarnProcApp(PSZS pszsFile, int32_t lwLine, PSZS pszsMsg);
#endif // 3DMMv1.0: DEBUG

    // 3DMMv1.0: cursor stuff
    virtual void SetCurs(PCURS pcurs, bool fLongOp = fFalse);
    virtual void SetCursCno(PRCA prca, CNO cno, bool fLongOp = fFalse);
    virtual void RefreshCurs(void);
    virtual uint32_t GrfcustCur(bool fAsynch = fFalse);
    virtual void ModifyGrfcust(uint32_t grfcustOr, uint32_t grfcustXor);
    virtual void HideCurs(void);
    virtual void ShowCurs(void);
    virtual void PositionCurs(int32_t xpScreen, int32_t ypScreen);
    virtual void BeginLongOp(void);
    virtual void EndLongOp(bool fAll = fFalse);

    // 3DMMv1.0: setting and fetching properties
    virtual bool FSetProp(int32_t prid, int32_t lw);
    virtual bool FGetProp(int32_t prid, int32_t *plw);

    // 3DMMv1.0: clipboard importing - normally only called by the clipboard object
    virtual bool FImportClip(int32_t clfm, void *pv = pvNil, int32_t cb = 0, PDOCB *ppdocb = pvNil,
                             bool *pfDelay = pvNil);

    // 3DMMv1.0: reset tooltip tracking.
    virtual void ResetToolTip(void);

    // 3DMMv1.0: modal loop support
    virtual bool FPushModal(PCEX pcex = pvNil);
    virtual bool FModalLoop(int32_t *plwRet);
    virtual void EndModal(int32_t lwRet);
    virtual void PopModal(void);
    virtual bool FCmdEndModal(PCMD pcmd);
    int32_t CactModal(void)
    {
        return _cactModal;
    }
    virtual void BadModalCmd(PCMD pcmd);

    // 3DMMv1.0: Query save changes for a document
    virtual tribool TQuerySaveDoc(PDOCB pdocb, bool fForce);

    // 3DMMv1.0: flush user generated events from the system event queue.
    virtual void FlushUserEvents(uint32_t grfevt = kgrfevtAll);

    // 3DMMv1.0: whether to allow a screen saver to come up
    virtual bool FAllowScreenSaver(void);

    virtual bool FIsMaximized();
    virtual bool FSetMaximized(bool fMaximized);

    // 3DMMEx: Translate a key code from the current platform to a Win32 virtual key
    static int32_t Win32VkFromVk(int32_t vk);
};

extern PAPPB vpappb;
extern PCEX vpcex;
extern PSNDM vpsndm;

// 3DMMv1.0: main entry point for the client app
void FrameMain(void);

// 3DMMv1.0: alert button kinds
enum
{
    bkOk,
    bkOkCancel,
    bkYesNo,
    bkYesNoCancel,
};

// 3DMMv1.0: alert icon kinds
enum
{
    cokNil,
    cokInformation, // 3DMMv1.0: general info to/from the user
    cokQuestion,    // 3DMMv1.0: ask the user something
    cokExclamation, // 3DMMv1.0: warn the user and/or ask something
    cokStop,        // 3DMMv1.0: inform the user that we can't do that
};

#ifdef KAUAI_SDL
// 3DMMEx: Enqueue a Kauai command using the SDL Event queue.
void SDLEnqueueCmd(PCMD pcmd);
#endif // 3DMMEx: KAUAI_SDL

#endif //! 3DMMv1.0: APPB_H
