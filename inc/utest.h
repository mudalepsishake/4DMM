/* 3DMMv1.0: Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/** 3DMMv1.0: *************************************************************************

    utest.h: Socrates main app class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

***************************************************************************/
#ifndef UTEST_H
#define UTEST_H

/** 3DMMv1.0: **************************************
    KidWorld for the App class
****************************************/
typedef class KWA *PKWA;
#define KWA_PAR WOKS
#define kclsKWA KLCONST3('K', 'W', 'A')
class KWA : public KWA_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PMBMP _pmbmp; // 3DMMv1.0: MBMP to draw in KWA (may be pvNil)
    bool _fAskForCD;

  public:
    KWA(GCB *pgcb) : WOKS(pgcb)
    {
        _fAskForCD = fTrue;
    }
    ~KWA(void);
    virtual void Draw(PGNV pgnv, RC *prcClip) override;
    virtual bool FFindFile(PSTN pstnSrc, PFNI pfni) override; // 3DMMv1.0: for finding AVIs
    virtual bool FModalTopic(PRCA prca, CNO cnoTopic, int32_t *plwRet) override;
    void SetMbmp(PMBMP pmbmp);
    void SetCDPrompt(bool fAskForCD)
    {
        _fAskForCD = fAskForCD;
    }
    bool FAskForCD(void)
    {
        return _fAskForCD;
    }
};

//
// 3DMMv1.0: If you change anything for the registry, notify SeanSe for setup changes.
//
#define kszSocratesKey PszLit("Software\\Microsoft\\Microsoft Kids\\3D Movie Maker")
#define kszWaveOutMsgValue PszLit("WaveOutMsg")
#define kszMidiOutMsgValue PszLit("MidiOutMsg")
#define kszGreaterThan8bppMsgValue PszLit("GreaterThan8bppMsg")
#define kszSwitchResolutionValue PszLit("SwitchResolution")
#define kszHomeDirValue PszLit("HomeDirectory")
#define kszInstallDirValue PszLit("InstallDirectory")
#define kszProductsKey PszLit("Software\\Microsoft\\Microsoft Kids\\3D Movie Maker\\Products")
#define kszUserDataValue PszLit("UserData")
#define kszBetterSpeedValue PszLit("BetterSpeed")

// 3DMMEx: If fTrue, play the startup sound and wait for it to finish.
#define kszStartupSoundValue PszLit("StartupSound")
#define kfStartupSoundDefault fTrue

// 3DMMEx: If fTrue, skip recompressing audio on import
#define kszHighQualitySoundImport PszLit("HighQualitySoundImport")
#define kszHighQualitySoundImportDefault fFalse

// 3DMMEx: If fTrue, mix sound in 44.1KHz 16-bit Stereo
#define kszStereoSound PszLit("StereoSound")
#define kszStereoSoundDefault fFalse

// 3DMMv1.0: FGetSetRegKey flags
enum
{
    fregNil = 0,
    fregSetKey = 0x01,
    fregSetDefault = 0x02,
    fregString = 0x04,
    fregBinary = 0x08, // 3DMMv1.0: not boolean
    fregMachine = 0x10
};

/** 3DMMEx: ****************************************************************************
    FGetSetRegKey
        Given a reg key (and option sub-key), attempts to either get or set
        the current value of the key.  If the reg key is created on a Get
        (fSetKey == fFalse), the data in pvData will be used to set the
        default value for the key.


    Arguments:
        PSZ pszValueName  --  The value name
        void *pvData      --  pointer to buffer to read or write key into or from
        long cbData       --  size of the buffer
        uint32_t grfreg   --  flags describing what we should do
        bool *pfNoValue  --  optional parameter, takes whether a real registry
                              error occurred or not

    Returns:  fTrue if all actions necessary could be performed

************************************************************ PETED ***********/
bool FGetSetRegKey(PCSZ pszValueName, void *pvData, int32_t cbData, uint32_t grfreg, bool *pfNoValue);

/** 3DMMv1.0: **************************************
    The app class
****************************************/
typedef class APP *PAPP;
#define APP_PAR APPB
#define kclsAPP KLCONST3('A', 'P', 'P')
class APP : public APP_PAR
{
    RTCLASS_DEC
    CMD_MAP_DEC(APP)
    ASSERT
    MARKMEM

  protected:
    bool _fDontReportInitFailure; // 3DMMv1.0: init failure was already reported
    bool _fOnscreenDrawing;
    PCFL _pcfl;                   // 3DMMv1.0: resource file for app
    PSTDIO _pstdio;               // 3DMMv1.0: Current studio
    PTATR _ptatr;                 // 3DMMv1.0: Current theater
    PCRM _pcrmAll;                // 3DMMv1.0: The app CRM -- all crfs are loaded into this.
    PGL _pglicrfBuilding;         // 3DMMv1.0: List of crfs in _pcrmAll belonging to Building.
    PGL _pglicrfStudio;           // 3DMMv1.0: List of crfs in _pcrmAll belonging to Studio.
    bool _fDontMinimize : 1,      // 3DMMv1.0: "/M" command-line switch
        _fSlowCPU : 1,            // 3DMMv1.0: running on slow CPU
        _fSwitchedResolution : 1, // 3DMMv1.0: we successfully switched to 640x480 mode
        _fMainWindowCreated : 1, _fMinimized : 1,
        _fRunInWindow : 1,    // 3DMMv1.0: run in a window (as opposed to fullscreen)
        _fForceWindow : 1,    // command line requested windowed startup
        _fViewportWindow : 1, // command line requested a 544x306 viewport mirror window
        _fPlaybackOnly : 1,         // command line requested viewport-only playback runner
        _fPlaybackAutoPlay : 1,     // -o movie has loaded and should start playing
        _fPlaybackEditorHidden : 1, // -o editor shell has been suppressed offscreen
        _fUndoHistory : 1,       // -u enables 50-level undo and the external history window
        _fExternalBrowsers : 1,   // -browser enables external actor/prop/3D-word browsers
        _fEditorOnly : 1,        // bypass the building and open directly into the editor
        _fFontError : 1,   // 3DMMv1.0: Have we already seen a font error?
        _fInPortfolio : 1; // 3DMMv1.0: Is the portfolio active?
    PCEX _pcex;            // 3DMMv1.0: Pointer to suspended cex.
    FNI _fniPortfolioDoc;  // 3DMMv1.0: document last opened in portfolio
    PMVIE _pmvieHandoff;   // 3DMMv1.0: Stores movie for studio to use
    PKWA _pkwa;            // 3DMMv1.0: Kidworld for App
    PGST _pgstBuildingFiles;
    PGST _pgstStudioFiles;
    PGST _pgstSharedFiles;
    PGST _pgstApp;        // 3DMMv1.0: Misc. app global strings
    STN _stnAppName;      // 3DMMv1.0: App name
    STN _stnProductLong;  // 3DMMv1.0: Long version of product name
    STN _stnProductShort; // 3DMMv1.0: Short version of product name
    STN _stnUser;         // 3DMMv1.0: User's name
    int32_t _sidProduct;
    FNI _fniCurrentDir;  // 3DMMEx: fni of current working directory
    FNI _fniExe;         // 3DMMv1.0: fni of this executable file
    FNI _fniMsKidsDir;   // 3DMMv1.0: e.g., \mskids
    FNI _fniUsersDir;    // 3DMMv1.0: e.g., \mskids\users
    FNI _fniMelanieDir;  // 3DMMv1.0: e.g., \mskids\users\melanie
    FNI _fniProductDir;  // 3DMMv1.0: e.g., \mskids\3dmovie or \mskids\otherproduct
    FNI _fniUserDir;     // 3DMMv1.0: User's preferred directory
    FNI _fni3DMovieDir;  // 3DMMv1.0: e.g., \mskids\3dMovie
    int32_t _dypTextDef; // 3DMMv1.0: Default text height

    int32_t _cactDisable; // 3DMMv1.0: disable count for keyboard accelerators
#ifdef BUG1085
    int32_t _cactCursHide; // 3DMMv1.0: hide count for cursor
    int32_t _cactCursSav;  // 3DMMv1.0: saved count for cursor
#endif

    //
    //
    //
    bool _fDown;
    int32_t _cactToggle;

    PATBL _patblMain;   // 3DMMEx: Main accelerator table
    PATBL _patblGlobal; // 3DMMEx: Global accelerator table (always active)
#ifdef KAUAI_WIN32
    HWND _hwndViewport;   // optional 544x306 viewport mirror window
    PGPT _pgptViewport;   // cached true movie viewport pixels for repaint/screenshot
#endif                    // 3DMMEx: KAUAI_WIN32

  protected:
    bool _FAppAlreadyRunning(void);
    void _TryToActivateWindow(void);
    bool _FEnsureOS(void);
    bool _FEnsureAudio(void);
    bool _FEnsureVideo(void);
    bool _FEnsureColorDepth(void);
    bool _FEnsureDisplayResolution(void);
    bool _FDisplaySwitchSupported(void);
    void _ParseCommandLine(void);
    void _SkipToSpace(PCSZ *ppch);
    void _SkipSpace(PCSZ *ppch);
    bool _FEnsureProductNames(void);
    bool _FFindProductDir(PGST pgst);
    bool _FQueryProductExists(STN *pstnLong, STN *pstnShort, FNI *pfni);
    bool _FFindMsKidsDir(void);
    bool _FFindMsKidsDirAt(FNI *path);
    bool _FCantFindFileDialog(PSTN pstn);
    bool _FGenericError(PCSZ message);
    bool _FGenericError(PSTN message);
    bool _FGenericError(FNI *path);
    bool _FGetUserName(void);
    bool _FGetUserDirectories(void);
    bool _FReadUserData(void);
    bool _FWriteUserData(void);
    bool _FDisplayHomeLogo(void);
    bool _FSetInitialPalette(void);
    bool _FDetermineIfSlowCPU(void);
    bool _FOpenResourceFile(void);
    bool _FInitKidworld(void);
    bool _FInitProductNames(void);
    bool _FReadTitlesFromReg(PGST *ppgst);
    bool _FInitTdt(void);
    PGST _PgstRead(CNO cno);
    bool _FReadStringTables(void);
    bool _FSetWindowTitle(void);
    bool _FInitCrm(void);
    bool _FAddToCrm(PGST pgstFiles, PCRM pcrm, PGL pglFiles);
    bool _FInitBuilding(void);
    bool _FInitStudio(PFNI pfniUserDoc, bool fFailIfDocOpenFailed = fTrue);
    bool _FInitAcceleratorTable(void);
    void _GetWindowProps(int32_t *pxp, int32_t *pyp, int32_t *pdxp, int32_t *pdyp, uint32_t *pdwStyle);
    void _RebuildMainWindow(void);
    bool _FSwitch640480(bool fTo640480);
    bool _FDisplayIs640480(void);
    bool _FSetRunInWindow(bool fRunInWindow);
    bool _FSaveScreenshot(void);
    void _QueuePlaybackOnlyPlay(void);
    void _HidePlaybackOnlyEditorWindow(void);
#ifdef KAUAI_WIN32
    bool _FEnsureViewportWindow(void);
    void _CloseViewportWindow(void);
    void _PaintViewportWindow(void);
    bool _FEnsureViewportCache(void);
    friend void AppMirrorViewportFromGnv(PGNV pgnvSrc, RC *prcSrc, RC *prcDst);
    void MirrorViewportFromGnv(PGNV pgnvSrc, RC *prcSrc, RC *prcDst);
#endif // 3DMMEx: KAUAI_WIN32
    bool _FShowSplashScreen(void);
    bool _FPlaySplashSound(void);
    PMVIE _Pmvie(void);
    void _CleanupTemp(void);
#ifdef WIN
    bool _FSendOpenDocCmd(HWND hwnd, PFNI pfniUserDoc);
    bool _FProcessOpenDocCmd(void);
#endif // 3DMMv1.0: WIN

    // 3DMMv1.0: APPB methods that we override
    virtual PGPT _PgptEnsure(RC *prc) override;
    virtual bool _FInit(uint32_t grfapp, uint32_t grfgob, int32_t ginDef) override;
    virtual bool _FInitOS(void) override;
    virtual bool _FInitMenu(void) override
    {
        return fTrue;
    } // 3DMMv1.0: no menubar
    virtual void _CopyPixels(PGNV pgvnSrc, RC *prcSrc, PGNV pgnvDst, RC *prcDst) override;
    virtual void _FastUpdate(PGOB pgob, PREGN pregnClip, uint32_t grfapp = fappNil, PGPT pgpt = pvNil) override;
    virtual void _CleanUp(void) override;
    virtual void _Activate(bool fActive) override;
#ifdef KAUAI_WIN32
    virtual bool _FGetNextEvt(PEVT pevt) override;
    virtual bool _FFrameWndProc(HWND hwnd, UINT wm, WPARAM wParam, LPARAM lw, int32_t *plwRet) override;
#endif // 3DMMEx: KAUAI_WIN32

  public:
    APP(void)
    {
        _dypTextDef = 0;
        _fRunInWindow = fFalse;
        _fForceWindow = fFalse;
        _fViewportWindow = fFalse;
        _fPlaybackOnly = fFalse;
        _fPlaybackAutoPlay = fFalse;
        _fPlaybackEditorHidden = fFalse;
        _fUndoHistory = fFalse;
        _fExternalBrowsers = fFalse;
        _fEditorOnly = fFalse;
#ifdef KAUAI_WIN32
        _hwndViewport = hNil;
        _pgptViewport = pvNil;
#endif // 3DMMEx: KAUAI_WIN32
    }

    // 3DMMv1.0: Overridden APPB functions
    virtual void GetStnAppName(PSTN pstn) override;
    virtual int32_t OnnDefVariable(void) override;
    virtual int32_t DypTextDef(void) override;
    virtual tribool TQuerySaveDoc(PDOCB pdocb, bool fForce) override;
    virtual void Quit(bool fForce) override;
#ifdef WIN
    virtual void UpdateHwnd(KWND hwnd, RC *prc, uint32_t grfapp = fappNil) override;
#endif // 3DMMv1.0: WIN
    virtual void Run(uint32_t grfapp, uint32_t grfgob, int32_t ginDef) override;
#ifdef BUG1085
    virtual void HideCurs(void) override;
    virtual void ShowCurs(void) override;

    // 3DMMv1.0: New cursor methods
    void PushCurs(void);
    void PopCurs(void);
#endif // 3DMMv1.0: BUG 1085

    // 3DMMv1.0: Command processors
    bool FCmdLoadStudio(PCMD pcmd);
    bool FCmdLoadBuilding(PCMD pcmd);
    bool FCmdTheaterOpen(PCMD pcmd);
    bool FCmdTheaterClose(PCMD pcmd);
    virtual bool FCmdIdle(PCMD pcmd) override;
    bool FCmdInfo(PCMD pcmd);
    bool FCmdPortfolioClear(PCMD pcmd);
    bool FCmdPortfolioOpen(PCMD pcmd);
    bool FCmdDisableAccel(PCMD pcmd);
    bool FCmdEnableAccel(PCMD pcmd);
    bool FCmdInvokeSplot(PCMD pcmd);
    bool FCmdExitStudio(PCMD pcmd);
    bool FCmdDeactivate(PCMD pcmd);
    bool FCmdToggleFullscreen(PCMD pcmd);
    bool FCmd4DMMSceneLights(PCMD pcmd);
    bool FCmd4DMMHideLightObjects(PCMD pcmd);
    bool FCmd4DMMSettings(PCMD pcmd);
    bool FCmd4DMMObjectGroups(PCMD pcmd);
    bool FCmd4DMMActorStudio(PCMD pcmd);
    bool FCmd4DMMObj2Vxp2(PCMD pcmd);

    static bool FInsertCD(PSTN pstnTitle);
    void DisplayErrors(void);
    void SetPortfolioDoc(PFNI pfni)
    {
        _fniPortfolioDoc = *pfni;
    }
    void GetPortfolioDoc(PFNI pfni)
    {
        *pfni = _fniPortfolioDoc;
    }
    bool FResolveVmmMovie(PFNI pfniVmm, PFNI pfniMovie);
    void SetFInPortfolio(bool fInPortfolio)
    {
        _fInPortfolio = fInPortfolio;
    }
    bool FInPortfolio(void)
    {
        return _fInPortfolio;
    }

    PSTDIO Pstdio(void)
    {
        return _pstdio;
    }
    PKWA Pkwa(void)
    {
        return _pkwa;
    }
    PCRM PcrmAll(void)
    {
        return _pcrmAll;
    }
    bool FMinimized()
    {
        return _fMinimized;
    }

    bool FGetStnApp(int32_t ids, PSTN pstn)
    {
        return _pgstApp->FFindExtra(&ids, pstn);
    }
    void GetStnProduct(PSTN pstn)
    {
        *pstn = _stnProductLong;
    }
    void GetStnUser(PSTN pstn)
    {
        *pstn = _stnUser;
    }
    void GetFniExe(PFNI pfni)
    {
        *pfni = _fniExe;
    }
    void GetFniProduct(PFNI pfni)
    {
        *pfni = _fniProductDir;
    }
    void GetFniUsers(PFNI pfni)
    {
        *pfni = _fniUsersDir;
    }
    void GetFniUser(PFNI pfni)
    {
        *pfni = _fniUserDir;
    }
    void GetFniMelanie(PFNI pfni)
    {
        *pfni = _fniMelanieDir;
    }
    int32_t SidProduct(void)
    {
        return _sidProduct;
    }
    bool FGetOnn(PSTN pstn, int32_t *ponn);
    void MemStat(int32_t *pdwTotalPhys, int32_t *pdwAvailPhys = pvNil);
    bool FSlowCPU(void)
    {
        return _fSlowCPU;
    }
    bool FUndoHistory(void)
    {
        return _fUndoHistory && !_fPlaybackOnly;
    }

    // 3DMMv1.0: Kid-friendly modal dialog stuff:
    void EnsureInteractive(void)
    {
#ifdef WIN
        if (_fMinimized || GetForegroundWindow() != vwig.hwndApp)
        {
            SetForegroundWindow(vwig.hwndApp);
            ShowWindow(vwig.hwndApp, SW_RESTORE);
        }
#endif // 3DMMv1.0: WIN
    }
    tribool TModal(PRCA prca, int32_t tpc, PSTN pstnBackup = pvNil, int32_t bkBackup = ivNil, int32_t stidSubst = ivNil,
                   PSTN pstnSubst = pvNil);

    // 3DMMv1.0: Enable/disable accelerator keys
    void DisableAccel(void);
    void EnableAccel(void);

    // 3DMMv1.0: Registry access function
    bool FGetSetRegKey(PCSZ pszValueName, void *pvData, int32_t cbData, uint32_t grfreg = fregSetDefault,
                       bool *pfNoValue = pvNil);

    // 3DMMv1.0: Movie handoff routines
    void HandoffMovie(PMVIE pmvie);
    PMVIE PmvieRetrieve(void);

    // 3DMMv1.0: Determines whether screen savers should be blocked.
    virtual bool FAllowScreenSaver(void) override;
};

#if defined(KAUAI_WIN32) && defined(BRENDER_MODERN_14)
// Explicit compositor hook for Kauai popup/font menus that cross the native
// 3D Word/Costume/Action preview. Popup roots know their real rectangle; the
// generic paint-owner heuristic does not reliably discover them.
void Show4DMMUiScaleApePopupOverlay(PGOB pgobPopup);
void Hide4DMMUiScaleApePopupOverlay(PGOB pgobPopup);
#endif

#define vpapp ((APP *)vpappb)

#endif //! 3DMMv1.0: UTEST_H
